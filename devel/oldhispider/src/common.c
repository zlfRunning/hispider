#include <stdio.h>
#include <string.h>
#include "common.h"
/* Convert String to unsigned long long int */
long long str2ll(char *str)
{
        char *s = str;
        long long ll  = 0ll;
        long long n = 1ll;
        while( *s >= '0' && *s <= '9'){s++;}
        while(s > str)
        {
                ll += ((*--s) - 48) * n;
                n *= 10ll;
        }
        return ll;
}

/* converts hex char (0-9, A-Z, a-z) to decimal */
char hex2int(unsigned char hex)
{
        hex = hex - '0';
        if (hex > 9) {
                hex = (hex + '0' - 1) | 0x20;
                hex = hex - 'a' + 11;
        }
        if (hex > 15)
                hex = 0xFF;

        return hex;
}



void urlencode(unsigned char *s, unsigned char *t) {
    unsigned char *p = NULL, *tp = NULL;
    int n = 0;
    if(t == NULL) return ;
    tp = t;
    for(p = s; *p; p++)
    {
        if((*p > 0x00 && *p < ',') ||
                (*p > '9' && *p < 'A') ||
                (*p > 'Z' && *p < '_') ||
                (*p > '_' && *p < 'a') ||
                (*p > 'z' && *p < 0xA1)) {
            tp += sprintf((char *)tp, "%%%02X", (*p));
        } else {
            *tp = *p;
            tp++;
        }
    }
    *tp='\0';
    return;
}

/* http URL decode */
void urldecode(char *url)
{
    unsigned char high, low;
    char *src = url;
    char *dst = url;
    int query = 0;
    while (*src != '\0')
    {
        if(*src == '?')
        {
            query = 1;
        }
        //if (*src == '+' && query)
        if (*src == '+')
        {
            *dst = 0x20;
        }
        else if (*src == '%')
        {
            *dst = '%';
            high = hex2int(*(src + 1));
            if (high != 0xFF)
            {
                low = hex2int(*(src + 2));
                if (low != 0xFF)
                {
                    high = (high << 4) | low;
                    if (high < 32 || high == 127) high = '_';
                    *dst = high;
                    src += 2;
                }
            }
        }
        else
        {
            *dst = *src;
        }
        dst++;
        src++;
    }
    *dst = '\0';
}
