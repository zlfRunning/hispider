#include "http.h"
#ifdef _HTTP_CHARSET_CONVERT
#include <iconv.h>
#include "chardet.h"
#include "zstream.h"
#define CHARSET_MAX 256
#endif
static char *wdays[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
#ifndef _STATIS_YMON
#define _STATIS_YMON
static char *ymonths[]= {
    "Jan", "Feb", "Mar",
    "Apr", "May", "Jun",
    "Jul", "Aug", "Sep",
    "Oct", "Nov", "Dec"};
#endif
#define HEX2CH(c, x) ( ((x = (c - '0')) >= 0 && x < 10) \
        || ((x = (c - 'a')) >= 0 && (x += 10) < 16) \
        || ((x = (c - 'A')) >= 0 && (x += 10) < 16) )
#define URLENCODE(dst, src)                                                     \
do                                                                              \
{                                                                               \
    while(*src != '\0')                                                         \
    {                                                                           \
        if(*src == 0x20)                                                        \
        {                                                                       \
            *dst++ = '+';                                                       \
            ++src;                                                              \
        }                                                                       \
        else if(*((unsigned char *)src) > 127)                                  \
        {                                                                       \
            dst += sprintf(dst, "%%%02X", *((unsigned char *)src));             \
            ++src;                                                              \
        }                                                                       \
        else *dst++ = *src++;                                                   \
    }                                                                           \
    *dst = '\0';                                                                \
}while(0)
#define URLDECODE(s, end, high, low, pp)                                            \
do                                                                                  \
{                                                                                   \
    if(*s == '%' && s < (end - 2) && HEX2CH(*(s+1), high)  && HEX2CH(*(s+2), low))  \
    {                                                                               \
        *pp++ = (high << 4) | low;                                                  \
        s += 3;                                                                     \
    }                                                                               \
    else *pp++ = *s++;                                                              \
}while(0)

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

int http_decode(unsigned char *src, int src_len, unsigned char *dst)
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
        dst[j++] = (base64_decode_map[src[i]] << 2) 
            | (base64_decode_map[src[i + 1]] >> 4);
        dst[j++] = (base64_decode_map[src[i + 1]] << 4) 
            | (base64_decode_map[src[i + 2]] >> 2);
        dst[j++] = (base64_decode_map[src[i + 2]] << 6) 
            | (base64_decode_map[src[i + 3]]);
    }
    dst[j] = '\0';
    return j;
}

/* parse argv line */
int http_argv_parse(char *p, char *end, HTTP_REQ *http_req)
{
    char *pp = NULL, *epp = NULL, *s = NULL;
    HTTP_KV *argv = NULL, *eargv = NULL;
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
            else if(*s == '&' && argv->k && argv->v)
                    //|| *s == 0x20 || *s == '\t')
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
        if(*s == '?') s += http_argv_parse(s+1, end, http_req);
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
                if((end - s) >= http_headers[i].elen
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
                pp += http_decode((unsigned char *)sp, (s - sp), (unsigned char *)pp);
                sp = http_req->hlines + http_req->auth.k;
                while(sp < pp && *sp != ':') sp++;
                if(*sp == ':')
                {
                    http_req->auth.nk = sp -  http_req->hlines - http_req->auth.k;
                    http_req->auth.v = sp + 1 - http_req->hlines;
                    http_req->auth.nv = pp -  http_req->hlines - http_req->auth.v;
                }
            }
            else
            {
                while(s < end && *s != '\r')*pp++ = *s++;
            }
            *pp++ = '\0';
            http_req->nhline = pp - http_req->hlines;
            ++s;
            while(s < end && *s != '\n')++s;
            ++s;
        }
        ret++;
    }
    return ret;
}

/* HTTP response parser */
int http_response_parse(char *p, char *end, HTTP_RESPONSE *http_resp)
{
    int i  = 0, ret = -1, high = 0, low = 0;
    HTTP_KV *cookie = NULL, *ecookie = NULL;
    char *s = p, *pp = NULL, *epp = NULL;

    if(p && end)
    {
        pp = http_resp->hlines;
        epp = http_resp->hlines + HTTP_HEADER_MAX;
        while(s < end && *s != 0x20 && *s != 0x09)*pp++ = *s++;
        while(s < end && (*s == 0x20 || *s == 0x09))*pp++ = *s++;
        for(i = 0; i < HTTP_RESPONSE_NUM; i++ )
        {
            if(memcmp(response_status[i].e, s, response_status[i].elen) == 0)
            {
                http_resp->respid = i;
                while(*s != '\r' && *s != '\n' && *s != '\0') *pp++ = *s++;
                while(*s == '\r' || *s == '\n')++s;
                break;
            }
        }
        *pp++ = '\0';
        cookie = &(http_resp->cookies[http_resp->ncookies]);
        ecookie = &(http_resp->cookies[HTTP_COOKIES_MAX + 1]);
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
                    http_resp->headers[i] = pp - http_resp->hlines;
                    ret++;
                    break;
                }
            }
            if(i == HEAD_RESP_SET_COOKIE && cookie < ecookie)
            {
                cookie->k = pp - http_resp->hlines;
                while(s < end && *s != '\r' && *s != '=' && pp < epp) 
                {
                    URLDECODE(s, end, high, low, pp);
                }
                if(*s == '=')
                {
                    cookie->nk = pp - http_resp->hlines - cookie->k;
                    *pp++ = *s++;
                    cookie->v = pp - http_resp->hlines;
                    while(s < end && *s != ';' && *s != 0x20 && *s != '\r' && pp < epp) 
                    {
                        URLDECODE(s, end, high, low, pp);
                    }
                    cookie->nv = pp - http_resp->hlines - cookie->v;
                    cookie++;
                    http_resp->ncookies++;
                }
                while(s < end && *s != '\r' && pp < epp) 
                {
                    URLDECODE(s, end, high, low, pp);
                }
            }
            else
            {
                while(s < end && *s != '\r' && pp < epp)*pp++ = *s++;
            }
            *pp++ = '\0';
            http_resp->nhline = pp - http_resp->hlines;
            ++s;
            while(s < end && *s != '\n')++s;
            ++s;
        }
        ret++;
    }
    return ret;
}
/* return HTTP key/value */
int http_kv(HTTP_KV *kv, char *line, int nline, char **key, char **val)
{
    int ret = -1;

    if(kv && line && key && val)
    {
        if(kv->k > 0 && kv->k < nline && kv->nk > 0 && kv->nk < nline)
        {
            *key = &(line[kv->k]);
            line[kv->k + kv->nk] = '\0';
            ret++;
        }
        if(kv->v > 0 && kv->v < nline && kv->nv > 0 && kv->nv < nline)
        {
            *val = &(line[kv->v]);
            line[kv->v + kv->nv] = '\0';
            ret++;
        }
    }
    return ret;
}
/* HTTP charset convert */
int http_charset_convert(char *content_type, char *content_encoding, char *data, int len,   
                char *tocharset, int is_need_compress, char **out)
{
    int nout = 0; 
#ifdef _HTTP_CHARSET_CONVERT
    char charset[CHARSET_MAX], *rawdata = NULL, *txtdata = NULL, *todata = NULL, 
         *zdata = NULL, *p = NULL, *ps = NULL, *outbuf = NULL;
    size_t nrawdata = 0, ntxtdata = 0, ntodata = 0, nzdata = 0, 
           ninbuf = 0, noutbuf = 0, n = 0;
    chardet_t pdet = NULL;
    iconv_t cd = NULL;

    if(content_type && content_encoding && data && len > 0 && tocharset && out
        && strncasecmp(content_type, "text", 4) == 0)
    {
        *out = NULL;
        if(strncasecmp(content_encoding, "gzip", 4) == 0)
        {
            nrawdata =  len * 8 + Z_HEADER_SIZE;
            if((rawdata = (char *)calloc(1, nrawdata)))
            {
                if((httpgzdecompress((Bytef *)data, len, 
                    (Bytef *)rawdata, (uLong *)&nrawdata)) == 0)
                {
                    txtdata = rawdata;
                    ntxtdata = nrawdata;
                }
                else goto err_end;
            }
            else goto err_end;
        }
        else if(strncasecmp(content_encoding, "deflate", 7) == 0)
        {
            nrawdata =  len * 8 + Z_HEADER_SIZE;
            if((rawdata = (char *)calloc(1, nrawdata)))
            {
                if((zdecompress((Bytef *)data, len, (Bytef *)rawdata, (uLong *)&nrawdata)) == 0)
                {
                    txtdata = rawdata;
                    ntxtdata = nrawdata;
                }
                else goto err_end;
            }
            else goto err_end;
        }
        else 
        {
            txtdata = data;
            ntxtdata = len;
        }
        memset(charset, 0, CHARSET_MAX);
        //charset detactor
        if(txtdata && ntxtdata > 0 && chardet_create(&pdet) == 0)
        {
            if(chardet_handle_data(pdet, txtdata, ntxtdata) == 0
                    && chardet_data_end(pdet) == 0 )
            {
                chardet_get_charset(pdet, charset, CHARSET_MAX);
            }
            chardet_destroy(pdet);
        }
        //convert string charset 
        if(txtdata && ntxtdata > 0)
        {
            if(strcasecmp(charset, tocharset) != 0 
                    && (cd = iconv_open(tocharset, charset)) != (iconv_t)-1)
            {
                p = txtdata;
                ninbuf = ntxtdata;
                n = noutbuf = ninbuf * 8;
                if((ps = outbuf = (char *)calloc(1, noutbuf)))
                {
                    if(iconv(cd, &p, &ninbuf, &ps, (size_t *)&n) == (size_t)-1)
                    {
                        free(outbuf);
                        outbuf = NULL;
                    }
                    else
                    {
                        noutbuf -= n;
                        todata = outbuf;
                        ntodata = noutbuf;
                    }
                }
                iconv_close(cd);
            }
            else
            {
                todata = txtdata;
                ntodata = ntxtdata;
            }
        }else goto err_end;
        if(is_need_compress && todata && ntodata > 0)
        {
            nzdata = ntodata + Z_HEADER_SIZE;
            if((zdata = (char *)calloc(1, nzdata)))
            {
                if(zcompress((Bytef *)todata, ntodata, 
                    (Bytef *)zdata, (uLong * )&(nzdata)) != 0)
                {
                    free(zdata);
                    zdata = NULL;
                    nzdata = 0;
                }
            }
        }
err_end:
        if(todata == data){todata = NULL;ntodata = 0;}
        if(zdata)
        {
            *out = zdata; nout = nzdata;
            if(outbuf){free(outbuf);outbuf = NULL;}
            if(rawdata){free(rawdata); rawdata = NULL;}
        }
        else if(todata)
        {
            if(rawdata && todata != rawdata)
            {
                free(rawdata); rawdata = NULL;
            }
            *out = todata; nout = ntodata;
        }
    }
#endif
    return nout;
}
/* HTTP charset convert data free*/
void http_charset_convert_free(char *data)
{
    if(data) free(data);
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
                    "Date: Tue, 21 Apr 2009 01:32:56 GMT\r\nContent-Length: 0\r\n"
                    "Server: gws\r\nCache-Control: private, x-gzip-ok=\"\"\r\n"
                    "Connection: Close\r\n%s\r\n\r\n",
                    "Set-Cookie: 你是水?=%E4%BD%A0%E5%A5%BDa; "
                    "expires=Wed, 22-Apr-2009 03:12:37 GMT; path=/; domain=abc.com\r\n"
                    "Set-Cookie: 你是菜鸟=%E4%BD%A0%E5%A5%BDb; "
                    "expires=Wed, 22-Apr-2009 03:12:37 GMT; path=/; domain=abc.com\r\n"
                    "Set-Cookie: 你是?=%E4%BD%A0%E5%A5%BDc; "
                    "expires=Wed, 22-Apr-2009 03:12:37 GMT; path=/; domain=abc.com"
                   )) > 0)
    {
        end = buf + n;
        if(http_response_parse(buf, end, &http_resp) != -1)
        {

            fprintf(stdout, "---------------------HEADERS---------------------\n"); 
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
                if((n = http_resp.headers[i]) > 0)
                {
                    fprintf(stdout, "%s %s\n", http_headers[i].e, http_resp.hlines + n);
                }
            }
            fprintf(stdout, "---------------------HEADERS END---------------------\n"); 
            fprintf(stdout, "---------------------COOKIES---------------------\n"); 
            for(i = 0; i < http_resp.ncookies; i++)
            {
                fprintf(stdout, "%d: %.*s => %.*s\n", i, 
                        http_resp.cookies[i].nk, http_resp.hlines + http_resp.cookies[i].k, 
                        http_resp.cookies[i].nv, http_resp.hlines + http_resp.cookies[i].v);
            }
            fprintf(stdout, "---------------------COOKIES END---------------------\n"); 

        }
    }
    return 0;
}
#endif
