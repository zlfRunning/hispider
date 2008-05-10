#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _LINK_H
#define _LINK_H
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
#ifndef LFILE_PATH_MAX
#define LFILE_PATH_MAX 256
#endif
#ifndef LBUF_SIZE
//500k
#define LBUF_SIZE               262144
#endif  
#define URL_STATUS_INIT         0x00
#define URL_STATUS_WAIT         0x02
#define URL_STATUS_WORKING      0x04
#define URL_STATUS_ERROR        0x08
#define URL_STATUS_OVER         0x10
#define LINK_QUEUE_MIN          2
#define HTTP_IP_MAX             16
#define URL_NTASK_DEFAULT       128
#define LINK_NQUEUE_DEFAULT     128
#define LINK_STATUS_INIT        0x00
#define LINK_STATUS_WAIT        0x02
#define LINK_STATUS_WORKING     0x04
#define LINK_STATUS_TIMEOUT     0x08
#define LINK_STATUS_DISCARD     0X10
#define LINK_STATUS_ERROR       0x20
#define LINK_STATUS_OVER        0x30
#define LINK_STATUS_REQUEST     0x40
#define LINK_STATUS_COMPLETE    0x50
#ifndef MD5_LEN
#define MD5_LEN 16
#endif
typedef struct _HTTP_REQUEST
{
    int status;
    int id;
    char ip[HTTP_IP_MAX];
    int port;
    char host[HTTP_HOST_MAX];
    char path[HTTP_PATH_MAX];
    unsigned char md5[MD5_LEN];
}HTTP_REQUEST;
typedef struct _DOCMETA
{
    int status;
    int id;
    int size;
    int zsize;
    int hostoff;
    int pathoff;
    int htmloff;
    long long offset;
}DOCMETA;
typedef struct _LINKTABLE
{
    int dnscount;
    char **dnslist;
    void *dnstable;
    void *md5table;
    void *lnkio;
    void *urlio;
    void *metaio;
    void *docio;
    long long size;
    long long zsize;
    long urlno;
    long url_total;
    long urlok_total;
    DOCMETA *tasks;
    DOCMETA task;
    int  iszlib;
    int  ntask;
    long  docno;
    long  doc_total;
    long  docok_total;
    void *logger;
    void *mutex;
    int  isinsidelogger;

    int     (*set_logger)(struct _LINKTABLE *, char *logfile, void *logger);
    int     (*set_lnkfile)(struct _LINKTABLE *, char *lnkfile);
    int     (*set_urlfile)(struct _LINKTABLE *, char *urlfile);
    int     (*set_metafile)(struct _LINKTABLE *, char *metafile);
    int     (*set_docfile)(struct _LINKTABLE *, char *docfile);
    int     (*parse)(struct _LINKTABLE *, char *host, char *path, char *content, char *end);
    int     (*add)(struct _LINKTABLE *, unsigned char *host, unsigned char *path, 
            unsigned char *href, unsigned char *ehref); 
    int     (*addurl)(struct _LINKTABLE *, char *host, char *path); 
    long    (*get_task)(struct _LINKTABLE *);
    long    (*get_task_one)(struct _LINKTABLE *);
    void    (*taskhandler)(struct _LINKTABLE *, long id); 
    int     (*set_ntask)(struct _LINKTABLE *, int ntask);
    char*   (*iptab)(struct _LINKTABLE *, char *host, char *ip);
    int     (*get_request)(struct _LINKTABLE *, HTTP_REQUEST *req);
    int     (*update_request)(struct _LINKTABLE *, int sid, int status);
    int     (*add_zcontent)(struct _LINKTABLE *, DOCMETA *, char *, int);
    int     (*add_content)(struct _LINKTABLE *, void *response, char *host, char *path,
                char *content, int ncontent);
    int     (*resume)(struct _LINKTABLE *);
    void    (*clean)(struct _LINKTABLE **);

}LINKTABLE;
/* Initialize LINKTABLE */
LINKTABLE *linktable_init();
#endif
