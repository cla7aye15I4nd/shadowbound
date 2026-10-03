FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    autoconf \
    autogen \
    automake \
    bison \
    build-essential \
    clang \
    lld \
    ninja-build \
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
    python-is-python3 \
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

COPY llvm-project /shadowbound/llvm-project

## ChakraCore do not support inject compilation flags, so we need to modify the source code
COPY artifact/chakra/keep-going.diff /keep-going.diff
RUN cd /shadowbound/llvm-project && \
    git apply /keep-going.diff

RUN cd /shadowbound/llvm-project && \
    mkdir build && \
    cd build && \
    cmake -G Ninja \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLLVM_USE_LINKER=lld \
      -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_BINUTILS_INCDIR=../../binutils/include \
      -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" \
      -DCMAKE_BUILD_TYPE=Release -DCLANG_ENABLE_OPAQUE_POINTERS=OFF \
      -DCOMPILER_RT_SANITIZERS_TO_BUILD="shadowbound;memp" ../llvm && \
    ninja clang clang_rt.shadowbound-x86_64 clang_rt.shadowbound_cxx-x86_64

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
    --sanitize="shadowbound" -j --static

COPY artifact/chakra/test.sh /test.sh
CMD ["/test.sh"]