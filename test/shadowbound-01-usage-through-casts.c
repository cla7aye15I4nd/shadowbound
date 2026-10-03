// BUG 01: pointers whose only users are casts / selects / atomics get no check.
//
// Where: OverflowDefense.cpp, GetPtrUsage() / filterToInstrument().
//
// GetPtrUsage() follows only PHI users and only counts load, store, ret and
// CallInst as uses. Any other user (bitcast, select, GEP, atomicrmw,
// cmpxchg, ...) yields kPtrNone, and filterToInstrument() drops the GEP no
// matter how large its offset is.
//
// Common C code hits this. `*(int *)(buf + i)` becomes
// `gep i8 -> bitcast i32* -> load`. The GEP's only user is the bitcast, so
// the GEP is dropped. The bitcast is 4 bytes (<= kReservedBytes), so
// isShrinkBitCast() drops it too, and the access gets no bounds check.
//
// Expected: each function below contains a bounds check.
//
// RUN: %shadowbound_ir %s 2>/dev/null | FileCheck %s

// CHECK-LABEL: @read_int(
// CHECK: call void @__shadowbound_abort()
int read_int(char *buf, long i) { return *(int *)(buf + i); }

// CHECK-LABEL: @read_select(
// CHECK: call void @__shadowbound_abort()
int read_select(int *a, int *b, long i, int c) { return *(c ? a + i : b); }

// CHECK-LABEL: @atomic_inc(
// CHECK: call void @__shadowbound_abort()
void atomic_inc(long *p, long i) {
  __atomic_fetch_add(&p[i], 1, __ATOMIC_RELAXED);
}

// CHECK-LABEL: @shadowbound.module_ctor(
