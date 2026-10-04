ARG BASE_IMAGE=ghcr.io/cla7aye15i4nd/shadowbound:latest
FROM ${BASE_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y lsof libpcre2-dev zlib1g-dev libssl-dev unzip curl

# Latest nginx mainline release. Extracted to a version-independent directory
# so eval.sh / smoke.sh do not need to change with it.
ARG NGINX_VERSION=1.31.6
RUN mkdir nginx && curl -fsSL https://nginx.org/download/nginx-$NGINX_VERSION.tar.gz \
      | tar xz --strip-components=1 -C nginx

# wrk 4.2.0 is the latest wrk release; build it against the system OpenSSL
# instead of its bundled 1.1.1 tarball.
RUN git clone https://github.com/wg/wrk.git --branch 4.2.0 && cd wrk && make -j`nproc` WITH_OPENSSL=/usr

ARG SCC=$SHADOWBOUND/llvm-project/build/bin/clang

RUN cd nginx && \
    ./configure --prefix=native && \
    make CC="$SCC -fsanitize=shadowbound-instrument-only -mllvm -shadowbound-skip-instrument=1" -j`nproc` && \
    make install


# Whole-program instrumentation at link time (-flto). This replaces the
# pattern file config/nginx.json, which was generated for nginx 1.22.1.
# configure runs with the default compiler: its feature tests execute the
# programs they build, and ShadowBound programs need address-space
# randomization off, which `docker build` cannot provide. The compiler is
# switched at make time (nginx links with $(CC) as well).
RUN cd nginx && \
    ./configure --prefix=shadowbound && \
    make CC="$SCC -fsanitize=shadowbound -flto -fuse-ld=lld -Wno-unused-command-line-argument" -j`nproc` && \
    make install

# The allocator must be on nginx's LINK line: nginx links with $(CC) plus the
# configure-time --with-ld-opt (it ignores a make-time LDFLAGS). But configure
# must not run programs that use the allocator or the ShadowBound runtime
# (both map fixed address ranges, which needs randomization off; see above),
# so configure plainly and append the allocator to the generated link line.
RUN cd nginx && \
    ./configure --prefix=shadowbound-ffmalloc && \
    sed -i 's|-Wl,-E$|-Wl,-E -L'"$SHADOWBOUND"'/ffmalloc -lffmalloc_st_perf -Wl,-rpath='"$SHADOWBOUND"'/ffmalloc|' objs/Makefile && \
    make CC="$SCC -fsanitize=shadowbound-instrument-only" -j`nproc` && \
    make install

# setup.sh installs MarkUs under $SHADOWBOUND/markus/markus-allocator.
RUN cd nginx && \
    ./configure --prefix=shadowbound-markus && \
    sed -i 's|-Wl,-E$|-Wl,-E -L'"$SHADOWBOUND"'/markus/markus-allocator/lib -lgc -lgccpp -Wl,-rpath='"$SHADOWBOUND"'/markus/markus-allocator/lib|' objs/Makefile && \
    make CC="$SCC -fsanitize=shadowbound-instrument-only" -j`nproc` && \
    make install

COPY artifact/nginx/eval.sh /root/eval.sh
COPY artifact/nginx/smoke.sh /root/smoke.sh
RUN chmod +x /root/eval.sh /root/smoke.sh
CMD ["/root/eval.sh"]