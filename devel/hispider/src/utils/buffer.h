#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#ifndef _BUFFER_H
#define _BUFFER_h
#ifdef __cplusplus
extern "C" {
#endif

#ifndef _TYPEDEF_BUFFER
#define _TYPEDEF_BUFFER
typedef struct _BUFFER
{
	void *data;
	void *end;
	size_t size;
	void *mutex;

	void 	*(*calloc)(struct _BUFFER *, size_t);
	void 	*(*malloc)(struct _BUFFER *, size_t);
	void 	*(*recalloc)(struct _BUFFER *, size_t);
	void 	*(*remalloc)(struct _BUFFER *, size_t);
	int 	(*push)(struct _BUFFER *, void *, size_t);
	int 	(*del)(struct _BUFFER *, size_t);
	void 	(*reset)(struct _BUFFER *);
	void 	(*clean)(struct _BUFFER **);

}BUFFER;

#define BUFFER_VIEW(buf) \
{ \
	if(buf) \
	{ \
		fprintf(stdout, "buf:%08X\n" \
		"buf->data:%08X\n" \
		"buf->end:%08X\n" \
		"buf->size:%ld\n" \
		"buf->recalloc():%08X\n" \
		"buf->remalloc():%08X\n" \
		"buf->push():%08X\n" \
		"buf->del():%08X\n" \
		"buf->reset():%08X\n" \
		"buf->clean():%08X\n", \
		buf, buf->data, buf->end, buf->size, \
		buf->recalloc, buf->remalloc, \
		buf->push, buf->del, \
		buf->reset, buf->clean); \
	} \
}
struct _BUFFER *buffer_init();
#endif

/* calloc memory at end of buffer */
void* buf_calloc(BUFFER *, size_t);
/* malloc memory at end of buffer */
void* buf_malloc(BUFFER *, size_t);
/* recalloc memory */
void* buf_recalloc(BUFFER *, size_t);
/* remalloc memory */
void* buf_remalloc(BUFFER *, size_t);
/* push data to buffer tail */
int  buf_push(BUFFER *, void *, size_t);
/* delete data from buffer */
int buf_del(BUFFER *, size_t);
/* reset buffer */
void buf_reset(BUFFER *);
/* clean and free */
void  buf_clean(BUFFER **);

#ifdef __cplusplus
 }
#endif
#endif

