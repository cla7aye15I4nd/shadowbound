# Laid-Back: A Layout-Aware Identification Defense for Buffer Overflow Attacks

**Laid** is a defense mechanism that can be implemented in both user-space programs and the kernel. It uses LLVM instrumentation and shadow memory to provide overflow protection, include stack/global/heap overflow and in-field overflow.

## Build
```
cd llvm-project
mkdir build && cd build
cmake -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" -DCMAKE_BUILD_TYPE=Release -DCLANG_ENABLE_OPAQUE_POINTERS=OFF -G "Unix Makefiles" ../llvm
make -j`nproc`
```

## Design

LLVM has two type of pointer computing instructions, both of them may result in memory overflow access when be used improperly:

- `getelementptr`: GEP is a LLVM instruction used for computing the address of an element in memory. The instruction takes a base pointer and a set of indices and computes the memory address. If the indices are not properly checked, an attacker may be able to exploit a GEP operation to access memory outside of the intended bounds of the array, leading to a buffer overflow.
- `bitcast`: BitCast can transfer a value of one type to a value of another type. However, if the target type is not large enough to contain the original value, a truncation will occur. This can lead to an overflow.

### Heap Overflow Checking
The shadow memory serves to store the distance of each 8-byte block of memory from the beginning and end of its corresponding chunk. The shadow memory is divided into two 4-byte segments, one for the distance to the start of the chunk and the other for the distance to the end, enabling the system to perform a single comparison for checking both the start and end of the chunk for out-of-bounds access.

In a previous experiment, we discovered that most chunks are less than 4 GB in size, with the largest chunk being 8 GB ($2^{33}$ bytes). In the event that the program allocates an extremely large chunk, we can use the shadow memory to store the chunk size divided by 8, as every chunk size is aligned to 8 bytes

Then for every GEP instruction, we insert following checking code:
```c
// %result = getelementptr %pointer, ...
ShadowAddr = GEP & kShadowMask;
Base = GEP & kShadowBase;
Packed = *(int32_t *) ShadowAddr;
Front = Packed & 0xffffffff;
Back = Packed >> 32;
Begin = Base - (Front << 3);
End = Base + (Back << 3);
if (GEP < Begin || GEP + NeededSize >= End)
  report_overflow();
```

### Stack/Global Overflow Checking

If a global pointer is stored or passed as an argument, it may violate the One Definition Rule. To address this, shadow memory will be added for the global pointer. The same goes for stack pointers: if a stack pointer is stored or passed as an argument, shadow memory will be allocated for it. So that we can check them with same method of heap overflow checking.

An unsafe stack in the heap can reduce the time consumption of transferring many stacks to the heap. To manage, allocate a large block of memory on the heap, use it as a stack and cut off pieces as needed. Track the current position and size of each data piece, updating as data is pushed and popped. When the unsafe stack is consumed, allocate a new unsafe stack chunk and connect using a list.

### In-Field Overflow Checking
Instrumenting the in-filed overflow checking do not require any runtime support. Consider the following example:
```c
struct Y{
  int a[1000];
};
struct X{
  int a;
  struct Y b[100];
};
int foo(struct X *x, int i, int j, int k) {
  return x[k].b[i].a[j];
}
```
The GEP of calculating `x[k].b[i].a[j]` will be like
```llvm
getelementptr %struct.X, %struct.X* %x, i64 %k, i32 1, i64 %i, i32 0, i64 %j
```
Note that the overflow caused by `k` is not an in-field overflow. This can be checked through heap overflow checking. To verify the bounds of `i` and `j`, we only need to check if `i < 100` and `j < 1000` at runtime, since the size of the fields can be determined at compile time.

## False Positive Cases
In theory, by adding checking code at every GEP and BC to verify that the input pointer and result pointer belong to the same memory chunk, we can potentially prevent all memory overflows. However, we found many false positive cases in practice.

### Poor Programming Practices
```c
int *a = ptr + overflow_length;
foo(a);
```
The code creates `a` pointer a by adding `overflow_length` to the value of ptr. Then, it passes the pointer a as an argument to the function `foo`. This practice is considered to be poor programming because it can lead to a potential memory overflow. Even though the pointer `a` is not accessed in this code snippet, any calculations based on a would be meaningless. As a result, it is best to avoid storing or passing an overflow pointer as an argument to prevent potential memory safety issues.

There is an [php-8.2.2](https://github.com/php/php-src/blob/b20c0e925fe401a44a99b0d34b438797be865bb0/Zend/zend_API.c#L2859) example which is the most wired case I found.
```c
num_args++;
new_arg_info = malloc(sizeof(zend_arg_info) * num_args);
memcpy(new_arg_info, arg_info, sizeof(zend_arg_info) * num_args);
reg_function->common.arg_info = new_arg_info + 1;
```
If `num_args` is initially zero, then the `new_arg_info` pointer will only allocate memory for a single `zend_arg_info` structure. When `reg_function->common.arg_info = new_arg_info + 1` is executed, `reg_function->common.arg_info` will point to the memory location at the end the allocated memory. 

However, I found the code only accesses `reg_function->common.arg_info[-1]` and never accesses any memory locations beyond the allocated memory, then the buffer overflow issue will not occur. However, this still leaves room for potential bugs or errors in the future, as the code may be modified or updated to access other memory locations.

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