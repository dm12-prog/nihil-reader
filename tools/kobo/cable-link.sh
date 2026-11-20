#!/bin/sh
# Nihil cable link: turns the Kobo's USB port into a network link to the
# computer and starts an SSH server on it, so tests can be pushed, started and
# read over the cable without unplugging or tapping through menus.
#
# Changes nothing on the Kobo's storage except its own log file. Everything it
# starts is gone after a restart. While the link is up the Kobo cannot also
# show up as a USB drive.
#
# Kobo address 169.254.42.2, SSH port 2222, key-only login (the key KOReader's
# SSH server already trusts: .adds/koreader/settings/SSH/authorized_keys).
D=/mnt/onboard/.adds/nihil
K=/mnt/onboard/.adds/koreader
exec > "$D/cable-link.log" 2>&1
date
if ifconfig usb0 2>/dev/null | grep -q 169.254.42.2 && [ -s /tmp/dropbear_nihil.pid ] && kill -0 "$(cat /tmp/dropbear_nihil.pid)" 2>/dev/null; then
  echo "link already up"; exit 0
fi
# USB network driver: the modules ship with the Kobo (nothing is installed).
for m in u_ether usb_f_ecm usb_f_ecm_subset usb_f_rndis g_ether; do
  if lsmod | grep -q "^$m "; then echo "$m already loaded"; continue; fi
  f=$(find /drivers -name "$m.ko" 2>/dev/null | head -1)
  [ -n "$f" ] || { echo "MISSING module $m"; continue; }
  if [ "$m" = g_ether ]; then
    # fixed addresses, so the computer sees the same network card every time
    insmod "$f" host_addr=02:4e:49:48:49:01 dev_addr=02:4e:49:48:49:02; echo "insmod $m -> $?"
  else
    insmod "$f"; echo "insmod $m -> $?"
  fi
done
sleep 1
ifconfig usb0 169.254.42.2 netmask 255.255.0.0 up; echo "ifconfig -> $?"
ifconfig usb0
# SSH server: KOReader's own dropbear, started the way its SSH plugin does.
if [ -x "$K/dropbear" ]; then
  cd "$K" || exit 1
  mkdir -p settings/SSH
  ./dropbear -E -R -p 2222 -P /tmp/dropbear_nihil.pid 2>>"$D/cable-link.log"
  echo "dropbear -> $?"
  sleep 1
  [ -s /tmp/dropbear_nihil.pid ] && echo "ssh server running, pid $(cat /tmp/dropbear_nihil.pid)"
else
  echo "MISSING $K/dropbear (KOReader not installed?)"
fi
sync
