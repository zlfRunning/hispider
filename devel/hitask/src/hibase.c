#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "hibase.h"
#include "trie.h"
#include "fqueue.h"
#include "timer.h"
#include "logger.h"
#include "base64.h"
#include "hio.h"
#define  HIBASE_TABLE_NAME          "hibase.table"
#define  HIBASE_TEMPLATE_NAME       "hibase.template"
#define  HIBASE_PNODE_NAME          "hibase.pnode"
#define  HIBASE_URLNODE_NAME        "hibase.urlnode"
#define  HIBASE_QPNODE_NAME         "hibase.qpnode"
#define  HIBASE_QTEMPLATE_NAME      "hibase.qtemplate"
#define  HIBASE_QURLNODE_NAME       "hibase.qurlnode"
#define _EXIT_(format...)                                                               \
do                                                                                      \
{                                                                                       \
    fprintf(stderr, "%s::%d ", __FILE__, __LINE__);                                     \
    fprintf(stderr, format);                                                            \
    _exit(-1);                                                                          \
}while(0)
#define HIO_INCRE(io, type, incre_num)                                                  \
do                                                                                      \
{                                                                                       \
    if(io.fd  > 0 && incre_num > 0)                                                     \
    {                                                                                   \
        io.size += ((off_t)sizeof(type) * (off_t)incre_num);                            \
        ftruncate(io.fd, io.size);                                                      \
        io.left += incre_num;                                                           \
        io.total = io.size/(off_t)sizeof(type);                                         \
    }                                                                                   \
}while(0)
#define HIO_MMAP(io, type, incre_num)                                                   \
do                                                                                      \
{                                                                                       \
    if(io.fd > 0 && incre_num > 0)                                                      \
    {                                                                                   \
        if(io.map && io.map != (void *)(-1))                                            \
        {                                                                               \
            msync(io.map, io.size, MS_SYNC);                                            \
            munmap(io.map, io.size);                                                    \
        }                                                                               \
        io.size += ((off_t)sizeof(type) * (off_t)incre_num);                            \
        ftruncate(io.fd, io.size);                                                      \
        io.left += incre_num;                                                           \
        io.total = io.size/(off_t)sizeof(type);                                         \
        if((io.map = mmap(NULL, io.size, PROT_READ|PROT_WRITE, MAP_SHARED,              \
                        io.fd, 0)) == (void *)-1)                                       \
        {                                                                               \
            _EXIT_("mmap %d size:%lld failed, %s\n", io.fd,                             \
                    (long long int)io.size, strerror(errno));                           \
        }                                                                               \
    }                                                                                   \
}while(0)
#define HIO_MAP(io, type) ((io.map && io.map != (void *)-1)?(type *)io.map : NULL)
#define HIO_MUNMAP(io)                                                                  \
do                                                                                      \
{                                                                                       \
    if(io.map && io.size > 0)                                                           \
    {                                                                                   \
        msync(io.map, io.size, MS_SYNC);                                                \
        munmap(io.map, io.size);                                                        \
        io.map = NULL;                                                                  \
    }                                                                                   \
}while(0)
#define HIO_INIT(io, file, st, type)                                                    \
do                                                                                      \
{                                                                                       \
    if(file && (io.fd = open(file, O_CREAT|O_RDWR, 0644)) > 0                           \
            && fstat(io.fd, &st) == 0)                                                  \
    {                                                                                   \
        io.size = st.st_size;                                                           \
        io.total = (st.st_size/(off_t)sizeof(type));                                    \
    }                                                                                   \
    else                                                                                \
    {                                                                                   \
        _EXIT_("initialize file(%s) failed, %s", file, strerror(errno));                \
    }                                                                                   \
}while(0)
#define HIO_CLEAN(io)                                                                   \
{                                                                                       \
    if(io.map && io.size > 0)                                                           \
    {                                                                                   \
        msync(io.map, io.size, MS_SYNC);                                                \
        munmap(io.map, io.size);                                                        \
        io.map = NULL;                                                                  \
    }                                                                                   \
    if(io.fd > 0)                                                                       \
    {                                                                                   \
        close(io.fd);                                                                   \
        io.fd = 0;                                                                      \
        io.left = 0;                                                                    \
        io.total = 0;                                                                   \
    }                                                                                   \
    io.size = 0;                                                                        \
}
//tables
static char *table_data_types[] = {"null", "int", "float", "null", "text",
    "null","null","null","blob"};
/* mkdir */
int hibase_mkdir(char *path, int mode)
{
    char *p = NULL, fullpath[HIBASE_PATH_MAX];
    int ret = 0, level = -1;
    struct stat st;

    if(path)
    {
        strcpy(fullpath, path);
        p = fullpath;
        while(*p != '\0')
        {
            if(*p == '/' )
            {
                level++;
                while(*p != '\0' && *p == '/' && *(p+1) == '/')++p;
                if(level > 0)
                {
                    *p = '\0';
                    memset(&st, 0, sizeof(struct stat));
                    ret = stat(fullpath, &st);
                    if(ret == 0 && !S_ISDIR(st.st_mode)) return -1;
                    if(ret != 0 && mkdir(fullpath, mode) != 0) return -1;
                    *p = '/';
                }
            }
            ++p;
        }
        return 0;
    }
    return -1;
}

/* set basedir */
int hibase_set_basedir(HIBASE *hibase, char *dir)
{
    char path[HIBASE_PATH_MAX], *p = NULL;
    ITEMPLATE *template = NULL;
    ITABLE *table = NULL;
    PNODE *pnode = NULL;
    URLNODE *urlnode = NULL;
    struct stat st = {0};
    int i = 0, j = 0, n = 0;
    void *dp = NULL;

    if(hibase && dir)
    {
        n = sprintf(hibase->basedir, "%s/", dir);
        hibase_mkdir(hibase->basedir, 0755);
        //resume table
        p = path;
        sprintf(path, "%s%s", hibase->basedir, HIBASE_TABLE_NAME);  
        HIO_INIT(hibase->tableio, p, st, ITABLE);
        HIO_MMAP(hibase->tableio, ITABLE, TABLE_INCRE_NUM); 
        if(hibase->tableio.fd > 0 && (table = HIO_MAP(hibase->tableio, ITABLE)))
        {
            hibase->tableio.left = 0;
            for(i = 0; i < hibase->tableio.total; i++)
            {
                if((n = strlen(table[i].name)) > 0)
                {
                    if(table[i].uid > hibase->db_uid_max)
                        hibase->db_uid_max = table[i].uid;
                    dp = (void *)((long)(table[i].uid));
                    TRIETAB_ADD(hibase->mdb, table[i].name, n, dp);
                    for(j = 0; j < table[i].nfields; j++)
                    {
                        if((n = strlen(table[i].fields[j].name)) > 0)
                        {
                            if(table[i].fields[j].uid > hibase->db_uid_max)
                                hibase->db_uid_max = table[i].fields[j].uid;
                            dp = (void *)((long)(table[i].fields[j].uid));
                            TRIETAB_ADD(hibase->mdb, table[i].fields[j].name, n, dp);
                        }
                    }
                    hibase->tableio.current = i;
                }
                else hibase->tableio.left++;
            }
        }
        //resume template 
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QTEMPLATE_NAME);
        p = path;
        FQUEUE_INIT(hibase->qtemplate, p, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_TEMPLATE_NAME);   
        HIO_INIT(hibase->templateio, p, st, ITEMPLATE);
        HIO_MMAP(hibase->templateio, ITEMPLATE, TEMPLATE_INCRE_NUM); 
        if(hibase->templateio.fd > 0 && (template = HIO_MAP(hibase->templateio, ITEMPLATE)))
        {
            hibase->templateio.left = 0;
            for(i = 1; i < hibase->templateio.total; i++)
            {
                if(template[i].status == TEMPLATE_STATUS_OK)
                {
                    hibase->templateio.current = i;
                }
                else hibase->templateio.left++;
            }
        }
        //resume pnode
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QPNODE_NAME);
        p = path;
        FQUEUE_INIT(hibase->qpnode, p, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_PNODE_NAME);  
        HIO_INIT(hibase->pnodeio, p, st, PNODE);
        HIO_MMAP(hibase->pnodeio, PNODE, PNODE_INCRE_NUM); 
        if(hibase->pnodeio.fd  > 0 && (pnode = HIO_MAP(hibase->pnodeio, PNODE)))
        {
            hibase->pnodeio.left = 0;
            for(i = 1; i < hibase->pnodeio.total; i++)
            {
                if(pnode[i].status && (n = strlen(pnode[i].name)) > 0)
                {
                    dp = (void *)((long)(i + 1));
                    TRIETAB_ADD(hibase->mpnode, pnode[i].name, n, dp);
                    hibase->pnodeio.current = i;
                    if(pnode[i].uid > hibase->uid_max) hibase->uid_max = pnode[i].uid;
                    if(pnode[i].nchilds > hibase->pnode_childs_max)
                        hibase->pnode_childs_max = pnode[i].nchilds;
                }
                else hibase->pnodeio.left++;
            }
        }
        //resume urlnode
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QURLNODE_NAME);
        p = path;
        FQUEUE_INIT(hibase->qurlnode, p, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_URLNODE_NAME);
        HIO_INIT(hibase->urlnodeio, p, st, URLNODE);
        HIO_MMAP(hibase->urlnodeio, URLNODE, URLNODE_INCRE_NUM);
        if(hibase->urlnodeio.fd  > 0 && (urlnode = HIO_MAP(hibase->urlnodeio, URLNODE)))
        {
            hibase->urlnodeio.left = 0;
            for(i = 1; i < hibase->urlnodeio.total; i++)
            {
                if(urlnode[i].status)
                {
                    hibase->urlnodeio.current = i;
                }
                else
                {
                    hibase->urlnodeio.left++;
                }
            }
            //HIO_MUNMAP(hibase->urlnodeio);
        }
    }
    return -1;
}

/* check table exists */
int hibase_db_uid_exists(HIBASE *hibase, int tableid, char *name, int len)
{
    ITABLE *tab = NULL;
    int uid = -1, i = 0;
    void *dp = NULL;

    if(hibase && name && len > 0 && hibase->mdb)
    {
        MUTEX_LOCK(hibase->mutex);
        TRIETAB_GET(hibase->mdb, name, len, dp);
        if((uid = (int)((long)dp)) == 0) --uid;
        if(uid < 0 && tableid >= 0)
        {
            uid = ++(hibase->db_uid_max);
            dp = (void *)((long)uid);
            TRIETAB_ADD(hibase->mdb, name, len, dp);
        }
        else
        {
            if(uid > 0 && tableid >= 0 && tableid < hibase->tableio.total
                    && (tab = (ITABLE *)hibase->tableio.map) && tab != (ITABLE *)-1)
            {
                for(i = 0; i < FIELD_NUM_MAX; i++)
                {
                    if(tab[tableid].fields[i].uid == uid)
                    {
                        uid = 0;
                        break;
                    }
                }
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return uid;
}

/* add table */
int hibase_add_table(HIBASE *hibase, char *table_name)
{
    int id = -1, uid = -1, i = 0, n = 0;
    ITABLE *tab = NULL;
    void *dp = NULL;

    if(hibase && (n = strlen(table_name))  > 0) 
    {
        uid = hibase_db_uid_exists(hibase, -1, table_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(hibase->tableio.left == 0)
        {
            HIO_MMAP(hibase->tableio, ITABLE, TABLE_INCRE_NUM);
        }        
        if(hibase->tableio.left > 0 && (tab = HIO_MAP(hibase->tableio, ITABLE))) 
        {
            for(i = 0; i < hibase->tableio.total; i++)
            {
                if(tab[i].uid == uid)
                {
                    uid = 0;
                    break;
                }
                //check exists
                else if(id < 0 && tab[i].status == TAB_STATUS_INIT)
                {
                    id = i;
                }
            }
            //set table
            if(id >= 0 && uid != 0)
            {
                memset(&(tab[id]), 0, sizeof(ITABLE));
                tab[id].status = TAB_STATUS_OK;
                memcpy(tab[id].name, table_name, n);
                if(uid > 0)
                    tab[id].uid = uid;
                else 
                {
                    tab[id].uid = ++(hibase->db_uid_max);
                    dp = (void *)((long)(tab[id].uid));
                    TRIETAB_ADD(hibase->mdb, table_name, n, dp);
                    hibase->tableio.left--;
                }
            }else id = -1;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    else id = -1;
    return id;
}

/* get table */
int hibase_get_table(HIBASE *hibase, int tableid, ITABLE *ptable)
{
    ITABLE *tab = NULL;
    int id = -1;

    if(hibase && tableid >= 0 && tableid < hibase->tableio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        id = tableid;
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            memcpy(ptable, &(tab[id]), sizeof(ITABLE)); 
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* rename table */
int hibase_rename_table(HIBASE *hibase, int tableid, char *table_name)
{
    int id = -1, uid =-1, i = 0, n = 0;
    ITABLE *tab = NULL;
    void*dp = NULL;

    if(hibase && hibase->mdb && (n = strlen(table_name)) > 0)
    {
        uid = hibase_db_uid_exists(hibase, -1, table_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(tableid >= 0 && tableid < hibase->tableio.total 
                && (tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            if(uid > 0)
            {
                for(i = 0; i < hibase->tableio.total; i++)
                {
                    if(tab[i].uid == uid)
                    {
                        //name exists
                        uid = 0;
                        break;
                    }
                }
            }
            else
            {
                uid = ++(hibase->db_uid_max);
                dp = (void *)((long) uid);
                TRIETAB_ADD(hibase->mdb, table_name, n, dp);
            }
            if(uid > 0)
            {
                tab[tableid].uid = uid;
                memset(tab[tableid].name, 0, TABLE_NAME_MAX);
                memcpy(tab[tableid].name, table_name, n);
                id = tableid;
                //failed
            }else id = uid;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* delete table */
int hibase_delete_table(HIBASE *hibase, int tableid)
{
    ITABLE *tab = NULL;
    int id = -1;

    if(hibase && hibase->mdb && tableid >= 0 && tableid < hibase->tableio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1
                && tab[tableid].status == TAB_STATUS_OK)
        {
            memset(&(tab[tableid]), 0, sizeof(ITABLE));
            hibase->tableio.left++;
            id = tableid;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* add new field */
int hibase_add_field(HIBASE *hibase, int tableid, char *name, int type, int flag)
{
    int from = 0, to = 0, i = 0, n = 0, id = -1, uid = -1;
    ITABLE *tab = NULL;

    if(hibase && tableid >= 0 && tableid < hibase->tableio.total 
            && (n = strlen(name)) > 0 && n < FIELD_NAME_MAX && (type & FTYPE_ALL) 
            && (uid = hibase_db_uid_exists(hibase, tableid, name, n)) > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1
                && tab[tableid].nfields < FIELD_NUM_MAX)
        {
            if(flag >= 0 && (flag & F_IS_INDEX) > 0){from = 0; to = HI_INDEX_MAX;}
            else {from = HI_INDEX_MAX; to = FIELD_NUM_MAX;}
            for(i = from; i < to; i++)
            {
                if(tab[tableid].fields[i].status == FIELD_STATUS_INIT)
                {
                    tab[tableid].fields[i].status = FIELD_STATUS_OK;
                    tab[tableid].fields[i].type   = type;
                    if(flag >= 0) tab[tableid].fields[i].flag   |= flag;
                    tab[tableid].fields[i].uid    = uid;
                    memcpy(tab[tableid].fields[i].name, name, n);
                    tab[tableid].nfields++;
                    id = i;
                    break;
                }
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* update  field */
int hibase_update_field(HIBASE *hibase, int tableid, int fieldid, 
        char *name, int type, int flag)
{
    int from = 0, to = 0, i = 0, n = 0, id = -1, uid = -1, 
        isindex = 0, is_need_move = 0, old_is_index = 0;
    ITABLE *tab = NULL;

    if(hibase && tableid >= 0 && tableid < hibase->tableio.total 
            && fieldid >= 0 && fieldid < FIELD_NUM_MAX)
    {
        if(name && (n = strlen(name)) > 0 && n < FIELD_NAME_MAX 
                && (uid = hibase_db_uid_exists(hibase, tableid, name, n)) == 0)
        {
            return -1;
        }
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1
            && tab[tableid].fields[fieldid].status == FIELD_STATUS_OK)
        {
            if(flag >= 0 ) isindex = (flag & F_IS_INDEX);
            old_is_index = (tab[tableid].fields[fieldid].flag & F_IS_INDEX);
            if(isindex > 0 && old_is_index == 0)
            {
                is_need_move = 1; from = 0; to = HI_INDEX_MAX;
            }
            else if(isindex == 0 && old_is_index > 0)
            {
                is_need_move = 1; from = HI_INDEX_MAX; to = FIELD_NUM_MAX;
            }
            if(is_need_move)
            {
                for(i = from; i < to; i++)
                {
                    if(tab[tableid].fields[i].status == FIELD_STATUS_INIT)
                    {
                        memcpy(&(tab[tableid].fields[i]), &(tab[tableid].fields[fieldid]), 
                                sizeof(IFIELD));
                        if(type > 0) tab[tableid].fields[i].type   = type;
                        memset(&(tab[tableid].fields[fieldid]), 0, sizeof(IFIELD));
                        id = i;
                        break;
                    }
                }
            }
            else
            {
                if(type & FTYPE_ALL) tab[tableid].fields[fieldid].type = type;
                id = fieldid;
            }
            if(id >= 0 && uid > 0)
            {
                tab[tableid].fields[id].uid    = uid;
                memset(tab[tableid].fields[id].name, 0, FIELD_NAME_MAX);
                memcpy(tab[tableid].fields[id].name, name, n);
            }
            if(id >= 0 && flag >= 0)
            {
                tab[tableid].fields[id].flag = (tab[tableid].fields[id].flag & flag);
                tab[tableid].fields[id].flag |= flag;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* delete field */
int hibase_delete_field(HIBASE *hibase, int tableid, int fieldid)
{
    ITABLE *tab = NULL;
    int id = -1;

    if(hibase && tableid >= 0 && tableid < hibase->tableio.total
            && fieldid >= 0 && fieldid < FIELD_NUM_MAX)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            if(tab[tableid].nfields > 0 && tab[tableid].fields[fieldid].status == FIELD_STATUS_OK)
            {
                memset(&(tab[tableid].fields[fieldid]), 0, sizeof(IFIELD));
                tab[tableid].nfields--;
                id = fieldid;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* view  table */
int hibase_view_table(HIBASE *hibase, int tableid, char *block)
{
    int ret = -1, i = 0;
    ITABLE *tab = NULL;
    char buf[HI_BUF_SIZE], *p =  NULL, *pp = NULL;

    if(hibase && tableid >= 0 && tableid < hibase->tableio.total && block)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1
            && tab[tableid].status == TAB_STATUS_OK)
        {
            p = buf;
            p += sprintf(p, "({'id':'%d', 'name':'%s', 'nfields':'%d', 'fields':[", 
                    tableid, tab[tableid].name, tab[tableid].nfields);
            pp = p;
            for(i = 0; i < FIELD_NUM_MAX; i++)
            {
                if(tab[tableid].fields[i].status == FIELD_STATUS_OK)
                {
                    p += sprintf(p, "{'id':'%d', 'name':'%s', 'type':'%d', "
                            "'flag':'%d', 'status':'%d'},", i, tab[tableid].fields[i].name, 
                            tab[tableid].fields[i].type, tab[tableid].fields[i].flag, 
                            tab[tableid].fields[i].status);
                }
            }
            if(p != pp) --p;
            p += sprintf(p, "%s", "]})");
            ret = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* list tables */
int hibase_list_table(HIBASE *hibase, char *block)
{
    int ret = -1, i = 0;
    ITABLE *tab = NULL;
    char buf[HI_BUF_SIZE], *p =  NULL, *pp = NULL;

    if(hibase && block)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            p = buf;
            p += sprintf(p, "%s","({'tables':[");
            pp = p;
            for(i = 0; i < hibase->tableio.total; i++)
            {
                if(tab[i].status == TAB_STATUS_OK)
                    p += sprintf(p, "{'id':'%d', 'name':'%s', 'nfields':'%d'},", 
                            i, tab[i].name, tab[i].nfields);
            }
            if(p == pp) ret = 0;
            else
            {
                --p;
                p += sprintf(p, "%s", "]})");
                ret = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (p - buf), buf);
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* view DB */
int hibase_view_database(HIBASE *hibase, char *block)
{
    int ret = -1, i = 0, j = 0;
    ITABLE *tab = NULL;
    char buf[HI_BUF_SIZE], *p =  NULL, *pp = NULL, *ppp = NULL;

    if(hibase && block)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            p = buf;
            p += sprintf(p, "%s","({'tables':{");
            pp = p;
            for(i = 0; i < hibase->tableio.total; i++)
            {
                if(tab[i].status == TAB_STATUS_OK)
                {
                    p += sprintf(p, "'%d':{'id':'%d', 'name':'%s', 'nfields':'%d', 'fields':{", 
                        i, i, tab[i].name, tab[i].nfields);
                    ppp = p;
                    for(j = 0; j < FIELD_NUM_MAX; j++)
                    {
                        if(tab[i].fields[j].status == FIELD_STATUS_OK)
                        {
                            p += sprintf(p, "'%d':{'id':'%d', 'name':'%s', 'type':'%d', "
                                    "flag:'%d', status:'%d'},", j, j, tab[i].fields[j].name, 
                                    tab[i].fields[j].type, tab[i].fields[j].flag, 
                                    tab[i].fields[j].status);
                        }
                    }
                    if(p != ppp) --p;
                    p += sprintf(p, "%s", "}},");
                }
            }
            if(p != pp)--p;
            p += sprintf(p, "%s", "}})");
            ret = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}
/* check pnode exists */
int hibase_pnode_exists(HIBASE *hibase, int parentid, char *name, int len)
{
    int uid = -1, id = -1;
    PNODE *pnode = NULL;
    void *dp = NULL;

    if(hibase && name && len > 0 && hibase->mpnode)
    {
        MUTEX_LOCK(hibase->mutex);
        TRIETAB_GET(hibase->mpnode, name, len, dp);
        if((uid = (long)dp) <= 0)
        {
            uid = ++(hibase->uid_max);
            dp = (void *)((long)uid);
            TRIETAB_ADD(hibase->mpnode, name, len, dp);
        }
        if(parentid >= 0  && parentid < hibase->pnodeio.total 
                && (pnode = (PNODE *)hibase->pnodeio.map) && pnode != (PNODE *)-1)
        {
            id = pnode[parentid].first;
            while(id > 0 && uid > 0)
            {
                if(pnode[id].uid == uid){uid = -1;break;}
                id = pnode[id].next;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return uid;
}

/* add pnode */
int hibase_add_pnode(HIBASE *hibase, int parentid, char *name)
{
    int pnodeid = -1, uid = -1, n = 0, *px = NULL, x = 0;
    PNODE *pnode = NULL, *pparent = NULL;

    if(hibase && name && (n = strlen(name))  > 0 
            && (uid = hibase_pnode_exists(hibase, parentid, name, n)) > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->pnodeio.left == 0)
        {HIO_MMAP(hibase->pnodeio, PNODE, PNODE_INCRE_NUM);}
        px = &x;
        if(FQUEUE_POP(hibase->qpnode, int, px) == 0)
            pnodeid = x;
        else
            pnodeid = ++(hibase->pnodeio.current);
        if(parentid >= 0 && parentid < hibase->pnodeio.total 
                && pnodeid >= 0  && pnodeid < hibase->pnodeio.total 
                && (pnode = HIO_MAP(hibase->pnodeio, PNODE)))
        {
            memset(&(pnode[pnodeid]), 0, sizeof(PNODE));
            memcpy(pnode[pnodeid].name, name, n); 
            pnode[pnodeid].id = pnodeid;
            pnode[pnodeid].uid = uid;
            pnode[pnodeid].status = 1;
            pnode[pnodeid].parent = parentid;
            pparent = &(pnode[parentid]);
            pnode[pnodeid].level = pparent->level + 1;
            if(pparent->nchilds == 0) 
                pparent->first = pparent->last = pnodeid;
            else 
            {
                pnode[pparent->last].next = pnodeid;
                pnode[pnodeid].prev = pparent->last;
                pparent->last = pnodeid;
            }
            pparent->nchilds++;
            if(pparent->nchilds > hibase->pnode_childs_max)
                hibase->pnode_childs_max = pparent->nchilds;
            hibase->pnodeio.left--;
            //fprintf(stdout, "%d::%s->%d\n", parentid, name, pnodeid);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    else pnodeid = -1;
    return pnodeid;
}

/* get pnode */
int hibase_get_pnode(HIBASE *hibase, int pnodeid, PNODE *ppnode)
{
    PNODE *pnode = NULL;
    int ret = -1;

    if(hibase && hibase->mpnode && ppnode)
    {
        MUTEX_LOCK(hibase->mutex);
        if(pnodeid >= 0 && pnodeid < hibase->pnodeio.total 
                && (pnode = (PNODE *)(hibase->pnodeio.map)) 
                && pnode != (PNODE *)-1 && pnode[pnodeid].status > 0)
        {
            memcpy(ppnode, &(pnode[pnodeid]), sizeof(PNODE));
            ret = 0;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* get pnode childs */
int hibase_get_pnode_childs(HIBASE *hibase, int pnodeid, PNODE *ppnode)
{
    PNODE *pnode = NULL, *parent = NULL;
    int i = 0, x = 0;

    if(hibase && hibase->mpnode && ppnode)
    {
        MUTEX_LOCK(hibase->mutex);
        if(pnodeid >= 0 && pnodeid < hibase->pnodeio.total 
                && (pnode = (PNODE *)(hibase->pnodeio.map)) && pnode != (PNODE *)-1)
        {
            parent = &(pnode[pnodeid]);
            x = parent->first;
            while(i < parent->nchilds && x >= 0 && x < hibase->pnodeio.total)
            {
                memcpy(ppnode, &(pnode[x]), sizeof(PNODE)); 
                x = pnode[x].next;
                ppnode++;
                i++;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return i;
}

/* view pnode childs */
int hibase_view_pnode_childs(HIBASE *hibase, int pnodeid, char *block)
{
    PNODE *pnode = NULL, *parent = NULL;
    char buf[HI_BUF_SIZE], *p = NULL;
    int i = 0, x = 0, n = -1;

    if(hibase && hibase->mpnode)
    {
        MUTEX_LOCK(hibase->mutex);
        if(pnodeid >= 0 && pnodeid < hibase->pnodeio.total 
                && (pnode = (PNODE *)(hibase->pnodeio.map)) && pnode != (PNODE *)-1)
        {
            parent = &(pnode[pnodeid]);
            p = buf;
            p += sprintf(p, "({'id':'%d','nchilds':'%d', 'childs':[", pnodeid, parent->nchilds);
            x = parent->first;
            while(i < parent->nchilds && x >= 0 && x < hibase->pnodeio.total)
            {
                if(i < (parent->nchilds - 1))
                    p += sprintf(p, "{'id':'%d','name':'%s','nchilds':'%d'},",
                            pnode[x].id, pnode[x].name, pnode[x].nchilds);
                else
                    p += sprintf(p, "{'id':'%d','name':'%s','nchilds':'%d'}",
                            pnode[x].id, pnode[x].name, pnode[x].nchilds);
                x = pnode[x].next;
                i++;
            }
            p += sprintf(p, "%s", "]})\r\n");
            n = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}


/* update pnode */
int hibase_update_pnode(HIBASE *hibase, int pnodeid, char *name)
{
    PNODE *pnode = NULL, node = {0};
    int id = -1, uid = -1, n = 0;

    if(hibase && hibase->mpnode && name && (n = strlen(name)) > 0 && n < PNODE_NAME_MAX
        && pnodeid > 0 && pnodeid < hibase->pnodeio.total
        && hibase_get_pnode(hibase, pnodeid, &node) == 0 
        && (uid = hibase_pnode_exists(hibase, node.parent, name, n)) > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if((pnode = (PNODE *)(hibase->pnodeio.map)) 
                && pnode != (PNODE *)-1 && pnode[pnodeid].status > 0)
        {
            memset(pnode[pnodeid].name, 0, PNODE_NAME_MAX);
            memcpy(pnode[pnodeid].name, name, n); 
            pnode[pnodeid].uid = uid;
            id = pnodeid;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* reset pnode */
int hibase_reset_pnode(HIBASE *hibase, int pnodeid)
{
    PNODE *pnodes = NULL, *parent = NULL;
    int id = -1, i = 0, x = 0, xx = 0, n = 0, *px = NULL;
    if(hibase)
    {
        if((id = pnodeid) > 0 && id < hibase->pnodeio.total 
                && (pnodes = (PNODE *)(hibase->pnodeio.map)) 
                && pnodes != (PNODE *)-1 && pnodes[id].status > 0)
        {
            if((x = pnodes[id].parent) >= 0 && x < hibase->pnodeio.total)
            {
                parent = &(pnodes[x]);
                if(parent->first == id) parent->first = pnodes[id].next;
                if(parent->last == id) parent->last = pnodes[id].prev;
                parent->nchilds--;
            }
            if((n = pnodes[id].prev) > 0 && n < hibase->pnodeio.total)
            {
                pnodes[n].next = pnodes[n].next;
            }
            if((n = pnodes[id].next) > 0 && n < hibase->pnodeio.total)
            {
                pnodes[n].prev = pnodes[n].prev;
            }
            px = &id;
            FQUEUE_PUSH(hibase->qpnode, int, px);
            i = 0;
            n = pnodes[id].nchilds;
            x = pnodes[id].first;
            while(i < n && x > 0 && x < hibase->pnodeio.total)
            {
                xx = pnodes[x].next;
                hibase_reset_pnode(hibase, x);
                x = xx;
                i++;
            }
            /*
            fprintf(stdout, "%d::delete %d nodes from node:%d ->%s\n",
                    __LINE__, n, id, pnodes[id].name);
            */
            memset(&(pnodes[id]), 0, sizeof(PNODE));
            hibase->pnodeio.left++;
        }
    }
    return id;
}
/* delete pnode */
int hibase_delete_pnode(HIBASE *hibase, int pnodeid)
{
    int id = -1;

    if(hibase)
    {
        MUTEX_LOCK(hibase->mutex);
        id = hibase_reset_pnode(hibase, pnodeid);
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* List pnode */
int hibase_list_pnode(HIBASE *hibase, int pnodeid, FILE *fp)
{
    PNODE *pnode = NULL;
    int i = 0, x = 0;

    if(hibase && pnodeid >= 0 && pnodeid < hibase->pnodeio.total
        && (pnode = (PNODE *)(hibase->pnodeio.map)) && pnode != (PNODE *)-1)
    {
        for(i = 0; i < pnode[pnodeid].level; i++)
        {
            fprintf(fp, "%s", "  ┆");
        }
        if(pnode[pnodeid].nchilds > 0)
        {
            fprintf(fp, "--+[%d]%s[%d]\n", pnode[pnodeid].nchilds, pnode[pnodeid].name, pnodeid);
        }
        else
        {
            fprintf(fp, "---%s[%d]\n", pnode[pnodeid].name, pnodeid);
        }
        x = pnode[pnodeid].first;
        for(i = 0; i < pnode[pnodeid].nchilds; i++)
        {
            if(x > 0 && x < hibase->pnodeio.total)
            {
                hibase_list_pnode(hibase, x, fp);
            }
            x = pnode[x].next;
        }
        return 0;
    }
    return -1;
}

/* add template */
int hibase_add_template(HIBASE *hibase, int pnodeid, ITEMPLATE *template)
{
    int templateid = -1, x = 0, *px = NULL;
    ITEMPLATE *ptemplate = NULL;
    PNODE *pnode = NULL;

    if(hibase && pnodeid >= 0 && pnodeid < hibase->pnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->templateio.left == 0)
        {HIO_MMAP(hibase->templateio, ITEMPLATE, TEMPLATE_INCRE_NUM);}
        px = &x;
        if(FQUEUE_POP(hibase->qtemplate, int, px) == 0)
            templateid = x;
        else
            templateid = ++(hibase->templateio.current);
        if((pnode = (PNODE *)(hibase->pnodeio.map)) && pnode != (PNODE *)-1
            && templateid >= 0 && templateid < hibase->templateio.total
            && (ptemplate = HIO_MAP(hibase->templateio, ITEMPLATE)))
        {
            memcpy(&(ptemplate[templateid]), template, sizeof(ITEMPLATE));
            ptemplate[templateid].status = TEMPLATE_STATUS_OK;
            if(pnode[pnodeid].ntemplates == 0)
            {
                pnode[pnodeid].template_first = pnode[pnodeid].template_last = templateid;
            }
            else
            {
               x =  pnode[pnodeid].template_last;
               ptemplate[x].next = templateid;
               ptemplate[templateid].prev = x;
            }
            pnode[pnodeid].ntemplates++;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return templateid;
}

/* get template */
int hibase_get_template(HIBASE *hibase, int templateid, ITEMPLATE *template)
{
    ITEMPLATE *ptemplate = NULL;
    int ret = -1;

    if(hibase && templateid >= 0 && templateid < hibase->templateio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((ptemplate = (ITEMPLATE *)(hibase->templateio.map)) && ptemplate != (ITEMPLATE *)-1)
        {
            memcpy(template, &(ptemplate[templateid]), sizeof(ITEMPLATE));
            ret = templateid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* update template */
int hibase_update_template(HIBASE *hibase, int templateid, ITEMPLATE *template)
{
    ITEMPLATE *ptemplate = NULL;
    int ret = -1;

    if(hibase && templateid >= 0 && templateid < hibase->templateio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((ptemplate = (ITEMPLATE *)(hibase->templateio.map)) && ptemplate != (ITEMPLATE *)-1)
        {
            memcpy(&(ptemplate[templateid]), template, sizeof(ITEMPLATE) - (sizeof(int) * 2));
            ptemplate[templateid].status = TEMPLATE_STATUS_OK;
            ret = templateid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* delete template */
int hibase_delete_template(HIBASE *hibase, int pnodeid, int templateid)
{
    int ret = -1, x = 0, *px = NULL;
    ITEMPLATE *ptemplate = NULL;
    PNODE *pnode = NULL;

    if(hibase && pnodeid >= 0 && pnodeid < hibase->pnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((pnode = (PNODE *)(hibase->pnodeio.map)) && pnode != (PNODE *)-1
            && templateid >= 0 && templateid < hibase->templateio.total
            && (ptemplate = (ITEMPLATE *)(hibase->templateio.map)) && ptemplate != (ITEMPLATE *)-1)
        {
            if(ptemplate[templateid].prev > 0)
            {
                x = ptemplate[templateid].prev;
                ptemplate[x].next = ptemplate[templateid].next;
            }
            if(ptemplate[templateid].next > 0)
            {
                x = ptemplate[templateid].next;
                ptemplate[x].prev = ptemplate[templateid].prev;
            }
            if(pnode[pnodeid].template_first == templateid)
                pnode[pnodeid].template_first = ptemplate[templateid].next;
            if(pnode[pnodeid].template_last == templateid)
                pnode[pnodeid].template_last = ptemplate[templateid].prev;
            memset(&(ptemplate[templateid]), 0, sizeof(ITEMPLATE));
            px = &templateid;
            FQUEUE_PUSH(hibase->qtemplate, int, px);
            pnode[pnodeid].ntemplates--;
            ret = templateid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* view templates */
int hibase_view_templates(HIBASE *hibase, int pnodeid, char *block)
{
    ITEMPLATE *ptemplate = NULL;
    PNODE *pnode = NULL;
    int n = -1, x = 0, i = 0;
    char buf[HI_BUF_SIZE], xbuf[HI_BUF_SIZE], *pattern = NULL, *p = NULL;

    if(hibase && pnodeid >= 0 && pnodeid < hibase->pnodeio.total && block)
    {
        MUTEX_LOCK(hibase->mutex);
        if((pnode = (PNODE *)(hibase->pnodeio.map)) && pnode != (PNODE *)-1
            && (ptemplate = (ITEMPLATE *)(hibase->templateio.map)) 
            && ptemplate != (ITEMPLATE *)-1)
        {
            p = buf;
            p += sprintf(p, "({'id':'%d', 'name':'%s', 'ntemplates':'%d', 'templates':[", 
                    pnodeid, pnode[pnodeid].name, pnode[pnodeid].ntemplates);
            if(pnode[pnodeid].ntemplates > 0)
            {
                x = pnode[pnodeid].template_first;
                while(x > 0)
                {
                    if((n = strlen(ptemplate[x].pattern)) > 0
                        && (HI_BUF_SIZE > BASE64_LEN(n)))
                    {
                        base64_encode(xbuf, (unsigned char *)ptemplate[x].pattern, n);
                        pattern = xbuf;
                    }
                    else pattern = "";
                    //fprintf(stdout, "%d::%s\n%s\n", __LINE__, ptemplate[x].pattern, pattern);
                    p += sprintf(p, "{'id':'%d', 'flags':'%d', 'pattern':'%s', 'link':'%s',",
                            x, ptemplate[x].flags, pattern, ptemplate[x].link);
                    {
                        p += sprintf(p, "'linkmap':{'tableid':'%d','fieldid':'%d', "
                                "'nodeid':'%d', 'flag':'%d'},", 
                            ptemplate[x].linkmap.tableid, ptemplate[x].linkmap.fieldid,
                            ptemplate[x].linkmap.nodeid, ptemplate[x].linkmap.flag);
                    }
                    p += sprintf(p, "'url':'%s', 'nfields':'%d', 'map':[", 
                            ptemplate[x].url, ptemplate[x].nfields);
                    if(ptemplate[x].nfields > 0)
                    {
                        i = 0;
                        while(i < ptemplate[x].nfields && ptemplate[x].nfields < FIELD_NUM_MAX)
                        {
                            p += sprintf(p, "{'tableid':'%d', 'fieldid':'%d', 'nodeid':'%d', "
                                    "'flag':'%d'},", ptemplate[x].map[i].tableid, 
                                    ptemplate[x].map[i].fieldid, ptemplate[x].map[i].nodeid, 
                                    ptemplate[x].map[i].flag);
                            i++;
                        }
                        --p;
                    }
                    p += sprintf(p, "%s","]},");
                    x = ptemplate[x].next;
                }
                *--p = '\0';
            }
            p += sprintf(p, "%s", "]})");
            n = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}

/* hibase check urlid exist */
int hibase_exists_urlnode(HIBASE *hibase, int nodeid, int parentid, int urlid)
{
    int urlnodeid = -1, x = 0, z = 0;
    URLNODE *urlnode = NULL;
    PNODE *pnode = NULL;

    if(hibase && urlid >= 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if((urlnode = HIO_MAP(hibase->urlnodeio, URLNODE)))
        {
            if(parentid > 0 && parentid < hibase->urlnodeio.total 
                && urlnode[parentid].nchilds > 0) 
                    x = urlnode[parentid].first;
            if(nodeid > 0 && nodeid < hibase->pnodeio.total
                    && (pnode = HIO_MAP(hibase->pnodeio, PNODE)) 
                    && pnode[nodeid].nurlnodes > 0)
                    z = pnode[nodeid].urlnode_first;
            while(x > 0 || z > 0)
            {
                if(x > 0)
                {
                    if(urlnode[x].urlid == urlid)
                    {
                        urlnodeid = x;
                        goto end;
                        break;
                    }
                    else x = urlnode[x].node_next;
                }
                if(z > 0)
                {
                    if(urlnode[z].urlid == urlid)
                    {
                        urlnodeid = z;
                        goto end;
                        break;
                    }
                    else z = urlnode[z].next;
                }
            }
        }   
end:
        MUTEX_UNLOCK(hibase->mutex);
    }
    return urlnodeid;
}

/* addd url node */
int hibase_add_urlnode(HIBASE *hibase, int nodeid, int parentid, int urlid, int level)
{
    int urlnodeid = -1, x = 0, *px = NULL;
    URLNODE *urlnode = NULL, *purlnode = NULL;
    PNODE *pnode = NULL;

    if(hibase && nodeid > 0 && parentid >= 0 && urlid >= 0 
            && hibase_exists_urlnode(hibase, nodeid, parentid, urlid) < 0)
    {
        MUTEX_LOCK(hibase->mutex);
        //fprintf(stdout, "%d::nodeid:%d parentid:%d urlid:%d level:%d\n", __LINE__, nodeid, parentid, urlid, level);
        if(hibase->urlnodeio.left == 0)
        {
            HIO_MMAP(hibase->urlnodeio, ITEMPLATE, TEMPLATE_INCRE_NUM);
        }
        px = &x;
        if(FQUEUE_POP(hibase->qurlnode, int, px) == 0)
            urlnodeid = x;
        else
            urlnodeid = ++(hibase->urlnodeio.current);
        if((urlnode = HIO_MAP(hibase->urlnodeio, URLNODE))
            && urlnodeid > 0 && urlnodeid < hibase->urlnodeio.total)
        {
            urlnode[urlnodeid].status = URLNODE_STATUS_OK;
            if(level >= 0) urlnode[urlnodeid].level = level;
            urlnode[urlnodeid].id = urlnodeid;
            urlnode[urlnodeid].parentid = parentid;
            urlnode[urlnodeid].urlid = urlid;
            urlnode[urlnodeid].nodeid = nodeid;
            if(parentid >= 0 && parentid < hibase->urlnodeio.total)
            {
                purlnode = &(urlnode[parentid]);
                if(purlnode->nchilds == 0)
                {
                    purlnode->first = purlnode->last = urlnodeid;
                }
                else
                {
                    x = purlnode->last;
                    urlnode[x].next = urlnodeid;
                    urlnode[urlnodeid].prev = x;
                    purlnode->last = urlnodeid;
                }
                purlnode->nchilds++;
            }
            if(nodeid > 0 && nodeid < hibase->pnodeio.total
                && (pnode = HIO_MAP(hibase->pnodeio, PNODE)))
            {
                if(pnode[nodeid].nurlnodes == 0)
                    pnode[nodeid].urlnode_first = pnode[nodeid].urlnode_last = urlnodeid;
                else
                {
                    x = pnode[nodeid].urlnode_last;
                    urlnode[x].node_next = urlnodeid;
                    urlnode[urlnodeid].node_prev = x;
                    pnode[nodeid].urlnode_last = urlnodeid;
                }
                pnode[nodeid].nurlnodes++;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return  urlnodeid;
}

/* update urlnode level */
int hibase_update_urlnode(HIBASE *hibase, int urlnodeid, int level)
{
    URLNODE *urlnode = NULL;
    int ret = -1;

    if(hibase && urlnodeid > 0 && urlnodeid < hibase->urlnodeio.total && level >= 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if((urlnode = HIO_MAP(hibase->urlnodeio, URLNODE)))
        {
            if(level >= 0) urlnode[urlnodeid].level = level;
            ret = urlnodeid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* reset urlnode */
int hibase_reset_urlnode(HIBASE *hibase, URLNODE *urlnodes, int urlnodeid)
{
    int x = 0, z = 0, *px = NULL, nodeid = 0;
    PNODE *pnode = NULL;

    if(urlnodes && urlnodeid > 0 && urlnodeid < hibase->urlnodeio.total)
    {
        if(urlnodes[urlnodeid].nchilds > 0)
        {
            x = urlnodes[urlnodeid].first;
            while(x > 0 && x < hibase->urlnodeio.total)
            {
                if(urlnodes[x].nchilds > 0) 
                    hibase_reset_urlnode(hibase, urlnodes, x);
                x = urlnodes[x].next;
            }
        }
        if((x = urlnodes[urlnodeid].next) > 0 && x < hibase->urlnodeio.total)
        {
            urlnodes[x].prev = urlnodes[urlnodeid].prev;
        }
        if((x = urlnodes[urlnodeid].prev) > 0 && x < hibase->urlnodeio.total)
        {
            urlnodes[x].next = urlnodes[urlnodeid].next;
        }
        z = urlnodes[urlnodeid].parentid;
        if(urlnodes[z].first == urlnodeid)
            urlnodes[z].first = urlnodes[urlnodeid].next;
        if(urlnodes[z].last == urlnodeid)
            urlnodes[z].last = urlnodes[urlnodeid].prev;
        urlnodes[z].nchilds--;
        if((nodeid = urlnodes[urlnodeid].nodeid) > 0 
                && nodeid < hibase->pnodeio.total
                && (pnode = HIO_MAP(hibase->pnodeio, PNODE)))
        {
            if(pnode[nodeid].urlnode_first == urlnodeid)
                pnode[nodeid].urlnode_first = urlnodes[urlnodeid].node_next;
            if(pnode[nodeid].urlnode_last == urlnodeid)
                pnode[nodeid].urlnode_last = urlnodes[urlnodeid].node_prev;
            pnode[nodeid].nurlnodes--;
        }
        memset(&(urlnodes[urlnodeid]), 0, sizeof(URLNODE));
        px = &urlnodeid;
        FQUEUE_PUSH(hibase->qurlnode, int, px);
    }
    return urlnodeid;
}

/* delete urlnode */
int hibase_delete_urlnode(HIBASE *hibase, int urlnodeid)
{
    URLNODE *urlnode = NULL;
    int ret = -1;
    if(hibase && urlnodeid > 0 && urlnodeid < hibase->urlnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((urlnode = HIO_MAP(hibase->urlnodeio, URLNODE)))
        {
            hibase_reset_urlnode(hibase, urlnode, urlnodeid);
            ret = urlnodeid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* get urlnode */
int hibase_get_urlnode(HIBASE *hibase, int urlnodeid, URLNODE *urlnode)
{
    URLNODE *urlnodes = NULL;
    int ret = -1;

    if(hibase && urlnode && urlnodeid >= 0 && urlnodeid < hibase->urlnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((urlnodes = HIO_MAP(hibase->urlnodeio, URLNODE)))
        {
            memcpy(&(urlnodes[urlnodeid]), urlnode, sizeof(URLNODE));
            ret = urlnodeid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* get urlnode childs */
int hibase_get_urlnode_childs(HIBASE *hibase, int urlnodeid, URLNODE **childs)
{
    URLNODE *urlnodes = NULL, *p = NULL;
    int n = -1, x = 0;

    if(hibase && childs && urlnodeid >= 0 && urlnodeid < hibase->urlnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((urlnodes = HIO_MAP(hibase->urlnodeio, URLNODE))
            && urlnodes[urlnodeid].nchilds > 0 
            && (p = *childs = (URLNODE *)calloc(urlnodes[urlnodeid].nchilds, sizeof(URLNODE))))
        {
            x = urlnodes[urlnodeid].first;
            n = 0;
            while(x > 0 && n < urlnodes[urlnodeid].nchilds)
            {
                memcpy(p, &(urlnodes[x]), sizeof(URLNODE));
                x = urlnodes[x].next;
                ++p;
                ++n;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}

/* free urlnodes childs */
void hibase_free_urlnodes(URLNODE *urlnodes)
{
    if(urlnodes) free(urlnodes);
    return ;
}

/* get urlnodes with pnodeid */
int hibase_get_pnode_urlnodes(HIBASE *hibase, int nodeid, URLNODE **purlnodes)
{
    PNODE *pnodes = NULL;
    URLNODE *urlnodes = NULL, *p = NULL;
    int n = -1, x = 0;

    if(hibase && nodeid > 0 && nodeid < hibase->pnodeio.total)
    {
        MUTEX_LOCK(hibase->mutex);
        if((pnodes = HIO_MAP(hibase->pnodeio, PNODE)) && pnodes[nodeid].nurlnodes > 0
            && (x = pnodes[nodeid].urlnode_first) > 0
            && x < hibase->urlnodeio.total
            && (urlnodes = HIO_MAP(hibase->urlnodeio, URLNODE))
            && (p = *purlnodes = (URLNODE *)calloc(pnodes[nodeid].nurlnodes, sizeof(URLNODE))))
        {
            n = 0;
            while(x > 0 && n < pnodes[nodeid].nurlnodes)
            {
                memcpy(p, &(urlnodes[x]), sizeof(URLNODE));
                x = urlnodes[x].node_next;
                ++p;
                ++n;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}

/* clean */
void hibase_clean(HIBASE **phibase)
{
    if(phibase && *phibase)
    {
        TRIETAB_CLEAN((*phibase)->mdb);
        TRIETAB_CLEAN((*phibase)->mpnode);
        HIO_CLEAN((*phibase)->tableio);
        HIO_CLEAN((*phibase)->templateio);
        HIO_CLEAN((*phibase)->pnodeio);
        HIO_CLEAN((*phibase)->urlnodeio);
        FQUEUE_CLEAN((*phibase)->qpnode);
        FQUEUE_CLEAN((*phibase)->qtemplate);
        FQUEUE_CLEAN((*phibase)->qurlnode);
        MUTEX_DESTROY((*phibase)->mutex);
        //fprintf(stdout, "%s::%d::OK\n", __FILE__, __LINE__);
        free(*phibase);
        *phibase = NULL;
    }
    return ;
}

/* initialize */
HIBASE * hibase_init()
{
    HIBASE *hibase = NULL;

    if((hibase = (HIBASE *)calloc(1, sizeof(HIBASE))))
    {
        TRIETAB_INIT(hibase->mdb);
        TRIETAB_INIT(hibase->mpnode);
        MUTEX_INIT(hibase->mutex);
        hibase->set_basedir         = hibase_set_basedir;
        hibase->db_uid_exists       = hibase_db_uid_exists;
        hibase->add_table           = hibase_add_table;
        hibase->get_table           = hibase_get_table;
        hibase->rename_table        = hibase_rename_table;
        hibase->delete_table        = hibase_delete_table;
        hibase->view_table          = hibase_view_table;
        hibase->list_table          = hibase_list_table;
        hibase->view_database       = hibase_view_database;
        hibase->add_field           = hibase_add_field;
        hibase->update_field        = hibase_update_field;
        hibase->delete_field        = hibase_delete_field;
        hibase->add_pnode           = hibase_add_pnode;
        hibase->get_pnode           = hibase_get_pnode;
        hibase->get_pnode_childs    = hibase_get_pnode_childs;
        hibase->view_pnode_childs   = hibase_view_pnode_childs;
        hibase->update_pnode        = hibase_update_pnode;
        hibase->delete_pnode        = hibase_delete_pnode;
        hibase->add_template        = hibase_add_template;
        hibase->get_template        = hibase_get_template;
        hibase->update_template     = hibase_update_template;
        hibase->delete_template     = hibase_delete_template;
        hibase->view_templates      = hibase_view_templates;
        hibase->add_urlnode         = hibase_add_urlnode;
        hibase->update_urlnode      = hibase_update_urlnode;
        hibase->delete_urlnode      = hibase_delete_urlnode;
        hibase->get_urlnode         = hibase_get_urlnode;
        hibase->get_urlnode_childs  = hibase_get_urlnode_childs;
        hibase->get_pnode_urlnodes  = hibase_get_pnode_urlnodes;
        hibase->free_urlnodes       = hibase_free_urlnodes;
        hibase->clean               = hibase_clean;
    }
    return hibase;
}

#ifdef _DEBUG_HIBASE
//gcc -o hibase hibase.c utils/trie.c -Iutils -D_DEBUG_HIBASE 
int main(int argc, char **argv)
{
    HIBASE *hibase = NULL;
    ITABLE *tab = NULL, table = {0};
    ITEMPLATE *temp = NULL, template = {0};
    int i = 0, j = 0, rand = 0, x = 0, n = 0, 
        tabid = 0, fieldid = 0, type = 0, 
        flag = 0, table_num = 13;
    char name[TABLE_NAME_MAX], block[HI_BUF_SIZE];

    if((hibase = hibase_init()))
    {
        hibase->set_basedir(hibase, "/tmp/hibase");
#ifdef  _DEBUG_TABLE
        //add table
        for(i = 0; i < table_num; i++)
        {
            sprintf(name, "table_%d", i);
            if((tabid = hibase_add_table(hibase, name)) >= 0)
            {
                rand = random() % FIELD_NUM_MAX;
                for(j = 0; j < rand; j++)
                {
                    sprintf(name, "field_%d", j);
                    x = j % 4;
                    if(x == 0 )type = FTYPE_INT;
                    else if (x == 1) type = FTYPE_FLOAT;
                    else if (x == 2) type = FTYPE_TEXT;
                    else  type = FTYPE_BLOB;
                    flag = F_IS_NULL;
                    if(j % 2) flag |= F_IS_INDEX; 
                    if((fieldid = hibase_add_field(hibase, tabid, name, type, flag)) > 0)
                    {
                        if((j % 9))
                        {
                            if(j % 5)flag = 0;
                            sprintf(name, "field[%d][%d]", i, j);
                            fieldid = hibase_update_field(hibase, tabid, fieldid, name, type, flag);
                        }
                        if((j % 10))
                        {
                            fieldid = hibase_delete_field(hibase, tabid, fieldid);
                        }
                    }
                }
                if(i % 2)
                {
                    sprintf(name, "table[%d]", i);
                    hibase_rename_table(hibase, tabid, name);
                }
                memset(block, 0, HI_BUF_SIZE);
                hibase_view_table(hibase, tabid, block);
                fprintf(stdout, "%s\n", block); 
                if(i % 5)
                {
                    hibase_delete_table(hibase, tabid);
                }
            }
        }
        memset(block, 0, HI_BUF_SIZE);
        hibase_list_table(hibase, block);
        fprintf(stdout, "%s\n", block);
        //update
#endif
#ifdef _DEBUG_PNODE
        //add pnode 
        char name[1024];
        for(i = 1; i < 10000; i++)
        {
            sprintf(name, "node_%d", i);
            x = random()%i;
            if((n = hibase_add_pnode(hibase, x, name)) >= 0)
            {
                fprintf(stdout, "%d::add node:%d to node:%d\n", n, i, x);
            }
        }
        //hibase_list_pnode(hibase, 0, stdout);
        PNODE pnode = {0};
        x = random()%10000;
        fprintf(stdout, "ready updated node_%d\n", x);
        if((n = sprintf(name, "node_%d", x)) > 0 
                && (x = hibase_pnode_exists(hibase, name, n)) > 0 
                && (n = sprintf(name, "my_node_%d", x)) > 0
                && (n = hibase_update_pnode(hibase, x, name)) == x)
        {
            fprintf(stdout, "%d:: updated node:%d name:%s\n", __LINE__, n, name);
            if((x = hibase_get_pnode(hibase, n, &pnode)) >= 0)
            {
                fprintf(stdout, "%d::node:%d name:%s level:%d nchilds:%d\n",
                        __LINE__, pnode.id, pnode.name, pnode.level, pnode.nchilds);
            }
        }
        if(hibase_get_pnode(hibase, 215, &pnode) >= 0)
        {
            fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n",
                    __LINE__, pnode.id, pnode.level, pnode.nchilds);
        }
        if((n = hibase_delete_pnode(hibase, 215)) > 0)
        {
            fprintf(stdout, "%d:: delete node:%d\n", __LINE__, n);
            if((x = hibase_get_pnode(hibase, n, &pnode)) >= 0)
            {
                fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n",
                        __LINE__, pnode.id, pnode.level, pnode.nchilds);
            }
        }
        if((n = hibase_delete_pnode(hibase, 8925)) > 0)
        {
            fprintf(stdout, "%d:: delete node:%d\n", __LINE__, n);
            if((x = hibase_get_pnode(hibase, n, &pnode)) >= 0)
            {
                fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n",
                        __LINE__, pnode.id, pnode.level, pnode.nchilds);
            }
        }
        PNODE pnodes[10000];
        memset(pnodes, 0, 10000 * sizeof(PNODE));
        if((n = hibase_get_pnode_childs(hibase, 0, pnodes)) > 0)
        {
            for(i = 0; i < n; i++)
            {
                fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n", 
                        i, pnodes[i].id, pnodes[i].level, pnodes[i].nchilds);
            }
        }
        fprintf(stdout, "%d::hibase->pnode_childs_max:%d\n", __LINE__, hibase->pnode_childs_max);
        hibase_list_pnode(hibase, 0, stdout);
#endif
        hibase->clean(&hibase);
    }
    return 0;
}
//gcc -o hibase hibase.c utils/*.c -I utils -D_DEBUG_HIBASE -D_DEBUG_PNODE -lz 
#endif
