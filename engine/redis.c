/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file redis.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/06 11:21:54
 * @brief 
 *  
 **/

#include "hiredis.h"
#include "async.h"
#include "engine.h"
#include "log.h"
#include "adapters/libev.h"
#include "action.h"
#include <sys/time.h>

typedef stDownServer stServerDetail;

typedef struct stRedisMonitorInfo{
    struct timeval tv;
    long long cmd_proc;
}stRedisMonitorInfo;

typedef struct redisData{
    stRedisMonitorInfo *last;
    stRedisMonitorInfo *compute;
    stServerDetail *serverDetail;
}redisData;

void logMonitorInfo(char * info, int len, stServerInfo *server);

void redisConnectCb(const redisAsyncContext *c, int status) {
    stServerInfo *server = (stServerInfo*)c->data;
    if (status != REDIS_OK) {
        log_warning("Connect to server ip[%s] port[%u] fail: %s", server->ip, server->port, c->errstr);
        if (server->onDisConnectCb)
            server->onDisConnectCb(server);
        return;
    }
    log_debug("Connect to server ip[%s] port[%u] success", server->ip, server->port);
    if (server->onConnectCb)
        server->onConnectCb(server);
}

void redisDisConnectCb(const redisAsyncContext *c, int status) {
    stServerInfo *server = (stServerInfo*)c->data;
    if (status != REDIS_OK) {
        log_warning("Server ip[%s] port[%u] has been disconnected: %s", server->ip, server->port, c->errstr);
    }
    if(server->privData){
        redisData * serverData = ((redisData *)(server->privData));
        if(serverData->last){
            free(serverData->last);
        }
        serverData->last = NULL;
        if(serverData->compute){
            free(serverData->compute);
        }
        serverData->compute= NULL;
        if(serverData->serverDetail){
            freeDownServer(serverData->serverDetail);
        }
        serverData->serverDetail = NULL;

        free(server->privData);
        server->privData = NULL;
    }
    if (server->onDisConnectCb)
        server->onDisConnectCb(server);
}


int redisCreateConnectionFn(EV_P_ stServerInfo *server) {
    log_debug("begin creating redis connection to ip[%s] port[%u]", server->ip, server->port);

    redisAsyncContext *ac = redisAsyncConnect(server->ip, server->port);
    if (ac == NULL) {
        log_fatal("oom");
        exit(-1);
    }
    if (ac->err) {
        log_warning("create redisAsyncConnect fail to ip[%s] port[%u]: %s", server->ip, server->port, ac->errstr);
        redisAsyncFree(ac); // will call ac->onDisConnect;
        server->connectionContext = NULL;
        return -1;
    }
    ac->data = (void*)server;
    server->connectionContext = (void *)ac;
    redisLibevAttach(EV_A_ ac); 
    redisAsyncSetConnectCallback(ac,redisConnectCb);
    redisAsyncSetDisconnectCallback(ac,redisDisConnectCb);
    return 0;
}

int redisDestroyConnectionFn(EV_P_ stServerInfo *server) {
    log_debug("begin disconnect redis connection to ip[%s] port[%u]", server->ip, server->port);
    // destroy connection
    if (server->connectionContext)
        redisAsyncFree((redisAsyncContext*)server->connectionContext); // will call ac->onDisConnect;
    server->connectionContext = NULL;
    return 0;
}

void redisQueryStateDoneCb(redisAsyncContext *ac, void *reply, void *privdata) {
    stServerInfo *server = (stServerInfo*)ac->data;
    if (ac->err) {
        log_warning("query state from server fail, ip[%s] port[%u] type[%s]: %s", 
                server->ip, server->port, server->type, ac->errstr);
        server->serverState.serverAliveState = SERVER_ALIVE_STATE_DOWN;
    } else {
        //TODO: r->type
        if (reply == NULL) {
            log_warning("connection to server is closed with no error, maybe timeout, ip[%s] port[%u]", server->ip, server->port);
        } else {
            redisReply *r = reply;
            switch(r->type) {
                case REDIS_REPLY_NIL:
                case REDIS_REPLY_ERROR:
                case REDIS_REPLY_STATUS:
                case REDIS_REPLY_INTEGER:
                case REDIS_REPLY_ARRAY:
                    server->serverState.serverAliveState = SERVER_ALIVE_STATE_DOWN;
                    log_warning("server is down, ip[%s] port[%u] type[%s]", server->ip, server->port, server->type);
                    break;
                case REDIS_REPLY_STRING:
                    server->serverState.serverAliveState = SERVER_ALIVE_STATE_UP;
                    logMonitorInfo(r->str, r->len, server);
                    break;
                default:
                    server->serverState.serverAliveState = SERVER_ALIVE_STATE_DOWN;
                    log_warning("unknow redisReply type[%d]", r->type);
                    break;
            }
        }
    }
    server->processStateFn((struct ev_loop*)privdata, server);
    ev_timer_stop((struct ev_loop*)privdata, &server->rwTimeoutTimer);
}


int redisQueryStateFn(EV_P_ stServerInfo *server) {
    log_debug("try to query state for server ip[%s] port[%u] type[%s]", server->ip, server->port, server->type);
    if (server->privData == NULL){
        server->privData = malloc(sizeof(redisData));
        if (server->privData == NULL){
            log_fatal("oom for alloc memory");
            exit(-1);
        }
        redisData *serverData = (redisData *)server->privData;
        serverData->last = (stRedisMonitorInfo *)calloc(1, sizeof(stRedisMonitorInfo));
        if (serverData->last == NULL){
            log_fatal("oom for alloc memory");
            exit(-1);
        }
        serverData->compute= (stRedisMonitorInfo *)calloc(1, sizeof(stRedisMonitorInfo));
        if (serverData->compute== NULL){
            log_fatal("oom for alloc memory");
            exit(-1);
        }
        serverData->serverDetail = (stServerDetail *)calloc(1, sizeof(stServerDetail));
        if (serverData->serverDetail == NULL){
            log_fatal("oom for alloc memory");
            exit(-1);
        }
        serverData->serverDetail->ip_port_type = sdsdup(server->serverIdKey);
        serverData->serverDetail->exchangeState.toBeUpdated = 1;
        updateExchangeState(EV_A_ serverData->serverDetail);
    }
    redisAsyncCommand((redisAsyncContext*)server->connectionContext,
            redisQueryStateDoneCb,
            EV_A,
            "INFO"
            );
    return 0;
}

void logMonitorInfo(char * info, int len, stServerInfo *server)
{
    char * keyStart, *keyEnd, *valueStart, *valueEnd;
    redisData *serverData = (redisData *)server->privData;
    stRedisMonitorInfo *last = serverData->last;
    stRedisMonitorInfo *compute = serverData->compute;
    stRedisMonitorInfo *now  = (stRedisMonitorInfo *)calloc(1, sizeof(stRedisMonitorInfo));
    stServerDetail *serverDetail = serverData->serverDetail;

    gettimeofday(&now->tv, NULL);
    short useOldCompute = 0;
    double interval = now->tv.tv_sec - last->tv.tv_sec + (now->tv.tv_usec - last->tv.tv_usec) / 1000000.0;
    if (interval <= 0){
        useOldCompute = 1;
    }

    char * pid = serverDetail->serverProperty.pid;
    char * partition = serverDetail->serverProperty.cluster;
    short role = serverDetail->serverProperty.role;
    if ( !pid || !partition){
        log_warning("No pid or partition info");
        return;
    }

    long long memory = 0;
    long long rss = 0;
    long long cmd_proc = 0;
    long long sync_delay = 0;
    long long keys = 0;
    long long expires = 0;
    long long fork_usec = 0;
    long long clients = 0;

    keyStart = info;
    while (info + len > keyStart){
        keyEnd = keyStart;
        while( info + len > keyEnd && *keyEnd != ':' ){
            keyEnd ++ ;
        }
        if ( info + len == keyEnd){
            log_warning("invalid info reply");
            return ;
        }
        * keyEnd = '\0';
        valueStart = keyEnd + 1;
        valueEnd = valueStart;
        while(info + len > valueEnd && *valueEnd != '\r' ){
            valueEnd ++;
        }
        if ( info + len == valueEnd){
            log_warning("invalid info reply");
            return ;
        }
        * valueEnd = '\0';

        if (strcmp(keyStart, "used_memory") == 0){
            memory = atoll(valueStart);
        } else if (strcmp(keyStart, "used_memory_rss") == 0){
            rss = atoll(valueStart);
        } else if (strcmp(keyStart, "total_commands_processed") == 0){
            now->cmd_proc = atoll(valueStart);
            if(useOldCompute){
                cmd_proc = compute->cmd_proc;
            } else {
                cmd_proc = (now->cmd_proc - last->cmd_proc);
                compute->cmd_proc = cmd_proc;
            }
        } else if (strcmp(keyStart, "sync_delay_mstime") == 0){
            sync_delay = atoll(valueStart);
        } else if (strncmp(keyStart, "dbx", 2) == 0){
            sscanf(valueStart, "keys=%lld,expires=%lld", &keys, &expires);
        } else if (strcmp(keyStart, "latest_fork_usec") == 0){
            fork_usec = atoll(valueStart);
        } else if (strcmp(keyStart, "connected_clients") == 0){
            clients = atoll(valueStart);
        }
        keyStart = valueEnd + 2;
    }

    log_notice("info: product=ksarch-redis subsys=%s module=dmonitor logid=%u local_ip=%s "
            "pid=%s partition=%s role=%s ip=%s port=%u used_memory=%lld used_rss=%lld proc_cmd=%lld "
            "keys=%lld expires=%lld clients=%lld fork_usec=%lld sync_delay=%lld interval=%f", 
            pid, rand() & 0x7fffffff, server->ip,
            pid, partition, role?"slave":"master", server->ip, server->port, memory, rss, cmd_proc, 
            keys, expires, clients, fork_usec, sync_delay, interval);
    serverData->last = now;
    free(last);
}
















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
