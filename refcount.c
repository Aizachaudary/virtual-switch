#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include "frame.h"

struct frame *frame_new(void)
{
    struct frame *f = malloc(sizeof(struct frame));

    if (f == NULL)
        return NULL;
    f->refs = 1;
    pthread_mutex_init(&f->lock, NULL);
    return f;
}

void frame_hold(struct frame *f)
{
    pthread_mutex_lock(&f->lock);
    f->refs++;
    pthread_mutex_unlock(&f->lock);
}

void frame_release(struct frame *f)
{
    int left;

    pthread_mutex_lock(&f->lock);
    f->refs--;
    left = f->refs;
    pthread_mutex_unlock(&f->lock);

    if (left == 0) {
        pthread_mutex_destroy(&f->lock);
        free(f);
    }
}
