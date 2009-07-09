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
#include "ltask.h"
#include "iniparser.h"
#include "evdns.h"
#include "queue.h"
#include "logger.h"
static SBASE *sbase = NULL;
static SERVICE *hitaskd = NULL, *histore = NULL, *adns = NULL;
static dictionary *dict = NULL;
static LTASK *ltask = NULL;
static void *hitaskd_logger = NULL, *histore_logger = NULL, *adns_logger = NULL;
static int is_need_authorization = 0;
static char *authorization_name = "Hitask Administration System";

/* dns packet reader */
int adns_packet_reader(CONN *conn, CB_DATA *buffer)
{
    HOSTENT hostent = {0};
    unsigned char *p = NULL;
    int tid = 0, n = 0, left = 0, ip  = 0;

    if(conn && (tid = conn->c_id) >= 0 && buffer->ndata > 0 && buffer->data)
    {
        p = (unsigned char *)buffer->data;
        left = buffer->ndata;
        while((n = evdns_parse_reply(p, left, &hostent)) > 0)
        {
            if(hostent.naddrs > 0)
            {
                ltask->set_host_ip(ltask, hostent.name, hostent.addrs, hostent.naddrs);
                ip = hostent.addrs[0];
                p = (unsigned char *)&ip;
                DEBUG_LOGGER(adns_logger, "Got host[%s]'s ip[%d.%d.%d.%d] from %s:%d", 
                        hostent.name, p[0], p[1], p[2], p[3], conn->remote_ip, conn->remote_port);
            }
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

    if(conn && (tid = conn->c_id) >= 0 && packet->ndata > 0 && packet->data)
    {
        return adns->newtransaction(adns, conn, tid);
    }
    else 
    {
        conn->over_cstate(conn);
        conn->over(conn);
        ltask->set_dns_status(ltask, tid, NULL, DNS_STATUS_ERR);
    }
    return -1;
}

/* adns error handler */
int adns_error_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    int tid = 0;

    if(conn && (tid = conn->c_id) >= 0 )
    {
        conn->over_cstate(conn);
        conn->over(conn);
        ltask->set_dns_status(ltask, tid, NULL, DNS_STATUS_ERR);
    }
    return -1;
}

/* adns timeout handler */
int adns_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    int tid = 0;

    if(conn && (tid = conn->c_id) >= 0)
    {
        return adns->newtransaction(adns, conn, tid);
    }
    return -1;
}

/* adns transaction handler */
int adns_trans_handler(CONN *conn, int tid)
{
    unsigned char hostname[DNS_NAME_MAX], buf[HTTP_BUF_SIZE];
    int qid = 0, n = 0;

    if(conn && tid >= 0)
        //&& tid < DNS_TASK_MAX && tasklist[tid].conn == conn)
    {
        memset(hostname, 0, DNS_NAME_MAX);
        conn->c_id = tid;
        conn->start_cstate(conn);
        conn->set_timeout(conn, EVDNS_TIMEOUT);
        DEBUG_LOGGER(adns_logger, "Ready for resolving dns on remote[%s:%d] local[%s:%d]", 
                conn->remote_ip, conn->remote_port, conn->local_ip, conn->local_port);
        if((qid = ltask->pop_host(ltask, (char *)hostname)) >= 0)
        {
            conn->s_id = qid;
            qid %= 65536;
            n = evdns_make_query((char *)hostname, 1, 1, (unsigned short)qid, 1, buf); 
            DEBUG_LOGGER(adns_logger, "Resolving %s from nameserver[%s]", 
                    hostname, conn->remote_ip);
            return conn->push_chunk(conn, buf, n);
        }
    }
    return -1;
}

/* heartbeat handler */
void adns_heartbeat_handler(void *arg)
{
    char dns_ip[HTTP_IP_MAX];
    int id = 0;
    CONN *conn = NULL;

    if(arg == (void *)adns)
    {
        /*
        total = QTOTAL(dnsqueue);
        while(total-- > 0)
        {
            id = -1;
            QUEUE_POP(dnsqueue, int, &id);
            if(id >= 0 && id < DNS_TASK_MAX)
            {
                if((tasklist[id].conn = adns->newconn(adns, -1, 
                    SOCK_DGRAM, tasklist[id].nameserver, DNS_DEFAULT_PORT, NULL)))
                {
                    tasklist[id].conn->c_id = id;
                    tasklist[id].conn->start_cstate(tasklist[id].conn);
                    adns->newtransaction(adns, tasklist[id].conn, id);
                }
                else
                {
                    QUEUE_PUSH(dnsqueue, int, &id);
                }
            }
        }
        */
        while((id = ltask->pop_dns(ltask, dns_ip)) >= 0 && 
                (conn = adns->newconn(adns, -1, 
                SOCK_DGRAM, dns_ip, DNS_DEFAULT_PORT, NULL)))

        {
            conn->c_id = id;
            conn->start_cstate(conn);
            adns->newtransaction(adns, conn, id);
        }
    }
}

/* hitaskd packet reader */
int hitaskd_packet_reader(CONN *conn, CB_DATA *buffer)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

#define TOAUTH(conn, buf, p, authorization_name)                    \
do                                                                  \
{                                                                   \
    p = buf;                                                        \
    p += sprintf(p, "HTTP/1.1 401 Unauthorized\r\n"                 \
            "WWW-Authenticate: Basic realm=\"%s\"\r\n\r\n",         \
            authorization_name);                                    \
    conn->push_chunk(conn, buf, p - buf);                           \
    conn->over(conn);                                               \
}while(0)

/* authorization */
int hitaskd_auth(CONN *conn, HTTP_REQ *http_req)
{
    char *key = NULL, *val = NULL;
    int i = 0, session_id = 0;
    LUSER user = {0};

    if(http_kv(&(http_req->auth), http_req->hlines, 
                http_req->nhline, &key, &val) >= 0) 
    {
        if((session_id = ltask->authorization(ltask, -1, key, val, &user)) >= 0)
        {
            return session_id;
        }
    }
    else if(http_req->ncookies > 0)
    {
        for(i = 0; i < http_req->ncookies; i++)
        {
            if(http_kv(&(http_req->cookies[i]), http_req->hlines,
                        http_req->nhline, &key, &val) >= 0)
            {
                return session_id; 
            }
        }
    }
    return -1;
}

/* packet handler */
int hitaskd_packet_handler(CONN *conn, CB_DATA *packet)
{
    char buf[HTTP_BUF_SIZE], *host = NULL, *ip = NULL, *p = NULL, *end = NULL;
    int urlid = 0, n = 0, i = 0, ips = 0;
    HTTP_REQ http_req = {0};

    if(conn)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        /*
        int fd = 0;
        if((fd = open("/tmp/header.txt", O_CREAT|O_RDWR|O_TRUNC, 0644)) > 0)
        {
            write(fd, packet->data, packet->ndata);
            close(fd);
        }
        */
        if(http_request_parse(p, end, &http_req) == -1) goto err_end;
        //authorized 
        if(is_need_authorization && hitaskd_auth(conn, &http_req) < 0)
        {
            TOAUTH(conn, buf, p, authorization_name);
            return 0;
        }
        if(http_req.reqid == HTTP_GET)
        {
            if(strncasecmp(http_req.path, "/index", 6) == 0)
            {
                p += sprintf(p, "%s", HTTP_RESP_OK);
                for(i = 0; i < http_req.nargvs; i++)
                {

                }
            }
            else if(strncasecmp(http_req.path, "/stop", 5) == 0)
            {
                ltask->set_state_running(ltask, 0);
            }
            else if(strncasecmp(http_req.path, "/running", 8) == 0)
            {
                ltask->set_state_running(ltask, 1);
            }
            /*
            if((n = ltask->get_stateinfo(ltask, buf)))
            {
                conn->push_chunk(conn, buf, n);
            }
            else
            {
                goto err_end;
            }*/
        }
        else if(http_req.reqid == HTTP_POST)
        {
            if((n = http_req.headers[HEAD_ENT_CONTENT_LENGTH]) > 0 
                    && (n = atol(http_req.hlines + n)) > 0)
            {
                conn->save_cache(conn, &http_req, sizeof(HTTP_REQ));
                conn->recv_chunk(conn, n);
                //conn->recv_file(conn, "/tmp/recv.txt", 0, n);
            }
        }
        else if(http_req.reqid == HTTP_TASK)
        {
            if(http_req.headers[HEAD_REQ_HOST] > 0 && http_req.headers[HEAD_RESP_SERVER] > 0)
            {
                host = http_req.hlines + http_req.headers[HEAD_REQ_HOST];
                ip = http_req.hlines + http_req.headers[HEAD_RESP_SERVER];
                ips = inet_addr(ip);
                ltask->set_host_ip(ltask, host, &ips, 1);
                DEBUG_LOGGER(hitaskd_logger, "Resolved name[%s]'s ip[%s] from client", host, ip);
            }
            urlid = atoi(http_req.path);
            /* get new task */
            if(ltask->get_task(ltask, buf, &n) >= 0) 
            {
                return conn->push_chunk(conn, buf, n);
            }
            else
            {
                if(conn->timeout >= TASK_WAIT_MAX) conn->timeout = 0;
                DEBUG_LOGGER(hitaskd_logger, "set_timeout(%d) on %s:%d", 
                        conn->timeout + TASK_WAIT_TIMEOUT, conn->remote_ip, conn->remote_port);
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
int hitaskd_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{

    if(conn && packet && cache && chunk && chunk->ndata > 0)
    {
            return 0;
    }
    return -1;
}

/* hitaskd timeout handler */
int hitaskd_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    char buf[HTTP_BUF_SIZE];
    int n = 0;

    if(conn && conn->evstate == EVSTATE_WAIT)
    {
        if(ltask->get_task(ltask, buf, &n) >= 0) 
        {
            conn->over_evstate(conn);
            return conn->push_chunk(conn, buf, n);
        }
        else
        {
            if(conn->timeout >= TASK_WAIT_MAX) conn->timeout = 0;
            //DEBUG_LOGGER(hitaskd_logger, "set_timeout(%d) on %s:%d", 
            //        conn->timeout + TASK_WAIT_TIMEOUT, conn->remote_ip, conn->remote_port);
            conn->wait_evstate(conn);
            return conn->set_timeout(conn, conn->timeout + TASK_WAIT_TIMEOUT);
        }
        return 0;
    }
    return -1;
}

int hitaskd_oob_handler(CONN *conn, CB_DATA *oob)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

/* packet reader */
int histore_packet_reader(CONN *conn, CB_DATA *buffer)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

/* packet handler */
int histore_packet_handler(CONN *conn, CB_DATA *packet)
{
    char *p = NULL, *end = NULL;
    HTTP_REQ http_req = {0};
    int urlid = 0, n = 0;

    if(conn)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        if(http_request_parse(p, end, &http_req) == -1) goto err_end;
        if(http_req.reqid == HTTP_GET)
        {
            return 0;
        }
        else if(http_req.reqid == HTTP_POST)
        {
            return 0;
        }
        else if(http_req.reqid == HTTP_PUT)
        {
            urlid = atoi(http_req.path);
            if(urlid >= 0)
            {
                if((n = http_req.headers[HEAD_ENT_CONTENT_LENGTH]) > 0 
                        && (n = atol(http_req.hlines + n)) > 0)
                {
                    conn->save_cache(conn, &http_req, sizeof(HTTP_REQ));
                    conn->recv_chunk(conn, n);
                    DEBUG_LOGGER(histore_logger, "Ready for recv_chunk[%d] from %s:%d via %d",
                            n, conn->remote_ip, conn->remote_port, conn->fd);
                }
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
int histore_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    int urlid = 0, n = 0;
    char *date = NULL, *p = NULL;
    HTTP_REQ *http_req = NULL;

    if(conn && packet && cache && chunk && chunk->ndata > 0)
    {
        if((http_req = (HTTP_REQ *)cache->data))
        {
            if(http_req->reqid == HTTP_PUT)
            {
                urlid = atoi(http_req->path);
                if((n = http_req->headers[HEAD_ENT_LAST_MODIFIED]) > 0)
                {
                    date = (http_req->hlines + n);
                }
                /* doctype */
                if((n = http_req->headers[HEAD_ENT_CONTENT_TYPE]) > 0)
                    p = http_req->hlines + n;
                ltask->update_content(ltask, urlid, date, p, chunk->data, chunk->ndata);
            }
            else if(http_req->reqid == HTTP_POST)
            {
            }
            return 0;
        }
    }
    return -1;
}

/* histore timeout handler */
int histore_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

/* oob handler */
int histore_oob_handler(CONN *conn, CB_DATA *oob)
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
    char *s = NULL, *p = NULL, *basedir = NULL, *start = NULL;
    //*ep = NULL, *whitelist = NULL, *whost = NULL, 
    //*host = NULL, *path = NULL;
    int interval = 0;

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
    /* HITASKD */
    if((hitaskd = service_init()) == NULL)
    {
        fprintf(stderr, "Initialize service failed, %s", strerror(errno));
        _exit(-1);
    }
    hitaskd->family = iniparser_getint(dict, "HITASKD:inet_family", AF_INET);
    hitaskd->sock_type = iniparser_getint(dict, "HITASKD:socket_type", SOCK_STREAM);
    hitaskd->ip = iniparser_getstr(dict, "HITASKD:service_ip");
    hitaskd->port = iniparser_getint(dict, "HITASKD:service_port", 2816);
    hitaskd->working_mode = iniparser_getint(dict, "HITASKD:working_mode", WORKING_PROC);
    hitaskd->service_type = iniparser_getint(dict, "HITASKD:service_type", S_SERVICE);
    hitaskd->service_name = iniparser_getstr(dict, "HITASKD:service_name");
    hitaskd->nprocthreads = iniparser_getint(dict, "HITASKD:nprocthreads", 1);
    hitaskd->ndaemons = iniparser_getint(dict, "HITASKD:ndaemons", 0);
    hitaskd->set_log(hitaskd, iniparser_getstr(dict, "HITASKD:logfile"));
    hitaskd->session.packet_type = iniparser_getint(dict, "HITASKD:packet_type",PACKET_DELIMITER);
    hitaskd->session.packet_delimiter = iniparser_getstr(dict, "HITASKD:packet_delimiter");
    p = s = hitaskd->session.packet_delimiter;
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
    hitaskd->session.packet_delimiter_length = strlen(hitaskd->session.packet_delimiter);
    hitaskd->session.buffer_size = iniparser_getint(dict, "HITASKD:buffer_size", SB_BUF_SIZE);
    hitaskd->session.packet_reader = &hitaskd_packet_reader;
    hitaskd->session.packet_handler = &hitaskd_packet_handler;
    hitaskd->session.data_handler = &hitaskd_data_handler;
    hitaskd->session.timeout_handler = &hitaskd_timeout_handler;
    hitaskd->session.oob_handler = &hitaskd_oob_handler;
    if((p = iniparser_getstr(dict, "HITASKD:access_log"))){LOGGER_INIT(hitaskd_logger,p);}
    /* link  task table */
    if((ltask = ltask_init()))
    {
        basedir = iniparser_getstr(dict, "HITASKD:basedir");
        start = iniparser_getstr(dict, "HITASKD:start");
        ltask->set_basedir(ltask, basedir);
        ltask->add_url(ltask, -1, 0, start);
        //ltable->set_logger(ltable, NULL, logger);
        /*
           host = iniparser_getstr(dict, "HITASKD:host");
           path = iniparser_getstr(dict, "HITASKD:path");
           whitelist = iniparser_getstr(dict, "HITASKD:whitelist");
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
         */
    }
    /* histore */
    if((histore = service_init()) == NULL)
    {
        fprintf(stderr, "Initialize service failed, %s", strerror(errno));
        _exit(-1);
    }
    histore->family = iniparser_getint(dict, "HISTORE:inet_family", AF_INET);
    histore->sock_type = iniparser_getint(dict, "HISTORE:socket_type", SOCK_STREAM);
    histore->ip = iniparser_getstr(dict, "HISTORE:service_ip");
    histore->port = iniparser_getint(dict, "HISTORE:service_port", 2816);
    histore->working_mode = iniparser_getint(dict, "HISTORE:working_mode", WORKING_PROC);
    histore->service_type = iniparser_getint(dict, "HISTORE:service_type", S_SERVICE);
    histore->service_name = iniparser_getstr(dict, "HISTORE:service_name");
    histore->nprocthreads = iniparser_getint(dict, "HISTORE:nprocthreads", 1);
    histore->ndaemons = iniparser_getint(dict, "HISTORE:ndaemons", 0);
    histore->set_log(histore, iniparser_getstr(dict, "HISTORE:logfile"));
    histore->session.packet_type = iniparser_getint(dict, "HISTORE:packet_type",PACKET_DELIMITER);
    histore->session.packet_delimiter = iniparser_getstr(dict, "HISTORE:packet_delimiter");
    p = s = histore->session.packet_delimiter;
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
    histore->session.packet_delimiter_length = strlen(histore->session.packet_delimiter);
    histore->session.buffer_size = iniparser_getint(dict, "HISTORE:buffer_size", SB_BUF_SIZE);
    histore->session.packet_reader = &histore_packet_reader;
    histore->session.packet_handler = &histore_packet_handler;
    histore->session.data_handler = &histore_data_handler;
    histore->session.timeout_handler = &histore_timeout_handler;
    histore->session.oob_handler = &histore_oob_handler;
    if((p = iniparser_getstr(dict, "HISTORE:access_log"))){LOGGER_INIT(histore_logger, p);}
    /* dns service */
    if((adns = service_init()) == NULL)
    {
        fprintf(stderr, "Initialize service failed, %s", strerror(errno));
        _exit(-1);
    }
    adns->family = iniparser_getint(dict, "ADNS:inet_family", AF_INET);
    adns->sock_type = iniparser_getint(dict, "ADNS:socket_type", SOCK_STREAM);
    adns->working_mode = iniparser_getint(dict, "ADNS:working_mode", WORKING_PROC);
    adns->service_type = iniparser_getint(dict, "ADNS:service_type", C_SERVICE);
    adns->service_name = iniparser_getstr(dict, "ADNS:service_name");
    adns->nprocthreads = iniparser_getint(dict, "ADNS:nprocthreads", 1);
    adns->ndaemons = iniparser_getint(dict, "ADNS:ndaemons", 0);
    adns->set_log(adns, iniparser_getstr(dict, "ADNS:logfile"));
    adns->session.packet_type = iniparser_getint(dict, "ADNS:packet_type", PACKET_CUSTOMIZED);
    adns->session.buffer_size = iniparser_getint(dict, "ADNS:buffer_size", SB_BUF_SIZE);
    adns->session.packet_reader = &adns_packet_reader;
    adns->session.packet_handler = &adns_packet_handler;
    adns->session.timeout_handler = &adns_timeout_handler;
    adns->session.error_handler = &adns_error_handler;
    adns->session.transaction_handler = &adns_trans_handler;
    if((p = iniparser_getstr(dict, "ADNS:access_log"))){LOGGER_INIT(adns_logger, p);}
    interval = iniparser_getint(dict, "ADNS:heartbeat_interval", SB_HEARTBEAT_INTERVAL);
    adns->set_heartbeat(adns, interval, &adns_heartbeat_handler, adns);
    p = iniparser_getstr(dict, "ADNS:nameservers");
    while(*p != '\0')
    {
        while(*p != '\0' && (*p < '0' || *p > '9'))++p;
        s = p;
        while(*p != '\0' && ((*p >= '0' && *p <= '9') || *p == '.')) ++p;
        if((p - s) > 0) 
        {
            *p = '\0';
            ltask->add_dns(ltask, s);
            ++p;
        }
    }
    return (sbase->add_service(sbase, hitaskd) | sbase->add_service(sbase, histore) 
            | sbase->add_service(sbase, adns));
}

static void hitaskd_stop(int sig){
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            fprintf(stderr, "hitaskd server is interrupted by user.\n");
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
    signal(SIGTERM, &hitaskd_stop);
    signal(SIGINT,  &hitaskd_stop);
    signal(SIGHUP,  &hitaskd_stop);
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
    if(hitaskd_logger){LOGGER_CLEAN(hitaskd_logger);}
    if(histore_logger){LOGGER_CLEAN(histore_logger);}
    if(adns_logger){LOGGER_CLEAN(adns_logger);}
    return 0;
}
