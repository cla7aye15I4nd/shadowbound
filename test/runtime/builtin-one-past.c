// A check against a statically known allocation size (BuiltinCheck) must
// accept a one-past-the-end pointer, like the shadow-memory check does.
//
// Checks run when a pointer is created. With the allocation visible in the
// same function (which LTO makes common by inlining wrappers like nginx's
// ngx_alloc), the loop below ends with `p` one past the buffer. That is legal
// and never dereferenced, but the builtin check used the exact size and
// aborted, which made nginx 1.31.6 fail at startup under -flto. A real
// overflow past the reserved bytes must still be caught.
//
// RUN: %clang_sb %s -o %t
// RUN: %t inbounds 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t overflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clang_sb -flto -fuse-ld=lld %s -o %t.lto
// RUN: %t.lto inbounds 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t.lto overflow 2>&1 | FileCheck %s --check-prefix=OOB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *words[] = {"alpha", "beta", "gamma", "delta"};

__attribute__((noinline)) char *join(long extra) {
  size_t size = 0;
  for (int i = 0; i < 4; i++)
    size += strlen(words[i]) + 1;

  char *buf = malloc(size);
  char *p = buf;
  for (int i = 0; i < 4; i++) {
    size_t n = strlen(words[i]) + 1;
    memcpy(p, words[i], n);
    p += n; // one past the end after the last word
  }
  p[extra] = 0; // only reached with a large `extra` in the overflow run
  return buf;
}

int main(int argc, char **argv) {
  int overflow = argc > 1 && strcmp(argv[1], "overflow") == 0;
  if (!overflow) {
    char *b = malloc(64);
    char *p = b;
    for (int i = 0; i < 4; i++) {
      size_t n = strlen(words[i]) + 1;
      memcpy(p, words[i], n);
      p += n;
    }
    // INB: inbounds-ok alpha
    printf("inbounds-ok %s\n", b);
    // Still build the join() buffer, with a write inside the reserve.
    free(join(-1));
    return 0;
  }
  // OOB: Overflow detected
  // OOB-NOT: UNDETECTED
  char *r = join(4096);
  printf("UNDETECTED %p\n", (void *)r);
  return 0;
}
