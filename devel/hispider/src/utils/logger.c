#include <stdio.h>
#include <string.h>
#include "logger.h"

/* Initialize LOGGER */
LOGGER *logger_init(char *logfile)
{
	//fprintf(stdout, "Initializing logger:%s\n", logfile);
	LOGGER *logger = (LOGGER *)calloc(1, sizeof(LOGGER));
	if(logger)
	{
		logger->add 	= logger_add;
		logger->close 	= logger_close;
		if(logfile)
		{
			strcpy(logger->file, logfile);
#ifdef HAVE_PTHREAD
			logger->mutex = calloc(1, sizeof(pthread_mutex_t));
			if(logger->mutex) pthread_mutex_init((pthread_mutex_t *)logger->mutex, NULL);
#endif

			if( (logger->fd = open(logger->file, O_CREAT |O_WRONLY |O_APPEND, 0644)) <= 0 )
			{
				fprintf(stderr, "FATAL:open log file[%s]  failed, %s",
						logfile, strerror(errno));
				logger->close(&logger);
			}
		}
		else
		{
			logger->fd = 1;
		}
	}
	return logger;
}

/* Add log */
void logger_add(LOGGER *logger, char *__file__, int __line__, int __level__, char *format, ...)
{ 
	va_list ap;
	char buf[LOGGER_LINE_LIMIT]; 
	char *s = buf;
	struct timeval tv; 
	time_t timep; 
	struct tm *p = NULL; 
	int n = 0; 
	if(logger) 
	{ 
#ifdef HAVE_PTHREAD
		if(logger->mutex) pthread_mutex_lock((pthread_mutex_t *)logger->mutex); 
#endif
		if(logger->fd) 
		{ 
			gettimeofday(&tv, NULL); 
			time(&timep); 
			p = localtime(&timep); 
			n = sprintf(s, 
					"[%02d/%s/%04d:%02d:%02d:%02d +%06u] [%u/%08x] #%s::%d# \"%s:", 
					p->tm_mday, ymonths[p->tm_mon], (1900+p->tm_year), p->tm_hour,  
					p->tm_min, p->tm_sec, (size_t)tv.tv_usec, (size_t)getpid(), 
					THREADID(), __file__, __line__, _logger_level_s[__level__]); 
			s += n;
			va_start(ap, format);
			n = vsprintf(s, format, ap);
			va_end(ap);
			s += n;
			n = sprintf(s, "\"\n");
			s += n;
			n = s - buf;
			if(write(logger->fd, buf, n) != n) 
			{ 
				fprintf(stderr, "FATAL:Writting LOGGER failed, %s", strerror(errno)); 
				close(logger->fd); 
				logger->fd = 0; 
			} 
		}	 
#ifdef HAVE_PTHREAD
		if(logger->mutex) pthread_mutex_unlock((pthread_mutex_t *)logger->mutex); 
#endif
	}	 
}

/* Close logger */
void logger_close(LOGGER **logger)
{ 
	if(*logger) 
	{ 
		if((*logger)->fd > 0 ) close((*logger)->fd); 
#ifdef HAVE_PTHREAD
		if((*logger)->mutex)
		{
                	pthread_mutex_unlock((pthread_mutex_t *) (*logger)->mutex); 
                	pthread_mutex_destroy((pthread_mutex_t *) (*logger)->mutex); 
			free((*logger)->mutex);
		}
#endif
		free((*logger)); 
		(*logger) = NULL; 
	} 
}

#ifdef _DEBUG_LOGGER
int main()
{
	LOGGER *logger = logger_init("/tmp/test.log");	
	if(logger)
	{
		DEBUG_LOGGER(logger, "调试信息 %s", "DEBUG");
               	WARN_LOGGER(logger, "警告信息 %s", "WARN");
                ERROR_LOGGER(logger, "错误信息 %s", "ERROR");
                FATAL_LOGGER(logger, "致命信息 %s", "FATAL");
		/*
		log->add(log, __FILE__, __LINE__, __DEBUG__, "调试信息 %s", "OK1");
		log->add(log, __FILE__, __LINE__, __WARN__, "警告信息 %s", "WARN");
		log->add(log, __FILE__, __LINE__, __ERROR__, "错误信息 %s", "ERROR");
		log->add(log, __FILE__, __LINE__, __FATAL__, "致命信息 %s", "FATAL");
		*/
		log->close(log);
	}
}
#endif
