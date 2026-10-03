#!/usr/bin/env python3
# Compute SPEC CPU2017 peak run-time overhead (shadowbound vs native) from the
# SPEC result CSVs. Overhead geomean is geomean(shadowbound/native) - 1.
import csv, glob, math, os, re, sys

result_dir = sys.argv[1] if len(sys.argv) > 1 else "."

def parse(path):
    cfg, times = None, {}
    in_full = hdr = None
    for ln in open(path).read().splitlines():
        if ln.startswith('"runcpu command:"'):
            m = re.search(r'--config\s+(\S+)', ln)
            if m:
                cfg = m.group(1)
        if ln.startswith('"Full Results Table"'):
            in_full, hdr = True, None
            continue
        if in_full:
            if ln.startswith('"') and 'Results Table' in ln:
                in_full = False
                continue
            row = next(csv.reader([ln]))
            if not row or not row[0]:
                continue
            if row[0] == "Benchmark":
                hdr = row
                continue
            if hdr and re.match(r'^\d+\.', row[0]):
                try:
                    times[row[0]] = float(row[7])  # Est. Peak Run Time
                except (IndexError, ValueError):
                    pass
    return cfg, times

cfgs = {}
for path in glob.glob(os.path.join(result_dir, "*rate.ref*.csv")):
    cfg, times = parse(path)
    if cfg:
        cfgs.setdefault(cfg, {}).update(times)

native = cfgs.get("native-local", {})
sb = cfgs.get("shadowbound-local", {})
benches = sorted(set(native) & set(sb))
if not benches:
    print("No paired native-local / shadowbound-local results in", result_dir)
    sys.exit(1)

print(f"{'benchmark':16}{'native(s)':>11}{'shadow(s)':>11}{'overhead':>10}")
ratios = []
for b in benches:
    r = sb[b] / native[b]
    ratios.append(r)
    print(f"{b:16}{native[b]:11.2f}{sb[b]:11.2f}{(r-1)*100:9.2f}%")
gm = math.exp(sum(math.log(r) for r in ratios) / len(ratios))
print("-" * 48)
print(f"{'GEOMEAN':16}{'':11}{'':11}{(gm-1)*100:9.2f}%  ({len(ratios)} benchmarks)")
