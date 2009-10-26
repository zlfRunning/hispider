#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <zlib.h>
#include "trie.h"
#include "timer.h"
#include "kvmap.h"
#include "ltask.h"
#include "md5.h"
#include "queue.h"
#include "fqueue.h"
#include "logger.h"
#include "tm.h"
#include "hio.h"
#ifndef LI
#define LI(_x_) ((long int)_x_)
#endif
#define HTTP_PREF  "http://"
#define ISALPHA(p) ((*p >= '0' && *p <= '9') ||(*p >= 'A' && *p <= 'Z')||(*p >= 'a' && *p <= 'z'))
#define UPDATE_SPEED(task, interval)                                                        \
do                                                                                          \
{                                                                                           \
    if(task && task->state)                                                                 \
    {                                                                                       \
        if((interval = (int)((PT_L_USEC(task->timer) - task->state->last_usec))) > 0)       \
        {                                                                                   \
            task->state->speed = (double)1000000 * (((double)(task->state->doc_total_size   \
                            - task->state->last_doc_size)/(double)1024)/(double)interval);  \
        }                                                                                   \
        if(interval  > L_SPEED_INTERVAL)                                                    \
        {                                                                                   \
            task->state->last_usec = PT_L_USEC(task->timer);                                \
            task->state->last_doc_size = task->state->doc_total_size;                       \
        }                                                                                   \
    }                                                                                       \
}while(0)
static const char *running_ops[] = {"running", "stop"};
/* mkdir */
int ltask_mkdir(char *path, int mode)
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
/* set basedir*/
int ltask_set_basedir(LTASK *task, char *dir)
{
    char path[HTTP_PATH_MAX], host[HTTP_HOST_MAX], *p = NULL, *pp = NULL, *end = NULL;
    void *dp = NULL, *olddp = NULL;
    int n = 0, i = 0, *px = NULL;
    unsigned char *ip = NULL;
    LHOST *host_node = NULL;
    LPROXY *proxy = NULL;
    struct stat st = {0};
    LUSER *user = NULL;
    LDNS *dns = NULL;

    if(task && dir)
    {
        /* state */
        sprintf(path, "%s/%s", dir, L_LOG_NAME);
        if(ltask_mkdir(path, 0755) != 0)
        {
            _EXIT_("mkdir -p %s failed, %s\n", path, strerror(errno));
        }
        LOGGER_INIT(task->logger, path);
        sprintf(path, "%s/%s", dir, L_ERR_NAME);
        LOGGER_INIT(task->errlogger, path);
        sprintf(path, "%s/%s", dir, L_STATE_NAME);
        if((task->state_fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            if(fstat(task->state_fd, &st) == 0)
            {
                if(st.st_size == 0) ftruncate(task->state_fd, sizeof(LSTATE));
                if((task->state = (LSTATE *)mmap(NULL, sizeof(LSTATE),PROT_READ|PROT_WRITE, 
                                MAP_SHARED, task->state_fd, 0)) == (void *)-1)
                {
                    _EXIT_("mmap %s failed, %s\n", path, strerror(errno));
                }
                task->state->last_usec = PT_L_USEC(task->timer);
                task->state->speed = 0;
                task->state->running = 1;
            }
            else
            {
                _EXIT_("state %s failed, %s\n", path, strerror(errno));
            }
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        /* proxy/task */
        sprintf(path, "%s/%s", dir, L_PROXY_NAME);
        p = path;
        HIO_INIT(task->proxyio, p, st, LPROXY, 1, PROXY_INCRE_NUM);
        if(task->proxyio.fd > 0 && (proxy = HIO_MAP(task->proxyio, LPROXY)))
        {
            task->proxyio.left = 0;
            i = 0;
            do
            {
                if(proxy->status == PROXY_STATUS_OK)
                {
                    ip = (unsigned char *)&(proxy->ip);
                    n = sprintf(host, "%d.%d.%d.%d:%d", ip[0], ip[1], 
                            ip[2], ip[3], proxy->port);
                    dp = (void *)((long)(i + 1));
                    p = host;
                    TRIETAB_ADD(task->table, p, n, dp);
                    px = &i;
                    QUEUE_PUSH(task->qproxy, int, px);
                }
                else
                {
                    task->proxyio.left++;
                }
                ++proxy;
            }while(++i < task->proxyio.total);
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_TASK_NAME);
        p = path;
        FQUEUE_INIT(task->qtask, p, LNODE);
        /* host/key/ip/url/domain/document */
        sprintf(path, "%s/%s", dir, L_KEY_NAME);
        if((task->key_fd = open(path, O_CREAT|O_RDWR|O_APPEND, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_DOMAIN_NAME);
        if((task->domain_fd = open(path, O_CREAT|O_RDWR|O_APPEND, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_URL_NAME);
        if((task->url_fd = open(path, O_CREAT|O_RDWR|O_APPEND, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_META_NAME);
        if((task->meta_fd = open(path, O_CREAT|O_RDWR, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_DOC_NAME);
        if((task->doc_fd = open(path, O_CREAT|O_RDWR|O_APPEND, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_HOST_NAME);
        p = path;
        HIO_INIT(task->hostio, p, st, LHOST, 1, HOST_INCRE_NUM);
        if(task->hostio.fd  < 0 || HIO_MAP(task->hostio, LHOST) == NULL)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_IP_NAME);
        p = path;
        HIO_INIT(task->ipio, p, st, int, 1, IP_INCRE_NUM);
        if(task->ipio.fd  < 0 || HIO_MAP(task->ipio, int) == NULL)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        /* host/table */
        //cookie
        sprintf(path, "%s/%s", dir, L_COOKIE_NAME);
        if((task->cookie_fd = open(path, O_CREAT|O_RDWR, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        if(task->hostio.total > 0 && (host_node = HIO_MAP(task->hostio, LHOST))
                && fstat(task->domain_fd, &st) == 0 && st.st_size > 0)
        {
            if((pp = (char *)mmap(NULL, st.st_size, PROT_READ, 
                            MAP_PRIVATE, task->domain_fd, 0)) != (void *)-1)
            {
                i = 0;
                do
                {
                    if(host_node->host_off >= 0 && host_node->host_len > 0)
                    {
                        p = pp + host_node->host_off;
                        dp = (void *)((long)(i+1));
                        TRIETAB_RADD(task->table, p, host_node->host_len, dp);
                    }
                    else 
                    {
                        task->hostio.end = (off_t)((char *)host_node - (char *)task->hostio.map);
                        break;
                    }
                    n = host_node->ip_off + host_node->ip_count * sizeof(int);
                    if(n > task->ipio.end) task->ipio.end = n;
                    ++host_node;
                }while(++i < task->hostio.total);
                munmap(pp, st.st_size);
            }
            else
            {
                _EXIT_("mmap domain(%d) failed, %s\n", task->domain_fd, strerror(errno));
            }
        }
        /* dns */
        sprintf(path, "%s/%s", dir, L_DNS_NAME);
        p = path;
        HIO_INIT(task->dnsio, p, st, LDNS, 1, DNS_INCRE_NUM);
        if(task->dnsio.fd < 0 || HIO_MAP(task->dnsio, LDNS) == NULL)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        if(task->dnsio.total > 0 && (dns = HIO_MAP(task->dnsio, LDNS)))
        {
            task->dnsio.left = 0;
            i = 0;
            do
            {
                if(dns->status != DNS_STATUS_ERR && (n = strlen((p = dns->name))) > 0)
                {
                    task->dnsio.current++;
                    dp = (void *)((long)(i+1));
                    TRIETAB_ADD(task->table, p, n, dp);
                    dns->status = DNS_STATUS_READY;
                }
                else task->dnsio.left++;
                ++dns;
            }while(++i < task->dnsio.total);
        }
        /* user */
        sprintf(path, "%s/%s", dir, L_USER_NAME);
        p = path;
        HIO_INIT(task->userio, p, st, LUSER, 1, USER_INCRE_NUM);
        if(task->userio.fd < 0 || HIO_MAP(task->userio, LUSER) == NULL)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        if(task->userio.total > 0 && (user = HIO_MAP(task->userio, LUSER)))
        {
            i = 0;
            do
            {
                if(user->status != USER_STATUS_ERR && (n = strlen((p = user->name))) > 0)
                {
                    task->userio.current++;
                    dp = (void *)((long)(i+1));
                    TRIETAB_ADD(task->users, p, n, dp);
                }
                else task->userio.left++;
                ++user;
            }while(++i < task->userio.total);
        }
        /* urlmap */
         if(fstat(task->key_fd, &st) == 0 && st.st_size > 0)
         {
             if((pp = (char *)mmap(NULL, st.st_size, PROT_READ, 
                             MAP_PRIVATE, task->key_fd, 0)) != (void *)-1)
             {
                 p = pp;
                 end = p + st.st_size;
                 i = 0;
                 do
                 {
                     dp = (void *)((long)(++i));
                     KVMAP_ADD(task->urlmap, p, dp, olddp);
                     p += MD5_LEN;
                 }while(p < end);
                 munmap(pp, st.st_size);
             }
             else
             {
                 _EXIT_("mmap domain(%d) failed, %s\n", task->domain_fd, strerror(errno));
             }
         }
        /* document */
        return 0;
    }
    return -1;
}

/* set state running */
int ltask_set_state_running(LTASK *task, int state)
{
    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(task->state) task->state->running = (short)state;
        MUTEX_UNLOCK(task->mutex);
    }
    return 0;
}

/* set state speed_limit */
int ltask_set_speed_limit(LTASK *task, double speed)
{
    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(task->state) task->state->speed_limit = speed;
        MUTEX_UNLOCK(task->mutex);
    }
    return 0;
}


/* set state proxy */
int ltask_set_state_proxy(LTASK *task, int state)
{
    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(task->state) task->state->is_use_proxy = (short)state;
        MUTEX_UNLOCK(task->mutex);
    }
    return 0;
}

/* add proxy */
int ltask_add_proxy(LTASK *task, char *host)
{
    char *p = NULL, *e = NULL, *pp = NULL, *ps = NULL, ip[HTTP_HOST_MAX];
    int n = 0, i = 0, ret = -1, *px = NULL;
    LPROXY *proxy = NULL;
    void *dp = NULL;

    if(task && host)
    {
        MUTEX_LOCK(task->mutex);
        p = host;
        ps = ip;
        while(*p != '\0')
        {
            if(*p == ':') {e = ps; pp = ps+1;}
            *ps++ = *p++;
        }
        *ps = '\0';
        n = p - host;
        TRIETAB_GET(task->table, host, n, dp);
        if(e && dp == NULL)
        {
            //fprintf(stdout, "%d:%s:%s\n", __LINE__, ip, pp);
            if(task->proxyio.left == 0){HIO_MMAP(task->proxyio, LPROXY, PROXY_INCRE_NUM);}
            if(task->proxyio.left > 0 && (proxy = HIO_MAP(task->proxyio, LPROXY)))
            {
                //fprintf(stdout, "%d:%s:%s\n", __LINE__, ip, pp);
                i = 0;
                do
                {
                    if(proxy->status != (short)PROXY_STATUS_OK)
                    {
                        dp = (void *)((long)(i+1));
                        TRIETAB_ADD(task->table, host, n, dp);
                        px = &i;
                        QUEUE_PUSH(task->qproxy, int, px);
                        proxy->status = (short)PROXY_STATUS_OK;
                        *e = '\0';
                        //fprintf(stdout, "%d:%s:%s:%d\n", __LINE__, ip, pp, task->proxyio.left);
                        proxy->ip = (int)inet_addr(ip);
                        proxy->port = (unsigned short)atoi(pp);
                        /*
                        unsigned char *s = (unsigned char *)&(proxy->ip);
                           fprintf(stdout, "%d::%s %d:%d.%d.%d.%d:%d\n", 
                           __LINE__, host, proxy->ip, 
                           s[0], s[1], s[2], s[3], proxy->port);
                           */
                        task->proxyio.left--;
                        ret = i;
                        //fprintf(stdout, "%d:%s:%s:%d:%d\n", __LINE__, ip, pp, task->proxyio.left, i);
                        break;
                    }
                    ++proxy;
                }while(i++ < task->proxyio.total);
            }
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* get random proxy */
int ltask_get_proxy(LTASK *task, LPROXY *proxy)
{
    int id = 0, ret = -1;
    LPROXY *node = NULL;

    if(task && proxy)
    {
        MUTEX_LOCK(task->mutex);
        if(QTOTAL(task->qproxy) > 0)
        {
            do
            {
                if(QUEUE_POP(task->qproxy, int, &id) == 0 && id >= 0 && id < task->proxyio.total
                    && (node = (LPROXY *)(task->proxyio.map)) && node != (LPROXY *)-1)
                {
                    if(node[id].status == PROXY_STATUS_OK)
                    {
                        memcpy(proxy, &(node[id]), sizeof(LPROXY));
                        break;
                    }
                    else 
                    {
                        node = NULL;
                    }
                }else break;
            }while(node == NULL);
            if(node) ret = 0;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* set proxy status */
int ltask_set_proxy_status(LTASK *task, int hostid, char *host, short status)
{
    int ret = -1, n = 0, id = -1, count = 0;
    LPROXY *proxy = NULL;
    void *dp = NULL;

    if(task && (hostid >= 0  || host) && (count = task->proxyio.total - task->proxyio.left) > 0)
    {
        MUTEX_LOCK(task->mutex);
        if(host)
        {
            n = strlen(host);
            TRIETAB_GET(task->table, host, n, dp);
            if(dp) id = (long)dp - 1;
        }
        else id = hostid;
        if(id >= 0 && id < task->proxyio.total 
                && (proxy = (LPROXY *)(task->proxyio.map)) && proxy != (LPROXY *)-1)
        {
            proxy[id].status = status;
            ret = id;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* delete proxy */
int ltask_del_proxy(LTASK *task, int hostid, char *host)
{
    int ret = -1, n = 0, id = -1, count = 0;
    LPROXY *proxy = NULL;
    char hostip[HTTP_URL_MAX], *p = NULL;
    unsigned char *sip = NULL;
    void *dp = NULL;

    if(task && (hostid >= 0  || host) && (count = task->proxyio.total - task->proxyio.left) > 0)
    {
        MUTEX_LOCK(task->mutex);
        if(host)
        {
            n = strlen(host);
            TRIETAB_GET(task->table, host, n, dp);
            if(dp) id = (long)dp - 1;
        }
        else id = hostid;
        //fprintf(stdout, "id:%d\n", id);
        if(id >= 0 && id < task->proxyio.total 
                && (proxy = (LPROXY *)(task->proxyio.map)) && proxy != (LPROXY *)-1)
        {
            sip = (unsigned char *)&(proxy[id].ip);
            n = sprintf(hostip, "%d.%d.%d.%d:%d", sip[0], sip[1], sip[2], sip[3], proxy[id].port);
            p = hostip;
            TRIETAB_DEL(task->table, p, n, dp);
            memset(&(proxy[id]), 0, sizeof(LPROXY));
            task->proxyio.left++;
            ret = id;
            //fprintf(stdout, "ret:%d\n", ret);
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* view proxy host */
int ltask_view_proxylist(LTASK *task, char *block)
{
    char buf[HTTP_BUF_SIZE], *p = NULL, *pp = NULL;
    int n = -1, i = 0, count = 0;
    unsigned char *sip = NULL;
    LPROXY *proxy = NULL;

    if(task && block && task->proxyio.total  > 0 
            && (count = task->proxyio.total - task->proxyio.left) > 0)
    {
        MUTEX_LOCK(task->mutex);
        if((proxy = (LPROXY *)(task->proxyio.map)) && proxy != (LPROXY *)-1)
        {
            p = buf;
            p += sprintf(p, "%s", "({");
            pp = p;
            for(i = 0; i < task->proxyio.total; i++)
            {
                if(proxy[i].status)
                {
                    sip = (unsigned char *)&(proxy[i].ip);
                    p += sprintf(p, "'%d':{'id':'%d', 'host':'%d.%d.%d.%d:%d', 'status':'%d'},", 
                            i, i, sip[0], sip[1], sip[2], sip[3], 
                            proxy[i].port, proxy[i].status);
                }
            }
            if(pp != p) --p;
            p += sprintf(p, "%s", "})");
            n = sprintf(block, "HTTP/1.0 200 OK\r\nContent-Type:text/html\r\n"
                    "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return n;
}

/* pop host for DNS resolving */
int ltask_pop_host(LTASK *task, char *host)
{
    LHOST *host_node = NULL;
    int host_id = -1;

    if(task && host && task->hostio.total > 0 && task->hostio.current >= 0
            && task->hostio.current < task->hostio.total
            && task->hostio.map && task->hostio.map != (void *)-1)
    {
        MUTEX_LOCK(task->mutex);
        host_node = (LHOST *)(task->hostio.map + task->hostio.current * sizeof(LHOST));
        do
        {
            if(host_node->host_len == 0) break;
            if(host_node->host_len > 0 && host_node->ip_count == 0)
            {
                if(pread(task->domain_fd, host, host_node->host_len, host_node->host_off) > 0)
                {
                    host_id = task->hostio.current++;
                    host[host_node->host_len] = '\0';
                }
                break;
            }
            ++host_node;
            task->hostio.current++;
        }while(task->hostio.current < task->hostio.total);
        MUTEX_UNLOCK(task->mutex);
    }
    return host_id;
}

/* set host ips */
int ltask_set_host_ip(LTASK *task, char *host, int *ips, int nips)
{
    LHOST *host_node = NULL;
    int i = 0, n = 0, ret = -1;
    void *dp = NULL;

    if(task && host && (n = strlen(host)) > 0)
    {
        MUTEX_LOCK(task->mutex);
        TRIETAB_RGET(task->table, host, n, dp);
        if((i = ((long)dp - 1)) >= 0 && i < task->hostio.total 
                && task->hostio.map && task->hostio.map != (void *)-1)
        {
            if((task->ipio.end + nips * sizeof(int)) >= task->ipio.size)
            {
                HIO_MMAP(task->ipio, int, IP_INCRE_NUM);
            }
            if(task->ipio.map && task->ipio.map != (void *)-1)
            {
                memcpy(task->ipio.map + task->ipio.end, ips, nips * sizeof(int));
                host_node = (LHOST *)(task->hostio.map + i * sizeof(LHOST));
                host_node->ip_off = task->ipio.end;
                host_node->ip_count = (short)nips;
                task->ipio.end += (off_t)(nips * sizeof(int));
                ret = 0;
            }
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* get host ip */
int ltask_get_host_ip(LTASK *task, char *host)
{
    LHOST *host_node = NULL;
    int i = 0, *ips = NULL, n = 0, ip = -1;
    void *dp = NULL;

    if(task && host && (n = strlen(host)) > 0)
    {
        MUTEX_LOCK(task->mutex);
        TRIETAB_RGET(task->table, host, n, dp);
        if((i = ((long)dp - 1)) >= 0 && i < task->hostio.total 
                && task->hostio.map && task->hostio.map != (void *)-1
                && (host_node = (LHOST *)(task->hostio.map + i * sizeof(LHOST)))
                && host_node->ip_count > 0 && task->ipio.size > host_node->ip_off
                && task->ipio.map && task->ipio.map != (void *)-1
                && (ips = (int *)(task->ipio.map + host_node->ip_off)))
        {
            i = 0;
            if(host_node->ip_count > 1)
                i = random()%(host_node->ip_count);
            ip = ips[i];
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ip;
}

/* list host for DNS resolving */
void ltask_list_host_ip(LTASK *task, FILE *fp)
{
    char host[HTTP_HOST_MAX];
    LHOST *host_node = NULL;
    int *ips = NULL, i = 0, x = 0;
    unsigned char *pp = NULL;

    if(task && task->hostio.total > 0)
    {
        MUTEX_LOCK(task->mutex);
        i = 0;
        host_node = (LHOST *)(task->hostio.map);
        do
        {
            if(host_node->host_len == 0) break;
            if(host_node->host_len > 0 && host_node->ip_count > 0 
                    && host_node->host_len < HTTP_HOST_MAX 
                    && host_node->ip_off < task->ipio.size
                    && task->ipio.map && task->ipio.map != (void *)-1)
            {
                if(pread(task->domain_fd, host, host_node->host_len, host_node->host_off) > 0)
                {
                    host[host_node->host_len] = '\0';
                    ips = (int *)(task->ipio.map + host_node->ip_off);
                    fprintf(fp, "[%d][%s]", i, host);
                    x = host_node->ip_count;
                    while(--x >= 0)
                    {
                        pp = (unsigned char *)&(ips[x]);
                        fprintf(fp, "[%d.%d.%d.%d]", pp[0], pp[1], pp[2], pp[3]);
                    }
                    fprintf(fp, "\n");
                }
            }
            ++host_node;
        }while(i++ < task->hostio.total);
        MUTEX_UNLOCK(task->mutex);
    }
    return ;
}

/* set host status */
int ltask_set_host_status(LTASK *task, int hostid, char *host, short status)
{
    LHOST *host_node = NULL;
    int id = 0, n = 0, ret = -1;
    void *dp = NULL;

    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(host && (n = strlen(host)) > 0)
        {
            TRIETAB_RGET(task->table, host, n, dp);
            id = ((long)dp - 1);
        }else id = hostid;
        if(id >= 0 && id < task->hostio.total 
                && task->hostio.map && task->hostio.map != (void *)-1)
        {
            host_node = (LHOST *)(task->hostio.map + id * sizeof(LHOST));
            host_node->status = (short)status;
            ret = 0;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* set host priority */
int ltask_set_host_level(LTASK *task, int hostid, char *host, short level)
{
    int id = 0, n = 0, ret = -1;
    LHOST *host_node = NULL;
    LNODE node = {0}, *tnode = NULL;
    void *dp = NULL;

    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(host && (n = strlen(host)) > 0)
        {
            TRIETAB_RGET(task->table, host, n, dp);
            id = ((long)dp - 1);
        }else id = hostid;
        if(id >= 0 && id < task->hostio.total 
                && task->hostio.map && task->hostio.map != (void *)-1)
        {
            host_node = (LHOST *)(task->hostio.map + id * sizeof(LHOST));
            if(host_node->level != level && level == L_LEVEL_UP)
            {
                node.type = Q_TYPE_HOST;
                node.id = id;
                tnode = &node;
                FQUEUE_PUSH(task->qtask, LNODE, tnode);
            }
            host_node->level = level;
            ret = 0;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* add url */
int ltask_add_url(LTASK *task, int parentid, int parent_depth, char *url, int flag)
{
    char newurl[HTTP_BUF_SIZE], URL[HTTP_BUF_SIZE], *p = NULL, 
         *pp = NULL, *e = NULL, *host = NULL, *purl = NULL;
    void *dp = NULL, *olddp = NULL;
    int ret = -1, n = 0, nurl = 0, id = 0, host_id = 0;
    LHOST *host_node = NULL;
    unsigned char key[MD5_LEN];
    struct stat st = {0};
    LMETA meta = {0};

    if(task && url)
    {
        MUTEX_LOCK(task->mutex);
        purl = p = url;
        pp = newurl;
        memset(newurl, 0, HTTP_URL_MAX);
        memset(URL, 0, HTTP_URL_MAX);
        while(*p != '\0')
        {
            if(host == NULL && *p == ':' && *(p+1) == '/' && *(p+2) == '/')
            {
                *pp++ = ':';
                *pp++ = '/';
                *pp++ = '/';
                p += 3;
                host = pp;
                continue;
            }
            if(host && e == NULL && *p != '.' && *p != '-' && !ISALPHA(p)) 
            {
                if(*p != '/' && *p != ':') *pp++ = '/';
                e = pp;
            }
            if(*p >= 'A' && *p <= 'Z')
            {
                *pp++ = *p++ + ('a' - 'A');
            }
            else if(*((unsigned char *)p) > 127 || *p == 0x20)
            {
                pp += sprintf(pp, "%%%02x", *((unsigned char *)p));
                ++p;
            }
            else
            {
                *pp++ = *p++;
            }
        }
        if(host == NULL) goto err;
        nurl = (p - url);
        if(e == NULL)
        {
            e = pp; 
            *e = '/'; 
            sprintf(URL, "%s/", url);
            purl = URL;
            nurl++;
            ++pp;
        }
        /* check/add host */
        DEBUG_LOGGER(task->logger, "Ready for adding url:%s", purl);
        DEBUG_LOGGER(task->logger, "Ready for adding e:%s", e);
        DEBUG_LOGGER(task->logger, "Ready for adding url:%s host:%s e:%s", purl, host, e);
        if((n = (e - host)) <= 0) goto err;
        TRIETAB_RGET(task->table, host, n, dp);
        if(dp == NULL)
        {
            if(task->hostio.end >= task->hostio.size)
            {HIO_MMAP(task->hostio, LHOST, HOST_INCRE_NUM);}
            host_id = task->hostio.end/(off_t)sizeof(LHOST);
            host_node = (LHOST *)(task->hostio.map + task->hostio.end);
	        DEBUG_LOGGER(task->logger, "%d:url[%d:%s] URL[%d:%s] path[%s]", 
                    host_id, n, newurl, nurl, purl, e);
            if(fstat(task->domain_fd, &st) != 0) goto err;
            host_node->host_off = st.st_size;
            host_node->host_len = n;
            *e = '\n';
            if(pwrite(task->domain_fd, host, n+1, st.st_size) <= 0) goto err;
            *e = '/';
            dp = (void *)((long)(host_id + 1));
            TRIETAB_RADD(task->table, host, n, dp);
            task->hostio.end += (off_t)sizeof(LHOST);
            if(task->state) task->state->host_total++;
        }
        else
        {
            host_id = (long)dp - 1;
            host_node = (LHOST *)(task->hostio.map + (host_id * sizeof(LHOST)));
	        DEBUG_LOGGER(task->logger, "%d:url[%d:%s] URL[%d:%s] path[%s] left:%d total:%d", 
                    host_id, n, newurl, nurl, purl, e, host_node->url_left, host_node->url_total);
        }
        /* check/add url */
        if((n = (pp - newurl)) <= 0 && n > HTTP_URL_MAX) goto err;
        memset(key, 0, MD5_LEN);
        md5((unsigned char *)newurl, n, key);
        if(fstat(task->meta_fd, &st) != 0) goto err;
        id = (st.st_size/(off_t)sizeof(LMETA));
        dp = (void *)((long)id + 1);
        KVMAP_ADD(task->urlmap, key, dp, olddp); 
        if(olddp == NULL)
        {
            //fprintf(stdout, "%d::url:%s id:%d md5:", __LINE__, newurl, id);
            //MD5OUT(key, stdout);
            //fprintf(stdout, "\n");
            if(fstat(task->url_fd, &st) != 0) goto err;
            if((n = sprintf(newurl, "%s\n", purl)) > 0 
                    &&  write(task->url_fd, newurl, n) > 0 
                    && write(task->key_fd, key, MD5_LEN) > 0)
            {
                meta.depth   = parent_depth + 1;
                if(flag >= 0) meta.flag = flag;
                meta.parent  = parentid;
                meta.url_off = st.st_size;
                meta.url_len = n - 1;
                meta.host_id = host_id;
                meta.prev    = host_node->url_last_id;
                meta.next    = -1;
                if(host_node->url_total == 0) 
                {
                    meta.prev = -1;
                    host_node->url_current_id = host_node->url_first_id = id;
                    //add to queue
                }
                else
                {
                    pwrite(task->meta_fd, &id, sizeof(int), (off_t)(host_node->url_last_id) 
                            * (off_t)sizeof(LMETA) + (off_t)((char *)&(meta.next) - (char *)&meta));
                }
                host_node->url_left++;
                host_node->url_last_id = id;
                host_node->url_total++;
                pwrite(task->meta_fd, &meta, sizeof(LMETA), (off_t)id * (off_t)sizeof(LMETA));
                if(task->state) task->state->url_total++;
                ret = id;
            }
        }
        else
        {
            ret = id = (int)((long)olddp - 1);
            //fprintf(stdout, "%d::url:%s id:%d md5:", __LINE__, newurl, id);
            //MD5OUT(key, stdout);
            //fprintf(stdout, "\n");
        }
err: 
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* pop url */
int ltask_pop_url(LTASK *task, int url_id, char *url, int *itime, 
        int referid, char *refer, char *cookie)
{
    int urlid = -1, n = -1, x = 0;
    LHOST *host_node = NULL;
    LNODE node = {0}, *tnode = NULL;
    LMETA meta = {0};

    if(task && url)
    {
        MUTEX_LOCK(task->mutex);
        if(url_id >= 0) urlid = url_id;
        else
        {
            if(FQUEUE_POP(task->qtask, LNODE, &node) == 0)
            {
                if(node.type == Q_TYPE_HOST && node.id >= 0)
                {
                    host_node = (LHOST *)(task->hostio.map + node.id * sizeof(LHOST));
                    urlid = host_node->url_current_id;
                    if(host_node->url_left > 1)
                    {
                        tnode = &node;
                        FQUEUE_PUSH(task->qtask, LNODE, tnode);
                    }
                }
                else if(node.type == Q_TYPE_URL && node.id >= 0)
                {
                    urlid = node.id;
                }
                else 
                {
                    DEBUG_LOGGER(task->logger, "Unknown task type:%d", node.type);
                    goto end;
                }
            }
            else
            {
                x = task->state->host_current;
                DEBUG_LOGGER(task->logger, "QURL host:%d", x);
                do
                {
                    host_node = (LHOST *)(task->hostio.map 
                            + task->state->host_current * sizeof(LHOST));
                    if(task->state->host_current++ == task->state->host_total) 
                        task->state->host_current = 0;
                    if(host_node && host_node->status >= 0 && host_node->url_left > 0)
                    {
                        urlid = host_node->url_current_id;
                        DEBUG_LOGGER(task->logger, "urlid:%d current:%d left:%d total:%d", urlid, 
                        task->state->host_current, host_node->url_left, host_node->url_total);
                        break;
                    }
                    else host_node = NULL;
                    if(x == task->state->host_current) break;
                }while(host_node == NULL);
            }
        }
        /* read url */
        DEBUG_LOGGER(task->logger, "READURL urlid:%d", urlid);
        if(urlid < 0 || pread(task->meta_fd, &meta, sizeof(LMETA), 
                    (off_t)urlid * (off_t)sizeof(LMETA)) < 0 || meta.url_len > HTTP_URL_MAX 
                || (meta.status < 0 && meta.retry_times > TASK_RETRY_TIMES) ) 
        {
            ERROR_LOGGER(task->logger, "ERR-META urlid:%d", urlid);
            urlid = -1;
            goto end;
        }
        if(meta.retry_times < 0)
        {
            meta.retry_times++;
            pwrite(task->meta_fd, &(meta.retry_times), sizeof(short), 
                    (off_t)urlid * (off_t)sizeof(LMETA) 
                    + (off_t)((char *)&(meta.retry_times) - (char *)&meta));        
        }
        if((n = pread(task->url_fd, url, meta.url_len, meta.url_off)) > 0)
        {
            if(itime) *itime = meta.last_modified;
            if(host_node == NULL) 
                host_node = (LHOST *)(task->hostio.map + meta.host_id * sizeof(LHOST));
            url[n] = '\0';
            host_node->url_current_id = meta.next;
            host_node->url_left--;
            //refer
            refer[0] = '\0';
            if(referid >= 0) n = referid;
            else n = meta.parent;
            if(n >= 0 && pread(task->meta_fd, &meta, sizeof(LMETA), (off_t)(n*sizeof(LMETA))) > 0 
                    && meta.url_len <= HTTP_URL_MAX && meta.status >= 0)
            {
                pread(task->url_fd, refer, meta.url_len, meta.url_off);
                refer[meta.url_len] = '\0';
            }
            //cookie
            cookie[0] = '\0';
        }
        else
        {
            DEBUG_LOGGER(task->logger, "Unknown urlid:%d", urlid);
            urlid = -1;
        }
end:
        MUTEX_UNLOCK(task->mutex);
    }
    return urlid;
}

/* get url */
int ltask_get_url(LTASK *task, int urlid, char *url)
{
    int ret = -1, n = 0;
    LMETA meta = {0};

    if(task && urlid >= 0 && url)
    {
        MUTEX_LOCK(task->mutex);
        if(task->meta_fd > 0 && pread(task->meta_fd, &meta, sizeof(LMETA), 
                (off_t)(urlid * sizeof(LMETA))) > 0 
                && meta.url_len < HTTP_URL_MAX && meta.status >= 0 
                && (n = pread(task->url_fd, url, meta.url_len, meta.url_off)) > 0)
        {
            ret = meta.url_len;
            url[n] = '\0';
        }
        MUTEX_UNLOCK(task->mutex);

    }
    return ret;
}

/* set url status */
int ltask_set_url_status(LTASK *task, int urlid, char *url, short status, short err)
{
    char newurl[HTTP_URL_MAX], *p = NULL, *pp = NULL;
    unsigned char key[MD5_LEN];
    int n = 0, id = -1, ret = -1;
    LMETA meta = {0};
    void *dp = NULL;

    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(url)
        {
            p = url;
            pp = newurl;
            while(*p != '\0')
            {
                if(*p >= 'A' && *p <= 'Z')
                    *pp++ = *p++ + 'a' - 'A';
                else *pp++ = *p++;
            }
            *pp = '\0';
            if((n = (pp - newurl)) > 0)
            {
                md5((unsigned char *)newurl, n, key);
                KVMAP_GET(task->urlmap, key, dp);
                if(dp) id = (long )dp - 1;
            }
        }else id = urlid;
        if(id >= 0) 
        {
            pwrite(task->meta_fd, &status, sizeof(short), (off_t)id * (off_t)sizeof(LMETA) 
                    + (off_t)((char *)&(meta.status) - (char *)&meta));        
            ret = 0;
        }
        if(status && task->state)task->state->url_task_error++;
        if(err){REALLOG(task->errlogger, "%d:%d", id, err);}
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* set url level */
int ltask_set_url_level(LTASK *task, int urlid, char *url, short level)
{
    char newurl[HTTP_URL_MAX], *p = NULL, *pp = NULL;
    unsigned char key[MD5_LEN];
    int n = 0, id = -1, ret = -1;
    LNODE node = {0}, *tnode = NULL;
    LMETA meta = {0};
    void *dp = NULL;

    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(url)
        {
            p = url;
            pp = newurl;
            while(*p != '\0')
            {
                if(*p >= 'A' && *p <= 'Z')
                    *pp++ = *p++ + 'a' - 'A';
                else *pp++ = *p++;
            }
            *pp = '\0';
            if((n = (pp - newurl)) > 0)
            {
                md5((unsigned char *)newurl, n, key);
                KVMAP_GET(task->urlmap, key, dp);
                if(dp) id = (long )dp - 1;
            }
        }else id = urlid;
        if(id >= 0) 
        {
            pwrite(task->meta_fd, &level, sizeof(short),  (off_t)id * (off_t)sizeof(LMETA)
                    + (off_t)((char *)&(meta.level) - (char *)&meta)); 
            node.type = Q_TYPE_URL;
            node.id = id;
            tnode = &node;
            FQUEUE_PUSH(task->qtask, LNODE, tnode);
            ret = 0;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* NEW TASK */
int ltask_get_task(LTASK *task, int url_id, int referid, int uuid, 
        int userid, char *buf, int *nbuf)
{
    char url[HTTP_URL_MAX], date[64], refer[HTTP_URL_MAX], cookie[HTTP_COOKIE_MAX], 
         ch = 0, *p = NULL, *ps = NULL, *host = NULL, *path = NULL;
    int urlid = -1, ip = 0, port = 0, itime = 0, interval = 0;
    unsigned char *sip = NULL, *pip = NULL;
    LPROXY proxy = {0};

    if(task && buf && task->state && task->state->running)
    {
        //limit
        if(task->state->speed_limit > 0.0f && task->state->speed > task->state->speed_limit)
        {
            UPDATE_SPEED(task, interval);
            fprintf(stdout, "%f:%f\n", task->state->speed, task->state->speed_limit);
            return urlid;
        }
        if((urlid = ltask_pop_url(task, url_id, url, &itime, referid, refer, cookie)) >= 0)
        {
            DEBUG_LOGGER(task->logger, "TASK-URL:%s", url);
            host = p = ps = url + strlen(HTTP_PREF);
            //DEBUG_LOGGER(task->logger, "TASK-HOST:%s", host);
            while(*p != '\0' && *p != ':' && *p != '/')++p;
            ch = *p;
            *p = '\0';
            ip = ltask_get_host_ip(task, ps);
            port = 80;
            if(ch == ':')
            {
                port = atoi((p+1));
                while(*p != '\0' && *p != '/')++p;
                path = (p+1);
            }
            else if(ch == '/') path = (p+1);
            sip = (unsigned char *)&ip;
            p = buf;
            if(path == NULL || *path == '\0')
            {
                p += sprintf(p, "HTTP/1.0 200 OK\r\nFrom: %ld\r\nLocation: /\r\n"
                        "Host: %s\r\nServer: %d.%d.%d.%d\r\nTE:%d\r\n", 
                        (long)urlid, host, sip[0], sip[1], sip[2], sip[3], port);
            }
            else
            {
                p += sprintf(p, "HTTP/1.0 200 OK\r\nFrom: %ld\r\nLocation: /%s\r\n"
                        "Host: %s\r\nServer: %d.%d.%d.%d\r\nTE:%d\r\n", 
                        (long)urlid, path, host, sip[0], sip[1], sip[2], sip[3], port);
            }
            if(itime != 0 && GMTstrdate(itime, date) > 0)
                p += sprintf(p, "Last-Modified: %s\r\n", date);
            if(refer[0] != '\0') p += sprintf(p, "Referer: %s\r\n", refer);
            if(cookie[0] != '\0') p += sprintf(p, "Cookie: %s\r\n", cookie);
            if(uuid >= 0) p += sprintf(p, "UUID: %d\r\n", uuid);
            if(userid >= 0) p += sprintf(p, "UserID: %d\r\n", userid);
            if(task->state->is_use_proxy && ltask_get_proxy(task, &proxy) >= 0)
            {
                pip = (unsigned char *)&(proxy.ip);
                p += sprintf(p, "User-Agent: %d.%d.%d.%d\r\nVia: %d\r\n", 
                        pip[0], pip[1], pip[2], pip[3], proxy.port);
            }
            p += sprintf(p, "%s", "\r\n");
            *nbuf = p - buf;
            DEBUG_LOGGER(task->logger, "New TASK[%d] ip[%d.%d.%d.%d:%d] "
                    "http://%s/%s", urlid, sip[0], sip[1], sip[2], sip[3],
                    port, host, path);
            if(task->state) task->state->url_ntasks++;
        }
    }
    return urlid;
}

/* add dns host */
int ltask_add_dns(LTASK *task, char *dns_ip)
{
    int n = 0, id = -1, i = 0;
    void *dp = NULL;
    LDNS *dns = NULL;

    if(task && dns_ip && (n = strlen(dns_ip)) > 0)
    {
        MUTEX_LOCK(task->mutex);
        TRIETAB_GET(task->table, dns_ip, n, dp);
        if((id = ((long)dp - 1)) < 0)
        {
            if(task->dnsio.left == 0)
            {HIO_MMAP(task->dnsio, LDNS, DNS_INCRE_NUM);}
            if((dns = HIO_MAP(task->dnsio, LDNS)))
            {
                while(i < task->dnsio.total)
                {
                    if(dns[i].status <= 0)
                    {
                        dns[i].status = DNS_STATUS_READY;
                        strcpy(dns[i].name, dns_ip);
                        dp = (void *)((long)(i + 1));
                        id = i;
                        TRIETAB_ADD(task->table, dns_ip, n, dp);
                        task->dnsio.left--;
                        task->dnsio.current++;
                        break;
                    }
                    ++i;
                }
            }
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return id;
}

/* delete dns host */
int ltask_del_dns(LTASK *task, int dns_id, char *dns_ip)
{
    int n = 0, id = -1;
    void *dp = NULL;
    LDNS *dns = NULL;

    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(dns_ip && (n = strlen(dns_ip)) > 0)
        {
            TRIETAB_DEL(task->table, dns_ip, n, dp);
            id = (long)dp - 1;
        }
        else if((id = dns_id) >= 0 && id < task->dnsio.total 
                && (dns = (LDNS *)(task->dnsio.map)) 
                && dns != (LDNS *)-1 && (n = strlen(dns[id].name)) > 0)
        {
            TRIETAB_DEL(task->table, dns[id].name, n, dp);
            dns[id].status = 0;
            task->dnsio.left++;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return id;
}

/* set dns host status */
int ltask_set_dns_status(LTASK *task, int dns_id, char *dns_ip, int status)
{
    int n = 0, id = -1;
    void *dp = NULL;
    LDNS *dns = NULL;

    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(dns_ip && (n = strlen(dns_ip)) > 0)
        {
            TRIETAB_GET(task->table, dns_ip, n, dp);
            id = (long)dp - 1;
        }else id = dns_id;
        if(id >= 0 && id < task->dnsio.total 
                && (dns = (LDNS *)(task->dnsio.map)) 
                && dns != (LDNS *)-1 && (n = strlen(dns[id].name)) > 0)
        {
            dns[id].status = status;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return id;
}


/* pop dns */
int ltask_pop_dns(LTASK *task, char *dns_ip)
{
    int id = -1, i = 0, count = 0;
    LDNS *dns = NULL;

    if(task && dns_ip && task->dnsio.current > 0  
            && (count = task->dnsio.total - task->dnsio.left) > 0)
    {
        MUTEX_LOCK(task->mutex);
        if((dns = (LDNS *)(task->dnsio.map)) && dns != (LDNS *)-1 )
        {
            for(i = 0; i < task->dnsio.total; i++)
            {
                if(dns[i].status == DNS_STATUS_READY)
                {
                    id = i;
                    strcpy(dns_ip, dns[i].name);
                    dns[i].status = DNS_STATUS_OK;
                    task->dnsio.current--;
                    break;
                }
            }
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return id;
}

/* view dns  */
int ltask_view_dnslist(LTASK *task, char *block)
{
    char buf[HTTP_BUF_SIZE], *p = NULL, *pp = NULL;
    int n = -1, i = 0, count = 0;
    LDNS *dns = NULL;

    if(task && block && (count = task->dnsio.total - task->dnsio.left) > 0)
    {
        MUTEX_LOCK(task->mutex);
        if((dns = (LDNS *)(task->dnsio.map)) && dns != (LDNS *)-1)
        {
            p = buf;
            p += sprintf(p, "%s", "({'dnslist':{");
            pp = p;
            for(i = 0; i < task->dnsio.total; i++)
            {
                if(dns[i].status != 0)
                {
                    p += sprintf(p, "'%d':{'id':'%d', 'host':'%s', 'status':'%d'},",
                            i, i, dns[i].name, dns[i].status);
                }
            }
            if(p != pp) --p;
            p += sprintf(p, "%s", "}})");
            n = sprintf(block, "HTTP/1.0 200 OK\r\nContent-Type:text/html\r\n"
                    "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return n;
}

/* add user host */
int ltask_add_user(LTASK *task, char *name, char *passwd)
{
    int n = 0, id = -1, i = 0, x = 0;
    void *dp = NULL;
    LUSER *user = NULL;
    unsigned char key[MD5_LEN];

    if(task && name && (n = strlen(name)) > 0 && passwd && (x = strlen(passwd)) > 0)
    {
        MUTEX_LOCK(task->mutex);
        TRIETAB_GET(task->users, name, n, dp);
        if((id = ((long)dp - 1)) < 0)
        {
            if(task->userio.left <= 0)
            {HIO_MMAP(task->userio, LUSER, USER_INCRE_NUM);}
            if((user = HIO_MAP(task->userio, LUSER)))
            {
                while(i < task->userio.total)
                {
                    if(user[i].status <= 0)
                    {
                        strcpy(user[i].name, name);
                        md5((unsigned char *)passwd, x, key);
                        memcpy(user[i].passwd, key, MD5_LEN);
                        dp = (void *)((long)(i + 1));
                        id = i;
                        TRIETAB_ADD(task->users, name, n, dp);
                        user[i].status = USER_STATUS_OK;
                        task->userio.left--;
                        task->userio.current++;
                        break;
                    }
                    ++i;
                }
            }
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return id;
}

/* delete user host */
int ltask_del_user(LTASK *task, int userid, char *name)
{
    int n = 0, id = -1;
    void *dp = NULL;
    LUSER *user = NULL;

    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(name && (n = strlen(name)) > 0)
        {
            TRIETAB_DEL(task->users, name, n, dp);
            id = (long)dp - 1;
        }
        else if((id = userid) >= 0 && id < task->userio.total 
                && (user = (LUSER *)(task->userio.map)) 
                && user != (LUSER *)-1 && (n = strlen(user[id].name)) > 0)
        {
            TRIETAB_DEL(task->users, user[id].name, n, dp);
            user[id].status = 0;
            task->userio.left++;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return id;
}

/* update user password */
int ltask_update_passwd(LTASK *task, int userid, char *name, char *passwd)
{
    unsigned char key[MD5_LEN];
    int n = 0, id = -1;
    void *dp = NULL;
    LUSER *user = NULL;

    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(name && (n = strlen(name)) > 0)
        {
            TRIETAB_GET(task->users, name, n, dp);
            id = (long)dp - 1;
        }else id = userid;
        if(id >= 0 && id < task->userio.total 
                && (user = (LUSER *)(task->userio.map)) 
                && user != (LUSER *)-1 && (n = strlen(user[id].name)) > 0)
        {
            md5((unsigned char *)name, n, key);
            memcpy(user[id].passwd, key, MD5_LEN);
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return id;
}

/* update user permistion */
int ltask_update_permission(LTASK *task, int userid, char *name, int permission)
{
    int n = 0, id = -1;
    void *dp = NULL;
    LUSER *user = NULL;

    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(name && (n = strlen(name)) > 0)
        {
            TRIETAB_GET(task->users, name, n, dp);
            id = (long)dp - 1;
        }else id = userid;
        if(id >= 0 && id < task->userio.total 
                && (user = (LUSER *)(task->userio.map)) 
                && user != (LUSER *)-1 && (n = strlen(user[id].name)) > 0)
        {
            user[id].permissions &= permission;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return id;
}

/* user authorization */
int ltask_authorization(LTASK *task, int userid, char *name, char *passwd, LUSER *puser)
{
    unsigned char key[MD5_LEN];
    int n = 0, id = -1, x = 0;
    void *dp = NULL;
    LUSER *user = NULL;

    if(task && passwd && (x = strlen(passwd)) > 0)
    {
        MUTEX_LOCK(task->mutex);
        if(name && (n = strlen(name)) > 0)
        {
            TRIETAB_GET(task->users, name, n, dp);
            id = (long)dp - 1;
        }else id = userid;
        if(id >= 0 && id < task->userio.total 
                && (user = (LUSER *)(task->userio.map)) 
                && user != (LUSER *)-1 && (n = strlen(user[id].name)) > 0)
        {
            md5((unsigned char *)passwd, x, key);
            if(memcmp(user[id].passwd, key, MD5_LEN) != 0) id = -1;
            else memcpy(puser, &(user[id]), sizeof(LUSER));
        }
        else id = -1;
        MUTEX_UNLOCK(task->mutex);
    }
    return id;
}

/* set user status */
int ltask_set_user_status(LTASK *task, int userid, char *name, int status)
{
    int n = 0, id = -1;
    void *dp = NULL;
    LUSER *user = NULL;

    if(task)
    {
        MUTEX_LOCK(task->mutex);
        if(name && (n = strlen(name)) > 0)
        {
            TRIETAB_GET(task->users, name, n, dp);
            id = (long)dp - 1;
        }else id = userid;
        if(id >= 0 && id < task->userio.total 
                && (user = (LUSER *)(task->userio.map)) 
                && user != (LUSER *)-1 && (n = strlen(user[id].name)) > 0)
        {
            user[id].status = status;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return id;
}

/* list users */
int ltask_list_users(LTASK *task, char *block, int *nblock)
{
    if(task && block && nblock && *nblock > 0)
    {
    }
    return 0;
}
/* get state infomation */
int ltask_get_stateinfo(LTASK *task, char *block)
{
    int ret = -1, n = 0, interval = 0, day = 0, hour = 0, 
        min = 0, sec = 0, usec = 0;
    char  buf[HTTP_BUF_SIZE], *p = NULL;

    if(task && task->state)
    {
        MUTEX_LOCK(task->mutex);
        TIMER_SAMPLE(task->timer);
        day  = (PT_SEC_U(task->timer) / 86400);
        hour = ((PT_SEC_U(task->timer) % 86400) /3600);
        min  = ((PT_SEC_U(task->timer) % 3600) / 60);
        sec  = (PT_SEC_U(task->timer) % 60);
        usec = (PT_USEC_U(task->timer) % 1000000ll);
        UPDATE_SPEED(task, interval);
        p = (char *)running_ops[task->state->running];
        n = sprintf(buf, "({'status':'%s','url_total':'%d','url_ntasks':'%d', 'url_task_ok':'%d',"
                "'url_task_error':'%d', 'doc_total_zsize':'%lld', 'doc_total_size':'%lld',"
                "'host_current':'%d', 'host_total':'%d', 't_day':'%d', 't_hour':'%d',"
                "'t_min':'%d', 't_sec':'%d', 't_usec':'%d', 'speed':'%f', 'speed_limit':'%f'})", 
                p, task->state->url_total, task->state->url_ntasks,
                task->state->url_task_ok, task->state->url_task_error, task->state->doc_total_zsize, 
                task->state->doc_total_size, task->state->host_current, task->state->host_total, 
                day, hour, min, sec, usec, task->state->speed, task->state->speed_limit);
        ret = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%d\r\nConnection:close\r\n\r\n%s", n, buf);
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* update url content  */
int ltask_update_content(LTASK *task, int urlid, char *date, char *type, 
        char *content, int ncontent , int is_extract_link)
{
    char buf[HTTP_BUF_SIZE], *url = NULL, *p = NULL, *data = NULL;
    int ret = -1, n = 0, interval = 0;
    size_t  ndata = 0;
    LDOCHEADER  *pdocheader = NULL;
    struct stat st = {0};
    LMETA meta = {0};

    if(task && urlid >= 0 && content && ncontent > 0)
    {
        if(date)
        {
            DEBUG_LOGGER(task->logger, "Ready for update_content:(%d, %s,%s, %d)", 
                    urlid, date, type, ncontent);
        }
        MUTEX_LOCK(task->mutex);
        fstat(task->doc_fd, &st); 
        memset(buf, 0, HTTP_BUF_SIZE);
        pdocheader = (LDOCHEADER *)buf;
        url = buf + sizeof(LDOCHEADER);
        if(pread(task->meta_fd, &meta, sizeof(LMETA), (off_t)urlid * (off_t)sizeof(LMETA)) > 0 )
        {
            DEBUG_LOGGER(task->logger, "url:%s nurl:%d status:%d url_off:%lld", 
                    url, meta.url_len, meta.status, meta.url_off);
        }else goto end;
        if(meta.url_len > 0 && meta.url_len <= HTTP_URL_MAX && meta.status >= 0 
                && (n = pread(task->url_fd, url, meta.url_len, meta.url_off)) > 0)
        {
            pdocheader->nurl    = (short)meta.url_len;
            p = url + meta.url_len + 1;
            pdocheader->ntype   = sprintf(p, "%s", type);
            p += pdocheader->ntype + 1;
            pdocheader->id      = urlid;
            pdocheader->parent  = meta.parent;
            if(date){meta.last_modified = pdocheader->date = str2time(date);}
            pdocheader->date    = time(NULL);
            pdocheader->ncontent = ncontent;
            pdocheader->total = pdocheader->ntype + 1 + pdocheader->nurl + 1 + ncontent;
            if((n = (p - buf)) > 0 && pwrite(task->doc_fd, buf, n, st.st_size) > 0
                    && (meta.content_off = st.st_size) >= 0 && pwrite(task->doc_fd, 
                        content, ncontent, (meta.content_off+(off_t)n)) > 0)
            {
                meta.content_len = pdocheader->total + sizeof(LDOCHEADER);
                meta.status = 0;
                pwrite(task->meta_fd, &meta, sizeof(LMETA), (off_t)urlid * (off_t)sizeof(LMETA));
                if(task->state)
                {
                    task->state->doc_total_zsize += (off_t)ncontent;
                    task->state->doc_total_size += (off_t)ncontent;
                    task->state->url_task_ok++;
                    UPDATE_SPEED(task, interval);
                }
                DEBUG_LOGGER(task->logger, "nurl:%d[%s] ntype:%d[%s] ncontent:%d total:%d ", 
                        pdocheader->nurl, url, pdocheader->ntype, type, pdocheader->ncontent, 
                        meta.content_len);
                ret = 0;
            }
        }
        if(date)
        {
            DEBUG_LOGGER(task->logger, "Over for update_content:(%d, %s,%s, %d)", 
                    urlid, date, type, ncontent);
        }
end:
        MUTEX_UNLOCK(task->mutex);
        /* uncompress text/html */
        if(ret == 0 && is_extract_link && url && type && strncasecmp(type, "text", 4) == 0) 
        {
            DEBUG_LOGGER(task->logger, "Ready for extract_link(%d:%s)", urlid, url);
            ndata = ncontent * 20;
            if((data = (char *)calloc(1, ndata)))
            {
                if(uncompress((Bytef *)data, (uLongf *)&ndata, 
                            (const Bytef *)content, (uLong)ncontent) == 0)
                {
                    if(task->state && (n = (ndata - ncontent)) > 0)
                    {
                        task->state->doc_total_size += (off_t)n;
                    }
                    DEBUG_LOGGER(task->logger, "url:%s nurl:%d ndata:%ld", 
                            url, meta.url_len, LI(ndata));
                    task->extract_link(task, urlid, meta.depth, url, data, ndata);
                }
                free(data);
            }
            DEBUG_LOGGER(task->logger, "Over for extract_link(%d:%s)", urlid, url);
        }
    }
    return ret;
}

/* get content */
int ltask_get_content(LTASK *task, int urlid, char **block)
{
    LMETA meta = {0};
    int n = -1;

    if(task && urlid >= 0 && block && task->state && urlid < task->state->url_total)
    {
        MUTEX_LOCK(task->mutex);
        if(pread(task->meta_fd, &meta, sizeof(LMETA), (off_t)urlid * (off_t)sizeof(LMETA)) > 0
                && meta.content_len > 0 && (*block = (char *)calloc(1, meta.content_len)))
        {
            //fprintf(stdout, "%s:%d read %d from %lld\n",
            //        __FILE__, __LINE__, meta.content_len, meta.content_off);
            if(pread(task->doc_fd, *block, meta.content_len, meta.content_off) > 0)
            {
                n = meta.content_len;
            }
            else 
            {
                free(*block);
                *block = NULL;
            }
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return n ;
}

void ltask_free_content(char *block)
{
    if(block)
    {
        free(block);
    }
    return ;
}

#define URLTOEND(start, up, eup, flag, dpp)                             \
do                                                                      \
{                                                                       \
    while(up < eup && dpp < (start + HTTP_URL_MAX - 1))                 \
    {                                                                   \
        if(*up == 0x20 && (*(up+1) == 0x20 || *(up+1) == '\''           \
                    || *(up+1) == '"' ))break;                          \
        if(flag && (*up == '"' || *up == '\'')){++up;break;}            \
        else if(flag == 0 && (*up == 0x20 || *up == '\t'                \
                    || *up != '\n' || *up != '\r'))                     \
        {                                                               \
            ++up; break;                                                \
        }                                                               \
        else                                                            \
        {                                                               \
            if(*up == '#' || *up == '<' || *up == '>'){++up;break;}     \
            else if(*up == '\r' || *up == '\n' || *up == '\t')++up;     \
            else if(*up == '/' && *(up+1) == '/' && *(up-1) != ':')     \
            {                                                           \
                *dpp++ = '/'; up+=2;                                    \
            }                                                           \
            else if(*((unsigned char *)up) > 127 || *up == 0x20)        \
            {                                                           \
                dpp += sprintf(dpp, "%%%02x",*((unsigned char *)up));   \
                ++up;                                                   \
            }                                                           \
            else *dpp++ = *up++;                                        \
        }                                                               \
    }                                                                   \
    while(dpp > (start+2) && *(dpp-1) == '0' && *(dpp-2) == '2'         \
            && *(dpp-3) == '%'){dpp -= 3; *dpp = '\0';}                 \
}while(0)
/* extract link */
int ltask_extract_link(LTASK *task, int urlid, int depth, char *url, char *content, int ncontent)
{
    char buf[HTTP_BUF_SIZE], *path = NULL, *last = NULL, *p = NULL, 
         *end = NULL, *pp = NULL, *ps = NULL;
    int ret = -1, pref = 0, n = 0;

    if(task && url && (p = content) && ncontent > 0)
    {
        //parse url prefix
        memset(buf, 0, HTTP_BUF_SIZE);
        ps = url;
        while(*ps != '\0')
        {
            if(*ps == ':' && *(ps+1) == '/' && *(ps+2) == '/')
            {
                ps += 3;
                while(*ps != '\0' && *ps != '/') ++ps;
                last = path = ps++;
            }
            else if(*ps == '/') last = ps++;
            else ++ps;
        }
        end = p + ncontent;
        DEBUG_LOGGER(task->logger, "path:%s last:%s url:%s ", path, last, url);
        //parse link(s)
        while(p < end)
        {
            pref = 0;
            if(*p == '<' && (*(p+1) == 'a' || *(p+1) == 'A') && (*(p+2) == 0x20 || *(p+2) == '\t'))
            {
                memset(buf, 0, HTTP_BUF_SIZE);
                p += 2;
                while(p < end  && (*p == 0x20 || *p == '\t' || *p == '\n' || *p == '\r'))++p;
                if(strncasecmp(p, "href", 4) != 0) continue;
                p += 4;
                while(p < end  && (*p == 0x20 || *p == '\t' || *p == '\n' || *p == '\r'))++p;
                if(*p != '=') continue;
                ++p;
                while(p < end  && (*p == 0x20 || *p == '\t' || *p == '\n' || *p == '\r'))++p;
                if(*p == '\'' || *p == '"') {++p; pref = 1;}
                if(*p == '#' || strncasecmp(p, "javascript", 10) == 0 
                        || strncasecmp(p, "mailto", 6) == 0)
                {
                    goto next;
                }
            }
            else if(task->state->is_extract_image && *p == '<' && (*(p+1) == 'i' || *(p+1) == 'I') 
                    && (*(p+2) == 'm' || *(p+2) == 'M') && (*(p+3) == 'g' || *(p+3) == 'G'))
            {
                p += 4;
                if(*p != 0x20 && *p != '\t') goto next;
                while(p < end && *p != '>')
                {
                    if((*p == 's' || *p == 'S') && (*(p+1) == 'r' || *(p+1) == 'R')
                            && (*(p+2) == 'c' || *(p+2) == 'C'))
                    {
                        p += 3;
                        while(p < end  && (*p == 0x20 || *p == '\t' || *p == '\n' || *p == '\r'))++p;
                        if(*p != '=') goto next;
                        ++p;
                        while(p < end  && (*p == 0x20 || *p == '\t' || *p == '\n' || *p == '\r'))++p;
                        if(*p == '\'' || *p == '"') {++p; pref = 1;}
                        break;
                    }
                    else ++p;
                }
            }
            else 
            {
                ++p;
                continue;
            }
            //DEBUG_LOGGER(task->logger, "path:%s last:%s url:%s ", path, last, url);
            //READURL(task, p, end, pp, buf, ps, path, url, last, n, pref);
            //read url
            if(*p == '/')                                                       
            {                                                                   
                pp = buf;                                                       
                if((n = (path - url)) > 0)                                      
                {                                                               
                    //DEBUG_LOGGER(task->logger, "copy nurl:%d url:%s", n, url);  
                    memcpy(pp, url, n);                                         
                    pp += n;                                                    
                    URLTOEND(buf, p, end, pref, pp);                                 
                }                                                               
            }                                                                   
            else if(*p == '.')                                                  
            {                                                                   
                if(*(p+1) == '/') p += 2;                                       
                ps = last;                                                      
                while(*p == '.' && *(p+1) == '.' && *(p+2) == '/')              
                {                                                               
                    p += 3;                                                     
                    while(*p == '/')++p;                                        
                    if(*ps != '/') goto next;                                   
                    --ps;                                                       
                    while(ps >= path && *ps != '/')--ps;                        
                    if(ps < path) goto next;                                    
                }                                                               
                pp = buf;                                                       
                if((n = (ps - url)) > 0)                                        
                {                                                               
                    strncpy(pp, url, n);                                        
                    pp += n;                                                    
                    *pp++ = '/';                                                
                    URLTOEND(buf, p, end, pref, pp);                                 
                }                                                               
            }                                                                   
            else if(strncasecmp(p, HTTP_PREF, 7) == 0)                          
            {                                                                   
                p += 7;                                                         
                pp = buf;                                                       
                pp += sprintf(pp, "%s", HTTP_PREF);                             
                URLTOEND(buf, p, end, pref, pp);                                     
            }                                                                   
            else                                                                
            {                                                                   
                ps = p;                                                         
                while(p < end && ((*p >= 'a' && *p <= 'z')                      
                            || (*p >= 'A' && *p <= 'Z')))                       
                    p++;                                                        
                if(*p == ':' && *(p+1) == '/' && *(p+2) == '/') goto next;      
                pp = buf;                                                       
                if(last && (n = (last - url)) > 0)                              
                {                                                               
                    strncpy(pp, url, n);                                        
                    pp += n;                                                    
                    *pp++ = '/';                                                
                    p = ps;
                    URLTOEND(buf, p, end, pref, pp);                                 
                }                                                               
            } 
            if((n = (pp - buf)) > 0 && n < HTTP_URL_MMAX)
            {
                *pp = '\0';
                DEBUG_LOGGER(task->logger, "add url:%s from %s\n", buf, url);
                task->add_url(task, urlid, depth, buf, 0);
            }
            /* to href last > */
next:
            while(p < end && *p != '>')++p;
            ++p;
        }
        ret = 0;
    }
    return ret;
}


/* clean */
void ltask_clean(LTASK **ptask)
{
    if(ptask && *ptask)
    {
        if((*ptask)->mutex) {MUTEX_DESTROY((*ptask)->mutex);}
        if((*ptask)->logger) {LOGGER_CLEAN((*ptask)->logger);}
        if((*ptask)->errlogger) {LOGGER_CLEAN((*ptask)->errlogger);}
        if((*ptask)->timer) {TIMER_CLEAN((*ptask)->timer);}
        if((*ptask)->urlmap) {KVMAP_CLEAN((*ptask)->urlmap);}
        if((*ptask)->table) {TRIETAB_CLEAN((*ptask)->table);}
        if((*ptask)->users) {TRIETAB_CLEAN((*ptask)->users);}
        if((*ptask)->cookies) {TRIETAB_CLEAN((*ptask)->cookies);}
        if((*ptask)->qtask){FQUEUE_CLEAN((*ptask)->qtask);}
        if((*ptask)->qproxy){QUEUE_CLEAN((*ptask)->qproxy);}
        if((*ptask)->key_fd > 0) close((*ptask)->key_fd);
        if((*ptask)->url_fd > 0) close((*ptask)->url_fd);
        if((*ptask)->domain_fd > 0) close((*ptask)->domain_fd);
        if((*ptask)->doc_fd > 0) close((*ptask)->doc_fd);
        if((*ptask)->cookie_fd > 0) close((*ptask)->cookie_fd);
        if((*ptask)->state_fd > 0) 
        {
	    msync((*ptask)->state, sizeof(LSTATE), MS_SYNC);
	    munmap((*ptask)->state, sizeof(LSTATE));
            close((*ptask)->state_fd);
        }
	HIO_CLEAN((*ptask)->proxyio);
	HIO_CLEAN((*ptask)->hostio);
	HIO_CLEAN((*ptask)->ipio);
	HIO_CLEAN((*ptask)->dnsio);
	HIO_CLEAN((*ptask)->userio);
        free(*ptask);
        *ptask = NULL;
    }
    return ;
}

/* initialize */
LTASK *ltask_init()
{
    LTASK *task = NULL;

    if((task = (LTASK *)calloc(1, sizeof(LTASK))))
    {
        KVMAP_INIT(task->urlmap);
        TRIETAB_INIT(task->table);
        TRIETAB_INIT(task->users);
        TRIETAB_INIT(task->cookies);
        TIMER_INIT(task->timer);
        MUTEX_INIT(task->mutex);
        QUEUE_INIT(task->qproxy);
        task->set_basedir           = ltask_set_basedir;
        task->set_state_running     = ltask_set_state_running;
        task->set_state_proxy       = ltask_set_state_proxy;
        task->set_speed_limit       = ltask_set_speed_limit;
        task->add_proxy             = ltask_add_proxy;
        task->get_proxy             = ltask_get_proxy;
        task->set_proxy_status      = ltask_set_proxy_status;
        task->del_proxy             = ltask_del_proxy;
        task->view_proxylist        = ltask_view_proxylist;
        task->pop_host              = ltask_pop_host;
        task->add_dns               = ltask_add_dns;
        task->del_dns               = ltask_del_dns;
        task->set_dns_status        = ltask_set_dns_status;
        task->pop_dns               = ltask_pop_dns;
        task->view_dnslist          = ltask_view_dnslist;
        task->set_host_ip           = ltask_set_host_ip;
        task->get_host_ip           = ltask_get_host_ip;
        task->list_host_ip          = ltask_list_host_ip;
        task->set_host_status       = ltask_set_host_status;
        task->set_host_level        = ltask_set_host_level;
        task->add_url               = ltask_add_url;
        task->set_url_status        = ltask_set_url_status;
        task->set_url_level         = ltask_set_url_level;
        task->pop_url               = ltask_pop_url;
        task->get_url               = ltask_get_url;
        task->get_task              = ltask_get_task;
        task->add_user              = ltask_add_user;
        task->del_user              = ltask_del_user;
        task->set_user_status       = ltask_set_user_status;
        task->update_passwd         = ltask_update_passwd;
        task->update_permission     = ltask_update_permission;
        task->authorization         = ltask_authorization;
        task->list_users            = ltask_list_users;
        task->get_stateinfo         = ltask_get_stateinfo;
        task->update_content        = ltask_update_content;
        task->get_content           = ltask_get_content;
        task->free_content          = ltask_free_content;
        task->extract_link          = ltask_extract_link;
        task->clean                 = ltask_clean;
    }
    return task;
}


#ifdef _DEBUG_LTASK
static char *proxylist[] = 
{
    "66.104.77.20:3128",
    "164.73.47.244:3128",
    "200.228.43.202:3128",
    "121.22.29.180:80",
    "202.181.184.203:80",
    "86.0.64.246:9090",
    "204.8.155.226:3124",
    "129.24.211.25:3128",
    "217.91.6.207:8080",
    "202.27.17.175:80",
    "128.233.252.12:3124",
    "67.69.254.244:80",
    "192.33.90.66:3128",
    "203.160.1.94:80",
    "201.229.208.2:80",
    "130.37.198.244:3127",
    "155.246.12.163:3124",
    "141.24.33.192:3128",
    "193.188.112.21:80",
    "128.223.8.111:3127",
    "67.69.254.243:80",
    "212.93.193.72:443",
    "141.24.33.192:3124",
    "121.22.29.182:80",
    "221.203.154.26:8080",
    "203.160.1.112:80",
    "193.39.157.48:80",
    "130.37.198.244:3128",
    "129.24.211.25:3124",
    "195.116.60.34:3127",
    "199.239.136.200:80",
    "199.26.254.65:3128",
    "193.39.157.15:80",
    "218.28.58.86:3128",
    "60.12.227.209:3128",
    "128.233.252.12:3128",
    "137.226.138.154:3128",
    "67.69.254.240:80",
    "152.3.138.5:3128",
    "142.150.238.13:3124",
    "199.239.136.245:80",
    "203.160.1.66:80",
    "123.130.112.17:8080",
    "203.160.1.103:80",
    "198.82.160.220:3124"
};
#define NPROXY 45
static char *urllist[] = 
{
    "http://www.sina.com.cn",
	"http://news.sina.com.cn/", 
	"http://mil.news.sina.com.cn/", 
	"http://news.sina.com.cn/society/", 
	"http://blog.sina.com.cn/", 
	"http://blog.sina.com.cn/lm/ruiblog/index.html?tj=1", 
	"http://blog.sina.com.cn/lm/rank/index.html?tj=1", 
	"http://news.sina.com.cn/guide/", 
	"http://weather.news.sina.com.cn/", 
	"http://news.sina.com.cn/health/index.shtml", 
	"http://news.sina.com.cn/china/", 
	"http://news.sina.com.cn/world/", 
	"http://sky.news.sina.com.cn/", 
	"http://news.sina.com.cn/opinion/index.shtml", 
	"http://news.sina.com.cn/interview/index.shtml", 
	"http://news.sina.com.cn/photo/", 
	"http://survey.news.sina.com.cn/list.php?channel=news&dpc=1", 
	"http://news.sina.com.cn/news1000/", 
	"http://news.sina.com.cn/hotnews/", 
	"http://news.sina.com.cn/zt/", 
	"http://news.sina.com.cn/w/p/2009-01-28/131217118076.shtml", 
	"http://news.sina.com.cn/z/2009europedavostrip/index.shtml", 
	"http://news.sina.com.cn/w/2009-01-28/164817118372.shtml", 
	"http://news.sina.com.cn/c/2009-01-28/110515088916s.shtml", 
	"http://news.sina.com.cn/c/2009-01-28/090617117716.shtml", 
	"http://news.sina.com.cn/z/2009chunjie/index.shtml", 
	"http://news.sina.com.cn/c/2009-01-28/061517117393.shtml", 
	"http://news.sina.com.cn/z/video/2009chunjie/index.shtml", 
	"http://blog.sina.com.cn/lm/z/2009chunjie/index.html", 
	"http://news.sina.com.cn/z/2009chunyun/index.shtml", 
	"http://comment4.news.sina.com.cn/comment/skin/simple.html?channel=yl&newsid=28-19-3738&style=1", 
	"http://news.sina.com.cn/c/2009-01-28/140817118125.shtml", 
	"http://news.sina.com.cn/w/2009-01-28/171417118397.shtml", 
	"http://news.sina.com.cn/w/2009-01-28/032417117117.shtml", 
	"http://news.sina.com.cn/w/2009-01-28/103015088902s.shtml", 
	"http://news.sina.com.cn/c/2009-01-28/050617117332.shtml", 
	"http://news.sina.com.cn/w/2009-01-28/101215088878s.shtml"
};
#define NURL 37
#define HTTP_BUF_MAX    65536
int main(int argc, char **argv)
{
    LTASK *task = NULL;
    LPROXY proxy = {0};
    char *basedir = "/tmp/html", *p = NULL, buf[HTTP_BUF_MAX],
         host[HTTP_HOST_MAX], url[HTTP_URL_MAX], ip[HTTP_IP_MAX],
         *username = "redor", *passwd ="jadskfhjksdfhdfdsf";
    int i = 0, n = 0, urlid = -1, nbuf = 0, userid = 0;
    LUSER user = {0};
    unsigned char *pp = NULL;

    if((task = ltask_init()))
    {
        task->set_basedir(task, basedir);
        /* user */
        if((userid = task->add_user(task, username, passwd)) >= 0)
        {
            fprintf(stdout, "%d::added user[%d]\n", __LINE__, userid);
            if(task->authorization(task, userid, NULL, passwd, &user) < 0)
            {
                fprintf(stderr, "%d::Authorize user[%s] failed\n", __LINE__, username);
            }
            fprintf(stdout, "%d::__OK__\n", __LINE__);
            if(task->authorization(task, -1, username, passwd, &user) < 0)
            {
                fprintf(stderr, "%d::Authorize user[%s] failed\n", __LINE__, username);
            }
            fprintf(stdout, "%d::__OK__\n", __LINE__);
            if(task->update_passwd(task, -1, username, "dksajfkldsfjls") < 0)
            {
                fprintf(stderr, "update user[%s] password failed\n", username);
            }
            fprintf(stdout, "%d::__OK__\n", __LINE__);
            if(task->authorization(task, -1, username, passwd, &user) < 0)
            {
                fprintf(stderr, "%d::Authorize user[%s] failed\n", __LINE__, username);
            }
            userid = task->del_user(task, -1, "redor");
            fprintf(stdout, "%d::__OK__\n", __LINE__);
            if(task->authorization(task, -1, username, passwd, &user) < 0)
            {
                fprintf(stderr, "%d::Authorize user[%s] failed\n", __LINE__, username);
            }
            userid = task->del_user(task, userid, NULL);
            fprintf(stdout, "%d::__OK__\n", __LINE__);
            userid = task->add_user(task, "abcdsd", "daksfjldksf");
            fprintf(stdout, "%d::added user[%d]\n", __LINE__, userid);
            userid = task->add_user(task, "sddsfkhdf", "daksfdsfaslkdjfaldsfjldksf");
            fprintf(stdout, "%d::added user[%d]\n", __LINE__, userid);
            //_exit(-1);
        }
        /* proxy */
        fprintf(stdout, "%d::qtotal:%d\n", __LINE__, QTOTAL(task->qproxy));
        for(i = 0; i < NPROXY; i++)
        {
            p = proxylist[i];
            task->add_proxy(task, p);
        }
        fprintf(stdout, "%d::qtotal:%d\n", __LINE__, QTOTAL(task->qproxy));
        task->set_proxy_status(task, -1, "198.82.160.220:3124", PROXY_STATUS_OK);
        task->set_proxy_status(task, -1, "199.239.136.245:80", PROXY_STATUS_OK);
        task->set_proxy_status(task, -1, "142.150.238.13:3124", PROXY_STATUS_OK);
        i = 0;
        while(task->get_proxy(task, &proxy) == 0)
        {
            pp = (unsigned char *)&(proxy.ip);
            fprintf(stdout, "%d::[%d][%d.%d.%d.%d:%d]\n", __LINE__, 
                    i++, pp[0], pp[1], pp[2], pp[3], proxy.port);
        }
        /* host/url */
        for(i = 0; i < NURL; i++)
        {
            p = urllist[i];
            task->add_url(task, 0, 0, p);
        }
        i = 0;
        while(task->pop_host(task, host) >= 0)
        {
            sprintf(ip, "202.0.16.%d", (random()%256));
            n = inet_addr(ip);
            pp = (unsigned char *)&n;
            fprintf(stdout, "[%d.%d.%d.%d]\n", pp[0], pp[1], pp[2], pp[3]);
            task->set_host_ip(task, host, &n, 1);
            task->set_host_status(task, -1, host, HOST_STATUS_OK);
            n = task->get_host_ip(task, host);
            pp = (unsigned char *)&n;
            fprintf(stdout, "%d::[%d][%s][%d.%d.%d.%d]\n",
                    __LINE__, i++, host, pp[0], pp[1], pp[2], pp[3]);
        }
        task->set_host_status(task, -1, "news.sina.com.cn", HOST_STATUS_ERR);
        task->set_host_level(task, -1, "blog.sina.com.cn", L_LEVEL_UP);
        task->list_host_ip(task, stdout);
        task->set_host_status(task, -1, "news.sina.com.cn", HOST_STATUS_OK);
        task->set_url_status(task, -1, 
                "http://news.sina.com.cn/w/2009-01-28/171417118397.shtml", 
                URL_STATUS_ERR);
        task->set_url_level(task, -1, 
                "http://news.sina.com.cn/w/2009-01-28/101215088878S.shtml", 
                L_LEVEL_UP);
        while((urlid = task->get_task(task, buf, &nbuf)) >= 0)
        {
            fprintf(stdout, "%d::block[%s]\n", urlid, buf);
        }
        //content/link
        char *file = NULL, *type = NULL, *url = NULL, *data = NULL, 
             *zdata = NULL, *content = NULL;
        int fd = 0, ncontent = 0, nzdata = 0;
        struct stat st = {0};
        if(argc > 3 && (type = argv[1]) && (url = argv[2]) && (file = argv[3]))
        {
            fprintf(stdout, "__%d__::type:%s url:%s file:%s\n", __LINE__, type, url, file);
            if((fd = open(file, O_RDONLY)) > 0)
            {
                if(fstat(fd, &st) == 0 && st.st_size > 0 
                        && (data = (char *)calloc(1, st.st_size + 1)))
                {
                    if((read(fd, data, st.st_size)) > 0)
                    {
                        content = data;
                        ncontent = st.st_size;
                        if(strncasecmp(type, "text", 4) == 0)
                        {
                            nzdata = compressBound(st.st_size);
                            if((zdata = (char *)calloc(1, nzdata))&& compress((Bytef *)zdata, 
                                        (uLongf *)&nzdata, (const Bytef *)data, st.st_size) == 0)  
                            {
                                content = zdata;
                                ncontent = nzdata;
                            }
                        }
                        task->update_content(task, 35, "Mon, 08 Jun 2009 02:22:52 GMT", 
                                "text/html", content, ncontent);
                    }
                    if(zdata) free(zdata);
                    zdata = NULL;
                    free(data);
                    data = NULL;
                }
                close(fd);
            }
        }
        task->clean(&task);
    }
}
//gcc -o task ltask.c utils/*.c -I utils/ -D_DEBUG_LTASK -lz && ./task (type url file)
#endif
