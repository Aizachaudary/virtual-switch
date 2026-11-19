#ifndef MACTABLE_H
#define MACTABLE_H

#include <pthread.h>

#define BUCKETS 16
#define MAX_PORTS 64

#define MAC_OK 0
#define MAC_MOVED 1
#define MAC_TOO_MANY 2

struct mac_entry {
    unsigned char mac[6];
    int port;
    long long seen;
    struct mac_entry *next;
};

struct mac_table {
    struct mac_entry *bucket[BUCKETS];
    pthread_mutex_t lock[BUCKETS];
    pthread_mutex_t meta;
    int total;
    int port_count[MAX_PORTS];
    int max_entries;
    int max_per_port;
    long long age_ms;
};

void mac_init(struct mac_table *t, int max_entries, int max_per_port, int age_seconds);
int mac_learn(struct mac_table *t, unsigned char *mac, int port);
int mac_lookup(struct mac_table *t, unsigned char *mac);
void mac_remove_port(struct mac_table *t, int port);
void mac_age(struct mac_table *t);
void *aging_thread(void *arg);

#endif
