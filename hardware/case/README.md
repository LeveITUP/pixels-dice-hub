# Enclosure – Waveshare ESP32-S3-Touch-AMOLED-1.64 (v2)

Two-part 3D-printable case with an internal 3.7 V LiPo pocket, generated
parametrically from the official Waveshare STEP model of the board.

| File | What |
|------|------|
| `s3_amoled_164_body.stl` / `.step` | Main shell: display pocket (front), mid-plate, battery cavity (rear) |
| `s3_amoled_164_lid.stl` / `.step`  | Rear battery cover |
| `s3_amoled_164_case.py`            | CadQuery source – edit the `P` dict and re-run to regenerate |

Outer size **34.4 × 53.0 × 22.9 mm**. Portrait, USB-C at the bottom edge,
matching the firmware's default orientation.

## How it goes together

```
front  ┌────────────────────────────┐  Z=+2.0  rim 0.4 mm proud of the glass
       │  display module (bezel)    │
       │  PCB + rear components     │
       │  4× M2 hex standoffs ──────┼── rest on the mid-plate (Z=-8.1)
       ├──── mid-plate 2.2 mm ──────┤  4× M2×4 pan-head screws from the rear
       │  ribs 1.0 mm               │
       │  603040 LiPo 6×30×40       │
       │  lid lip 1.0 mm            │
rear   └──── lid 2.0 mm ────────────┘  2× M2×6 countersunk screws
```

1. Drop the module into the front pocket (USB-C towards the end with the
   large cutout). It is a 0.25 mm/side fit; no force needed.
2. From the rear, drive **4× M2×4 pan-head screws** through the counterbored
   holes in the mid-plate into the board's own brass hex standoffs. The
   standoffs only have ~2.9 mm of female thread – do **not** use longer screws.
3. Plug the battery into the MX1.25 header on the back of the board
   (see polarity below). Feed the lead through the rounded slot in the
   plate, lay the cell on the three ribs with its leads at the USB end.
4. Fit the lid (lip plugs into the cavity) and secure with **2× M2×6
   countersunk self-tapping screws** into the diagonal bosses.

## Cutouts

| Feature | Location |
|---------|----------|
| USB-C + PWR/CHG LEDs | 15 × 7 mm window, bottom edge |
| BOOT / RESET side switches | Ø3.4 mm holes in the left/right walls – press with a pin or SIM tool |
| Battery lead | 10 × 9 mm slot in the mid-plate under the MX1.25 header |

Not exposed: the microSD socket (sits between PCB and mid-plate) and the
U.FL antenna connector. Neither is used by the firmware.

## Battery

The header is a **2-pin 1.25 mm pitch (MX1.25 / "JST 1.25")**, J2 on the
schematic: **pin 1 = VBAT (+), pin 2 = GND (−)**, silkscreened `− +`
next to the connector. Charger is an ETA6098 set to **500 mA** CC charge,
and the V1.1 PCB has reverse-polarity protection, but still check polarity
before plugging in – vendors ship 1.25 mm leads both ways round.

Designed around a **603040 cell: 3.7 V, 700–800 mAh, 6 × 30 × 40 mm**,
ordered *with a 2-pin 1.25 mm connector* (listings titled e.g.
"603040 3.7V 800mAh LiPo with JST 1.25 / MX1.25 2P plug", from the usual
AliExpress/eBay/Amazon suppliers, or Waveshare's own "3.7V Lithium Battery
with MX1.25" accessories). Cavity is 30.6 × 48 × 6.6 mm, so any cell up to
30 × ~43 × 6 mm with leads at a short end also fits, e.g.:

| Cell | Capacity | Size (T×W×L) |
|------|----------|--------------|
| 603040 (design target) | ~750 mAh | 6 × 30 × 40 |
| 602535 | ~500 mAh | 6 × 25 × 35 |
| 503035 | ~500 mAh | 5 × 30 × 35 |
| 502030 | ~250 mAh | 5 × 20 × 30 |

Adafruit cells have JST-PH 2.0 mm plugs and will **not** fit the header
without re-terminating. If you must use one, crimp a 1.25 mm 2-pin housing
on with red to pin 1 (+).

For a thicker 103040 (~1200 mAh, 10 mm) cell set `batt_t=10.0` in the
script and regenerate; the case grows to ~27 mm deep.

## Printing

- Material: PLA / PETG, 0.2 mm layers, 0.4 mm nozzle, 3 perimeters.
- **Body**: print with the front (display) face **up**. The only overhang
  is the mid-plate ceiling inside the battery cavity; the three ribs split
  it into ≤14 mm bridges, so it prints without supports. Use a brim – the
  bed contact is just the rear wall rim and the two bosses.
- **Lid**: flat face down, no supports.
- Tolerances are tuned for a typical FDM printer (0.25 mm module
  clearance, 0.2 mm lid-lip clearance). Adjust `pocket_clearance` /
  `lid_clear` in `P` if your printer runs tight or loose.

## Hardware

| Qty | Part |
|-----|------|
| 4 | M2 × 4 mm pan-head machine screw (into the board standoffs) |
| 2 | M2 × 6 mm countersunk self-tapping screw (lid) |
| 1 | 603040 3.7 V LiPo with 1.25 mm 2-pin plug |

## Regenerating

```sh
python -m venv .venv && .venv/bin/pip install cadquery trimesh
.venv/bin/python s3_amoled_164_case.py \
    --check ../../esp32-s3-touch-amoled-1_64.stp   # optional interference check
```

`--check` intersects the body with the key solids from Waveshare's STEP
(PCB, bezel, standoffs, USB-C, switches, battery header, SD socket …) and
also runs fit probes (USB plug, switch pin, battery block, screw heads,
screwdriver access, seated lid). Download the STEP from the Waveshare wiki
("3D drawing") if it isn't present; it is not committed (13 MB).

Dimensions taken from the STEP: bezel 29.12 × 44.0 mm R3.69, PCB
28.62 × 43.52 × 1.6 mm, standoff centres (±11.0, 18.75) / (±11.0, −19.75)
relative to board origin (22.0 × 38.5 mm pattern), rear face of standoffs
8.04 mm behind the display glass, switch actuators at Y −15.24 / Z −4.7,
USB-C centre Z −5.17.
