#!/bin/sh
# Computer side of the Nihil cable link (see tools/kobo/cable-link.sh).
#   nihil-cable.sh wait [seconds]   wait until the Kobo answers on the cable
#   nihil-cable.sh sh "<command>"   run a command on the Kobo
#   nihil-cable.sh push <files...>  copy files into the Kobo's .adds/nihil folder
#   nihil-cable.sh pull <name...>   copy files out of .adds/nihil into ./logs/
#   nihil-cable.sh test             start a Nihil test (the Kobo restarts at the end)
#   nihil-cable.sh blackbox         read what the last test recorded and print it
KEY="${KOBO_KEY:-$HOME/Claude/kobo-ssh/id_ed25519}"
IP="${KOBO_IP:-169.254.42.2}"
D=/mnt/onboard/.adds/nihil
SSH="ssh -i $KEY -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR root@$IP"
case "$1" in
  wait)
    n=${2:-180}
    while [ "$n" -gt 0 ]; do
      $SSH true 2>/dev/null && { echo "kobo is up"; exit 0; }
      sleep 2; n=$((n - 2))
    done
    echo "kobo did not answer"; exit 1;;
  sh) shift; $SSH "$@";;
  push)
    shift
    for f in "$@"; do
      $SSH "cat > $D/$(basename "$f").new && mv $D/$(basename "$f").new $D/$(basename "$f") && md5sum $D/$(basename "$f")" < "$f" || exit 1
    done
    $SSH sync;;
  pull)
    shift; mkdir -p logs
    for f in "$@"; do $SSH "cat $D/$f" > "logs/$f" || exit 1; done;;
  test)
    # detached: the SSH connection dies when Linux is stopped
    $SSH "rm -rf /tmp/nihil.lock; nohup /bin/sh $D/run-test1.sh >/dev/null 2>&1 &" ; echo "test started";;
  blackbox)
    $SSH "/bin/sh $D/read-blackbox.sh; cat $D/blackbox.log; echo ---- run.log; cat $D/run.log";;
  *) sed -n '2,9p' "$0";;
esac
