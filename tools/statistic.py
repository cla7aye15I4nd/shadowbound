import os
import sys

if len(sys.argv) != 2:
	print("Usage: python statistic.py <path>")
	sys.exit(1)
 
path = sys.argv[1]
statistic = {}
with open(path) as f:
	prog = None
	for line in f.readlines():
		if line.strip().startswith("Building"):
			prog = line.strip().split()[1].split(".")[1]
			statistic[prog] = [0, 0]
   
		if line.strip().startswith("Fetch Instrument:"):
			statistic[prog][0] += int(line.strip().split()[2])
		if line.strip().startswith("Check Instrument:"):
			statistic[prog][1] += int(line.strip().split()[2])
   
for prog in statistic:
	if 'spec' in prog:
		continue
	
	print(f"& {prog[:-2]:12} & & & & & {statistic[prog][0]:6} & {statistic[prog][1]:6} \\\\")
 
# & perlbench    & & & & &   6377 &   7909 \\
# & gcc          & & & & &  13571 &  17516 \\
# & mcf          & & & & &     49 &     99 \\
# & omnetpp      & & & & &   2590 &   3015 \\
# & xalancbmk    & & & & &  10449 &  12589 \\
# & x264         & & & & &   4759 &   6677 \\
# & deepsjeng    & & & & &      9 &     17 \\
# & leela        & & & & &    113 &    132 \\
# & xz           & & & & &    279 &    631 \\
# & namd         & & & & &   1274 &   4088 \\
# & parest       & & & & &  33368 &  39771 \\
# & povray       & & & & &   2111 &   2672 \\
# & lbm          & & & & &     10 &    244 \\
# & blender      & & & & &  15479 &  22445 \\
# & imagick      & & & & &   4237 &   5374 \\
# & nab          & & & & &    780 &   1214 \\