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
#include <pthread.h>
#include "link.h"
#include "basedef.h"
#include "iniparser.h"
#include "zstream.h"
#include "logger.h"
#include "timer.h"
#include "http.h"
//gen.sh 
//gcc -o tlink -D_DEBUG_LINKTABLE -D_FILE_OFFSET_BITS=64 link.c http.c utils/*.c -I utils/ -DHAVE_PTHREAD -lpthread -lz -D_DEBUG && ./tlink www.sina.com.cn / 2 &
#define DCON_TIMEOUT  20000000
#define DCON_BUF_SIZE 65536
#define DCON_STATUS_WAIT        0
#define DCON_STATUS_WORKING     1
#define DCON_STATUS_DISCARD     2
#define DCON_STATUS_ERROR       4
#define DCON_STATUS_OVER        8
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
static DOCMETA *metalist        = NULL;
static LOGGER *logger           = NULL;
static EVBASE *evbase           = NULL;
static DCON *conns              = NULL;
static int nconns               = 32;
static int running_conns        = 0;
static int conn_buf_size        = 2097152;
void ev_handler(int ev_fd, short flag, void *arg);
#define DCON_CLOSE(conn)                                                                    \
{                                                                                           \
    if(conn->fd > 0)                                                                        \
    {                                                                                       \
        conn->event->destroy(conn->event);                                                  \
        shutdown(conn->fd, SHUT_RD|SHUT_WR);                                                \
        close(conn->fd);                                                                    \
        conn->status = DCON_STATUS_WAIT;                                                    \
        running_conns--;                                                                    \
        conn->fd = -1;                                                                      \
    }                                                                                       \
}
#define DCON_DATA(conn)                                                                     \
{                                                                                           \
    conn->status = DCON_STATUS_OVER;                                                        \
    conn->req.status = LINK_STATUS_ERROR;                                                   \
    if(conn->content && conn->resp.respid == RESP_OK)                                       \
    {                                                                                       \
        *(conn->p) = '\0';                                                                  \
        if(linktable->add_content(linktable, &(conn->resp), conn->req.host,                 \
                    conn->req.path, conn->content, (conn->p - conn->content)) != 0)         \
        {                                                                                   \
            ERROR_LOGGER(linktable->logger, "Adding http://%s%s content failed, %s",        \
                    conn->req.host, conn->req.path, strerror(errno));                       \
        }                                                                                   \
        else                                                                                \
        {                                                                                   \
            conn->req.status = LINK_STATUS_OVER;                                            \
        }                                                                                   \
    }                                                                                       \
    linktable->update_request(linktable, conn->req.id, conn->req.status);                   \
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
                    ERROR_LOGGER(linktable->logger, "Invalid type[%s] req[%d] http://%s%s", \
                            conn->resp.headers[HEAD_ENT_CONTENT_TYPE],  conn->req.id,       \
                            conn->req.host, conn->req.path);                                \
                }                                                                           \
                if((conn->tp = conn->resp.headers[HEAD_ENT_CONTENT_LENGTH]))                \
                {                                                                           \
                    conn->data_size = atoi(conn->tp);                                       \
                    DEBUG_LOGGER(linktable->logger, "Ready for Reading %d bytes "           \
                        "[%d] [%s:%d] via %d [http://%s%s]", conn->data_size, conn->req.id, \
                conn->req.ip, conn->req.port, conn->fd, conn->req.host, conn->req.path);    \
                }                                                                           \
                break;                                                                      \
            }                                                                               \
            else conn->ps++;                                                                \
        }                                                                                   \
    }                                                                                       \
}
#define DCON_OVER(conn)                                                                     \
{                                                                                           \
    DCON_PACKET(conn);                                                                      \
    DCON_DATA(conn);                                                                        \
}
#define DCON_READ(conn)                                                                     \
{                                                                                           \
    if((conn->n = read(conn->fd, conn->p, conn->left)) > 0)                                 \
    {                                                                                       \
        conn->left -= conn->n;                                                              \
        conn->p += conn->n;                                                                 \
        DCON_PACKET(conn);                                                                  \
        if(conn->content && conn->data_size > 0                                             \
                && (conn->p - conn->content) >= conn->data_size)                            \
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
    if(conn->status == DCON_STATUS_WORKING)                                                 \
    {                                                                                       \
        conn->n = sprintf(conn->http_header, "GET %s HTTP/1.0\r\n"                          \
                "Host: %s\r\nConnection: close\r\n"                                         \
                "User-Agent: Mozilla\r\n\r\n", conn->req.path, conn->req.host);             \
        if((conn->n = write(conn->fd, conn->http_header, conn->n)) > 0)                     \
        {                                                                                   \
            conn->event->del(conn->event, E_WRITE);                                         \
            DEBUG_LOGGER(linktable->logger, "Wrote %d bytes request[%d] "                   \
                    "to http://%s%s] via %d",                                               \
                    conn->n, conn->req.id, conn->req.host, conn->req.path, conn->fd);       \
        }                                                                                   \
        else                                                                                \
        {                                                                                   \
            ERROR_LOGGER(linktable->logger,"Writting request[%d] to http://%s%s] "          \
                    "via %d failed, %s", conn->req.id, conn->req.host, conn->req.path,      \
                    conn->fd, strerror(errno));                                             \
            linktable->update_request(linktable, conn->req.id, LINK_STATUS_ERROR);          \
            conn->status = DCON_STATUS_ERROR;                                               \
        }                                                                                   \
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
            conn->fd = -1;                                                                  \
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
            ERROR_LOGGER(linktable->logger, "Connection[%s:%d] via %d [%d] [%s:%s] TIMEOUT",\
                conn->req.ip, conn->req.port, conn->fd, conn->req.id,                       \
                    conn->req.host, conn->req.path);                                        \
            DCON_OVER(conn);                                                                \
            DCON_CLOSE(conn);                                                               \
        }                                                                                   \
        conn = NULL;                                                                        \
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
        if(flag & E_READ)
        {
            DEBUG_LOGGER(linktable->logger, "Ready for reading [%d] [http://%s%s][%s:%d] via %d",
                    conn->req.id, conn->req.host, conn->req.path,
                    conn->req.ip, conn->req.port, conn->fd);
            DCON_READ(conn);
            DEBUG_LOGGER(linktable->logger, "Read over [%d] [%s:%d] via %d [http://%s%s] "
                    "data_size:%d content_size:%d left:%d ", conn->req.id, conn->req.ip,
                    conn->req.port, conn->fd, conn->req.host, conn->req.path,
                    conn->data_size, (conn->p - conn->content), conn->left);
        }
        if(flag & E_WRITE)
        {
            DCON_REQ(conn);
        }
        if(conn->status != DCON_STATUS_WORKING)
        {
            DEBUG_LOGGER(linktable->logger, "Ready close request[%d] connection[%s:%d] via %d",
                conn->req.id, conn->req.ip, conn->req.port, conn->fd);
            DCON_CLOSE(conn);
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
    long long total = 0;
    int i = 0;

    if((evbase = evbase_init()))
    {
        DCONS_INIT(i);
        while(1)
        {
            DCON_POP(conn, i);
            if(conn)
            {
                if(linktable->get_request(linktable, &request) != -1)
                {
                    DEBUG_LOGGER(linktable->logger, "New request[%d][http://%s%s] [%s:%d] "
                            "via conns[%d][%08x]", request.id, request.host, request.path,
                            request.ip, request.port, i, conn);
                    NEW_DCON(conn, request);
                }
                else
                {
                    DCON_FREE(conn);
                }
            }
            //fprintf(stdout, "running_conns:%d\n", running_conns);
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
                        "urlno:%d urlok:%d urltotal:%d docno:%d docok:%d "
                        "doctotal:%d size:%lld zsize:%lld\n",
                        linktable->urlno, linktable->urlok_total, linktable->url_total,
                        linktable->docno, linktable->docok_total, 
                        linktable->doc_total, linktable->size, linktable->zsize);
            }
            else 
            {
                /*
                ERROR_LOGGER(linktable->logger, 
                        "urlno:%d urlok:%d urltotal:%d docno:%d docok:%d doctotal:%d\n",
                        linktable->urlno, linktable->urlok_total, linktable->url_total,
                        linktable->docno, linktable->docok_total, linktable->doc_total);
                */
            }
            usleep(100);
        }
    }
}
