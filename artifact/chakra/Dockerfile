FROM ubuntu:20.04

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
    libicu-dev \
    libmpfr-dev \
    libtool \
    lsb-release \
    m4 \
    make \
    nano \
    nasm \
    openssh-client \
    pkg-config \
    python \
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

COPY llvm-project /shadowbound/llvm-project

## ChakraCore do not support inject compilation flags, so we need to modify the source code
COPY artifact/chakra/keep-going.diff /keep-going.diff
RUN cd /shadowbound/llvm-project && \
    git apply /keep-going.diff

RUN cd /shadowbound/llvm-project && \
    mkdir build && \
    cd build && \
    cmake -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_BINUTILS_INCDIR=../../binutils/include -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" -DLLVM_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DCLANG_ENABLE_OPAQUE_POINTERS=OFF -G "Unix Makefiles" ../llvm && \
    make -j`nproc`

ENV SHADOWBOUND=/shadowbound

COPY config /config

RUN git clone https://github.com/Microsoft/ChakraCore.git && git -C ChakraCore checkout ac3739e25d3692a2b591a0e60b459701f0c12572

RUN cd ChakraCore && \
    ./build.sh \
    --target-path=/ChakraCore/build/native \
    --cc=$SHADOWBOUND/llvm-project/build/bin/clang \
    --cxx=$SHADOWBOUND/llvm-project/build/bin/clang++ -j --static

RUN cd ChakraCore && \
    ./build.sh \
    --target-path=/ChakraCore/build/shadowbound \
    --cc=$SHADOWBOUND/llvm-project/build/bin/clang \
    --cxx=$SHADOWBOUND/llvm-project/build/bin/clang++ \
    --sanitize="overflow-defense" -j --static

COPY artifact/chakra/test.sh /test.sh
CMD ["/test.sh"]