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
#define ADD_TO_MD5TABLE(ptr, md5str, id)                        \
{                                                               \
    if(ptr)                                                     \
    {                                                           \
        MUTEX_LOCK(ptr->mutex);                                 \
        TABLE_ADD(ptr->md5table, md5str, id);                   \
        MUTEX_UNLOCK(ptr->mutex);                               \
    }                                                           \
}
#define GET_FROM_MD5TABLE(ptr, md5str, id)                      \
{                                                               \
    if(ptr)                                                     \
    {                                                           \
        MUTEX_LOCK(ptr->mutex);                                 \
        id = (long )TABLE_GET(ptr->md5table, md5str);           \
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
        NIO_CHECK(linktable->md5io);
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
        NIO_CHECK(linktable->urlio);
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
        NIO_CHECK(linktable->metaio);
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
        NIO_CHECK(linktable->docio);
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

    if(linktable && host && path && content && end)	
    {
        p = content;
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
                        linktable->add(linktable, (unsigned char *)host, 
                                (unsigned char *)path,  (unsigned char *)link,
                                (unsigned char *)p);
                        //fprintf(stdout, "%s\n", link);
                    }
                }
                //}
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
        linktable->addurl(linktable, (char *)lhost, (char *)lpath);
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

    if(linktable)
    {
        DEBUG_LOGGER(linktable->logger, "New URL:http://%s%s", host, path);
        memset(&req, 0, sizeof(HTTP_REQUEST));
        //check dns
        if(NIO_CHECK(linktable->md5io) == 0 
                && NIO_CHECK(linktable->urlio) == 0 
                && (n = sprintf(url, "http://%s%s", host, path)) > 0)
        {
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
            NIO_LOCK(linktable->urlio);
            if(NIO_APPEND(linktable->urlio, url, n+1) <= 0) 
            {
                NIO_UNLOCK(linktable->urlio);
                goto err_end;
            }
            NIO_UNLOCK(linktable->urlio);
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
            DEBUG_LOGGER(linktable->logger, "New URL:http://%s%s", host, path);
            req.id = id = linktable->url_total++;
            NIO_LOCK(linktable->md5io);
            if(NIO_APPEND(linktable->md5io, &req, sizeof(HTTP_REQUEST)) <= 0 ) 
            {
                NIO_UNLOCK(linktable->md5io);
                goto err_end;
            }
            NIO_UNLOCK(linktable->md5io);
            ADD_TO_MD5TABLE(linktable, md5str, (long *)id);
            ret = 0;
            DEBUG_LOGGER(linktable->logger, "Added URL[%d] md5[%s] %s", id, md5str, url);
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
        MUTEX_LOCK(linktable->mutex);
        DEBUG_LOGGER(linktable->logger, "Ready for [%s]'s ip", hostname);
        if((ip = (char *)TABLE_GET(linktable->dnstable, hostname))) goto end;
        DEBUG_LOGGER(linktable->logger, "Ready for [%s]'s ip", hostname);
        if((hp = gethostbyname((const char *)hostname)) == NULL) goto end;
        DEBUG_LOGGER(linktable->logger, "Ready for [%s]'s ip", hostname);
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

    if(linktable && req)
    {
        if(NIO_CHECK(linktable->md5io) == 0)
        {
            NIO_LOCK(linktable->md5io);
            offset = (linktable->urlno * sizeof(HTTP_REQUEST));
            if(NIO_SEEK(linktable->md5io, offset) >= 0)
            {
                while(NIO_READ(linktable->md5io, req, sizeof(HTTP_REQUEST)) > 0)
                {
                    linktable->urlno++;
                    if(req->status == LINK_STATUS_INIT)
                    {
                        ret = 0;
                        break;
                    }
                    else continue;
                }
            }
            NIO_UNLOCK(linktable->md5io);
        }
    }
    return ret;
}

/* Update link status */
int linktable_update_request(LINKTABLE *linktable, int id, int status)
{
    int ret = -1;

    if(linktable && id >= 0 && id < linktable->url_total)
    {
        NIO_LOCK(linktable->md5io);
        if(NIO_CHECK(linktable->md5io) == 0 && NIO_SWRITE(linktable->md5io, 
                    &status, sizeof(int), (id * sizeof(HTTP_REQUEST))))
        {
            DEBUG_LOGGER(linktable->logger, "Update request[%d] to status[%d]", id ,status);
            ret = 0;
        }
        NIO_UNLOCK(linktable->md5io);
    }
    return ret;
}

//get url task
long linktable_get_urltask(LINKTABLE *linktable)
{
    URLMETA urlmeta;
    long taskid = -1;
    int i = 0, n = 0;
    long long offset = 0;

    if(linktable)
    {
        for(i = 0; i < linktable->ntask; i++)
        {
            if(taskid == -1 && linktable->tasks[i].status == URL_STATUS_WAIT)
            {
                DEBUG_LOGGER(linktable->logger, "task[%d] taskid:%d status:%d docno:%d", 
                        i, taskid, linktable->tasks[i].status, linktable->docno);
                taskid = i;
                linktable->tasks[i].status = URL_STATUS_WORKING;
                continue;
            }
            if(linktable->tasks[i].status != URL_STATUS_WORKING 
                    && linktable->tasks[i].status != URL_STATUS_WAIT 
                    && linktable->docno < linktable->doc_total)
            {
                offset = linktable->docno * sizeof(URLMETA);
                NIO_LOCK(linktable->metaio);
                if(NIO_SEEK(linktable->metaio, offset) >= 0)
                {
                    while((n = NIO_READ(linktable->metaio, 
                                    &(linktable->tasks[i]), sizeof(URLMETA))) > 0)
                    {
                        DEBUG_LOGGER(linktable->logger, 
                                "n:%d task[%d] taskid:%d status:%d docno:%d off:%lld size:%d", 
                                n, i, taskid, linktable->tasks[i].status, linktable->docno, 
                                linktable->tasks[i].offset, linktable->tasks[i].zsize);
                        offset += sizeof(URLMETA);
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
                NIO_UNLOCK(linktable->metaio);
            }
        }
        if(taskid != -1)
        {
            DEBUG_LOGGER(linktable->logger, "task:%d off:%lld length:%d docno:%d doctotal:%d", 
                    taskid, linktable->tasks[taskid].offset, linktable->tasks[taskid].zsize,
                    linktable->docno, linktable->doc_total);
        }
    }
end:
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
    int fd = 0;
    long long offset;

    if(linktable && linktable->tasks)
    {
        urlmeta = &(linktable->tasks[taskid]); 
        if((data = (char *)calloc(1, urlmeta->size)))
        {
            ndata = urlmeta->size;
            if(urlmeta->zsize > 0 )
            {
                if((zdata = (char *)calloc(1, urlmeta->zsize)) == NULL)
                {
                    FATAL_LOGGER(linktable->logger, "Calloc zdata size:%d failed %s", 
                            urlmeta->zsize, strerror(errno));
                    goto err_end;
                }
                //read document
                NIO_LOCK(linktable->docio);
                if(NIO_SREAD(linktable->docio, zdata, urlmeta->zsize, urlmeta->offset) <= 0)
                {
                    FATAL_LOGGER(linktable->logger, 
                            "Read from ducoment[%s] offset[%lld] size:%d failed, %s",
                            PF(linktable->docio)->path, urlmeta->offset, 
                            urlmeta->zsize, strerror(errno));
                    NIO_UNLOCK(linktable->docio);
                    goto err_end;
                }
                NIO_UNLOCK(linktable->docio);
                //decompress
                n = urlmeta->zsize;
                if(zdecompress(zdata, n, data, &ndata) != 0)
                {
                    FATAL_LOGGER(linktable->logger, "Decompress zdata size:%d failed, %s",
                            urlmeta->zsize, strerror(errno));
                    goto err_end;
                }
                if(zdata) free(zdata);
                zdata = NULL;
            }
            else
            {
                NIO_LOCK(linktable->docio);
                if(NIO_SREAD(linktable->docio, data, urlmeta->size, urlmeta->offset) <= 0)
                {
                    FATAL_LOGGER(linktable->logger, 
                            "Read from ducoment[%s] offset[%lld] size:%d failed, %s",
                            PF(linktable->docio)->path, urlmeta->offset, 
                            urlmeta->size, strerror(errno));
                    NIO_UNLOCK(linktable->docio);
                    goto err_end;
                }
                NIO_UNLOCK(linktable->docio);
            }
            if(data)
            {
                host = data + urlmeta->hostoff;
                path = data + urlmeta->pathoff;
                p = data + urlmeta->htmloff;
                end = data + urlmeta->size;
                linktable->parse(linktable, host, path, p, end);
                status = URL_STATUS_OVER;
                free(data); data = NULL;
            }
err_end:
            NIO_LOCK(linktable->metaio);
            offset = urlmeta->id * sizeof(URLMETA);
            if(NIO_SWRITE(linktable->metaio, &status, sizeof(int), offset) <= 0)
            {
                DEBUG_LOGGER(linktable->logger, "Write status for id[%d] failed, %s", 
                        urlmeta->id, strerror(errno));
            }
            NIO_UNLOCK(linktable->metaio);
            urlmeta->status = status;
        }
    }
    return ;
}

/* add content as zdata */
int linktable_add_zcontent(LINKTABLE *linktable, URLMETA *urlmeta, char *zdata, int nzdata)
{
    int ret = -1;
    if(linktable && urlmeta && zdata)
    {
        NIO_LOCK(linktable->docio);
        urlmeta->offset = NIO_SEEK_END(linktable->docio);
        if(NIO_WRITE(linktable->docio, zdata, nzdata) > 0)
        {
            DEBUG_LOGGER(linktable->logger, "Add meta offset:%lld zsize:%d hostoff:%d "
                    "pathoff:%d htmloff:%d",
                    urlmeta->offset, urlmeta->zsize, urlmeta->hostoff, 
                    urlmeta->pathoff, urlmeta->htmloff);
            NIO_LOCK(linktable->metaio);
            urlmeta->status = URL_STATUS_INIT;
            if(NIO_APPEND(linktable->metaio, (urlmeta), sizeof(URLMETA)) > 0)
            {
                linktable->docok_total++;
                linktable->size += urlmeta->size;
                linktable->zsize += nzdata;
                ret = 0;
            }
            else
            {
                ERROR_LOGGER(linktable->logger, "Adding meta document length[%d] failed, %s",
                        urlmeta->size, strerror(errno));
            }
            NIO_UNLOCK(linktable->metaio);
        }
        else
        {
            ERROR_LOGGER(linktable->logger, "Adding document length[%d] failed, %s",
                    urlmeta->size, strerror(errno));
        }
        NIO_UNLOCK(linktable->docio);
        linktable->doc_total++;
    }
    return ret;
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
                DEBUG_LOGGER(linktable->logger, "Compress document size:%d to size:%d ",
                        urlmeta.size, urlmeta.zsize);
            }
            else
            {
                ERROR_LOGGER(linktable->logger, "Compress data length[%d] failed, %s",
                        n, strerror(errno));
                goto end;
            }
            NIO_LOCK(linktable->docio);
            urlmeta.offset = NIO_SEEK_END(linktable->docio);
            if(NIO_WRITE(linktable->docio, p, n) > 0)
            {
                DEBUG_LOGGER(linktable->logger, "Add meta offset:%lld zsize:%d hostoff:%d "
                        "pathoff:%d htmloff:%d",
                        urlmeta.offset, urlmeta.zsize, urlmeta.hostoff, 
                        urlmeta.pathoff, urlmeta.htmloff);
                NIO_LOCK(linktable->metaio);
                if(NIO_APPEND(linktable->metaio, (&urlmeta), sizeof(URLMETA)) > 0)
                {
                    linktable->docok_total++;
                    linktable->size += ncontent;
                    linktable->zsize += n;
                    ret = 0;
                }
                else
                {
                    ERROR_LOGGER(linktable->logger, "Adding meta document length[%d] failed, %s",
                            urlmeta.size, strerror(errno));
                }
                NIO_UNLOCK(linktable->metaio);
            }
            else
            {
                ERROR_LOGGER(linktable->logger, "Adding document length[%d] failed, %s",
                        urlmeta.size, strerror(errno));
            }
            NIO_UNLOCK(linktable->docio);
end:
            if(zdata) free(zdata);
            if(data) free(data);
        }
        linktable->doc_total++;
    }
    return ret;
}


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
        NIO_CHECK(linktable->md5io);
        if(NIO_SEEK_START(linktable->md5io) < 0) return;
        while(NIO_READ(linktable->md5io, &req, sizeof(HTTP_REQUEST)) > 0)
        {
            p = md5str;
            for(i = 0; i < MD5_LEN; i++)
                p += sprintf(p, "%02x", req.md5[i]);
            ADD_TO_MD5TABLE(linktable, md5str, (long *)++n);
            ip = linktable->getip(linktable->dnstable, req.host);
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
        NIO_CHECK(linktable->metaio);
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
        linktable->urlhandler       = linktable_urlhandler; 
        linktable->resume           = linktable_resume; 
        linktable->clean            = linktable_clean; 
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
   // DEBUG_LOGGER(ltable->logger, "thread[%08x] start .....", pthread_self());
    while(1)
    {
        if((taskid = ltable->get_urltask(ltable)) != -1)
        {
            DEBUG_LOGGER(ltable->logger, "start task:%d", taskid);
            ltable->urlhandler(ltable, taskid);
            DEBUG_LOGGER(ltable->logger, "Completed task:%d", taskid);
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
    HTTP_REQUEST request;
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
        linktable->set_logger(linktable, "/tmp/link.log", NULL);
        linktable->set_md5file(linktable, "/tmp/link.md5");
        linktable->set_urlfile(linktable, "/tmp/link.url");
        linktable->set_metafile(linktable, "/tmp/link.meta");
        linktable->set_docfile(linktable, "/tmp/link.doc");
        linktable->set_ntask(linktable, 4);
        linktable->iszlib = 1;
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
            //DEBUG_LOGGER(linktable->logger, "thread[%08x] start .....", pthread_self());
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
                if(fd > 0 &&  connect(fd, (struct sockaddr *)&sa, sa_len) == 0 )
                {
                    fcntl(fd, F_SETFL, O_NONBLOCK);
                    FD_ZERO(&readset);
                    FD_SET(fd,&readset);
                    n = sprintf(buf, "GET %s HTTP/1.0\r\nHOST: %s\r\nUser-Agent: Mozilla\r\n\r\n", 
                            request.path, request.host);
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
                        if(linktable->add_content(linktable, &response, 
                                request.host, request.path, p, (end - p)) != 0)
                        {
                            ERROR_LOGGER(linktable->logger, "Adding http://%s%s content failed, %s",
                                    request.host, request.path, strerror(errno));
                        }
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
    }
}
#endif
