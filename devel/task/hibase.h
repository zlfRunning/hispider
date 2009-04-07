#ifndef _HIBASE_H_
#define _HIBASE_H_
#define FTYPE_INT 		    0x01
#define FTYPE_FLOAT		    0x02
#define FTYPE_TEXT		    0x04
#define FTYPE_ALL 		    (FTYPE_INT|FTYPE_FLOAT|FTYPE_TEXT)	
#define FIELD_NUM_MAX		256
#define FIELD_NAME_MAX		32
#define TABLE_NAME_MAX		32
#define TEMPLATE_NAME_MAX	32
#define HIBASE_PATH_MAX		256
#define REGX_SIZE_MAX		4096
#define F_IS_LINK           0x01
#define F_IS_IMAGE          0x02
#define TABLE_INCRE_NUM     256
#define TEMPLATE_INCRE_NUM  10000
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
/* regular expression */
typedef struct _IREGX
{
    short table_id;
    short field_id;
}IREGX;
/* template */
typedef struct _ITEMPLATE
{
    short status;
    short nfields;
    IREGX map[FIELD_NAME_MAX];
    char  regx[REGX_SIZE_MAX];
    char  name[TEMPLATE_NAME_MAX];
}ITEMPLATE;

/* iomap */
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
    char    basedir[HIBASE_PATH_MAX];
    HIO     tableio;
    HIO     templateio;

    int 	(*set_basedir)(struct _HIBASE *, char *dir);
    int 	(*add_table)(struct _HIBASE *, ITABLE *tab);
    int	    (*get_table)(struct _HIBASE *, int table_id, char *table_name);
    int 	(*update_table)(struct _HIBASE *, int table_id, ITABLE *tab);
    int 	(*delete_table)(struct _HIBASE *, int table_id);
    int 	(*add_template)(struct _HIBASE *, ITEMPLATE *);
    int 	(*get_template)(struct _HIBASE *, int template_id, char *template_name);
    int 	(*update_template)(struct _HIBASE *, int template_id, ITEMPLATE *);
    int 	(*delete_template)(struct _HIBASE *, int template_id, ITEMPLATE *);
    void 	(*clean)(struct _HIBASE **);	
}HIBASE;
#endif
