#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <locale.h>
#include <sbase.h>
#include <iconv.h>
#include <chardet.h>
#include "http.h"
#include "ltable.h"
#include "iniparser.h"
#include "queue.h"
#include "zstream.h"
#include "logger.h"
#define CHARSET_MAX 256
typedef struct _TASK
{
    CONN *s_conn;
    CONN *c_conn;
    int taskid;
    int  state;
    char request[HTTP_BUF_SIZE];
    int nrequest;
    int is_new_host;
    char host[DNS_NAME_MAX];
    char ip[DNS_IP_MAX];
}TASK;
static SBASE *sbase = NULL;
static SERVICE *service = NULL;
static dictionary *dict = NULL;
static TASK *tasklist = NULL;
static int ntask = 0;
static char *server_ip = NULL;
static int server_port = 0;
static void *taskqueue = NULL;
static void *logger = NULL;
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

    if(c_id >= 0 && c_id < ntask && tasklist[c_id].s_conn)
    {
        if(tasklist[c_id].is_new_host)
        {
            n = sprintf(buf, "TASK %d HTTP/1.0\r\nHost: %s\r\n Server:%s\r\n\r\n", 
                    tasklist[c_id].taskid, tasklist[c_id].host, tasklist[c_id].ip);
        }
        else
        {
            n = sprintf(buf, "TASK %d HTTP/1.0\r\n\r\n", tasklist[c_id].taskid);
        }
        tasklist[c_id].is_new_host = 0;
        return tasklist[c_id].s_conn->push_chunk(tasklist[c_id].s_conn, buf, n);
    }
    return -1;
}

int hispider_packet_reader(CONN *conn, CB_DATA *buffer)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

int hispider_packet_handler(CONN *conn, CB_DATA *packet)
{
    char *p = NULL, *end = NULL, *ip = NULL, *host = NULL, *path = NULL;
    HTTP_RESPONSE http_resp = {0};
    int taskid = 0, n = 0;
    int c_id = 0, port = 0;
    struct hostent *hp = NULL;

    if(conn && tasklist && (c_id = conn->c_id) >= 0 && c_id < ntask)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        /* http handler */        
        if(conn == tasklist[c_id].c_conn)
        {
            if(p == NULL || http_response_parse(p, end, &http_resp) == -1)
            {
                conn->over_cstate(conn);
                conn->over(conn);
                return http_download_error(c_id);
            }
            if(http_resp.respid == RESP_OK && http_resp.headers[HEAD_ENT_CONTENT_TYPE] 
                    && strncasecmp(http_resp.headers[HEAD_ENT_CONTENT_TYPE], "text", 4) == 0)
            {
                conn->save_cache(conn, &http_resp, sizeof(HTTP_RESPONSE));
                if((p = http_resp.headers[HEAD_ENT_CONTENT_LENGTH]) && (n = atol(p)) > 0)
                {
                    conn->recv_chunk(conn, n);
                }
                else
                {
                    conn->set_timeout(conn, HTTP_DOWNLOAD_TIMEOUT);
                    conn->recv_chunk(conn, HTML_MAX_SIZE);
                }
            }
            else
            {
                conn->over_cstate(conn);
                conn->close(conn);
                return http_download_error(c_id);
            }
        }
        /* task handler */
        else if(conn == tasklist[c_id].s_conn)
        {
            if(p == NULL || http_response_parse(p, end, &http_resp) == -1)
            {
                conn->over_cstate(conn);
                conn->over(conn);
                QUEUE_PUSH(taskqueue, int, &c_id);
                tasklist[c_id].s_conn = NULL;
                return -1;
            }
            if(http_resp.respid == RESP_OK)
            {
                host = http_resp.headers[HEAD_REQ_HOST];
                ip = http_resp.headers[HEAD_RESP_SERVER];
                port = (http_resp.headers[HEAD_REQ_REFERER])
                    ? atoi(http_resp.headers[HEAD_REQ_REFERER]) : 0;
                path = http_resp.headers[HEAD_RESP_LOCATION];
                taskid = tasklist[c_id].taskid = (http_resp.headers[HEAD_REQ_FROM])
                    ? atoi(http_resp.headers[HEAD_REQ_FROM]) : 0;
                //fprintf(stdout, "%s::%d OK host:%s ip:%s port:%d path:%s taskid:%d \n", 
                //        __FILE__, __LINE__, host, ip, port, path, taskid);
                if(host == NULL || ip == NULL || path == NULL || port == 0 || taskid < 0) 
                    goto restart_task;
                if(strcmp(ip, "255.255.255.255") == 0)
                {
                    if((hp = gethostbyname(host)))
                    {
                        tasklist[c_id].is_new_host = 1;
                        strcpy(tasklist[c_id].host, host);
                        sprintf(tasklist[c_id].ip, "%s", 
                                inet_ntoa(*((struct in_addr *)(hp->h_addr))));
                        ip = tasklist[c_id].ip;
                    }
                    else
                    {
                        DEBUG_LOGGER(logger, "Resolving name[%s] failed, %s", 
                                host, strerror(h_errno));
                        goto restart_task;
                    }
                }
                if((tasklist[c_id].c_conn = service->newconn(service, -1, -1, ip, port, NULL)))
                {
                    tasklist[c_id].nrequest = sprintf(tasklist[c_id].request, 
                        "GET %s HTTP/1.0\r\nHost: %s\r\n"
                        "User-Agent: %s\r\nAccept: %s\r\nAccept-Language: %s\r\n"
                        //"Accept-Encoding: %\r\nAccept-Charset: %s\r\nConnection: close\r\n\r\n", 
                        "Accept-Charset: %s\r\nConnection: close\r\n\r\n", 
                        path, host, USER_AGENT, ACCEPT_TYPE, ACCEPT_LANGUAGE, ACCEPT_CHARSET);
                        //ACCEPT_ENCODING, ACCEPT_CHARSET);
                    tasklist[c_id].c_conn->c_id = c_id;
                    tasklist[c_id].c_conn->start_cstate(tasklist[c_id].c_conn);
                    return service->newtransaction(service, tasklist[c_id].c_conn, c_id);
                }
                else
                {
                    ERROR_LOGGER(logger, "Connect to [%s][%s:%d] failed, %s\n", 
                            host, ip, port, strerror(errno));
                }
            }
            restart_task:
                return http_download_error(c_id);
        }
        return -1;
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
                tasklist[c_id].c_conn = NULL;
                conn->over_cstate(conn);
                conn->over(conn);
                return http_download_error(c_id);
            }
        }
        else if(conn == tasklist[c_id].s_conn)
        {
            ERROR_LOGGER(logger, "error_handler(%08x) on remote[%s:%d] local[%s:%d] ", (int)conn, 
                    conn->remote_ip, conn->remote_port, conn->local_ip, conn->local_port);
            QUEUE_PUSH(taskqueue, int, &c_id);
            tasklist[c_id].s_conn = NULL;
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
        //fprintf(stdout, "%s::%d OK chunk[%d]\n", __FILE__, __LINE__, packet->ndata);
        if(conn == tasklist[c_id].c_conn && packet && cache && chunk) 
        {
            if(packet->ndata > 0 && cache->ndata > 0 && chunk->ndata > 0)
            {
                return hispider_data_handler(conn, packet, cache, chunk);
            }
            else
            {
                tasklist[c_id].state = TASK_STATE_ERROR;
                tasklist[c_id].c_conn = NULL;
                conn->over_cstate(conn);
                conn->over(conn);
                return http_download_error(c_id);
            }
        }
        else
        {
            QUEUE_PUSH(taskqueue, int, &c_id);
            tasklist[c_id].s_conn = NULL;
        }
    }
    return -1;
}

/* transaction handler */
int hispider_trans_handler(CONN *conn, int tid)
{
    char buf[HTTP_BUF_SIZE];
    int n = 0;

    if(conn && tid >= 0 && tid < ntask)
    {
        if(conn == tasklist[tid].c_conn)
        {
            //fprintf(stdout, "%s::%d OK nrequest:%d\n", __FILE__, __LINE__, tasklist[tid].nrequest);
            conn->set_timeout(conn, HTTP_DOWNLOAD_TIMEOUT);
            return conn->push_chunk(conn, tasklist[tid].request, tasklist[tid].nrequest);      
        }
        else if(conn == tasklist[tid].s_conn)
        {
            n = sprintf(buf, "TASK %d HTTP/1.0\r\n\r\n", -1);
            //fprintf(stdout, "%s::%d OK %s\n", __FILE__, __LINE__, buf);
            return conn->push_chunk(conn, buf, n);
        }
        return 0;
    }
    return -1;
}

/* data handler */
int hispider_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    CONN *s_conn = NULL;
    HTTP_RESPONSE *http_resp = NULL;
    char buf[HTTP_BUF_SIZE], charset[CHARSET_MAX], *zdata = NULL, 
         *p = NULL, *ps = NULL, *outbuf = NULL;
    int  ret = -1, c_id = 0, n = 0, nzdata = 0, is_gzip = 0, is_need_convert = 0;
    size_t ninbuf = 0, noutbuf = 0;
    chardet_t pdet = NULL;
    iconv_t cd = NULL;

    if(conn && (c_id = conn->c_id) >= 0 && c_id < ntask)
    {
        if(conn == tasklist[c_id].c_conn && chunk && chunk->data && chunk->ndata > 0)
        {
            http_resp = (HTTP_RESPONSE *)cache->data;
            if((p = http_resp->headers[HEAD_ENT_CONTENT_ENCODING]) 
                    && strncasecmp(p, "gzip", 4) == 0)
            {
                zdata = chunk->data;
                nzdata = chunk->ndata;
                is_gzip = 1;
            }
            else
            {
                if(chardet_create(&pdet) == 0)
                {
                    if(chardet_handle_data(pdet, chunk->data, chunk->ndata) == 0 
                        && chardet_data_end(pdet) == 0 )
                    {
                        chardet_get_charset(pdet, charset, CHARSET_MAX);
                        if(memcmp(charset, "UTF-8", 5) != 0) is_need_convert = 1;
                    }
                    chardet_destroy(pdet);
                }
                if(is_need_convert && (cd = iconv_open("UTF-8", charset)) != (iconv_t)-1)
                {
                    p = chunk->data;
                    ninbuf = chunk->ndata;
                    n = noutbuf = ninbuf * 4;
                    if((ps = outbuf = calloc(1, noutbuf)))
                    {
                        if(iconv(cd, &p, &ninbuf, &ps, (size_t *)&n) == -1)
                        {
                            free(outbuf);
                            outbuf = NULL;
                        }
                        else
                        {
                            noutbuf -= n;
                        }
                    }
                    iconv_close(cd);
                }
                if(outbuf && noutbuf > 0)
                {
                    nzdata = noutbuf + Z_HEADER_SIZE;
                    p = outbuf;
                    n = noutbuf;
                }
                else
                {
                    nzdata = chunk->ndata + Z_HEADER_SIZE;
                    p = chunk->data;
                    n = chunk->ndata;
                }
                if((zdata = (char *)calloc(1, nzdata)))
                {
                    zcompress((Bytef *)p, n, (Bytef *)zdata, (uLong * )&(nzdata));
                }
                if(outbuf) {free(outbuf); outbuf = NULL;}
            }
            if(zdata && nzdata > 0)
            {
                p = buf;
                p += sprintf(p, "TASK %d HTTP/1.0\r\n", tasklist[c_id].taskid);
                if((ps = http_resp->headers[HEAD_ENT_LAST_MODIFIED]))
                {
                    p += sprintf(p, "Last-Modified: %s\r\n", ps);
                }
                if(tasklist[c_id].is_new_host)
                {
                    p += sprintf(p, "Host: %s\r\nServer: %s\r\n", 
                            tasklist[c_id].host, tasklist[c_id].ip);
                }
                p += sprintf(p, "Content-Length: %d\r\n\r\n", nzdata);
                tasklist[c_id].is_new_host = 0;
                if((s_conn = tasklist[c_id].s_conn) && (n = p - buf) > 0)
                {
                    //fprintf(stdout, "Over task[%ld]\n", tasklist[c_id].taskid);
                    s_conn->push_chunk(s_conn, buf, n);
                    s_conn->push_chunk(s_conn, zdata, nzdata);
                    if(is_gzip == 0 && zdata) {free(zdata);  zdata = NULL;}
                    tasklist[c_id].taskid = -1;
                    tasklist[c_id].c_conn = NULL;
                }
                ret = 0;
                goto end;
            }
            else goto err_end;
        }
err_end:
        http_download_error(c_id);
end:    
        conn->over_cstate(conn);
        conn->close(conn);
    }
    return ret;
}

int hispider_oob_handler(CONN *conn, CB_DATA *oob)
{
    if(conn)
    {
        return 0;
    }
    return -1;
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
                    tasklist[id].s_conn->start_cstate(tasklist[id].s_conn);
                    service->newtransaction(service, tasklist[id].s_conn, id);
                }
                else
                {
                    ERROR_LOGGER(logger, "Connect to %s:%d failed, %s", 
                            server_ip, server_port, strerror(errno));
                    QUEUE_PUSH(taskqueue, int, &id);
                    break;
                }
            }
        }
    }
    return ;
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
    char *s = NULL, *p = NULL;
    int i = 0, interval = 0;
    if((dict = iniparser_new(conf)) == NULL)
    {
        fprintf(stderr, "Initializing conf:%s failed, %s\n", conf, strerror(errno));
        _exit(-1);
    }
    /* SBASE */
    sbase->nchilds = iniparser_getint(dict, "SBASE:nchilds", 0);
    sbase->connections_limit = iniparser_getint(dict, "SBASE:connections_limit", SB_CONN_MAX);
    setrlimiter("RLIMIT_NOFILE", RLIMIT_NOFILE, sbase->connections_limit);
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
    ntask = iniparser_getint(dict, "HISPIDER:ntask", 64);
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
    LOGGER_INIT(logger, iniparser_getstr(dict, "HISPIDER:access_log"));
    fprintf(stdout, "Parsing for server...\n");
    return sbase->add_service(sbase, service);
}

static void hispider_stop(int sig){
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            fprintf(stderr, "hispider is interrupted by user.\n");
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
    return 0;
}
