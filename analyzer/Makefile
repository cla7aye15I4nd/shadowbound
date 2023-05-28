SOURCE := ${CURDIR}/src
TARGET := ${CURDIR}/build

LLVM_BUILD := ${CURDIR}/../llvm-project/build

build_analyzer = \
	(mkdir -p ${2} \
		&& cd ${2} \
		&& PATH=${LLVM_BUILD}/bin:${PATH} \
			LLVM_ROOT_DIR=${LLVM_BUILD}/bin \
			LLVM_LIBRARY_DIRS=${LLVM_BUILD}/lib \
			LLVM_INCLUDE_DIRS=${LLVM_BUILD}/include \
			CC=gcc CXX=g++ \
            cmake ${1} \
                -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_CXX_FLAGS_RELEASE="-std=c++17 -fno-rtti -fpic -g" \
		&& make -j`nproc`)

all: analyzer

analyzer:
	$(call build_analyzer, ${SOURCE}, ${TARGET})

clean:
	rm -rf ${BUILD_DIR}