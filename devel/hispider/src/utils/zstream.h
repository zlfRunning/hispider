#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#ifndef _ZSTREAM_H
#define _ZSTREAM_H
/* Compress data */
int zcompress(char *data, uLong ndata, 
	char *zdata, uLong *nzdata);
/* Uncompress data */
int zdecompress(char *zdata, uLong nzdata,
        char *data, uLong *ndata);
#endif
