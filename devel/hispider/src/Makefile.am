#Automake
bin_PROGRAMS = hispiderd hispider
hispiderd_SOURCES = basedef.h \
evdns.c \
evdns.h \
hispiderd.c \
http.c \
http.h \
iniparser.c \
iniparser.h \
ltable.c \
ltable.h \
utils/ibio.c \
utils/ibio.h \
utils/logger.h \
utils/kvmap.h \
utils/kvmap.c \
utils/md5.c \
utils/md5.h \
utils/mutex.h \
utils/queue.h \
utils/timer.h \
utils/trie.c \
utils/trie.h \
utils/zstream.c \
utils/zstream.h
hispiderd_CPPFLAGS = -I utils -D_FILE_OFFSET_BITS=64 -Wall
hispiderd_LDFLAGS = -lsbase -lz 

hispider_SOURCES = basedef.h \
hispider.c \
http.c \
http.h \
iniparser.c \
iniparser.h \
utils/logger.h \
utils/md5.c \
utils/md5.h \
utils/mutex.h \
utils/queue.h \
utils/timer.h \
utils/trie.c \
utils/trie.h \
utils/zstream.c \
utils/zstream.h

hispider_CPPFLAGS = -I utils -D_FILE_OFFSET_BITS=64 -Wall
hispider_LDFLAGS = -lsbase -lz -lchardet
