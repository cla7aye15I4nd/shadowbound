FROM shadowbound

COPY config /config

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y libpcre3-dev

RUN wget http://nginx.org/download/nginx-1.22.1.tar.gz && tar -xvf nginx-1.22.1.tar.gz

RUN git clone https://github.com/wg/wrk.git --branch 4.2.0 && cd wrk && make -j`nproc`

ARG SCC=$SHADOWBOUND/llvm-project/build/bin/clang

RUN cd nginx-1.22.1 && \
    ./configure --prefix=native && \
    make CC="$SCC -fsanitize=memprotect -mllvm -odef-skip-instrument=1" -j`nproc` && \
    make install


RUN cd nginx-1.22.1 && \
    ./configure --prefix=shadowbound && \
    make CC="$SCC -fsanitize=overflow-defense -mllvm -odef-pattern-opt-file=/config/nginx.json" -j`nproc` && \
    make install

RUN cd nginx-1.22.1 && \
    ./configure --prefix=shadowbound-ffmalloc && \
    make -j`nproc` CC="$SCC -fsanitize=memprotect" \ 
        CFLAGS="-L$SHADOWBOUND/ffmalloc -lffmalloc_st_perf -Wl,-rpath=$SHADOWBOUND/ffmalloc" \
        LDFLAGS="-L$SHADOWBOUND/ffmalloc -lffmalloc_st_perf -Wl,-rpath=$SHADOWBOUND/ffmalloc" && \
    make install

RUN cd nginx-1.22.1 && \
    ./configure --prefix=shadowbound-markus && \
    make -j`nproc` CC="$SCC -fsanitize=memprotect" \
        CFLAGS="-L$SHADOWBOUND/markus-allocator/lib -lgc -lgccpp  -Wl,-rpath=$SHADOWBOUND/markus-allocator/lib" \
        LDFLAGS="-L$SHADOWBOUND/markus-allocator/lib -lgc -lgccpp  -Wl,-rpath=$SHADOWBOUND/markus-allocator/lib" && \
    make install

COPY artifact/nginx/eval.sh /root/eval.sh
CMD ["/root/eval.sh"]