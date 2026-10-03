#!/bin/bash
# Run SPEC CPU2017 locally with the ShadowBound compiler (test it locally before
# relying on any image). SPEC cannot be redistributed, so you must supply the
# ISO. This does not use Docker.
#
# Usage:
#   CPU2017_ISO=/path/to/cpu2017.iso \
#   SHADOWBOUND_BUILD=/path/to/llvm-project/build \
#   artifact/spec2017/local-run.sh [benchmarks...]
#
# Defaults to a small C/C++ subset. Produces a native and a shadowbound run and
# prints the per-benchmark and geomean overhead.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
CPU2017_ISO="${CPU2017_ISO:?set CPU2017_ISO to your SPEC CPU2017 ISO}"
SHADOWBOUND_BUILD="${SHADOWBOUND_BUILD:-$REPO/llvm-project/build}"
WORK="${WORK:-$REPO/.spec}"
BENCHES=("${@:-505.mcf_r 519.lbm_r 557.xz_r 541.leela_r 525.x264_r}")

CLANGDIR="$SHADOWBOUND_BUILD/bin"
[ -x "$CLANGDIR/clang" ] || { echo "No clang at $CLANGDIR; build the toolchain first." >&2; exit 1; }

# ShadowBound maps shadow/allocator at fixed addresses; disable ASLR for the run.
if [ "$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null || echo 2)" != "0" ]; then
    echo "note: ASLR is on; run under 'setarch -R' or 'sysctl kernel.randomize_va_space=0' if runs crash." >&2
fi

# 1. Extract + install SPEC (once).
if [ ! -x "$WORK/bin/runcpu" ]; then
    rm -rf "$WORK.iso" && mkdir -p "$WORK.iso"
    (cd "$WORK.iso" && 7z x -bso0 -bsp0 "$CPU2017_ISO" >/dev/null)
    chmod -R a+rx "$WORK.iso/bin" "$WORK.iso/tools"
    (cd "$WORK.iso" && echo yes | ./install.sh -d "$WORK")
fi

# 2. Generate configs pointing at this toolchain.
gen_cfg() {
    sed -e "s|/shadowbound/llvm-project/build/bin|$CLANGDIR|g" \
        -e "s|/root/cpu2017/whitelist.txt|$REPO/artifact/spec2017/config/whitelist.txt|g" \
        -e "s|/shadowbound/config/|$REPO/config/|g" \
        "$REPO/artifact/spec2017/config/$1.cfg" > "$WORK/config/$2.cfg"
}
gen_cfg native native-local
gen_cfg shadowbound shadowbound-local

# 3. Build + run.
cd "$WORK" && source shrc >/dev/null
runcpu --config native-local      --action run --size ref --iterations 1 --noreportable ${BENCHES[@]}
runcpu --config shadowbound-local --action run --size ref --iterations 1 --noreportable ${BENCHES[@]}

# 4. Report overhead (geomean of ratios).
python3 "$REPO/artifact/spec2017/scripts/overhead.py" "$WORK/result"
