#include <unistd.h>
#include <stdlib.h>
#ifndef _ILIST_H
#define _ILIST_H
typedef struct _ILIST
{
    int left;
    int head;
    int tail;
    int current;
    int new_count;
    int count;
    long *new_list;
    long *list;
}ILIST;
#ifndef ILIST_OP
#define ILIST_OP
#define ILIST_BLOCK_SIZE  32
#define ILIST_BLOCK_LEFT  128
#define ILIST_INIT(ptr) ((ILIST *)calloc(1, sizeof(ILIST)))
#define ILIST_CHECK(ptr)                                                            \
{                                                                                   \
    if(ptr)                                                                         \
    {                                                                               \
        ptr->new_count = 0;                                                         \
        if(ptr->left  == 0)                                                         \
            ptr->new_count = (ptr->count + ILIST_BLOCK_SIZE);                       \
        if(ptr->left > ILIST_BLOCK_LEFT)                                            \
            ptr->new_count = ptr->count - ptr->left + 32;                           \
        if(ptr->new_count  > 0 &&  (ptr->new_list = (long *)                        \
                    calloc(ptr->new_count, sizeof(long))))                          \
        {                                                                           \
            if(ptr->list)                                                           \
            {                                                                       \
                ptr->current = 0;                                                   \
                while(ptr->head != ptr->tail)                                       \
                {                                                                   \
                    if(ptr->head == ptr->count)                                     \
                        ptr->head = 0;                                              \
                    ptr->new_list[ptr->current++] = ptr->list[ptr->head++];         \
                }                                                                   \
                free(ptr->list);                                                    \
            }                                                                       \
            ptr->list = ptr->new_list;                                              \
            ptr->new_list = NULL;                                                   \
            ptr->head = 0;                                                          \
            ptr->tail = ptr->count;                                                 \
            ptr->count = ptr->new_count;                                            \
            ptr->left = ILIST_BLOCK_SIZE;                                           \
            ptr->new_count = 0;                                                     \
            ptr->current = 0;                                                       \
        }                                                                           \
    }                                                                               \
}
#define ILIST_PUSH(ptr, off)                                                        \
{                                                                                   \
    if(ptr)                                                                         \
    {                                                                               \
        ILIST_CHECK(ptr);                                                           \
        if(ptr->left > 0)                                                           \
        {                                                                           \
            if(ptr->tail == ptr->count)                                             \
                ptr->tail = 0;                                                      \
            ptr->list[ptr->tail++] = off;                                           \
            ptr->left--;                                                            \
        }                                                                           \
    }                                                                               \
}
#define ILIST_POP(ptr, off)                                                         \
{                                                                                   \
    if(ptr)                                                                         \
    {                                                                               \
        if(ptr->head == ptr->count)                                                 \
            ptr->head = 0;                                                          \
        if(ptr->count > 0 && ptr->head != ptr->tail                                 \
            && ptr->head >= 0 && ptr->head < ptr->count)                            \
        {                                                                           \
            off = ptr->list[ptr->head];                                             \
            ptr->list[ptr->head++] = 0;                                             \
            ptr->left++;                                                            \
        }                                                                           \
    }                                                                               \
}
#define ILIST_LIST(ptr, fp)                                                         \
{                                                                                   \
    if(ptr)                                                                         \
    {                                                                               \
        if(ptr->count > 0)                                                          \
        {                                                                           \
            ptr->current = ptr->head;                                               \
            while(ptr->current != ptr->tail)                                        \
            {                                                                       \
                if(ptr->current == ptr->count)                                      \
                    ptr->current = 0;                                               \
                fprintf(stdout, "%ld\n", (long)ptr->list[ptr->current++]);          \
            }                                                                       \
        }                                                                           \
    }                                                                               \
}
#define ILIST_CLEAN(ptr)                                                            \
{                                                                                   \
    if(ptr)                                                                         \
    {                                                                               \
        if(ptr->list)                                                               \
            free(ptr->list);                                                        \
        free(ptr);                                                                  \
        ptr = NULL;                                                                 \
    }                                                                               \
}
#endif
#endif
