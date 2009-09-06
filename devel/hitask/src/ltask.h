#ifndef _LTASK_H
#define _LTASK_H
#define HTTP_PATH_MAX           256
#define HTTP_HOST_MAX           256
#define HTTP_IP_MAX             16
#define HTTP_URL_MAX            4096
#define HTTP_URL_MMAX            2048
#define HTTP_COOKIE_MAX         4096
#define HOST_INCRE_NUM          1000000   
#define PROXY_INCRE_NUM         10000
#define DNS_INCRE_NUM           256
#define URL_INCRE_NUM           1000000   
#define IP_INCRE_NUM            1000000
#define USER_INCRE_NUM          256
#define HTTP_BUF_SIZE           65536
#define Q_TYPE_URL              0x01
#define Q_TYPE_HOST             0x02
#define ERR_PROXY               0x01
#define ERR_HTTP_RESP           0x02
#define ERR_PROGRAM             0x04
#define ERR_CONTENT_TYPE        0x08
#define ERR_HOST_IP             0x10
#define ERR_TASK_CONN           0x20
#define ERR_TASK_TIMEOUT        0x40
#define ERR_DATA                0x80
#define L_LEVEL_UP              1
#define L_LEVEL_DOWN            -1
#define PROXY_STATUS_OK         1
#define PROXY_STATUS_ERR        -1
#define HOST_STATUS_OK 	        1
#define HOST_STATUS_ERR         -1
#define URL_STATUS_OK 	        1
#define URL_STATUS_ERR          -1
#define DNS_STATUS_OK           1
#define DNS_STATUS_ERR          -1
#define DNS_STATUS_READY        2
#define USER_STATUS_OK          1
#define USER_STATUS_ERR         -1
#define USER_STATUS_READY       2
#define TASK_WAIT_TIMEOUT       10000000
#define TASK_WAIT_MAX           30000000
#define DNS_TIMEOUT_MAX         4
#define DNS_PATH_MAX            256
#define DNS_BUF_SIZE            65536
#define DNS_TASK_MAX            32
#define DNS_IP_MAX              16
#define DNS_NAME_MAX            128
#define HTTP_DOWNLOAD_TIMEOUT   20000000
#define HTML_MAX_SIZE           1048576
#define TASK_STATE_INIT         0x00
#define TASK_STATE_OK           0x02
#define TASK_STATE_ERROR        0x046
#define L_SPEED_INTERVAL        60000000
#define L_COOKIE_MAX            1024
#define L_USER_MAX              32
#define L_PASSWD_MAX            16
#define L_STATE_NAME            "hi.state"
#define L_URL_NAME              "hi.url"
#define L_PROXY_NAME            "hi.proxy"
#define L_HOST_NAME             "hi.host"
#define L_IP_NAME               "hi.ip"
#define L_DOMAIN_NAME           "hi.domain"
#define L_DOC_NAME              "hi.doc"
#define L_TASK_NAME             "hi.task"
#define L_LOG_NAME              "hi.log"
#define L_KEY_NAME              "hi.key"
#define L_META_NAME             "hi.meta"
#define L_DNS_NAME              "hi.dns"
#define L_USER_NAME             "hi.user"
#define L_ERR_NAME              "hi.err"
#define L_COOKIE_NAME           "hi.cookie"
#define USER_AGENT              "Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; zh-CN; rv:1.9.0.1) Gecko/2008070206 Firefox/3.0.1"
#define ACCEPT_TYPE             "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"
#define ACCEPT_LANGUAGE         "zh-cn,zh;q=0.5"
#define ACCEPT_ENCODING         "deflate,gzip"
#define ACCEPT_CHARSET          "gb2312,utf-8;q=0.7,*;q=0.7"
#define HTTP_RESP_OK            "HTTP/1.0 200 OK"
#define HTTP_BAD_REQUEST        "HTTP/1.0 400 Bad Request\r\n\r\n"
#define HTTP_NOT_FOUND          "HTTP/1.0 404 Not Found\r\n\r\n" 
#define HTTP_NOT_MODIFIED       "HTTP/1.0 304 Not Modified\r\n\r\n"
#define HTTP_NO_CONTENT         "HTTP/1.0 206 No Content\r\n\r\n"
//#define HTTP_BAD_REQUEST    "HTTP/1.0 404 Not Found\r\n\r\n"
/* host/domain */
typedef struct _LHOST
{
    short ip_count;
    short status;
    short level;
    short depth;
    int host_off;
    int host_len;
    int ip_off;
    int url_first_id;
    int url_current_id;
    int url_total;
    int url_left;
    int url_last_id;
    int cookie_off;
}LHOST;
/* proxy */
typedef struct _LPROXY
{
    int ip;
    unsigned short port;
    short status;
}LPROXY;
/* url/doc meta */
typedef struct _LMETA
{
    short status;
    short type;
    short level;
    short depth;
    int date;
    int parent;
    off_t url_off;
    int url_len;
    int host_id;
    off_t content_off;
    int content_len;
    int prev;
    int next;
}LMETA;
/* doc header */
typedef struct _LDOCHEADER
{
    int id;
    int parent;
    int date;
    short nurl;
    short ntype;
    int ncontent;
    int total;
}LDOCHEADER;
/* state */
typedef struct _LSTATE
{
    short running;
    short is_use_proxy;
    int   is_extract_image;
    int   url_total;
    int   url_ok;
    int   url_error;
    int   url_tasks;
    int   host_current;
    int   host_total;
    off_t doc_total_zsize;
    off_t doc_total_size;
    off_t last_doc_size;
    long long int last_usec;
    double  speed;
}LSTATE;
/* IO/MAP */
typedef struct _LIO
{
    int fd;
    void *map;
    off_t size;
    off_t end;
    int total;
    int current;
    int left;
}LIO;
/* PRIORITY QUEUE */
typedef struct _LNODE
{
    short type;
    short status;
    int id;
}LNODE;
/* DNS */
typedef struct _LDNS
{
    int  status;
    char name[HTTP_IP_MAX];
}LDNS;
/* COOKIE */
typedef struct _LCOOKIE
{
    int hostid;
    int offsets[L_COOKIE_MAX];
}LCOOKIE;
/* USER */
typedef struct _LUSER
{
    int  status;
    int  permissions;
    char name[L_USER_MAX];
    char passwd[L_PASSWD_MAX];
}LUSER;
/* TASK */
typedef struct _LTASK
{
    LIO  proxyio;
    LIO  hostio;
    LIO  ipio;
    LIO  dnsio;
    LIO  userio;
    int  key_fd;
    int  url_fd;
    int  domain_fd;
    int  meta_fd;
    int  doc_fd;
    void *urlmap;
    void *table;
    void *qproxy;
    void *qtask;
    void *users;
    LSTATE *state;
    int state_fd;
    int  cookie_fd;
    void *cookies;
    void *timer;
    void *mutex;
    void *logger;
    void *errlogger;

    int (*set_basedir)(struct _LTASK *, char *basedir);
    int (*set_state_running)(struct _LTASK *, int state);
    int (*set_state_proxy)(struct _LTASK *, int state);
    int (*add_proxy)(struct _LTASK *, char *host);
    int (*get_proxy)(struct _LTASK *, LPROXY *proxy);
    int (*set_proxy_status)(struct _LTASK *, int id, char *host, short status);
    int (*del_proxy)(struct _LTASK *, int id, char *host);
    int (*view_proxylist)(struct _LTASK *, char *block);
    int (*add_dns)(struct _LTASK *, char *dns_ip);
    int (*del_dns)(struct _LTASK *, int dnsid, char *dns_ip);
    int (*set_dns_status)(struct _LTASK *, int dnsid, char *dns_ip, int status);
    int (*pop_dns)(struct _LTASK *, char *dns_ip);
    int (*view_dnslist)(struct _LTASK *, char *block);
    int (*pop_host)(struct _LTASK *, char *host);
    int (*set_host_ip)(struct _LTASK *, char *host, int *ips, int nip);
    int (*get_host_ip)(struct _LTASK *, char *host);
    void(*list_host_ip)(struct _LTASK *, FILE *fp);
    int (*set_host_status)(struct _LTASK *, int hostid, char *host, short status);
    int (*set_host_level)(struct _LTASK *, int hostid, char *host, short level);
    int (*add_cookie)(struct _LTASK *, int hostid, char *cookies);
    int (*update_cookie)(struct _LTASK *, int hostid, char *cookies);
    int (*del_cookie)(struct _LTASK *, int hostid, char *cookies);
    int (*add_url)(struct _LTASK *, int parentid, int parent_depth, char *url);
    int (*pop_url)(struct _LTASK *, char *url, int *time, char *refer, char *cookie);
    int (*get_url)(struct _LTASK *, int urlid, char *url);
    int (*set_url_status)(struct _LTASK *, int urlid, char *url, short status, short err);
    int (*set_url_level)(struct _LTASK *, int urlid, char *url, short level);
    int (*get_task)(struct _LTASK *, char *buf, int *nbuf);
    int (*add_user)(struct _LTASK *, char *name, char *passwd);
    int (*del_user)(struct _LTASK *, int userid, char *username);
    int (*update_passwd)(struct _LTASK *, int userid, char *username, char *passwd);
    int (*update_permission)(struct _LTASK *, int userid, char *username, int permission);
    int (*authorization)(struct _LTASK *, int userid, char *username, char *passwd, LUSER *user);
    int (*set_user_status)(struct _LTASK *, int userid, char *username, int status);
    int (*list_users)(struct _LTASK *, char *block, int *nblock);
    int (*get_stateinfo)(struct _LTASK *, char *block);
    int (*update_content)(struct _LTASK *, int urlid, char *date, 
            char *type, char *content, int ncontent);
    int (*extract_link)(struct _LTASK *, int urlid, int depth, 
            char *url, char *content, int ncontent);
    void (*clean)(struct _LTASK **);
}LTASK;
/* initialize LTASK */
LTASK *ltask_init();
#endif
