// LTO: with -flto, instrumentation moves to link time on the merged module.
//
// Where: BackendUtil.cpp (pre-link: ShadowBoundLTOPrepPass), and
// PassBuilderPipelines.cpp (link time: ShadowBoundLTOPass in the full and thin
// LTO pipelines).
//
// The pre-link compile only marks the module and its functions; the checks are
// inserted by the LTO pipeline, after internalization, so the interprocedural
// non-heap analysis sees every caller of what used to be an external function.
// This is what the out-of-tree analyzer/ + -shadowbound-pattern-opt-file approximated.
//
// Pre-link: no checks, no runtime constructor, just the marker.
// RUN: %clang -flto -fsanitize=shadowbound -O2 -c %s -o %t.bc
// RUN: %opt -opaque-pointers=0 %t.bc -S -o - | FileCheck %s --check-prefix=PRE
//
// Link time: the LTO pipeline instruments the merged, internalized module.
// RUN: %opt -opaque-pointers=0 -passes='internalize,lto<O2>' \
// RUN:   -internalize-public-api-list=main %t.bc -S -o - | FileCheck %s
//
// A module compiled without ShadowBound is left alone by the LTO pipeline.
// RUN: %clang -flto -O2 -c %s -o %t.plain.bc
// RUN: %opt -opaque-pointers=0 -passes='lto<O2>' %t.plain.bc -S -o - \
// RUN:   | FileCheck %s --check-prefix=PLAIN

#include <stdlib.h>

void sink(void *);

// External, but after internalization every caller is known and passes a
// stack buffer: no check at link time.
__attribute__((noinline)) void fill(char *p, long i) { p[i] = 1; }

// External and called with a heap pointer: keeps its check.
__attribute__((noinline)) void fill_heap(char *p, long i) { p[i] = 2; }

int main(int argc, char **argv) {
  char buf[64];
  sink(buf);
  fill(buf, argc);
  char *h = malloc(64);
  fill_heap(h, argc);
  sink(h);
  return 0;
}

// PRE-NOT: call void @__shadowbound_abort()
// PRE-NOT: shadowbound.module_ctor
// PRE: attributes #{{[0-9]+}} = { {{.*}}"shadowbound-lto"
// PRE: !{i32 7, !"shadowbound-lto", i32 1}

// CHECK-DAG: @llvm.global_ctors = {{.*}}@shadowbound.module_ctor
// CHECK-LABEL: define internal {{.*}}@fill(
// CHECK-NOT: call void @__shadowbound_abort()
// CHECK: ret void
// CHECK-LABEL: define internal {{.*}}@fill_heap(
// CHECK: call void @__shadowbound_abort()

// PLAIN-NOT: __shadowbound
// PLAIN-NOT: shadowbound.module_ctor
