#!/bin/sh
# Nihil test 1, step B: hand the Sage over to sage.bin. Writes only run.log in this folder.
D=/mnt/onboard/.adds/nihil
exec > "$D/run.log" 2>&1
date
md5sum "$D/sage.bin" "$D/nihil_chainload.ko"
sync; sleep 1; sync
insmod "$D/nihil_chainload.ko" image="$D/sage.bin"
echo "insmod returned $? (if you can read this, Linux is still running: the test did not start)"
dmesg | tail -40
sync
