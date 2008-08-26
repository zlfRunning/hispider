#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "ltable.h"
#include "md5.h"
#include "trie.h"
#include "ibio.h"
#include "basedef.h"
#include "zstream.h"
#include "mutex.h"
#include "logger.h"
#include "timer.h"
#include "http.h"
#include "dnsdb.h"
static const char *__html__body__  = 
"<HTML><HEAD>\n"
"<TITLE>Hispider Running Status</TITLE>\n"
"<meta http-equiv='refresh' content='10; URL=/'>\n</HEAD>\n"
"<meta http-equiv='content-type' content='text/html; charset=UTF-8'>\n"
"<BODY bgcolor='#000000' align=center >\n"
"<h1><font color=white >Hispider Running State</font></h1>\n"
"<hr noshade><ul><br><table  align=center width='100%%' >\n"
"<tr><td align=left ><li><font color=red size=72 >URL Total:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >URL Current :%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >URL OK:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >URL ERROR:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Doc Size:%lld </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Doc Zsize:%lld </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >DNS count:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Time Used:%lld (usec)</font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Speed:%lld (k/s)</font></li></td></tr>\n"
"</table><br><hr  noshade><em>\n"
"<font color=white ><a href='http://code.google.com/p/hispider' >"
"Hispider</a> Powered By <a href='http://code.google.com/p/hispider'>"
"http://code.google.com/p/hispider</a></font>"
"</BODY></HTML>\n";

int pmkdir(char *path, int mode)
{
   char *p = NULL, fullpath[HTTP_PATH_MAX];
   int ret = 0, level = -1;
   struct stat st;

   if(path)
   {
       strcpy(fullpath, path);    
       p = fullpath;
       while(*p != '\0')
       {
           if(*p == '/' ) 
           {       
               level++;
               while(*p != '\0' && *p == '/' && *(p+1) == '/')++p;
               if(level > 0)
               {       
                   *p = '\0'; 
                   memset(&st, 0, sizeof(struct stat)); 
                   ret = stat(fullpath, &st);
                   if(ret == 0 && !S_ISDIR(st.st_mode)) return -1;
                   if(ret != 0 && mkdir(fullpath, mode) != 0) return -1;
                   *p = '/';
               }       
           }       
           ++p;
       }
       return 0;
   }
   return -1;
}

/* set logger */
int ltable_set_logger(LTABLE *ltable, char *logfile, void *logger)
{
    if(ltable)
    {
        ltable->logger = logger;
        ltable->isinsidelogger = 0;
        if(logfile) 
        {
            LOGGER_INIT(ltable->logger, logfile);
            ltable->isinsidelogger = 1;
        }
        DEBUG_LOGGER(ltable->logger, "Setting logfile %s", logfile);
        return 0;
    }
    return -1;
}


/* set basedir */
int ltable_set_basedir(LTABLE *ltable, char *basedir)
{
    char path[LTABLE_PATH_MAX];

    if(ltable)
    {
        sprintf(path, "%s/%s", basedir, LTABLE_META_NAME);
        pmkdir(path, 0755);
        ltable->meta_fd = iopen(path);
        sprintf(path, "%s/%s", basedir, LTABLE_URL_NAME);
        ltable->url_fd = iopen(path);
        sprintf(path, "%s/%s", basedir, LTABLE_DOC_NAME);
        ltable->doc_fd = iopen(path);
        ltable->dnsdb->set_basedir(ltable->dnsdb, basedir);
        return 0;
    }
    return -1;
}

/* Parse HTML CODE for getting links */
int ltable_parselink(LTABLE *ltable, char *host, char *path, char *content, char *end)
{
    char *p = NULL;
    char *link = NULL;
    int n = 0, pref = 0;
    int count = 0;

    if(ltable && host && path && content && end)	
    {
        p = content;
        while(p < end)
        {
            if(p < (end - 1) && *p == '<' && (*(p+1) == 'a' || *(p+1) == 'A'))
            {
                p += 2;
                pref = 0;
                while(p < end && (*p == 0x20 || *p == 0x09)) ++p;
                //DEBUG_LOGGER(ltable->logger, "%d %c\n", __LINE__, *p);
                if(p >= (end - 4) && strncasecmp(p, "href", 4) != 0) continue;
                //fprintf(stdout, "%s\n", p);
                //DEBUG_LOGGER(ltable->logger, "%d %c\n", __LINE__, *p);
                p += 4;
                //DEBUG_LOGGER(ltable->logger, "%d %c\n", __LINE__, *p);
                while(p < end && (*p == 0x20 || *p == 0x09)) ++p;
                //DEBUG_LOGGER(ltable->logger, "%d %c\n", __LINE__, *p);
                if(*p != '=') continue;
                ++p;
                while(p < end && (*p == 0x20 || *p == 0x09)) ++p;
                //DEBUG_LOGGER(ltable->logger, "%d %c\n", __LINE__, *p);
                if(*p == '"' || *p == '\''){++p; pref = 1;}
                //DEBUG_LOGGER(ltable->logger, "%d %c\n", __LINE__, *p);
                //if(*p == '/' || (*p >= '0' && *p <= '9') 
                //        || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
                //{
                //DEBUG_LOGGER(ltable->logger, "%d %c\n", __LINE__, *p);
                link = p;
                if(pref){while(p < end && *p != '\'' && *p != '"')++p;}
                else {while(p < end && *p != 0x20 && *p != 0x09 && *p != '>')++p;}
                DEBUG_LOGGER(ltable->logger, "left:%d\n",(end - p));
                //fprintf(stdout, "%s\n", p);
                if((n = (p - link)) > 0)
                {
                    if(*link != '#' && strncasecmp("javascript", link, 10) != 0)
                    {
                        DEBUG_LOGGER(ltable->logger, 
                                "Ready for adding URL from page[%s%s] %d", host, path, n);
                        ltable->addlink(ltable, (unsigned char *)host, (unsigned char *)path, 
                                (unsigned char *)link, (unsigned char *)p);
                        count++;
                        //fprintf(stdout, "%s\n", link);
                    }
                }
                //}
            }
            ++p;
        }	
        DEBUG_LOGGER(ltable->logger, "Parsed http://%s%s count:%d", host, path, count);
        return 0;
    }
    return -1;
}

/* add link to table */
int ltable_addlink(LTABLE *ltable, unsigned char *host, unsigned char *path, 
        unsigned char *href, unsigned char *ehref)
{
    unsigned char lhost[HTTP_HOST_MAX];
    unsigned char lpath[HTTP_PATH_MAX];
    unsigned char *p = NULL, *ps = NULL, *last = NULL, *end = lpath + HTTP_PATH_MAX;
    int n = 0;

    if(ltable && host && path && href && (ehref - href) > 0 && (ehref - href) < HTTP_PATH_MAX)
    {
        memset(lhost, 0, HTTP_HOST_MAX);
        memset(lpath, 0, HTTP_PATH_MAX);
        p = href;
        //DEBUG_LOGGER(ltable->logger, "addurl:http://%s%s ", lhost, lpath);
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
            while(p < ehref && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) p++;
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
        //DEBUG_LOGGER(ltable->logger, "addurl:http://%s%s ", lhost, lpath);
        if(p && ps)
        {
            while(p < ehref && ps < end)
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
        //DEBUG_LOGGER(ltable->logger, "addurl:http://%s%s ", lhost, lpath);
        ltable->addurl(ltable, (char *)lhost, (char *)lpath);
        //DEBUG_LOGGER(ltable->logger, "addurl:http://%s%s ", lhost, lpath);
        //TIMER_INIT(timer);
        //TIMER_SAMPLE(timer);
        //DEBUG_LOGGER(ltable->logger, "addurl:http://%s%s time used:%lld ", 
        //        lhost, lpath, PT_LU_USEC(timer));
        //TIMER_CLEAN(timer);
    }
    return 0;
}

/* add url to ltable */
int ltable_addurl(LTABLE *ltable, char *host, char *path)
{
    char url[HTTP_URL_MAX];
    LMETA lmeta = {0};
    char *p = NULL, *ps = NULL;
    void *dp = NULL;
    off_t offset = 0;
    int ret = -1, n = 0;
    long id = 0;

    if(ltable)
    {
        //check dns
        MUTEX_LOCK(ltable->mutex);
        if((n = sprintf(url, "http://%s%s", host, path)) > 0)
        {
            md5((unsigned char *)url, n, lmeta.id);
            TRIETAB_GET(ltable->urltable, lmeta.id, DOC_KEY_LEN, dp);
            if(dp) 
            {
                DEBUG_LOGGER(ltable->logger, "URL:[http://%s%s][%ld] is exists", 
                        host, path, (long)dp);
                goto end;
            }
            //add url to url file 
            url[n] = '\n';
            if(iappend(ltable->url_fd, url, (n + 1), &offset) <= 0)
            {
                ERROR_LOGGER(ltable->logger, "Adding url via %d failed, %s", 
                        ltable->url_fd, strerror(errno));
                goto err_end;
            }
            url[1] = '\0';
            lmeta.offurl = offset;
            lmeta.nurl = n;
            if(iappend(ltable->meta_fd, &lmeta, sizeof(LMETA), &offset) <= 0)
            {
                ERROR_LOGGER(ltable->logger, "Adding meta via %d failed, %s",
                        ltable->meta_fd, strerror(errno));
                goto err_end;
            }
            //add to urltable
            ltable->url_total = id = (offset/sizeof(LMETA)) + 1;
            dp = (void *)id;
            TRIETAB_ADD(ltable->urltable, lmeta.id, DOC_KEY_LEN, dp);
            //host
            p = ps = url + strlen("http://");
            while(*p != '\0' && *p != ':' && *p != '/')++p;
            if((n = (p - ps)) > 0)
            {
                *p = '\0';
                //add host
                ltable->dnsdb->resolve(ltable->dnsdb, ps);
            }
        }
end:
        ret = 0;
err_end:
        MUTEX_UNLOCK(ltable->mutex);
    }
    return ret;
}

/* resume ltable */
int ltable_resume(LTABLE *ltable)
{
    LMETA lmeta = {0};
    void *dp = NULL;
    int flag = 0;

    if(ltable)
    {
        /* resume dnsdb */
        ltable->dnsdb->resume(ltable->dnsdb);
        /* resume meta to urltable */
        lseek(ltable->meta_fd, 0, SEEK_SET);
        //fprintf(stdout, "%d::%ld:%ld\n", __LINE__, ltable->url_current, ltable->url_total);
        while(read(ltable->meta_fd, &lmeta, sizeof(LMETA)) > 0)
        {
            dp = (void *)++(ltable->url_total);
            TRIETAB_ADD(ltable->urltable, lmeta.id, DOC_KEY_LEN, dp);
            //check status
            if(lmeta.state == TASK_STATE_INIT && flag == 0)
            {
                ltable->url_current  = ltable->url_total - 1;
            }
            else if(lmeta.state == TASK_STATE_OK) ltable->url_ok++;
            else if(lmeta.state == TASK_STATE_ERROR) ltable->url_error++;
        }
        //fprintf(stdout, "%d::%ld:%ld\n", __LINE__, ltable->url_current, ltable->url_total);
        return 0;
    }
    return -1;
}


/* get task (return task id) */
int  ltable_get_task(LTABLE *ltable, char *block, long *nblock)
{
    char url[HTTP_URL_MAX], *p = NULL, *ps = NULL, *path = NULL, ch = 0;
    unsigned char *sip = NULL;
    int taskid = -1, ip = 0, port = 0;
    off_t offset = 0;
    LMETA lmeta = {0};

    if(ltable)
    {
        MUTEX_LOCK(ltable->mutex);
        if(ltable->url_total > 0 && ltable->url_total > ltable->url_current)
        {
            do
            {
                offset = (ltable->url_current) * sizeof(LMETA);
                if(iread(ltable->meta_fd, &lmeta, sizeof(LMETA), offset) > 0)
                {
                    ltable->url_current++;
                    if(lmeta.state == TASK_STATE_INIT)
                    {
                        if(iread(ltable->url_fd, url, lmeta.nurl, lmeta.offurl) > 0)
                        {
                            url[lmeta.nurl] = '\0';
                            p = ps = url + strlen("http://");
                            while(*p != '\0' && *p != ':' && *p != '/')++p;
                            ch = *p;
                            *p = '\0';
                            if((ip = ltable->dnsdb->get(ltable->dnsdb, ps)) <= 0) 
                            {
                                ltable->url_current--;
                                goto err_end;
                            }
                            port = 80;
                            if(ch == ':') port = atoi((p+1));
                            if(ch == '/') path = (p+1);
                            else 
                            {
                                while(*p != '\0' && *p != '/')++p;
                                path = (p+1);
                            }
                            sip = (unsigned char *)&ip;
                            taskid = ltable->url_current - 1;
                            *nblock = sprintf(block, "%s\r\nFrom: %d\r\nLocation: /%s\r\n"
                                    "Host: %s\r\nServer: %d.%d.%d.%d\r\nReferer:%d\r\n\r\n", 
                                    HTTP_RESP_OK, taskid, path, ps, 
                                    sip[0], sip[1], sip[2], sip[3], port);
                            /*
                            fprintf(stdout, "%d::OK taskid:%d current:%d total:%d\n", 
                                    __LINE__, taskid, ltable->url_current, ltable->url_total);
                            */
                        }
                        else 
                        {
                            goto err_end;
                        }
                    }
                }else goto err_end;
            }while(taskid == -1);
        }
err_end:
        MUTEX_UNLOCK(ltable->mutex);
    }

    return taskid;
}
/*
n = sprintf(buf, "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n"
                                    "Accept: %s\r\nAccept-Language: %s\r\nAccept-Encoding: %s\r\n"
                                    "Accept-Charset: %s\r\nConnection: close\r\n\r\n", path, ps, 
                                    USER_AGENT, ACCEPT_TYPE, ACCEPT_LANGUAGE, ACCEPT_ENCODING, 
                                    ACCEPT_CHARSET);
*/
/* get ltable status */
int ltable_get_stateinfo(LTABLE *ltable, char *block)
{
    char buf[HTTP_BUF_SIZE];
    int ret = -1, n = 0;

    if(ltable)
    {
        n = sprintf(buf, __html__body__, ltable->url_total, ltable->url_current, 
                ltable->url_ok, ltable->url_error, ltable->doc_size, ltable->doc_size,
                ltable->doc_zsize, ltable->dnsdb->total);   
        ret = sprintf(block, "HTTP/1.0 200 OK \r\nContent-Type: text/html\r\n"
                                    "Content-Length: %d\r\n\r\n%s", n, buf);
    }
    return ret;
}

/* set task state */
int ltable_set_task_state(LTABLE *ltable, int taskid, int state)
{
    if(ltable && ltable->meta_fd > 0 && taskid >= 0 && taskid < ltable->url_total )
    {
        return iwrite(ltable->meta_fd, &state, sizeof(int), taskid * sizeof(LMETA));
    }
    return -1;
}

/* add document as taskid/status/date/content */
int ltable_add_document(LTABLE *ltable, int taskid, int date, char *content, int ncontent)
{
    int ret = -1, ndata = 0, n = 0;
    off_t offset = 0;
    char *data = NULL, *p = NULL, *ps = NULL, *path = NULL, 
         host[HTTP_HOST_MAX], url[HTTP_URL_MAX];
    LMETA lmeta = {0};
    LHEADER *lheader = NULL;

    if(ltable && content && ncontent > 0)
    {
       if(ltable->meta_fd > 0 && iread(ltable->meta_fd, &lmeta, 
                   sizeof(LMETA), taskid * sizeof(LMETA)) > 0)
        {
            memset(url, 0, HTTP_URL_MAX);
            if(ltable->url_fd > 0 && iread(ltable->url_fd, url, lmeta.nurl, lmeta.offurl) > 0)
            {
                url[lmeta.nurl] = '\0';
                p = url + strlen("http://");
                ps = host;
                while(*p != '\0' && *p != '/') *ps++ = *p++;
                *ps = '\0'; 
                path = p;
                ndata = ncontent * 20;
                if((data = calloc(1, ndata)) && zdecompress((Bytef *)content, 
                            (uLong )ncontent, (Bytef *)data, (uLong *)&ndata) == 0)
                {
                    ltable->parselink(ltable, host, path, data, (data + ndata));
                    lheader = (LHEADER *)data;
                    p = data + sizeof(LHEADER);
                    lheader->ndate = time(NULL);
                    lheader->nurl = lmeta.nurl;
                    lheader->nzdata = ncontent;
                    lheader->ndata = ndata;
                    strcpy(p, url);
                    p += lmeta.nurl + 1;
                    memcpy(p, content, ncontent);
                    n = p - data;
                    if(iappend(ltable->doc_fd, data, n, &offset) > 0)
                    {
                        lmeta.offset    = offset;
                        lmeta.length    = n;
                        lmeta.date      = date;
                        lmeta.state     = TASK_STATE_OK;
                        iwrite(ltable->meta_fd, &lmeta, sizeof(LMETA), taskid * sizeof(LMETA));
                        ret = 0;
                    }
                    free(data);
                    data = NULL;
                }
            }
        }
    }
    return ret;
}

/* clean ltable */
void ltable_clean(LTABLE **pltable)
{
    if(pltable && *pltable)
    {
        TRIETAB_CLEAN((*pltable)->urltable);
        if((*pltable)->mutex)
        {
            MUTEX_DESTROY((*pltable)->mutex);
        }
        if((*pltable)->isinsidelogger)
        {
            LOGGER_CLEAN((*pltable)->logger);
        }
        if((*pltable)->dnsdb)
        {
            (*pltable)->dnsdb->clean(&((*pltable)->dnsdb));
        }
        free(*pltable);
        (*pltable) = NULL;
    }
}

/* initialize ltable */
LTABLE *ltable_init()
{
    LTABLE *ltable = NULL;

    if((ltable = calloc(1, sizeof(LTABLE))))
    {
        MUTEX_INIT(ltable->mutex);
        ltable->urltable        = TRIETAB_INIT();
        ltable->dnsdb           = dnsdb_init();
        ltable->set_basedir     = ltable_set_basedir;
        ltable->resume          = ltable_resume;
        ltable->parselink       = ltable_parselink;
        ltable->addlink         = ltable_addlink;
        ltable->addurl          = ltable_addurl;
        ltable->get_task        = ltable_get_task;
        ltable->set_task_state  = ltable_set_task_state;
        ltable->get_stateinfo   = ltable_get_stateinfo;
        ltable->add_document    = ltable_add_document;
        ltable->clean           = ltable_clean;
    }
    return ltable;
}

