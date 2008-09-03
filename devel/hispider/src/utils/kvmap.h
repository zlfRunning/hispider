#ifndef	_KVMAP_H
#define	_KVMAP_H
#define VK_MAX 16
typedef struct _KVNODE
{
    unsigned char key[VK_MAX];
    void *dptr;
    struct _KVNODE *kve_left;     
    struct _KVNODE *kve_right;    
    struct _KVNODE *kve_parent;   
    int kve_color;         
}KVNODE;
typedef struct _KVMAP
{
    KVNODE node;
    KVNODE *kvh_root;
    KVNODE *old;
    KVNODE *max;
    KVNODE *min;
    KVNODE *bnode;
    KVNODE *p;
    KVNODE *pp;
    KVNODE *head;
    KVNODE *tail;
    int count;
    int total;
    int free_count;
}KVMAP;
#define PKV(ptr) ((KVMAP *)ptr)
#define KV_NEGINF	-1
#define KV_INF	    1
#define KV_BLACK	0
#define KV_RED		1
#define KV_LEFT(elm)		(elm->kve_left)
#define KV_RIGHT(elm)		(elm->kve_right)
#define KV_PARENT(elm)		(elm->kve_parent)
#define KV_COLOR(elm)		(elm->kve_color)
#define KV_ROOT(head)       (PKV(head)->kvh_root)
#define PKV_PARENT(head)    (PKV(head)->parent)
#define PKV_GPARENT(head)   (PKV(head)->gparent)
#define PKV_CHILD(head)     (PKV(head)->child)
#define PKV_OLEFT(head)     (PKV(head)->oleft)
#define PKV_ORIGHT(head)    (PKV(head)->oright)
#define PKV_COLOR(head)     (PKV(head)->color)
#define PKV_OLD(head)       (PKV(head)->old)
#define PKV_TMP(head)       (PKV(head)->tmp)
#define PKV_MAX(head)       (PKV(head)->max)
#define PKV_MIN(head)       (PKV(head)->min)
#define PKV_MAXK(head)      (PKV(head)->max->key)
#define PKV_MINK(head)      (PKV(head)->min->key)
#define KV_EMPTY(head)      (KV_ROOT(head) == NULL)
#define KV_NODE_SET(elm, old)                                                               \
do                                                                                          \
{                                                                                           \
    KV_PARENT(elm) = KV_PARENT(old);                                                        \
    KV_LEFT(elm) = KV_LEFT(old);                                                            \
    KV_RIGHT(elm) = KV_RIGHT(old);                                                          \
    KV_COLOR(elm) = KV_COLOR(old);                                                          \
}while(0)
#define KV_SET(elm, parent)                                                                 \
do                                                                                          \
{					                                                                        \
	KV_PARENT(elm) = parent;					                                            \
	KV_LEFT(elm) = KV_RIGHT(elm) = NULL;		                                            \
	KV_COLOR(elm) = KV_RED;					                                                \
}while(0)

#define KV_SET_BLACKRED(black, red)                                                         \
do                                                                                          \
{				                                                                            \
	KV_COLOR(black) = KV_BLACK;				                                                \
	KV_COLOR(red) = KV_RED;					                                                \
}while(0)

#ifndef KV_AUGMENT
#define KV_AUGMENT(x)
#endif

#define KV_ROTATE_LEFT(head, elm, tmp)                                                      \
do                                                                                          \
{			                                                                                \
	(tmp) = KV_RIGHT(elm);					                                                \
	if ((KV_RIGHT(elm) = KV_LEFT(tmp)))                                                     \
    {		                                                                                \
		KV_PARENT(KV_LEFT(tmp)) = (elm);		                                            \
	}								                                                        \
	KV_AUGMENT(elm);						                                                \
	if ((KV_PARENT(tmp) = KV_PARENT(elm)))                                                  \
    {		                                                                                \
		if ((elm) == KV_LEFT(KV_PARENT(elm)))	                                            \
			KV_LEFT(KV_PARENT(elm)) = (tmp);	                                            \
		else							                                                    \
			KV_RIGHT(KV_PARENT(elm)) = (tmp);	                                            \
	} else								                                                    \
		KV_ROOT(head) = (tmp);				                                                \
	KV_LEFT(tmp) = (elm);					                                                \
	KV_PARENT(elm) = (tmp);					                                                \
	KV_AUGMENT(tmp);						                                                \
	if ((KV_PARENT(tmp))) KV_AUGMENT(KV_PARENT(tmp));			                            \
}while(0)

#define KV_ROTATE_RIGHT(head, elm, tmp)                                                     \
do                                                                                          \
{			                                                                                \
	(tmp) = KV_LEFT(elm);					                                                \
	if ((KV_LEFT(elm) = KV_RIGHT(tmp)))                                                     \
    {		                                                                                \
		KV_PARENT(KV_RIGHT(tmp)) = (elm);		                                            \
	}								                                                        \
	KV_AUGMENT(elm);						                                                \
	if ((KV_PARENT(tmp) = KV_PARENT(elm)))                                                  \
    {		                                                                                \
		if ((elm) == KV_LEFT(KV_PARENT(elm)))	                                            \
			KV_LEFT(KV_PARENT(elm)) = (tmp);	                                            \
		else							                                                    \
			KV_RIGHT(KV_PARENT(elm)) = (tmp);	                                            \
	} else								                                                    \
		KV_ROOT(head) = (tmp);				                                                \
	KV_RIGHT(tmp) = (elm);					                                                \
	KV_PARENT(elm) = (tmp);					                                                \
	KV_AUGMENT(tmp);						                                                \
	if ((KV_PARENT(tmp)))KV_AUGMENT(KV_PARENT(tmp));			                            \
}while(0)
/* remove node */
KVNODE *kv_remove(KVMAP *map, KVNODE *elm);
/* insert new node */
KVNODE *kv_insert(KVMAP *map, KVNODE *elm);
/* find node */
KVNODE *kv_find(KVMAP *map, KVNODE *elm);
/* next node for great */
KVNODE *kv_next(KVNODE *elm);
/* prev node for less */
KVNODE *kv_prev(KVNODE *elm);
/* find min/max node */
KVNODE *kv_minmax(KVMAP *map, int val);

#define KV_MIN(map) kv_minmax(map, KV_NEGINF)
#define KV_MAX(map)	kv_minmax(map, KV_INF)
#define KVMAP_MAX(ptr) PKV(ptr)->max
#define KVMAP_MIN(ptr) PKV(ptr)->min
#define KVMAP_INIT() (calloc(1, sizeof(KVMAP)))
/* add to min/max */
#define KVMAP_MINMAX_ADD(ptr, node)                                                         \
do                                                                                          \
{                                                                                           \
    if(PKV(ptr)->max  == NULL || node->key > PKV(ptr)->max->key)                            \
    {                                                                                       \
        PKV(ptr)->max = node;                                                               \
    }                                                                                       \
    if(PKV(ptr)->min == NULL || node->key < PKV(ptr)->min->key)                             \
        PKV(ptr)->min = node;                                                               \
}while(0)

/* rebuild min/max */
#define KVMAP_MINMAX_REBUILD(ptr, node)                                                     \
do                                                                                          \
{                                                                                           \
    if(node == PKV(ptr)->max)                                                               \
    {                                                                                       \
        PKV(ptr)->max = KV_MAX(PKV(ptr));                                                   \
    }                                                                                       \
    if(node == PKV(ptr)->min)                                                               \
    {                                                                                       \
        PKV(ptr)->min = KV_MIN(PKV(ptr));                                                   \
    }                                                                                       \
}while(0)

/* pop node from freelist */
#define KVMAP_POP_NODE(ptr, node)                                                           \
do                                                                                          \
{                                                                                           \
    if(PKV(ptr)->head)                                                                      \
    {                                                                                       \
        node = PKV(ptr)->head;                                                              \
        PKV(ptr)->head = PKV(ptr)->head->kve_parent;                                        \
        if(PKV(ptr)->head == NULL) PKV(ptr)->tail = NULL;                                   \
        memset(node, 0, sizeof(KVNODE));                                                    \
        PKV(ptr)->free_count--;                                                             \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        node = (KVNODE *)calloc(1, sizeof(KVNODE));                                         \
        PKV(ptr)->total++;                                                                  \
    }                                                                                       \
}while(0)

/* push free node to freelist */
#define KVMAP_PUSH_NODE(ptr, node)                                                          \
do                                                                                          \
{                                                                                           \
    memset(node, 0, sizeof(KVNODE));                                                        \
    if(PKV(ptr)->tail)                                                                      \
    {                                                                                       \
        PKV(ptr)->tail->kve_parent = node;                                                  \
        PKV(ptr)->tail = node;                                                              \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        PKV(ptr)->head = PKV(ptr)->tail = node;                                             \
    }                                                                                       \
    PKV(ptr)->free_count++;                                                                 \
}while(0)

/* free all nodes */
#define KVMAP_FREE_NODES(ptr)                                                               \
do                                                                                          \
{                                                                                           \
    while(PKV(ptr)->free_count > 0)                                                         \
    {                                                                                       \
        if(PKV(ptr)->head)                                                                  \
        {                                                                                   \
            PKV(ptr)->p = PKV(ptr)->head;                                                   \
            PKV(ptr)->head = PKV(ptr)->head->kve_parent;                                    \
            free(PKV(ptr)->p);                                                              \
            PKV(ptr)->p = NULL;                                                             \
        }                                                                                   \
        PKV(ptr)->free_count--;                                                             \
    }                                                                                       \
}while(0)

/* add key/val(dp) olddp is return if key exists */
#define KVMAP_ADD(ptr, nkey, dp, olddp)                                                     \
do                                                                                          \
{                                                                                           \
    olddp = NULL;                                                                           \
    if(ptr)                                                                                 \
    {                                                                                       \
        KVMAP_POP_NODE(ptr, PKV(ptr)->p);                                                   \
        if((PKV(ptr)->pp = PKV(ptr)->p))                                                    \
        {                                                                                   \
            memcpy(PKV(ptr)->p->key, nkey, VK_MAX);                                         \
            PKV(ptr)->p->dptr = dp;                                                         \
            PKV(ptr)->old = kv_insert(PKV(ptr), PKV(ptr)->p);                               \
            if(PKV(ptr)->old)                                                               \
            {                                                                               \
                olddp  = PKV(ptr)->old->dptr;                                               \
                KVMAP_PUSH_NODE(ptr, PKV(ptr)->pp);                                         \
            }                                                                               \
            else                                                                            \
            {                                                                               \
                KVMAP_MINMAX_ADD(ptr, PKV(ptr)->pp);                                        \
                PKV(ptr)->count++;                                                          \
            }                                                                               \
        }                                                                                   \
    }                                                                                       \
}while(0)
    //fprintf(stdout, "NEW[%lf][%ld]\n", PKV(ptr)->p->key, (int)(PKV(ptr)->p->dptr));\
    //fprintf(stdout, "OLD[%lf][%ld]\n", PKV(ptr)->old->key, (int)(PKV(ptr)->old->dptr));\
    if(nkey < 1){fprintf(stdout, "0:maxk:%lf maxp:%ld node->k:%lf node->p:%ld\n", nkey, dp, PKV(ptr)->p->key, PKV(ptr)->p->dptr);}   \

/* get val(dp) of nkey */
#define KVMAP_GET(ptr, nkey, dp)                                                            \
do                                                                                          \
{                                                                                           \
    dp = NULL;                                                                              \
    if(ptr)                                                                                 \
    {                                                                                       \
        memcpy(PKV(ptr)->node.key, nkey, VK_MAX);                                           \
        PKV(ptr)->bnode = &(PKV(ptr)->node);                                                \
        PKV(ptr)->p = kv_find(PKV(ptr), PKV(ptr)->bnode);                                   \
        if(PKV(ptr)->p)                                                                     \
        {                                                                                   \
            dp = PKV(ptr)->p->dptr;                                                         \
        }                                                                                   \
    }                                                                                       \
}while(0)

/* delete node(key/val) from map */
#define KVMAP_DEL(ptr, nkey, dp)                                                            \
do                                                                                          \
{                                                                                           \
    dp = NULL;                                                                              \
    if(ptr)                                                                                 \
    {                                                                                       \
        memcpy(PKV(ptr)->node.key, nkey, VK_MAX);                                           \
        PKV(ptr)->bnode = &(PKV(ptr)->node);                                                \
        PKV(ptr)->old = kv_remove(PKV(ptr), PKV(ptr)->bnode);                               \
        if(PKV(ptr)->old)                                                                   \
        {                                                                                   \
            KVMAP_PUSH_NODE(ptr, PKV(ptr)->old);                                            \
            KVMAP_MINMAX_REBUILD(ptr, PKV(ptr)->old);                                       \
            PKV(ptr)->count--;                                                              \
            dp = PKV(ptr)->old->dptr;                                                       \
        }                                                                                   \
    }                                                                                       \
}while(0)

/* pop min node */
#define KVMAP_POP_MIN(ptr, nkey, dp)                                                        \
do                                                                                          \
{                                                                                           \
    dp = NULL;                                                                              \
    if(ptr)                                                                                 \
    {                                                                                       \
        if(PKV(ptr)->min)                                                                   \
        {                                                                                   \
            memcpy(nkey, PKV(ptr)->min->key, VK_MAX);                                       \
            dp  = PKV(ptr)->min->dptr;                                                      \
            PKV(ptr)->p = PKV(ptr)->min;                                                    \
            PKV(ptr)->old = kv_remove(PKV(ptr), PKV(ptr)->p);                               \
            KVMAP_PUSH_NODE(ptr, PKV(ptr)->old);                                            \
            KVMAP_MINMAX_REBUILD(ptr, PKV(ptr)->old);                                       \
            PKV(ptr)->count--;                                                              \
        }                                                                                   \
    }                                                                                       \
}while(0)

/* pop max node */
#define KVMAP_POP_MAX(ptr, nkey, dp)                                                        \
do                                                                                          \
{                                                                                           \
    dp = NULL;                                                                              \
    if(ptr)                                                                                 \
    {                                                                                       \
        if(PKV(ptr)->max)                                                                   \
        {                                                                                   \
            memcpy(nkey, PKV(ptr)->max->key, VK_MAX);                                       \
            dp  = PKV(ptr)->max->dptr;                                                      \
            PKV(ptr)->p = PKV(ptr)->max;                                                    \
            PKV(ptr)->old = kv_remove(PKV(ptr), PKV(ptr)->p);                               \
            KVMAP_PUSH_NODE(ptr, PKV(ptr)->old);                                            \
            KVMAP_MINMAX_REBUILD(ptr, PKV(ptr)->old);                                       \
            PKV(ptr)->count--;                                                              \
        }                                                                                   \
    }                                                                                       \
}while(0)

/* find max node and pop it */
#define KVMAP_POP_RMAX(ptr, nkey, dp)                                                       \
do                                                                                          \
{                                                                                           \
    dp = NULL;                                                                              \
    if(ptr)                                                                                 \
    {                                                                                       \
        if(PKV(ptr)->max)                                                                   \
        {                                                                                   \
            PKV(ptr)->bnode = NULL;                                                         \
            PKV(ptr)->bnode = KV_MAX(PKV(ptr));                                             \
            if(PKV(ptr)->bnode)                                                             \
            {                                                                               \
                memcpy(nkey, PKV(ptr)->bnode->key, VK_MAX);                                 \
                dp = PKV(ptr)->bnode->dptr;                                                 \
                PKV(ptr)->old = kv_remove(PKV(ptr), PKV(ptr)->bnode);                       \
                KVMAP_PUSH_NODE(ptr, PKV(ptr)->old);                                        \
                PKV(ptr)->count--;                                                          \
            }                                                                               \
        }                                                                                   \
    }                                                                                       \
}while(0)

/* clean map */
#define KVMAP_CLEAN(ptr)                                                                    \
do                                                                                          \
{                                                                                           \
    if(ptr)                                                                                 \
    {                                                                                       \
        PKV(ptr)->p = NULL;                                                                 \
        while(PKV(ptr)->p = KV_MAX(PKV(ptr)))                                               \
        {                                                                                   \
                PKV(ptr)->old = kv_remove(PKV(ptr), PKV(ptr)->p);                           \
                if(PKV(ptr)->old) free(PKV(ptr)->old);                                      \
                PKV(ptr)->p = NULL;                                                         \
                PKV(ptr)->old = NULL;                                                       \
        }                                                                                   \
        KVMAP_FREE_NODES(ptr);                                                              \
        free(ptr);                                                                          \
        ptr = NULL;                                                                         \
    }                                                                                       \
}while(0)
#endif
