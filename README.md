# ShadowBound: Efficient Memory Protection through Advanced Metadata Management and Customized Compiler Optimization

## Installation

### Docker Build

We recommend using Docker to build and run ShadowBound. The Docker image contains all the dependencies and configurations required to build and run ShadowBound.

```bash
docker compose up --build shadowbound
```

### Manual Build

#### ShadowBound
```bash
cd llvm-project
mkdir build && cd build
cmake -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" -DCMAKE_BUILD_TYPE=Release -DCLANG_ENABLE_OPAQUE_POINTERS=OFF -G "Unix Makefiles" ../llvm
make -j`nproc`
```

#### FFMalloc
```bash
cd ffmalloc
make -j`nproc`
```

#### MarkUS
```bash
cd markus
./setup.sh
```

## Usage

You can use `-fsanitize=overflow-defense` to enable ShadowBound. In this mode, ShadowBound will use the default allocator to manage metadata. The following example shows how to compile a simple program with ShadowBound.

```bash
clang -fsanitize=overflow-defense -O2 test/test.c
```

If you want to customize the allocator in ShadowBound, you can use `-fsanitize=memprotect` to disable the default allocator and use the customized allocator. The following example shows how to compile a simple program with ShadowBound and the FFMalloc allocator.

```bash
clang -fsanitize=memprotect -O2 test/test.c -L$PWD/ffmalloc -lffmalloc_st_perf -Wl,-rpath=$PWD/ffmalloc
```

## Evaluation

To support the claims in the paper, you can follow the instructions in the [eval/README.md](eval/README.md) to reproduce the evaluation results.

## Bibtex

```
@inproceedings{yu2024shadowbound,
    title = {{ShadowBound}: Efficient Memory Protection through Advanced Metadata Management and Customized Compiler Optimization},
    author={Yu, Zheng and Yang, Ganxiang and Xing, Xinyu},
    booktitle = {33rd USENIX Security Symposium (USENIX Security 24)},
    year = {2024},
}
```
