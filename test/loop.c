#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) 
void magic(int *x) {
  *x = rand();
}

__attribute__((noinline)) 
void setup1(int *ptr, int l, int r) {
  for (int i = l; i < r; ++i) {
    magic(ptr + i);
  }
}

__attribute__((noinline)) 
void setup2(int *ptr, int64_t l, int64_t r) {
  for (int64_t i = l; i < r; ++i) {
    magic(ptr + i);
  }
}

__attribute__((noinline)) 
void setup3(int *ptr, int64_t l, int64_t r) {
  int *p = ptr + l;
  while (p < ptr + r) {
    magic(p++);
  }
}

__attribute__((noinline)) 
void setup4(int *ptr, int r) {
  int *p = ptr;
  while (p < ptr + r) {
    magic(p++);
  }
}

__attribute__((noinline)) 
void setup5(int *ptr, int64_t l, int64_t r) {
  int *p = ptr + r;
  while (p > ptr + l) {
    magic(p--);
  }
}

int main() {
  int *x = malloc(sizeof(int) * 0x1000);
  setup1(x, 0, 0x1000);
  setup2(x, 33, 44);
  setup3(x, 444, 555);
  setup4(x + 111, 1000);
  setup5(x, 111, 999);
  printf("%d\n", x[233]);

  return 0;
}
