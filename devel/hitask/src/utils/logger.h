#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
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
#define	__ACCESS__ 		4
static char *_logger_level_s[] = {"DEBUG", "WARN", "ERROR", "FATAL", ""};
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
#define PLF(ptr) (PL(ptr)->file)
#define PLP(ptr) (PL(ptr)->p)
#define PLTV(ptr) (PL(ptr)->tv)
#define PLTP(ptr) (PL(ptr)->timep)
#define PLID(ptr) (PL(ptr)->id)
#define PLFD(ptr) (PL(ptr)->fd)
#define PLB(ptr) (PL(ptr)->buf)
#define PLPS(ptr) (PL(ptr)->ps)
#define LOGGER_INIT(ptr, lp)                                                        \
do{                                                                                   \
    if((ptr = (LOGGER *)calloc(1, sizeof(LOGGER))))                                 \
    {                                                                               \
        MUTEX_INIT(PL(ptr)->mutex);                                                 \
        strcpy(PLF(ptr), lp);                                                       \
        PLFD(ptr) = open(PLF(ptr), O_CREAT|O_WRONLY|O_APPEND, 0644);                \
    }                                                                               \
}while(0)
#define LOGGER_ADD(ptr, __level__, format...)                                       \
do{                                                                                 \
    if(ptr)                                                                         \
    {                                                                               \
    MUTEX_LOCK(PL(ptr)->mutex);                                                     \
    gettimeofday(&(PLTV(ptr)), NULL); time(&(PLTP(ptr)));                           \
    PLP(ptr) = localtime(&PLTP(ptr));                                               \
    PLPS(ptr) = PLB(ptr);                                                           \
    PLPS(ptr) += sprintf(PLPS(ptr), "[%02d/%s/%04d:%02d:%02d:%02d +%06u] "          \
            "[%u/%08x] #%s::%d# %s:", PLP(ptr)->tm_mday, ymonths[PLP(ptr)->tm_mon], \
            (1900+PLP(ptr)->tm_year), PLP(ptr)->tm_hour, PLP(ptr)->tm_min,          \
        PLP(ptr)->tm_sec, (unsigned int)(PLTV(ptr).tv_usec),(unsigned int)getpid(), \
        (unsigned int)THREADID(), __FILE__, __LINE__, _logger_level_s[__level__]);  \
    PLPS(ptr) += sprintf(PLPS(ptr), format);                                        \
    *PLPS(ptr)++ = '\n';                                                            \
    write(PLFD(ptr), PLB(ptr), (PLPS(ptr) - PLB(ptr)));                             \
    MUTEX_UNLOCK(PL(ptr)->mutex);                                                   \
    }                                                                               \
}while(0)
#ifdef _DEBUG
#define DEBUG_LOGGER(ptr, format...) {LOGGER_ADD(ptr, __DEBUG__, format);}
#else
#define DEBUG_LOGGER(ptr, format...)
#endif
#define WARN_LOGGER(ptr, format...) {LOGGER_ADD(ptr, __WARN__, format);}
#define ERROR_LOGGER(ptr, format...) {LOGGER_ADD(ptr, __ERROR__, format);}
#define FATAL_LOGGER(ptr, format...) {LOGGER_ADD(ptr, __FATAL__, format);}
#define ACCESS_LOGGER(ptr, format...) {LOGGER_ADD(ptr, __ACCESS__, format);}
#define LOGGER_CLEAN(ptr)                                                           \
do{                                                                                 \
    if(ptr)                                                                         \
    {                                                                               \
        close(PLFD(ptr));                                                           \
        if(PL(ptr)->mutex){MUTEX_DESTROY(PL(ptr)->mutex);}                          \
        free(ptr);                                                                  \
        ptr = NULL;                                                                 \
    }                                                                               \
}while(0)
#ifdef __cplusplus
 }
#endif
#endif
