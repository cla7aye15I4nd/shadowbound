# SPEC CPU2017 source fixes for ShadowBound (`shadowbound` src.alt)

ShadowBound checks a pointer when it is *created*, not only when it is
dereferenced. It tolerates 32 bytes before and after every heap object (the
allocator reserves them), so the common idioms of pointing one past the end
or just before the start are accepted. A program that builds a pointer far
outside its object is reported even if it never accesses memory through it:
forming such a pointer is undefined behavior in C, and ShadowBound treats it
as an overflow.

These patches change such code to compute the same result without forming
the out-of-bounds pointer. They do not change the program's behavior or
output. Install them with `install.sh <SPEC dir>`, which uses SPEC's
`makesrcalt` mechanism; the ShadowBound configs select them with
`srcalt = shadowbound`. The native configs use them too, so both builds
compile the same source.

## 538.imagick_r: `magick/cache.c`, `IsAuthenticPixelCache`

```c
offset = nexus_info->region.y * cache_info->columns + nexus_info->region.x;
status = nexus_info->pixels == (cache_info->pixels + offset) ? ...;
```

The `-edge 41` convolution reads "virtual pixels" around the image, so the
region has `x = y = -41` and `offset` is negative: `cache_info->pixels +
offset` points about 440 KB before the pixel buffer. It is only compared,
never dereferenced, but it is far beyond the 32-byte reserve. The patch
compares the addresses as integers.

## Not patched (fixed in ShadowBound instead)

The other SPEC aborts were false positives in the instrumentation, not out of
bounds pointers in the source:

* 525.x264_r (ldecod): `pl ? MbQ->qpc[pl-1] : MbQ->qp` was turned into a
  load from `select(pl == 0, &qp, &qpc[pl-1])`, so `&qpc[-1]` was computed
  (and checked) even though it is never used. Checks are now placed where a
  pointer is used, and a select's unchosen arm is not checked.
* 525.x264_r (x264): `x264_prefetch(&h->mb.mv[l][top_4x4 - 1])` passes an
  address before the array to a prefetch for the top macroblock row. A
  prefetch never faults or reads memory the program sees, so it is no longer
  a checked use.
* 520.omnetpp_r: `std::vector::push_back` stores `++_M_finish`, a legal
  one-past-the-end pointer, but it was checked as if a whole 96-byte element
  were accessed there. Pointers that are only stored or returned now only
  have to be at most one past the end.
