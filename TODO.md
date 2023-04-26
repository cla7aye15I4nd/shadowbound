# Laid-Back: A Layout-Aware Identification Defense for Buffer Overflow Attacks

### Todo

#### Kernel
- [ ] Study the principle of `kmsan`'s runtime and implement the runtime of `laid` in kernel.
- [ ] Try compile a single kernel module with `laid` to see if it works.
- [ ] Try compile the whole kernel with `laid` to see if it works.
- [ ] Allocate more 50% in malloc to avoid last bytes.
#### Optimization
- [ ] Optimize the checks in the monotonic loop.
```c
// Before Optimization
getChunkBound(p, &begin, &end);
for (int i = 0; i < n; ++i) {
  if (p+i < begin || p+i+1 > end)
    crash();
  foo(p[i]);
}
// After Optimization
getChunkBound(p, &begin, &end);
if (p < begin || p+n > end)
  crash();
for (int i = 0; i < n; ++i) 
  foo(p[i]);
```
- [ ] Simplify checks when compiler can determine the direction of the pointer calculation. (e.g. `p + 1` is always larger than `p`)
```c
// Before Optimization
getChunkBound(p, &begin, &end);
if (p < begin || p+5+1 > end)
  crash();
foo(p[5])
// After Optimization
getChunkEnd(p, &end);
if (p+5+1 > end)
  crash();
foo(p[5])
```
- [ ] Use PGO and LTO to inline the indirect call.
```c
void calc_8x8(int *ptr) {
  check(ptr, ptr + 3, sizeof(int));
  ptr[0] = 1;
  ptr[1] = 33;
  ptr[2] = 22;
  ptr[3] = 233;
}
void init() {
  // calc_8x8 is a function pointer
  node->foo[mode] = calc_8x8;
}

// Before Optimization
void run() {
  int ptr[5];
  // we need to check ptr's bound in both caller and callee.
  check(ptr, ptr + 1, sizeof(int));
  node->foo[mode](ptr + 1);
}
// After Optimization
void run() {
  int ptr[8];
  if (node->foo[mode] == calc_8x8) {
    // compiler can inline calc_8x8 to avoid check `ptr` in callee.
    check(ptr, ptr + 4, sizeof(int));
    ptr[1] = 1;
    ptr[2] = 33;
    ptr[3] = 22;
    ptr[4] = 233;
  } else
    node->foo[mode](ptr);
}
```
- [ ] Identify all protected pointers access and remove checks for them in LTO.
```c
struct node {
  int *ptr;
  int ptr_len; // ptr_len is always ptr's length
};

// No checks needed for ptr
for (int i = 0; i < n->ptr_len; ++i)
  n->ptr[i] = 1;
```

#### Bugs

- [ ] Investigate the cause of the 70% time consumption in ReleaseMemoryToOS of `602.gcc_s`.
- [ ] Investigate why parameter `kSpaceSize` in `AP64` not work.
- [ ] There are some unknown false positive in `gcc_r` and `parest_r`.
- [ ] The `-fsanitize-recover=overflow-defense` not work, use `-mllvm -odef-keep-going=1` instead temporarily.
