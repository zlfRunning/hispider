#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <locale.h>
#include <sbase.h>
#include <iconv.h>
#include <chardet.h>
#include "http.h"
#include "ltask.h"
#include "iniparser.h"
#include "queue.h"
#include "zstream.h"
#include "logger.h"
#include "doctype.h"
#include "url.h"
#define CHARSET_MAX 256
#ifndef UI
#define UI(_xp_) ((unsigned int)_xp_)
#endif
#ifndef LI
#define LI(_x_) ((long int)(_x_))
#endif
#ifndef LLI
#define LLI(_x_) ((long long int)(_x_))
#endif
typedef struct _TASK
{
    CONN *d_conn;
    CONN *s_conn;
    CONN *c_conn;
    int  urlid;
    int  userid;
    int  uuid;
    int  state;
    int  is_redirect;
    char request[HTTP_BUF_SIZE];
    char location[HTTP_URL_MAX];
    char url[HTTP_URL_MAX];
    int  nrequest;
    int  is_new_host;
    char host[DNS_NAME_MAX];
    char ip[DNS_IP_MAX];
}TASK;
static DOCTYPE_MAP doctype_map = {0};
static int http_download_limit = 67108864;
static SBASE *sbase = NULL;
static SERVICE *service = NULL;
static dictionary *dict = NULL;
static TASK *tasklist = NULL;
static int ntask = 0;
static int task_type = 0;
static char *hitaskd_ip = NULL;
static int  hitaskd_port = 0;
static int  hitaskd_use_SSL = 0;
static char *histore_ip = NULL;
static int histore_port = 0;
static int histore_use_SSL = 0;
static void *taskqueue = NULL;
static void *logger = NULL;
static long long int doc_total = 0ll;
static long long int gzdoc_total = 0ll;
static long long int zdoc_total = 0ll;
static SESSION session = {0};
/* data handler */
int hitask_packet_reader(CONN *conn, CB_DATA *buffer);
int hitask_packet_handler(CONN *conn, CB_DATA *packet);
int hitask_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk);
int hitask_error_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk);
int hitask_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk);
int hitask_trans_handler(CONN *conn, int tid);
int hitask_oob_handler(CONN *conn, CB_DATA *oob);
int http_request(int c_id, HTTP_RESPONSE *http_resp, char *url);

/* http download error */
int http_download_error(int c_id, int err)
{
    char *p = NULL, buf[HTTP_BUF_SIZE];
    int n = 0;

    if(c_id >= 0 && c_id < ntask && tasklist[c_id].s_conn)
    {
        memset(buf, 0, HTTP_BUF_SIZE);
        p = buf;
        p += sprintf(p, "TASK %d HTTP/1.0\r\n", tasklist[c_id].urlid);
        if(tasklist[c_id].is_new_host)
        {
            p += sprintf(p, "Host: %s\r\n Server:%s\r\n\r\n", 
                    tasklist[c_id].host, tasklist[c_id].ip);
        }
        tasklist[c_id].is_new_host = 0;
        /*
        if(tasklist[c_id].is_redirect)
        {
            p += sprintf(p, "Location: %s\r\n", tasklist[c_id].location);
            memset(tasklist[c_id].location, 0, HTTP_URL_MAX);
        }*/
        tasklist[c_id].is_redirect = 0;
        if(tasklist[c_id].userid >= 0)
            p += sprintf(p, "UserID:%d\r\n", tasklist[c_id].userid);
        if(tasklist[c_id].uuid >= 0)
            p += sprintf(p, "UUID:%d\r\n", tasklist[c_id].uuid);
        if(err > 0) p += sprintf(p, "Warning: %d\r\n", err);
        p += sprintf(p, "Task-Type:%d\r\n", task_type);
        p += sprintf(p, "%s", "\r\n");
        n = p - buf;
        DEBUG_LOGGER(logger, "Over task:%d error:%d", tasklist[c_id].urlid, err);
        tasklist[c_id].c_conn = NULL;
        if(tasklist[c_id].s_conn)
            return tasklist[c_id].s_conn->push_chunk(tasklist[c_id].s_conn, buf, n);
    }
    return -1;
}

int hitask_packet_reader(CONN *conn, CB_DATA *buffer)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}


/* download */
int hitask_packet_handler(CONN *conn, CB_DATA *packet)
{
    int n = 0, c_id = 0, doctype = -1, *px = NULL;
    HTTP_RESPONSE http_resp = {0}, resp = {0}, *presp = NULL;
    char *p = NULL, *end = NULL;

    if(conn && tasklist && (c_id = conn->c_id) >= 0 && c_id < ntask)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        //*end = '\0';
        /* http handler */        
        if(conn == tasklist[c_id].c_conn)
        {
            if(p == NULL || http_response_parse(p, end, &http_resp) == -1)
            {
                conn->over_cstate(conn);
                conn->over(conn);
                ERROR_LOGGER(logger, "Invalid http response header on TASK[%d] via urlid:%d", 
                        c_id, tasklist[c_id].urlid);
                return http_download_error(c_id, ERR_PROGRAM);
            }
            if(http_resp.respid >= 0 && http_resp.respid < HTTP_RESPONSE_NUM)
                p = response_status[http_resp.respid].e;
            DEBUG_LOGGER(logger, "Ready for handling http response:[%d:%s] on TASK[%d] "
                    "remote[%s:%d] local[%s:%d] via %d", http_resp.respid, p, c_id,
                    conn->remote_ip, conn->remote_port, conn->local_ip, 
                    conn->local_port, conn->fd);
            //location
            if(http_resp.respid == RESP_MOVEDPERMANENTLY
                && (n = http_resp.headers[HEAD_RESP_LOCATION]) > 0
                && (p = (http_resp.hlines + n))
                && (tasklist[c_id].s_conn) && tasklist[c_id].s_conn->cache
                && (presp = (HTTP_RESPONSE *)(PCB(tasklist[c_id].s_conn->cache)->data))
                && memcpy(&resp, presp, sizeof(HTTP_RESPONSE)))
            {
                //return http_download_error(c_id, ERR_PROGRAM);
                return http_request(c_id, &resp, p);
            }
            //check content-type
            if((n = http_resp.headers[HEAD_ENT_CONTENT_TYPE]) > 0 && (p = http_resp.hlines + n))
                doctype = doctype_id(&doctype_map, p, strlen(p));
            if(http_resp.respid != RESP_OK
                || (doctype_map.num > 0 && doctype == -1))
            {
                conn->over_cstate(conn);
                conn->close(conn);
                tasklist[c_id].c_conn = NULL;
                ERROR_LOGGER(logger, "Invalid response number[%d] on TASK[%d].url:%s urlid:%d", 
                        http_resp.respid, c_id, tasklist[c_id].url, tasklist[c_id].urlid);
                return http_download_error(c_id, ERR_CONTENT_TYPE);
            }
            else
            {
                conn->save_cache(conn, &http_resp, sizeof(HTTP_RESPONSE));
                if((n = http_resp.headers[HEAD_ENT_CONTENT_LENGTH]) > 0 
                        && (n = atol(http_resp.hlines + n)) > 0 
                        && n < http_download_limit)
                {
                    conn->recv_chunk(conn, n);
                }
                else
                {
                    conn->set_timeout(conn, HTTP_DOWNLOAD_TIMEOUT);
                    conn->recv_chunk(conn, HTML_MAX_SIZE);
                }
            }
        }
        /* task handler */
        else if(conn == tasklist[c_id].s_conn)
        {
            if(p == NULL || http_response_parse(p, end, &http_resp) == -1)
            {
                conn->over_cstate(conn);
                conn->over(conn);
                px = &c_id;
                QUEUE_PUSH(taskqueue, int,  px);
                tasklist[c_id].s_conn = NULL;
                ERROR_LOGGER(logger, "Invalid http response from taskd on TASK[%d]", c_id);
                return -1;
            }
            if(http_resp.respid == RESP_OK) 
            {
                conn->save_cache(conn, &http_resp, sizeof(HTTP_RESPONSE));
                memset(tasklist[c_id].url, 0, HTTP_URL_MAX);
                memset(tasklist[c_id].location, 0, HTTP_URL_MAX);
                memset(tasklist[c_id].host, 0, DNS_NAME_MAX);
                memset(tasklist[c_id].ip, 0, DNS_IP_MAX);
                memset(tasklist[c_id].request, 0, HTTP_BUF_SIZE);
                return http_request(c_id, &http_resp, NULL);
            }
        }
        else
        {
            FATAL_LOGGER(logger, "Invalid TASK[%d] connection[%p][%s:%d] local[%s:%d] via %d,"
                    "c_conn:[%p] host:%s ip:%s", c_id, conn, conn->remote_ip, conn->remote_port, 
                    conn->local_ip, conn->local_port, conn->fd, tasklist[c_id].c_conn, 
                    tasklist[c_id].host, tasklist[c_id].ip);
        }
        return -1;
    }
    return -1;
}

/* error handler */
int hitask_error_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    int c_id = 0, *px = NULL;

    if(conn && (c_id = conn->c_id) >= 0 && c_id < ntask)
    {
        if(conn == tasklist[c_id].c_conn && packet && cache && chunk) 
        {
            if(packet->ndata > 0 && cache->ndata > 0 && chunk->ndata > 0)
            {
                return hitask_data_handler(conn, packet, cache, chunk);
            }
            else
            {
                ERROR_LOGGER(logger, "error_handler(%p) on remote[%s:%d] local[%s:%d] via %d "
                    "packet_len:%d cache_len:%d chunk_len:%d", conn, conn->remote_ip, 
                    conn->remote_port, conn->local_ip, conn->local_port, conn->fd,
                    packet->ndata, cache->ndata, chunk->ndata);
                tasklist[c_id].state = TASK_STATE_ERROR;
                tasklist[c_id].c_conn = NULL;
                conn->over_cstate(conn);
                conn->close(conn);
                return http_download_error(c_id, ERR_TASK_CONN);
            }
        }
        else if(conn == tasklist[c_id].s_conn)
        {
            ERROR_LOGGER(logger, "error_handler(%p) on remote[%s:%d] local[%s:%d] ", conn, 
                    conn->remote_ip, conn->remote_port, conn->local_ip, conn->local_port);
            px = &c_id;
            QUEUE_PUSH(taskqueue, int, px);
            tasklist[c_id].s_conn = NULL;
        }
        else if(conn == tasklist[c_id].d_conn)
        {
            ERROR_LOGGER(logger, "error_handler(%p) on remote[%s:%d] local[%s:%d] ", conn, 
                    conn->remote_ip, conn->remote_port, conn->local_ip, conn->local_port);
            px = &c_id;
            QUEUE_PUSH(taskqueue, int, px);
            tasklist[c_id].d_conn = NULL;
        }
    }
    return -1;
}

/* timeout handler */
int hitask_timeout_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    int c_id = 0, *px = NULL;

    if(conn && (c_id = conn->c_id) >= 0 && c_id < ntask)
    {
        //fprintf(stdout, "%s::%d OK chunk[%d]\n", __FILE__, __LINE__, packet->ndata);
        if(conn == tasklist[c_id].c_conn && packet && cache && chunk) 
        {
            ERROR_LOGGER(logger, "TIMEOUT on %s:%d via %d, chunk->size:%d via urlid:%d", 
                        conn->remote_ip, conn->remote_port, conn->fd, 
                        chunk->ndata, tasklist[c_id].urlid);
            if(packet->ndata > 0 && cache->ndata > 0 && chunk->ndata > 0)
            {
                return hitask_data_handler(conn, packet, cache, chunk);
            }
            else
            {
                tasklist[c_id].state = TASK_STATE_ERROR;
                tasklist[c_id].c_conn = NULL;
                conn->over_cstate(conn);
                conn->over(conn);
                return http_download_error(c_id, ERR_TASK_TIMEOUT);
            }
        }
        else if(conn == tasklist[c_id].c_conn)
        {
            px = &c_id;
            QUEUE_PUSH(taskqueue, int, px);
            tasklist[c_id].s_conn = NULL;
        }
        else if(conn == tasklist[c_id].d_conn)
        {
            px = &c_id;
            QUEUE_PUSH(taskqueue, int, px);
            tasklist[c_id].d_conn = NULL;
        }
    }
    return -1;
}

/* transaction handler */
int hitask_trans_handler(CONN *conn, int tid)
{
    char buf[HTTP_BUF_SIZE];
    int n = 0;

    if(conn && tid >= 0 && tid < ntask)
    {
        if(conn == tasklist[tid].c_conn)
        {
            //fprintf(stdout, "%s::%d OK nrequest:%d\n", __FILE__, __LINE__, tasklist[tid].nrequest);
            conn->set_timeout(conn, HTTP_DOWNLOAD_TIMEOUT);
            return conn->push_chunk(conn, tasklist[tid].request, tasklist[tid].nrequest);      
        }
        else if(conn == tasklist[tid].s_conn)
        {
            n = sprintf(buf, "TASK %d HTTP/1.0\r\n\r\n", -1);
            DEBUG_LOGGER(logger, "TASK[%d] transaction[%s:%d] via %d", 
                    tid, conn->local_ip, conn->local_port, conn->fd);
            //fprintf(stdout, "%s::%d OK %s\n", __FILE__, __LINE__, buf);
            return conn->push_chunk(conn, buf, n);
        }
        return 0;
    }
    return -1;
}

/* data handler */
int hitask_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{
    char buf[HTTP_BUF_SIZE], charset[CHARSET_MAX], *zdata = NULL, *p = NULL, 
         *ps = NULL, *outbuf = NULL, *data = NULL, *rawdata = NULL;
    int  ret = -1, c_id = 0, n = 0, i = 0, is_need_convert = 0, urlid = 0, 
         is_need_compress = 0, is_new_zdata = 0, is_text = 0;
    size_t ninbuf = 0, noutbuf = 0, nzdata = 0, ndata = 0, nrawdata = 0;
    CONN *s_conn = NULL, *d_conn = NULL;
    HTTP_RESPONSE *http_resp = NULL;
    chardet_t pdet = NULL;
    iconv_t cd = NULL;

    if(conn && (c_id = conn->c_id) >= 0 && c_id < ntask)
    {
        if(conn == tasklist[c_id].c_conn && chunk && chunk->data && chunk->ndata > 0
                && cache && cache->ndata > 0 && (http_resp = (HTTP_RESPONSE *)cache->data))
        {
            urlid = tasklist[c_id].urlid;
            DEBUG_LOGGER(logger, "Ready for handling TASK[%d] on remote[%s:%d] "
                    "via %d ndata:%d urlid:%d", c_id, conn->remote_ip, conn->remote_port, 
                    conn->fd, chunk->ndata, tasklist[c_id].urlid);
            doc_total++;
            //check content is text 
            if((n = http_resp->headers[HEAD_ENT_CONTENT_TYPE]) > 0
                    && (p = (http_resp->hlines + n))  && strncasecmp(p, "text", 4) == 0)
                is_text = 1;
            //check content encoding 
            if((n = http_resp->headers[HEAD_ENT_CONTENT_ENCODING]) > 0 
                    && (p = (http_resp->hlines + n)) )
            {
                zdata = chunk->data;
                nzdata = chunk->ndata;
                if(strncasecmp(p, "gzip", 4) == 0) 
                {
                    ndata =  nzdata * 8 + Z_HEADER_SIZE;
                    if((data = (char *)calloc(1, ndata)))
                    {
                        if((n = httpgzdecompress((Bytef *)zdata, 
                            nzdata, (Bytef *)data, (uLong *)((void *)&ndata))) == 0)
                        {
                            gzdoc_total++;
                            DEBUG_LOGGER(logger, "gzdecompress data from %ld to %ld "
                                    " rate:(%lld/%lld) = %f", LI(nzdata), LI(ndata), 
                                    gzdoc_total, doc_total, ((double)gzdoc_total/(double)doc_total));
                            rawdata = data;
                            nrawdata = ndata;
                            is_need_compress = 1;
                        }
                        else 
                        {
                            free(data);
                            data = NULL;
                            goto err_end;
                        }
                    }
                    else 
                    {
                        ERROR_LOGGER(logger, "httpgzdecompress data from %ld to %ld failed, %d:%s", 
                                LI(nzdata), LI(ndata), n, strerror(errno));
                        goto err_end;
                    }
                }
                else if(strncasecmp(p, "deflate", 7) == 0)
                {
                    ndata =  nzdata * 8 + Z_HEADER_SIZE;
                    if((data = (char *)calloc(1, ndata)))
                    {

                        if(zdecompress((Bytef *)zdata, nzdata, (Bytef *)data, 
                                    (uLong *)((void *)&ndata)) == 0)
                        {
                            zdoc_total++;
                            DEBUG_LOGGER(logger, "zdecompress data from %ld to %ld "
                                    "rate:(%lld/%lld) = %f", LI(nzdata), LI(ndata), gzdoc_total, 
                                    doc_total, ((double)zdoc_total/(double)doc_total));
                            rawdata = data;
                            nrawdata = ndata;
                        }
                        else 
                        {
                            free(data);
                            data = NULL;
                            goto err_end;
                        }
                    }
                    else 
                    {
                        ERROR_LOGGER(logger, "gzdecompress data from %ld to %ld "
                                "via urlid:%d failed, %d:%s", LI(nzdata), LI(ndata), 
                                tasklist[c_id].urlid, n, strerror(errno));
                        goto err_end;
                    }
                }
                else
                {
                    //DEBUG_LOGGER(logger, "unspported encoding:%s", p);
                    goto err_end;
                }
            }
            else
            {
                rawdata = chunk->data;
                nrawdata = chunk->ndata;
                if(is_text) is_need_compress = 1;
            }
            //check text/plain/html/xml...  charset 
            memset(charset, 0, CHARSET_MAX);
            if(is_text)
            {
                DEBUG_LOGGER(logger, "is_need_convert:%d data:%p ndata:%ld via urlid:%d", 
                        is_need_convert, rawdata, LI(nrawdata), tasklist[c_id].urlid);
                if(rawdata && nrawdata > 0 && chardet_create(&pdet) == 0)
                {
                    if(chardet_handle_data(pdet, rawdata, nrawdata) == 0 
                            && chardet_data_end(pdet) == 0 )
                    {
                        chardet_get_charset(pdet, charset, CHARSET_MAX);
                        if(memcmp(charset, "UTF-8", 5) != 0) is_need_convert = 1;
                    }
                    chardet_destroy(pdet);
                }
                DEBUG_LOGGER(logger, "is_need_convert:%d data:%p ndata:%ld via urlid:%d", 
                        is_need_convert, rawdata, LI(nrawdata), tasklist[c_id].urlid);
                //convert charset 
                if(is_need_convert && (cd = iconv_open("UTF-8", charset)) != (iconv_t)-1)
                {
                    p = rawdata;
                    ninbuf = nrawdata;
                    n = noutbuf = ninbuf * 8;
                    if((ps = outbuf = (char *)calloc(1, noutbuf)))
                    {
                        if(iconv(cd, &p, &ninbuf, &ps, (size_t *)&n) == (size_t)-1)
                        {
                            free(outbuf);
                            outbuf = NULL;
                        }
                        else
                        {
                            noutbuf -= n;
                            DEBUG_LOGGER(logger, "convert %s[%ld] to UTF-8 len:%ld via urlid:%d", 
                                    charset, LI(nrawdata), LI(noutbuf), tasklist[c_id].urlid);
                            rawdata = outbuf;
                            nrawdata = noutbuf;
                        }
                    }
                    iconv_close(cd);
                    if(is_need_compress == 0) is_need_compress = 1;
                }
            }
            zdata = NULL;
            nzdata = 0;
            //compress with zlib::inflate()
            DEBUG_LOGGER(logger, "is_need_compess:%d data:%p ndata:%ld via urlid:%d", 
                    is_need_compress, rawdata, LI(nrawdata), tasklist[c_id].urlid);
            if(is_need_compress && rawdata && nrawdata  > 0)
            {
                nzdata = nrawdata + Z_HEADER_SIZE;
                p = rawdata;
                n = nrawdata;
                if((zdata = (char *)calloc(1, nzdata)))
                {
                    if(zcompress((Bytef *)p, n, (Bytef *)zdata, (uLong * )((void *)&nzdata)) != 0)
                    {
                        free(zdata);
                        zdata = NULL;
                        nzdata = 0;
                    }
                    else  is_new_zdata = 1;
                }
                DEBUG_LOGGER(logger, "compressed data %d to %ld via urlid:%d", 
                        n, LI(nzdata), tasklist[c_id].urlid);
            }
            //content data
            if(zdata && nzdata > 0 && http_resp)
            {
                /* task header */
                memset(buf, 0, HTTP_BUF_SIZE);
                p = buf;
                p += sprintf(p, "TASK %d HTTP/1.0\r\n", urlid);
                if((n = http_resp->headers[HEAD_ENT_LAST_MODIFIED]))
                {
                    ps = http_resp->hlines + n;
                    p += sprintf(p, "Last-Modified: %s\r\n", ps);
                }
                if(tasklist[c_id].is_new_host)
                {
                    DEBUG_LOGGER(logger, "new domain:%s ip:%s via urlid:%d", 
                            tasklist[c_id].host, tasklist[c_id].ip, urlid);
                    p += sprintf(p, "Host: %s\r\nServer: %s\r\n", 
                            tasklist[c_id].host, tasklist[c_id].ip);
                    tasklist[c_id].is_new_host = 0;
                }
                //if(tasklist[c_id].userid >= 0)
                //    p += sprintf(p, "UserID:%d\r\n", tasklist[c_id].userid);
                //if(tasklist[c_id].uuid >= 0)p += sprintf(p, "UUID:%d\r\n", tasklist[c_id].uuid);
                if(http_resp->ncookies > 0)
                {
                    ps = p;
                    p += sprintf(p, "%s", "Cookie: ");
                    i = 0;
                    do
                    {
                        p += sprintf(p, "%.*s=%.*s; ", http_resp->cookies[i].nk, 
                                http_resp->hlines + http_resp->cookies[i].k,
                                http_resp->cookies[i].nv, http_resp->hlines 
                                + http_resp->cookies[i].v);
                    }while(++i < http_resp->ncookies);
                    DEBUG_LOGGER(logger, "%s", ps); 
                    p += sprintf(p, "%s", "\r\n");
                }
                if(nrawdata > 0) p += sprintf(p, "Raw-Length: %ld\r\n", LI(nrawdata));
                p += sprintf(p, "Download-Length: %d\r\n", chunk->ndata);
                p += sprintf(p, "Task-Type: %d\r\n", task_type);
                p += sprintf(p, "%s", "\r\n");
                if((s_conn = tasklist[c_id].s_conn) && (n = (p - buf)) > 0)
                {
                    DEBUG_LOGGER(logger, "sending header on TASK[%d] size:%d "
                            " remote[%s:%d] local[%s:%d] via %d to hitaskd-urlid:%d", 
                            c_id, n, s_conn->remote_ip, s_conn->remote_port,
                            s_conn->local_ip, s_conn->local_port, s_conn->fd, urlid);
                    s_conn->push_chunk(s_conn, buf, n);
                    tasklist[c_id].c_conn = NULL;
                }
                //store data 
                memset(buf, 0, HTTP_BUF_SIZE);
                p = buf;
                p += sprintf(p, "PUT %d HTTP/1.0\r\n", urlid);
                if((n = http_resp->headers[HEAD_ENT_LAST_MODIFIED]))
                {
                    ps = http_resp->hlines + n;
                    p += sprintf(p, "Last-Modified: %s\r\n", ps);
                }
                if(nrawdata > 0) p += sprintf(p, "Raw-Length: %ld\r\n", LI(nrawdata));
                p += sprintf(p, "Download-Length: %d\r\n", chunk->ndata);
                if((n = http_resp->headers[HEAD_ENT_CONTENT_TYPE]))
                {
                    p += sprintf(p, "Content-Type: %s\r\n", http_resp->hlines +n);
                }
                //if(tasklist[c_id].userid >= 0)
                //    p += sprintf(p, "UserID:%d\r\n", tasklist[c_id].userid);
                //if(tasklist[c_id].uuid >= 0)
                //    p += sprintf(p, "UUID:%d\r\n", tasklist[c_id].uuid);
                p += sprintf(p, "Content-Length: %ld\r\n", LI(nzdata));
                p += sprintf(p, "Task-Type: %d\r\n", task_type);
                p += sprintf(p, "%s", "\r\n");
                if((d_conn = tasklist[c_id].d_conn) && d_conn->push_chunk && (n = (p - buf)) > 0)
                {
                    d_conn->start_cstate(d_conn);
                    DEBUG_LOGGER(logger, "send header TASK[%d] len:%d urlid:%d", c_id, n, urlid);
                    d_conn->push_chunk(d_conn, buf, n);
                    d_conn->push_chunk(d_conn, zdata, nzdata);
                    DEBUG_LOGGER(logger, "sent storage on TASK[%d] data:%p size:%ld "
                            "to histore-urlid:%d remote[%s:%d] local[%s:%d] via %d", 
                            c_id, zdata, LI(nzdata), urlid, d_conn->remote_ip,
                            d_conn->remote_port, d_conn->local_ip, d_conn->local_port,
                            d_conn->fd);
                }
                tasklist[c_id].urlid = -1;
                ret = 0;
                goto end;
            }
            else goto err_end; 
        }
err_end:
        http_download_error(c_id, ERR_DATA);
end:    
        conn->over_cstate(conn);
        conn->close(conn);
        if(data) free(data); data = NULL;
        if(outbuf) free(outbuf); outbuf = NULL;
        if(is_new_zdata && zdata) free(zdata); zdata = NULL;
    }
    return ret;
}

int hitask_oob_handler(CONN *conn, CB_DATA *oob)
{
    if(conn)
    {
        return 0;
    }
    return -1;
}

/* http_request */
int http_request(int c_id, HTTP_RESPONSE *http_resp, char *url)
{
    char *p = NULL, *e = NULL, *s = NULL, *es = NULL, *pp = NULL, *epp = NULL,
        *last = NULL, *end = NULL , *ip = NULL, *sip = NULL, *pip = NULL, *host = NULL, 
         *path = NULL, newurl[HTTP_URL_MAX], newhost[HTTP_HOST_MAX];
    // *cookie = NULL, *refer = NULL;
    int urlid = 0, n = 0, port = 0, pport = 0, userid = -1, uuid = -1, 
        sport = 0, is_use_proxy = 0;
    CONN *conn = NULL;
    struct hostent *hp = NULL;

    if(c_id >= 0 && c_id < ntask && http_resp)
    {
        if((conn = tasklist[c_id].c_conn))
        {
            FATAL_LOGGER(logger, "REPEAT-TASK[%d] to connection[%s:%d] "
                    "local[%s:%d] via %d url:%s", c_id, conn->remote_ip, conn->remote_port,
                    conn->local_ip, conn->local_port, conn->fd, url);
            return -1;
        }
        if(url)
        {
            s = tasklist[c_id].url;
            es = s + strlen(s);
            p = url;
            e = p + strlen(p);
            pp = newurl;
            epp = newurl + HTTP_URL_MAX;
            memset(newurl, 0, HTTP_URL_MAX);
            CPURL(s, es, p, e, pp, epp, end, host, path, last);
            if(host == NULL || path == NULL) 
            {
                ERROR_LOGGER(logger, "Invalid url:%s on TASK[%d] to location:%s", 
                        tasklist[c_id].url, c_id, url);
                goto restart_task;
            }
            tasklist[c_id].is_redirect = 1;
            strcpy(tasklist[c_id].location, newurl);
            p = host;
            pp = newhost;
            epp = newhost + HTTP_HOST_MAX;
            memset(newhost, 0, HTTP_HOST_MAX);
            while(*p != '\0' && *p != '/' && *p != ':' && pp < epp) *pp++ = *p++;
            sport = port = 80;
            if(*p == ':')
            {
                ++p;
                port = sport = atoi(p);
                while(*p >= '0' && *p <= '9') ++p;
            }
            if(tasklist[c_id].c_conn) 
            {
                tasklist[c_id].c_conn->over_cstate(tasklist[c_id].c_conn);
                tasklist[c_id].c_conn->close(tasklist[c_id].c_conn);
                tasklist[c_id].c_conn = NULL;
            }
            DEBUG_LOGGER(logger, "Redirect %s on TASK[%d] to %s host[%s] newhost[%s:%d]", 
                    tasklist[c_id].url, c_id, newurl, host, newhost, port);
            host = newhost;
            if(strcasecmp(host, tasklist[c_id].host) == 0)
            {
                ip = tasklist[c_id].ip;
            }
            else ip = "255.255.255.255";
        }
        else
        {
            host = ((n = http_resp->headers[HEAD_REQ_HOST]) > 0) 
                ? (http_resp->hlines + n): NULL; 
            sip = ip = ((n = http_resp->headers[HEAD_RESP_SERVER]) > 0)
                ? (http_resp->hlines + n): NULL;
            sport = port = ((n = http_resp->headers[HEAD_REQ_TE]) > 0)
                ? atoi(http_resp->hlines + n) : 0;
            path = ((n = http_resp->headers[HEAD_RESP_LOCATION]) > 0)
                ? (http_resp->hlines + n): NULL;
            if(host && path)
            {
                if(port > 0 && port != 80)
                    sprintf(tasklist[c_id].url, "http://%s:%d%s", host, sport, path);
                else
                    sprintf(tasklist[c_id].url, "http://%s%s", host, path);
            }
            DEBUG_LOGGER(logger, "TASK[%d] host:%s ip:%s port:%d path:%s urlid:%d", 
                    c_id, host, ip, port, path, urlid);
        }
        pip = ((n = http_resp->headers[HEAD_REQ_USER_AGENT]) > 0)
            ? (http_resp->hlines + n): NULL;
        pport = ((n = http_resp->headers[HEAD_GEN_VIA]) > 0)
            ? atoi(http_resp->hlines + n) : 0;
        urlid = tasklist[c_id].urlid = ((n = http_resp->headers[HEAD_REQ_FROM]) > 0)
            ? atoi(http_resp->hlines + n) : 0;
        userid = tasklist[c_id].userid = ((n = http_resp->headers[HEAD_GEN_USERID]) > 0)
            ? atoi(http_resp->hlines + n) : -1;
        uuid = tasklist[c_id].uuid = ((n = http_resp->headers[HEAD_GEN_UUID]) > 0)
            ? atoi(http_resp->hlines + n) : -1;
        if(pip && pport > 0) 
        {
            ip = pip;
            port = pport;
            is_use_proxy = 1; 
        }
        if(host == NULL || ip == NULL || path == NULL || port == 0 || urlid < 0) 
        {
            goto restart_task;
        }
        if(is_use_proxy == 0 && strcmp(ip, "255.255.255.255") == 0)
        {
            if((hp = gethostbyname(host)))
            {
                tasklist[c_id].is_new_host = 1;
                strcpy(tasklist[c_id].host, host);
                sprintf(tasklist[c_id].ip, "%s", 
                        inet_ntoa(*((struct in_addr *)(hp->h_addr))));
                ip = tasklist[c_id].ip;
            }
            else
            {
                DEBUG_LOGGER(logger, "Resolving name[%s] failed, %s", host, strerror(h_errno));
                goto restart_task;
            }
        }
        if((conn = tasklist[c_id].c_conn = service->newconn(service, -1, -1, ip, port, NULL)))
        {
            p = tasklist[c_id].request;
            //GET/POST path
            if(is_use_proxy || sport != 80)
            {
                if(*path == '/')
                    p += sprintf(p, "GET http://%s:%d%s HTTP/1.0\r\n", host, sport, path);
                else
                    p += sprintf(p, "GET http://%s:%d/%s HTTP/1.0\r\n", host, sport, path);
            }
            else 
            {
                if(*path == '/')
                    p += sprintf(p, "GET %s HTTP/1.0\r\n", path);
                else
                    p += sprintf(p, "GET /%s HTTP/1.0\r\n", path);
            }
            //general
            p += sprintf(p, "Host: %s\r\nUser-Agent: %s\r\nAccept: %s\r\n"
                    "Accept-Language: %s\r\nAccept-Encoding: %s\r\n"
                    "Accept-Charset: %s\r\nConnection: close\r\n", host, 
                    USER_AGENT, ACCEPT_TYPE, ACCEPT_LANGUAGE, 
                    ACCEPT_ENCODING, ACCEPT_CHARSET);
            if((n = http_resp->headers[HEAD_REQ_COOKIE]) > 0)
                p += sprintf(p, "Cookie: %s\r\n", http_resp->hlines + n);
            if((n = http_resp->headers[HEAD_REQ_REFERER]) > 0)
                p += sprintf(p, "Referer: %s\r\n", http_resp->hlines + n);
            if((n = http_resp->headers[HEAD_ENT_LAST_MODIFIED]) > 0)
                p += sprintf(p, "If-Modified-Since: %s\r\n", http_resp->hlines + n);
            //end
            p += sprintf(p, "%s", "\r\n");
            tasklist[c_id].nrequest = p - tasklist[c_id].request;
            tasklist[c_id].userid = userid;
            tasklist[c_id].uuid = uuid;
            tasklist[c_id].c_conn->c_id = c_id;
            tasklist[c_id].c_conn->start_cstate(tasklist[c_id].c_conn);
            DEBUG_LOGGER(logger, "starting TASK[%d] connection[%p] to remote[%s:%d] "
                    " via local[%s:%d] fd[%d]", c_id, conn, conn->remote_ip, 
                    conn->remote_port, conn->local_ip, conn->local_port, conn->fd);
            return service->newtransaction(service, tasklist[c_id].c_conn, c_id);
        }
        else
        {
            ERROR_LOGGER(logger, "Connect TASK[%d] to [%s][%s:%d] failed, %s\n", 
                    c_id, host, ip, port, strerror(errno));
        }
restart_task:
        return http_download_error(c_id, ERR_HOST_IP);
    }
    return -1;
}

/* heartbeat */
void cb_heartbeat_handler(void *arg)
{
    int id = 0, left = 0, *px = NULL;
    SESSION sess = {0}, *psess = NULL;

    if(arg == (void *)service)
    {
        memcpy(&sess, &session, sizeof(SESSION));
        psess = &sess;
        while(QTOTAL(taskqueue) > 0)
        {
            id = -1;
            QUEUE_POP(taskqueue, int, &id);
            left = 0;
            if(id >= 0 && id < ntask)
            {
                if(tasklist[id].s_conn == NULL)
                {
                    if(hitaskd_use_SSL) sess.is_use_SSL = 1;
                    else sess.is_use_SSL = 0;
                    if((tasklist[id].s_conn = service->newconn(service, -1, -1, 
                                    hitaskd_ip, hitaskd_port, psess)))
                    {
                        tasklist[id].s_conn->c_id = id;
                        tasklist[id].s_conn->start_cstate(tasklist[id].s_conn);
                        if(tasklist[id].c_conn == NULL)
                            service->newtransaction(service, tasklist[id].s_conn, id);
                    }
                    else
                    {
                        ERROR_LOGGER(logger, "Connect %d to %s:%d failed, %s", 
                                id, hitaskd_ip, hitaskd_port, strerror(errno));
                        left = id;
                        goto err;
                    }
                }
                if(tasklist[id].d_conn == NULL)
                {
                    if(histore_use_SSL) sess.is_use_SSL = 1;
                    else sess.is_use_SSL = 0;
                    if((tasklist[id].d_conn = service->newconn(service, -1, -1, 
                                    histore_ip, histore_port, psess)))
                    {   
                        tasklist[id].d_conn->c_id = id;
                        tasklist[id].d_conn->start_cstate(tasklist[id].d_conn);
                        service->newtransaction(service, tasklist[id].d_conn, id);
                    }
                    else
                    {
                        ERROR_LOGGER(logger, "Connect %d to %s:%d failed, %s", 
                                id, histore_ip, histore_port, strerror(errno));
                        left = id;
                        goto err;
                    }
                }
            }
        }
        return ;
err:
        if(left >= 0 && left < ntask)
        {
            px = &left;
            QUEUE_PUSH(taskqueue, int, px);
        }
    }
    return ;
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
    char *s = NULL, *p = NULL, *end = NULL;
    int i = 0, interval = 0, *px = NULL;
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
    /* initialize service */
    if((service = service_init()) == NULL)
    {
        fprintf(stderr, "Initialize service failed, %s", strerror(errno));
        _exit(-1);
    }
    service->family = iniparser_getint(dict, "HITASK:inet_family", AF_INET);
    service->sock_type = iniparser_getint(dict, "HITASK:socket_type", SOCK_STREAM);
    hitaskd_ip = iniparser_getstr(dict, "HITASK:hitaskd_ip");
    hitaskd_port = iniparser_getint(dict, "HITASK:hitaskd_port", 2816);
    hitaskd_use_SSL = iniparser_getint(dict, "HITASK:hitaskd_use_SSL", 0);
    histore_ip = iniparser_getstr(dict, "HITASK:histore_ip");
    histore_port = iniparser_getint(dict, "HITASK:histore_port", 3927);
    histore_use_SSL = iniparser_getint(dict, "HITASK:histore_use_SSL", 0);
    service->working_mode = iniparser_getint(dict, "HITASK:working_mode", WORKING_PROC);
    service->service_type = iniparser_getint(dict, "HITASK:service_type", C_SERVICE);
    service->service_name = iniparser_getstr(dict, "HITASK:service_name");
    service->nprocthreads = iniparser_getint(dict, "HITASK:nprocthreads", 1);
    service->ndaemons = iniparser_getint(dict, "HITASK:ndaemons", 0);
#ifdef _DEBUG
    service->set_log(service, iniparser_getstr(dict, "HITASK:logfile"));
#endif
    service->session.packet_type = iniparser_getint(dict, "HITASK:packet_type",PACKET_DELIMITER);
    service->session.packet_delimiter = iniparser_getstr(dict, "HITASK:packet_delimiter");
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
    service->session.buffer_size = iniparser_getint(dict, "HITASK:buffer_size", SB_BUF_SIZE);
    service->session.packet_reader = &hitask_packet_reader;
    service->session.packet_handler = &hitask_packet_handler;
    service->session.data_handler = &hitask_data_handler;
    service->session.error_handler = &hitask_error_handler;
    service->session.timeout_handler = &hitask_timeout_handler;
    service->session.transaction_handler = &hitask_trans_handler;
    service->session.oob_handler = &hitask_oob_handler;
    memcpy(&session, &(service->session), sizeof(SESSION));
    interval = iniparser_getint(dict, "HITASK:heartbeat_interval", SB_HEARTBEAT_INTERVAL);
    service->set_heartbeat(service, interval, &cb_heartbeat_handler, service);
    /* server */
    if((p = iniparser_getstr(dict, "HITASK:document_types")))
    {
        end = p + strlen(p);
        if(doctype_map_init(&doctype_map) == 0)
            doctype_add_line(&doctype_map, p, end);
    }
    http_download_limit = iniparser_getint(dict, "HITASK:http_download_limit", 67108864);
    ntask = iniparser_getint(dict, "HITASK:ntask", 64);
    task_type = iniparser_getint(dict, "HITASK:task_type", 0);
    if(ntask <= 0)
    {
        fprintf(stderr, "[ntask] is invalid...\n");
        _exit(-1);
    }
    tasklist = (TASK *)calloc(ntask, sizeof(TASK));
    QUEUE_INIT(taskqueue);
    for(i = 0; i < ntask; i++)
    {
        px = &i;
        QUEUE_PUSH(taskqueue, int, px);
    }
#ifdef _DEBUG
    LOGGER_INIT(logger, iniparser_getstr(dict, "HITASK:access_log"));
#endif
    fprintf(stdout, "Parsing for server...\n");
    return sbase->add_service(sbase, service);
}

static void hitask_stop(int sig){
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            fprintf(stderr, "hitask is interrupted by user.\n");
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
    signal(SIGTERM, &hitask_stop);
    signal(SIGINT,  &hitask_stop);
    signal(SIGHUP,  &hitask_stop);
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
    //sbase->stop(sbase);
    sbase->clean(&sbase);
    doctype_map_clean(&doctype_map);
    if(tasklist){free(tasklist); tasklist = NULL;}
    if(dict)iniparser_free(dict);
    if(taskqueue){QUEUE_CLEAN(taskqueue);}
    if(logger){LOGGER_CLEAN(logger);}
    return 0;
}
