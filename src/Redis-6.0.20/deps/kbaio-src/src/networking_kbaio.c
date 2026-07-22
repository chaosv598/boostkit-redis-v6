/*
 * Copyright (c) Huawei Technilogies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "networking_kbaio.h"


void setCreateReadEventKbaio(connection *conn) {
    client *c = connGetPrivateData(conn);
    int readlen;
    size_t qblen;

    /* Update total number of reads on server */
    server.stat_total_reads_processed++;

    readlen = PROTO_IOBUF_LEN;

    if (c->reqtype == PROTO_REQ_MULTIBULK && c->multibulklen && c->bulklen != -1
        && c->bulklen >= PROTO_MBULK_BIG_ARG)
    {
        ssize_t remaining = (size_t)(c->bulklen+2)-sdslen(c->querybuf);

        /* Note that the 'remaining' variable may be zero in some edge case,
         * for example once we resume a blocked client after CLIENT PAUSE. */
        if (remaining > 0 && remaining < readlen) readlen = remaining;
    }

    qblen = c->qblen = sdslen(c->querybuf);
    if (c->querybuf_peak < qblen) c->querybuf_peak = qblen;
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);
    c->riov.iov_len = readlen;
    c->riov.iov_base = c->querybuf+qblen;
    c->submitted_query++;
    if (aeCreateFileEventWithBuf(server.el, conn->fd, AE_READABLE, conn->type->ae_handler, conn, &c->riov) == AE_ERR) {
        serverPanic("Unrecoverable error creating file event.");
    }
}


int connSetReadHandlerKbaio(connection *conn, ConnectionCallbackFunc func) {
    if (func == conn->read_handler) return C_OK;
    conn->read_handler = func;
    return C_OK;
}

int connSetWriteHandlerKbaio(connection *conn, ConnectionCallbackFunc func, int barrier) {
    if (func == conn->write_handler) return C_OK;
    conn->write_handler = func;
    if (barrier)
        conn->flags |= CONN_FLAG_WRITE_BARRIER;
    else
        conn->flags &= ~CONN_FLAG_WRITE_BARRIER;
    
    return C_OK;
}

void readDoneFromClientKbaio(connection *conn) {
    client *c = connGetPrivateData(conn);
    int nread = conn->cqe_res;
    if (nread < 0) {
        if (connGetState(conn) == CONN_STATE_CONNECTED) {
            return;
        } else {
            serverLog(LL_VERBOSE, "Reading from client: %s", connGetLastError(c->conn));
            freeClientAsync(c);
            return;
        }
    } else if (nread == 0) {
        serverLog(LL_VERBOSE, "Client closed connection");
        freeClientAsync(c);
        return;
    } else if (c->flags & CLIENT_MASTER) {
        /* Append the query buffer to the pending (not applied) buffer
         * of the master. We'll use this buffer later in order to have a
         * copy of the string applied by the last command executed. */
        c->pending_querybuf = sdscatlen(c->pending_querybuf,
                                        c->querybuf+c->qblen,nread);
    }
    sdsIncrLen(c->querybuf,nread);
    c->lastinteraction = server.unixtime;
    if (c->flags & CLIENT_MASTER) c->read_reploff += nread;
    server.stat_net_input_bytes += nread;
    if (sdslen(c->querybuf) > server.client_max_querybuf_len) {
        sds ci = catClientInfoString(sdsempty(),c), bytes = sdsempty();
        bytes = sdscatrepr(bytes,c->querybuf,64);
        serverLog(LL_WARNING,"Closing client that reached max query buffer length: %s (qbuf initial bytes: %s)", ci, bytes);
        sdsfree(ci);
        sdsfree(bytes);
        freeClientAsync(c);
        return;
    }
    /* There is more data in the client input buffer, continue parsing it
     * in case to check if there is a full command to execute. */
    processInputBuffer(c);
    c->submitted_query--;
}

void writeDoneToClientKbaio(connection *conn) {
    client *c = connGetPrivateData(conn);
    ssize_t nwritten = conn->cqe_res;
    if (nwritten < 0) {
        if (connGetState(conn) == CONN_STATE_CONNECTED) {
            nwritten = 0;
        } else {
            serverLog(LL_VERBOSE,
                "Error writing to client: %s", connGetLastError(c->conn));
            freeClientAsync(c);
            return;
        }
    }
    if (nwritten <= 0) return;
    c->sentlen += nwritten;
    c->totwritten += nwritten;
    size_t objlen;
    clientReplyBlock *o;
    if (c->bufpos > 0) {
        /* If the buffer was sent, set bufpos to zero to continue with
         * the remainder of the reply. */
        if ((int)c->sentlen == c->bufpos) {
            c->bufpos = 0;
            c->sentlen = 0;
        }
    } else if (c->reply->head != NULL) {
        o = listNodeValue(listFirst(c->reply));
        objlen = o->used;
        /* If we fully sent the object on head go to the next one */
        if (c->sentlen == objlen) {
            c->reply_bytes -= o->size;
            listDelNode(c->reply,listFirst(c->reply));
            c->sentlen = 0;
            /* If there are no longer objects in the list, we expect
             * the count of reply bytes to be exactly zero.  */
            if (listLength(c->reply) == 0)
                serverAssert(c->reply_bytes == 0);
        }
    }
    /* Note that we avoid to send more than NET_MAX_WRITES_PER_EVENT
     * bytes, in a signle threaded server it's a good idea to serve
     * other clients as well, even if a very large request comes from
     * super fast link that is always able to accept data (in real world
     * scenario think about 'KEYS *' against the loopback interface).
     *
     * However if we are over the maxmemory limit we ignore that and
     * just deliver as much data as it is possible to deliver. 
     * 
     * Moreover, we also send as much as possible if the client is
     * a slave or a monitor (otherwise, on high-speed traffic, the
     * replication/output buffer will grow indefinitely) */
    if (c->totwritten > NET_MAX_WRITES_PER_EVENT && (server.maxmemory == 0
        || zmalloc_used_memory() < server.maxmemory) && !(c->flags & CLIENT_SLAVE))
        return;
    if (clientHasPendingReplies(c)) {
        writeToClient(c, 0);
    } else {
        server.stat_net_output_bytes += c->totwritten;
        if (c->totwritten > 0) {
            /* For clients representing masters we don't count sending data
             * as an interaction, since we always send REPLCONF ACK commsnds
             * that take some time to just fill the socket output buffer.
             * We just rely on data / pings received for timeout detection. */
             if (!(c->flags & CLIENT_MASTER)) c->lastinteraction = server.unixtime;
        }
        c->sentlen = 0;
        c->totwritten = 0;
        /* Close connection after entire reply has been sent. */
        if (c->flags & CLIENT_CLOSE_AFTER_REPLY) {
            freeClientAsync(c);
        }
        if (c->flags & CLIENT_CLOSE_AFTER_REPLY) return;
        setCreateReadEventKbaio(conn);
    }
}


void writeToClientKbaio(client *c) {
    size_t objlen;
    clientReplyBlock *o;
    if (clientHasPendingReplies(c)) {
        if (c->bufpos > 0) {
            c->wiov.iov_len = c->bufpos-c->sentlen;
            c->wiov.iov_base = c->buf+c->sentlen;
        } else {
            o = listNodeValue(listFirst(c->reply));
            objlen = o->used;
            if (objlen == 0) {
                c->reply_bytes -= o->size;
                listDelNode(c->reply,listFirst(c->reply));
                return;
            }
            c->wiov.iov_len = objlen - c->sentlen;
            c->wiov.iov_base = o->buf + c->sentlen;
        }
        if (aeCreateFileEventWithBuf(server.el, c->conn->fd, AE_WRITABLE,
                                     c->conn->type->ae_handler, c->conn, &(c->wiov)) == AE_ERR)
        {
            serverPanic("Unrecoverable error creating WRITABLE file event.");
        }
    }
}