#include <stdio.h>

int sum8(int *p) {
  int s = 0;
  for (int i = 0; i < 8; i++) {
    s += p[i];
  }
  return s;
}

int and8(int *p) {
  int s = 0;
  for (int i = 0; i < 8; i++) {
    s &= p[i];
  }
  return s;
}

int xor8(int *p) {
  int s = 0;
  for (int i = 0; i < 8; i++) {
    s ^= p[i];
  }
  return s;
}

void __attribute__((noinline)) foo(int *a, int (*f[3])(int *)) {
  for (int i = 0; i < 3; i++) {
    printf("%d\n", f[i](a));
  }
}

int main() {
  int (*f[3])(int *) = {sum8, and8, xor8};
  int a[8] = {1, 2, 3, 4, 5, 6, 7, 8};

  foo(a, f);
}
