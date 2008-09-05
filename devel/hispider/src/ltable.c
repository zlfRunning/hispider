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
#include "kvmap.h"
static const char *__html__body__  = 
"<HTML><HEAD>\n"
"<TITLE>Hispider Running Status</TITLE>\n"
"<meta http-equiv='refresh' content='10;'>\n</HEAD>\n"
"<meta http-equiv='content-type' content='text/html; charset=UTF-8'>\n"
"<BODY bgcolor='#000000' align=center >\n"
"<h1><font color=white >Hispider Running State  ["
"<script language='javascript'>\n"
"if(location.pathname == '/stop')\n"
"    document.write(\"<a href='/running' >running</a>\");\n"
"else\n"
"    document.write(\"<a href='/stop' >stop</a>\");\n"
"</script>]</font>\n</h1>\n"
"<hr noshade><ul><br><table  align=center width='100%%' >\n"
"<tr><td align=left ><li><font color=red size=72 >URL Total:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >URL Current :%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >URL OK:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >URL ERROR:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Doc Total:%lld/%lld </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Doc Current:%lld/%lld </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >DNS Count:%d/%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Time Used: %d day(s) [%02d:%02d:%02d +%06d]</font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Speed:%f (k/s)</font></li></td></tr>\n"
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
        sprintf(path, "%s/%s", basedir, DNS_HOST_NAME);
        ltable->host_fd = iopen(path);
        sprintf(path, "%s/%s", basedir, DNS_IP_NAME);
        ltable->dns_fd = iopen(path);
        return 0;
    }
    return -1;
}

/* Parse HTML CODE for getting links */
int ltable_parselink(LTABLE *ltable, char *host, char *path, char *content, char *end)
{
    char url[HTTP_URL_MAX], *p = NULL, *link = NULL, *ps = NULL;
    int n = 0, pref = 0;
    int count = 0;

    if(ltable && host && path && content && end)	
    {
        p = content;
        while(p < end)
        {
            if(p < (end - 2) && *p == '<' && (*(p+1) == 'a' || *(p+1) == 'A') && *(p+2) == 0x20)
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
                ps = url;
                if(pref && *link != '#' && strncasecmp("javascript", link, 10) != 0
                    && strncasecmp("mailto:", link, 7) != 0)
                {
                    while(p < end && *p != '\'' && *p != '"' && *p != '\r' && *p != '\n' 
                    && *p != '\t' && *p != 0x20 && (ps - url) < HTTP_URL_MAX) *ps++ = *p++;
                    if(*p == '\r' || *p == '\n' || *p == '\t' || *p == 0x20) continue;
                    if((n = (ps - url)) >= HTTP_URL_MAX) continue;
                    url[n] = '\0';
                    ltable->addlink(ltable, (unsigned char *)host, (unsigned char *)path, 
                            (unsigned char *)url, (unsigned char *)ps);
                    //DEBUG_LOGGER(ltable->logger, "NEWURL[%s] from http://%s%s", url, host, path);
                    count++;
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
    unsigned char lhost[HTTP_HOST_MAX], lpath[HTTP_PATH_MAX], tmp[HTTP_PATH_MAX];
    unsigned char *p = NULL, *ps = NULL, *pp = NULL,
        *last = NULL, *end = NULL;
    int n = 0;

    if(ltable && host && path && href && (ehref - href) > 0 && (ehref - href) < HTTP_PATH_MAX)
    {
        memset(lhost, 0, HTTP_HOST_MAX);
        memset(lpath, 0, HTTP_PATH_MAX);
        memset(tmp, 0, HTTP_PATH_MAX);
        p = href;
        //DEBUG_LOGGER(ltable->logger, "addurl:http://%s%s ", lhost, lpath);
        if(*p == '/')
        {
            ps = host;
            pp = href;
            //DEBUG_LOGGER(ltable->logger, "URL-0:http://%s|%s", ps, pp);
        }
        else if(*p == '.')
        {
            //n = sprintf(url, "http://%s/%s", host, path);
            ps = path;
            pp = tmp;
            *pp++ = '/';
            while(*ps != '\0')
            {
                if(*ps == '/') last = pp;
                *pp++ = *ps++;
            }
            pp = last;
            if(*(p+1) == '/') p += 2;
            while(*p == '.' && *(p+1) == '.' && *(p+2) == '/')
            {
                p += 3;
                --pp;
                while(pp > tmp && *pp != '/')--pp;
                if(pp <= tmp) break;
            }
            if(*p == '.' || pp < tmp) return -1;
            while(*p != '\0') *pp++ = *p++;
            *pp = '\0';
            ps = host;
            if(tmp[0] == '/' && tmp[1] == '/') pp = tmp+1;
            //DEBUG_LOGGER(ltable->logger, "URL-1:http://%s|%s", ps, pp);
        }
        else if((p < (ehref - 7)) && strncasecmp((char *)p, "http://", 7) == 0)
        {
            ps = p + 7;
            pp = ps;
            n = HTTP_HOST_MAX;
            while(pp < ehref && *pp != '/' && *pp != '?' && *pp != 0x20) ++pp; 
            if(*pp == '?' || *pp == 0x20) return -1;
            //DEBUG_LOGGER(ltable->logger, "URL-2:http://%s|%s", ps, pp);
        }
        else
        {
            //delete file:// mail:// ftp:// news:// rss:// eg. 
            p = href;
            while(p < ehref && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) p++;
            if(p < (ehref - 3) && *p == ':' && *(p+1) == '/' && *(p+2) == '/') return -1;
            ps = path;
            pp = tmp;
            while(*ps != '\0')
            {
                if(*ps == '/') last = (pp+1);
                *pp++ = *ps++;
            }
            pp = last;
            end = tmp + HTTP_PATH_MAX;
            while(p < ehref) 
            {
                *pp++ = *p++;
                if(p > end) return -1;
            }
            *pp = '\0';
            ps = host;
            pp = tmp;
            //DEBUG_LOGGER(ltable->logger, "URL-3:http://%s|%s", ps, pp);
        }
        if(ps && pp)
        {
            p = lhost;
            end = lhost + HTTP_HOST_MAX;
            while(*ps != '\0' && *ps != '/')
            {
                if(*ps >= 'A' && *ps <= 'Z')
                {
                    *p++ = *ps++ - ('A' - 'a');
                }
                else
                {
                    *p++ = *ps++;
                }
                if(p >= end) return -1;
            }
            if(p > lhost && *(p-1) == '.') *(p-1) = '\0';
            p = lpath;
            end = lpath + HTTP_PATH_MAX;
            while(*pp != '\0')
            {
                if(*pp >= 'A' && *pp <= 'Z')
                {
                    *p++ = *pp++ - ('A' - 'a');
                }
                else if(*pp > 127 || *pp == 0x20)
                {
                    p += sprintf((char *)p, "%%%02x", *pp++);
                }
                else
                {
                    *p++ = *pp++;
                }
                if(p >= end) return -1;
            }
            if(lpath[0] == '\0'){lpath[0] = '/';}
            if(lhost[0] == '\0' || lpath[0] == '\0') return -1;
            DEBUG_LOGGER(ltable->logger, "addurl:http://%s%s", lhost, lpath);
            ltable->addurl(ltable, (char *)lhost, (char *)lpath);
        }
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
    char *p = NULL, *ps = NULL, *dot_off = NULL, *newhost = NULL;
    void *dp = NULL, *olddp = NULL;
    off_t offset = 0;
    int ret = -1, n = 0;
    long id = 0;

    if(ltable)
    {
        //check dns
        p = host;
        while((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') 
                || (*p >= 'A' && *p <= 'Z') || *p == '.' || *p == '-')
        {
            if(*p == '.') dot_off = p;
            ++p;
        }
        if(dot_off == NULL || (p - host) > 64 || (p - dot_off) > 4) return -1;
        MUTEX_LOCK(ltable->mutex);
        if((n = sprintf(url, "http://%s%s", host, path)) > 0)
        {
            md5((unsigned char *)url, n, lmeta.id);
            KVMAP_GET(ltable->urltable, lmeta.id, dp);
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
            KVMAP_ADD(ltable->urltable, lmeta.id, dp, olddp);
            //host
            p = ps = url + strlen("http://");
            while(*p != '\0' && *p != ':' && *p != '/')++p;
            if((n = (p - ps)) > 0)
            {
                *p = '\0';
                newhost = ps;
            }
        }
end:
        ret = 0;
err_end:
        MUTEX_UNLOCK(ltable->mutex);
        if(newhost) ltable->add_host(ltable, newhost);
    }
    return ret;
}

/* add host */
int ltable_add_host(LTABLE *ltable, char *host)
{
    int n = 0, ret = -1;
    void *dp = NULL;
    DNS dns = {0};
    off_t offset = 0;

    if(ltable)
    {
        MUTEX_LOCK(ltable->mutex);
        n = strlen(host);
        TRIETAB_RGET(ltable->dnstable, host, n, dp);
        host[n] = '\n';
        if(dp == NULL && iappend(ltable->host_fd, host, n+1, &offset) > 0)
        {
            dns.offset = offset;
            dns.length = n;
            if(iappend(ltable->dns_fd, &dns, sizeof(DNS), &offset) > 0)
            {
                dp = (void *)((long)(( offset/ sizeof(DNS) ) + 1));
                TRIETAB_RADD(ltable->dnstable, host, n, dp);
                ltable->dns_total++;
            }
            ret = 0;
        }
        host[n] = '\0';
        MUTEX_UNLOCK(ltable->mutex);
    }
    return ret;
}

/* new dns task */
int ltable_new_dnstask(LTABLE *ltable, char *host)
{
    DNS dns = {0};
    int taskid  = -1;

    if(ltable && ltable->dns_current < ltable->dns_total)
    {
        MUTEX_LOCK(ltable->mutex);
        while(iread(ltable->dns_fd, &dns, sizeof(DNS), ltable->dns_current * sizeof(DNS)) > 0)
        {
            ltable->dns_current++;
            if(dns.ip == 0 && iread(ltable->host_fd, host, dns.length, dns.offset) > 0)
            {
                taskid = ltable->dns_current - 1;
                host[dns.length] = '\0';
                DEBUG_LOGGER(ltable->logger, "Ready for resolving name[%s] %d of %d", 
                        host, taskid, ltable->dns_total);
                break;
            }
        }
        MUTEX_UNLOCK(ltable->mutex);
    }
    return taskid;
}

/* set dns timeout */
int ltable_set_state(LTABLE *ltable, int taskid, int state)
{
    if(ltable)
    {
        return 0;
    }
    return -1;
}

/* set dns */
int ltable_set_dns(LTABLE *ltable, char *host, int ip)
{
    int n = 0, ret = -1;
    void *dp = NULL;
    long id = 0;

    if(ltable)
    {
        MUTEX_LOCK(ltable->mutex);
        n = strlen(host);
        TRIETAB_RGET(ltable->dnstable, host, n, dp);
        id = ((long) dp - 1);
        if(id >= 0 && id < ltable->dns_total && iread(ltable->dns_fd, &n, 
                    sizeof(int), id * sizeof(DNS)) > 0 && n == 0)
        {
            ltable->dns_ok++;
            DEBUG_LOGGER(ltable->logger, "Resolved name[%s]'s ip[%d]\n", host, ip);
            ret = iwrite(ltable->dns_fd, &ip, sizeof(int), id * sizeof(DNS));
        }
        MUTEX_UNLOCK(ltable->mutex);
    }
    return ret;

}

/* resolve */
int ltable_resolve(LTABLE *ltable, char *host)
{
    int n = 0, ip = -1;
    void *dp = NULL;
    long id = 0;

    if(ltable)
    {
        n = strlen(host);
        TRIETAB_RGET(ltable->dnstable, host, n, dp);
        if((id = (long)dp - 1) >= 0 && id < ltable->dns_total)
        {
            iread(ltable->dns_fd, &ip, sizeof(int), id * sizeof(DNS));
            if(ip == 0) ip = -1;
        }
    }
    return ip;
}

/* resume ltable */
int ltable_resume(LTABLE *ltable)
{
    LMETA lmeta = {0};
    DNS *dns = NULL;
    void *dp = NULL, *olddp = NULL;
    char *host = NULL, *p = NULL;
    int i = 0, flag = 0, size = 0;

    if(ltable)
    {
        /* resume dns */
        if((size = ifsize(ltable->dns_fd)) > 0 && (dns = calloc(1, size)))
        {
            ltable->dns_total = (size/sizeof(DNS));
            if(read(ltable->dns_fd, dns, size) > 0)
            {
                if((size = ifsize(ltable->host_fd)) > 0 && (p = calloc(1, size)))
                {
                    if(read(ltable->host_fd, p, size) > 0)
                    {
                        for(i = 0; i < ltable->dns_total; i++)
                        {
                            if(dns[i].offset < size)
                            {
                                host = p + dns[i].offset;
                                dp = (void *)((long)(i+1));
                                TRIETAB_RADD(ltable->dnstable, host, dns[i].length, dp);
                            }
                            if(dns[i].ip == 0)
                            {
                                if(flag == 0)
                                {
                                    ltable->dns_current = i;   
                                    flag = 1;
                                }
                            }
                            else
                            {
                                ltable->dns_ok++;
                            }
                        }

                    }
                    free(p);
                    p = NULL;
                }
            }
            free(dns);
            dns = NULL;
        }
        flag = 0;
        /* resume meta to urltable */
        lseek(ltable->meta_fd, 0, SEEK_SET);
        //fprintf(stdout, "%d::%ld:%ld\n", __LINE__, ltable->url_current, ltable->url_total);
        while(read(ltable->meta_fd, &lmeta, sizeof(LMETA)) > 0)
        {
            dp = (void *)++(ltable->url_total);
            KVMAP_ADD(ltable->urltable, lmeta.id, dp, olddp);
            //check status
            if(lmeta.state == TASK_STATE_INIT && flag == 0)
            {
                flag = 1;
                ltable->url_current  = ltable->url_total - 1;
            }
            else if(lmeta.state == TASK_STATE_OK) ltable->url_ok++;
            else if(lmeta.state == TASK_STATE_ERROR) ltable->url_error++;
            ltable->doc_total_size += lmeta.ndata;
            ltable->doc_total_zsize += lmeta.nzdata;
        }
        //fprintf(stdout, "%d::%ld:%ld\n", __LINE__, ltable->url_current, ltable->url_total);
        return 0;
    }
    return -1;
}


/* get task (return task id) */
int  ltable_get_task(LTABLE *ltable, char *block, int *nblock)
{
    char url[HTTP_URL_MAX], *p = NULL, *ps = NULL, *path = NULL, ch = 0;
    unsigned char *sip = NULL;
    int taskid = -1, ip = -1, port = 0;
    off_t offset = 0;
    LMETA lmeta = {0};

    if(ltable && ltable->running_state)
    {
        MUTEX_LOCK(ltable->mutex);
        if(ltable->url_total > 0 && ltable->url_current < ltable->url_total)
        {
            do
            {
                offset = (ltable->url_current) * sizeof(LMETA);
                if(iread(ltable->meta_fd, &lmeta, sizeof(LMETA), offset) > 0)
                {
                    ltable->url_current++;
                    if(lmeta.state == TASK_STATE_INIT)
                    {
                        //fprintf(stdout, "%s:%d OK\n", __FILE__, __LINE__);
                        if(iread(ltable->url_fd, url, lmeta.nurl, lmeta.offurl) > 0)
                        {
                            //fprintf(stdout, "%s:%d OK\n", __FILE__, __LINE__);
                            url[lmeta.nurl] = '\0';
                            DEBUG_LOGGER(ltable->logger, "TASK-URL:%s", url);
                            p = ps = url + strlen("http://");
                            while(*p != '\0' && *p != ':' && *p != '/')++p;
                            ch = *p;
                            *p = '\0';
                            ip = ltable->resolve(ltable, ps);
                            port = 80;
                            if(ch == ':') 
                            {
                                port = atoi((p+1));
                                while(*p != '\0' && *p != '/')++p;
                                path = (p+1);
                            }
                            else if(ch == '/') path = (p+1);
                            sip = (unsigned char *)&ip;
                            taskid = ltable->url_current - 1;
                            *nblock = sprintf(block, "%s\r\nFrom: %d\r\nLocation: /%s\r\n"
                                    "Host: %s\r\nServer: %d.%d.%d.%d\r\nReferer:%d\r\n\r\n", 
                                    HTTP_RESP_OK, taskid, path, ps, 
                                    sip[0], sip[1], sip[2], sip[3], port);
                            DEBUG_LOGGER(ltable->logger, "New task[%d][%d.%d.%d.%d:%d] "
                                    "http://%s/%s", taskid, sip[0], sip[1], sip[2], sip[3], 
                                    port, ps, path);
                            goto end;
                        }
                        else 
                        {
                            ERROR_LOGGER(ltable->logger, "Reading url from  %lld failed, %s", 
                                    lmeta.offurl, strerror(errno));
                            goto end;
                        }
                    }
                }
                else 
                {
                    ERROR_LOGGER(ltable->logger, "Reading meta from  %lld failed, %s", 
                            offset, strerror(errno));
                    goto end;
                }
            }while(taskid == -1);
        }
end:
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
    int ret = -1, n = 0, day = 0, hour = 0, min = 0, sec = 0, usec = 0;
    double speed = 0.0;

    if(ltable)
    {
        TIMER_SAMPLE(ltable->timer);
        day  = (PT_SEC_U(ltable->timer) / 86400);
        hour = ((PT_SEC_U(ltable->timer) % 86400) /3600);
        min  = ((PT_SEC_U(ltable->timer) % 3600) / 60);
        sec  = (PT_SEC_U(ltable->timer) % 60);
        usec = (PT_USEC_U(ltable->timer) % 1000000ll);
        speed = (PT_SEC_U(ltable->timer) > 0) 
            ? ((ltable->doc_current_size / 1024)/PT_SEC_U(ltable->timer)) : 0;
        n = sprintf(buf, __html__body__, ltable->url_total, ltable->url_current, 
                ltable->url_ok, ltable->url_error, ltable->doc_total_zsize, ltable->doc_total_size,
                ltable->doc_current_zsize, ltable->doc_current_size, ltable->dns_ok, 
                ltable->dns_total, day, hour, min, sec, usec, speed);   
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
        if(state == TASK_STATE_ERROR) ltable->url_error++;
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
            //fprintf(stdout, "%s::%d OK \n", __FILE__,  __LINE__);
            if(ltable->url_fd > 0 && iread(ltable->url_fd, url, lmeta.nurl, lmeta.offurl) > 0)
            {
                url[lmeta.nurl] = '\0';
                p = url + strlen("http://");
                ps = host;
                while(*p != '\0' && *p != '/') *ps++ = *p++;
                *ps = '\0'; 
                path = p;
                ndata = ncontent * 40;
                //fprintf(stdout, "%s::%d OK %d:%d\n", __FILE__,  __LINE__, ncontent, ndata);
                if((data = calloc(1, ndata)))
                {
                    if(zdecompress((Bytef *)content, (uLong)ncontent, 
                            (Bytef *)data, (uLong *)&ndata) != 0) 
                    {
                        ERROR_LOGGER(ltable->logger, "decompress task[%d] nzdata[%d] failed, %s", 
                                taskid, ncontent, strerror(errno));
                        goto over;
                    }
                    //fprintf(stdout, "%s::%d OK \n", __FILE__,  __LINE__);
                    ltable->parselink(ltable, host, path, data, (data + ndata));
                    //fprintf(stdout, "%s::%d OK \n", __FILE__,  __LINE__);
                    lheader = (LHEADER *)data;
                    p = data + sizeof(LHEADER);
                    lheader->date = time(NULL);
                    lheader->nurl = lmeta.nurl;
                    lheader->nzdata = ncontent;
                    lheader->ndata = ndata;
                    strcpy(p, url);
                    p += lmeta.nurl + 1;
                    memcpy(p, content, ncontent);
                    p += ncontent;
                    n = p - data;
                    //fprintf(stdout, "%s::%d OK \n", __FILE__,  __LINE__);
                    if(iappend(ltable->doc_fd, data, n, &offset) > 0)
                    {
                        //fprintf(stdout, "%s::%d OK offset:%lld\n", __FILE__,  __LINE__, offset);
                        lmeta.offset    = offset;
                        lmeta.length    = n;
                        lmeta.nzdata    = ncontent;
                        lmeta.ndata     = ndata;
                        lmeta.date      = date;
                        lmeta.state     = TASK_STATE_OK;
                        ltable->doc_total_size += ndata;
                        ltable->doc_total_zsize += ncontent;
                        ltable->doc_current_size += ndata;
                        ltable->doc_current_zsize += ncontent;
                        iwrite(ltable->meta_fd, &lmeta, sizeof(LMETA), taskid * sizeof(LMETA));
                        ret = 0;
                        ltable->url_ok++;
                    }
                    //fprintf(stdout, "%s::%d OK \n", __FILE__,  __LINE__);
                    over:
                        free(data);
                        data = NULL;
                }
                //fprintf(stdout, "%s::%d OK \n", __FILE__,  __LINE__);
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
        KVMAP_CLEAN((*pltable)->urltable);
        TRIETAB_CLEAN((*pltable)->dnstable);
        if((*pltable)->mutex)
        {
            MUTEX_DESTROY((*pltable)->mutex);
        }
        if((*pltable)->timer)
        {
            TIMER_CLEAN((*pltable)->timer);
        }
        if((*pltable)->isinsidelogger)
        {
            LOGGER_CLEAN((*pltable)->logger);
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
        ltable->running_state = 1;
        MUTEX_INIT(ltable->mutex);
        TIMER_INIT(ltable->timer);
        ltable->urltable        = KVMAP_INIT();
        ltable->dnstable        = TRIETAB_INIT();
        ltable->set_basedir     = ltable_set_basedir;
        ltable->set_logger      = ltable_set_logger;
        ltable->resume          = ltable_resume;
        ltable->parselink       = ltable_parselink;
        ltable->addlink         = ltable_addlink;
        ltable->addurl          = ltable_addurl;
        ltable->get_task        = ltable_get_task;
        ltable->set_task_state  = ltable_set_task_state;
        ltable->get_stateinfo   = ltable_get_stateinfo;
        ltable->add_document    = ltable_add_document;
        ltable->add_host        = ltable_add_host;
        ltable->new_dnstask     = ltable_new_dnstask;
        ltable->set_dns         = ltable_set_dns;
        ltable->resolve         = ltable_resolve;
        ltable->clean           = ltable_clean;
    }
    return ltable;
}

