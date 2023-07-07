LIB_NAME=ffmalloc
LIB_SHARED_MT=lib${LIB_NAME}_mt.so
LIB_SHARED_ST=lib${LIB_NAME}_st.so
LIB_SHARED_MT_LARGE=lib${LIB_NAME}_mt_large.so
LIB_SHARED_ST_LARGE=lib${LIB_NAME}_st_large.so

LIB_OBJS_MT=ffmalloc_mt.o
LIB_OBJS_ST=ffmalloc_st.o
LIB_OBJS_MT_LARGE=ffmalloc_mt_large.o
LIB_OBJS_ST_LARGE=ffmalloc_st_large.o

CFLAGS=-Wall -Wextra -Wno-unknown-pragmas -fPIC -c -g -O3 -DFF_GROWLARGEREALLOC -D_GNU_SOURCE

all: ${LIB_SHARED_MT} ${LIB_SHARED_ST} ${LIB_SHARED_MT_LARGE} ${LIB_SHARED_ST_LARGE}

${LIB_SHARED_MT}: ${LIB_OBJS_MT}
	${CC} -o $@ -shared -fPIC -pthread $^

${LIB_SHARED_ST}: ${LIB_OBJS_ST}
	${CC} -o $@ -shared -fPIC $^

${LIB_SHARED_MT_LARGE}: ${LIB_OBJS_MT_LARGE}
	${CC} -o $@ -shared -fPIC -pthread $^

${LIB_SHARED_ST_LARGE}: ${LIB_OBJS_ST_LARGE}
	${CC} -o $@ -shared -fPIC $^

${LIB_OBJS_MT}: ffmalloc.c
	${CC} ${CFLAGS} ffmalloc.c -o $@

${LIB_OBJS_ST}: ffmalloc.c
	${CC} ${CFLAGS} -DFFSINGLE_THREADED ffmalloc.c -o $@

${LIB_OBJS_MT_LARGE}: ffmalloc.c
	${CC} ${CFLAGS} -DUSE_LARGE_CHUNK ffmalloc.c -o $@

${LIB_OBJS_ST_LARGE}: ffmalloc.c
	${CC} ${CFLAGS} -DFFSINGLE_THREADED -DUSE_LARGE_CHUNK ffmalloc.c -o $@

ffmalloc.c: ffmalloc.h

clean:
	rm *.o
	rm *.so
