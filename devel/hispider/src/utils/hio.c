#include <stdio.h>
#include <stdlib.h>
#include "hio.h"
#ifdef _DEBUG_HIO
#define BUF_SIZE 1024
int main()
{
    char buf[BUF_SIZE];
    char *s = "kjdfaljfladjfldfjldkfjdlksfjldsfjldsfjldjsfl";
    void *hio = NULL;
    int n = 0;
    long long offset = 0;

    HIO_INIT(hio);
    HIO_SET(hio, "/tmp/list");
    HIO_WRITE(hio, s, strlen(s)); 
    HIO_SWRITE(hio, s, strlen(s), 20); 
    HIO_APPEND(hio, s, strlen(s), offset); 
    HIO_READ(hio, buf, BUF_SIZE);
    fprintf(stdout, "%s\n", buf);
    HIO_SREAD(hio, buf, BUF_SIZE, 30);
    fprintf(stdout, "%s\n", buf);
    HIO_CLEAN(hio);
}
#endif
