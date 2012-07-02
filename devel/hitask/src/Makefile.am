#libhibase
#lib_LTLIBRARIES = libhibase.la
#libhibase_la_SOURCES = hibase.c hibase.h utils/trie.h utils/trie.c utils/timer.h utils/logger.h
#libhibase_la_CPPFLAGS = -I utils -D_FILE_OFFSET_BITS=64 -Wall
#include_HEADERS = hibase.h
#hitaskd hitask
bin_PROGRAMS=hitaskd hitask
hitaskd_SOURCES= hitaskd.c evdns.c evdns.h ltask.c ltask.h htmlbase64.h hibase.h hibase.c \
hio.h url.h utils/http.h utils/http.c utils/fqueue.h utils/kvmap.c utils/kvmap.h utils/logger.h \
utils/md5.c utils/md5.h utils/mmtree.c utils/mutex.h utils/queue.h utils/timer.h \
utils/trie.c utils/trie.h utils/base64.h utils/base64.c utils/zstream.c utils/zstream.h \
iniparser.c iniparser.h utils/tm.c utils/tm.h utils/mmtree.h xbase.h 
hitaskd_CPPFLAGS= -I utils -D_FILE_OFFSET_BITS=64 -D_HTTP_CHARSET_CONVERT -Wall
hitaskd_LDFLAGS= -lz -lsbase -lpcre -lchardet
#hitask 
hitask_SOURCES= hitask.c ltask.h utils/http.h utils/http.c utils/logger.h utils/trie.h utils/trie.c \
utils/mutex.h utils/queue.h utils/timer.h utils/zstream.c utils/zstream.h \
iniparser.c iniparser.h doctype.h doctype.c  
hitask_CPPFLAGS= -I utils -D_FILE_OFFSET_BITS=64 -D_HTTP_CHARSET_CONVERT -Wall
hitask_LDFLAGS= -lz -lsbase -lchardet
