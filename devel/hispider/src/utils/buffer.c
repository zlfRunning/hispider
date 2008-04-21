#include "buffer.h"
/* Initialize buffer */
BUFFER *buffer_init()
{
	BUFFER *buf = (BUFFER *)calloc(1, sizeof(BUFFER));
	if(buf)
        {
                buf->recalloc   = buf_recalloc;
                buf->remalloc   = buf_remalloc;
		buf->malloc	= buf_malloc;
		buf->calloc	= buf_calloc;
                buf->push       = buf_push;
                buf->del        = buf_del;
                buf->reset      = buf_reset;
                buf->clean      = buf_clean;
#ifdef HAVE_PTHREAD
		buf->mutex	= calloc(1, sizeof(pthread_mutex_t));
		if(buf->mutex) pthread_mutex_init((pthread_mutex_t *)buf->mutex, NULL);	
#endif
        }
	return buf;
}

/* calloc memory at end of buffer */
void* buf_calloc(BUFFER *buf, size_t size)
{
	void *ret  = NULL;	
	if(buf)
	{
#ifdef HAVE_PTHREAD
		if(buf->mutex) pthread_mutex_lock((pthread_mutex_t *)buf->mutex);
#endif

		if(buf->data)
		{
			buf->data = realloc(buf->data, buf->size + size);	
			ret = ((char *)buf->data) + buf->size;
                        memset(ret, 0, size);
		}
		else
		{
			buf->data = calloc(1, size);
			ret = buf->data;
		}
		buf->size += size;
		buf->end = (char *)(buf->data) + buf->size;
#ifdef HAVE_PTHREAD
		if(buf->mutex) pthread_mutex_unlock((pthread_mutex_t *)buf->mutex);
#endif

	}
	return 	ret;
}


/* malloc memory at end of buffer */
void* buf_malloc(BUFFER *buf, size_t size)
{
	void *ret = NULL;
	if(buf)
        {
#ifdef HAVE_PTHREAD
                if(buf->mutex) pthread_mutex_lock((pthread_mutex_t *)buf->mutex);
#endif

                if(buf->data)
                {
                        buf->data = realloc(buf->data, buf->size + size); 
			ret = ((char *)buf->data) + buf->size;
                }
                else
                {
                        buf->data = malloc(size);
			ret = buf->data;
                } 
		buf->size += size;
                buf->end = (char *)(buf->data) + buf->size;
#ifdef HAVE_PTHREAD
                if(buf->mutex) pthread_mutex_unlock((pthread_mutex_t *)buf->mutex);
#endif

        }
        return  ret;	
}

/* recalloc memory */
void* buf_recalloc(BUFFER *buf, size_t size)
{
	void *ret  = NULL;	
	if(buf)
	{
#ifdef HAVE_PTHREAD
		if(buf->mutex) pthread_mutex_lock((pthread_mutex_t *)buf->mutex);
#endif

		if(buf->data)
		{
			buf->data = realloc(buf->data, size);	
			if(buf->data) memset(buf->data, 0, size);
		}
		else
		{
			buf->data = calloc(1, size);
		}
		if(buf->data)
		{
			buf->size = size;
			buf->end = (char *)(buf->data) + size;
			ret = buf->data;
		}
		else
		{
			buf->size = 0;
                        buf->data = NULL;
			buf->end = NULL;
		}
#ifdef HAVE_PTHREAD
		if(buf->mutex) pthread_mutex_unlock((pthread_mutex_t *)buf->mutex);
#endif

	}
	return 	ret;
}


/* remalloc memory */
void* buf_remalloc(BUFFER *buf, size_t size)
{
	void *ret = NULL;
	if(buf)
        {
#ifdef HAVE_PTHREAD
                if(buf->mutex) pthread_mutex_lock((pthread_mutex_t *)buf->mutex);
#endif

                if(buf->data)
                {
                        buf->data = realloc(buf->data, size); 
                }
                else
                {
                        buf->data = malloc(size);
                } 
		if(buf->data)
                {
                        buf->size = size;
                        buf->end = (char *)(buf->data) + size;
                        ret = buf->data;
                }
                else
                {
                        buf->size = 0;
                        buf->data = NULL;
                        buf->end = NULL;
                }
#ifdef HAVE_PTHREAD
                if(buf->mutex) pthread_mutex_unlock((pthread_mutex_t *)buf->mutex);
#endif

        }
        return  ret;	
}

/* push data to buffer tail */
int  buf_push(BUFFER *buf, void *data, size_t size)
{
	char *p = NULL;
	int ret = -1;
	if(buf)
	{
#ifdef HAVE_PTHREAD
		if(buf->mutex) pthread_mutex_lock((pthread_mutex_t *)buf->mutex);
#endif
		buf->data = realloc(buf->data, (buf->size + size));
		buf->size += size;
		buf->end  =  buf->data + buf->size;
		if(buf->end && memcpy(((char *)(buf->end) - size), data, size) != NULL )
		{
			ret = 0;
		}
#ifdef HAVE_PTHREAD
		if(buf->mutex) pthread_mutex_unlock((pthread_mutex_t *)buf->mutex);
#endif

	}
	return ret;
}

/* delete data from buffer head */
int buf_del(BUFFER *buf, size_t size)
{
	int ret = -1;
	if(buf)
	{
#ifdef HAVE_PTHREAD
                if(buf->mutex) pthread_mutex_lock((pthread_mutex_t *)buf->mutex);
#endif
	
		if(buf->data)
		{
			if(buf->size > size )
			{
				if(memmove(buf->data, (char *)(buf->data) + size,
					 buf->size - size) == buf->data)
				{
					buf->data =  realloc(buf->data, (buf->size - size));
					buf->size -= size;
					buf->end  =  buf->data + buf->size;
					ret = 0;
				}				
				else
				{
					ret = -1;
				}
			}
			else
			{
				if(buf->data) free(buf->data);
				buf->data = NULL;
				buf->size = 0;
				buf->end = NULL;
			}
		}
#ifdef HAVE_PTHREAD
                if(buf->mutex) pthread_mutex_unlock((pthread_mutex_t *)buf->mutex);
#endif

	}
	return ret;
}

/* reset buffer */
void buf_reset(BUFFER *buf)
{
	if(buf)
	{                       
#ifdef HAVE_PTHREAD
		if(buf->mutex) pthread_mutex_lock((pthread_mutex_t *)buf->mutex);
#endif
		if(buf->data) free(buf->data);
		buf->data = NULL;
		buf->size = 0;
		buf->end = NULL;
#ifdef HAVE_PTHREAD
		if(buf->mutex) pthread_mutex_unlock((pthread_mutex_t *)buf->mutex);
#endif

	}
}

/* clean and free */
void buf_clean(BUFFER **buf)
{
	if((*buf))
	{
#ifdef HAVE_PTHREAD
	if((*buf)->mutex)
	{
		pthread_mutex_unlock((pthread_mutex_t *)(*buf)->mutex);
		pthread_mutex_destroy((pthread_mutex_t *)(*buf)->mutex);
		free((*buf)->mutex);
	}
#endif
		if((*buf)->data) free((*buf)->data);	
		free((*buf));
		(*buf) = NULL;	
	}			
}

#ifdef _DEBUG_BUFFER
int main()
{
	BUFFER *buf = NULL;
	char *s = "dsfadfdsffd";	
	int n = 0;
	buf = buffer_init();	
	if(buf)
	{
		BUFFER_VIEW(buf);
		fprintf(stdout, "buf->push\n");
		buf->push(buf, s, strlen(s) + 1);	
		fprintf(stdout, "data:%s\n", buf->data);	
		BUFFER_VIEW(buf);
		fprintf(stdout, "buf->remalloc\n");
		buf->remalloc(buf, 1024);	
		BUFFER_VIEW(buf);
		sprintf((char *)buf->data, "akdladsjfkldjflkdfkdjflkdsfjsdk");
		fprintf(stdout, "data:%s\n", buf->data);	
		fprintf(stdout, "buf->recalloc\n");
		buf->recalloc(buf, 1024);	
		sprintf((char *)buf->data, "test", 4);
		fprintf(stdout, "data:%s\n", buf->data);	
		BUFFER_VIEW(buf);
		n = buf->del(buf, 2);	
		fprintf(stdout, "buf->del(buf, 2) = %d\n", n);
		fprintf(stdout, "data:%s\n", buf->data);	
		BUFFER_VIEW(buf);
		fprintf(stdout, "buf->reset(buf)\n");
		buf->reset(buf);
		fprintf(stdout, "data:%s\n", buf->data);	
		BUFFER_VIEW(buf);
		fprintf(stdout, "buf->clean(buf, 2)\n");
		buf->clean(&buf);
		//BUFFER_VIEW(buf);
	}
}
#endif
