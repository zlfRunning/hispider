#ifndef _HIBASE_H_
#define _HIBASE_H_
#ifdef __cplusplus
extern "C" {
#endif
#define FTYPE_INT 		        0x01
#define FTYPE_FLOAT		        0x02
#define FTYPE_TEXT		        0x04
#define FTYPE_ALL 		        (FTYPE_INT|FTYPE_FLOAT|FTYPE_TEXT)	
#define FIELD_NUM_MAX		    256
#define PATTERN_NUM_MAX         8
#define FIELD_NAME_MAX		    32
#define TABLE_NAME_MAX		    32
#define TEMPLATE_NAME_MAX	    32
#define HIBASE_PATH_MAX		    256
#define REGX_SIZE_MAX		    4096
#define F_IS_LINK               0x01
#define F_IS_IMAGE              0x02
#define F_IS_MULTI              0x04
#define F_IS_INDEX              0x08
#define TABLE_INCRE_NUM         256
#define TEMPLATE_INCRE_NUM      10000
#define PNODE_INCRE_NUM         10000
#define PNODE_CHILDS_MAX        10000
#define TAB_STATUS_ERR          -1
#define TAB_STATUS_OK           1
#define TEMP_STATUS_ERR         -1
#define TEMP_STATUS_OK          1
/* field */
typedef struct _IFILED
{
    short  data_type;
    short  template_id;
    int    flag;
    int    off;
    int    len;
    char   name[FIELD_NAME_MAX];
}IFIELD;
/* table */
typedef struct _ITABLE
{
    short   status;
    short   nfields;
    char    name[TABLE_NAME_MAX];
    IFIELD  fields[FIELD_NUM_MAX];
}ITABLE;
/* page node */
#define PNODE_NUM_MAX   256
#define PNODE_NAME_MAX  64
typedef struct _PNODE
{
    short status;
    short level;
    int id;
    int nchilds;
    int parent;
    int first;
    int last;
    int prev;
    int next;
    char name[PNODE_NAME_MAX];
}PNODE;
typedef struct _IREGX
{
    short table_id;
    short field_id;
    short page_id;
    short flag;
}IREGX;
#define RP_IS_PUBLIC 0x01
#define RP_IS_MULTI  0x02
#define RP_IS_HTML   0x04
#define RP_IS_IMAGE  0x08
#define RP_IS_LINK   0x10
/* regular expression */
typedef struct _IPATTERN
{
    int   flags;
    short status;
    short nfields;
    IREGX map[FIELD_NUM_MAX];
    char  text[REGX_SIZE_MAX];
}IPATTERN;
/* template */
typedef struct _ITEMPLATE
{
    short status;
    short npatterns;
    IPATTERN patterns[PATTERN_NUM_MAX];
    char  name[TEMPLATE_NAME_MAX];
}ITEMPLATE;
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
    void    *mtable;
    void    *mtemplate;
    void    *mpnode;
    HIO     tableio;
    HIO     templateio;
    HIO     pnodeio;
    void    *qpnode;
    int     pnode_childs_max;
    void    *logger;
    void    *mutex;
    char    basedir[HIBASE_PATH_MAX];

    int 	(*set_basedir)(struct _HIBASE *, char *dir);
    int     (*table_exists)(struct _HIBASE *, char *name, int len);
    int 	(*add_table)(struct _HIBASE *, ITABLE *tab);
    int	    (*get_table)(struct _HIBASE *, int table_id, char *table_name, ITABLE *table);
    int 	(*update_table)(struct _HIBASE *, int table_id, ITABLE *tab);
    int 	(*delete_table)(struct _HIBASE *, int table_id, char *table_name);
    int     (*template_exists)(struct _HIBASE *, char *name, int len);
    int 	(*add_template)(struct _HIBASE *, ITEMPLATE *);
    int 	(*get_template)(struct _HIBASE *, int template_id, char *template_name, ITEMPLATE *);
    int 	(*update_template)(struct _HIBASE *, int template_id, ITEMPLATE *);
    int 	(*delete_template)(struct _HIBASE *, int template_id, char *template_name);
    int     (*pnode_exists)(struct _HIBASE *, char *name, int name_len);
    int     (*add_pnode)(struct _HIBASE *, int parent, char *name);
    int     (*get_pnode)(struct _HIBASE *, int id, PNODE *pnode);
    int     (*get_pnode_childs)(struct _HIBASE *, int id, PNODE *pnodes);
    int     (*update_pnode)(struct _HIBASE *, int id, char *name);
    int     (*delete_pnode)(struct _HIBASE *, int id, char *name);
    void 	(*clean)(struct _HIBASE **);	
}HIBASE;
/* hibase initialize */
HIBASE *hibase_init();
#ifdef __cplusplus
 }
#endif
#endif
