#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef HAVE_PTHREAD
#include "mutex.h"
#else
#define MUTEX_INIT(ptr)
#define MUTEX_LOCK(ptr)
#define MUTEX_UNLOCK(ptr)
#define MUTEX_DESTROY(ptr)
#endif

#ifndef _TIMER_H
#define _TIMER_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef _TYPEDEF_TIMER
#define _TYPEDEF_TIMER
typedef struct _TIMER 
{
	struct timeval tv;
	time_t start_sec;
	long long start_usec;
	time_t sec_used;
	long long usec_used;
	time_t  last_sec;
	long long last_usec;
	time_t last_sec_used;
    long long last_usec_used;
	void *mutex;
}TIMER;
#define PT(ptr) ((TIMER *)ptr)
#define PT_SEC_U(ptr) ((PT(ptr))?PT(ptr)->sec_used:0)
#define PT_USEC_U(ptr) ((PT(ptr))?PT(ptr)->usec_used:0)
#define PT_L_SEC(ptr) ((PT(ptr))?PT(ptr)->last_sec:0)
#define PT_LU_SEC(ptr) ((PT(ptr))?PT(ptr)->last_sec_used:0)
#define PT_L_USEC(ptr) ((PT(ptr))?PT(ptr)->last_usec:0)
#define PT_LU_USEC(ptr) ((PT(ptr))?PT(ptr)->last_usec_used:0)
#define TIMER_INIT(ptr)                                                         \
{                                                                               \
    if((ptr = (calloc(1, sizeof(TIMER)))))                                      \
    {                                                                           \
        gettimeofday(&(PT(ptr)->tv), NULL);                                     \
        PT(ptr)->start_sec    = PT(ptr)->tv.tv_sec;                             \
        PT(ptr)->start_usec   = PT(ptr)->tv.tv_sec * 1000000ll                  \
            + PT(ptr)->tv.tv_usec * 1ll;                                        \
        PT(ptr)->last_sec     = PT(ptr)->start_sec;                             \
        PT(ptr)->last_usec    = PT(ptr)->start_usec;                            \
        MUTEX_INIT(PT(ptr)->mutex);                                             \
    }                                                                           \
}                                                                       
#define TIMER_SAMPLE(ptr)                                                       \
{                                                                               \
    if(ptr)                                                                     \
    {                                                                           \
        MUTEX_LOCK(PT(ptr)->mutex);                                             \
        gettimeofday(&(PT(ptr)->tv), NULL);                                     \
        PT(ptr)->last_sec_used    = PT(ptr)->tv.tv_sec - PT(ptr)->last_sec;     \
        PT(ptr)->last_usec_used   = PT(ptr)->tv.tv_sec * 1000000ll              \
            + PT(ptr)->tv.tv_usec - PT(ptr)->last_usec;                         \
        PT(ptr)->last_sec         = PT(ptr)->tv.tv_sec;                         \
        PT(ptr)->last_usec        = PT(ptr)->tv.tv_sec * 1000000ll              \
            + PT(ptr)->tv.tv_usec;                                              \
        PT(ptr)->sec_used     = PT(ptr)->tv.tv_sec - PT(ptr)->start_sec;        \
        PT(ptr)->usec_used    = PT(ptr)->last_usec - PT(ptr)->start_usec;       \
        MUTEX_UNLOCK(PT(ptr)->mutex);                                           \
    }                                                                           \
}
#define TIMER_RESET(ptr)                                                        \
{                                                                               \
    if(ptr)                                                                     \
    {                                                                           \
        gettimeofday(&(PT(ptr)->tv), NULL);                                     \
        PT(ptr)->start_sec    = PT(ptr)->tv.tv_sec;                             \
        PT(ptr)->start_usec   = PT(ptr)->tv.tv_sec * 1000000ll                  \
            + PT(ptr)->tv.tv_usec * 1ll;                                        \
        PT(ptr)->last_sec     = PT(ptr)->start_sec;                             \
        PT(ptr)->last_usec    = PT(ptr)->start_usec;                            \
        PT(ptr)->last_sec_used     = 0ll;                                       \
        PT(ptr)->last_usec_used    = 0ll;                                       \
    }                                                                           \
}

#define TIMER_CHECK(ptr, interval)                                              \
    (PT(ptr) && (gettimeofday(&(PT(ptr)->tv), NULL) == 0                        \
     && ((PT(ptr)->tv.tv_sec * 1000000ll + PT(ptr)->tv.tv_usec)                 \
        - PT(ptr)->last_usec) > interval) ? 0 : -1)

#define TIMER_CLEAN(ptr)                                                        \
{                                                                               \
    if(ptr)                                                                     \
    {                                                                           \
        MUTEX_DESTROY(PT(ptr)->mutex);                                          \
        free(ptr);                                                              \
        ptr = NULL;                                                             \
    }                                                                           \
}
#endif 

#ifdef __cplusplus
 }
#endif

#endif
