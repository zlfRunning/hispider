#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/resource.h>
#include <locale.h>
#include <sbase.h>
#include "http.h"
#include "ltable.h"
#include "iniparser.h"
#include "queue.h"
#include "zstream.h"
typedef struct _TASK
{
    CONN *s_conn;
    CONN *c_conn;
    long taskid;
    int  state;
    char request[HTTP_BUF_SIZE];
    int nrequest;
}TASK;
static SBASE *sbase = NULL;
static SERVICE *service = NULL;
static dictionary *dict = NULL;
static TASK *tasklist = NULL;
static int ntask = 0;
static char *server_ip = NULL;
static int server_port = 0;
static void *taskqueue = NULL;
/* data handler */
int hispider_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk);
int hispider_error_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk);
int hispider_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk);
int hispider_trans_handler(CONN *conn, int tid);

/* http download error */
int http_download_error(int c_id)
{
    char buf[HTTP_BUF_SIZE];
    int n = 0;

    if(c_id >= 0 && c_id < ntask)
    {
        n = sprintf(buf, "TASK %ld HTTP/1.0\r\n\r\n", tasklist[c_id].taskid);
        return tasklist[c_id].s_conn->push_chunk(tasklist[c_id].s_conn, buf, n);
    }
    return -1;
}

int hispider_packet_reader(CONN *conn, CB_DATA *buffer)
{

}

int hispider_packet_handler(CONN *conn, CB_DATA *packet)
{
    char *p = NULL, *end = NULL, buf[HTTP_BUF_SIZE], *ip = NULL, *host = NULL, *path = NULL;
    HTTP_RESPONSE http_resp = {0};
    long taskid = 0, n = 0;
    int c_id = 0, port = 0;

    if(conn && tasklist && (c_id = conn->c_id) >= 0 && c_id < ntask)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        if(p == NULL || http_response_parse(p, end, &http_resp) == -1) goto err_end;
        /* http handler */        
        if(conn == tasklist[c_id].c_conn)
        {
            if(http_resp.respid == RESP_OK && http_resp.headers[HEAD_ENT_CONTENT_TYPE] 
                    && strncasecmp(http_resp.headers[HEAD_ENT_CONTENT_TYPE], "text", 4) == 0)
            {
                conn->save_cache(conn->cache, &http_resp, sizeof(HTTP_RESPONSE));
                if(http_resp.headers[HEAD_ENT_CONTENT_LENGTH] 
                    && (n = atol(http_resp.headers[HEAD_ENT_CONTENT_LENGTH])) > 0)
                {
                    conn->recv_chunk(conn, n);
                }
                else
                {
                    conn->recv_chunk(conn, HTML_MAX_SIZE);
                }
            }
            else
            {
                conn->over_cstate(conn);
                conn->over(conn);
                http_download_error(c_id);
            }
        }
        /* task handler */
        else if(conn == tasklist[c_id].s_conn)
        {
           host = http_resp.headers[HEAD_REQ_HOST];
           ip = http_resp.headers[HEAD_RESP_SERVER];
           port = atoi(http_resp.headers[HEAD_REQ_REFERER]);
           path = http_resp.headers[HEAD_RESP_LOCATION];
           taskid = atol(http_resp.headers[HEAD_REQ_FROM]);
           if(host == NULL || ip == NULL || path == NULL || port == 0 || taskid <= 0) goto err_end;
           if((tasklist[c_id].c_conn = service->newconn(service, -1, -1, ip, port, NULL)))
           {
               tasklist[c_id].taskid = taskid;
               tasklist[c_id].nrequest = sprintf(tasklist[c_id].request, 
                       "GET %s HTTP/1.0\r\nHost: %s\r\n"
                       "User-Agent: %s\r\nAccept: %s\r\nAccept-Language: %s\r\n"
                       "Accept-Encoding: %s\r\nAccept-Charset: %s\r\nConnection: close\r\n\r\n", 
                       path, host, USER_AGENT, ACCEPT_TYPE, ACCEPT_LANGUAGE, 
                       ACCEPT_ENCODING, ACCEPT_CHARSET);
               tasklist[c_id].c_conn->c_id = c_id;
               tasklist[c_id].c_conn->start_cstate(tasklist[c_id].c_conn);
               service->newtransaction(service, tasklist[c_id].c_conn, c_id);
           }
           conn->over_cstate(conn);
        }
        return 0;
err_end:
        if(conn == tasklist[c_id].c_conn)
        {
            conn->over_cstate(conn);
            conn->over(conn);
            http_download_error(c_id);
        }
        else
        {
            conn->over_cstate(conn);
            QUEUE_PUSH(taskqueue, int, &c_id);
        }
    }
    return -1;
}

/* error handler */
int hispider_error_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    int c_id = 0;

    if(conn && (c_id = conn->c_id) >= 0 && c_id < ntask)
    {
        if(conn == tasklist[c_id].c_conn && packet && cache && chunk) 
        {
            
            if(packet->ndata > 0 && cache->ndata > 0 && chunk->ndata > 0)
            {
                return hispider_data_handler(conn, packet, cache, chunk);
            }
            else
            {
                tasklist[c_id].state = TASK_STATE_ERROR;
                return http_download_error(c_id);
            }
        }
        else
        {
            QUEUE_PUSH(taskqueue, int, &c_id);
        }
    }
    return -1;
}

/* timeout handler */
int hispider_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    int c_id = 0;

    if(conn && (c_id = conn->c_id) >= 0 && c_id < ntask)
    {
        if(conn == tasklist[c_id].c_conn && packet && cache && chunk) 
        {
            if(packet->ndata > 0 && cache->ndata > 0 && chunk->ndata > 0)
            {
                return hispider_data_handler(conn, packet, cache, chunk);
            }
            else
            {
                tasklist[c_id].state = TASK_STATE_ERROR;
                return http_download_error(c_id);
            }
        }
        else
        {
            QUEUE_PUSH(taskqueue, int, &c_id);
        }
    }
    return -1;
}

/* transaction handler */
int hispider_trans_handler(CONN *conn, int tid)
{
    char buf[HTTP_BUF_SIZE], *p = NULL;
    int n = 0;

    if(conn && tid >= 0 && tid < ntask)
    {
        if(conn == tasklist[tid].c_conn)
        {
            conn->set_timeout(conn, HTTP_DOWNLOAD_TIMEOUT);
            conn->push_chunk(conn, tasklist[tid].request, tasklist[tid].nrequest);      
        }
        else if(conn == tasklist[tid].s_conn)
        {
            p = buf;
            p += sprintf(p, "TASK %ld HTTP/1.0\r\n\r\n", -1);
            conn->push_chunk(conn, buf, (p - buf));
        }
        return 0;
    }
    return -1;
}

/* data handler */
int hispider_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    HTTP_RESPONSE *http_resp = NULL;
    char buf[HTTP_BUF_SIZE], *zdata = NULL;
    int c_id = 0, n = 0, nzdata = 0, is_gzip = 0;

    if(conn && (c_id = conn->c_id) >= 0 && c_id < ntask)
    {
        if(chunk && chunk->data && chunk->ndata > 0)
        {
            http_resp = (HTTP_RESPONSE *)cache->data;
            if(http_resp->headers[HEAD_ENT_CONTENT_ENCODING] 
                    && strncasecmp(http_resp->headers[HEAD_ENT_CONTENT_ENCODING], "gzip", 4) == 0)
            {
                zdata = chunk->data;
                nzdata = chunk->ndata;
                is_gzip = 1;
            }
            else
            {
                nzdata = chunk->ndata + Z_HEADER_SIZE;
                if((zdata = (char *)calloc(1, nzdata)))
                    zcompress((Bytef *)chunk->data, chunk->ndata, 
                            (Bytef *)zdata, (uLong * )&(nzdata));
            }
            if(nzdata > 0)
            {
                n = sprintf(buf, "TASK %ld HTTP/1.0\r\nLast-Modified: %s\r\n"
                        "Content-Length: %ld\r\n\r\n",tasklist[c_id].taskid,
                        http_resp->headers[HEAD_ENT_LAST_MODIFIED], nzdata);
                conn->push_chunk(conn, buf, n);
                conn->push_chunk(conn, zdata, nzdata);
                if(is_gzip == 0 && zdata) free(zdata); 
                return 0;
            }
        }
err_end:
        http_download_error(c_id);
    }
    return -1;
}

int hispider_oob_handler(CONN *conn, CB_DATA *oob)
{
}

/* heartbeat */
void cb_heartbeat_handler(void *arg)
{
    int id = 0;

    if(arg == (void *)service)
    {
        while(QTOTAL(taskqueue) > 0)
        {
            id = -1;
            QUEUE_POP(taskqueue, int, &id);
            if(id >= 0 && id < ntask)
            {
                if((tasklist[id].s_conn = service->newconn(service, 
                                -1, -1, server_ip, server_port, NULL)))
                {
                    tasklist[id].s_conn->c_id = id;
                    service->newtransaction(service, tasklist[id].s_conn, id);
                }
            }
        }
    }
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
    char *logfile = NULL, *s = NULL, *p = NULL;
    int n = 0, i = 0, interval = 0;
    if((dict = iniparser_new(conf)) == NULL)
    {
        fprintf(stderr, "Initializing conf:%s failed, %s\n", conf, strerror(errno));
        _exit(-1);
    }
    /* SBASE */
    sbase->nchilds = iniparser_getint(dict, "SBASE:nchilds", 0);
    sbase->connections_limit = iniparser_getint(dict, "SBASE:connections_limit", SB_CONN_MAX);
    sbase->usec_sleep = iniparser_getint(dict, "SBASE:usec_sleep", SB_USEC_SLEEP);
    sbase->set_log(sbase, iniparser_getstr(dict, "SBASE:logfile"));
    sbase->set_evlog(sbase, iniparser_getstr(dict, "SBASE:evlogfile"));
    /* HISPIDER */
    if((service = service_init()) == NULL)
    {
        fprintf(stderr, "Initialize service failed, %s", strerror(errno));
        _exit(-1);
    }
    service->family = iniparser_getint(dict, "HISPIDER:inet_family", AF_INET);
    service->sock_type = iniparser_getint(dict, "HISPIDER:socket_type", SOCK_STREAM);
    server_ip = service->ip = iniparser_getstr(dict, "HISPIDER:service_ip");
    server_port = service->port = iniparser_getint(dict, "HISPIDER:service_port", 3721);
    service->working_mode = iniparser_getint(dict, "HISPIDER:working_mode", WORKING_PROC);
    service->service_type = iniparser_getint(dict, "HISPIDER:service_type", C_SERVICE);
    service->service_name = iniparser_getstr(dict, "HISPIDER:service_name");
    service->nprocthreads = iniparser_getint(dict, "HISPIDER:nprocthreads", 1);
    service->ndaemons = iniparser_getint(dict, "HISPIDER:ndaemons", 0);
    service->set_log(service, iniparser_getstr(dict, "HISPIDER:logfile"));
    service->session.packet_type = iniparser_getint(dict, "HISPIDER:packet_type",PACKET_DELIMITER);
    service->session.packet_delimiter = iniparser_getstr(dict, "HISPIDER:packet_delimiter");
    p = s = service->session.packet_delimiter;
    while(*p != 0 )
    {
        if(*p == '\\' && *(p+1) == 'n')
        {
            *s++ = '\n';
            p += 2;
        }
        else if (*p == '\\' && *(p+1) == 'r')
        {
            *s++ = '\r';
            p += 2;
        }
        else
            *s++ = *p++;
    }
    *s++ = 0;
    service->session.packet_delimiter_length = strlen(service->session.packet_delimiter);
    service->session.buffer_size = iniparser_getint(dict, "HISPIDER:buffer_size", SB_BUF_SIZE);
    service->session.packet_reader = &hispider_packet_reader;
    service->session.packet_handler = &hispider_packet_handler;
    service->session.data_handler = &hispider_data_handler;
    service->session.error_handler = &hispider_error_handler;
    service->session.timeout_handler = &hispider_timeout_handler;
    service->session.transaction_handler = &hispider_trans_handler;
    service->session.oob_handler = &hispider_oob_handler;
    interval = iniparser_getint(dict, "HISPIDER:heartbeat_interval", SB_HEARTBEAT_INTERVAL);
    service->set_heartbeat(service, interval, &cb_heartbeat_handler, service);
    /* server */
    ntask = iniparser_getint(dict, "HISPIDER:ntask", 128);
    if(ntask <= 0)
    {
        fprintf(stderr, "[ntask] is invalid...\n");
        _exit(-1);
    }
    tasklist = (TASK *)calloc(ntask, sizeof(TASK));
    QUEUE_INIT(taskqueue);
    for(i = 0; i < ntask; i++)
    {
        QUEUE_PUSH(taskqueue, int, &i);
    }
    fprintf(stdout, "Parsing for server...\n");
    return sbase->add_service(sbase, service);
}

static void hispider_stop(int sig){
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            fprintf(stderr, "lhttpd server is interrupted by user.\n");
            if(sbase)sbase->stop(sbase);
            break;
        default:
            break;
    }
}

int main(int argc, char **argv)
{
    pid_t pid;
    char *conf = NULL;

    /* get configure file */
    if(getopt(argc, argv, "c:") != 'c')
    {
        fprintf(stderr, "Usage:%s -c config_file\n", argv[0]);
        _exit(-1);
    }
    conf = optarg;
    /* locale */
    setlocale(LC_ALL, "C");
    /* signal */
    signal(SIGTERM, &hispider_stop);
    signal(SIGINT,  &hispider_stop);
    signal(SIGHUP,  &hispider_stop);
    signal(SIGPIPE, SIG_IGN);
    pid = fork();
    switch (pid) {
        case -1:
            perror("fork()");
            exit(EXIT_FAILURE);
            break;
        case 0: /* child process */
            if(setsid() == -1)
                exit(EXIT_FAILURE);
            break;
        default:/* parent */
            _exit(EXIT_SUCCESS);
            break;
    }
    /*setrlimiter("RLIMIT_NOFILE", RLIMIT_NOFILE, 65536)*/
    if((sbase = sbase_init()) == NULL)
    {
        exit(EXIT_FAILURE);
        return -1;
    }
    fprintf(stdout, "Initializing from configure file:%s\n", conf);
    /* Initialize sbase */
    if(sbase_initialize(sbase, conf) != 0 )
    {
        fprintf(stderr, "Initialize from configure file failed\n");
        return -1;
    }
    fprintf(stdout, "Initialized successed\n");
    sbase->running(sbase, 0);
    //sbase->running(sbase, 3600);
    //sbase->running(sbase, 60000000);
    sbase->stop(sbase);
    sbase->clean(&sbase);
    if(dict)iniparser_free(dict);
}
