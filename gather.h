/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file gather.h
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/14 15:20:42
 * @brief 
 *  
 **/




#ifndef  __GATHER_H_
#define  __GATHER_H_


#include "connection.h"
#include "uthash.h"

#define SERVER_LIST_TOBEUPDATED 1
#define SERVER_LIST_UPDATEDONE 0

#define SERVER_IN_DOWNLIST 1
#define SERVER_NOT_IN_DOWNLIST 0

typedef struct struct_target_server {
    sds serverIdKey; /* ip_port_type */
    long long gatherSeqId; /* will be set every gatherTimerCallback */
    int failCount; /* down count */
    int serverInDownList; /* is already in downlist? */
    UT_hash_handle hh;
} stTargetServer;

typedef struct struct_gather_data {
    long long gatherSeqId;
    long long gatherServerListId;
    EV_P;
} stGatherData; /* for gather cmd */

typedef struct struct_server_list_node {
    stTargetServer *server;
    struct struct_server_list_node *next;
} stServerListNode;

typedef struct struct_gather_server_list {
    stServerListNode *gatherServerListHead;
    int gatherServerListLen;
    sds gatherQueryStr;
    int queryArgc;
    char **queryArgv;
    size_t *queryArgvLen;
    long long gatherServerListId;
    short toBeUpdated;
} stGatherServerList;

int gatherUpdateLocalMetaForServer(EV_P_ sds ip_port_type);
int gatherClearOldLocalMeta(EV_P);
void gatherTimerCb(EV_P_ ev_timer *timer, int revents);













#endif  //__GATHER_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
