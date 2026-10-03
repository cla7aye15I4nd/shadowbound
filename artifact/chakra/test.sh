#!/bin/bash
set -euo pipefail

# Measure the ShadowBound build relative to the native baseline. (Previously the
# -baseline and measured binaries were swapped, which inverted the overhead.)
cd /ChakraCore/test/benchmarks/
perl perf.pl -baseline -binary:/ChakraCore/build/native/Release/ch
perl perf.pl -binary:/ChakraCore/build/shadowbound/Release/ch | tee /results/shadowbound.txt
