#include <string.h>
#ifndef _COMMON_H_
#define _COMMON_H_
/* Convert String to unsigned long long int */
unsigned long long str2llu(char *str);

/* converts hex char (0-9, A-Z, a-z) to decimal */
char hex2int(unsigned char hex);
/* HTTP URL encode */
void urlencode(unsigned char *s, unsigned char *t);
/* http URL decode */
void urldecode(char *url);
#endif
