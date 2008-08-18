#ifndef _IBIO_H
#define _IBIO_H
typedef struct _IBIO
{
    int fd;
    void *mp;
    size_t size;
}IBIO;
/* fsize */
off_t ifsize(int fd);
/* open file */
int iopen(char *path);
/* read data from position "offset" */
int iread(int fd, void *ptr, size_t size, off_t offset);
/* write to offset */
int iwrite(int fd, void *ptr, size_t size, off_t offset);
/* write to file end */
int iappend(int fd, void *ptr, size_t size, off_t *offset);
/* truncate file */
int itruncate(int fd, off_t length);
/* mmap */
void *immap(int fd, void *addr, size_t len, off_t offset);
/* map sync */
int imsync(void *mp, size_t len);
/* unmmap */
int imunmap(void *mp, size_t len);
#define PIO(ptr) ((IBIO *)ptr)
#define PIFD(ptr) (PIO(ptr)->fd) 
#define PIMP(ptr) (PIO(ptr)->mp) 
#define PISIZE(ptr) (PIO(ptr)->size) 

/* initialize and open file for reading  and writting */
#define IBIO_INIT(ptr, path)                                    \
do                                                              \
{                                                               \
    if((ptr = calloc(1, sizeof(IBIO))))                         \
    {                                                           \
        PIO(ptr)->fd = iopen(path);                             \
    }                                                           \
}while(0)

/* mmap */
#define IBIO_MMAP(ptr)                                          \
do                                                              \
{                                                               \
    if(ptr && (PIO(ptr)->size = ifsize(PIO(ptr)->fd)) > 0)      \
    {                                                           \
        PIO(ptr)->mp = immap(PIO(ptr)->fd,                      \
                PIO(ptr)->mp, PIO(ptr)->size, 0);               \
    }                                                           \
}while(0)

/* munmap */
#define IBIO_OFF(ptr)                                           \
do                                                              \
{                                                               \
    if(ptr)                                                     \
    {                                                           \
        if(PIO(ptr)->mp)                                        \
        {                                                       \
            imunmap(PIO(ptr)->mp, PIO(ptr)->size);              \
        }                                                       \
        PIO(ptr)->mp = NULL;                                    \
        PIO(ptr)->size = 0;                                     \
    }                                                           \
}while(0)
/* clean ibio */
#define IBIO_CLEAN(ptr)                                         \
do                                                              \
{                                                               \
    if(ptr)                                                     \
    {                                                           \
        if(PIO(ptr)->fd > 0)                                    \
        {                                                       \
            close(PIO(ptr)->fd);                                \
            PIO(ptr)->fd = 0;                                   \
        }                                                       \
        if(PIO(ptr)->mp)                                        \
        {                                                       \
            imunmap(PIO(ptr)->mp, PIO(ptr)->size);              \
            PIO(ptr)->mp = NULL;                                \
        }                                                       \
        free(ptr);                                              \
        ptr = NULL;                                             \
    }                                                           \
}while(0)
#endif
