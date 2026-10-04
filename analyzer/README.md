# analyzer (superseded)

This out-of-tree tool read a program's bitcode and wrote the pattern files in
`config/*.json`, which the compiler consumed through
`-mllvm -shadowbound-pattern-opt-file=...`.

The compiler now does this analysis itself. Build with `-flto` and ShadowBound
instruments the merged program at link time, where the `shadowbound-ipo`
analysis sees every caller:

* `funarg` patterns become the non-heap argument analysis. It is sound, and it
  is also transitive and covers return values, which this tool did not.
* `struct` patterns are available as `-mllvm -shadowbound-struct-heuristic`.
  It is off by default because it is a heuristic, not a proof.

This directory and `config/*.json` stay only to reproduce the paper's original
numbers with `artifact/spec2017/config/shadowbound.cfg`.
