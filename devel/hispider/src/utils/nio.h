#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#ifndef _NIO_H
#define _NIO_H
#ifndef NIO_PATH_MAX
#define NIO_PATH_MAX 256
#endif
typedef struct _NIO
{
    int fd;
    char path[NIO_PATH_MAX]; 
}NIO;
#define PF(ptr) ((NIO *)ptr)
#define PFD(ptr) ((NIO *)ptr)->fd
#define PPATH(ptr) ((NIO *)ptr)->path
#define NIO_INIT() ((NIO *)calloc(1, sizeof(NIO)))
#define NIO_SET(ptr, lpath) ((PF(ptr) && lpath && strcpy(PF(ptr)->path, lpath)) ? 0 : -1) 
#define NIO_OPEN(ptr) ((PF(ptr))?(PFD(ptr) = open(PPATH(ptr), O_CREAT|O_RDWR, 0644)) : -1)
#define NIO_CHECK(ptr) ((PF(ptr) == NULL || (PFD(ptr) <= 0  \
            && (PFD(ptr) = NIO_OPEN(ptr)) <= 0 )) ? (PFD(ptr) = -1) : 0)
#define NIO_LOCK(ptr) ((PF(ptr)) ? flock(PFD(ptr), LOCK_EX|LOCK_NB) : -1)
#define NIO_UNLOCK(ptr) ((PF(ptr)) ? flock(PFD(ptr), LOCK_UN) : -1)
#define NIO_SEEK(ptr, off) ((PF(ptr))? lseek(PFD(ptr), off, SEEK_SET) : -1)
#define NIO_SEEK_START(ptr) ((PF(ptr))? lseek(PFD(ptr), 0, SEEK_SET) : -1)
#define NIO_SEEK_END(ptr) ((PF(ptr))? lseek(PFD(ptr), 0, SEEK_END) : -1)
#define NIO_READ(ptr, p, n) read(PFD(ptr), p, n)
#define NIO_WRITE(ptr, p, n) write(PFD(ptr), p, n)
#define NIO_APPEND(ptr, p, n) ( (PF(ptr) && (lseek(PFD(ptr), 0, SEEK_END) >= 0)) ?  \
            write(PFD(ptr), p, n) : -1 )
#define NIO_SREAD(ptr, p, n, off) ( (PF(ptr) && (lseek(PFD(ptr), off, SEEK_SET) >= 0)) ? \
            read(PFD(ptr), p, n) : -1 )
#define NIO_SWRITE(ptr, p, n, off) ( (PF(ptr) && (lseek(PFD(ptr), off, SEEK_SET) >= 0)) ? \
            write(PFD(ptr), p, n) : -1 )
#define NIO_LAPPEND(ptr, p, n) ( (PF(ptr)) ?                             \
        ( ((flock(PFD(ptr), LOCK_EX|LOCK_NB) == 0)?                     \
         ( (lseek(PFD(ptr), 0, SEEK_END) >= 0) ?                        \
            (write(PFD(ptr), p, n) | flock(PFD(ptr), LOCK_UN))          \
            : (flock(PFD(ptr), LOCK_UN) | -1) ) : -1) ) : -1)
#define NIO_LSREAD(ptr, p, n, off) ( (PF(ptr)) ?                         \
        ( (flock(PFD(ptr), LOCK_EX|LOCK_NB) ?                           \
         ( (lseek(PFD(ptr), off, SEEK_SET) >= 0) ?                      \
            (read(PFD(ptr), p, n) | flock(PFD(ptr), LOCK_UN))           \
            : (flock(PFD(ptr), LOCK_UN) | -1) ) : -1) ) : -1) 
#define NIO_LSWRITE(ptr, p, n, off) ( (PF(ptr)) ?                        \
        ( (flock(PFD(ptr), LOCK_EX|LOCK_NB) ?                           \
         ( (lseek(PFD(ptr), off, SEEK_SET) >= 0) ?                      \
            (write(PFD(ptr), p, n) | flock(PFD(ptr), LOCK_UN))          \
            : (flock(PFD(ptr), LOCK_UN) | -1) ) : -1) ) : -1) 
#define NIO_WRITE_END(ptr, p, n)                                        \
{                                                                       \
    if(PF(ptr))                                                         \
    {                                                                   \
        if(flock(PFD(ptr), LOCK_EX|LOCK_NB) == 0)                       \
        {                                                               \
            if((lseek(PFD(ptr), 0, SEEK_END) >= 0))                     \
            {                                                           \
                write(PFD(ptr), p, n);                                  \
            }                                                           \
            flock(PFD(ptr), LOCK_UN);                                   \
        }                                                               \
    }                                                                   \
}
#define NIO_CLEAN(ptr) {if(ptr){close(PFD(ptr));free(ptr);ptr = NULL;}}
#endif
