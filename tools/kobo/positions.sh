#!/bin/sh
# Nihil reading places: copies them from RAM into the positions file.
#
# Nihil itself never stores anything (its storage driver cannot write). It
# keeps the latest places in RAM, in the first entries of its black box. This
# script runs in Kobo's Linux, right after the black box was printed to the
# kernel log (insmod nihil_chainload.ko readlog=1), and merges those entries
# into .adds/nihil/positions.txt, which Nihil reads at its next start.
#
# Called by run-test1.sh (before Nihil starts) and by read-blackbox.sh.
D=${NIHIL_DIR:-/mnt/onboard/.adds/nihil}
P=$D/positions.txt
# the newest black-box print-out in the kernel log
DUMP=$(dmesg | awk '/nihil-bb: magic=/{b=""} /nihil-bb: r /{b=b $0 "\n"} END{printf "%s", b}' | grep -o 'ef000[0-9a-f][0-9a-f][0-9a-f]=[0-9a-f]*')
[ -n "$DUMP" ] || { echo "positions: nothing in RAM"; exit 0; }
val() { echo "$DUMP" | grep "^$1=" | head -1 | cut -d= -f2; }
[ "$(val ef000000)" = 504f5331 ] || { echo "positions: no valid block in RAM"; exit 0; }
NEW=$D/positions.new
: > "$NEW" || exit 0
# the book read last
LEN=$(val ef000300)
if [ -n "$LEN" ] && [ "$LEN" != 00000000 ]; then
  LINE="L $LEN $(val ef000301)"
  i=0
  while [ $i -lt 60 ]; do
    LINE="$LINE $(val ef0002$(printf %02x $i))"
    i=$((i + 1))
  done
  echo "$LINE" >> "$NEW"
else
  grep '^L ' "$P" 2>/dev/null | head -1 >> "$NEW"
fi
# light settings
L=$(val ef000010); W=$(val ef000011)
[ -n "$L" ] && [ -n "$W" ] && echo "S $L $W" >> "$NEW"
# places: the ones from RAM first, then the older ones for other books
for s in 0 1 2 3; do
  H=$(val ef0001${s}0)
  [ -n "$H" ] && [ "$H" != 00000000 ] || continue
  # check 0 = Nihil was stopped in the middle of changing this entry: keep the older one from the file
  [ "$(val ef0001${s}4)" != 00000000 ] || continue
  echo "P $H $(val ef0001${s}1) $(val ef0001${s}2) $(val ef0001${s}3) $(val ef0001${s}4)" >> "$NEW"
done
grep '^P ' "$P" 2>/dev/null >> "$NEW"
awk '$1 != "P" {print; next} !seen[$2]++' "$NEW" | head -300 > "$P"
rm -f "$NEW"
sync
echo "positions: $(grep -c '^P ' "$P") place(s) saved"
