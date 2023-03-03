# Laid-Back: A Layout-Aware Identification Defense for Buffer Overflow Attacks

### Todo

#### Bugs

- [ ] Investigate the cause of the 70% time consumption in ReleaseMemoryToOS of `602.gcc_s`.
- [x] Investigate why `kSpaceSize` in `AP64` not work.
- [ ] Add loop optimization to monotonous loop and strlen style loop.
- [ ] There are some false positive in `perlbench`, `gcc_s`.
- [x] Some testcase even slower than `CAMP`, I found that some optimization in `Laid` not work.
- [ ] Enable stack and heap-checking at same time in `Laid`.