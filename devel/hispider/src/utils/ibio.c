#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "ibio.h"

int iopen(char *path)
{
    int fd = -1;
    struct stat st = {0};

    if(path)
    {
        fd = open(path, O_CREAT|O_RDWR, 0644);
        fstat(fd, &st);
        fprintf(stdout, "open file[%s] via %d size:%lld\n", path, fd, st.st_size);
    }

    return fd;
}

/* fsize */
off_t ifsize(int fd)
{
    off_t size = -1;
    struct stat st = {0};

    if(fd > 0 && fstat(fd, &st) == 0)
    {
        size = st.st_size;
    }
    return size;
}

/* read data from position "offset" */
int iread(int fd, void *ptr, size_t size, off_t offset)
{
    int ret = -1;

    if(fd > 0 && ptr && size > 0 && offset >= 0)
    {
        flock(fd, LOCK_EX);
        ret = pread(fd, ptr, size, offset);
        flock(fd, LOCK_UN);
    }
    return ret;
}

/* write to offset */
int iwrite(int fd, void *ptr, size_t size, off_t offset)
{
    int ret = -1;

    if(fd > 0 && ptr && size > 0 && offset >= 0)
    {
        flock(fd, LOCK_EX);
        ret = pwrite(fd, ptr, size, offset);
        flock(fd, LOCK_UN);
    }
    return ret;
}

/* write to file end */
int iappend(int fd, void *ptr, size_t size, off_t *offset)
{
    int ret = -1;
    struct stat st = {0};

    if(fd > 0 && ptr && size > 0)
    {
        flock(fd, LOCK_EX);
        fstat(fd, &st);
        (*offset) = lseek(fd, st.st_size, SEEK_SET);
        ret = write(fd, ptr, size);
        flock(fd, LOCK_UN);
    }
    return ret;
}

/* truncate file */
int itruncate(int fd, off_t length)
{
    int ret = -1;
    struct stat st = {0};

    if(fd > 0 && length >= 0)
    {
        flock(fd, LOCK_EX);
        fstat(fd, &st);
        if(st.st_size < length) ret = ftruncate(fd, length);
        flock(fd, LOCK_UN);
    }
    return ret;
}

/* mmap */
void *immap(int fd, void *addr, size_t len, off_t offset)
{
    void *mp = NULL;
    if(fd > 0 && len > 0 && offset >= 0)
    {
        flock(fd, LOCK_EX);
        if((mp = mmap(addr, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, offset)) == (void *)-1)
        {
            mp = NULL;
        }
        flock(fd, LOCK_UN);
    }

    return mp;
}

/* map sync */
int imsync(void *mp, size_t len)
{
    int ret = -1;

    if(mp && len > 0)
    {
        ret = msync(mp, len, MS_ASYNC);
    }
    return ret;
}

/* unmmap */
int imunmap(void *mp, size_t len)
{
    int ret = -1;

    if(mp && len > 0)
    {
        ret = munmap(mp, len);
    }
    return ret;
}

#ifdef _DEBUG_IBIO
#include <string.h>
#include "timer.h"
#define str "dskfjadslfkhdslfkjdsklfjdklsjfkladjfkldsjflkdsjflk"
int main()
{
    int fd = -1, n = 0;
    off_t off = 1024;
    void *timer = NULL;

    if((fd = open("/tmp/ibio.lock", O_CREAT|O_RDWR, 0644)) > 0)
    {
        TIMER_INIT(timer);
        itruncate(fd, off + (random()%off) );
        while(1)
        {
            TIMER_SAMPLE(timer);
            off = ifsize(fd);
            TIMER_SAMPLE(timer);
            fprintf(stdout, "fsize:%lld time:%lld\n", off, PT_LU_USEC(timer));
            n = iwrite(fd, str, strlen(str), (off = (random()%off)));
            TIMER_SAMPLE(timer);
            fprintf(stdout, "write to offset[%lld] str[%d] time:%lld\n", off, n, PT_LU_USEC(timer));
            n = iappend(fd, str, strlen(str), &off);
            TIMER_SAMPLE(timer);
            fprintf(stdout, "append to offset[%lld] str[%d] time:%lld\n", off, n, PT_LU_USEC(timer));
            usleep(1);
        }
        TIMER_CLEAN(timer);
        close(fd);
    }
}
#endif
