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
SERVICE *serv = NULL;
static dictionary *dict = NULL;
static LOGGER *daemon_logger = NULL;
static LINKTABLE *linktable = NULL;
static long long global_timeout_times = 60000000;

//daemon task handler 
void cb_serv_task_handler(void *arg);

void cb_serv_heartbeat_handler(void *arg)
{
    int  n = 0, tid = 0;
    long taskid = 0;

    if(serv && linktable)
    {
        DEBUG_LOGGER(daemon_logger, "start heartbeat docno:%d doctotal:%d", 
                linktable->docno, linktable->doc_total);
        //request
        //task
        if((taskid = linktable->get_urltask(linktable)) != -1)
        {
            serv->newtask(serv, (void *)&cb_serv_task_handler, (void *)taskid);
            DEBUG_LOGGER(daemon_logger, "linktable->docno:%d doc_total:%d", 
                    linktable->docno, linktable->doc_total);
        }
        DEBUG_LOGGER(daemon_logger, "end heartbeat docno:%d doctotal:%d", 
                linktable->docno, linktable->doc_total);
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
    HTTP_REQUEST request;
    HTTP_REQ http_req;
    char header[HTTP_BUF_SIZE];
    char buf[HTTP_BUF_SIZE];
    char *p = NULL, *ps = NULL, *end = NULL;
    int n = 0, m = 0;
    
    if(conn)
    {
        p = packet->data;
        end = packet->end;
        memset(&http_req, 0, sizeof(HTTP_REQ));
        http_req.reqid = -1;
        if(http_request_parse(p, end, &http_req) == -1) goto end ;
        //DEBUG_LOGGER(daemon_logger, "HTTP:%d %s", http_req.reqid, (char *)packet->data);
        if(http_req.reqid == HTTP_GET)
        {
            p = buf;
            m = sprintf(p, __html__body__, linktable->url_total, 
                    linktable->urlno, linktable->urlok_total, 
                    linktable->doc_total, linktable->docno,
                    linktable->docok_total, linktable->size, 
                    linktable->zsize, linktable->dnscount);
            n = sprintf(header, "HTTP/1.0 200 OK \r\nContent-Type: text/html\r\n"
                    "Content-Length: %d\r\n\r\n", m);
            conn->push_chunk(conn, header, n);
            conn->push_chunk(conn, buf, m);
            //conn->over(conn);
            return ;
        }
        if(http_req.reqid == HTTP_TASK)
        {
            if(linktable->get_request(linktable, &request) != -1) 
            {
                p = buf;
                m = sprintf(p, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\n\r\n", 
                        sizeof(HTTP_REQUEST));
                conn->push_chunk(conn, buf, m);
                conn->push_chunk(conn, &request, sizeof(HTTP_REQUEST));
            }
            else
            {
                p = "HTTP/1.0 500 Internal Server Error\r\n\r\n";
                conn->push_chunk(conn, p, strlen(p));
            }
            return ;
        }
        if(http_req.reqid == HTTP_PUT)
        {
            if(http_req.headers[HEAD_ENT_CONTENT_LENGTH])
            {
                DEBUG_LOGGER(daemon_logger, "ready for PUT %s bytes", 
                        http_req.headers[HEAD_ENT_CONTENT_LENGTH]);
                n = atoi(http_req.headers[HEAD_ENT_CONTENT_LENGTH]);
                conn->cache->reset(conn->cache);
                conn->cache->push(conn->cache, &http_req, sizeof(HTTP_REQ));
                conn->recv_chunk(conn, n);
            }
            else
            {
                DEBUG_LOGGER(daemon_logger, "ready for PUT %s bytes", 
                        http_req.headers[HEAD_ENT_CONTENT_LENGTH]);
                p = "HTTP/1.0 400 Bad Request\r\n\r\n";
                conn->push_chunk(conn, p, strlen(p));
            }
            return ;
        }
end:
        DEBUG_LOGGER(daemon_logger, "%s", packet->data);
        conn->over(conn);
    }
    return ;
}

void cb_serv_data_handler(CONN *conn, BUFFER *packet, 
        CHUNK *chunk, BUFFER *cache)
{
    URLMETA *urlmeta = NULL;
    char *zdata = NULL;
    int id = 0, status = 0;

    if(conn && chunk->buf && chunk->buf->data)
    {
        urlmeta = (URLMETA *)chunk->buf->data;                
        zdata = (char *)chunk->buf->data + sizeof(URLMETA);
        status = urlmeta->status;
        id = urlmeta->id;
        linktable->update_request(linktable, id, status);
        if(urlmeta->zsize > 0)
            linktable->add_zcontent(linktable, urlmeta, zdata, urlmeta->zsize);
    }
}

//daemon transaction handler 
void cb_serv_transaction_handler(CONN *conn, int tid)
{
}

void cb_serv_oob_handler(CONN *conn, BUFFER *oob)
{
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
	char *logfile = NULL, *s = NULL, *p = NULL, *hostname = NULL, *path = NULL, 
         *docfile = NULL, *md5file = NULL, *urlfile = NULL, *metafile = NULL;
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
	serv->heartbeat_interval = iniparser_getint(dict, "DAEMON:heartbeat_interval", 10000000);
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
        global_timeout_times = str2ll(p);
    //linktable files
	md5file = iniparser_getstr(dict, "DAEMON:md5file");
    metafile = iniparser_getstr(dict, "DAEMON:metafile");
    urlfile = iniparser_getstr(dict, "DAEMON:urlfile");
    docfile = iniparser_getstr(dict, "DAEMON:docfile");
    logfile = iniparser_getstr(dict, "DAEMON:logfile");
    hostname = iniparser_getstr(dict, "DAEMON:hostname");
    path = iniparser_getstr(dict, "DAEMON:path");
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
    linktable->set_ntask(linktable, ntask);
    linktable->resume(linktable);
    linktable->addurl(linktable, hostname, path);
    return 0;
}

void bt_handler()
{
    char* callstack[128];
    int i, frames = backtrace(callstack, 128);
    char** strs = backtrace_symbols(callstack, frames);
    for (i = 0; i < frames; ++i) {
        fprintf(stderr, "%s\n", strs[i]);
    }
    free(strs);
}

static void cb_stop(int sig){
        switch (sig) {
                case SIGINT:
                case SIGTERM:
                        fprintf(stderr, "lhttpd server is interrupted by user.\n");
                        if(sbase)sbase->stop(sbase);
                        break;
                case SIGSEGV:
                    //bt_handler();
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
	signal(SIGSEGV,  &cb_stop);
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
