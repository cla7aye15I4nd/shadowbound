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
    run_analyzer "400.perlbench"
    run_analyzer "403.gcc"
    run_analyzer "429.mcf"
    run_analyzer "433.milc"
    run_analyzer "444.namd"
    run_analyzer "445.gobmk"
    run_analyzer "447.dealII"
    run_analyzer "450.soplex"
    run_analyzer "453.povray"
    run_analyzer "456.hmmer"
    run_analyzer "458.sjeng"
    run_analyzer "462.libquantum"
    run_analyzer "464.h264ref"
    run_analyzer "470.lbm"
    run_analyzer "473.astar"
    run_analyzer "482.sphinx3"
    run_analyzer "483.xalancbmk"
else
    run_analyzer $1
fi
