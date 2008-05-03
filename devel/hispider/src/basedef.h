#ifndef _BASEDEF_H
#define _BASEDEF_H
#ifndef _TABLE_OP
#define _TABLE_OP
#include "hash.h"
#define TABLE_SIZE 65535
#define HSH(ptr) ((hash *)ptr)
#define HK(k) ((const char *)k)
#define HV(v) ((void *)v)
#define TABLE_INIT(sz) (void *)hash_new(sz)
#define TABLE_ADD(ptr, k, v) {if(ptr)hash_add(HSH(ptr), HK(k), HV(v));}
#define TABLE_GET(ptr, k) ((ptr) ? hash_get(HSH(ptr), HK(k)) : NULL)
#define TABLE_DELETE(ptr, k) {if(ptr)hash_remove(HSH(ptr), HK(k));}
#define TABLE_COUNT(ptr) ((ptr)? hash_size(HSH(ptr)) : 0 )
#define TABLE_DESTROY(ptr) {if(ptr)hash_destroy(HSH(ptr));}
#endif
#define URLENCODE(p, ps)                                                            \
{                                                                                   \
    if((*p >= 'a' && *p <= 'z') || ('A' <= *p && *p <= 'Z')                         \
            || ('0' <= *p && *p <= '9') || *p == '-' || *p == '_' || *p == '.')     \
    *ps++ = *p++;                                                                   \
    else ps += sprintf((char *)ps, "%%%02X", *((unsigned char *)p++));             \
}
#endif

