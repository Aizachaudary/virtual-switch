#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mactable.h"

static long long now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

static int hash(unsigned char *mac)
{
    unsigned int h = 0;
    int i;

    for (i = 0; i < 6; i++)
        h = h * 31 + mac[i];
    return h % BUCKETS;
}

static struct mac_entry *find(struct mac_table *t, int b, unsigned char *mac)
{
    struct mac_entry *e = t->bucket[b];

    while (e != NULL) {
        if (memcmp(e->mac, mac, 6) == 0)
            return e;
        e = e->next;
    }
    return NULL;
}

static void count_down(struct mac_table *t, int port)
{
    pthread_mutex_lock(&t->meta);
    t->total--;
    t->port_count[port]--;
    pthread_mutex_unlock(&t->meta);
}

static void evict_oldest(struct mac_table *t)
{
    struct mac_entry *e;
    struct mac_entry **pp;
    unsigned char best_mac[6];
    long long best = 0;
    int best_b = -1;
    int b;

    for (b = 0; b < BUCKETS; b++) {
        pthread_mutex_lock(&t->lock[b]);
        for (e = t->bucket[b]; e != NULL; e = e->next) {
            if (best_b == -1 || e->seen < best) {
                best = e->seen;
                best_b = b;
                memcpy(best_mac, e->mac, 6);
            }
        }
        pthread_mutex_unlock(&t->lock[b]);
    }

    if (best_b == -1)
        return;

    pthread_mutex_lock(&t->lock[best_b]);
    pp = &t->bucket[best_b];
    while (*pp != NULL) {
        e = *pp;
        if (memcmp(e->mac, best_mac, 6) == 0) {
            *pp = e->next;
            count_down(t, e->port);
            free(e);
            break;
        }
        pp = &e->next;
    }
    pthread_mutex_unlock(&t->lock[best_b]);
}

void mac_init(struct mac_table *t, int max_entries, int max_per_port, int age_seconds)
{
    int i;

    for (i = 0; i < BUCKETS; i++) {
        t->bucket[i] = NULL;
        pthread_mutex_init(&t->lock[i], NULL);
    }
    pthread_mutex_init(&t->meta, NULL);
    t->total = 0;
    for (i = 0; i < MAX_PORTS; i++)
        t->port_count[i] = 0;
    t->max_entries = max_entries;
    t->max_per_port = max_per_port;
    t->age_ms = age_seconds * 1000LL;
}

int mac_learn(struct mac_table *t, unsigned char *mac, int port)
{
    struct mac_entry *e;
    int b = hash(mac);
    int moved = 0;
    int need_evict = 0;

    pthread_mutex_lock(&t->lock[b]);
    e = find(t, b, mac);

    if (e != NULL) {
        if (e->port != port) {
            pthread_mutex_lock(&t->meta);
            if (t->port_count[port] >= t->max_per_port) {
                pthread_mutex_unlock(&t->meta);
                pthread_mutex_unlock(&t->lock[b]);
                return MAC_TOO_MANY;
            }
            t->port_count[e->port]--;
            t->port_count[port]++;
            pthread_mutex_unlock(&t->meta);
            e->port = port;
            moved = 1;
        }
        e->seen = now_ms();
        pthread_mutex_unlock(&t->lock[b]);
        if (moved)
            return MAC_MOVED;
        return MAC_OK;
    }

    e = malloc(sizeof(struct mac_entry));
    if (e == NULL) {
        pthread_mutex_unlock(&t->lock[b]);
        return MAC_OK;
    }

    pthread_mutex_lock(&t->meta);
    if (t->port_count[port] >= t->max_per_port) {
        pthread_mutex_unlock(&t->meta);
        pthread_mutex_unlock(&t->lock[b]);
        free(e);
        return MAC_TOO_MANY;
    }
    t->port_count[port]++;
    t->total++;
    if (t->total > t->max_entries)
        need_evict = 1;
    pthread_mutex_unlock(&t->meta);

    memcpy(e->mac, mac, 6);
    e->port = port;
    e->seen = now_ms();
    e->next = t->bucket[b];
    t->bucket[b] = e;
    pthread_mutex_unlock(&t->lock[b]);

    if (need_evict)
        evict_oldest(t);
    return MAC_OK;
}

int mac_lookup(struct mac_table *t, unsigned char *mac)
{
    struct mac_entry *e;
    int b = hash(mac);
    int port = -1;

    pthread_mutex_lock(&t->lock[b]);
    e = find(t, b, mac);
    if (e != NULL)
        port = e->port;
    pthread_mutex_unlock(&t->lock[b]);
    return port;
}

void mac_remove_port(struct mac_table *t, int port)
{
    struct mac_entry *e;
    struct mac_entry **pp;
    int b;

    for (b = 0; b < BUCKETS; b++) {
        pthread_mutex_lock(&t->lock[b]);
        pp = &t->bucket[b];
        while (*pp != NULL) {
            e = *pp;
            if (e->port == port) {
                *pp = e->next;
                count_down(t, port);
                free(e);
            } else {
                pp = &e->next;
            }
        }
        pthread_mutex_unlock(&t->lock[b]);
    }
}

void mac_age(struct mac_table *t)
{
    struct mac_entry *e;
    struct mac_entry **pp;
    long long now = now_ms();
    int b;

    for (b = 0; b < BUCKETS; b++) {
        pthread_mutex_lock(&t->lock[b]);
        pp = &t->bucket[b];
        while (*pp != NULL) {
            e = *pp;
            if (now - e->seen > t->age_ms) {
                *pp = e->next;
                count_down(t, e->port);
                free(e);
            } else {
                pp = &e->next;
            }
        }
        pthread_mutex_unlock(&t->lock[b]);
    }
}

void *aging_thread(void *arg)
{
    struct mac_table *t = arg;

    for (;;) {
        sleep(1);
        mac_age(t);
    }
    return NULL;
}
