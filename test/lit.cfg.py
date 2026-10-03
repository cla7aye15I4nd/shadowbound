# -*- Python -*-
# lit configuration for ShadowBound compiler regression / PoC tests.
#
# Run with:
#   python3 llvm-project/llvm/utils/lit/lit.py -v test/
#
# The toolchain is taken from $SHADOWBOUND_BUILD (default: llvm-project/build).

import os

import lit.formats

config.name = 'ShadowBound'
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = ['.c', '.cpp', '.ll']
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.test_source_root, 'Output')

# Pre-existing ad-hoc sample programs, not lit tests.
config.excludes = ['Inputs', 'Output', 'test.c', 'pgo.c', 'pattern.cpp']

repo_root = os.path.dirname(config.test_source_root)
build_dir = os.environ.get('SHADOWBOUND_BUILD',
                           os.path.join(repo_root, 'llvm-project', 'build'))
bin_dir = os.path.join(build_dir, 'bin')

config.environment['PATH'] = os.pathsep.join(
    [bin_dir, config.environment.get('PATH', os.environ.get('PATH', ''))])

clang = os.path.join(bin_dir, 'clang')
clangxx = os.path.join(bin_dir, 'clang++')

# %clang_odef compiles and links a program with ShadowBound instrumentation
# in its default configuration (-O2, as recommended by the README).
config.substitutions.append(
    ('%clang_odef', clang + ' -fsanitize=overflow-defense -O2 -g'))
config.substitutions.append(
    ('%clangxx_odef', clangxx + ' -fsanitize=overflow-defense -O2 -g'))
# %odef_ir / %odefxx_ir print the instrumented IR of a C / C++ file, so a
# test can FileCheck which bounds checks the pass emitted.
config.substitutions.append(
    ('%odef_ir', clang + ' -fsanitize=overflow-defense -O2 -S -emit-llvm -o -'))
config.substitutions.append(
    ('%odefxx_ir',
     clangxx + ' -fsanitize=overflow-defense -O2 -S -emit-llvm -o -'))
config.substitutions.append(('%clangxx', clangxx))
config.substitutions.append(('%clang', clang))
config.substitutions.append(('%opt', os.path.join(bin_dir, 'opt')))
config.substitutions.append(('%inputs',
                             os.path.join(config.test_source_root, 'Inputs')))
