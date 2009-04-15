#libhibase
lib_LTLIBRARIES = libhibase.la
libhibase_la_SOURCES = hibase.c hibase.h utils/trie.h utils/trie.c utils/timer.h utils/logger.h
libhibase_la_CPPFLAGS = -I utils -D_FILE_OFFSET_BITS=64 -Wall
include_HEADERS = ibase.h
#hitaskd hitask
bin_PROGRAMS=hitaskd hitask
hitaskd_SOURCES= hitaskd.c evdns.c evdns.h http.c http.h ltask.c ltask.h \
utils/fqueue.h utils/kvmap.c utils/kvmap.h utils/logger.h utils/md5.c utils/md5.h \
utils/mutex.h utils/queue.h utils/timer.h utils/trie.c utils/trie.h \
utils/zstream.c utils/zstream.h iniparser.c iniparser.h
hitaskd_CPPFLAGS= -I utils -D_FILE_OFFSET_BITS=64 -Wall
hitaskd_LDFLAGS= -lz -lsbase
#hitask 
hitask_SOURCES= hitask.c ltask.h utils/logger.h utils/http.h utils/http.c \
utils/mutex.h utils/queue.h utils/timer.h utils/zstream.c utils/zstream.h \
iniparser.c iniparser.h
hitask_CPPFLAGS= -I utils -D_FILE_OFFSET_BITS=64 -Wall
hitask_LDFLAGS= -lz -lsbase
