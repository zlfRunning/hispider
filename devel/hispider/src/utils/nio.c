#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "nio.h"
#ifdef _DEBUG_NIO
int main()
{
    void *nio = NULL;
    char *es = NULL;
    char *s = "daffdsfsdfdf";
    nio = NIO_INIT();
    NIO_SET(nio, "/tmp/test.txt");
    if(NIO_CHECK(nio) == -1)
        fprintf(stdout, "open file failed, %s\n", strerror(errno));
    else fprintf(stdout, "nio->fd:%d", NIO_FD(nio));
    if(NIO_WRITE(nio, s, strlen(s)) > 0)
    {
        s = "dssddddddddddddddddd";
        NIO_SWRITE(nio, s, strlen(s), 10);
    }
    es = "ldsfasdjljkldsfjasdkjfldsfjldkasfjkld";
    if(NIO_SEEK(nio, 20) >= 0)
    {
        NIO_WRITE(nio, es, strlen(es));
    }
    es = "你好";
    NIO_APPEND(nio, es, strlen(es));
    NIO_CLEAN(nio);
}
#endif
