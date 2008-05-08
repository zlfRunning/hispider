#include <stdlib.h>
#ifndef _TRIE_H
#define _TRIE_H
#define BYTE_SIZE  256
#define WORD_MAX_SIZE 1024
typedef struct _HWORD
{
    int n;
    unsigned char ps;
    unsigned char buf[WORD_MAX_SIZE];
}HWORD;
typedef struct _HNODE
{
    unsigned char max;
    unsigned char chr;
    void *dptr;
    struct _HNODE *list;
}HNODE;
#define UNS(s) ((unsigned char)s)
#define HN(ptr) ((HNODE *)ptr)
#define HMAX(ptr) (ptr->max)
#define HCHR(ptr) (ptr->chr)
#define HNL(ptr) (ptr->list)
#define HNP(ptr, n) (&(ptr->list[n]))
#define HNPC(ptr, n) (ptr->list[n].chr)
#define HNCPY(ptr, m, n) memcpy(&(ptr->list[m]), &(ptr->list[n]), sizeof(HNODE))
#define HNUNSET(ptr, n) memset(&(ptr->list[n]), 0, sizeof(HNODE))
       //fprintf(stdout, "Ready realloc %d bytes\n", (HCNT(ptr)+1));               
#define HN_FIND(ptr, s, min, max, n)                                                    \
{                                                                                       \
    n = -1;                                                                             \
    if(HNL(ptr))                                                                        \
    {                                                                                   \
        min = 0;                                                                        \
        max = HMAX(ptr);                                                                \
        if(UNS(s) <= HNPC(ptr, min)) n = min;                                           \
        else if(UNS(s) >= HNPC(ptr, max))n = max;                                       \
        else                                                                            \
        {                                                                               \
            while(max > min)                                                            \
            {                                                                           \
                n = min + ((max - min)/2);                                              \
                if(n == min) break;                                                     \
                if(UNS(s) == HNPC(ptr, n)) break;                                       \
                else if(UNS(s) > HNPC(ptr, n)) min = n;                                 \
                else max = n;                                                           \
            }                                                                           \
        }                                                                               \
    }                                                                                   \
}                                                                                       
#define HN_RESIZE(ptr)                                                                  \
{                                                                                       \
    if(ptr->list == NULL)                                                               \
    {                                                                                   \
        ptr->list = (HNODE *)calloc((HMAX(ptr)+1), sizeof(HNODE));                      \
    }                                                                                   \
    else                                                                                \
    {                                                                                   \
        ptr->list = (HNODE *)realloc(ptr->list, (++HMAX(ptr)+1) * sizeof(HNODE));       \
    }                                                                                   \
}
 
#define HN_ADD(ptr, s, min, max, n)                                                     \
{                                                                                       \
    if(ptr)                                                                             \
    {                                                                                   \
        HN_RESIZE(ptr);                                                                 \
        if(ptr->list)                                                                   \
        {                                                                               \
            max = HMAX(ptr);                                                            \
            min = 0;                                                                    \
            if(n > 0)                                                                  \
            {                                                                           \
                min = n;                                                                \
                if(UNS(s) > HNPC(ptr, n))min =(n+1);                                    \
                if(UNS(s) < HNPC(ptr, n))min =(n-1);                                    \
            }                                                                           \
            while(max > min) HNCPY(ptr, max, --max);                                    \
            HNUNSET(ptr, max);                                                          \
            HNPC(ptr, max) = UNS(s);                                                    \
            n = max;                                                                    \
        }                                                                               \
    }                                                                                   \
}
//fprintf(stdout, "1:max:%d:%d\n", max, UNS(s));
//fprintf(stdout, "2:max:%d\n", UNS(s));
#define HN_DEL(ptr, n)                                                                  \
{                                                                                       \
    if(HN(ptr) && HNL(ptr))                                                             \
    {                                                                                   \
        while(n < HMAX(ptr)) HNCPY(ptr, n, ++n);                                        \
        HNUNSET(ptr, n);                                                                \
        MAX(ptr)--;                                                                     \
    }                                                                                   \
}
#define HN_NEXT(ptr, n, p)                                                          \
{                       \
    if(ptr && HNL(ptr)) \
    {                   \
                        \
    }                       \
}
typedef struct _TIRETAB
{
    HNODE table[BYTE_SIZE];
    HNODE *pnode;
    int count;
    int size;
    int  i ;
    int   n ;
    int min;
    int max;
}TRIETAB;
#define  HBCNT(ptr) (((TRIETAB *)ptr)->count)
#define  HBSIZE(ptr) (((TRIETAB *)ptr)->size)
#define  HBTB(ptr) (((TRIETAB *)ptr)->table)
#define  HBTBN(ptr, n) (((TRIETAB *)ptr)->table[n])
#define  NHB(ptr) (((TRIETAB *)ptr)->n)
#define  IHB(ptr) (((TRIETAB *)ptr)->i)
#define  MINHB(ptr) (((TRIETAB *)ptr)->min)
#define  MAXHB(ptr) (((TRIETAB *)ptr)->max)
#define  HBND(ptr) (((TRIETAB *)ptr)->pnode)
#define  TRIETAB_INIT() ((TRIETAB *)calloc(1, sizeof(TRIETAB)))
#define  TRIETAB_ADD(ptr, key, nkey, pdata)                                                 \
{                                                                                           \
    if(ptr && key)                                                                          \
    {                                                                                       \
        HBND(ptr) = (HNODE *)&(HBTBN(ptr, UNS(key[0])));                                    \
        IHB(ptr) = 1;                                                                       \
        do                                                                                  \
        {                                                                                   \
            if(HBND(ptr))                                                                   \
            {                                                                               \
                HN_FIND(HBND(ptr), key[IHB(ptr)], MINHB(ptr), MAXHB(ptr), NHB(ptr));        \
                if(NHB(ptr) < 0 || HNPC(HBND(ptr), NHB(ptr)) != key[IHB(ptr)])              \
                {                                                                           \
                    HN_ADD(HBND(ptr), key[IHB(ptr)], MINHB(ptr), MAXHB(ptr), NHB(ptr));     \
                    ++HBCNT(ptr);                                                           \
                    HBSIZE(ptr) += sizeof(HNODE);                                           \
                }                                                                           \
                HBND(ptr) = HNP(HBND(ptr), NHB(ptr));                                       \
            }                                                                               \
        }while(++IHB(ptr) < nkey);                                                          \
        if(HBND(ptr)) HBND(ptr)->dptr = pdata;                                              \
    }                                                                                       \
}

#define TRIETAB_GET(ptr, key, nkey, pdata)                                                  \
{                                                                                           \
    pdata = NULL;                                                                           \
    if(ptr && key)                                                                          \
    {                                                                                       \
        HBND(ptr) = (HNODE *)&(HBTBN(ptr, UNS(key[0])));                                    \
        IHB(ptr) = 1;                                                                       \
        do                                                                                  \
        {                                                                                   \
            if(HBND(ptr))                                                                   \
            {                                                                               \
                HN_FIND(HBND(ptr), key[IHB(ptr)], MINHB(ptr), MAXHB(ptr), NHB(ptr));        \
                if(NHB(ptr) < 0 || HNPC(HBND(ptr), NHB(ptr)) != key[IHB(ptr)])              \
                {                                                                           \
                    HBND(ptr) = NULL;                                                       \
                    break;                                                                  \
                }                                                                           \
                HBND(ptr) = HNP(HBND(ptr), NHB(ptr));                                       \
            }else break;                                                                    \
        }while(++IHB(ptr) < nkey);                                                          \
        if(HBND(ptr)) pdata = HBND(ptr)->dptr;                                              \
    }                                                                                       \
}                                                                                           

#define TRIETAB_DEL(ptr, key, nkey, pdata)                                                  \
{                                                                                           \
    if(ptr && key)                                                                          \
    {                                                                                       \
        HBND(ptr) = (HNODE *)&(HBTBN(ptr, (*key)));                                         \
        for(IHB(ptr) = 1; IHB(ptr) < nkey; IHB(ptr)++)                                      \
        {                                                                                   \
            HN_FIND(HBND(ptr), key[IHB(ptr)], MINHB(ptr), MAXHB(ptr), NHB(ptr));            \
            if(NHB(ptr) < 0 || HNPC(HBND(ptr), NHB(ptr)) != key[IHB(ptr)])                  \
            {                                                                               \
                HBND(ptr) = NULL;                                                           \
                pdata = NULL;                                                               \
                break;                                                                      \
            }                                                                               \
            if(IHB(ptr) == (nkey -1))                                                       \
            {                                                                               \
                pdata = HNP(HBND(ptr), NHB(ptr))->dptr;                                     \
                HN_DEL(HBND(ptr), NHB(ptr));                                                \
            }                                                                               \
            else                                                                            \
            {                                                                               \
                HBND(ptr) = HNP(HBND(ptr), NHB(ptr));                                       \
            }                                                                               \
        }                                                                                   \
    }                                                                                       \
}
#define TRIETAB_CLEAN(ptr)\
{\
  if(ptr)\
  {\
     \
  }\
}
#endif
