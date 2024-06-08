# ShadowBound: Efficient Memory Protection through Advanced Metadata Management and Customized Compiler Optimization

## Installation

### Docker Build

We recommend using Docker to build and run ShadowBound. The Docker image contains all the dependencies and configurations required to build and run ShadowBound.

```bash
docker compose up --build
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

## Bibtex

```
@inproceedings{yu2024shadowbound,
    title = {{ShadowBound}: Efficient Memory Protection through Advanced Metadata Management and Customized Compiler Optimization},
    author={Yu, Zheng and Yang, Ganxiang and Xing, Xinyu},
    booktitle = {33rd USENIX Security Symposium (USENIX Security 24)},
    year = {2024},
}
```
