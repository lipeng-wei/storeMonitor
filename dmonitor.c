/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file dmonitor.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/04 15:43:37
 * @brief 
 *  
 **/

#include <signal.h>

#include "ev.h"

#include "dmonitor.h"
#include "log.h"
#include "config.h"
#include "meta.h"
#include "connection.h"
#include "monitor.h"

stDMonitor dmonitor;

static int dmonitorInit(void) {
    int ret = 0;
    // init monnitorMap
    dmonitor.monitorMapCur = NULL;
    dmonitor.monitorMapTmp = NULL;

    // init connectionPool
    initConnectionPool();

    // init type
    ret = initTypeTable();
    if (ret == -1) {
        log_fatal("init typeTable fail");
        return -1;
    }

    // init meta
    initMeta(monitorUpdateLocalMetaForServer, 
           monitorClearOldLocalMeta); 

    return 0;
}

static void dmonitorRun(EV_P) {
    ev_timer metaUpdateTimer;
    ev_init(&metaUpdateTimer, metaUpdateTimerCb);
    metaUpdateTimer.repeat = config.metaUpdateInterval;
    ev_timer_again(EV_A_ &metaUpdateTimer);

    ev_timer connectionRepairTimer;
    ev_init(&connectionRepairTimer, connectionRepairTimerCb);
    connectionRepairTimer.repeat = config.connectionRepairInterval;
    ev_timer_again(EV_A_ &connectionRepairTimer);

    ev_timer monitorTimer;
    ev_init(&monitorTimer, monitorTimerCb);
    monitorTimer.repeat = config.monitorInterval;
    ev_timer_again(EV_A_ &monitorTimer);

    ev_run(EV_A_ 0);
}

int main(int argc, char *argv[]) {

    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    log_init("dmonitor", "./log", 16);
    log_notice("server start...");
    
    initConfig();
    loadConfig("./conf/dmonitor.conf");
    dumpConfig();

    log_set_loglevel(config.logLevel);

    struct ev_loop *loop = EV_DEFAULT;

    if (dmonitorInit()) {
        sleep(1);
        exit(-1);
    }
    dmonitorRun(EV_A);

    sleep(1);
    return 0;
}
















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
