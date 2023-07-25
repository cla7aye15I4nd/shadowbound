LIB_NAME=ffmalloc
LIB_SHARED_MT=lib${LIB_NAME}_mt.so
LIB_SHARED_ST=lib${LIB_NAME}_st.so
LIB_SHARED_MT_LARGE=lib${LIB_NAME}_mt_large.so
LIB_SHARED_ST_LARGE=lib${LIB_NAME}_st_large.so
LIB_SHARED_MT_PERF=lib${LIB_NAME}_mt_perf.so
LIB_SHARED_ST_PERF=lib${LIB_NAME}_st_perf.so
LIB_SHARED_MT_LARGE_PERF=lib${LIB_NAME}_mt_large_perf.so
LIB_SHARED_ST_LARGE_PERF=lib${LIB_NAME}_st_large_perf.so

LIB_OBJS_MT=ffmalloc_mt.o
LIB_OBJS_ST=ffmalloc_st.o
LIB_OBJS_MT_LARGE=ffmalloc_mt_large.o
LIB_OBJS_ST_LARGE=ffmalloc_st_large.o
LIB_OBJS_MT_PERF=ffmalloc_mt_perf.o
LIB_OBJS_ST_PERF=ffmalloc_st_perf.o
LIB_OBJS_MT_LARGE_PERF=ffmalloc_mt_large_perf.o
LIB_OBJS_ST_LARGE_PERF=ffmalloc_st_large_perf.o

CFLAGS=-Wall -Wextra -Wno-unknown-pragmas -fPIC -c -g -O3 -DFF_GROWLARGEREALLOC -D_GNU_SOURCE

all: ${LIB_SHARED_MT} ${LIB_SHARED_ST} ${LIB_SHARED_MT_LARGE} ${LIB_SHARED_ST_LARGE} ${LIB_SHARED_MT_PERF} ${LIB_SHARED_ST_PERF} ${LIB_SHARED_MT_LARGE_PERF} ${LIB_SHARED_ST_LARGE_PERF}

${LIB_SHARED_MT}: ${LIB_OBJS_MT}
	${CC} -o $@ -shared -fPIC -pthread $^

${LIB_SHARED_ST}: ${LIB_OBJS_ST}
	${CC} -o $@ -shared -fPIC $^

${LIB_SHARED_MT_LARGE}: ${LIB_OBJS_MT_LARGE}
	${CC} -o $@ -shared -fPIC -pthread $^

${LIB_SHARED_ST_LARGE}: ${LIB_OBJS_ST_LARGE}
	${CC} -o $@ -shared -fPIC $^

${LIB_SHARED_MT_PERF}: ${LIB_OBJS_MT_PERF}
	${CC} -o $@ -shared -fPIC -pthread $^

${LIB_SHARED_ST_PERF}: ${LIB_OBJS_ST_PERF}
	${CC} -o $@ -shared -fPIC $^

${LIB_SHARED_MT_LARGE_PERF}: ${LIB_OBJS_MT_LARGE_PERF}
	${CC} -o $@ -shared -fPIC -pthread $^

${LIB_SHARED_ST_LARGE_PERF}: ${LIB_OBJS_ST_LARGE_PERF}
	${CC} -o $@ -shared -fPIC $^

${LIB_OBJS_MT}: ffmalloc.c
	${CC} ${CFLAGS} ffmalloc.c -o $@

${LIB_OBJS_ST}: ffmalloc.c
	${CC} ${CFLAGS} -DFFSINGLE_THREADED ffmalloc.c -o $@

${LIB_OBJS_MT_LARGE}: ffmalloc.c
	${CC} ${CFLAGS} -DUSE_LARGE_CHUNK ffmalloc.c -o $@

${LIB_OBJS_ST_LARGE}: ffmalloc.c
	${CC} ${CFLAGS} -DFFSINGLE_THREADED -DUSE_LARGE_CHUNK ffmalloc.c -o $@

${LIB_OBJS_MT_PERF}: ffmalloc.c
	${CC} ${CFLAGS} -DPERF ffmalloc.c -o $@

${LIB_OBJS_ST_PERF}: ffmalloc.c
	${CC} ${CFLAGS} -DFFSINGLE_THREADED -DPERF ffmalloc.c -o $@

${LIB_OBJS_MT_LARGE_PERF}: ffmalloc.c
	${CC} ${CFLAGS} -DUSE_LARGE_CHUNK -DPERF ffmalloc.c -o $@

${LIB_OBJS_ST_LARGE_PERF}: ffmalloc.c
	${CC} ${CFLAGS} -DFFSINGLE_THREADED -DUSE_LARGE_CHUNK -DPERF ffmalloc.c -o $@

ffmalloc.c: ffmalloc.h

clean:
	rm *.o
	rm *.so
