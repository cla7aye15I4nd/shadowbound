// BUG 11: bitcasts inside a merged (cluster) check are never checked.
//
// Where: OverflowDefense.cpp, setOffsetDir() / commitClusterCheck().
//
// When a source has enough uses (or a use in a loop) its checks are merged
// into a ClusterCheck. A bitcast taken off that source has direction
// kOffsetUnknown (0), because setOffsetDir() never meets a GEP on the way back
// to the source. commitClusterCheck() then emitted `or(false, false)` for it,
// i.e. no check, and ignored the cast's access size. A `char *` -> `struct Big *`
// cast inside a loop was therefore never validated, even though
// instrumentBitCast() would check it outside a cluster.
//
// Expected: loop() checks both the pointer arithmetic (base + offs[i]) and the
// cast to struct Big (access size 256), so two checks are emitted.
//
// RUN: %shadowbound_ir %s 2>/dev/null | FileCheck %s

struct Big {
  char data[256];
};
void use_big(struct Big *b);

// CHECK-LABEL: @loop(
// The cast to a 256-byte struct must produce a real access-size compare
// (ptr + 256 > end), not a dead `or(false, false)`.
// CHECK: add i64 %{{[0-9]+}}, 256
// CHECK: call void @__shadowbound_abort()
void loop(char *base, long *offs, int n) {
  for (int i = 0; i < n; i++)
    use_big((struct Big *)(base + offs[i]));
}

// CHECK-LABEL: @shadowbound.module_ctor(
