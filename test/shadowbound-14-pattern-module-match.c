// BUG 14: security patterns are matched to static functions by a loose name/suffix test.
//
// Where: OverflowDefense.cpp, patternMatch().
//
// A funarg pattern removes every check on that argument. For a function with
// local linkage the module is compared with
//   getModuleIdentifier().endswith(FAI->getModuleName())
// which is wrong in two ways:
//   * a pattern without a "module" key (it describes an external function)
//     has an empty module name, and endswith("") is always true, so it also
//     applies to any static function with the same name;
//   * "pattern-module-match.c" also matches "odef-14-pattern-module-match.c"
//     (or "dir/x.c" matches "otherdir/x.c"), because only a suffix is compared.
// In both cases an unrelated function silently loses its bounds checks.
//
// Expected: the static store_at() in this file keeps its check with both
// pattern files.
//
// RUN: %shadowbound_ir -mllvm -shadowbound-pattern-opt-file=%inputs/shadowbound-14-no-module.json %s 2>/dev/null | FileCheck %s
// RUN: %shadowbound_ir -mllvm -shadowbound-pattern-opt-file=%inputs/shadowbound-14-suffix-module.json %s 2>/dev/null | FileCheck %s

// CHECK-LABEL: @store_at(
// CHECK: call void @__shadowbound_abort()
static __attribute__((noinline)) void store_at(char *p, long i) { p[i] = 1; }

void entry(char *p, long i) { store_at(p, i); }

// CHECK-LABEL: @shadowbound.module_ctor(
