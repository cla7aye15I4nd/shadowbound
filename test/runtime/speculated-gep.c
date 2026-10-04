// A check belongs where the pointer is used, not where the optimizer computed
// it: address computations get hoisted above the condition guarding their use.
//
// Reduced from SPEC CPU2017 525.x264_r (ldecod, EdgeLoopLumaVer):
//   QP = pl ? (MbP->qpc[pl-1] + MbQ->qpc[pl-1] + 1) >> 1 : (MbP->qp + ...);
// With pl == 0 the program only reads qp, but the optimizer turns this into a
// load from `select(pl == 0, &qp, &qpc[pl-1])`, computing &qpc[pl-1] (pl-1
// zero-extended, 16 GB past the object) unconditionally. Checking it where it
// is computed aborted the benchmark; the check now applies only when the
// select picks it. A real out-of-bounds
// access through the same expression is still caught.
//
// RUN: %clang_sb %s -o %t
// RUN: %t 0 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t 9 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clang_sb -flto -fuse-ld=lld %s -o %t.lto
// RUN: %t.lto 0 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t.lto 9 2>&1 | FileCheck %s --check-prefix=OOB

#include <stdio.h>
#include <stdlib.h>

typedef struct Macroblock {
  void *p_Vid;
  char pad[56];
  int qp;
  int qpc[2];
  struct Macroblock *mbleft;
} Macroblock;

// Becomes `load (select pl == 0, &qp, &qpc[pl-1])`: both addresses are
// computed up front, so &qpc[pl-1] exists even when pl == 0.
__attribute__((noinline)) int qp_of(Macroblock *MbQ, unsigned pl) {
  return pl ? MbQ->qpc[pl - 1] : MbQ->qp;
}

int main(int argc, char **argv) {
  Macroblock *mb = calloc(1, sizeof(Macroblock));
  mb->qp = 26;
  unsigned pl = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
  // INB: qp=26
  // OOB: Overflow detected
  // OOB-NOT: qp=
  printf("qp=%d\n", qp_of(mb, pl));
  free(mb);
  return 0;
}
