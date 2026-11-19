#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "frame.h"

ssize_t read_full(int fd, void *buf, size_t n)
{
    size_t got = 0;

    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r == 0)
            return 0;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        got += r;
    }
    return got;
}

ssize_t write_full(int fd, const void *buf, size_t n)
{
    size_t done = 0;

    while (done < n) {
        ssize_t w = write(fd, (const char *)buf + done, n - done);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        done += w;
    }
    return done;
}

int read_frame(int fd, struct frame *f)
{
    unsigned char hdr[HDR_LEN];
    uint16_t len, vlan;
    ssize_t r;

    r = read_full(fd, hdr, HDR_LEN);
    if (r <= 0)
        return r;

    memcpy(&len, hdr, 2);
    memcpy(f->src, hdr + 2, 6);
    memcpy(f->dst, hdr + 8, 6);
    memcpy(&vlan, hdr + 14, 2);
    f->len = ntohs(len);
    f->vlan = ntohs(vlan);
    f->prio = hdr[16];

    if (f->len > MAX_PAYLOAD)
        return -1;

    if (f->len > 0) {
        r = read_full(fd, f->data, f->len);
        if (r <= 0)
            return r;
    }
    return 1;
}

int write_frame(int fd, struct frame *f)
{
    unsigned char buf[HDR_LEN + MAX_PAYLOAD];
    uint16_t len = htons(f->len);
    uint16_t vlan = htons(f->vlan);

    memcpy(buf, &len, 2);
    memcpy(buf + 2, f->src, 6);
    memcpy(buf + 8, f->dst, 6);
    memcpy(buf + 14, &vlan, 2);
    buf[16] = f->prio;
    memcpy(buf + HDR_LEN, f->data, f->len);

    if (write_full(fd, buf, HDR_LEN + f->len) < 0)
        return -1;
    return 0;
}

int is_broadcast(unsigned char *mac)
{
    unsigned char all[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    return memcmp(mac, all, 6) == 0;
}
