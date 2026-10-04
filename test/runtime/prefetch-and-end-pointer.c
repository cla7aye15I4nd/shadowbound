// Two pointers that are formed but not accessed where they are checked:
//
//  * a prefetch address: a prefetch is only a cache hint, it never faults and
//    reads nothing the program sees. x264 (SPEC 525.x264_r) prefetches
//    &h->mb.mv[l][top_4x4 - 1] for the top macroblock row, about 1.2 KB
//    before the array, and aborted.
//  * an end pointer that is only stored: std::vector::push_back stores
//    `++_M_finish`, one past the last element, and was checked as if a whole
//    element (96 bytes in SPEC 520.omnetpp_r) were accessed there, beyond the
//    32-byte reserve. It only has to be at most one past the end.
//
// Accesses through the same pointers are still checked.
//
// RUN: %clang_sb %s -o %t
// RUN: %t ok 2>&1 | FileCheck %s --check-prefix=OK
// RUN: not %t deref-before 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: not %t deref-past 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clang_sb -flto -fuse-ld=lld %s -o %t.lto
// RUN: %t.lto ok 2>&1 | FileCheck %s --check-prefix=OK
// RUN: not %t.lto deref-before 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: not %t.lto deref-past 2>&1 | FileCheck %s --check-prefix=OOB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char bytes[96]; } KeyValue;
typedef struct { KeyValue *start, *finish, *end; } Vec;

volatile long g_off;
volatile char g_sink;

__attribute__((noinline)) void prefetch_at(int *mv, long off) {
  __builtin_prefetch(&mv[off]);
}

__attribute__((noinline)) void push_back(Vec *v, const KeyValue *x) {
  if (v->finish != v->end) {
    *v->finish = *x;
    ++v->finish; // one past the last element once the vector is full
  }
}

int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "ok";
  int *mv = malloc(64 * sizeof(int));
  Vec v;
  v.start = v.finish = malloc(4 * sizeof(KeyValue));
  v.end = v.start + 4;
  KeyValue x;
  memset(&x, 'x', sizeof x);

  g_off = -300;
  // OOB: Overflow detected
  // OOB-NOT: UNDETECTED
  if (!strcmp(mode, "deref-before")) {
    g_sink = ((char *)mv)[g_off * 4];
    printf("UNDETECTED\n");
    return 0;
  }
  if (!strcmp(mode, "deref-past")) {
    g_off = 5;
    g_sink = v.start[g_off].bytes[0];
    printf("UNDETECTED\n");
    return 0;
  }

  prefetch_at(mv, g_off);
  for (int i = 0; i < 4; i++)
    push_back(&v, &x);
  // OK: ok 4
  printf("ok %ld\n", (long)(v.finish - v.start));
  return 0;
}
