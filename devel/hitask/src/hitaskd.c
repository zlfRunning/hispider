#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <locale.h>
#include <sbase.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "http.h"
#include "ltask.h"
#include "iniparser.h"
#include "evdns.h"
#include "queue.h"
#include "logger.h"
#include "hibase.h"
#include "trie.h"
#include "tm.h"
#define PROXY_TIMEOUT 1000000
static char *httpd_home = NULL;
static SBASE *sbase = NULL;
static SERVICE *hitaskd = NULL, *histore = NULL, *adns = NULL, *hiproxy = NULL;
static dictionary *dict = NULL;
static LTASK *ltask = NULL;
static HIBASE *hibase = NULL;
static void *hitaskd_logger = NULL, *histore_logger = NULL, 
            *adns_logger = NULL, *hiproxy_logger = NULL;
static int is_need_authorization = 0;
static char *authorization_name = "Hitask Administration System";
static void *argvmap = NULL;
static SESSION hiproxy_session = {0};
static int proxy_timeout = 20000000;
static char *e_argvs[] = 
{
    "op", 
#define E_ARGV_OP       0
    "host",
#define E_ARGV_HOST     1
    "url",
#define E_ARGV_URL      2
    "pattern",
#define E_ARGV_PATTERN  3
    "hostid",
#define E_ARGV_HOSTID   4
    "urlid",
#define E_ARGV_URLID    5
    "name",
#define E_ARGV_NAME     6
    "parentid",
#define E_ARGV_PARENTID 7
    "nodeid",
#define E_ARGV_NODEID   8
    "tableid",
#define E_ARGV_TABLEID  9
    "fieldid",
#define E_ARGV_FIELDID  10
    "type",
#define E_ARGV_TYPE     11
    "flag",
#define E_ARGV_FLAG     12
    "templateid",
#define E_ARGV_TEMPLATEID 13
    "map"
#define E_ARGV_MAP      14
};
#define E_ARGV_NUM      15
static char *e_ops[]=
{
    "host_up",
#define E_OP_HOST_UP        0
    "host_down",
#define E_OP_HOST_DOWN      1
    "node_add",
#define E_OP_NODE_ADD       2
    "node_update",
#define E_OP_NODE_UPDATE    3
    "node_delete",
#define E_OP_NODE_DELETE    4
    "node_childs",
#define E_OP_NODE_CHILDS    5
    "task_stop",
#define E_OP_TASK_STOP      6
    "task_running",
#define E_OP_TASK_RUNNING   7
    "task_view",
#define E_OP_TASK_VIEW      8
    "table_add",
#define E_OP_TABLE_ADD      9
    "table_view",
#define E_OP_TABLE_VIEW     10
    "table_list",
#define E_OP_TABLE_LIST     11
    "table_rename",
#define E_OP_TABLE_RENAME   12
    "table_delete",
#define E_OP_TABLE_DELETE   13
    "field_add",
#define E_OP_FIELD_ADD      14
    "field_update",
#define E_OP_FIELD_UPDATE   15
    "field_delete",
#define E_OP_FIELD_DELETE   16
    "template_add",
#define E_OP_TEMPLATE_ADD   17
    "template_update",
#define E_OP_TEMPLATE_UPDATE    18
    "template_delete"
#define E_OP_TEMPLATE_DELETE    19
};
#define E_OP_NUM 20
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

/* hiproxy packet reader */
int hiproxy_proxy_handler(CONN *conn, CB_DATA *buffer)
{
    CONN *parent = NULL;
    if(conn && buffer && (parent = hiproxy->findconn(hiproxy, conn->session.parentid))
            && parent == (CONN *)conn->session.parent)
    {
        parent->push_chunk(parent, buffer->data, buffer->ndata);
        return buffer->ndata;
    }
    return -1;
}

/* hiproxy packet reader */
int hiproxy_packet_reader(CONN *conn, CB_DATA *buffer)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

/* hiproxy http handler */
int hiproxy_http_handler(CONN *conn, char *host, int port) 
{
    struct hostent *hp = NULL;
    CONN *new_conn = NULL;
    SESSION session = {0};
    char *ip = NULL;

    if(conn && host && port > 0)
    {
        if((hp = gethostbyname(host))
                && (ip = inet_ntoa(*((struct in_addr *)(hp->h_addr)))))
        {
            memcpy(&session, &hiproxy_session, sizeof(SESSION));
            session.packet_type = PACKET_PROXY;
            session.parent = conn;
            session.parentid = conn->index;
            if((new_conn = hiproxy->newconn(hiproxy, -1, -1, ip, port, &session)))
            {
                return hiproxy->newtransaction(hiproxy, new_conn, conn->index);
            }
        }
    }
    return -1;
}

 
/* hiproxy packet handler */
int hiproxy_packet_handler(CONN *conn, CB_DATA *packet)
{
    char buf[HTTP_BUF_SIZE], *host = NULL, *path = NULL, 
         *s = NULL, *p = NULL, *end = NULL;
    int n = 0, i = 0, port = 80;
    HTTP_REQ http_req = {0};

    if(conn && packet->ndata < HTTP_BUF_SIZE)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        //*end = '\0';
        //memcpy(buf, p, packet->ndata);
        if(http_request_parse(p, end, &http_req) == -1) goto err_end;
        p = http_req.path;
        if(strncasecmp(p, "http://", 7) == 0)
        {
            p += 7;
            host = p;
            while(*p != '\0' && *p != ':' && *p != '/') ++p;
            if(*p == ':')
            {
                ++p; 
                port = atoi(p);
                while(*p >= '0' && *p <= '9') ++p;
                path = p;
            }
            else if(*p == '/')
            {
                path = ++p;
            }
            else if(*p == '\0') 
            {
                path = "";
            }
            else path = p;
        }
        else
        {
            if((n = http_req.headers[HEAD_REQ_HOST]) > 0 )
            {
                path = p;
                host = (http_req.hlines + n);
            }
            else goto err_end;
        }
        //authorized 
        if(http_req.reqid == HTTP_GET)
        {
            p = buf;
            p += sprintf(p, "GET /%s HTTP/1.0\r\n", path);
            p += sprintf(p, "Host: %s\r\n", host);
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
                if((n = http_req.headers[i]) > 0 && (s = (http_req.hlines + n)))
                {
                    p += sprintf(p, "%s %s\r\n", http_headers[i].e, s);
                }
            }
            p += sprintf(p, "%s", "\r\n");
            conn->save_cache(conn, buf, (p - buf));
            return hiproxy_http_handler(conn, host, port);
        }
        else if(http_req.reqid == HTTP_POST)
        {
            p = buf;
            p += sprintf(p, "POST /%s HTTP/1.0\r\n", path);
            p += sprintf(p, "Host: %s\r\n", host);
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
                if((n = http_req.headers[i]) > 0 && (s = http_req.hlines + n))
                {
                    p += sprintf(p, "%s %s\r\n", http_headers[i].e, s);
                }
            }
            p += sprintf(p, "%s", "\r\n");
            conn->save_cache(conn, buf, (p - buf));
            if((n = http_req.headers[HEAD_ENT_CONTENT_LENGTH]) > 0 
                    && (n = atol(http_req.hlines + n)) > 0)
            {
                conn->recv_chunk(conn, n);
            }
            return hiproxy_http_handler(conn, host, port);
        }
        else goto err_end;
        return 0;
    }
err_end:
    conn->push_chunk(conn, HTTP_BAD_REQUEST, strlen(HTTP_BAD_REQUEST));
    return -1;
}

/* hiproxy data handler */
int hiproxy_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    CONN *child = NULL;
    if(conn)
    {
        if((child = hiproxy->findconn(hiproxy,  conn->session.childid))
                && child == conn->session.child)
        {
            if(child->c_state == C_STATE_FREE)
            {
                return child->push_chunk(child, chunk->data, chunk->ndata);
            }
            else
            {
                return conn->set_timeout(conn, PROXY_TIMEOUT);
            }
        }
    }
    return -1;
}

/* hiproxy timeout handler */
int hiproxy_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    CONN *parent = NULL, *child = NULL;

    if(conn)
    {
        if((parent = hiproxy->findconn(hiproxy,  conn->session.parentid))
                && parent == conn->session.parent)
        {
            parent->over(parent);
            conn->over(conn);
        }
        else if((child = hiproxy->findconn(hiproxy,  conn->session.childid))
                && child == conn->session.child)
        {
            hiproxy->newtransaction(hiproxy, conn, conn->index);
        }
        return 0;
    }
    return -1;
}

/* hiproxy transaction handler */
int hiproxy_trans_handler(CONN *conn, int tid)
{
    CB_DATA *cache = NULL, *chunk = NULL;
    CONN *parent = NULL, *child = NULL;

    if(conn && tid >= 0)
    {
        if((parent = hiproxy->findconn(hiproxy,  conn->session.parentid))
                && parent == conn->session.parent)
        {
            if((cache = PCB(parent->cache)) && cache->ndata > 0 )
            {
                conn->start_cstate(conn);
                conn->set_timeout(conn, proxy_timeout);
                return conn->push_chunk(conn, cache->data, cache->ndata);
            }
        }
        else if((child = hiproxy->findconn(hiproxy,  conn->session.childid))
                && child == conn->session.child && (chunk = PCB(parent->chunk)) 
                && chunk->ndata > 0)
        {
            if(child->c_state == C_STATE_FREE)
            {
                return child->push_chunk(child, chunk->data, chunk->ndata);
            }
            else
            {
                return conn->set_timeout(conn, PROXY_TIMEOUT);
            }
        }
    }
    return -1;
}
 
/* hiproxy OOB handler */
int hiproxy_oob_handler(CONN *conn, CB_DATA *oob)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

/* hiproxy error handler */
int hiproxy_error_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    CONN *parent = NULL;

    if(conn)
    {
        if((parent = hiproxy->findconn(hiproxy,  conn->session.parentid))
                && parent == conn->session.parent)
            parent->over(parent);
        conn->over(conn);
        return 0;
    }
    return -1;
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
    char buf[HTTP_BUF_SIZE], file[HTTP_PATH_MAX], *host = NULL, 
        *ip = NULL, *p = NULL, *end = NULL;
    int urlid = 0, n = 0, ips = 0, err = 0;
    struct stat st = {0};
    HTTP_REQ http_req = {0};

    if(conn)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        *end = '\0';
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
            if(httpd_home)
            {
                p = file;
                if(http_req.path[0] != '/')
                    p += sprintf(p, "%s/%s", httpd_home, http_req.path);
                else
                    p += sprintf(p, "%s%s", httpd_home, http_req.path);
                if(http_req.path[1] == '\0')
                    p += sprintf(p, "%s", "index.html");
                DEBUG_LOGGER(hitaskd_logger, "HTTP_GET[%s]", file);
                if((n = (p - file)) > 0 && lstat(file, &st) == 0)
                {
                    if(st.st_size == 0)
                    {
                        return conn->push_chunk(conn, HTTP_NO_CONTENT, strlen(HTTP_NO_CONTENT));
                    }
                    else if((n = http_req.headers[HEAD_REQ_IF_MODIFIED_SINCE]) > 0
                        && str2time(http_req.hlines + n) == st.st_mtime)
                    {
                        return conn->push_chunk(conn, HTTP_NOT_MODIFIED, strlen(HTTP_NOT_MODIFIED));
                    }
                    else
                    {
                        p = buf;
                        p += sprintf(p, "HTTP/1.0 200 OK\r\nContent-Length:%lld\r\nLast-Modified:", 
                                (long long int)(st.st_size)); 
                        p += GMTstrdate(st.st_mtime, p);
                        p += sprintf(p, "%s", "\r\n");//date end
                        p += sprintf(p, "%s", "\r\n");
                        conn->push_chunk(conn, buf, (p - buf));
                        return conn->push_file(conn, file, 0, st.st_size);
                    }
                }
                else
                {
                    return conn->push_chunk(conn, HTTP_NOT_FOUND, strlen(HTTP_NOT_FOUND));
                }
            }
            else
            {
                goto err_end;
            }
        }
        else if(http_req.reqid == HTTP_POST)
        {
            if((n = http_req.headers[HEAD_ENT_CONTENT_LENGTH]) > 0 
                    && (n = atol(http_req.hlines + n)) > 0)
            {
                DEBUG_LOGGER(hitaskd_logger, "HTTP_POST[%s]", http_req.path);
                conn->save_cache(conn, &http_req, sizeof(HTTP_REQ));
                return conn->recv_chunk(conn, n);
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
            //error 
            if(urlid >= 0 && (n = http_req.headers[HEAD_GEN_WARNING]) > 0)
            {
                err = atoi(http_req.hlines + n);
                ltask->set_url_status(ltask, urlid, NULL, URL_STATUS_ERR, err);
            }
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
    int i = 0, id = 0, n = 0, op = -1, nodeid = -1, 
        parentid = -1, urlid = -1, hostid = -1, tableid = -1, fieldid = -1,
        type = -1, flag = -1, templateid = -1;
    char *p = NULL, *end = NULL, *name = NULL, *host = NULL, *url = NULL, 
         *pattern = NULL, *map = NULL, buf[HTTP_BUF_SIZE], block[HTTP_BUF_SIZE];
    HTTP_REQ httpRQ = {0}, *http_req = NULL;
    void *dp = NULL;

    if(conn && packet && cache && chunk && chunk->ndata > 0)
    {
        if((http_req = (HTTP_REQ *)cache->data))
        {
            if(http_req->reqid == HTTP_POST)
            {
                p = chunk->data;
                end = chunk->data + chunk->ndata;
                DEBUG_LOGGER(hitaskd_logger, "Ready parsing(%s)", p);
                if(http_argv_parse(p, end, &httpRQ) == -1)goto err_end;
                DEBUG_LOGGER(hitaskd_logger, "Over parsing(%s) nargvs:%d", p, httpRQ.nargvs);
                for(i = 0; i < httpRQ.nargvs; i++)
                {
                    if(httpRQ.argvs[i].nk > 0 && (n = httpRQ.argvs[i].k) > 0 
                            && (p = (httpRQ.line + n)))
                    {
                        DEBUG_LOGGER(hitaskd_logger, "argv[%d]:%.*s->%.*s", 
                                i, httpRQ.argvs[i].nk, httpRQ.line+httpRQ.argvs[i].k, 
                                httpRQ.argvs[i].nv, httpRQ.line+httpRQ.argvs[i].v);
                        TRIETAB_GET(argvmap, p, httpRQ.argvs[i].nk, dp);
                        if((id = ((long)dp - 1)) >= 0 && httpRQ.argvs[i].nv > 0
                                && (n = httpRQ.argvs[i].v) > 0 
                                && (p = (httpRQ.line + n)))
                        {
                            switch(id)
                            {
                                case E_ARGV_OP :
                                    TRIETAB_GET(argvmap, p, httpRQ.argvs[i].nv, dp);
                                    op = (long)dp - 1;
                                    break;
                                case E_ARGV_NAME :
                                    name = p;
                                    break;
                                case E_ARGV_NODEID :
                                    nodeid = atoi(p);
                                    break;
                                case E_ARGV_PARENTID:
                                    parentid = atoi(p);
                                    break;
                                case E_ARGV_PATTERN:
                                    pattern = p;
                                    break;
                                case E_ARGV_HOST :
                                    host = p;
                                    break;
                                case E_ARGV_HOSTID :
                                    hostid = atoi(p);
                                    break;
                                case  E_ARGV_URL :
                                    url = p;
                                    break;
                                case  E_ARGV_URLID :
                                    urlid = atoi(p);
                                    break;
                                case E_ARGV_TABLEID:
                                    tableid = atoi(p);
                                    break;
                                case E_ARGV_FIELDID:
                                    fieldid = atoi(p);
                                    break;
                                case E_ARGV_TYPE:
                                    type = atoi(p);
                                    break;
                                case E_ARGV_FLAG:
                                    flag = atoi(p);
                                    break;
                                case E_ARGV_TEMPLATEID:
                                    templateid = atoi(p);
                                    break;
                                case E_ARGV_MAP:
                                    map = p;
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
                }
                switch(op)
                {
                    case E_OP_NODE_ADD :
                        if(parentid >= 0 && name)
                        {
                            if((id = hibase->add_pnode(hibase, parentid, name)) > 0
                                    && (n = hibase->view_pnode_childs(hibase, parentid, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }
                        else goto err_end;
                        break;
                    case E_OP_NODE_UPDATE :
                        if(nodeid > 0 && name)
                        {
                            DEBUG_LOGGER(hitaskd_logger, "op:%d id:%d name:%s", op, nodeid, name);
                            id = hibase->update_pnode(hibase, nodeid, name);
                            n = sprintf(buf, "%d\r\n", id);
                            n = sprintf(buf, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                                    "Content-Length:%d\r\nConnection:close\r\n\r\n%d\r\n", n, id);
                            conn->push_chunk(conn, buf, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_NODE_DELETE :
                        if(nodeid > 0 || name)
                        {
                            DEBUG_LOGGER(hitaskd_logger, "op:%d id:%d", op, nodeid);
                            id = hibase->delete_pnode(hibase, nodeid);
                            n = sprintf(buf, "%d\r\n", id);
                            n = sprintf(buf, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                                    "Content-Length:%d\r\nConnection:close\r\n\r\n%d\r\n", n, id);
                            conn->push_chunk(conn, buf, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_NODE_CHILDS :
                        if(nodeid >= 0)
                        {
                            //DEBUG_LOGGER(hitaskd_logger, "op:%d id:%d name:%s", op, nodeid, name);
                            if((n = hibase->view_pnode_childs(hibase, nodeid, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }else goto err_end;
                        break;
                    case E_OP_TASK_VIEW:
                        if((n = ltask->get_stateinfo(ltask, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_TASK_STOP:
                        if(ltask->set_state_running(ltask, 0) == 0
                            && (n = ltask->get_stateinfo(ltask, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_TASK_RUNNING:
                        if(ltask->set_state_running(ltask, 1) == 0
                            && (n = ltask->get_stateinfo(ltask, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_TABLE_ADD:
                        if(name && (id = hibase->add_table(hibase, name)) >= 0)
                        {
                            if((n = hibase->list_table(hibase, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }else goto err_end;
                        break;
                    case E_OP_TABLE_RENAME:
                        if(tableid >= 0 && name 
                                && (id = hibase->rename_table(hibase, tableid, name)) >= 0)
                        {
                            if((n = hibase->list_table(hibase, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }else goto err_end;
                        break;
                    case E_OP_TABLE_DELETE:
                        if(tableid >= 0 && (id = hibase->delete_table(hibase, tableid)) >= 0)
                        {
                            if((n = hibase->list_table(hibase, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }else goto err_end;
                        break;
                    case E_OP_TABLE_VIEW:
                        if(tableid >= 0)
                        {
                            if((n = hibase->view_table(hibase, tableid, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }else goto err_end;
                        break;
                    case E_OP_TABLE_LIST:
                        if((n = hibase->list_table(hibase, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_FIELD_ADD:
                        if(tableid >= 0 && name && type > 0 && (id = hibase->add_field(hibase, 
                                        tableid, name, type, flag)) >= 0)
                        {
                            if((n = hibase->view_table(hibase, tableid, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }else goto err_end;
                        break;
                    case E_OP_FIELD_UPDATE:
                        if(tableid >= 0 && fieldid >= 0 && (name || type >= 0 || flag >= 0)
                            && (id = hibase->update_field(hibase, tableid,
                                    fieldid, name, type, flag)) >= 0)
                        {
                            if((n = hibase->view_table(hibase, tableid, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }else goto err_end;
                        break;
                    case E_OP_FIELD_DELETE:
                        if(tableid >= 0 && fieldid >= 0
                            && (id = hibase->delete_field(hibase, tableid, fieldid)) > 0)
                        {
                            if((n = hibase->view_table(hibase, tableid, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }else goto err_end;
                        break;
                    case E_OP_TEMPLATE_ADD:
                        break;
                    default:
                        goto err_end;
                        break;
                }
            }
        }
end:
        return 0;
err_end:
        conn->push_chunk(conn, HTTP_BAD_REQUEST, strlen(HTTP_BAD_REQUEST));
    }
    return -1;
}

/* hitaskd timeout handler */
int hitaskd_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    char buf[HTTP_BUF_SIZE];
    int n = 0;

    if(conn)
    {   if(conn->evstate == EVSTATE_WAIT)
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
        }
        else
        {
            conn->over(conn);
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
        *end = '\0';
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
    int interval = 0, i = 0, n = 0;
    void *dp = NULL;

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
    hitaskd->ndaemons = iniparser_getint(dict, "HITASKD:ndaemons", 1);
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
    //argvmap
    TRIETAB_INIT(argvmap);
    if(argvmap == NULL) _exit(-1);
    else
    {
        for(i = 0; i < E_ARGV_NUM; i++)
        {
            dp = (void *)((long)(i+1));
            n = strlen(e_argvs[i]);
            TRIETAB_ADD(argvmap, e_argvs[i], n, dp);
        }
        for(i = 0; i < E_OP_NUM; i++)
        {
            dp = (void *)((long)(i+1));
            n = strlen(e_ops[i]);
            TRIETAB_ADD(argvmap, e_ops[i], n, dp);
        }
    }
    /* httpd_home */
    httpd_home = iniparser_getstr(dict, "HITASKD:httpd_home");
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
    else 
    {
        _exit(-1);
    }
    if((hibase = hibase_init()))
    {
        if((p = iniparser_getstr(dict, "HITASKD:hibasedir")) == NULL)
            p = "/tmp/hibase";
        hibase->set_basedir(hibase, p); 
    }
    else _exit(-1);
    /* hiproxy */
    if((hiproxy = service_init()) == NULL)
    {
        fprintf(stderr, "Initialize service failed, %s", strerror(errno));
        _exit(-1);
    }
    hiproxy->family = iniparser_getint(dict, "HIPROXY:inet_family", AF_INET);
    hiproxy->sock_type = iniparser_getint(dict, "HIPROXY:socket_type", SOCK_STREAM);
    hiproxy->ip = iniparser_getstr(dict, "HIPROXY:service_ip");
    hiproxy->port = iniparser_getint(dict, "HIPROXY:service_port", 2888);
    hiproxy->working_mode = iniparser_getint(dict, "HIPROXY:working_mode", WORKING_PROC);
    hiproxy->service_type = iniparser_getint(dict, "HIPROXY:service_type", S_SERVICE);
    hiproxy->service_name = iniparser_getstr(dict, "HIPROXY:service_name");
    hiproxy->nprocthreads = iniparser_getint(dict, "HIPROXY:nprocthreads", 1);
    hiproxy->ndaemons = iniparser_getint(dict, "HIPROXY:ndaemons", 1);
    hiproxy->set_log(hiproxy, iniparser_getstr(dict, "HIPROXY:logfile"));
    hiproxy->session.packet_type = iniparser_getint(dict, "HIPROXY:packet_type",PACKET_DELIMITER);
    hiproxy->session.packet_delimiter = iniparser_getstr(dict, "HIPROXY:packet_delimiter");
    p = s = hiproxy->session.packet_delimiter;
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
    hiproxy->session.packet_delimiter_length = strlen(hiproxy->session.packet_delimiter);
    hiproxy->session.buffer_size = iniparser_getint(dict, "HIPROXY:buffer_size", SB_BUF_SIZE);
    hiproxy->session.packet_reader = &hiproxy_packet_reader;
    hiproxy->session.packet_handler = &hiproxy_packet_handler;
    hiproxy->session.data_handler = &hiproxy_data_handler;
    hiproxy->session.timeout_handler = &hiproxy_timeout_handler;
    hiproxy->session.oob_handler = &hiproxy_oob_handler;
    hiproxy->session.transaction_handler = &hiproxy_trans_handler;
    hiproxy->session.proxy_handler = &hiproxy_proxy_handler;
    if((p = iniparser_getstr(dict, "HIPROXY:access_log"))){LOGGER_INIT(hiproxy_logger, p);}
    proxy_timeout = iniparser_getint(dict, "HIPROXY:timeout", 20000000);
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
    return (sbase->add_service(sbase, hitaskd) | sbase->add_service(sbase, hiproxy) 
             | sbase->add_service(sbase, histore) | sbase->add_service(sbase, adns));
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
    char *conf = NULL, ch = 0;
    int is_daemon = 0;

    /* get configure file */
    while((ch = getopt(argc, argv, "c:d")) != -1)
    {
        if(ch == 'c') conf = optarg;
        else if(ch == 'd') is_daemon = 1;
    }
    if(conf == NULL)
    {
        fprintf(stderr, "Usage:%s -c config_file\n", argv[0]);
        _exit(-1);
    }
    /* locale */
    setlocale(LC_ALL, "C");
    /* signal */
    signal(SIGTERM, &hitaskd_stop);
    signal(SIGINT,  &hitaskd_stop);
    signal(SIGHUP,  &hitaskd_stop);
    signal(SIGPIPE, SIG_IGN);
    //daemon
    if(is_daemon)
    {
        pid = fork();
        switch (pid) {
            case -1:
                perror("fork()");
                exit(EXIT_FAILURE);
                break;
            case 0: // child process 
                if(setsid() == -1)
                    exit(EXIT_FAILURE);
                break;
            default:// parent 
                _exit(EXIT_SUCCESS);
                break;
        }
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
    if(hiproxy_logger){LOGGER_CLEAN(hiproxy_logger);}
    if(argvmap){TRIETAB_CLEAN(argvmap);}
    return 0;
}
