#!/usr/bin/env python3
# One-time migration: remove stale [bed_mesh adaptive-XXXXXXXX] profile
# stanzas left behind in printer.cfg's own SAVE_CONFIG block by the
# pre-2026-07-18 adaptive-mesh bug (see patch_bed_mesh.py and the
# project_adaptive_mesh_profile_name_regression memory for the full story).
#
# Before that fix, every adaptive print permanently saved its mesh under a
# random, never-reused name ("adaptive-<hex>") - each one an inert, dead
# block that just accumulates in the live config forever (confirmed on a
# real user's printer via Mainsail's Profiles list). The fix stops NEW ones
# from being created; this cleans up ones that already exist. Not something
# the disk-cleanup service (S45cleanup/cleanup-files.sh) can do - that
# rotates standalone backup FILES, this edits content inside the live,
# actively-parsed printer.cfg itself.
#
# Safe by construction: only removes stanzas whose header exactly matches
# the old bug's naming pattern (adaptive-<hex digits>, from Python's
# "%X" % id(self) formatting) - never touches "default" or any other real,
# user-named profile. Run this BEFORE the Klipper service restart that
# follows installation - Klipper only ever re-derives its SAVE_CONFIG block
# from what's currently in memory, so a stale profile still loaded by an
# already-running klippy process would otherwise get rewritten right back
# by the next unrelated SAVE_CONFIG (e.g. saving a Z-offset).
#
# Usage: python3 cleanup_stale_adaptive_profiles.py [/path/to/printer.cfg]
# Default path: /usr/data/printer_data/config/printer.cfg

import re
import sys

p = sys.argv[1] if len(sys.argv) > 1 else '/usr/data/printer_data/config/printer.cfg'

try:
    lines = open(p).readlines()
except OSError as e:
    sys.exit('STOP: cannot read %s (%s)' % (p, e))

HEADER_RE = re.compile(r'^#\*#\s*\[([^\]]+)\]\s*$')
STALE_RE = re.compile(r'^bed_mesh adaptive-[0-9A-Fa-f]+$')

# Every "#*# [section]" header line in the auto-generated SAVE_CONFIG block,
# in file order - a stanza runs from its own header up to (not including)
# the next header, or end of file.
headers = [(i, m.group(1)) for i, l in enumerate(lines)
           for m in (HEADER_RE.match(l),) if m]

if not headers:
    print('No SAVE_CONFIG profile sections found - nothing to do.')
    sys.exit(0)

to_remove = set()
stale_names = []
for idx, (line_no, name) in enumerate(headers):
    if STALE_RE.match(name):
        start = line_no
        end = headers[idx + 1][0] if idx + 1 < len(headers) else len(lines)
        to_remove.update(range(start, end))
        stale_names.append(name)

if not to_remove:
    print('No stale adaptive-* bed_mesh profiles found - nothing to do.')
    sys.exit(0)

new_lines = [l for i, l in enumerate(lines) if i not in to_remove]
open(p, 'w').writelines(new_lines)

print('Removed %d stale adaptive-mesh profile(s): %s' %
      (len(stale_names), ', '.join(stale_names)))
