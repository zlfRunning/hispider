#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "MT(x).h"
#include "mutex.h"
#define MT(px) ((MTREE *)px)
#define MT_MUNMAP(x)                                                        \
do                                                                          \
{                                                                           \
    if(MT(x)->start && MT(x)->start != (void *)-1)                          \
    {                                                                       \
        msync(MT(x)->start, MT(x)->size, MS_SYNC);                          \
        munmap(MT(x)->start, MT(x)->size);                                  \
        MT(x)->start = NULL;                                                \
    }                                                                       \
}while(0)

#define MT_MAP(x, size)                                                     \
do                                                                          \
{                                                                           \
    if(MT(x))                                                               \
    {                                                                       \
        MT_MUNMAP(x)                                                        \
        if((MT(x)->start = mmap(NULL, size, PROT_READ|PROT_WRITE,           \
                        MAP_SHARED, MT(x)->fd, 0)) != (void *)-1)           \
        {                                                                   \
            MT(x)->state = (MTSTATE *)MT(x)->start;                         \
            MT(x)->map = (MTNODE *)MT(x)->start + sizeof(MTSTATE);          \
            MT(x)->size = size;                                             \
        }                                                                   \
    }                                                                       \
}while(0)

#define MT_INCRE(x)                                                         \
do                                                                          \
{                                                                           \
    if(MT(x))                                                               \
    {                                                                       \
        MT(x)->size += MT_INCRE_NUM * sizeof(MTNODE);                       \
        MT_MAP(x, MT(x)->size);                                             \
    }                                                                       \
}while(0)

/* init mtree */
void *mtree_init(char *file)
{
    void *x = NULL;
    struct stat  st = {0};
    int size = 0;

    if((MT(x) = (MTREE *)calloc(1, sizeof(MTREE))))
    {
        if((MT(x)->fd = open(file, O_CREAT|O_RDWR, 0644)) > 0 
                && fstat(MT(x)->fd, &stat) == 0)
        {
            MUTEX_INIT(MT(x)->mutex);
            //init truncate
            if(st.st_size == 0)
            {
                size = sizeof(MTSTATE) + MT_INCRE_NUM * sizeof(MTNODE);
                ftruncate(MT(x)->fd, size);
                st.st_size = (off_t) size;
            }
            //mmap
            MT_MAP(x);
        }
        else 
        {
            if(MT(x)->fd > 0) close(MT(x)->fd);
            free(x);
            x = NULL;
        }
    }
    return x;
}

/* insert new node */
int mtree_insert(void *x, int parentid, int key, int *old)
{
    int id = 0, nodeid = 0;
    MTNODE *node = NULL;

    if(x && MT(x)->state)
    {
        MUTEX_LOCK(MT(x)->mutex);
        if(parentid > 0 && parentid < MT(x)->state->total)
        {
            nodeid = parentid;
            while(nodeid != 0)
            {
                node = &(MT(x)->map[parentid]);
                if(key == node->key)
                {
                    *old = id = nodeid;
                    break;
                }
                else if(key > node->key)
                {
                    if(node->right == 0) break;
                    nodeid = node->right;
                }
                else 
                {
                    if(node->left == 0) break;
                    nodeid = node->left;
                }
            }
        }
        //new node
        if(id == 0)
        {
            if(MT(x)->state->left == 0)
            {
                MT_INCRE(x);
            }
            if(MT(x)->state->left > 0)
            {
                id = MT(x)->state->qleft_first;
                MT(x)->state->qleft_first = MT(x)->map[id].parent;
            }
            else
            {
                id = ++(MT(x)->state->current);
            }
            MT(x)->map[id].parent = nodeid;
            MT(x)->map[id].key = key;
            if(key > MT(x)->map[nodeid].key) 
                MT(x)->map[nodeid].right = id;
            else
                MT(x)->map[nodeid].left = id;
        }
        MUTEX_UNLOCK(MT(x)->mutex);
    }
    return id;
}

//close mtree
void mtree_close(void *x)
{
    if(x)
    {
        MT_MUNMAP(x);
        MUTEX_DESTROY(MT(x)->mutex);
        if(MT(x)->fd) close(MT(x)->fd);
        free(x);
    }
}

