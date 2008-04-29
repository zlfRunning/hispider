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
#define HTTP_CHUNK_MAX 1048576
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
    URLMETA *tasks;
    char **results;
}TASK;
static TASK task = {0};
#define NCONN(tp, n) ((tp.conns[n])?(tp.conns[n])\
        :(tp.conns[n] = transport->newconn(transport, tp.daemon_ip, tp.daemon_port)))
#define DCONN(tp, n) (tp.conns[n] = NULL)
#define NEWREQ(tp, n)                                       \
{                                                           \
    tp.requests[n].id = -2;                                 \
    memset(&(tp.tasks[n]), 0, sizeof(URLMETA));             \
    tp.tasks[n].id = -1;                                    \
    tp.ntask_total++;                                       \
    tp.conns[n]->c_id = n;                                  \
    tp.conns[n]->start_cstate(tp.conns[n]);                 \
    tp.conns[n]->set_timeout(tp.conns[n], tp.timeout);      \
    transport->newtransaction(transport, tp.conns[n], n);   \
    DEBUG_LOGGER(daemon_logger, "NEW REQUEST");             \
}
#define OVERREQ(tp, n)                                      \
{                                                           \
    memset(&(tp.requests[n]), 0, sizeof(HTTP_REQUEST));     \
    tp.requests[n].id = -1;                                 \
    tp.ntask_total--;                                       \
    DEBUG_LOGGER(daemon_logger, "OVER REQUEST");            \
}
#define CLEARREQ(tp, n)                                     \
{                                                           \
    tp.conns[n]->over_cstate(tp.conns[n]);                  \
    tp.conns[n]->over(tp.conns[n]);                         \
    tp.conns[n] = NULL;                                     \
}
#define NEWTASK(tp, n, rq)                                  \
{                                                           \
    memcpy(&(tp.requests[n]), rq, sizeof(HTTP_REQUEST));    \
    tp.tasks[n].id = tp.requests[n].id;                     \
    tp.ntask_wait++;                                        \
    DEBUG_LOGGER(daemon_logger, "NEW TASK %s:%d",           \
            tp.requests[n].ip, tp.requests[n].port);        \
}
#define ENDTASK(tp, n, st)                                  \
{                                                           \
    tp.requests[n].status = st;                             \
    tp.ntask_over++;                                        \
    DEBUG_LOGGER(daemon_logger, "TASK END %s:%d",           \
            tp.requests[n].ip, tp.requests[n].port);        \
}
#define OVERTASK(tp, n)                                     \
{                                                           \
    memset(&(tp.tasks[n]), 0, sizeof(URLMETA));             \
    tp.tasks[n].id = -1;                                    \
    tp.ntask_over--;                                        \
}
//error handler 
void cb_server_error_handler(CONN *conn)
{
    HTTP_REQUEST *req = NULL;
    HTTP_RESPONSE *resp = NULL;
    URLMETA *purlmeta = NULL;
    char buf[HTTP_BUF_SIZE];
    char *data = NULL, *zdata = NULL, *p = NULL;
    int i = 0, n = 0, nhost = 0, npath = 0, c_id = 0, status = 0;
    unsigned long nzdata = 0;

    if(conn)
    {
        c_id = conn->c_id;
        if(conn == task.conns[c_id])
        {
            OVERREQ(task, c_id);
            CLEARREQ(task, c_id);
        }
        else
        {
            purlmeta = &(task.tasks[c_id]);
            req = &(task.requests[c_id]); 
            req->status = LINK_STATUS_ERROR;
            if(conn->cache->data)
            {
                resp = (HTTP_RESPONSE *)conn->cache->data;
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
                purlmeta->hostoff = (p - buf);
                purlmeta->pathoff =  purlmeta->hostoff + nhost;
                purlmeta->htmloff = purlmeta->pathoff + npath;
                purlmeta->size = purlmeta->htmloff + conn->chunk->buf->size;
                if((data = (char *)calloc(1, purlmeta->size)))
                {
                    p = data;
                    memcpy(p, buf, purlmeta->hostoff);
                    p += purlmeta->hostoff;
                    memcpy(p, req->host, nhost);
                    p += nhost;
                    memcpy(p, req->path, npath);
                    p += npath;
                    memcpy(p, conn->chunk->buf->data, conn->chunk->buf->size);
                    if((zdata = (char *)calloc(1, purlmeta->size)))
                    {
                        nzdata = purlmeta->size;
                        if(zcompress(p, purlmeta->size, zdata, &(nzdata)) == 0)
                        {
                            req->status = LINK_STATUS_OVER;
                            purlmeta->zsize = nzdata;
                            task.results[i] = zdata;
                        }
                    }
                    free(data);
                    data = NULL;
                }
            }
        }
        ENDTASK(task, c_id, req->status);
    }
    return ;
}

//daemon task handler 
void cb_trans_task_handler(void *arg);

void cb_trans_heartbeat_handler(void *arg)
{
    int  n = 0, tid = 0, i = 0;
    CONN *conn = NULL;

    if(transport)
    {
        //DEBUG_LOGGER(daemon_logger, "ntask_total:%d ntask_wait:%d ntask_over:%d",
          //      task.ntask_total, task.ntask_wait, task.ntask_over);
        //get new request
        if(task.ntask_total < task.ntask_limit)
        {
            for(i = 0; i < task.ntask_limit; i++)
            {
                if(task.requests[i].id == -1) 
                {
                    if((conn = NCONN(task, i)))
                    {
                        NEWREQ(task, i);
                    }
                    break;
                }
            }
        }
        //new task 
        if(task.ntask_wait > 0)
        {
            for(i = 0; i < task.ntask_limit; i++)
            {
                if(task.requests[i].id >= 0 
                        && task.requests[i].status == LINK_STATUS_INIT
                        && strlen(task.requests[i].ip) > 0)

                {
                    if((conn = transport->newconn(transport, task.requests[i].ip, 
                                    task.requests[i].port)))
                    {
                        conn->start_cstate(conn);
                        conn->c_id = i;
                        conn->set_timeout(conn, task.timeout);
                        DEBUG_LOGGER(daemon_logger, "Set timeout[%lld] on %s:%d via %d",
                                task.timeout, conn->ip, conn->port, conn->fd);
                        task.ntask_wait--;
                        task.requests[i].status = LINK_STATUS_WAIT;
                        transport->newtransaction(transport, conn, i);
                    }
                    break;
                }
            }
        }
        //handle over task
        if(task.ntask_over > 0)
        {
           for(i = 0; i < task.ntask_limit; i++)
           {
               if(task.requests[i].id != -1)
               {
                   if(task.requests[i].status == LINK_STATUS_OVER 
                           || task.requests[i].status == LINK_STATUS_ERROR)
                   {
                       if((conn = NCONN(task, i)))
                       {
                           task.tasks[i].id = task.requests[i].id;
                           task.tasks[i].status = task.requests[i].status;
                           conn->c_id = i;
                           transport->newtransaction(transport, conn, i);
                       }
                   }
               }
           }
        }
    }
    return  ;
}

//daemon task handler 
void cb_trans_task_handler(void *arg)
{
}

int cb_trans_packet_reader(CONN *conn, BUFFER *buffer)
{
}

void cb_trans_packet_handler(CONN *conn, BUFFER *packet)
{
    HTTP_RESPONSE response;
    char *p = NULL, *end = NULL;
    int len = 0, c_id = 0;

    if(conn)
    {
        memset(&response, 0, sizeof(HTTP_RESPONSE));
        response.respid = -1;
        p   = (char *)packet->data;
        end = (char *)packet->end;
        if(http_response_parse(p, end, &response) == -1) goto end;
        c_id = conn->c_id;
        if(conn == task.conns[c_id])
        {
            if(response.respid == RESP_OK)
            {
                if(response.headers[HEAD_ENT_CONTENT_LENGTH])
                {
                    len = atoi(response.headers[HEAD_ENT_CONTENT_LENGTH]);       
                    conn->recv_chunk(conn, len);
                }
            }
            else
            {
                OVERREQ(task, c_id);
                CLEARREQ(task, c_id);
            }
            return ;
        }
        else
        {
            if(response.respid == RESP_OK)
            {
                if(response.headers[HEAD_ENT_CONTENT_TYPE]
                        && strncasecmp(response.headers[HEAD_ENT_CONTENT_TYPE], "text", 4) == 0)
                {
                    if(response.headers[HEAD_ENT_CONTENT_LENGTH])
                    {
                        len = atoi(response.headers[HEAD_ENT_CONTENT_LENGTH]);       
                        conn->cache->push(conn->cache, &response, sizeof(HTTP_RESPONSE));
                        conn->recv_chunk(conn, len);
                    }
                    else
                    {
                        conn->cache->push(conn->cache, &response, sizeof(HTTP_RESPONSE));
                        conn->recv_chunk(conn, HTTP_CHUNK_MAX);
                    }
                    return ;
                }
            }
        }
end:
        DEBUG_LOGGER(daemon_logger, "Invalid response on %s:%d via %d", 
                conn->ip, conn->port, conn->fd);
        conn->over_cstate(conn);
        conn->over(conn);
        ENDTASK(task, c_id, LINK_STATUS_ERROR);
    }
    return ;
}

void cb_trans_data_handler(CONN *conn, BUFFER *packet, 
        CHUNK *chunk, BUFFER *cache)
{
    URLMETA *purlmeta = NULL;
    HTTP_RESPONSE *resp = NULL;
    HTTP_REQUEST *req = NULL;
    char buf[HTTP_BUF_SIZE];
    char *data = NULL, *zdata = NULL, *p = NULL;
    int i = 0, c_id = 0, n = 0, nhost = 0, npath = 0, status = 0;
    unsigned long nzdata = 0, ndata = 0;

    if(conn && chunk->buf)
    {
        c_id = conn->c_id;
        if(conn == task.conns[c_id])
        {
            if(chunk->buf->size == sizeof(HTTP_REQUEST))
            {
                NEWTASK(task, c_id, chunk->buf->data);
            }
            else
            {
                OVERREQ(task, c_id);
                CLEARREQ(task, c_id);
            }
            return ;
        }
        else
        {
            c_id = conn->c_id;
            purlmeta = &(task.tasks[c_id]);
            req = &(task.requests[c_id]); 
            memset(purlmeta, 0, sizeof(URLMETA));
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
            purlmeta->hostoff = (p - buf);
            purlmeta->pathoff =  purlmeta->hostoff + nhost;
            purlmeta->htmloff = purlmeta->pathoff + npath;
            purlmeta->size = purlmeta->htmloff + chunk->buf->size;
            if((data = (char *)calloc(1, purlmeta->size)))
            {
                p = data;
                memcpy(p, buf, purlmeta->hostoff);
                p += purlmeta->hostoff;
                memcpy(p, req->host, nhost);
                p += nhost;
                memcpy(p, req->path, npath);
                p += npath;
                memcpy(p, chunk->buf->data, chunk->buf->size);
                p += chunk->buf->size;
                ndata = purlmeta->size;
                DEBUG_LOGGER(daemon_logger, 
                "nhost:%d npath:%d hostoff:%d htmloff:%d off:%d dsize:%d ndata:%d", 
                nhost, npath, purlmeta->hostoff, purlmeta->htmloff, 
                (p - data), chunk->buf->size, ndata);
                if((zdata = (char *)calloc(1, ndata)))
                {
                    nzdata = purlmeta->size;
                    if(zcompress(data, ndata, zdata, &(nzdata)) == 0)
                    {
                        DEBUG_LOGGER(daemon_logger, "compress %d  to %d body:%d ",
                                ndata, nzdata, chunk->buf->size);
                        req->status = LINK_STATUS_OVER;
                        purlmeta->zsize = nzdata;
                        task.results[c_id] = zdata;
                    }
                }
                free(data);
                data = NULL;
            }
            //complete data and over connection
            conn->over_cstate(conn);
            conn->over(conn);
            ENDTASK(task, c_id, req->status);
        }
    }
    return ;
}

//daemon transaction handler 
void cb_trans_transaction_handler(CONN *conn, int tid)
{
    char buf[HTTP_BUF_SIZE];
    char *p = NULL;
    int n = 0;

    if(conn && tid >= 0 && tid < task.ntask_limit)
    {
        if(conn == task.conns[tid])
        {
            if(task.requests[tid].id == -2)
            {
                p = buf;
                n = sprintf(p, "TASK / HTTP/1.0\r\n\r\n");
                conn->push_chunk(conn, buf, n);
                return ;
            }
            if(task.tasks[tid].id  != -1)
            {
                p = buf;
                n = sprintf(p, "PUT / HTTP/1.0\r\nContent-Length: %d\r\n\r\n", 
                        (sizeof(URLMETA) + task.tasks[tid].zsize)); 
                conn->push_chunk(conn, buf, n);
                conn->push_chunk(conn, &(task.tasks[tid]), sizeof(URLMETA));
                if(task.results[tid])
                {
                    conn->push_chunk(conn, task.results[tid], task.tasks[tid].zsize);
                    free(task.results[tid]);
                    task.results[tid] = NULL;
                }
                OVERREQ(task, tid);
                OVERTASK(task, tid);
                return ;
            }
        }
        else
        {
            if(task.requests[tid].id != -1 && task.requests[tid].status == LINK_STATUS_WAIT)
            {
                task.requests[tid].status = LINK_STATUS_WORKING;
                p = buf;
                n = sprintf(p, "GET %s HTTP/1.0\r\nHOST: %s\r\nUser-Agent: Mozilla\r\n\r\n",
                        task.requests[tid].path, task.requests[tid].host);
                conn->push_chunk(conn, buf, n);
                //DEBUG_LOGGER(daemon_logger, "ready for visit:%s on %s:%d via %d", 
                  //      buf, conn->ip, conn->port, conn->fd);

            }
            else
            {
                conn->over_cstate(conn);
                conn->over(conn);
            }
        }
    }
    return ;
}

void cb_trans_oob_handler(CONN *conn, BUFFER *oob)
{
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
	char *logfile = NULL, *s = NULL, *p = NULL;
	int i = 0, n = 0;
	int ret = 0;

	if((dict = iniparser_new(conf)) == NULL)
	{
		fprintf(stderr, "Initializing conf:%s failed, %s\n", conf, strerror(errno));
		_exit(-1);
	}
	/* SBASE */
	//fprintf(stdout, "Parsing SBASE...\n");
	sbase->working_mode = iniparser_getint(dict, "SBASE:working_mode", WORKING_PROC);
	sbase->max_procthreads = iniparser_getint(dict, "SBASE:max_procthreads", 1);
	if(sbase->max_procthreads > MAX_PROCTHREADS) sbase->max_procthreads = MAX_PROCTHREADS;
	sbase->sleep_usec = iniparser_getint(dict, "SBASE:sleep_usec", MIN_SLEEP_USEC);
	if((logfile = iniparser_getstr(dict, "SBASE:logfile")) == NULL) logfile = SBASE_LOG;
	fprintf(stdout, "Parsing LOG[%s]...\n", logfile);
	fprintf(stdout, "SBASE[%08x] sbase->evbase:%08x ...\n", sbase, sbase->evbase);
	sbase->set_log(sbase, logfile);
	if((logfile = iniparser_getstr(dict, "SBASE:evlogfile")))
	    sbase->set_evlog(sbase, logfile);
    /* TRANSPORT */
    if((transport = service_init()) == NULL)
	{
		fprintf(stderr, "Initialize serv failed, %s", strerror(errno));
		_exit(-1);
	}
    /* service type */
	transport->service_type = iniparser_getint(dict, "TRANSPORT:service_type", 0);
	/* INET protocol family */
	n = iniparser_getint(dict, "TRANSPORT:inet_family", 0);
	/* INET protocol family */
	n = iniparser_getint(dict, "TRANSPORT:inet_family", 0);
	switch(n)
	{
		case 0:
			transport->family = AF_INET;
			break;
		case 1:
			transport->family = AF_INET6;
			break;
		default:
			fprintf(stderr, "Illegal INET family :%d \n", n);
			_exit(-1);
	}
	/* socket type */
	n = iniparser_getint(dict, "TRANSPORT:socket_type", 0);
	switch(n)
	{
		case 0:
			transport->socket_type = SOCK_STREAM;
			break;
		case 1:
			transport->socket_type = SOCK_DGRAM;
			break;
		default:
			fprintf(stderr, "Illegal socket type :%d \n", n);
			_exit(-1);
	}
	/* serv name and ip and port */
	transport->name = iniparser_getstr(dict, "TRANSPORT:service_name");
	transport->ip = iniparser_getstr(dict, "TRANSPORT:service_ip");
	if(transport->ip && transport->ip[0] == 0 ) transport->ip = NULL;
	transport->port = iniparser_getint(dict, "TRANSPORT:service_port", 80);
	transport->max_procthreads = iniparser_getint(dict, "TRANSPORT:max_procthreads", 1);
	transport->sleep_usec = iniparser_getint(dict, "TRANSPORT:sleep_usec", 100);
	transport->heartbeat_interval = iniparser_getint(dict, "TRANSPORT:heartbeat_interval", 1000000);
	logfile = iniparser_getstr(dict, "TRANSPORT:logfile");
	transport->logfile = logfile;
	logfile = iniparser_getstr(dict, "TRANSPORT:evlogfile");
	transport->evlogfile = logfile;
	transport->max_connections = iniparser_getint(dict, 
            "TRANSPORT:max_connections", MAX_CONNECTIONS);
	transport->packet_type = PACKET_DELIMITER;
	transport->packet_delimiter = iniparser_getstr(dict, "TRANSPORT:packet_delimiter");
	p = s = transport->packet_delimiter;
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
	transport->packet_delimiter_length = strlen(transport->packet_delimiter);
	transport->buffer_size = iniparser_getint(dict, "TRANSPORT:buffer_size", SB_BUF_SIZE);
	transport->cb_packet_reader = &cb_trans_packet_reader;
	transport->cb_packet_handler = &cb_trans_packet_handler;
	transport->cb_data_handler = &cb_trans_data_handler;
	transport->cb_transaction_handler = &cb_trans_transaction_handler;
	transport->cb_oob_handler = &cb_trans_oob_handler;
	transport->cb_heartbeat_handler = &cb_trans_heartbeat_handler;
        /* server */
	if((ret = sbase->add_service(sbase, transport)) != 0)
	{
		fprintf(stderr, "Initiailize service[%s] failed, %s\n", transport->name, strerror(errno));
		return ret;
	}
    //logger 
	daemon_logger = logger_init(iniparser_getstr(dict, "TRANSPORT:access_log"));
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
        && (task.tasks = (URLMETA *)calloc(task.ntask_limit, sizeof(URLMETA)))
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
