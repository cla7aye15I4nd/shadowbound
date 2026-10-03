// BUG 05: the two-sided bounds helper relies on undefined behaviour in the compiler.
//
// Where: OverflowDefense.cpp, getPointerBeginEnd() and the other getPointer*
//        helpers.
//
// Commit 1a569d936 changed `if (ClRuntimeName == "default")` into
// `if (Options.Runtime == "runtime")`, but every pass instance is built with
// Runtime == "default" (BackendUtil.cpp). The else branch is
// __builtin_unreachable(). So every GEP whose offset sign is unknown
// (kOffsetBoth, the common case) runs undefined behaviour inside clang.
//
// A Release clang usually works only because the host compiler folded the
// branch away. A Debug clang, or a different host compiler, can crash or emit
// garbage. The tag runtime ("tag") hits __builtin_unreachable() in all four
// helpers.
//
// Expected: a two-sided check (lower and upper bound) is emitted with
// -fsanitize=shadowbound. 

//
// RUN: %shadowbound_ir %s 2>/dev/null | FileCheck %s

// CHECK-LABEL: @get(
// A real two-sided check is emitted (lower bound and reserve-adjusted upper
// bound), not undefined behaviour.
// CHECK-DAG: icmp ugt i64 %{{[0-9]+}}, %{{[0-9]+}}
// CHECK-DAG: add i64 %{{[0-9]+}}, 32
// CHECK: call void @__shadowbound_abort()
char get(char *p, long i) { return p[i]; }

// CHECK-LABEL: @shadowbound.module_ctor(
