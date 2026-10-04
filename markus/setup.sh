MARKUS=`pwd`

cd bdwgc-markus
./autogen.sh
# GCC 14+ turns implicit declarations and pointer/integer mismatches into
# errors; this bdwgc fork has benign ones (e.g. a call before its prototype,
# dead code after assert(0)). -fpermissive restores the old warnings.
CFLAGS="-g -O2 -fpermissive" ./configure --prefix=$MARKUS/markus-allocator --enable-redirect-malloc --enable-threads=posix --disable-gc-assertions --enable-thread-local-alloc --enable-parallel-mark --disable-munmap --enable-cplusplus --enable-large-config --disable-gc-debug
make install
