#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mmap.h>
#include <sys/stat.h>
#include "hibase.h"

int hibase_mkdir(char *path, int mode)
{
   char *p = NULL, fullpath[HIBASE_PATH_MAX];
   int ret = 0, level = -1;
   struct stat st;

   if(path)
   {
       strcpy(fullpath, path);
       p = fullpath;
       while(*p != '\0')
       {
           if(*p == '/' )
           {
               level++;
               while(*p != '\0' && *p == '/' && *(p+1) == '/')++p;
               if(level > 0)
               {
                   *p = '\0';
                   memset(&st, 0, sizeof(struct stat));
                   ret = stat(fullpath, &st);
                   if(ret == 0 && !S_ISDIR(st.st_mode)) return -1;
                   if(ret != 0 && mkdir(fullpath, mode) != 0) return -1;
                   *p = '/';
               }
           }
           ++p;
       }
       return 0;
   }
   return -1;
}

/* set basedir */
int hibase_set_basedir(HIBASE *hibase, char *dir)
{
	if(hibase && dir)
	{
		
	}
	return -1;
}
