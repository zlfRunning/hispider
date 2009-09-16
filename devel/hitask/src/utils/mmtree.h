#ifndef _MMTREE_H
#define _MMTREE_H
#define MMTREE_INCRE_NUM   10000
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
typedef struct _MMTREE
{
    off_t size;
    void *start;
    MTSTATE *state;
    MTNODE *map;
    int fd;
    void *mutex;
}MMTREE;
void *mmtree_init(char *file);
int mmtree_new_tree(void *mmtree, int key);
int mmtree_insert(void *mmtree, int rootid, int key, int *old);
void mmtree_view_tree(void *mmtree, int rootid, FILE *fp);
void mmtree_remove(void *mmtree, int tnodeid, int *key);
void mmtree_remove_tree(void *mmtree, int rootid);
void mmtree_close(void *mmtree);
#endif
