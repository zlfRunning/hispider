#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/resource.h>
#include <sbase.h>
#include "http.h"
#include "ltable.h"
#include "iniparser.h"
static SBASE *sbase = NULL;
static SERVICE *service = NULL;
static dictionary *dict = NULL;

int hispiderd_packet_reader(CONN *conn, CB_DATA *buffer)
{
}

int hispiderd_packet_handler(CONN *conn, CB_DATA *packet)
{
    char *p = NULL, *end = NULL, buf[HTTP_BUF_SIZE];
    HTTP_REQ http_req = {0};
    long taskid = 0, n = 0;

    if(conn)
    {
        p = packet->data;
        end = packet->end;
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
            if(taskid == -1)
            {
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

int hispiderd_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
}

int hispiderd_oob_handler(CONN *conn, CB_DATA *oob)
{
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
    char *logfile = NULL, *s = NULL, *p = NULL;
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

    service->port = iniparser_getint(dict, "HISPIDERD:service_port", 80);
    service->working_mode = iniparser_getint(dict, "HISPIDERD:working_mode", WORKING_PROC);
    service->service_type = iniparser_getint(dict, "HISPIDERD:service_type", C_SERVICE);
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
    /* server */
    fprintf(stdout, "Parsing for server...\n");
    return sbase->add_service(sbase, service);
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
