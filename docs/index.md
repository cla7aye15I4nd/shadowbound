# ShadowBound

**Efficient Heap Memory Protection through Advanced Metadata Management and
Customized Compiler Optimization**

Zheng Yu, Ganxiang Yang, Xinyu Xing — Northwestern University
· 33rd USENIX Security Symposium (USENIX Security '24)

[📄 Paper (PDF)](paper/shadowbound-usenixsec24.pdf){: .btn}
[USENIX page](https://www.usenix.org/conference/usenixsecurity24/presentation/yu-zheng){: .btn}
[Source & artifact](https://github.com/cla7aye15I4nd/shadowbound){: .btn}

## Abstract

In software development, the prevalence of unsafe languages such as C and C++
introduces potential vulnerabilities, especially within the heap. While prior
solutions aiming for temporal and spatial memory safety exhibit overheads deemed
impractical, we present ShadowBound, a unique heap memory protection design. At
its core, ShadowBound is an efficient out-of-bounds defense that can work with
various use-after-free defenses (e.g. MarkUs, FFMalloc, PUMM) without
compatibility constraints. We harness a shadow memory-based metadata management
mechanism to store heap chunk boundaries and apply customized compiler
optimizations tailored for boundary checking.

## Artifact

The compiler, runtime, and allocators live in this repository. The experiments
(SPEC CPU2017, Nginx, Chakra) are packaged as Docker images and validated by
GitHub CI. See
[artifact/README.md](https://github.com/cla7aye15I4nd/shadowbound/blob/main/artifact/README.md).

## BibTeX

```bibtex
@inproceedings{yu2024shadowbound,
  title     = {{ShadowBound}: Efficient Heap Memory Protection through Advanced
               Metadata Management and Customized Compiler Optimization},
  author    = {Yu, Zheng and Yang, Ganxiang and Xing, Xinyu},
  booktitle = {33rd USENIX Security Symposium (USENIX Security 24)},
  year      = {2024},
}
```
