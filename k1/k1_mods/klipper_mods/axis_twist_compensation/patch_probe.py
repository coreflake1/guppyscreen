#!/usr/bin/env python3
# Idempotent patcher: connect Klipper's probe to axis_twist_compensation.
#
# By itself, dropping axis_twist_compensation.py into klippy/extras does nothing — Klipper's
# probe code has to call into it during each probe. This script makes that one edit safely:
#   - refuses to run twice (checks for an existing reference),
#   - refuses to run if your firmware's probe.py differs from what's expected (exits "STOP"),
#   - byte-compiles the result so a broken edit can't silently ship.
#
# Usage:  python3 patch_probe.py [/path/to/klippy/extras/probe.py]
# Default path: /usr/share/klipper/klippy/extras/probe.py
#
# This is the same edit documented in the OpenKE wiki (Axis Twist Compensation, Step 2),
# turned into a standalone, re-runnable script for the installer. It is deliberately tolerant:
# a "STOP" result is safe (nothing was changed) and is handled by the installer.

import py_compile
import sys

p = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/klipper/klippy/extras/probe.py'

try:
    s = open(p).read()
except OSError as e:
    sys.exit('STOP: cannot read %s (%s)' % (p, e))

ANCHOR = '        msg = "probe at %.3f,%.3f is z=%.6f" % (epos[0], epos[1], epos[2] - self.z_offset)'

if 'axis_twist_compensation' in s:
    print('Already patched — nothing to change.')
    sys.exit(0)

if s.count(ANCHOR) != 1:
    sys.exit('STOP: probe.py differs from the expected layout; not patching. '
             'Apply the edit by hand (see the OpenKE Axis Twist Compensation wiki page).')

graft = (
    "        # --- axis_twist_compensation (added by OpenKE installer) ---\n"
    "        axis_twist_compensation = self.printer.lookup_object(\n"
    "            'axis_twist_compensation', None)\n"
    "        if axis_twist_compensation is not None:\n"
    "            epos[2] += axis_twist_compensation.get_z_compensation_value(pos)\n"
)

open(p, 'w').write(s.replace(ANCHOR, graft + ANCHOR, 1))
try:
    py_compile.compile(p, doraise=True)
except py_compile.PyCompileError as e:
    sys.exit('STOP: patched probe.py failed to compile (%s). Restore from backup.' % e)

print('Success: probe is now connected to axis twist compensation.')
