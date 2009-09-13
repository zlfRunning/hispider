#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#ifndef _HIO_H
#define _HIO_H
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
#define HIO_INIT(io, file, st, type, use_mmap, incre_num)                               \
do                                                                                      \
{                                                                                       \
    if(file && (io.fd = open(file, O_CREAT|O_RDWR, 0644)) > 0                           \
            && fstat(io.fd, &st) == 0)                                                  \
    {                                                                                   \
        if(io.size == 0){HIO_INCRE(io, type, incre_num);}                               \
        if(use_mmap && io.size > 0 && (io.map = mmap(NULL, io.size,                     \
                        PROT_READ|PROT_WRITE, MAP_SHARED, io.fd, 0)) == (void *)-1)     \
        {                                                                               \
            _EXIT_("mmap %d size:%lld failed, %s\n", io.fd,                             \
                    (long long int)io.size, strerror(errno));                           \
        }                                                                               \
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
#endif
