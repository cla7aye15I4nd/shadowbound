// BUG 23: the pass prints debug output to stderr in release builds.
//
// Where: OverflowDefense.cpp, sanitizeFunction() / collectMonoLoop() /
//        monotonicLoopOptimize() (dbgs() without LLVM_DEBUG).
//
// Every compiled function prints "[name]" and per-function check counters
// to stderr, even with a release compiler and no -debug flag. This floods
// build logs and breaks tools that treat compiler stderr output as a
// warning or error.
//
// Expected: a clean compile prints nothing.
//
// RUN: %clang -fsanitize=shadowbound -O2 -c %s -o %t.o 2>&1 | count 0

char get(char *p, long i) { return p[i]; }
