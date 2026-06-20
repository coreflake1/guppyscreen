#!/bin/sh
# guppycam-freezelog.sh - forensic logger for the full-system freeze.
# Writes to /usr/data (eMMC, SURVIVES reboot) every 1s with sync, so the last
# record before a hard freeze is recoverable after a power-cycle - no dependence
# on the network surviving. Captures the memory/CPU trend + a fresh full dmesg
# (which would include an OOM-killer report) right up to the freeze.
LOG=/usr/data/guppycam-freeze.log
DM=/usr/data/guppycam-freeze-dmesg.txt
echo "freezelog start uptime=$(cut -d. -f1 /proc/uptime)s" > "$LOG"
while :; do
  U=$(cut -d. -f1 /proc/uptime)
  MEM=$(free -m | awk '/Mem:/{printf "avail=%s used=%s free=%s buf=%s", $7,$3,$4,$6}')
  SW=$(free -m | awk '/Swap:/{printf "swap=%s/%s", $3,$2}')
  GP=$(pidof guppycam)
  GR=""; [ -n "$GP" ] && GR=$(awk '/VmRSS/{print $2"KB"}' /proc/$GP/status 2>/dev/null)
  TOP=$(top -bn1 2>/dev/null | grep -vE 'CPU:|Mem:|Load|PID|top ' | head -3 | awk '{print $NF":"$7}' | tr '\n' ' ')
  printf 'u=%ss %s %s gup=[%s]%s | top: %s\n' "$U" "$MEM" "$SW" "$GP" "$GR" "$TOP" >> "$LOG"
  dmesg > "$DM" 2>/dev/null
  sync
  sleep 1
done
