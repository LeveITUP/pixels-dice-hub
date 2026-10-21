#!/usr/bin/env python3
"""Parametric enclosure for the Waveshare ESP32-S3-Touch-AMOLED-1.64 (v2).

All coordinates use the frame of the official Waveshare STEP model
(ESP32-S3-TOUCH-AMOLED-1_64_ASM): X across the board, Y along the board
(USB-C at -Y), Z towards the viewer with the display face at Z=+1.6 and
the PCB occupying Z=-4.0..-2.4. Dimensions below were measured from that
STEP file, not from the wiki.

Two printed parts:
  body  - open-fronted shell. The module drops in from the front and is
          fixed with 4x M2x4 pan-head screws driven from the rear, through
          the mid-plate, into the module's own M2 hex standoffs.
  lid   - rear battery cover, 2x M2x6 countersunk screws into diagonal
          bosses.

Run:  python s3_amoled_164_case.py [--check path/to/board.stp]
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import cadquery as cq

# ---------------------------------------------------------------- module facts
BEZEL_W, BEZEL_L, BEZEL_R = 29.12, 44.00, 3.69   # plastic display frame
BEZEL_CY = -0.5                                   # frame centre in STEP Y
Z_DISPLAY_FACE = 1.6
STANDOFF_XY = [(11.0, 18.75), (-11.0, 18.75), (11.0, -19.75), (-11.0, -19.75)]
Z_STANDOFF_END = -8.04                            # rear face of hex studs
STANDOFF_THREAD_DEPTH = 2.9                       # usable M2 female thread
USB_CY, USB_CZ = BEZEL_CY - BEZEL_L / 2, -5.17    # USB-C on -Y edge, mid-mount
LED_X = 6.88                                      # side-view LEDs beside USB
SWITCH_Y, SWITCH_Z = -15.24, -4.70                # side tact switches (+/-X)
BATT_CONN_CX, BATT_CONN_CY = -2.7, -10.4          # MX1.25 2P, rear side

# ---------------------------------------------------------------- parameters
P = dict(
    pocket_clearance=0.25,     # per side around the module
    front_proud=0.4,           # case rim stands proud of the glass
    plate_t=2.2,               # mid plate between module and battery
    plate_gap=0.06,            # plate sits just below the standoff ends
    cb_dia=4.4, cb_depth=1.0,  # counterbore for M2 pan heads (rear side)
    screw_hole=2.3,
    batt_l=40.0, batt_w=30.0, batt_t=6.0,   # 603040 cell nominal
    batt_clear_w=0.6, batt_clear_t=0.6,
    batt_lead_space=8.0,       # extra cavity length; lid bosses eat 2.3 mm at each end
    rib_h=1.0, rib_w=1.2,
    outer_w=34.4, outer_l=53.0, outer_r=5.0,
    cavity_r=1.5,
    lid_t=2.0, lid_lip_h=1.0, lid_lip_w=1.2, lid_clear=0.2,
    boss_dia=4.6, boss_hole=1.6, boss_x=8.0,
    lid_screw_hole=2.3, lid_csk_dia=4.2,
    usb_w=15.0, usb_h=7.0,     # includes the two LED windows
    switch_hole=3.4,
    lead_slot_w=10.0, lead_slot_l=9.0,
    edge_fillet=1.0,
)


def rrect(w: float, l: float, r: float, cy: float = BEZEL_CY) -> cq.Workplane:
    return cq.Workplane("XY").center(0, cy).rect(w, l).extrude(1).edges("|Z").fillet(r).faces("<Z").wires().toPending()


def box_xy(w: float, l: float, r: float, z0: float, z1: float, cy: float = BEZEL_CY) -> cq.Workplane:
    """Rounded-rectangle prism from z0 to z1 (z1 > z0)."""
    return (
        cq.Workplane("XY", origin=(0, 0, z0))
        .center(0, cy)
        .rect(w, l)
        .extrude(z1 - z0)
        .edges("|Z")
        .fillet(r)
    )


def derived():
    d = {}
    d["pocket_w"] = BEZEL_W + 2 * P["pocket_clearance"]
    d["pocket_l"] = BEZEL_L + 2 * P["pocket_clearance"]
    d["pocket_r"] = BEZEL_R + P["pocket_clearance"]
    d["z_front"] = Z_DISPLAY_FACE + P["front_proud"]
    d["z_plate_top"] = Z_STANDOFF_END - P["plate_gap"]
    d["z_plate_bot"] = d["z_plate_top"] - P["plate_t"]
    d["cavity_w"] = P["batt_w"] + P["batt_clear_w"]
    d["cavity_l"] = P["batt_l"] + P["batt_lead_space"]
    d["cavity_depth"] = P["rib_h"] + P["batt_t"] + P["batt_clear_t"] + P["lid_lip_h"]
    d["z_back"] = d["z_plate_bot"] - d["cavity_depth"]
    d["z_lid_bot"] = d["z_back"] - P["lid_t"]
    d["boss_y"] = (BEZEL_CY + d["cavity_l"] / 2, BEZEL_CY - d["cavity_l"] / 2)
    d["bosses"] = [(P["boss_x"], d["boss_y"][0]), (-P["boss_x"], d["boss_y"][1])]
    return d


def build_body(d) -> cq.Workplane:
    body = box_xy(P["outer_w"], P["outer_l"], P["outer_r"], d["z_back"], d["z_front"])
    body = body.faces(">Z").edges().fillet(P["edge_fillet"])

    # module pocket, open to the front
    body = body.cut(box_xy(d["pocket_w"], d["pocket_l"], d["pocket_r"], d["z_plate_top"], d["z_front"] + 1))

    # battery cavity, open to the rear
    body = body.cut(box_xy(d["cavity_w"], d["cavity_l"], P["cavity_r"], d["z_back"] - 1, d["z_plate_bot"]))

    # lid screw bosses (full cavity depth, half-buried in the end walls)
    for (bx, by) in d["bosses"]:
        boss = (
            cq.Workplane("XY", origin=(bx, by, d["z_back"]))
            .circle(P["boss_dia"] / 2)
            .extrude(d["z_plate_bot"] - d["z_back"] + 0.01)
        )
        body = body.union(boss)
        hole = (
            cq.Workplane("XY", origin=(bx, by, d["z_back"] - 0.01))
            .circle(P["boss_hole"] / 2)
            .extrude(7.0)
        )
        body = body.cut(hole)

    # battery support ribs across the cavity (also shorten bridging spans)
    for ry in (BEZEL_CY - 15.5, BEZEL_CY - 1.5, BEZEL_CY + 12.5):
        rib = (
            cq.Workplane("XY", origin=(0, ry, d["z_plate_bot"] - P["rib_h"]))
            .rect(d["cavity_w"], P["rib_w"])
            .extrude(P["rib_h"] + 0.01)
        )
        body = body.union(rib)

    # standoff screws: through-hole plus rear counterbore
    for (sx, sy) in STANDOFF_XY:
        through = (
            cq.Workplane("XY", origin=(sx, sy, d["z_plate_bot"] - P["rib_h"] - 0.5))
            .circle(P["screw_hole"] / 2)
            .extrude(P["plate_t"] + P["rib_h"] + 1.0)
        )
        cb = (
            cq.Workplane("XY", origin=(sx, sy, d["z_plate_bot"] - P["rib_h"] - 0.5))
            .circle(P["cb_dia"] / 2)
            .extrude(P["cb_depth"] + P["rib_h"] + 0.5)
        )
        body = body.cut(through).cut(cb)

    # battery lead slot through the plate under the MX1.25 connector
    slot = (
        cq.Workplane("XY", origin=(BATT_CONN_CX, BATT_CONN_CY, d["z_plate_bot"] - P["rib_h"] - 0.5))
        .rect(P["lead_slot_w"], P["lead_slot_l"])
        .extrude(P["plate_t"] + P["rib_h"] + 1.0)
        .edges("|Z")
        .fillet(1.5)
    )
    body = body.cut(slot)

    # USB-C + LED window through the -Y end wall
    usb = (
        cq.Workplane("XZ", origin=(0, USB_CY + 2, USB_CZ))
        .rect(P["usb_w"], P["usb_h"])
        .extrude(P["outer_l"])          # XZ normal is -Y: extrudes towards -Y
        .edges("|Y")
        .fillet(2.0)
    )
    body = body.cut(usb)

    # side switch access holes (+X and -X walls)
    for sx in (1, -1):
        hole = (
            cq.Workplane("YZ", origin=(sx * (P["outer_w"] / 2 + 1), SWITCH_Y, SWITCH_Z))
            .circle(P["switch_hole"] / 2)
            .extrude(-sx * 6.0)
        )
        body = body.cut(hole)

    return body


def build_lid(d) -> cq.Workplane:
    lid = box_xy(P["outer_w"], P["outer_l"], P["outer_r"], d["z_lid_bot"], d["z_back"])
    lid = lid.faces("<Z").edges().fillet(P["edge_fillet"])

    # locating lip that plugs into the battery cavity
    lip_w = d["cavity_w"] - 2 * P["lid_clear"]
    lip_l = d["cavity_l"] - 2 * P["lid_clear"]
    lip_r = max(P["cavity_r"] - P["lid_clear"], 0.3)
    outer = box_xy(lip_w, lip_l, lip_r, d["z_back"] - 0.01, d["z_back"] + P["lid_lip_h"])
    inner = box_xy(lip_w - 2 * P["lid_lip_w"], lip_l - 2 * P["lid_lip_w"], 0.3,
                   d["z_back"] - 1, d["z_back"] + P["lid_lip_h"] + 1)
    lip = outer.cut(inner)
    for (bx, by) in d["bosses"]:
        lip = lip.cut(
            cq.Workplane("XY", origin=(bx, by, d["z_back"] - 1))
            .circle(P["boss_dia"] / 2 + 0.3)
            .extrude(P["lid_lip_h"] + 2)
        )
    lid = lid.union(lip)

    # countersunk lid screws
    for (bx, by) in d["bosses"]:
        lid = lid.cut(
            cq.Workplane("XY", origin=(bx, by, d["z_lid_bot"] - 1))
            .circle(P["lid_screw_hole"] / 2)
            .extrude(P["lid_t"] + P["lid_lip_h"] + 2)
        )
        csk_h = (P["lid_csk_dia"] - P["lid_screw_hole"]) / 2  # 90 deg countersink
        cone = cq.Solid.makeCone(P["lid_csk_dia"] / 2, P["lid_screw_hole"] / 2, csk_h,
                                 cq.Vector(bx, by, d["z_lid_bot"]), cq.Vector(0, 0, 1))
        lid = lid.cut(cq.Workplane("XY").add(cone))
    return lid


def check_interference(body: cq.Workplane, step_path: Path) -> None:
    """Intersect the body with the mechanically relevant module parts."""
    from OCP.STEPCAFControl import STEPCAFControl_Reader
    from OCP.TDocStd import TDocStd_Document
    from OCP.TCollection import TCollection_ExtendedString
    from OCP.XCAFDoc import XCAFDoc_DocumentTool, XCAFDoc_ShapeTool
    from OCP.TDF import TDF_LabelSequence, TDF_Label
    from OCP.TDataStd import TDataStd_Name
    from OCP.TopLoc import TopLoc_Location

    wanted = {
        "BOARD", "ESP32-S3-TOUCH-AMOLED-1_64-CASE", "MS-DO0164FMST02",
        "HEX_STUDS-M2XH4XL3", "HP1_25MM-2P-SMT-HORIZONTAL", "MUP-M6017-8P-2_5H",
        "SWITCH-TS24CA", "SOLID-323683", "SOLID-182694", "USER_LIBRARY-U_FL-R-SMT_10_",
        "USER_LIBRARY-ESP32-S3-SOC_13", "EXTRUDED-25286",
    }
    doc = TDocStd_Document(TCollection_ExtendedString("doc"))
    rdr = STEPCAFControl_Reader()
    rdr.SetNameMode(True)
    rdr.ReadFile(str(step_path))
    rdr.Transfer(doc)
    st = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    roots = TDF_LabelSequence()
    st.GetFreeShapes(roots)

    def name(l):
        n = TDataStd_Name()
        return n.Get().ToExtString() if l.FindAttribute(TDataStd_Name.GetID_s(), n) else "?"

    parts = []

    def walk(l, loc):
        nm = name(l)
        if XCAFDoc_ShapeTool.IsReference_s(l):
            rl = TDF_Label()
            XCAFDoc_ShapeTool.GetReferredShape_s(l, rl)
            loc = loc.Multiplied(XCAFDoc_ShapeTool.GetLocation_s(l))
            l = rl
        if XCAFDoc_ShapeTool.IsAssembly_s(l):
            comps = TDF_LabelSequence()
            XCAFDoc_ShapeTool.GetComponents_s(l, comps)
            for i in range(1, comps.Length() + 1):
                walk(comps.Value(i), loc)
            return
        if nm in wanted:
            parts.append((nm, cq.Shape.cast(XCAFDoc_ShapeTool.GetShape_s(l).Moved(loc))))

    for i in range(1, roots.Length() + 1):
        walk(roots.Value(i), TopLoc_Location())

    body_solid = body.val()
    bad = 0
    for nm, shp in parts:
        vol = body_solid.intersect(shp).Volume()
        bb = shp.BoundingBox()
        flag = "CLASH" if vol > 1e-3 else "ok"
        bad += flag == "CLASH"
        print(f"  {flag:5s} {nm:36s} overlap={vol:7.3f} mm^3  "
              f"X[{bb.xmin:6.2f},{bb.xmax:6.2f}] Y[{bb.ymin:6.2f},{bb.ymax:6.2f}] Z[{bb.zmin:6.2f},{bb.zmax:6.2f}]")
    print(f"  checked {len(parts)} parts, {bad} clash(es)")
    return bad


def self_check(body: cq.Workplane, lid: cq.Workplane, d) -> int:
    """Probe volumes that must be free (plug, pin, battery, screw access) or fully solid."""
    bs = body.val()
    probes = {
        # USB-C plug: 12x6.5 overmould up to the bezel face, 8.4x2.6 shell 6.5 mm into the receptacle
        "usb-c overmould": cq.Workplane("XY").box(12.0, 20.0, 6.5).translate((0, USB_CY - 10, USB_CZ)),
        "usb-c shell": cq.Workplane("XY").box(8.4, 10.0, 2.6).translate((0, USB_CY - 5 + 6.5, USB_CZ)),
        # 2.5 mm pin reaching each side switch through the wall
        "switch pin +X": cq.Workplane("YZ", origin=(P["outer_w"] / 2 + 5, SWITCH_Y, SWITCH_Z)).circle(1.25).extrude(-(5 + 2.0 + 0.3)),
        "switch pin -X": cq.Workplane("YZ", origin=(-P["outer_w"] / 2 - 5, SWITCH_Y, SWITCH_Z)).circle(1.25).extrude(5 + 2.0 + 0.3),
        # 603040 cell resting on the ribs, pushed against the +Y lid boss (leads at -Y)
        "battery": cq.Workplane("XY").box(P["batt_w"], P["batt_l"], P["batt_t"]).translate(
            (0, BEZEL_CY + d["cavity_l"] / 2 - P["boss_dia"] / 2 - 0.3 - P["batt_l"] / 2,
             d["z_plate_bot"] - P["rib_h"] - P["batt_t"] / 2 - 0.05)),
        # M2 pan head (3.8 dia x 1.3) sitting in each counterbore
        **{f"screw head {i}": cq.Workplane("XY", origin=(sx, sy, d["z_plate_bot"] - P["rib_h"] + 0.0)).circle(1.9).extrude(-1.3)
           for i, (sx, sy) in enumerate(STANDOFF_XY)},
        # screwdriver shaft (3.5 mm) straight down to each standoff screw
        **{f"driver {i}": cq.Workplane("XY", origin=(sx, sy, d["z_plate_bot"])).circle(1.75).extrude(-(d["cavity_depth"] + 10))
           for i, (sx, sy) in enumerate(STANDOFF_XY)},
    }
    failures = 0
    for nm, probe in probes.items():
        vol = bs.intersect(probe.val()).Volume()
        ok = vol < 1e-3
        failures += not ok
        print(f"  {'ok' if ok else 'FAIL':4s} {nm:16s} overlap={vol:.3f} mm^3")
    # lid must not collide with the body when seated
    vol = bs.intersect(lid.val()).Volume()
    ok = vol < 1e-3
    failures += not ok
    print(f"  {'ok' if ok else 'FAIL':4s} {'lid seated':16s} overlap={vol:.3f} mm^3")
    return failures


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=Path(__file__).parent)
    ap.add_argument("--check", type=Path, help="board STEP file for interference check")
    args = ap.parse_args()

    d = derived()
    body = build_body(d)
    lid = build_lid(d)

    total_t = d["z_front"] - d["z_lid_bot"]
    print(f"outer {P['outer_w']} x {P['outer_l']} x {total_t:.2f} mm "
          f"(front Z={d['z_front']:.2f}, back Z={d['z_lid_bot']:.2f})")
    print(f"module pocket {d['pocket_w']:.2f} x {d['pocket_l']:.2f}, plate Z {d['z_plate_top']:.2f}..{d['z_plate_bot']:.2f}")
    print(f"battery cavity {d['cavity_w']:.2f} x {d['cavity_l']:.2f} x {d['cavity_depth']:.2f} (incl. ribs+lid lip)")
    print(f"standoff screw engagement {P['plate_t'] - P['cb_depth']:.1f} mm under head -> "
          f"M2x4 gives {4 - (P['plate_t'] - P['cb_depth']):.1f} mm of {STANDOFF_THREAD_DEPTH} mm thread")

    args.out.mkdir(parents=True, exist_ok=True)
    cq.exporters.export(body, str(args.out / "s3_amoled_164_body.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(lid, str(args.out / "s3_amoled_164_lid.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(body, str(args.out / "s3_amoled_164_body.step"))
    cq.exporters.export(lid, str(args.out / "s3_amoled_164_lid.step"))
    print(f"body volume {body.val().Volume() / 1000:.1f} cm^3, lid {lid.val().Volume() / 1000:.1f} cm^3")

    print("access/fit probes")
    failures = self_check(body, lid, d)
    if args.check:
        print("interference check against", args.check)
        failures += check_interference(body, args.check)
    raise SystemExit(1 if failures else 0)


if __name__ == "__main__":
    main()
