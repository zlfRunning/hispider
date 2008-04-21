#ifndef _BASEDEF_H
#define _BASEDEF_H
#ifndef _TABLE_OP
#define _TABLE_OP
#include "hash.h"
#define TABLE_SIZE 65535
#define TABLE_INIT(size) (void *)hash_new(size)
#define TABLE_ADD(table, key, val) hash_add(((hash *)table), (const char *)key, (void *)val)
#define TABLE_GET(table, key) hash_get(((hash *)table), (const char *)key)
#define TABLE_DELETE(table, key) hash_remove(((hash *)table), (const char *)key)
#define TABLE_COUNT(table) ((table)?hash_size(((hash *)table)):0)
#define TABLE_DESTROY(table) if(table){hash_destroy(((hash *)table));}
#endif
#define URLENCODE(p, ps)                                                            \
{                                                                                   \
    if((*p >= 'a' && *p <= 'z') || ('A' <= *p && *p <= 'Z')                         \
            || ('0' <= *p && *p <= '9') || *p == '-' || *p == '_' || *p == '.')     \
    *ps++ = *p++;                                                                   \
    else ps += sprintf((char *)ps, "%%%02X", *((unsigned char *)p++));             \
}
#endif

