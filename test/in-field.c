#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Y{
  int a[1000];
  int c;
};

struct X{
  int a;
  struct Y b[100];
};

__attribute__((noinline)) int foo(struct X *x, int i, int j, int k) {
  return x[k].b[i].a[j];
}

int main(int argc, char **argv) {
  struct X x[10];

  memset(x, 1, sizeof(x));
  
  int i = -1;
  int j = 1;
  int k = 0;
  
  if (argc > 1) i = atoi(argv[1]);
  if (argc > 2) j = atoi(argv[2]);
  if (argc > 3) k = atoi(argv[3]);

  printf("%d\n", foo(x, i, j, k));
}