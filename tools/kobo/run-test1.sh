#!/bin/sh
# Nihil test 2b/2c: screen engine. DRIVE=0 -> rehearsal with the panel power
# off (2b). DRIVE=1 -> really draw (2c). Writes only log files in this folder.
DRIVE=0
MODE=1          # 1 = GC16 (full 16-grey update), 3 = DU (fast black/white)
D=/mnt/onboard/.adds/nihil
mkdir /tmp/nihil.lock 2>/dev/null || exit 0     # a double tap must not start two runs
exec > "$D/run.log" 2>&1
date
md5sum "$D/sage.bin" "$D/nihil_chainload.ko"

# Panel timing from the live device tree (big-endian 32-bit numbers).
dt() {
  # /proc/device-tree is a symbolic link: the trailing slash makes find go inside it
  f=$(find /proc/device-tree/ /sys/firmware/devicetree/base/ -name "$1" 2>/dev/null | head -1)
  [ -n "$f" ] || { echo ""; return; }
  set -- $(hexdump -v -e '4/1 "%u "' "$f" 2>/dev/null)
  [ $# -ge 4 ] || set -- $(od -An -tu1 "$f" 2>/dev/null)
  [ $# -ge 4 ] || { echo ""; return; }
  echo $(( ($1 << 24) | ($2 << 16) | ($3 << 8) | $4 ))
}
ls -ld /proc/device-tree /sys/firmware/devicetree/base 2>&1
find /proc/device-tree/ -name "eink_*" 2>&1 | head -30
ARGS=""
for k in eink_lsl eink_lbl eink_ldl eink_lel eink_gdck_sta eink_lgonl eink_fsl eink_fbl eink_fdl eink_fel; do
  v=$(dt $k)
  echo "device tree $k = '$v'"
  case "$v" in ''|*[!0-9]*) echo "MISSING $k: not starting Nihil"; rmdir /tmp/nihil.lock; sync; exit 1;; esac
  ARGS="$ARGS$v,"
done
for k in eink_width eink_height eink_bits eink_mode eink_fresh_hz lcd_dclk_freq lcd_ht lcd_vt lcd_hbp lcd_vbp lcd_hspw lcd_vspw; do
  echo "device tree $k = '$(dt $k)'"
done
ARGS="${ARGS}${DRIVE},${MODE},0"
echo "args=$ARGS"
grep -o "waveform[^ ]*" /proc/cmdline
dmesg | grep -i -E "wavefile|waveform|eink lib|temperature" | tail -20

echo "--- dry run ---"
insmod "$D/nihil_chainload.ko" image="$D/sage.bin" args="$ARGS" dryrun=1
echo "dry run insmod returned $? (expected: non-zero, 'dry run OK' below)"
dmesg | grep -i -E "nihil:|version|taint" | tail -12
if ! dmesg | grep -q "nihil: dry run OK"; then
  echo "DRY RUN FAILED: not starting Nihil"; rmdir /tmp/nihil.lock; sync; exit 1
fi
echo "--- real run ---"
sync; sleep 1; sync
insmod "$D/nihil_chainload.ko" image="$D/sage.bin" args="$ARGS"
echo "insmod returned $? (if you can read this, Linux is still running: the test did not start)"
dmesg | tail -40
sync
rmdir /tmp/nihil.lock
