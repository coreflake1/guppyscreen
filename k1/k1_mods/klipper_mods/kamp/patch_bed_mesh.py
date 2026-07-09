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

if 'set_adaptive_mesh' in s:
    print('Already patched — nothing to change.')
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
        # Upstream Klipper sets this to None here - fine on mainline, but this
        # fork's probe_finalize() unconditionally does
        # self.bedmesh.save_profile(self._profile_name), and save_profile()
        # does self.name + " " + prof_name with no None-guard, so a bare None
        # crashes with "can only concatenate str (not NoneType) to str".
        # Use a synthetic name instead - keeps the same "don't collide with a
        # real saved profile" intent without touching the existing "default"
        # profile or needing a save_profile() change of our own.
        self._profile_name = "adaptive-%X" % (id(self),)
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

open(p, 'w').write(s)
try:
    py_compile.compile(p, doraise=True)
except py_compile.PyCompileError as e:
    sys.exit('STOP: patched bed_mesh.py failed to compile (%s). Restore from backup.' % e)

print('Success: bed_mesh.py now supports BED_MESH_CALIBRATE ADAPTIVE=1.')
