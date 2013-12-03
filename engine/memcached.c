/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file memcached.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/06/17 13:04:13
 * @brief 
 *  
 **/

#include <errno.h>
#include <netinet/in.h>

#include "engine.h"
#include "log.h"
#include "rawAsyncClient.h"
#include "action.h"

#define BUF_LEN 1024
#define MEMCACHED_READ_HEADER 1
#define MEMCACHED_READ_KEY 2
#define MEMCACHED_READ_VALUE 3

#pragma pack(4)
typedef struct stMemcachedCommandHeader {
    int8_t magic;
    int8_t opcode;
    int16_t keyLen;
    int8_t extraLen;
    int8_t dataType;
    int16_t status;
    int32_t totalBodyLen; // extraLen + keyLen + valueLen
    char opaque[4];
    char cas[8];
    char body[]; // extra + key + value, if have
} stMemcachedCommandHeader;

typedef struct stMemcachedPacket {
    char *body;
    char *pcur;
    int left;
    char buffer[BUF_LEN];
    int bufferLeft;
    char readState;
    stMemcachedCommandHeader hdr;
} stMemcachedPacket;
#pragma pack()

typedef struct stMonitorInfo {
    int time; // timestamp
    long long currConns;
    long long totalConns;
    double rusageUser;
    double rusageSystem;
    long long currItems;
    long long cmdGets;
    long long cmdSets;
    long long getHits;
    long long getMiss;
    long long deleteHits;
    long long deleteMiss;
    long long bytesRead;
    long long bytesWrite;
    long long listenDisabledNum;
    long long connYields;
    long long expiredUnfetched;
    long long evictedUnfetched;
    long long evictions;
    long long reclaimed;
    long long slabsMoved;
} stMonitorInfo;

typedef struct stRACData {
    stMemcachedPacket *pkt;
    stServerInfo *server;
    stMonitorInfo *info0;
    stMonitorInfo *info1;
    stMonitorInfo *last;
    stMonitorInfo *now;
    stMonitorInfo *compute;
    stDownServer *serverInfo; // used action.h; I know, the name is toooooo confused, FIXME
} stRACData;

static void memcachedSendStatsToServer(stRawAsyncContext *rac, void *privdata);
static void memcachedReadStatsResponse (stRawAsyncContext *rac, void *privdata);
static void logMonitorInfo(stRawAsyncContext *rac);

void memcachedConnectCb(stRawAsyncContext *rac, int status) {
    stRACData *racdata = (stRACData*)rac->data;
    stServerInfo *server = racdata->server;
    if (status != RAC_OK) {
        log_warning("Connect to server ip[%s] port[%u] fail: %s", server->ip, server->port, rac->errstr);
//        if (server->onDisConnectCb)
//            server->onDisConnectCb(server);
        rawAsyncDisConnect(rac);
        return;
    }
    log_debug("Connect to server ip[%s] port[%u] success", server->ip, server->port);
    if (server->onConnectCb)
        server->onConnectCb(server);
    rawAsyncEventDelete(rac, EV_READ);
    rawAsyncEventDelete(rac, EV_WRITE);
}

void memcachedDisConnectCb(stRawAsyncContext *rac, int status) {
    // called by asyncDisconnect(rac);
    stRACData *racdata = (stRACData*)rac->data;
    stServerInfo *server = racdata->server;
    log_warning("Server ip[%s] port[%u] is disconnected: %s", server->ip, server->port, rac->errstr);
    if (server->onDisConnectCb)
        server->onDisConnectCb(server);
    racdata->server = NULL;
    if (racdata->pkt) {
        free(racdata->pkt);
        racdata->pkt = NULL;
    }
    if (racdata->info0) {
        free(racdata->info0);
        racdata->info0 = NULL;
    }
    if (racdata->compute) {
        free(racdata->compute);
        racdata->compute = NULL;
    }
    if (racdata->serverInfo) {
        if (racdata->serverInfo->serverProperty.pid == NULL) {
            racdata->serverInfo->isNoUse = 1;
        } else {
            freeDownServer(racdata->serverInfo);
        }
        racdata->serverInfo = NULL;
    }
}

int memcachedCreateConnectionFn(EV_P_ stServerInfo *server) {
    log_debug("begin creating memcached connection to ip[%s] port[%u]", server->ip, server->port);

    stRawAsyncContext *rac = rawAsyncConnect(server->ip, server->port);
    if (rac == NULL) {
        log_warning("oom");
        exit(-1);
    }
    if (rac->err) {
        log_warning("create memcachedAsyncConnect fail to ip[%s] port[%u]: %s", server->ip, server->port, rac->errstr);
        return -1;
    }
    stRACData *racdata = (stRACData*)malloc(sizeof(stRACData));
    stMemcachedPacket *pkt = (stMemcachedPacket*)malloc(sizeof(stMemcachedPacket));
    stMonitorInfo *info = (stMonitorInfo*)calloc(2, sizeof(stMonitorInfo));
    stMonitorInfo *compute = (stMonitorInfo*)calloc(1, sizeof(stMonitorInfo));
    stDownServer *serverInfo = (stDownServer*)calloc(1, sizeof(stDownServer));
    if (!pkt || !racdata || !info || !serverInfo) {
        log_fatal("oom");
        exit(-1);
    }
    racdata->pkt = pkt;
    racdata->server = server;
    racdata->info0 = info;
    racdata->info1 = info+1;
    racdata->last = racdata->info0;
    racdata->now = racdata->info1;
    racdata->serverInfo = serverInfo;
    racdata->serverInfo->ip_port_type = sdsdup(server->serverIdKey);
    racdata->compute = compute;
    rac->data = racdata;
    server->connectionContext = (void*)rac;
    rawAsyncLibevAttach(EV_A_ rac, memcachedConnectCb, memcachedDisConnectCb); 
    return 0;
}

int memcachedDestroyConnectionFn(EV_P_ stServerInfo *server) {
    log_debug("begin disconnect memcached connection to ip[%s] port[%u]", server->ip, server->port);
    // destroy connection
    if (server->connectionContext)
        rawAsyncDisConnect((stRawAsyncContext*)server->connectionContext); // will call ac->onDisConnect;
    server->connectionContext = NULL;
    return 0;
}

void memcachedQueryStateDoneCb(stRawAsyncContext *rac, void *privdata) {
    stRACData *racdata = (stRACData*)rac->data;
    stServerInfo *server = racdata->server;
    if (rac->err) {
        log_warning("query state from server fail, ip[%s] port[%u] type[%s]: %s", 
                server->ip, server->port, server->type, rac->errstr);
        server->serverState.serverAliveState = SERVER_ALIVE_STATE_DOWN;
    } else {
        server->serverState.serverAliveState = SERVER_ALIVE_STATE_UP;
    }
    server->processStateFn((struct ev_loop*)privdata, server);
    ev_timer_stop((struct ev_loop*)privdata, &server->rwTimeoutTimer);
}


int memcachedQueryStateFn(EV_P_ stServerInfo *server) {
    log_debug("try to query state for server ip[%s] port[%u] type[%s]", server->ip, server->port, server->type);
    rawAsyncEventAdd((stRawAsyncContext *)server->connectionContext, EV_WRITE, memcachedSendStatsToServer, ((stRACData*)((stRawAsyncContext*)server->connectionContext)->data)->pkt);
    stRACData *racdata = (stRACData*)((stRawAsyncContext*)server->connectionContext)->data;
    if (racdata->serverInfo->serverProperty.pid == NULL) {
        // update serverInfo
        racdata->serverInfo->exchangeState.toBeUpdated = 1;
        updateExchangeState(EV_A_ racdata->serverInfo);
    }
    return 0;
}

static void memcachedSendStatsToServer(stRawAsyncContext *rac, void *privdata) {
    stMemcachedPacket *req = (stMemcachedPacket*)privdata;
    memset(req, 0, sizeof(*req));
    req->hdr.magic = 0x80;
    req->hdr.opcode = 0x10;
    req->pcur = NULL;
    req->left = 0;

    rac->err = 0;
    // here, we think we can write command, it's only 24bytes
    if (write(rac->fd, &req->hdr, sizeof(stMemcachedCommandHeader)) != sizeof(stMemcachedCommandHeader)) {
        rac->err = errno;
        stServerInfo *server = (stServerInfo*)rac->data;
        snprintf(rac->errstr, sizeof(rac->errstr), "send stats to memcached failed, ip[%s], port[%u]: %s", server->ip, server->port, strerror(errno));
        memcachedQueryStateDoneCb(rac, rac->loop);
        rawAsyncDisConnect(rac);
        return;
    }

    stMemcachedPacket *res = req; // resused 
    res->pcur = (char*)&res->hdr;
    res->left = sizeof(res->hdr);
    res->body = NULL;
    res->bufferLeft = BUF_LEN;
    res->readState = MEMCACHED_READ_HEADER;
    rawAsyncEventDelete(rac, EV_WRITE);
    rawAsyncEventAdd(rac, EV_READ, memcachedReadStatsResponse, res);
    return;
}

static void memcachedReadStatsResponse (stRawAsyncContext *rac, void *privdata) {
    stMemcachedPacket *res = (stMemcachedPacket*)privdata;
    rac->err = 0;
    int nread = 0;
    while (res->left) {
        if ((nread = read(rac->fd, res->pcur, res->left)) < 0) {
            if (errno == EAGAIN) {
                return;
            } else if ( errno == EINTR) {
                continue;
            } else {
                // error
                rac->err = errno;
                snprintf(rac->errstr, sizeof(rac->errstr), "read responser header failed: %s", strerror(errno));
                goto done;
            }
        } else if (nread == 0) {
            // closed
            rac->err = -1;
            snprintf(rac->errstr, sizeof(rac->errstr), "server closed the connection");
            goto done;
        } else {
            // read sth
            res->left -= nread;
            res->pcur += nread;
        }
    }
    stMemcachedCommandHeader *hdr = &res->hdr;
    switch (res->readState) {
        case MEMCACHED_READ_HEADER:
            // header done
            hdr->keyLen = ntohs(hdr->keyLen);
            hdr->totalBodyLen = ntohl(hdr->totalBodyLen);
            if (hdr->totalBodyLen == 0) {
                // stats done
                res->buffer[BUF_LEN - res->bufferLeft] = '\0';
//                log_notice("memcached ip[%s] port[%u], stat: %s", server->ip, server->port, res->buffer);
                logMonitorInfo(rac);
                rawAsyncEventDelete(rac, EV_READ);
                goto done;
            }
            if (hdr->totalBodyLen + 3 >= res->bufferLeft) { // 3 is for '=' ' 'and '\0'
                rac->err = -1;
                snprintf(rac->errstr, sizeof(rac->errstr), "no space left for buffer the result");
                goto done;
            }
            res->left = hdr->keyLen;
            res->pcur = &(res->buffer[BUF_LEN - res->bufferLeft]);
            res->readState = MEMCACHED_READ_KEY;
            break;
        case MEMCACHED_READ_KEY:
            *res->pcur = '=';
            res->pcur ++;
            res->bufferLeft -= (res->hdr.keyLen + 1);
            res->left = (res->hdr.totalBodyLen - res->hdr.keyLen);
            res->readState = MEMCACHED_READ_VALUE;
            break;
        case MEMCACHED_READ_VALUE:
            *res->pcur = ' ';
            res->pcur = (char*)&res->hdr;
            res->bufferLeft -= (res->hdr.totalBodyLen - res->hdr.keyLen + 1);
            res->left = sizeof(res->hdr);
            res->readState = MEMCACHED_READ_HEADER;
            break;
        default:
            rac->err = -1;
            snprintf(rac->errstr, sizeof(rac->errstr), "not supported in STATS");
            goto done;
            break;
    }
    return;

done:
    memcachedQueryStateDoneCb(rac, rac->loop);
    if (rac->err) 
        rawAsyncDisConnect(rac);
}

static void logMonitorInfo(stRawAsyncContext *rac) {
    stRACData *racData = (stRACData*)rac->data;
    stMonitorInfo *now = racData->now;
    stMonitorInfo *last = racData->last;

    int uptime = 0;
    char version[64] = {0};
    char buildinfo[128] = {0};
    long long bytes = 0;
    long long limitMaxBytes = 0;
    int slabReassignRunning = 0;

    char *stats = racData->pkt->buffer;
    char *keyStart, *keyEnd, *valueStart, *valueEnd;
    keyStart = stats;
    while ((keyEnd = strchr(keyStart, '='))) {
        *keyEnd = '\0';
        valueStart = keyEnd + 1;
        if ((valueEnd = strchr(valueStart, ' ')) == NULL) {
            log_warning("invalid stats result");
            return;
        }
        *keyEnd = '\0';
        if (strcmp(keyStart, "time") == 0) {
            now->time = atoi(valueStart);
        } else if (strcmp(keyStart, "uptime") == 0) {
            uptime = atoi(valueStart);
        } else if (strcmp(keyStart, "version") == 0) {
            snprintf(version, sizeof(version), "%.*s", (int)(valueEnd - valueStart), valueStart);
        } else if (strcmp(keyStart, "buildinfo") == 0) {
            snprintf(buildinfo, sizeof(buildinfo), "%.*s", (int)(valueEnd - valueStart), valueStart);
        } else if (strcmp(keyStart, "curr_connections") == 0) {
            now->currConns = atoll(valueStart);
        } else if (strcmp(keyStart, "total_connections") == 0) {
            now->totalConns = atoll(valueStart);
        } else if (strcmp(keyStart, "rusage_user") == 0) {
            now->rusageUser = atof(valueStart);
        } else if (strcmp(keyStart, "rusage_system") == 0) {
            now->rusageSystem = atof(valueStart);
        } else if (strcmp(keyStart, "curr_items") == 0) {
            now->currItems = atoll(valueStart);
        } else if (strcmp(keyStart, "cmd_get") == 0) {
            now->cmdGets = atoll(valueStart);
        } else if (strcmp(keyStart, "cmd_set") == 0) {
            now->cmdSets = atoll(valueStart);
        } else if (strcmp(keyStart, "get_hits") == 0) {
            now->getHits = atoll(valueStart);
        } else if (strcmp(keyStart, "get_misses") == 0) {
            now->getMiss = atoll(valueStart);
        } else if (strcmp(keyStart, "delete_hits") == 0) {
            now->deleteHits = atoll(valueStart);
        } else if (strcmp(keyStart, "delete_misses") == 0) {
            now->deleteMiss = atoll(valueStart);
        } else if (strcmp(keyStart, "bytes_read") == 0) {
            now->bytesRead = atoll(valueStart);
        } else if (strcmp(keyStart, "bytes_written") == 0) {
            now->bytesWrite = atoll(valueStart);
        } else if (strcmp(keyStart, "listen_disabled_num") == 0) {
            now->listenDisabledNum = atoll(valueStart);
        } else if (strcmp(keyStart, "conn_yields") == 0) {
            now->connYields = atoll(valueStart);
        } else if (strcmp(keyStart, "expired_unfetched") == 0) {
            now->expiredUnfetched = atoll(valueStart);
        } else if (strcmp(keyStart, "evicted_unfetched") == 0) {
            now->evictedUnfetched = atoll(valueStart);
        } else if (strcmp(keyStart, "evictions") == 0) {
            now->evictions = atoll(valueStart);
        } else if (strcmp(keyStart, "reclaimed") == 0) {
            now->reclaimed = atoll(valueStart);
        } else if (strcmp(keyStart, "bytes") == 0) {
            bytes = atoll(valueStart);
        } else if (strcmp(keyStart, "limit_maxbytes") == 0) {
            limitMaxBytes = atoll(valueStart);
        } else if (strcmp(keyStart, "slab_reassign_running") == 0) {
            slabReassignRunning = atoi(valueStart);
        } else if (strcmp(keyStart, "slabs_moved") == 0) {
            now->slabsMoved = atoll(valueStart);
        } else {
            // not need
        }
        keyStart = valueEnd + 1;
    }
    // ok, do it
    int interval = now->time - last->time;
    if (last->time == 0) {
        log_notice("first stat for server[%s]", racData->server->serverIdKey);
    } else if (interval < 0) {
        log_warning("invalid time from server[%s], now[%d], last[%d]", racData->server->serverIdKey, now->time, last->time);
        return;
    } else if (interval <= 1) {
        log_notice("too frequent stat for server[%s], now[%d], last[%d]", racData->server->serverIdKey, now->time, last->time);
    } else {
        racData->compute->currConns = now->currConns;
        racData->compute->totalConns = (now->totalConns - last->totalConns)/interval;
        racData->compute->rusageUser = (now->rusageUser - last->rusageUser)/interval;
        racData->compute->rusageSystem = (now->rusageSystem - last->rusageSystem)/interval;
        racData->compute->currItems = now->currItems;
        racData->compute->cmdGets = (now->cmdGets - last->cmdGets);
        racData->compute->cmdSets = (now->cmdSets - last->cmdSets);
        racData->compute->getHits = (now->getHits - last->getHits);
        racData->compute->getMiss = (now->getMiss - last->getMiss);
        racData->compute->deleteHits = (now->deleteHits - last->deleteHits);
        racData->compute->deleteMiss = (now->deleteMiss - last->deleteMiss);
        racData->compute->bytesRead = (now->bytesRead - last->bytesRead);
        racData->compute->bytesWrite = (now->bytesWrite - last->bytesWrite);
        racData->compute->listenDisabledNum = (now->listenDisabledNum - last->listenDisabledNum);
        racData->compute->connYields = (now->connYields - last->connYields);
        racData->compute->expiredUnfetched = (now->expiredUnfetched - last->expiredUnfetched);
        racData->compute->evictedUnfetched = (now->evictedUnfetched - last->evictedUnfetched);
        racData->compute->evictions = (now->evictions - last->evictions);
        racData->compute->reclaimed = (now->reclaimed - last->reclaimed);
        racData->compute->slabsMoved = (now->slabsMoved - last->slabsMoved);

        stServerProperty *p = &racData->serverInfo->serverProperty;
        stMonitorInfo *c = racData->compute;
        log_notice("STATS: product=ksarch-memcached subsys=%s module=dmonitor logid=%u local_ip=%s "
            "engine=memcached pid=%s cluster=%s ip=%s port=%d "
            "uptime=%ld version=%s buildinfo=%s curr_connections=%ld total_connections_delta=%ld "
            "rusage_user=%.3f rusage_system=%.3f rusage_total=%.3f "
            "curr_items=%ld "
            "cmd_get=%ld cmd_set=%ld cmd_delete=%ld "
            "get_hits=%ld get_misses=%ld get_hitrate=%.3f "
            "delete_hits=%ld delete_misses=%ld delete_hitrate=%.3f "
            "bytes_read=%ld bytes_written=%ld listen_disabled_num=%ld "
            "conn_yields=%ld expired_unfetched=%ld evicted_unfetched=%ld "
            "evictions=%ld reclaimed=%ld "
            "bytes=%ld limit_maxbytes=%ld mem_rate=%.3f "
            "slab_reassign_running=%d slabs_moved=%d "
            "interval=%d ", 
            p->pid, rand() & 0x7fffffff, racData->server->ip,
            p->pid, p->cluster, racData->server->ip, racData->server->port, 
            uptime, version, buildinfo, c->currConns, c->totalConns, 
            c->rusageUser < 0 ? 0 : c->rusageUser,
            c->rusageSystem < 0 ? 0 : c->rusageSystem,
            c->rusageUser < 0 || c->rusageSystem < 0 ? 0 : c->rusageUser + c->rusageSystem,
            c->currItems, 
            c->cmdGets < 0 ? 0 : c->cmdGets, 
            c->cmdSets < 0 ? 0 : c->cmdSets, 
            c->deleteHits < 0 || c->deleteMiss < 0 ? 0 : c->deleteHits + c->deleteMiss,
            c->getHits < 0 ? 0 : c->getHits, 
            c->getMiss < 0 ? 0 : c->getMiss, 
            (c->cmdGets <= 0 ? 0 : (float)c->getHits / c->cmdGets),
            c->deleteHits < 0 ? 0 : c->deleteHits, 
            c->deleteMiss < 0 ? 0 : c->deleteMiss, 
            (c->deleteHits < 0 || c->deleteMiss < 0 || c->deleteHits + c->deleteMiss <=0 ? 0 : (float)c->deleteHits/(c->deleteHits+c->deleteMiss)),
            c->bytesRead < 0 ? 0 : c->bytesRead, 
            c->bytesWrite < 0 ? 0 : c->bytesWrite, 
            c->listenDisabledNum < 0 ? 0 : c->listenDisabledNum,
            c->connYields < 0 ? 0 : c->connYields,
            c->expiredUnfetched < 0 ? 0 : c->expiredUnfetched,
            c->evictedUnfetched < 0 ? 0 : c->evictedUnfetched,
            c->evictions < 0 ? 0 : c->evictions,
            c->reclaimed < 0 ? 0 : c->reclaimed,
            bytes, limitMaxBytes,
            (limitMaxBytes < 0 ? 0 : (float)bytes / limitMaxBytes),
            slabReassignRunning, c->slabsMoved,
            interval);
    }

    racData->now = last;
    racData->last = now;
}






















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
