// BUG 25: the driver accepts sanitizer combinations that cannot work together.
//
// Where: clang/lib/Driver/SanitizerArgs.cpp (IncompatibleGroups has no
//        entry for OverflowDefense / MemProtect / TagOverflowDefense).
//
//   * shadowbound with address / memory / thread / hwaddress links two
//     malloc interposers and two conflicting shadow layouts;
//   * shadowbound with shadowbound-instrument-only links clang_rt.odef and
//     clang_rt.memp, which both define __shadowbound_init / __shadowbound_abort, and runs
//     the instrumentation pass twice.
// These fail late (duplicate symbols, crashes at startup) instead of being
// rejected by the driver.
//
// Expected: the driver rejects each combination.
//
// RUN: not %clang -fsanitize=shadowbound,address -c %s -o /dev/null 2>&1 | FileCheck %s
// RUN: not %clang -fsanitize=shadowbound,memory -c %s -o /dev/null 2>&1 | FileCheck %s
// RUN: not %clang -fsanitize=shadowbound,shadowbound-instrument-only -c %s -o /dev/null 2>&1 | FileCheck %s
// RUN: not %clang -fsanitize=shadowbound-instrument-only,thread -c %s -o /dev/null 2>&1 | FileCheck %s
//
// CHECK: error: invalid argument '-fsanitize={{.*}}' not allowed with '-fsanitize={{.*}}'

int main(void) { return 0; }
