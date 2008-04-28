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

static SBASE *sbase = NULL;
SERVICE *serv = NULL;
static dictionary *dict = NULL;
static LOGGER *daemon_logger = NULL;
static long long global_timeout_times = 60000000;
static char *daemon_ip = NULL;
static int daemon_port = 3721;
static CONN *c_conn = NULL;
static int global_ntask = 0;
static int global_ntask_total = 0;
static int global_ntask_wait = 0;
static int global_ntask_over = 0;
static int global_ntask_working = 0;
static HTTP_REQUEST *requests = NULL;
static URLMETA *tasks = NULL;
static char **results = NULL;

//error handler 
void cb_server_error_handler(CONN *conn)
{
    if(conn)
    {
        if(conn == c_conn)
        {
            c_conn = NULL;
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

    if(serv && linktable)
    {
        if(c_conn == NULL && daemon_ip)
        {
            if((c_conn = serv->newconn(serv, daemon_ip, daemon_port)))
                c_conn->start_cstate(c_conn);
        }
        if(c_conn && requests)
        {
            for(i = 0; i < nrequest; i++)
            {
                if(requests[i].status != URL_STATUS_)
            }
        }
    }
    return  ;
}

//daemon task handler 
void cb_serv_task_handler(void *arg)
{
    long taskid = (long )arg;
    if(taskid >= 0)
    {
        DEBUG_LOGGER(daemon_logger, "start task:%d", taskid);
        linktable->urlhandler(linktable, taskid);
        DEBUG_LOGGER(daemon_logger, "Completed task:%d", taskid);
        //fprintf(stdout, "%d:task:%d\n", __LINE__, taskid);
    }
}

int cb_serv_packet_reader(CONN *conn, BUFFER *buffer)
{
}

void cb_serv_packet_handler(CONN *conn, BUFFER *packet)
{
    HTTP_RESPONSE response;
    char *p = NULL, *end = NULL;
    int len = 0, sid = 0;
    
    if(conn)
    {
        memset(&response, 0, sizeof(HTTP_RESPONSE));
        response.respid = -1;
        p   = (char *)packet->data;
        end = (char *)packet->end;
        http_response_parse(p, end, &response);
        if(conn == c_conn)
        {
            if(response.respid == RESP_OK && response.headers[HEAD_ENT_CONTENT_LENGTH])
            {
                len = atoi(response.headers[HEAD_ENT_CONTENT_LENGTH]);       
                conn->receive_chunk(conn, len);
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
            if(tasks)
            {
                tasks[c_id].status = URL_STATUS_ERROR;
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
    char *data = NULL, *zdata = NULL;
    int i = 0, c_id = 0, n = 0, nhost = 0, npath = 0;
    uLong nzdata = 0;

    if(conn && chunk->buf && (req = (HTTP_REQUEST *)chunk->buf->data))
    {
        if(conn == c_conn)
        {
            if(chunk->buf->size == sizeof(HTTP_REQUEST))
            {
                for(i = 0; i < ntask_global; i++)
                {
                    if(request[i].id == -1)
                    {
                        memcpy(&(request[i]), chunk->buf->data, sizeof(HTTP_REQUEST));
                        global_ntask_wait++;
                        break;
                    }
                }
            }
        }
        else
        {
            resp = (HTTP_RESPONSE *)cache->data;
            memset(&urlmeta, 0, sizeof(urlmeta));
            p = buf;
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
                if(resp->headers[i])
                {
                    p += sprintf(p, "[%d:%s:%s]", i, http_headers[i].e, resp->headers[i]);                   
                }
            }
            c_id = conn->c_id;
            req = &(requests[c_id]); 
            nhost = strlen(req->host) + 1;
            npath = strlen(req->path) + 1;
            purlmeta = &(tasks[c_id]);
            purlmeta->hostoff = (p - buf);
            purlmeta->pathoff =  purlmeta->hostoff + nhost;
            purlmeta->htmloff = purlmeta->pathoff + npath;
            purlmeta->size = purlmeta->htmloff + chunk->buf->size;
            purlmeta->status = URL_STATUS_ERROR;
            global_ntask_wait++;
            if((data = (char *)calloc(1, purlmeta.size)))
            {
                p = data;
                memcpy(p, buf, purlmeta->hostoff);
                p += urlmeta.hostoff;
                memcpy(p, req->host, nhost)
                p += nhost;
                memcpy(p, req->path, npath);
                p += npath;
                memcpy(p, chunk->buf->data, chunk->buf->size);
                if((zdata = (char *)calloc(1, purlmeta->size)))
                {
                    nzdata = urlmeta.size;
                    if(zcompress(p, urlmeta.size, zdata, &(nzdata)) == 0)
                    {
                        purlmeta->status = URL_STATUS_OVER;
                        purlmeta->zsize = nzdata;
                        result[i] = zdata;
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
    if(conn && tid >= 0 )
    {
        
    }
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
	/* TRANSPORT */
	fprintf(stdout, "Parsing TRANSPORT...\n");
	if((transport = service_init()) == NULL)
	{
		fprintf(stderr, "Initialize transport failed, %s", strerror(errno));
		_exit(-1);
	}
    /* service type */
    transport->service_type = iniparser_getint(dict, "TRANSPORT:service_type", 1);
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
	if(logfile == NULL) logfile = TRANSPORT_LOG;
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
	if((ret = sbase->add_service(sbase, transport)) != 0)
	{
		fprintf(stderr, "Initiailize service[%s] failed, %s\n", transport->name, strerror(errno));
		return ret;
	}
    //logger 
	daemon_logger = logger_init(iniparser_getstr(dict, "DAEMON:access_log"));
    //timeout
    if((p = iniparser_getstr(dict, "DAEMON:timeout")))
        global_timeout_times = str2ll(p);
    //linktable files
    ntask = iniparser_getint(dict, "DAEMON:ntask", 128);
    return 0;
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
