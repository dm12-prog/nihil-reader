#!/bin/sh
# Nihil test 1b: dry run (logged), then hand the Sage over to sage.bin.
# Writes only run.log / dryrun.log in this folder.
D=/mnt/onboard/.adds/nihil
exec > "$D/run.log" 2>&1
date
md5sum "$D/sage.bin" "$D/nihil_chainload.ko"
echo "--- dry run ---"
insmod "$D/nihil_chainload.ko" image="$D/sage.bin" dryrun=1
echo "dry run insmod returned $? (expected: non-zero, 'dry run OK' below)"
dmesg | grep -i -E "nihil|version|taint" | tail -20
if ! dmesg | grep -q "nihil: dry run OK"; then
  echo "DRY RUN FAILED: not starting Nihil"; sync; exit 1
fi
echo "--- real run ---"
sync; sleep 1; sync
insmod "$D/nihil_chainload.ko" image="$D/sage.bin"
echo "insmod returned $? (if you can read this, Linux is still running: the test did not start)"
dmesg | tail -40
sync
