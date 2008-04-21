#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
/* Compress data */
int zcompress(char *data, uLong ndata, 
	char *zdata, uLong *nzdata)
{
	z_stream c_stream;
	int err = 0;

	if(data && ndata > 0)
	{
		c_stream.zalloc = (alloc_func)0;
		c_stream.zfree = (free_func)0;
		c_stream.opaque = (voidpf)0;
		deflateInit(&c_stream, Z_DEFAULT_COMPRESSION);
		c_stream.next_in  = (Bytef *)data;
		c_stream.next_out = (Bytef *)zdata;
		while (c_stream.total_in != ndata && c_stream.total_out < *nzdata) 
		{
			c_stream.avail_in = c_stream.avail_out = 1; /* force small buffers */
			if(deflate(&c_stream, Z_NO_FLUSH) != Z_OK) return -1;
		}
		for (;;) {
			c_stream.avail_out = 1;
			if((err = deflate(&c_stream, Z_FINISH)) == Z_STREAM_END) break;
			if(err != Z_OK) return -1;
			
		}
		if(deflateEnd(&c_stream) != Z_OK) return -1;
		*nzdata = c_stream.total_out;
		return 0;
	}
	return -1;
}

/* Uncompress data */
int zdecompress(char *zdata, uLong nzdata,                 
        char *data, uLong *ndata)
{
	int err = 0;
	z_stream d_stream; /* decompression stream */

	d_stream.zalloc = (alloc_func)0;
	d_stream.zfree = (free_func)0;
	d_stream.opaque = (voidpf)0;

	d_stream.next_in  = (Byte *)zdata;
	d_stream.avail_in = 0;
	d_stream.next_out = (Byte *)data;

	if(inflateInit(&d_stream) != Z_OK) return -1;
	while (d_stream.total_out < *ndata && d_stream.total_in < nzdata) {
		d_stream.avail_in = d_stream.avail_out = 1; /* force small buffers */
		if((err = inflate(&d_stream, Z_NO_FLUSH)) == Z_STREAM_END) break;
		if(err != Z_OK) return -1;
	}
	if(inflateEnd(&d_stream) != Z_OK) return -1;
	*ndata = d_stream.total_out;
	return 0;
}
#ifdef _DEBUG_ZSTREAM
#define BUF_SIZE 1024
int main()
{
	char *data = "kjdalkfjdflkjdlkfjdklfjdlkfjlkdjflkdjflddajfkdjfkdfaskf;ldsfk;ldakf;ldskfl;dskf;ld";	
	uLong ndata = strlen(data);	
	char zdata[BUF_SIZE];
	uLong nzdata = BUF_SIZE;
	char  odata[BUF_SIZE];
	uLong nodata = BUF_SIZE;
	
	memset(zdata, 0, BUF_SIZE);
	if(zcompress(data, ndata, zdata, &nzdata) == 0)
	{
		fprintf(stdout, "nzdata:%d %s\n", nzdata, zdata);
		memset(odata, 0, BUF_SIZE);
		if(zdecompress(zdata, nzdata, odata, &nodata) == 0)
		{
			fprintf(stdout, "%d %d %s\n", ndata, nodata, odata);
		}
	}
}
#endif
