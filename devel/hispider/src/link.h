#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _LINK_H
#define _LINK_H
#define HTTP_BUF_SIZE 8192
#ifndef HTTP_HOST_MAX
#define HTTP_HOST_MAX  256
#endif
#ifndef HTTP_PATH_MAX
#define HTTP_PATH_MAX 3840
#endif
#ifndef HTTP_URL_MAX
#define HTTP_URL_MAX  4096
#endif
#ifndef LFILE_PATH_MAX
#define LFILE_PATH_MAX 256
#endif
#ifndef LBUF_SIZE
//900k
#define LBUF_SIZE               921600
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
#ifndef MD5_LEN
#define MD5_LEN 16
#endif
typedef struct _LINK
{
    int status;
    unsigned char md5[MD5_LEN]; 
    long long offset;
    long nurl;
    int nhost;
    int npath;
}LINK;
typedef struct _HTTP_REQUEST
{
    int status;
    int id;
    char ip[HTTP_IP_MAX];
    int port;
    char host[HTTP_HOST_MAX];
    char path[HTTP_PATH_MAX];
    void *handler;
}HTTP_REQUEST;
typedef struct _URLMETA
{
    int status;
    int id;
    int size;
    int zsize;
    int hostoff;
    int pathoff;
    int htmloff;
    long long offset;
}URLMETA;
typedef struct _LINKTABLE
{
    int dnscount;
    char **dnslist;
    void *dnstable;
    void *md5table;
    void *md5io;
    void *urlio;
    void *metaio;
    void *docio;
    long long size;
    long long zsize;
    HTTP_REQUEST *requests;
    int nrequest;
    long urlno;
    long url_total;
    long urlok_total;
    URLMETA *tasks;
    int  iszlib;
    int  ntask;
    long  docno;
    long  doc_total;
    long  docok_total;
    void *logger;
    void *mutex;
    int  isinsidelogger;

    int     (*set_logger)(struct _LINKTABLE *, char *logfile, void *logger);
    int     (*set_md5file)(struct _LINKTABLE *, char *md5file);
    int     (*set_urlfile)(struct _LINKTABLE *, char *urlfile);
    int     (*set_metafile)(struct _LINKTABLE *, char *metafile);
    int     (*set_docfile)(struct _LINKTABLE *, char *docfile);
    int     (*parse)(struct _LINKTABLE *, char *host, char *path, char *content, char *end);
    int     (*add)(struct _LINKTABLE *, unsigned char *host, unsigned char *path, 
            unsigned char *href, unsigned char *ehref); 
    int     (*addurl)(struct _LINKTABLE *, char *host, char *path); 
    long    (*get_urltask)(struct _LINKTABLE *);
    void    (*urlhandler)(struct _LINKTABLE *, long id); 
    int     (*set_nrequest)(struct _LINKTABLE *, int nrequest);
    int     (*set_ntask)(struct _LINKTABLE *, int ntask);
    char*   (*getip)(struct _LINKTABLE *, char *host);
    int     (*get_request)(struct _LINKTABLE *, HTTP_REQUEST **req);
    int     (*update_request)(struct _LINKTABLE *, int sid, int status);
    int     (*add_content)(struct _LINKTABLE *, void *response, char *host, char *path,
                char *content, int ncontent);
    int     (*resume)(struct _LINKTABLE *);
    void    (*clean)(struct _LINKTABLE **);

}LINKTABLE;
/* Initialize LINKTABLE */
LINKTABLE *linktable_init();
#endif
