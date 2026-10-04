#!/bin/bash
# Install the `shadowbound` src.alt for each benchmark under this directory
# into a SPEC CPU2017 installation, using SPEC's own alternate-source
# mechanism (makesrcalt). Only the patches live in this repository; the
# sources they apply to come from your SPEC installation.
#
# Usage: artifact/spec2017/src.alt/install.sh <path to SPEC CPU2017>
# Then select it in the config, e.g. (already done in shadowbound*.cfg):
#   538.imagick_r:
#      srcalt = shadowbound
set -euo pipefail

SPEC_DIR="$(realpath "${1:?usage: $0 <SPEC CPU2017 directory>}")"
HERE="$(dirname "$(realpath "$0")")"
NAME=shadowbound

cd "$SPEC_DIR"
# shellcheck disable=SC1091
source ./shrc

for patch in "$HERE"/*/"$NAME".patch; do
  bench="$(basename "$(dirname "$patch")")"
  src="$SPEC_DIR/benchspec/CPU/$bench/src"
  alt="$src/src.alt/$NAME"
  echo "== $bench"
  rm -rf "$alt"
  mkdir -p "$alt"
  # Copy the original of every file the patch touches, then patch the copies.
  for f in $(sed -n 's/^+++ \([^\t ]*\).*/\1/p' "$patch"); do
    mkdir -p "$alt/$(dirname "$f")"
    cp "$src/$f" "$alt/$f"
  done
  patch -s -d "$alt" -p0 <"$patch"
  cp "$HERE/README.md" "$alt/README"
  rm -f "$SPEC_DIR/$bench.$NAME".*tar.xz
  makesrcalt "$bench" "$NAME" >/dev/null
  tarball="$(ls "$SPEC_DIR/$bench.$NAME".*tar.xz)"
  specxz -dc "$tarball" | spectar -xf -
  rm -f "$tarball"
  echo "   installed src.alt '$NAME'"
done
