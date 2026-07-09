#!/usr/bin/env python3
# Migrates variable_* values from an old KAMP_Settings.cfg into the new
# Settings.cfg, so upgrading off KAMP doesn't reset anything a user already
# customized (mesh margin, purge amount, dockable-probe macros, etc).
#
# Only touches values for variables that exist in BOTH files - fuzz_amount
# (KAMP-only, dropped - no longer used by anything) is simply not migrated,
# since it has no matching line in the new file to update.
#
# Usage: migrate_settings.py <old_kamp_settings.cfg> <new_settings.cfg>
# If <old_kamp_settings.cfg> doesn't exist, this is a no-op (nothing to
# migrate) - safe to always call from the installer.

import re
import sys

if len(sys.argv) != 3:
    sys.exit("Usage: migrate_settings.py <old_kamp_settings.cfg> <new_settings.cfg>")

old_path, new_path = sys.argv[1], sys.argv[2]

try:
    old_text = open(old_path).read()
except OSError:
    print("No old KAMP settings file at %s - nothing to migrate." % old_path)
    sys.exit(0)

old_values = dict(re.findall(r"^variable_(\w+):\s*(.+?)\s*(?:#.*)?$", old_text, re.MULTILINE))

new_text = open(new_path).read()
migrated = []

def replace(m):
    name, value, comment = m.group(1), m.group(2), m.group(3) or ""
    if name in old_values and old_values[name] != value:
        migrated.append(name)
        return "variable_%s: %s%s" % (name, old_values[name], comment)
    return m.group(0)

new_text = re.sub(r"^variable_(\w+):\s*(.+?)(\s*#.*)?$", replace, new_text, flags=re.MULTILINE)

open(new_path, "w").write(new_text)
if migrated:
    print("Migrated from your existing KAMP settings: %s" % ", ".join(migrated))
else:
    print("Old KAMP settings found, but all values already matched the defaults - nothing to migrate.")
