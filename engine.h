/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file egnine.h
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/06 18:05:42
 * @brief 
 *  
 **/




#ifndef  __EGNINE_H_
#define  __EGNINE_H_

#include "sds.h"
#include "uthash.h"
#include "ev.h"

#define SERVER_ROLE_META 0
#define SERVER_ROLE_STORE 1
#define SERVER_ROLE_MONITOR 2

#define CONNECTION_STATE_CONNECT 0
#define CONNECTION_STATE_CONNECTING 1
#define CONNECTION_STATE_CONNECTED 2
#define CONNECTION_STATE_DESTROYED 3
#define CONNECTION_STATE_RESET 4

#define PROCESS_STATE_PROCESSNONE 0
#define PROCESS_STATE_INPROCESS 1

#define SERVER_ALIVE_STATE_DOWN 0
#define SERVER_ALIVE_STATE_UP 1
#define SERVER_ALIVE_STATE_UNKNOW 2

#define META_SERVER_TYPES 2 /* r & rw */
#define META_SERVER_TYPE_READONLY 0
#define META_SERVER_TYPE_READWRITE 1

#define MONITOR_SERVER_LIST_SUFFIX "_monitor_server_list"
#define EXCHANGED_SERVER_LIST_SUFFIX "_exchanged_server_list"
#define SERVER_INFO_SUFFIX "_server_info"

#define SERVER_QUEUE_TYPE_META 0
#define SERVER_QUEUE_TYPE_STORE 1
#define SERVER_QUEUE_TYPE_MONITOR 2
#define SERVER_QUEUE_TYPE_UNCONNECTED 3

struct stServerInfo;

typedef int (onConnectCbFunc)(struct stServerInfo*);
typedef int (onDisConnectCbFunc)(struct stServerInfo*);
typedef int (createConnectionFunc)(EV_P_ struct stServerInfo*);
typedef int (destroyConnectionFunc)(EV_P_ struct stServerInfo*);
typedef int (processTimeoutFunc)(EV_P_ struct stServerInfo*);
typedef int (stateMonitorFunc)(EV_P_ struct stServerInfo*);

typedef struct struct_server_state {
    short serverAliveState;
} stServerState;

typedef struct struct_server_info {
    sds serverIdKey; /* ip_port_type */
    sds ip;
    unsigned short port;
    sds type;
    short connectionState;

    createConnectionFunc *createConnectionFn; // onsuccess, return 0; esle -1
    onConnectCbFunc *onConnectCb;
    onDisConnectCbFunc *onDisConnectCb;
    destroyConnectionFunc *destroyConnectionFn;
    processTimeoutFunc *processTimeoutFn;

    // monitor
    stateMonitorFunc *queryStateFn;
    stateMonitorFunc *processStateFn;

    short serverRole; /* SERVER_ROLE_XXX */
    short queueId; /*queue ID in the roleQueue, will put into pool.roleServerQueue[Id]*/
    short canWrite;
    struct struct_server_info *next;
    struct struct_server_info *prev;

    void *connectionContext; /* for engine connectionContext */
    ev_timer rwTimeoutTimer;
    double rwTimeoutSec;

    stServerState serverState;
    short processState;

    void *privData; /* for some privdata */
} stServerInfo;

typedef struct struct_server_info_queue {
    stServerInfo *head;
    stServerInfo *tail;
    sds queueTypeStr;
    short queueType;
} stServerInfoQueue;

typedef struct struct_type_property {
    sds type;
    createConnectionFunc *createConnectionFn;
    destroyConnectionFunc *destroyConnectionFn;
    stateMonitorFunc *queryStateFn;
    UT_hash_handle hh;
} stTypeProperty;

extern stTypeProperty *typeTable;










#endif  //__EGNINE_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
