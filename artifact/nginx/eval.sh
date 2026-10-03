#!/bin/bash
set -euo pipefail

: "${TARGET:?set TARGET to one of native|shadowbound|shadowbound-ffmalloc|shadowbound-markus}"
: "${DURATION:=60s}"

pushd nginx-1.22.1 >/dev/null

# Start nginx and wait (bounded) until it is listening, instead of a busy loop
# that spins forever if the binary crashes on startup.
./"$TARGET"/sbin/nginx
for _ in $(seq 1 30); do
    if lsof -i:80 >/dev/null 2>&1; then
        break
    fi
    sleep 0.5
done
if ! lsof -i:80 >/dev/null 2>&1; then
    echo "ERROR: nginx ($TARGET) did not start listening on :80" >&2
    exit 1
fi

../wrk/wrk -t8 -c100 -d"$DURATION" --latency http://localhost:80/index.html \
    | tee /results/"$TARGET".txt

./"$TARGET"/sbin/nginx -s stop || true
popd >/dev/null
