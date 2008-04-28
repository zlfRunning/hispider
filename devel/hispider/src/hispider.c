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
SERVICE *serv = NULL;
static dictionary *dict = NULL;
static LOGGER *daemon_logger = NULL;
static long long global_timeout_times = 60000000;
static char *daemon_ip = NULL;
static int daemon_port = 3721;
typedef struct _TASK
{
    CONN *conn;
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
//error handler 
void cb_server_error_handler(CONN *conn)
{
    if(conn)
    {
        if(conn == task.conn)
        {
            task.conn = NULL;
        }
        else
        {
            
        }
    }
}

//daemon task handler 
void cb_serv_task_handler(void *arg);

void cb_serv_heartbeat_handler(void *arg)
{
    int  n = 0, tid = 0;
    long taskid = 0;
    CONN *conn = NULL;

    if(serv)
    {
        if(task.conn == NULL && daemon_ip)
        {
            if((task.conn = serv->newconn(serv, daemon_ip, daemon_port)))
                task.conn->start_cstate(task.conn);
        }
        //get new request
        if(task.conn && task.ntask_total < task.ntask_limit)
        {
        }
        //handle over task
        if(task.conn && task.ntask_over > 0)
        {
            
        }
    }
    return  ;
}

//daemon task handler 
void cb_serv_task_handler(void *arg)
{
}

int cb_serv_packet_reader(CONN *conn, BUFFER *buffer)
{
}

void cb_serv_packet_handler(CONN *conn, BUFFER *packet)
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
        http_response_parse(p, end, &response);
        if(conn == task.conn)
        {
            if(response.respid == RESP_OK && response.headers[HEAD_ENT_CONTENT_LENGTH])
            {
                len = atoi(response.headers[HEAD_ENT_CONTENT_LENGTH]);       
                conn->recv_chunk(conn, len);
                return ;
            }
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
            c_id = conn->c_id;
            if(task.tasks)
            {
                task.tasks[c_id].status = URL_STATUS_ERROR;
                conn->over_cstate(conn);
                conn->over(conn);
                return ;
            }
        }
    }
    return ;
}

void cb_serv_data_handler(CONN *conn, BUFFER *packet, 
        CHUNK *chunk, BUFFER *cache)
{
    URLMETA *purlmeta = NULL;
    HTTP_RESPONSE *resp = NULL;
    HTTP_REQUEST *req = NULL;
    char buf[HTTP_BUF_SIZE];
    char *data = NULL, *zdata = NULL, *p = NULL;
    int i = 0, c_id = 0, n = 0, nhost = 0, npath = 0;
    unsigned long nzdata = 0;

    if(conn && chunk->buf && (req = (HTTP_REQUEST *)chunk->buf->data))
    {
        if(conn == task.conn)
        {
            c_id = task.conn->c_id;
            if(chunk->buf->size == sizeof(HTTP_REQUEST))
            {
                memcpy(&(task.requests[c_id]), chunk->buf->data, sizeof(HTTP_REQUEST));
                task.ntask_wait++;
            }
        }
        else
        {
            resp = (HTTP_RESPONSE *)cache->data;
            p = buf;
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
                if(resp->headers[i])
                {
                    p += sprintf(p, "[%d:%s:%s]", i, http_headers[i].e, resp->headers[i]);                   
                }
            }
            c_id = conn->c_id;
            req = &(task.requests[c_id]); 
            nhost = strlen(req->host) + 1;
            npath = strlen(req->path) + 1;
            purlmeta = &(task.tasks[c_id]);
            memset(purlmeta, 0, sizeof(URLMETA));
            purlmeta->hostoff = (p - buf);
            purlmeta->pathoff =  purlmeta->hostoff + nhost;
            purlmeta->htmloff = purlmeta->pathoff + npath;
            purlmeta->size = purlmeta->htmloff + chunk->buf->size;
            purlmeta->status = URL_STATUS_ERROR;
            task.ntask_over++;
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
                if((zdata = (char *)calloc(1, purlmeta->size)))
                {
                    nzdata = purlmeta->size;
                    if(zcompress(p, purlmeta->size, zdata, &(nzdata)) == 0)
                    {
                        purlmeta->status = URL_STATUS_OVER;
                        purlmeta->zsize = nzdata;
                        task.results[i] = zdata;
                    }
                }
                free(data);
                data = NULL;
            }
        }
    }
}

//daemon transaction handler 
void cb_serv_transaction_handler(CONN *conn, int tid)
{
    char buf[HTTP_BUF_SIZE];
    char *p = NULL;
    int n = 0;

    if(conn && tid >= 0 && tid < task.ntask_limit)
    {
        //daemon connection 
        if(conn == task.conn)
        {
            if(task.results[tid])
            {
                p = buf;
                n = sprintf(p, "PUT / HTTP/1.0\r\nContent-Length : %d\r\n\r\n", 
                        sizeof(URLMETA) + task.tasks[tid].zsize); 
                conn->push_chunk(conn, p, n);
                conn->push_chunk(conn, &(task.tasks[tid]), sizeof(URLMETA));
                conn->push_chunk(conn, task.results[tid], task.tasks[tid].zsize);
                memset(&(task.requests[tid]), 0, sizeof(HTTP_REQUEST));
                task.requests[tid].id = -1;
                memset(&(task.tasks[tid]), 0, sizeof(URLMETA));
                task.tasks[tid].id = -1;
                free(task.results[tid]);
                task.results[tid] = NULL;
            }
            if(task.requests[tid].id == -1)
            {
                n = sprintf(p, "TASK / HTTP/1.0\r\n\r\n");
                conn->push_chunk(conn, p, n);
            }
        }
        else
        {
            if(task.requests[tid].id != -1)
            {
                n = sprintf(p, "GET %s HTTP/1.0\r\nHOST: %s User-Agent:Mozilla\r\n\r\n",
                        task.requests[tid].path, task.requests[tid].host);
                conn->push_chunk(conn, p, n);
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

void cb_serv_oob_handler(CONN *conn, BUFFER *oob)
{
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
	char *logfile = NULL, *s = NULL, *p = NULL;
	int n = 0, ntask = 0;
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
    /* DAEMON */
    if((serv = service_init()) == NULL)
	{
		fprintf(stderr, "Initialize serv failed, %s", strerror(errno));
		_exit(-1);
	}
    /* service type */
	serv->service_type = iniparser_getint(dict, "DAEMON:service_type", 0);
	/* INET protocol family */
	n = iniparser_getint(dict, "DAEMON:inet_family", 0);
	/* INET protocol family */
	n = iniparser_getint(dict, "DAEMON:inet_family", 0);
	switch(n)
	{
		case 0:
			serv->family = AF_INET;
			break;
		case 1:
			serv->family = AF_INET6;
			break;
		default:
			fprintf(stderr, "Illegal INET family :%d \n", n);
			_exit(-1);
	}
	/* socket type */
	n = iniparser_getint(dict, "DAEMON:socket_type", 0);
	switch(n)
	{
		case 0:
			serv->socket_type = SOCK_STREAM;
			break;
		case 1:
			serv->socket_type = SOCK_DGRAM;
			break;
		default:
			fprintf(stderr, "Illegal socket type :%d \n", n);
			_exit(-1);
	}
	/* serv name and ip and port */
	serv->name = iniparser_getstr(dict, "DAEMON:service_name");
	serv->ip = iniparser_getstr(dict, "DAEMON:service_ip");
	if(serv->ip && serv->ip[0] == 0 ) serv->ip = NULL;
	serv->port = iniparser_getint(dict, "DAEMON:service_port", 80);
	serv->max_procthreads = iniparser_getint(dict, "DAEMON:max_procthreads", 1);
	serv->sleep_usec = iniparser_getint(dict, "DAEMON:sleep_usec", 100);
	serv->heartbeat_interval = iniparser_getint(dict, "DAEMON:heartbeat_interval", 1000000);
	logfile = iniparser_getstr(dict, "DAEMON:logfile");
	serv->logfile = logfile;
	logfile = iniparser_getstr(dict, "DAEMON:evlogfile");
	serv->evlogfile = logfile;
	serv->max_connections = iniparser_getint(dict, "DAEMON:max_connections", MAX_CONNECTIONS);
	serv->packet_type = PACKET_DELIMITER;
	serv->packet_delimiter = iniparser_getstr(dict, "DAEMON:packet_delimiter");
	p = s = serv->packet_delimiter;
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
	serv->packet_delimiter_length = strlen(serv->packet_delimiter);
	serv->buffer_size = iniparser_getint(dict, "DAEMON:buffer_size", SB_BUF_SIZE);
	serv->cb_packet_reader = &cb_serv_packet_reader;
	serv->cb_packet_handler = &cb_serv_packet_handler;
	serv->cb_data_handler = &cb_serv_data_handler;
	serv->cb_transaction_handler = &cb_serv_transaction_handler;
	serv->cb_oob_handler = &cb_serv_oob_handler;
	serv->cb_heartbeat_handler = &cb_serv_heartbeat_handler;
        /* server */
	if((ret = sbase->add_service(sbase, serv)) != 0)
	{
		fprintf(stderr, "Initiailize service[%s] failed, %s\n", serv->name, strerror(errno));
		return ret;
	}
    //logger 
	daemon_logger = logger_init(iniparser_getstr(dict, "DAEMON:access_log"));
    //timeout
    if((p = iniparser_getstr(dict, "DAEMON:timeout")))
        task.timeout = str2ll(p);
    //linktable files
    task.ntask_limit = iniparser_getint(dict, "DAEMON:ntask", 128);
    if((task.requests = (HTTP_REQUEST *)calloc(task.ntask_limit, sizeof(HTTP_REQUEST)))
        && (task.tasks = (URLMETA *)calloc(task.ntask_limit, sizeof(URLMETA)))
        && (task.results = (char **)calloc(task.ntask_limit, sizeof(char *))))
    {
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
