#!/bin/bash

set -e

pushd nginx-1.22.1

while true; do
    ./$TARGET/sbin/nginx >/dev/null 2>&1 || true
    if [ -n "$(lsof -i:80)" ]; then
        break
    fi
done

../wrk/wrk -t8 -c100 -d60s --latency http://localhost:80/index.html | tee /results/$TARGET.txt

popd