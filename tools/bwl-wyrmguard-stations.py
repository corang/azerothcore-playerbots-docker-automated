#!/usr/bin/env python3
"""bwl-wyrmguard-stations.py — author the six BWL Wyrmguard tank stations (patch 0020, spec §4.3).

Needs a character (GM is fine) parked on map 469 between the two packs, so the probe has a path
owner and the grids are loaded. Run from the overlay repo root:

  python3 tools/bwl-wyrmguard-stations.py gen > /tmp/bwlwg-probe.cmds      # full grid, ~1460 probes
  bash -c 'mapfile -t C < /tmp/bwlwg-probe.cmds; WGCONSOLE_PUMP=0.4 python3 tools/wgconsole.py /tmp/bwlwg-probe.log "${C[@]}"'
  python3 tools/bwl-wyrmguard-stations.py pick /tmp/bwlwg-probe.log      # writes the JSON, prints report + C++
  bash -c 'mapfile -t C < /tmp/bwlwg-verify.cmds; WGCONSOLE_PUMP=1 python3 tools/wgconsole.py /tmp/bwlwg-verify.log "${C[@]}"'
  python3 tools/bwl-wyrmguard-stations.py verify /tmp/bwlwg-verify.log
"""
import itertools
import json
import math
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_OUT = os.path.join(ROOT, "tools", "bwl-wyrmguard-stations.json")
VERIFY_CMDS = "/tmp/bwlwg-verify.cmds"

# Slot order matches the C++ table: slot 0 is the formation leader (main tank's Wyrmguard).
PACKS = {
    1: {"guids": (84634, 84628, 84635),
        "spawns": [(-7394.61, -956.29, 465.15), (-7389.70, -951.74, 465.15), (-7400.41, -961.01, 465.14)]},
    2: {"guids": (84648, 84639, 84650),
        "spawns": [(-7443.51, -1045.13, 477.02), (-7437.04, -1053.58, 477.04), (-7450.65, -1035.77, 477.00)]},
}
PULLED_WITH = {84627}  # Master Elemental Shaper Krixix, next to pack 1 - not a station hazard

# Ebonroc (guid 85780) patrol path 857800, walked back and forth between top and bottom of the ramp.
EBONROC_PATH = [(-7358.2, -994.3), (-7368.8, -983.1), (-7378.3, -977.8), (-7392.4, -983.4),
                (-7405.9, -987.8), (-7418.1, -976.6), (-7414.4, -963.3)]
# Keep-outs per pack: only bosses still ALIVE when that pack is pulled (owner, 2026-10-06).
# Pack 1 is pulled before Ebonroc (his ramp patrol); pack 2 after Ebonroc + Flamegor are dead, so
# only Chromaggus (stationary, behind his gate) remains.
KEEPOUTS = {
    1: {"paths": [(EBONROC_PATH, 45.0)], "points": []},
    2: {"paths": [], "points": [(-7515.34, -1029.62, 45.0)]},
}
SPACING = 35.0
GRID_STEP = 5.0       # probe grid spacing (yards)
GRID_HALF = 65.0      # grid extends this far from the pack centre in x and y
CLEARANCE = 10.0      # open floor required around a station: no alcoves, no wall-hugging
# Per-pack overrides (owner, 2026-10-06 dev pull): pack 2's first stations hugged the room's back edge
# and melee could not reach one of them, so pack 2 is centred on the big room in front of it (Flamegor's
# room - he is dead by then) with more open floor. Score = centroid distance to the anchor.
PACK_ANCHOR = {2: (-7407.94, -1031.04)}   # Flamegor's spawn = middle of his room
PACK_CLEARANCE = {2: 15.0}
# Owner-placed stations (in game, `.gps`), applied after the triangle pick: (pack, slot) -> (x, y, z).
# Pack 2 slot 1: the picked one sat on the bots' path to slot 0 (2026-10-06).
STATION_OVERRIDE = {(2, 1): (-7424.41, -1010.11, 477.17)}
SAME_FLOOR_DZ = 4.0   # a probe end this far off the pack's z is another floor (ramp/ledge) = blocked
MAX_CANDIDATES = 150  # nearest-first candidate cap for the triangle search
LOS_STEP = 2.0

PROBE_RE = re.compile(r"\.bgbots probe 469 (\S+) (\S+) (\S+) (\S+) (\S+) (\S+)")
VERDICT_RE = re.compile(
    r"probe: (?:(REACHABLE|BLOCKED) calc=\d type=0x[0-9a-f]+ points=(\d+) "
    r"end=\(([-\d.]+),([-\d.]+),([-\d.]+)\)|(NO GROUND|no loaded instance|no player))")


def center(pack):
    n = len(pack["spawns"])
    return tuple(sum(s[i] for s in pack["spawns"]) / n for i in range(3))


def seg_dist(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / length2))
    return math.hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy))


def keepout_margin(n, p):
    """Smallest slack (yards) over pack n's keep-outs; +inf when it has none."""
    m = math.inf
    for path, clr in KEEPOUTS[n]["paths"]:
        m = min(m, min(seg_dist(p, path[i], path[i + 1]) for i in range(len(path) - 1)) - clr)
    for bx, by, clr in KEEPOUTS[n]["points"]:
        m = min(m, math.hypot(p[0] - bx, p[1] - by) - clr)
    return m


def keepout_ok(n, p):
    return keepout_margin(n, p) >= 0.0


def parse(log_path):
    """Pair every sent probe with its verdict line, in order. Each probe prints exactly one verdict."""
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
        if v.group(6):  # NO GROUND: a candidate inside a wall or over a void - just not usable
            out.append({"to": c[:3], "from": c[3:], "verdict": "NO GROUND", "points": 0, "end": c[:3]})
            continue
        out.append({"to": c[:3], "from": c[3:], "verdict": v.group(1), "points": int(v.group(2)),
                    "end": (float(v.group(3)), float(v.group(4)), float(v.group(5)))})
    return out


def db_spawns():
    env = os.path.join(ROOT, "azerothcore-wotlk", ".env")
    pw = "password"
    if os.path.exists(env):
        for line in open(env):
            if line.startswith("DOCKER_DB_ROOT_PASSWORD="):
                pw = line.split("=", 1)[1].strip() or pw
    q = ("SELECT c.guid, t.name, c.position_x, c.position_y, c.position_z FROM creature c "
         "JOIN creature_template t ON t.entry = c.id WHERE c.map = 469")
    res = subprocess.run(["docker", "exec", "ac-database", "mysql", "-uroot", f"-p{pw}", "acore_world", "-N", "-e", q],
                         capture_output=True, text=True, check=True)
    rows = []
    for line in res.stdout.splitlines():
        g, name, x, y, z = line.split("\t")
        rows.append((int(g), name, float(x), float(y), float(z)))
    return rows


def grid_cells():
    k = int(GRID_HALF // GRID_STEP)
    return [(i, j) for i in range(-k, k + 1) for j in range(-k, k + 1)]


def cmd_gen():
    # The whole grid is probed (not only keep-out-clear points): the blocked cells ARE the wall map
    # that the clearance and line-of-floor tests read.
    for pack in PACKS.values():
        cx, cy, cz = center(pack)
        for i, j in grid_cells():
            x, y = cx + i * GRID_STEP, cy + j * GRID_STEP
            print(f".bgbots probe 469 {x:.2f} {y:.2f} {cz:.2f} {cx:.2f} {cy:.2f} {cz:.2f}")


def floor_map(results, pack):
    """(i, j) -> navmesh end point for open same-floor cells, None for blocked ones."""
    cx, cy, cz = center(pack)
    floor = {}
    for r in results:
        if abs(r["from"][0] - cx) >= 0.05 or abs(r["from"][1] - cy) >= 0.05:
            continue
        cell = (round((r["to"][0] - cx) / GRID_STEP), round((r["to"][1] - cy) / GRID_STEP))
        ok = (r["verdict"] == "REACHABLE" and math.dist(r["end"][:2], r["to"][:2]) < 1.5
              and abs(r["end"][2] - cz) < SAME_FLOOR_DZ)
        floor[cell] = r["end"] if ok else None
    return floor


def print_map(n, pack, floor, stations, cen):
    """North (+x) up, west (+y) left. . open  # blocked  x open but in a keep-out  P pack  S station  C centroid."""
    cx, cy, _ = center(pack)
    k = int(GRID_HALF // GRID_STEP)
    marks = {(0, 0): "P", (round((cen[0] - cx) / GRID_STEP), round((cen[1] - cy) / GRID_STEP)): "C"}
    for idx, s in enumerate(stations):
        marks[(round((s[0] - cx) / GRID_STEP), round((s[1] - cy) / GRID_STEP))] = str(idx)
    print(f"  map (north up, west left, {GRID_STEP:.0f} y cells; digits = station slots):")
    for i in range(k, -k - 1, -1):
        row = ""
        for j in range(k, -k - 1, -1):
            if (i, j) in marks:
                row += marks[(i, j)]
            elif floor.get((i, j)) is None:
                row += "#"
            elif not keepout_ok(n, (cx + i * GRID_STEP, cy + j * GRID_STEP)):
                row += "x"
            else:
                row += "."
        print("    " + row)


def cmd_pick(log_path):
    results = parse(log_path)
    spawns = db_spawns()
    picked = {}
    verify = []
    for n, pack in PACKS.items():
        cx, cy, cz = center(pack)
        floor = floor_map(results, pack)
        blocked = [(cx + i * GRID_STEP, cy + j * GRID_STEP) for (i, j), v in floor.items() if v is None]

        def clearance(p):
            return min((math.dist(p, b) for b in blocked), default=math.inf)

        def on_floor(p):
            return floor.get((round((p[0] - cx) / GRID_STEP), round((p[1] - cy) / GRID_STEP))) is not None

        def line_of_floor(a, b):
            steps = max(1, int(math.dist(a, b) / LOS_STEP))
            return all(on_floor((a[0] + (b[0] - a[0]) * t / steps, a[1] + (b[1] - a[1]) * t / steps))
                       for t in range(steps + 1))

        need = PACK_CLEARANCE.get(n, CLEARANCE)
        anchor = PACK_ANCHOR.get(n, (cx, cy))
        cands = [v for v in floor.values() if v and keepout_ok(n, v[:2]) and clearance(v[:2]) >= need]
        cands.sort(key=lambda v: math.dist(v[:2], anchor))
        cands = cands[:MAX_CANDIDATES]
        best = None
        for a, b, c in itertools.combinations(cands, 3):
            if (math.dist(a[:2], b[:2]) < SPACING or math.dist(a[:2], c[:2]) < SPACING
                    or math.dist(b[:2], c[:2]) < SPACING):
                continue
            tri = (a, b, c)
            if n in PACK_ANCHOR:
                far = math.dist(((a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3), anchor)
            else:
                far = max(math.dist(p[:2], (cx, cy)) for p in tri)
            score = (round(far, 1), -min(clearance(p[:2]) for p in tri))
            if best is not None and score >= best[0]:
                continue
            cen = (sum(p[0] for p in tri) / 3, sum(p[1] for p in tri) / 3)
            if not keepout_ok(n, cen) or not on_floor(cen):
                continue
            if not all(line_of_floor(cen, p[:2]) for p in tri):
                continue
            best = (score, tri, cen)
        if best is None:
            sys.exit(f"pack {n}: no station triangle meets the constraints — widen GRID_HALF or relax CLEARANCE")
        stations = sorted(best[1], key=lambda p: math.dist(p[:2], (cx, cy)))  # slot 0 = nearest the pack
        stations = [STATION_OVERRIDE.get((n, i), s) for i, s in enumerate(stations)]
        cen = (sum(p[0] for p in stations) / 3, sum(p[1] for p in stations) / 3)
        picked[n] = {"stations": [list(s) for s in stations], "centroid": [cen[0], cen[1], cz]}
        far_spawn = max(math.dist(p[:2], (cx, cy)) for p in stations)
        print(f"pack {n}: farthest station {far_spawn:.1f} y from the spawn centre, score {best[0][0]} "
              f"({len(cands)} candidates with >= {need:.0f} y open floor)")
        for i, s in enumerate(stations):
            near = [f"{name}({g}) {math.dist((x, y), s[:2]):.0f}y" for g, name, x, y, z in spawns
                    if g not in pack["guids"] and g not in PULLED_WITH and abs(z - s[2]) < 10
                    and math.dist((x, y), s[:2]) < 30]
            print(f"  slot {i}: ({s[0]:.2f}, {s[1]:.2f}, {s[2]:.2f})  open floor {clearance(s[:2]):.0f}y  "
                  f"keep-out slack {keepout_margin(n, s[:2]):.1f}y  spawns <30y: {', '.join(near) or 'none'}")
            verify.append(f".bgbots probe 469 {s[0]:.2f} {s[1]:.2f} {s[2]:.2f} {cen[0]:.2f} {cen[1]:.2f} {cz:.2f}")
        print(f"  centroid ({cen[0]:.2f}, {cen[1]:.2f})  keep-out slack {keepout_margin(n, cen):.1f}y")
        print_map(n, pack, floor, stations, cen)
    with open(JSON_OUT, "w") as f:
        json.dump(picked, f, indent=2)
    with open(VERIFY_CMDS, "w") as f:
        f.write("\n".join(verify) + "\n")
    print(f"\nwrote {JSON_OUT} and {VERIFY_CMDS}\n\n// ---- paste into BWLHelpers.cpp (Task 5) ----")
    print("    WyrmguardPack const WYRMGUARD_PACKS[WYRMGUARD_PACK_COUNT] = {")
    for n, pack in PACKS.items():
        cx, cy, cz = center(pack)
        st = picked[n]["stations"]
        print(f"        {{ {{ {pack['guids'][0]}, {pack['guids'][1]}, {pack['guids'][2]} }}, "
              f"Position({cx:.2f}f, {cy:.2f}f, {cz:.2f}f),")
        print("          { " + ", ".join(f"Position({s[0]:.2f}f, {s[1]:.2f}f, {s[2]:.2f}f)" for s in st) + " } },")
    print("    };")


def cmd_verify(log_path):
    bad = 0
    # Path point counts are NOT a line-of-sight signal (PathGenerator subdivides straight runs over
    # open floor into 6-7 points); line of floor is checked in `pick` from the probe grid instead.
    for r in parse(log_path):
        print(f"centroid -> ({r['to'][0]:.2f}, {r['to'][1]:.2f}): {r['verdict']} points={r['points']}")
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
