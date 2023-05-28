#!/usr/bin/env python3

import os
import argparse

fprate = [
    '508.namd_r',
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

basedir = os.path.join(os.path.dirname(os.path.realpath(__file__)), '..')

parser = argparse.ArgumentParser(
    description='Compile SPEC CPU 2017/2006 bitcodes')
parser.add_argument('--spec2017dir', type=str,
                    default=os.path.join(os.getenv('HOME'), 'cpu2017'))

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
