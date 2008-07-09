#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <sys/stat.h>
#include <sbase.h>
#include "http.h"
#include "link.h"
#include "iniparser.h"
#include "logger.h"
#include "common.h"
#include "timer.h"
#include "zstream.h"

#define HTTP_CB_DATA_MAX 1048576
#define SBASE_LOG "/tmp/sbase.log"

static SBASE *sbase = NULL;
SERVICE *transport = NULL;
static dictionary *dict = NULL;
static LOGGER *daemon_logger = NULL;
typedef struct _TASK
{
    CONN **conns;
    char *daemon_ip;
    int daemon_port;
    long long timeout;
    int ntask_limit;
    int ntask_total;
    int ntask_wait;
    int ntask_working;
    int ntask_over;
    HTTP_REQUEST *requests;
    DOCMETA *tasks;
    char **results;
}TASK;
static TASK task = {0};
#define NCONN(tp, n) ((tp.conns[n])?(tp.conns[n])\
        :(tp.conns[n] = transport->newconn(transport, -1, -1, tp.daemon_ip, tp.daemon_port, NULL)))
#define OCONN(tp, n)                                        \
{                                                           \
    if(tp.conns[n])                                         \
    {                                                       \
        tp.conns[n]->over_cstate(tp.conns[n]);              \
        tp.conns[n]->over(tp.conns[n]);                     \
        tp.conns[n] = NULL;                                 \
    }                                                       \
}
#define DCONN(tp, n) (tp.conns[n] = NULL)
#define NEWREQ(tp, n)                                       \
{                                                           \
    memset(&(tp.tasks[n]), 0, sizeof(DOCMETA));             \
    tp.conns[n]->c_id = n;                                  \
    tp.conns[n]->start_cstate(tp.conns[n]);                 \
    transport->newtransaction(transport, tp.conns[n], n);   \
    DEBUG_LOGGER(daemon_logger, "NEW REQUEST[%d] %s:%d via %d", \
        n, tp.conns[n]->ip, tp.conns[n]->port, tp.conns[n]->fd);             \
}
#define NEWTASK(tp, n, rq)                                  \
{                                                           \
    memcpy(&(tp.requests[n]), rq, sizeof(HTTP_REQUEST));    \
    tp.requests[n].status = LINK_STATUS_DNSQUERY;           \
    tp.tasks[n].id = tp.requests[n].id;                     \
    DEBUG_LOGGER(daemon_logger, "NEW TASK[%d] %s:%d id:%d",           \
            n, tp.requests[n].ip, tp.requests[n].port,  tp.tasks[n].id);        \
}
#define ENDTASK(tp, n, st)                                  \
{                                                           \
    tp.requests[n].status = LINK_STATUS_OVER;               \
    tp.tasks[n].id      = tp.requests[n].id;                \
    tp.tasks[n].status  = st;                               \
    DEBUG_LOGGER(daemon_logger, "END TASK[%d] %s:%d %d",    \
            n, tp.requests[n].ip, tp.requests[n].port, st); \
}
#define RESETTASK(tp, n)                                    \
{                                                           \
    memset(&(tp.tasks[n]), 0, sizeof(DOCMETA));             \
    memset(&(tp.requests[n]), 0, sizeof(HTTP_REQUEST));     \
    if(tp.results[n]){free(tp.results[n]); tp.results[n] = NULL;} \
    DEBUG_LOGGER(daemon_logger, "RESET TASK[%d]", n);       \
}
//error handler 
int cb_trans_error_handler(CONN *conn)
{
    int c_id = 0;
    if(conn)
    {
        c_id = conn->c_id;
        if(conn == task.conns[c_id])
        {
            if(task.requests[c_id].status == LINK_STATUS_REQUEST)
            {
                task.requests[c_id].status = LINK_STATUS_INIT;
            }
            if(task.requests[c_id].status == LINK_STATUS_COMPLETE)
            {
                task.requests[c_id].status = LINK_STATUS_OVER;
            }
            DCONN(task, c_id);
        }
        else
        {
            if(((CB_DATA *)(conn->packet))->data && ((CB_DATA *)(conn->cache))->data)
            {
                conn->session.data_handler(conn, (CB_DATA *)conn->packet, 
                        (CB_DATA *)conn->cache, (CB_DATA *)conn->chunk);
            }
            ENDTASK(task, c_id, LINK_STATUS_ERROR);
        }
    }
    return -1;
}

//daemon task handler 
void cb_trans_task_handler(void *arg);

void cb_trans_heartbeat_handler(void *arg)
{
    int  n = 0, tid = 0, i = 0;
    long taskid = 0;
    CONN *conn = NULL;

    if(transport)
    {
        //DEBUG_LOGGER(daemon_logger, "ntask_total:%d ntask_wait:%d ntask_over:%d",
        //        task.ntask_total, task.ntask_wait, task.ntask_over);
        for(i = 0; i < task.ntask_limit; i++)
        {
            if(task.requests[i].status == LINK_STATUS_INIT && (conn = NCONN(task, i)))
            {
                task.requests[i].status = LINK_STATUS_REQUEST;
                NEWREQ(task, i);
            }
            if(task.requests[i].status == LINK_STATUS_WAIT)
            {
                if((conn = transport->newconn(transport, -1, -1, task.requests[i].ip, 
                                task.requests[i].port, NULL)))
                {
                    conn->start_cstate(conn);
                    conn->c_id = i;
                    conn->set_timeout(conn, task.timeout);
                    DEBUG_LOGGER(daemon_logger, "Set timeout[%lld] on %s:%d via %d",
                            task.timeout, conn->ip, conn->port, conn->fd);
                    task.requests[i].status = LINK_STATUS_WORKING;
                    transport->newtransaction(transport, conn, i);
                }
            }
            if(task.requests[i].status == LINK_STATUS_DNSQUERY)
            {
                taskid = i;
                transport->newtask(transport, (void *)&cb_trans_task_handler, (void *)taskid);
                task.requests[i].status = LINK_STATUS_DNSWAIT;
            }
            if(task.requests[i].status == LINK_STATUS_OVER)
            {
                if((conn = NCONN(task, i)))
                {
                    conn->c_id = i;
                    task.requests[i].status = LINK_STATUS_COMPLETE;
                    transport->newtransaction(transport, conn, i);
                    DEBUG_LOGGER(daemon_logger, "Ready for over TASK:%d id:%d",
                            i,  task.tasks[i].id);
                }
            }
        }
    }
    return  ;
}

//daemon task handler 
void cb_trans_task_handler(void *arg)
{
    long taskid = (long )arg;
    struct hostent *hp = NULL;
}

int cb_trans_packet_reader(CONN *conn, CB_DATA *buffer)
{
}

int cb_trans_packet_handler(CONN *conn, CB_DATA *packet)
{
    HTTP_RESPONSE response;
    char *p = NULL, *end = NULL;
    int len = 0, c_id = 0;

    if(conn)
    {
        memset(&response, 0, sizeof(HTTP_RESPONSE));
        response.respid = -1;
        p   = (char *)packet->data;
        end = (char *)packet->data + packet->ndata;
        if(http_response_parse(p, end, &response) == -1)  return ;
        c_id = conn->c_id;
        if(conn == task.conns[c_id])
        {
            if(task.requests[c_id].status == LINK_STATUS_REQUEST)
            {
                if(response.respid == RESP_OK && response.headers[HEAD_ENT_CONTENT_LENGTH])
                {
                    len = atoi(response.headers[HEAD_ENT_CONTENT_LENGTH]);
                    conn->recv_chunk(conn, len);
                    return 0;
                }
                //set re request
                task.requests[c_id].status = LINK_STATUS_INIT;
            }
            if(task.requests[c_id].status == LINK_STATUS_COMPLETE)
            {
                if(response.respid != RESP_OK)
                {
                    //resend complete data
                    task.requests[c_id].status = LINK_STATUS_OVER;
                    return -1;
                }
                //over cstate
                conn->over_cstate(conn);
                //RESET TASK
                RESETTASK(task, c_id);
                return 0;
            }
            return -1;
        }
        else
        {
            if(task.requests[c_id].status == LINK_STATUS_WORKING)
            {
                if(response.respid == RESP_OK)
                {
                    if(response.headers[HEAD_ENT_CONTENT_TYPE]
                            && strncasecmp(response.headers[HEAD_ENT_CONTENT_TYPE], "text", 4) == 0)
                    {
                        if(response.headers[HEAD_ENT_CONTENT_LENGTH])
                        {
                            len = atoi(response.headers[HEAD_ENT_CONTENT_LENGTH]);       
                            conn->save_cache(conn->cache, &response, sizeof(HTTP_RESPONSE));
                            conn->recv_chunk(conn, len);
                        }
                        else
                        {
                            conn->save_cache(conn->cache, &response, sizeof(HTTP_RESPONSE));
                            conn->recv_chunk(conn, HTTP_CB_DATA_MAX);
                        }
                        return -1;
                    }
                }
            }
            conn->over_cstate(conn);
            conn->over(conn);
            ENDTASK(task, c_id, LINK_STATUS_ERROR);
        }
    }
    return -1;
}

int cb_trans_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    DOCMETA *pdocmeta = NULL;
    HTTP_RESPONSE *resp = NULL;
    HTTP_REQUEST *req = NULL;
    char buf[LBUF_SIZE];
    char *data = NULL, *zdata = NULL, *p = NULL;
    int i = 0, c_id = 0, n = 0, nhost = 0, npath = 0, status = 0;
    unsigned long nzdata = 0, ndata = 0;

    if(conn && chunk->data)
    {
        c_id = conn->c_id;
        if(conn == task.conns[c_id])
        {
            if(task.requests[c_id].status == LINK_STATUS_REQUEST)
            {
                NEWTASK(task, c_id, chunk->data);
            }
            return -1;
        }
        else
        {
            if(task.requests[c_id].status == LINK_STATUS_WORKING)
            {
                pdocmeta = &(task.tasks[c_id]);
                req = &(task.requests[c_id]); 
                memset(pdocmeta, 0, sizeof(DOCMETA));
                req->status = LINK_STATUS_ERROR;
                resp = (HTTP_RESPONSE *)cache->data;
                p = buf;
                for(i = 0; i < HTTP_HEADER_NUM; i++)
                {
                    if(resp->headers[i])
                    {
                        p += sprintf(p, "[%d:%s:%s]", i, http_headers[i].e, resp->headers[i]);                   
                    }
                }
                nhost = strlen(req->host) + 1;
                npath = strlen(req->path) + 1;
                pdocmeta->hostoff = (p - buf);
                pdocmeta->pathoff =  pdocmeta->hostoff + nhost;
                pdocmeta->htmloff = pdocmeta->pathoff + npath;
                pdocmeta->size = pdocmeta->htmloff + chunk->ndata;
                if((data = (char *)calloc(1, pdocmeta->size)))
                {
                    p = data;
                    memcpy(p, buf, pdocmeta->hostoff);
                    p += pdocmeta->hostoff;
                    memcpy(p, req->host, nhost);
                    p += nhost;
                    memcpy(p, req->path, npath);
                    p += npath;
                    memcpy(p, chunk->data, chunk->ndata);
                    p += chunk->ndata;
                    ndata = pdocmeta->size;
                    DEBUG_LOGGER(daemon_logger, 
                            "nhost:%d npath:%d hostoff:%d htmloff:%d off:%d dsize:%d ndata:%d", 
                            nhost, npath, pdocmeta->hostoff, pdocmeta->htmloff, 
                            (p - data), chunk->ndata, ndata);
                    nzdata = pdocmeta->size + Z_HEADER_SIZE;
                    if((zdata = (char *)calloc(1, nzdata)))
                    {
                        if(zcompress(data, ndata, zdata, &(nzdata)) == 0)
                        {
                            DEBUG_LOGGER(daemon_logger, "compress %d  to %d body:%d ",
                                    ndata, nzdata, chunk->ndata);
                            req->status = LINK_STATUS_OVER;
                            pdocmeta->zsize = nzdata;
                            task.results[c_id] = zdata;
                        }
                    }
                    free(data);
                    data = NULL;
                }
            }
            //complete data and over connection
            conn->over_cstate(conn);
            conn->over(conn);
            ENDTASK(task, c_id, req->status);
        }
    }
    return -1;
}

//daemon transaction handler 
int cb_trans_transaction_handler(CONN *conn, int tid)
{
    char buf[HTTP_BUF_SIZE];
    char *p = NULL;
    int n = 0;

    if(conn && tid >= 0 && tid < task.ntask_limit)
    {
        if(conn == task.conns[tid])
        {
            if(task.requests[tid].status == LINK_STATUS_REQUEST)
            {
                p = buf;
                n = sprintf(p, "TASK / HTTP/1.0\r\n\r\n");
                conn->push_chunk(conn, buf, n);
                return 0;
            }
            if(task.requests[tid].status == LINK_STATUS_COMPLETE)
            {
                p = buf;
                n = sprintf(p, "PUT / HTTP/1.0\r\nContent-Length: %d\r\n\r\n", 
                        (sizeof(DOCMETA) + task.tasks[tid].zsize)); 
                conn->push_chunk(conn, buf, n);
                conn->push_chunk(conn, &(task.tasks[tid]), sizeof(DOCMETA));
                if(task.results[tid])
                {
                    conn->push_chunk(conn, task.results[tid], task.tasks[tid].zsize);
                }
                return 0;
            }
        }
        else
        {
            if(task.requests[tid].status == LINK_STATUS_WORKING)
            {
                p = buf;
                n = sprintf(p, "GET %s HTTP/1.0\r\nHOST: %s\r\n"
                        "Connection:close\r\nUser-Agent: Hispider\r\n\r\n",
                        task.requests[tid].path, task.requests[tid].host);
                conn->push_chunk(conn, buf, n);
                DEBUG_LOGGER(daemon_logger, "ready for visit:http://%s%s "
                        "on %s:%d via %d", 
                        task.requests[tid].host, task.requests[tid].path,
                        conn->ip, conn->port, conn->fd);
                return 0;
            }
        }
        conn->over_cstate(conn);
        conn->over(conn);
    }
    return -1;
}

int cb_trans_oob_handler(CONN *conn, CB_DATA *oob)
{
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
	char *logfile = NULL, *s = NULL, *p = NULL;
	int i = 0, n = 0, interval = 0;
	int ret = 0;

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

    /* TRANSPORT */
    if((transport = service_init()) == NULL)
	{
		fprintf(stderr, "Initialize serv failed, %s", strerror(errno));
		_exit(-1);
    }
    if((transport = service_init()) == NULL)
    {
        fprintf(stderr, "Initialize transport failed, %s", strerror(errno));
        _exit(-1);
    }
    transport->family = iniparser_getint(dict, "TRANSPORT:inet_family", AF_INET);
    transport->sock_type = iniparser_getint(dict, "TRANSPORT:socket_type", SOCK_STREAM);
    transport->ip = iniparser_getstr(dict, "TRANSPORT:service_ip");
    transport->port = iniparser_getint(dict, "TRANSPORT:service_port", 80);
    transport->working_mode = iniparser_getint(dict, "TRANSPORT:working_mode", WORKING_PROC);
    transport->service_type = iniparser_getint(dict, "TRANSPORT:service_type", C_SERVICE);
    transport->service_name = iniparser_getstr(dict, "TRANSPORT:service_name");
    transport->nprocthreads = iniparser_getint(dict, "TRANSPORT:nprocthreads", 1);
    transport->ndaemons = iniparser_getint(dict, "TRANSPORT:ndaemons", 0);
    transport->set_log(transport, iniparser_getstr(dict, "TRANSPORT:logfile"));
    transport->session.packet_type = iniparser_getint(dict, "TRANSPORT:packet_type",
            PACKET_DELIMITER);
    transport->session.packet_delimiter = iniparser_getstr(dict, "TRANSPORT:packet_delimiter");
    p = s = transport->session.packet_delimiter;
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
    transport->session.packet_delimiter_length = strlen(transport->session.packet_delimiter);
    transport->session.buffer_size = iniparser_getint(dict, "TRANSPORT:buffer_size", SB_BUF_SIZE);
    transport->session.packet_reader = &cb_trans_packet_reader;
    transport->session.packet_handler = &cb_trans_packet_handler;
    transport->session.data_handler = &cb_trans_data_handler;
    transport->session.oob_handler = &cb_trans_oob_handler;
    transport->client_connections_limit = iniparser_getint(dict,
            "TRANSPORT:client_connections_limit", 8);
    interval = iniparser_getint(dict, "TRANSPORT:heartbeat_interval", SB_HEARTBEAT_INTERVAL);
    transport->set_heartbeat(transport, interval, &cb_trans_heartbeat_handler, transport);
	if((ret = sbase->add_service(sbase, transport)) != 0)
	{
		fprintf(stderr, "Initiailize service[%s] failed, %s\n", 
                transport->service_name, strerror(errno));
		return ret;
	}
    //logger 
	LOGGER_INIT(daemon_logger, iniparser_getstr(dict, "TRANSPORT:access_log"));
    //daemon ip and ip
    task.daemon_ip = iniparser_getstr(dict, "TRANSPORT:daemon_ip");
    task.daemon_port = iniparser_getint(dict, "TRANSPORT:daemon_port", 3721);
    //timeout
    task.timeout = 60000000;
    if((p = iniparser_getstr(dict, "TRANSPORT:timeout")))
        task.timeout = str2ll(p);
    //linktable files
    task.ntask_limit = iniparser_getint(dict, "TRANSPORT:ntask", 128);
    if((task.requests = (HTTP_REQUEST *)calloc(task.ntask_limit, sizeof(HTTP_REQUEST)))
        && (task.tasks = (DOCMETA *)calloc(task.ntask_limit, sizeof(DOCMETA)))
        && (task.conns = (CONN **)calloc(task.ntask_limit, sizeof(CONN *)))
        && (task.results = (char **)calloc(task.ntask_limit, sizeof(char *))))
    {
        for(i = 0; i < task.ntask_limit; i++) task.requests[i].id = -1;
        return 0;
    }
    return -1;
}

static void cb_stop(int sig){
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
	// locale
	setlocale(LC_ALL, "C");
	// signal
	signal(SIGTERM, &cb_stop);
	signal(SIGINT,  &cb_stop);
	signal(SIGHUP,  &cb_stop);
	signal(SIGPIPE, SIG_IGN);
	pid = fork();
	switch (pid) {
		case -1:
			perror("fork()");
			exit(EXIT_FAILURE);
			break;
		case 0: //child process
			if(setsid() == -1)
				exit(EXIT_FAILURE);
			break;
		default://parent
			_exit(EXIT_SUCCESS);
			break;
	}

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
        //sbase->running(sbase, 3600);
        sbase->running(sbase, 0);
}
