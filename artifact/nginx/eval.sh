#!/bin/bash

set -e

pushd nginx-1.22.1

./$TARGET/sbin/nginx
../wrk/wrk -t8 -c100 -d60s --latency http://localhost:80/index.html | tee /results/$TARGET.txt

popd