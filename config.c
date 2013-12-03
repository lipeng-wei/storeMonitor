/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file config.cpp
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/04 15:11:51
 * @brief 
 *  
 **/

#include <stdio.h>
#include "config.h"
#include "sds.h"
#include "log.h"
#include "connection.h"

stServerConfig config;

void initConfig() {
    // timer interval
    config.metaUpdateInterval = 5;
    config.connectionRepairInterval = 1; // default
    config.monitorInterval = 3.0; // default
    config.actionCheckInterval = 3;

    // timeout
    config.rwTimeoutSec = 0.5; // double
    config.serverStateExpireTime = 15;

    config.serverDownQuota = 3;

}

void loadConfig(char *filename) {
    FILE *fp;
    char buf[CONFIGLINE_MAX+1], *err = NULL;
    int linenum = 0;
    char *line = NULL;

    if (filename[0] == '-' && filename[1] == '\0')
        fp = stdin;
    else {
        if ((fp = fopen(filename,"r")) == NULL) {
            fprintf(stderr, "Fatal error, can't open config file '%s'\n", filename);
            exit(1);
        }
    }

    while(fgets(buf,CONFIGLINE_MAX+1,fp) != NULL) {
        sds *argv;
        int argc, j;

        linenum++;
        line = sdsnew(buf);
        line = sdstrim(line," \t\r\n");

        /* Skip comments and blank lines*/
        if (line[0] == '#' || line[0] == '\0') {
            sdsfree(line);
            continue;
        }

        /* Split into arguments */
        argv = sdssplitargs(line,&argc);
        sdstolower(argv[0]);

        /* Execute config directives */
        if (!strcasecmp(argv[0],"loglevel") && argc == 2) {
            config.logLevel = atoi(argv[1]);
        } else if (!strcasecmp(argv[0],"port") && argc == 2) {
            config.port = atoi(argv[1]);
            if (config.port < 0 || config.port > 65535) {
                err = "Invalid port"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"type") && argc >=2) {
            config.typeStr = (sds*)malloc(sizeof(sds)*(argc-1));
            if (config.typeStr == NULL) {
                err = "oom in malloc"; goto loaderr;
            }
            int i;
            config.typeCount = 0;
            for (i = 1; i < argc; i ++) {
               config.typeStr[i-1] = sdsdup(argv[i]);
               config.typeCount ++;
            }
        } else if (!strcasecmp(argv[0], "meta-server-w") && argc ==3) {
            sds type = sdsnew("redis");
            int j;
//            for (j = 0; j < 100; j ++) {
                stServerInfo *server = createServerInfo(argv[1], atoi(argv[2]), type);
                if (server == NULL ) {
                    err = "oom inmalloc"; goto loaderr;
                }
                if (server->port < 0 || server->port > 65535) {
                    err = "invalid meta-server-w port"; goto loaderr;
                }
                server->queueId = META_SERVER_TYPE_READWRITE;
                server->canWrite = 1;
                server->serverRole = SERVER_ROLE_META;
                addServerToServerQueue(server, &connectionPool.unconnectedServerQueue);
                log_debug("add one meta-server-w: ip[%s] port[%u]", server->ip, server->port);
//            }
            sdsfree(type);
        } else if (!strcasecmp(argv[0], "meta-server-r") && argc >= 3 && (argc - 1)%2 == 0) {
            int i;
            sds type = sdsnew("redis");
            for (i=1; i < argc; i +=2) {
                int j;
//                for (j=0; j < 1; j ++) {
                    stServerInfo *server = createServerInfo(argv[i], atoi(argv[i+1]), type);
                    if (server == NULL ) {
                        err = "oom inmalloc"; goto loaderr;
                    }
                    if (server->port < 0 || server->port > 65535) {
                        err = "invalid meta-server-w port"; goto loaderr;
                    }
                    server->queueId = META_SERVER_TYPE_READONLY;
                    server->canWrite = 0;
                    server->serverRole = SERVER_ROLE_META;
                    addServerToServerQueue(server, &connectionPool.unconnectedServerQueue);
                    log_debug("add one meta-server-r: ip[%s] port[%u]", server->ip, server->port);
//                }
            }
            sdsfree(type);
        } else if(!strcasecmp(argv[0], "meta-update-interval") && argc == 2) {
            config.metaUpdateInterval = atoi(argv[1]);
        } else if (!strcasecmp(argv[0], "store-server") && argc > 2) {
            int i, j=0, connectionNum = 1;
            int serverNum;
            if (argc %2 == 0) {
                serverNum = (argc -2)/2;
            } else {
                serverNum = (argc -1) / 2;
            }
            sds type = sdsnew("redis");
            connectionPool.storeServerNum = serverNum;
            for (; j < serverNum; j ++) {
                for (i=0; i < connectionNum; i ++) {
                    stServerInfo *server = createServerInfo(argv[j*2+1], atoi(argv[j*2+2]), type);
                    if (server == NULL ) {
                        err = "oom inmalloc"; goto loaderr;
                    }
                    if (server->port < 0 || server->port > 65535) {
                        err = "invalid meta-server-w port"; goto loaderr;
                    }
                    server->queueId = j;
                    server->canWrite = 1;
                    server->serverRole = SERVER_ROLE_STORE;
                    addServerToServerQueue(server, &connectionPool.unconnectedServerQueue);
                    log_debug("add one store-server[%d]: ip[%s] port[%u]", i, server->ip, server->port);
                }
            }
            sdsfree(type);
        } else if(!strcasecmp(argv[0], "monitor-interval") && argc == 2) {
            config.monitorInterval = atof(argv[1]);
        } else if(!strcasecmp(argv[0], "gather-interval") && argc == 2) {
            config.gatherInterval = atoi(argv[1]);
        } else if(!strcasecmp(argv[0], "action-check-interval") && argc == 2) {
            config.actionCheckInterval = atoi(argv[1]);
        } else if(!strcasecmp(argv[0], "rw-timeout") && argc == 2) {
            config.rwTimeoutSec = atof(argv[1]);
        } else if(!strcasecmp(argv[0], "server-state-expire-time") && argc == 2) {
            config.serverStateExpireTime = atoi(argv[1]);
        } else if(!strcasecmp(argv[0], "lib-dir") && argc ==2) {
            config.libDir = sdsdup(argv[1]);
        } else if(!strcasecmp(argv[0], "scripts-dir") && argc ==2) {
            config.scriptsDir = sdsdup(argv[1]);
        } else if(!strcasecmp(argv[0], "exchange-script-file") && argc ==2) {
            config.exchangeScriptFile = sdsdup(argv[1]);
        } else if(!strcasecmp(argv[0], "server-down-quota") && argc ==2) {
            config.serverDownQuota = atoi(argv[1]);
        } else if(!strcasecmp(argv[0], "monitor-group") && argc ==2) {
            config.monitorGroup = sdsdup(argv[1]);
	    } else {
            err = "Bad directive or wrong number of arguments"; goto loaderr;
        }
        for (j = 0; j < argc; j++)
            sdsfree(argv[j]);
        free(argv);
        sdsfree(line);
    }
    if (fp != stdin) fclose(fp);
    return;

loaderr:
    fprintf(stderr, "\n*** FATAL CONFIG FILE ERROR ***\n");
    fprintf(stderr, "Reading the configuration file, at line %d\n", linenum);
    fprintf(stderr, ">>> '%s'\n", line);
    fprintf(stderr, "%s\n", err);
    exit(1);

}

int dumpConfig() {
    log_trace("dump config ------------------>");
    log_trace("loglevel: %d", config.logLevel);
    log_trace("port: %u", config.port);
    int i;
    for (i = 0; i < config.typeCount; i ++) {
        log_trace("type[%d]: %s", i, config.typeStr[i]);
    }
    
    stServerInfo *server = NULL;
    for (i=0, server = connectionPool.unconnectedServerQueue.head; server != NULL; i++, server = server->next) {
       log_trace("serverrole[%d]: %s:%u, canWrite=%d", i, server->ip, server->port, server->canWrite);
    } 

    log_trace("metaUpdateInterval: %d", config.metaUpdateInterval);
    log_trace("connectionRepairInterval: %d", config.connectionRepairInterval);
    log_trace("<--------------------dump config done");
    return 0;
}


















/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
