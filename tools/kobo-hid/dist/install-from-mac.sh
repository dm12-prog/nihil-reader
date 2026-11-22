#!/bin/sh
# Copies the Kobo tablet + trackpad files onto a Kobo mounted in drive mode
# (/Volumes/KOBOeReader), checks them, and replaces the old settings file.
# Usage on the Mac: sh install-from-mac.sh
set -e
V=/Volumes/KOBOeReader
B=https://raw.githubusercontent.com/dm12-prog/nihil-reader/claude/project-thread-da60co/tools/kobo-hid/dist
[ -d "$V/.adds" ] || { echo "Kobo not mounted at $V"; exit 1; }
T=$(mktemp -d)
for f in kobo-hid usb_f_hid.ko pad.desc kobo-hid.sh nm-kobohid config MD5SUMS; do
  curl -fsSL -o "$T/$f" "$B/$f"
done
( cd "$T" && md5 -r kobo-hid usb_f_hid.ko pad.desc kobo-hid.sh nm-kobohid config | diff - MD5SUMS ) || { echo "download checksum mismatch"; exit 1; }
D="$V/.adds/kobohid"
mkdir -p "$D" "$V/.adds/nm"
[ -f "$D/config" ] && cp "$D/config" "$D/config.old"
for f in kobo-hid usb_f_hid.ko pad.desc kobo-hid.sh config; do cp "$T/$f" "$D/$f"; done
cp "$T/nm-kobohid" "$V/.adds/nm/kobohid"
rm -f "$D/combo.desc" "$D/kbd.desc" "$D/ptr.desc"
sync
cd "$D" && md5 -r kobo-hid usb_f_hid.ko pad.desc kobo-hid.sh config
md5 -r "$V/.adds/nm/kobohid"
echo "installed; expected:"; cat "$T/MD5SUMS"
