#ifndef _MTREE_H
#define _MTREE_H
#define MT_INCRE_NUM   1000
typedef struct _MTNODE
{
    int key;
    int left;
    int right;
    int parent;
}MTNODE;
typedef struct _MTSTATE
{
    int left;
    int current;
    int total;
    int qleft;
    int qfirst;
    int qlast;
}MTSTATE;
typedef struct _MTREE
{
    void *start;
    off_t size;
    MTSTATE *state;
    MTNODE *map;
    MTNODE *node;
    int fd;
    void *mutex;
}MTREE;
void *mtree_init(char *file);
int mtree_insert(void *mtree, int parentid, int key, int *old);
int mtree_remove(void *mtree, int parentid, int key);
void mtree_close(void *mtree);
#endif
