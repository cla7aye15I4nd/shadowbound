#!/usr/bin/env python
# -*- coding: utf-8 -*-

import os
import sys

if len(sys.argv) < 2:
    print("Usage: %s <file>" % sys.argv[0])
    sys.exit(1)

else:
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
  for i, f in enumerate(fn):
    if f['runtime'] + f['cluster'] <= 2:
      continue
    print(f"{i} {f['name']}: {f['runtime']} {f['cluster']} {f['builtin']}")
