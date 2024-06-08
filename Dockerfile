FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    autoconf \
    autogen \
    build-essential \
    cmake \
    curl \
    g++ \
    gcc \
    git \
    jq \
    less \
    libtool \
    lsb-release \
    make \
    nano \
    openssh-client \
    python3 \
    python3-pip \
    sudo \
    unzip \
    wget \
    zip \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir /shadowbound

## Install Shadowbound
COPY llvm-project /shadowbound/llvm-project
RUN cd /shadowbound/llvm-project && \
    mkdir build && \
    cd build && \
    cmake -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" -DLLVM_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DCLANG_ENABLE_OPAQUE_POINTERS=OFF -G "Unix Makefiles" ../llvm && \
    make -j`nproc`

## Install FFmalloc
COPY ffmalloc /shadowbound/ffmalloc
RUN cd /shadowbound/ffmalloc && make -j`nproc`

## Install MarkUs
COPY markus /shadowbound/markus
RUN cd /shadowbound/markus && ./setup.sh
