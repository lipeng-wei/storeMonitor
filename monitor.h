/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file monitor.h
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/12 16:58:58
 * @brief 
 *  
 **/




#ifndef  __MONITOR_H_
#define  __MONITOR_H_

#include "ev.h"
#include "sds.h"
#include "engine.h"

typedef struct struct_monitor_map {
    sds ip_port_type;
    sds ip;
    unsigned short port;
    sds type;
    stServerInfo *serverInfo;
    UT_hash_handle hh;
} stMonitorMap;

int monitorUpdateLocalMetaForServer(EV_P_ sds ip_port_type);
int monitorClearOldLocalMeta(EV_P);
void monitorTimerCb(EV_P_ ev_timer *timer, int revents);















#endif  //__MONITOR_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
