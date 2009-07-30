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
#define  HIBASE_TABLE_NAME   		"hibase.table"
#define  HIBASE_TEMPLATE_NAME 		"hibase.temp"
#define  HIBASE_PNODE_NAME 		    "hibase.pnode"
#define  HIBASE_QPNODE_NAME 		"hibase.qpnode"
#define _EXIT_(format...)                                                               \
do                                                                                      \
{                                                                                       \
    fprintf(stderr, "%s::%d ", __FILE__, __LINE__);                                     \
    fprintf(stderr, format);                                                            \
    _exit(-1);                                                                          \
}while(0)

#define _MMAP_(io, st, type, incre_num)                                                 \
do                                                                                      \
{                                                                                       \
    if(io.fd > 0 && incre_num > 0)                                                      \
    {                                                                                   \
        if(io.map && io.size > 0)                                                       \
        {                                                                               \
            msync(io.map, io.size, MS_SYNC);                                            \
            munmap(io.map, io.size);                                                    \
        }                                                                               \
        else                                                                            \
        {                                                                               \
            if(fstat(io.fd, &st) != 0)                                                  \
            {                                                                           \
                _EXIT_("fstat(%d) failed, %s\n", io.fd, strerror(errno));               \
            }                                                                           \
            io.size = st.st_size;                                                       \
        }                                                                               \
        if(io.size == 0 || io.map)                                                      \
        {                                                                               \
            io.size += ((off_t)sizeof(type) * (off_t)incre_num);                        \
            ftruncate(io.fd, io.size);                                                  \
            io.left += incre_num;                                                       \
        }                                                                               \
        io.total = io.size/(off_t)sizeof(type);                                         \
        if((io.map = mmap(NULL, io.size, PROT_READ|PROT_WRITE, MAP_SHARED,              \
                        io.fd, 0)) == (void *)-1)                                       \
        {                                                                               \
            _EXIT_("mmap %d size:%lld failed, %s\n", io.fd,                             \
                    (long long int)io.size, strerror(errno));                           \
        }                                                                               \
    }                                                                                   \
}while(0)
#define _MUNMAP_(mp, size)                                                              \
do                                                                                      \
{                                                                                       \
    if(mp && size > 0)                                                                  \
    {                                                                                   \
        msync(mp, size, MS_SYNC);                                                       \
        munmap(mp, size);                                                               \
        mp = NULL;                                                                      \
    }                                                                                   \
}while(0)
static char *table_data_types[] = {"null", "int", "float", "null", "text"};
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
    char path[HIBASE_PATH_MAX];
    ITEMPLATE *template = NULL;
    ITABLE *table = NULL;
    PNODE *pnode = NULL;
    struct stat st = {0};
    int i = 0, n = 0;
    void *dp = NULL;

    if(hibase && dir)
    {
        n = sprintf(hibase->basedir, "%s/", dir);
        hibase_mkdir(hibase->basedir, 0755);
        //resume table
        sprintf(path, "%s%s", hibase->basedir, HIBASE_TABLE_NAME);	
        if((hibase->tableio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            _MMAP_(hibase->tableio, st, ITABLE, TABLE_INCRE_NUM); 
            table = (ITABLE *)hibase->tableio.map;
            hibase->tableio.left = 0;
            for(i = 0; i < hibase->tableio.total; i++)
            {
                if(table[i].status == TAB_STATUS_OK && table[i].nfields > 0 
                        && (n = strlen(table[i].name)) > 0)
                {
                    dp = (void *)((long)(i + 1));
                    TRIETAB_ADD(hibase->mtable, table[i].name, n, dp);
                    hibase->tableio.current = i;
                }
                else hibase->tableio.left++;
            }
        }
        //resume template 
        sprintf(path, "%s%s", hibase->basedir, HIBASE_TEMPLATE_NAME);	
        if((hibase->templateio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            _MMAP_(hibase->templateio, st, ITEMPLATE, TEMPLATE_INCRE_NUM); 
            template = (ITEMPLATE *)hibase->templateio.map;
            hibase->templateio.left = 0;
            for(i = 0; i < hibase->templateio.total; i++)
            {
                if(template[i].status == TEMP_STATUS_OK && template[i].npatterns > 0 
                        && (n = strlen(template[i].name)) > 0)
                {
                    dp = (void *)((long)(i + 1));
                    TRIETAB_ADD(hibase->mtemplate, template[i].name, n, dp);
                    hibase->templateio.current = i;
                }
                else hibase->templateio.left++;
            }
        }
        //resume pnode
        sprintf(path, "%s%s", hibase->basedir, HIBASE_QPNODE_NAME);
        FQUEUE_INIT(hibase->qpnode, path, int);
        sprintf(path, "%s%s", hibase->basedir, HIBASE_PNODE_NAME);	
        if((hibase->pnodeio.fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
        {
            _MMAP_(hibase->pnodeio, st, PNODE, PNODE_INCRE_NUM); 
            pnode = (PNODE *)hibase->pnodeio.map;
            hibase->pnodeio.left = 0;
            for(i = 1; i < hibase->pnodeio.total; i++)
            {
                if(pnode[i].status && (n = strlen(pnode[i].name)) > 0)
                {
                    dp = (void *)((long)(i + 1));
                    TRIETAB_ADD(hibase->mpnode, pnode[i].name, n, dp);
                    hibase->pnodeio.current = i;
                }
                else hibase->pnodeio.left++;
            }
        }

    }
    return -1;
}

/* check table exists */
int hibase_table_exists(HIBASE *hibase, char *table_name, int len)
{
    void *dp = NULL;
    int tableid = -1;

    if(hibase && table_name && len > 0 && hibase->mtable)
    {
        MUTEX_LOCK(hibase->mutex);
        TRIETAB_GET(hibase->mtable, table_name, len, dp);
        tableid = ((long)dp - 1);
        MUTEX_UNLOCK(hibase->mutex);
    }
    return tableid;
}

/* add table */
int hibase_add_table(HIBASE *hibase, ITABLE *table)
{
    int tableid = -1, i = 0, n = 0;
    struct stat st = {0};
    ITABLE *tab = NULL;
    void *dp = NULL;

    if(hibase && table && (n = strlen(table->name))  > 0 
            && (tableid = hibase_table_exists(hibase, table->name, n)) < 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->tableio.left == 0){_MMAP_(hibase->tableio, st, ITABLE, TABLE_INCRE_NUM);}        
        if(hibase->tableio.left > 0 && (tab = (ITABLE *)hibase->tableio.map) 
                && tab != (ITABLE *)-1)
        {
            for(i = 0; i < hibase->tableio.total; i++)
            {
                if(tab[i].status != TAB_STATUS_OK)
                {
                    table->status = TAB_STATUS_OK;
                    memcpy(&(tab[i]), table, sizeof(ITABLE));
                    tableid = i;
                    dp = (void *)((long)(tableid + 1));
                    TRIETAB_ADD(hibase->mtable, table->name, n, dp);
                    hibase->tableio.left--;
                    break;
                }
            }
        }
        
        MUTEX_UNLOCK(hibase->mutex);
    }
    else tableid = -1;
    return tableid;
}

/* get table */
int hibase_get_table(HIBASE *hibase, int tableid, char *table_name, ITABLE *ptable)
{
    ITABLE *tab = NULL;
    int id = -1, n = 0;

    if(hibase && hibase->mtable && ptable)
    {
        if(table_name && (n = strlen(table_name)) > 0) 
            id = hibase_table_exists(hibase, table_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(id < 0 && tableid >= 0 ) id = tableid;
        if(id >= 0 && id < hibase->tableio.total 
                && (tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            memcpy(ptable, &(tab[id]), sizeof(ITABLE)); 
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* update table */
int hibase_update_table(HIBASE *hibase, int tableid, ITABLE *ptable)
{
    char *table_name = NULL;
    ITABLE *tab = NULL;
    int id = -1, n = 0;

    if(hibase && hibase->mtable && ptable)
    {
        if((table_name = ptable->name) && (n = strlen(table_name)) > 0) 
            id = hibase_table_exists(hibase, table_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(id < 0 && tableid >= 0 ) id = tableid;
        if(id >= 0 && id < hibase->tableio.total 
                && (tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            memcpy(&(tab[id]), ptable, sizeof(ITABLE)); 
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}


/* delete table */
int hibase_delete_table(HIBASE *hibase, int tableid, char *table_name)
{
    ITABLE *tab = NULL;
    int id = -1, n = 0;
    void *dp = NULL;

    if(hibase && hibase->mtable)
    {
        if(table_name && (n = strlen(table_name)) > 0) 
            id = hibase_table_exists(hibase, table_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(id < 0 && tableid >= 0 ) id = tableid;
        if(id >= 0 && id < hibase->tableio.total 
                && (tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            n = strlen(tab[id].name);
            TRIETAB_DEL(hibase->mtable, tab[id].name, n, dp);
            tab[id].status = TAB_STATUS_ERR;
            hibase->tableio.left--;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* list tables */
int hibase_list_table(HIBASE *hibase, FILE *fp)
{
    ITABLE *tab = NULL;
    int i = 0, j = 0, x = 0;

    if(hibase && fp && hibase->tableio.total > 0 
            && (tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)(-1))
    {
        for(i = 0; i < hibase->tableio.total; i++)
        {
            if(tab[i].status == TAB_STATUS_OK && tab[i].nfields > 0)
            {
                fprintf(fp, "id[%d] table[%s] nfields[%d]\n{\n", i, tab[i].name, tab[i].nfields);
                for(j = 0; j < tab[i].nfields; j++)
                {
                    if((x = tab[i].fields[j].data_type & FTYPE_ALL))
                    {
                        fprintf(fp, "\tname[%s] type[%s],\n", 
                                tab[i].fields[j].name, table_data_types[x]);
                    }
                }
                fprintf(fp, "};\n");
            }
        }
    }
    return 0;
}

/* check template exists */
int hibase_template_exists(HIBASE *hibase, char *template_name, int len)
{
    void *dp = NULL;
    int templateid = -1;

    if(hibase && template_name && len > 0 && hibase->mtemplate)
    {
        MUTEX_LOCK(hibase->mutex);
        TRIETAB_GET(hibase->mtemplate, template_name, len, dp);
        templateid = ((long)dp - 1);
        MUTEX_UNLOCK(hibase->mutex);
    }
    return templateid;
}

/* add template */
int hibase_add_template(HIBASE *hibase, ITEMPLATE *template)
{
    int templateid = -1, i = 0, n = 0;
    struct stat st = {0};
    ITEMPLATE *temp = NULL;
    void *dp = NULL;

    if(hibase && template && (n = strlen(template->name))  > 0 
            && (templateid = hibase_template_exists(hibase, template->name, n)) < 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->templateio.left == 0)
        {_MMAP_(hibase->templateio, st, ITEMPLATE, TEMPLATE_INCRE_NUM);}        
        if(hibase->templateio.left > 0 && (temp = (ITEMPLATE *)hibase->templateio.map) 
                && temp != (ITEMPLATE *)-1)
        {
            for(i = 0; i < hibase->templateio.total; i++)
            {
                if(temp[i].status != TEMP_STATUS_OK)
                {
                    template->status = TEMP_STATUS_OK;
                    memcpy(&(temp[i]), template, sizeof(ITEMPLATE));
                    templateid = i;
                    dp = (void *)((long)(templateid + 1));
                    TRIETAB_ADD(hibase->mtemplate, template->name, n, dp);
                    hibase->templateio.left--;
                    break;
                }
            }
        }
        
        MUTEX_UNLOCK(hibase->mutex);
    }
    else templateid = -1;
    return templateid;
}

/* get template */
int hibase_get_template(HIBASE *hibase, int templateid, char *template_name, ITEMPLATE *ptemplate)
{
    ITEMPLATE *temp = NULL;
    int id = -1, n = 0;

    if(hibase && hibase->mtemplate && ptemplate)
    {
        if(template_name && (n = strlen(template_name)) > 0) 
            id = hibase_template_exists(hibase, template_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(id < 0 && templateid >= 0 ) id = templateid;
        if(id >= 0 && id < hibase->templateio.total 
                && (temp = (ITEMPLATE *)(hibase->templateio.map)) 
                && temp != (ITEMPLATE *)-1)
        {
            memcpy(ptemplate, &(temp[id]), sizeof(ITEMPLATE)); 
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* update template */
int hibase_update_template(HIBASE *hibase, int templateid, ITEMPLATE *ptemplate)
{
    char *template_name = NULL;
    ITEMPLATE *temp = NULL;
    int id = -1, n = 0;

    if(hibase && hibase->mtemplate && ptemplate)
    {
        if((template_name = ptemplate->name) && (n = strlen(template_name)) > 0) 
            id = hibase_template_exists(hibase, template_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(id < 0 && templateid >= 0 ) id = templateid;
        if(id >= 0 && id < hibase->templateio.total 
                && (temp = (ITEMPLATE *)(hibase->templateio.map)) 
                && temp != (ITEMPLATE *)-1)
        {
            memcpy(&(temp[id]), ptemplate, sizeof(ITEMPLATE)); 
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* delete template */
int hibase_delete_template(HIBASE *hibase, int templateid, char *template_name)
{
    ITEMPLATE *temp = NULL;
    int id = -1, n = 0;
    void *dp = NULL;

    if(hibase && hibase->mtemplate)
    {
        if(template_name && (n = strlen(template_name)) > 0) 
            id = hibase_template_exists(hibase, template_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(id < 0 && templateid >= 0 ) id = templateid;
        if(id >= 0 && id < hibase->templateio.total 
                && (temp = (ITEMPLATE *)(hibase->templateio.map)) 
                && temp != (ITEMPLATE *)-1)
        {
            n = strlen(temp[id].name);
            TRIETAB_DEL(hibase->mtemplate, temp[id].name, n, dp);
            temp[id].status = TEMP_STATUS_ERR;
            hibase->templateio.left--;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* list templates */
int hibase_list_template(HIBASE *hibase, FILE *fp)
{
    ITEMPLATE *temp = NULL;
    int i = 0, j = 0, x = 0;

    if(hibase && fp && hibase->templateio.total > 0 
            && (temp = (ITEMPLATE *)(hibase->templateio.map)) 
            && temp != (ITEMPLATE *)(-1))
    {
        for(i = 0; i < hibase->templateio.total; i++)
        {
            if(temp[i].status == TEMP_STATUS_OK && temp[i].npatterns > 0)
            {
                fprintf(fp, "id[%d] template[%s] npatterns[%d] \n{\n", 
                        i, temp[i].name, temp[i].npatterns);
                for(j = 0; j < temp[i].npatterns; j++)
                {
                    fprintf(fp, "\t/%s/",temp[i].patterns[j].text);
                    x = 0;
                    while(x < temp[i].patterns[j].nfields)  
                        fprintf(fp, "table[%d] field[%d]; ", 
                                temp[i].patterns[j].map[x].table_id, 
                                temp[i].patterns[j].map[x].field_id);
                    fprintf(fp, "\n");
                }
                fprintf(fp, "};\n");
            }
        }
    }
    return 0;
}

/* check pnode exists */
int hibase_pnode_exists(HIBASE *hibase, char *name, int len)
{
    void *dp = NULL;
    int pnodeid = -1;

    if(hibase && name && len > 0 && hibase->mpnode)
    {
        MUTEX_LOCK(hibase->mutex);
        TRIETAB_GET(hibase->mpnode, name, len, dp);
        pnodeid = ((long)dp - 1);
        MUTEX_UNLOCK(hibase->mutex);
    }
    return pnodeid;
}

/* add pnode */
int hibase_add_pnode(HIBASE *hibase, int parentid, char *name)
{
    int pnodeid = -1, n = 0, *px = NULL, x = 0;
    struct stat st = {0};
    PNODE *pnode = NULL, *pparent = NULL;
    void *dp = NULL;

    if(hibase && name && (n = strlen(name))  > 0 
            && (hibase_pnode_exists(hibase, name, n)) < 0)
    {
        MUTEX_LOCK(hibase->mutex);
        if(hibase->pnodeio.left == 0)
        {_MMAP_(hibase->pnodeio, st, PNODE, PNODE_INCRE_NUM);}
        px = &x;
        if(FQUEUE_POP(hibase->qpnode, int, px) != 0)
        {
            pnodeid = ++(hibase->pnodeio.current);
        }
        else
        {
            pnodeid = x;
        }
        if(parentid >= 0 && parentid < hibase->pnodeio.total 
                && pnodeid >= 0  && pnodeid < hibase->pnodeio.total 
                && (pnode = (PNODE *)hibase->pnodeio.map) && pnode != (PNODE *)-1)
        {
            memset(&(pnode[pnodeid]), 0, sizeof(PNODE));
            memcpy(pnode[pnodeid].name, name, n); 
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
            dp = (void *)((long)(pnodeid + 1));
            TRIETAB_ADD(hibase->mpnode, name, n, dp);
            hibase->pnodeio.left--;
            fprintf(stdout, "%d::%d->%d\n", parentid, pnodeid, hibase->pnodeio.current);
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
                && (pnode = (PNODE *)(hibase->pnodeio.map)) && pnode != (PNODE *)-1)
        {
            memcpy(ppnode, &(pnode[pnodeid]), sizeof(PNODE));
            ret = 0;
        }
        MUTEX_UNLOCK(hibase->mutex);
    }
    return ret;
}

/* get pnode */
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

/* update pnode */
int hibase_update_pnode(HIBASE *hibase, int pnodeid, char *name)
{
    PNODE *pnode = NULL;
    int id = -1, n = 0, x = 0;
    void *dp = NULL;

    if(hibase && hibase->mpnode && name && (n = strlen(name)) > 0 && n < PNODE_NAME_MAX)
    {
        if((id = hibase_pnode_exists(hibase, name, n)) >= 0) return id;
        MUTEX_LOCK(hibase->mutex);
        if(pnodeid >= 0 && pnodeid < hibase->pnodeio.total 
                && (pnode = (PNODE *)(hibase->pnodeio.map)) 
                && pnode != (PNODE *)-1)
        {
            if((x = strlen(pnode[pnodeid].name)) > 0)
            {
                TRIETAB_DEL(hibase->mpnode, pnode[pnodeid].name, x, dp);
            }
            memset(pnode[pnodeid].name, 0, PNODE_NAME_MAX);
            memcpy(pnode[pnodeid].name, name, n); 
            dp = (void *)((long ) (pnodeid + 1));
            TRIETAB_ADD(hibase->mpnode, name, n, dp);
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* delete pnode */
int hibase_delete_pnode(HIBASE *hibase, int pnodeid, char *pnode_name)
{
    PNODE *pnode = NULL, *parent = NULL;
    int id = -1, i = 0, x = 0, n = 0;
    void *dp = NULL;

    if(hibase && hibase->mpnode)
    {
        if(pnode_name && (n = strlen(pnode_name)) > 0) 
            id = hibase_pnode_exists(hibase, pnode_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(id < 0 && pnodeid >= 0 ) id = pnodeid;
        if(id >= 0 && id < hibase->pnodeio.total 
                && (pnode = (PNODE *)(hibase->pnodeio.map)) && pnode != (PNODE *)-1)
        {
            if((n = strlen(pnode[id].name)) > 0)
            {
                TRIETAB_DEL(hibase->mpnode, pnode[id].name, n, dp);
            }
            if((n = pnode[id].parent) >= 0 && n < hibase->pnodeio.total)
            {
                parent = &(pnode[n]);
                if(parent->first == id) parent->first = pnode[id].next;
                if(parent->last == id) parent->last = pnode[id].prev;
                parent->nchilds--;
            }
            if((n = pnode[id].prev) > 0 && n < hibase->pnodeio.total)
            {
                pnode[n].next = pnode[n].next;
            }
            if((n = pnode[id].next) > 0 && n < hibase->pnodeio.total)
            {
                pnode[n].prev = pnode[n].prev;
            }
            if(pnode[id].nchilds > 0)
            {
                i = 0;
                x = pnode[id].first;
                while(i < pnode[id].nchilds && x >= 0 && x < hibase->pnodeio.total)
                {
                    hibase_delete_pnode(hibase, x, NULL);
                    x = pnode[x].next;
                    i++;
                }
            }
            memset(&(pnode[id]), 0, sizeof(PNODE));
            FQUEUE_PUSH(hibase->qpnode, int, &id);
            hibase->pnodeio.left++;
        }
        else id = -1;
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
            fprintf(fp, "%s", "  |");
        }
        if(pnode[pnodeid].nchilds > 0)
        {
            fprintf(fp, "--+%s\n", pnode[pnodeid].name);
        }
        else
        {
            fprintf(fp, "-%s\n", pnode[pnodeid].name);
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

/* clean */
void hibase_clean(HIBASE **phibase)
{
    if(phibase && *phibase)
    {
        if((*phibase)->mtable) {TRIETAB_CLEAN((*phibase)->mtable);}
        if((*phibase)->mtemplate) {TRIETAB_CLEAN((*phibase)->mtemplate);}
        if((*phibase)->mpnode) {TRIETAB_CLEAN((*phibase)->mpnode);}
        if((*phibase)->tableio.map && (*phibase)->tableio.size > 0)
        {
            _MUNMAP_((*phibase)->tableio.map, (*phibase)->tableio.size);
        }
        if((*phibase)->tableio.map && (*phibase)->tableio.size > 0)
        {
            _MUNMAP_((*phibase)->templateio.map, (*phibase)->templateio.size);
        }
        if((*phibase)->pnodeio.map && (*phibase)->pnodeio.size > 0)
        {
            _MUNMAP_((*phibase)->pnodeio.map, (*phibase)->pnodeio.size);
        }
        if((*phibase)->tableio.fd > 0) close((*phibase)->tableio.fd);
        if((*phibase)->templateio.fd > 0) close((*phibase)->templateio.fd);
        if((*phibase)->pnodeio.fd > 0) close((*phibase)->pnodeio.fd);
        if((*phibase)->qpnode){FQUEUE_CLEAN((*phibase)->qpnode);}
        if((*phibase)->mutex){MUTEX_DESTROY((*phibase)->mutex);}
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
        TRIETAB_INIT(hibase->mtable);
        TRIETAB_INIT(hibase->mtemplate);
        TRIETAB_INIT(hibase->mpnode);
        MUTEX_INIT(hibase->mutex);
        hibase->set_basedir         = hibase_set_basedir;
        hibase->table_exists        = hibase_table_exists;
        hibase->add_table           = hibase_add_table;
        hibase->get_table           = hibase_get_table;
        hibase->update_table        = hibase_update_table;
        hibase->delete_table        = hibase_delete_table;
        hibase->template_exists     = hibase_template_exists;
        hibase->add_template        = hibase_add_template;
        hibase->get_template        = hibase_get_template;
        hibase->update_template     = hibase_update_template;
        hibase->delete_template     = hibase_delete_template;
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
    int i = 0, j = 0, rand = 0, x = 0, n = 0, table_num = 92;

    if((hibase = hibase_init()))
    {
        hibase->set_basedir(hibase, "/tmp/hibase");
        //add table
        for(i = 0; i < table_num; i++)
        {
            memset(&table, 0, sizeof(ITABLE));
            sprintf(table.name, "table_%d", i);
            rand = random() % FIELD_NUM_MAX;
            for(j = 0; j < rand; j++)
            {
                sprintf(table.fields[j].name, "field_%d", j);
                x = j % 3;
                if(x == 0 ) table.fields[j].data_type = FTYPE_INT;
                else if (x == 1) table.fields[j].data_type = FTYPE_FLOAT;
                else  table.fields[j].data_type = FTYPE_TEXT;
            }
            table.nfields = rand;
            hibase->add_table(hibase, &table);
        }
        if(hibase->add_table(hibase, &table) == -1)
        {
            fprintf(stdout, "add table[%s] failed\n", table.name);
        }
        hibase_list_table(hibase, stdout);
        //update
        x = random() % table_num;
        hibase->get_table(hibase, x, NULL, &table);
        n = (random() % FIELD_NUM_MAX) + 1;
        fprintf(stdout, "updated table[%d] nfields[%d to %d]\n", x, table.nfields, n);
        table.nfields =  n;
        hibase->update_table(hibase, x, &table);
        x = random() % table_num;
        sprintf(table.name, "table_%d", x);
        hibase->get_table(hibase, -1, table.name, &table);
        n = (random() % FIELD_NUM_MAX) + 1;
        fprintf(stdout, "updated table_%d nfields[%d to %d]\n", x, table.nfields, n);
        table.nfields =  n;
        hibase->update_table(hibase, -1, &table);
        hibase_list_table(hibase, stdout);
        //delete 
        x = random() % table_num;
        hibase->get_table(hibase, x, NULL, &table);
        n = (random() % FIELD_NUM_MAX) + 1;
        fprintf(stdout, "deleted table[%d] nfields[%d to %d]\n", x, table.nfields, n);
        table.nfields =  n;
        hibase->delete_table(hibase, x, NULL);
        x = random() % table_num;
        sprintf(table.name, "table_%d", x);
        hibase->get_table(hibase, -1, table.name, &table);
        n = (random() % FIELD_NUM_MAX) + 1;
        fprintf(stdout, "deleted table_%d nfields[%d to %d]\n", x, table.nfields, n);
        table.nfields =  n;
        hibase->delete_table(hibase, -1, table.name);
        hibase_list_table(hibase, stdout);
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
        hibase_list_pnode(hibase, 0, stdout);
        hibase_update_pnode(hibase, 9999, "my_node");
        hibase_list_pnode(hibase, 0, stdout);
        fprintf(stdout, "%d::OK\n", __LINE__);
        hibase->clean(&hibase);
    }
    return 0;
}
//gcc -o hibase hibase.c utils/*.c -I utils -D_DEBUG_HIBASE -lz 
#endif
