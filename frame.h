#ifndef FRAME_H
#define FRAME_H

#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>

#define HDR_LEN 17
#define MAX_PAYLOAD 1500

struct frame {
    int refs;
    pthread_mutex_t lock;
    uint16_t len;
    unsigned char src[6];
    unsigned char dst[6];
    uint16_t vlan;
    unsigned char prio;
    unsigned char data[MAX_PAYLOAD];
};

ssize_t read_full(int fd, void *buf, size_t n);
ssize_t write_full(int fd, const void *buf, size_t n);
int read_frame(int fd, struct frame *f);
int write_frame(int fd, struct frame *f);
int is_broadcast(unsigned char *mac);

struct frame *frame_new(void);
void frame_hold(struct frame *f);
void frame_release(struct frame *f);

#endif
