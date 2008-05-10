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
#include "trie.h"
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
#define ADD_TO_MD5TABLE(ptr, k, nk, pp)                         \
{                                                               \
    if(ptr)                                                     \
    {                                                           \
        MUTEX_LOCK(ptr->mutex);                                 \
        TRIETAB_ADD((ptr->md5table), k, nk, pp);                \
        MUTEX_UNLOCK(ptr->mutex);                               \
    }                                                           \
}
#define GET_FROM_MD5TABLE(ptr, k, nk, pp)                       \
{                                                               \
    if(ptr)                                                     \
    {                                                           \
        MUTEX_LOCK(ptr->mutex);                                 \
        TRIETAB_GET((ptr->md5table), k, nk, pp);                \
        MUTEX_UNLOCK(ptr->mutex);                               \
    }                                                           \
}
/* set lnkfile */
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


/* set lnkfile */
int linktable_set_lnkfile(LINKTABLE *linktable, char *lnkfile)
{
    if(linktable)
    {
        DEBUG_LOGGER(linktable->logger, "Setting lnkfile %s", lnkfile);
        HIO_SET(linktable->lnkio, lnkfile);
        HIO_CHK(linktable->lnkio);
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
        linktable->tasks = (DOCMETA *)realloc(linktable->tasks,
                                sizeof(DOCMETA *) * ntask);
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
                        total += PT_LU_USEC(times);
                        count++;
                        //fprintf(stdout, "%s\n", link);
                    }
                }
                //}
            }
            ++p;
        }	
        TIMER_SAMPLE(timer);
        DEBUG_LOGGER(linktable->logger, "Parsed http://%s%s count:%d times:%lld time:%lld", host, path, count, total, PT_LU_USEC(timer));
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

    if(linktable && host && path && href && (ehref - href) > 0 && (ehref - href) < HTTP_PATH_MAX)
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
            if(p < (ehref - 3) && memcmp(p, "://", 3) == 0) return -1;
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
                if(*((unsigned char *)p) > 127 || *p == 0x20)
                    ps += sprintf((char *)ps, "%%%02X", *p++);
                else *ps++ = *p++;
            }
            *ps = '\0';
            ps = lpath;
            //auto complete home page / 
            if(*ps == '\0') *ps = '/';
        }
        if(lhost[0] == '\0' || lpath[0] == '\0') return -1;
        DEBUG_LOGGER(linktable->logger, "addurl:http://%s%s ", lhost, lpath);
        TIMER_INIT(timer);
        linktable->addurl(linktable, (char *)lhost, (char *)lpath);
        TIMER_SAMPLE(timer);
        DEBUG_LOGGER(linktable->logger, "addurl:http://%s%s time used:%lld ", 
                lhost, lpath, PT_LU_USEC(timer));
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
    char *p = NULL, *ip = NULL, *ps = NULL;
    void *ptr = NULL;
    int i = 0;
    //uLong n = 0, nzurl = 0;
    long long offset = 0;
    long n = 0, id = 0;
    int ret = -1;
    void *timer = NULL;

    if(linktable)
    {
        //DEBUG_LOGGER(linktable->logger, "New URL:http://%s%s", host, path);
        //check dns
        if((n = sprintf(url, "http://%s%s", host, path)) > 0)
        {
            memset(&req, 0, sizeof(HTTP_REQUEST));
            md5((unsigned char *)url, n, req.md5);
            GET_FROM_MD5TABLE(linktable, req.md5, MD5_LEN, ptr);
            if(ptr) 
            {
                id = (long )ptr;
                DEBUG_LOGGER(linktable->logger, "URL:[http://%s%s][%d] is exists", host, path, id);
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
            if((ip = linktable->iptab(linktable, req.host, NULL)) == NULL) 
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
            if(HIO_APPEND(linktable->lnkio, &req, sizeof(HTTP_REQUEST), offset) <= 0 ) 
            {
                goto err_end;
            }
            ptr = (void *)id;
            ADD_TO_MD5TABLE(linktable, req.md5, MD5_LEN, ptr);
            if(ptr)
            {
            linktable->url_total++;
            DEBUG_LOGGER(linktable->logger, "New[%d] URL:http://%s%s TOTAL:%d STATUS:%d", 
                    id, host, path, linktable->url_total, req.status);
            //GET_FROM_MD5TABLE(linktable, req.md5, MD5_LEN, ptr);
            //DEBUG_LOGGER(linktable->logger, "Got id[%d] URL:http://%s%s", (long)ptr, host, path);
            ret = 0;
            }
        }
    }
err_end:
    return ret;
}

/* get link DNS */
char *linktable_iptab(LINKTABLE *linktable, char *hostname, char *ipstr)
{
    struct hostent *hp = NULL;
    char *ip = NULL;

    if(linktable)
    {
        TRIETAB_GET(linktable->dnstable, hostname, strlen(hostname), ip);
        if(ip) 
        {
            DEBUG_LOGGER(linktable->logger, "DNS [%s:%s] is exists", hostname, ip);
            return ip;
        }
        DEBUG_LOGGER(linktable->logger, "Ready for parsing [%s]'s ip", hostname);
        if(ipstr == NULL) 
        {
            if((hp = gethostbyname((const char *)hostname)) == NULL) return NULL;
            ip = (char *)calloc(1, HTTP_IP_MAX); 
            sprintf(ip, "%s", inet_ntoa(*((struct in_addr *)(hp->h_addr))));
        }
        else 
        {
            ip = (char *)calloc(1, HTTP_IP_MAX);
            strcpy(ip, ipstr);
        }
        if(ip)
        {
            MUTEX_LOCK(linktable->mutex);

            if((linktable->dnslist = (char **)realloc(linktable->dnslist, 
                            sizeof(char *) * (linktable->dnscount + 1))))
            {
                linktable->dnslist[linktable->dnscount] = ip;
                TRIETAB_ADD(linktable->dnstable, hostname, strlen(hostname), ip);
            }
            MUTEX_UNLOCK(linktable->mutex);
        }
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

    if(linktable && req && linktable->urlno < linktable->url_total)
    {
        offset = (linktable->urlno * sizeof(HTTP_REQUEST));
        if(HIO_RSEEK(linktable->lnkio, offset) >= 0)
        {
            while(HIO_READ(linktable->lnkio, req, sizeof(HTTP_REQUEST)) > 0)
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
        if(HIO_SWRITE(linktable->lnkio, &status, sizeof(int), offset) > 0)
        {
            DEBUG_LOGGER(linktable->logger, "Update request[%d] to status[%d]", id ,status);
            ret = 0;
        }
        if(status == LINK_STATUS_OVER) linktable->urlok_total++;
    }
    return ret;
}

//get url task
long linktable_get_task_one(LINKTABLE *linktable)
{
    long long offset = 0;
    long taskid = -1;
    int n = 0;

    if(linktable && linktable->task.id == -1 && linktable->docno < linktable->doc_total)
    {
        fprintf(stdout, "%d id:%d\n", linktable->docno, linktable->task.id);
        offset = linktable->docno * sizeof(DOCMETA);
        if(HIO_RSEEK(linktable->metaio, offset) >= 0)
        {
            while((n = HIO_READ(linktable->metaio, &(linktable->task), sizeof(DOCMETA))) > 0)
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
long linktable_get_task(LINKTABLE *linktable)
{
    DOCMETA *pdocmeta = NULL;
    int i = 0;
    long taskid = -1;
    long long offset = 0;

    if(linktable && linktable->tasks && linktable->docno < linktable->doc_total)
    {
        for(i = 0; i < linktable->ntask; i++)
        {
            pdocmeta = &(linktable->tasks[i]);
            offset = linktable->docno * sizeof(DOCMETA);
            if(linktable->docno < linktable->doc_total
                    && pdocmeta->status != URL_STATUS_WAIT 
                    && pdocmeta->status != URL_STATUS_WORKING
                    && HIO_RSEEK(linktable->metaio, offset) >= 0) 
            {
                while(HIO_READ(linktable->metaio, pdocmeta, sizeof(DOCMETA)) > 0)
                {
                    DEBUG_LOGGER(linktable->logger, "docno:%d id:%d status:%d size:%d "
                            "zsize:%d hostoff:%d pathoff:%d htmloff:%d offset:%lld",
                            linktable->docno, pdocmeta->id, pdocmeta->status,
                            pdocmeta->size, pdocmeta->zsize, pdocmeta->hostoff, 
                            pdocmeta->pathoff, pdocmeta->htmloff, pdocmeta->offset);
                    linktable->docno++;
                    if(pdocmeta->status == URL_STATUS_INIT)
                    {
                        pdocmeta->status = URL_STATUS_WAIT;
                        break;
                    }
                }
            }
            if(taskid == -1 && pdocmeta->status == URL_STATUS_WAIT)
            {
                taskid = i;
                pdocmeta->status = URL_STATUS_WORKING;
                break;
            }
        }
    }
    return taskid;
}

/* URL task HANDLER */
void linktable_taskhandler(LINKTABLE *linktable, long taskid)
{
    DOCMETA *pdocmeta = NULL;
    char *data = NULL, *zdata = NULL, *host = NULL, 
         *path = NULL, *p = NULL, *end = NULL;
    uLong ndata = 0, nzdata = 0; 
    int status = URL_STATUS_ERROR;
    int i = 0, fd = 0;
    long long offset = 0;
    void *timer = NULL;

    if(linktable && linktable->tasks)
    {
        pdocmeta = &(linktable->tasks[taskid]);
        if(pdocmeta->status != URL_STATUS_WORKING) return ;
        if(pdocmeta->zsize > 0 && pdocmeta->size > 0 && pdocmeta->offset >= 0
                && (data = (char *)calloc(1, pdocmeta->size))
                && (zdata = (char *)calloc(1, pdocmeta->zsize)))
        {
            nzdata = pdocmeta->zsize;
            //fprintf(stdout, "line:%d id:%d offset:%lld status:%d size:%d zsize:%d\n", __LINE__,
            //pdocmeta->id, pdocmeta->offset, pdocmeta->status, pdocmeta->size, pdocmeta->zsize);
            DEBUG_LOGGER(linktable->logger, "Ready for reading data from docfile");
            if(HIO_SREAD(linktable->docio, zdata, nzdata, pdocmeta->offset) <= 0)
            {
                FATAL_LOGGER(linktable->logger, "Read from %s offset:%lld failed, %s",
                        PTH(linktable->docio), pdocmeta->offset, strerror(errno));
                goto end;
            }
            DEBUG_LOGGER(linktable->logger, "Ready for decompressing data");
            ndata = pdocmeta->size;
            if(zdecompress(zdata, nzdata, data, &ndata) != 0)
            {
                FATAL_LOGGER(linktable->logger, "Decompress data zsize[%d] to size[%d] failed, %s",
                        pdocmeta->zsize, pdocmeta->size, strerror(errno));
                goto end;
            }
            host = data + pdocmeta->hostoff;
            path = data + pdocmeta->pathoff;
            p = data + pdocmeta->htmloff;
            end = data + pdocmeta->size;
            if(ndata != pdocmeta->size) 
                FATAL_LOGGER(linktable->logger, "invalid decompress ndata:%d size:%d", 
                        ndata, pdocmeta->size);
            linktable->parse(linktable, host, path, p, end);
            linktable->docok_total++;
            status = URL_STATUS_OVER;
        }
end:
        if(data) free(data);
        if(zdata) free(zdata);
        offset = pdocmeta->id * sizeof(DOCMETA); 
        if(HIO_SWRITE(linktable->metaio, &status, sizeof(int), offset) <= 0)
        {
            FATAL_LOGGER(linktable->logger, "Update meta[%d] status[%d] failed, %s",
                    pdocmeta->id, status, strerror(errno));
        }
        memset(pdocmeta, 0, sizeof(DOCMETA));
        pdocmeta->status = status;
    }
    return ;
}

/* add content as zdata */
int linktable_add_zcontent(LINKTABLE *linktable, DOCMETA *pdocmeta, char *zdata, int nzdata)
{
    DOCMETA docmeta;
    int ret = -1;
    long long offset = 0;

    if(linktable && pdocmeta && zdata && nzdata > 0)
    {
        DEBUG_LOGGER(linktable->logger, "URL[%d] hostoff:%d pathoff:%d size:%d zsize:%d",
                pdocmeta->id, pdocmeta->hostoff, pdocmeta->pathoff, pdocmeta->size, pdocmeta->zsize);
        if(HIO_APPEND(linktable->docio, zdata, nzdata, offset) <= 0)
        {
            FATAL_LOGGER(linktable->logger, "Write compressed document zsize[%d] failed, %s",
                    nzdata, strerror(errno));
            goto end;
        }
        memcpy(&docmeta, pdocmeta, sizeof(DOCMETA));
        docmeta.id = linktable->doc_total;
        docmeta.status = URL_STATUS_INIT;
        if(HIO_APPEND(linktable->metaio, &(docmeta), sizeof(DOCMETA), offset) <= 0)
        {
            FATAL_LOGGER(linktable->logger, "Write DOCMETA[%d]  failed, %s",
                    pdocmeta->id, strerror(errno));
            goto end;
        }
        linktable->doc_total++;
        DEBUG_LOGGER(linktable->logger, "Added DOCMETA[%d] hostoff:%d pathoff:%d"
                "size:%d zsize:%d to offset:%lld",
                docmeta.id, docmeta.hostoff, docmeta.pathoff, 
                docmeta.size, docmeta.zsize, offset);
    }
end:
    return ret;
}

/* Add content */
int linktable_add_content(LINKTABLE *linktable, void *response, 
        char *host, char *path, char *content, int ncontent)
{
    DOCMETA docmeta;
    HTTP_RESPONSE *resp = (HTTP_RESPONSE *)response;
    char buf[LBUF_SIZE], *p = NULL, *ps = NULL;
    char *data = NULL, *zdata = NULL;
    uLong ndata = 0, nzdata = 0;
    long long offset = 0;
    int i = 0, ret = -1, n = 0, nhost = 0, npath = 0;

    DEBUG_LOGGER(linktable->logger, "OK");
    if(linktable && resp && host && path && content && ncontent > 0)
    {
        memset(&docmeta, 0, sizeof(DOCMETA));
        p = buf;
        for(i = 0; i < HTTP_HEADER_NUM; i++)
        {
            if(resp->headers[i])
                p += sprintf(p, "[%d:%s:%s]", i, http_headers[i].e, resp->headers[i]);
        }
        docmeta.id  = linktable->doc_total;
        docmeta.status = URL_STATUS_INIT;
        docmeta.hostoff = (p - buf);
        nhost = strlen(host) + 1;    
        docmeta.pathoff = docmeta.hostoff + nhost;
        npath = strlen(path) + 1;    
        docmeta.htmloff = docmeta.pathoff + npath;
        nzdata = ndata = docmeta.size = (docmeta.htmloff + ncontent);
        if((data = (char *)calloc(1, ndata)) == NULL 
                || (zdata = (char *)calloc(1, ndata)) == NULL) goto end;
        DEBUG_LOGGER(linktable->logger, "Ready data combine");
        ps = data;
        memcpy(ps, buf, docmeta.hostoff);
        ps += docmeta.hostoff;
        memcpy(ps, host, nhost);
        ps += nhost;
        memcpy(ps, path, npath);
        ps += npath;
        memcpy(ps, content, ncontent);
        DEBUG_LOGGER(linktable->logger, "Ready for compressing data");
        if(zcompress(data, nzdata, zdata, &nzdata) != 0) goto end;
        docmeta.zsize = nzdata;
        DEBUG_LOGGER(linktable->logger, "Ready for append doc to docfile");
        if(HIO_APPEND(linktable->docio, zdata, nzdata, docmeta.offset) <= 0) goto end;
        DEBUG_LOGGER(linktable->logger, "Ready for append docmeta to metafile");
        if(HIO_APPEND(linktable->metaio, &(docmeta), sizeof(DOCMETA), offset) <= 0) goto end;
        linktable->doc_total++;
        ret = 0;
        DEBUG_LOGGER(linktable->logger, "Added DOCMETA[%d] hostoff:%d pathoff:%d"
                "size:%d zsize:%d to offset:%lld meta:%s",
                docmeta.id, docmeta.hostoff, docmeta.pathoff, 
                docmeta.size, docmeta.zsize, offset, data);
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
    DOCMETA docmeta;
    int i = 0, id = -1;
    long n = 0;
    char *p = NULL, md5str[MD5_LEN * 2 +1];
    char *ip = NULL;
    void *ptr = NULL;

    if(linktable)
    {
        //request
        if(HIO_RSEEK(linktable->lnkio, 0) < 0) return;
        while(HIO_READ(linktable->lnkio, &req, sizeof(HTTP_REQUEST)) > 0)
        {
            linktable->iptab(linktable->dnstable, req.host, req.ip);
            ADD_TO_MD5TABLE(linktable, req.md5, MD5_LEN, ptr);
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
        while(HIO_READ(linktable->metaio, &docmeta, sizeof(DOCMETA)) > 0)
        {
            if(docmeta.status == URL_STATUS_INIT && id == -1)
            {
                id = linktable->docno = n;
            }
            if(docmeta.status == URL_STATUS_OVER) linktable->docok_total++;
            linktable->doc_total++;
        }
    }
}

/* Clean linktable */
void linktable_clean(LINKTABLE **linktable)
{
    if(*linktable)
    {
        HIO_CLEAN((*linktable)->lnkio);
        HIO_CLEAN((*linktable)->urlio);
        HIO_CLEAN((*linktable)->metaio);
        HIO_CLEAN((*linktable)->docio);
        TRIETAB_CLEAN((*linktable)->md5table);
        TRIETAB_CLEAN((*linktable)->dnstable);
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
        HIO_INIT(linktable->lnkio);
        HIO_INIT(linktable->urlio);
        HIO_INIT(linktable->metaio);
        HIO_INIT(linktable->docio);
        linktable->task.id = -1;
        linktable->md5table         = TRIETAB_INIT();
        linktable->dnstable         = TRIETAB_INIT();
        linktable->set_logger       = linktable_set_logger;
        linktable->set_lnkfile      = linktable_set_lnkfile;
        linktable->set_urlfile      = linktable_set_urlfile;
        linktable->set_metafile     = linktable_set_metafile;
        linktable->set_docfile      = linktable_set_docfile;
        linktable->set_ntask        = linktable_set_ntask;
        linktable->parse            = linktable_parse; 
        linktable->add              = linktable_add; 
        linktable->addurl           = linktable_addurl; 
        linktable->iptab            = linktable_iptab; 
        linktable->get_request      = linktable_get_request; 
        linktable->update_request   = linktable_update_request; 
        linktable->add_zcontent     = linktable_add_zcontent; 
        linktable->add_content      = linktable_add_content; 
        linktable->get_task      = linktable_get_task; 
        linktable->get_task_one  = linktable_get_task_one; 
        linktable->taskhandler       = linktable_taskhandler; 
        linktable->resume           = linktable_resume; 
        linktable->clean            = linktable_clean; 
        linktable->set_ntask(linktable, URL_NTASK_DEFAULT);
    }
    return linktable;
}
#ifdef _DEBUG_LINKTABLE
//gen.sh 
//gcc -o tlink -D_DEBUG_LINKTABLE -D_FILE_OFFSET_BITS=64 link.c http.c utils/*.c -I utils/ -DHAVE_PTHREAD -lpthread -lz -D_DEBUG && ./tlink www.sina.com.cn / 2 &
#include <pthread.h>
#include <evbase.h>
#include "http.h"
#include "timer.h"
#include "buffer.h"
#include "basedef.h"
#define DCON_TIMEOUT  30000000
#define DCON_BUF_SIZE 65536
#define DCON_STATUS_WAIT        0
#define DCON_STATUS_WORKING     1
#define DCON_STATUS_DISCARD     2
#define DCON_STATUS_OVER        4
typedef struct _DCON
{
    char http_header[DCON_BUF_SIZE];
    char *tp;
    char *p ;
    char *ps;
    char *content;
    char *buffer;
    void *timer;
    int data_size;
    int left;
    int n ;
    struct sockaddr_in sa;
    socklen_t sa_len;
    int fd ;
    int status;
    EVENT *event;
    HTTP_REQUEST req;
    HTTP_RESPONSE resp;
}DCON;
static LINKTABLE *linktable     = NULL;
static EVBASE *evbase           = NULL;
static DCON *conns              = NULL;
static int nconns               = 32;
static int running_conns        = 0;
static int conn_buf_size        = 2097152;
void ev_handler(int ev_fd, short flag, void *arg);
#define DCON_CLOSE(conn)                                                                    \
{                                                                                           \
   conn->event->destroy(conn->event);                                                       \
   shutdown(conn->fd, SHUT_RD|SHUT_WR);                                                     \
   close(conn->fd);                                                                         \
   conn->status = DCON_STATUS_WAIT;                                                         \
   running_conns--;                                                                         \
}
#define DCON_PACKET(conn)                                                                   \
{                                                                                           \
    conn->ps = conn->buffer;                                                                \
    if(conn->content == NULL && conn->status == DCON_STATUS_WORKING)                        \
    {                                                                                       \
        while(conn->ps < conn->p)                                                           \
        {                                                                                   \
            if(conn->ps < (conn->p - 4) && *(conn->ps) == '\r' && *(conn->ps+1) == '\n'     \
                    && *(conn->ps+2) == '\r' && *(conn->ps+3) == '\n')                      \
            {                                                                               \
                *(conn->ps) = '\0';                                                         \
                (conn->content) = (conn->ps + 4);                                           \
                http_response_parse(conn->buffer, conn->ps, &(conn->resp));                 \
                if((conn->tp = conn->resp.headers[HEAD_ENT_CONTENT_TYPE]) == NULL           \
                    || strncasecmp(conn->tp, "text", 4) != 0)                               \
                {                                                                           \
                    conn->resp.respid = RESP_NOCONTENT;                                     \
                    conn->req.status = LINK_STATUS_DISCARD;                                 \
                    conn->status = DCON_STATUS_DISCARD;                                     \
                    linktable->update_request(linktable, conn->req.id, conn->req.status);   \
                    ERROR_LOGGER(linktable->logger, "Invalid type[%s] to http://%s%s",      \
                            conn->resp.headers[HEAD_ENT_CONTENT_TYPE],                      \
                            conn->req.host, conn->req.path);                                \
                    DCON_CLOSE(conn);                                                       \
                }                                                                           \
                if((conn->tp = conn->resp.headers[HEAD_ENT_CONTENT_LENGTH]))                \
                {                                                                           \
                    conn->data_size = atoi(conn->tp);                                       \
                    DEBUG_LOGGER(linktable->logger, "Ready for Reading %d bytes "           \
                        "from [%s:%d] via %d [http://%s%s]", conn->data_size, conn->req.ip, \
                            conn->req.port, conn->fd, conn->req.host, conn->req.path);      \
                }                                                                           \
                break;                                                                      \
            }                                                                               \
            else conn->ps++;                                                                \
        }                                                                                   \
    }                                                                                       \
}
#define DCON_DATA(conn)                                                                     \
{                                                                                           \
    DCON_PACKET(conn);                                                                      \
    if(conn->content && conn->resp.respid == RESP_OK)                                       \
    {                                                                                       \
        *(conn->p) = '\0';                                                                  \
        if(linktable->add_content(linktable, &(conn->resp), conn->req.host,                 \
                    conn->req.path, conn->content, (conn->p - conn->content)) != 0)         \
        {                                                                                   \
            ERROR_LOGGER(linktable->logger, "Adding http://%s%s content failed, %s",        \
                    conn->req.host, conn->req.path, strerror(errno));                       \
            conn->req.status = LINK_STATUS_ERROR;                                           \
        }                                                                                   \
        else                                                                                \
        {                                                                                   \
            conn->req.status = LINK_STATUS_OVER;                                            \
        }                                                                                   \
    }                                                                                       \
    linktable->update_request(linktable, conn->req.id, conn->req.status);                   \
}
#define DCON_OVER(conn)                                                                     \
{                                                                                           \
    DCON_DATA(conn);                                                                        \
    DCON_CLOSE(conn);                                                                       \
}
#define DCON_READ(conn)                                                                     \
{                                                                                           \
    if((conn->n = read(conn->fd, conn->p, conn->left)) > 0)                                 \
    {                                                                                       \
        conn->left -= conn->n;                                                              \
        conn->p += conn->n;                                                                 \
        DCON_PACKET(conn);                                                                  \
        if(conn->content && (conn->p - conn->content) >= conn->data_size)                   \
        {                                                                                   \
            DCON_OVER(conn);                                                                \
        }                                                                                   \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        DEBUG_LOGGER(linktable->logger, "Reading from connection[%s:%d] via %d faild, %s",  \
                conn->req.ip, conn->req.port, conn->fd, strerror(errno));                   \
        DCON_OVER(conn);                                                                    \
    }                                                                                       \
}                                                                                           
#define DCON_REQ(conn)                                                                      \
{                                                                                           \
    conn->n = sprintf(conn->http_header, "GET %s HTTP/1.0\r\n"                              \
            "Host: %s\r\nConnection: close\r\n"                                             \
            "User-Agent: Mozilla\r\n\r\n", conn->req.path, conn->req.host);                 \
    if((conn->n = write(conn->fd, conn->http_header, conn->n)) > 0)                         \
    {                                                                                       \
        conn->event->del(conn->event, E_WRITE);                                             \
        DEBUG_LOGGER(linktable->logger, "Wrote %d bytes request to http://%s%s] via %d ",   \
                conn->n, conn->req.host, conn->req.path, conn->fd);                         \
    }                                                                                       \
    else                                                                                    \
    {                                                                                       \
        linktable->update_request(linktable, conn->req.id, LINK_STATUS_ERROR);              \
        DCON_CLOSE(conn);                                                                   \
    }                                                                                       \
}
//if(fcntl(conn->fd, F_SETFL, O_NONBLOCK) == 0)                                       
#define NEW_DCON(conn, request)                                                             \
{                                                                                           \
    if((conn->fd = socket(AF_INET, SOCK_STREAM, 0)) > 0)                                    \
    {                                                                                       \
        memset(&(conn->sa), 0, sizeof(struct sockaddr_in));                                 \
        conn->sa.sin_family = AF_INET;                                                      \
        conn->sa.sin_addr.s_addr = inet_addr(request.ip);                                   \
        conn->sa.sin_port = htons(request.port);                                            \
        conn->sa_len = sizeof(struct sockaddr);                                             \
        DEBUG_LOGGER(linktable->logger, "Ready for connecting to [%s][%s:%d] via %d",       \
                request.host, request.ip, request.port, conn->fd);                          \
        if(fcntl(conn->fd, F_SETFL, O_NONBLOCK) == 0)                                       \
        {                                                                                   \
            connect(conn->fd, (struct sockaddr *)&(conn->sa), conn->sa_len);                \
            conn->p  = conn->buffer;                                                        \
            memset(conn->buffer, 0, conn_buf_size);                                         \
            conn->left = conn_buf_size;                                                     \
            memset(&(conn->resp), 0, sizeof(HTTP_RESPONSE));                                \
            conn->content = NULL;                                                           \
            conn->resp.respid = -1;                                                         \
            conn->data_size = 0;                                                            \
            TIMER_RESET(conn->timer);                                                       \
            memcpy(&(conn->req), &(request), sizeof(HTTP_REQUEST));                         \
            conn->event->set(conn->event, conn->fd, E_READ|E_WRITE|E_PERSIST,               \
                (void *)conn, (void *)&ev_handler);                                         \
            evbase->add(evbase, conn->event);                                               \
            DEBUG_LOGGER(linktable->logger, "Added connection[%s][%s:%d] via %d",           \
                    conn->req.host, conn->req.ip, conn->req.port, conn->fd);                \
        }                                                                                   \
        else                                                                                \
        {                                                                                   \
            ERROR_LOGGER(linktable->logger, "Connecting to %s:%d host[%s] failed, %s",      \
                    conn->req.ip, conn->req.port, conn->req.host, strerror(errno));         \
            close(conn->fd);                                                                \
            linktable->update_request(linktable, request.id, LINK_STATUS_DISCARD);          \
            conn->status = DCON_STATUS_WAIT;                                                \
            running_conns--;                                                                \
        }                                                                                   \
    }                                                                                       \
}
#define DCON_POP(conn, n)                                                                   \
{                                                                                           \
    n = 0;                                                                                  \
    while(n < nconns)                                                                       \
    {                                                                                       \
        if(conns[n].status == DCON_STATUS_WORKING                                           \
                && TIMER_CHECK(conns[n].timer, DCON_TIMEOUT) == 0)                          \
        {                                                                                   \
            conn = &(conns[n]);                                                             \
            ERROR_LOGGER(linktable->logger, "Connection[%s:%d] via %d to [%s:%s] TIMEOUT",  \
                conn->req.ip, conn->req.port, conn->fd, conn->req.host, conn->req.path);    \
            DCON_OVER(conn);                                                                \
            conn = NULL;                                                                    \
        }                                                                                   \
        if(conns[n].status == DCON_STATUS_WAIT)                                             \
        {                                                                                   \
            conns[n].status = DCON_STATUS_WORKING;                                          \
            conn = &(conns[n]);                                                             \
            running_conns++;                                                                \
            break;                                                                          \
        }else ++n;                                                                          \
    }                                                                                       \
}
#define DCON_FREE(conn)                                                                     \
{                                                                                           \
    conn->status = DCON_STATUS_WAIT;                                                        \
    running_conns--;                                                                        \
}
#define DCONS_INIT(n)                                                                       \
{                                                                                           \
    if((conns = (DCON *)calloc(nconns, sizeof(DCON))))                                      \
    {                                                                                       \
        n = 0;                                                                              \
        while(n < nconns )                                                                  \
        {                                                                                   \
            conns[n].buffer = (char *)calloc(1, conn_buf_size);                             \
            conns[n].event = ev_init();                                                     \
            TIMER_INIT(conns[n].timer);                                                     \
            n++;                                                                            \
        }                                                                                   \
    }                                                                                       \
}

/* event handler */
void ev_handler(int ev_fd, short flag, void *arg)
{
    DCON *conn = (DCON *)arg;
    if(conn && ev_fd == conn->fd)
    {
        if(flag & E_WRITE)
        {
            DCON_REQ(conn);
        }
        if(flag & E_READ)
        {
            DEBUG_LOGGER(linktable->logger, "Ready for reading from [%s][%s:%d] via %d",
                    conn->req.host, conn->req.ip, conn->req.port, conn->fd);
            DCON_READ(conn);
            DEBUG_LOGGER(linktable->logger, "Read over from [%s:%d] via %d [http://%s%s] "
                    "data_size:%d content_size:%d left:%d ", conn->req.ip,
                    conn->req.port, conn->fd, conn->req.host, conn->req.path,
                    conn->data_size, (conn->p - conn->content), conn->left);
        }
        TIMER_SAMPLE(conn->timer);
    }
    return ;
}

void *pthread_handler(void *arg)
{
    LINKTABLE *linktable = (LINKTABLE *)arg;
    HTTP_REQUEST request;
    DCON *conn = NULL;
    int i = 0;

    if((evbase = evbase_init()))
    {
        DCONS_INIT(i);
        while(1)
        {
            if(running_conns < nconns)
            {
                DCON_POP(conn, i);
                if(conn)
                {
                    if(linktable->get_request(linktable, &request) != -1)
                    {
                        DEBUG_LOGGER(linktable->logger, "New request[http://%s%s] [%s:%d] "
                                "via conns[%d][%08x]", request.host, request.path,
                                request.ip, request.port, i, conn);
                        NEW_DCON(conn, request);
                    }
                    else
                    {
                        DCON_FREE(conn);
                    }
                }
            }
            evbase->loop(evbase, 0, NULL);
            usleep(100);
        }
    }
    return NULL;
}


int main(int argc, char **argv)
{
    int i = 0, n = 0;
    int taskid = -1;
    char *hostname = NULL, *path = NULL;
    pthread_t threadid = 0;

    if(argc < 3)
    {
        fprintf(stderr, "Usage:%s hostname path connections\n", argv[0]);
        _exit(-1);
    }

    hostname = argv[1];
    path = argv[2];
    if(argc > 3 && argv[3]) nconns = atoi(argv[3]); 

    if(linktable = linktable_init())
    {
        linktable->set_logger(linktable, "/tmp/link.log", NULL);
        linktable->set_lnkfile(linktable, "/tmp/link.lnk");
        linktable->set_urlfile(linktable, "/tmp/link.url");
        linktable->set_metafile(linktable, "/tmp/link.meta");
        linktable->set_docfile(linktable, "/tmp/link.doc");
        linktable->set_ntask(linktable, 32);
        linktable->iszlib = 1;
        linktable->resume(linktable);
        linktable->addurl(linktable, hostname, path);
        if(pthread_create(&threadid, NULL, &pthread_handler, (void *)linktable) != 0)
        {
            fprintf(stderr, "creating thread failed, %s\n", strerror(errno));
            _exit(-1);
        }
        //pthreads 
        /*
        for(i = 0; i < threads_count; i++)
        {
            if(pthread_create(&threadid, NULL, &pthread_handler, (void *)linktable) != 0)
            {
                fprintf(stderr, "Create NEW threads[%d][%08x] failed, %s\n", 
                        i, threadid, strerror(errno));
                _exit(-1);
            }
        }*/
        // DEBUG_LOGGER(ltable->logger, "thread[%08x] start .....", pthread_self());
        while(1)
        {
            if((taskid = linktable->get_task(linktable)) != -1)
            {
                DEBUG_LOGGER(linktable->logger, "start task:%d", taskid);
                linktable->taskhandler(linktable, taskid);
                DEBUG_LOGGER(linktable->logger, "Completed task:%d", taskid);
                DEBUG_LOGGER(linktable->logger, 
                        "urlno:%d urlok:%d urltotal:%d docno:%d docok:%d doctotal:%d\n",
                        linktable->urlno, linktable->urlok_total, linktable->url_total,
                        linktable->docno, linktable->docok_total, linktable->doc_total);
            }
            else 
            {
                ERROR_LOGGER(linktable->logger, 
                        "urlno:%d urlok:%d urltotal:%d docno:%d docok:%d doctotal:%d\n",
                        linktable->urlno, linktable->urlok_total, linktable->url_total,
                        linktable->docno, linktable->docok_total, linktable->doc_total);

            }
            usleep(100);
        }
    }
}
#endif
