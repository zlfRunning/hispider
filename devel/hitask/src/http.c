#include "http.h"
#define HEX2CH(c, x) ( ((x = (c - '0')) >= 0 && x < 10) \
        || ((x = (c - 'a')) >= 0 && (x += 10) < 16) \
        || ((x = (c - 'A')) >= 0 && (x += 10) < 16) )

int http_encode(char *src, int src_len, char *dst)
{
    int i = 0, j = 0;
    char base64_map[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    for (; i < src_len - src_len % 3; i += 3)
    {
        dst[j++] = base64_map[(src[i] >> 2) & 0x3F];
        dst[j++] = base64_map[((src[i] << 4) & 0x30) + ((src[i + 1] >> 4) & 0xF)];
        dst[j++] = base64_map[((src[i + 1] << 2) & 0x3C) + ((src[i + 2] >> 6) & 0x3)];
        dst[j++] = base64_map[src[i + 2] & 0x3F];
    }

    if (src_len % 3 == 1) 
    {
        dst[j++] = base64_map[(src[i] >> 2) & 0x3F];
        dst[j++] = base64_map[(src[i] << 4) & 0x30];
        dst[j++] = '=';
        dst[j++] = '=';
    }
    else if (src_len % 3 == 2) 
    {
        dst[j++] = base64_map[(src[i] >> 2) & 0x3F];
        dst[j++] = base64_map[((src[i] << 4) & 0x30) + ((src[i + 1] >> 4) & 0xF)];
        dst[j++] = base64_map[(src[i + 1] << 2) & 0x3C];
        dst[j++] = '=';
    }
    dst[j] = '\0';
    return j;
}

int http_decode(char *src, int src_len, char *dst)
{
    int i = 0, j = 0;
    char base64_decode_map[256] = {
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 62, 255, 255, 255, 63, 52, 53, 54, 
        55, 56, 57, 58, 59, 60, 61, 255, 255, 255, 0, 255, 
        255, 255, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 
        255, 255, 255, 255, 255, 255, 26, 27, 28, 29, 30, 31, 
        32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 
        46, 47, 48, 49, 50, 51, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
        255, 255, 255, 255, 255, 255};
    for (; i < src_len; i += 4) 
    {
        dst[j++] = base64_decode_map[src[i]] << 2 |
            base64_decode_map[src[i + 1]] >> 4;
        dst[j++] = base64_decode_map[src[i + 1]] << 4 |
            base64_decode_map[src[i + 2]] >> 2;
        dst[j++] = base64_decode_map[src[i + 2]] << 6 |
            base64_decode_map[src[i + 3]];
    }
    dst[j] = '\0';
    return j;
}

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
        if(http_req->nline == 0) http_req->nline = 1;
        argv->k = http_req->nline;
        pp = http_req->line + http_req->nline;
        epp = http_req->line + HTTP_ARGV_LINE_MAX;
        while(s < end && *s != '\r' && argv < eargv && pp < epp)
        {
            high = 0;low = 0;
            //if(*s == '?'){argv->k = pp - http_req->line; ++s;}
            if(*s == '+'){*pp++ = 0x20; ++s;}
            else if(*s == '=')
            {
                if(argv->k > 0) argv->nk = pp - http_req->line - argv->k;
                *pp++ = '\0';
                argv->v = pp - http_req->line;
                ++s;
            }
            else if((*s == '&' || *s == 0x20 || *s == '\t')
                    && argv->k && argv->v)
            {
                argv->nv = pp - http_req->line - argv->v;
                *pp++ = '\0';
                http_req->nline = pp - http_req->line;
                http_req->nargvs++;
                argv++;
                if(*s++ == '&') argv->k = pp - http_req->line;
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
            }
            else ++s;
            if(s == end && argv < eargv && argv->k && argv->v)
            {
                argv->nv = pp - http_req->line - argv->v;
                *pp++ = '\0';
                http_req->nline = pp - http_req->line;
                http_req->nargvs++;
                argv++;
            }
        }
        n = s - p;
    }
    return n;
}

/* HTTP cookie parser */
int http_cookie_parse(char *p, char *end, HTTP_REQ *http_req)
{    
    HTTP_KV *cookie = NULL, *ecookie = NULL;
    char *s = NULL, *pp = NULL, *epp = NULL;
    int n = 0, high = 0, low = 0;

    if(p && end && (s = p) < end && http_req)
    {
        cookie = &(http_req->cookies[http_req->ncookies]);
        ecookie = &(http_req->cookies[HTTP_COOKIES_MAX+1]);
        if(http_req->nhline == 0) http_req->nhline = 1;
        cookie->k = http_req->nhline;
        pp = http_req->hlines + http_req->nhline;
        epp = http_req->hlines + HTTP_HEADER_MAX;
        while(s < end && *s != '\r' && cookie < ecookie && pp < epp)
        {
            high = 0;low = 0;
            if(*s == '+'){*pp++ = 0x20; ++s;}
            else if(*s == '=')
            {
                if(cookie->k > 0) cookie->nk = pp - http_req->hlines - cookie->k;
                *pp++ = *s++;
                cookie->v = pp - http_req->hlines;
            }
            else if((*s == ';' || *s == 0x20 || *s == '\t')
                    && cookie->k && cookie->v)
            {
                cookie->nv = pp - http_req->hlines - cookie->v;
                *pp++ = *s;
                http_req->nhline = pp - http_req->hlines;
                http_req->ncookies++;
                cookie++;
                if(*s++ == ';') cookie->k = pp - http_req->hlines;
                while(*s == 0x20) ++s;
            }
            else if(*s == '%' && s < (end - 2) && HEX2CH(*(s+1), high)  && HEX2CH(*(s+2), low))
            {
                *pp++ = (high << 4) | low;
                s += 3;
            }
            else if(cookie->k || cookie->v)
            {
                *pp++ = *s++;
            }
            else ++s;
            if((s == end || *s == '\r') && cookie < ecookie && cookie->k && cookie->v)
            {
                cookie->nv = pp - http_req->hlines - cookie->v;
                http_req->nhline = pp - http_req->hlines;
                http_req->ncookies++;
                cookie++;
                break;
            }
        }
        n = s - p;

    }
    return n;
}

/* HTTP HEADER parser */
int http_request_parse(char *p, char *end, HTTP_REQ *http_req)
{
    char *s = p, *ps = NULL, *pp = NULL, *sp = NULL;
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
            if(i == HEAD_REQ_COOKIE)
            {
                s += http_cookie_parse(s, end, http_req);
                //fprintf(stdout, "cookie:%s\n", s);
                pp = http_req->hlines + http_req->nhline;
            }
            else if(i == HEAD_REQ_AUTHORIZATION && strncasecmp(s, "Basic", 5) == 0)
            {
                while(s < end && *s != 0x20 && *s != '\r')*pp++ = *s++;
                while(s < end && *s == 0x20) *pp++ = *s++;
                sp = s;
                while(s < end && *s != '\r' && *s != 0x20) ++s;
                http_req->auth.k = pp - http_req->hlines;
                pp += http_decode(sp, (s - sp), pp);
                sp = http_req->hlines + http_req->auth.k;
                while(sp < pp && *sp != ':') sp++;
                if(*sp == ':')
                {
                    http_req->auth.nk = sp -  http_req->hlines - http_req->auth.k;
                    http_req->auth.v = sp + 1 - http_req->hlines;
                    http_req->auth.nv = pp -  http_req->hlines - http_req->auth.v;
                }
                http_req->nhline = pp - http_req->hlines;
            }
            else
            {
                while(s < end && *s != '\r')*pp++ = *s++;
                http_req->nhline = pp - http_req->hlines + 1;
            }
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
    if((p = buf) && (n = sprintf(p, "%s %s HTTP/1.0\r\nHost: %s\r\n"
                    "Cookie: %s\r\nAuthorization: %s\r\nConnection: close\r\n\r\n", "GET",
                    "/search?hl=zh-CN&client=safari&rls=zh-cn"
                    "&newwindow=1&q=%E5%A5%BD&btnG=Google+%E6%90%9C%E7%B4%A2&meta=&aq=f&oq=",
                    "abc.com", "acddsd=dakhfksf; abc=dkjflasdff; "
                    "abcd=%E4%BD%A0%E5%A5%BD%E9%A9%AC; "
                    "你是水?=%E4%BD%A0%E5%A5%BD%E9%A9%AC", 
                    "Basic YWRtaW46YWtkamZsYWRzamZs"
                    "ZHNqZmxzZGpmbHNkamZsa3NkamZsZHNm")) > 0)
    {
        end = p + n;
        if(http_request_parse(p, end, &http_req) != -1)
        {
            if((n = sprintf(buf, "%s", "client=safari&rls=zh-cn"
                            "&q=base64%E7%BC%96%E7%A0%81%E8%A7%84%E5%88%99"
                            "&ie=UTF-8&oe=UTF-8")) > 0)
            {
                end = buf + n;
                http_argv_parse(buf, end, &http_req);
            }
            fprintf(stdout, "---------------------STATE-----------------------\n"); 
            fprintf(stdout, "HTTP reqid:%d\npath:%s\nnargvs:%d\nncookie:%d\n", 
                    http_req.reqid, http_req.path, http_req.nargvs, http_req.ncookies);
            if(http_req.auth.nk > 0 || http_req.auth.nv > 0)
            {
                fprintf(stdout, "Authorization: %.*s => %.*s\n",
                        http_req.auth.nk,  http_req.hlines + http_req.auth.k, 
                        http_req.auth.nv, http_req.hlines + http_req.auth.v);
            }
            fprintf(stdout, "---------------------STATE END---------------------\n"); 
            fprintf(stdout, "---------------------HEADERS---------------------\n"); 
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
                if((n = http_req.headers[i]) > 0)
                {
                    fprintf(stdout, "%s %s\n", http_headers[i].e, http_req.hlines + n);
                }
            }
            fprintf(stdout, "---------------------HEADERS END---------------------\n"); 
            fprintf(stdout, "---------------------COOKIES---------------------\n"); 
            for(i = 0; i < http_req.ncookies; i++)
            {
                fprintf(stdout, "%d: %.*s => %.*s\n", i, 
                        http_req.cookies[i].nk, http_req.hlines + http_req.cookies[i].k, 
                        http_req.cookies[i].nv, http_req.hlines + http_req.cookies[i].v);
            }
            fprintf(stdout, "---------------------COOKIES END---------------------\n"); 
            fprintf(stdout, "---------------------ARGVS-----------------------\n"); 
            for(i = 0; i < http_req.nargvs; i++)
            {
                fprintf(stdout, "%d: %s[%d]=>%s[%d]\n", i, 
                        http_req.line + http_req.argvs[i].k, http_req.argvs[i].nk,
                        http_req.line + http_req.argvs[i].v, http_req.argvs[i].nv);
            }
            fprintf(stdout, "---------------------ARGVS END---------------------\n"); 
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
