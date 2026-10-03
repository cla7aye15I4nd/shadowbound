// BUG 18: isStdFunction() runs a shell command built from the function name.
//
// Where: Identification.cpp, isStdFunction() (called for every function in
//        OverflowDefense::sanitizeFunction()).
//
// The name is demangled with popen("c++filt " + name). A symbol name chosen
// through an asm label (`__asm__("...")`) is passed to /bin/sh unquoted, so
// compiling an untrusted source file executes arbitrary shell commands. It
// also spawns one process per function, which is slow.
//
// Expected: compiling this file does not create the marker file.
//
// RUN: rm -f %t.marker
// RUN: %clang -fsanitize=overflow-defense -O2 -S -emit-llvm -o /dev/null -DMARKER=%t.marker %s 2>/dev/null
// RUN: test ! -e %t.marker

#define STR2(x) #x
#define STR(x) STR2(x)

void victim(char *p) __asm__("odef_poc$(touch${IFS}" STR(MARKER) ")");
void victim(char *p) { p[1] = 0; }
