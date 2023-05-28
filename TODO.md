# TODO

## Optimization
- [x] Tail bytes optimization.
- [x] Pointer calculation direction optimization.
- [ ] Optimize the checks in the monotonic loop.
- [ ] Use PGO and LTO to inline the indirect call.
- [ ] Identify all protected pointers access and remove checks for them in LTO. (Stucture-Based) 
- [ ] Identify the relationship between the argument of the function.
- [ ] Optimize `string-iteration` loop.

## Bugs

- [ ] Investigate the cause of the 70% time consumption in ReleaseMemoryToOS of `602.gcc_s`.
- [ ] Investigate why parameter `kSpaceSize` in `AP64` not work.
- [ ] There are some unknown false positive in `gcc_r` and `parest_r`.
- [ ] The `-fsanitize-recover=overflow-defense` not work, use `-mllvm -odef-keep-going=1` instead temporarily.
