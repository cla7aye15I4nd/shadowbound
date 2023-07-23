import sys
import json

if len(sys.argv) < 2:
    print("Usage: python fmt-json.py <input_file>")
    sys.exit(1)

input_file = sys.argv[1]
with open(input_file, 'r') as f:
    data = json.load(f)
    data = sorted(data, key=lambda x : json.dumps(x, sort_keys=True))

with open(input_file, 'w') as f:
    json.dump(data, f, indent=2, sort_keys=True)
