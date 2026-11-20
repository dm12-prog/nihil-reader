#!/bin/sh
# Nihil test 2a: look at the screen hardware (read-only), first from inside
# Linux, then bare-metal. Writes only regdump.log / run.log in this folder.
D=/mnt/onboard/.adds/nihil
exec > "$D/run.log" 2>&1
date
md5sum "$D/sage.bin" "$D/nihil_chainload.ko"
echo "--- register watch from Linux (4 s, read-only) ---"
insmod "$D/nihil_chainload.ko" regdump=1
echo "regdump insmod returned $? (non-zero is expected)"
dmesg | grep "nihil-rd" > "$D/regdump.log"
sync
echo "--- dry run ---"
insmod "$D/nihil_chainload.ko" image="$D/sage.bin" dryrun=1
echo "dry run insmod returned $? (expected: non-zero, 'dry run OK' below)"
dmesg | grep -i -E "nihil:|version|taint" | tail -12
if ! dmesg | grep -q "nihil: dry run OK"; then
  echo "DRY RUN FAILED: not starting Nihil"; sync; exit 1
fi
echo "--- real run ---"
sync; sleep 1; sync
insmod "$D/nihil_chainload.ko" image="$D/sage.bin"
echo "insmod returned $? (if you can read this, Linux is still running: the test did not start)"
dmesg | tail -40
sync
