/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file store.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/13 12:57:51
 * @brief 
 *  
 **/

#include "store.h"
#include "log.h"
#include "async.h"
#include "config.h"

int storeServerTimeout(EV_P_ stServerInfo *storeServer) {
    log_warning("talk to storeServer timeout, ip[%s] port[%u]", storeServer->ip, storeServer->port);
    ev_timer_stop(EV_A_ &storeServer->rwTimeoutTimer);
    return 0;
}

void updateServerStateInStoreDone(redisAsyncContext *ac, void *reply, void *privdata) {
    stServerInfo *storeServer = (stServerInfo *)ac->data;
    if (storeServer->connectionState == CONNECTION_STATE_CONNECT) {
        log_debug("server has been RESET or DISCONNECTED, ip[%s] port[%u] type[%s]", storeServer->ip, storeServer->port, storeServer->type);
        return;
    }
    ev_timer_stop((struct ev_loop*)privdata, &storeServer->rwTimeoutTimer);
    if (ac->err) {
        log_warning("set server state in store server fail: %s", ac->errstr);
        // will be added to unconnected by ac->onDisconnect
    } else {
        if (reply == NULL) {
            log_warning("connection to server is closed with no error, maybe timeout, ip[%s] port[%u]", storeServer->ip, storeServer->port);
        } else {
            log_debug("set server state in store server done");
            // addServerToServerQueue(storeServer, &connectionPool.storeServerQueue[storeServer->queueId]);
        }
    }
}

int updateServerStateInStore(EV_P_ stServerInfo *server, stServerInfo *storeServer) {
    // update serverstate in stor server
    if (server->serverState.serverAliveState == SERVER_ALIVE_STATE_UP) {
        log_debug("now, to updateServerStateInStore");
        redisAsyncCommand((redisAsyncContext*)storeServer->connectionContext,
                updateServerStateInStoreDone, 
                EV_A, 
                "SETEX %s %d %d", 
                server->serverIdKey, 
                config.serverStateExpireTime, 
                SERVER_ALIVE_STATE_UP
                );
        storeServer->processTimeoutFn = storeServerTimeout;
        storeServer->rwTimeoutTimer.repeat = config.rwTimeoutSec;
        ev_timer_again(EV_A_ &storeServer->rwTimeoutTimer);
    } else {
        log_notice("monitor found target server down: targetServer ip[%s] port[%u] type [%s]", server->ip, server->port, server->type);
        // addServerToServerQueue(storeServer, &connectionPool.storeServerQueue[storeServer->queueId]);
    }
    return 0;
}


int updateMonitorStateInStore(EV_P_ stServerInfo *storeServer) {
    // update monitor state in stor server
    log_debug("now, to updateMonitorStateInStore");
    redisAsyncCommand((redisAsyncContext*)storeServer->connectionContext,
            updateServerStateInStoreDone, 
            EV_A, 
            "SETEX monitor_state %d %d", 
            config.serverStateExpireTime, 
            SERVER_ALIVE_STATE_UP
            );
    storeServer->processTimeoutFn = storeServerTimeout;
    storeServer->rwTimeoutTimer.repeat = config.rwTimeoutSec;
    ev_timer_again(EV_A_ &storeServer->rwTimeoutTimer);
    return 0;
}




















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
