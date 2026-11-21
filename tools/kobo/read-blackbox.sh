#!/bin/sh
# Nihil: read what the last run recorded in RAM. Writes only blackbox.log here.
D=/mnt/onboard/.adds/nihil
exec > "$D/blackbox.log" 2>&1
date
insmod "$D/nihil_chainload.ko" readlog=1
echo "insmod returned $? (non-zero is expected)"
dmesg | grep "nihil-bb"
# also keep the reading places of that run (see positions.sh)
sh "$D/positions.sh"
sync
