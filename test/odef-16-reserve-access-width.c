// BUG 16: reserve-based elimination ignores access width and whether the base was checked.
//
// Where: OverflowDefense.cpp, isZeroAccessGep() / isShrinkBitCast().
//
// isZeroAccessGep() drops a GEP whose constant offset is <= 0x20 (`ule`, not
// `<`) and that is dereferenced, relying on the 0x20 reserved bytes after
// every chunk. isShrinkBitCast() drops any cast to a type of at most 0x20
// bytes for the same reason. Both are only sound when:
//   * offset + access size <= 0x20 (an 8-byte load at offset 0x20 ends 8
//     bytes past the reserve), and
//   * the base pointer itself is checked or is the source pointer, not
//     another pointer whose own check was dropped.
// The paper's rule (offset < n AND the result is never used as the base of
// another pointer) is not implemented.
//
// Expected: the 8-byte load at q + 32 keeps its own check.
//
// RUN: %odef_ir %s 2>/dev/null | FileCheck %s

void sink(char *);

// CHECK-LABEL: @tail_read(
// CHECK: call void @__odef_abort()
// CHECK: call void @__odef_abort()
// CHECK: load i64
long tail_read(char *p, long n) {
  char *q = p + n;
  sink(q);
  return *(long *)(q + 32);
}

// CHECK-LABEL: @odef.module_ctor(
