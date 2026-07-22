/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef NETWORKING_KBAIO_H_
#define NETWORKING_KBAIO_H_
#include "server.h"

void setCreateReadEventKbaio(int fd, void *privdata, aeFileProc *proc);
void readDoneFromClientKbaio(aeEventLoop *el, int fd, void *privdata, int mask);
void writeDoneToClientKbaio(aeEventLoop *el, int fd, void *privdata, int mask);
void writeToClientKbaio(client *c, int fd);
#endif

