// A pointer that only feeds other (checked) pointer arithmetic must not be
// checked on its own: the optimizer may build it out of bounds.
//
// Reduced from nginx's X-Forwarded-For parser (ngx_http_get_forwarded_addr),
// which aborted every request with that header under ShadowBound. The source
// computes `xff + len - 1`, in bounds; the optimizer reassociates it to
// `(xff - 1) + len`, and the intermediate `xff - 1` (a GEP without `inbounds`)
// lies before the object when `xff` is its first byte. It only feeds the
// checked pointer, so it is not checked itself (-shadowbound-intermediate-opt).
// The dereferenced pointer is still checked: a real overflow through the same
// code is caught.
//
// (One round only: with `recursive`, nginx also computes `p - 1 - xff` with
// `p == xff`, a pointer before the object built in the source itself, which
// ShadowBound reports by design.)
//
// RUN: %clang_sb -O2 %s -o %t
// RUN: %t inbounds 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t overflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clang_sb -O2 -flto -fuse-ld=lld %s -o %t.lto
// RUN: %t.lto inbounds 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t.lto overflow 2>&1 | FileCheck %s --check-prefix=OOB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Reduced from ngx_http_get_forwarded_addr_internal: walk the list from the
// end, one item per iteration while `recursive` and `trust(item)` hold.
__attribute__((noinline)) int trust(const char *item) { return item[0] == '1'; }

__attribute__((noinline)) long last_item(const char *xff, long xfflen,
                                         int recursive) {
  const char *p;
  long found = -1;
  do {
    for (p = xff + xfflen - 1; p > xff; p--, xfflen--)
      if (*p != ' ' && *p != ',')
        break;
    for (; p > xff; p--)
      if (*p == ' ' || *p == ',') {
        p++;
        break;
      }
    found = p - xff;
    if (!trust(p))
      break;
    xfflen = p - 1 - xff;
  } while (recursive && p > xff);
  return found;
}

int main(int argc, char **argv) {
  const char *hdr = "192.0.2.1, 10.0.0.1";
  long n = (long)strlen(hdr);
  char *xff = malloc(n); // xff is the first byte of its heap object
  memcpy(xff, hdr, n);

  int overflow = argc > 1 && strcmp(argv[1], "overflow") == 0;
  // INB: inbounds-ok 11
  // OOB: Overflow detected
  // OOB-NOT: UNDETECTED
  long r = last_item(xff, overflow ? n + 4096 : n, 0);
  printf(overflow ? "UNDETECTED %ld\n" : "inbounds-ok %ld\n", r);
  free(xff);
  return 0;
}
