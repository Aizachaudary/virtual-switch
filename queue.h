#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include "frame.h"

#define QUEUE_SIZE 32

struct queue {
    struct frame *item[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    int open;
    unsigned long dropped;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
};

void queue_init(struct queue *q);
void queue_open(struct queue *q);
void queue_close(struct queue *q);
int queue_push(struct queue *q, struct frame *f);
struct frame *queue_pop(struct queue *q);
void queue_drain(struct queue *q);

#endif
