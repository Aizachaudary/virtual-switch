#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "frame.h"
#include "queue.h"
#include "mactable.h"

#define MAX_MACS 100
#define MACS_PER_PORT 8
#define AGE_TIME 30
#define WAIT_TIME 5

struct port {
    int used;
    int fd;
    struct queue q;
};

struct port *ports;
int num_ports;
struct mac_table table;
sem_t free_ports;
pthread_mutex_t ports_lock = PTHREAD_MUTEX_INITIALIZER;

void send_full(int fd)
{
    struct frame f;
    char *text = "switch full";

    memset(&f, 0, sizeof(f));
    f.len = strlen(text);
    memset(f.dst, 0xff, 6);
    f.vlan = 1;
    memcpy(f.data, text, f.len);
    write_frame(fd, &f);
}

void send_to(int i, struct frame *f)
{
    frame_hold(f);
    if (queue_push(&ports[i].q, f) < 0)
        frame_release(f);
}

void forward(struct frame *f, int from)
{
    int out = -1;
    int i;

    if (!is_broadcast(f->dst))
        out = mac_lookup(&table, f->dst);

    if (out >= 0) {
        if (out != from)
            send_to(out, f);
        return;
    }

    for (i = 0; i < num_ports; i++) {
        if (i != from)
            send_to(i, f);
    }
}

void *sender(void *arg)
{
    struct port *p = arg;
    struct frame *f;
    int failed = 0;

    while ((f = queue_pop(&p->q)) != NULL) {
        if (!failed && write_frame(p->fd, f) < 0) {
            failed = 1;
            shutdown(p->fd, SHUT_RDWR);
        }
        frame_release(f);
    }
    return NULL;
}

void *host_thread(void *arg)
{
    int fd = *(int *)arg;
    int me = -1;
    int i, r, res;
    struct timespec t;
    struct frame *f;
    pthread_t tx;

    free(arg);

    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += WAIT_TIME;
    while (sem_timedwait(&free_ports, &t) == -1) {
        if (errno == EINTR)
            continue;
        if (errno == ETIMEDOUT) {
            printf("switch full\n");
            send_full(fd);
        } else {
            perror("sem_timedwait");
        }
        close(fd);
        return NULL;
    }

    pthread_mutex_lock(&ports_lock);
    for (i = 0; i < num_ports; i++) {
        if (!ports[i].used) {
            me = i;
            break;
        }
    }
    ports[me].used = 1;
    ports[me].fd = fd;
    pthread_mutex_unlock(&ports_lock);

    queue_open(&ports[me].q);
    if (pthread_create(&tx, NULL, sender, &ports[me]) != 0) {
        perror("pthread_create");
        queue_close(&ports[me].q);
        close(fd);
        pthread_mutex_lock(&ports_lock);
        ports[me].used = 0;
        pthread_mutex_unlock(&ports_lock);
        sem_post(&free_ports);
        return NULL;
    }
    printf("host on port %d\n", me + 1);

    for (;;) {
        f = frame_new();
        if (f == NULL) {
            perror("malloc");
            break;
        }
        r = read_frame(fd, f);
        if (r <= 0) {
            frame_release(f);
            break;
        }

        res = mac_learn(&table, f->src, me);
        if (res == MAC_TOO_MANY) {
            printf("too many macs on port %d\n", me + 1);
            frame_release(f);
            break;
        }
        if (res == MAC_MOVED)
            printf("mac moved to port %d\n", me + 1);

        forward(f, me);
        frame_release(f);
    }

    queue_close(&ports[me].q);
    shutdown(fd, SHUT_RDWR);
    pthread_join(tx, NULL);
    queue_drain(&ports[me].q);
    mac_remove_port(&table, me);
    close(fd);

    if (ports[me].q.dropped > 0)
        printf("port %d dropped %lu frames\n", me + 1, ports[me].q.dropped);
    printf("port %d closed\n", me + 1);

    pthread_mutex_lock(&ports_lock);
    ports[me].used = 0;
    pthread_mutex_unlock(&ports_lock);
    if (sem_post(&free_ports) == -1)
        perror("sem_post");
    return NULL;
}

int main(int argc, char *argv[])
{
    struct sockaddr_in addr;
    pthread_t tid;
    int tcp_port, s, c, i, yes = 1;
    int *arg;

    if (argc != 3) {
        printf("usage ./switch tcp_port num_ports\n");
        return 1;
    }
    tcp_port = atoi(argv[1]);
    num_ports = atoi(argv[2]);
    if (tcp_port <= 0 || num_ports <= 0 || num_ports > MAX_PORTS) {
        printf("bad arguments\n");
        return 1;
    }

    setbuf(stdout, NULL);
    signal(SIGPIPE, SIG_IGN);

    ports = calloc(num_ports, sizeof(struct port));
    if (ports == NULL) {
        perror("calloc");
        return 1;
    }
    for (i = 0; i < num_ports; i++)
        queue_init(&ports[i].q);

    if (sem_init(&free_ports, 0, num_ports) == -1) {
        perror("sem_init");
        return 1;
    }
    mac_init(&table, MAX_MACS, MACS_PER_PORT, AGE_TIME);

    if (pthread_create(&tid, NULL, aging_thread, &table) != 0) {
        perror("pthread_create");
        return 1;
    }
    pthread_detach(tid);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        perror("socket");
        return 1;
    }
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(tcp_port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        return 1;
    }
    if (listen(s, 128) == -1) {
        perror("listen");
        return 1;
    }
    printf("switch up with %d ports\n", num_ports);

    for (;;) {
        c = accept(s, NULL, NULL);
        if (c == -1) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }
        arg = malloc(sizeof(int));
        if (arg == NULL) {
            close(c);
            continue;
        }
        *arg = c;
        if (pthread_create(&tid, NULL, host_thread, arg) != 0) {
            perror("pthread_create");
            free(arg);
            close(c);
            continue;
        }
        pthread_detach(tid);
    }

    close(s);
    return 0;
}
