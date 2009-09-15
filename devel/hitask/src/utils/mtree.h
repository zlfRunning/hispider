#ifndef _MTREE_H
#define _MTREE_H
#define MT_INCRE_NUM   1000
typedef struct _MTNODE
{
    void *data;
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
    int qleft_first;
    int qleft_last;
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
MTREE *mtree_init(char *file);
int mtree_insert(MTREE *mtree, int parentid, int key);
int mtree_remove(MTREE *mtree, int parentid, int key);
int mtree_close(MTREE *mtree);
#endif
