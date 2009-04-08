#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "hibase.h"
#include "trie.h"
#include "timer.h"
#include "logger.h"
#define  HIBASE_TABLE_NAME   		".table"
#define  HIBASE_TEMPLATE_NAME 		".template"

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
                if(template[i].status == TEMP_STATUS_OK && template[i].nfields > 0 
                        && (n = strlen(template[i].name)) > 0)
                {
                    dp = (void *)((long)(i + 1));
                    TRIETAB_ADD(hibase->mtemplate, template[i].name, n, dp);
                    hibase->templateio.current = i;
                }
                else hibase->templateio.left++;
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
int hibase_update_table(HIBASE *hibase, int tableid, char *table_name, ITABLE *ptable)
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

    if(hibase && hibase->mtable)
    {
        if(table_name && (n = strlen(table_name)) > 0) 
            id = hibase_table_exists(hibase, table_name, n);
        MUTEX_LOCK(hibase->mutex);
        if(id < 0 && tableid >= 0 ) id = tableid;
        if(id >= 0 && id < hibase->tableio.total 
                && (tab = (ITABLE *)(hibase->tableio.map)) && tab != (ITABLE *)-1)
        {
            tab[id].status = TAB_STATUS_ERR;
            hibase->tableio.left--;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
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
        {
            _MMAP_(hibase->templateio, st, ITEMPLATE, TEMPLATE_INCRE_NUM);
        }        
        if(hibase->templateio.left > 0 && (temp = (ITEMPLATE *)hibase->templateio.map) 
                && temp != (ITEMPLATE *)-1)
        {
            for(i = 0; i < hibase->templateio.total; i++)
            {
                if(temp[i].status != TAB_STATUS_OK)
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
    return templateid;
}

/* get template */
int hibase_get_template(HIBASE *hibase, int templateid, 
        char *template_name, ITEMPLATE *ptemplate)
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
int hibase_update_template(HIBASE *hibase, int templateid, 
        char *template_name, ITEMPLATE *ptemplate)
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
            temp[id].status = TEMP_STATUS_ERR;
            hibase->templateio.left--;
        }
        else id = -1;
        MUTEX_UNLOCK(hibase->mutex);
    }
    return id;
}

/* clean */
void hibase_clean(HIBASE **phibase)
{
    if(phibase && *phibase)
    {
        if((*phibase)->mtable) {TRIETAB_CLEAN((*phibase)->mtable);}
        if((*phibase)->mtemplate) {TRIETAB_CLEAN((*phibase)->mtemplate);}
        if((*phibase)->tableio.map && (*phibase)->tableio.size > 0)
        {
            _MUNMAP_((*phibase)->tableio.map, (*phibase)->tableio.size);
        }
        if((*phibase)->tableio.map && (*phibase)->tableio.size > 0)
        {
            _MUNMAP_((*phibase)->templateio.map, (*phibase)->templateio.size);
        }
        if((*phibase)->tableio.fd > 0) close((*phibase)->tableio.fd);
        if((*phibase)->templateio.fd > 0) close((*phibase)->templateio.fd);
        if((*phibase)->mutex) MUTEX_DESTROY((*phibase)->mutex);
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
