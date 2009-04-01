#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mmap.h>
#include <sys/stat.h>
#include "hibase.h"
#define  HIBASE_TABLE_NAME   		".table"
#define  HIBASE_TEMPLATE_NAME 		".template"

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
	char path[HIBASE_PATH_MAX];
	int n = 0;

	if(hibase && dir)
	{
		n = sprintf(hibase->basedir, "%s/", dir);
		hibase_mkdir(hibase->basedir);
		//resume table
		sprintf(path, "%s%s", hibase->basedir, HIBASE_TABLE_NAME);	
		if((fd = open(path, O_CREAT|O_RDWR, 0644)) > 0)
		{
		}
		//resume template 

	}
	return -1;
}
