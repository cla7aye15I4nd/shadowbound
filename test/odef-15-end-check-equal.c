// BUG 15: the upper-bound check lets a pointer reach the very end of the chunk.
//
// Where: OverflowDefense.cpp, instrumentGep() / commitClusterCheck()
//        (`ICmpUGT(CmpPtr, End)` with -odef-tail-check off by default).
//
// The paper's check is `res >= end`. The code uses `res > end`, where End is
// the end of the whole chunk including the 0x20 reserved bytes, and ignores
// the access width. A pointer equal to End passes, and the following 8-byte
// load reads the next chunk.
//
// The elimination rules (isZeroAccessGep, isShrinkBitCast) assume that a
// checked pointer still has kReservedBytes of the chunk in front of it, so
// the check has to be `ptr > End - max(access size, kReservedBytes)`.
//
// Expected: the upper bound used by get() is End - 32.
//
// RUN: %odef_ir %s 2>/dev/null | FileCheck %s

// CHECK-LABEL: @get(
// The access pointer is bumped by kReservedBytes (32) before the end compare,
// so a pointer at the end of the chunk (and the reserve) is rejected.
// CHECK: add i64 %{{[0-9]+}}, 32
// CHECK: icmp ugt i64
// CHECK: call void @__odef_abort()
long get(long *p, long i) { return p[i]; }

// CHECK-LABEL: @odef.module_ctor(
