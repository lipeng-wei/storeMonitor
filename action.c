/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file action.c
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/19 17:18:55
 * @brief 
 *  
 **/

#include <sys/types.h>
#include <sys/wait.h>

#include "action.h"
#include "log.h"
#include "meta.h"
#include "async.h"
#include "connection.h"
#include "config.h"

stAction action;

int actionInit(EV_P) {
    action.loop = loop;
    action.downServerList = NULL;
    action.actionEnable = 0;
    action.checkActionEnableOption = ACTION_ENABLE_OPTION_CHECK;

    return 0;
}

void freeDownServer(stDownServer *s) {
    sdsfree(s->ip_port_type);
    sdsfree(s->ip);
    sdsfree(s->type);
    sdsfree(s->serverProperty.backupIp);
    sdsfree(s->serverProperty.backupType);
    sdsfree(s->serverProperty.pid);
    sdsfree(s->serverProperty.cluster);
    free(s);
}

int addServerToDownList (sds ip_port_type) {
    log_debug("add serverIdKey to downServerList, server[%s]", ip_port_type);
    stDownServer *server;
    HASH_FIND_STR(action.downServerList, ip_port_type, server);
    if (server) {
        log_debug("server already in downlist, server[%s]", ip_port_type);
        return 0;
    }
    server = (stDownServer*)calloc(1, sizeof(stDownServer));
    if (server == NULL) {
        log_warning("oom for calloc");
        exit(-1);
    }
    server->ip_port_type = sdsnew(ip_port_type);
    int count = 0;
    sds *tokens = sdssplitlen(ip_port_type, sdslen(ip_port_type), "_", 1, &count);
    if (count != 3) {
        log_warning("error in sdssplitlen for server[%s]", ip_port_type);
        int i;
        for (i = 0; i < count; i ++) 
            sdsfree(tokens[i]);
        return -1;
    }
    server->ip = sdsnew(tokens[0]);
    server->port = atoi(tokens[1]);
    server->type = sdsnew(tokens[2]);
    server->exchangeState.state = EXCHANGE_STATE_EXCHANGE;
    server->exchangeState.toBeUpdated = 1;
    int i;
    for (i = 0; i < count; i ++) 
        sdsfree(tokens[i]);

    HASH_ADD_KEYPTR(hh, action.downServerList, server->ip_port_type, strlen(server->ip_port_type), server);
    log_debug("add one server to downServerList, ip[%s] port[%u] type[%s]", server->ip, server->port, server->type);
    return 0;
}

int rmServerFromDownList (sds ip_port_type) {
    stDownServer *s;
    HASH_FIND_STR(action.downServerList, ip_port_type, s);
    if (s) {
        if (s->exchangeState.state != EXCHANGE_STATE_EXCHANGE) {
            log_warning("downserver is recovered, but exchange started, can't remove from downServerList: downServer[%s]", ip_port_type);
            return -1;
        }
        HASH_DEL(action.downServerList, s);
        freeDownServer(s);
        log_trace("downServer removed from downServerList, downServer[%s]", ip_port_type);
    } else {
        log_warning("downServer already removed from downServerList, downServer[%s]", ip_port_type);
    }
    return 0;
}

void *actionThread(void *arg) {
    stDownServer *server = (stDownServer*)arg;
    log_notice("start exchange for downServer[%s]: role[%d] backupIp[%s] backupPort[%u] backupType[%s] pid[%s]", server->ip_port_type, server->serverProperty.role, server->serverProperty.backupIp, server->serverProperty.backupPort, server->serverProperty.backupType, server->serverProperty.pid);
    char command[512] = {0};
    snprintf(command, sizeof(command), "%s/%s/%s %s %s %s", 
            config.scriptsDir, server->type, config.exchangeScriptFile,
            server->serverProperty.pid, server->ip, server->serverProperty.backupIp);
    log_notice("exec command: %s", command);
    int err = system(command);
    if (err == -1) {
        log_warning("system fail, err[%d]", err);
        server->exchangeState.state = EXCHANGE_STATE_EXCHANGE;
    } else {
        int exitCode = WEXITSTATUS(err);
        if (exitCode == 0) {
            log_notice("exchange done succussfully");
            server->exchangeState.state = EXCHANGE_STATE_EXCHANGED;
            server->exchangeState.toBeUpdated = 1;
        } else {
            // repeat?
            log_warning("exchange fail, exit code[%d]", exitCode);
            server->exchangeState.state = EXCHANGE_STATE_EXCHANGE;
        } 
    }
    return NULL;
}

void startExchangeForServer(EV_P_ stDownServer *server) {
    int err;
    if (err=pthread_create(&server->actionThreadId, NULL, actionThread, server)) {
        char errstr[128] = {0};
        log_warning("create actionThread for downServer fail, ip[%s] port[%u] type[%s]: %s",
                server->ip, server->port, server->type, strerror_r(err, errstr, sizeof(errstr)));
        return;
    }
    server->exchangeState.state = EXCHANGE_STATE_EXCHANGING;
}

int updateDownServerInfo(stDownServer *downServer, redisReply *reply) {
    if (!downServer->exchangeState.toBeUpdated) {
        return 0;
    } else {
        {

        {
            // set pid
            int i = 0;
            while(strcmp("pid", reply->element[i]->str)) {
                i += 2;
                if (i>=reply->elements) 
                    break;
            }
            if (i >= reply->elements) {
                log_warning("no pid found in meta server for downserver[%s]", downServer->ip_port_type);
                return -1;
            }
            i ++;
            if (reply->element[i]->type != REDIS_REPLY_STRING) {
                log_warning("pid value in meta server is not string, downserver[%s] valuetype[%d]", downServer->ip_port_type, reply->element[i]->type);
                return -1;
            }
            downServer->serverProperty.pid = sdsnew(reply->element[i]->str);
            log_debug("see downserver[%s] pid[%s]", downServer->ip_port_type, downServer->serverProperty.pid);
        }

        {
            // set cluster
            int i = 0;
            while(strcmp("cluster", reply->element[i]->str)) {
                i += 2;
                if (i>=reply->elements) 
                    break;
            }
            if (i < reply->elements) {
                i ++;
                if (reply->element[i]->type != REDIS_REPLY_STRING) {
                    log_warning("cluster value in meta server is not string, downserver[%s] valuetype[%d]", downServer->ip_port_type, reply->element[i]->type);
                    return -1;
                }
                downServer->serverProperty.cluster = sdsnew(reply->element[i]->str);
                log_debug("see downserver[%s] cluster[%s]", downServer->ip_port_type, downServer->serverProperty.cluster);
            }
        }

        {
            // set role
            int i = 0;
            while(strcmp("role", reply->element[i]->str)) {
                i += 2;
                if (i>=reply->elements) 
                    break;
            }
            if (i >= reply->elements) {
                log_warning("no role found in meta server for downserver[%s]", downServer->ip_port_type);
                return -1;
            }
            i ++;
            if (reply->element[i]->type != REDIS_REPLY_STRING) {
                log_warning("role value in meta server is not string, downserver[%s] vauletype[%d]", downServer->ip_port_type, reply->element[i]->type);
                return -1;
            }

            if (strcmp("master", reply->element[i]->str) == 0) {
                downServer->serverProperty.role = DOWNSERVER_ROLE_MASTER;
            } else if (strcmp("slave", reply->element[i]->str) ==0) {
                downServer->serverProperty.role = DOWNSERVER_ROLE_SLAVE;
            } else {
                log_warning("unknown downserver role in meta, server[%s] role[%s]", downServer->ip_port_type, reply->element[i]->str);
                return -1;
            }
            log_debug("see downserver[%s] role[%d]", downServer->ip_port_type, downServer->serverProperty.role);
        }
        }
        if (downServer->serverProperty.role != DOWNSERVER_ROLE_MASTER) {
            log_notice("downserver is NOT master, server[%s] role[%d]", downServer->ip_port_type, downServer->serverProperty.role);
            // TODO
            // may do sth else; but now, we don't deal with this case.
            downServer->exchangeState.toBeUpdated = 0;
            return 0;
        }

        {
            // set new master
            int i = 0;
            while(strcmp("backup_machine", reply->element[i]->str)) {
                i += 2;
                if (i>=reply->elements) 
                    break;
            }
            if (i >= reply->elements) {
                log_warning("no backup_machine found in meta server for downserver[%s]", downServer->ip_port_type);
                return -1;
            }
            i ++;
            if (reply->element[i]->type != REDIS_REPLY_STRING) {
                log_warning("backup_machine value in meta server is not string, downserver[%s] valuetype[%d]", downServer->ip_port_type, reply->element[i]->type);
                return -1;
            }
            stDownServer *bkserver;
            HASH_FIND_STR(action.downServerList, reply->element[i]->str, bkserver);
            if (bkserver) {
                log_warning("backup_machine is DOWN too, no exchange performed: downserver[%s] backup_machine[%s]", downServer->ip_port_type, reply->element[i]->str);
                return -1;
            }
            int count;
            int j;
            sds *tokens = sdssplitlen(reply->element[i]->str, sdslen(reply->element[i]->str), "_", strlen("_"), &count);
            if (count != 3) {
                log_warning("backup_machine value in meta server is invalid, downserver[%s] value[%s]", downServer->ip_port_type, reply->element[i]->str);
                for (j = 0; j < count; j ++)
                    sdsfree(tokens[j]);
                return -1;
            }
            downServer->serverProperty.backupIp = sdsnew(tokens[0]);
            downServer->serverProperty.backupPort = atoi(tokens[1]);
            downServer->serverProperty.backupType = sdsnew(tokens[2]);
            for (j = 0; j < count; j ++)
                sdsfree(tokens[j]);
                log_debug("see downserver[%s] backupIp[%s] backupPort[%u] backupType[%s]", downServer->ip_port_type, downServer->serverProperty.backupIp, downServer->serverProperty.backupPort, downServer->serverProperty.backupType);
        }

        // done
        downServer->exchangeState.toBeUpdated = 0;
    }
    return 0;
}

void updateExchangeStateDone(redisAsyncContext *ac, void *reply, void *privdata) {
    stDownServer *downServer = (stDownServer*)privdata;
    stServerInfo *metaServer = (stServerInfo*)ac->data;
    if (downServer->isNoUse == 1) {
        log_warning("updateDownServerProperty fail [isNoUse == 1], downServer[%s]", downServer->ip_port_type);
        freeDownServer(downServer);
        return 0;
    }
    int updateInfoSuccess = 0;
    if (ac->err) {
        log_warning("communicate with metaserver fail, ip[%s] port[%u]", metaServer->ip, metaServer->port);
        // will put back to unconnected queue by onDisConnect;
    } else {
        if (reply == NULL) {
            // connection will be closed; onDisConnect will be called; do nothing
        } else {
            redisReply *r = reply;
            switch(r->type) {
                case REDIS_REPLY_NIL:
                    log_warning("meta server return NIL");
                    break;
                case REDIS_REPLY_ERROR:
                    log_warning("meta server return ERROR: %s", r->str);
                    break;
                case REDIS_REPLY_STATUS:
                    log_warning("meta server return STATUS: %s", r->str);
                    break;
                case REDIS_REPLY_INTEGER:
                    log_warning("meta server return INTEGER: %lld", r->integer);
                    break;
                case REDIS_REPLY_STRING:
                    log_warning("meta server return STRING: %s", r->str);
                    break;
                case REDIS_REPLY_ARRAY:
                    if (r->elements == 0) {
                        log_warning("meta server return empty array");
                    } else if(updateDownServerInfo(downServer, r) == 0) {
                        updateInfoSuccess = 1;
                    } else {
                        log_warning("downServer info is invalid, downServer[%s]", downServer->ip_port_type);
                    }
                    break;
                default:
                    log_warning("unknow redisReply type[%d]", r->type);
                    break;
                }
            if (updateInfoSuccess) {
                log_trace("updateDownServerProperty success, downServer[%s]", downServer->ip_port_type);
            } else {
                log_warning("updateDownServerProperty fail, downServer[%s]", downServer->ip_port_type);
            }
            // addServerToServerQueue(metaServer, &connectionPool.metaServerQueue[metaServer->queueId]);
        }
    }
}

void updateExchangeState(EV_P_ stDownServer *server) {
    stServerInfo *metaServer = fetchMetaServerForRead();
    if (metaServer == NULL) {
        log_warning("no ivalid meta server to updateExchangeState");
        return;
    }

    int err;
    err = redisAsyncCommand((redisAsyncContext*)metaServer->connectionContext, updateExchangeStateDone, server, "HGETALL %s%s", server->ip_port_type, SERVER_INFO_SUFFIX);
    if (err != REDIS_OK) {
        log_warning("fail to asyncCommand to server ip[%s] port[%u]", metaServer->ip, metaServer->port);
        // addServerToServerQueue(metaServer, &connectionPool.metaServerQueue[metaServer->queueId]);
        return;
    }
    return;

}

void updateExchangingState(EV_P_ stDownServer *server) {
}

int checkUpdateExchangedStateResult(stServerInfo *metaServer, stDownServer *downServer, redisReply *reply) {
    // between (multi, exec)
    if (reply->elements == 0) {
        log_warning("empty result array");
        return -1;
    }

    int cmdIndex = 0;
    int ret = 0;
    for (; cmdIndex < reply->elements && ret == 0; cmdIndex ++) {
        redisReply *r = reply->element[cmdIndex];
        switch(cmdIndex) {
            case 0:
                if (r->type != REDIS_REPLY_INTEGER) {
                    log_warning("check updateExchangedState result fail, wrong type: metaServer[%s], downServer[%s], cmdIndex[%d], replytype[%d]", 
                            metaServer->serverIdKey, downServer->ip_port_type, cmdIndex,
                            r->type);
                    ret = -1;
                } else if (r->integer != 0 && r->integer != 1) {
                    log_warning("check updateExchangedState result fail, wrong value: metaServer[%s], downServer[%s], cmdIndex[%d], replyvalue[%d]", 
                            metaServer->serverIdKey, downServer->ip_port_type, cmdIndex,
                            r->integer);
                    ret = -1;
                }
                break;
            default:
                log_warning("check updateExchangedState result fail, wrong cmdIndex: metaServer[%s], downServer[%s], cmdIndex[%d]", 
                        metaServer->serverIdKey, downServer->ip_port_type, cmdIndex);
                ret = -1;
                break;
        }
    }
    return ret;
}

void updateExchangedStateDone(redisAsyncContext *ac, void *reply, void *privdata) {
    stDownServer *downServer = (stDownServer*)privdata;
    stServerInfo *metaServer = (stServerInfo*)ac->data;
    if (ac->err) {
        log_warning("communicate with metaserver fail, ip[%s] port[%u]", metaServer->ip, metaServer->port);
        // will put back to unconnected queue by onDisConnect;
    } else {
        if (reply == NULL) {
            // connection will be closed; onDisConnect will be called; do nothing
        } else {
            log_debug("transaction for updateExchangedState, metaServer[%s],downServer[%s]: EXEC", metaServer->serverIdKey, downServer->ip_port_type);
            redisReply *r = reply;
            int updateSuccess = 0;
            switch(r->type) {
                case REDIS_REPLY_NIL:
                    log_warning("meta server return NIL");
                    break;
                case REDIS_REPLY_ERROR:
                    log_warning("meta server return ERROR: %s", r->str);
                    break;
                case REDIS_REPLY_STATUS:
                    log_warning("meta server return STATUS: %s", r->str);
                    break;
                case REDIS_REPLY_INTEGER:
                    log_warning("meta server return INTEGER: %lld", r->integer);
                    break;
                case REDIS_REPLY_STRING:
                    log_warning("meta server return STRING: %s", r->str);
                    break;
                case REDIS_REPLY_ARRAY:
                    log_debug("meta server return ARRAY");
                    if (checkUpdateExchangedStateResult(metaServer, downServer, r) == 0) {
                        updateSuccess = 1;
                    }
                    break;
                default:
                    log_warning("unknow redisReply type[%d]", r->type);
                    break;
                }
            if (updateSuccess) {
                log_notice("updateExchangedState success, metaServer[%s],downServer[%s]", metaServer->serverIdKey, downServer->ip_port_type);
            } else {
                log_warning("updateExchangedState fail, metaServer[%s],downServer[%s]", metaServer->serverIdKey, downServer->ip_port_type);
                downServer->exchangeState.toBeUpdated = 1;
            }
            // addServerToServerQueue(metaServer, &connectionPool.metaServerQueue[metaServer->queueId]);
        }
    }
}
void updateExchangedStateTransStart(redisAsyncContext *ac, void *reply, void *privdata) {
    stDownServer *downServer = (stDownServer*)privdata;
    stServerInfo *metaServer = (stServerInfo*)ac->data;
    if (ac->err) {
        log_warning("communicate with metaserver fail, ip[%s] port[%u]", metaServer->ip, metaServer->port);
        // will put back to unconnected queue by onDisConnect;
    } else {
        if (reply == NULL) {
            // connection will be closed; onDisConnect will be called; do nothing
        } else {
            redisReply *r = reply;
            if (r->type != REDIS_REPLY_STATUS 
                    || strcmp(r->str, "OK")) {
                log_warning("start transaction for updateExchangedState fail");
                resetConnectionForServer(action.loop, metaServer);
            } else {
                log_debug("transaction for updateExchangedState, metaServer[%s],downServer[%s]: START", metaServer->serverIdKey, downServer->ip_port_type);
                // don't putback
            }
        }
    }
}

void updateExchangedStateInTrans(redisAsyncContext *ac, void *reply, void *privdata) {
    stDownServer *downServer = (stDownServer*)privdata;
    stServerInfo *metaServer = (stServerInfo*)ac->data;
    if (ac->err) {
        log_warning("communicate with metaserver fail, ip[%s] port[%u]", metaServer->ip, metaServer->port);
        // will put back to unconnected queue by onDisConnect;
    } else {
        if (reply == NULL) {
            // connection will be closed; onDisConnect will be called; do nothing
        } else {
            redisReply *r = reply;
            if (r->type != REDIS_REPLY_STATUS 
                    || strcmp(r->str, "QUEUED")) {
                log_warning("transaction for updateExchangedState fail");
                resetConnectionForServer(action.loop, metaServer);
            } else {
                log_debug("transaction for updateExchangedState, metaServer[%s],downServer[%s]: QUEUED", metaServer->serverIdKey, downServer->ip_port_type);
                // don't putback
            }
        }
    }
}

void updateExchangedState(EV_P_ stDownServer *server) {
    stServerInfo *metaServer = fetchMetaServerForWrite();
    if (metaServer == NULL) {
        log_warning("no ivalid meta server for write to updateExchangedState");
        return;
    }
    
    int err;
    err = redisAsyncCommand((redisAsyncContext*)metaServer->connectionContext, updateExchangedStateTransStart, server, "MULTI");
    if (err != REDIS_OK) {
        log_warning("fail to asyncCommand to server ip[%s] port[%u]", metaServer->ip, metaServer->port);
        // addServerToServerQueue(metaServer, &connectionPool.metaServerQueue[metaServer->queueId]);
        return;
    }
    // move old master to exchanged-server-list
    err = redisAsyncCommand((redisAsyncContext*)metaServer->connectionContext, updateExchangedStateInTrans, server, 
            "SMOVE %s%s %s%s %s", config.monitorGroup, MONITOR_SERVER_LIST_SUFFIX, config.monitorGroup, EXCHANGED_SERVER_LIST_SUFFIX, server->ip_port_type);
    if (err != REDIS_OK) {
        log_warning("fail to asyncCommand to server ip[%s] port[%u]", metaServer->ip, metaServer->port);
        // addServerToServerQueue(metaServer, &connectionPool.metaServerQueue[metaServer->queueId]);
        return;
    }
    // TODO:
    // modify old master property.enabled: 1 to 0
    // modify new master property.role: "slave" to "master"
    err = redisAsyncCommand((redisAsyncContext*)metaServer->connectionContext, updateExchangedStateDone, server, "EXEC");
    if (err != REDIS_OK) {
        log_warning("fail to asyncCommand to server ip[%s] port[%u]", metaServer->ip, metaServer->port);
        // addServerToServerQueue(metaServer, &connectionPool.metaServerQueue[metaServer->queueId]);
        return;
    }
    server->exchangeState.toBeUpdated = 0;
    return;
}

void doActionToDownServer(EV_P_ stDownServer *server) {
    switch (server->exchangeState.state) {
        case EXCHANGE_STATE_EXCHANGE:
            if (server->exchangeState.toBeUpdated) {
                updateExchangeState(EV_A_ server);
            } else {
                if (server->serverProperty.role == DOWNSERVER_ROLE_MASTER) {
                    startExchangeForServer(EV_A_ server);
                } else {
                    log_trace("down server is SLAVE, do nothing, server[%s]", server->ip_port_type);
                }
            }
            break;
        case EXCHANGE_STATE_EXCHANGING:
            log_debug("server is exchanging, server[%s] tobeupdated[%d]", server->ip_port_type, server->exchangeState.toBeUpdated);
            if (server->exchangeState.toBeUpdated) {
                updateExchangingState(EV_A_ server);
            }
            break;
        case EXCHANGE_STATE_EXCHANGED:
            if (server->exchangeState.toBeUpdated) {
                log_debug("server exchanged done, server[%s] tobeupdated[%d]", server->ip_port_type, server->exchangeState.toBeUpdated);
                updateExchangedState(EV_A_ server);
            } else {
                log_debug("server exchanged done && update already beginning, so wait to be removed from down list: server[%s] tobeupdated[%d]", server->ip_port_type, server->exchangeState.toBeUpdated);
            }

            break;
        default:
            log_warning("unknow exchange state, server[%s] exchangestate[%d]", server->ip_port_type, server->exchangeState.state);
            break;
    }
}

void checkActionEnableOptionDone(redisAsyncContext *ac, redisReply *reply, void *privdata) {
    stServerInfo *metaServer = (stServerInfo*)ac->data;
    if (ac->err) {
        log_warning("communicate with metaserver fail, ip[%s] port[%u]", metaServer->ip, metaServer->port);
        action.checkActionEnableOption = ACTION_ENABLE_OPTION_CHECK;
        // will put back to unconnected queue by onDisConnect;
    } else {
        if (reply == NULL) {
            // connection will be closed; onDisConnect will be called; do nothing
            action.checkActionEnableOption = ACTION_ENABLE_OPTION_CHECK;
        } else {
            redisReply *r = reply;
            if (r->type == REDIS_REPLY_NIL) {
                log_warning("no %s_action_enable set, use default 0", config.monitorGroup);
                action.actionEnable = 0;
            } else if (r->type == REDIS_REPLY_STRING) {
                if (strcmp("0", r->str) == 0) {
                    action.actionEnable = 0;
                } else if (strcmp("1", r->str) == 0) {
                    action.actionEnable = 1;
                } else {
                    log_warning("invalid value return from metaserver for %s_action_enable: value[%s], use default 0", config.monitorGroup, r->str);
                    action.actionEnable = 0;
                }
            } else {
                log_warning("invalid value return from metaserver for %s_action_enable: type not string, use default 0", config.monitorGroup);
                action.actionEnable = 0;
            }
            
            action.checkActionEnableOption = ACTION_ENABLE_OPTION_CHECKED;
            // addServerToServerQueue(metaServer, &connectionPool.metaServerQueue[metaServer->queueId]);

            if (action.actionEnable == 1) {
                log_trace("action is enabled, begin action for downservers");
                stDownServer *s, *tmp;
                HASH_ITER(hh, action.downServerList, s, tmp) {
                    doActionToDownServer(action.loop, s);
                }
            } else {
                log_notice("%s_action_enable is 0, so no action to be done", config.monitorGroup);
            }

            action.actionEnable = 0;
            action.checkActionEnableOption = ACTION_ENABLE_OPTION_CHECK;
        }
    }
}

int checkActionEnableOption(EV_P) {
    log_debug("begin to check action_enable option...");
    stServerInfo *metaServer = fetchMetaServerForRead();
    if (metaServer == NULL) {
        log_warning("no ivalid meta server for checkActionEnableOption");
        return;
    }
    
    int err;
    err = redisAsyncCommand((redisAsyncContext*)metaServer->connectionContext, checkActionEnableOptionDone, NULL, "GET %s_action_enable", config.monitorGroup);
    if (err != REDIS_OK) {
        log_warning("fail to asyncCommand to server ip[%s] port[%u]", metaServer->ip, metaServer->port);
        // addServerToServerQueue(metaServer, &connectionPool.metaServerQueue[metaServer->queueId]);
        return;
    }

    action.checkActionEnableOption = ACTION_ENABLE_OPTION_CHECKING;
}

void actionCheckTimerCb(EV_P_ ev_timer *timer, int revents) {
    log_debug("in actionCheckTimerCb");
    if (action.checkActionEnableOption == ACTION_ENABLE_OPTION_CHECK) {
        checkActionEnableOption(EV_A);
    }
}
















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
