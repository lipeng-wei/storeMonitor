 /*************************************************************************
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file meta.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/06 09:28:07
 * @brief 
 *  
 **/

#include "meta.h"
#include "log.h"
#include "engine.h"
#include "async.h"
#include "connection.h"
#include "config.h"

stMeta meta;

void initMeta(updateLocalMetaForServerFunc *updateFn, clearOldLocalMetaFunc *clearFn) {
    meta.updateLocalMetaForServerFn = updateFn;
    meta.clearOldLocalMetaFn = clearFn;
}

static int updateLocalMeta(redisAsyncContext *ac, void *reply, void *privdata) {
    stServerInfo *server = (stServerInfo*)ac->data;
    if (ac->err) {
        log_warning("get info from metaServer fail: %s", ac->errstr);
        // will be add to unconnectedQueue by ac->onDisconnect
    } else {
        //TODO: r->type
        if (reply == NULL) {
            log_warning("connection to server is closed with no error, maybe timeout, ip[%s] port[%u]", server->ip, server->port);
        } else {
            redisReply *r = reply;
            switch(r->type) {
                case REDIS_REPLY_NIL:
                    log_debug("meta server return NIL");
                    break;
                case REDIS_REPLY_ERROR:
                    log_debug("meta server return ERROR: %s", r->str);
                    break;
                case REDIS_REPLY_STATUS:
                    log_debug("meta server return STATUS: %s", r->str);
                    break;
                case REDIS_REPLY_INTEGER:
                    log_debug("meta server return INTEGER: %lld", r->integer);
                    break;
                case REDIS_REPLY_STRING:
                    log_debug("meta server return STRING: %s", r->str);
                    break;
                case REDIS_REPLY_ARRAY:
                    log_debug("meta server return ARRAY");
                    if (r->elements == 0) {
                        log_debug("empty list or set");
                    } else {
                        int i = 0;
                        for (; i < r->elements; i ++) {
                            log_debug("monitor member[%d]: %s", i, r->element[i]->str);
                            if(meta.updateLocalMetaForServerFn((struct ev_loop*)privdata, r->element[i]->str) != 0) {
                                log_warning("fail to updateMetaForServer, [%s]", r->element[i]->str);
                                continue; // ignore new server failing!
                            }
                        }
                    }
                    meta.clearOldLocalMetaFn((struct ev_loop*)privdata);
                    break;
                default:
                    log_warning("unknow redisReply type[%d]", r->type);
                    break;
                }
            // addServerToServerQueue((stServerInfo*)ac->data, &connectionPool.metaServerQueue[((stServerInfo*)ac->data)->queueId]);
        }
    } 
    return 0;
}

stServerInfo *fetchMetaServerForRead() {
    stServerInfo *server = NULL;
    while(server = returnServerFromServerQueue(&connectionPool.metaServerQueue[META_SERVER_TYPE_READONLY])) {
        if (server->connectionContext == NULL) {
            // server close connection
            rmServerFromServerQueue(server, &connectionPool.metaServerQueue[META_SERVER_TYPE_READONLY]);
            addServerToServerQueue(server, &connectionPool.unconnectedServerQueue);
            continue;
        } else {
            break;
        }
    }
    if (server == NULL) {
        while(server = returnServerFromServerQueue(&connectionPool.metaServerQueue[META_SERVER_TYPE_READWRITE])) {
            if (server->connectionContext == NULL) {
                // server close connection
                rmServerFromServerQueue(server, &connectionPool.metaServerQueue[META_SERVER_TYPE_READWRITE]);
                addServerToServerQueue(server, &connectionPool.unconnectedServerQueue);
                continue;
            } else {
                break;
            }
        }
    }

    return server;
}

stServerInfo *fetchMetaServerForWrite() {
    stServerInfo *server = NULL;
    while(server = returnServerFromServerQueue(&connectionPool.metaServerQueue[META_SERVER_TYPE_READWRITE])) {
        if (server->connectionContext == NULL) {
            // server close connection
            rmServerFromServerQueue(server, &connectionPool.metaServerQueue[META_SERVER_TYPE_READWRITE]);
            addServerToServerQueue(server, &connectionPool.unconnectedServerQueue);
            continue;
        } else {
            break;
        }
    }

    return server;
}
void metaUpdateTimerCb(EV_P_ ev_timer *timer, int revents) {
    log_debug("try to fetch meta info from meta-server...");

    stServerInfo *server = fetchMetaServerForRead();
    if (server == NULL) {
        log_warning("no ivalid meta server to update meta info");
        return;
    }

    int err;
    err = redisAsyncCommand((redisAsyncContext*)server->connectionContext, updateLocalMeta, EV_A, "SMEMBERS %s%s", config.monitorGroup, MONITOR_SERVER_LIST_SUFFIX);
    if (err != REDIS_OK) {
        log_warning("fail to asyncCommand to server ip[%s] port[%u]", server->ip, server->port);
        // addServerToServerQueue(server, &connectionPool.metaServerQueue[server->queueId]);
        return;
    }
    return;
}





















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
