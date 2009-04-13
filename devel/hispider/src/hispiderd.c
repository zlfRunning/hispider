#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/resource.h>
#include <locale.h>
#include <sbase.h>
#include <arpa/inet.h>
#include "http.h"
#include "ltable.h"
#include "iniparser.h"
#include "evdns.h"
#include "queue.h"
#include "logger.h"
typedef struct _DNSTASK
{
    CONN *conn;
    char nameserver[DNS_IP_MAX];
}DNSTASK;
static SBASE *sbase = NULL;
static SERVICE *service = NULL;
static SERVICE *adnservice = NULL;
static dictionary *dict = NULL;
static LTABLE *ltable = NULL;
static void *dnsqueue = NULL;
static void *logger = NULL;
//static void *undnsqueue = NULL;
static DNSTASK tasklist[DNS_TASK_MAX];
//static void *waitqueue = NULL;

/* dns packet reader */
int adns_packet_reader(CONN *conn, CB_DATA *buffer)
{
    HOSTENT hostent = {0};
    unsigned char *p = NULL;
    int tid = 0, n = 0, left = 0, ip  = 0;

    if(conn && (tid = conn->c_id) >= 0 && tid < DNS_TASK_MAX 
            && tasklist[tid].conn == conn && buffer->ndata > 0 && buffer->data)
    {
        p = (unsigned char *)buffer->data;
        left = buffer->ndata;
        while((n = evdns_parse_reply(p, left, &hostent)) > 0)
        {
            ip = DNS_SELF_IP;
            if(hostent.naddrs > 0)
            {
                ip = hostent.addrs[0];
                p = (unsigned char *)&ip;
                DEBUG_LOGGER(logger, "Got host[%s]'s ip[%d.%d.%d.%d] from %s:%d", 
                        hostent.name, p[0], p[1], p[2], p[3], conn->remote_ip, conn->remote_port);
            }
            ltable->set_dns(ltable, hostent.name, ip);
            left -= n;
            memset(&hostent, 0, sizeof(HOSTENT));
        }
        return (buffer->ndata - left);
    }
    return -1;
}

/* dns packet handler */
int adns_packet_handler(CONN *conn, CB_DATA *packet)
{
    int tid = 0;

    if(conn && (tid = conn->c_id) >= 0 && tid < DNS_TASK_MAX 
            && tasklist[tid].conn == conn && packet->ndata > 0 && packet->data)
    {
        tasklist[tid].conn->c_id = tid;
        return adnservice->newtransaction(adnservice, tasklist[tid].conn, tid);
    }
    else 
    {
        conn->over_cstate(conn);
        conn->over(conn);
        tasklist[tid].conn = NULL;
        QUEUE_PUSH(dnsqueue, int, &tid);
    }
    return -1;
}

/* adns error handler */
int adns_error_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    int tid = 0;

    if(conn && (tid = conn->c_id) >= 0 && tid < DNS_TASK_MAX && tasklist[tid].conn == conn)
    {
        conn->over_cstate(conn);
        conn->over(conn);
        tasklist[tid].conn = NULL;
        QUEUE_PUSH(dnsqueue, int, &tid);
    }
    return -1;
}

/* adns timeout handler */
int adns_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    int tid = 0;

    if(conn && (tid = conn->c_id) >= 0 && tid < DNS_TASK_MAX && tasklist[tid].conn == conn)
    {
        return adnservice->newtransaction(adnservice, tasklist[tid].conn, tid);
    }
    return -1;
}

/* adns transaction handler */
int adns_trans_handler(CONN *conn, int tid)
{
    unsigned char hostname[DNS_NAME_MAX], buf[HTTP_BUF_SIZE];
    int qid = 0, n = 0;

    if(conn && tid >= 0 && tid < DNS_TASK_MAX && tasklist[tid].conn == conn)
    {
        memset(hostname, 0, DNS_NAME_MAX);
        conn->c_id = tid;
        conn->start_cstate(conn);
        conn->set_timeout(conn, EVDNS_TIMEOUT);
        DEBUG_LOGGER(logger, "Ready for resolving dns on remote[%s:%d] local[%s:%d]", 
                conn->remote_ip, conn->remote_port, conn->local_ip, conn->local_port);
        if((qid = ltable->new_dnstask(ltable, (char *)hostname)) >= 0)
        {
            qid %= 65536;
            n = evdns_make_query((char *)hostname, 1, 1, (unsigned short)qid, 1, buf); 
            DEBUG_LOGGER(logger, "Resolving %s from nameserver[%s]", 
                    hostname, tasklist[tid].nameserver);
            //fprintf(stdout, "%d::trans:%d taskid:[%d][%d]\n", __LINE__, tid, taskid, n);
            return conn->push_chunk(conn, buf, n);
        }
    }
    return -1;
}

/* heartbeat handler */
void adns_heartbeat_handler(void *arg)
{
    int id = 0, total = 0;

    if(arg == (void *)adnservice)
    {
        total = QTOTAL(dnsqueue);
        while(total-- > 0)
        {
            id = -1;
            QUEUE_POP(dnsqueue, int, &id);
            if(id >= 0 && id < DNS_TASK_MAX)
            {
                if((tasklist[id].conn = adnservice->newconn(adnservice, -1, 
                    SOCK_DGRAM, tasklist[id].nameserver, DNS_DEFAULT_PORT, NULL)))
                {
                    tasklist[id].conn->c_id = id;
                    tasklist[id].conn->start_cstate(tasklist[id].conn);
                    adnservice->newtransaction(adnservice, tasklist[id].conn, id);
                }
                else
                {
                    QUEUE_PUSH(dnsqueue, int, &id);
                }
            }
        }
    }
}

/* hispiderd packet reader */
int hispiderd_packet_reader(CONN *conn, CB_DATA *buffer)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

/* packet handler */
int hispiderd_packet_handler(CONN *conn, CB_DATA *packet)
{
    char *p = NULL, *end = NULL, buf[HTTP_BUF_SIZE];
    HTTP_REQ http_req = {0};
    int taskid = 0, n = 0;

    if(conn)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        int fd = 0;
        if((fd = open("/tmp/header.txt", O_CREAT|O_RDWR|O_TRUNC, 0644)) > 0)
        {
            write(fd, packet->data, packet->ndata);
            close(fd);
        }
        if(http_request_parse(p, end, &http_req) == -1) goto err_end;
        if(http_req.reqid == HTTP_GET)
        {
            if(strncasecmp(http_req.path, "/stop", 5) == 0)
            {
                ltable->set_state(ltable, 0);
            }
            else if(strncasecmp(http_req.path, "/running", 8) == 0)
            {
                ltable->set_state(ltable, 1);
            }
            if((n = ltable->get_stateinfo(ltable, buf)))
            {
                conn->push_chunk(conn, buf, n);
            }
            else
            {
                goto err_end;
            }
        }
        else if(http_req.reqid == HTTP_POST)
        {
            if((p = http_req.headers[HEAD_ENT_CONTENT_LENGTH]) && (n = atol(p)) > 0)
            {
                conn->save_cache(conn, &http_req, sizeof(HTTP_REQ));
                conn->recv_file(conn, "/tmp/recv.txt", 0, n);
            }
        }
        else if(http_req.reqid == HTTP_TASK)
        {
            taskid = atoi(http_req.path);
            //fprintf(stdout, "%s::%d TASK: %ld path:%s\n", 
            //__FILE__, __LINE__, taskid, http_req.path);
            if(taskid != -1)
            {
                if((p = http_req.headers[HEAD_ENT_CONTENT_LENGTH]) && (n = atol(p)) > 0)
                {
                    conn->save_cache(conn, &http_req, sizeof(HTTP_REQ));
                    conn->recv_chunk(conn, n);
                }
                else
                {
                    ltable->set_task_state(ltable, taskid, TASK_STATE_ERROR);
                }
            }
            /* get new task */
            if(ltable->get_task(ltable, buf, &n) >= 0) 
            {
                return conn->push_chunk(conn, buf, n);
            }
            else
            {
                if(conn->timeout >= TASK_WAIT_MAX) conn->timeout = 0;
                //DEBUG_LOGGER(logger, "set_timeout(%d) on %s:%d", 
                //    conn->timeout + TASK_WAIT_TIMEOUT, conn->remote_ip, conn->remote_port);
                conn->wait_evstate(conn);
                return conn->set_timeout(conn, conn->timeout + TASK_WAIT_TIMEOUT);
            }
        }
        else goto err_end;
        return 0;
    }
err_end:
    conn->push_chunk(conn, HTTP_BAD_REQUEST, strlen(HTTP_BAD_REQUEST));
    return -1;
}

/*  data handler */
int hispiderd_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    HTTP_REQ *http_req = NULL;
    int taskid = 0;
    char *host = NULL, *ip = NULL;

    if(conn && packet && cache && chunk)
    {
        if((http_req = (HTTP_REQ *)cache->data))
        {
            host = http_req->headers[HEAD_REQ_HOST];
            ip = http_req->headers[HEAD_RESP_SERVER];
            if(host && ip)
            {
                ltable->set_dns(ltable, host, inet_addr(ip));
                DEBUG_LOGGER(logger, "Resolved name[%s]'s ip[%s] from client", host, ip);
            }
            taskid = atoi(http_req->path);
            DEBUG_LOGGER(logger, "taskid:%d length:%d", taskid, chunk->ndata);
            ltable->add_document(ltable, taskid, 0, chunk->data, chunk->ndata); 
            DEBUG_LOGGER(logger, "over taskid:%d length:%d", taskid, chunk->ndata);
            return 0;
        }
    }
    return -1;
}

/* hispiderd timeout handler */
int hispiderd_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    char buf[HTTP_BUF_SIZE];
    int n = 0;

    if(conn && conn->evstate == EVSTATE_WAIT)
    {
        if(ltable->get_task(ltable, buf, &n) >= 0) 
        {
            conn->over_evstate(conn);
            return conn->push_chunk(conn, buf, n);
        }
        else
        {
            if(conn->timeout >= TASK_WAIT_MAX) conn->timeout = 0;
            //DEBUG_LOGGER(logger, "set_timeout(%d) on %s:%d", 
            //        conn->timeout + TASK_WAIT_TIMEOUT, conn->remote_ip, conn->remote_port);
            conn->wait_evstate(conn);
            return conn->set_timeout(conn, conn->timeout + TASK_WAIT_TIMEOUT);
        }
        return 0;
    }
    return -1;
}

int hispiderd_oob_handler(CONN *conn, CB_DATA *oob)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
    char *s = NULL, *p = NULL, *ep = NULL, *whitelist = NULL, *whost = NULL, 
         *basedir = NULL, *host = NULL, *path = NULL;
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
    /* HISPIDERD */
    if((service = service_init()) == NULL)
    {
        fprintf(stderr, "Initialize service failed, %s", strerror(errno));
        _exit(-1);
    }
    service->family = iniparser_getint(dict, "HISPIDERD:inet_family", AF_INET);
    service->sock_type = iniparser_getint(dict, "HISPIDERD:socket_type", SOCK_STREAM);
    service->ip = iniparser_getstr(dict, "HISPIDERD:service_ip");
    service->port = iniparser_getint(dict, "HISPIDERD:service_port", 3721);
    service->working_mode = iniparser_getint(dict, "HISPIDERD:working_mode", WORKING_PROC);
    service->service_type = iniparser_getint(dict, "HISPIDERD:service_type", S_SERVICE);
    service->service_name = iniparser_getstr(dict, "HISPIDERD:service_name");
    service->nprocthreads = iniparser_getint(dict, "HISPIDERD:nprocthreads", 1);
    service->ndaemons = iniparser_getint(dict, "HISPIDERD:ndaemons", 0);
    service->set_log(service, iniparser_getstr(dict, "HISPIDERD:logfile"));
    service->session.packet_type = iniparser_getint(dict, "HISPIDERD:packet_type",PACKET_DELIMITER);
    service->session.packet_delimiter = iniparser_getstr(dict, "HISPIDERD:packet_delimiter");
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
    service->session.buffer_size = iniparser_getint(dict, "HISPIDERD:buffer_size", SB_BUF_SIZE);
    service->session.packet_reader = &hispiderd_packet_reader;
    service->session.packet_handler = &hispiderd_packet_handler;
    service->session.data_handler = &hispiderd_data_handler;
    service->session.timeout_handler = &hispiderd_timeout_handler;
    service->session.oob_handler = &hispiderd_oob_handler;
    if((ltable = ltable_init()))
    {
        basedir = iniparser_getstr(dict, "HISPIDERD:basedir");
        ltable->set_basedir(ltable, basedir);
        ltable->resume(ltable);
        LOGGER_INIT(logger, iniparser_getstr(dict, "HISPIDERD:access_log"));
        ltable->set_logger(ltable, NULL, logger);
        host = iniparser_getstr(dict, "HISPIDERD:host");
        path = iniparser_getstr(dict, "HISPIDERD:path");
        whitelist = iniparser_getstr(dict, "HISPIDERD:whitelist");
        ep = p = whitelist;
        while(*p != '\0' || ep)
        {
            if(*p == ',' || *p == ' ')
            {
                *p = '\0';
                whost = ep;
                ep = ++p;
            }else ++p;
            if(*p == '\0') whost = ep;
            if(whost){ltable->add_to_whitelist(ltable, whost); ep = whost = NULL;}
        }
        ltable->addurl(ltable, host, path);
    }
    /* dns service */
    if((adnservice = service_init()) == NULL)
    {
        fprintf(stderr, "Initialize service failed, %s", strerror(errno));
        _exit(-1);
    }
    adnservice->family = iniparser_getint(dict, "ADNS:inet_family", AF_INET);
    adnservice->sock_type = iniparser_getint(dict, "ADNS:socket_type", SOCK_STREAM);
    adnservice->working_mode = iniparser_getint(dict, "ADNS:working_mode", WORKING_PROC);
    adnservice->service_type = iniparser_getint(dict, "ADNS:service_type", C_SERVICE);
    adnservice->service_name = iniparser_getstr(dict, "ADNS:service_name");
    adnservice->nprocthreads = iniparser_getint(dict, "ADNS:nprocthreads", 1);
    adnservice->ndaemons = iniparser_getint(dict, "ADNS:ndaemons", 0);
    adnservice->set_log(adnservice, iniparser_getstr(dict, "ADNS:logfile"));
    adnservice->session.packet_type = iniparser_getint(dict, "ADNS:packet_type", PACKET_CUSTOMIZED);
    adnservice->session.buffer_size = iniparser_getint(dict, "ADNS:buffer_size", SB_BUF_SIZE);
    adnservice->session.packet_reader = &adns_packet_reader;
    adnservice->session.packet_handler = &adns_packet_handler;
    adnservice->session.timeout_handler = &adns_timeout_handler;
    adnservice->session.error_handler = &adns_error_handler;
    adnservice->session.transaction_handler = &adns_trans_handler;
    interval = iniparser_getint(dict, "ADNS:heartbeat_interval", SB_HEARTBEAT_INTERVAL);
    adnservice->set_heartbeat(adnservice, interval, &adns_heartbeat_handler, adnservice);
    p = iniparser_getstr(dict, "ADNS:nameservers");
    QUEUE_INIT(dnsqueue);
    i = 0;
    while(*p != '\0' && i < DNS_TASK_MAX)
    {
        memset(&(tasklist[i]), 0, sizeof(DNSTASK));
        while(*p != '\0' && (*p < '0' || *p > '9'))++p;
        s = tasklist[i].nameserver;
        while(*p != '\0' && ((*p >= '0' && *p <= '9') || *p == '.')) *s++ = *p++;
        if(s > tasklist[i].nameserver)
        {
            QUEUE_PUSH(dnsqueue, int, &i);
            i++;
        }
    }
    /* wait queue */
    //QUEUE_INIT(waitqueue);
    //while(i < DNS_TASK_MAX){QUEUE_PUSH(undnsqueue, int, &i++);}
    return (sbase->add_service(sbase, service) | sbase->add_service(sbase, adnservice));
}

static void hispiderd_stop(int sig){
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            fprintf(stderr, "hispiderd server is interrupted by user.\n");
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
    signal(SIGTERM, &hispiderd_stop);
    signal(SIGINT,  &hispiderd_stop);
    signal(SIGHUP,  &hispiderd_stop);
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
