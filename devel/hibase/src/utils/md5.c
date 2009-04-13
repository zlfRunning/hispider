#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include "md5.h"
//32768=32K 65536=64K 131072=128K 262144=256K 524288=512K 786432=768K 
////1048576=1M  2097152=2M 4194304=4M 8388608 = 8M 16777216=16M  33554432=32M
#define  MD5_BUF_SIZE   1048576
/* Initialize */
void md5_init(MD5_CTX *context)
{
	MEMSET(context, 0, sizeof(MD5_CTX));
	context->state[0] = 0x67452301;
	context->state[1] = 0xefcdab89;
	context->state[2] = 0x98badcfe;
	context->state[3] = 0x10325476;
}

/* Update */
void md5_update(MD5_CTX *context, unsigned char *data, u_int32_t ndata)
{
	//u_int32_t x[_MD5_BLOCK_N];
	u_int32_t i = 0, offset = 0, npart = 0;
	offset = (u_int32_t)((context->total[0] >> 3) & 0x3f);	
	if((context->total[0] += ((u_int32_t)ndata << 3)) < ((u_int32_t)ndata << 3))
		context->total[1]++;
	context->total[1] += ((u_int32_t)ndata >> 29);
	npart = _MD5_BLOCK_N - offset;
	if(ndata >= npart)
	{
		MEMCPY((context->buf + offset), data, npart);	
		MD5_CALCULATE(context, context->buf, _MD5_BLOCK_N);
		MEMSET(context->buf, 0, _MD5_BLOCK_N);
		for(i = npart; (i + _MD5_BLOCK_N - 1) < ndata; i += _MD5_BLOCK_N)
		{
			MD5_CALCULATE(context, (data + i), _MD5_BLOCK_N);
		}
		offset = 0;
	}
	else i = 0;
	MEMCPY((context->buf + offset), (data + i), (ndata - i));
	//fprintf(stdout, "buffer:%s\n", context->buf);
}

/* Final */
void md5_final(MD5_CTX *context)
{
	unsigned char bits[_MD5_BITS_N];
	u_int32_t  index = 0, npad = 0;	
	if(context)
	{
		ENCODE(bits, context->total, 8);
		index	= (u_int32_t) ((context->total[0] >> 3) & 0x3f);
		npad	= (index < _MD5_SET_N) ? (_MD5_SET_N - index ) 
				: (_MD5_SET_N + _MD5_BLOCK_N - index); 
		md5_update(context, PADDING, npad);	
		md5_update(context, bits, _MD5_BITS_N);
		ENCODE(context->digest, context->state, MD5_LEN);		
	}
}

/* md5 */
void md5(unsigned char *data, u_int32_t ndata, unsigned char *digest)
{
        MD5_CTX ctx;
        md5_init(&ctx);
        md5_update(&ctx, data, ndata);
        md5_final(&ctx);
        memcpy(digest, ctx.digest, MD5_LEN);
}


/* Cacalute FILE md5 */
int md5_file(const char *file, unsigned char *digest)
{
//32768=32K 65536=64K 131072=128K 262144=256K 524288=512K 786432=768K 
//1048576=1M  2097152=2M 4194304=4M 8388608 = 8M 16777216=16M  33554432=32M
	MD5_CTX ctx;
	//unsigned char buf[MD5_BUF_SIZE];
    unsigned char *p = NULL;
	int fd = 0, n = 0;
	struct stat st;
	if(file && stat(file, &st) == 0 && S_ISREG(st.st_mode) )
	{
		if((fd = open(file, O_RDONLY)) > 0)	
        {
            if((p = (unsigned char *)calloc(1, MD5_BUF_SIZE)))
            {
                md5_init(&ctx);
                while(( n = read(fd, p, MD5_BUF_SIZE)) > 0)
                {
                    md5_update(&ctx, p, (u_int32_t)n);	
                }
                md5_final(&ctx);
                memcpy(digest, ctx.digest, MD5_LEN);
                free(p);
            }
            close(fd);
            return 0;
        }
	}
	return -1;
}

#ifdef _DEBUG_MD5
int main(int argc, char **argv)
{
	int i = 0, j = 0;
	unsigned char digest[MD5_LEN];
	if(argc < 2)
	{
		fprintf(stderr, "Usage:%s string1 string2 ...\n", argv[0]);	
		_exit(-1);
	}	
	for(i = 1; i < argc; i++)
	{
		MD5(argv[i], strlen(argv[i]), digest);
		MD5OUT(digest, stdout);
		fprintf(stdout, " %s\n", argv[i]);
	}
}
#endif


#ifdef _DEBUG_MD5FILE
int main(int argc, char **argv)
{
	int i = 0, j = 0;
	unsigned char digest[MD5_LEN];
	if(argc < 2)
	{
		fprintf(stderr, "Usage:%s file1 file2 ...\n", argv[0]);	
		_exit(-1);
	}	
	for(i = 1; i < argc; i++)
	{
		if(md5_file(argv[i], digest) == 0)
		{
			MD5OUT(digest, stdout);
			fprintf(stdout, " %s\n", argv[i]);
		}
	}
}
#endif
