/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file connection.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/06 09:54:21
 * @brief 
 *  
 **/

#include <dlfcn.h>

#include "log.h"
#include "connection.h"
#include "config.h"

stConnectionPool connectionPool;
stTypeProperty *typeTable;

void initConnectionPool() {
    connectionPool.monitorServerQueue.queueTypeStr = sdsnew("monitor");
    connectionPool.monitorServerQueue.queueType = SERVER_QUEUE_TYPE_MONITOR;
    connectionPool.unconnectedServerQueue.queueTypeStr = sdsnew("unconnected");
    connectionPool.unconnectedServerQueue.queueType = SERVER_QUEUE_TYPE_UNCONNECTED;
    connectionPool.storeServerQueue = (stServerInfoQueue*)calloc(connectionPool.storeServerNum, sizeof(stServerInfoQueue));
    int i = 0;
    for (; i < connectionPool.storeServerNum; i ++) {
        connectionPool.storeServerQueue[i].queueTypeStr = sdsnew("store");
        connectionPool.storeServerQueue[i].queueType = SERVER_QUEUE_TYPE_STORE;
    }
    connectionPool.metaServerQueue = (stServerInfoQueue*)calloc(META_SERVER_TYPES, sizeof(stServerInfoQueue));
    for (i = 0; i < META_SERVER_TYPES; i ++) {
        connectionPool.metaServerQueue[i].queueTypeStr = sdsnew("meta");
        connectionPool.metaServerQueue[i].queueType = SERVER_QUEUE_TYPE_META;
    }
}

int initTypeTable() {
    int i;
    // init typeTable
    stTypeProperty *s;
    for (i=0; i < config.typeCount; i++) {
        HASH_FIND_STR(typeTable, config.typeStr[i], s);
        if (s) continue;
        s = (stTypeProperty*)calloc(1, sizeof(stTypeProperty));
        if (s == NULL) {
            log_fatal("oom when calloc");
            return -1;
        }
        s->type = sdsdup(config.typeStr[i]);
        char filename[256] = {0};
        snprintf(filename, sizeof(filename), "%s/%s.so", config.libDir, s->type);
        void *dlhandler = dlopen(filename, RTLD_NOW);
        if (dlhandler == NULL) {
            log_fatal("fail to dlopen %s: %s", filename, dlerror());
            return -1;
        }
        char funcname[256] = {0};
        snprintf(funcname, sizeof(filename), "%sCreateConnectionFn", s->type);
        dlerror();
        s->createConnectionFn = dlsym(dlhandler, funcname);
        char *err;
        if ((err = dlerror())) {
            log_fatal("fali to find function %s in %s: %s", funcname, filename, err);
            return -1;
        }
        snprintf(funcname, sizeof(filename), "%sDestroyConnectionFn", s->type);
        dlerror();
        s->destroyConnectionFn = dlsym(dlhandler, funcname);
        if ((err = dlerror())) {
            log_fatal("fali to find function %s in %s: %s", funcname, filename, err);
            return -1;
        }
        snprintf(funcname, sizeof(filename), "%sQueryStateFn", s->type);
        dlerror();
        s->queryStateFn = dlsym(dlhandler, funcname);
        if ((err = dlerror())) {
            log_fatal("fali to find function %s in %s: %s", funcname, filename, err);
            return -1;
        }

        HASH_ADD_KEYPTR(hh, typeTable, config.typeStr[i], sdslen(config.typeStr[i]), s);
        log_debug("load type %s done", s->type);
    }
    return 0;
}

stServerInfo *addServerToServerQueue(stServerInfo *server, stServerInfoQueue *queue) {
    server->next = NULL;
    server->prev = NULL;
    if (queue->tail == NULL) {
        queue->tail = server;
    } else {
        queue->tail->next = server;
        server->prev = queue->tail;
        queue->tail = server;
    }

    if (queue->head == NULL) {
        queue->head = server;
    }

    log_debug("add server queue[%s], ip[%s] port[%u] type[%s]", queue->queueTypeStr, server->ip, server->port, server->type);
    return server;
}

stServerInfo *getServerFromServerQueue(stServerInfoQueue *queue) {
    // get an remove
    stServerInfo *server = NULL;
    if (queue->head == NULL) {
        return NULL;
    } else {
        server = queue->head;
        queue->head = queue->head->next;
    }

    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    server->next = NULL;
    server->prev = NULL;
    log_debug("fetch one server from queue[%s], ip[%s] port[%u] type[%s]", queue->queueTypeStr, server->ip, server->port, server->type);
    return server;
}

stServerInfo *returnServerFromServerQueue(stServerInfoQueue *queue) {
    // get NOT remove
    if (queue->queueType == SERVER_QUEUE_TYPE_MONITOR) {
        log_warning("monitor server cannot use this func");
        return NULL;
    }
    return queue->head;
}

stServerInfo *rmServerFromServerQueue(stServerInfo *server, stServerInfoQueue *queue) {
    if (server == NULL)
        return NULL;

    if (server->prev == NULL) {
        queue->head = server->next;
    } else {
        server->prev->next = server->next;
    }

    if (server->next == NULL) {
        queue->tail = server->prev;
    } else {
        server->next->prev = server->prev;
    }

    server->prev = NULL;
    server->next = NULL;

    log_debug("rm server from queue[%s], ip[%s] port[%u] type[%s]", queue->queueTypeStr, server->ip, server->port, server->type);
    return server;
}

int onConnectCb(stServerInfo *server) {
    log_debug("server CONNECTED: ip[%s] port[%u] type[%s]", server->ip, server->port, server->type);
    //rmServerFromServerQueue(server, &connectionPool.unconnectedServerQueue);
    switch (server->serverRole) {
        case SERVER_ROLE_META:
            addServerToServerQueue(server, &connectionPool.metaServerQueue[server->queueId]);
            break;
        case SERVER_ROLE_MONITOR:
            addServerToServerQueue(server, &connectionPool.monitorServerQueue);
            break;
        case SERVER_ROLE_STORE:
            addServerToServerQueue(server, &connectionPool.storeServerQueue[server->queueId]);
            break;
        default:
            log_warning("unknow server role, ip[%s] port[%u] role[%d]", server->ip, server->port, server->serverRole);
            break;
    }
    server->connectionState = CONNECTION_STATE_CONNECTED;
    return 0;
}

int onDisConnectCb(stServerInfo *server) {
    log_debug("server DISCONNECTED: ip[%s] port[%u] type[%s]", server->ip, server->port, server->type);
    server->connectionContext = NULL;
    if (server->connectionState == CONNECTION_STATE_DESTROYED ) {
        //wait to be destroyed in queue check, do nothing
    }
    else if (server->connectionState == CONNECTION_STATE_CONNECT) {
        //create fail, just put back to unconnected queue
        //timeout or client reset, put into unconnected queue
        // only for SERVER_ROLE_MONITOR: store/meta used deffirent pool model (not rm)
        if (server->serverRole == SERVER_ROLE_MONITOR) {
            addServerToServerQueue(server, &connectionPool.unconnectedServerQueue);
        }
    } else {
        // server closed aysnc
        server->connectionState = CONNECTION_STATE_CONNECT;
    }
    return 0;
}

int resetConnectionForServer(EV_P_ stServerInfo *server) {
    log_debug("connection will be RESETED: ip[%s] port[%u] type[%s]", server->ip, server->port, server->type);
    server->destroyConnectionFn(EV_A_ server);
    return 0;
}


void rwTimeoutTimerCb(EV_P_ ev_timer *timer, int revents) {
    stServerInfo *server = (stServerInfo *)((char *)timer - offsetof(stServerInfo, rwTimeoutTimer));
    log_warning("communicate to server timeout, ip[%s] port[%u] timeout[%gs]", server->ip, server->port, config.rwTimeoutSec);
    server->connectionState = CONNECTION_STATE_CONNECT;
    if (server->processTimeoutFn) {
        server->processTimeoutFn(EV_A_ server);
    }
    resetConnectionForServer(EV_A_ server);
    return;
}

stServerInfo* createServerInfo(sds ip, unsigned short port, sds type) {
    stServerInfo *server = (stServerInfo *)calloc(1, sizeof(stServerInfo));
    if (server == NULL) {
        log_fatal("oom for calloc");
        return NULL;
    }
    server->ip = sdsdup(ip);
    server->port = port;
    server->type = sdsdup(type);
    char tmp[256] = {0};
    snprintf(tmp, sizeof(tmp),  "%s_%u_%s", server->ip, server->port, server->type);
    server->serverIdKey = sdsnew(tmp);
    server->processState = PROCESS_STATE_PROCESSNONE;
    server->privData = NULL;
    server->rwTimeoutSec = 0;
    server->queueId = 0;

    //ev_timer_init(&server->rwTimeoutTimer, rwTimeoutTimerCb, server->rwTimeoutSec, 0);
    ev_init(&server->rwTimeoutTimer, rwTimeoutTimerCb);
    return server;
}

int createConnectionForServer(EV_P_ stServerInfo *server) {
    if (server->createConnectionFn == NULL) {
        stTypeProperty *s;
        HASH_FIND_STR(typeTable, server->type, s);
        if (s == NULL) {
            log_fatal("not find typeentry for type[%s] in typeTable", server->type);
            return -1;
        }
        if (s->createConnectionFn == NULL) {
            log_fatal("no createConnectionFn in typeentry[%s]", server->type);
            return -1;
        }
        if (s->destroyConnectionFn == NULL) {
            log_fatal("no destroyConnectionFn in typeentry[%s]", server->type);
            return -1;
        }
        if (s->queryStateFn == NULL) {
            log_fatal("no queryStateFn in typeentry[%s]", server->type);
            return -1;
        }
        server->createConnectionFn = s->createConnectionFn;
        server->destroyConnectionFn = s->destroyConnectionFn;
        server->onConnectCb = onConnectCb;
        server->onDisConnectCb = onDisConnectCb;
        server->queryStateFn = s->queryStateFn;
    }
    if (server->createConnectionFn(EV_A_ server)) {
        log_warning("fail to connect to server type[%s] ip[%s] port[%u]", server->type, server->ip, server->port);
        return -1;
    }
    //server->connectionState = CONNECTION_STATE_CONNECTING;
    return 0;
}

int destroyServerInfo(stServerInfo *server) {    
    log_debug("serverInfo be destroyed, ip[%s] port[%u] type[%s]", server->ip, server->port, server->type);
    if (server->ip) {
        sdsfree(server->ip);
        server->ip = NULL;
    }
    if (server->type) {
        sdsfree(server->type);
        server->type = NULL;
    }
    server->privData = NULL;
    free(server);
    server = NULL;
}

//int destroyConnectionForServer(EV_P_ stServerInfo *server) {
//    //server->destroyConnectionFn(EV_A_ server);
//    server->connectionState = CONNECTION_STATE_DESTROYED;
//    return 0;
//}

void connectionRepairTimerCb(EV_P_ ev_timer *timer, int revents) {
    stServerInfo *server = NULL;
    stServerInfo *failedServerHead = NULL;
    stServerInfo *failedServerTail = NULL;
    while (server = getServerFromServerQueue(&connectionPool.unconnectedServerQueue)) {
        if (server->connectionState == CONNECTION_STATE_CONNECT) {
            log_debug("try to create connection for server ip[%s] port[%u] type[%s]", server->ip, server->port, server->type);
            // try to connect
            if (createConnectionForServer(EV_A_ server) != 0) {
                // not put tail, dead circle
                server->next = NULL;
                if (failedServerHead == NULL) {
                    failedServerHead = server;
                    failedServerTail = server;
                } else {
                    failedServerTail->next = server;
                    failedServerTail = server;
                }
                continue; 
            }
        } else if (server->connectionState == CONNECTION_STATE_DESTROYED) {
            destroyServerInfo(server);
        } else {
            log_warning("invalid server state in unconnectionQueue, serverState[%d]", server->connectionState);
            //TODO: delete it
            log_warning("serverInfo be destroyed, ip[%s] port[%u] type[%s]", server->ip, server->port, server->type);
            destroyServerInfo(server);
            continue;
        }
    }

    // add failed to unconnected queue for next circle
    while (failedServerHead) {
        addServerToServerQueue(failedServerHead, &connectionPool.unconnectedServerQueue);
        failedServerHead = failedServerHead->next;
    }
        
    return;
}





















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
