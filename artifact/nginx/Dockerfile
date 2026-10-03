ARG BASE_IMAGE=ghcr.io/cla7aye15i4nd/shadowbound:latest
FROM ${BASE_IMAGE}

COPY config /config

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y lsof libpcre3-dev curl

RUN wget http://nginx.org/download/nginx-1.22.1.tar.gz && tar -xvf nginx-1.22.1.tar.gz

RUN git clone https://github.com/wg/wrk.git --branch 4.2.0 && cd wrk && make -j`nproc`

ARG SCC=$SHADOWBOUND/llvm-project/build/bin/clang

RUN cd nginx-1.22.1 && \
    ./configure --prefix=native && \
    make CC="$SCC -fsanitize=shadowbound-instrument-only -mllvm -shadowbound-skip-instrument=1" -j`nproc` && \
    make install


RUN cd nginx-1.22.1 && \
    ./configure --prefix=shadowbound && \
    make CC="$SCC -fsanitize=shadowbound -mllvm -shadowbound-pattern-opt-file=/config/nginx.json" -j`nproc` && \
    make install

# The allocator must be on nginx's LINK line. nginx links with $(CC) plus the
# configure-time --with-ld-opt; it ignores a make-time LDFLAGS and -l in CFLAGS
# only reaches compile steps. So pass the allocator via --with-ld-opt.
RUN cd nginx-1.22.1 && \
    ./configure --prefix=shadowbound-ffmalloc \
        --with-cc="$SCC -fsanitize=shadowbound-instrument-only" \
        --with-ld-opt="-L$SHADOWBOUND/ffmalloc -lffmalloc_st_perf -Wl,-rpath=$SHADOWBOUND/ffmalloc" && \
    make -j`nproc` && \
    make install

# setup.sh installs MarkUs under $SHADOWBOUND/markus/markus-allocator.
RUN cd nginx-1.22.1 && \
    ./configure --prefix=shadowbound-markus \
        --with-cc="$SCC -fsanitize=shadowbound-instrument-only" \
        --with-ld-opt="-L$SHADOWBOUND/markus/markus-allocator/lib -lgc -lgccpp -Wl,-rpath=$SHADOWBOUND/markus/markus-allocator/lib" && \
    make -j`nproc` && \
    make install

COPY artifact/nginx/eval.sh /root/eval.sh
COPY artifact/nginx/smoke.sh /root/smoke.sh
RUN chmod +x /root/eval.sh /root/smoke.sh
CMD ["/root/eval.sh"]