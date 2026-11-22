#!/bin/sh
# Nihil test 2b/2c: screen engine. DRIVE=0 -> rehearsal with the panel power
# off (2b). DRIVE=1 -> really draw (2c). Writes only log files in this folder.
DRIVE=1
ROT=1           # page direction on the panel: 1 or 3 = portrait (one of them is upside down), 0 = landscape
MODE=1          # 1 = GC16 (full 16-grey update), 3 = DU (fast black/white)
NAP=1           # 1 = the processor core stands still while Nihil waits for a button, 0 = it runs flat out as before
LIGHT=1         # 1 = Nihil drives the frontlight (menu: Light, Warmth), 0 = it leaves the light chips alone
LOOK=2          # page look: 0 = plain (as it always was), 1 = Kobalt plus, 2 = Classic book, 3 = Bare
IDLE_MIN=30     # minutes without a button press before Nihil hands the Kobo back to its own software (1..1440)
STORE=1         # 1 = Nihil reads the book library straight from the Kobo's storage chip (read-only), 0 = only the one book handed over in RAM
D=/mnt/onboard/.adds/nihil
mkdir /tmp/nihil.lock 2>/dev/null || exit 0     # a double tap must not start two runs
exec > "$D/run.log" 2>&1
date
md5sum "$D/sage.bin" "$D/nihil_chainload.ko"

# Screen power chip: its registers as Linux sets them, sampled for 3 s while
# the screen is still refreshing from the menu tap. Read-only.
insmod "$D/nihil_chainload.ko" pmicdump=1
dmesg | grep "nihil-pm"
for k in vldo vpdd vddh_ext vpdd_len vp_ext vee_ext sy7636_on_delay1 sy7636_on_delay2 sy7636_on_delay3 sy7636_on_delay4; do
  f=$(find /proc/device-tree/ -name "$k" 2>/dev/null | head -1)
  echo "device tree $k: $(hexdump -C "$f" 2>/dev/null | head -1)"
done
dmesg | grep -i -E "vcom|sy7636_regs|papyrus" | grep -v "pwr req" | tail -15

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
# The kernel in this Kobo does not use the device tree's fsl/fbl as they are.
# Ask the running kernel for the numbers it really uses (read-only search).
echo "--- timing really used by the running kernel ---"
insmod "$D/nihil_chainload.ko" findtiming=1 args="$ARGS"0
dmesg | grep "nihil-tm"
# Every copy of the timing found in kernel memory must agree. (The engine
# registers are only a second opinion: Linux clears them after a sleep.)
N=$(dmesg | grep "nihil-tm: at" | sed 's/.*: //' | sort -u | wc -l)
LIVE=$(dmesg | grep "nihil-tm: at" | sed 's/.*: //' | sort -u | head -1 | tr ' ' ',')
echo "distinct timings in kernel memory: $N, first: $LIVE"
if [ "$N" = 1 ] && [ -n "$LIVE" ]; then
  ARGS="$LIVE,"
else
  echo "no single timing found: not starting Nihil"; rmdir /tmp/nihil.lock; sync; exit 1
fi
# Screen power chip settings (regs 1,2,3,5,6) as Linux had them. Only samples
# taken while Linux had the panel rails ON count (register 0 = e0): when the
# chip has just woken up it still shows its factory defaults for a moment.
# Those samples must all agree, otherwise Nihil gets 0,0 and refuses to draw.
PMS=$(dmesg | grep "nihil-pm: t=" | sed 's/.*regs0-8: //' | awk '$1 == "e0" {print $2,$3,$4,$6,$7}' | sort -u)
PMN=$(echo "$PMS" | grep -c .)
set -- $PMS
PM1=0; PM2=0
if [ "$PMN" = 1 ] && [ $# = 5 ] && [ "$1" != 00 ]; then
  PM1=$(( 0x$1 | (0x$2 << 8) | (0x$3 << 16) ))
  PM2=$(( 0x$4 | (0x$5 << 8) ))
fi
# These are fixed factory settings: remember a good reading, and use the
# remembered one when Linux did not have the rails on during this sampling.
if [ "$PM1" != 0 ]; then
  echo "$PM1 $PM2" > "$D/pmic.cfg"
elif [ "$PMN" = 0 ] && [ -s "$D/pmic.cfg" ]; then
  read PM1 PM2 < "$D/pmic.cfg"
  echo "no rails-on sample this time: using remembered settings"
fi
echo "power chip settings: distinct=$PMN -> $PM1,$PM2 (vcom reg=$1 $2)"
# Battery as Kobo's own software sees it right now (read-only). Nihil shows
# this number in its menu; 255 = not found.
echo "--- battery as Linux sees it ---"
BATT=255
for f in /sys/class/power_supply/*/capacity; do
  [ -r "$f" ] || continue
  v=$(cat "$f" 2>/dev/null)
  echo "$f: $v"
  case "$v" in ''|*[!0-9]*) continue;; esac
  case "$f" in */battery/*|*bd71*|*main*) BATT=$v;; *) [ "$BATT" = 255 ] && BATT=$v;; esac
done
for f in /sys/class/power_supply/*/status /sys/class/power_supply/*/voltage_now /sys/class/power_supply/*/current_now /sys/class/power_supply/*/charge_now /sys/class/power_supply/*/charge_full /sys/class/power_supply/*/charge_full_design; do
  [ -r "$f" ] && echo "$f: $(cat $f 2>&1)"
done
echo "battery handed to Nihil: $BATT"
ARGS="${ARGS}${DRIVE},${MODE},0,${PM1},${PM2},${ROT},${STORE},${LIGHT},${NAP},${BATT},${LOOK},${IDLE_MIN}"
echo "args=$ARGS"
grep -o "waveform[^ ]*" /proc/cmdline
dmesg | grep -i -E "wavefile|waveform|eink lib|temperature" | tail -20

# A book handed over in RAM: an EPUB or a plain text file (at most 4 MB). With
# STORE=1 Nihil reads the library from storage itself and this is only the
# fallback if that does not work.
BOOK=""
[ -s "$D/book.txt" ] && BOOK="book=$D/book.txt"
[ -s "$D/book.epub" ] && BOOK="book=$D/book.epub"
echo "book: $BOOK"
echo "--- dry run ---"
insmod "$D/nihil_chainload.ko" image="$D/sage.bin" args="$ARGS" $BOOK dryrun=1
echo "dry run insmod returned $? (expected: non-zero, 'dry run OK' below)"
dmesg | grep -i -E "nihil:|version|taint" | tail -12
if ! dmesg | grep -q "nihil: dry run OK"; then
  echo "DRY RUN FAILED: not starting Nihil"; rmdir /tmp/nihil.lock; sync; exit 1
fi
# Storage facts as Linux sees them (read-only), to compare with what Nihil logs.
echo "--- frontlight as Linux sees it ---"
grep -o "ntxfw[^ ]*" /proc/cmdline
for f in /sys/class/leds/aw99703-bl_FL*/color /sys/class/leds/aw99703-bl_FL*/brightness /sys/class/backlight/mxc_msp430.0/brightness /sys/class/backlight/mxc_msp430.0/actual_brightness; do
  echo "$f: $(cat $f 2>&1)"
done
dmesg | grep "AW99703" | tail -3
grep -n -A6 "aw99703" /mnt/onboard/.adds/koreader/frontend/device/kobo/device.lua 2>/dev/null | grep -E "nl_|mixer" | head -8
echo "--- storage as Linux sees it ---"
cat /proc/partitions
mount | grep -E "onboard|mmcblk"
ls /sys/class/mmc_host/*/ 2>&1
for f in /sys/block/mmcblk0/device/name /sys/block/mmcblk0/device/type /sys/block/mmcblk0/size /sys/block/mmcblk0/device/rev; do
  echo "$f: $(cat $f 2>&1)"
done
for p in /sys/block/mmcblk0/mmcblk0p*; do echo "$p start=$(cat $p/start) size=$(cat $p/size)"; done
mount -t debugfs none /sys/kernel/debug 2>/dev/null
cat /sys/kernel/debug/mmc*/ios 2>&1
echo "--- reading places ---"
# Nihil keeps the places of the books being read in RAM (it cannot write to
# storage). Copy the ones from the last run into positions.txt now, so this
# run opens the last book where it was left.
insmod "$D/nihil_chainload.ko" readlog=1 2>/dev/null
sh "$D/positions.sh"
echo "--- real run ---"
# Ask Linux to hand everything it has already saved over to the storage chip
# for good (the chip keeps recent saves in a short-term memory of its own).
# This opens the storage read-only and stores nothing new.
flush_chip() {
  if fsync /dev/mmcblk0 2>/dev/null; then echo "fsync"
  elif busybox fsync /dev/mmcblk0 2>/dev/null; then echo "busybox fsync"
  elif [ -x /mnt/onboard/.adds/koreader/luajit ] && /mnt/onboard/.adds/koreader/luajit -e 'local ffi=require("ffi"); ffi.cdef("int open(const char*,int,...); int fsync(int); int close(int);"); local fd=ffi.C.open("/dev/mmcblk0",0); assert(fd>=0 and ffi.C.fsync(fd)==0); ffi.C.close(fd)' 2>/dev/null; then echo "luajit fsync"
  else echo "none (sync only)"; fi
}
sync; sleep 1; sync
echo "storage flush tool: $(flush_chip)"
echo "now closing this log, flushing once more and starting Nihil"
# Close this log file before the jump, so no file is left half-written when
# Linux stops (earlier runs left FSCK0000.* fragments behind for that reason).
exec > /dev/null 2>&1
sync
flush_chip
# Make sure the chip's normal storage area is the one selected (not a boot
# area): read its first block for real, past Linux's own cache.
echo 1 > /proc/sys/vm/drop_caches
dd if=/dev/mmcblk0 of=/dev/null bs=512 count=1
sleep 1
insmod "$D/nihil_chainload.ko" image="$D/sage.bin" args="$ARGS" $BOOK
RC=$?
exec >> "$D/run.log" 2>&1
echo "insmod returned $RC (if you can read this, Linux is still running: the test did not start)"
dmesg | tail -40
sync
rmdir /tmp/nihil.lock
