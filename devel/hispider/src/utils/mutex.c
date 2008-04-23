#include <unistd.h>
#include <stdlib.h>
#include "mutex.h"
/* Initialize */
MUTEX *mutex_init()
{
    MUTEX *mutex = NULL;
    if((mutex = (MUTEX *)calloc(1, sizeof(MUTEX))))
    {
        return mutex; 
    }
    return NULL;
}

/* Lock */
int mutex_lock(MUTEX *mutex)
{
    int n = 0;
    if(mutex)
    {
        n = mutex->lockid++;
        for(;;)
        {
            if(mutex->current == n) return 0;
            usleep(__MUTEX__WAIT__);
        }
    }
    return -1;
}

/* Unlock*/
int mutex_unlock(MUTEX *mutex)
{
    if(mutex)
    {
        mutex->current++;
        return 0;
    }
    return -1;
}

/* Destroy */
int mutex_destroy(MUTEX **mutex)
{
    if(*mutex)
    {
        free(*mutex);
        (*mutex) = NULL;
        return 0;
    }
}

#ifdef _DEBUG_MUTEX
#include <stdio.h>
int main()
{
    int i = 0;
    MUTEX *mutex = NULL;
    
    if((mutex = mutex_init()))
    {
        mutex_lock(mutex);
        while(1)
        {

            if((i % 200)  == 0 && mutex_lock(mutex) == 0)
            {
                fprintf(stdout, "Lock on %d successed\n", i);
            }
            usleep(1000);
            i++;
            if((i % 200)  == 0 && mutex_unlock(mutex) == 0)
            {
                fprintf(stdout, "Unlock on %d successed\n", i);
            }
        }
    }
}
#endif
