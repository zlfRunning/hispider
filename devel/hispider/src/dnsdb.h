#include <unistd.h>
#include <string.h>
#ifndef _DNSDB_H
#define _DNSDB_H
#define DNS_PATH_MAX 256
#define DNS_BUF_SIZE 65536
#define DNS_DOMAIN_NAME "dnsdb.domain"
#define DNS_IP_NAME "dnsdb.dns"
typedef struct _DNS
{
    int ip;
    int offset;
    int length;
}DNS;
typedef struct _DNS_QUERY
{
    
}DNS_QUERY;
typedef struct _DNSDB
{
    void *table;
    int  current;
    long total;
    int  domain_fd;
    int  dns_fd;
    void *mutex;

    int (*get)(struct _DNSDB *, char *domain);
    int (*update)(struct _DNSDB *, int no, int ip);
    int (*del)(struct _DNSDB *, char *domain);
    int (*resolve)(struct _DNSDB *, char *domain);
    int (*set_basedir)(struct _DNSDB *, char *path);
    int (*resume)(struct _DNSDB *);
    void (*clean)(struct _DNSDB **);
}DNSDB;
/* initialize */
DNSDB *dnsdb_init();
#endif
