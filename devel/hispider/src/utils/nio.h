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
    int rfd;
    int wfd;
    char path[NIO_PATH_MAX]; 
}NIO;
#define PF(ptr) ((NIO *)ptr)
#define PFR(ptr) ((NIO *)ptr)->rfd
#define PFW(ptr) ((NIO *)ptr)->wfd
#define NIO_INIT() ((NIO *)calloc(1, sizeof(NIO)))
//{if((ptr = (NIO *)calloc(1, sizeof(NIO)))){MUTEX_INIT(PF(ptr)->mutex);}}
#define NIO_SET(ptr, lpath) ((PF(ptr) && lpath && strcpy(PF(ptr)->path, lpath)) ? 0 : -1) 
#define NIO_CHECK_READ(ptr) ( (PF(ptr) == NULL  || (PFR(ptr) <= 0  \
                && (PFR(ptr) = open(PF(ptr)->path, O_CREAT|O_RDONLY, 0644)) <= 0 ) ) ? -1 : 0)
#define NIO_CHECK_WRITE(ptr) ( (PF(ptr) == NULL  || (PFW(ptr) <= 0  \
                && (PFW(ptr) = open(PF(ptr)->path, O_CREAT|O_WRONLY, 0644)) <= 0 ) ) ? -1 : 0)
#define NIO_SEEKR(ptr, off) ( ((NIO_CHECK_READ(ptr)) == 0) ? (lseek(PFR(ptr), off, SEEK_SET)) : -1)
#define NIO_SEEKW(ptr, off) ( ((NIO_CHECK_WRITE(ptr)) == 0) ? (lseek(PFW(ptr), off, SEEK_SET)) : -1)
#define NIO_SEEK_REND(ptr) ( ((NIO_CHECK_READ(ptr)) == 0) ?  (lseek(PFR(ptr), 0, SEEK_END)) : -1)
#define NIO_SEEK_WEND(ptr) ( ((NIO_CHECK_WRITE(ptr)) == 0) ?  (lseek(PFW(ptr), 0, SEEK_END)) : -1)
#define NIO_SEEK_RSTART(ptr) ( ((NIO_CHECK_READ(ptr)) == 0) ? (lseek(PFR(ptr), 0, SEEK_SET)) : -1)
#define NIO_SEEK_WSTART(ptr) ( ((NIO_CHECK_WRITE(ptr)) == 0) ? (lseek(PFW(ptr), 0, SEEK_SET)) : -1)
#define NIO_READ(ptr, ps, n) ( ((NIO_CHECK_READ(ptr)) == 0) ? (read(PFR(ptr), ps, n)) : -1)
#define NIO_WRITE(ptr, ps, n) ( ((NIO_CHECK_WRITE(ptr)) == 0) ? (write(PFW(ptr), ps, n)) : -1)
#define NIO_SREAD(ptr, ps, n, off) ( (NIO_CHECK_READ(ptr) == 0) ?    \
              ( (lseek(PFR(ptr), off, SEEK_SET) >= 0) ?  \
                (read(PFR(ptr), ps, n)) : -1) : -1) 
#define NIO_SWRITE(ptr, ps, n, off) ( (NIO_CHECK_WRITE(ptr) == 0) ?    \
              ( (lseek(PFW(ptr), off, SEEK_SET) >= 0) ?  \
                (write(PFW(ptr), ps, n)) : -1) : -1) 
#define NIO_APPEND(ptr, ps, n) ( (NIO_CHECK_WRITE(ptr) == 0) ?    \
              ( (lseek(PFW(ptr), 0, SEEK_END) >= 0) ?  \
                (write(PFW(ptr), ps, n)) : -1) : -1) 
#define NIO_CLEAN(ptr) {if(ptr){close(PFR(ptr)); close(PFW(ptr));free(ptr);ptr = NULL;}}
#endif
