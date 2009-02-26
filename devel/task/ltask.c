#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "trie.h"
#include "timer.h"
#include "kvmap.h"
#include "ltask.h"
#include "queue.h"
#include "logger.h"
#define _EXIT_(format...)                                                               \
do                                                                                      \
{                                                                                       \
    fprintf(stderr, "%s::%d ", __FILE__, __LINE__);                                     \
    fprintf(stderr, format);                                                            \
    _exit(-1);                                                                          \
}while(0)
#define _MMAP_(io, st, type, incre_num)                                                 \
do                                                                                      \
{                                                                                       \
    if(io.fd > 0 && incre_num > 0)                                                      \
    {                                                                                   \
        if(io.map && io.size > 0)                                                       \
        {                                                                               \
            msync(io.map, io.size, MS_SYNC);                                            \
            munmap(io.map, io.size);                                                    \
        }                                                                               \
        else                                                                            \
        {                                                                               \
            if(fstat(io.fd, &st) != 0)                                                  \
            {                                                                           \
                _EXIT_("fstat(%d) failed, %s\n", io.fd, strerror(errno));               \
            }                                                                           \
            io.size = st.st_size;                                                       \
        }                                                                               \
        if(io.size == 0 || io.map)                                                      \
        {                                                                               \
            io.size += ((off_t)sizeof(type) * (off_t)incre_num);                        \
            ftruncate(io.fd, io.size);                                                  \
            io.left += incre_num;                                                       \
        }                                                                               \
        io.total = io.size/(off_t)sizeof(type);                                         \
        if((io.map = mmap(NULL, io.size, PROT_READ|PROT_WRITE, MAP_SHARED,              \
                        io.fd, 0)) == (void *)-1)                                       \
        {                                                                               \
            _EXIT_("mmap %d size:%lld failed, %s\n", io.fd,                             \
                    (long long int)io.size, strerror(errno));                           \
        }                                                                               \
    }                                                                                   \
}while(0)
#define _MUNMAP_(mp, size)                                                              \
do                                                                                      \
{                                                                                       \
    if(mp && size > 0)                                                                  \
    {                                                                                   \
        msync(mp, size, MS_SYNC);                                                       \
        munmap(mp, size);                                                               \
        mp = NULL;                                                                      \
    }                                                                                   \
}while(0)

/* mkdir */
int ltask_mkdir(char *path, int mode)
{
    char *p = NULL, fullpath[L_PATH_MAX];
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
    char path[L_PATH_MAX], host[L_HOST_MAX], *p = NULL;
    unsigned char *ip = NULL;
    struct stat st = {0};
    LPROXY *proxy = NULL;
    void *dp = NULL;
    int n = 0, id = 0;

    if(task && dir)
    {
        /* state */
        sprintf(path, "%s/%s", dir, L_LOG_NAME);
        if(ltask_mkdir(path, 0755) != 0)
        {
            _EXIT_("mkdir -p %s failed, %s\n", path, strerror(errno));
        }
        LOGGER_INIT(task->logger, path);
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
        /* proxy */
        sprintf(path, "%s/%s", dir, L_PROXY_NAME);
        if((task->proxyio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            _MMAP_(task->proxyio, st, LPROXY, PROXY_INCRE_NUM);
            if((proxy = (LPROXY *)task->proxyio.map) && task->proxyio.total > 0)
            {
                task->proxyio.left = 0;
                id = 0;
                do
                {
                    if(proxy->status == PROXY_STATUS_OK)
                    {
                        ip = (unsigned char *)&(proxy->ip);
                        n = sprintf(host, "%d.%d.%d.%d:%d", ip[0], ip[1], 
                                ip[2], ip[3], proxy->port);
                        dp = (void *)((long)(id + 1));
                        TRIETAB_ADD(task->table, host, n, dp);
                        QUEUE_PUSH(task->qproxy, int, &id);
                    }
                    else
                    {
                       task->proxyio.left++;
                    }
                    ++proxy;
                }while(id++ < task->proxyio.total);
            }
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        /* host/ip/url/domain/document */
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
        sprintf(path, "%s/%s", dir, L_DOC_NAME);
        if((task->doc_fd = open(path, O_CREAT|O_RDWR|O_APPEND, 0644)) < 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_HOST_NAME);
        if((task->hostio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
                _MMAP_(task->hostio, st, LHOST, HOST_INCRE_NUM);
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s/%s", dir, L_IP_NAME);
        if((task->ipio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            _MMAP_(task->ipio, st, int, IP_INCRE_NUM);
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        /* queue */
        sprintf(path, "%s/%s", dir, L_QUEUE_NAME);
        if((task->queueio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            _MMAP_(task->queueio, st, LNODE, QUEUE_INCRE_NUM);
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        /* document */
        return 0;
    }
    return -1;
}

/* add proxy */
int ltask_add_proxy(LTASK *task, char *host)
{
    char *p = NULL, *e = NULL, *pp = NULL, *ps = NULL, ip[L_HOST_MAX];
    int n = 0, i = 0, ret = -1;
    struct stat st = {0};
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
            if(task->proxyio.left == 0){_MMAP_(task->proxyio, st, LPROXY, PROXY_INCRE_NUM);}
            if(task->proxyio.left > 0 && (proxy = (LPROXY *)(task->proxyio.map)))
            {
                i = 0;
                do
                {
                    if(proxy->status != (short)PROXY_STATUS_OK)
                    {
                        dp = (void *)((long)(i+1));
                        TRIETAB_ADD(task->table, host, n, dp);
                        QUEUE_PUSH(task->qproxy, int, &i);
                        proxy->status = (short)PROXY_STATUS_OK;
                        *e = '\0';
                        proxy->ip = inet_addr(ip);
                        proxy->port = (unsigned short)atoi(pp);
                        /*
                        unsigned char *s = (unsigned char *)&(proxy->ip);
                        fprintf(stdout, "%d::%s %d:%d.%d.%d.%d:%d\n", 
                                __LINE__, host, proxy->ip, 
                        s[0], s[1], s[2], s[3], proxy->port);
                        */
                        task->proxyio.left--;
                        ret = 0;
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
    int rand = 0, id = 0, ret = -1;
    LPROXY *node = NULL;

    if(task && proxy)
    {
        MUTEX_LOCK(task->mutex);
        if(QTOTAL(task->qproxy) > 0)
        {
            do
            {
                if(QUEUE_POP(task->qproxy, int, &id) == 0)
                {
                    node = (LPROXY *)(task->proxyio.map + id * sizeof(LPROXY));
                    if(node->status == PROXY_STATUS_OK)
                    {
                        memcpy(proxy, node, sizeof(LPROXY));
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

/* delete proxy */
int ltask_set_proxy_status(LTASK *task, int id, char *host, short status)
{
    int ret = -1, n = 0, i = -1;
    LPROXY *proxy = NULL;
    void *dp = NULL;

    if(task && (id >= 0  || host))
    {
        MUTEX_LOCK(task->mutex);
        if(host)
        {
            n = strlen(host);
            TRIETAB_GET(task->table, host, n, dp);
            if(dp) i = (long)dp - 1;
        }
        else i = id;
        if(i >= 0 && i < task->proxyio.total)
        {
            proxy = (LPROXY *)(task->proxyio.map + i * sizeof(LPROXY));
            proxy->status = status;
        }
        MUTEX_UNLOCK(task->mutex);
    }
    return ret;
}

/* clean */
void ltask_clean(LTASK **ptask)
{
    if(ptask && *ptask)
    {
        if((*ptask)->mutex) {MUTEX_DESTROY((*ptask)->mutex);}
        if((*ptask)->timer) {TIMER_CLEAN((*ptask)->timer);}
        if((*ptask)->urlmap) {KVMAP_CLEAN((*ptask)->urlmap);}
        if((*ptask)->table) {TRIETAB_CLEAN((*ptask)->table);}
        if((*ptask)->qtask){QUEUE_CLEAN((*ptask)->qtask);}
        if((*ptask)->qproxy){QUEUE_CLEAN((*ptask)->qproxy);}
        if((*ptask)->url_fd > 0) close((*ptask)->url_fd);
        if((*ptask)->domain_fd > 0) close((*ptask)->domain_fd);
        if((*ptask)->doc_fd > 0) close((*ptask)->doc_fd);
        if((*ptask)->state_fd > 0) 
        {
            _MUNMAP_((*ptask)->state, sizeof(LSTATE));
            close((*ptask)->state_fd);
        }
        if((*ptask)->proxyio.fd > 0)
        {
            _MUNMAP_((*ptask)->proxyio.map, (*ptask)->proxyio.end);
            close((*ptask)->proxyio.fd);
        }
        if((*ptask)->hostio.fd > 0)
        {
            _MUNMAP_((*ptask)->hostio.map, (*ptask)->hostio.end);
            close((*ptask)->hostio.fd);
        }
        if((*ptask)->ipio.fd > 0)
        {
            _MUNMAP_((*ptask)->ipio.map, (*ptask)->ipio.end);
            close((*ptask)->ipio.fd);
        }
        if((*ptask)->queueio.fd > 0)
        {
            _MUNMAP_((*ptask)->queueio.map, (*ptask)->queueio.end);
            close((*ptask)->queueio.fd);
        }
    }
}

/* initialize */
LTASK *ltask_init()
{
    LTASK *task = NULL;

    if((task = (LTASK *)calloc(1, sizeof(LTASK))))
    {
        KVMAP_INIT(task->urlmap);
        TRIETAB_INIT(task->table);
        TIMER_INIT(task->timer);
        MUTEX_INIT(task->mutex);
        QUEUE_INIT(task->qtask);
        QUEUE_INIT(task->qproxy);
        task->set_basedir           = ltask_set_basedir;
        task->add_proxy             = ltask_add_proxy;
        task->get_proxy             = ltask_get_proxy;
        task->set_proxy_status      = ltask_set_proxy_status;
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
int main()
{
    LTASK *task = NULL;
    LPROXY proxy = {0};
    char *basedir = "/tmp/html", *p = NULL;
    unsigned char *ip = NULL;
    int i = 0;

    if((task = ltask_init()))
    {
        task->set_basedir(task, basedir);
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
            ip = (unsigned char *)&(proxy.ip);
            fprintf(stdout, "%d::[%d][%d.%d.%d.%d:%d]\n", __LINE__, 
                    i++, ip[0], ip[1], ip[2], ip[3], proxy.port);
        }
        task->clean(&task);
    }
}
//gcc -o task ltask.c utils/*.c -I utils/ -D_DEBUG_LTASK && ./task
#endif
