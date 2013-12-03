/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file rawAsyncClient.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/06/17 13:38:09
 * @brief 
 *  
 **/

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "rawAsyncClient.h"

static void rawAsyncReadCallback(EV_P_ ev_io *w, int revents);
static void rawAsyncWriteCallback(EV_P_ ev_io *w, int revents);
static int rawAsyncCheckConnectOK(stRawAsyncContext *rac);
static void rawAsyncConnectDone(EV_P_ ev_io *w, int revents);

stRawAsyncContext *rawAsyncConnect(char *ip, short port) {
    // create ctx
    stRawAsyncContext *rac = (stRawAsyncContext*)calloc(1, sizeof(stRawAsyncContext));
    if (rac == NULL) {
        return NULL;
    }
    rac->connection_state = RAC_CONNECTION_STATE_CONNECT;

    // try to connect
    char _port[6] = {0};
    int ret = snprintf(_port, 6, "%d", port);
    if (ret < 0 || ret >= 6) {
        rac->err = -1;
        snprintf(rac->errstr, sizeof(rac->errstr), "invalid port");
        goto errorout;
    }

    // get server addr
    struct addrinfo *result, *cur;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    // hints.ai_flags = AI_NUMERICHOST;
    ret = getaddrinfo(ip, _port, &hints, &result);
    if (ret != 0) {
        rac->err = -1;
        snprintf(rac->errstr, sizeof(rac->errstr), "getaddrinfo error: %s", gai_strerror(ret));
        goto errorout;
    }
    for (cur = result; cur != NULL; cur = cur->ai_next) {
        // create socket
        rac->fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (rac->fd == -1) {
            rac->err = errno;
            snprintf(rac->errstr, sizeof(rac->errstr), "fail to create socket: %s", strerror(errno));
            continue;
        }
        // set opt
        int optval = 1;
        ret = setsockopt(rac->fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
        if (ret == -1) {
            rac->err = errno;
            snprintf(rac->errstr, sizeof(rac->errstr), "fail to setsockopt: %s", strerror(errno));
            goto errorout;
        }
        int flags;
        if ((flags = fcntl(rac->fd, F_GETFL)) == -1) {
            rac->err = errno;
            snprintf(rac->errstr, sizeof(rac->errstr), "fail to fcntl: %s", strerror(errno));
            goto errorout;
        }
        flags |= O_NONBLOCK;
        if (fcntl(rac->fd, F_SETFL, flags) == -1) {
            rac->err = errno;
            snprintf(rac->errstr, sizeof(rac->errstr), "fail to fcntl: %s", strerror(errno));
            goto errorout;
        }
        // connect
        ret = connect(rac->fd, cur->ai_addr, cur->ai_addrlen);
        if (ret == -1) {
            if (errno == EHOSTUNREACH) {
                rac->err = errno;
                snprintf(rac->errstr, sizeof(rac->errstr), "fail to connect: %s", strerror(errno));
                close(rac->fd); // next ip
                rac->fd = -1;
                continue;
            } else if (errno == EINPROGRESS) {
                // ok,
                // rac->connection_state = CONNECTION_STATE_CONNECTING;
                // don't care here, let state change in first write
                break;
            } else {
                // error
                rac->err = errno;
                snprintf(rac->errstr, sizeof(rac->errstr), "fail to connect: %s", strerror(errno));
                close(rac->fd);
                rac->fd = -1;
                goto errorout;
            }
        } else {
            // connect done
            // don't care here, let state change in first write
            break;
        }
    }
    freeaddrinfo(result);
    return rac;

errorout:
    if (rac->fd != -1) {
        close(rac->fd);
        rac->fd = -1;
    }
    // rac shall be destroyed in disconnect
    return rac;
}
    
int rawAsyncLibevAttach(EV_P_ stRawAsyncContext *rac, funcRawAsyncConnectCallback connCb, funcRawAsyncDisConnectCallback disConnCb) {
    rac->loop = loop;
    rac->rev.data = rac;
    rac->wev.data = rac;
    rac->connectCallback = connCb;
    rac->disConnectCallback = disConnCb;
    rac->readCallback = NULL;
    rac->readCallbackPrivData = NULL;
    rac->writeCallback = NULL;
    rac->writeCallbackPrivData = NULL;
    rac->connection_state = RAC_CONNECTION_STATE_CONNECT; // always check 

    ev_io_init(&rac->rev, rawAsyncReadCallback, rac->fd, EV_READ);
    ev_io_init(&rac->wev, rawAsyncWriteCallback, rac->fd, EV_WRITE);

    ev_io_start(EV_A_ &rac->wev); // only start write to connect
    return RAC_OK;
}

int rawAsyncDisConnect(stRawAsyncContext *rac) {
    if (rac == NULL) {
        return RAC_OK;
    }
    if (rac->disConnectCallback) {
        rac->disConnectCallback(rac, rac->err);
    }
    rawAsyncEventDelete(rac, EV_READ);
    rawAsyncEventDelete(rac, EV_WRITE);
    if (rac->fd != -1) {
        close(rac->fd);
        rac->fd = -1;
    }
    free(rac);

    return RAC_OK;
}

int rawAsyncEventAdd(stRawAsyncContext *rac, int event, funcCallback cb, void *privdata) {
    rac->err = 0;
    switch(event) {
        case EV_READ:
            rac->readCallback = cb;
            rac->readCallbackPrivData = privdata;
            if (!ev_is_active(&rac->rev))
                ev_io_start(rac->loop, &rac->rev);
            break;
        case EV_WRITE:
            rac->writeCallback = cb;
            rac->writeCallbackPrivData = privdata;
            if (!ev_is_active(&rac->wev))
                ev_io_start(rac->loop, &rac->wev);
            break;
        default:
            rac->err = -1;
            snprintf(rac->errstr, sizeof(rac->errstr), "unsuported event: %d", event);
            break;
    }
    return rac->err;
}

int rawAsyncEventDelete(stRawAsyncContext *rac, int event) {
    // event will be expired, and ignored
    rac->err = 0;
    switch(event) {
        case EV_READ:
            rac->readCallback = NULL;
            rac->readCallbackPrivData = NULL;
            ev_io_stop(rac->loop, &rac->rev);
            break;
        case EV_WRITE:
            rac->writeCallback = NULL;
            rac->writeCallbackPrivData = NULL;
            ev_io_stop(rac->loop, &rac->wev);
            break;
        default:
            rac->err = -1;
            snprintf(rac->errstr, sizeof(rac->errstr), "unsuported event: %d", event);
            break;
    }
    return rac->err;
}

static void rawAsyncReadCallback(EV_P_ ev_io *w, int revents) { 
    stRawAsyncContext *rac = (stRawAsyncContext*)w->data;
    if (rac->readCallback) {
        rac->readCallback(rac, rac->readCallbackPrivData);
    } else {
        ev_io_stop(rac->loop, &rac->rev);
    }
}

static void rawAsyncWriteCallback(EV_P_ ev_io *w, int revents) { 
    stRawAsyncContext *rac = (stRawAsyncContext*)w->data;
    if (rac->connection_state != RAC_CONNECTION_STATE_CONNECTED) {
        return  rawAsyncConnectDone(EV_A_ w, revents);
    }
    if (rac->writeCallback) {
        rac->writeCallback(rac, rac->writeCallbackPrivData);
    } else {
        ev_io_stop(rac->loop, &rac->wev);
    }
}

static int rawAsyncCheckConnectOK(stRawAsyncContext *rac) {
    int err = RAC_OK;
    socklen_t errlen = sizeof(err);

    if (getsockopt(rac->fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == RAC_ERROR) {
        // wrong
        rac->err = errno;
        snprintf(rac->errstr, sizeof(rac->errstr), "fail to getsockopt: %s", strerror(errno));
        return RAC_ERROR;
    }

    if (err) {
        rac->err = err;
        snprintf(rac->errstr, sizeof(rac->errstr), "fail to connect: %s", strerror(err));
        return RAC_ERROR;
    }

    // done
    rac->err = RAC_OK;
    return RAC_OK;
}

static void rawAsyncConnectDone(EV_P_ ev_io *w, int revents) {
    stRawAsyncContext *rac = (stRawAsyncContext*)w->data;
    int ret = rawAsyncCheckConnectOK(rac);
    if (ret != RAC_OK) {
        rac->connection_state = RAC_CONNECTION_STATE_CONNECT;
        rac->connectCallback(rac, RAC_ERROR);
    } else {
        rac->connection_state = RAC_CONNECTION_STATE_CONNECTED;
        rac->connectCallback(rac, RAC_OK);
    }
}
   





/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */

