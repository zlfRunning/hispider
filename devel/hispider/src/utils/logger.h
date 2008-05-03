#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "mutex.h"
#ifndef _LOGGER_H
#define _LOGGER_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef _TYPEDEF_LOGGER
#define _TYPEDEF_LOGGER
#define LOGGER_FILENAME_LIMIT  	1024
#define LOGGER_LINE_LIMIT  	    512000
#define __DEBUG__		0
#define	__WARN__ 		1
#define	__ERROR__ 		2
#define	__FATAL__ 		3
static char *_logger_level_s[] = {"DEBUG", "WARN", "ERROR", "FATAL"};
#ifndef _STATIS_YMON
#define _STATIS_YMON
static char *ymonths[]= {
        "Jan", "Feb", "Mar",
        "Apr", "May", "Jun",
        "Jul", "Aug", "Sep",
        "Oct", "Nov", "Dec"};
#endif
typedef struct _LOGGER
{
	char file[LOGGER_FILENAME_LIMIT];
    char buf[LOGGER_LINE_LIMIT];
    struct timeval tv;
    time_t timep;
    struct tm *p;
    char *ps;
    int id;
    void *mutex;
	int fd ;
}LOGGER;
#endif
#ifdef HAVE_PTHREAD
#define THREADID() (size_t)pthread_self()
#else
#define THREADID() (0)
#endif
#define PL(ptr) ((LOGGER *)ptr)
#define PLF(ptr) ((LOGGER *)ptr)->file
#define PLP(ptr) ((LOGGER *)ptr)->p
#define PLTV(ptr) ((LOGGER *)ptr)->tv
#define PLTP(ptr) ((LOGGER *)ptr)->timep
#define PLID(ptr) ((LOGGER *)ptr)->id
#define PLFD(ptr) ((LOGGER *)ptr)->fd
#define PLB(ptr) ((LOGGER *)ptr)->buf
#define PLPS(ptr) ((LOGGER *)ptr)->ps
#define LOGGER_INIT(ptr, lp)                                                        \
{                                                                                   \
    if((ptr = (LOGGER *)calloc(1, sizeof(LOGGER))))                                 \
    {                                                                               \
        MUTEX_INIT(PL(ptr)->mutex);                                                 \
        strcpy(PLF(ptr), lp);                                                       \
        PLFD(ptr) = open(PLF(ptr), O_CREAT|O_WRONLY|O_APPEND, 0644);                \
    }                                                                               \
}
#define LOGGER_ADD(ptr, __level__, format...)                                       \
{                                                                                   \
    if(ptr)                                                                         \
    {                                                                               \
    MUTEX_LOCK(PL(ptr)->mutex);                                                     \
    gettimeofday(&(PLTV(ptr)), NULL); time(&PLTP(ptr));                             \
    PLP(ptr) = localtime(&PLTP(ptr));                                               \
    PLPS(ptr) = PLB(ptr);                                                           \
    PLPS(ptr) += sprintf(PLPS(ptr), "[%02d/%s/%04d:%02d:%02d:%02d +%06u] "          \
            "[%u/%08x] #%s::%d# %s:", PLP(ptr)->tm_mday, ymonths[PLP(ptr)->tm_mon], \
            (1900+PLP(ptr)->tm_year), PLP(ptr)->tm_hour, PLP(ptr)->tm_min,          \
            PLP(ptr)->tm_sec, (size_t)(PLTV(ptr).tv_usec), (size_t)getpid(),        \
            THREADID(), __FILE__, __LINE__, _logger_level_s[__level__]);            \
    PLPS(ptr) += sprintf(PLPS(ptr), format);                                        \
    *PLPS(ptr)++ = '\n';                                                            \
    write(PLFD(ptr), PLB(ptr), (PLPS(ptr) - PLB(ptr)));                             \
    MUTEX_UNLOCK(PL(ptr)->mutex);                                                   \
    }                                                                               \
}
#ifdef _DEBUG
#define DEBUG_LOGGER(ptr, format...) {LOGGER_ADD(ptr, __DEBUG__, format);}
#else
#define DEBUG_LOGGER(ptr, format...)
#endif
#define WARN_LOGGER(ptr, format...) {LOGGER_ADD(ptr, __WARN__, format);}
#define ERROR_LOGGER(ptr, format...) {LOGGER_ADD(ptr, __ERROR__, format);}
#define FATAL_LOGGER(ptr, format...) {LOGGER_ADD(ptr, __FATAL__, format);}
#define LOGGER_CLEAN(ptr)                                                           \
{                                                                                   \
    if(ptr)                                                                         \
    {                                                                               \
        close(PLFD(ptr));                                                           \
        if(PL(ptr)->mutex){MUTEX_DESTROY(PL(ptr)->mutex);}                          \
        free(ptr);                                                                  \
        ptr = NULL;                                                                 \
    }                                                                               \
}
#ifdef __cplusplus
 }
#endif
#endif
