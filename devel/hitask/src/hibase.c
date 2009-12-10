#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "hibase.h"
#include "trie.h"
#include "fqueue.h"
#include "timer.h"
#include "logger.h"
#include "base64.h"
#include "hio.h"
#include "mmtree.h"
#define  HIBASE_TABLE_NAME          "hibase.table"
#define  HIBASE_TEMPLATE_NAME       "hibase.template"
#define  HIBASE_TNODE_NAME          "hibase.tnode"
#define  HIBASE_URLNODE_NAME        "hibase.urlnode"
#define  HIBASE_URI_NAME            "hibase.uri"
#define  HIBASE_MMTREE_NAME         "hibase.mmtree"
#define  HIBASE_QTNODE_NAME         "hibase.qtnode"
#define  HIBASE_QTEMPLATE_NAME      "hibase.qtemplate"
#define  HIBASE_QURLNODE_NAME       "hibase.qurlnode"
#define  HIBASE_QTASK_NAME          "hibase.qtask"
#define  HIBASE_QWAIT_NAME          "hibase.qwait"
#define  HIBASE_ISTATE_NAME         "hibase.istate"
#define  HIBASE_LOG_NAME            "hibase.log"
#define  HIBASE_RECORD_NAME         "hibase.record"
#define  HIBASE_DB_NAME             "hibase.db"
#define INCRE_STATE(hibase, io, name) (hibase->istate->io##_##name = ++(hibase->io.name))
#define DECRE_STATE(hibase, io, name) (hibase->istate->io##_##name = --(hibase->io.name))
#define UPDATE_STATE(hibase, io)                                        \
do                                                                      \
{                                                                       \
    if(hibase && hibase->istate)                                        \
    {                                                                   \
        hibase->istate->io##_left = hibase->io.left;                    \
        hibase->istate->io##_current = hibase->io.current;              \
    }                                                                   \
}while(0)
#define RESUME_STATE(hibase, io)                                           \
do                                                                      \
{                                                                       \
    if(hibase && hibase->istate)                                        \
    {                                                                   \
        hibase->io.left = hibase->istate->io##_left;                    \
        hibase->io.current = hibase->istate->io##_current;              \
    }                                                                   \
}while(0)
#define ID_IS_VALID(hibase, io, x) (x > 0 && x < hibase->io.total)
#define ID_IS_VALID2(hibase, io, x) (x >= 0 && x < hibase->io.total)
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
    ITABLE *table = NULL;
    TNODE *tnode = NULL;
    int i = 0, j = 0, n = 0, fd = -1;
    struct stat st = {0};
    void *dp = NULL;

    if(hibase && dir)
    {
        n = sprintf(hibase->basedir, "%s/", dir);
        hibase_mkdir(hibase->basedir, 0755);
        sprintf(path, "%s/%s", dir, HIBASE_LOG_NAME);
        LOGGER_INIT(hibase->logger, path);
        //resum state 
        sprintf(path, "%s/%s", dir, HIBASE_ISTATE_NAME);
        if((fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            if(fstat(fd, &st) == 0)
            {
                if(st.st_size == 0) ftruncate(fd, sizeof(ISTATE));
                if((hibase->istate = (ISTATE *)mmap(NULL, sizeof(ISTATE),
                    PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == (void *)-1)
                {
                    _EXIT_("mmap %s failed, %s\n", path, strerror(errno));
                }
            }
            else
            {
                _EXIT_("state %s failed, %s\n", path, strerror(errno));
            }
            close(fd);
        }
        else
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        p = path;
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QTASK_NAME);  
        FQUEUE_INIT(hibase->qtask, p, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QWAIT_NAME);  
        FQUEUE_INIT(hibase->qwait, p, int);
        //resume table
        p = path;
        sprintf(path, "%s%s", hibase->basedir, HIBASE_TABLE_NAME);  
        HIO_INIT(hibase->tableio, p, st, ITABLE, 1, TABLE_INCRE_NUM);
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
        HIO_INIT(hibase->templateio, p, st, ITEMPLATE, 0, TEMPLATE_INCRE_NUM);
        RESUME_STATE(hibase, templateio);
        /*
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
        */
        //resume tnode
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QTNODE_NAME);
        p = path;
        FQUEUE_INIT(hibase->qtnode, p, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_TNODE_NAME);  
        HIO_INIT(hibase->tnodeio, p, st, TNODE, 1, TNODE_INCRE_NUM);
        if(hibase->tnodeio.fd  > 0 && (tnode = HIO_MAP(hibase->tnodeio, TNODE)))
        {
            hibase->tnodeio.left = 0;
            for(i = 1; i < hibase->tnodeio.total; i++)
            {
                if(tnode[i].status && (n = strlen(tnode[i].name)) > 0)
                {
                    dp = (void *)((long)(tnode[i].uid));
                    TRIETAB_ADD(hibase->mtnode, tnode[i].name, n, dp);
                    hibase->tnodeio.current = i;
                    if(tnode[i].uid > hibase->uid_max) hibase->uid_max = tnode[i].uid;
                    //if(tnode[i].nchilds > hibase->tnode_childs_max)
                    //    hibase->tnode_childs_max = tnode[i].nchilds;
                }
                else hibase->tnodeio.left++;
            }
        }
        //resume urlnode
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QURLNODE_NAME);
        p = path;
        FQUEUE_INIT(hibase->qurlnode, p, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_URLNODE_NAME);
        HIO_INIT(hibase->urlnodeio, p, st, URLNODE, 0, URLNODE_INCRE_NUM);
        RESUME_STATE(hibase, urlnodeio);
        /*
        //HIO_MMAP(hibase->urlnodeio, URLNODE, URLNODE_INCRE_NUM);
        if(hibase->urlnodeio.fd  > 0 && (urlnode = HIO_MAP(hibase->urlnodeio, URLNODE)))
        {
            hibase->urlnodeio.left = 0;
            for(i = 1; i < hibase->urlnodeio.total; i++)
            {
                if(urlnode[i].status > 0)
                {
                    hibase->urlnodeio.current = i;
                }
                else
                {
                    hibase->urlnodeio.left++;
                }
            }
            //HIO_MUNMAP(hibase->urlnodeio);
        }*/
        //urlio
        sprintf(path, "%s%s", hibase->basedir, HIBASE_URI_NAME);
        if((hibase->uri_fd = open(path, O_CREAT|O_RDWR, 0644))  <= 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
        }
        sprintf(path, "%s%s", hibase->basedir, HIBASE_MMTREE_NAME);
        hibase->mmtree = mmtree_init(path);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_RECORD_NAME);
        p = path;
        HIO_INIT(hibase->recordio, p, st, IRECORD, 0, RECORD_INCRE_NUM);
        RESUME_STATE(hibase, recordio);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_DB_NAME);
        if((hibase->db_fd = open(path, O_CREAT|O_RDWR, 0644))  <= 0)
        {
            _EXIT_("open %s failed, %s\n", path, strerror(errno));
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
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
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
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
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
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}
/* check tnode exists */
int hibase_tnode_exists(HIBASE *hibase, int parentid, char *name, int len)
{
    int id = -1, uid = 0, tnodeid = 0;
    TNODE *tnode_map = NULL;
    void *dp = NULL;

    if(hibase)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnode_map = HIO_MAP(hibase->tnodeio, TNODE)))
        {
            TRIETAB_GET(hibase->mtnode, name, len, dp);
            if((uid = (long)dp) <= 0)
            {
                uid = ++(hibase->uid_max);
                dp = (void *)((long)uid);
                TRIETAB_ADD(hibase->mtnode, name, len, dp);
            }
            if(tnode_map[parentid].childs_rootid <= 0 || mmtree_find(hibase->mmtree, 
                        tnode_map[parentid].childs_rootid, uid, &tnodeid) <= 0)
            {
                id = uid;
            }
            else
            {
                id = -1;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* add tnode */
int hibase_add_tnode(HIBASE *hibase, int parentid, char *name)
{
    int tnodeid = -1, uid = 0, n = 0, *px = NULL, x = 0, old = 0;
    TNODE *tnode_map = NULL;

    if(hibase && name && (n = strlen(name))  > 0 && ID_IS_VALID2(hibase, tnodeio, parentid) 
            && (uid = hibase_tnode_exists(hibase, parentid, name, n)) > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->tnodeio.left == 0)
        {
            HIO_MMAP(hibase->tnodeio, TNODE, TNODE_INCRE_NUM);
        }
        px = &x;
        if(FQUEUE_POP(hibase->qtnode, int, px) == 0)
            tnodeid = x;
        else
            tnodeid = ++(hibase->tnodeio.current);
        if(ID_IS_VALID(hibase, tnodeio, tnodeid) 
                && (tnode_map = HIO_MAP(hibase->tnodeio, TNODE)))
        {
            if(tnode_map[parentid].childs_rootid == 0)
            {
                tnode_map[tnodeid].mmid = tnode_map[parentid].childs_rootid 
                = mmtree_new_tree(hibase->mmtree, uid, tnodeid);
            }
            else
            {
                tnode_map[tnodeid].mmid = mmtree_insert(hibase->mmtree, 
                        &(tnode_map[parentid].childs_rootid), uid, tnodeid, &old);
            }
            memcpy(tnode_map[tnodeid].name, name, n); 
            tnode_map[tnodeid].id = tnodeid;
            tnode_map[tnodeid].uid = uid;
            tnode_map[tnodeid].status = TNODE_STATUS_OK;
            tnode_map[tnodeid].parent = parentid;
            tnode_map[tnodeid].level = tnode_map[parentid].level + 1;
            tnode_map[parentid].nchilds++;
            tnode_map[parentid].status = TNODE_STATUS_OK;
            hibase->tnodeio.left--;
            /*
            hibase->tnode_childs_max = parent.nchilds;
            if(pwrite(hibase->tnodeio.fd, &tnode, sizeof(TNODE), 
                        (off_t)sizeof(TNODE) * (off_t) tnodeid) > 0)
            {
                pwrite(hibase->tnodeio.fd, &parent, sizeof(TNODE), 
                        (off_t)sizeof(TNODE) * (off_t) parentid);
            }*/
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    else tnodeid = -1;
    return tnodeid;
}

/* get tnode */
int hibase_get_tnode(HIBASE *hibase, int tnodeid, TNODE *ptnode)
{
    int ret = -1;
    TNODE *tnode_map = NULL;

    if(hibase && ptnode && ID_IS_VALID(hibase, tnodeio, tnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnode_map = HIO_MAP(hibase->tnodeio, TNODE)))
        {
            memcpy(ptnode, &(tnode_map[tnodeid]), sizeof(TNODE));
            ret = tnodeid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* get tnode templates */
int hibase_get_tnode_templates(HIBASE *hibase, int tnodeid, ITEMPLATE **templates)
{
    int n = -1, x = -1, id = 0, rootid = 0, data = 0;
    ITEMPLATE *template = NULL;
    TNODE *tnode_map = NULL;

    if(hibase && templates && ID_IS_VALID(hibase, tnodeio, tnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnode_map = HIO_MAP(hibase->tnodeio, TNODE)) 
                &&  tnode_map[tnodeid].status > 0 
                && (n = tnode_map[tnodeid].ntemplates) > 0
                && (rootid = tnode_map[tnodeid].templates_rootid) > 0 
                && (x = mmtree_min(hibase->mmtree, rootid, &id, &data)) > 0
                && (template = *templates = (ITEMPLATE *)calloc(n, sizeof(ITEMPLATE))))
        {
            do
            {
                if(pread(hibase->templateio.fd, template, sizeof(ITEMPLATE), 
                            (off_t)sizeof(ITEMPLATE) * (off_t)id) > 0)
                {
                    ++template;
                }
                else break; 
            }while((x = mmtree_next(hibase->mmtree, rootid, x, &id, &data)) > 0);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}

/* free templates */
void hibase_free_templates(ITEMPLATE *templates)
{
    if(templates) free(templates);
    return ;
}

/* get tnode childs */
int hibase_get_tnode_childs(HIBASE *hibase, int tnodeid, TNODE **tnodes)
{
    TNODE *tnode_map = NULL, *tnode = NULL;
    int n = 0, x = 0, rootid = 0, uid = 0, id = 0;

    if(hibase && tnodes && ID_IS_VALID2(hibase, tnodeio, tnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnode_map = HIO_MAP(hibase->tnodeio, TNODE)) 
            && tnode_map[tnodeid].status > 0 
            && (n = tnode_map[tnodeid].nchilds) > 0 
            && (rootid = tnode_map[tnodeid].childs_rootid) > 0 
            && (x = mmtree_min(hibase->mmtree, rootid, &uid, &id)) > 0
            && (tnode = *tnodes = (TNODE *)calloc(n, sizeof(TNODE))))
        {
            do
            {
                if(ID_IS_VALID(hibase, tnodeio, id))
                {
                    memcpy(tnode, &(tnode_map[id]), sizeof(TNODE));
                    ++tnode;
                }
                else break; 
            }while((x = mmtree_next(hibase->mmtree, rootid, x, &uid, &id)) > 0);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}

/* free tnode childs */
void hibase_free_tnode_childs(TNODE *childs)
{
    if(childs) free(childs);
    return ;
}

/* view tnode childs */
int hibase_view_tnode_childs(HIBASE *hibase, int tnodeid, char *block)
{
    char buf[HI_BUF_SIZE], *p = NULL, *pp = NULL;
    int x = 0, n = -1, uid = 0, rootid = 0, id = 0;
    TNODE *tnode_map = NULL;

    if(hibase && ID_IS_VALID2(hibase, tnodeio, tnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnode_map = HIO_MAP(hibase->tnodeio, TNODE)) && tnode_map[tnodeid].status > 0)
        {
            n = tnode_map[tnodeid].nchilds;
            p = buf;
            p += sprintf(p, "({'id':'%d','parent':'%d', 'nchilds':'%d', 'childs':[", 
                    tnodeid, tnode_map[tnodeid].parent, n);
            pp = p;
            if(n > 0 && (rootid = tnode_map[tnodeid].childs_rootid) > 0 
                && (x = mmtree_min(hibase->mmtree, rootid, &uid, &id)) > 0)
            {
                do
                {
                    if(ID_IS_VALID(hibase, tnodeio, id))
                    {
                        p += sprintf(p, "{'id':'%d','name':'%s','nchilds':'%d'},",
                                tnode_map[id].id, tnode_map[id].name, tnode_map[id].nchilds);
                    }
                    else break; 
                }while((x = mmtree_next(hibase->mmtree, rootid, x, &uid, &id))>0);
            }
            if(pp != p) --p;
            p += sprintf(p, "]})\r\n");
            n = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}


/* update tnode */
int hibase_update_tnode(HIBASE *hibase, int parentid, int tnodeid, char *name)
{
    int id = -1, uid = 0, rootid = -1, n = 0, old = 0;
    TNODE *tnode_map = NULL;

    if(hibase && name && (n = strlen(name))  > 0 && ID_IS_VALID2(hibase, tnodeio, parentid)
            && (uid = hibase_tnode_exists(hibase, parentid, name, n)) > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(ID_IS_VALID(hibase, tnodeio, tnodeid) 
                && (tnode_map = HIO_MAP(hibase->tnodeio, TNODE))
                && (rootid = tnode_map[parentid].childs_rootid) > 0)
        {
            mmtree_remove(hibase->mmtree, &(tnode_map[parentid].childs_rootid), 
                    tnode_map[tnodeid].mmid, &id, &old);
            tnode_map[tnodeid].mmid = mmtree_insert(hibase->mmtree, 
                    &(tnode_map[parentid].childs_rootid), uid, tnodeid, &old);
            memset(tnode_map[tnodeid].name, 0, TNODE_NAME_MAX);
            memcpy(tnode_map[tnodeid].name, name, n); 
            tnode_map[tnodeid].id = tnodeid;
            tnode_map[tnodeid].uid = uid;
            tnode_map[tnodeid].status = TNODE_STATUS_OK;
            tnode_map[tnodeid].parent = parentid;
            tnode_map[tnodeid].level = tnode_map[parentid].level + 1;
            tnode_map[parentid].status = TNODE_STATUS_OK;
            id = tnodeid;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* reset tnode */
int hibase_reset_tnode(HIBASE *hibase, int parentid, int tnodeid)
{
    int id = -1, x = 0, rootid = 0 , uid = 0, childid = 0, i = 0, *list = NULL;
    TNODE *tnode_map = NULL;

    if(hibase && ID_IS_VALID2(hibase, tnodeio, parentid) 
            && ID_IS_VALID(hibase, tnodeio, tnodeid))
    {
        fprintf(stdout, "%s::%d reset_tnode():%d parentid:%d\n", __FILE__, __LINE__, tnodeid, parentid);
        if((tnode_map =  HIO_MAP(hibase->tnodeio, TNODE)))
        {
            if(tnode_map[tnodeid].nchilds > 0 && (rootid = tnode_map[tnodeid].childs_rootid) > 0
                && (list = (int *)calloc(tnode_map[tnodeid].nchilds, sizeof(int))))
            {
                i = 0;
                x = mmtree_min(hibase->mmtree, rootid, &uid, &childid);
                do
                {
                    list[i++] = childid;
                }while((x = mmtree_next(hibase->mmtree, rootid, x, &uid, &childid)) > 0);
                for(i = 0; i < tnode_map[tnodeid].nchilds; i++)
                    hibase_reset_tnode(hibase, tnodeid, list[i]);
                free(list);
                list = NULL;
            }
            tnode_map[parentid].nchilds--;
            mmtree_remove(hibase->mmtree, &(tnode_map[parentid].childs_rootid), 
                    tnode_map[tnodeid].mmid, &uid, &childid);
            memset(&(tnode_map[tnodeid]), 0, sizeof(TNODE));
            id = tnodeid;
        }
        fprintf(stdout, "%s::%d reset_tnode():%d parentid:%d\n", __FILE__, __LINE__, tnodeid, parentid);
    }
    return id;
}

/* delete tnode */
int hibase_delete_tnode(HIBASE *hibase, int parentid, int tnodeid)
{
    int id = -1;

    if(hibase && ID_IS_VALID2(hibase, tnodeio, parentid) 
            && ID_IS_VALID(hibase, tnodeio, tnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
        id = hibase_reset_tnode(hibase, parentid, tnodeid);
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* List tnode */
int hibase_list_tnode(HIBASE *hibase, int tnodeid, FILE *fp)
{
    int i = 0, x = 0, rootid = 0, uid = 0, childid = 0;
    TNODE *tnode_map = NULL;

    if(hibase && ID_IS_VALID2(hibase, tnodeio, tnodeid)
        && (tnode_map = HIO_MAP(hibase->tnodeio, TNODE)) )
    {
        for(i = 0; i < tnode_map[tnodeid].level; i++)
        {
            fprintf(fp, "%s", "  ┆");
        }
        if(tnode_map[tnodeid].nchilds > 0)
        {
            fprintf(fp, "--+[%d]%s[%d]\n", tnode_map[tnodeid].nchilds, 
                    tnode_map[tnodeid].name, tnodeid);
        }
        else
        {
            fprintf(fp, "---%s[%d]\n", tnode_map[tnodeid].name, tnodeid);
        }
        if(tnode_map[tnodeid].nchilds > 0 && (rootid = tnode_map[tnodeid].childs_rootid) > 0 
                && (x = mmtree_min(hibase->mmtree, rootid, &uid, &childid)) > 0)
        {
            do
            {
                hibase_list_tnode(hibase, childid, fp);
            }while((x = mmtree_next(hibase->mmtree, rootid, x, &uid, &childid)) > 0);
        }
        return 0;
    }
    return -1;
}

/* add template */
int hibase_add_template(HIBASE *hibase, int tnodeid, ITEMPLATE *template)
{
    int templateid = -1, x = 0, old = 0, *px = NULL;
    TNODE *tnode = NULL;

    if(hibase && template && ID_IS_VALID(hibase, tnodeio, tnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->templateio.left == 0)
        {
            HIO_INCRE(hibase->templateio, ITEMPLATE, TEMPLATE_INCRE_NUM);
            UPDATE_STATE(hibase, templateio);
        }
        px = &x;
        if(FQUEUE_POP(hibase->qtemplate, int, px) == 0)
            templateid = x;
        else
            templateid = INCRE_STATE(hibase, templateio, current);
        if((tnode = HIO_MAP(hibase->tnodeio, TNODE)))
        {
            if(tnode[tnodeid].templates_rootid == 0)
            {
                template->mmid = tnode[tnodeid].templates_rootid 
                    = mmtree_new_tree(hibase->mmtree, templateid, templateid);
            }
            else
            {
                template->mmid = mmtree_insert(hibase->mmtree, &(tnode[tnodeid].templates_rootid), 
                        templateid, templateid, &old);
            }
            template->tnodeid = tnodeid;
            if(pwrite(hibase->templateio.fd, template, sizeof(ITEMPLATE), 
                        (off_t)sizeof(ITEMPLATE) * (off_t) templateid) > 0)
            {
                tnode[tnodeid].ntemplates++;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return templateid;
}

/* get template */
int hibase_get_template(HIBASE *hibase, int templateid, ITEMPLATE *template)
{
    int ret = -1;

    if(hibase && template && ID_IS_VALID(hibase,templateio, templateid))
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->templateio.fd > 0 && pread(hibase->templateio.fd, template, 
                    sizeof(ITEMPLATE), (off_t)sizeof(ITEMPLATE) * (off_t)templateid) > 0)
        {
            //memcpy(template, &(ptemplate[templateid]), sizeof(ITEMPLATE));
            ret = templateid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* update template */
int hibase_update_template(HIBASE *hibase, int templateid, ITEMPLATE *template)
{
    int ret = -1;

    if(hibase && ID_IS_VALID(hibase, templateio, templateid))
    {
        MUTEX_LOCK(hibase->mutex);
        template->status = TEMPLATE_STATUS_OK;
        if(hibase->templateio.fd > 0 && pwrite(hibase->templateio.fd, template, 
                    ((void *)&(template->mmid) - (void *)template), //ignore mmid
                    (off_t)sizeof(ITEMPLATE) * (off_t)templateid) > 0)
        {
            ret = templateid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* delete template */
int hibase_delete_template(HIBASE *hibase, int tnodeid, int templateid)
{
    int ret = -1, *px = NULL, id = 0, data = 0;
    ITEMPLATE template = {0};
    TNODE *tnode = NULL;

    if(hibase && ID_IS_VALID(hibase, tnodeio, tnodeid) 
            && ID_IS_VALID(hibase, templateio, templateid))
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnode = HIO_MAP(hibase->tnodeio, TNODE))
            && pread(hibase->templateio.fd, &template, sizeof(ITEMPLATE),
                 (off_t)sizeof(ITEMPLATE) * (off_t)templateid)> 0)
        {
            mmtree_remove(hibase->mmtree, &(tnode[tnodeid].templates_rootid), 
                    template.mmid, &id, &data);
            px = &templateid;
            FQUEUE_PUSH(hibase->qtemplate, int, px);
            tnode[tnodeid].ntemplates--;
            ret = templateid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* view templates */
int hibase_view_templates(HIBASE *hibase, int tnodeid, char *block)
{
    char buf[HI_BUF_SIZE], xbuf[HI_BUF_SIZE], *pattern = NULL, *p = NULL, *pp = NULL;
    int n = -1, x = 0, i = 0, rootid = 0, id = 0, data = 0;
    ITEMPLATE template = {0};
    TNODE *tnode = NULL;
    //fprintf(stdout, "%s::%d OK\n", __FILE__, __LINE__);
    if(hibase && ID_IS_VALID(hibase, tnodeio, tnodeid) && block)
    {
        MUTEX_LOCK(hibase->mutex);
        if((tnode = HIO_MAP(hibase->tnodeio, TNODE)))
        {
            p = buf;
            p += sprintf(p, "({'id':'%d', 'name':'%s', 'ntemplates':'%d','templates':[", 
                    tnodeid, tnode[tnodeid].name, tnode[tnodeid].ntemplates);
            pp = p;
            if(tnode[tnodeid].ntemplates > 0 && (rootid = tnode[tnodeid].templates_rootid) > 0 
                && (x = mmtree_min(hibase->mmtree, rootid, &id, &data)) > 0)
            {
                do
                {
                    if(pread(hibase->templateio.fd, &template, sizeof(ITEMPLATE),
                                (off_t)id * (off_t)sizeof(ITEMPLATE)) > 0)
                    {
                        if((n = strlen(template.pattern)) > 0 && (HI_BUF_SIZE > BASE64_LEN(n)))
                        {
                            base64_encode(xbuf, (unsigned char *)template.pattern, n);
                            pattern = xbuf;
                        }
                        else pattern = "";
                        //fprintf(stdout, "%d::%s\n%s\n", __LINE__, ptemplate[x].pattern, pattern);
                        p += sprintf(p, "{'id':'%d', 'tableid':'%d', 'flags':'%d', "
                                "'pattern':'%s', 'link':'%s',", id, template.tableid,
                                template.flags, pattern, template.link);
                        {
                            p += sprintf(p, "'linkmap':{'fieldid':'%d','nodeid':'%d', 'flag':'%d'},", 
                                    template.linkmap.fieldid, template.linkmap.nodeid, 
                                    template.linkmap.flag);
                        }
                        p += sprintf(p, "'url':'%s', 'nfields':'%d', 'map':[", 
                                template.url, template.nfields);
                        if(template.nfields > 0)
                        {
                            i = 0;
                            while(i < template.nfields && template.nfields < FIELD_NUM_MAX)
                            {
                                p += sprintf(p, "{'fieldid':'%d', 'nodeid':'%d', 'flag':'%d'},", 
                                        template.map[i].fieldid, template.map[i].nodeid, 
                                        template.map[i].flag);
                                i++;
                            }
                            --p;
                        }
                        p += sprintf(p, "%s","]},");
                    }
                }while((x = mmtree_next(hibase->mmtree, rootid, x, &id, &data)) > 0);
            }
            if(pp != p) --p;
            p += sprintf(p, "%s", "]})");
            n = sprintf(block, "HTTP/1.0 200\r\nContent-Type:text/html\r\n"
                "Content-Length:%ld\r\nConnection:close\r\n\r\n%s", (long)(p - buf), buf);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}
/* url => urlnodes */

int hibase_add_uri(HIBASE *hibase, int urlid, int urlnodeid)
{
    URI uri = {0};
    off_t offset = 0;
    int n = 0, mmid = -1, old = -1;

    if(hibase && urlid >= 0 && urlnodeid > 0 && hibase->uri_fd > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->istate->uri_total == 0 || hibase->istate->uri_total  <= urlid)
        {
            if(hibase->istate->uri_total == 0)
                n  = 1;
            else
            {
                n = urlid / URI_INCRE_NUM;
                if((urlid % URI_INCRE_NUM)) n++;
            }
            hibase->istate->uri_total = n * URI_INCRE_NUM;
            offset = (off_t)(hibase->istate->uri_total) * (off_t)sizeof(URI);
            if(ftruncate(hibase->uri_fd, offset) < 0) goto err;
            mmid = uri.rootid = mmtree_new_tree(hibase->mmtree, urlnodeid, urlid);
            uri.count++;
        }
        else
        {
            offset = (off_t)urlid * (off_t)sizeof(URI);
            if(pread(hibase->uri_fd, &uri, sizeof(URI), offset) <= 0) goto err;
            DEBUG_LOGGER(hibase->logger, "Ready for read uri from offset:%lld "
                    "and insert to mmtree[%d:%d] count:%d",
                    offset, urlnodeid, urlid, uri.count);
            if(uri.count == 0)
                mmid = uri.rootid = mmtree_new_tree(hibase->mmtree, urlnodeid, urlid);
            else
            {
                mmid = mmtree_insert(hibase->mmtree, &(uri.rootid), urlnodeid, urlid, &old);
                DEBUG_LOGGER(hibase->logger, "Ready for insert to mmtree[%d:%d] count:%d",
                        urlnodeid, urlid, uri.count);
            }
            if(old <= 0) uri.count++;
        }
        if(mmid > 0 && old <= 0)
        {
            offset = (off_t)urlid * (off_t)sizeof(URI);
            DEBUG_LOGGER(hibase->logger, "Ready write_back URI[%d] {%d,%d} offset:%lld total:%d", 
                    urlid, uri.rootid, uri.count, offset, hibase->istate->uri_total);
            if(pwrite(hibase->uri_fd, &uri, sizeof(URI), offset) <= 0)
                mmid = -1;
            DEBUG_LOGGER(hibase->logger, "Over write_back URI[%d] {%d,%d} offset:%lld total:%d", 
                    urlid, uri.rootid, uri.count, offset, hibase->istate->uri_total);
        }

err:
        MUTEX_UNLOCK(hibase->mutex);
    }
    return mmid;
}

/* get uris */
int hibase_get_uris(HIBASE *hibase, int urlid, int **urlnodeids)
{
    int n = 0, x = 0, id = 0, urlnodeid = 0;
    off_t offset = 0;
    URI uri = {0};

    if(hibase && urlnodeids && hibase->uri_fd > 0)
    {
        MUTEX_LOCK(hibase->mutex);   
        offset = (off_t)urlid * (off_t)sizeof(URI);
        DEBUG_LOGGER(hibase->logger, "Ready for reading uris from urlid:%d offset:%lld", urlid, offset);
        if(hibase->istate && urlid >= 0 && urlid < hibase->istate->uri_total
            && pread(hibase->uri_fd, &uri, sizeof(URI), offset) > 0 && uri.rootid > 0 
            && uri.count > 0 && (*urlnodeids = (int *)calloc(uri.count, sizeof(int))))
        {
            DEBUG_LOGGER(hibase->logger, "Ready for read uris");
            urlnodeid = 0;
            if((x = mmtree_min(hibase->mmtree, uri.rootid, &urlnodeid, &id)) > 0)
            {
                do
                {
                    *urlnodeids[n++] = urlnodeid;
                    urlnodeid = 0;
                }while((x = mmtree_next(hibase->mmtree, uri.rootid, x, &urlnodeid, &id))>0);
            }
            DEBUG_LOGGER(hibase->logger, "Over for read uris count:%d", n);
        }
        MUTEX_UNLOCK(hibase->mutex);   
    }
    return n;
}

/* delete uri */
void hibase_del_uri(HIBASE *hibase, int urlid, int urlnodeid)
{
    off_t offset = 0;
    URI uri = {0};
    int mmid = 0;

    if(hibase && hibase->uri_fd > 0)
    {
        MUTEX_LOCK(hibase->mutex);   
        offset = (off_t)urlid * (off_t)sizeof(URI);
        if(hibase->istate && urlid >= 0 && urlid < hibase->istate->uri_total
            && pread(hibase->uri_fd, &uri, sizeof(URI), offset) > 0
            && uri.rootid > 0 && uri.count > 0)
        {
            if((mmid = mmtree_find(hibase->mmtree, uri.rootid, urlnodeid, NULL)) > 0)
            {
                mmtree_remove(hibase->mmtree, &(uri.rootid), mmid, NULL, NULL);
                uri.count--;
                pwrite(hibase->uri_fd, &uri, sizeof(URI), offset);
            }
        }
        MUTEX_UNLOCK(hibase->mutex);   
    }
    return ;
}

/* free uri */
void hibase_free_uris(int *urlnodeids)
{
    if(urlnodeids) free(urlnodeids);
}

/* add urlnode */
int hibase_add_urlnode(HIBASE *hibase, int tnodeid, int parentid, int urlid, int level)
{
    int urlnodeid = -1, x = 0, old = 0, *px = NULL;
    URLNODE urlnode = {0}, parent = {0};
    TNODE *tnode =  NULL;
    off_t offset = 0;

    if(hibase && ID_IS_VALID(hibase, tnodeio, tnodeid) 
            && ID_IS_VALID2(hibase, urlnodeio, parentid) && urlid >= 0) 
    {
        MUTEX_LOCK(hibase->mutex);
        offset = (off_t)sizeof(URLNODE) * (off_t) parentid;
        if(pread(hibase->urlnodeio.fd, &parent, sizeof(URLNODE), offset) > 0
            && (tnode = HIO_MAP(hibase->tnodeio, TNODE))) 
        {
            //fprintf(stdout, "%s::%d urlid:%d parent_root:%d tnode_root:%d\n", __FILE__, __LINE__, urlid, parent.childs_rootid, tnode[tnodeid].urlnodes_rootid);
            //check urlid exists 
            if(((mmtree_find(hibase->mmtree, tnode[tnodeid].urlnodes_rootid, urlid, NULL) <= 0
                            && mmtree_find(hibase->mmtree, parent.childs_rootid, urlid, NULL) <= 0)))
            {
                DEBUG_LOGGER(hibase->logger, "Ready for add urlid:%d to tnode[%d]->nurlnodes[%d] urlnode[%d]->nchilds[%d] left:%d total:%d", urlid, tnodeid, tnode[tnodeid].nurlnodes, parentid, parent.nchilds, hibase->urlnodeio.left, hibase->urlnodeio.total);
                //fprintf(stdout, "%s::%d urlid:%d parent_root:%d tnode_root:%d\n", __FILE__, __LINE__, urlid, parent.childs_rootid, tnode[tnodeid].urlnodes_rootid);
                if(hibase->urlnodeio.left == 0)
                {
                    HIO_INCRE(hibase->urlnodeio, URLNODE, URLNODE_INCRE_NUM);
                    UPDATE_STATE(hibase, urlnodeio);
                }
                px = &x;
                if(FQTOTAL(hibase->qurlnode) > 0 && FQUEUE_POP(hibase->qurlnode, int, px) == 0)
                {
                    urlnodeid = x;
                }
                else
                {
                    urlnodeid = ++(hibase->urlnodeio.current);
                    UPDATE_STATE(hibase, urlnodeio);
                }
                if(tnode[tnodeid].urlnodes_rootid == 0)
                {
                    urlnode.tnode_mmid = tnode[tnodeid].urlnodes_rootid 
                        = mmtree_new_tree(hibase->mmtree, urlid, urlnodeid);
                }
                else
                {
                    urlnode.tnode_mmid = mmtree_insert(hibase->mmtree, 
                            &(tnode[tnodeid].urlnodes_rootid), urlid, urlnodeid, &old);
                }
                if(parent.childs_rootid == 0)
                {
                    urlnode.mmid = parent.childs_rootid = mmtree_new_tree(hibase->mmtree,
                            urlid, urlnodeid);
                }
                else
                {
                    urlnode.mmid = mmtree_insert(hibase->mmtree, &(parent.childs_rootid), 
                            urlid, urlnodeid, &old);
                }
                //fprintf(stdout, "%d::nodeid:%d parentid:%d urlid:%d level:%d id:%d\n", __LINE__, tnodeid, parentid, urlid, level, urlnodeid);
                urlnode.status = URLNODE_STATUS_OK;
                if(level >= 0) urlnode.level = level;
                /*
                   if(level > 0) 
                   {
                   px = &urlnodeid;
                   FQUEUE_PUSH(hibase->qtask, int, px);
                   }
                   */
                urlnode.id = urlnodeid;
                urlnode.parentid = parentid;
                urlnode.urlid = urlid;
                urlnode.tnodeid = tnodeid;
                if(pwrite(hibase->urlnodeio.fd, &urlnode, sizeof(URLNODE),
                            (off_t)urlnodeid * (off_t)sizeof(URLNODE))  > 0
                        && ++(parent.nchilds) > 0 && ++(tnode[tnodeid].nurlnodes) > 0
                        && pwrite(hibase->urlnodeio.fd , &parent, sizeof(URLNODE), 
                            (off_t)sizeof(URLNODE) * (off_t) parentid) > 0) 
                {                
                    hibase->urlnodeio.left--;
                    UPDATE_STATE(hibase, urlnodeio);
                }
                else
                {
                    ERROR_LOGGER(hibase->logger, "failed sync urlnode:%d urlid:%d parentid:%d",
                            urlnodeid, urlid, parentid);
                }
                DEBUG_LOGGER(hibase->logger, "Over for adding urlid:%d urlnodeid:%d to tnode[%d]->nurlnodes[%d] parent[%d]->nchilds[%d] left:%d total:%d", urlid, urlnodeid, tnodeid, tnode[tnodeid].nurlnodes, parentid, parent.nchilds, hibase->urlnodeio.left, hibase->urlnodeio.total);
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
        if(urlid >= 0 && urlnodeid >0)
        {
            DEBUG_LOGGER(hibase->logger, "Ready for add URI[%d:%d] total:%d left:%d", 
                    urlid, urlnodeid, hibase->urlnodeio.total, hibase->urlnodeio.left);
            hibase_add_uri(hibase, urlid, urlnodeid);
        }
    }
    return urlnodeid;
}

/* update urlnode level */
int hibase_update_urlnode(HIBASE *hibase, int urlnodeid, int level)
{
    URLNODE urlnode = {0};
    int ret = -1, *px = NULL;

    if(hibase && ID_IS_VALID(hibase, urlnodeio, urlnodeid) && level >= 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(level >= 0) 
        {
            pwrite(hibase->urlnodeio.fd, &level, sizeof(level), 
                    (off_t)urlnodeid * (off_t)sizeof(URLNODE) 
                    + ((char *)&(urlnode.level) - (char *)&urlnode));
        }
        if(level > 0)
        {
            px = &urlnodeid;
            FQUEUE_PUSH(hibase->qtask, int, px);
        }
        ret = urlnodeid;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* reset urlnode */
int hibase_reset_urlnode(HIBASE *hibase, int urlnodeid)
{
    int x = 0, id = 0, rootid = 0, urlid = 0, *px = NULL;
    URLNODE urlnode = {0}, parent = {0};
    TNODE *tnode = NULL;

    if(hibase && ID_IS_VALID(hibase, urlnodeio, urlnodeid))
    {
        if(pread(hibase->urlnodeio.fd, &urlnode, sizeof(URLNODE), 
                    (off_t)sizeof(URLNODE) * (off_t)urlnodeid) > 0)
        {
            /* need to reimplement */
            if(urlnode.nchilds > 0 && (rootid = urlnode.childs_rootid) > 0 
                    && (x = mmtree_min(hibase->mmtree, rootid, &urlid, &id)) > 0)
            {
                do
                {
                    hibase_reset_urlnode(hibase, id);
                }while((x = mmtree_next(hibase->mmtree, rootid, x, &urlid, &id))>0);
            }
            if(pread(hibase->urlnodeio.fd, &parent, sizeof(URLNODE), 
                        (off_t)sizeof(URLNODE) * (off_t)urlnode.parentid) > 0)
            {
                mmtree_remove(hibase->mmtree, &(parent.childs_rootid), urlnode.mmid,&urlid, &id);
                parent.nchilds--;
                pwrite(hibase->urlnodeio.fd, &parent, sizeof(URLNODE), 
                        (off_t)sizeof(URLNODE) * (off_t)urlnode.parentid);
            }
            if(ID_IS_VALID(hibase, tnodeio, urlnode.tnodeid) 
                    && (tnode = HIO_MAP(hibase->tnodeio, TNODE)))
            {
                if((rootid = tnode[urlnode.tnodeid].urlnodes_rootid) > 0)
                    mmtree_remove(hibase->mmtree, &(tnode[urlnode.tnodeid].urlnodes_rootid), 
                            urlnode.tnode_mmid, &urlid,&id);
                tnode[urlnode.tnodeid].nurlnodes--;
            }
            px = &urlnodeid;
            FQUEUE_PUSH(hibase->qurlnode, int, px);
            hibase->urlnodeio.left++;
            UPDATE_STATE(hibase, urlnodeio);
        }
    }
    return urlnodeid;
}

/* delete urlnode */
int hibase_delete_urlnode(HIBASE *hibase, int urlnodeid)
{
    int ret = -1;

    if(hibase && ID_IS_VALID(hibase, urlnodeio, urlnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
        ret = hibase_reset_urlnode(hibase, urlnodeid);
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* get urlnode */
int hibase_get_urlnode(HIBASE *hibase, int urlnodeid, URLNODE *urlnode)
{
    int ret = -1;

    DEBUG_LOGGER(hibase->logger, "Ready for get_urlnode(%d) urlnode:%p total:%d", 
            urlnodeid, urlnode, hibase->urlnodeio.total)
    if(hibase && urlnode && ID_IS_VALID2(hibase, urlnodeio, urlnodeid))
    {
        DEBUG_LOGGER(hibase->logger, "Ready for read urlnode:%d", urlnodeid)
        MUTEX_LOCK(hibase->mutex);
        if(pread(hibase->urlnodeio.fd, urlnode, sizeof(URLNODE), 
                    (off_t)sizeof(URLNODE) * (off_t) urlnodeid) > 0)
        {
            DEBUG_LOGGER(hibase->logger, "over for read urlnode:%d", urlnodeid)
            ret = urlnodeid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* find tnode from parents urlnodes */
int hibase_find_tnode_from_parents(HIBASE *hibase, int parentid, int tnodeid)
{
    URLNODE urlnode = {0};
    int urlnodeid = 0;

    if(hibase && ID_IS_VALID(hibase, urlnodeio, parentid) && tnodeid > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        urlnodeid = parentid;
        while(urlnodeid > 0)
        {
            if(pread(hibase->urlnodeio.fd, &urlnode, sizeof(URLNODE), 
                        (off_t)sizeof(URLNODE) * (off_t) urlnodeid) > 0)
            {
                if(urlnode.tnodeid == tnodeid) break;
                else urlnodeid = urlnode.parentid;
            }
            else
            {
                urlnodeid = -1; 
                break;
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return urlnodeid;
}

/* get urlnode childs */
int hibase_get_urlnode_childs(HIBASE *hibase, int urlnodeid, URLNODE **childs, 
        int *total, int from, int count)
{
    int n = -1, x = 0, i = 0, urlid = 0, rootid = 0, id = 0, to = 0;
    URLNODE urlnode = {0}, *p = NULL;

    if(hibase && childs && ID_IS_VALID2(hibase, urlnodeio, urlnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
        DEBUG_LOGGER(hibase->logger, "Ready for reading urlnode:%d childs", urlnodeid)
        if(pread(hibase->urlnodeio.fd, &urlnode, sizeof(URLNODE), 
                    (off_t)sizeof(URLNODE) * (off_t)urlnodeid) > 0
            && (*total = urlnode.nchilds) > 0 && from >= 0 && from < *total 
            && (rootid = urlnode.childs_rootid) > 0)
        {
            DEBUG_LOGGER(hibase->logger, "Ready for reading urlnode[%d]->childs[%d] from:%d ", urlnodeid, urlnode.nchilds, from);
            to = from + count;
            if(to > *total) to = *total;
            if((n = (to - from)) > 0 && (x = mmtree_min(hibase->mmtree, rootid, &urlid, &id)) > 0
                && (p = *childs = (URLNODE *)calloc(n, sizeof(URLNODE))))
            {
                DEBUG_LOGGER(hibase->logger, "from:%d count:%d total:%d to:%d", from, count, *total, to);
                i = 0;
                do
                {
                    DEBUG_LOGGER(hibase->logger, "x:%d id:%d urlid:%d", x, id, urlid);
                    if(id < 0 || i >= to)break;
                    if(i < from){++i;continue;}
                    if(id > 0 && pread(hibase->urlnodeio.fd, p, sizeof(URLNODE),
                            (off_t)sizeof(URLNODE)*(off_t)id) > 0)
                    { 
                        DEBUG_LOGGER(hibase->logger, "i:%d level:%d parent:%d tnodeid:%d, urlid:%d nchilds:%d, childs_rootid:%d tnode_mmid:%d mmid:%d", i, p->level, p->parentid, p->tnodeid, p->urlid, p->nchilds, p->childs_rootid, p->tnode_mmid, p->mmid);
                        ++p;
                    }
                    i++;
                }while((x = mmtree_next(hibase->mmtree, rootid, x, &urlid, &id)) > 0);
                DEBUG_LOGGER(hibase->logger, "from:%d count:%d total:%d to:%d i:%d n:%d", from, count, *total, to, i, n);
            }
        }
        DEBUG_LOGGER(hibase->logger, "Over for reading urlnode:%d childs", urlnodeid)
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

/* get urlnodes with tnodeid */
int hibase_get_tnode_urlnodes(HIBASE *hibase, int tnodeid, URLNODE **urlnodes, 
        int *total, int from, int count)
{
    int n = -1, i = 0, x = 0, urlid = 0, rootid = 0, id = 0, to = 0;
    TNODE *tnode = NULL;
    URLNODE *p = NULL;

    if(hibase && ID_IS_VALID(hibase, tnodeio, tnodeid))
    {
        MUTEX_LOCK(hibase->mutex);
            DEBUG_LOGGER(hibase->logger, "Ready for reading tnode[%d]->urlnodes[%d] from:%d ", tnodeid, *total, from);
        if((tnode = HIO_MAP(hibase->tnodeio, TNODE))
            && tnode[tnodeid].nurlnodes > 0 && (rootid = tnode[tnodeid].urlnodes_rootid) > 0
            && (*total = tnode[tnodeid].nurlnodes) > 0 && from >= 0 && from < *total)
        {
            DEBUG_LOGGER(hibase->logger, "Ready for reading tnode[%d]->urlnodes[%d] from:%d ", tnodeid, tnode[tnodeid].nurlnodes, from);
            to = from + count;
            if(to > *total) to = *total;
            if((n = (to - from)) > 0 && (x = mmtree_min(hibase->mmtree, rootid, &urlid, &id)) > 0
                && (p = *urlnodes = (URLNODE *)calloc(n, sizeof(URLNODE))))
            {
                DEBUG_LOGGER(hibase->logger, "from:%d count:%d total:%d to:%d", from, count, *total, to);
                i = 0;
                do
                {
                    DEBUG_LOGGER(hibase->logger, "x:%d id:%d urlid:%d", x, id, urlid);
                    if(id < 0 || i >= to)break;
                    if(i < from){++i;continue;}
                    if(id > 0 && pread(hibase->urlnodeio.fd, p, sizeof(URLNODE),
                            (off_t)sizeof(URLNODE)*(off_t)id) > 0)
                    { 
                        DEBUG_LOGGER(hibase->logger, "i:%d level:%d parent:%d tnodeid:%d, urlid:%d nchilds:%d, childs_rootid:%d tnode_mmid:%d mmid:%d", i, p->level, p->parentid, p->tnodeid, p->urlid, p->nchilds, p->childs_rootid, p->tnode_mmid, p->mmid);
                        ++p;
                    }
                    i++;
                }while((x = mmtree_next(hibase->mmtree, rootid, x, &urlid, &id)) > 0);
                DEBUG_LOGGER(hibase->logger, "from:%d count:%d total:%d to:%d i:%d n:%d", from, count, *total, to, i, n);
            }
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}

/* pop urlnodeid for download task */
int hibase_pop_urlnode(HIBASE *hibase, URLNODE *urlnode)
{
    int urlnodeid = -1, x = 0, *px = NULL;

    if(hibase && hibase->qtask && hibase->istate && urlnode)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->urlnodeio.total > 0 && hibase->urlnodeio.current > 0) 
        {
            /*
            fprintf(stdout, "%s::%d qtotal:%d urlnodeid:%d current:%d total:%d task_current:%d\n",
                    __FILE__, __LINE__,  FQTOTAL(hibase->qtask), 
                    hibase->istate->urlnode_task_current, 
                    hibase->urlnodeio.current, hibase->urlnodeio.total,
                     hibase->istate->urlnode_task_current);
               px = &x;
               if(FQTOTAL(hibase->qtask)>0 && FQUEUE_POP(hibase->qtask, int, px) == 0)
               {
               urlnodeid = x;
               pread(hibase->urlnodeio.fd, urlnode, sizeof(urlnode), 
               (off_t)sizeof(urlnode) * (off_t)urlnodeid);
            //memcpy(urlnode, &(purlnode[urlnodeid]), sizeof(URLNODE));
            }
            */
            while(FQTOTAL(hibase->qtask)> 0 || (hibase->urlnodeio.current < hibase->urlnodeio.total
                        && hibase->istate->urlnode_task_current <= hibase->urlnodeio.current))
            {
                x = -1;
                px = &x;
                if(FQUEUE_POP(hibase->qtask, int, px) == 0)
                {
                    urlnodeid = x;
                }
                else if(hibase->istate->urlnode_task_current <= hibase->urlnodeio.current)
                {
                    urlnodeid = hibase->istate->urlnode_task_current++;
                }
                //fprintf(stdout, "%s::%d::urlnodeid:%d current:%d\n", __FILE__, __LINE__, urlnodeid, hibase->urlnodeio.current);
                if(urlnodeid > 0 && pread(hibase->urlnodeio.fd, urlnode, sizeof(URLNODE), 
                    (off_t)sizeof(URLNODE) * (off_t)urlnodeid) > 0)
                {
                    if(urlnode->level > 0 && urlnodeid != x) 
                    {
                        urlnodeid = -1;
                        continue;
                    }
                    break;
                }
                else urlnodeid = -1;
                //fprintf(stdout, "%s::%d::urlnodeid:%d\n", __FILE__, __LINE__, urlnodeid);
            }
            //fprintf(stdout, "%s::%d::urlnodeid:%d\n", __FILE__, __LINE__, urlnodeid);
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return urlnodeid;
}

/* push urlnodeid from wait queue */
int hibase_push_task_urlnodeid(HIBASE *hibase, int urlnodeid)
{
    int id = -1, *px = NULL;

    if(hibase && urlnodeid > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        px = &urlnodeid;
        if(hibase->qwait)
        {
            FQUEUE_PUSH(hibase->qwait, int, px);
            id = urlnodeid;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* pop urlnodeid from wait queue */
int hibase_pop_task_urlnodeid(HIBASE *hibase)
{
    int urlnodeid = -1, x = 0, *px = NULL;

    if(hibase)
    {
        MUTEX_LOCK(hibase->mutex);
        px = &x;
        if(hibase->qwait && FQTOTAL(hibase->qwait) > 0
            && FQUEUE_POP(hibase->qwait, int, px) == 0)
        {
            urlnodeid = x;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return urlnodeid;
}

/* update record */
int hibase_update_record(HIBASE *hibase, int parentid, int urlnodeid, PRES *pres, 
        int tableid, char *block, int nblock)
{
    int id = -1, i = 0, n = 0;
    struct stat st = {0};
    IRECORD record = {0};
    URLNODE urlnode = {0};
    off_t offset  = 0;

    if(hibase && ID_IS_VALID(hibase, urlnodeio, urlnodeid) && pres && block && nblock > 0)
    {
        MUTEX_LOCK(hibase->mutex);
        offset = (off_t)urlnodeid * (off_t)sizeof(URLNODE);
        if(pread(hibase->urlnodeio.fd, &urlnode, sizeof(URLNODE), offset) <= 0) goto err;
        if((id = urlnode.recordid) <= 0)
        {

            if(hibase->recordio.left == 0)
            {
                HIO_INCRE(hibase->recordio, IRECORD, RECORD_INCRE_NUM);
                UPDATE_STATE(hibase, recordio);
            }
            else
            {
                id = ++(hibase->recordio.current);
                UPDATE_STATE(hibase, recordio);
            }
        }
        else
        {
            offset = (off_t)id * (off_t)sizeof(IRECORD); 
            if(pread(hibase->recordio.fd, &record, sizeof(IRECORD), offset) < 0) goto err;
        }
        //write block
        if(fstat(hibase->db_fd, &st) == 0 && pwrite(hibase->db_fd, block, nblock, st.st_size) > 0)
        {
            //rebuild record
            n = 0;
            for(i = 0; i < FIELD_NUM_MAX; i++)
            {
                if(pres[i].end > 0)
                {
                    n++;
                    if(record.records[i].length > 0)
                        record.length -= record.records[i].length;
                    else 
                        record.nrecords++;
                    record.records[i].length = pres[i].end - pres[i].start;
                    record.records[i].offset = st.st_size + (off_t)pres[i].start;
                    record.length += record.records[i].length;
                }
            }
            if(n > 0)
            {
                record.parentid = parentid;
                record.tableid = tableid;
                offset = (off_t)id * (off_t)sizeof(IRECORD);
                pwrite(hibase->recordio.fd, &record, sizeof(IRECORD), offset);
            }
            //wirte back uri
            if(urlnode.recordid <= 0)
            {
                urlnode.recordid = id;
                pwrite(hibase->urlnodeio.fd, &urlnode, sizeof(URLNODE), 
                        (off_t)urlnodeid * (off_t)sizeof(URLNODE));
            }
        }
err:
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* view record */
int hibase_view_record(HIBASE *hibase, int recordid, int urlnodeid, char **block)
{
    char *p = NULL, *pp = NULL, buf[HI_BUF_SIZE], *data = NULL, *s = NULL;
    int n = -1, i = 0, id = -1, tabid = 0;
    IRECORD record = {0};
    ITABLE *tab = NULL;
    URLNODE urlnode = {0};

    if(hibase && block && (recordid > 0 || urlnodeid > 0))
    {
        MUTEX_LOCK(hibase->mutex);
        if(recordid >= 0) id = recordid;
        else if(urlnodeid >= 0 && pread(hibase->urlnodeio.fd, &urlnode, sizeof(URLNODE), 
                    (off_t)urlnodeid * (off_t)sizeof(URLNODE)) && urlnode.recordid > 0) 
            id = urlnode.recordid;
        else goto err;
        if(pread(hibase->recordio.fd, &record,sizeof(IRECORD),(off_t)id*(off_t)sizeof(IRECORD))>0 
                && record.length > 0 && record.nrecords > 0 && (tabid = record.tableid) >= 0
                && (n = BASE64_LEN(record.length) + FIELD_NUM_MAX * FIELD_NAME_MAX * 2) > 0
                && tabid < hibase->tableio.total && (tab = (ITABLE *)(hibase->tableio.map))
                && tab != (void *)-1 && (p =*block=(char *)calloc(1, n)))
        {
            p += sprintf(p, "({'id':'%d', 'nrecords':'%d', 'records':{", id, record.nrecords);
            pp = p;
            for(i = 0; i < FIELD_NUM_MAX; i++)
            {
                if(record.records[i].length > 0)
                {
                    if(record.records[i].length >= HI_BUF_SIZE)
                    {
                        s = data = (char *)calloc(1, record.records[i].length + 1);
                    }
                    else 
                        s = buf;
                    if(buf == s) memset(s, 0, HI_BUF_SIZE);
                    p += sprintf(p, "'%d':{'name':'%s', 'data':'", i, tab[tabid].fields[i].name);
                    if(s && pread(hibase->db_fd, s, record.records[i].length, 
                                record.records[i].offset)>0)
                    {
                        p += base64_encode(p, (unsigned char *)s, record.records[i].length);
                    }
                    p += sprintf(p, "%s", "'},");
                    if(data){free(data); data = NULL;}
                }
            }
            if(p != pp) --p;
            p += sprintf(p, "}})");
            n = p - *block;
        }
err:
        MUTEX_UNLOCK(hibase->mutex);
    }
    return n;
}

/* free record */
void hibase_free_record(void *record)
{
    if(record) free(record);
}

/* clean */
void hibase_clean(HIBASE **phibase)
{
    if(phibase && *phibase)
    {
        TRIETAB_CLEAN((*phibase)->mdb);
        TRIETAB_CLEAN((*phibase)->mtnode);
        mmtree_close((*phibase)->mmtree);
        HIO_CLEAN((*phibase)->tableio);
        HIO_CLEAN((*phibase)->templateio);
        HIO_CLEAN((*phibase)->tnodeio);
        HIO_CLEAN((*phibase)->urlnodeio);
        HIO_CLEAN((*phibase)->recordio);
        if((*phibase)->uri_fd > 0) close((*phibase)->uri_fd);
        if((*phibase)->db_fd > 0) close((*phibase)->db_fd);
        //HIO_CLEAN((*phibase)->uriio);
        FQUEUE_CLEAN((*phibase)->qtnode);
        FQUEUE_CLEAN((*phibase)->qtemplate);
        FQUEUE_CLEAN((*phibase)->qurlnode);
        FQUEUE_CLEAN((*phibase)->qtask);
        FQUEUE_CLEAN((*phibase)->qwait);
        if((*phibase)->istate) 
        {
            msync((*phibase)->istate, sizeof(ISTATE), MS_SYNC);
            munmap((*phibase)->istate, sizeof(ISTATE));
        }
        MUTEX_DESTROY((*phibase)->mutex);
        LOGGER_CLEAN((*phibase)->logger);
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
        TRIETAB_INIT(hibase->mtnode);
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
        hibase->add_tnode           = hibase_add_tnode;
        hibase->get_tnode           = hibase_get_tnode;
        hibase->get_tnode_templates = hibase_get_tnode_templates;
        hibase->free_templates      = hibase_free_templates;
        hibase->get_tnode_childs    = hibase_get_tnode_childs;
        hibase->free_tnode_childs   = hibase_free_tnode_childs;
        hibase->view_tnode_childs   = hibase_view_tnode_childs;
        hibase->update_tnode        = hibase_update_tnode;
        hibase->delete_tnode        = hibase_delete_tnode;
        hibase->add_template        = hibase_add_template;
        hibase->get_template        = hibase_get_template;
        hibase->update_template     = hibase_update_template;
        hibase->delete_template     = hibase_delete_template;
        hibase->view_templates      = hibase_view_templates;
        hibase->add_uri             = hibase_add_uri;
        hibase->get_uris            = hibase_get_uris;
        hibase->del_uri             = hibase_del_uri;
        hibase->free_uris           = hibase_free_uris;
        hibase->add_urlnode         = hibase_add_urlnode;
        hibase->update_urlnode      = hibase_update_urlnode;
        hibase->delete_urlnode      = hibase_delete_urlnode;
        hibase->find_tnode_from_parents  = hibase_find_tnode_from_parents;
        hibase->get_urlnode         = hibase_get_urlnode;
        hibase->get_urlnode_childs  = hibase_get_urlnode_childs;
        hibase->get_tnode_urlnodes  = hibase_get_tnode_urlnodes;
        hibase->free_urlnodes       = hibase_free_urlnodes;
        hibase->pop_urlnode         = hibase_pop_urlnode;
        hibase->push_task_urlnodeid = hibase_push_task_urlnodeid;
        hibase->pop_task_urlnodeid  = hibase_pop_task_urlnodeid;
        hibase->update_record       = hibase_update_record;
        hibase->view_record         = hibase_view_record;
        hibase->free_record         = hibase_free_record;
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
#ifdef _DEBUG_TNODE
        //add tnode 
        char name[1024];
        for(i = 1; i < 10000; i++)
        {
            sprintf(name, "node_%d", i);
            x = random()%i;
            if((n = hibase_add_tnode(hibase, x, name)) >= 0)
            {
                fprintf(stdout, "%d::add node:%d to node:%d\n", n, i, x);
            }
        }
        hibase_list_tnode(hibase, 0, stdout);
        TNODE tnode = {0};
        /*
        x = random()%10000;
        fprintf(stdout, "ready updated node_%d\n", x);
        if((n = sprintf(name, "node_%d", x)) > 0 
                && (x = hibase_tnode_exists(hibase, 0, name, n)) > 0 
                && (n = sprintf(name, "my_node_%d", x)) > 0
                && (n = hibase_update_tnode(hibase, 0, x, name)) == x)
        {
            fprintf(stdout, "%d:: updated node:%d name:%s\n", __LINE__, n, name);
            if((x = hibase_get_tnode(hibase, n, &tnode)) >= 0)
            {
                fprintf(stdout, "%d::node:%d name:%s level:%d nchilds:%d\n",
                        __LINE__, tnode.id, tnode.name, tnode.level, tnode.nchilds);
            }
        }
        hibase_list_tnode(hibase, 0, stdout);
        */
        if(hibase_get_tnode(hibase, 215, &tnode) >= 0)
        {
            fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n",
                    __LINE__, tnode.id, tnode.level, tnode.nchilds);
            if((n = hibase_delete_tnode(hibase, tnode.parent, 215)) > 0)
            {
                fprintf(stdout, "%d:: delete node:%d\n", __LINE__, n);
                if((x = hibase_get_tnode(hibase, n, &tnode)) >= 0)
                {
                    fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n",
                            __LINE__, tnode.id, tnode.level, tnode.nchilds);
                }
            }
        }
        hibase_list_tnode(hibase, 0, stdout);
        //TNODE tnodes[10000];
        //memset(tnodes, 0, 10000 * sizeof(TNODE));
        TNODE *tnodes = NULL;
        if((n = hibase_get_tnode_childs(hibase, 0, &tnodes)) > 0)
        {
            for(i = 0; i < n; i++)
            {
                fprintf(stdout, "%d::node:%d level:%d nchilds:%d\n", 
                      i, tnodes[i].id, tnodes[i].level, tnodes[i].nchilds);
            }
            hibase_free_tnode_childs(tnodes);
        }
        fprintf(stdout, "%d::hibase->tnode_childs_max:%d\n", __LINE__, hibase->tnode_childs_max);
        //hibase_list_tnode(hibase, 0, stdout);
#endif
#ifdef _DEBUG_URLNODE
        int urlid = 0, urlnodeid = 0, itotal = 0, jtotal = 0;
        for(i = 1; i < 100; i++)
        {
            if((urlnodeid = hibase_add_urlnode(hibase, 1, 0, i, 0)) > 0)
            {
                itotal++;
                for(j = 0; j < 10000; j++)
                {
                    urlid = random()%(10000 * i);
                    if((x = hibase_add_urlnode(hibase, 2, urlnodeid, urlid, 0)) > 0) jtotal++;
                }
            }
        }
        fprintf(stdout, "%s::%d itotal:%d jtotal:%d\n", __FILE__, __LINE__, itotal, jtotal);
        URLNODE *urlnodes = NULL;
        for(i = 0; i < 100; i++)
        {
            if((n = hibase_get_urlnode_childs(hibase, i, &urlnodes, &total, from, 100)) > 0)
            {
                for(i = 0; i < n; i++)
                {
                    fprintf(stdout, "%s::%d i:%d urlnode:%d tnodeid:%d urlid:%d parent:%d nchilds:%d\n",
                            __FILE__, __LINE__, i, urlnodes[i].id, urlnodes[i].tnodeid, 
                            urlnodes[i].urlid, urlnodes[i].parentid, urlnodes[i].nchilds);
                }
                fprintf(stdout, "urlnode[%d]->nchilds:%d\n", 0, n);
                hibase_free_urlnodes(urlnodes);
            }
        }
        int total = 0, from = 0, count = 1000000;
        if((n = hibase_get_tnode_urlnodes(hibase, 1, &urlnodes, &total, from, count)) > 0)
        {
            for(i = 0; i < n; i++)
            {
                fprintf(stdout, "%s::%d i:%d urlnode:%d tnodeid:%d urlid:%d parent:%d nchilds:%d\n",
                        __FILE__, __LINE__, i, urlnodes[i].id, urlnodes[i].tnodeid, 
                        urlnodes[i].urlid, urlnodes[i].parentid, urlnodes[i].nchilds);
            }
            fprintf(stdout, "urlnode[%d]->nchilds:%d total:%d\n", 0, n, total);
            hibase_free_urlnodes(urlnodes);
        }
        if((n = hibase_get_tnode_urlnodes(hibase, 2, &urlnodes, &total, from, count)) > 0)
        {
            for(i = 0; i < n; i++)
            {
                fprintf(stdout, "%s::%d i:%d urlnode:%d tnodeid:%d urlid:%d parent:%d nchilds:%d\n",
                        __FILE__, __LINE__, i, urlnodes[i].id, urlnodes[i].tnodeid, 
                        urlnodes[i].urlid, urlnodes[i].parentid, urlnodes[i].nchilds);
            }
            fprintf(stdout, "urlnode[%d]->nchilds:%d total:%d\n", 0, n, total);
            hibase_free_urlnodes(urlnodes);
        }
#endif
        hibase->clean(&hibase);
    }
    return 0;
}
//gcc -o hibase hibase.c utils/*.c -I utils -D_DEBUG_HIBASE -D_DEBUG_TNODE -lz 
#endif
