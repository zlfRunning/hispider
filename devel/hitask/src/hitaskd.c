#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <locale.h>
#include <pcre.h>
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
#include "url.h"
#include "zstream.h"
#define PROXY_TIMEOUT 1000000
static char *http_default_charset = "UTF-8";
static char *httpd_home = NULL;
static SBASE *sbase = NULL;
static SERVICE *hitaskd = NULL, *histore = NULL, *adns = NULL;
static dictionary *dict = NULL;
static LTASK *ltask = NULL;
static HIBASE *hibase = NULL;
static void *hitaskd_logger = NULL, *histore_logger = NULL, *adns_logger = NULL;
static int is_need_authorization = 0;
static int is_need_extract_link = 0;
static char *authorization_name = "Hitask Administration System";
static void *argvmap = NULL;
static int proxy_timeout = 2000000;
static int histore_ntask = 0;
static int histore_task_running = 0;
static int http_page_num  = 100;
//static int working_mode = 1;
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
    "map",
#define E_ARGV_MAP      14
    "link",
#define E_ARGV_LINK     15
    "linkmap",
#define E_ARGV_LINKMAP  16
    "urlnodeid",
#define E_ARGV_URLNODEID 17
    "level",
#define E_ARGV_LEVEL     18
    "speed",
#define E_ARGV_SPEED     19
    "page"
#define E_ARGV_PAGE      20
};
#define E_ARGV_NUM       21
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
    "template_delete",
#define E_OP_TEMPLATE_DELETE    19
    "template_list",
#define E_OP_TEMPLATE_LIST      20
    "database_view",
#define E_OP_DATABASE_VIEW      21
    "urlnode_add",
#define E_OP_URLNODE_ADD        22
    "urlnode_update",
#define E_OP_URLNODE_UPDATE     23
    "urlnode_delete",
#define E_OP_URLNODE_DELETE     24
    "urlnode_childs",
#define E_OP_URLNODE_CHILDS     25
    "urlnode_list",
#define E_OP_URLNODE_LIST       26
    "dns_add",
#define E_OP_DNS_ADD            27
    "dns_delete",
#define E_OP_DNS_DELETE         28
    "dns_list",
#define E_OP_DNS_LIST           29
    "proxy_add",
#define E_OP_PROXY_ADD          30
    "proxy_delete",
#define E_OP_PROXY_DELETE       31
    "proxy_list",
#define E_OP_PROXY_LIST         32
    "speed_limit",
#define E_OP_SPEED_LIMIT        33
    "node_brother"
#define E_OP_NODE_BROTHER       34
};
#define E_OP_NUM 35

/* dns packet reader */
int adns_packet_reader(CONN *conn, CB_DATA *buffer)
{
    int tid = 0,  i = 0, n = 0, left = 0, ip  = 0;
    unsigned char *p = NULL, *s = NULL;
    EVHOSTENT hostent = {0};

    if(conn && (tid = conn->c_id) >= 0 && buffer->ndata > 0 && buffer->data)
    {
        s = (unsigned char *)buffer->data;
        left = buffer->ndata;
        do
        {
            if((n = evdns_parse_reply(s, left, &hostent)) > 0)
            {
                s += n;
                left -= n;
                DEBUG_LOGGER(adns_logger, "name:%s left:%d naddrs:%d", 
                        hostent.name, left, hostent.naddrs);
                if(hostent.naddrs > 0)
                {
                    ltask->set_host_ip(ltask, (char *)hostent.name, 
                            hostent.addrs, hostent.naddrs);
                    for(i = 0; i < hostent.nalias; i++)
                    {
                        ltask->set_host_ip(ltask, (char *)hostent.alias[i], 
                                hostent.addrs, hostent.naddrs);
                    }
                    ip = hostent.addrs[0];
                    p = (unsigned char *)&ip;
                    DEBUG_LOGGER(adns_logger, "Got host[%s]'s ip[%d.%d.%d.%d] from %s:%d", 
                            hostent.name, p[0], p[1], p[2], p[3], 
                            conn->remote_ip, conn->remote_port);
                }
            }else break;
            memset(&hostent, 0, sizeof(EVHOSTENT));
        }while(left > 0);
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
        DEBUG_LOGGER(adns_logger, "error_DNS()[%d] remote[%s:%d] local[%s:%d] via %d", tid, conn->remote_ip, conn->remote_port, conn->local_ip, conn->local_port, conn->fd);
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
        DEBUG_LOGGER(adns_logger, "error_handler()[%d] remote[%s:%d] local[%s:%d] via %d", tid, conn->remote_ip, conn->remote_port, conn->local_ip, conn->local_port, conn->fd);
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
        DEBUG_LOGGER(adns_logger, "timeout_handler()[%d] remote[%s:%d] local[%s:%d] via %d", tid, conn->remote_ip, conn->remote_port, conn->local_ip, conn->local_port, conn->fd);
        return adns->newtransaction(adns, conn, tid);
    }
    return -1;
}

/* adns transaction handler */
int adns_trans_handler(CONN *conn, int tid)
{
    unsigned char hostname[EVDNS_NAME_MAX], buf[HTTP_BUF_SIZE];
    int qid = 0, n = 0;

    if(conn && tid >= 0)
        //&& tid < DNS_TASK_MAX && tasklist[tid].conn == conn)
    {
        memset(hostname, 0, EVDNS_NAME_MAX);
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
        while((id = ltask->pop_dns(ltask, dns_ip)) >= 0 && 
                (conn = adns->newconn(adns, -1, 
                SOCK_DGRAM, dns_ip, DNS_DEFAULT_PORT, NULL)))

        {
            conn->c_id = id;
            conn->start_cstate(conn);
            adns->newtransaction(adns, conn, id);
        }
    }
    return ;
}

/* packet reader */
int http_proxy_packet_reader(CONN *conn, CB_DATA *buffer)
{
    char *p = NULL, *end = NULL;
    int n = -1;

    if(conn && buffer && buffer->ndata > 0 && (p = buffer->data)
        && (end = (buffer->data + buffer->ndata)))
    {
        //fprintf(stdout, "%d::%s\r\n", __LINE__, buffer->data);
        while(p < end)
        {
            if(p < (end - 3) && *p == '\r' && *(p+1) == '\n' && *(p+2) == '\r' && *(p+3) == '\n')
            {
                n = p + 4 - buffer->data;
                break;
            }
            else ++p;
        }
        //fprintf(stdout, "%d::%d-%d\r\n", __LINE__, n, buffer->ndata);
    }
    return n;
}

/* http proxy packet handler */
int http_proxy_handler(CONN *conn,  HTTP_REQ *http_req);


/* packet handler */
int http_proxy_packet_handler(CONN *conn, CB_DATA *packet)
{
    char *p = NULL, *end = NULL;
    HTTP_RESPONSE http_resp = {0};
    HTTP_REQ *http_req = NULL;
    CONN *parent = NULL;
    int n = 0, len = 0;

    if(conn && packet && packet->ndata > 0 && (p = packet->data) 
            && (end = packet->data + packet->ndata))
    {
        if(http_response_parse(p, end, &http_resp) == -1) goto err_end;
        conn->save_cache(conn, &http_resp, sizeof(HTTP_RESPONSE));
        if(http_resp.respid == RESP_MOVEDPERMANENTLY 
                &&  (n = http_resp.headers[HEAD_RESP_LOCATION]) > 0
                && (p = http_resp.hlines + n))
        {
            //fprintf(stdout, "Redirect to %s\n", p);
            if(conn->session.parent
                && (parent = hitaskd->findconn(hitaskd, conn->session.parentid))
                && conn->session.parent == parent
                && (http_req = (HTTP_REQ *)(PCB(parent->cache)->data))
                && (n = strlen(p)) > 0 && n < HTTP_URL_PATH_MAX)
            {
                fprintf(stdout, "Redirect %s to %s[%d]\n", http_req->path, p, n);
                memcpy(http_req->path, p, n);
                http_req->path[n] = '\0';
                return http_proxy_handler(parent, http_req); 
            }
            goto err_end;
        }
        if((n = http_resp.headers[HEAD_ENT_CONTENT_TYPE])
            && (p = http_resp.hlines + n) && strncasecmp(p, "text", 4) == 0)
        {
            if((n = http_resp.headers[HEAD_ENT_CONTENT_LENGTH]) > 0 
                && (p = http_resp.hlines + n) && (n = atoi(p)))
            {
                len = n;
            }
            else
            {
                len = 1024 * 1024 * 16;
            }
            return conn->recv_chunk(conn, len);
        }
        else
        {
            if(conn->session.parent
                    && (parent = hitaskd->findconn(hitaskd, conn->session.parentid))
                    && conn->session.parent == parent)
            {
                //fprintf(stdout, "%d::OK:%d\n", __LINE__, packet->ndata);
                conn->session.packet_type = PACKET_PROXY;
                parent->push_chunk(parent, packet->data, packet->ndata);
                //fprintf(stdout, "%d::OK:%d\n", __LINE__, packet->ndata);
                return 0;
            }
        }
err_end: 
        if(conn)conn->over(conn);
    }
    return -1;
}

/* data handler */
int http_proxy_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    char buf[HTTP_BUF_SIZE], *content_type = NULL, *content_encoding = NULL, 
         *p = NULL, *out = NULL, *s = NULL;
    int n = 0, i = 0, nout = 0, is_need_compress = 0;
    HTTP_RESPONSE *http_resp = NULL;
    CONN *parent = NULL;

    if(conn && packet && packet->ndata > 0 && packet->data 
            && cache && cache->ndata > 0 && (http_resp = (HTTP_RESPONSE *)cache->data) 
            && chunk && chunk->ndata && chunk->data)
    {
        if((n = http_resp->headers[HEAD_ENT_CONTENT_ENCODING]))
            content_encoding = http_resp->hlines + n;
        else 
            content_encoding = "";
        if((n = http_resp->headers[HEAD_ENT_CONTENT_TYPE]) > 0 
                && (content_type = http_resp->hlines + n)
                && (nout = http_charset_convert(content_type, content_encoding, 
                chunk->data, chunk->ndata, http_default_charset, is_need_compress, &out)) > 0)
        {
            p = buf;
            p += sprintf(p, "%s\r\n", http_resp->hlines);
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
               if(HEAD_ENT_CONTENT_ENCODING == i
               || HEAD_ENT_CONTENT_LENGTH == i
               || HEAD_ENT_CONTENT_TYPE == i
               || HEAD_RESP_SET_COOKIE == i)
               {
                    continue;
               }
               else if((n = http_resp->headers[i]) > 0 && (s = (http_resp->hlines + n)))
               {
                    p += sprintf(p, "%s %s\r\n", http_headers[i].e, s);
               }
            }
            /*
            memcpy(p, packet->data, packet->ndata - 2);
            p += packet->ndata - 2;
            */
            if(is_need_compress)
                p += sprintf(p, "%s deflate\r\n", http_headers[HEAD_ENT_CONTENT_ENCODING].e);
            p += sprintf(p, "%s text/html;charset=%s\r\n", http_headers[HEAD_ENT_CONTENT_TYPE].e, 
                        http_default_charset);
            p += sprintf(p, "%s %d\r\n", http_headers[HEAD_ENT_CONTENT_LENGTH].e, nout);
            p += sprintf(p, "%s", "\r\n");
            //conn->push_exchange(conn, buf, (p - buf));
            //conn->push_exchange(conn, out, nout);
            if(conn->session.parent
                    && (parent = hitaskd->findconn(hitaskd, conn->session.parentid))
                    && conn->session.parent == parent)
            {
                parent->push_chunk(parent, buf, (p - buf));
                parent->push_chunk(parent, out, nout);
            }
            if(out) http_charset_convert_free(out);
            return 0;
        }
        else
        {
            if(conn->session.parent
                    && (parent = hitaskd->findconn(hitaskd, conn->session.parentid))
                    && conn->session.parent == parent)
            {
                parent->push_chunk(parent, packet->data, packet->ndata);
                parent->push_chunk(parent, chunk->data, chunk->ndata);
            }
            return 0;
        }
    }
    if(conn) conn->over(conn);
    return -1;
}

/* error handler */
int http_proxy_error_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    if(conn)
    {
        return http_proxy_data_handler(conn, packet, cache, chunk);
    }
    return -1;
}

/* timeout handler */
int http_proxy_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    if(conn)
    {
        return http_proxy_data_handler(conn, packet, cache, chunk);
    }
    return -1;
}

/* bind proxy  */
int hitaskd_bind_proxy(CONN *conn, char *host, int port) 
{
    struct hostent *hp = NULL;
    CONN *new_conn = NULL;
    SESSION session = {0};
    char *ip = NULL, cip[HTTP_IP_MAX];
    unsigned char *sip = NULL;
    int bitip = 0;

    if(conn && host && port > 0)
    {
        if((bitip = ltask->get_host_ip(ltask, host)) != -1) 
        {
            sip = (unsigned char *)&bitip;
            ip = cip;
            sprintf(ip, "%d.%d.%d.%d", sip[0], sip[1], sip[2], sip[3]);
        }
        else
        {
            if((hp = gethostbyname(host))
                && sprintf(cip, "%s", inet_ntoa(*((struct in_addr *)(hp->h_addr))))> 0)
            {
                ip = cip;
                bitip = inet_addr(ip);
                ltask->set_host_ip(ltask, host, &bitip, 1);
            }
        }
        if(ip)
        {
            session.packet_type = PACKET_PROXY;
            session.timeout = proxy_timeout;
#ifdef  _HTTP_CHARSET_CONVERT
            session.packet_type |= PACKET_CUSTOMIZED;
            session.packet_reader = &http_proxy_packet_reader;
            session.packet_handler = &http_proxy_packet_handler;
            session.data_handler = &http_proxy_data_handler;
            session.timeout_handler = &http_proxy_timeout_handler;
            session.error_handler = &http_proxy_error_handler;
#endif
            if((new_conn = hitaskd->newproxy(hitaskd, conn, -1, -1, ip, port, &session)))
            {
                new_conn->start_cstate(new_conn);
                return 0;
            }
        }
    }
    return -1;
}

 
/* http proxy packet handler */
int http_proxy_handler(CONN *conn,  HTTP_REQ *http_req)
{
    char buf[HTTP_BUF_SIZE], *host = NULL, *path = NULL, 
         *s = NULL, *p = NULL;
    int n = 0, i = 0, port = 80;

    if(conn && http_req)
    {
        p = http_req->path;
        if(strncasecmp(p, "http://", 7) == 0)
        {
            p += 7;
            host = p;
            while(*p != '\0' && *p != ':' && *p != '/') ++p;
            if(*p == ':')
            {
                *p++ = '\0'; 
                port = atoi(p);
                while(*p >= '0' && *p <= '9') ++p;
                path = p;
            }
            else if(*p == '/') {*p++ = '\0'; path = p;}
            else if(*p == '\0') path = "";
            else path = p;
        }
        else
        {
            if((n = http_req->headers[HEAD_REQ_HOST]) > 0 )
            {
                path = p;
                host = (http_req->hlines + n);
            }
            else goto err_end;
        }
        if(path && *path == '/') ++path;
        if(path == NULL) path = "";
        //authorized 
        if(http_req->reqid == HTTP_GET)
        {
            p = buf;
            p += sprintf(p, "GET /%s HTTP/1.0\r\n", path);
            if(host) p += sprintf(p, "Host: %s\r\n", host);
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
                if(HEAD_REQ_HOST == i && host) continue;
                if(HEAD_REQ_REFERER == i || HEAD_REQ_COOKIE == i) continue;
                if((n = http_req->headers[i]) > 0 && (s = (http_req->hlines + n)))
                {
                    p += sprintf(p, "%s %s\r\n", http_headers[i].e, s);
                }
            }
            p += sprintf(p, "%s", "\r\n");
            fprintf(stdout, "%s", buf);
            conn->push_exchange(conn, buf, (p - buf));
        }
        else if(http_req->reqid == HTTP_POST)
        {
            p = buf;
            p += sprintf(p, "POST /%s HTTP/1.0\r\n", path);
            if(host) p += sprintf(p, "Host: %s\r\n", host);
            for(i = 0; i < HTTP_HEADER_NUM; i++)
            {
                if(HEAD_REQ_HOST == i && host) continue;
                if(HEAD_REQ_REFERER == i || HEAD_REQ_COOKIE == i) continue;
                if((n = http_req->headers[i]) > 0 && (s = http_req->hlines + n))
                {
                    p += sprintf(p, "%s %s\r\n", http_headers[i].e, s);
                }
            }
            p += sprintf(p, "%s", "\r\n");
            fprintf(stdout, "%s", buf);
            conn->push_exchange(conn, buf, (p - buf));
            if((n = http_req->headers[HEAD_ENT_CONTENT_LENGTH]) > 0 
                    && (n = atol(http_req->hlines + n)) > 0)
            {
                conn->recv_chunk(conn, n);
            }
        }
        else goto err_end;
        if(hitaskd_bind_proxy(conn, host, port) == -1) goto err_end;
        return 0;
    }
err_end:
    conn->push_chunk(conn, HTTP_BAD_REQUEST, strlen(HTTP_BAD_REQUEST));
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

/* new task */
int hitaskd_newtask(CONN *conn)
{
    //int n = 0, urlnodeid = -1, urlid = -1, referid = -1;
    //URLNODE urlnode = {0}, parent = {0};
    char buf[HTTP_BUF_SIZE];
    int n = 0, urlid = 0;

    if(conn)
    {   
        //fprintf(stdout, "%s::%d OK\n", __FILE__,__LINE__);
        DEBUG_LOGGER(hitaskd_logger, "newtask()::%d on connection[%s:%d] local[%s:%d] via %d",
                conn->s_id, conn->remote_ip, conn->remote_port, 
                conn->local_ip, conn->local_port, conn->fd);
        if((urlid = ltask->get_urltask(ltask, -1, -1, -1, -1, buf, &n)) >= 0 && n > 0) 
        {
            DEBUG_LOGGER(hitaskd_logger, "Ready for download-urlid:%d buffer_len:%d "
                    " to download-node[%s:%d] local[%s:%d] via %d", urlid, n, conn->remote_ip, 
                    conn->remote_port, conn->local_ip, conn->local_port, conn->fd);
            conn->over_evstate(conn);
            conn->start_cstate(conn);
            conn->c_id = urlid;
            return conn->push_chunk(conn, buf, n);
        }
        else 
        {
            conn->c_id = -1;
            ERROR_LOGGER(hitaskd_logger, "get_urltask() failed");
            goto time_out;
        }
        /*
           if((urlnodeid = hibase->pop_urlnode(hibase, &urlnode)) > 0 
           && (urlid = urlnode.urlid) >= 0)
           {
           DEBUG_LOGGER(hitaskd_logger, "pop_urlnode(%d) on %s:%d via %d", urlnodeid, conn->remote_ip, conn->remote_port, conn->fd);
        //fprintf(stdout, "urlid:%d parent:%d node:%d\n", urlnode.urlid, urlnode.parentid, urlnode.tnodeid);
        //fprintf(stdout, "urlid:%d\n", urlnode.urlid);
        //fprintf(stdout, "%d::usernodeid:%d userid:%d\n", __LINE__, urlnodeid, urlid);
        if(urlnode.parentid> 0 && hibase->get_urlnode(hibase,urlnode.parentid,&parent) > 0)
        {
        referid = parent.urlid;
        }
        if((ltask->get_urltask(ltask,urlnode.urlid,referid,urlnodeid,-1,buf,&n))>= 0 && n > 0)
        {
        //fprintf(stdout, "%d::usernodeid:%d userid:%d %s\n", 
        //    __LINE__, urlnodeid, urlid, buf);
        conn->over_evstate(conn);
        return conn->push_chunk(conn, buf, n);
        }
        goto time_out;
        }
        else
        {
        }
        */
        //fprintf(stdout, "%s::%d URLNODEID:%d OK\n", __FILE__,__LINE__, urlnodeid);
time_out:
        if(conn->timeout >= TASK_WAIT_MAX) conn->timeout = 0;
        conn->wait_evstate(conn);
        return conn->set_timeout(conn, conn->timeout + TASK_WAIT_TIMEOUT);
    }
    return -1;
}

/* packet handler */
int hitaskd_packet_handler(CONN *conn, CB_DATA *packet)
{
    int urlid = 0, n = 0, ips = 0, err = 0, uuid = 0, nrawdata = 0;
    char buf[HTTP_BUF_SIZE], file[HTTP_PATH_MAX], *host = NULL, 
        *ip = NULL, *p = NULL, *end = NULL;
    struct stat st = {0};
    HTTP_REQ http_req = {0};

    if(conn)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        /*
        *end = '\0';
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
        //proxy special
        if(strncasecmp(http_req.path, "/proxy/", 7) == 0)
        {
            strcpy(http_req.path, http_req.path + 7);
            conn->save_cache(conn, &http_req, sizeof(HTTP_REQ));
            return http_proxy_handler(conn, &http_req);
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
                        p += sprintf(p, "HTTP/1.0 200 OK\r\nContent-Length:%lld\r\n"
                                "Content-Type: text/html;charset=%s\r\n",
                                (long long int)(st.st_size), http_default_charset); 
                        if((n = http_req.headers[HEAD_GEN_CONNECTION]) > 0)
                            p += sprintf(p, "Connection: %s\r\n", http_req.hlines + n);
                        p += sprintf(p, "Last-Modified:");
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
            if((n = http_req.headers[HEAD_GEN_TASK_TYPE]) > 0)
                conn->s_id = atoi(http_req.hlines +n);
            urlid = atoi(http_req.path);
            //error 
            if(urlid >= 0 && (n = http_req.headers[HEAD_GEN_WARNING]) > 0)
            {
                err = atoi(http_req.hlines + n);
                if((n = http_req.headers[HEAD_GEN_UUID]) > 0
                    && (uuid = atoi(http_req.hlines + n)) > 0)
                    //&& err != ERR_HTTP_RESP)
                {
                    DEBUG_LOGGER(hitaskd_logger, "ERR-redownload urlid:%d uuid:%d", urlid, uuid);
                    hibase->update_urlnode(hibase, uuid, 1);
                }
                ltask->set_url_status(ltask, urlid, NULL, URL_STATUS_ERR, err);
            }
            if((n = http_req.headers[HEAD_GEN_RAW_LENGTH]) > 0 
                && (nrawdata = atoi(http_req.hlines + n)) == 0)
            {
                ltask->set_url_status(ltask, urlid, NULL, URL_STATUS_OK, ERR_NODATA);
            }
            /* get new task */
            return hitaskd_newtask(conn);
        }
        else goto err_end;
        return 0;
    }
err_end:
    conn->push_chunk(conn, HTTP_BAD_REQUEST, strlen(HTTP_BAD_REQUEST));
    return -1;
}
#define URLNODE_BLOCK_MAX 1024 * 1024 * 32
#define VIEW_URLNODES(conn, pp, p, buf, node_id, tnode,  tnodes, ntnodes,                       \
        urlnodes, nurlnodes, total, x, ret)                                                     \
do                                                                                              \
{                                                                                               \
    if((p = pp = (char *)calloc(1, URLNODE_BLOCK_MAX)))                                         \
    {                                                                                           \
        hibase->get_tnode(hibase, node_id, &tnode);                                             \
        p += sprintf(p, "({'nodeid':'%d', 'parent':'%d', 'name':'%s', 'ntnodes':'%d',"          \
            "'nurlnodes':'%d', 'total':'%d', 'pages':'%d',",node_id, tnode.parent, tnode.name,  \
            ntnodes, nurlnodes, total, (total/http_page_num)+((total%http_page_num)>0));        \
        if(tnodes && ntnodes > 0)                                                               \
        {                                                                                       \
            p += sprintf(p, "'tnodes':{");                                                      \
            for(x = 0; x < ntnodes; x++)                                                        \
            {                                                                                   \
                p += sprintf(p, "'%d':{'id':'%d','name':'%s','nchilds':'%d'},",                 \
                    tnodes[x].id, tnodes[x].id, tnodes[x].name, tnodes[x].nchilds);             \
            }                                                                                   \
            --p;                                                                                \
            p += sprintf(p, "},");                                                              \
        }                                                                                       \
        if(urlnodes && nurlnodes > 0)                                                           \
        {                                                                                       \
            p += sprintf(p, "'urlnodes':{");                                                    \
            for(x = 0; x < nurlnodes; x++)                                                      \
            {                                                                                   \
                memset(buf, 0, HTTP_URL_MAX);                                                   \
                ret = ltask->get_url(ltask, urlnodes[x].urlid, buf);                            \
                p += sprintf(p, "'%d':{'id':'%d', 'nodeid':'%d', 'level':'%d', "                \
                        "'nchilds':'%d', 'urlid':'%d', 'url':'%s', 'status':'%d'},",            \
                        urlnodes[x].id, urlnodes[x].id, urlnodes[x].tnodeid, urlnodes[x].level, \
                        urlnodes[x].nchilds, urlnodes[x].urlid, buf, ret);                      \
            }                                                                                   \
            --p;                                                                                \
            p += sprintf(p, "}");                                                               \
        }                                                                                       \
        p += sprintf(p, "})");                                                                  \
        x = sprintf(buf, "HTTP/1.0 200\r\nContent-Type:text/html;charset=%s\r\n"                \
                "Content-Length:%ld\r\nConnection:close\r\n\r\n",                               \
                http_default_charset, (long)(p - pp));                                          \
        conn->push_chunk(conn, buf, x);                                                         \
        conn->push_chunk(conn, pp, (p - pp));                                                   \
        free(pp); pp = NULL;                                                                    \
    }                                                                                           \
}while(0)

/*  data handler */
int hitaskd_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    int i = 0, id = 0, n = 0, op = -1, nodeid = -1, x = -1, fieldid = -1,
        parentid = -1, urlid = -1, hostid = -1, tableid = -1, type = -1, 
        flag = -1, templateid = -1, urlnodeid = -1, level = -1, count = 0, 
        page = 1, from = 0, total = 0, ret = 0, is_purl = 0;
    char *p = NULL, *end = NULL, *name = NULL, *host = NULL, *url = NULL, *link = NULL, 
         *pattern = NULL, *map = NULL, *linkmap = NULL, *pp = NULL, 
         format[HTTP_URL_MAX], buf[HTTP_BUF_SIZE], block[HTTP_BUF_SIZE];
    HTTP_REQ httpRQ = {0}, *http_req = NULL;
    TNODE *tnodes = NULL, tnode = {0};
    ITEMPLATE template = {0};
    URLNODE *urlnodes = NULL;
    PURL purl = {0};
    //PURL purls[PURL_NUM_MAX];
    double speed = 0.0;
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
                                case E_ARGV_LINK:
                                    link = p;
                                    break;
                                case E_ARGV_LINKMAP:
                                    linkmap = p;
                                    break;
                                case E_ARGV_URLNODEID:
                                    urlnodeid = atoi(p);
                                    break;
                                case E_ARGV_LEVEL:
                                    level = atoi(p);
                                    break;
                                case E_ARGV_SPEED:
                                    speed = atof(p);
                                    break;
                                case E_ARGV_PAGE:
                                    page = atoi(p);
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
                }
                if(map)
                {
                    p = map;
                    while(*p != '{' && *p != '\0')++p;
                    if(*p != '{') goto err_end;
                    ++p;
                    while(*p != '\0' && *p != '[')++p;
                    if(*p != '[') goto err_end;
                    for(i = 0; i < FIELD_NUM_MAX; i++)
                    {
                        if(*p == '[') ++p;
                        else goto err_end;
                        template.map[i].fieldid = atoi(p);
                        while(*p != '\0' && ((*p >= '0' && *p <= '9') || *p == '-'))++p;
                        while(*p != '\0' && *p != ',')++p;
                        if(*p != ',') goto err_end;
                        ++p;
                        template.map[i].nodeid = atoi(p);
                        while(*p != '\0' && ((*p >= '0' && *p <= '9') || *p == '-'))++p;
                        while(*p != '\0' && *p != ',')++p;
                        if(*p != ',') goto err_end;
                        ++p;
                        template.map[i].flag = atoi(p);
                        while(*p != '\0' && ((*p >= '0' && *p <= '9') || *p == '-'))++p;
                        while(*p != '\0' && *p != ']')++p;
                        ++p;
                        while(*p != '\0' && *p != ';')++p;
                        if(*p != ';') break;
                        ++p;
                        while(*p != '\0' && *p != '[')++p;
                        if(*p != '[') break;
                    }
                    template.nfields = ++i;
                }
                if(link && (x = strlen(link)) && linkmap)
                {
                    memcpy(template.link, link, x);
                    p = linkmap;
                    while(*p != '\0' && *p != '[')++p;
                    if(*p != '[') goto err_end;
                    ++p;
                    template.linkmap.fieldid = atoi(p);
                    while(*p != '\0' && ((*p >= '0' && *p <= '9') || *p == '-'))++p;
                    while(*p != '\0' && *p != ',')++p;
                    if(*p != ',') goto err_end;
                    ++p;
                    template.linkmap.nodeid = atoi(p);
                    while(*p != '\0' && ((*p >= '0' && *p <= '9') || *p == '-'))++p;
                    while(*p != '\0' && *p != ',')++p;
                    if(*p != ',') goto err_end;
                    ++p;
                    template.linkmap.flag = atoi(p);
                    template.flags |= TMP_IS_LINK; 
                }
                if(op == E_OP_URLNODE_ADD && url)
                {
                    is_purl = 0;
                    p = url;
                    while(*p != '\0')
                    {
                        if(*p == '[')
                        {
                            memset(&purl, 0, sizeof(PURL));
                            purl.sfrom = p++;
                            if(*p >= '0' && *p <= '9')
                            {
                                purl.from = atoi(p++);
                                purl.type = PURL_TYPE_INT; 
                                while((*p >= '0' && *p <= '9') || *p == '-')
                                {
                                    if(*p == '-') purl.to = atoi(++p);
                                    else ++p;
                                }
                                if(*p == ']')
                                {
                                    is_purl = 1;
                                    purl.sto = ++p;
                                    if(*p++ == '{')
                                    {
                                        if(*p > '0' && *p <= '9' && *(p+1) == '}')
                                        {
                                            purl.length = atoi(p++);
                                            purl.sto = ++p;
                                        }
                                    }
                                    break;
                                }
                            }
                            else if((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
                            {
                                purl.from = (int)*p++;
                                purl.type = PURL_TYPE_CHAR; 
                                if(*p != '-')continue;
                                ++p;
                                if((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
                                {
                                    purl.to = (int)*p++;
                                    if(*p != ']') continue;
                                    else 
                                    {
                                        purl.sto = ++p;
                                        is_purl = 1;
                                        break;
                                    }
                                }
                            }
                            else ++p;
                        }
                        else ++p;
                    }
                }
                //if(pattern)fprintf(stdout, "%d::%s\n", __LINE__, pattern);
                if(page <= 0) page = 1;
                from = (page - 1) * http_page_num;
                switch(op)
                {
                    case E_OP_NODE_ADD :
                        if(parentid >= 0 && name)
                        {
                            if((id = hibase->add_tnode(hibase, parentid, name)) > 0
                                    && (n = hibase->view_tnode_childs(hibase, parentid, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }
                        else goto err_end;
                        break;
                    case E_OP_NODE_UPDATE :
                        if(parentid >= 0 && nodeid > 0 && name)
                        {
                            DEBUG_LOGGER(hitaskd_logger, "op:%d id:%d name:%s", op, nodeid, name);
                            id = hibase->update_tnode(hibase, parentid, nodeid, name);
                            n = sprintf(buf, "%d\r\n", id);
                            n = sprintf(buf, "HTTP/1.0 200\r\nContent-Type:text/html;charset=%s\r\n"
                                    "Content-Length:%d\r\nConnection:close\r\n\r\n%d\r\n", 
                                    http_default_charset, n, id);
                            conn->push_chunk(conn, buf, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_NODE_DELETE :
                        if(parentid >= 0 && nodeid > 0)
                        {
                            DEBUG_LOGGER(hitaskd_logger, "op:%d id:%d", op, nodeid);
                            id = hibase->delete_tnode(hibase, parentid, nodeid);
                            n = sprintf(buf, "%d\r\n", id);
                            n = sprintf(buf, "HTTP/1.0 200\r\nContent-Type:text/html;charset=%s\r\n"
                                    "Content-Length:%d\r\nConnection:close\r\n\r\n%d\r\n", 
                                    http_default_charset, n, id);
                            conn->push_chunk(conn, buf, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_NODE_CHILDS :
                        if(nodeid >= 0)
                        {
                            //DEBUG_LOGGER(hitaskd_logger, "op:%d id:%d name:%s", op, nodeid, name);
                            if((n = hibase->view_tnode_childs(hibase, nodeid, block)) > 0)
                            {
                                conn->push_chunk(conn, block, n);
                                goto end;
                            }else goto err_end;
                        }else goto err_end;
                        break;
                    case E_OP_NODE_BROTHER :
                        if(nodeid >= 0 && hibase->get_tnode(hibase, nodeid, &tnode) > 0
                                && (n = hibase->view_tnode_childs(hibase, tnode.parent, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
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
                     case E_OP_DATABASE_VIEW:
                        if((n = hibase->view_database(hibase, block)) > 0)
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
                        template.tableid = tableid;
                        if(nodeid >= 0 && pattern && (map||linkmap) && url
                            && (template.flags = flag) >= 0
                            && (n = strlen(pattern)) > 0 && n < PATTERN_LEN_MAX
                            && (memcpy(template.pattern, pattern, n))
                            && (x = strlen(url)) > 0 && x < HI_URL_MAX
                            && (memcpy(template.url, url, x))
                            && hibase->add_template(hibase, nodeid, &template) >= 0
                            && (n = hibase->view_templates(hibase, nodeid, block)) > 0)
                        {
                             conn->push_chunk(conn, block, n);
                             goto end;
                        }else goto err_end;
                        break;
                    case E_OP_TEMPLATE_UPDATE:
                        template.tableid = tableid;
                        if(nodeid >= 0 && templateid >= 0 && pattern && (map||linkmap) && url
                            && (template.flags = flag) >= 0
                            && (n = strlen(pattern)) > 0 && n < PATTERN_LEN_MAX
                            && (memcpy(template.pattern, pattern, n))
                            && (x = strlen(url)) > 0 && x < HI_URL_MAX
                            && (memcpy(template.url, url, x))
                            && hibase->update_template(hibase, templateid, &template) >= 0
                            && (n = hibase->view_templates(hibase, nodeid, block)) > 0)
                        {
                             conn->push_chunk(conn, block, n);
                             goto end;
                        }else goto err_end;
                        break;
                    case E_OP_TEMPLATE_DELETE:
                        if(nodeid >= 0 && templateid >= 0 
                                && hibase->delete_template(hibase, nodeid, templateid) >= 0
                                && (n = hibase->view_templates(hibase, nodeid, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_TEMPLATE_LIST:
                        if(nodeid >= 0 && (n = hibase->view_templates(hibase, nodeid, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_URLNODE_ADD:
                        if(level > 0) flag = URL_IS_PRIORITY;
                        if(parentid < 0) parentid = 0;
                        if(nodeid >= 0 && url && hibase->get_tnode(hibase, nodeid, &tnode) > 0)
                        {
                            if(is_purl)
                            {
                                if((n = (purl.sfrom - url)) > 0)
                                {
                                    memset(buf, 0, HTTP_BUF_SIZE);
                                    memcpy(buf, url, n);
                                    if(purl.type == PURL_TYPE_INT)
                                    {
                                        for(i = purl.from; i <= purl.to; i++)
                                        {
                                            p = buf + n;
                                            if(purl.length > 1)
                                            {
                                                sprintf(format, "%%0%dd", purl.length);
                                                p += sprintf(p, format, i);
                                            }
                                            else
                                            {
                                                p += sprintf(p, "%d", i);
                                            }
                                            p += sprintf(p, "%s", purl.sto);
                                            if((urlid=ltask->add_url(ltask,-1,0, buf, flag))>= 0)
                                                hibase->add_urlnode(hibase, nodeid, parentid, urlid,level);

                                        }
                                    }
                                    else if(purl.type == PURL_TYPE_CHAR)
                                    {
                                        for(i = purl.from; i <= purl.to; i++)
                                        {
                                            p = buf + n;
                                            p += sprintf(p, "%c", (char )i);
                                            p += sprintf(p, "%s", purl.sto);
                                            fprintf(stdout, "%s::%d %s\n", __FILE__, __LINE__, buf);
                                            if((urlid=ltask->add_url(ltask,-1,0, buf, flag))>= 0)
                                                hibase->add_urlnode(hibase, nodeid, parentid, urlid,level);
                                        }
                                    }
                                }
                            }
                            else
                            {
                                if((urlid=ltask->add_url(ltask,-1,0,url, flag))>= 0)
                                    hibase->add_urlnode(hibase, nodeid, parentid, urlid,level);
                            }
                            if((n = hibase->get_tnode_urlnodes(hibase, nodeid, &urlnodes, 
                                            &total, from, http_page_num)) > 0)
                            {
                                count = hibase->get_tnode_childs(hibase, nodeid, &tnodes);
                                hibase->get_tnode(hibase, nodeid, &tnode);
                                VIEW_URLNODES(conn,pp,p,buf,nodeid,tnode,tnodes,count,urlnodes,n,total,i,ret);
                                hibase->free_urlnodes(urlnodes);
                                goto end;
                            }else goto err_end;
                        }
                        else 
                        {
                            fprintf(stdout, "%d::%d:%s %d:%d\n", __LINE__, 
                                    urlid, url, urlnodeid, flag);
                            goto err_end;
                        }
                        break;
                    case E_OP_URLNODE_UPDATE:
                        if(nodeid >= 0 && urlnodeid > 0 && level >= 0 
                            && (hibase->update_urlnode(hibase, urlnodeid, level))> 0
                            && (ltask->set_url_level(ltask, urlid, NULL, level)) == 0
                            && (n = hibase->get_tnode_urlnodes(hibase, nodeid, &urlnodes,
                                    &total, from, http_page_num)) > 0)
                        {
                            count = hibase->get_tnode_childs(hibase, nodeid, &tnodes);
                            hibase->get_tnode(hibase, nodeid, &tnode);
                            VIEW_URLNODES(conn,pp,p,buf,nodeid,tnode,tnodes,count,urlnodes,n,total,i,ret);
                            hibase->free_urlnodes(urlnodes);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_URLNODE_DELETE:
                        if(nodeid >= 0 && urlnodeid > 0 
                            && hibase->delete_urlnode(hibase, urlnodeid) > 0
                            && (n = hibase->get_tnode_urlnodes(hibase, nodeid, &urlnodes,
                                    &total, from, http_page_num)) > 0)
                        {
                            count = hibase->get_tnode_childs(hibase, nodeid, &tnodes);
                            hibase->get_tnode(hibase, nodeid, &tnode);
                            VIEW_URLNODES(conn,pp,p,buf,nodeid,tnode,tnodes,count,urlnodes,n,total,i,ret);
                            hibase->free_urlnodes(urlnodes);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_URLNODE_CHILDS:
                        if(urlnodeid > 0 && hibase->get_urlnode_childs(hibase, 
                                    urlnodeid, &urlnodes)> 0)
                        {
                            nodeid = urlnodes[0].tnodeid;
                            n = hibase->get_tnode_urlnodes(hibase, nodeid, &urlnodes, &total, from, http_page_num);
                            hibase->get_tnode(hibase, nodeid, &tnode);
                            VIEW_URLNODES(conn,pp,p,buf,nodeid,tnode,tnodes,count,urlnodes,n,total,i,ret);
                            hibase->free_urlnodes(urlnodes);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_URLNODE_LIST:
                        if(nodeid >= 0)
                        {
                            n = hibase->get_tnode_urlnodes(hibase, nodeid, &urlnodes, &total, from, http_page_num);
                            count = hibase->get_tnode_childs(hibase, nodeid, &tnodes);
                            hibase->get_tnode(hibase, nodeid, &tnode);
                            VIEW_URLNODES(conn,pp,p,buf,nodeid,tnode,tnodes,count,urlnodes,n,total,i,ret);
                            if(tnodes)hibase->free_tnode_childs(tnodes);
                            if(urlnodes)hibase->free_urlnodes(urlnodes);
                            goto end;
                        }
                        break;
                    case E_OP_DNS_ADD:
                        if(host && ltask->add_dns(ltask, host) >= 0 
                                && (n = ltask->view_dnslist(ltask, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_DNS_DELETE:
                        if(hostid >= 0 && ltask->del_dns(ltask, hostid, NULL) >= 0 
                                && (n = ltask->view_dnslist(ltask, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_DNS_LIST:
                        if((n = ltask->view_dnslist(ltask, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_PROXY_ADD:
                        if(host && ltask->add_proxy(ltask, host) >= 0
                                && (n = ltask->view_proxylist(ltask, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_PROXY_DELETE:
                        if(hostid >= 0 && ltask->del_proxy(ltask, hostid, NULL) >= 0 
                                && (n = ltask->view_proxylist(ltask, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_PROXY_LIST:
                        if((n = ltask->view_proxylist(ltask, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
                        break;
                    case E_OP_SPEED_LIMIT:
                        ltask->set_speed_limit(ltask, speed);
                        if((n = ltask->get_stateinfo(ltask, block)) > 0)
                        {
                            conn->push_chunk(conn, block, n);
                            goto end;
                        }else goto err_end;
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
    if(conn && conn->evstate == EVSTATE_WAIT)
    {
            return hitaskd_newtask(conn);
        /*
        else
        {
            ERROR_LOGGER(hitaskd_logger, "Closing connection[%s:%d] via %d", 
                    conn->remote_ip, conn->remote_port, conn->fd);
            return conn->over(conn);
        }*/
    }
    return -1;
}

/* error handler */
int hitaskd_error_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    if(conn)
    {
        if(conn->c_id >= 0) 
            ltask->set_url_status(ltask, conn->c_id, NULL, URL_STATUS_ERR, ERR_NETWORK);
        return 0;
    }
    return -1;
}

/* OOB handler */
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
        //*end = '\0';
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
            if((n = http_req.headers[HEAD_ENT_CONTENT_LENGTH]) > 0) 
            {
                if((n = atol(http_req.hlines + n)) >= 0)
                {
                    conn->save_cache(conn, &http_req, sizeof(HTTP_REQ));
                    conn->recv_chunk(conn, n);
                    DEBUG_LOGGER(histore_logger, "Ready for recv_chunk[%d] urlid:%d "
                            "from %s:%d via %d", n, urlid, conn->remote_ip, 
                            conn->remote_port, conn->fd);
                }
            }
            else
            {
                FATAL_LOGGER(histore_logger, "recv-data failed urlid:%d from remote[%s:%d]"
                        " local[%s:%d] via %d", urlid, conn->remote_ip, conn->remote_port,
                        conn->local_ip, conn->local_port, conn->fd);
            }
            /*
               else
               {
               FATAL_LOGGER(histore_logger, "recv-data-urlid failed from remote[%s:%d]"
               " local[%s:%d] via %d", conn->remote_ip, conn->remote_port,
               conn->local_ip, conn->local_port, conn->fd);

               }*/
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
    int urlid = 0, n = 0, nrawdata = 0, ndownload = 0;
    char *date = NULL, *p = NULL;
    HTTP_REQ *http_req = NULL;

    if(conn && packet && cache && chunk)
    {
        DEBUG_LOGGER(histore_logger, "recv-data-urlid:%d data_len:%d "
                        " remote[%s:%d] local[%s:%d] via %d ", urlid, chunk->ndata, 
                        conn->remote_ip, conn->remote_port, conn->local_ip, 
                        conn->local_port, conn->fd);
        if(chunk->ndata > 0 && (http_req = (HTTP_REQ *)cache->data))
        {
            DEBUG_LOGGER(histore_logger, "reqid:%d data-urlid:%d data_len:%d "
                        " remote[%s:%d] local[%s:%d] via %d ", http_req->reqid, 
                        urlid, chunk->ndata, conn->remote_ip, conn->remote_port, 
                        conn->local_ip, conn->local_port, conn->fd);
            if(http_req->reqid == HTTP_PUT)
            {
                urlid = atoi(http_req->path);
                if((n = http_req->headers[HEAD_ENT_LAST_MODIFIED]) > 0)
                {
                    date = (http_req->hlines + n);
                }
                if((n = http_req->headers[HEAD_GEN_RAW_LENGTH]) > 0)
                    nrawdata = atoi(http_req->hlines + n);
                if((n = http_req->headers[HEAD_GEN_DOWNLOAD_LENGTH]) > 0)
                    ndownload = atoi(http_req->hlines + n);
                /* doctype */
                if((n = http_req->headers[HEAD_ENT_CONTENT_TYPE]) > 0)
                    p = http_req->hlines + n;
                DEBUG_LOGGER(histore_logger, "recv-urlid:%d data_len:%d "
                        " remote[%s:%d] local[%s:%d] via %d ", urlid, chunk->ndata, 
                        conn->remote_ip, conn->remote_port, conn->local_ip, 
                        conn->local_port, conn->fd);
                ltask->update_content(ltask, urlid, date, p, chunk->data, chunk->ndata, 
                        nrawdata, ndownload, is_need_extract_link);
                /*
                if((n = http_req->headers[HEAD_GEN_UUID]) > 0
                    && (uuid = atoi(http_req->hlines + n)) > 0)
                {
                    fprintf(stdout, "%d::over download urlnode:%d doc_len:%d\n", 
                            __LINE__, uuid, chunk->ndata);
                    hibase->push_task_urlnodeid(hibase, uuid);
                }
                */
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

/* histore error handler */
int histore_error_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

/* histore pcre match */
void histore_data_matche(ITEMPLATE *templates, int ntemplates, TNODE *tnode, URLNODE *urlnode,
        LDOCHEADER *docheader, char *content, int ncontent, char *url, char *type)
{
    char *p = NULL, *e = NULL, *pp = NULL, *epp = NULL, *s = NULL, *es = NULL, *end = NULL, 
         *host = NULL, *path = NULL, *last = NULL, newurl[HTTP_URL_MAX];
    int i = -1, j = 0, flag = 0, start_offset = 0, erroffset = 0, res[FIELD_NUM_MAX * 2], 
        nres = 0, n = 0, count = 0, x = 0, urlid = 0, nodeid = 0, id = 0,
        parentid = 0, start = 0, over = 0, length = 0, level = 0, urlflag = 0;
    const char *error = NULL;
    pcre *reg = NULL;
    PRES *pres = NULL;

    if(templates && ntemplates > 0 && tnode && urlnode && docheader && content 
            && ncontent > 0 && url && type)
    {
        for(i = 0; i < ntemplates; i++)
        {
            flag = PCRE_DOTALL|PCRE_MULTILINE|PCRE_UTF8;
            if(templates[i].flags & TMP_IS_IGNORECASE) flag |= PCRE_CASELESS;
            if((reg = pcre_compile(templates[i].pattern, flag, &error, &erroffset, NULL))) 
            {
                nres = FIELD_NUM_MAX * 2;
                pres = (PRES *)res;
                start_offset = 0;
                while(start_offset >= 0)
                {
                    if((count = pcre_exec(reg, NULL, content, ncontent, 
                                    start_offset, 0, res, nres)) > 0 )
                    {
                        if((templates[i].flags & TMP_IS_GLOBAL)) 
                            start_offset = pres[count - 1].end;
                        else start_offset = -1;
                        if(templates[i].flags & TMP_IS_LINK)
                        {
                            //link
                            p = templates[i].link;
                            pp = newurl;
                            epp = newurl + HTTP_URL_MAX;
                            memset(newurl, 0, HTTP_URL_MAX);
                            MATCHEURL(count, p, pp, epp, s, es, x, pres, content);
                            DEBUG_LOGGER(histore_logger, "Matched count:%d nfields:%d flag:%d",  
                                    count, templates[i].nfields, templates[i].linkmap.flag);
                            level = 0;
                            urlflag = 0;
                            if(templates[i].linkmap.flag & REG_IS_LIST) 
                            {
                                urlflag |= URL_IS_PRIORITY;
                                level = 1;
                            }
                            if(templates[i].linkmap.flag & REG_IS_POST) urlflag |=  URL_IS_POST;
                            if(pp>newurl && *pp == '\0'&& (nodeid=templates[i].linkmap.nodeid)>0 
                             &&(urlid=ltask->add_url(ltask,urlnode->urlid,0,newurl,urlflag))>= 0)
                            {
                                DEBUG_LOGGER(histore_logger, "extract link-url:%s", newurl);
                                //nodeid = templates[i].linkmap.nodeid;
                                if(nodeid == urlnode->tnodeid) parentid = urlnode->parentid;
                                else 
                                {
                                    if((parentid = hibase->find_tnode_from_parents(hibase, 
                                            urlnode->parentid, nodeid)) <= 0)
                                    parentid = urlnode->id;
                                }
                                id = hibase->add_urlnode(hibase, nodeid, parentid, urlid,level);
                                DEBUG_LOGGER(histore_logger,"new-URLNODE id:%d urlid:%d tnodeid:%d urlnode->parentid:%d url:%s level:%d", id, urlid, nodeid, parentid, newurl, level);
                            }
                            else
                            {
                                ERROR_LOGGER(histore_logger, "link error link:%s pattern:%s url:%s",
                                        templates[i].link, templates[i].pattern, url);
                            }
                        }
                        else
                        {
                            //fprintf(stdout, "MATCH_RES:");
                            for(j = 1; j < count; j++)
                            {
                                x = j - 1;
                                start = res[2*j];
                                over = res[2*j+1];
                                length = over - start;
                                //fprintf(stdout, "[%.*s]", (over-start), content+start);
                                //continue;
                                //fprintf(stdout, "%s::%d %.*s\n", __FILE__,__LINE__,
                                //length, content+start);
                                //handling data
                                //DEBUG_LOGGER(histore_logger, "Matched count:%d nfields:%d flag:%d",
                                //count, templates[i].nfields, templates[i].map[x].flag);
                                nodeid = templates[i].map[x].nodeid;
                                if((templates[i].map[x].flag & REG_IS_URL) && nodeid > 0 
                                        && length > 0 && length < HTTP_URL_MAX 
                                        && x < templates[i].nfields)
                                {
                                    //fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
                                    //add to urlnode
                                    p = content + start;
                                    e = content + over;
                                    pp = newurl;
                                    memset(newurl, 0, HTTP_URL_MAX);
                                    epp = pp + HTTP_URL_MAX - 1;
                                    s = url; 
                                    es = url + docheader->nurl;
                                    CPURL(s, es, p, e, pp, epp, end, host, path, last);
                                    n = (pp - newurl);
                                    DEBUG_LOGGER(histore_logger, "RES-URL url:%s newurl:%s %.*s",
                                            url, newurl, (over-start), content+start);
                                    urlflag = 0;
                                    if(templates[i].map[x].flag & REG_IS_LIST) 
                                    {
                                        urlflag |= URL_IS_PRIORITY;
                                        level = 1;
                                    }
                                    if(templates[i].map[x].flag & REG_IS_FILE) 
                                    {
                                        urlflag |= URL_IS_FILE;
                                    }
                                    if(pp > newurl && *pp == '\0' && (urlid = ltask->add_url(
                                                    ltask, urlnode->urlid, 0,newurl,urlflag))>= 0)
                                    {   
                                        //fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
                                        if(nodeid == urlnode->tnodeid) parentid = urlnode->parentid;
                                        else 
                                        {
                                            if((parentid = hibase->find_tnode_from_parents(hibase, 
                                                urlnode->parentid, nodeid)) <= 0)
                                            parentid = urlnode->id;
                                        }
                                        level = 0;
                                        //fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
                                        if(templates[i].map[x].flag & REG_IS_LIST) level = 1;
                                        id=hibase->add_urlnode(hibase,nodeid,parentid,urlid,level);
                                        DEBUG_LOGGER(histore_logger,"new-urlnode:%s id:%d x:%d tnodeid:%d urlnode->parentid:%d urlid:%d level:%d", newurl, id, x, nodeid, parentid, urlid, urlnode->level);
                                    }
                                    else
                                    {
                                        ERROR_LOGGER(hitaskd_logger, "matche url:%s failed", url);
                                        //ERROR_LOGGER(hitaskd_logger, "matche %s url:%s failed",
                                        //        templates[i].pattern, url);
                                    }
                                }
                                else
                                {
                                    //DEBUG_LOGGER(histore_logger, "%s::%d [%d][%d] %d-%d length:%d\n",
                                    //        __FILE__, __LINE__, i, j, start, over, length);
                                }
                            }
                            //fprintf(stdout, "\n");
                        }
                    }
                    else
                    {
                        start_offset = -1;
                        if(n == PCRE_ERROR_NOMATCH)
                        {
                            FATAL_LOGGER(histore_logger, 
                                    "No match result pattern[%s] url[%s]", templates[i].pattern, url);
                        }
                    }
                }
                pcre_free(reg);
            }
            else
            {
                //fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__, ntemplates);
                //error
                FATAL_LOGGER(histore_logger, "pcre compile error:%s at offset:%d\n", 
                        error, erroffset);
            }
        }
    }
    //_exit(-1);
    return ;
}

/* task handler */
void histore_task_handler(void *arg)
{
    char *block = NULL, *url = NULL, *type = NULL, *content = NULL, *zdata = NULL;
    int i = 0, id = 0, urlid = -1, urlnodeid = -1, len = 0, 
        count = -1, n = 0, *uris = NULL; 
    LDOCHEADER *docheader = NULL;
    ITEMPLATE *templates = NULL;
    URLNODE urlnode = {0};
    void *argx = NULL;
    TNODE tnode = {0};
    size_t ndata = 0;

    DEBUG_LOGGER(histore_logger, "Ready for task_handler();");
    if((urlid  = (int)((long)arg)) >= 0
            && (n = hibase->get_uris(hibase, urlid, &uris)) > 0
            //&& hibase->get_urlnode(hibase, id, &urlnode) > 0 && urlnode.tnodeid > 0 
            //&& hibase->get_tnode(hibase, urlnode.tnodeid, &tnode) > 0 
            && (len = ltask->get_content(ltask, urlid, &block)) > 0
            && len > sizeof(LDOCHEADER) && (docheader = (LDOCHEADER *)block))
    {
        url = block + sizeof(LDOCHEADER);
        type = url + docheader->nurl + 1;
        zdata = type + docheader->ntype + 1;
        ndata = docheader->ncontent * 16;
        DEBUG_LOGGER(histore_logger, "ready for deal url:%s type:%s ncontent:%d -> ndata:%d", 
                url, type, docheader->ncontent, (int)ndata);
        if(type && strncasecmp(type, "text", 4) == 0 && zdata < (block + len) 
                && (content = (char *)calloc(1, ndata)))
        {
            DEBUG_LOGGER(histore_logger, "ready for decompress %d:%d ",
                    docheader->ncontent, (int)ndata);
            if(zdecompress((Bytef *)zdata, (uLong )(docheader->ncontent), 
                        (Bytef *)content, (uLong *)&ndata) == 0)
            {
                DEBUG_LOGGER(histore_logger, "compressed nzdata:%d -> ndata:%d", 
                        (int)docheader->ncontent, (int)ndata);
                for(i = 0; i < n; i++)
                {
                    memset(&urlnode, 0, sizeof(URLNODE));
                    memset(&tnode, 0, sizeof(TNODE));
                    urlnodeid = uris[i];
                    count = 0;
                    DEBUG_LOGGER(histore_logger, "Ready for reading urlnode:%d", urlnodeid);
                    if(hibase->get_urlnode(hibase, urlnodeid, &urlnode) > 0 && urlnode.tnodeid > 0
                            && hibase->get_tnode(hibase, urlnode.tnodeid, &tnode) > 0
                            && (count = hibase->get_tnode_templates(hibase, 
                                    urlnode.tnodeid, &templates)) > 0) 
                    {
                        DEBUG_LOGGER(histore_logger,"ready for deal url:%d nodeid:%d "
                                "content_len:%d nurl:%d ntype:%d ncentent:%d", urlnode.urlid, 
                                urlnode.tnodeid, len, docheader->nurl, docheader->ntype, 
                                docheader->ncontent);
                        histore_data_matche(templates, count, &tnode, &urlnode,
                                docheader, content, ndata, url, type);
                        if(templates)hibase->free_templates(templates);
                        templates = NULL;
                    }
                    else
                    {
                        ERROR_LOGGER(histore_logger, "Read urlnode:%d failed, "
                                "tnodeid:%d templates_count:%d ",urlnodeid,urlnode.tnodeid,count);
                    }
                }
            }
            else
            {
                ERROR_LOGGER(hitaskd_logger, "decompress failed, %s", strerror(errno));
            }
            if(content) free(content);
        }
    }
    if(block)ltask->free_content(block); 
    if(uris) hibase->free_uris(uris);
    //new task 
    //DEBUG_LOGGER(histore_logger, "Ready for newtask():%d", id);
    if((id = ltask->pop_task(ltask)) >= 0)
    {
        argx = (void *)((long) id);
        histore->newtask(histore, &histore_task_handler, argx);
        DEBUG_LOGGER(histore_logger, "newtask():%d", id);
        //fprintf(stdout, "%s::%d id:%d\n", __FILE__, __LINE__, id);
    }
    else
    {
        histore_task_running--;
    }
    return ;
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

/* hearbeat handler */
void histore_heartbeat_handler(void *arg)
{
    int id = -1;
    void *argx = NULL;

    if(arg && arg == histore && histore_ntask > 0)
    {
        while(histore_task_running < histore_ntask)
        {
            //DEBUG_LOGGER(histore_logger, "Ready for newtask():%d", id);
            if((id = ltask->pop_task(ltask)) >= 0)
            {
                //fprintf(stdout, "%s::%d new task:%d\n", __FILE__, __LINE__, id);
                argx = (void *)((long) id);
                histore->newtask(histore, &histore_task_handler, argx);
                histore_task_running++;
                DEBUG_LOGGER(histore_logger, "newtask():%d", id);
            }
            else break;
        }
    }
    return ;
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
    char *s = NULL, *p = NULL, *basedir = NULL;
    //*start = NULL;
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
#ifdef _DEBUG
    sbase->set_log(sbase, iniparser_getstr(dict, "SBASE:logfile"));
#endif
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
#ifdef _DEBUG
    hitaskd->set_log(hitaskd, iniparser_getstr(dict, "HITASKD:logfile"));
#endif
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
    hitaskd->session.error_handler = &hitaskd_error_handler;
    hitaskd->session.oob_handler = &hitaskd_oob_handler;
#ifdef _DEBUG
    if((p = iniparser_getstr(dict, "HITASKD:access_log"))){LOGGER_INIT(hitaskd_logger,p);}
#endif
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
    /* page number */
    http_page_num = iniparser_getint(dict, "HITASKD:http_page_num", 100);
    /* httpd_home */
    httpd_home = iniparser_getstr(dict, "HITASKD:httpd_home");
    /* link  task table */
    if((ltask = ltask_init()))
    {
        basedir = iniparser_getstr(dict, "HITASKD:basedir");
        ltask->set_basedir(ltask, basedir);
        //start = iniparser_getstr(dict, "HITASKD:start");
        //ltask->add_url(ltask, -1, 0, start);
        /*
           ltable->set_logger(ltable, NULL, logger);
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
    histore_ntask = histore->ndaemons = iniparser_getint(dict, "HISTORE:ndaemons", 8);
#ifdef _DEBUG
    histore->set_log(histore, iniparser_getstr(dict, "HISTORE:logfile"));
#endif
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
    histore->session.error_handler = &histore_error_handler;
    histore->session.oob_handler = &histore_oob_handler;
    interval = iniparser_getint(dict, "HISTORE:heartbeat_interval", SB_HEARTBEAT_INTERVAL);
    histore->set_heartbeat(histore, interval, &histore_heartbeat_handler, histore);
#ifdef _DEBUG
    if((p = iniparser_getstr(dict, "HISTORE:access_log"))){LOGGER_INIT(histore_logger, p);}
#endif
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
#ifdef _DEBUG
    adns->set_log(adns, iniparser_getstr(dict, "ADNS:logfile"));
#endif
    adns->session.packet_type = iniparser_getint(dict, "ADNS:packet_type", PACKET_CUSTOMIZED);
    adns->session.buffer_size = iniparser_getint(dict, "ADNS:buffer_size", SB_BUF_SIZE);
    adns->session.packet_reader = &adns_packet_reader;
    adns->session.packet_handler = &adns_packet_handler;
    adns->session.timeout_handler = &adns_timeout_handler;
    adns->session.error_handler = &adns_error_handler;
    adns->session.transaction_handler = &adns_trans_handler;
#ifdef _DEBUG
    if((p = iniparser_getstr(dict, "ADNS:access_log"))){LOGGER_INIT(adns_logger, p);}
#endif
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
    return (sbase->add_service(sbase, hitaskd) 
             | sbase->add_service(sbase, histore) 
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
    //sbase->running(sbase, 1000000); sbase->stop(sbase);
    //fprintf(stdout, "%d::OK\n", __LINE__);
    //sbase->stop(sbase);
    sbase->clean(&sbase);
    if(dict)iniparser_free(dict);
    if(hitaskd_logger){LOGGER_CLEAN(hitaskd_logger);}
    if(histore_logger){LOGGER_CLEAN(histore_logger);}
    if(adns_logger){LOGGER_CLEAN(adns_logger);}
    if(argvmap){TRIETAB_CLEAN(argvmap);}
    if(hibase) hibase->clean(&hibase);
    if(ltask) ltask->clean(&ltask);
    return 0;
}
