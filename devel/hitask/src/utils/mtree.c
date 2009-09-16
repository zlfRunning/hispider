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
        if(MT(x)->size == 0)                                                \
        {                                                                   \
            MT(x)->size = sizeof(MTSTATE);                                  \
        }                                                                   \
        MT(x)->size += MT_INCRE_NUM * sizeof(MTNODE);                       \
        ftruncate(MT(x)->fd, MT(x)->size);                                  \
        MT_MMAP(x, MT(x)->size);                                            \
        MT(x)->state->left += MT_INCRE_NUM;                                 \
        if(MT(x)->state->left == MT_INCRE_NUM) MT(x)->state->left--;        \
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

    if(x && parentid >= 0)
    {
        MUTEX_LOCK(MT(x)->mutex);
        if(MT(x)->state && MT(x)->map && parentid < MT(x)->state->total)
        {
            nodeid = parentid;
            while(nodeid >= 0)
            {
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
            //new node
            if(id == 0)
            {
                if(MT(x)->state->left == 0)
                {
                    MT_INCRE(x);
                }
                if(MT(x)->state->qleft > 0)
                {
                    id = MT(x)->state->qfirst;
                    MT(x)->state->qfirst = MT(x)->map[id].parent;
                    MT(x)->state->qleft--;
                }
                else
                {
                    id = ++(MT(x)->state->current);
                }
                MT(x)->state->left--;
                MT(x)->map[id].parent = nodeid;
                MT(x)->map[id].key = key;
                if(key > MT(x)->map[nodeid].key) 
                    MT(x)->map[nodeid].right = id;
                else
                    MT(x)->map[nodeid].left = id;
            }
            else
            {
                fprintf(stdout, "%s::%d old id:%d pid:%d key:%d\n", __FILE__, __LINE__, id, parentid, key);
            }
        }
        MUTEX_UNLOCK(MT(x)->mutex);
    }
    return id;
}

void mtree_view_tnode(void *x, int tnodeid, FILE *fp)
{
    int id = 0;

    if(x)
    {
        if(MT(x)->map[tnodeid].left > 0 && MT(x)->map[tnodeid].left < MT(x)->state->total)
        {
            mtree_view_tnode(x, MT(x)->map[tnodeid].left, fp);
        }
        fprintf(fp, "[%d]\n", MT(x)->map[tnodeid].key);
        if(MT(x)->map[tnodeid].right > 0 && MT(x)->map[tnodeid].right < MT(x)->state->total)
        {
            mtree_view_tnode(x, MT(x)->map[tnodeid].right, fp);
        }
    }
    return ;
}

void mtree_view(void *x, int parentid, FILE *fp)
{
    int id = 0;

    if(x && parentid >= 0)
    {
        MUTEX_LOCK(MT(x)->mutex);
        if(MT(x)->map && MT(x)->state && parentid < MT(x)->state->total)
        {
             mtree_view_tnode(x, parentid, fp);
        }
        MUTEX_UNLOCK(MT(x)->mutex);
    }
    return ;
}

/* remove node */
void mtree_remove_node(void *x, int tnodeid, int *key)
{
    int id = 0, pid = 0;

    if(x && tnodeid > 0)
    {
        MUTEX_LOCK(MT(x)->mutex);
        if(MT(x)->map && MT(x)->state && tnodeid < MT(x)->state->total)
        {
            *key = MT(x)->map[tnodeid].key;
            if((id = MT(x)->map[tnodeid].left) > 0)
            {
                //find max on left->right 
                while(id > 0 && id < MT(x)->state->total)
                {
                    if(MT(x)->map[id].right > 0)
                    {
                        id = MT(x)->map[id].right;
                    }
                    else break;
                }
                //reset node[id]->parent->right
                pid = MT(x)->map[id].parent;
                if(id != tnodeid && pid > 0 && pid < MT(x)->state->total)
                {
                    MT(x)->map[pid].right = 0;
                }
            }
            else if((id = MT(x)->map[tnodeid].right) > 0)
            {
                while(id > 0 && id < MT(x)->state->total)
                {
                    if(MT(x)->map[id].left != 0)
                    {
                        id = MT(x)->map[id].left;
                    }
                    else break;
                }
                pid = MT(x)->map[id].parent;
                if(id != tnodeid && pid > 0 && pid < MT(x)->state->total)
                {
                    MT(x)->map[pid].left = 0;
                }
            }
            if(id  > 0 && MT(x)->state->total)
            {
                MT(x)->map[id].left = 0;
                if(id != MT(x)->map[tnodeid].left) 
                    MT(x)->map[id].left  = MT(x)->map[tnodeid].left;
                MT(x)->map[id].right = 0;
                if(id != MT(x)->map[tnodeid].right) 
                    MT(x)->map[id].right = MT(x)->map[tnodeid].right;
                pid = MT(x)->map[id].parent = MT(x)->map[tnodeid].parent;
                if(pid > 0 && pid < MT(x)->state->total)
                {
                    if(MT(x)->map[id].key > MT(x)->map[pid].key)
                        MT(x)->map[pid].left = id;
                    else
                        MT(x)->map[pid].right = id;
                }
            }
            //add to qleft
            memset(&(MT(x)->map[tnodeid]), 0, sizeof(MTNODE));
            if(MT(x)->state->qleft == 0)
            {
                MT(x)->state->qfirst = MT(x)->state->qlast = tnodeid;
            }
            else
            {
                id = MT(x)->state->qlast;
                MT(x)->map[id].parent = tnodeid;
                MT(x)->state->qlast = tnodeid;
            }
            MT(x)->state->qleft++;
            MT(x)->state->left--;
        }
        MUTEX_UNLOCK(MT(x)->mutex);
    }
    return ;
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
    int i = 0, id = 0, j = 0, old = 0;

    if((mtree = mtree_init("/tmp/test.mtree")))
    {
        for(i = 1; i < 100; i++)
        {
            old = 0;
            id = mtree_insert(mtree, 0, i, &old);
            for(j = 100; j < 200; j++)
            {
                old = 0;
                mtree_insert(mtree, id, j, &old);
            }
        }
        mtree_view(mtree, 1, stdout);
        mtree_close(mtree);
    }
}
#endif
