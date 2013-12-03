/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file dmonitor.h
 * @author chendazhuang(com@baidu.com)
 * @date 2013/03/04 15:41:02
 * @brief 
 *  
 **/




#ifndef  __DMONITOR_H_
#define  __DMONITOR_H_

#include "sds.h"
#include "connection.h"
#include "meta.h"
#include "config.h"
#include "monitor.h"

typedef struct struct_dmonitor {

    stMonitorMap *monitorMapCur;
    stMonitorMap *monitorMapTmp;

} stDMonitor;

extern stDMonitor dmonitor;








#endif  //__DMONITOR_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
