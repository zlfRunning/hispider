#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#ifndef _HIO_H
#define _HIO_H
#ifndef HIO_PATH_MAX
#define HIO_PATH_MAX 256
#endif
typedef struct _HIO
{
    int rfd;
    int wfd;
    void *mutex;
    char path[HIO_PATH_MAX]; 
}HIO;
#define PH(ptr) ((HIO *)ptr)
#define RD(ptr) ((HIO *)ptr)->rfd
#define WD(ptr) ((HIO *)ptr)->wfd
#define PM(ptr) ((HIO *)ptr)->mutex
#define PTH(ptr) ((HIO *)ptr)->path
#define RCD(ptr) ((ptr) ? (close(RD(ptr)) | -1) : -1) 
#define WCD(ptr) ((ptr) ? (close(WD(ptr)) | -1) : -1)
#define HIO_INIT(ptr) (ptr = calloc(1, sizeof(HIO)))
#define HIO_SET(ptr, lp)                    \
{                                           \
    if(ptr)                                 \
    {                                       \
        strcpy(PTH(ptr), lp);               \
        if(RD(ptr) > 0) close(RD(ptr));     \
        if(WD(ptr) > 0) close(WD(ptr));     \
        RD(ptr) = -1;                       \
        WD(ptr) = -1;                       \
    }                                       \
}
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
#define HIO_CLEAN(ptr)                          \
{                                               \
    if(ptr)                                     \
    {                                           \
        close(RD(ptr));                         \
        close(WD(ptr));                         \
        free(ptr);                              \
    }                                           \
}
#endif
