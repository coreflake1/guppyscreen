#!/bin/sh
# Recurring disk-cleanup for the KE, adapted from pellcorp's tools/cleanup-files.sh
# (pellcorp/creality). Fixes two confirmed-real unbounded-growth gaps:
#   - OpenKE's own BACKUP_DIR (/usr/data/guppyify-backup/): a fresh timestamped
#     .bak on every guarded-config install/upgrade, no rotation (13+
#     guppyconfig.json.*.bak, 12+ printer.cfg.*.bak on a live printer).
#   - Klipper's OWN SAVE_CONFIG mechanism (printer_data/config/printer-*.cfg) -
#     not an OpenKE thing at all, but by far the bigger of the two in
#     practice: 133 files / 1MB on the same live printer, going back to
#     April 2025, completely unrotated. Easy to miss if you only look at
#     BACKUP_DIR, since it's a totally separate directory pellcorp's own
#     script already had a section for - this port originally dropped it
#     by only checking guppyify-backup/, not because it doesn't apply.
#
# Real deltas from pellcorp's original, confirmed against a live KE over SSH
# rather than assumed (not guessed - an earlier pass here wrongly dropped the
# NTP-wait after only checking /dev/rtc* NODE existence, which is NOT the same
# as a working clock; `hwclock` on the real device reads a fixed
# "Sun Mar 1 2020" every time, same stuck-epoch symptom pellcorp's comment
# describes for the K1 - the fix is needed here too):
#   - Backup rotation ALSO targets OUR actual BACKUP_DIR/naming
#     (printer.cfg.*.bak, guppyconfig.json.*.bak), not pellcorp's own
#     backups/ dir with *.override.bkp / backup-*.tar.gz - those specific
#     patterns don't exist on this device at all (his printer-*.cfg rotation,
#     on the other hand, applies verbatim - see above).
#   - /usr/data/tmp (a persistent-storage scratch dir - NOT the OS's own
#     ephemeral tmpfs /tmp, which does exist and is unrelated) does not exist
#     on the KE, so that section of pellcorp's script is dropped.
#   - Log cleanup pattern broadened to also catch dated rotations like
#     klippy.log.2026-07-16 and moonraker.log.2026-07-15 (seen up to 20MB
#     each on a live device) - these do NOT end in ".log" so pellcorp's own
#     plain "*.log" glob silently misses them entirely.
#   - guppyscreen.log replaces grumpyscreen.log in the do-not-delete list
#     (this fork's actual log filename, config.cpp's log_path).
#
# Checked and NOT included: gcode-thumbnail PNGs (guppyscreen/thumbnails/) -
# only 2 files / 28KB on a live device after months of use, not a real
# problem yet. Revisit if that ever changes.

BASEDIR=/usr/data

# get the timestamp before ntp starts - the KE's RTC is stuck at a fixed
# "2020-03-01" epoch (confirmed via `hwclock` on a real device, not the
# system `date`, which already looks correct once NTP has synced it), so on
# a fresh boot this script can otherwise run before that sync finishes.
start_timestamp=$(date +%s)

log() {
    local msg="$1"
    if [ "$client" = "true" ]; then
        echo "$msg" | tee -a $BASEDIR/cleanup.log
    else
        echo "$msg" >> $BASEDIR/cleanup.log
    fi
}

delete() {
    local file="$1"

    if [ "$dryrun" = "true" ]; then
        log "[Dryrun] Deleting $file"
    else
        log "Deleting $file"
        rm "$file"
    fi
}

if [ -f $BASEDIR/cleanup.log ]; then
    rm $BASEDIR/cleanup.log
    sync
fi

client=false
dryrun=false
while true; do
    if [ "$1" = "--dry-run" ]; then
        dryrun=true
        client=true
        shift
    elif [ "$1" = "--client" ]; then
        client=true
        shift
    else # no more parameters
        break
    fi
done

# Same wait pellcorp's script uses for the K1 (this script starts well before
# ntp does): if start_timestamp looks like it was taken before the clock had
# synced (i.e. we're still near that stuck 2020 epoch), wait for a large
# forward jump - anything less than a real NTP correction wouldn't produce a
# 20-minute swing like this.
if [ "$dryrun" != "true" ] || [ "$client" != "true" ]; then
    if [ $start_timestamp -lt 1583064047 ]; then
        log "Waiting for clock to sync..."
        while true; do
            timestamp=$(date +%s)
            drift=$(($timestamp-$start_timestamp))
            if [ $drift -gt 1200 ]; then
                break
            else
                sleep 1
            fi
        done
    fi
fi

# kill pip cache to free up overlayfs
if [ -d /root/.cache ]; then
    rm -rf /root/.cache
    sync
fi

# if there is less than 1GB left, activate deletion of old gcode files
REMAINING_DISK=$(df -m $BASEDIR | tail -1 | awk '{print $4}')
if [ $REMAINING_DISK -lt 1000 ]; then
    log "Performing gcode cleanup"
    files=$(find $BASEDIR/printer_data/gcodes/ -maxdepth 1 -name "*.gcode" -type f -mtime +7 -print)
    for file in $files; do
        delete "$file"
    done
fi
sync

# log files - both the live rotated names (guppyscreen.1.log etc, already
# capped by GuppyScreen's own logger) and dated rotations that don't end in
# ".log" at all (klippy.log.2026-07-16, moonraker.log.2026-07-15) - a plain
# "*.log" glob misses the latter entirely, and those were the biggest single
# consumer found on a real device (up to 20MB per file).
files=$(find $BASEDIR/printer_data/logs/ -maxdepth 1 \( -name "*.log" -o -name "*.log.*" \) -type f -mtime +7 -print)
for file in $files; do
    filename=$(basename "$file")
    # never delete the active logs currently being written to
    if [ "$filename" != "moonraker.log" ] && [ "$filename" != "guppyscreen.log" ] && [ "$filename" != "klippy.log" ]; then
        delete "$file"
    fi
done
sync

# Rotation: keep only the newest of each accumulating pattern, past a 7-day
# grace period (never touches anything less than a week old). Deliberately
# narrow (named patterns only, not a generic glob) so this can never touch
# the one-time named backups (S50dropbear, probe.py.bak, ft2font.*.so,
# printer.cfg.bak-skew, printer.cfg.before-kamp, etc.) that installer.sh (or
# a person, before some risky test) keeps forever for uninstall/restore -
# none of those match a "prefix-then-timestamp" pattern this script targets.
rotate_backups() {
    local dir="$1"
    local pattern="$2"
    local files
    files=$(find "$dir" -maxdepth 1 -name "$pattern" -type f -mtime +7 -print | sort -r)
    local skipped=false
    for file in $files; do
        if [ "$skipped" = "false" ]; then
            skipped=true
        else
            delete "$file"
        fi
    done
}
rotate_backups "$BASEDIR/guppyify-backup/" "printer.cfg.*.bak"
rotate_backups "$BASEDIR/guppyify-backup/" "guppyconfig.json.*.bak"
# installer.sh backs up moonraker.conf with a fresh timestamp at 3 separate
# code paths (Adaptive Print Setup, Exclude Object, Creality macros moonraker
# edits) - same unbounded-growth shape as printer.cfg/guppyconfig above, just
# hadn't accumulated multiple copies yet on the device this was checked
# against because those specific steps hadn't re-run there.
rotate_backups "$BASEDIR/guppyify-backup/" "moonraker.conf.bak-*"
# M600 support gets patched/reinstalled occasionally (see k1_mods/klipper_mods/
# creality_macros), each run drops another dated snapshot of these two files.
rotate_backups "$BASEDIR/guppyify-backup/" "M600-support.cfg.bak-m600-*"
rotate_backups "$BASEDIR/guppyify-backup/" "gcode_macro.cfg.bak-m600-*"
# Klipper's own SAVE_CONFIG backup mechanism - not an OpenKE thing at all, but
# confirmed on a real device to be the single biggest unbounded accumulation
# here by far: 133 files / 1MB going back to April 2025, completely
# unrotated. This is pellcorp's own original "old save and restart config
# files" section - it applies here unchanged, just needed to actually check
# printer_data/config/ instead of assuming guppyify-backup/ was the only
# place backups pile up.
rotate_backups "$BASEDIR/printer_data/config/" "printer-*.cfg"
sync
