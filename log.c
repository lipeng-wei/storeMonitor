/***************************************************************************
 * 
 * Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/
 
 
 
/**
 * @file log.cpp
 * @author liuqingjun(com@baidu.com)
 * @date 2012/05/09 20:19:50
 * @brief 
 *  
 **/

/** modified by chendazhuang
 * @NOT need lib: Configure, libevent
 *
 **/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "log.h"

//#define DEBUG 1
//
typedef enum {
    false = 0,
    true = 1
} bool;

static const char *name;
static const char *dir;
int _log_level;

static int pid;

static bool write_to_stderr = false;

static char log_filename[1024];
static char logwf_filename[1024];
static FILE *log_f = NULL;
static FILE *logwf_f = NULL;
static char *log_buf = NULL;
static char *logwf_buf = NULL;
static int log_write_offset = 0;
static int logwf_write_offset = 0;
static int log_read_offset = 0;
static int logwf_read_offset = 0;
static pthread_t log_thread;

static int log_pipe[2];
#define LOG_BUF_SIZE (16*1024*1024)
#define LOG_SLEEP_TIME 5

log_data_t *log_data_new() {
    log_data_t *ld = (log_data_t*) malloc(sizeof(log_data_t));
    ld->next = NULL;
    ld->prev = NULL;
    ld->size = 0;
    ld->tail = ld;
    return ld;
}

void log_data_free(log_data_t *ld) {
    log_data_t *next = ld;
    log_data_t *tmp;
    while(next) {
        tmp = next->next;
        free(next);
        next = tmp;
    }
}

void log_data_push (log_data_t *ld, const char *key, const char *value) {
    log_data_push_str(ld, key, value);
}

#define _PUSH(ld, key, value, _field, _type) do { \
    log_data_t *node = ld->tail;            \
    if (node->size >= _LOG_DATA_SIZE) {     \
        node->next = log_data_new();        \
        if (node->next == NULL) return;     \
        node->next->prev = node;            \
        node = node->next;                  \
        ld->tail = node;                    \
    }                                       \
    node->key[node->size] = key;            \
    node->value[node->size]. _field = value;\
    node->type[node->size] = _type;           \
    node->size++;                           \
}while(0)

void log_data_push_str(log_data_t *ld, const char *key, const char *value) {
    _PUSH(ld, key, value, strval, STR);
}
void log_data_push_int(log_data_t *ld, const char *key, long value) {
    _PUSH(ld, key, value, intval, INT);
}
void log_data_push_uint(log_data_t *ld, const char *key, unsigned long value) {
    _PUSH(ld, key, value, uintval, UINT);
}
void log_data_push_double(log_data_t *ld, const char *key, double value) {
    _PUSH(ld, key, value, doubleval, DOUBLE);
}

//void log_data_pop(log_data_t *ld, int num) {
//}

static char _log_temp_buf[16384];
int log_printf(log_data_t *ld, int level, const char *fileline, const char *levelstr, const char* format, ...) {
    char timebuf[32];
    char *buf = _log_temp_buf;
    int size = sizeof(_log_temp_buf);
    int ret;
    
    //prefix part
    //construct time
    struct timeval tv;
    time_t nowtime;
    struct tm* nowtm;
    gettimeofday(&tv, NULL);
    nowtime = tv.tv_sec;
    nowtm = localtime(&nowtime);
    strftime(timebuf, sizeof(timebuf), "%m-%d %X", nowtm);
    ret = snprintf(buf, size, "%s: %s: %s * %d [", levelstr, timebuf, name, pid);
    if (ret < 0)
        ret = 0;
    else if (ret >= size)
        ret = size - 1;
    buf += ret;
    size -= ret;

    //suffix part
    if (fileline) {
        ret = snprintf(buf, size, "file=%s ", fileline);
        if (ret < 0)
            ret = 0;
        else if (ret >= size)
            ret = size - 1;
        buf += ret;
        size -= ret;
    }
    log_data_t *cur = ld;
    while(cur) {
        int i;
        for (i = 0; i < cur->size; i++) {
            switch (cur->type[i]) {
            case STR:
                ret = snprintf(buf, size, "%s=%s ", cur->key[i], cur->value[i].strval);
                break;
            case INT:
                ret = snprintf(buf, size, "%s=%ld ", cur->key[i], cur->value[i].intval);
                break;
            case UINT:
                ret = snprintf(buf, size, "%s=%lu ", cur->key[i], cur->value[i].uintval);
                break;
            case DOUBLE:
                ret = snprintf(buf, size, "%s=%lf ", cur->key[i], cur->value[i].doubleval);
                break;
            default:
                break;
            }
            if (ret < 0)
                ret = 0;
            else if (ret >= size)
                ret = size - 1;
            buf += ret;
            size -= ret;
        }
        cur = cur->next;
    }
    
    //user message part
    va_list fmtargs;
    va_start(fmtargs, format);
    ret = snprintf(buf, size, "msg=");
    if (ret < 0)
        ret = 0;
    else if (ret >= size)
        ret = size - 1;
    buf += ret;
    size -= ret;
    ret = vsnprintf(buf, size, format, fmtargs);
    if (ret < 0)
        ret = 0;
    else if (ret >= size)
        ret = size - 1;
    buf += ret;
    size -= ret;
    va_end(fmtargs);
    //if (ret >= sizeof(_log_temp_buf)) ret = sizeof(ret) - 1;
    //_log_temp_buf[ret] = '\n';
    //ret++;
    //
    //put ending "]\n" to buf
    ret = snprintf(buf, size, "]\n");
    if (ret < 0)
        ret = 0;
    else if (ret >= size)
        ret = size - 1;
    buf += ret;
    size -= ret;
    if (*(buf - 1) != '\n') {
        *(buf-1) = '\n';
    }

    //copy the log string to global log buffer
#ifdef DEBUG
    fprintf(stderr, "%s", _log_temp_buf);
#endif

    //ret is the written log size
    ret = buf - _log_temp_buf;
    if (level <= 2) {
        int buf_left_size = LOG_BUF_SIZE - ((logwf_write_offset - logwf_read_offset + LOG_BUF_SIZE) % LOG_BUF_SIZE);
        if (ret > buf_left_size - 1) { 
            if (buf_left_size - 1 > 0) {
                ret = buf_left_size - 1;
                _log_temp_buf[ret-1] = '\n';
            } else {
                ret = 0;
            }
        }
        if (logwf_write_offset + ret < LOG_BUF_SIZE) {
            memcpy(logwf_buf + logwf_write_offset, _log_temp_buf, ret);
        } else {
            memcpy(logwf_buf + logwf_write_offset, _log_temp_buf, LOG_BUF_SIZE - logwf_write_offset);
            memcpy(logwf_buf, _log_temp_buf + LOG_BUF_SIZE - logwf_write_offset, ret - LOG_BUF_SIZE + logwf_write_offset);
        }
        logwf_write_offset += ret;
        logwf_write_offset %= LOG_BUF_SIZE;
    } else {
        int buf_left_size = LOG_BUF_SIZE - ((log_write_offset - log_read_offset + LOG_BUF_SIZE) % LOG_BUF_SIZE);
        if (ret > buf_left_size - 1) { 
            if (buf_left_size - 1 > 0) {
                ret = buf_left_size - 1;
                _log_temp_buf[ret-1] = '\n';
            } else {
                ret = 0;
            }
        }
        if (log_write_offset + ret < LOG_BUF_SIZE) {
            memcpy(log_buf + log_write_offset, _log_temp_buf, ret);
        } else {
            memcpy(log_buf + log_write_offset, _log_temp_buf, LOG_BUF_SIZE - log_write_offset);
            memcpy(log_buf, _log_temp_buf + LOG_BUF_SIZE - log_write_offset, ret - LOG_BUF_SIZE + log_write_offset);
        }
        log_write_offset += ret;
        log_write_offset %= LOG_BUF_SIZE;
    }
    write(log_pipe[1], "1", 1);
    return 0;
}

int* log_thread_loop() {
    int size;

    char tmp[1024];
    struct timeval tv;
    int retval;
    fd_set rfds;
    struct stat stat_data;
    FILE *ftemp = NULL;

    while(true) {
        FD_SET(log_pipe[0], &rfds);
        tv.tv_sec = LOG_SLEEP_TIME;
        tv.tv_usec = 0;
        retval = select(log_pipe[0]+1, &rfds, NULL, NULL, &tv);
        size = read(log_pipe[0], tmp, sizeof(tmp));
        
        //check if file is moved or deleted
        if (stat(log_filename, &stat_data) < 0) {
            ftemp = fopen(log_filename, "a");
            if (ftemp != NULL) {
                fclose(log_f);
                log_f = ftemp;
                ftemp = NULL;
            }
        }
        if (stat(logwf_filename, &stat_data) < 0) {
            ftemp = fopen(logwf_filename, "a");
            if (ftemp != NULL) {
                fclose(logwf_f);
                logwf_f = ftemp;
                ftemp = NULL;
            }
        }

        //if (size < 0) perror("READ ERROR");
        bool write_log = false;
        bool write_logwf = false;
        if (log_write_offset != log_read_offset) {
            if (log_write_offset > log_read_offset) {
                size = fwrite(log_buf + log_read_offset, 1, log_write_offset - log_read_offset, log_f);
                if (write_to_stderr) fwrite(log_buf + log_read_offset, 1, log_write_offset - log_read_offset, stderr);
                log_read_offset += size;
                log_read_offset %= LOG_BUF_SIZE;
            } else {
                size = fwrite(log_buf + log_read_offset, 1, LOG_BUF_SIZE - log_read_offset, log_f);
                if (write_to_stderr) fwrite(log_buf + log_read_offset, 1, LOG_BUF_SIZE - log_read_offset, stderr);
                log_read_offset += size;
                log_read_offset %= LOG_BUF_SIZE;

                size = fwrite(log_buf + log_read_offset, 1, log_write_offset - log_read_offset, log_f);
                if (write_to_stderr) fwrite(log_buf + log_read_offset, 1, log_write_offset - log_read_offset, stderr);
                log_read_offset += size;
                log_read_offset %= LOG_BUF_SIZE;
            }
            write_log = true;
        }
        if (logwf_write_offset != logwf_read_offset) {
            if (logwf_write_offset > logwf_read_offset) {
                size = fwrite(logwf_buf + logwf_read_offset, 1, logwf_write_offset - logwf_read_offset, logwf_f);
                if (write_to_stderr) fwrite(logwf_buf + logwf_read_offset, 1, logwf_write_offset - logwf_read_offset, stderr);
                logwf_read_offset += size;
                logwf_read_offset %= LOG_BUF_SIZE;
            } else {
                size = fwrite(logwf_buf + logwf_read_offset, 1, LOG_BUF_SIZE - logwf_read_offset, logwf_f);
                if (write_to_stderr) fwrite(logwf_buf + logwf_read_offset, 1, LOG_BUF_SIZE - logwf_read_offset, stderr);
                logwf_read_offset += size;
                logwf_read_offset %= LOG_BUF_SIZE;

                size = fwrite(logwf_buf + logwf_read_offset, 1, logwf_write_offset - logwf_read_offset, logwf_f);
                if (write_to_stderr) fwrite(logwf_buf + logwf_read_offset, 1, logwf_write_offset - logwf_read_offset, stderr);
                logwf_read_offset += size;
                logwf_read_offset %= LOG_BUF_SIZE;
            }
            write_logwf = true;
        }

        if (write_log) fflush(log_f);
        if (write_logwf) fflush(logwf_f);
    }
    return NULL;
}

void log_set_loglevel(int loglevel) {
    _log_level = loglevel;
}
int log_init(char *logname, char *logdir, int loglevel) {
    char filename[256];
    pid = getpid();
    if (getenv("log_to_stderr") != NULL) write_to_stderr = true;
    name = strdup(logname);
    dir = strdup(logdir);
    _log_level = loglevel;
    snprintf(log_filename, sizeof(log_filename), "%s/%s.log", dir, name);
    snprintf(logwf_filename, sizeof(logwf_filename), "%s/%s.log.wf", dir, name);

    log_f = fopen(log_filename, "a");
    logwf_f = fopen(logwf_filename, "a");

    log_buf = (char*) malloc(LOG_BUF_SIZE);
    logwf_buf = (char*) malloc(LOG_BUF_SIZE);
    if (!(log_f && logwf_f && log_buf && logwf_buf)) {
        perror("Error init log");
        exit(1);
    }

    int rc;

    rc = pipe(log_pipe);
    if (rc) {
        perror("Error create pipes");
        exit(1);
    }
    //fcntl(log_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(log_pipe[1], F_SETFL, O_NONBLOCK);

    rc = pthread_create(&log_thread, NULL, log_thread_loop, NULL);
    if (rc) {
        perror("Error create log thread");
        exit(1);
    }
    return 0;
}

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
