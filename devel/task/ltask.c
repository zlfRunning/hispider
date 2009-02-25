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
#include "logger.h"
#define _EXIT_(format...)                                                               \
do                                                                                      \
{                                                                                       \
    fprintf(stderr, format);                                                            \
    _exit(-1);                                                                          \
}while(0)
#define _MMAP_(io, type, incre_num)                                                     \
do                                                                                      \
{                                                                                       \
    if(io.fd > 0 && io.end >= io.size && incre_num > 0)                                 \
    {                                                                                   \
        if(io.map && io.end > 0)                                                        \
        {                                                                               \
            msync(io.map, io.end, MS_SYNC);                                             \
            munmap(io.map, io.end);                                                     \
            io.map = NULL;                                                              \
        }                                                                               \
        io.size = (((io.end/(off_t)sizeof(type))/(off_t)incre_num)                      \
                + (off_t)1) * (off_t)incre_num;                                         \
        if((io.map = mmap(NULL, io.size, MAP_SHARED,                                    \
                        PROT_READ|PROT_WRITE, io.fd, 0)) == (void *)-1)                 \
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
    struct stat st = {0};
    char path[L_PATH_MAX];
    int n = 0;

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
            if(fstat(task->proxyio.fd, &st) == 0)
            {
                task->proxyio.end = st.st_size;
                task->proxyio.total = st.st_size/(off_t)sizeof(LPROXY);
                _MMAP_(task->proxyio, LPROXY, PROXY_INCRE_NUM);
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
            if(fstat(task->hostio.fd, &st) == 0)
            {
                task->hostio.end = st.st_size;
                task->hostio.total = st.st_size/(off_t)sizeof(LHOST);
                _MMAP_(task->hostio, LHOST, HOST_INCRE_NUM);
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
        sprintf(path, "%s/%s", dir, L_IP_NAME);
        if((task->ipio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            if(fstat(task->ipio.fd, &st) == 0)
            {
                task->ipio.end = st.st_size;
                task->ipio.total = st.st_size/(off_t)sizeof(int);
                _MMAP_(task->ipio, int, IP_INCRE_NUM);
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
        /* queue */
        sprintf(path, "%s/%s", dir, L_QUEUE_NAME);
        if((task->queueio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            if(fstat(task->queueio.fd, &st) == 0)
            {
                task->queueio.end = st.st_size;
                task->queueio.total = st.st_size/(off_t)sizeof(LNODE);
                _MMAP_(task->queueio, LNODE, QUEUE_INCRE_NUM);
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
        /* document */
        return 0;
    }
    return -1;
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
        task->set_basedir   = ltask_set_basedir;
        task->clean         = ltask_clean;
    }
    return task;
}


#ifdef _DEBUG_LTASK
int main()
{
    LTASK *task = NULL;
    char *basedir = "/tmp/html";

    if((task = ltask_init()))
    {
        task->set_basedir(task, basedir);
        task->clean(&task);
    }
}
#endif
