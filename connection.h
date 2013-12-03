/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file connection.h
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/06 09:53:12
 * @brief 
 *  
 **/




#ifndef  __CONNECTION_H_
#define  __CONNECTION_H_

#include "ev.h"
#include "sds.h"
#include "engine.h"

typedef struct struct_connection_pool {
    stServerInfoQueue monitorServerQueue;
    stServerInfoQueue unconnectedServerQueue;

    int storeServerNum; /* one store, one queue */
    stServerInfoQueue *storeServerQueue;
    stServerInfoQueue *metaServerQueue;
} stConnectionPool;

void initConnectionPool();
int initTypeTable();
void connectionRepairTimerCb(EV_P_ ev_timer *timer, int revents);
stServerInfo* createServerInfo(sds ip, unsigned short port, sds type);
int createConnectionForServer(EV_P_ stServerInfo *server);

extern stConnectionPool connectionPool;



#endif  //__CONNECTION_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
