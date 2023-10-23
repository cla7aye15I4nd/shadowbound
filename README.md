# ShadowBound: Efficient Memory Protection through Advanced Metadata Management and Customized Compiler Optimization

## Abstract

In software development, the prevalence of unsafe languages such as C and C++ introduces potential vulnerabilities, especially within the heap, a pivotal component for dynamic memory allocation. Despite its significance, heap management complexities have made heap corruption pervasive, posing severe threats to system security. While prior solutions aiming for temporal and spatial memory safety exhibit overheads deemed impractical, we present ShadowBound, a unique heap memory protection design. At its core, ShadowBound is an efficient out-of-bounds defense that integrates with various Use-After-Free (UAF) defenses without compatibility constraints. We harness a shadow memory-based metadata management mechanism to store heap chunk boundaries and apply customized compiler optimizations tailored for boundary checking. Built atop LLVM 15, ShadowBound incorporates three state-of-the-art UAF defenses. Our evaluations show that ShadowBound provides robust protection against exploitable out-of-bound bugs with minimal time and memory overhead, suggesting its applicability and effectiveness in safeguarding real-world programs against prevalent heap vulnerabilities.

## Installation

```bash
cd llvm-project
mkdir build && cd build
cmake -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" -DLLVM_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DCLANG_ENABLE_OPAQUE_POINTERS=OFF -G "Unix Makefiles" ../llvm
make -j`nproc`
```
