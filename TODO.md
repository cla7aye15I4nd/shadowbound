# TODO

## Performance Optimization
- [x] Tail bytes optimization. (+++)

1. If a pointer exists outside the function it was created in, it is called an "escaped pointer."
2. If ensure an escaped pointer always points to a memory chunk with at least `k` valid bytes, we can avoid checking non-escaped pointers whose offsets from the base pointer are less than `k`.

For example, when `k` is 20, we can avoid checking `f[3]` on the following code. However, we cannot avoid checking `f[2]` because `f + 2` is a escaped pointer.
```c
void bar() {
  int *a = malloc(0x10 + k); // pad k bytes in malloc
  foo(a);
  check(a, a + 8);
  foo(a + 8);

}
void foo(int *f) {
  f[3] = 0; // f + 3 is not a escaped pointer.
  call(f + 2); // f + 2 is a escaped pointer.
}
```

An special case for tail-bytes optimization is structure, because an stucture pointer is always generate by the `bitcast` instruction:

```c
struct obj {
  int a;
  int b;
};

struct obj* bar() {
  // type-casting from void* to obj*
  struct obj *o = malloc(sizeof(struct obj));
  __check_range(o, o, sizeof(struct obj));
  // ...
}
int foo(struct obj *ptr) {
  __check_range(ptr, &ptr->a, sizeof(ptr->a));
  ptr->a = 1;
  __check_range(ptr, &ptr->b, sizeof(ptr->b));
  ptr->b = 2;
}
```

For example, the function `bar` allocates a new object `obj`. The return type of `malloc`, is `void*`, not `struct obj*`, so the compiler inserts a typecasting instruction, after which `laid` inserts a range check to ensure the memory space is sufficient to hold the structure `obj`. With this type-casting validation, the compiler can safely infer that the memory space of a typed pointer is at least its type size. As a result, the compiler can conclude that pointers referring to the structure field are in-bound. Therefore, `laid` can remove the range checks in `foo`.

- [ ] Optimize the checks in the monotonic loop. (++)
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
- [ ] Optimize `string-iter` loop? (+)
```c
// if we can reserve one bytes for every string, 
// we can remove the check in the loop.
// However, is it deserved ?
for( aep = aexpr; *aep; aep++)
  // do something
```
- [ ] Simplify checks when compiler can determine the direction of the pointer calculation. (e.g. `p + 1` is always larger than `p`) (+)
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
- [ ] Use PGO and LTO to inline the indirect call. (+++)
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
- [ ] Identify all protected pointers access and remove checks for them in LTO. (Stucture-Based) (+++)
```c
struct node {
  int *ptr;
  int ptr_len; // ptr_len is always ptr's length
};

// No checks needed for ptr
for (int i = 0; i < n->ptr_len; ++i)
  n->ptr[i] = 1;
```

- [ ] Identify the relationship between the argument of the function? (++)

I have not find a elegant a method to implement this optimization. My current method need to split the function.
```c
// Before Optimization
void bar() {
  while (a) {
    foo(a->data, a->length);
    a = a->next;
  };
}
void foo(int *ptr, int ptr_len) {
  // ptr_len is ptr's length with high probability.
  getChunkBound(ptr, &begin, &end);
  for (int i = 0; i < ptr_len; ++i) {
    if (magic(i))
      break;
    if (ptr+i < begin || ptr+i+1 > end)
      crash();
    ptr[i] = random();
  }
}
// After Optimization
void foo(int *ptr, int ptr_len) {
  for (int i = 0; i < ptr_len; ++i) {
    if (magic(i))
      break;
    ptr[i] = random();
  }
}
```

## Kernel
- [ ] Study the principle of `kmsan`'s runtime and implement the runtime of `laid` in kernel.
- [ ] Try compile a single kernel module with `laid` to see if it works.
- [ ] Try compile the whole kernel with `laid` to see if it works.
- [ ] Allocate more 50% in malloc to avoid last bytes.

## Hardware

- [ ] Implement the per-pointer bounds tracking with more hardware support, such as `CHERI`. `CHERI` extend the the 64-bit virtual address to 128-bit, with the higher 64-bit being used to store the upper bounds of the pointer.

- [ ] An alternative approach is to utilize SGX (Software Guard Extensions) for implementing per-pointer bounds tracking. SGX enclave programs utilize a 64-bit virtual address, with only the lower 36 bits being valid. In this approach, we can allocate the higher 28 bits of the virtual address to store the upper bounds of the pointer.
> For the lower bounds, we can continue using shadow memory to store them. However, it is worth noting that in most cases, pointer arithmetic tends to have a positive direction. As a result, we can optimize the implementation by minimizing accesses to shadow memory. 

## Memory Optimization
- [ ] Although allocating a large region for storing bounds information may be necessary, only a few of these regions are actually used for checking. For instance:

  - When dealing with a large chunk that contains many pages, from `base` to `base + 4G,` most `GEP` instructions have their base pointer set to `base`. This implies that only the information from the first page is utilized.

  - When working with a small chunk, typically representing a single struct, our optimization eliminates the need for accessing the single struct.

  Considering the two optimizations mentioned above, if we can devise a method to dynamically allocate shadow memory, meaning allocate physical memory only when the program actually needs to access the corresponding shadow memory, we can reduce the waste of shadow memory.

  One potential approach to implementing this is by modifying the kernel. We can configure the kernel to execute a user callback function whenever it allocates memory.

## Bugs

- [ ] Investigate the cause of the 70% time consumption in ReleaseMemoryToOS of `602.gcc_s`.
- [ ] Investigate why parameter `kSpaceSize` in `AP64` not work.
- [ ] There are some unknown false positive in `gcc_r` and `parest_r`.
- [ ] The `-fsanitize-recover=overflow-defense` not work, use `-mllvm -odef-keep-going=1` instead temporarily.
