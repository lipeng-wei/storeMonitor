/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file config.h
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/04 13:32:02
 * @brief 
 *  
 **/




#ifndef  __CONFIG_H_
#define  __CONFIG_H_

#include "sds.h"

#define CONFIGLINE_MAX 128

typedef struct struct_server_config {
    int logLevel; /* log level */
    char logFile[256]; /*log file name */
    unsigned short port;

    sds *typeStr; /* typestr from dmonitor.conf */
    size_t typeCount; /* num of the array typestr */

    sds libDir;
    sds monitorGroup;

    int metaUpdateInterval; /* second for meta update */
    int connectionRepairInterval;
    double monitorInterval;
    int gatherInterval;
    int actionCheckInterval;

    int serverStateExpireTime; // sec
    double rwTimeoutSec; /* support ms */

    int serverDownQuota;

    sds scriptsDir; 
    sds exchangeScriptFile; 
} stServerConfig;


void initConfig();
void loadConfig(char *filename);
int dumpConfig();

extern stServerConfig config;












#endif  //__CONFIG_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
