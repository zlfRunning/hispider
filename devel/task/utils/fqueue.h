#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "mutex.h"
#ifndef _FQUEUE_H
#define _FQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#define FQ_BLOCK_SIZE  1024
typedef struct _QSTATE
{
    int left;
    int count;
    int head;
    int tail;
    int total;
}QSTATE;
typedef struct _FQUEUE
{
    QSTATE *state;
    int pos;
    int size;
    int fd;
    struct stat st;
    void *table;
    void *newtable;
    void *mutex;
}FQUEUE;
#define FQ(ptr) ((FQUEUE *)ptr)
#define FQSIZE(ptr) ((FQ(ptr)->size))
#define FQNTAB(ptr) ((FQ(ptr)->newtable))
#define FQTAB(ptr) ((FQ(ptr)->table))
#define FQTI(ptr, type, n) (((type *)(FQ(ptr)->table))[n])
#define FQNTI(ptr, type, n) (((type *)(FQ(ptr)->newtable))[n])
#define FQPOS(ptr) (FQ(ptr)->pos)
#define FQHEAD(ptr) (FQ(ptr)->state->head)
#define FQTAIL(ptr) (FQ(ptr)->state->tail)
#define FQLEFT(ptr) (FQ(ptr)->state->left)
#define FQCOUNT(ptr) (FQ(ptr)->state->count)
#define FQTOTAL(ptr) (FQ(ptr)->state->total)
#define FQMUTEX(ptr) (FQ(ptr)->mutex)
#define FQUEUE_INIT(ptr, qfile, type)                                                       \
do                                                                                          \
{                                                                                           \
    if((ptr = calloc(1, sizeof(FQUEUE))))                                                   \
    {                                                                                       \
        MUTEX_INIT(FQ(ptr)->mutex);                                                         \
        if(qfile && (FQ(ptr)->fd = open(qfile, O_CREAT|O_RDWR, 0644)) > 0)                  \
        {                                                                                   \
            fstat(FQ(ptr)->fd, &(FQ(ptr)->st));                                             \
            if(FQ(ptr)->st.st_size == 0)                                                    \
            {                                                                               \
                FQ(ptr)->size = sizeof(QSTATE) + FQ_BLOCK_SIZE * sizeof(type);              \
                ftruncate(FQ(ptr)->fd, FQ(ptr)->size);                                      \
                FQ(ptr)->state = (QSTATE *)mmap(NULL, FQ(ptr)->size,                        \
                        PROT_READ|PROT_WRITE, MAP_SHARED, FQ(ptr)->fd, 0);                  \
                FQ(ptr)->state->count = FQ_BLOCK_SIZE;                                      \
                FQ(ptr)->table = FQ(ptr)->state + sizeof(QSTATE);                           \
                FQ(ptr)->state->left = FQ_BLOCK_SIZE;                                       \
            }                                                                               \
            else                                                                            \
            {                                                                               \
                FQ(ptr)->size = FQ(ptr)->st.st_size;                                        \
                FQ(ptr)->state = (QSTATE *)mmap(NULL, FQ(ptr)->size,                        \
                        PROT_READ|PROT_WRITE, MAP_SHARED, FQ(ptr)->fd, 0);                  \
                FQ(ptr)->table = FQ(ptr)->state + sizeof(QSTATE);                           \
            }                                                                               \
        }                                                                                   \
    }                                                                                       \
}while(0)

#define FQUEUE_RESIZE(ptr, type)                                                            \
do                                                                                          \
{                                                                                           \
    if(FQLEFT(ptr) <= 0)                                                                    \
    {                                                                                       \
        if((FQNTAB(ptr) = calloc(FQCOUNT(ptr), sizeof(type))))                              \
        {                                                                                   \
            if(FQTAB(ptr) && FQCOUNT(ptr) > 0)                                              \
            {                                                                               \
                FQPOS(ptr) = 0;                                                             \
                while(FQPOS(ptr) < FQCOUNT(ptr))                                            \
                {                                                                           \
                    if(FQHEAD(ptr) == FQCOUNT(ptr)) FQHEAD(ptr) = 0;                        \
                    memcpy(&(FQNTI(ptr, type, FQPOS(ptr))),                                 \
                            &(FQTI(ptr, type, FQHEAD(ptr))), sizeof(type));                 \
                    FQPOS(ptr)++;FQHEAD(ptr)++;                                             \
                }                                                                           \
                msync(FQ(ptr)->state, FQSIZE(ptr), MS_SYNC);                                \
                munmap(FQ(ptr)->state, FQSIZE(ptr));                                        \
                FQSIZE(ptr) += sizeof(type) * FQ_BLOCK_SIZE;                                \
                ftruncate(FQ(ptr)->fd, FQSIZE(ptr));                                        \
                FQ(ptr)->state = (QSTATE *)mmap(NULL, FQSIZE(ptr),                          \
                        PROT_READ|PROT_WRITE, MAP_SHARED, FQ(ptr)->fd, 0);                  \
                FQTAB(ptr) = FQ(ptr)->state + sizeof(QSTATE);                               \
                memcpy(FQTAB(ptr), FQNTAB(ptr), FQCOUNT(ptr) * sizeof(type));               \
                FQCOUNT(ptr) += FQ_BLOCK_SIZE;                                              \
                FQLEFT(ptr) += FQ_BLOCK_SIZE;                                               \
                FQTAIL(ptr) = FQPOS(ptr);                                                   \
                free(FQNTAB(ptr));                                                          \
                FQHEAD(ptr) = 0;                                                            \
                FQNTAB(ptr) = NULL;                                                         \
                FQPOS(ptr) = 0;                                                             \
            }                                                                               \
        }                                                                                   \
    }                                                                                       \
}while(0)

#define FQUEUE_PUSH(ptr, type, dptr)                                                        \
do                                                                                          \
{                                                                                           \
    MUTEX_LOCK(FQMUTEX(ptr));                                                               \
    if(ptr && dptr)                                                                         \
    {                                                                                       \
        FQUEUE_RESIZE(ptr, type);                                                           \
        if(FQTAB(ptr) && FQLEFT(ptr) > 0)                                                   \
        {                                                                                   \
            if(FQTAIL(ptr) == FQCOUNT(ptr)) FQTAIL(ptr) = 0;                                \
            memcpy(&(FQTI(ptr, type, FQTAIL(ptr))), dptr, sizeof(type));                    \
            FQTAIL(ptr)++;FQLEFT(ptr)--;FQTOTAL(ptr)++;                                     \
        }                                                                                   \
    }                                                                                       \
    MUTEX_UNLOCK(FQMUTEX(ptr));                                                             \
}while(0)

#define FQUEUE_POP(ptr, type, dptr) ((ptr && FQLEFT(ptr) < FQCOUNT(ptr)                     \
        && MUTEX_LOCK(FQMUTEX(ptr)) == 0                                                    \
        && (FQHEAD(ptr) = ((FQHEAD(ptr) == FQCOUNT(ptr))? 0 : FQHEAD(ptr))) >= 0            \
        && memcpy(dptr, &(FQTI(ptr, type, FQHEAD(ptr))), sizeof(type))                      \
        && FQHEAD(ptr)++ >= 0 && FQLEFT(ptr)++ >= 0 && FQTOTAL(ptr)--  >= 0                 \
        && MUTEX_UNLOCK(FQMUTEX(ptr)) == 0)? 0: -1)

#define FQUEUE_HEAD(ptr, type, dptr) ((ptr && FQLEFT(ptr) < FQCOUNT(ptr)                    \
        && MUTEX_LOCK(FQMUTEX(ptr)) == 0                                                    \
        && (FQHEAD(ptr) = ((FQHEAD(ptr) == FQCOUNT(ptr))? 0 : FQHEAD(ptr))) >= 0            \
        && memcpy(dptr, &(FQTI(ptr, type, FQHEAD(ptr))), sizeof(type))                      \
        && MUTEX_UNLOCK(FQMUTEX(ptr)) == 0) ? 0 : -1)

#define FQUEUE_RESET(ptr)                                                                   \
do                                                                                          \
{                                                                                           \
    MUTEX_LOCK(FQMUTEX(ptr));                                                               \
    if(ptr)                                                                                 \
    {                                                                                       \
        if(FQ(ptr)->state)memset(FQ(ptr)->state, 0, sizeof(QSTATE));                        \
    }                                                                                       \
    MUTEX_UNLOCK(FQMUTEX(ptr));                                                             \
}while(0)

#define FQUEUE_CLEAN(ptr)                                                                   \
do{                                                                                         \
    if(ptr)                                                                                 \
    {                                                                                       \
        if(FQTAB(ptr))                                                                      \
        {                                                                                   \
            msync(FQ(ptr)->state, FQSIZE(ptr), MS_SYNC);                                    \
            munmap(FQ(ptr)->state, FQSIZE(ptr));                                            \
            FQTAB(ptr) = NULL;                                                              \
        }                                                                                   \
        if(FQ(ptr)->fd) close(FQ(ptr)->fd);                                                 \
        MUTEX_DESTROY(FQMUTEX(ptr));                                                        \
        free(ptr);                                                                          \
        ptr = NULL;                                                                         \
    }                                                                                       \
}while(0)
#ifdef __cplusplus
     }
#endif
#endif
