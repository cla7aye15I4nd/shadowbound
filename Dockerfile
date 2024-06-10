FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    autoconf \
    autogen \
    automake \
    bison \
    build-essential \
    cmake \
    curl \
    flex \
    g++ \
    gcc \
    git \
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
    git clone --depth 1 git://sourceware.org/git/binutils-gdb.git binutils -b binutils-2_41-release && \
    cd binutils && mkdir build && cd build && \
    ../configure --enable-gold --enable-plugins --disable-werror && \
    make -j`nproc`

## Install Shadowbound
COPY llvm-project /shadowbound/llvm-project
RUN cd /shadowbound/llvm-project && \
    mkdir build && \
    cd build && \
    cmake -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_BINUTILS_INCDIR=../../binutils/include -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" -DLLVM_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DCLANG_ENABLE_OPAQUE_POINTERS=OFF -G "Unix Makefiles" ../llvm && \
    make -j`nproc`

## Install FFmalloc
COPY ffmalloc /shadowbound/ffmalloc
RUN cd /shadowbound/ffmalloc && make -j`nproc`

## Install MarkUs
COPY markus /shadowbound/markus
RUN cd /shadowbound/markus && ./setup.sh

ENV SHADOWBOUND=/shadowbound