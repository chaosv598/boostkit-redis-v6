/* 
 * Copyright (c) Huawei Technilogies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef AE_KBAIO_H
#define AE_KBAIO_H


int aeApiCreate(aeEventLoop *eventLoop);

int aeApiResize(aeEventLoop *eventLoop, int setsize);

void aeApiFree(aeEventLoop *eventLoop);

int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask,
                        struct iovec *iovecs);


void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask);

int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp);

void aeApiRegisterFile(aeEventLoop *eventLoop, int fd);

char *aeApiName();

#endif
