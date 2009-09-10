#ifndef _HIBASE_H_
#define _HIBASE_H_
#ifdef __cplusplus
extern "C" {
#endif
#define FTYPE_INT 		        0x01
#define FTYPE_FLOAT		        0x02
#define FTYPE_TEXT		        0x04
#define FTYPE_BLOB		        0x08
#define FTYPE_ALL 		        (FTYPE_INT|FTYPE_FLOAT|FTYPE_TEXT|FTYPE_BLOB)	
#define FIELD_NUM_MAX		    256
#define HI_INDEX_MAX		    32
#define PATTERN_NUM_MAX         8
#define FIELD_NAME_MAX		    32
#define TABLE_NAME_MAX		    32
#define F_IS_INDEX              0x01
#define F_IS_NULL               0x02
#define TEMPLATE_NAME_MAX	    32
#define HIBASE_PATH_MAX		    256
#define PATTERN_LEN_MAX		    4096
#define TABLE_INCRE_NUM         256
#define TEMPLATE_INCRE_NUM      1000
#define PNODE_INCRE_NUM         10000
#define URLNODE_INCRE_NUM       10000
#define PNODE_CHILDS_MAX        10000
#define TAB_STATUS_ERR          -1
#define TAB_STATUS_INIT         0
#define TAB_STATUS_OK           1
#define FIELD_STATUS_INIT       0
#define FIELD_STATUS_OK         1
#define TEMPLATE_STATUS_ERR     -1
#define TEMPLATE_STATUS_INIT    0
#define TEMPLATE_STATUS_OK      1
#define URLNODE_STATUS_ERR      -1
#define URLNODE_STATUS_INIT     0
#define URLNODE_STATUS_OK       1
#ifndef HI_URL_MAX
#define HI_URL_MAX              4096
#endif
#ifndef HI_BUF_SIZE
#define HI_BUF_SIZE             262144
#endif
/* field */
typedef struct _IFIELD
{
    short   status;
    short   type;
    int     flag;
    int     uid;
    char    name[FIELD_NAME_MAX];
}IFIELD;
/* table */
typedef struct _ITABLE
{
    short   status;
    short   nfields;
    int     uid;
    char    name[TABLE_NAME_MAX];
    IFIELD  fields[FIELD_NUM_MAX];
}ITABLE;
/* page node */
#define PNODE_NUM_MAX   256
#define PNODE_NAME_MAX  256
typedef struct _PNODE
{
    short status;
    short level;
    int id;
    int uid;
    int nchilds;
    int parent;
    int first;
    int last;
    int prev;
    int next;
    int ntemplates;
    int template_first;
    int template_last;
    int nurlnodes;
    int urlnode_first;
    int urlnode_last;
    char name[PNODE_NAME_MAX];
}PNODE;
typedef struct _URLNODE
{
    short status;
    short level;
    int id;
    int parentid;
    int nchilds;
    int urlid;
    int nodeid;
    int node_prev;
    int node_next;
    int first;
    int last;
    int prev;
    int next;
}URLNODE;
#define REG_IS_URL               0x01
#define REG_IS_FILE              0x02
#define REG_IS_NEED_CLEARHTML    0x04
#define REG_IS_NEED_ANTISPAM     0x08
#define REG_IS_POST              0x10
#define REG_IS_UNIQE             0x20
typedef struct _IREGX
{
    short flag;
    short fieldid;
    int   nodeid;
}IREGX;
/* PCRE RES */
typedef struct _PRES
{
    int start;
    int end;
}PRES;
#define TMP_IS_PUBLIC       0x01
#define TMP_IS_GLOBAL       0x02
#define TMP_IS_IGNORECASE   0x04
#define TMP_IS_LINK         0x08
#define TMP_IS_POST         0x10
/* template regular expression */
typedef struct _ITEMPLATE
{
    short status;
    short nfields;
    IREGX map[FIELD_NUM_MAX];
    char  pattern[PATTERN_LEN_MAX];
    char  url[HI_URL_MAX];
    char  link[HI_URL_MAX];
    int   tableid;
    IREGX linkmap;
    int   flags;
    int prev;
    int next;
}ITEMPLATE;
/* state info */
typedef struct _ISTATE
{
    int templateio_current;
    int templateio_left;
    int templateio_total;
    int urlnodeio_current;
    int urlnodeio_left;
    int urlnodeio_total;
    int urlnode_task_current;
}ISTATE;
/* hibase io/map */
typedef struct _HIO
{
    int     fd;
    int     current;
    int     total;
    int     left;
    off_t   end;
    off_t   size;
    void    *map;
}HIO;
/* hibase */
typedef struct _HIBASE
{
    void    *mdb;
    int     db_uid_max;
    void    *mpnode;
    HIO     tableio;
    HIO     pnodeio;
    void    *qpnode;
    int     pnode_childs_max;
    int     uid_max;
    HIO     templateio;
    void    *qtemplate;
    HIO     urlnodeio;
    void    *qurlnode;
    void    *qtask;
    void    *qwait;
    ISTATE  *istate;
    void    *logger;
    void    *mutex;
    char    basedir[HIBASE_PATH_MAX];

    int 	(*set_basedir)(struct _HIBASE *, char *dir);
    int     (*db_uid_exists)(struct _HIBASE *, int table_id, char *name, int len);
    int 	(*add_table)(struct _HIBASE *, char *name);
    int	    (*get_table)(struct _HIBASE *, int table_id, ITABLE *table);
    int 	(*rename_table)(struct _HIBASE *, int table_id, char *table_name);
    int 	(*delete_table)(struct _HIBASE *, int table_id);
    int	    (*view_table)(struct _HIBASE *, int table_id, char *block);
    int	    (*view_database)(struct _HIBASE *, char *block);
    int 	(*list_table)(struct _HIBASE *, char *block);
    int     (*add_field)(struct _HIBASE *, int table_id, 
            char *name, int type, int flag);
    int     (*update_field)(struct _HIBASE *, int table_id, int field_id, 
            char *name, int type, int is_index);
    int     (*delete_field)(struct _HIBASE *, int table_id, int field_id);
    int     (*add_template)(struct _HIBASE *, int pnodeid, ITEMPLATE *ptemplate);
    int     (*get_template)(struct _HIBASE *, int templateid, ITEMPLATE *ptemplate);
    int     (*update_template)(struct _HIBASE *, int templateid, ITEMPLATE *ptemplate);
    int     (*delete_template)(struct _HIBASE *, int pnodeid, int templateid);
    int     (*view_templates)(struct _HIBASE *, int pnodeid, char *block);
    /*
    int     (*template_exists)(struct _HIBASE *, char *name, int len);
    int 	(*add_template)(struct _HIBASE *, ITEMPLATE *);
    int 	(*get_template)(struct _HIBASE *, int template_id, char *template_name, ITEMPLATE *);
    int 	(*update_template)(struct _HIBASE *, int template_id, ITEMPLATE *);
    int 	(*delete_template)(struct _HIBASE *, int template_id, char *template_name);
    */
    int     (*pnode_exists)(struct _HIBASE *, int parent, char *name, int name_len);
    int     (*add_pnode)(struct _HIBASE *, int parent, char *name);
    int     (*get_pnode)(struct _HIBASE *, int id, PNODE *pnode);
    int     (*get_pnode_templates)(struct _HIBASE *, int id, ITEMPLATE **templates);
    void    (*free_templates)(ITEMPLATE *templates);
    int     (*get_pnode_childs)(struct _HIBASE *, int id, PNODE *pnodes);
    int     (*view_pnode_childs)(struct _HIBASE *, int id, char *block);
    int     (*update_pnode)(struct _HIBASE *, int id, char *name);
    int     (*delete_pnode)(struct _HIBASE *, int id);
    int     (*add_urlnode)(struct _HIBASE *, int nodeid, int parentid, int urlid, int level);
    int     (*update_urlnode)(struct _HIBASE *, int urlnodeid, int level);
    int     (*delete_urlnode)(struct _HIBASE *, int urlnodeid);
    int     (*get_urlnode)(struct _HIBASE *, int urlnodeid, URLNODE *urlnode);
    int     (*get_urlnode_childs)(struct _HIBASE *, int urlnodeid, URLNODE **childs);
    int     (*get_pnode_urlnodes)(struct _HIBASE *, int nodeid, URLNODE **urlnodes);
    void    (*free_urlnodes)(URLNODE *urlnodes);
    int     (*pop_urlnode)(struct _HIBASE *, URLNODE *urlnode);
    int     (*pop_task_urlnodeid)(struct _HIBASE *);
    void 	(*clean)(struct _HIBASE **);	
}HIBASE;
/* hibase initialize */
HIBASE *hibase_init();
#ifdef __cplusplus
 }
#endif
#endif
