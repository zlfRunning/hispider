#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include "dnsdb.h"
#include "trie.h"
#include "mutex.h"
#include "ibio.h"

/* get dns */
int dnsdb_get(DNSDB *dnsdb, char *domain)
{
    int ret = -1, n = 0;
    void *dp = NULL;

    if(dnsdb && dnsdb->table)
    {
        MUTEX_LOCK(dnsdb->mutex);
        n = strlen(domain);
        TRIETAB_RGET(dnsdb->table, domain, n, dp);
        if(dp)
        {
            ret = (int)dp;
        }
        MUTEX_UNLOCK(dnsdb->mutex);
    }
    return ret;
}

/* update dns */
int dnsdb_update(DNSDB *dnsdb, int no, char *domain, int ip)
{
    int ret = -1;
    void *dp = NULL;
    char buf[DNS_BUF_SIZE];
    DNS dns = {0};
    off_t offset = 0;

    if(dnsdb && no > 0 && no <= dnsdb->total)
    {
        MUTEX_LOCK(dnsdb->mutex);
        offset = (no-1) * sizeof(DNS);
        if(iwrite(dnsdb->dns_fd, &ip, sizeof(int), offset) > 0)
        {
            dp = (void *)((long)ip);
            TRIETAB_RADD(dnsdb->table, domain, strlen(domain), dp);
            ret = 0;
        }
        MUTEX_UNLOCK(dnsdb->mutex);
    }
    return ret;
}

/* delete dns */
int dnsdb_del(DNSDB *dnsdb, char *domain)
{
    int ret = -1, n = 0;
    long id = 0;
    void *dp = NULL;

    if(dnsdb)
    {
        MUTEX_LOCK(dnsdb->mutex);
        n = strlen(domain);
        TRIETAB_RGET(dnsdb->table, domain, n, dp);
        if((id = ((long)dp - 1)) >= 0)
        {
            iwrite(dnsdb->dns_fd, &n, sizeof(int), id * sizeof(DNS));
            ret = id;
        }
        MUTEX_UNLOCK(dnsdb->mutex);
    }
    return ret;
}

/* resolve */
int dnsdb_resolve(DNSDB *dnsdb, char *domain)
{
 
    int ret = -1, n = 0;
    void *dp = NULL;
    long id = 0;
    DNS dns = {0};
    char buf[DNS_BUF_SIZE];
    off_t offset = 0;

    if(dnsdb)
    {
        MUTEX_LOCK(dnsdb->mutex);
        if(domain && (n = strlen(domain)) > 0)
        {
            TRIETAB_RGET(dnsdb->table, domain, n, dp);
            if(dp)
            {
                ret = (int)dp;
            }
            else
            {
                n = sprintf(buf, "%s\n", domain);
                if(iappend(dnsdb->domain_fd, buf, n, &offset) > 0)
                {
                    dns.offset = offset;
                    dns.length = n;
                    iappend(dnsdb->dns_fd, &dns, sizeof(DNS), &offset);
                    id = -1;
                    dp = (void *)&id;
                    TRIETAB_RADD(dnsdb->table, domain, (n-1), dp);
                    dnsdb->total++;
                    ret = 0;
                }
            }
        }
        MUTEX_UNLOCK(dnsdb->mutex);
    }
    return ret;
}

/* get task */
int dnsdb_get_task(DNSDB *dnsdb, char *domain)
{
    int taskid = -1;
    off_t offset = 0;
    DNS dns = {0};

    if(dnsdb)
    {
        MUTEX_LOCK(dnsdb->mutex);
        if(dnsdb->current < dnsdb->total)
        {
            dns.ip = -1;
            do
            {
                offset = sizeof(DNS) * dnsdb->current++;
                if(iread(dnsdb->dns_fd, &dns, sizeof(DNS), offset) > 0 && dns.ip == 0)
                {
                    if(iread(dnsdb->domain_fd, domain, dns.length, dns.offset) > 0)
                    {
                        taskid = dnsdb->current - 1;
                        domain[dns.length] = '\0';
                        break;
                    }
                }
            }while(dns.ip != 0);
        }
        MUTEX_UNLOCK(dnsdb->mutex);
    }
    return taskid;
}

/* set basedir */
int dnsdb_set_basedir(DNSDB *dnsdb, char *path)
{
    int ret = -1;
    char buf[DNS_PATH_MAX];

    if(dnsdb)
    {
        sprintf(buf, "%s/%s", path, DNS_DOMAIN_NAME);
        dnsdb->domain_fd = open(buf, O_CREAT|O_RDWR, 0644);
        sprintf(buf, "%s/%s", path, DNS_IP_NAME);
        dnsdb->dns_fd = open(buf, O_CREAT|O_RDWR, 0644);
        if(dnsdb->domain_fd > 0 && dnsdb->dns_fd > 0)
        {
            ret = 0;
        }
    }
    return ret;
}

/* resume dnsdb */
int dnsdb_resume(DNSDB *dnsdb)
{
    int ret = -1;
    char *domain = NULL, *domain_list = NULL, *domain_list_end = NULL;
    DNS *dns = NULL, *dns_list =  NULL, *dns_list_end = NULL;
    struct stat st = {0};
    void *dp = NULL;

    if(dnsdb && dnsdb->domain_fd > 0 && dnsdb->dns_fd > 0)
    {
        if(fstat(dnsdb->domain_fd, &st) == 0 && (domain_list = calloc(1, st.st_size)))         
        {
            read(dnsdb->domain_fd, domain_list, st.st_size);
            domain_list_end = domain_list + st.st_size;
        }
        if(fstat(dnsdb->dns_fd, &st) == 0 && (dns_list = calloc(1, st.st_size)))         
        {
            read(dnsdb->dns_fd, dns_list, st.st_size);
            dns_list_end = dns_list + st.st_size;
        }
        dns = dns_list;
        while(dns < dns_list_end)
        {
            if(dns->ip == 0) 
            {
                dp = (void *)-1;
            }
            else 
            {
                dnsdb->current = ((dns - dns_list) /sizeof(DNS)) + 1;
                dp = (void *)((long)(dns->ip));
            }
            domain = domain_list + dns->offset;
            if(domain < domain_list_end)
            {
                TRIETAB_RADD(dnsdb->table, domain, dns->length, dp);
            }
            dnsdb->total++;
            dns++;
        }
        if(domain_list) free(domain_list);
        if(dns_list) free(dns_list);
    }
    return ret;
}

/* clean dnsdb */
void dnsdb_clean(DNSDB **pdnsdb)
{
    if(pdnsdb && *pdnsdb)
    {
        TRIETAB_CLEAN((*pdnsdb)->table);
        MUTEX_DESTROY((*pdnsdb)->mutex);
        if((*pdnsdb)->domain_fd > 0)close((*pdnsdb)->domain_fd);
        if((*pdnsdb)->dns_fd > 0)close((*pdnsdb)->dns_fd);
        free(*pdnsdb);
        (*pdnsdb) = NULL;
    }
}

/* initialize dnsdb */
DNSDB *dnsdb_init()
{
    DNSDB *dnsdb = NULL;
    if((dnsdb = (DNSDB *)calloc(1, sizeof(DNSDB))))
    {
        MUTEX_INIT(dnsdb->mutex);
        dnsdb->table = TRIETAB_INIT();
        dnsdb->get = dnsdb_get;
        dnsdb->update = dnsdb_update;
        dnsdb->del = dnsdb_del;
        dnsdb->resolve = dnsdb_resolve;
        dnsdb->get_task = dnsdb_get_task;
        dnsdb->set_basedir = dnsdb_set_basedir;
        dnsdb->resume = dnsdb_resume;
        dnsdb->clean = dnsdb_clean;
    }
    return dnsdb;
}

#ifndef _DEBUG_DNSDB
#include <stdio.h>
int main(int argc, char **argv)
{
    DNSDB *dnsdb = NULL;


}
#endif
