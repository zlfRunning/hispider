#ifndef _DOCTYPE_H
#define _DOCTYPE_H
typedef struct _DOCTYPE_MAP
{
    void *map;
    int num;
}DOCTYPE_MAP;
/* initialize doctype */
int doctype_map_init(DOCTYPE_MAP *doctype_map);
/* add doctype */
int doctype_add(DOCTYPE_MAP *doctype_map, char *doctype, int len);
/* add doctype line */
int doctype_add_line(DOCTYPE_MAP *doctype_map, char *p, char *end);
/* return doctype id*/
int doctype_id(DOCTYPE_MAP *doctype_map, char *doctype, int len);
/* clean doctype map*/
void doctype_map_clean(DOCTYPE_MAP *doctype_map);
#endif
