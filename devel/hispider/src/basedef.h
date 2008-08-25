#ifndef _BASEDEF_H
#define _BASEDEF_H
#define URLENCODE(p, ps)                                                            \
{                                                                                   \
    if((*p >= 'a' && *p <= 'z') || ('A' <= *p && *p <= 'Z')                         \
            || ('0' <= *p && *p <= '9') || *p == '-' || *p == '_' || *p == '.')     \
    *ps++ = *p++;                                                                   \
    else ps += sprintf((char *)ps, "%%%02X", *((unsigned char *)p++));             \
}
#endif

