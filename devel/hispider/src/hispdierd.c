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
#include "evdns.h"

typedef struct _DNSTASK
{
    int id;
    CONN *conn;
    char nameserver[DNS_IP_MAX];
    char host[DNS_NAME_MAX];
}DNSTASK;
static SBASE *sbase = NULL;
static SERVICE *service = NULL;
static SERVICE *dnservice = NULL;
static dictionary *dict = NULL;
static LTABLE *ltable = NULL;
static void *dnsqueue = NULL;
//static void *undnsqueue = NULL;
static DNSTASK tasklist[DNS_TASK_MAX];

/* dns packet reader */
int adns_packet_reader(CONN *conn, CB_DATA *buffer)
{
    int n = 0, tid = 0;
    char *buf = NULL;

    if(conn && buffer->ndata > 0 && buffer->data)
    {
        return buffer->ndata;
    }
    return -1;
}

/* dns packet handler */
int adns_packet_handler(CONN *conn, CB_DATA *packet)
{
    int tid = 0, i = 0, ip = 0;
    HOSTENT hostent = {0};

    if(conn && (tid = conn->c_id) >= 0 && tid < DNS_TASK_MAX 
            && tasklist[tid].conn == conn && packet->ndata > 0 && packet->data)
    {
        if(evdns_parse_reply(packet->data, packet->ndata, &hostent)  == 0 && hostent.naddrs > 0)
        {
            ip = hostent.addrs[0];
            ltable->dnsdb->update(ltable->dnsdb, tasklist[tid].id, tasklist[tid].host, ip);
        }
        tasklist[tid].conn->c_id = tid;
        adnservice->newtransaction(adnservice, tasklist[id].conn, tid);
        return 0;
    }
    else 
    {
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

    if(conn && (tid = conn->c_id) >= 0 && tid < DNS_TASK_MAX && tasklist[id].conn == conn)
    {
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

    if(conn && (tid = conn->c_id) >= 0 && tid < DNS_TASK_MAX && tasklist[id].conn == conn)
    {
        conn->over(conn);
        tasklist[tid].conn = NULL;
        QUEUE_PUSH(dnsqueue, int, &tid);
    }
    return -1;
}

/* adns transaction handler */
int adns_trans_handler(CONN *conn, int tid)
{
    char buf[HTTP_BUF_SIZE];
    int taskid = 0, n = 0;

    if(conn && tid >= 0 && tid < DNS_TASK_MAX && tasklist[tid].conn == conn)
    {
        if((tasklist[tid].taskid = ltable->dnsdb->get_task(ltable->dnsdb, tasklist[tid].host)) >= 0)
        {
            n = evdns_make_query(tasklist[tid].host, 1, 1, (tid % 65536), 1, buf); 
            conn->c_id = tid;
            conn->start_cstate(conn);
            conn->set_timeout(conn, EVDNS_TIMEOUT);
            return conn->push_chunk(conn, buf, n);
        }
        else
        {
            tasklist[tid].conn->c_id = tid;
            adnservice->newtransaction(adnservice, tasklist[id].conn, tid);
        }
    }
    return -1;
}

/* heartbeat handler */
void adns_heartbeat_handler(void *arg)
{
    int id = 0;

    if(arg == (void *)adnservice)
    {
        while(QTOTAL(dnsqueue) > 0)
        {
            id = -1;
            QUEUE_POP(dnsqueue, int, &id);
            if(id >= 0 && id < DNS_TASK_MAX)
            {
                if((tasklist[id].conn = adnservice->newconn(adnservice,
                                -1, -1, tasklist[id].nameserver, DNS_DEFAULT_PORT, NULL)))
                {
                    tasklist[id].conn->c_id = id;
                    adnservice->newtransaction(adnservice, tasklist[id].conn, id);
                }
            }
        }
    }
}

/* hispiderd packet reader */
int hispiderd_packet_reader(CONN *conn, CB_DATA *buffer)
{
}

/* packet handler */
int hispiderd_packet_handler(CONN *conn, CB_DATA *packet)
{
    char *p = NULL, *end = NULL, buf[HTTP_BUF_SIZE];
    HTTP_REQ http_req = {0};
    long taskid = 0, n = 0;

    if(conn)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        if(http_request_parse(p, end, &http_req) == -1) goto err_end;
        if(http_req.reqid == HTTP_GET)
        {
            if((n = ltable->get_stateinfo(ltable, buf)))
            {
                conn->push_chunk(conn, buf, n);
            }
            else
            {
                goto err_end;
            }
        }
        else if(http_req.reqid == HTTP_TASK)
        {
            taskid = atol(http_req.path);
            if(taskid != -1)
            {
                if(http_req.headers[HEAD_ENT_CONTENT_LENGTH] 
                        && (n = atol(http_req.headers[HEAD_ENT_CONTENT_LENGTH])) > 0)
                {
                    conn->save_cache(conn->cache, &http_req, sizeof(HTTP_RESPONSE));
                    conn->recv_chunk(conn, n);
                }
                else
                {
                    ltable->set_task_state(ltable, taskid, TASK_STATE_ERROR);
                }
            }
            if((ltable->get_task(ltable, buf, &n) == -1)) goto err_end;
            conn->push_chunk(conn, buf, n);
        }
        return 0;
err_end:
        conn->push_chunk(conn, HTTP_BAD_REQUEST, strlen(HTTP_BAD_REQUEST));
    }
    return -1;
}

/*  data handler */
int hispiderd_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    HTTP_REQ *http_req = NULL;
    int taskid = 0, n = 0;

    if(conn && packet && cache && chunk)
    {
        if((http_req = (HTTP_REQ *)packet->data))
        {
            taskid = atoi(http_req->path);
            return ltable->add_document(ltable, taskid, 0, chunk->data, chunk->ndata); 
        }
    }
    return -1;
}

int hispiderd_oob_handler(CONN *conn, CB_DATA *oob)
{
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
    char *logfile = NULL, *s = NULL, *p = NULL, *basedir = NULL;
    int n = 0;
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
    service->session.oob_handler = &hispiderd_oob_handler;
    if((ltable = ltable_init()))
    {
        basedir = iniparser_getstr(dict, "HISPIDERD:basedir");
        ltable->set_basedir(ltable, basedir);
        ltable->resume(ltable);
    }
    /* dns service */
    if((adnservice = service_init()) == NULL)
    {
        fprintf(stderr, "Initialize service failed, %s", strerror(errno));
        _exit(-1);
    }
    adnservice->family = iniparser_getint(dict, "ADNS:inet_family", AF_INET);
    adnservice->sock_type = iniparser_getint(dict, "ADNS:socket_type", SOCK_STREAM);
    adnservice->ip = iniparser_getstr(dict, "ADNS:service_ip");
    adnservice->port = iniparser_getint(dict, "ADNS:service_port", 3721);
    adnservice->working_mode = iniparser_getint(dict, "ADNS:working_mode", WORKING_PROC);
    adnservice->service_type = iniparser_getint(dict, "ADNS:service_type", C_SERVICE);
    adnservice->service_name = iniparser_getstr(dict, "ADNS:service_name");
    adnservice->nprocthreads = iniparser_getint(dict, "ADNS:nprocthreads", 1);
    adnservice->ndaemons = iniparser_getint(dict, "ADNS:ndaemons", 0);
    adnservice->set_log(service, iniparser_getstr(dict, "ADNS:logfile"));
    adnservice->session.packet_type = iniparser_getint(dict, "ADNS:packet_type", PACKET_CUSTOMIZED);
    adnservice->session.buffer_size = iniparser_getint(dict, "ADNS:buffer_size", SB_BUF_SIZE);
    adnservice->session.packet_reader = &adns_packet_reader;
    adnservice->session.packet_handler = &adns_packet_handler;
    adnservice->session.data_handler = &adns_data_handler;
    adnservice->session.oob_handler = &adns_oob_handler;
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
        memset(&(tasklist[i]), 0, sizeof(DNS_TASK));
        while(*p != '\0' && (*p < '0' || *p > '5'))++p;     
        ps = tasklist[i].nameserver;
        while(*p != '\0' && ((*p >= '0' && *p <= '5') || *p == '.')) *ps++ = *p++;
        if(ps > tasklist[i].nameserver)
        {
            QUEUE_PUSH(dnsqueue, int, &i);
            i++;
        }
    }
    /* unused dns queue */
    //QUEUE_INIT(undnsqueue);
    //while(i < DNS_TASK_MAX){QUEUE_PUSH(undnsqueue, int, &i++);}
    return (sbase->add_service(sbase, service) | sbase->add_service(sbase, adnservice));
}

static void hispiderd_stop(int sig){
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
}
