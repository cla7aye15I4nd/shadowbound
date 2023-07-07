# Efficient Memory Protection through Whole-Program Optimization and Advanced Metadata Management

**Laid** is a defense mechanism that can be used in user-space programs. It utilizes LLVM instrumentation and shadow memory to offer memory protection, effectively preventing heap-based memory corruption, including heap out-of-bounds access and use-after-free vulnerabilities.

## Out-Of-Bound Defense Design

LLVM has two type of pointer computing instructions, both of them may result in memory overflow access when be used improperly:

### `getelementptr`
GEP is a LLVM instruction used for computing the address of an element in memory. The instruction takes a base pointer and a set of indices to compute the memory address. Here is a example:

```c
int* result = &base[offset];
// %ptr = getelementptr i32, i32* %array, i64 %offset
```

If the indices (`offset`) are not properly checked, an attacker may be able to exploit a GEP operation to access memory outside of the intended bounds of the array, leading to a buffer overflow.

### `bitcast`

BC can transfer a value of one type to a value of another type. However, if the target type is not large enough to contain the original value, a truncation will occur. This can lead to an overflow.

**Laid** will check OOB after the two type instructions, here is the example:
```c
void main() {
  char *buf = malloc(16);
  check_gep(buf, &buf[3], sizeof(char));
  check_bc(buf + 3, sizeof(struct obj));
  foo((struct obj*)buf + 3);
  check_gep(buf, &buf[1], sizeof(char));
  buf[1] = 'y';
}
```

### Metadata Managment

The `laid` tool employs [shadow memory](https://github.com/google/sanitizers/wiki/AddressSanitizerAlgorithm) to track the bound of each chunk and instrument the checking code during compile time. To achieve this, the shadow memory of address `p` is used to store two distances - one from `p` to the start of the chunk and another from `p` to the end of the chunk. Since we require two 64-bit integers to store the distances, the shadow memory size is 16 bytes for each byte.

```
|<----------D_1---------->p<------------D_2------------>|
```

However, we can optimize this approach by considering two key facts. Firstly, almost all allocators align the size of the chunk to 8 bytes, allowing us to store distance information for every 8 bytes. Secondly, the largest size of a chunk is 8 GB ($2^{33}$ bytes), and the size's low 3 bits are always zero due to the first fact. Consequently, we can use two 32-bit integers to store distance information for every 8 bytes, which means we only need 8 bytes for every 8 bytes of memory. This size of shadow memory is identical to the original memory size itself.

> Another interesting fact worth noting is that `MemorySanitizer` also employs shadow memory that is the same size as the memory itself. This means that we can leverage `MemorySanitizer` to learn how to implement the shadow memory effectively.

Then for every GEP instruction, we insert following checking code:
```c
// Layout Example for chunk size = 0x20
// |<-- 8 bytes -->|<-- 8 bytes -->|<-- 8 bytes -->|<-- 8 bytes -->|
// |<- 4 ->|<- 4 ->|<- 4 ->|<- 4 ->|<- 4 ->|<- 4 ->|<- 4 ->|<- 4 ->|
// |  0x0  |  0x4  |  0x1  |  0x3  |  0x2  |  0x2  |  0x3  |  0x1  |

// Minor optimization (implemented): 
//   Many programs do not require chunk larger than 4 GB.
//   Hence, we can store the original distance in the shadow memory,
//   which eliminates the need for two extra shift operations in runtime.

// Under the optimization, the shadow memory layout will be:
// |<-- 8 bytes -->|<-- 8 bytes -->|<-- 8 bytes -->|<-- 8 bytes -->|
// |<- 4 ->|<- 4 ->|<- 4 ->|<- 4 ->|<- 4 ->|<- 4 ->|<- 4 ->|<- 4 ->|
// | 0x000 | 0x020 | 0x008 | 0x018 | 0x010 | 0x010 | 0x018 | 0x008 |

// %GEP = getelementptr %Ptr, %Offset1, %Offset2, ...
ShadowAddr = Ptr & kShadowMask;
Base = Ptr & kShadowBase;
Packed = *(int32_t *) ShadowAddr;
Front = Packed & 0xffffffff;
Back = Packed >> 32;
Begin = Base - (Front << 3);
End = Base + (Back << 3);
if (GEP < Begin || GEP + NeededSize > End)
  report_overflow();
```
## Use-After-Free Defense Design

As mentioned in the previous section, we can infer that the out-of-bound checking mechanism does not have any specific requirements for the allocator's algorithm. Therefore, we do not need to replace the allocator to implement out-of-bound checking.

Additionally, it's worth noting that several state-of-the-art use-after-free defense mechanisms, such as MarkUs and FFmalloc, are based on the replacement of the allocator. However, since the out-of-bound checking mechanism does not rely on allocator replacement, it can be easily combined with use-after-free defense mechanisms.


| Name    | Runtime | Memory |        Algorithm    | Implemtation          |
| ------- | --------| ------ | ------------------- | --------------------- |
| MarkUS  | 9.62%   | 17.76% | Grbage Collector    | Allocator Replacement |
| FFmalloc| 2.27%   | 125.57%| One-time Allocation | Allocator Replacement |
| PUMM    | 0.57%   | 0.09%  | Grbage Collector    | Binary Instrument     |


By combining the out-of-bound checking and use-after-free defense mechanisms, we can achieve a more comprehensive memory safety solution. This integration can help to detect and prevent both out-of-bound access and use-after-free vulnerabilities, which are among the most common types of memory safety issues.

## Performance Optimization
- Tail bytes optimization. (+++)

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
  check_bc(o, o, sizeof(struct obj));
  // ...
}
int foo(struct obj *ptr) {
  check_gep(ptr, &ptr->a, sizeof(ptr->a));
  ptr->a = 1;
  check_gep(ptr, &ptr->b, sizeof(ptr->b));
  ptr->b = 2;
}
```

For example, the function `bar` allocates a new object `obj`. The return type of `malloc`, is `void*`, not `struct obj*`, so the compiler inserts a typecasting instruction, after which `laid` inserts a range check to ensure the memory space is sufficient to hold the structure `obj`. With this type-casting validation, the compiler can safely infer that the memory space of a typed pointer is at least its type size. As a result, the compiler can conclude that pointers referring to the structure field are in-bound. Therefore, `laid` can remove the range checks in `foo`.

- Optimize the checks in the monotonic loop. (++)
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
- Simplify checks when compiler can determine the direction of the pointer calculation. (e.g. `p + 1` is always larger than `p`) (+)
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
- Use PGO and LTO to inline the indirect call. (+++)
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
- Identify all protected pointers access and remove checks for them in LTO. (Stucture-Based) (+++)
```c
struct node {
  int *ptr;
  int ptr_len; // ptr_len is always ptr's length
};

// No checks needed for ptr
for (int i = 0; i < n->ptr_len; ++i)
  n->ptr[i] = 1;
```

- Identify the relationship between the argument of the function? (++)

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
- Optimize `string-iter` loop.
```c
// if we can reserve one bytes for every string, 
// we can remove the check in the loop.
// However, is it deserved ?
for(aep = aexpr; *aep; aep++)
  // do something
```

## Memory Optimization (\*)
- Although allocating a large region for storing bounds information may be necessary, only a few of these regions are actually used for checking. For instance:

  - When dealing with a large chunk that contains many pages, from `base` to `base + 4G,` most `GEP` instructions have their base pointer set to `base`. This implies that only the information from the first page is utilized.

  - When working with a small chunk, typically representing a single struct, our optimization eliminates the need for accessing the single struct.

  Considering the two optimizations mentioned above, if we can devise a method to dynamically allocate shadow memory, meaning allocate physical memory only when the program actually needs to access the corresponding shadow memory, we can reduce the waste of shadow memory.

  One potential approach to implementing this is by modifying the kernel. We can configure the kernel to execute a user callback function whenever it allocates memory.

## Hardware (\*)

- Implement the per-pointer bounds tracking with more hardware support, such as `CHERI`. `CHERI` extend the the 64-bit virtual address to 128-bit, with the higher 64-bit being used to store the upper bounds of the pointer.

- An alternative approach is to utilize SGX (Software Guard Extensions) for implementing per-pointer bounds tracking. SGX enclave programs utilize a 64-bit virtual address, with only the lower 36 bits being valid. In this approach, we can allocate the higher 28 bits of the virtual address to store the upper bounds of the pointer.
> For the lower bounds, we can continue using shadow memory to store them. However, it is worth noting that in most cases, pointer arithmetic tends to have a positive direction. As a result, we can optimize the implementation by minimizing accesses to shadow memory. 

## Evaluation

**Basic Goal**: 
- Performance Overhead: less than 8%.
- Security: Prevent all heap memory corruption vulnerabilities.

**Advance Goal**:
- Performance Overhead: less than 6%.
- Security: Prevent all heap memory corruption vulnerabilities.

**Stretch Goal**:
- Performance Overhead: less than 6%.
- Security: Prevent all heap and stack corruption vulnerabilities.

## "False Positive Cases"
In theory, by adding checking code at every GEP and BC to verify that the input pointer and result pointer belong to the same memory chunk, we can potentially prevent all memory overflows. However, we found many false positive cases in practice.

### Poor Programming Practices
```c
int *a = ptr + overflow_length;
foo(a);
```
The code creates `a` pointer a by adding `overflow_length` to the value of ptr. Then, it passes the pointer a as an argument to the function `foo`. This practice is considered to be poor programming because it can lead to a potential memory overflow. Even though the pointer `a` is not accessed in this code snippet, any calculations based on a would be meaningless. As a result, it is best to avoid storing or passing an overflow pointer as an argument to prevent potential memory safety issues.

**PHP**

There is an [php-8.2.2](https://github.com/php/php-src/blob/b20c0e925fe401a44a99b0d34b438797be865bb0/Zend/zend_API.c#L2859) example which is the most wired case I found.
```c
num_args++;
new_arg_info = malloc(sizeof(zend_arg_info) * num_args);
memcpy(new_arg_info, arg_info, sizeof(zend_arg_info) * num_args);
reg_function->common.arg_info = new_arg_info + 1;
```
If `num_args` is initially zero, then the `new_arg_info` pointer will only allocate memory for a single `zend_arg_info` structure. When `reg_function->common.arg_info = new_arg_info + 1` is executed, `reg_function->common.arg_info` will point to the memory location at the end the allocated memory. 

However, I found the code only accesses `reg_function->common.arg_info[-1]` and never accesses any memory locations beyond the allocated memory, then the buffer overflow issue will not occur. However, this still leaves room for potential bugs or errors in the future, as the code may be modified or updated to access other memory locations.

**508.named_r**

```c++
Type *getNewArray(int n)
{
  // `end` is the end of the pos
  Type *rpos;
  rpos = pos;
  if ((pos += n) > end)
  {
    pos = new Type[blockSize];
    push();
    end = pos + blockSize;
    rpos = pos;
    pos += n;
  }

  return rpos;
}
```
**526.blender_r**
```c++
if (coords_sign == 1) {
  for (i = 0; i < coords_tot; i++) {
    indices[i].next = &indices[i + 1]; // store an overflow pointer when i == coords_tot - 1
    indices[i].prev = &indices[i - 1]; // store an overflow pointer when i == 0
    indices[i].index = i;
  }
}
else {
  /* reversed */
  unsigned int n = coords_tot - 1;
  for (i = 0; i < coords_tot; i++) {
    indices[i].next = &indices[i + 1];
    indices[i].prev = &indices[i - 1];
    indices[i].index = (n - i);
  }
}

indices[0].prev = &indices[coords_tot - 1]; // cover the overflow pointer
indices[coords_tot - 1].next = &indices[0]; // cover the overflow pointer
```

**429.mcf**
```c
for( ; arc < stop_arcs; arc += nr_group ) {
  // not handle arc in loop optimization
}
```

**403.gcc**
```c
// SPEC 2006
// alloc
reg_known_value  = (rtx *) xcalloc ((maxreg - FIRST_PSEUDO_REGISTER), sizeof (rtx)) - FIRST_PSEUDO_REGISTER;
// access
for (i = FIRST_PSEUDO_REGISTER; i < maxreg; i++)
if (reg_known_value[i] == 0)
  reg_known_value[i] = regno_reg_rtx[i];

// SPEC 2017
// alloc
reg_known_value = GGC_CNEWVEC (rtx, reg_known_value_size);
// access
if (regno >= FIRST_PSEUDO_REGISTER)
{
  regno -= FIRST_PSEUDO_REGISTER;
  if (regno < reg_known_value_size)
    return reg_known_value[regno];
}
```
**483.xalancbmk**

```c++
// SPEC 2006
SchemaGrammar& sGrammar = (SchemaGrammar&) grammarEnum.nextElement();
if (sGrammar.getGrammarType() != Grammar::SchemaGrammarType || sGrammar.getValidated())
     continue;

// SPEC 2017
// Replace the following 3 lines with the 4 lines below it per report that the code is
// down-casting a variable, sGrammar, of type Gramar& to type SchemaGrammar& before it
// Knows if the variable is really of type SchemaGrammar& 
//      SchemaGrammar& sGrammar = (SchemaGrammar&) grammarEnum.nextElement();
//      if (sGrammar.getGrammarType() != Grammar::SchemaGrammarType || sGrammar.getValidated())
//           continue;
Grammar& gGrammar = grammarEnum.nextElement();
if (gGrammar.getGrammarType() !=Grammar::SchemaGrammarType ||gGrammar.getValidated())
      continue;
SchemaGrammar& sGrammar = (SchemaGrammar&)gGrammar;
```

**450.soplex**

This test case stores data pointers in a list. Initially, these pointers were all located within the same array called `theItem`. Then the code calls `reMax` to expand the `theItem` array using `realloc` and returns the offset between the new and old pointers. To synchronize the data within the list, the code calls `move` to add this offset to all the pointers in the list. This behavior actually constitutes a typical Use-After-Free (UAF) and Out-of-Bounds (OOB) scenario. The code should handle these operations before the "realloc" call, freeing the old pointers. 

```c
// svset.cc
ptrdiff_t reMax(int newmax = 0)
{
  struct Item * old_theitem = theitem;
  newmax = (newmax < size()) ? size() : newmax;

  int* lastfree = &firstfree;
  while (*lastfree != -themax - 1)
     lastfree = &(theitem[ -1 - *lastfree].info);
  *lastfree = -newmax - 1;

  themax = newmax;

  spx_realloc(theitem, themax);
  spx_realloc(thekey,  themax);

  return reinterpret_cast<char*>(theitem) 
     - reinterpret_cast<char*>(old_theitem);
}

// islist.h
void move(ptrdiff_t delta)
{
  if (the_first)
  {
     T* elem;
     the_last  = reinterpret_cast<T*>(reinterpret_cast<char*>(the_last) + delta);
     the_first = reinterpret_cast<T*>(reinterpret_cast<char*>(the_first) + delta);
     for (elem = first(); elem; elem = next(elem))
        if (elem != last())
           elem->next() = reinterpret_cast<T*>(reinterpret_cast<char*>(elem->next()) + delta);
  }
}
```

### End-of-the-Array Pointers
```c
struct obj* objs = malloc(sizeof(struct obj) * num_obj);
struct obj* obj_end = objs + num_obj;
```
Note that `obj_end` points to the memory location immediately after the last element of the objs array, and is not a valid pointer that can be used to access the `struct obj` objects.

The purpose of having both `objs` and `obj_end` pointers is to have a way to traverse the objs array, while also being able to detect when the end of the array is reached. This can be useful in various scenarios, such as iterating through the objs array or checking the number of elements remaining in the array.

In theory, by identifying patterns in the allocation of struct arrays and intentionally allocating one additional object, it may be possible to avoid certain problems.

### Compiler Optimization
```c
// The compiler will generate `a + x` in this code.
// However, if the program falls into the `else` branch, 
// `a + x` may result in a buffer overflow.
// The value of `a[x]` is accessed in multiple branches.
if (branch_a) {
  a[x] = 1;
} else if (branch_b) {
  a[x] = 2;
} else {
  // No action is taken in this branch.
}
```

## Reference
- https://llvm.org/docs/OpaquePointers.html
- https://llvm.org/docs/Vectorizers.html
- https://llvm.org/devmtg/2020-09/slides/PGO_Instrumentation.pdf
- https://llvm.org/devmtg/2015-10/slides/Baev-IndirectCallPromotion.pdf
- https://llvm.org/docs/LinkTimeOptimization.html
- https://releases.llvm.org/15.0.0/docs/GoldPlugin.html
