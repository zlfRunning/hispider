#include "http.h"
#define HEX2CH(c, x) ( ((x = (c - '0')) >= 0 && x < 10) \
        || ((x = (c - 'a')) >= 0 && (x += 10) < 16) \
        || ((x = (c - 'A')) >= 0 && (x += 10) < 16) )
/* parse argv line */
int http_argv_parse(char *p, char *end, HTTP_REQ *http_req)
{
    char *pp = NULL, *epp = NULL, *s = NULL;
    HTTP_ARG *argv = NULL, *eargv = NULL;
    int n = 0, high = 0, low = 0;

    if(p && end && (s = p) < end && http_req)
    {
        argv = &(http_req->argvs[http_req->nargvs]);
        eargv = &(http_req->argvs[HTTP_ARGVS_MAX+1]);
        argv->k = pp = http_req->line + http_req->nline;
        epp = http_req->line + HTTP_ARGV_LINE_MAX;
        while(s < end && *s != '\r' && argv < eargv && pp < epp)
        {
            high = 0;low = 0;
            if(*s == '?'){argv->k = pp; ++s;}
            else if(*s == '+'){*pp++ = 0x20; ++s;}
            else if(*s == '=')
            {
                if(argv->k) argv->nk = pp - argv->k;
                *pp++ = '\0';
                argv->v = pp;
                ++s;
            }
            else if((*s == '&' || *s == 0x20 || *s == '\t')
                    && argv->k && argv->v)
            {
                argv->nv = pp - argv->v;
                *pp++ = '\0';
                http_req->nline = pp - http_req->line;
                http_req->nargvs++;
                argv++;
                if(*s++ == '&') argv->k = pp;
                else break;
            }
            else if(*s == '%' && s < (end - 2) && HEX2CH(*(s+1), high)  && HEX2CH(*(s+2), low))
            {
                *pp++ = (high << 4) | low;
                s += 3;
            }
            else if(argv->k || argv->v)
            {
                *pp++ = *s++;
                if(s == (end - 1))
                {
                    argv->nv = pp - argv->v;
                    *pp++ = '\0';
                    http_req->nline = pp - http_req->line;
                    http_req->nargvs++;
                    argv++;
                }
            }
            else ++s;
        }
        n = s - p;
    }
    return n;
}

/* HTTP HEADER parser */
int http_request_parse(char *p, char *end, HTTP_REQ *http_req)
{
    char *s = p, *ps = NULL, *pp = NULL;
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
        pp = http_req->hlines + 1;
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
                    http_req->headers[i] = pp - http_req->hlines;
                    ret++;
                    break;
                }
            }
            while(s < end && *s != '\r')*pp++ = *s++;
            *pp++ = '\0';
            ++s;
            while(s < end && *s != '\n')++s;
            ++s;
        }
        ret++;
    }
    return ret;
}

/* HTTP response parser */
int http_response_parse(char *p, char *end, HTTP_RESPONSE *http_response)
{
    char *s = p, *pp = NULL;
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
        pp = http_response->hlines + 1;
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
                    http_response->headers[i] = pp - http_response->hlines;
                    ret++;
                    break;
                }
            }
            while(s < end && *s != '\r')*pp++ = *s++;
            *pp++ = '\0';
            ++s;
            while(s < end && *s != '\n')++s;
            ++s;
        }
        ret++;
    }
    return ret;
}

#ifdef _DEBUG_HTTP
int main(int argc, char **argv)
{
    HTTP_REQ http_req = {0};
    HTTP_RESPONSE http_resp = {0};
    char buf[HTTP_BUFFER_SIZE], block[HTTP_BUFFER_SIZE], *p = NULL, *end = NULL;
    int i = 0, n = 0;
    /* test request parser */
    if((p = buf) && (n = sprintf(p, "%s %s HTTP/1.0\r\nHost: %s\r\n\r\n", "GET",
                    "/search?hl=zh-CN&client=safari&rls=zh-cn&newwindow=1&q=%E5%A5%BD&btnG=Google+%E6%90%9C%E7%B4%A2&meta=&aq=f&oq=",
                    "abc.com")) > 0)
    {
        end = p + n;
        if(http_request_parse(p, end, &http_req) != -1)
        {
            fprintf(stdout, "sizeof(HTTP_REQ):%d reqid:%d\npath:%s\nnargvs:%d:%d\n", 
                    sizeof(HTTP_REQ), http_req.reqid, 
                    http_req.path, http_req.nargvs, http_req.nline);
            if((n = sprintf(buf, "%s", "client=safari&rls=zh-cn&q=base64%E7%BC%96%E7%A0%81%E8%A7%84%E5%88%99&ie=UTF-8&oe=UTF-8")) > 0)
            {
                end = buf + n;
                http_argv_parse(buf, end, &http_req);
                fprintf(stdout, "reqid:%d\npath:%s\nnargvs:%d:%d\n", http_req.reqid, 
                        http_req.path, http_req.nargvs, http_req.nline);
            }
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
                if((n = http_req.headers[i]) > 0)
                {
                    fprintf(stdout, "%s %s\n", http_headers[i].e, http_req.hlines + n);
                }
            }
            for(i = 0; i < http_req.nargvs; i++)
            {
                fprintf(stdout, "%d: %s[%d]=>%s[%d]\n", i, 
                        http_req.argvs[i].k, http_req.argvs[i].nk,
                        http_req.argvs[i].v, http_req.argvs[i].nv);
            }
        }
    }
    if((n = sprintf(buf, "HTTP/1.0 403 Forbidden\r\nContent-Type: text/html; charset=UTF-8\r\n"
                    "Date: Tue, 21 Apr 2009 01:32:56 GMT\r\n"
                    "Server: gws\r\nCache-Control: private, x-gzip-ok=\"\"\r\n"
                    "Connection: Close\r\n\r\n")) > 0)
    {
        end = buf + n;
        if(http_response_parse(buf, end, &http_resp) != -1)
        {
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
                if((n = http_resp.headers[i]) > 0)
                {
                    fprintf(stdout, "%s %s\n", http_headers[i].e, http_resp.hlines + n);
                }
            }
        }
    }
}
#endif
