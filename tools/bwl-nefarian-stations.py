#!/usr/bin/env python3
"""bwl-nefarian-stations.py — author Nefarian's MT station T, parking spot P and the flank stations
(patch 0020, spec 2026-10-07 §4.2/§4.2a).

Needs a character (GM is fine) parked on map 469 in Nefarian's room, near the throne, so the probe has
a path owner and the grids are loaded. Run from the overlay repo root:

  python3 tools/bwl-nefarian-stations.py gen > /tmp/bwlnef-probe.cmds
  bash -c 'mapfile -t C < /tmp/bwlnef-probe.cmds; WGCONSOLE_PUMP=0.4 python3 tools/wgconsole.py /tmp/bwlnef-probe.log "${C[@]}"'
  python3 tools/bwl-nefarian-stations.py pick /tmp/bwlnef-probe.log     # writes JSON + verify cmds, prints C++
  bash -c 'mapfile -t C < /tmp/bwlnef-verify.cmds; WGCONSOLE_PUMP=1 python3 tools/wgconsole.py /tmp/bwlnef-verify.log "${C[@]}"'
  python3 tools/bwl-nefarian-stations.py verify /tmp/bwlnef-verify.log
"""
import json
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_OUT = os.path.join(ROOT, "tools", "bwl-nefarian-stations.json")
VERIFY_CMDS = "/tmp/bwlnef-verify.cmds"

LANDING = (-7502.00, -1256.50, 476.76)   # waypoint path 11583 point 9 (spec §2.1)
THRONE = (-7587.76, -1261.43, 482.21)    # Victor Nefarius / GO 179118
# Mean of the nine Shadowblink teleport points (boss_nefarian.cpp spellPos) = middle of the combat floor.
# The dais is a raised corner platform (south-east); its steps face the room, so T is searched along
# throne -> ROOM_CENTER, not throne -> landing (the first probe run, 2026-10-07, put that line against
# the east wall: P had 3.6 y of open floor).
ROOM_CENTER = (-7523.52, -1220.25)
FLOOR_Z = LANDING[2]
GRID_CENTER = ((ROOM_CENTER[0] + THRONE[0]) / 2, (ROOM_CENTER[1] + THRONE[1]) / 2)
GRID_STEP = 5.0
GRID_HALF = 55.0
SAME_FLOOR_DZ = 3.0      # the dais is ~5 y up; anything this far off the floor is not floor
LOS_STEP = 2.0
P_SEARCH = (20.0, 50.0)  # P = the most open floor point this far (y) from the throne along throne -> centre
PARK_OFFSET = 5.0        # T = P - this toward the throne (the MT stands between Nefarian and the dais)
P_CLEARANCE = 8.0        # Nefarian is large; P needs open floor around it
CONE_HALF = 41.0         # spell_cone 82
MARGIN = 20.0            # stations keep this clear of both cone edges (BwlNefarianRules::STATION_MARGIN_DEG)
HEALER_MAX_TO_T = 35.0

# Nominal rings: (angle off facing in degrees, radius) per slot; mirrored on both sides.
MELEE = [(95, 5), (100, 5), (105, 5), (110, 5), (115, 5)]
# The dais sits in a walled corner: the widest floor on the line has ~22 y clearance (owner choice A,
# 2026-10-07), so the rings are sized to fit it.
RANGED = [(65, 12), (80, 12), (95, 12), (110, 12), (65, 17), (80, 17), (95, 17), (110, 17)]
HEALER = [(65, 10), (75, 10), (85, 10), (70, 14), (80, 14)]
OT_STAGING = (90, 10)    # side +1
RING_CLEARANCE = {"melee": 2.0, "ranged": 6.0, "healer": 6.0, "ot": 6.0}
NUDGES = [(0, 0), (0, 2), (0, -2), (4, 0), (-4, 0), (4, 2), (-4, -2), (0, 4), (0, -4)]  # (deg, yards)

PROBE_RE = re.compile(r"\.bgbots probe 469 (\S+) (\S+) (\S+) (\S+) (\S+) (\S+)")
VERDICT_RE = re.compile(
    r"probe: (?:(REACHABLE|BLOCKED) calc=\d type=0x[0-9a-f]+ points=(\d+) "
    r"end=\(([-\d.]+),([-\d.]+),([-\d.]+)\)|(NO GROUND|no loaded instance|no player))")


def parse(log_path):
    """Pair every sent probe with its verdict line, in order (same contract as bwl-wyrmguard-stations.py)."""
    text = open(log_path, "rb").read().decode("utf-8", "replace")
    cmds = [tuple(float(v) for v in m.groups()) for m in PROBE_RE.finditer(text) if ">>> SENDING" in
            text[max(0, m.start() - 20):m.start()]]
    verdicts = list(VERDICT_RE.finditer(text))
    if len(cmds) != len(verdicts):
        sys.exit(f"parse: {len(cmds)} probes sent but {len(verdicts)} verdicts — re-run with a larger WGCONSOLE_PUMP")
    out = []
    for c, v in zip(cmds, verdicts):
        if v.group(6) in ("no loaded instance", "no player"):
            sys.exit(f"parse: probe failed outright ({v.group(6)}) — is a character on map 469?")
        if v.group(6):
            out.append({"to": c[:3], "from": c[3:], "verdict": "NO GROUND", "points": 0, "end": c[:3]})
            continue
        out.append({"to": c[:3], "from": c[3:], "verdict": v.group(1), "points": int(v.group(2)),
                    "end": (float(v.group(3)), float(v.group(4)), float(v.group(5)))})
    return out


def grid_cells():
    k = int(GRID_HALF // GRID_STEP)
    return [(i, j) for i in range(-k, k + 1) for j in range(-k, k + 1)]


def cmd_gen():
    cx, cy = GRID_CENTER
    for i, j in grid_cells():
        x, y = cx + i * GRID_STEP, cy + j * GRID_STEP
        print(f".bgbots probe 469 {x:.2f} {y:.2f} {FLOOR_Z:.2f} {LANDING[0]:.2f} {LANDING[1]:.2f} {FLOOR_Z:.2f}")


def floor_map(results):
    cx, cy = GRID_CENTER
    floor = {}
    for r in results:
        cell = (round((r["to"][0] - cx) / GRID_STEP), round((r["to"][1] - cy) / GRID_STEP))
        ok = (r["verdict"] == "REACHABLE" and math.dist(r["end"][:2], r["to"][:2]) < 1.5
              and abs(r["end"][2] - FLOOR_Z) < SAME_FLOOR_DZ)
        floor[cell] = r["end"] if ok else None
    return floor


def rel_bearing(origin, facing, p):
    a = math.atan2(p[1] - origin[1], p[0] - origin[0]) - facing
    a = (a + math.pi) % (2 * math.pi) - math.pi
    return abs(math.degrees(a))


def cmd_pick(log_path):
    cx, cy = GRID_CENTER
    floor = floor_map(parse(log_path))
    blocked = [(cx + i * GRID_STEP, cy + j * GRID_STEP) for (i, j), v in floor.items() if v is None]

    def cell(p):
        return (round((p[0] - cx) / GRID_STEP), round((p[1] - cy) / GRID_STEP))

    def on_floor(p):
        return floor.get(cell(p)) is not None

    def z_at(p):
        v = floor.get(cell(p))
        return v[2] if v else FLOOR_Z

    def clearance(p):
        return min((math.dist(p, b) for b in blocked), default=math.inf)

    def line_of_floor(a, b):
        steps = max(1, int(math.dist(a, b) / LOS_STEP))
        return all(on_floor((a[0] + (b[0] - a[0]) * t / steps, a[1] + (b[1] - a[1]) * t / steps))
                   for t in range(steps + 1))

    # P: the most open floor point P_SEARCH yards out from the throne toward the room centre (the dais
    # is a walled corner, so its foot has no room for the flanks). T = P - PARK_OFFSET toward the throne.
    ux, uy = ROOM_CENTER[0] - THRONE[0], ROOM_CENTER[1] - THRONE[1]
    length = math.hypot(ux, uy)
    ux, uy = ux / length, uy / length
    best = None
    for step in range(int(P_SEARCH[0]), int(P_SEARCH[1]) + 1):
        p = (THRONE[0] + ux * step, THRONE[1] + uy * step)
        if on_floor(p) and (best is None or clearance(p) > clearance(best)):
            best = p
    if best is None:
        sys.exit("pick: no floor between the throne and the room centre — is the probe log from Nefarian's room?")
    park = best
    t = (park[0] - ux * PARK_OFFSET, park[1] - uy * PARK_OFFSET)
    if not on_floor(t) or clearance(park) < P_CLEARANCE:
        sys.exit(f"pick: P {park} has only {clearance(park):.1f} y open floor (< {P_CLEARANCE})")
    facing = math.atan2(t[1] - park[1], t[0] - park[0])

    def place(angle, radius, side, kind):
        for dang, drad in NUDGES:
            a = angle + dang
            if a < CONE_HALF + MARGIN or a > 180 - CONE_HALF - MARGIN:
                continue
            r = radius + drad
            th = facing + side * math.radians(a)
            p = (park[0] + r * math.cos(th), park[1] + r * math.sin(th))
            if not on_floor(p) or clearance(p) < RING_CLEARANCE[kind] or not line_of_floor(park, p):
                continue
            if kind == "healer" and math.dist(p, t) > HEALER_MAX_TO_T:
                continue
            assert abs(rel_bearing(park, facing, p) - a) < 0.5
            return (round(p[0], 2), round(p[1], 2), round(z_at(p), 2))
        sys.exit(f"pick: no floor for {kind} slot ({angle}°, {radius} y, side {side:+d}) — inspect the map")

    rings = {"melee": MELEE, "ranged": RANGED, "healer": HEALER}
    picked = {"mt": [round(t[0], 2), round(t[1], 2), round(z_at(t), 2)],
              "park": [round(park[0], 2), round(park[1], 2), round(z_at(park), 2)],
              "facing_deg": round(math.degrees(facing), 2)}
    for kind, ring in rings.items():
        picked[kind] = [[list(place(a, r, side, kind)) for a, r in ring] for side in (1, -1)]
    picked["ot"] = list(place(OT_STAGING[0], OT_STAGING[1], 1, "ot"))

    verify = [f".bgbots probe 469 {picked['mt'][0]:.2f} {picked['mt'][1]:.2f} {picked['mt'][2]:.2f} "
              f"{park[0]:.2f} {park[1]:.2f} {FLOOR_Z:.2f}"]
    for kind in rings:
        for side in picked[kind]:
            for s in side:
                verify.append(f".bgbots probe 469 {s[0]:.2f} {s[1]:.2f} {s[2]:.2f} {park[0]:.2f} {park[1]:.2f} {FLOOR_Z:.2f}")
    s = picked["ot"]
    verify.append(f".bgbots probe 469 {s[0]:.2f} {s[1]:.2f} {s[2]:.2f} {park[0]:.2f} {park[1]:.2f} {FLOOR_Z:.2f}")
    with open(JSON_OUT, "w") as f:
        json.dump(picked, f, indent=2)
    with open(VERIFY_CMDS, "w") as f:
        f.write("\n".join(verify) + "\n")

    print(f"T {picked['mt']}  P {picked['park']}  facing {picked['facing_deg']}°  "
          f"P open floor {clearance(park):.1f} y  T->throne {math.dist(t, THRONE[:2]):.1f} y")
    print(f"wrote {JSON_OUT} and {VERIFY_CMDS} ({len(verify)} probes)\n")
    print("// ---- paste into BWLHelpers.cpp (Task 5) ----")

    def pos(p):
        return f"Position({p[0]:.2f}f, {p[1]:.2f}f, {p[2]:.2f}f)"

    def arr(rows):
        return "{ " + ", ".join("{ " + ", ".join(pos(p) for p in side) + " }" for side in rows) + " }"

    print("    NefarianStations const NEF_STATIONS = {")
    print(f"        {pos(picked['mt'])},")
    print(f"        {pos(picked['park'])},")
    print(f"        {arr(picked['melee'])},")
    print(f"        {arr(picked['ranged'])},")
    print(f"        {arr(picked['healer'])},")
    print(f"        {pos(picked['ot'])},")
    print("    };")


def cmd_verify(log_path):
    bad = 0
    for r in parse(log_path):
        print(f"P -> ({r['to'][0]:.2f}, {r['to'][1]:.2f}): {r['verdict']} points={r['points']}")
        bad += r["verdict"] != "REACHABLE"
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "gen":
        cmd_gen()
    elif len(sys.argv) == 3 and sys.argv[1] == "pick":
        cmd_pick(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "verify":
        cmd_verify(sys.argv[2])
    else:
        sys.exit(__doc__)
