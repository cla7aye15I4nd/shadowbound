# Laid-Back: A Layout-Aware Identification Defense for Buffer Overflow Attacks

### Todo

#### Optimization
- [ ] Optimize the monotonic loop.
- [ ] Simplify checks when compiler can determine the direction of the pointer calculation. (e.g. `p + 1` is always larger than `p`)

#### Bugs

- [ ] Investigate the cause of the 70% time consumption in ReleaseMemoryToOS of `602.gcc_s`.
- [ ] Investigate why parameter `kSpaceSize` in `AP64` not work.
- [ ] There are some false positive in `perlbench`, `gcc_s`, `xalancbmk` and `imagick`.
- [ ] Allocate more 50% in malloc to avoid last bytes.