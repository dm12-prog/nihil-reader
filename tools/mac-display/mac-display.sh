#!/bin/sh
# Mac display mode: the Kobo shows one of the Mac's screens (a virtual
# "extended display") over Wi-Fi, using einkvnc (github.com/ivy-rew/eink-vnc,
# v0.2.1, built with Kobo Sage support).
#
# Settings: display.cfg next to this script (HOST = the Mac's address).
# While it runs, the page buttons and touch screen are held away from the
# Kobo's own software (kobo-hid watch). Stop: power button, or hold both page
# buttons 1.5 s, or hold a finger (not the pen) in any screen corner for 2 s.
# "mac-display.sh stop" also stops it.
D=/mnt/onboard/.adds/macdisplay
FB=/mnt/onboard/.adds/koreader/fbink
msg() { [ -x "$FB" ] && "$FB" -q -m -y -6 "$*" >/dev/null 2>&1; echo "msg: $*"; }

resume_nickel() {
  if [ -f /tmp/macdisplay-nickel-paused ]; then
    killall -CONT nickel 2>/dev/null && echo "nickel resumed"
    rm -f /tmp/macdisplay-nickel-paused
  fi
}

stop_all() {
  for f in /tmp/macdisplay-vnc.pid /tmp/macdisplay-watch.pid; do
    [ -s "$f" ] && kill "$(cat "$f")" 2>/dev/null
    rm -f "$f"
  done
}

if [ "$1" = stop ]; then
  stop_all
  resume_nickel
  exit 0
fi

exec > "$D/start.log" 2>&1
date
if [ -s /tmp/macdisplay-vnc.pid ] && kill -0 "$(cat /tmp/macdisplay-vnc.pid)" 2>/dev/null; then
  echo "already running"; exit 0
fi
HOST=""; PORT=5901; PASSWORD=""; ROTATE=""; CONTRAST=2; TOUCH=1
[ -f "$D/display.cfg" ] && . "$D/display.cfg"
if [ -z "$HOST" ]; then
  msg "Mac display: put the Mac's address in .adds/macdisplay/display.cfg"
  exit 1
fi
ifconfig wlan0 2>/dev/null | grep -q "inet addr" || echo "warning: Wi-Fi seems off"
ifconfig wlan0 2>/dev/null | grep "inet addr"

# pen/touch -> Mac mouse: kobo-hid holds the real touch screen away from the
# Kobo's software and copies it to a virtual device that only einkvnc reads
rm -f /tmp/macdisplay-touchdev
if [ "$TOUCH" = 1 ]; then
  "$D/kobo-hid" "$D/watch.log" relay /tmp/macdisplay-touchdev &
  echo $! > /tmp/macdisplay-watch.pid
  for i in 1 2 3 4 5 6 7 8 9 10; do [ -s /tmp/macdisplay-touchdev ] && break; usleep 300000; done
  TDEV=$(cat /tmp/macdisplay-touchdev 2>/dev/null)
  echo "touch relay device: ${TDEV:-none}"
  if [ -n "$TDEV" ] && [ ! -c "$TDEV" ]; then
    n=${TDEV##*/}; mm=$(cat /sys/class/input/$n/dev 2>/dev/null)
    [ -n "$mm" ] && mknod "$TDEV" c "${mm%%:*}" "${mm##*:}" && echo "mknod $TDEV $mm"
  fi
fi
if [ "$TOUCH" = 1 ] && [ -c "$TDEV" ]; then
  set -- "$HOST" --port "$PORT" --contrast "$CONTRAST" --touch "$TDEV"
else
  [ "$TOUCH" = 1 ] && echo "touch relay failed: view only"
  set -- "$HOST" --port "$PORT" --contrast "$CONTRAST" --viewonly true
fi
[ -n "$PASSWORD" ] && set -- "$@" --password "$PASSWORD"
[ -n "$ROTATE" ] && set -- "$@" --rotate "$ROTATE"
echo "einkvnc $HOST port $PORT rotate ${ROTATE:-default} contrast $CONTRAST"

# einkvnc picks the screen driver from these (same as Plato's launcher)
PRODUCT=$(/bin/kobo_config.sh 2>/dev/null); export PRODUCT
MODEL_NUMBER=$(cut -f 6 -d ',' /mnt/onboard/.kobo/version | sed -e 's/^[0-]*//'); export MODEL_NUMBER
echo "PRODUCT=$PRODUCT MODEL_NUMBER=$MODEL_NUMBER"

msg "Mac display: connecting to $HOST:$PORT ... Stop: hold a finger in a corner 2 s"
RUST_LOG=info "$D/einkvnc" "$@" > "$D/einkvnc.log" 2>&1 &
echo $! > /tmp/macdisplay-vnc.pid
# pause the Kobo's own software so its screen updates (clock, battery, Wi-Fi
# icons) don't draw over the Mac picture; resumed when the display stops.
# Its touch input is held by kobo-hid, so no taps pile up meanwhile.
sleep 3
killall -STOP nickel 2>/dev/null && echo "nickel paused" && touch /tmp/macdisplay-nickel-paused
if ! { [ -s /tmp/macdisplay-watch.pid ] && kill -0 "$(cat /tmp/macdisplay-watch.pid)" 2>/dev/null; }; then
  "$D/kobo-hid" "$D/watch.log" watch &
  echo $! > /tmp/macdisplay-watch.pid
fi

# run until either one ends (viewer lost the connection, or stop pressed)
while kill -0 "$(cat /tmp/macdisplay-vnc.pid 2>/dev/null)" 2>/dev/null &&
      kill -0 "$(cat /tmp/macdisplay-watch.pid 2>/dev/null)" 2>/dev/null; do
  sleep 1
done
kill -0 "$(cat /tmp/macdisplay-vnc.pid 2>/dev/null)" 2>/dev/null || echo "viewer ended first"
stop_all
resume_nickel
tail -n 20 "$D/einkvnc.log"
# repaint the Kobo's own screen (einkvnc drew from its own buffer)
[ -x "$FB" ] && "$FB" -q -s -f >/dev/null 2>&1
msg "Mac display OFF"
sync
