// BUG 26: the driver allows -no-pie, which the ShadowBound runtime cannot support.
//
// Where: clang/lib/Driver/SanitizerArgs.cpp (RequiresPIE does not include
//        OverflowDefense / MemProtect).
//
// The runtime maps shadow memory at 0x2000_0000_0000 and requires the
// address range below it to be empty. A non-PIE executable is loaded at
// 0x400000, so InitShadow() fails. Its result is ignored, and the program
// crashes at the first allocation with no diagnostic.
//
// Expected: the driver rejects -no-pie together with these sanitizers.
//
// RUN: not %clang -fsanitize=shadowbound -no-pie %s -o /dev/null 2>&1 | FileCheck %s
// RUN: not %clang -fsanitize=shadowbound-instrument-only -no-pie %s -o /dev/null 2>&1 | FileCheck %s
//
// CHECK: error: invalid argument '-fsanitize={{.*}}' not allowed with '-no-pie'

int main(void) { return 0; }
