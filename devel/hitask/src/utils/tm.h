#ifndef _TM_H
#define _TM_H
//convert str datetime to time
time_t str2time(char *datestr);
/* time to GMT */
int GMTstrdate(time_t time, char *date);
#endif
