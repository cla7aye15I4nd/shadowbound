#!/usr/bin/env python3

import os
import argparse

fprate = [
    '508.namd_r',
    "510.parest_r",
    '511.povray_r',
    '519.lbm_r',
    '526.blender_r',
    '538.imagick_r',
    '544.nab_r',
]

intrate = [
    '500.perlbench_r',
    '502.gcc_r',
    '505.mcf_r',
    '520.omnetpp_r',
    '523.xalancbmk_r',
    '525.x264_r',
    '531.deepsjeng_r',
    '541.leela_r',
    '557.xz_r'
]

all_c = [
  '400.perlbench',
  '403.gcc',
  '429.mcf',
  '433.milc',
  '445.gobmk',
  '456.hmmer',
  '458.sjeng',
  '462.libquantum',
  '464.h264ref',
  '470.lbm',
  '482.sphinx3'
]

all_cpp = [
  '444.namd',
  '447.dealII',
  '450.soplex',
  '453.povray',
  '473.astar',
  '483.xalancbmk'
]

basedir = os.path.join(os.path.dirname(os.path.realpath(__file__)), '..')

parser = argparse.ArgumentParser(
    description='Compile SPEC CPU 2017/2006 bitcodes')
parser.add_argument('--spec2017dir', type=str,
                    default=os.path.join(os.getenv('HOME'), 'cpu2017'))
parser.add_argument('--spec2006dir', type=str,
                    default=os.path.join(os.getenv('HOME'), 'cpu2006'))


args = parser.parse_args()

benchdir = os.path.join(args.spec2017dir, 'benchspec', 'CPU')
for bench in fprate + intrate:
  buildpath = os.path.join(benchdir, bench, 'build', 'build_peak_laid.0000')
  bitcodepath = os.path.join(basedir, 'bitcodes', bench)
  if not os.path.exists(bitcodepath):
    os.makedirs(bitcodepath)
  
  print('Copying bitcode files for ' + bench)
  for root, dirs, files in os.walk(buildpath):
    for file in files:
      if file.endswith('.bc'):
        bcfile = os.path.join(root, file)
        os.system('cp ' + bcfile + ' ' + bitcodepath)

benchdir = os.path.join(args.spec2006dir, 'benchspec', 'CPU2006')
for bench in all_c + all_cpp:
  buildpath = os.path.join(benchdir, bench, 'build', 'build_base_laid.0000')
  bitcodepath = os.path.join(basedir, 'bitcodes', bench)
  if not os.path.exists(bitcodepath):
    os.makedirs(bitcodepath)
  
  print('Copying bitcode files for ' + bench)
  for root, dirs, files in os.walk(buildpath):
    for file in files:
      if file.endswith('.bc'):
        bcfile = os.path.join(root, file)
        os.system('cp ' + bcfile + ' ' + bitcodepath)
