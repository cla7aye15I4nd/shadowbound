# -*- Python -*-
# lit configuration for ShadowBound RUNTIME tests. Unlike the compile-only
# suite in test/, these compile, link against the ShadowBound runtime, and run
# the program, so they need compiler-rt built.
#
# Run with:
#   python3 llvm-project/llvm/utils/lit/lit.py -v test/runtime/
#
# ShadowBound maps its shadow and allocator at fixed addresses; the runtime
# re-execs with ASLR disabled when permitted, but CI should also disable ASLR
# (kernel.randomize_va_space=0) for determinism.

import os

import lit.formats

config.name = 'ShadowBound-Runtime'
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = ['.c', '.cpp']
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.test_source_root, 'Output')

repo_root = os.path.dirname(os.path.dirname(config.test_source_root))
build_dir = os.environ.get('SHADOWBOUND_BUILD',
                           os.path.join(repo_root, 'llvm-project', 'build'))
bin_dir = os.path.join(build_dir, 'bin')
config.environment['PATH'] = os.pathsep.join(
    [bin_dir, config.environment.get('PATH', os.environ.get('PATH', ''))])

clang = os.path.join(bin_dir, 'clang')
# %clang_sb compiles and links an executable with the ShadowBound runtime.
config.substitutions.append(
    ('%clang_sb', clang + ' -fsanitize=shadowbound -O1 -g'))
config.substitutions.append(('%clang', clang))
