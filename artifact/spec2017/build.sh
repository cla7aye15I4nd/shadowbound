#!/bin/bash

set -e

CPU2017_PATH=${CPU2017_PATH:-$HOME/shadowbound/artifact/spec2017/cpu2017.iso}

if [ ! -f $CPU2017_PATH ]; then
    echo "Please set CPU2017_PATH to the path of the SPEC CPU2017 ISO"
    echo "If you do not have the ISO, please use our pre-built image"
    exit 1
fi

CPU2017_DIR=`dirname $CPU2017_PATH`

docker compose up --build spec2017-eval
docker run --privileged -v $CPU2017_DIR:/cpu2017 -it spec2017-eval /root/scripts/start.sh