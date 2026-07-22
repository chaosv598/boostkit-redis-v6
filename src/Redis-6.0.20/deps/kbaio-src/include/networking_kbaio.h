/* 
 * Copyright (c) Huawei Technilogies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef NETWORKINGKBAIO_H_
#define NETWORKINGKBAIO_H_
#include "server.h"

void setCreateReadEventKbaio(connection *conn);
int connSetReadHandlerKbaio(connection *conn, ConnectionCallbackFunc func);
int connSetWriteHandlerKbaio(connection *conn, ConnectionCallbackFunc func, int barrier);
void readDoneFromClientKbaio(connection *conn);
void writeDoneToClientKbaio(connection *conn);
void writeToClientKbaio(client *c);
#endif
