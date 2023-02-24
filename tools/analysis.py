#!/usr/bin/env python
# -*- coding: utf-8 -*-

import os
import sys

def analysis_compile_log():
  filepath = os.path.realpath(sys.argv[1])
  print("Starting analysis of %s" % filepath)

  with open(filepath, "r") as f:
    lines = f.read().splitlines()

  fn = []
  while lines:
    line = lines.pop(0)
    if line.startswith("[") and line.endswith("]"):
      name = line.strip("[]")
      builtin = int(lines.pop(0).split(':')[1].strip())
      cluster = int(lines.pop(0).split(':')[1].strip())
      runtime = int(lines.pop(0).split(':')[1].strip())

      fn.append({
        'name': name, 
        'builtin': builtin,
        'cluster': cluster,
        'runtime': runtime
      })
      
  fn.sort(key=lambda x: x['runtime'] + x['cluster'] * 1.1 + x['builtin'] * 0.1, reverse=True)

  return fn

def analysis_run_log():
  filepath = os.path.realpath(sys.argv[2])
  print("Starting analysis of %s" % filepath)

  with open(filepath, "r") as f:
    lines = f.read().strip().splitlines()

  cnt = []
  for line in lines:
    fn, count = line.split(':')
    cnt.append((fn, int(count)))

  return cnt

if len(sys.argv) < 2:
    print("Usage: %s <file>" % sys.argv[0])
    sys.exit(1)

else:
  fn = analysis_compile_log()

  if len(sys.argv) == 2:
    for i, f in enumerate(fn):
      if f['runtime'] + f['cluster'] <= 2:
        continue
      print(f"{i} {f['name']}: {f['runtime']} {f['cluster']} {f['builtin']}")
  else:
    cnt = analysis_run_log()

    data = []
    for c in cnt:  
      for f in fn:
        if f['name'] == c[0]:
          data.append(
            {
              'name': f['name'],
              'runtime': f['runtime'],
              'cluster': f['cluster'],
              'builtin': f['builtin'],
              'count': c[1],
            }
          )
          break

    def weight(x):
      return x['count'] * (x['runtime'] + x['cluster'] * 1.1 + x['builtin'] * 0.1)

    data.sort(key=weight, reverse=True)
    s = sum(weight(x) for x in data)

    v = 0
    for i, x in enumerate(data):
      v += weight(x) / s * 100
      print(f"{i} {round(weight(x) / s * 100, 2)}% {x['name']}: {x['runtime']} {x['cluster']} {x['builtin']} {x['count']}")
      if v > 99:
        break
