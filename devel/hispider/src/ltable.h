#ifndef _LTABLE_H
#define _LTABLE_H
#define DOC_KEY_LEN 16
#define LTABLE_PATH_MAX     256
#define HTTP_BUF_SIZE  65536
#ifndef HTTP_HOST_MAX
#define HTTP_HOST_MAX  64
#endif
#ifndef HTTP_PATH_MAX
#define HTTP_PATH_MAX 960
#endif
#ifndef HTTP_URL_MAX
#define HTTP_URL_MAX  1024
#endif
#define TASK_STATE_INIT         0x00
#define TASK_STATE_OK           0x02
#define TASK_STATE_ERROR        0x04
#define HTTP_DOWNLOAD_TIMEOUT   10000000
#define HTML_MAX_SIZE           2097152
#define TASK_WAIT_TIMEOUT       10000000
#define TASK_WAIT_MAX           30000000
#define DNS_TIMEOUT_MAX         4
#define DNS_PATH_MAX            256
#define DNS_BUF_SIZE            65536
#define DNS_TASK_MAX            32
#define DNS_IP_MAX              16
#define DNS_NAME_MAX            128
#define DNS_SELF_IP             0x0100007f
#define DNS_HOST_NAME           "hispider.host"
#define DNS_IP_NAME             "hispider.dns"
#define LTABLE_META_NAME        "hispider.meta"
#define LTABLE_URL_NAME         "hispider.url"
#define LTABLE_DOC_NAME         "hispider.doc"
#define LTABLE_STATE_NAME       "hispider.state"
#define USER_AGENT "Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; zh-CN; rv:1.9.0.1) Gecko/2008070206 Firefox/3.0.1"
#define ACCEPT_TYPE          "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"
#define ACCEPT_LANGUAGE     "zh-cn,zh;q=0.5"
#define ACCEPT_ENCODING     "deflate,gzip"
#define ACCEPT_CHARSET      "gb2312,utf-8;q=0.7,*;q=0.7"
#define HTTP_RESP_OK        "HTTP/1.0 200 OK"
#define HTTP_BAD_REQUEST    "HTTP/1.0 404 Not Found\r\n\r\n"
typedef long long LLong;
typedef struct _LMETA
{
    int state;
    int date;
    off_t offset;
    int length;
    int nzdata;
    int ndata;
    off_t offurl;
    int nurl;
    unsigned char id[DOC_KEY_LEN];
}LMETA;
typedef struct _LHEADER
{
    int date;
    int nurl;
    int nzdata;
    int ndata;
}LHEADER;
typedef struct _LSTATE
{
    int taskid;
    int lastid;
    int running;
}LSTATE;
typedef struct _DNS
{
    int ip;
    int offset;
    int length;
    int ntimeout;
}DNS;
typedef struct _LTABLE
{
    void *mutex;
    void *urltable;
    void *logger;
    int isinsidelogger;
    int url_fd;
    int meta_fd;
    int doc_fd;
    int state_fd;
    LSTATE *state;
    long url_current;
    long url_total;
    long url_ok;
    long url_error;
    LLong doc_total_size;
    LLong doc_total_zsize;
    LLong doc_current_size;
    LLong doc_current_zsize;
    void *timer;
    LLong time_usec;
    /* URL */
    void *whitelist;
    int nwhitelist;
    /* DNS */
    //char last_host[DNS_NAME_MAX];
    //int is_wait_last_host;
    int host_fd;
    int dns_fd;
    void *dnstable;
    int dns_current;
    int dns_ok;
    int dns_total;

    int     (*set_basedir)(struct _LTABLE *, char *basedir);
    int     (*set_logger)(struct _LTABLE *, char *logfile, void *logger);
    int     (*resume)(struct _LTABLE *);
    int     (*parselink)(struct _LTABLE *, char *host, char *path, 
            char *content, char *end);
    int     (*addlink)(struct _LTABLE *, unsigned char *host, 
            unsigned char *path, unsigned char *href, unsigned char *ehref);
    int     (*addurl)(struct _LTABLE *, char *host, char *path);
    int     (*get_task)(struct _LTABLE *, char *block, int *nblock);
    int     (*set_state)(struct _LTABLE *, int state);
    int     (*set_task_state)(struct _LTABLE *, int taskid, int state);
    int     (*get_stateinfo)(struct _LTABLE *, char *block);
    int     (*add_document)(struct _LTABLE *, int taskid, int date, char *content, int ncontent);
    int     (*add_to_whitelist)(struct _LTABLE *, char *host);
    /* dns resolve */
    int     (*add_host)(struct _LTABLE *, char *host);
    int     (*new_dnstask)(struct _LTABLE *, char *host);
    int     (*set_dns)(struct _LTABLE *, char *host, int ip);
    int     (*resolve)(struct _LTABLE *, char *host);
    void    (*clean)(struct _LTABLE **);
}LTABLE;
/* initialize LTABLE */
LTABLE *ltable_init();
#endif
