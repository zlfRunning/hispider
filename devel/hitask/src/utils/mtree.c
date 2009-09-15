#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "mtree.h"
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

#define MT_MMAP(x, sz)                                                      \
do                                                                          \
{                                                                           \
    if(MT(x))                                                               \
    {                                                                       \
        MT_MUNMAP(x);                                                       \
        if((MT(x)->start = mmap(NULL, sz, PROT_READ|PROT_WRITE,             \
                        MAP_SHARED, MT(x)->fd, 0)) != (void *)-1)           \
        {                                                                   \
            MT(x)->state = (MTSTATE *)MT(x)->start;                         \
            MT(x)->map = (MTNODE *)MT(x)->start + sizeof(MTSTATE);          \
            MT(x)->size = sz;                                               \
        }                                                                   \
    }                                                                       \
}while(0)

#define MT_INCRE(x)                                                         \
do                                                                          \
{                                                                           \
    if(MT(x))                                                               \
    {                                                                       \
        if(MT(x)->size == 0) MT(x)->size = sizeof(MTSTATE);                 \
        MT(x)->size += MT_INCRE_NUM * sizeof(MTNODE);                       \
        ftruncate(MT(x)->fd, MT(x)->size);                                  \
        MT_MMAP(x, MT(x)->size);                                            \
        MT(x)->state->left += MT_INCRE_NUM;                                 \
        MT(x)->state->total += MT_INCRE_NUM;                                \
    }                                                                       \
}while(0)

/* init mtree */
void *mtree_init(char *file)
{
    void *x = NULL;
    struct stat  st = {0};
    int size = 0;

    if((x = (MTREE *)calloc(1, sizeof(MTREE))))
    {
        if((MT(x)->fd = open(file, O_CREAT|O_RDWR, 0644)) > 0 
                && fstat(MT(x)->fd, &st) == 0)
        {
            MUTEX_INIT(MT(x)->mutex);
            MT(x)->size = st.st_size;
            //init truncate
            if(st.st_size == 0)
            {
                MT_INCRE(x);
            }
            else
            {
                //mmap
                MT_MMAP(x, st.st_size);
            }
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
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
        MUTEX_LOCK(MT(x)->mutex);
        if(parentid > 0 && parentid < MT(x)->state->total)
        {
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
            nodeid = parentid;
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
            while(nodeid != 0)
            {
        fprintf(stdout, "%s::%d nodeid:%d\n", __FILE__, __LINE__, nodeid);
                node = &(MT(x)->map[nodeid]);
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
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
        }
        //new node
        if(id == 0)
        {
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
            if(MT(x)->state->left == 0)
            {
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
                MT_INCRE(x);
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
            }
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
            if(MT(x)->state->qleft > 0)
            {
                id = MT(x)->state->qleft_first;
                MT(x)->state->qleft_first = MT(x)->map[id].parent;
                MT(x)->state->qleft--;
            }
            else
            {
                id = ++(MT(x)->state->current);
                MT(x)->state->left--;
            }
        fprintf(stdout, "%s::%d OK id:%d\n", __FILE__, __LINE__, id);
            MT(x)->map[id].parent = nodeid;
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
            MT(x)->map[id].key = key;
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
            if(key > MT(x)->map[nodeid].key) 
                MT(x)->map[nodeid].right = id;
            else
                MT(x)->map[nodeid].left = id;
        fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
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


#ifdef _DEBUG_MTREE
int main(int argc, char **argv) 
{
    void *mtree = NULL;
    int i = 0, j = 0, old = 0;

    if((mtree = mtree_init("/tmp/test.mtree")))
    {
        for(i = 1; i < 1000; i++)
        {
            for(j = 1; j < 10000; j++)
            {
                old = 0;
                mtree_insert(mtree, i, j, &old);
            }
        }
        mtree_close(mtree);
    }
}
#endif
