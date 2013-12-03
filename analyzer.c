/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file analyzer.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/14 14:05:32
 * @brief 
 *  
 **/


#include <signal.h>

#include "ev.h"

#include "log.h"
#include "config.h"
#include "meta.h"
#include "connection.h"
#include "analyzer.h"
#include "gather.h"
#include "action.h"

stAnalyzer analyzer;

static int analyzerInit(void) {
    int ret = 0;
    analyzer.curGatherSeqId = 0;

    analyzer.targetServerMapCur = NULL;
    analyzer.targetServerMapTmp = NULL;
    analyzer.gatherServerList.gatherServerListHead = NULL;
    analyzer.gatherServerList.gatherServerListLen = 0;
    analyzer.gatherServerList.gatherQueryStr = sdsempty(); 
    analyzer.gatherServerList.gatherServerListId = 0; 
    analyzer.gatherServerList.toBeUpdated = SERVER_LIST_UPDATEDONE;


    // init connectionPool
    initConnectionPool();

    // init type
    ret = initTypeTable();
    if (ret == -1) {
        log_fatal("init typeTable fail");
        return -1;
    }

    // init meta
    initMeta(gatherUpdateLocalMetaForServer, gatherClearOldLocalMeta);

    return 0;
}

static void analyzerRun(EV_P) {
    // analyzer
    ev_timer metaUpdateTimer;
    ev_init(&metaUpdateTimer, metaUpdateTimerCb);
    metaUpdateTimer.repeat = config.metaUpdateInterval;
    ev_timer_again(EV_A_ &metaUpdateTimer);

    ev_timer connectionRepairTimer;
    ev_init(&connectionRepairTimer, connectionRepairTimerCb);
    connectionRepairTimer.repeat = config.connectionRepairInterval;
    ev_timer_again(EV_A_ &connectionRepairTimer);

    ev_timer gatherTimer;
    ev_init(&gatherTimer, gatherTimerCb);
    gatherTimer.repeat = config.gatherInterval;
    ev_timer_again(EV_A_ &gatherTimer);

    // action
    ev_timer actionCheckTimer;
    ev_init(&actionCheckTimer, actionCheckTimerCb);
    actionCheckTimer.repeat = config.actionCheckInterval;
    ev_timer_again(EV_A_ &actionCheckTimer);

    ev_run(EV_A_ 0);
}

int main(int argc, char *argv[]) {

    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    log_init("analyzer", "./log", 16);
    log_notice("server start...");
    
    initConfig();
    loadConfig("./conf/analyzer.conf");
    dumpConfig();

    log_set_loglevel(config.logLevel);

    struct ev_loop *loop = EV_DEFAULT;

    if (analyzerInit()) {
        sleep(1);
        exit(-1);
    }

    if (actionInit(EV_A)) {
        sleep(1);
        exit(-1);
    }

    analyzerRun(EV_A);

    sleep(1);
    return 0;
}
















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
