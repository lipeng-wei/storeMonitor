/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file rawAsyncClient.h
 * @author chendazhuang(com@baidu.com)
 * @date 2013/06/17 13:09:20
 * @brief raw async client based on libev
 *  
 **/




#ifndef  __RAWASYNCCLIENT_H_
#define  __RAWASYNCCLIENT_H_

#include "ev.h"

#define RAC_OK 0
#define RAC_ERROR -1

#define RAC_CONNECTION_STATE_CONNECT 0
#define RAC_CONNECTION_STATE_CONNECTED 1

struct stRawAsyncContext;

typedef void (*funcConnectCallback)(struct stRawAsyncContext *rac, int status);
typedef funcConnectCallback funcRawAsyncConnectCallback;
typedef funcConnectCallback funcRawAsyncDisConnectCallback;

typedef void (*funcCallback)(struct stRawAsyncContext *rac, void *privdata);

//typedef void (*funcRawAsyncConnectCallback)(struct stRawAsyncContext *rac, int status);
//typedef void (*funcRawAsyncDisConnectCallback)(struct stRawAsyncContext *rac, int status);

typedef struct stRawAsyncContext {
    int fd;
    char connection_state;
    struct ev_loop *loop;
    ev_io rev, wev;
    void *data;
    int err;
    char errstr[256];
    funcRawAsyncConnectCallback connectCallback;
    funcRawAsyncDisConnectCallback disConnectCallback;
    funcCallback readCallback;
    void *readCallbackPrivData;
    funcCallback writeCallback;
    void *writeCallbackPrivData;
} stRawAsyncContext;

stRawAsyncContext* rawAsyncConnect(char *ip, short port);
int rawAsyncLibevAttach(EV_P_ stRawAsyncContext *rac, funcRawAsyncConnectCallback connCb, funcRawAsyncDisConnectCallback disConnCb);
int rawAsyncDisConnect(stRawAsyncContext *rac);

int rawAsyncEventAdd(stRawAsyncContext *rac, int event, funcCallback cb, void *privdata);
int rawAsyncEventDelete(stRawAsyncContext *rac, int event);














#endif  //__RAWASYNCCLIENT_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
