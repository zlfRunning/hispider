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
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#ifndef _LOGGER_H
#define _LOGGER_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef _TYPEDEF_LOGGER
#define _TYPEDEF_LOGGER
#define LOGGER_FILENAME_LIMIT  	1024
#define LOGGER_LINE_LIMIT  	8192
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
	int fd ;
	void *mutex;

	void (*add)(struct _LOGGER *, char *, int, int, char *format, ...);		
	void (*close)(struct _LOGGER **);
}LOGGER;
/* Initialize LOGGER */
LOGGER *logger_init(char *logfile);
#endif

/* Add log */
void logger_add(LOGGER *, char *, int, int, char *format,...);
/* Close log */
void logger_close(LOGGER **);
#ifdef _DEBUG
#define DEBUG_LOGGER(log, format...)                                                        \
{                                                                                           \
    if(log){((LOGGER *)log)->add((LOGGER *)log, __FILE__, __LINE__, __DEBUG__,format);}     \
}                                                                                       
#else
#define DEBUG_LOGGER(log, format...)
#endif
#define WARN_LOGGER(log, format...)                                                         \
{                                                                                           \
    if(log){((LOGGER *)log)->add((LOGGER *)log, __FILE__, __LINE__, __WARN__,format);}      \
}                                                                                       
#define ERROR_LOGGER(log, format...)                                                        \
{                                                                                           \
    if(log){((LOGGER *)log)->add((LOGGER *)log, __FILE__, __LINE__, __ERROR__,format);}     \
}                                                                                       
#define FATAL_LOGGER(log, format...)                                                        \
{                                                                                           \
    if(log){((LOGGER *)log)->add((LOGGER *)log, __FILE__, __LINE__, __FATAL__,format);}     \
}                                                                                       
#define CLOSE_LOGGER(logger) ((LOGGER *)logger)->close((LOGGER **)&logger)
#ifdef HAVE_PTHREAD
#define THREADID() (size_t)pthread_self()
#else
#define THREADID() (0)
#endif

#ifdef __cplusplus
 }
#endif
#endif
