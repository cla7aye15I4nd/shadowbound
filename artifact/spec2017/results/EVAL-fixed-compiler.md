# SPEC CPU2017 evaluation of the fixed ShadowBound compiler

Toolchain: the `fix/compiler-poc-tests` branch (26 correctness fixes + rename
`-fsanitize=overflow-defense` → `-fsanitize=shadowbound`), built with clang/lld
in an Ubuntu 22.04 container. SPEC CPU2017 v1.1.8, peak tune, `-O1`
(the artifact's setting), 1 iteration, ref size.

Overhead = shadowbound_time / native_time − 1. Geomean is geomean(ratio) − 1.

## Real protection (shadow checks active)

| benchmark   | native (s) | shadowbound (s) | overhead |
|-------------|-----------:|----------------:|---------:|
| 505.mcf_r   |     191.96 |          216.91 |  +13.00% |
| 519.lbm_r   |     121.51 |          122.05 |   +0.44% |

Both also pass at `test` size. mcf is pointer/heap-intensive (higher overhead);
lbm is compute-bound (near-zero), consistent with the paper's per-benchmark
spread.

## Benchmarks that abort under real protection

557.xz_r, 541.leela_r, 511.povray_r (and 508.namd_r) abort with "Overflow
detected", in SPEC's shared `spec_mem_io` SHA-512 harness (`sha_process`).
525.x264_r hit a transient input-copy setup error under concurrent runs and
sets up fine standalone.

These are NOT regressions from the fixes. Verified against the baseline
(pre-fix, commit 8a0bdd8ec) compiler:

- **557.xz_r aborts identically on the baseline**, via the runtime libc
  interceptor `check_range` (an issue in `odef_interceptors.cpp`, independent
  of the instrumentation pass).
- **541.leela_r and 511.povray_r do not even build on the baseline** compiler;
  they build and link cleanly with the fixed compiler.
- 508.namd_r is run with `-mllvm -odef-perf-test=1` (checks disabled) in the
  original artifact config, i.e. the authors did not run it with real checks.

Disabling the fixes' most-invasive changes one at a time (broadened pointer-use
classification, reserve-headroom end check, both-direction checks) did not
remove the aborts, confirming they stem from pre-existing ShadowBound behavior
(the runtime interceptor and the design's checking of pointer arithmetic),
not from the fixes.

## Notes
- `perf-test` mode is confounded for timing: the runtime `check_range`
  interceptor misreads the 0xFF perf-test shadow, so perf-test runs are not
  usable for overhead numbers here.
- The fixed compiler builds the full C/C++ subset
  (mcf, lbm, xz, leela, x264, namd, povray) in both native and shadowbound
  configs; the baseline fails to build leela and povray.
