#ifndef _MTREE_H
#define _MTREE_H
#define MTREE_INCRE_NUM   10000
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
    off_t size;
    void *start;
    MTSTATE *state;
    MTNODE *map;
    int fd;
    void *mutex;
}MTREE;
void *mtree_init(char *file);
int mtree_new_tree(void *mtree, int key);
int mtree_insert(void *mtree, int rootid, int key, int *old);
void mtree_view_tree(void *mtree, int rootid, FILE *fp);
void mtree_remove(void *mtree, int tnodeid, int *key);
void mtree_remove_tree(void *mtree, int rootid);
void mtree_close(void *mtree);
#endif
