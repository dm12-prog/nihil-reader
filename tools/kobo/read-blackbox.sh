#!/bin/sh
# Nihil: read what the last run recorded in RAM. Writes only blackbox.log here.
D=/mnt/onboard/.adds/nihil
exec > "$D/blackbox.log" 2>&1
date
insmod "$D/nihil_chainload.ko" readlog=1
echo "insmod returned $? (non-zero is expected)"
dmesg | grep "nihil-bb"
# battery as Kobo's software sees it now (read-only), to compare with what Nihil recorded
for f in /sys/class/power_supply/*/capacity /sys/class/power_supply/*/status /sys/class/power_supply/*/voltage_now /sys/class/power_supply/*/current_now /sys/class/power_supply/*/charge_now /sys/class/power_supply/*/charge_full; do
  [ -r "$f" ] && echo "$f: $(cat $f 2>&1)"
done
# also keep the reading places of that run (see positions.sh)
sh "$D/positions.sh"
sync
