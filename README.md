# Efficient Memory Protection through Whole-Program Optimization and Advanced Metadata Management

**Laid** is a defense mechanism that can be used in user-space programs. It utilizes LLVM instrumentation and shadow memory to offer memory protection, effectively preventing heap-based memory corruption, including heap out-of-bounds access and use-after-free vulnerabilities. The detail of design can see [here](doc/README.md).
## Installation
### Build `ld-gold`
```bash
git clone --depth 1 git://sourceware.org/git/binutils-gdb.git binutils
cd binutils
mkdir build && cd build
../configure --enable-gold --enable-plugins --disable-werror
make -j`nproc`
cp /usr/bin/ld /usr/bin/ld.backup
cp gold/ld-new /usr/bin/ld
ld -v # GNU gold (GNU Binutils ...
```

### Build `clang`
```bash
cd llvm-project
mkdir build && cd build
cmake -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_BINUTILS_INCDIR=../../binutils/include -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" -DLLVM_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DCLANG_ENABLE_OPAQUE_POINTERS=OFF -G "Unix Makefiles" ../llvm
# cmake -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_BINUTILS_INCDIR=../../binutils/include -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" -DLLVM_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DCLANG_ENABLE_OPAQUE_POINTERS=OFF -G "Unix Makefiles" ../llvm
make -j`nproc`
```
