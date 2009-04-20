#include "http.h"
#define HEX2CH(c, x) ( ((x = (c - '0')) >= 0 && x < 10) \
        || ((x = (c - 'a')) >= 0 && (x += 10) < 16) \
        || ((x = (c - 'A')) >= 0 && (x += 10) < 16) )

/* parse argv line */
int http_argv_parse(char *p, char *end, HTTP_REQ *http_req)
{
    char *pp = NULL, *epp = NULL, *s = NULL, high = 0, low = 0;
    HTTP_ARG *argv = NULL, *eargv = NULL;
    int n = 0;

    if(p && end && (s = p) < end && http_req)
    {
        argv = http_req->argvs + http_req->nargvs * sizeof(HTTP_ARG);
        eargv = http_req->argvs + HTTP_ARGVS_MAX * sizeof(HTTP_ARG);
        if(http_req->eargv == NULL) http_req->eargv = http_req->argvline;
        argv->k = pp = http_req->eargv;
        epp = pp + HTTP_ARGV_LINE_MAX;
        while(s < end && *s != '\r' && argv < eargv && pp < epp)
        {
            high = 0;low = 0;
            if(*s == '?') {argv->k = pp;++s;}
            else if(*s == '%' && s < (end - 2) && HEX2CH(*(s+1), high)  && HEX2CH(*(s+2), low))
            {
                *pp++ = (high << 4) | low;
                s += 3;
            }
            else if(*s == '+'){*pp++ = 0x20; ++s;}
            else if(*s == '&' || *s == 0x20 || *s == '\t')
            {
                if(argv->v) 
                {
                    argv->nv = pp - argv->v;
                    http_req->nargvs++;
                    argv++;
                }
                *pp++ = '\0';
                argv->k = pp;
                http_req->eargv = pp;
                ++s;
            }
            else if(*s == '=')
            {
                if(argv->k){argv->nk = pp - argv->k;}
                *pp++ = '\0';
                argv->v = pp;
                ++s;
            }
            else
            {
                *pp++ = *s++;
            }
        }
        n = s - p;
    }
    return n;
}

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
                s += http_methods[i].elen;
                break;
            }
        }
        //path
        while(s < end && *s == 0x20)s++;
        //fprintf(stdout, "%s:%d path:%s\n", __FILE__, __LINE__, s);
        ps = http_req->path;
        while(s < end && *s != 0x20 && *s != '\r' && *s != '?')*ps++ = *s++;
        *ps = '\0';
        if(*s == '?') s += http_argv_parse(++s, end, http_req);
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
        ret++;
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
                s += response_status[i].elen;
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
        ret++;
    }
    return ret;
}

#ifdef _DEBUG_HTTP
int main(int argc, char **argv)
{
    HTTP_REQ http_req = {0};
    char buf[HTTP_BUFFER_SIZE], *p = NULL, *end = NULL;
    int i = 0, n = 0;
    /* test request parser */
    if((p = buf) && (n = sprintf(p, "%s %s HTTP/1.0\r\nHost: %s\r\n\r\n", "GET",
                    "/search?hl=zh-CN&client=safari&rls=zh-cn&newwindow=1&q=%E5%A5%BD&btnG=Google+%E6%90%9C%E7%B4%A2&meta=&aq=f&oq=",
                    "abc.com")) > 0)
    {
        end = p + n;
        if(http_request_parse(p, end, &http_req) != -1)
        {
            fprintf(stdout, "reqid:%d\npath:%s\nnargvs:%d\n", http_req.reqid, 
                    http_req.path, http_req.nargvs);
            for(i = 0; i < http_req.nargvs; i++)
            {
                fprintf(stdout, "%d: %s=>%s\n", i, http_req.argvs[i].k, http_req.argvs[i].v);
            }
        }
    }

}
#endif
