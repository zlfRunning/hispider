#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#ifndef _HIO_H
#define _HIO_H
#ifdef __cplusplus
extern "C" {
#endif
#ifndef HIO_PATH_MAX
#define HIO_PATH_MAX 256
#endif
typedef struct _HIO
{
    void *mp;
    struct stat st;
    int fd;
    int rfd;
    int wfd;
    void *mutex;
    char path[HIO_PATH_MAX]; 
}HIO;
#define PH(ptr) ((HIO *)ptr)
#define MP(ptr) ((HIO *)ptr)->mp
#define ST(ptr) ((HIO *)ptr)->st
#define FD(ptr) ((HIO *)ptr)->fd
#define RD(ptr) ((HIO *)ptr)->rfd
#define WD(ptr) ((HIO *)ptr)->wfd
#define PM(ptr) ((HIO *)ptr)->mutex
#define PTH(ptr) ((HIO *)ptr)->path
#define RCD(ptr) ((ptr) ? (close(RD(ptr)) | -1) : -1) 
#define WCD(ptr) ((ptr) ? (close(WD(ptr)) | -1) : -1)
#define HIO_INIT(ptr) (ptr = calloc(1, sizeof(HIO)))
#define HIO_SET(ptr, lp)                    \
do{                                         \
    if(ptr)                                 \
    {                                       \
        strcpy(PTH(ptr), lp);               \
        if(RD(ptr) > 0) close(RD(ptr));     \
        if(WD(ptr) > 0) close(WD(ptr));     \
        if(FD(ptr) > 0) close(FD(ptr));     \
        RD(ptr) = -1;                       \
        WD(ptr) = -1;                       \
        FD(ptr) = -1;                       \
    }                                       \
}while(0)
#define FDCHK(ptr) ((PH(ptr) == NULL \
        || (FD(ptr) <= 0 && (FD(ptr) = open(PTH(ptr), O_CREAT|O_RDWR, 0644)) < 0 ) ) ? -1 : 0) 
#define RCHK(ptr) ((PH(ptr) == NULL \
        || (RD(ptr) <= 0 && (RD(ptr) = open(PTH(ptr), O_CREAT|O_RDONLY, 0644)) < 0 ) ) ? -1 : 0) 
#define WCHK(ptr) ((PH(ptr) == NULL \
        || (WD(ptr) <= 0 && (WD(ptr) = open(PTH(ptr), O_CREAT|O_WRONLY, 0644)) < 0 ) ) ? -1 : 0) 
#define HIO_CHK(ptr) {RCHK(ptr); WCHK(ptr);}
#define HIO_RSEEK(ptr, off) ((RCHK(ptr) == 0) ? lseek(RD(ptr), off, SEEK_SET) : RCD(ptr))
#define HIO_WSEEK(ptr, off) ((WCHK(ptr) == 0) ? lseek(WD(ptr), off, SEEK_SET) : WCD(ptr))
#define HIO_READ(ptr, s, ns) ((RCHK(ptr) == 0) ? read(RD(ptr), s, ns) : RCD(ptr))
#define HIO_WRITE(ptr, s, ns) ((WCHK(ptr) == 0) ? write(WD(ptr), s, ns) : WCD(ptr))
#define HIO_SREAD(ptr, s, ns, off) ((RCHK(ptr) == 0 && lseek(RD(ptr), off, SEEK_SET) >= 0) \
        ? read(RD(ptr), s, ns) : RCD(ptr))
#define HIO_SWRITE(ptr, s, ns, off) ((WCHK(ptr)==0 && lseek(WD(ptr), off, SEEK_SET) >= 0) \
        ? write(WD(ptr), s, ns) : WCD(ptr))
#define HIO_APPEND(ptr, s, ns, off) ((WCHK(ptr)==0 && (off = lseek(WD(ptr), 0, SEEK_END))>= 0) \
        ? write(WD(ptr), s, ns) : WCD(ptr))
#define HIO_MMAP(ptr) ((ptr && FDCHK(ptr) == 0 && fstat(FD(ptr), &(ST(ptr))) == 0)?         \
    (MP(ptr) = mmap(NULL, ST(ptr).st_size, PROT_READ|PROT_WRITE, MAP_SHARED, FD(ptr), 0)):MAP_FAILED)
#define HIO_MSYNC(ptr) ((ptr && MP(ptr) && MP(ptr) != -1)?                                  \
        msync(MP(ptr), ST(ptr).st_size, MS_ASYNC) : -1)
#define HIO_MUNMAP(ptr) ((ptr && MP(ptr) != MAP_FAILED && MP(ptr) != NULL)?                 \
                munmap(MP(ptr), ST(ptr).st_size):-1)
#define HIO_CLEAN(ptr)                                                                      \
do{                                                                                           \
    if(ptr)                                                                                 \
    {                                                                                       \
        if(RD(ptr) > 0)close(RD(ptr));                                                      \
        if(WD(ptr) > 0)close(WD(ptr));                                                      \
        HIO_MUNMAP(ptr);                                                                    \
        if(FD(ptr) > 0)close(FD(ptr));                                                      \
        free(ptr);                                                                          \
    }                                                                                       \
}while(0)

#ifdef __cplusplus
 }
#endif
#endif
