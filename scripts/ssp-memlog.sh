#!/bin/sh
# Runs on the SSP: logs Synthor's memory, threads and open files once a second until it exits.
# Each line: uptime (s), VmRSS (kB), threads, open files.
#
# usage, from the host:
#   scp -O scripts/ssp-memlog.sh root@192.168.1.6:/tmp/
#   ssh root@192.168.1.6 'nohup sh /tmp/ssp-memlog.sh /tmp/mem.log >/dev/null 2>&1 &'
out="${1:-/tmp/mem.log}"
pid=$(pidof SYNTHOR) || { echo "ssp-memlog: Synthor is not running" >&2; exit 1; }
: > "$out"
while [ -d "/proc/$pid" ]; do
    up=$(cut -d" " -f1 /proc/uptime)
    rss=$(awk '/^VmRSS:/ {print $2}' "/proc/$pid/status")
    threads=$(awk '/^Threads:/ {print $2}' "/proc/$pid/status")
    fds=$(ls "/proc/$pid/fd" | wc -l)
    echo "$up $rss $threads $fds" >> "$out"
    sleep 1
done
