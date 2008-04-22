bin_PROGRAMS = hispider 
hispider_SOURCES = hispider.c iniparser.c iniparser.h link.h link.c \
common.h common.c http.h http.c basedef.h \
utils/buffer.c utils/buffer.h utils/hash.c utils/hash.h utils/ilist.h \
utils/logger.c utils/logger.h utils/md5.c utils/md5.h utils/mutex.h \
utils/timer.c utils/timer.h utils/zstream.c utils/zstream.h utils/nio.h utils/nio.c
hispider_CPPFLAGS = -I utils -D_FILE_OFFSET_BITS=64
hispider_LDFLAGS = -lsbase -lz -lm
