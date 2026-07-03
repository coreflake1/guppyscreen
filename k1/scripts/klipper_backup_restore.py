#!/usr/bin/env python3

# OpenKE — self-contained Klipper config / Moonraker database backup & restore.
#
# Replaces a dependency on the separate Creality Helper Script's useful_macros.sh
# (most OpenKE-only installs don't have it — those macros existed on-screen but
# silently failed for anyone without a separate Helper Script install). Same
# backup/restore semantics as upstream (compress on backup, delete-then-extract
# on restore), just stored in OpenKE's own backup dir instead of inside the
# directory being backed up.

import argparse
import os
import shutil
import sys
import tarfile

PRINTER_DATA_DIR = "/usr/data/printer_data"
BACKUP_DIR = "/usr/data/guppyify-backup"

TARGETS = {
    "klipper": ("config", os.path.join(BACKUP_DIR, "backup_config.tar.gz")),
    "moonraker": ("database", os.path.join(BACKUP_DIR, "backup_database.tar.gz")),
}


def backup(target):
    subdir, archive_path = TARGETS[target]
    source = os.path.join(PRINTER_DATA_DIR, subdir)
    if not os.path.isdir(source):
        print(f"Nothing to back up — {source} does not exist")
        sys.exit(1)

    os.makedirs(BACKUP_DIR, exist_ok=True)
    with tarfile.open(archive_path, "w:gz") as tar:
        tar.add(source, arcname=subdir)
    print(f"Backed up {source} to {archive_path}")


def restore(target):
    subdir, archive_path = TARGETS[target]
    if not os.path.isfile(archive_path):
        print(f"No backup found at {archive_path}")
        sys.exit(1)

    dest = os.path.join(PRINTER_DATA_DIR, subdir)
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    with tarfile.open(archive_path, "r:gz") as tar:
        tar.extractall(PRINTER_DATA_DIR)
    print(f"Restored {dest} from {archive_path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "action",
        choices=["-backup_klipper", "-restore_klipper", "-backup_moonraker", "-restore_moonraker"],
    )
    args = parser.parse_args()

    target = "klipper" if "klipper" in args.action else "moonraker"
    if "backup" in args.action:
        backup(target)
    else:
        restore(target)


if __name__ == "__main__":
    main()
