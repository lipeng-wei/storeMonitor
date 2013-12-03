/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file gather.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/14 15:21:08
 * @brief 
 *  
 **/

#include "gather.h"
#include "log.h"
#include "analyzer.h"
#include "uthash.h"
#include "async.h"
#include "action.h"

int gatherUpdateLocalMetaForServer(EV_P_ sds ip_port_type) {
    log_debug("begin to update local meta for server[%s]", ip_port_type);
    stTargetServer *s, *tmp;
    HASH_FIND_STR(analyzer.targetServerMapCur, ip_port_type, s);
    if (s) {
        HASH_DEL(analyzer.targetServerMapCur, s);
        // check uniquess
        // here, no need
        HASH_ADD_KEYPTR(hh, analyzer.targetServerMapTmp, s->serverIdKey, strlen(s->serverIdKey), s);
    } else {
        //HASH_FIND_STR(analyzer.targetServerMapTmp, ip_port_type, tmp);
        //if (tmp) {
        //    log_warning("duplicate monitor server found, server[%s]", ip_port_type);
        //    return 0;
        //}
        analyzer.gatherServerList.toBeUpdated = SERVER_LIST_TOBEUPDATED;
        s = (stTargetServer*)calloc(1, sizeof(stTargetServer));
        if (s==NULL) {
            log_fatal("oom for calloc");
            exit(-1);
        }
        s->serverIdKey = sdsnew(ip_port_type);
        s->gatherSeqId = 0;
        s->failCount = 0;
        HASH_ADD_KEYPTR(hh, analyzer.targetServerMapTmp, s->serverIdKey, strlen(s->serverIdKey), s);

        log_debug("add one target server[%s]", s->serverIdKey);
    }
    return 0;
}

int gatherClearOldLocalMeta(EV_P) {
    log_debug("begin to clear old local meta");
    stTargetServer *s, *tmp;
    HASH_ITER(hh, analyzer.targetServerMapCur, s, tmp) {
        analyzer.gatherServerList.toBeUpdated = SERVER_LIST_TOBEUPDATED;
        HASH_DEL(analyzer.targetServerMapCur, s);
        rmServerFromDownList(s->serverIdKey);
        sdsfree(s->serverIdKey);
        free(s);
    }
    analyzer.targetServerMapCur = analyzer.targetServerMapTmp;
    analyzer.targetServerMapTmp = NULL;

    if (analyzer.gatherServerList.toBeUpdated == SERVER_LIST_TOBEUPDATED) {
        // free old list
        stServerListNode *server = analyzer.gatherServerList.gatherServerListHead, *tmpServer;
        while (server != NULL) {
            tmpServer = server->next;
            free(server);
            server = tmpServer;
        }
//        // free old str
//        sdsfree(analyzer.gatherServerList.gatherQueryStr);
//        analyzer.gatherServerList.gatherQueryStr = sdsempty();
//        if ((analyzer.gatherServerList.gatherQueryStr=sdscat(analyzer.gatherServerList.gatherQueryStr, "monitor_state ")) == NULL) {
//            log_fatal("oom for sdscat");
//            exit(-1);
//        }

        // create new list & str
        stGatherServerList *slist = &analyzer.gatherServerList;
        int j = 0;
        for (; j < slist->queryArgc; j ++) {
            free(slist->queryArgv[j]);
        }
        slist->queryArgc = HASH_COUNT(analyzer.targetServerMapCur) + 2;
        slist->queryArgv = (char **)realloc(slist->queryArgv, slist->queryArgc*sizeof(char*));
        slist->queryArgvLen = (size_t*)realloc(slist->queryArgvLen, slist->queryArgc*sizeof(size_t));
        if (slist->queryArgv == NULL || slist->queryArgvLen == NULL) {
            log_fatal("oom for realloc");
            exit(-1);
        }

        slist->queryArgvLen[0] = strlen("MGET");
        slist->queryArgv[0] = (char *)calloc(1, slist->queryArgvLen[0]+1);
        if (slist->queryArgv[0] == NULL) {
            log_fatal("oom for calloc");
            exit(-1);
        }
        strcpy(slist->queryArgv[0], "MGET");

        slist->queryArgvLen[1] = strlen("monitor_state");
        slist->queryArgv[1] = (char *)calloc(1, slist->queryArgvLen[1]+1);
        if (slist->queryArgv[1] == NULL) {
            log_fatal("oom for calloc");
            exit(-1);
        }
        strcpy(slist->queryArgv[1], "monitor_state");
        
        int i = 2;
        analyzer.gatherServerList.gatherServerListLen = 0;
        analyzer.gatherServerList.gatherServerListHead = NULL;
        tmpServer = NULL;
        HASH_ITER(hh, analyzer.targetServerMapCur, s, tmp) {
            if (tmpServer == NULL) {
                tmpServer = (stServerListNode*)calloc(1, sizeof(stServerListNode));
                if (tmpServer == NULL) {
                    log_fatal("oom for calloc");
                    exit(-1);
                }
                tmpServer->server = s;
                tmpServer->next = NULL;
                analyzer.gatherServerList.gatherServerListHead = tmpServer;
            } else {
                tmpServer->next = (stServerListNode*)calloc(1, sizeof(stServerListNode));
                if (tmpServer->next == NULL) {
                    log_fatal("oom for calloc");
                    exit(-1);
                }
                tmpServer->next->server = s;
                tmpServer->next->next = NULL;
                tmpServer = tmpServer->next;
            }
            analyzer.gatherServerList.gatherServerListLen ++;
//            if ((analyzer.gatherServerList.gatherQueryStr=sdscat(analyzer.gatherServerList.gatherQueryStr, s->serverIdKey)) == NULL
//                    || (analyzer.gatherServerList.gatherQueryStr=sdscat(analyzer.gatherServerList.gatherQueryStr, " ")) == NULL) {
//                log_fatal("oom for sdscat");
//                exit(-1);
//            }
            slist->queryArgvLen[i] = strlen(s->serverIdKey);
            slist->queryArgv[i] = (char *)calloc(1, slist->queryArgvLen[i]+1);
            if (slist->queryArgv[i] == NULL) {
                log_fatal("oom for calloc");
                exit(-1);
            }
            strcpy(slist->queryArgv[i], s->serverIdKey);

            i++;
        }
        // update id
        analyzer.gatherServerList.gatherServerListId ++;
        analyzer.gatherServerList.toBeUpdated = SERVER_LIST_UPDATEDONE;

//        log_debug("update gatherServerList done, id[%ld] list[%s]", 
//                analyzer.gatherServerList.gatherServerListId, 
//                analyzer.gatherServerList.gatherQueryStr
//                );
    }
    return 0;
}

int gatherTargetStateTimeoutFromStoreServer(EV_P_ stServerInfo *storeServer) {
    log_warning("gather target state from storeserver timout, storesrver ip[%s] port[%u]", storeServer->ip, storeServer->port);
    ev_timer_stop(EV_A_ &storeServer->rwTimeoutTimer);
    if (storeServer->privData) {
        free(storeServer->privData);
        storeServer->privData = NULL;
    }
    return 0;
}

int processTargetState(stServerInfo *storeServer, stGatherData *gatherData, redisReply *r) {
    if (r->elements == 0 || (r->elements-1) != analyzer.gatherServerList.gatherServerListLen) {
        log_warning("invalid result return from storeServer arrayLen[%d] != serverListLen[%d+1]: ip[%s] port[%u]", 
                r->elements,
                analyzer.gatherServerList.gatherServerListLen,
                storeServer->ip,
                storeServer->port
                );
        return -1;
    }
    if (gatherData->gatherSeqId != analyzer.curGatherSeqId) {
        log_warning("target state returned from storeserver is too late, new gather already started");
        return -1;
    }
    if (gatherData->gatherServerListId != analyzer.gatherServerList.gatherServerListId) {
        log_warning("target server list already changed when target state returned");
        return -1;
    }
    
    int i = 0;
    if (r->element[i]->type != REDIS_REPLY_STRING || atoi(r->element[i]->str) != SERVER_ALIVE_STATE_UP) {
        log_warning("store server ip[%s] port[%u]: moitor is down, ignore!", storeServer->ip, storeServer->port);
        return -1;
    }
    
    stServerListNode *serverNode = analyzer.gatherServerList.gatherServerListHead;
    while (i++, serverNode != NULL) {
        if (r->element[i]->type != REDIS_REPLY_STRING || atoi(r->element[i]->str) != SERVER_ALIVE_STATE_UP) {
            if (++serverNode->server->failCount >= config.serverDownQuota) {
                log_warning("target server is DOWN, server[%s]", serverNode->server->serverIdKey);
                //TODO
                // for action module
                addServerToDownList(serverNode->server->serverIdKey);
                serverNode->server->serverInDownList = SERVER_IN_DOWNLIST;
            }
        }
        serverNode = serverNode->next;
    }

    return 0;
}
        

void gatherTargetStateDoneFromStoreServer(redisAsyncContext *ac, void *reply, void *privdata) {
    stServerInfo *storeServer = (stServerInfo*)ac->data;
    stGatherData *gatherData = (stGatherData*)storeServer->privData;

    if (storeServer->connectionState == CONNECTION_STATE_CONNECT) {
        log_debug("server has been RESET or DISCONNECTED, ip[%s] port[%u] type[%s]", storeServer->ip, storeServer->port, storeServer->type);
        return;
    }

    ev_timer_stop(gatherData->loop, &storeServer->rwTimeoutTimer);

    if (ac->err) {
        log_warning("gather serverState from storeServer fail, ip[%s] port[%u], connState[%d]: %s", storeServer->ip, storeServer->port, storeServer->connectionState, ac->errstr);
        // will call ac->onDisconect
        // be added to unconnectedQueue
    } else {
        if (reply == NULL) {
            log_warning("connection to server is closed with no error, maybe timeout, ip[%s] port[%u]", storeServer->ip, storeServer->port);
        } else {
            redisReply *r = reply;
            switch(r->type) {
                case REDIS_REPLY_NIL:
                case REDIS_REPLY_ERROR:
                case REDIS_REPLY_STATUS:
                case REDIS_REPLY_INTEGER:
                case REDIS_REPLY_STRING:
                    log_warning("storeServer reply type error, type[%d] value[%s]", r->type, r->str);
                    break;
                case REDIS_REPLY_ARRAY:
                    processTargetState(storeServer, gatherData, r);
                    break;
                default:
                    log_warning("unknow redisReply type[%d]", r->type);
                    break;
            }
        }
    } 
    if (storeServer->privData) {
        free(storeServer->privData);
        storeServer->privData = NULL;
    }
    return;
}

int gatherTargetStateFromStoreServer(EV_P_ stServerInfo *storeServer) {
    stGatherData *gatherData = (stGatherData*)calloc(1, sizeof(stGatherData));
    if (gatherData == NULL) {
        log_fatal("oom for calloc stGatherData");
        exit(-1);
    }
    gatherData->gatherSeqId = analyzer.curGatherSeqId;
    gatherData->gatherServerListId = analyzer.gatherServerList.gatherServerListId;
    gatherData->loop = EV_A;
    storeServer->privData = gatherData;
    redisAsyncCommandArgv((redisAsyncContext*)storeServer->connectionContext, 
            gatherTargetStateDoneFromStoreServer, 
            gatherData, 
            analyzer.gatherServerList.queryArgc,
            analyzer.gatherServerList.queryArgv,
            analyzer.gatherServerList.queryArgvLen
            );
    storeServer->processTimeoutFn = gatherTargetStateTimeoutFromStoreServer;
    storeServer->rwTimeoutTimer.repeat = config.rwTimeoutSec;
    ev_timer_again(EV_A_ &storeServer->rwTimeoutTimer);
}

void gatherTimerCb(EV_P_ ev_timer *timer, int revents) {
    log_debug("start to gather targetServer state...");

    if (analyzer.targetServerMapCur == NULL) {
        log_trace("no server to be analyzed");
        return; 
    }

    // update gatherSeqId
    analyzer.curGatherSeqId ++;
    stServerListNode *serverNode = analyzer.gatherServerList.gatherServerListHead;
    while (serverNode != NULL) {
        stTargetServer *tServer = serverNode->server;
        if (tServer->serverInDownList == SERVER_IN_DOWNLIST 
                && tServer->failCount < config.serverDownQuota ) {
            // server is ok now, remove
            if (rmServerFromDownList(tServer->serverIdKey) == 0) {
                tServer->serverInDownList = SERVER_NOT_IN_DOWNLIST;
            }
        }
        tServer->gatherSeqId = analyzer.curGatherSeqId;
        tServer->failCount = 0;
        serverNode = serverNode->next;
    }
    // fetch storeConnection
    int i;
    stServerInfo *storeServer;
    for (i=0; i<connectionPool.storeServerNum; i ++) {
        while (storeServer = returnServerFromServerQueue(&connectionPool.storeServerQueue[i])) {
            if (storeServer->connectionContext == NULL) {
                // server closed connection
                rmServerFromServerQueue(storeServer, &connectionPool.storeServerQueue[i]);
                addServerToServerQueue(storeServer, &connectionPool.unconnectedServerQueue);
                continue;
            } else {
                gatherTargetStateFromStoreServer(EV_A_ storeServer);
                break;
            }
        }
        if (storeServer == NULL) {
            log_warning("no valid stroe server, queueID[%d]", i);
            continue;
        } 
    }
    return;
}

















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
