#define _POSIX_C_SOURCE 200809L
#include "queue.h"

void queue_init(struct queue *q)
{
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->open = 0;
    q->dropped = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

void queue_open(struct queue *q)
{
    pthread_mutex_lock(&q->lock);
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->dropped = 0;
    q->open = 1;
    pthread_mutex_unlock(&q->lock);
}

void queue_close(struct queue *q)
{
    pthread_mutex_lock(&q->lock);
    q->open = 0;
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

int queue_push(struct queue *q, struct frame *f)
{
    pthread_mutex_lock(&q->lock);

    if (!q->open) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    if (q->count == QUEUE_SIZE) {
        q->dropped++;
        pthread_mutex_unlock(&q->lock);
        return -1;
    }

    q->item[q->tail] = f;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

struct frame *queue_pop(struct queue *q)
{
    struct frame *f;

    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && q->open)
        pthread_cond_wait(&q->not_empty, &q->lock);

    if (!q->open) {
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }

    f = q->item[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    pthread_mutex_unlock(&q->lock);
    return f;
}

void queue_drain(struct queue *q)
{
    pthread_mutex_lock(&q->lock);
    while (q->count > 0) {
        frame_release(q->item[q->head]);
        q->head = (q->head + 1) % QUEUE_SIZE;
        q->count--;
    }
    pthread_mutex_unlock(&q->lock);
}
