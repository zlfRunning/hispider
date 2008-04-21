#include "http.h"

/* HTTP HEADER parser */
int http_response_parse(char *p, char *end, HTTP_RESPONSE *http_response)
{
    char *s = p;
    int i  = 0, ret = 0;

    while(s < end && *s != 0x20 && *s != 0x09)++s;
    while(s < end && (*s == 0x20 || *s == 0x09))++s;
    for(i = 0; i < HTTP_RESPONSE_NUM; i++ )
    {
        if(memcmp(response_status[i].e, s, response_status[i].elen) == 0)
        {
            http_response->respid = i;
        }
    }
    while(s < end)
    {
        //parse response  code 
        /* ltrim */
        while(*s == 0x20)s++;
        for(i = 0; i < HTTP_HEADER_NUM; i++)
        {
            if( (end - s) >= http_headers[i].elen
                    && strncasecmp(s, http_headers[i].e, http_headers[i].elen) == 0)
            {
                s +=  http_headers[i].elen;
                while(s < end && *s == 0x20)s++;
                http_response->headers[i] = s;
                ret++;
                break;
            }
        }
        while(s < end && *s != '\r')++s;
        *s++ = '\0';
        while(s < end && *s != '\n')++s;
        *s++ = '\0';
    }
    return ret;
}
