#!/bin/sh
# Mac display mode: the Kobo shows one of the Mac's screens (a virtual
# "extended display") over Wi-Fi, using einkvnc (github.com/ivy-rew/eink-vnc,
# v0.2.1, built with Kobo Sage support).
#
# Settings: display.cfg next to this script (HOST = the Mac's address).
# While it runs, the page buttons and touch screen are held away from the
# Kobo's own software (kobo-hid watch). Stop: power button, or hold both page
# buttons 1.5 s. "mac-display.sh stop" also stops it.
D=/mnt/onboard/.adds/macdisplay
FB=/mnt/onboard/.adds/koreader/fbink
msg() { [ -x "$FB" ] && "$FB" -q -m -y -6 "$*" >/dev/null 2>&1; echo "msg: $*"; }

stop_all() {
  for f in /tmp/macdisplay-vnc.pid /tmp/macdisplay-watch.pid; do
    [ -s "$f" ] && kill "$(cat "$f")" 2>/dev/null
    rm -f "$f"
  done
}

if [ "$1" = stop ]; then
  stop_all
  exit 0
fi

exec > "$D/start.log" 2>&1
date
if [ -s /tmp/macdisplay-vnc.pid ] && kill -0 "$(cat /tmp/macdisplay-vnc.pid)" 2>/dev/null; then
  echo "already running"; exit 0
fi
HOST=""; PORT=5901; PASSWORD=""; ROTATE=""; CONTRAST=2
[ -f "$D/display.cfg" ] && . "$D/display.cfg"
if [ -z "$HOST" ]; then
  msg "Mac display: put the Mac's address in .adds/macdisplay/display.cfg"
  exit 1
fi
ifconfig wlan0 2>/dev/null | grep -q "inet addr" || echo "warning: Wi-Fi seems off"
ifconfig wlan0 2>/dev/null | grep "inet addr"

set -- "$HOST" --port "$PORT" --contrast "$CONTRAST" --viewonly true
[ -n "$PASSWORD" ] && set -- "$@" --password "$PASSWORD"
[ -n "$ROTATE" ] && set -- "$@" --rotate "$ROTATE"
echo "einkvnc $HOST port $PORT rotate ${ROTATE:-default} contrast $CONTRAST"

# einkvnc picks the screen driver from these (same as Plato's launcher)
PRODUCT=$(/bin/kobo_config.sh 2>/dev/null); export PRODUCT
MODEL_NUMBER=$(cut -f 6 -d ',' /mnt/onboard/.kobo/version | sed -e 's/^[0-]*//'); export MODEL_NUMBER
echo "PRODUCT=$PRODUCT MODEL_NUMBER=$MODEL_NUMBER"

msg "Mac display: connecting to $HOST:$PORT ..."
RUST_LOG=info "$D/einkvnc" "$@" > "$D/einkvnc.log" 2>&1 &
echo $! > /tmp/macdisplay-vnc.pid
"$D/kobo-hid" "$D/watch.log" watch &
echo $! > /tmp/macdisplay-watch.pid

# run until either one ends (viewer lost the connection, or stop pressed)
while kill -0 "$(cat /tmp/macdisplay-vnc.pid 2>/dev/null)" 2>/dev/null &&
      kill -0 "$(cat /tmp/macdisplay-watch.pid 2>/dev/null)" 2>/dev/null; do
  sleep 1
done
kill -0 "$(cat /tmp/macdisplay-vnc.pid 2>/dev/null)" 2>/dev/null || echo "viewer ended first"
stop_all
tail -n 20 "$D/einkvnc.log"
# repaint the Kobo's own screen (einkvnc drew from its own buffer)
[ -x "$FB" ] && "$FB" -q -s -f >/dev/null 2>&1
msg "Mac display OFF"
sync
