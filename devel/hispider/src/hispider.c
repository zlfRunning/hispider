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
#define DEFAULT_CONTENT_LENGTH 1048576
#define SBASE_LOG "/tmp/sbase.log"
#define TRANSPORT_LOG "/tmp/transport.log"
static const char *__html__body__  = 
"<HTML><HEAD>\n"
"<TITLE>Hispider Running Status</TITLE>\n"
"<meta http-equiv='refresh' content='2; URL=/'>\n</HEAD>\n"
"<meta http-equiv='content-type' content='text/html; charset=UTF-8'>\n"
"<BODY bgcolor='#000000' align=center >\n"
"<h1><font color=white >Hispider Running Status</font></h1>\n"
"<hr noshade><ul><br><table  align=center width='100%%' >\n"
"<tr><td align=left ><li><font color=red size=72 >URL Totoal:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >URL Current :%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >URL Handled:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Doc Total:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Doc Current:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Doc Handled:%d </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Doc Size:%lld </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >Doc Zsize:%lld </font></li></td></tr>\n"
"<tr><td align=left ><li><font color=red size=72 >DNS count:%d </font></li></td></tr>\n"
"</table><br><hr  noshade><em>\n"
"<font color=white ><a href='http://code.google.com/p/hispider' >"
"Hispider</a> Powered By <a href='http://code.google.com/p/hispider'>"
"http://code.google.com/p/hispider</a></font>"
"</BODY></HTML>\n";

static SBASE *sbase = NULL;
SERVICE *transport = NULL;
SERVICE *serv = NULL;
static dictionary *dict = NULL;
static LOGGER *daemon_logger = NULL;
static long long global_timeout_times = 60000000ll;
static LINKTABLE *linktable = NULL;

//daemon task handler 
void cb_serv_task_handler(void *arg);
//functions
//error handler for cstate
void cb_transport_error_handler(CONN *conn)
{
    HTTP_REQUEST *request = NULL;
    HTTP_RESPONSE *http_response = NULL;
    int c_id = 0;
    if(conn)
    {
        DEBUG_LOGGER(daemon_logger, "Connection[%s:%d] notified error via %d\n",
                conn->ip, conn->port, conn->fd);
        if((c_id = conn->c_id) >= 0 && c_id < linktable->nrequest 
                && linktable->requests && linktable->requests[c_id].handler == conn)
        {
            if(conn->packet->size > 0 && conn->cache->size > 0 && conn->chunk->buf->size > 0)
            {
                http_response = (HTTP_RESPONSE *)conn->cache->data; 
                c_id = conn->c_id;
                if(linktable->requests && linktable->requests[c_id].handler == conn)
                {
                    if(http_response->respid == RESP_OK)
                    {
                        request = &(linktable->requests[c_id]);
                        DEBUG_LOGGER(daemon_logger, "Completed request[%s:%d][%s]",
                                request->ip, request->port, request->path);
                        linktable->add_content(linktable, http_response, request->host,
                                request->path, conn->chunk->buf->data, conn->chunk->buf->size);
                        linktable->update_request(linktable, c_id, LINK_STATUS_OVER);
                        return ;
                    }
                }
            }
            linktable->update_request(linktable, c_id, LINK_STATUS_ERROR);
        }
    }
    return ;
}

int cb_transport_packet_reader(CONN *conn, BUFFER *buffer)
{
    return 0;
}

void cb_transport_packet_handler(CONN *conn, BUFFER *packet)
{        
    char *p = NULL, *end = NULL;
    HTTP_RESPONSE http_response;

    if(conn)
    {
        p = (char *)packet->data;
        end = (char *)packet->end;
        memset(&http_response, 0, sizeof(HTTP_RESPONSE));
        http_response.respid = -1;
        http_response_parse(p, end, &http_response);
        if((p = http_response.headers[HEAD_ENT_CONTENT_TYPE])
                && strncasecmp(p, "text", 4) != 0)
        {
            http_response.respid = RESP_NOCONTENT;
        }
        if(http_response.respid == RESP_OK)
        {
            conn->cache->push(conn->cache, &http_response, sizeof(HTTP_RESPONSE));
            if((p = http_response.headers[HEAD_ENT_CONTENT_LENGTH]))
            {
                conn->recv_chunk(conn, atol(p));
            }
            else
            {
                conn->recv_chunk(conn, DEFAULT_CONTENT_LENGTH);
            }
        }
        else
        {
            linktable->update_request(linktable, conn->c_id, LINK_STATUS_DISCARD);
            conn->over_cstate(conn);
            conn->close(conn);
        }
    }
}

void cb_transport_data_handler(CONN *conn, BUFFER *packet, 
        CHUNK *chunk, BUFFER *cache)
{
    HTTP_REQUEST  *request = NULL;
    HTTP_RESPONSE *http_response = NULL;
    char *p = NULL, *end = NULL;
    int c_id = 0;

    if(conn)
    {
        http_response = (HTTP_RESPONSE *)cache->data; 
        c_id = conn->c_id;
        if(linktable->requests && linktable->requests[c_id].handler == conn)
        {
            if(http_response->respid == RESP_OK)
            {
                request = &(linktable->requests[c_id]);
                DEBUG_LOGGER(daemon_logger, "Completed request[%s:%d][%s]",
                        request->ip, request->port, request->path);
                //fprintf(stdout, "%d::qleft:%d\n", __LINE__, linktable->qleft);
                linktable->add_content(linktable, http_response, request->host,
                        request->path, chunk->buf->data, chunk->buf->size);
                //linktable->parse(linktable, request->host,
                //        request->path, (char *)chunk->buf->data,(char*)chunk->buf->end);
                linktable->update_request(linktable, c_id, LINK_STATUS_OVER);
                goto end;
            }
            else 
                linktable->update_request(linktable, c_id, LINK_STATUS_ERROR);
        }
end:
        conn->over_cstate(conn);
        conn->close(conn);
        return ;
    }
}

void cb_transport_oob_handler(CONN *conn, BUFFER *oob)
{
}

//transaction handler 
void cb_transport_transaction_handler(CONN *conn, int tid)
{
    char buf[HTTP_BUF_SIZE];
    HTTP_REQUEST *request = NULL;
    int n = 0;
    if(conn)
    {

        if(tid >= 0 && tid < linktable->nrequest
                && linktable->requests && linktable->requests[tid].handler == conn)
        {
            request = &(linktable->requests[tid]);
            n = sprintf(buf, "GET %s HTTP/1.0\r\nHOST: %s\r\nUser-Agent: Mozilla\r\n\r\n",
                    request->path, request->host);
            conn->push_chunk(conn, buf, n);
            DEBUG_LOGGER(daemon_logger, "Request to %s:%d http://%s%s via %d", 
                    request->ip, request->port, request->host, request->path, conn->fd);
        }
        //fprintf(stdout, "%d::qleft:%d\n", __LINE__, linktable->qleft);
    }
}

void cb_serv_heartbeat_handler(void *arg)
{
    HTTP_REQUEST *request = NULL;
    CONN *c_conn = NULL;
    int  n = 0, tid = 0;
    void *timer = NULL;
    long taskid = 0;

    if(serv && transport && linktable)
    {
        //request
        while((tid = linktable->get_request(linktable, &request)) != -1)
        {
            DEBUG_LOGGER(daemon_logger, "Got request http://%s%s", request->host, request->path);
            if((c_conn = transport->newconn(transport, request->ip, request->port)))
            {
                DEBUG_LOGGER(daemon_logger, "Got conenction to %s:%d via %d on %08x newtranc:%08x", 
                        request->ip, request->port, c_conn->fd, c_conn, transport->newtransaction);
                request->handler = c_conn;
                c_conn->c_id = tid; 
                c_conn->start_cstate(c_conn);  
                c_conn->set_timeout(c_conn, global_timeout_times);
                transport->newtransaction(transport, c_conn, tid);
            }
            else
            {
                DEBUG_LOGGER(daemon_logger, "Getting conenction %s:%d failed", 
                        request->host, request->port);
                request->status = LINK_STATUS_WAIT;
                break;
            }
        }
        //task
        while((taskid = linktable->get_urltask(linktable)) != -1)
        {
            serv->newtask(serv, (void *)&cb_serv_task_handler, (void *)taskid);
            DEBUG_LOGGER(daemon_logger, "linktable->docno:%d doc_total:%d", 
                    linktable->docno, linktable->doc_total);
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
    char header[HTTP_BUF_SIZE];
    char buf[HTTP_BUF_SIZE];
    char *p = buf;
    int n = 0, m = 0;

    if(conn)
    {
        m = sprintf(p, __html__body__, linktable->url_total, 
                linktable->urlno, linktable->urlok_total, 
                linktable->doc_total, linktable->docno,
                linktable->docok_total, linktable->size, 
                linktable->zsize, linktable->dnscount);
        n = sprintf(header, "HTTP/1.0 200 OK \r\nContent-Type: text/html\r\n"
                "Content-Length: %d\r\n\r\n", m);
        conn->push_chunk(conn, header, n);
        conn->push_chunk(conn, buf, m);
        conn->over(conn);
    }
    return ;
}

void cb_serv_data_handler(CONN *conn, BUFFER *packet, 
        CHUNK *chunk, BUFFER *cache)
{
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
	char *logfile = NULL, *s = NULL, *p = NULL, *hostname = NULL, *path = NULL, 
         *docfile = NULL, *md5file = NULL, *urlfile = NULL, *metafile = NULL;
	int n = 0, nrequest = 0, ntask = 0;
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
	/* transport name and ip and port */
	transport->name = iniparser_getstr(dict, "TRANSPORT:service_name");
	transport->ip = iniparser_getstr(dict, "TRANSPORT:service_ip");
	if(transport->ip && transport->ip[0] == 0 ) transport->ip = NULL;
	transport->port = iniparser_getint(dict, "TRANSPORT:service_port", 80);
	transport->max_procthreads = iniparser_getint(dict, "TRANSPORT:max_procthreads", 1);
	transport->sleep_usec = iniparser_getint(dict, "TRANSPORT:sleep_usec", 100);
	transport->heartbeat_interval = iniparser_getint(dict, "TRANSPORT:heartbeat_interval", 10000000);
    /* connections number */
	transport->connections_limit = iniparser_getint(dict, "TRANSPORT:connections_limit", 32);
	logfile = iniparser_getstr(dict, "TRANSPORT:logfile");
	if(logfile == NULL)logfile = TRANSPORT_LOG;
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
	transport->cb_packet_reader = &cb_transport_packet_reader;
	transport->cb_packet_handler = &cb_transport_packet_handler;
	transport->cb_data_handler = &cb_transport_data_handler;
	transport->cb_transaction_handler = &cb_transport_transaction_handler;
	transport->cb_oob_handler = &cb_transport_oob_handler;
	transport->cb_error_handler = &cb_transport_error_handler;
	/* server */
	fprintf(stdout, "Parsing for transport...\n");

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
	md5file = iniparser_getstr(dict, "DAEMON:md5file");
    metafile = iniparser_getstr(dict, "DAEMON:metafile");
    urlfile = iniparser_getstr(dict, "DAEMON:urlfile");
    docfile = iniparser_getstr(dict, "DAEMON:docfile");
    logfile = iniparser_getstr(dict, "DAEMON:logfile");
    hostname = iniparser_getstr(dict, "DAEMON:hostname");
    path = iniparser_getstr(dict, "DAEMON:path");
    nrequest = iniparser_getint(dict, "DAEMON:nrequest", 128);
    ntask = iniparser_getint(dict, "DAEMON:ntask", 128);
    //linktable setting 
    if((linktable = linktable_init()) == NULL)
    {
        fprintf(stderr, "Initialize linktable failed, %s\n", strerror(errno));
        return -1;
    }
    linktable->iszlib = iniparser_getint(dict, "DAEMON:iszlib", 1);
    linktable->set_logger(linktable, NULL, daemon_logger);
    linktable->set_md5file(linktable, md5file);
    linktable->set_urlfile(linktable, urlfile);
    linktable->set_metafile(linktable, metafile);
    linktable->set_docfile(linktable, docfile);
    linktable->set_nrequest(linktable, nrequest);
    linktable->set_ntask(linktable, ntask);
    linktable->resume(linktable);
    linktable->addurl(linktable, hostname, path);
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
