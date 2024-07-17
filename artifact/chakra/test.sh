#!/bin/bash

cd /ChakraCore/test/benchmarks/
perl perf.pl -baseline -binary:/ChakraCore/build/shadowbound/Release/ch
perl perf.pl -binary:/ChakraCore/build/native/Release/ch | tee /results/shadowbound.txt
