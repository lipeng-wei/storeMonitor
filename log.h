/***************************************************************************
 * 
 * Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file log.h
 * @author liuqingjun(com@baidu.com)
 * @date 2012/04/25 22:13:19
 * @brief 
 *  
 **/




#ifndef  __LOG_H_
#define  __LOG_H_

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define LOG_DEBUG 16
#define LOG_TRACE 8
#define LOG_NOTICE 4
#define LOG_WARNING 2
#define LOG_FATAL 1

extern int _log_level;

#define _LOG_DATA_SIZE 16

typedef enum _log_data_type_t {
    STR,
    INT,
    UINT,
    DOUBLE
} log_data_type_t;

typedef union _log_data_data_t {
    const char *strval;
    long intval;
    unsigned long uintval;
    double doubleval;
} log_data_data_t;

typedef struct _log_data_t {
    const char *key[_LOG_DATA_SIZE];
    log_data_data_t value[_LOG_DATA_SIZE];
    log_data_type_t type[_LOG_DATA_SIZE];
    struct _log_data_t *prev;
    struct _log_data_t *next;
    int size;
    struct _log_data_t *tail;
} log_data_t;

#define log_base(data, lvl, ...) do { \
    if (_log_level >= LOG_ ## lvl) \
        log_printf(data, LOG_ ## lvl, __FILE__ ":" LINESTR, #lvl, __VA_ARGS__); \
} while(0)

#define _TOSTR(x) #x
#define _LINESTR(x) _TOSTR(x)
#define LINESTR _LINESTR(__LINE__)

#define rlog_debug(data, ...) log_base(data, DEBUG, __VA_ARGS__)
#define rlog_trace(data, ...) log_base(data, TRACE, __VA_ARGS__)
#define rlog_notice(data, ...) log_base(data, NOTICE, __VA_ARGS__)
#define rlog_warning(data, ...) log_base(data, WARNING, __VA_ARGS__)
#define rlog_fatal(data, ...) log_base(data, FATAL, __VA_ARGS__)
#define log_debug(...) log_base(NULL, DEBUG, __VA_ARGS__)
#define log_trace(...) log_base(NULL, TRACE, __VA_ARGS__)
#define log_notice(...) log_base(NULL, NOTICE, __VA_ARGS__)
#define log_warning(...) log_base(NULL, WARNING, __VA_ARGS__)
#define log_fatal(...) log_base(NULL, FATAL, __VA_ARGS__)

#define _log_level_check_base(lvl) (_log_level >= LOG_ ## lvl)
#define log_has_debug() _log_level_check_base(DEBUG)
#define log_has_trace() _log_level_check_base(TRACE)
#define log_has_notice() _log_level_check_base(NOTICE)
#define log_has_warning() _log_level_check_base(WARNING)
#define log_has_fatal() _log_level_check_base(FATAL)

int log_init(char *logname, char *logdir, int loglevel);
int log_printf(log_data_t *ld, int level, const char *fileline, const char *levelstr, const char* format, ...);

void log_set_loglevel(int loglevel);

log_data_t *log_data_new();
void log_data_free(log_data_t *ld);

void log_data_push(log_data_t *ld, const char *key, const char *value); //alias for log_data_push_str
void log_data_push_str(log_data_t *ld, const char *key, const char *value);
void log_data_push_int(log_data_t *ld, const char *key, long value);
void log_data_push_uint(log_data_t *ld, const char *key, unsigned long value);
void log_data_push_double(log_data_t *ld, const char *key, double value);
//void log_data_pop(log_data_t *ld, int num);

#endif  //__LOG_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
