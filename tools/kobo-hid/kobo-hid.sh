#!/bin/sh
# Mac tablet mode: the Kobo becomes a USB keyboard + pen tablet for the
# computer it is plugged into.
#   page buttons -> Left / Right arrow keys
#   stylus       -> moves the computer's pointer, pen down = click
#   both page buttons, short press -> next pen orientation (saved)
#   both page buttons, hold 1.5 s, or the power button -> stop
#
# How: loads usb_f_hid.ko (built by us for the Kobo's own kernel; nothing in
# the Kobo's system is changed), sets up a USB HID gadget through configfs,
# then runs kobo-hid. Everything is gone after "stop" or a restart.
#
# "kobo-hid.sh stop" tears it down (used by the menu entry and on exit).
D=/mnt/onboard/.adds/kobohid
G=/sys/kernel/config/usb_gadget/kobohid
FB=/mnt/onboard/.adds/koreader/fbink

msg() { [ -x "$FB" ] && "$FB" -q -m -y -6 "$*" >/dev/null 2>&1; echo "msg: $*"; }

teardown() {
  if [ -s /tmp/kobohid.pid ]; then
    P=$(cat /tmp/kobohid.pid)
    [ "$P" != "$$" ] && kill "$P" 2>/dev/null && { echo "stopped kobo-hid ($P)"; sleep 1; }
    rm -f /tmp/kobohid.pid
  fi
  if [ -d "$G" ]; then
    echo "" > "$G/UDC" 2>/dev/null
    rm -f "$G"/configs/c.1/hid.usb0 "$G"/configs/c.1/hid.usb1
    rmdir "$G"/configs/c.1/strings/0x409 2>/dev/null
    rmdir "$G"/configs/c.1 2>/dev/null
    rmdir "$G"/functions/hid.usb0 "$G"/functions/hid.usb1 2>/dev/null
    rmdir "$G"/strings/0x409 2>/dev/null
    rmdir "$G" 2>/dev/null && echo "gadget removed"
  fi
  lsmod | grep -q "^usb_f_hid " && { rmmod usb_f_hid; echo "rmmod usb_f_hid -> $?"; }
}

if [ "$1" = stop ]; then
  exec >> /tmp/kobohid-stop.log 2>&1
  date
  teardown
  exit 0
fi

exec > "$D/start.log" 2>&1
date
if [ -s /tmp/kobohid.pid ] && kill -0 "$(cat /tmp/kobohid.pid)" 2>/dev/null; then echo "already running"; exit 0; fi
if lsmod | grep -q "^g_mass_storage " || ! grep -q " /mnt/onboard " /proc/mounts; then
  echo "drive mode is on: not starting (restart the Kobo first)"; exit 1
fi
# the Nihil cable link uses the same USB port: take it down first
if lsmod | grep -q "^g_ether "; then
  if [ -f /mnt/onboard/.adds/nihil/cable-link.sh ]; then
    /bin/sh /mnt/onboard/.adds/nihil/cable-link.sh stop
  fi
  lsmod | grep -q "^g_ether " && { echo "cable link still loaded: stopping"; exit 1; }
fi
teardown

grep -q " /sys/kernel/config " /proc/mounts || mount -t configfs none /sys/kernel/config
echo "configfs mount -> $?"
insmod "$D/usb_f_hid.ko"; echo "insmod usb_f_hid -> $?"
dmesg | tail -n 5

UDC=$(ls /sys/class/udc 2>/dev/null | head -1)
[ -n "$UDC" ] || { echo "no USB device controller found"; teardown; exit 1; }
echo "udc: $UDC"

mkdir -p "$G" || { echo "mkdir gadget failed"; teardown; exit 1; }
cd "$G" || exit 1
echo 0x1d6b > idVendor     # Linux Foundation
echo 0x0104 > idProduct    # Multifunction Composite Gadget
echo 0x0100 > bcdDevice
echo 0x0200 > bcdUSB
mkdir -p strings/0x409
echo "Kobo" > strings/0x409/manufacturer
echo "Kobo Sage Tablet" > strings/0x409/product
echo "KOBOHID0001" > strings/0x409/serialnumber

# keyboard (boot keyboard, 8-byte reports)
mkdir -p functions/hid.usb0
echo 1 > functions/hid.usb0/protocol
echo 1 > functions/hid.usb0/subclass
echo 8 > functions/hid.usb0/report_length
cat "$D/kbd.desc" > functions/hid.usb0/report_desc
# absolute pointer: 3 buttons + X/Y 0..32767 (5-byte reports)
mkdir -p functions/hid.usb1
echo 0 > functions/hid.usb1/protocol
echo 0 > functions/hid.usb1/subclass
echo 5 > functions/hid.usb1/report_length
cat "$D/ptr.desc" > functions/hid.usb1/report_desc

mkdir -p configs/c.1/strings/0x409
echo "keyboard + tablet" > configs/c.1/strings/0x409/configuration
echo 100 > configs/c.1/MaxPower
ln -s functions/hid.usb0 configs/c.1/
ln -s functions/hid.usb1 configs/c.1/
echo "$UDC" > UDC; echo "bind -> $?"
cd /

# device files: create them if nothing else did
sleep 1
for n in 0 1; do
  if [ ! -c /dev/hidg$n ] && [ -f /sys/class/hidg/hidg$n/dev ]; then
    mm=$(cat /sys/class/hidg/hidg$n/dev)
    mknod /dev/hidg$n c "${mm%%:*}" "${mm##*:}"; echo "mknod hidg$n $mm -> $?"
  fi
done
ls -l /dev/hidg* 2>&1

[ -f "$D/config" ] || printf 'ORIENT=1\nFINGER=0\nKEY_NEXT=0x4f\nKEY_PREV=0x50\n' > "$D/config"
msg "Mac tablet ON. Both buttons: rotate pen. Hold both or power: stop"
sync
"$D/kobo-hid" "$D/kobo-hid.log" &
echo $! > /tmp/kobohid.pid
wait $!
echo "kobo-hid exit -> $?"
rm -f /tmp/kobohid.pid
teardown
msg "Mac tablet OFF"
sync
