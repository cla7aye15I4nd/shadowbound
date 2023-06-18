#!/bin/bash

# define function, with one arg (benchmark name)
function run_analyzer {
    echo "Running analyzer on ${1}..."
    ./build/harness/analyzer `find ../bitcodes/${1} -name *bc | xargs` -pattern-opt-file ../config/${1}.json
    ../llvm-project/build/bin/clang-format -i ../config/${1}.json
}

if [ $# -ne 1 ]; then
    run_analyzer "500.perlbench_r"
    run_analyzer "502.gcc_r"
    run_analyzer "505.mcf_r"
    run_analyzer "520.omnetpp_r"
    run_analyzer "523.xalancbmk_r"
    run_analyzer "525.x264_r"
    run_analyzer "531.deepsjeng_r"
    run_analyzer "541.leela_r"
    run_analyzer "557.xz_r"
    run_analyzer "508.namd_r"
    run_analyzer "510.parest_r"
    run_analyzer "511.povray_r"
    run_analyzer "519.lbm_r"
    run_analyzer "526.blender_r"
    run_analyzer "538.imagick_r"
    run_analyzer "544.nab_r"
else
    run_analyzer $1
fi
