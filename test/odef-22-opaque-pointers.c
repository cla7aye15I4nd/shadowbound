// BUG 22: the pass crashes on opaque pointers.
//
// Where: OverflowDefense.cpp (getPointerElementType() is used throughout).
//
// The pass only works with typed pointers. LLVM 15 uses opaque pointers by
// default; the repository only avoids the crash by configuring clang with
// -DCLANG_ENABLE_OPAQUE_POINTERS=OFF. With opaque pointers (cc1
// -opaque-pointers, or a toolchain built with the default) clang crashes
// inside the pass instead of reporting the unsupported configuration.
//
// Expected: a clear error, not a compiler crash.
//
// RUN: not %clang -fsanitize=shadowbound -Xclang -opaque-pointers -O2 -c %s -o /dev/null 2>&1 | FileCheck %s
//
// CHECK: error: {{.*}}requires typed pointers
// CHECK-NOT: PLEASE submit a bug report

char get(char *p, long i) { return p[i]; }
