FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive
# Build LLVM 15 with clang/lld rather than the distro gcc: recent Ubuntu ships
# gcc versions too new to compile LLVM 15, so pinning to clang keeps the build
# working across 24.04/26.04/future releases.
RUN apt-get update && apt-get install -y \
    autoconf \
    autogen \
    automake \
    bison \
    build-essential \
    clang \
    lld \
    cmake \
    curl \
    flex \
    g++ \
    gcc \
    git \
    ninja-build \
    jq \
    less \
    libgmp-dev \
    libmpfr-dev \
    libtool \
    lsb-release \
    m4 \
    make \
    nano \
    nasm \
    openssh-client \
    pkg-config \
    python3 \
    python3-pip \
    sudo \
    texinfo \
    unzip \
    vim \
    wget \
    zip \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir /shadowbound

RUN cd /shadowbound && \
    git clone --depth 1 https://sourceware.org/git/binutils-gdb.git binutils -b binutils-2_41-release && \
    cd binutils && mkdir build && cd build && \
    ../configure --enable-gold --enable-plugins --disable-werror && \
    make -j`nproc`

## Install Shadowbound
COPY llvm-project /shadowbound/llvm-project
RUN cd /shadowbound/llvm-project && \
    mkdir build && \
    cd build && \
    cmake -G Ninja \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DLLVM_USE_LINKER=lld \
      -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_BINUTILS_INCDIR=../../binutils/include \
      -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt" \
      -DCMAKE_BUILD_TYPE=Release -DCLANG_ENABLE_OPAQUE_POINTERS=OFF \
      -DCOMPILER_RT_SANITIZERS_TO_BUILD="shadowbound;memp" \
      ../llvm && \
    ninja clang lld clang_rt.shadowbound-x86_64 clang_rt.shadowbound_cxx-x86_64 clang_rt.memp-x86_64

## Install FFmalloc
COPY ffmalloc /shadowbound/ffmalloc
RUN cd /shadowbound/ffmalloc && make -j`nproc`

## Install MarkUs
COPY markus /shadowbound/markus
RUN cd /shadowbound/markus && ./setup.sh

ENV SHADOWBOUND=/shadowbound