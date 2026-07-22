/*
 * Copyright (c) Huawei Technilogies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "networking_kbaio.h"


void setCreateReadEventKbaio(int fd, void *privdata, aeFileProc *proc) {
    client *c = (client *)privdata;
    int readlen;
    size_t qblen;
    readlen = PROTO_IOBUF_LEN;
    /* If this is a multi bulk request, and we are processing a bulk reply
     * that is large enough, try to maximize the probability that the query
     * buffer contains exactly the SDS string representing the object, even
     * at the risk of requiring more read(2) calls. This way the function
     * processMultiBulkBuffer() can avoid copying buffers to create the
     * Redis Object representing the argument. */
    if (c->reqtype == PROTO_REQ_MULTIBULK && c->multibulklen && c->bulklen != -1
        && c->bulklen >= PROTO_MBULK_BIG_ARG) {
        ssize_t remaining = (size_t)(c->bulklen+2)-sdslen(c->querybuf);

        if (remaining < readlen) readlen = remaining;
    }

    qblen = sdslen(c->querybuf);
    if (c->querybuf_peak < qblen) {
        c->querybuf_peak = qblen;
    }
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);
    c->riov.iov_len = readlen;
    c->riov.iov_base = c->querybuf+qblen;
    c->submitted_query++;
    if (aeCreateFileEventWithBuf(server.el, fd, AE_READABLE, proc, c, &c->riov) == AE_ERR) {
        serverPanic("Unrecoverable error creating file event.");
    }
}

void readDoneFromClientKbaio(aeEventLoop *el, int fd, void *privdata, int mask) {
    UNUSED(mask);
    client *c = (client *)privdata;
    int nread = el->events[fd].res;
    if (nread < 0) {
        if (errno == EAGAIN) {
            return;
        } else {
            serverLog(LL_VERBOSE, "Reading from client: %s", strerror(errno));
            freeClient(c);
            return;
        }
    } else if (nread == 0) {
        serverLog(LL_VERBOSE, "Client closed connection");
        freeClient(c);
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
    if (c->flags & CLIENT_MASTER) {
        c->read_reploff += nread;
    }
    server.stat_net_input_bytes += nread;
    if (sdslen(c->querybuf) > server.client_max_querybuf_len) {
        sds ci = catClientInfoString(sdsempty(),c), bytes = sdsempty();
        bytes = sdscatrepr(bytes,c->querybuf,64);
        serverLog(LL_WARNING,"Closing client that reached max query buffer length: %s (qbuf initial bytes: %s)", ci, bytes);
        sdsfree(ci);
        sdsfree(bytes);
        freeClient(c);
        return;
    }
    /* Time to process the buffer. If the client is a master we need to
     * compute the difference between the applied offset before and after
     * processing the buffer, to understand how much of the replication stream
     * was actually applied to the master state: this quantity, and its
     * corresponding part of the replication stream, will be propagated to
     * the sub-slaves and to the replication backlog. */
    if (!(c->flags & CLIENT_MASTER)) {
        processInputBuffer(c);
    } else {
        size_t prev_offset = c->reploff;
        processInputBuffer(c);
        size_t applied = c->reploff - prev_offset;
        if (applied) {
            replicationFeedSlavesFromMasterStream(server.slaves,
                    c->pending_querybuf, applied);
            sdsrange(c->pending_querybuf, applied, -1);
        }
    }
    c->submitted_query--;
}

void writeDoneToClientKbaio(aeEventLoop *el, int fd, void *privdata, int mask) {
    UNUSED(mask);
    client *c = (client *)privdata;
    ssize_t nwritten = el->events[fd].res;
    if (nwritten < 0) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else {
            serverLog(LL_VERBOSE,
                "Error writing to client: %s", strerror(errno));
            freeClient(c);
            return;
        }
    }
    if (nwritten <= 0) return;
    c->sentlen += nwritten;
    c->totwritten += nwritten;
    size_t objlen;
    sds o;
    if (c->bufpos > 0) {
        /* If the buffer was sent, set bufpos to zero to continue with
         * the remainder of the reply. */
        if ((int)c->sentlen == c->bufpos) {
            c->bufpos = 0;
            c->sentlen = 0;
        }
    } else if (c->reply->head != NULL) {
        o = listNodeValue(listFirst(c->reply));
        objlen = sdslen(o);
        /* If we fully sent the object on head go to the next one */
        if (c->sentlen == objlen) {
            listDelNode(c->reply,listFirst(c->reply));
            c->sentlen = 0;
            c->reply_bytes -= objlen;
            /* If there are no longer objects in the list, we expect
             * the count of reply bytes to be exactly zero.  */
            if (listLength(c->reply) == 0)
                serverAssert(c->reply_bytes == 0);
        }
    }
    /* Note that we avoid to send more than NET_MAX_WRITES_PER_EVENT
     * bytes, in a single threaded server it's a good idea to serve
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
        writeToClient(fd, c, 0);
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
            freeClient(c);
            return;
        }
        setCreateReadEventKbaio(fd, c, readDoneFromClientKbaio);
    }
}

void writeToClientKbaio(client *c, int fd) {
    size_t objlen;
    sds o;
    if (clientHasPendingReplies(c)) {
        if (c->bufpos > 0) {
            c->wiov.iov_len = c->bufpos-c->sentlen;
            c->wiov.iov_base = c->buf+c->sentlen;
        } else {
            o = listNodeValue(listFirst(c->reply));
            objlen = sdslen(o);
            if (objlen == 0) {
                listDelNode(c->reply,listFirst(c->reply));
                return;
            }
            c->wiov.iov_len = objlen - c->sentlen;
            c->wiov.iov_base = o + c->sentlen;
        }

        if (aeCreateFileEventWithBuf(server.el, fd, AE_WRITABLE,
                                     writeDoneToClientKbaio, c, &(c->wiov)) == AE_ERR) {
            serverPanic("Unrecoverable error creating WRITABLE file event.");
        }
    }
}
