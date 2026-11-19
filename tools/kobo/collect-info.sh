#!/bin/sh
# Nihil test 1, step A: collect facts from the running Kobo. Reads only; writes only into this folder.
D=/mnt/onboard/.adds/nihil/info
rm -rf "$D"; mkdir -p "$D"
exec > "$D/log.txt" 2>&1
set -x
date
uname -a
cat /proc/version /proc/cmdline
id
[ -e /proc/config.gz ] && zcat /proc/config.gz > "$D/config.txt"
cat /proc/kallsyms > "$D/kallsyms.txt"
lsmod > "$D/lsmod.txt"
cat /proc/iomem > "$D/iomem.txt"
cat /proc/cpuinfo > "$D/cpuinfo.txt"
cat /proc/meminfo > "$D/meminfo.txt"
dmesg > "$D/dmesg.txt"
mkdir -p "$D/modules"
cp /drivers/b300-ntx/*.ko "$D/modules/" 2>/dev/null
find / -xdev -name "*.ko" 2>/dev/null | head -50
ls -la /dev/mem /proc/kcore
# Exported-symbol tables (names + fingerprints) straight from kernel memory.
S=$(awk '$3=="__start___ksymtab"{print $1}' /proc/kallsyms)
E=$(awk '$3=="__stop___ksymtab_strings"{print $1}' /proc/kallsyms)
echo "ksymtab $S .. $E"
if [ -n "$S" ] && [ -n "$E" ]; then
  s=$((0x$S)); e=$((0x$E))
  phys=$((s - 0xC0000000 + 0x40000000))
  blk=$((phys / 4096)); cnt=$(( (e - s + (phys % 4096)) / 4096 + 2 ))
  dd if=/dev/mem of="$D/ksymtab.bin" bs=4096 skip=$blk count=$cnt
  echo "dump_phys_start=$((blk * 4096)) virt_start=$s" > "$D/ksymtab.txt"
fi
for f in /sys/class/backlight/*/brightness /sys/class/backlight/*/actual_brightness; do echo "$f: $(cat $f)"; done
cat /sys/kernel/debug/gpio 2>/dev/null > "$D/gpio.txt"
grep -E "PH|PL" /sys/kernel/debug/sunxi_pinctrl/* 2>/dev/null | head
sync
echo DONE
