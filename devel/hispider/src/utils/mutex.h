#ifndef _MUTEX_H
#define _MUTEX_H
#ifdef HAVE_PTHREAD
#include <pthread.h>
#define MUTEX_INIT(mlock) {if(mlock = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t))) pthread_mutex_init(mlock, NULL);}
#define MUTEX_LOCK(mlock) {if(mlock) pthread_mutex_lock(mlock);}
#define MUTEX_UNLOCK(mlock) {if(mlock) pthread_mutex_unlock(mlock);}
#define MUTEX_DESTROY(mlock) {if(mlock){pthread_mutex_destroy(mlock);free(mlock); mlock = NULL;}}
#else
#define MUTEX_INIT(mlock)
#define MUTEX_LOCK(mlock)
#define MUTEX_UNLOCK(mlock)
#define MUTEX_DESTROY(mlock)
#endif
#endif
