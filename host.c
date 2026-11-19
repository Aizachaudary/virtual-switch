#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "frame.h"

int parse_mac(char *s, unsigned char *m)
{
    return sscanf(s, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
}

void *receiver(void *arg)
{
    int fd = *(int *)arg;
    struct frame *f = frame_new();

    if (f == NULL)
        return NULL;
    while (read_frame(fd, f) == 1) {
        printf("from %02x:%02x:%02x:%02x:%02x:%02x %.*s\n",
               f->src[0], f->src[1], f->src[2], f->src[3], f->src[4], f->src[5],
               f->len, f->data);
    }
    printf("disconnected\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    struct sockaddr_in addr;
    struct frame f;
    pthread_t tid;
    unsigned char mymac[6], dst[6];
    char line[2000];
    char *space, *text;
    int fd, len;

    if (argc != 4 && argc != 5) {
        printf("usage ./host ip tcp_port mac\n");
        return 1;
    }
    if (parse_mac(argv[3], mymac) != 6) {
        printf("bad mac\n");
        return 1;
    }

    setbuf(stdout, NULL);
    signal(SIGPIPE, SIG_IGN);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return 1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1) {
        printf("bad ip\n");
        return 1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        return 1;
    }

    if (argc == 4) {
        if (pthread_create(&tid, NULL, receiver, &fd) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    while (fgets(line, sizeof(line), stdin) != NULL) {
        line[strcspn(line, "\n")] = 0;
        space = strchr(line, ' ');
        if (space == NULL) {
            printf("need dst and text\n");
            continue;
        }
        *space = 0;
        text = space + 1;

        if (strcmp(line, "all") == 0)
            memset(dst, 0xff, 6);
        else if (parse_mac(line, dst) != 6) {
            printf("bad dst\n");
            continue;
        }

        len = strlen(text);
        if (len > MAX_PAYLOAD)
            len = MAX_PAYLOAD;
        memset(&f, 0, sizeof(f));
        f.len = len;
        memcpy(f.src, mymac, 6);
        memcpy(f.dst, dst, 6);
        f.vlan = 1;
        f.prio = 1;
        memcpy(f.data, text, len);

        if (write_frame(fd, &f) < 0) {
            printf("send failed\n");
            break;
        }
    }

    close(fd);
    return 0;
}
