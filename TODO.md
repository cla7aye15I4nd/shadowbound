# TODO

## Optimization
- [x] Tail bytes optimization.
- [x] Pointer calculation direction optimization.
- [ ] Optimize the checks in the monotonic loop.
  - [x] pointer iteration. (`for (int *ptr = begin; ptr < end; ptr++)`)
  - [ ] index iteration. (`for (int i = 0; i < size; i++)`)
- [ ] Use PGO and LTO to inline the indirect call.
- [ ] Identify all protected pointers access and remove checks for them in LTO. (Stucture-Based)
  - [x] Identify array pattern at malloc site.
  - [ ] Identify array pattern at all store site.◊
- [ ] Identify the relationship between the argument of the function.
  - [x] Identify stack and global arguments.
  - [ ] Identify array pattern arguments.

## Bugs

- [x] Investigate the cause of the 70% time consumption in ReleaseMemoryToOS of `502.gcc_r`.
- [x] Investigate why parameter `kSpaceSize` in `AP64` not work.
- [x] There are some unknown false positive in `gcc_r` and `parest_r`.
- [ ] The `-fsanitize-recover=overflow-defense` not work, use `-mllvm -odef-keep-going=1` instead temporarily.
