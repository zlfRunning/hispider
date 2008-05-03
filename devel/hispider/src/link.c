#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include "link.h"
#include "md5.h"
#include "ilist.h"
#include "basedef.h"
#include "zstream.h"
#include "mutex.h"
#include "logger.h"
#include "timer.h"
#include "http.h"
#include "hio.h"

#define PARSE_RESPONSE(p, end, buffer, response)                \
{                                                               \
    p = (char *)buffer->data;                                   \
    end = (char *)buffer->end;                                  \
    if(p && end)                                                \
    {                                                           \
        while(p < (end-4))                                      \
        {                                                       \
            if(*p == '\r' && *(p+1) == '\n'                     \
                    && *(p+2) == '\r' && *(p+3) == '\n') break; \
            else ++p;                                           \
        }                                                       \
        if(p != (end-4))                                        \
        {                                                       \
            end = p+4;                                          \
            p = (char *)buffer->data;                           \
            response.header_size = (end - p);                   \
            http_response_parse(p, end, &response);             \
        }                                                       \
    }                                                           \
}
#define ADD_TO_MD5TABLE(ptr, md5str, id)                        \
{                                                               \
    if(ptr)                                                     \
    {                                                           \
        MUTEX_LOCK(ptr->mutex);                                 \
        TABLE_ADD((ptr->md5table), md5str, id);                 \
        MUTEX_UNLOCK(ptr->mutex);                               \
    }                                                           \
}
#define GET_FROM_MD5TABLE(ptr, md5str, id)                      \
{                                                               \
    if(ptr)                                                     \
    {                                                           \
        MUTEX_LOCK(ptr->mutex);                                 \
        id = (long )TABLE_GET((ptr->md5table), md5str);         \
        MUTEX_UNLOCK(ptr->mutex);                               \
    }                                                           \
}
/* set md5file */
int linktable_set_logger(LINKTABLE *linktable, char *logfile, void *logger)
{
    if(linktable)
    {
        linktable->logger = logger;
        linktable->isinsidelogger = 0;
        if(logfile) 
        {
            LOGGER_INIT(linktable->logger, logfile);
            linktable->isinsidelogger = 1;
        }
        DEBUG_LOGGER(linktable->logger, "Setting logfile %s", logfile);
        return 0;
    }
    return -1;
}


/* set md5file */
int linktable_set_md5file(LINKTABLE *linktable, char *md5file)
{
    if(linktable)
    {
        DEBUG_LOGGER(linktable->logger, "Setting md5file %s", md5file);
        HIO_SET(linktable->md5io, md5file);
        HIO_CHK(linktable->md5io);
        return 0;
    }
    return -1;
}

/* set urlfile */
int linktable_set_urlfile(LINKTABLE *linktable, char *urlfile)
{
    if(linktable)
    {
        DEBUG_LOGGER(linktable->logger, "Setting urlfile %s", urlfile);
        HIO_SET(linktable->urlio, urlfile);
        HIO_CHK(linktable->urlio);
        return 0;
    }
    return -1;
}

/* set metafile */
int linktable_set_metafile(LINKTABLE *linktable, char *metafile)
{
    if(linktable)
    {
        DEBUG_LOGGER(linktable->logger, "Setting metafile %s", metafile);
        HIO_SET(linktable->metaio, metafile);
        HIO_CHK(linktable->metaio);
        return 0;
    }
    return -1;
}

/* set docfile */
int linktable_set_docfile(LINKTABLE *linktable, char *docfile)
{
    if(linktable)
    {
        DEBUG_LOGGER(linktable->logger, "Setting docfile %s", docfile);
        HIO_SET(linktable->docio, docfile);
        HIO_CHK(linktable->docio);
        return 0;
    }
    return -1;
}

/* set ntask */
int linktable_set_ntask(LINKTABLE *linktable, int ntask)
{
    if(linktable && ntask > 0)
    {
        linktable->tasks = (URLMETA *)realloc(linktable->tasks,
                                sizeof(URLMETA *) * ntask);
        linktable->ntask = ntask;
        DEBUG_LOGGER(linktable->logger, "Setting ntask %d", ntask);
        return 0;
    }
    return -1;
}


/* Parse HTML CODE for getting links */
int linktable_parse(LINKTABLE *linktable, char *host, char *path, char *content, char *end)
{
    char *p = NULL, *s = NULL;
    char *link = NULL;
    int n = 0, pref = 0;
    void *timer = NULL;
    void *times = NULL;
    long long total = 0;
    int count = 0;

    if(linktable && host && path && content && end)	
    {
        p = content;
        TIMER_INIT(timer);
        TIMER_INIT(times);
        while(p < end)
        {
            if(p < (end - 1) && *p == '<' && (*(p+1) == 'a' || *(p+1) == 'A'))
            {
                p += 2;
                pref = 0;
                while(p < end && (*p == 0x20 || *p == 0x09)) ++p;
                //DEBUG_LOGGER(linktable->logger, "%d %c\n", __LINE__, *p);
                if(p >= (end - 4) && strncasecmp(p, "href", 4) != 0) continue;
                //fprintf(stdout, "%s\n", p);
                //DEBUG_LOGGER(linktable->logger, "%d %c\n", __LINE__, *p);
                p += 4;
                //DEBUG_LOGGER(linktable->logger, "%d %c\n", __LINE__, *p);
                while(p < end && (*p == 0x20 || *p == 0x09)) ++p;
                //DEBUG_LOGGER(linktable->logger, "%d %c\n", __LINE__, *p);
                if(*p != '=') continue;
                ++p;
                while(p < end && (*p == 0x20 || *p == 0x09)) ++p;
                //DEBUG_LOGGER(linktable->logger, "%d %c\n", __LINE__, *p);
                if(*p == '"' || *p == '\''){++p; pref = 1;}
                //DEBUG_LOGGER(linktable->logger, "%d %c\n", __LINE__, *p);
                //if(*p == '/' || (*p >= '0' && *p <= '9') 
                //        || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
                //{
                //DEBUG_LOGGER(linktable->logger, "%d %c\n", __LINE__, *p);
                link = p;
                if(pref){while(p < end && *p != '\'' && *p != '"')++p;}
                else {while(p < end && *p != 0x20 && *p != 0x09 && *p != '>')++p;}
                //DEBUG_LOGGER(linktable->logger, "left:%d\n",(end - p));
                //fprintf(stdout, "%s\n", p);
                if((n = (p - link)) > 0)
                {
                    if(*link != '#' && strncasecmp("javascript", link, 10) != 0)
                    {
                        DEBUG_LOGGER(linktable->logger, 
                                "Ready for adding URL from page[%s%s] %d", host, path, n);
                        TIMER_RESET(times);
                        linktable->add(linktable, (unsigned char *)host, 
                                (unsigned char *)path,  (unsigned char *)link,
                                (unsigned char *)p);
                        TIMER_SAMPLE(times);
                        total += PT_USEC_USED(times);
                        count++;
                        //fprintf(stdout, "%s\n", link);
                    }
                }
                //}
            }
            ++p;
        }	
        TIMER_SAMPLE(timer);
        DEBUG_LOGGER(linktable->logger, "Parsed http://%s%s count:%d times:%lld time:%lld", host, path, count, total, PT_USEC_USED(timer));
        TIMER_CLEAN(timer);
        return 0;
    }
    return -1;
}

/* add link to table */
int linktable_add(LINKTABLE *linktable, unsigned char *host, unsigned char *path, 
        unsigned char *href, unsigned char *ehref)
{
    unsigned char lhost[HTTP_HOST_MAX];
    unsigned char lpath[HTTP_PATH_MAX];
    int n = 0, isneedencode = 0, isquery = 0;
    unsigned char *p = NULL, *ps = NULL, *last = NULL;
    void *timer = NULL;

    if(linktable && host && path && href && (ehref - href) < HTTP_PATH_MAX)
    {
        memset(lhost, 0, HTTP_HOST_MAX);
        memset(lpath, 0, HTTP_PATH_MAX);
        p = href;
        if(*p == '/')
        {
            strcpy((char *)lhost, (char *)host);
            ps = lpath;
            p = href;
        }
        else if((p < (ehref - 7)) && strncasecmp((char *)p, "http://", 7) == 0)
        {
            ps = lhost;
            p = href + 7;
            n = HTTP_HOST_MAX;
            while(p < ehref && *p != '/') 
            {
                *ps++ = *p++; 
                if(--n <= 0) return 0;
            } 
            *ps = '\0';
            ps = lpath;
        }
        else
        {
            //delete file:// mail:// ftp:// news:// rss:// eg. 
            p = href;
            while(p < ehref && (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) p++;
            if(memcmp(p, "://", 3) == 0) return -1;
            strcpy((char *)lhost, (char *)host);
            p = path;
            ps = lpath;
            n = HTTP_PATH_MAX;
            while(p < ehref)
            {
                if(*p == '/') last = ps+1;
                *ps++ = *p++;
                if(--n <= 0) return 0;
            }
            if(last) ps = last ;
            p = href;
            *ps = '\0';
        }
        if(p && ps)
        {
            while(p < ehref)
            {
                //while(p < ehref && (*p == '/' && *(p+1) == '/')) ++p;
                if(*(unsigned char *)p > 127 || *p == 0x20)
                    {ps += sprintf((char *)ps, "%%%02X", *p++);}
                else *ps++ = *p++;
            }
            *ps = '\0';
            ps = lpath;
            if(*ps == '\0') *ps = '/';
        }
        if(lhost[0] == '\0' || lpath[0] == '\0') return -1;
        TIMER_INIT(timer);
        linktable->addurl(linktable, (char *)lhost, (char *)lpath);
        TIMER_SAMPLE(timer);
        DEBUG_LOGGER(linktable->logger, "addurl:http://%s%s time used:%lld ", lhost, lpath, PT_USEC_USED(timer));
        TIMER_CLEAN(timer);
    }
    return 0;
}

/* add url to linktable */
int linktable_addurl(LINKTABLE *linktable, char *host, char *path)
{
    HTTP_REQUEST req;
    char url[HTTP_URL_MAX];
    //char zurl[HTTP_URL_MAX];
    char md5str[MD5_LEN * 2 +1];
    char *p = NULL, *ip = NULL, *ps = NULL;
    int i = 0;
    //uLong n = 0, nzurl = 0;
    long long offset = 0;
    long n = 0, id = 0;
    int ret = -1;
    void *timer = NULL;

    if(linktable)
    {
        DEBUG_LOGGER(linktable->logger, "New URL:http://%s%s", host, path);
        //check dns
        if((n = sprintf(url, "http://%s%s", host, path)) > 0)
        {
            memset(&req, 0, sizeof(HTTP_REQUEST));
            memset(md5str, 0, MD5_LEN * 2 + 1);
            md5((unsigned char *)url, n, req.md5);
            p = md5str;
            for(i = 0; i < MD5_LEN; i++) p += sprintf(p, "%02x", req.md5[i]);
            id = 0;
            GET_FROM_MD5TABLE(linktable, md5str, id);
            if(id) 
            {
                DEBUG_LOGGER(linktable->logger, "URL[%d] is exists", id);
                ret = 0; 
                goto err_end;
            }
            //\n end
            url[n] = '\n';
            if(HIO_APPEND(linktable->urlio, url, n+1, offset) <= 0) 
            {
                goto err_end;
            }
            url[n] = '\0';
            //host
            ps = req.host;
            p = host;
            n = HTTP_HOST_MAX;
            while(*p != '\0' && *p != ':')
            {
                *ps++ = *p++;
                if(n-- <= 0) {goto err_end;}
            }
            //port
            req.port = 80;
            if(*p == ':') req.port = atoi(++p);
            //ip
            if((ip = linktable->getip(linktable, req.host)) == NULL) 
                goto err_end; 
            p = ip;
            ps = req.ip;
            while(*p != '\0') *ps++ = *p++;
            //path
            ps = req.path;
            p = path;
            n = HTTP_PATH_MAX;
            while(*p != '\0')
            {
                *ps++ = *p++;
                if(n-- <= 0) {goto err_end;}
            }
            req.id = id = (linktable->url_total + 1);
            if(HIO_APPEND(linktable->md5io, &req, sizeof(HTTP_REQUEST), offset) <= 0 ) 
            {
                goto err_end;
            }
            ADD_TO_MD5TABLE(linktable, md5str, (long *)id);
            linktable->url_total++;
            DEBUG_LOGGER(linktable->logger, "New[%d] URL:http://%s%s TOTAL:%d STATUS:%d", 
                    id, host, path, linktable->url_total, req.status);
            ret = 0;
        }
    }
err_end:
    return ret;
}

/* get link DNS */
char *linktable_getip(LINKTABLE *linktable, char *hostname)
{
    struct hostent *hp = NULL;
    char *ip = NULL;
    if(linktable)
    {
        if((ip = ((char *)TABLE_GET(linktable->dnstable, hostname)))) return ip; 
        DEBUG_LOGGER(linktable->logger, "Ready for [%s]'s ip", hostname);
        if((hp = gethostbyname((const char *)hostname)) == NULL) return NULL;
        MUTEX_LOCK(linktable->mutex);
        if((linktable->dnslist = (char **)realloc(linktable->dnslist, 
                        sizeof(char *) * (linktable->dnscount + 1)))
                && (ip = linktable->dnslist[linktable->dnscount++] 
                    = (char *)calloc(1, HTTP_IP_MAX)))
        {
            sprintf(ip, "%s", inet_ntoa(*((struct in_addr *)(hp->h_addr))));
            TABLE_ADD(linktable->dnstable, hostname, ip);
        }
end:
        MUTEX_UNLOCK(linktable->mutex);
        if(ip){DEBUG_LOGGER(linktable->logger, "DNS name[%s] ip[%s]", hostname, ip);}
        else {WARN_LOGGER(linktable->logger, "DNS name[%s] failed", hostname);}
    }
    return ip;
}

/* get request */
int linktable_get_request(LINKTABLE *linktable, HTTP_REQUEST *req)
{
    long long offset = 0;
    int ret = -1;
    int lock = 0;

    if(linktable && req && linktable->urlno < linktable->url_total)
    {
        offset = (linktable->urlno * sizeof(HTTP_REQUEST));
        if(HIO_RSEEK(linktable->md5io, offset) >= 0)
        {
            while(HIO_READ(linktable->md5io, req, sizeof(HTTP_REQUEST)) > 0)
            {
                linktable->urlno++;
                if(req->status == LINK_STATUS_INIT)
                {
                    ret = 0;
                    break;
                }
            }
        }
    }
    return ret;
}

/* Update link status */
int linktable_update_request(LINKTABLE *linktable, int id, int status)
{
    int ret = -1;
    long long offset = 0;

    if(linktable && id > 0 && id <= linktable->url_total)
    {
        offset = (id-1) * sizeof(HTTP_REQUEST);
        if(HIO_SWRITE(linktable->md5io, &status, sizeof(int), offset) > 0)
        {
            DEBUG_LOGGER(linktable->logger, "Update request[%d] to status[%d]", id ,status);
            ret = 0;
        }
    }
    return ret;
}

//get url task
long linktable_get_urltask_one(LINKTABLE *linktable)
{
    long long offset = 0;
    long taskid = -1;
    int n = 0;

    if(linktable && linktable->task.id == -1 && linktable->docno < linktable->doc_total)
    {
        fprintf(stdout, "%d id:%d\n", linktable->docno, linktable->task.id);
        offset = linktable->docno * sizeof(URLMETA);
        if(HIO_RSEEK(linktable->metaio, offset) >= 0)
        {
            while((n = HIO_READ(linktable->metaio, &(linktable->task), sizeof(URLMETA))) > 0)
            {
                linktable->docno++;
                DEBUG_LOGGER(linktable->logger, 
                        "task[%d] status:%d docno:%d off:%lld zsize:%d size:%d", 
                        linktable->task.id, linktable->task.status, linktable->docno, 
                        linktable->task.offset, linktable->task.zsize, linktable->task.size);
                if(linktable->task.status == URL_STATUS_INIT)
                {
                    taskid = linktable->task.id;
                    linktable->task.status = URL_STATUS_WORKING;
                    break;
                }
            }
        }
    }
    return taskid;
}

//get url task
long linktable_get_urltask(LINKTABLE *linktable)
{
    URLMETA *purlmeta = NULL;
    int i = 0;
    long taskid = -1;
    long long offset = 0;

    if(linktable && linktable->tasks && linktable->docno < linktable->doc_total)
    {
        for(i = 0; i < linktable->ntask; i++)
        {
            purlmeta = &(linktable->tasks[i]);

            offset = linktable->docno * sizeof(URLMETA);
            if(linktable->docno < linktable->doc_total
                    && purlmeta->status != URL_STATUS_WAIT 
                    && purlmeta->status != URL_STATUS_WORKING
                    && HIO_RSEEK(linktable->metaio, offset) >= 0) 
            {
                while(HIO_READ(linktable->metaio, purlmeta, sizeof(URLMETA)) > 0)
                {
                    linktable->docno++;
                    if(purlmeta->status == URL_STATUS_INIT)
                    {
                        purlmeta->status = URL_STATUS_WAIT;
                        break;
                    }
                }
            }
            if(taskid == -1 && purlmeta->status == URL_STATUS_WAIT)
            {
                taskid = i;
                purlmeta->status = URL_STATUS_WORKING;
                break;
            }
        }
    }
    return taskid;
}

/* URL task HANDLER */
void linktable_urlhandler(LINKTABLE *linktable, long taskid)
{
    URLMETA *purlmeta = NULL;
    char *data = NULL, *zdata = NULL, *host = NULL, 
         *path = NULL, *p = NULL, *end = NULL;
    uLong ndata = 0, nzdata = 0; 
    int status = URL_STATUS_ERROR;
    int i = 0, fd = 0;
    long long offset = 0;
    void *timer = NULL;

    if(linktable && linktable->tasks)
    {
        purlmeta = &(linktable->tasks[taskid]);
        if(purlmeta->status != URL_STATUS_WORKING) return ;
        if(purlmeta->zsize > 0 && purlmeta->size > 0 && purlmeta->offset >= 0
                && (data = (char *)calloc(1, purlmeta->size))
                && (zdata = (char *)calloc(1, purlmeta->zsize)))
        {
            ndata = purlmeta->size;
            nzdata = purlmeta->zsize;
            //fprintf(stdout, "line:%d id:%d offset:%lld status:%d size:%d zsize:%d\n", __LINE__,
            //purlmeta->id, purlmeta->offset, purlmeta->status, purlmeta->size, purlmeta->zsize);
            if(HIO_SREAD(linktable->docio, zdata, nzdata, purlmeta->offset) <= 0)
            {
                FATAL_LOGGER(linktable->logger, "Read from %s offset:%lld failed, %s",
                        PTH(linktable->docio), purlmeta->offset, strerror(errno));
                goto end;
            }
            if(zdecompress(zdata, nzdata, data, &ndata) != 0)
            {
                FATAL_LOGGER(linktable->logger, "Decompress data zsize[%d] to size[%d] failed, %s",
                        purlmeta->zsize, purlmeta->size, strerror(errno));
                goto end;
            }
            host = data + purlmeta->hostoff;
            path = data + purlmeta->pathoff;
            p = data + purlmeta->htmloff;
            end = data + purlmeta->size;
            linktable->parse(linktable, host, path, p, end);
            linktable->docok_total++;
            status = URL_STATUS_OVER;
        }
end:
        if(data) free(data);
        if(zdata) free(zdata);
        offset = purlmeta->id * sizeof(URLMETA); 
        if(HIO_SWRITE(linktable->metaio, &status, sizeof(int), offset) <= 0)
        {
            FATAL_LOGGER(linktable->logger, "Update meta[%d] status[%d] failed, %s",
                    purlmeta->id, status, strerror(errno));
        }
        memset(purlmeta, 0, sizeof(URLMETA));
        purlmeta->status = status;
    }
    return ;
}

/* add content as zdata */
int linktable_add_zcontent(LINKTABLE *linktable, URLMETA *purlmeta, char *zdata, int nzdata)
{
    URLMETA urlmeta;
    int ret = -1;
    long long offset = 0;

    if(linktable && purlmeta && zdata)
    {
        DEBUG_LOGGER(linktable->logger, "URL[%d] hostoff:%d pathoff:%d size:%d zsize:%d",
                purlmeta->id, purlmeta->hostoff, purlmeta->pathoff, purlmeta->size, purlmeta->zsize);
        if(HIO_APPEND(linktable->docio, zdata, nzdata, offset) <= 0)
        {
            FATAL_LOGGER(linktable->logger, "Write compressed document zsize[%d] failed, %s",
                    nzdata, strerror(errno));
            goto end;
        }
        purlmeta->id = linktable->doc_total;
        purlmeta->status = URL_STATUS_INIT;
        if(HIO_APPEND(linktable->metaio, purlmeta, sizeof(URLMETA), offset) <= 0)
        {
            FATAL_LOGGER(linktable->logger, "Write URLMETA[%d]  failed, %s",
                    purlmeta->id, strerror(errno));
            goto end;
        }
        linktable->doc_total++;
        DEBUG_LOGGER(linktable->logger, "Added URLMETA[%d] hostoff:%d pathoff:%d"
                "size:%d zsize:%d to offset:%lld",
                purlmeta->id, purlmeta->hostoff, purlmeta->pathoff, 
                purlmeta->size, purlmeta->zsize, offset);
    }
end:
    return ret;
}

/* Add content */
int linktable_add_content(LINKTABLE *linktable, void *response, 
        char *host, char *path, char *content, int ncontent)
{
    URLMETA urlmeta;
    HTTP_RESPONSE *resp = (HTTP_RESPONSE *)response;
    char buf[LBUF_SIZE], *p = NULL, *ps = NULL;
    char *data = NULL, *zdata = NULL;
    uLong ndata = 0, nzdata = 0;
    long long offset = 0;
    int i = 0, ret = -1, n = 0, nhost = 0, npath = 0;

    if(linktable && resp && host && path && content && ncontent > 0)
    {
        memset(&urlmeta, 0, sizeof(URLMETA));
        nhost = strlen(host) + 1;    
        npath = strlen(path) + 1;    
        p = buf;
        for(i = 0; i < HTTP_HEADER_NUM; i++)
        {
            if(resp->headers[i])
                p += sprintf(p, "[%d:%s:%s]", i, http_headers[i].e, resp->headers[i]);
        }
        urlmeta.id  = linktable->doc_total;
        urlmeta.status = URL_STATUS_INIT;
        urlmeta.hostoff = (p - buf);
        urlmeta.pathoff = urlmeta.hostoff + nhost;
        urlmeta.htmloff = urlmeta.pathoff + npath;
        nzdata = ndata = urlmeta.size = urlmeta.htmloff + ncontent;
        if((data = (char *)calloc(1, ndata)) == NULL 
                || (zdata = (char *)calloc(1, ndata)) == NULL) goto end;
        ps = data;
        memcpy(ps, buf, urlmeta.hostoff);
        ps += urlmeta.hostoff;
        memcpy(ps, host, nhost);
        ps += nhost;
        memcpy(ps, path, npath);
        ps += npath;
        memcpy(ps, content, ncontent);
        if(zcompress(data, nzdata, zdata, &nzdata) != 0) goto end;
        urlmeta.zsize = nzdata;
        if(HIO_APPEND(linktable->docio, zdata, nzdata, urlmeta.offset) <= 0) goto end;
        if(HIO_APPEND(linktable->metaio, &(urlmeta), sizeof(URLMETA), offset) <= 0) goto end;
        linktable->doc_total++;
        ret = 0;
        DEBUG_LOGGER(linktable->logger, "Added URLMETA[%d] hostoff:%d pathoff:%d"
                "size:%d zsize:%d to offset:%lld",
                urlmeta.id, urlmeta.hostoff, urlmeta.pathoff, 
                urlmeta.size, urlmeta.zsize, offset);
end:
        if(data) free(data);
        if(zdata) free(zdata);
    }
    /*
    */
    return ret;
}

/* Resume */
int linktable_resume(LINKTABLE *linktable)
{
    HTTP_REQUEST req;
    URLMETA urlmeta;
    int i = 0, id = -1;
    long n = 0;
    char *p = NULL, md5str[MD5_LEN * 2 +1];
    char *ip = NULL;

    if(linktable)
    {
        //request
        if(HIO_RSEEK(linktable->md5io, 0) < 0) return;
        while(HIO_READ(linktable->md5io, &req, sizeof(HTTP_REQUEST)) > 0)
        {
            p = md5str;
            for(i = 0; i < MD5_LEN; i++)
                p += sprintf(p, "%02x", req.md5[i]);
            ADD_TO_MD5TABLE(linktable, md5str, (long *)++n);
            TABLE_ADD(linktable->dnstable, req.host, req.ip);
            if(req.status == LINK_STATUS_INIT && id == -1)
            {
                id = linktable->urlno = n;
            }
            if(req.status == LINK_STATUS_OVER) linktable->urlok_total++;
            linktable->url_total++;
            n++;
        }
        //task
        id = -1;
        n = 0 ;
        if(HIO_RSEEK(linktable->metaio, 0) < 0) return;
        while(HIO_READ(linktable->metaio, &urlmeta, sizeof(URLMETA)) > 0)
        {
            if(urlmeta.status == URL_STATUS_INIT && id == -1)
            {
                id = linktable->docno = n;
            }
            if(urlmeta.status == URL_STATUS_OVER) linktable->docok_total++;
            linktable->doc_total++;
        }
    }
}

/* Clean linktable */
void linktable_clean(LINKTABLE **linktable)
{
    if(*linktable)
    {
        HIO_CLEAN((*linktable)->md5io);
        HIO_CLEAN((*linktable)->urlio);
        HIO_CLEAN((*linktable)->metaio);
        HIO_CLEAN((*linktable)->docio);
        TABLE_DESTROY((*linktable)->md5table);
        TABLE_DESTROY((*linktable)->dnstable);
        MUTEX_DESTROY((*linktable)->mutex);
        if((*linktable)->isinsidelogger) {LOGGER_CLEAN((*linktable)->logger);}
        if((*linktable)->tasks) free((*linktable)->tasks);
        if((*linktable)->dnslist)
        {
            while((*linktable)->dnscount-- > 0)
            {
                if((*linktable)->dnslist[(*linktable)->dnscount])
                    free((*linktable)->dnslist[(*linktable)->dnscount]);
            }
            free((*linktable)->dnslist);
        }
        free((*linktable));
        (*linktable) = NULL;
    }
}

/* Initialize linktable */
LINKTABLE *linktable_init()
{
    LINKTABLE *linktable = NULL;
    if((linktable = (LINKTABLE *)calloc(1, sizeof(LINKTABLE))))
    {
        MUTEX_INIT(linktable->mutex);
        HIO_INIT(linktable->md5io);
        HIO_INIT(linktable->urlio);
        HIO_INIT(linktable->metaio);
        HIO_INIT(linktable->docio);
        linktable->task.id = -1;
        linktable->md5table         = TABLE_INIT(TABLE_SIZE);
        linktable->dnstable         = TABLE_INIT(TABLE_SIZE);
        linktable->set_logger       = linktable_set_logger;
        linktable->set_md5file      = linktable_set_md5file;
        linktable->set_urlfile      = linktable_set_urlfile;
        linktable->set_metafile     = linktable_set_metafile;
        linktable->set_docfile      = linktable_set_docfile;
        linktable->set_ntask        = linktable_set_ntask;
        linktable->parse            = linktable_parse; 
        linktable->add              = linktable_add; 
        linktable->addurl           = linktable_addurl; 
        linktable->getip            = linktable_getip; 
        linktable->get_request      = linktable_get_request; 
        linktable->update_request   = linktable_update_request; 
        linktable->add_zcontent     = linktable_add_zcontent; 
        linktable->add_content      = linktable_add_content; 
        linktable->get_urltask      = linktable_get_urltask; 
        linktable->get_urltask_one  = linktable_get_urltask_one; 
        linktable->urlhandler       = linktable_urlhandler; 
        linktable->resume           = linktable_resume; 
        linktable->clean            = linktable_clean; 
        linktable->set_ntask(linktable, URL_NTASK_DEFAULT);
    }
    return linktable;
}
#ifdef _DEBUG_LINKTABLE
//gen.sh 
//gcc -o tlink -D_DEBUG_LINKTABLE link.c http.c utils/*.c -I utils/ -DHAVE_PTHREAD -lpthread -lz _D_DEBUG && ./tlink www.sina.com.cn / &
#include <pthread.h>
#include "http.h"
#include "timer.h"
#include "buffer.h"
#include "basedef.h"
#define BUF_SIZE 8192
void *pthread_handler(void *arg)
{
    int i = 0, n = 0;
    char *p = NULL, *end = NULL;
    BUFFER *buffer = NULL;
    LINKTABLE *linktable = (LINKTABLE *)arg;
    HTTP_RESPONSE response;
    int fd = 0;
    struct sockaddr_in sa;
    socklen_t sa_len;
    fd_set readset;
    char buf[BUF_SIZE];
    char *path = NULL, *hostname = NULL;
    HTTP_REQUEST request;
    int sid = 0;
    int flag = 0;
    long long count = 0;

    buffer = buffer_init();
    while(1)
    {
        fprintf(stdout, "thread[%08x] start .....logger[%08x] urlno:%d urltotal:%d\n", 
                pthread_self(), linktable->logger, linktable->urlno, linktable->url_total);
        if((sid = linktable->get_request(linktable, &request)) != -1)
        {
            fprintf(stdout, "num:%d http://%s%s %s:%d\n", sid, request.host, 
                    request.path, request.ip, request.port);
            memset(&sa, 0, sizeof(struct sockaddr_in));
            sa.sin_family = AF_INET;
            sa.sin_addr.s_addr = inet_addr(request.ip);
            sa.sin_port = htons(request.port);
            sa_len = sizeof(struct sockaddr);
            fd = socket(AF_INET, SOCK_STREAM, 0);
            n = sprintf(buf, "GET %s HTTP/1.0\r\nHOST: %s\r\nUser-Agent: Mozilla\r\n\r\n", 
                    request.path, request.host);
            if(fd > 0 &&  connect(fd, (struct sockaddr *)&sa, sa_len) == 0 && write(fd, buf, n) > 0)
            {
                fprintf(stdout, "---request----\r\n%s----[end]----\r\n", buf);
                fcntl(fd, F_SETFL, O_NONBLOCK);
                FD_ZERO(&readset);
                FD_SET(fd,&readset);
                memset(&response, 0, sizeof(HTTP_RESPONSE));
                response.respid = -1;
                buffer->reset(buffer);
                DEBUG_LOGGER(linktable->logger, "%d OK", __LINE__);
                for(;;)
                {
                    select(fd+1,&readset,NULL,NULL,NULL);
                    if(FD_ISSET(fd, &readset))
                    {
                        if((n = read(fd, buf, BUF_SIZE)) > 0)
                        {
                            buffer->push(buffer, buf, n);   
                            PARSE_RESPONSE(p, end, buffer, response);
                            if((p = response.headers[HEAD_ENT_CONTENT_TYPE])
                                    && strncasecmp(p, "text", 4) != 0)
                            {
                                response.respid = RESP_NOCONTENT;
                                fprintf(stdout, "Content-Type:%s\n", p);
                            }
                            if(response.respid != -1 && response.respid != RESP_OK)
                            {
                                DEBUG_LOGGER(linktable->logger, "%d OK", __LINE__);
                                shutdown(fd, SHUT_RDWR);
                                close(fd);
                                break;
                            }
                        }
                        else 
                        {
                            DEBUG_LOGGER(linktable->logger, "%d OK", __LINE__);
                            shutdown(fd, SHUT_RDWR);
                            close(fd);
                            break;
                        }
                    }
                    usleep(10);
                }
                DEBUG_LOGGER(linktable->logger, "%d OK", __LINE__);
                buffer->push(buffer, "\0", 1);
                if(response.respid != -1)
                {
                    PARSE_RESPONSE(p, end, buffer, response);
                }
                if(response.respid == RESP_OK)
                {
                    p = (char *)(buffer->data + response.header_size);
                    end = (char *)buffer->end;
                    DEBUG_LOGGER(linktable->logger, "Ready for add[%08x] document[http://%s%s] %08x:%d",
                                linktable->add_content, request.host, request.path, p, (end - p));
                    if(linktable->add_content(linktable, &response, 
                                request.host, request.path, p, (end - p)) != 0)
                    {
                        ERROR_LOGGER(linktable->logger, "Adding http://%s%s content failed, %s",
                                request.host, request.path, strerror(errno));
                    }
                    DEBUG_LOGGER(linktable->logger, "OK");
                    linktable->update_request(linktable, request.id, LINK_STATUS_OVER);
                    DEBUG_LOGGER(linktable->logger, "OK response ");
                }
                else
                {
                    DEBUG_LOGGER(linktable->logger, "ERROR response ");
                    linktable->update_request(linktable, sid, LINK_STATUS_ERROR);
                }
                shutdown(fd, SHUT_RDWR);
                close(fd);
            } 
            else
            {
                fprintf(stderr, "connected failed,%s", strerror(errno));
                linktable->update_request(linktable, sid, LINK_STATUS_DISCARD);
            }
        }
        usleep(100);
    }
    linktable->clean(&linktable);
    buffer->clean(&buffer);
    return NULL;
}


int main(int argc, char **argv)
{
    int i = 0, n = 0;
    int taskid = -1;
    int threads_count = 1;
    char *hostname = NULL, *path = NULL;
    pthread_t threadid = 0;
    LINKTABLE *linktable = NULL;

    if(argc < 3)
    {
        fprintf(stderr, "Usage:%s hostname path pthreads_count\n", argv[0]);
        _exit(-1);
    }

    hostname = argv[1];
    path = argv[2];
    if(argc > 3 && argv[3]) threads_count = atoi(argv[3]); 

    if(linktable = linktable_init())
    {
        linktable->set_logger(linktable, "/tmp/link.log", NULL);
        linktable->set_md5file(linktable, "/tmp/link.md5");
        linktable->set_urlfile(linktable, "/tmp/link.url");
        linktable->set_metafile(linktable, "/tmp/link.meta");
        linktable->set_docfile(linktable, "/tmp/link.doc");
        linktable->set_ntask(linktable, threads_count);
        linktable->iszlib = 1;
        linktable->resume(linktable);
        linktable->addurl(linktable, hostname, path);
        //pthreads 
        for(i = 0; i < threads_count; i++)
        {
            if(pthread_create(&threadid, NULL, &pthread_handler, (void *)linktable) != 0)
            {
                fprintf(stderr, "Create NEW threads[%d][%08x] failed, %s\n", 
                        i, threadid, strerror(errno));
                _exit(-1);
            }
        }
        // DEBUG_LOGGER(ltable->logger, "thread[%08x] start .....", pthread_self());
        while(1)
        {
            if((taskid = linktable->get_urltask(linktable)) != -1)
            {
                DEBUG_LOGGER(linktable->logger, "start task:%d", taskid);
                linktable->urlhandler(linktable, taskid);
                DEBUG_LOGGER(linktable->logger, "Completed task:%d", taskid);
                fprintf(stdout, "urlno:%d urlok:%d urltotal:%d docno:%d docok:%d doctotal:%d\n",
                        linktable->urlno, linktable->urlok_total, linktable->url_total,
                        linktable->docno, linktable->docok_total, linktable->doc_total);
            }
            usleep(100);
        }
    }
}
#endif
