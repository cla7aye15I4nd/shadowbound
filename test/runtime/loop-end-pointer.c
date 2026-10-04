// A loop's pointer increment is checked where the next iteration uses it.
//
// `for (; first != last; ++first) destroy(first);` ends with first == last,
// one past the array; the exit test keeps it out of the body. Checked at the
// increment with room for a whole element, it aborted when elements are larger
// than the 32-byte reserve (SPEC 520.omnetpp_r destroying a vector of 72-byte
// ValueIterator::Item). The check now applies to the loop-header phi, i.e. to
// every pointer the body uses. A loop that runs past the end is still caught.
//
// RUN: %clang_sb %s -o %t
// RUN: %t 1 2>&1 | FileCheck %s --check-prefix=OK
// RUN: %t 3 2>&1 | FileCheck %s --check-prefix=OK
// RUN: not %t overrun 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clang_sb -flto -fuse-ld=lld %s -o %t.lto
// RUN: %t.lto 1 2>&1 | FileCheck %s --check-prefix=OK
// RUN: not %t.lto overrun 2>&1 | FileCheck %s --check-prefix=OOB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Item {
  char *name;
  char inline_name[16];
  long value[6];
} Item; // 72 bytes

__attribute__((noinline)) void destroy_n(Item *first, long n) {
  for (Item *last = first + n; first != last; ++first) {
    if (first->name != first->inline_name)
      free(first->name);
    first->value[5] = 0;
  }
}

int main(int argc, char **argv) {
  int overrun = argc > 1 && strcmp(argv[1], "overrun") == 0;
  long n = overrun ? 1 : atol(argv[1]);
  Item *items = malloc(n * sizeof(Item));
  for (long i = 0; i < n; i++)
    items[i].name = items[i].inline_name;
  // OOB: Overflow detected
  // OOB-NOT: destroyed
  destroy_n(items, n + (overrun ? 2 : 0));
  // OK: destroyed
  printf("destroyed %ld\n", n);
  free(items);
  return 0;
}
