// BUG 17: merged checks report pointers derived from non-heap memory.
//
// Where: OverflowDefense.cpp, commitClusterCheck().
//
// When the source is not a heap pointer, the single-check path skips the
// check entirely. The merged (cluster) path instead feeds Begin = 0 and
// End = kMaxAddress (2^48) into the per-instruction compares. Any derived
// pointer >= 2^48 (e.g. NULL - 1, tagged pointers, or kernel addresses in
// kernel mode) is then reported as an overflow, although the same code
// outside a loop is not checked at all.
//
// Expected: the non-heap fallback for End is all ones (never fires).
//
// RUN: %odef_ir %s 2>/dev/null | FileCheck %s

void sink(char *);

// CHECK-LABEL: @loop(
// CHECK-NOT: 281474976710656
// CHECK: phi i64 {{.*}}-1
void loop(char *base, long off, int n) {
  for (int i = 0; i < n; i++)
    sink(base + off + i);
}

// CHECK-LABEL: @odef.module_ctor(
