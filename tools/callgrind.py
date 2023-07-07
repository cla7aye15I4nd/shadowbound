#!/usr/bin/env python3

import os
import argparse

basedir = os.path.join(os.path.dirname(os.path.realpath(__file__)), '..')

parser = argparse.ArgumentParser(
    description='Compile SPEC CPU 2017/2006 bitcodes')
parser.add_argument('testcase', type=str)
parser.add_argument('--spec2017dir', type=str,
                    default=os.path.join(os.getenv('HOME'), 'cpu2017', 'benchspec', 'CPU'))
parser.add_argument('--spec2006dir', type=str,
                    default=os.path.join(os.getenv('HOME'), 'cpu2006', 'benchspec', 'CPU2006'))

args = parser.parse_args()

benchmark = {
  'spec2017': {
    'testcases': [
      '508.namd_r',
      '510.parest_r',
      '511.povray_r',
      '519.lbm_r',
      '526.blender_r',
      '538.imagick_r',
      '544.nab_r',
      '500.perlbench_r',
      '502.gcc_r',
      '505.mcf_r',
      '520.omnetpp_r',
      '523.xalancbmk_r',
      '525.x264_r',
      '531.deepsjeng_r',
      '541.leela_r',
      '557.xz_r'
    ],
    'path': args.spec2017dir,
    'run': 'run_peak_refrate_baseline.0000'
  },
  'spec2006': {
    'testcases': [
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
      '482.sphinx3',
      '444.namd',
      '447.dealII',
      '450.soplex',
      '453.povray',
      '473.astar',
      '483.xalancbmk'
    ],
    'path': args.spec2006dir,
    'run': 'run_base_ref_baseline.0000'
  }
}

suite = next((benchmark[s] for s in benchmark if args.testcase in benchmark[s]['testcases']), None)
if suite is None:
  print('ERROR: testcase not found')
  exit(1)

print('Testing ' + args.testcase)
runpath = os.path.join(suite['path'], args.testcase, 'run', suite['run'])

if not os.path.exists(runpath):
  print('ERROR: runpath not found')
  exit(1)

with open(os.path.join(runpath, 'speccmds.cmd')) as f:
  commands = []
  for line in f.read().splitlines():
    if line.startswith('-o') or line.startswith('-i'):
      line_split = line.split()

      inp = None
      if line.startswith('-i'):
          inp = line_split[1]
      while not line_split[0].startswith('../run'):
          line_split.pop(0)
      
      cmd = ' '.join(line_split)
      if inp:
        cmd += ' < ' + inp
      
      commands.append(cmd)

print('Found ' + str(len(commands)) + ' commands in ' + runpath)
for idx, cmd in enumerate(commands):
  os.system('rm -f ' + os.path.join(runpath, 'callgrind.out.*'))

  print('Running ' + cmd)
  os.chdir(runpath)
  os.system('valgrind --tool=callgrind ' + cmd)

  callgrind_file = next((f for f in os.listdir(runpath) if f.startswith('callgrind.out.')), None)
  if callgrind_file is None:
    print('ERROR: callgrind file not found')
    exit(1)
  
  src = os.path.join(runpath, callgrind_file)
  dst = os.path.join(basedir, 'callgrind' + args.testcase + '.' + str(idx) + '.out')
  os.system('cp ' + src + ' ' + dst)
