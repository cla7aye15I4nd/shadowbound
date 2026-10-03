// BUG 02: a pointer passed to a call emitted as `invoke` gets no check.
//
// Where: OverflowDefense.cpp, isEscapeInstruction().
//
// Escapes are recognised with dyn_cast<CallInst>. In C++, a call made while
// a destructor is pending is an InvokeInst, which is not a CallInst, so
// `consume(a + i)` is not an escape. The GEP is classified kPtrNone and
// dropped, although the callee treats its argument as a checked pointer.
//
// Expected: test() checks `a + i` before the invoke.
//
// RUN: %shadowboundxx_ir %s 2>/dev/null | FileCheck %s

struct Guard {
  ~Guard();
};
void consume(int *p);

// CHECK-LABEL: @_Z4testPil(
// CHECK: call void @__shadowbound_abort()
// CHECK: invoke void @_Z7consumePi(
void test(int *a, long i) {
  Guard g;
  consume(a + i);
}

// CHECK-LABEL: @shadowbound.module_ctor(
