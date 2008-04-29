#include "http.h"

/* HTTP HEADER parser */
int http_request_parse(char *p, char *end, HTTP_REQ *http_req)
{
    char *s = p, *ps = NULL;
    int i  = 0, ret = -1;

    if(p && end)
    {
        //request method
        //while(s < end && *s != 0x20 && *s != 0x09)++s;
        while(s < end && (*s == 0x20 || *s == 0x09))++s;
        for(i = 0; i < HTTP_METHOD_NUM; i++ )
        {
            if(strncasecmp(http_methods[i].e, s, http_methods[i].elen) == 0)
            {
                http_req->reqid = i;
                break;
            }
        }
        //path
        while(s < end && *s != 0x20)s++;
        while(s < end && *s == 0x20)s++;
        s++;
        ps = http_req->path;
        while(s < end && *s != 0x20 && *s != '\r') *ps++ = *s++;
        while(s < end && *s != '\n')s++;
        s++;
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
                    http_req->headers[i] = s;
                    ret++;
                    break;
                }
            }
            while(s < end && *s != '\r')++s;
            *s++ = '\0';
            while(s < end && *s != '\n')++s;
            *s++ = '\0';
        }
        ret = 0;
    }
    return ret;
}

/* HTTP response parser */
int http_response_parse(char *p, char *end, HTTP_RESPONSE *http_response)
{
    char *s = p;
    int i  = 0, ret = -1;

    if(p && end)
    {
        while(s < end && *s != 0x20 && *s != 0x09)++s;
        while(s < end && (*s == 0x20 || *s == 0x09))++s;
        for(i = 0; i < HTTP_RESPONSE_NUM; i++ )
        {
            if(memcmp(response_status[i].e, s, response_status[i].elen) == 0)
            {
                http_response->respid = i;
                break;
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
        ret = 0;
    }
    return ret;
}
