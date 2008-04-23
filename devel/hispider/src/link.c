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
#include "http.h"
#include "nio.h"

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

/* set md5file */
int linktable_set_logger(LINKTABLE *linktable, char *logfile, void *logger)
{
    if(linktable)
    {
        linktable->logger = logger;
        linktable->isinsidelogger = 0;
        if(logfile) 
        {
            linktable->logger = logger_init(logfile);
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
        NIO_SET(linktable->md5io, md5file);
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
        NIO_SET(linktable->urlio, urlfile);
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
        NIO_SET(linktable->metaio, metafile);
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
        NIO_SET(linktable->docio, docfile);
        return 0;
    }
    return -1;
}

/* set nrequest */
int linktable_set_nrequest(LINKTABLE *linktable, int nrequest)
{
    if(linktable && nrequest > 0)
    {
        linktable->requests = (HTTP_REQUEST *)realloc(linktable->requests, 
                sizeof(HTTP_REQUEST) * nrequest);
        linktable->nrequest = nrequest;
        DEBUG_LOGGER(linktable->logger, "Setting nrequest %d", nrequest);
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
                                sizeof(URLMETA) * ntask);
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

    if(linktable && host && path && content && end)	
    {
        DEBUG_LOGGER(linktable->logger, "Ready for parse page[%s%s] length %d", host, path, (end - content));
        p = content;
        while(p < end)
        {
            if(*p == '<' && (*(p+1) == 'a' || *(p+1) == 'A'))
            {
                p += 2;
                pref = 0;
                while(p < end && (*p == 0x20 || *p == 0x09)) ++p;
                fprintf(stdout, "%d %c\n", __LINE__, *p);
                if(strncasecmp(p, "href", 4) != 0) continue;
                //fprintf(stdout, "%s\n", p);
                p += 4;
                fprintf(stdout, "%d %c\n", __LINE__, *p);
                while(p < end && (*p == 0x20 || *p == 0x09)) ++p;
                fprintf(stdout, "%d %c\n", __LINE__, *p);
                if(*p != '=') continue;
                fprintf(stdout, "%d %c\n", __LINE__, *p);
                ++p;
                fprintf(stdout, "%d %c\n", __LINE__, *p);
                while(p < end && (*p == 0x20 || *p == 0x09)) ++p;
                if(*p == '"' || *p == '\''){++p; pref = 1;}
                fprintf(stdout, "%d %c\n", __LINE__, *p);
                link = p;
                if(pref){while(p < end && *p != '\'' && *p != '"')++p;}
                else {while(p < end && *p != 0x20 && *p != 0x09 && *p != '>')++p;}
                *p = '\0';
                //fprintf(stdout, "%s\n", p);
                if((n = (p - link)) > 0)
                {
                    if(*link != '#' && strncasecmp("javascript", link, 10) != 0)
                    {
                        DEBUG_LOGGER(linktable->logger, "Ready for adding URL from page[%s%s] %d", host, path, n);
                        linktable->add(linktable, (unsigned char *)host, 
                                (unsigned char *)path,  (unsigned char *)link,
                                (unsigned char *)p);
                        //fprintf(stdout, "%s\n", link);
                    }
                }
            }
            ++p;
        }	
        DEBUG_LOGGER(linktable->logger, "Parsed http://%s%s", host, path);
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

    if(linktable && host && path && href && (ehref - href) < HTTP_PATH_MAX)
    {
        p = href;
        if(*p == '/')
        {
            strcpy((char *)lhost, (char *)host);
            ps = lpath;
            p = href;
        }
        else if(strncasecmp((char *)p, "http://", 7) == 0)
        {
            ps = lhost;
            p = href + 7;
            n = HTTP_HOST_MAX;
            while(p < ehref && *p != '/') {*ps++ = *p++; if(--n <= 0) return 0;} 
            *ps = '\0';
            ps = lpath;
        }
        else
        {
            //delete file:// mail:// ftp:// news:// rss:// eg. 
            p = href;
            while(p < ehref && (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) p++;
            if(memcmp(p, "://", 4) == 0) return -1;
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
                while(p < ehref && (*p == '/' && *(p+1) == '/')) ++p;
                if(*(unsigned char *)p > 127 || *p == 0x20)
                    {ps += sprintf((char *)ps, "%%%02X", *p++);}
                else *ps++ = *p++;
            }
            *ps = '\0';
            ps = lpath;
            if(*ps == '\0') *ps = '/';
        }
        if(lhost[0] == '\0' || lpath[0] == '\0') return -1;
        linktable->addurl(linktable, (char *)lhost, (char *)lpath);
    }
    return 0;
}

/* add url to linktable */
int linktable_addurl(LINKTABLE *linktable, char *host, char *path)
{
    LINK link;
    char url[HTTP_URL_MAX];
    //char zurl[HTTP_URL_MAX];
    char md5str[MD5_LEN * 2 +1];
    char *p = NULL;
    int i = 0;
    //uLong n = 0, nzurl = 0;
    long long offset = 0;
    long n = 0, id = 0;
    int ret = -1;

    if(linktable)
    {
        MUTEX_LOCK(linktable->mutex);
        DEBUG_LOGGER(linktable->logger, "New URL:http://%s%s", host, path);
        memset(&link, 0, sizeof(LINK));
        if((n = sprintf(url, "http://%s%s", host, path)) > 0)
        {
            md5((unsigned char *)url, n, link.md5);
            p = md5str;
            for(i = 0; i < MD5_LEN; i++)
                p += sprintf(p, "%02x", link.md5[i]);
            *p = '\0';
            if((id = (long )TABLE_GET(linktable->md5table, md5str))) 
            {
                DEBUG_LOGGER(linktable->logger, "URL[%d] is exists", id);
                ret = 0; 
                goto err_end;
            }
            if(NIO_APPEND(linktable->urlio, url, n+1) <= 0) goto err_end;
            link.offset = offset; 
            link.nurl = n + 1;
            link.nhost = strlen(host);
            link.npath = strlen(path);
            if(((offset = NIO_SEEK_END(linktable->md5io)) < 0)
                || (NIO_WRITE(linktable->md5io, &link, sizeof(LINK)) <= 0))
            {
                ERROR_LOGGER(linktable->logger, "ERR:MD5:%s URL:%s\n", md5str, url);
                goto err_end;
            }
            id = (offset / sizeof(LINK)) + 1;
            TABLE_ADD(linktable->md5table, md5str, (long *)id);
            linktable->url_total++;
            ret = 0;
            DEBUG_LOGGER(linktable->logger, "Added URL[%d] md5[%s] %s", id, md5str, url);
        }
err_end:
        MUTEX_UNLOCK(linktable->mutex);
    }
    return ret;
}

/* get link DNS */
char *linktable_getip(LINKTABLE *linktable, char *hostname)
{
    struct hostent *hp = NULL;
    char *ip = NULL;
    if(linktable)
    {
        DEBUG_LOGGER(linktable->logger, "Ready for [%s]'s ip", hostname);
        if((ip = (char *)TABLE_GET(linktable->dnstable, hostname))) goto end;
        if((hp = gethostbyname((const char *)hostname)) == NULL) goto end;
        if((linktable->dnslist = (char **)realloc(linktable->dnslist, 
                        sizeof(char *) * (linktable->dnscount + 1)))
                && (ip = linktable->dnslist[linktable->dnscount++] 
                    = (char *)calloc(1, HTTP_IP_MAX)))
        {
            sprintf(ip, "%s", inet_ntoa(*((struct in_addr *)(hp->h_addr))));
            TABLE_ADD(linktable->dnstable, hostname, ip);
        }
end:
        if(ip){DEBUG_LOGGER(linktable->logger, "DNS name[%s] ip[%s]", hostname, ip);}
        else {WARN_LOGGER(linktable->logger, "DNS name[%s] failed", hostname);}
    }
    return ip;
}

/* get request */
int linktable_get_request(LINKTABLE *linktable, HTTP_REQUEST **req)
{
    char url[HTTP_URL_MAX], *p = NULL, *ps = NULL;
    LINK link;
    long long offset = 0;
    int i = 0, reqid = -1, n = 0;
    if(linktable)
    {
        MUTEX_LOCK(linktable->mutex);
        for(i = 0; i < linktable->nrequest; i++)
        {
            if(reqid == -1 && linktable->requests[i].status == LINK_STATUS_WAIT)
            {
                reqid = i;
                linktable->requests[i].status = LINK_STATUS_WORKING;
                (*req) = &(linktable->requests[i]);
                continue;
            }
            //DEBUG_LOGGER(linktable->logger, "queue[%d] status:%d urlno:%d", 
            //        i, linktable->requests[i].status, linktable->urlno);
            offset = linktable->urlno * sizeof(LINK);
            if(linktable->requests[i].status != LINK_STATUS_WORKING 
                    && linktable->requests[i].status != LINK_STATUS_WAIT 
                    && linktable->urlno < linktable->url_total
                    && (offset = NIO_SEEK(linktable->md5io, offset)) >= 0)
            {
                while(NIO_READ(linktable->md5io, &link, sizeof(LINK)) > 0)
                {
                    linktable->requests[i].id = linktable->urlno++;
                    memset(&url, 0, HTTP_URL_MAX);
                    if(link.nurl > 0 && link.status == LINK_STATUS_INIT
                            && NIO_SREAD(linktable->urlio, url, link.nurl, link.offset) > 0)
                    {
                        p = url;
                        p += strlen("http://");
                        //port
                        linktable->requests[i].port = 80;
                        //host 
                        ps =  linktable->requests[i].host;
                        while(*p != '\0' && *p != '/') 
                        {
                            if(*p == ':'){linktable->requests[i].port = atoi(++p);*ps++ = '\0';}
                            else *ps++ = *p++;
                        }
                        *ps = '\0';
                        //path
                        ps = linktable->requests[i].path;
                        while(*p != '\0') *ps++ = *p++; *ps = '\0';
                        //get ip
                        ps = linktable->requests[i].ip; 
                        if((p = linktable->getip(linktable, linktable->requests[i].host)) == NULL)
                        {
                            linktable->requests[i].status = LINK_STATUS_ERROR;
                            if(NIO_SWRITE(linktable->md5io, &(linktable->requests[i].status), 
                                    sizeof(int), offset) <= 0)
                            {
                                ERROR_LOGGER(linktable, "Write status for md5[%d] failed, %s",
                                        linktable->requests[i].host, strerror(errno));
                                goto err_end;
                            }
                            continue;
                        }
                        while(*p != '\0') *ps++ = *p++; *ps = '\0';
                        linktable->requests[i].status = LINK_STATUS_WAIT;
                        if(reqid == -1) 
                        {
                            reqid = i;
                            linktable->requests[i].status = LINK_STATUS_WORKING;
                            (*req) = &(linktable->requests[i]);
                        }
                        break;
                    }
                }
            }
        }
err_end:
        MUTEX_UNLOCK(linktable->mutex);
    }
    return reqid;
}

/* Update link status */
int linktable_update_request(LINKTABLE *linktable, int id, int status)
{
    HTTP_REQUEST *req = NULL;
    int ret = -1;
    long long offset = 0;

    if(linktable && id >= 0 && id < linktable->nrequest)
    {
        MUTEX_LOCK(linktable->mutex);
        req = &(linktable->requests[id]);
        req->status = status;
        if(status != LINK_STATUS_WAIT && status != LINK_STATUS_WORKING)
        {
            offset = req->id * sizeof(LINK);
            if(NIO_SWRITE(linktable->md5io, &status, sizeof(int), offset) <= 0) goto err_end;
        }
        if(status  == LINK_STATUS_OVER) linktable->urlok_total++;
        ret = 0;
         DEBUG_LOGGER(linktable->logger, "Update status[%d] on request[%d] to %s:%d",
                 status, id, req->ip, req->port);
err_end:
         MUTEX_UNLOCK(linktable->mutex);

    }
    return ret;
}

long linktable_get_urltask(LINKTABLE *linktable)
{
    URLMETA urlmeta;
    long taskid = -1;
    int i = 0;
    long long offset = 0;

    if(linktable)
    {
        MUTEX_LOCK(linktable->mutex);
        for(i = 0; i < linktable->ntask; i++)
        {
            if(taskid == -1 && linktable->tasks[i].status == URL_STATUS_WAIT)
            {
                taskid = i;
                linktable->tasks[i].status = URL_STATUS_WORKING;
                continue;
            }
            offset = linktable->docno * sizeof(URLMETA);
            if(linktable->tasks[i].status != URL_STATUS_WORKING 
                    && linktable->tasks[i].status != URL_STATUS_WAIT 
                    && linktable->docno < linktable->doc_total
                    && NIO_SEEK(linktable->metaio, offset) >= 0 )
            {
                while(NIO_READ(linktable->metaio, &(linktable->tasks[i]), sizeof(URLMETA)) > 0)
                {
                    linktable->tasks[i].id = linktable->docno++;
                    if(linktable->tasks[i].status == URL_STATUS_INIT)
                    {
                        linktable->tasks[i].status = URL_STATUS_WAIT;
                        if(taskid == -1) 
                        {
                            taskid = i;
                            linktable->tasks[i].status = URL_STATUS_WORKING;
                        }
                        break;
                    }
                }
            }
        }
err_end:
         MUTEX_UNLOCK(linktable->mutex);
    }
    return taskid;
}

/* URL task HANDLER */
void linktable_urlhandler(LINKTABLE *linktable, long taskid)
{
    URLMETA *urlmeta = NULL;
    char *data = NULL, *zdata = NULL, *host = NULL, 
         *path = NULL, *p = NULL, *end = NULL;
    uLong ndata = 0, n = 0; 
    int status = URL_STATUS_ERROR;
    int i = 0;
    long long offset;

    if(linktable && linktable->tasks)
    {
        urlmeta = &(linktable->tasks[taskid]); 
        ndata = urlmeta->size;
        if(urlmeta->zsize > 0 )
        {
            if((zdata = (char *)calloc(1, urlmeta->zsize)) == NULL
                    || NIO_SREAD(linktable->docio, zdata, urlmeta->zsize, urlmeta->offset) <= 0
                    || (data = (char *)calloc(1, urlmeta->size)) == NULL
                    || zdecompress(zdata, urlmeta->zsize, data, &ndata) != 0)
                goto end;
        }
        else
        {
            if((data = (char *)calloc(1, urlmeta->size)) == NULL
                    || NIO_SREAD(linktable->docio, data, urlmeta->size, urlmeta->offset) <= 0) 
                goto end;
        }
        if(data)
        {
            host = data + urlmeta->hostoff;
            path = data + urlmeta->pathoff;
            p = data + urlmeta->htmloff;
            end = data + urlmeta->size;
            linktable->parse(linktable, host, path, p, end);
            status = URL_STATUS_OVER;
        }
end:
        MUTEX_LOCK(linktable->mutex);
        urlmeta->status = status;
        offset = urlmeta->id * sizeof(URLMETA);
        if(NIO_SWRITE(linktable->metaio, &status, sizeof(int), offset) <= 0)
        {
            DEBUG_LOGGER(linktable->logger, "Write status for id[%d] failed, %s", 
                    urlmeta->id, strerror(errno));
        }
        if(data) free(data);
        if(zdata) free(zdata);
        MUTEX_UNLOCK(linktable->mutex);
    }
    return ;
}

/* Add content */
int linktable_add_content(LINKTABLE *linktable, void *response, 
        char *host, char *path, char *content, int ncontent)
{
    int i = 0, ret = -1;
    long long offset = 0;
    int nhost = 0, npath = 0;
    char buf[LBUF_SIZE], *p = NULL, *data = NULL, *zdata = NULL;
    uLong n = 0, nzdata = 0;
    URLMETA urlmeta;
    HTTP_RESPONSE *http_response = (HTTP_RESPONSE *)response;

    if(linktable && response && host && path && content && ncontent > 0)
    {
        memset(&urlmeta, 0, sizeof(urlmeta));
        p = buf;
        for(i = 0; i < HTTP_HEADER_NUM; i++)
        {
            if(http_response->headers[i])
            {
                p += sprintf(p, "[%d:%s:%s]", i, http_headers[i].e, http_response->headers[i]);
            }
        }
        nhost = strlen(host) + 1;
        npath = strlen(path) + 1;
        urlmeta.hostoff = (p - buf);
        urlmeta.pathoff =  urlmeta.hostoff + nhost;
        urlmeta.htmloff = urlmeta.pathoff + npath;
        urlmeta.size = urlmeta.htmloff + ncontent;
        //fprintf(stdout, "offhost:%d offpath:%d\n", urlmeta.hostoff, urlmeta.pathoff);
        if((p = data = (char *)calloc(1, urlmeta.size)))
        {
            memcpy(p, buf, urlmeta.hostoff);
            p += urlmeta.hostoff;
            memcpy(p, host, nhost);
            p += nhost;
            memcpy(p, path, npath);
            p += npath;
            memcpy(p, content, ncontent);
            nzdata = n  = urlmeta.size;
            p = data;
            if(linktable->iszlib && (zdata = calloc(1, urlmeta.size))
                    && zcompress(data, n, zdata, &nzdata) == 0)
            {
                p = zdata;
                n = nzdata;
                urlmeta.zsize = nzdata;
            }
            if( NIO_APPEND(linktable->docio, p, n) > 0
                    && NIO_APPEND(linktable->metaio, &urlmeta, sizeof(URLMETA)) > 0)
            {
                linktable->docok_total++;
                linktable->size += ncontent;
                linktable->zsize += n;
                ret = 0;
            }
            if(zdata) free(zdata);
            if(data) free(data);
        }
        linktable->doc_total++;
    }
    return ret;
}

int linktable_resume(LINKTABLE *linktable)
{
    LINK link;
    URLMETA urlmeta;
    int i = 0, id = -1;
    long n = 0;
    char *p = NULL, md5str[MD5_LEN * 2 +1];

    if(linktable)
    {
        //request
        if(NIO_SEEK_START(linktable->md5io) < 0) return;
        while(NIO_READ(linktable->md5io, &link, sizeof(LINK)) > 0)
        {
            p = md5str;
            for(i = 0; i < MD5_LEN; i++)
                p += sprintf(p, "%02x", link.md5[i]);
            TABLE_ADD(linktable->md5table, md5str, (long *)n);
            if(link.status == LINK_STATUS_INIT && id == -1)
            {
                id = linktable->urlno = n;
            }
            if(link.status == LINK_STATUS_OVER) linktable->urlok_total++;
            linktable->url_total++;
            n++;
        }
        //task
        id = -1;
        n = 0 ;
        if(NIO_SEEK_START(linktable->metaio) < 0) return;
        while(NIO_READ(linktable->metaio, &urlmeta, sizeof(URLMETA)) > 0)
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
        NIO_CLEAN((*linktable)->md5io);
        NIO_CLEAN((*linktable)->urlio);
        NIO_CLEAN((*linktable)->metaio);
        NIO_CLEAN((*linktable)->docio);
        TABLE_DESTROY((*linktable)->md5table);
        TABLE_DESTROY((*linktable)->dnstable);
        MUTEX_DESTROY((*linktable)->mutex);
        if((*linktable)->isinsidelogger) {CLOSE_LOGGER((*linktable)->logger);}
        if((*linktable)->requests) free((*linktable)->requests);
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
        linktable->md5io = NIO_INIT();
        linktable->urlio = NIO_INIT();
        linktable->metaio = NIO_INIT();
        linktable->docio = NIO_INIT();
        linktable->md5table         = TABLE_INIT(TABLE_SIZE);
        linktable->dnstable         = TABLE_INIT(TABLE_SIZE);
        linktable->set_logger       = linktable_set_logger;
        linktable->set_md5file      = linktable_set_md5file;
        linktable->set_urlfile      = linktable_set_urlfile;
        linktable->set_metafile     = linktable_set_metafile;
        linktable->set_docfile      = linktable_set_docfile;
        linktable->set_nrequest     = linktable_set_nrequest;
        linktable->set_ntask        = linktable_set_ntask;
        linktable->parse            = linktable_parse; 
        linktable->add              = linktable_add; 
        linktable->addurl           = linktable_addurl; 
        linktable->getip            = linktable_getip; 
        linktable->get_request      = linktable_get_request; 
        linktable->update_request   = linktable_update_request; 
        linktable->add_content      = linktable_add_content; 
        linktable->get_urltask      = linktable_get_urltask; 
        linktable->urlhandler       = linktable_urlhandler; 
        linktable->resume           = linktable_resume; 
        linktable->clean            = linktable_clean; 
        linktable->set_nrequest(linktable, LINK_NQUEUE_DEFAULT);
        linktable->set_ntask(linktable, URL_NTASK_DEFAULT);
    }
    return linktable;
}
#ifdef _DEBUG_LINKTABLE
//gen.sh 
//gcc -o tlink -D_DEBUG_LINKTABLE link.c http.c utils/buffer.c utils/hash.c utils/md5.c utils/zstream.c -I utils/ -lz && ./tlink www.sina.com.cn / &
#include <pthread.h>
#include "http.h"
#include "timer.h"
#include "buffer.h"
#include "basedef.h"
#define BUF_SIZE 8192
void *pth_handler(void *arg)
{
    LINKTABLE *ltable = (LINKTABLE *)arg;
    int taskid = -1;
    while(1)
    {
        if((taskid = ltable->get_urltask(ltable)) != -1)
        {
            fprintf(stdout, "start task:%d", taskid);
            ltable->urlhandler(ltable, taskid);
            fprintf(stdout, "Completed task:%d", taskid);
        }
        usleep(100);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int i = 0, n = 0;
    char *p = NULL, *end = NULL;
    BUFFER *buffer = NULL;
    LINKTABLE *linktable = NULL;
    HTTP_RESPONSE response;
    int fd = 0;
    struct sockaddr_in sa;
    socklen_t sa_len;
    fd_set readset;
    char buf[BUF_SIZE];
    char *path = NULL, *hostname = NULL;
    HTTP_REQUEST *request = NULL;
    int sid = 0;
    int flag = 0;
    long long count = 0;
    pthread_t threadid = 0;

    if(argc < 3)
    {
        fprintf(stderr, "Usage:%s hostname path\n", argv[0]);
        _exit(-1);
    }

    hostname = argv[1];
    path = argv[2];

    if(linktable = linktable_init())
    {
        linktable->set_logger(linktable, "/tmp/hispider.log", NULL);
        linktable->set_md5file(linktable, "/tmp/hispider.md5");
        linktable->set_urlfile(linktable, "/tmp/hispider.url");
        linktable->set_metafile(linktable, "/tmp/hispider.meta");
        linktable->set_docfile(linktable, "/tmp/hispider.doc");
        linktable->set_nrequest(linktable, 64);
        linktable->set_ntask(linktable, 64);
        linktable->resume(linktable);
        linktable->addurl(linktable, hostname, path);
        buffer = buffer_init();
        if(pthread_create(&threadid, NULL, &pth_handler, (void *)linktable) != 0)
        {
            fprintf(stderr, "Create NEW thread failed, %s\n", strerror(errno));
            _exit(-1);
        }
        while(1)
        {
            if((sid = linktable->get_request(linktable, &request)) != -1)
            {
                fprintf(stdout, "num:%d http://%s%s %s\n", sid, request->host, 
                        request->path, request->ip);
                memset(&sa, 0, sizeof(struct sockaddr_in));
                sa.sin_family = AF_INET;
                sa.sin_addr.s_addr = inet_addr(request->ip);
                sa.sin_port = htons(request->port);
                sa_len = sizeof(struct sockaddr);
                fd = socket(AF_INET, SOCK_STREAM, 0);
                if(fd > 0 &&  connect(fd, (struct sockaddr *)&sa, sa_len) == 0 )
                {
                    fcntl(fd, F_SETFL, O_NONBLOCK);
                    FD_ZERO(&readset);
                    FD_SET(fd,&readset);
                    n = sprintf(buf, "GET %s HTTP/1.0\r\nHOST: %s\r\nUser-Agent: Mozilla\r\n\r\n", 
                            request->path, request->host);
                    write(fd, buf, n); 
                    fprintf(stdout, "---request----\r\n%s----end----\r\n", buf);
                    buffer->reset(buffer);
                    memset(&response, 0, sizeof(HTTP_RESPONSE));
                    response.respid = -1;
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
                                    shutdown(fd, SHUT_RDWR);
                                    close(fd);
                                    break;
                                }
                            }
                            else 
                            {
                                shutdown(fd, SHUT_RDWR);
                                close(fd);
                                break;
                            }
                        }
                        usleep(10);
                    }
                    buffer->push(buffer, "\0", 1);
                    if(response.respid != -1)
                    {
                        PARSE_RESPONSE(p, end, buffer, response);
                    }
                    if(response.respid == RESP_OK)
                    {
                        p = (buffer->data+response.header_size);
                        end = (char *)(char *)buffer->end;
                        /*
                        for(i = 0; i < HTTP_RESPONSE_NUM; i++)
                        {
                            if(response.headers[i])
                                fprintf(stdout, "%s\n", response.headers[i]);
                        }*/
                        linktable->add_content(linktable, &response, 
                                request->host, request->path, p, (end - p));
                        fprintf(stdout, "parser start\n");
                        linktable->parse(linktable, request->host, 
                                request->path, p, end);
                        linktable->update_request(linktable, sid, LINK_STATUS_OVER);
                        fprintf(stdout, "parser over\n");
                    }
                    else
                    {
                        linktable->update_request(linktable, sid, LINK_STATUS_ERROR);
                    }
                } 
                else
                {
                    fprintf(stderr, "connected failed,%s", strerror(errno));
                    close(fd);
                    linktable->update_request(linktable, sid, LINK_STATUS_DISCARD);
                }
                
            }
            usleep(100);
        }
        linktable->clean(&linktable);
        buffer->clean(&buffer);
    }
}
#endif
