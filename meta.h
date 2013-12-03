/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file meta.h
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/06 09:26:01
 * @brief 
 *  
 **/




#ifndef  __META_H_
#define  __META_H_

#include "ev.h"
#include "uthash.h"
#include "engine.h"

typedef int (updateLocalMetaForServerFunc)(EV_P_ sds serverIdKey);
typedef int (clearOldLocalMetaFunc)(EV_P);

typedef struct struct_meta {
    updateLocalMetaForServerFunc *updateLocalMetaForServerFn;
    clearOldLocalMetaFunc *clearOldLocalMetaFn;
} stMeta;

void metaUpdateTimerCb(EV_P_ ev_timer *timer, int revents);
stServerInfo *fetchMetaServerForRead();

extern stMeta meta;












#endif  //__META_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
