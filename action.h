/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file action.h
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/19 16:37:20
 * @brief 
 *  
 **/




#ifndef  __ACTION_H_
#define  __ACTION_H_

#include <pthread.h>

#include "sds.h"
#include "ev.h"
#include "uthash.h"

#define EXCHANGE_STATE_EXCHANGE 0
#define EXCHANGE_STATE_EXCHANGING 1
#define EXCHANGE_STATE_EXCHANGED 2

#define DOWNSERVER_ROLE_MASTER 0
#define DOWNSERVER_ROLE_SLAVE 1
#define DOWNSERVER_ROLE_BACKUP 3

#define ACTION_ENABLE_OPTION_CHECK 0
#define ACTION_ENABLE_OPTION_CHECKING 1
#define ACTION_ENABLE_OPTION_CHECKED 2

typedef struct struct_exchange_state {
    short state;
    short toBeUpdated;
} stExchangeState;

typedef struct struct_server_property {
    short role;
    sds backupIp;
    unsigned short backupPort;
    sds backupType;
    sds pid;
    sds cluster;
} stServerProperty;

typedef struct struct_down_server {
    sds ip_port_type;
    sds ip;
    unsigned short port;
    sds type;
    stExchangeState exchangeState;
    short isNoUse; // 1, will be freed

    pthread_t actionThreadId;

    stServerProperty serverProperty;
    UT_hash_handle hh;
} stDownServer;


typedef struct struct_action {
    EV_P;
    stDownServer *downServerList;
    int actionEnable;
    int checkActionEnableOption;
} stAction;

extern stAction action;

//int addDwonServerToAction
int addServerToDownList (sds ip_port_type);
int rmServerFromDownList (sds ip_port_type);
int actionInit(EV_P);
void actionCheckTimerCb(EV_P_ ev_timer *timer, int revents);








#endif  //__ACTION_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
