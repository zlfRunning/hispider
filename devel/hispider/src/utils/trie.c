#include <stdio.h>
#include <unistd.h>
#include <string.h>
#ifdef _DEBUG_TRIE
#include "trie.h"
#include "timer.h"
#define LINE_MAX 1024
typedef  struct _RECORD
{
    char *key;
    char *data;
}RECORD;
#define NRECORD 24
static RECORD recdlist[] = {
    {"abc", "kdajfkldjflkdsjflkd"},
    {"abca", "kdajfkldjflkdsjflkd"},
    {"abcd", "ldjfaldsjfkdslfjdslkfjlds"},
    {"abcde", "kjLJDAFJDALFJA;DLFJL;AJL;"},
    {"abcdeb", "kjLJDAFJDALFJA;DLFJL;AJL;"},
    {"abcdefd", "dklajfiuiosdufaodsufouo"},
    {"abcdedc", "dklajfiuiosdufaodsufouo"},
    {"abcdee", "dklajfiuiosdufaodsufouo"},
    {"abcdefg", "dlfkja;ldsfkl;dsfkl;daskfl;d"},
    {"abcdefgd", "dlfkja;ldsfkl;dsfkl;daskfl;d"},
    {"abcdefgh", "dsa.fkl;dsfkl;dskf;ldskfl;dkf"},
    {"abcdefghe", "dsa.fkl;dsfkl;dskf;ldskfl;dkf"},
    {"abcdefghi", "ld;kfa;lsdfk;ldskf;ldskf;ldskf;ld"},
    {"abcdefghif", "ld;kfa;lsdfk;ldskf;ldskf;ldskf;ld"},
    {"abcdefghij", "dslfjaldsfjdslfjkdslfjldsjfldsfj"},
    {"abcdefghijg", "dslfjaldsfjdslfjkdslfjldsjfldsfj"},
    {"abcdefghijk", "lkjdfakldsjflsdkjfldsjfeow[rowe[r"},
    {"abcdefghijkh", "lkjdfakldsjflsdkjfldsjfeow[rowe[r"},
    {"abcdefghijkl", "sdfu9OIUIOUIODFAUSIOFUAOISFUOI"},
    {"abcdefghijkli", "sdfu9OIUIOUIODFAUSIOFUAOISFUOI"},
    {"abcdefghijklm", "kdjfakldsjfldsjfldsjflajdslfjdslf"},
    {"abcdefghijklmj", "kdjfakldsjfldsjfldsjflajdslfjdslf"},
    {"abcdefghijklmn", "dafhjkldsjfld;sjfl;dsjfl;dsjfl;djsal;f"},
    {"abcdefghijklmnk", "dafhjkldsjfld;sjfl;dsjfl;dsjfl;djsal;f"}
};
int main(int argc, char **argv)
{
    void *trietab = NULL;
    char *key = NULL;
    void *skey = NULL;
    void *pdata = NULL;
    char *file = NULL;
    unsigned char buf[LINE_MAX];
    unsigned char *p = NULL;
    int i = 0, n = 0;
    long wordid = 1;
    FILE *fp = NULL;
    TIMER *timer = NULL;
    long long insert_total = 0, query_total = 0;
    /*
     *
    */
    if(argc < 2)
    {
        fprintf(stderr, "Usage:%s wordfile\n", argv[0]);
        _exit(-1);
    }
    file = argv[1];
    if((trietab = TRIETAB_INIT()))
    {
        if((fp = fopen(file, "r")))
        {
            TIMER_INIT(timer);
            while(fgets(buf, LINE_MAX, fp))
            {
                p = buf;
                while(*p != '\n' && *p != '\0')p++;
                *p = '\0';
                n = p - buf;
                //fprintf(stdout, "%d:%s\n", n, buf);
                TIMER_SAMPLE(timer);
                TRIETAB_ADD(trietab, buf, n, (void *)wordid++);
                TIMER_SAMPLE(timer);
                insert_total += PT_LU_USEC(timer);
            }
            fprintf(stdout, "wordid:%d count:%d size:%d\n"
                    "insert_usec:%lld insert_avg:%f\n"
                    "query_usec:%lld query_avg:%f\n", 
                    wordid, HBCNT(trietab), HBSIZE(trietab), 
                    insert_total, (double)(insert_total/wordid),
                    query_total, (double)(query_total/wordid));

            fseek(fp, 0, SEEK_SET);
            while(fgets(buf, LINE_MAX, fp))
            {
                p = buf;
                while(*p != '\n' && *p != '\0')p++;
                *p = '\0';
                n = p - buf;
                TIMER_SAMPLE(timer);
                TRIETAB_GET(trietab, buf, n, pdata);
                TIMER_SAMPLE(timer);
                query_total += PT_LU_USEC(timer);
                if(pdata) fprintf(stdout, "%d\n", (long)pdata);
            }
            fprintf(stdout, "wordid:%d count:%d size:%d\n"
                    "insert_usec:%lld insert_avg:%f\n"
                    "query_usec:%lld query_avg:%f\n", 
                    wordid, HBCNT(trietab), HBSIZE(trietab), 
                    insert_total, (double)(insert_total/wordid),
                    query_total, (double)(query_total/wordid));
            TIMER_CLEAN(timer);
            fclose(fp);
        }
    }
    /*
    if((trietab = TRIETAB_INIT()))
    {
        for(i = 0; i < NRECORD; i++)
        {
            key = recdlist[i].key;
            skey = recdlist[i].data;
            TRIETAB_ADD(trietab, key, strlen(key), skey);
        }
        for(i = 0; i < NRECORD; i++)
        {
            key = recdlist[i].key;
            skey = recdlist[i].data;
            TRIETAB_GET(trietab, key, strlen(key), pdata);
            if(pdata) fprintf(stdout, "get:%s\nold:%s\n", pdata, skey);
        }
        fprintf(stdout, "count:%d size:%d\n", HBCNT(trietab), HBSIZE(trietab));
    }
    */
    return 0;
}
#endif
