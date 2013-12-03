/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file monitor.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/12 17:00:29
 * @brief 
 *  
 **/

#include "monitor.h"
#include "log.h"
#include "engine.h"
#include "store.h"
#include "config.h"
#include "dmonitor.h"

int monitorUpdateLocalMetaForServer(EV_P_ sds ip_port_type) {
    stMonitorMap *s, *tmp;
    HASH_FIND_STR(dmonitor.monitorMapCur, ip_port_type, s);
    if (s) {
        HASH_DEL(dmonitor.monitorMapCur, s);
        // check uniquess
        // here, no need
        HASH_ADD_KEYPTR(hh, dmonitor.monitorMapTmp, s->ip_port_type, strlen(s->ip_port_type), s);
        log_debug("server already in monitor, ip[%s] port[%u]", s->ip, s->port);
        return 0;
    } else {
        //HASH_FIND_STR(dmonitor.monitorMapTmp, ip_port_type, tmp);
        //if (tmp) {
        //    log_warning("duplicate monitor server found, server[%s]", ip_port_type);
        //    return 0;
        //}
        s = (stMonitorMap *)calloc(1, sizeof(stMonitorMap));
        if (s == NULL) {
            log_fatal("oom for calloc");
            exit(-1);
        }
        int count = 0;
        sds *tokens = sdssplitlen(ip_port_type, sdslen(ip_port_type), "_", strlen("_"), &count);
        if (count != 3) {
            log_warning("invalid server member in moniter_server_list, server[%s]", ip_port_type);
            free(s);
            for (count --; count >= 0; count --) {
                sdsfree(tokens[count]);
            }
            return -1;
        }
        s->ip_port_type = sdsnew(ip_port_type);
        s->ip = sdsdup(tokens[0]);
        s->port = atoi(tokens[1]);
        s->type = sdsdup(tokens[2]);
        
        for (count --; count >= 0; count --) {
            sdsfree(tokens[count]);
        }

        // add to unconnectionQueue
        stServerInfo *server = createServerInfo(s->ip, s->port, s->type);
        if (server == NULL) {
            log_warning("fail to create serverInfo for server, ip[%s] port[%u] type[%s]", s->ip, s->port, s->type);
            free(s);
            free(server);
            return -1;
        }
        server->serverRole = SERVER_ROLE_MONITOR;
        server->connectionState = CONNECTION_STATE_CONNECT;
        addServerToServerQueue(server, &connectionPool.unconnectedServerQueue);

        s->serverInfo = server;
        HASH_ADD_KEYPTR(hh, dmonitor.monitorMapTmp, s->ip_port_type, strlen(s->ip_port_type), s);
        log_debug("add new server to be monitored, ip[%s] port[%u] type[%s]", s->ip, s->port, s->type);
        return 0;
    }
    return 0;
}

int monitorClearOldLocalMeta(EV_P) {
    stMonitorMap *s, *tmp;

    HASH_ITER(hh, dmonitor.monitorMapCur, s, tmp) {
        HASH_DEL(dmonitor.monitorMapCur, s);
        stServerInfo *server = s->serverInfo;
        server->connectionState = CONNECTION_STATE_DESTROYED;
        free(s);
    }

    dmonitor.monitorMapCur = dmonitor.monitorMapTmp;
    dmonitor.monitorMapTmp = NULL;

    return 0;
}

int monitorProcessState(EV_P_ stServerInfo *server) {
    stServerInfo *storeServer;
    while (storeServer=returnServerFromServerQueue(&connectionPool.storeServerQueue[server->queueId])) {
        if (storeServer->connectionContext == NULL) {
            rmServerFromServerQueue(storeServer, &connectionPool.storeServerQueue[server->queueId]);
            addServerToServerQueue(storeServer, &connectionPool.unconnectedServerQueue);
        } else {
            updateServerStateInStore(EV_A_ server, storeServer);
            break;
        }
    }
    if (storeServer == NULL) {
        log_warning("no valid storeServer connection");
    } 
    server->processState = PROCESS_STATE_PROCESSNONE;
    if (server->connectionState != CONNECTION_STATE_CONNECT) {
        addServerToServerQueue(server, &connectionPool.monitorServerQueue);
    }
    return 0;
}

int monitorProcessTimeout(EV_P_ stServerInfo *server) {
    log_debug("I will report this timeout to store");
    server->serverState.serverAliveState = SERVER_ALIVE_STATE_DOWN;
    server->processStateFn(EV_A_ server);
    ev_timer_stop(EV_A_ &server->rwTimeoutTimer);

    return 0;
}

static int startMonitorForServer(EV_P_ stServerInfo *server) {
    server->processStateFn = monitorProcessState;
    server->queryStateFn(EV_A_ server);
    server->rwTimeoutTimer.repeat = config.rwTimeoutSec;
    ev_timer_again(EV_A_ &server->rwTimeoutTimer);
    server->processTimeoutFn = monitorProcessTimeout;
    server->processState = PROCESS_STATE_INPROCESS;
    return 0;
}

static int startUpdateMonitorStateInStore(EV_P) {
    stServerInfo *storeServer;
    while (storeServer=returnServerFromServerQueue(&connectionPool.storeServerQueue[0])) {
        if (storeServer->connectionContext == NULL) {
            rmServerFromServerQueue(&connectionPool.storeServerQueue[0]);
            addServerToServerQueue(storeServer, &connectionPool.unconnectedServerQueue);
        } else {
            updateMonitorStateInStore(EV_A_ storeServer);
            break;
        }
    }
    if (storeServer == NULL) {
        log_warning("no valid storeServer connection");
    }
    return 0; 
}

void monitorTimerCb(EV_P_ ev_timer *timer, int revents) {
    log_debug("begin monitor the state of all the server monitored");
    stServerInfo *server = NULL;
    while (server = getServerFromServerQueue(&connectionPool.monitorServerQueue)) {
        if (server->connectionState == CONNECTION_STATE_DESTROYED) {
            if (server->connectionContext) {
                server->destroyConnectionFn(EV_A_ server);
                server->connectionContext = NULL;
            }
            destroyServerInfo(server);
        } else if (server->connectionContext == NULL) {
            // server close the connection
            addServerToServerQueue(server, &connectionPool.unconnectedServerQueue);
        } else {
            startMonitorForServer(EV_A_ server);
        }
    }
    // update monitor state in store
    startUpdateMonitorStateInStore(EV_A);
    return;
}




















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
