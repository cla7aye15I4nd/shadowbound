#!/bin/bash

if [ $# -ne 1 ]; then
    benchmark="505.mcf_r"
else
    benchmark=$1
fi

echo "Running analyzer on ${benchmark}..."
./build/harness/analyzer `find ../bitcodes/${benchmark} -name *bc | xargs`
