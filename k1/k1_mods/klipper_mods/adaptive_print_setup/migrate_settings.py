#!/usr/bin/env python3
# Migrates variable_* values from an old settings file into the new Settings.cfg,
# so upgrading doesn't reset anything a user already customized (mesh margin,
# purge amount, dockable-probe macros, etc). Used for two cases: migrating off
# KAMP_Settings.cfg, and preserving values across a plain re-run of the installer
# (the installer overwrites Settings.cfg unconditionally on every run so newly
# added macros - e.g. START_PRINT - always land; this is what stops that from
# also silently resetting anything the user already changed).
#
# Only touches values for variables that exist in BOTH files - a variable removed
# from the new file (e.g. KAMP's old fuzz_amount) is simply not migrated, since it
# has no matching line in the new file to update.
#
# Usage: migrate_settings.py <old_settings.cfg> <new_settings.cfg>
# If <old_settings.cfg> doesn't exist, this is a no-op (nothing to migrate) -
# safe to always call from the installer.

import re
import sys

if len(sys.argv) != 3:
    sys.exit("Usage: migrate_settings.py <old_settings.cfg> <new_settings.cfg>")

old_path, new_path = sys.argv[1], sys.argv[2]

try:
    old_text = open(old_path).read()
except OSError:
    print("No old settings file at %s - nothing to migrate." % old_path)
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
    print("Migrated from your existing settings: %s" % ", ".join(migrated))
else:
    print("Old settings found, but all values already matched the defaults - nothing to migrate.")
