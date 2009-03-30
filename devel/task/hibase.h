#ifndef _HIBASE_H_
#define _HIBASE_H_
#define FTYPE_INT 		0x01
#define FTYPE_FLOAT		0x02
#define FTYPE_TEXT		0x04
#define FTYPE_ALL 		(FTYPE_INT|FTYPE_FLOAT|FTYPE_TEXT)	
#define FIELD_NUM_MAX		256
#define FIELD_NAME_MAX		256
#define REGX_SIZE_MAX		65536
/* field */
typedef struct _IFILED
{
	short  type;
	short  template_id;
	int    off;
	int    len;
}IFIELD;
/* table */
typedef struct _ITABLE
{

	short id;
	short nfields;
	IFIELD fields[FIELD_NUM_MAX];
	char name[FIELD_NAME_MAX];
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
	short docid;
	short nfields;
	IREGX map[FIELD_NAME_MAX];
	char  regx[REGX_SIZE_MAX];

}ITEMPLATE;
#endif
