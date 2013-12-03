/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file analyzer.h
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/14 14:03:34
 * @brief 
 *  
 **/




#ifndef  __ANALYZER_H_
#define  __ANALYZER_H_

#include "config.h"
#include "connection.h"
#include "gather.h"

typedef struct struct_analyzer {
    long long curGatherSeqId;
    stGatherServerList gatherServerList;
    stTargetServer *targetServerMapCur;
    stTargetServer *targetServerMapTmp;
} stAnalyzer;

extern stAnalyzer analyzer;












#endif  //__ANALYZER_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
