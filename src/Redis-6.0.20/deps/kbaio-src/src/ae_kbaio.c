/* Linux epoll(2) based ae.c module
 *
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <sys/epoll.h> //used for define EPOLLIN and EPOLLOUT
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include "zmalloc.h"
#include "common.h"
#include "liburing.h"
#include "ae_kbaio.h"

#define BACKLOG 8192
#define MAX_ENTRIES 16384 /* entries should be configured by users */
#define AE_POLLABLE 8

static int enable_register_files = 1;
struct io_uring_cqe *cqes[BACKLOG];

typedef struct kbaio_event {
    int fd;
    int type;
} kbaio_event;

typedef struct aeApiState {
    int ring_fd;
    struct io_uring *uring;
    struct kbaio_event *events;
} aeApiState;

int aeApiCreate(aeEventLoop *eventLoop) {
    int ret;
    struct io_uring_params params;
    aeApiState *state = zmalloc(sizeof(aeApiState));
    if (!state) return -1;
    state->events = zmalloc(sizeof(struct kbaio_event)*eventLoop->setsize);
    if (!state->events) {
        zfree(state);
        return -1;
    }

    state->uring = zmalloc(sizeof(struct io_uring));
    if (!state->uring) {
        zfree(state->events);
        zfree(state);
        return -1;
    }
    __asm__ volatile (
        "mov x0, %[params]\n\t"
        "mov x2, %[size_params]\n\t"
        "mov w1, 0\n\t"
        "bl memset\n\t"
        "str x0, [sp, #0x30]\n"

        "add x0, %[eventLoop], #%[kbaio_offset]\n\t"
        "ldr x0, [x0]\n\t"
        "cbz x0, 4f\n\t"
        "add x1, x0, #%[sqpoll_offset]\n\t"
        "ldr w1, [x1]\n\t"
        "cbz w1, 4f\n\t"

        "add x13, sp, #0x30\n\t"
        "mov x0, %[params]\n\t"
        "add x2, x0, #%[flags_offset]\n\t"
        "ldr w1, [x2]\n\t"
        "orr w1, w1, #%[SQPOLL]\n\t"
        "str w1, [x2]\n\t"
        "add x2, %[eventLoop], #%[kbaio_offset]\n\t"
        "ldr x2, [x2]\n\t"
        "add x3, x2, #%[specify_core_offset]\n\t"
        "ldr w3, [x3]\n\t"
        "cbz w3, 4f\n\t"
        "orr w1, w1, #%[SQ_AFF]\n\t"
        "str w1, [x0, #%[flags_offset]]\n\t"
        "add x3, x2, #%[sq_thread_cpu_kbaio_offset]\n\t"
        "ldr w3, [x3]\n\t"
        "str w3, [x0, #%[sq_thread_cpu_offset]]\n\t"

        "4:\n\t"
        "mov x0, #%[max_entries]\n\t"
        "ldr x11, [sp, 192]\n\t"
        "add x1, %[state], #%[uring_offset]\n\t"
        "ldr x1, [x1]\n\t"
        "mov x2, %[params]\n\t"
        "bl io_uring_queue_init_params\n\t"
        "ldr x11, [sp, 192]\n\t"
        "add x3, %[state], #%[ring_fd_offset]\n\t"
        "str w0, [x3]\n\t"
        "cmp w0, #-1\n\t"
        "beq 5f\n\t"
        "mov %w[ret], #0\n\t"
        "b 0f\n\t"

        "5:\n\t"
        "add x0, %[state], #%[uring_offset]\n\t"
        "ldr x0, [x0]\n\t"
        "bl zfree\n\t"
        "3:\n\t"
        "add x0, %[state], #%[events_offset]\n\t"
        "ldr x0, [x0]\n\t"
        "bl zfree\n\t"
        "2:\n\t"
        "mov x0, %[state]\n\t"
        "bl zfree\n\t"
        "1:\n\t"
        "mov %w[ret], #-1\n\t"
        "0:\n\t"
        : [ret] "=r" (ret), [state] "=&r" (state)
        : [eventLoop] "r" (eventLoop),
        [kbaio_offset] "i" (offsetof(aeEventLoop, kbaio)),
        [events_offset] "i" (offsetof(aeApiState, events)),
        [uring_offset] "i" (offsetof(aeApiState, uring)),
        [ring_fd_offset] "i" (offsetof(aeApiState, ring_fd)),
        [params] "r" (&params),
        [size_params] "i" (sizeof(struct io_uring_params)),
        [SQPOLL] "i" (IORING_SETUP_SQPOLL),
        [SQ_AFF] "i" (IORING_SETUP_SQ_AFF),
        [sqpoll_offset] "i" (offsetof(struct aeKbaioFlags, sqpoll)),
        [specify_core_offset] "i" (offsetof(struct aeKbaioFlags, specify_core)),
        [sq_thread_cpu_kbaio_offset] "i" (offsetof(struct aeKbaioFlags, sq_thread_cpu)),
        [sq_thread_cpu_offset] "i" (offsetof(struct io_uring_params, sq_thread_cpu)),
        [flags_offset] "i" (offsetof(struct io_uring_params, flags)),
        [max_entries] "i" (MAX_ENTRIES)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x19", "memory"
    );
    if (ret == -1) return ret;
    if (!(params.features & IORING_FEAT_FAST_POLL)) {
        io_uring_queue_exit(state->uring);
        zfree(state->uring);
        zfree(state->events);
        zfree(state);
        return -1;
    }
    eventLoop->apidata = state;
    if (enable_register_files) {
        int *files = malloc(MAX_ENTRIES * sizeof(int));
        if (!files) {
            enable_register_files = 0;
            return 0;
        }
        for (int i = 0; i < MAX_ENTRIES; i++)
            files[i] = -1;
        int retval = io_uring_register_files(state->uring, files, MAX_ENTRIES);
        if (retval < 0) {
            fprintf(stderr, "error register_files %d\n", ret);
            enable_register_files = 0;
        }
    }
    return ret;
}

int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    (void)eventLoop;
    (void)setsize;
    return 0;
}

void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;

    close(state->ring_fd);
    zfree(state->events);
    zfree(state->uring);
    zfree(state);
}

int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask,
                         struct iovec *iovecs) {
    aeApiState *state = eventLoop->apidata;
    struct io_uring_sqe *sqe = io_uring_get_sqe(state->uring);
    if (!sqe) return -1;

    /* NULL iovec means doing poll_add behavior */
    if (!iovecs) {
        unsigned int poll_mask = 0;
        if (mask == AE_READABLE) poll_mask |= EPOLLIN;
        if (mask == AE_WRITABLE) poll_mask |= EPOLLOUT;
        io_uring_prep_poll_add(sqe, fd, poll_mask);
        // io_uring_prep_poll_multishot(sqe ,fd, poll_mask);
    } else {
        if (mask & AE_READABLE)
            io_uring_prep_readv(sqe, fd, iovecs, 1, 0);
        if (mask & AE_WRITABLE)
            io_uring_prep_writev(sqe, fd, iovecs, 1, 0);
    }
    kbaio_event *ev = &state->events[fd];
    ev->fd = fd;
    ev->type = mask;
    if (!iovecs)
        ev->type |= AE_POLLABLE;
    if (enable_register_files) {
        sqe->flags |= IOSQE_FIXED_FILE;
    }
    io_uring_sqe_set_data(sqe, (void *)ev);
    return 0;
}

void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask) {
    (void)delmask;
    aeApiState *state = eventLoop->apidata;

    struct io_uring_sqe *sqe = io_uring_get_sqe(state->uring);
    if (!sqe) exit(1);

    kbaio_event *ev = &state->events[fd];
    io_uring_prep_poll_remove(sqe, (void *)ev);
}

int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    struct __kernel_timespec timeout = {
        .tv_sec = tvp->tv_sec,
        .tv_nsec = tvp->tv_usec * 1000,
    };
    struct io_uring_cqe *cqes_ptr;
    retval = io_uring_submit_and_wait_timeout(state->uring, &cqes_ptr, 1, &timeout, NULL);
    if (retval < 0) {
        return numevents;
    }
    int cqe_count = io_uring_peek_batch_cqe(state->uring, cqes, BACKLOG);
    /* go through all the cqe's */
    for (int i = 0; i < cqe_count; ++i) {
        int mask = 0;
        struct io_uring_cqe *cqe = cqes[i];
        kbaio_event *ev = io_uring_cqe_get_data(cqe);
        if (!ev) {
            io_uring_cqe_seen(state->uring, cqe);
            continue;
        }

        if (ev->type & AE_POLLABLE) {
            if (cqe->res < 0) {
                io_uring_cqe_seen(state->uring, cqe);
                continue;
            }
            if (cqe->res & EPOLLIN) mask |= AE_READABLE | AE_POLLABLE;
            if (cqe->res & EPOLLOUT) mask |= AE_WRITABLE | AE_POLLABLE;
            if (cqe->res & EPOLLERR) mask |= AE_WRITABLE | AE_READABLE | AE_POLLABLE;
            if (cqe->res & EPOLLHUP) mask |= AE_WRITABLE | AE_READABLE | AE_POLLABLE;
        } else {
            eventLoop->events[ev->fd].res = cqe->res;
            mask = ev->type;
        }

        eventLoop->fired[numevents].fd = ev->fd;
        eventLoop->fired[numevents].mask = mask;

        io_uring_cqe_seen(state->uring, cqe);
        numevents++;
    }

    return numevents;
}
void aeApiRegisterFile(aeEventLoop *eventLoop, int fd) {
    aeApiState *state = eventLoop->apidata;
    if (enable_register_files) {
        int ret;
        void *uring = state->uring;
        int tmp_fd = fd;

        __asm__ volatile (
            "mov x0, %[uring_ptr]\n\t"
            "mov w1, %w[fd_val]\n\t"
            "str w1, [sp, #-16]!\n\t"
            "mov x2, sp\n\t"
            "mov w3, #1\n\t"
            "bl io_uring_register_files_update\n\t"
            "add sp, sp, #16\n\t"
            "mov %w[ret_val], w0\n\t"
            : [ret_val] "=r" (ret)
            : [uring_ptr] "r" (uring),
              [fd_val] "r" (tmp_fd)
            : "x0", "x1", "x2", "x3", "x30",
              "memory", "cc"
        );
        if (ret < 0) {
            fprintf(stderr, "lege io_uring_register_files_update failed: %d %d\n", fd, ret);
            enable_register_files = 0;
        }
    }
}

char *aeApiName() {
    return "kbaio";
}