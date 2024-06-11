#!/bin/bash

set -e

## Install SPEC CPU2017
mkdir /root/cpu2017
mount -t iso9660 -o ro,exec,loop /cpu2017/cpu2017.iso /mnt
cd /mnt
echo "yes" | ./install.sh -d /root/cpu2017
cp /root/config/whitelist.txt /root/cpu2017
cp /root/config/native.cfg /root/cpu2017/config
cp /root/config/shadowbound.cfg /root/cpu2017/config
cd /root/cpu2017
umount /mnt

## Set up environment
source shrc
runcpu -c native -a runsetup intrate_no_fortran fprate_no_fortran
runcpu -c shadowbound -a runsetup intrate_no_fortran fprate_no_fortran

## Clean SPEC CPU2017
find /root/cpu2017 -mindepth 1 -maxdepth 1 ! -name 'benchspec' -exec rm -rf {} +
rm /root/cpu2017/benchspec/CPU/*.bset
rm -rf /root/cpu2017/benchspec/CPU/6* /root/cpu2017/benchspec/CPU/9*
rm -rf /root/cpu2017/benchspec/CPU/5*/src /root/cpu2017/benchspec/CPU/5*/Docs /root/cpu2017/benchspec/CPU/5*/Spec

## Run SPEC CPU2017
python3 -u /root/scripts/spectest.py | tee /root/spectest.log

## Interactive shell
/bin/bash