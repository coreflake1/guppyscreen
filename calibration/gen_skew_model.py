#!/usr/bin/env python3
"""Generate a skew-calibration frame G-code for the Ender-3 V3 KE.

A 150 mm square frame (2 perimeters, 5 layers) centred on the bed. You print
it, measure the two diagonals (A-C, B-D) and the left edge (A-D) with calipers,
and enter them in GuppyKE's Tune -> Skew panel.

Safety: it homes with the probe (G28) and a fresh bed mesh, and never overrides
Z-offset, so first-layer height is whatever the printer is already set to --
exactly like any sliced print. Conservative PLA temps and speeds.

Corner map (as it sits on the bed, printer front toward you):
    D (back-left) ------- C (back-right)
    |                              |
    |                              |
    A (front-left) ----- B (front-right)
  A-C and B-D are the diagonals; A-D is the left edge.
"""

SIDE = 150.0            # outer square side (mm)
CX, CY = 110.0, 110.0   # bed centre (KE bed is 220x220)
LAYER_H = 0.2
LINE_W = 0.4
LAYERS = 5
FIL_DIA = 1.75
NOZZLE_T = 200
BED_T = 60
FIRST_F = 1200          # mm/min, first layer
PRINT_F = 3000          # mm/min, other layers
TRAVEL_F = 7200
RETRACT = 0.8

area = 3.14159265 * (FIL_DIA / 2.0) ** 2
E_PER_MM = (LAYER_H * LINE_W) / area    # ~0.0333 for 0.2x0.4 / 1.75mm

half = SIDE / 2.0
# outer corners A(FL) B(FR) C(BR) D(BL)
A = (CX - half, CY - half)
B = (CX + half, CY - half)
C = (CX + half, CY + half)
D = (CX - half, CY + half)
inset = LINE_W
Ai = (A[0] + inset, A[1] + inset)
Bi = (B[0] - inset, B[1] + inset)
Ci = (C[0] - inset, C[1] - inset)
Di = (D[0] + inset, D[1] - inset)

g = []
def add(line): g.append(line)

def dist(p, q):
    return ((p[0]-q[0])**2 + (p[1]-q[1])**2) ** 0.5

def loop(corners, f):
    """Extrude a closed loop starting and ending at corners[0]."""
    pts = corners + [corners[0]]
    for i in range(1, len(pts)):
        d = dist(pts[i-1], pts[i])
        add("G1 X%.3f Y%.3f E%.5f F%d" % (pts[i][0], pts[i][1], d * E_PER_MM, f))

# ---- header ----
add("; GuppyKE skew calibration frame  (150 mm, 2 perimeters, 5 layers)")
add("; Measure A-C, B-D (diagonals) and A-D (left edge); enter in Tune > Skew.")
add("M140 S%d" % BED_T)            # start bed heat
add("M104 S%d" % NOZZLE_T)         # start nozzle heat (no wait)
add("G90")                          # absolute XYZ
add("M83")                          # relative E
add("M190 S%d" % BED_T)            # wait bed
add("M109 S%d" % NOZZLE_T)         # wait nozzle
add("G28")                          # home (probe-homes Z; uses saved offset)
add("BED_MESH_CALIBRATE")          # fresh mesh for a flat first layer
add("G92 E0")
# prime line up the far left, clear of the frame
add("G1 Z%.3f F600" % LAYER_H)
add("G1 X20 Y40 F%d" % TRAVEL_F)
add("G1 X20 Y180 E%.5f F%d" % (140 * E_PER_MM * 1.5, FIRST_F))  # fat prime line
add("G1 E-%.2f F1800" % RETRACT)
add("G92 E0")

for layer in range(LAYERS):
    z = LAYER_H * (layer + 1)
    f = FIRST_F if layer == 0 else PRINT_F
    add("; --- layer %d  z=%.2f ---" % (layer + 1, z))
    add("G1 Z%.3f F600" % z)
    if layer == 1:
        add("M106 S255")     # part cooling on after the first layer (sharper corners)
    # outer perimeter (start at A)
    add("G1 X%.3f Y%.3f F%d" % (A[0], A[1], TRAVEL_F))
    add("G1 E%.2f F1800" % RETRACT)     # unretract
    loop([A, B, C, D], f)
    # inner perimeter (start at Ai) -- short travel, no full retract
    add("G1 X%.3f Y%.3f F%d" % (Ai[0], Ai[1], TRAVEL_F))
    loop([Ai, Bi, Ci, Di], f)
    add("G1 E-%.2f F1800" % RETRACT)    # retract before layer change

# ---- end ----
add("; --- end ---")
add("M104 S0")
add("M140 S0")
add("M107")                 # fan off
add("G91")
add("G1 Z10 F600")          # lift
add("G90")
add("G1 X110 Y210 F%d" % TRAVEL_F)   # present the bed
add("M84")                  # motors off

out = "\n".join(g) + "\n"
open("calibration/GuppyKE_Skew_Calibration.gcode", "w").write(out)
print("wrote calibration/GuppyKE_Skew_Calibration.gcode  (%d lines, E/mm=%.5f, side=%dmm)"
      % (len(g), E_PER_MM, int(SIDE)))
