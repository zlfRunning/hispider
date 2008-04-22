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
#define NIO_INIT() ((NIO *)calloc(1, sizeof(NIO)))
//{if((ptr = (NIO *)calloc(1, sizeof(NIO)))){MUTEX_INIT(PF(ptr)->mutex);}}
#define NIO_SET(ptr, lpath) ((PF(ptr) && lpath && strcpy(PF(ptr)->path, lpath)) ? 0 : -1) 
#define NIO_FD(ptr) ((PF(ptr)) ? PF(ptr)->fd : -1)
#define NIO_CHECK(ptr) ( (PF(ptr) == NULL  \
|| (PF(ptr)->fd <= 0 && (PF(ptr)->fd = open(PF(ptr)->path, O_CREAT|O_RDWR, 0644)) <= 0 ) ) ? -1 : 0)
#define NIO_SEEK(ptr, off) ( ((NIO_CHECK(ptr)) == 0) ? (lseek(PF(ptr)->fd, off, SEEK_SET)) : -1)
#define NIO_SEEK_END(ptr) ( ((NIO_CHECK(ptr)) == 0) ?  (lseek(PF(ptr)->fd, 0, SEEK_END)) : -1)
#define NIO_SEEK_START(ptr) ( ((NIO_CHECK(ptr)) == 0) ? (lseek(PF(ptr)->fd, 0, SEEK_SET)) : -1)
#define NIO_READ(ptr, ps, n) ( ((NIO_CHECK(ptr)) == 0) ? (read(PF(ptr)->fd, ps, n)) : -1)
#define NIO_SREAD(ptr, ps, n, off) ( ((NIO_SEEK(ptr, off)) >= 0) ? read(PF(ptr)->fd, ps, n) : -1)
#define NIO_WRITE(ptr, ps, n) ( ((NIO_CHECK(ptr)) == 0) ? (write(PF(ptr)->fd, ps, n)) : -1)
#define NIO_SWRITE(ptr, ps, n, off) ( ((NIO_SEEK(ptr, off)) >= 0) ? write(PF(ptr)->fd, ps, n) : -1)
#define NIO_APPEND(ptr, ps, n) (((NIO_SEEK_END(ptr)) >= 0) ? write(PF(ptr)->fd, ps, n) : -1)
#define NIO_CLEAN(ptr) {if(ptr){close(PF(ptr)->fd);free(ptr);ptr = NULL;}}
#endif
