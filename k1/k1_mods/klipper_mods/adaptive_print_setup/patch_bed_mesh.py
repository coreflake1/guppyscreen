#!/usr/bin/env python3
# Idempotent patcher: add Klipper's native adaptive-mesh support
# (BED_MESH_CALIBRATE ADAPTIVE=1 / ADAPTIVE_MARGIN=) to the KE's bed_mesh.py.
#
# The KE's Creality-forked Klipper predates upstream's adaptive-mesh merge - its
# bed_mesh.py has none of it at all (verified: zero matches for "adaptive", any
# case, in the shipped file). This ports just that feature as a self-contained
# method (set_adaptive_mesh, lifted near-verbatim from upstream Klipper's current
# bed_mesh.py) plus one config option and one call site - NOT a wholesale file
# replacement, which would be too risky here: upstream has drifted ~450 lines
# beyond just this feature, with unknown interaction with Creality's own
# probe/z_compensate customizations, and this is core bed-leveling code.
#
# Purely additive: without ADAPTIVE=1 on the command line, need_cfg_update is
# unaffected and every existing BED_MESH_CALIBRATE call (including any
# already-saved mesh profile) behaves exactly as before.
#
# Usage:  python3 patch_bed_mesh.py [/path/to/klippy/extras/bed_mesh.py]
# Default path: /usr/share/klipper/klippy/extras/bed_mesh.py

import py_compile
import sys

p = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/klipper/klippy/extras/bed_mesh.py'

try:
    s = open(p).read()
except OSError as e:
    sys.exit('STOP: cannot read %s (%s)' % (p, e))

# Line 871 of the on-device bed_mesh.py, confirmed via SSH against a real
# printer. Mainline Klipper guards this exact call with
# "if self._profile_name is not None:" - this fork never got that guard
# because it predates upstream's adaptive-mesh feature (which is the only
# thing that ever sets _profile_name to None) entirely.
UNGUARDED_SAVE = ('        self.gcode.respond_info("Mesh Bed Leveling Complete")\n'
                   '        self.bedmesh.save_profile(self._profile_name)\n')
GUARDED_SAVE = ('        self.gcode.respond_info("Mesh Bed Leveling Complete")\n'
                '        # Matches mainline Klipper\'s own None-guard here (this fork\'s\n'
                '        # probe_finalize predates upstream\'s adaptive-mesh feature and\n'
                '        # never needed one before): an adaptive mesh\'s _profile_name is\n'
                '        # None (see set_adaptive_mesh below) because it\'s print-specific -\n'
                '        # it\'s used live for this print but was never meant to be\n'
                '        # persisted/reloadable by name.\n'
                '        if self._profile_name is not None:\n'
                '            self.bedmesh.save_profile(self._profile_name)\n')

# The pre-fix workaround: real user-reported regression (2026-07-18, community
# feedback via a Discord user testing a custom start-gcode sequence) - see
# project_adaptive_mesh_profile_name_regression memory for the full story.
# Giving the adaptive mesh a random synthetic name instead of skipping the
# save entirely (as mainline intends) meant any leftover "BED_MESH_PROFILE
# LOAD=default" habit (harmless under the old KAMP macros, which really did
# always save under "default") now silently discards the fresh adaptive mesh
# and reloads a stale full-bed one instead - with no error or warning.
OLD_BUGGY_LINE = '        self._profile_name = "adaptive-%X" % (id(self),)\n'
NEW_LINE = '        self._profile_name = None\n'

if 'set_adaptive_mesh' in s:
    # Already has the adaptive-mesh method - but that doesn't mean it's the
    # current, fixed version. Upgrade in place rather than silently leaving a
    # known-buggy printer on the old behavior forever (the old blanket
    # "already patched, nothing to do" exit would do exactly that).
    upgraded = False
    if OLD_BUGGY_LINE in s:
        s = s.replace(OLD_BUGGY_LINE, NEW_LINE, 1)
        upgraded = True
    if UNGUARDED_SAVE in s:
        s = s.replace(UNGUARDED_SAVE, GUARDED_SAVE, 1)
        upgraded = True

    if not upgraded:
        print('Already patched (current version) — nothing to change.')
        sys.exit(0)

    open(p, 'w').write(s)
    try:
        py_compile.compile(p, doraise=True)
    except py_compile.PyCompileError as e:
        sys.exit('STOP: patched bed_mesh.py failed to compile after upgrade (%s). '
                  'Restore from backup.' % e)
    print('Upgraded: adaptive mesh is no longer saved under a throwaway profile name '
          '(matches mainline Klipper - it stays active for the print but is not '
          'persisted/reloadable by name).')
    sys.exit(0)

# 1. adaptive_margin config option, added at the end of _init_mesh_config.
ANCHOR_1 = "        self._verify_algorithm(config.error)\n"
if s.count(ANCHOR_1) != 1:
    sys.exit('STOP: bed_mesh.py differs from the expected layout (anchor 1); not patching. '
             'Apply the edit by hand (see the OpenKE wiki).')
graft_1 = ANCHOR_1 + "        self.adaptive_margin = config.getfloat('adaptive_margin', 0.)\n"
s = s.replace(ANCHOR_1, graft_1, 1)

# 2. set_adaptive_mesh method, added right before update_config.
ANCHOR_2 = "    def update_config(self, gcmd):\n"
if s.count(ANCHOR_2) != 1:
    sys.exit('STOP: bed_mesh.py differs from the expected layout (anchor 2); not patching. '
             'Apply the edit by hand (see the OpenKE wiki).')
graft_2 = '''    def set_adaptive_mesh(self, gcmd):
        # Ported from upstream Klipper's bed_mesh.py (added by OpenKE installer).
        if not gcmd.get_int('ADAPTIVE', 0):
            return False
        exclude_objects = self.printer.lookup_object("exclude_object", None)
        if exclude_objects is None:
            gcmd.respond_info("Exclude objects not enabled. Using full mesh...")
            return False
        objects = exclude_objects.get_status().get("objects", [])
        if not objects:
            return False
        margin = gcmd.get_float('ADAPTIVE_MARGIN', self.adaptive_margin)

        list_of_xs = []
        list_of_ys = []
        gcmd.respond_info("Found %s objects" % (len(objects)))
        for obj in objects:
            for point in obj["polygon"]:
                list_of_xs.append(point[0])
                list_of_ys.append(point[1])

        mesh_min = [min(list_of_xs), min(list_of_ys)]
        mesh_max = [max(list_of_xs), max(list_of_ys)]
        adjusted_mesh_min = [x - margin for x in mesh_min]
        adjusted_mesh_max = [x + margin for x in mesh_max]

        adjusted_mesh_min[0] = max(adjusted_mesh_min[0],
                                   self.orig_config["mesh_min"][0])
        adjusted_mesh_min[1] = max(adjusted_mesh_min[1],
                                   self.orig_config["mesh_min"][1])
        adjusted_mesh_max[0] = min(adjusted_mesh_max[0],
                                   self.orig_config["mesh_max"][0])
        adjusted_mesh_max[1] = min(adjusted_mesh_max[1],
                                   self.orig_config["mesh_max"][1])

        adjusted_mesh_size = (adjusted_mesh_max[0] - adjusted_mesh_min[0],
                              adjusted_mesh_max[1] - adjusted_mesh_min[1])

        ratio = (adjusted_mesh_size[0] /
                 (self.orig_config["mesh_max"][0] -
                  self.orig_config["mesh_min"][0]),
                 adjusted_mesh_size[1] /
                 (self.orig_config["mesh_max"][1] -
                  self.orig_config["mesh_min"][1]))

        gcmd.respond_info("Original mesh bounds: (%s,%s)" %
                          (self.orig_config["mesh_min"],
                           self.orig_config["mesh_max"]))
        gcmd.respond_info("Original probe count: (%s,%s)" %
                          (self.mesh_config["x_count"],
                           self.mesh_config["y_count"]))
        gcmd.respond_info("Adapted mesh bounds: (%s,%s)" %
                          (adjusted_mesh_min, adjusted_mesh_max))
        gcmd.respond_info("Ratio: (%s, %s)" % ratio)

        new_x_probe_count = int(
            math.ceil(self.mesh_config["x_count"] * ratio[0]))
        new_y_probe_count = int(
            math.ceil(self.mesh_config["y_count"] * ratio[1]))

        min_num_of_probes = 3
        if max(new_x_probe_count, new_y_probe_count) > 6 and \\
           min(new_x_probe_count, new_y_probe_count) < 4:
            min_num_of_probes = 4

        new_x_probe_count = max(min_num_of_probes, new_x_probe_count)
        new_y_probe_count = max(min_num_of_probes, new_y_probe_count)

        gcmd.respond_info("Adapted probe count: (%s,%s)" %
                          (new_x_probe_count, new_y_probe_count))

        adjusted_mesh_size = (max(adjusted_mesh_size[0], new_x_probe_count),
                              max(adjusted_mesh_size[1], new_y_probe_count))

        if self.radius is not None:
            adapted_radius = math.sqrt((adjusted_mesh_size[0] ** 2) +
                                       (adjusted_mesh_size[1] ** 2)) / 2
            adapted_origin = (adjusted_mesh_min[0] +
                              (adjusted_mesh_size[0] / 2),
                              adjusted_mesh_min[1] +
                              (adjusted_mesh_size[1] / 2))
            to_adapted_origin = math.sqrt(adapted_origin[0]**2 +
                                          adapted_origin[1]**2)
            if adapted_radius + to_adapted_origin < self.radius:
                self.radius = adapted_radius
                self.origin = adapted_origin
                self.mesh_min = (-self.radius, -self.radius)
                self.mesh_max = (self.radius, self.radius)
                new_probe_count = max(new_x_probe_count, new_y_probe_count)
                new_probe_count += 1 - (new_probe_count % 2)
                self.mesh_config["x_count"] = self.mesh_config["y_count"] = \\
                        new_probe_count
        else:
            self.mesh_min = adjusted_mesh_min
            self.mesh_max = adjusted_mesh_max
            self.mesh_config["x_count"] = new_x_probe_count
            self.mesh_config["y_count"] = new_y_probe_count
        # Matches upstream Klipper exactly: an adaptive mesh is print-specific
        # (its bounds depend on THIS print's objects), so it's never persisted
        # as a named, reloadable profile - just used live for the print that
        # calibrated it. probe_finalize() below is patched with the matching
        # "if self._profile_name is not None" guard, same as mainline, so this
        # None never reaches save_profile()/crashes.
        self._profile_name = None
        return True
'''
s = s.replace(ANCHOR_2, graft_2 + ANCHOR_2, 1)

# 3. Call set_adaptive_mesh from update_config, right after the ALGORITHM param block.
ANCHOR_3 = ("        if \"ALGORITHM\" in params:\n"
            "            self.mesh_config['algo'] = gcmd.get('ALGORITHM').strip().lower()\n"
            "            need_cfg_update = True\n")
if s.count(ANCHOR_3) != 1:
    sys.exit('STOP: bed_mesh.py differs from the expected layout (anchor 3); not patching. '
             'Apply the edit by hand (see the OpenKE wiki).')
graft_3 = ANCHOR_3 + "\n        need_cfg_update |= self.set_adaptive_mesh(gcmd)\n"
s = s.replace(ANCHOR_3, graft_3, 1)

# 4. Guard probe_finalize's save_profile call so a None _profile_name (set by
# set_adaptive_mesh above) doesn't crash - restores the guard mainline has and
# this fork is missing (it predates the adaptive-mesh feature, the only thing
# that ever passed None here before now).
if s.count(UNGUARDED_SAVE) != 1:
    sys.exit('STOP: bed_mesh.py differs from the expected layout (anchor 4, '
             'probe_finalize save_profile call); not patching. Apply the edit by hand.')
s = s.replace(UNGUARDED_SAVE, GUARDED_SAVE, 1)

open(p, 'w').write(s)
try:
    py_compile.compile(p, doraise=True)
except py_compile.PyCompileError as e:
    sys.exit('STOP: patched bed_mesh.py failed to compile (%s). Restore from backup.' % e)

print('Success: bed_mesh.py now supports BED_MESH_CALIBRATE ADAPTIVE=1.')
