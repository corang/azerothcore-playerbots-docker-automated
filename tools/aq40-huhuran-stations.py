#!/usr/bin/env python3
"""aq40-huhuran-stations.py — author Princess Huhuran's park spot P and the fallback tank station T, and
verify the ranged/healer rings and the tank-station circle around P on the floor (AQ40 Huhuran strategy, Task 2).

Needs a character (GM is fine) parked on map 531 in Huhuran's room, near her spawn
(-8515.8, 1693.7, -90.5), so the probe has a path owner and the grids are loaded. Run from the overlay
repo root:

  python3 tools/aq40-huhuran-stations.py gen > /tmp/aq40huh-probe.cmds
  bash -c 'mapfile -t C < /tmp/aq40huh-probe.cmds; WGCONSOLE_PUMP=0.4 python3 tools/wgconsole.py /tmp/aq40huh-probe.log "${C[@]}"'
  python3 tools/aq40-huhuran-stations.py pick /tmp/aq40huh-probe.log     # writes JSON + verify cmds, prints C++
  bash -c 'mapfile -t C < /tmp/aq40huh-verify.cmds; WGCONSOLE_PUMP=1 python3 tools/wgconsole.py /tmp/aq40huh-verify.log "${C[@]}"'
  python3 tools/aq40-huhuran-stations.py verify /tmp/aq40huh-verify.log   # OK/CLIPPED/DETOUR per point + counts

Run time: gen emits ~3,200 probes (a ~1,600-cell floor map from the patrol midpoint plus ~800 end-box cells from
each patrol end), so the probe run takes ~20-25 min at WGCONSOLE_PUMP=0.4. The verify run is 156 probes
(72 + 72 + 12) at WGCONSOLE_PUMP=1, ~3 min.

P is the floor point with the most open floor within 36 y that Huhuran's patrol can path to from BOTH
patrol ends. T is only the DEGENERATE fallback: the C++ latches the real tank station per pull from her
approach direction (GetHuhuranTankStation); HUHURAN_MT_STATION is used only when she is within 0.5 y of P.

Which points are checked: the C++ latches the ring reference bearing per pull as P -> T, and T itself per pull
at P + 5 y along her approach (GetHuhuranTankStation), so that bearing can be ANY angle, and the ring count is
the latched number of bots. A fixed set of 17/9 spots therefore proves nothing; every check sweeps:
  - the ranged ring (17 y) and the healer ring (32 y) at 5 degree steps: 72 + 72 points;
  - the 5 y circle around P (every possible dynamic T) at 30 degree steps: 12 points.

Line of sight to P is checked twice, because path reachability alone is not LOS (a point behind a pillar can
be reached by a detour):
  - pick: a floor-grid line_of_floor(P, point) for every swept point (samples every ~2 y along the segment;
    each sample's nearest grid cell must be floor), plus an explicit floor check on each tank-circle point.
    Failing points are printed and pick exits 1 without printing the C++ block (the JSON and verify cmds are
    still written). That is the plan's BLOCKED path: stop and report with the JSON.
  - verify: a point is OK only if REACHABLE, its end is within 1.5 y of the target, AND its path is
    near-straight. `.bgbots probe` prints the path point count but not a path length, and its PathGenerator
    runs FindSmoothPath (4 y steps), so a straight path has ~dist/4 + 1 points. The test is therefore
    points <= ceil(dist_xy(P, spot) / 4) + 2; a spot over that is DETOUR and counts as not ok. This
    straightness test is APPROXIMATE (a gentle bend fits inside the slack); the floor-grid line_of_floor in
    pick is the primary line-of-sight gate. verify prints ranged_ok=N/72 healer_ok=M/72 tank_ok=K/12 and
    exits 1 on any point that is not OK.

Probe plan (gen): a 5 y grid over the bounding box of patrol path 880140 padded by 40 y, every cell probed
from the patrol midpoint (this builds the floor map). Cells inside the patrol box padded by END_PAD are also
probed from each patrol end, so pick can require P to be reachable from both ends. Pick only considers P
candidates that have both end verdicts in the log.
"""
import json
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_OUT = os.path.join(ROOT, "tools", "aq40-huhuran-stations.json")
VERIFY_CMDS = "/tmp/aq40huh-verify.cmds"

MAP_ID = 531
# Patrol path 880140 bounding box and the points the probes start from.
BOX_X = (-8656.3, -8512.99)
BOX_Y = (1693.65, 1788.96)
BOX_PAD = 40.0
MID = (-8575.84, 1788.96, -91.80)         # patrol midpoint: start of every floor-map probe
END_A = (-8515.80, 1693.65, -91.0)        # patrol end 1 (near her spawn)
END_B = (-8656.29, 1757.13, -91.0)        # patrol end 2
TARGET_Z = -91.0                          # z handed to a grid probe (the core snaps to ground)
END_PAD = 10.0                            # end-probe cells: patrol box padded by this much
GRID_STEP = 5.0
SAME_FLOOR_DZ = 3.0
OPEN_RADIUS = 36.0                        # P maximises floor points within this
T_OFFSET = 5.0                            # T = P + this toward the open-floor centroid, facing P
LOS_STEP = 2.0
# verify: FindSmoothPath steps SMOOTH_PATH_STEP_SIZE = 4 y (PathGenerator.h:46, PathGenerator.cpp:1002-1005) and
# the probe's PathGenerator is not _useStraightPath (PathGenerator.cpp:131), so a straight path has about
# dist/4 + 1 points; more than ceil(dist/SMOOTH_STEP) + DETOUR_SLACK_POINTS = the path bends round something.
SMOOTH_STEP = 4.0
DETOUR_SLACK_POINTS = 2
END_TOL = 1.5                             # a probe "arrives" when its end is this close to the target (xy)

# Swept check points (see the docstring): the rings at RING_STEP_DEG, the dynamic-T circle at TANK_STEP_DEG.
RANGED_R = 17.0                           # Aq40HuhuranRules::RANGED_RADIUS
HEALER_R = 32.0                           # Aq40HuhuranRules::HEALER_RADIUS
RING_STEP_DEG = 5
RING_N = 360 // RING_STEP_DEG             # 72 per ring
TANK_R = T_OFFSET                         # HUHURAN_MT_STATION_OFFSET: every per-pull T is on this circle
TANK_STEP_DEG = 30
TANK_N = 360 // TANK_STEP_DEG             # 12
ROLES = (("ranged", RANGED_R, RING_N, RING_STEP_DEG), ("healer", HEALER_R, RING_N, RING_STEP_DEG),
         ("tank", TANK_R, TANK_N, TANK_STEP_DEG))

PROBE_RE = re.compile(r"\.bgbots probe 531 (\S+) (\S+) (\S+) (\S+) (\S+) (\S+)")
VERDICT_RE = re.compile(
    r"probe: (?:(REACHABLE|BLOCKED) calc=\d type=0x[0-9a-f]+ points=(\d+) "
    r"end=\(([-\d.]+),([-\d.]+),([-\d.]+)\)|(NO GROUND|no loaded instance|no player))")


def parse(log_path):
    """Pair every sent probe with its verdict line, in order (same contract as bwl-nefarian-stations.py)."""
    text = open(log_path, "rb").read().decode("utf-8", "replace")
    cmds = [tuple(float(v) for v in m.groups()) for m in PROBE_RE.finditer(text) if ">>> SENDING" in
            text[max(0, m.start() - 20):m.start()]]
    verdicts = list(VERDICT_RE.finditer(text))
    if len(cmds) != len(verdicts):
        sys.exit(f"parse: {len(cmds)} probes sent but {len(verdicts)} verdicts — re-run with a larger WGCONSOLE_PUMP")
    out = []
    for c, v in zip(cmds, verdicts):
        if v.group(6) in ("no loaded instance", "no player"):
            sys.exit(f"parse: probe failed outright ({v.group(6)}) — is a character on map {MAP_ID}?")
        if v.group(6):
            out.append({"to": c[:3], "from": c[3:], "verdict": "NO GROUND", "points": 0, "end": c[:3]})
            continue
        out.append({"to": c[:3], "from": c[3:], "verdict": v.group(1), "points": int(v.group(2)),
                    "end": (float(v.group(3)), float(v.group(4)), float(v.group(5)))})
    return out


def grid_origin():
    return (BOX_X[0] - BOX_PAD, BOX_Y[0] - BOX_PAD)


def grid_dims():
    nx = int(math.floor((BOX_X[1] + BOX_PAD - (BOX_X[0] - BOX_PAD)) / GRID_STEP)) + 1
    ny = int(math.floor((BOX_Y[1] + BOX_PAD - (BOX_Y[0] - BOX_PAD)) / GRID_STEP)) + 1
    return nx, ny


def cell_xy(i, j):
    x0, y0 = grid_origin()
    return (x0 + i * GRID_STEP, y0 + j * GRID_STEP)


def in_end_box(x, y):
    return (BOX_X[0] - END_PAD <= x <= BOX_X[1] + END_PAD) and (BOX_Y[0] - END_PAD <= y <= BOX_Y[1] + END_PAD)


def cmd_gen():
    nx, ny = grid_dims()
    for i in range(nx):
        for j in range(ny):
            x, y = cell_xy(i, j)
            print(f".bgbots probe {MAP_ID} {x:.2f} {y:.2f} {TARGET_Z:.2f} {MID[0]:.2f} {MID[1]:.2f} {MID[2]:.2f}")
    for end in (END_A, END_B):
        for i in range(nx):
            for j in range(ny):
                x, y = cell_xy(i, j)
                if in_end_box(x, y):
                    print(f".bgbots probe {MAP_ID} {x:.2f} {y:.2f} {TARGET_Z:.2f} "
                          f"{end[0]:.2f} {end[1]:.2f} {end[2]:.2f}")


def arrived(r, ref_z):
    """REACHABLE, ended on the target, and on the same floor as the start."""
    return (r["verdict"] == "REACHABLE" and math.dist(r["end"][:2], r["to"][:2]) < END_TOL
            and abs(r["end"][2] - ref_z) < SAME_FLOOR_DZ)


def same_pt(a, b):
    return abs(a[0] - b[0]) < 0.02 and abs(a[1] - b[1]) < 0.02


def check_points(px, py):
    """Every swept point, in verify order: (role, degrees, x, y) — ranged ring, healer ring, then tank circle."""
    out = []
    for name, rad, n, step in ROLES:
        for k in range(n):
            a = math.radians(k * step)
            out.append((name, k * step, px + rad * math.cos(a), py + rad * math.sin(a)))
    return out


def cmd_pick(log_path):
    results = parse(log_path)
    x0, y0 = grid_origin()

    def cell(p):
        return (round((p[0] - x0) / GRID_STEP), round((p[1] - y0) / GRID_STEP))

    floor = {}          # cell -> end point, or None when not floor (midpoint probes only)
    from_end = {0: set(), 1: set()}   # cells reachable from each patrol end
    for r in results:
        c = cell(r["to"])
        if same_pt(r["from"], MID):
            floor[c] = r["end"] if arrived(r, MID[2]) else None
        elif same_pt(r["from"], END_A) or same_pt(r["from"], END_B):
            k = 0 if same_pt(r["from"], END_A) else 1
            if arrived(r, END_A[2]):
                from_end[k].add(c)
    if not any(v is not None for v in floor.values()):
        sys.exit("pick: no floor cells in the log — is it a gen run from Huhuran's room on map 531?")

    floor_cells = [c for c, v in floor.items() if v is not None]
    cand = [c for c in floor_cells if c in from_end[0] and c in from_end[1]]
    if not cand:
        sys.exit("pick: no floor cell is reachable from BOTH patrol ends — check the end probes in the log")

    def xy(c):
        return cell_xy(*c)

    def neighbours(c):
        px, py = xy(c)
        return [d for d in floor_cells if math.dist(xy(d), (px, py)) <= OPEN_RADIUS]

    nx, ny = grid_dims()
    nonfloor = [xy((i, j)) for i in range(nx) for j in range(ny) if floor.get((i, j)) is None]

    def clearance(c):
        return min((math.dist(xy(c), b) for b in nonfloor), default=math.inf)

    # P = the candidate with the most open floor; ties break on clearance (largest distance to the nearest
    # non-floor grid cell, so P is not on a plateau edge), then on the lowest (x, y) so a re-run is stable.
    # A candidate whose open-floor centroid sits on it (<0.5 y) has no bearing for T, so it is skipped.
    counts = {c: len(neighbours(c)) for c in cand}
    best = None
    cx = cy = 0.0
    for c in sorted(cand, key=lambda c: (-counts[c], -round(clearance(c), 3), xy(c))):
        near = [xy(d) for d in neighbours(c)]
        ccx = sum(p[0] for p in near) / len(near)
        ccy = sum(p[1] for p in near) / len(near)
        if math.hypot(ccx - xy(c)[0], ccy - xy(c)[1]) >= 0.5:
            best, cx, cy = c, ccx, ccy
            break
    if best is None:
        sys.exit("pick: every P candidate sits on its open-floor centroid — no bearing for the fallback T")
    best_n = counts[best]
    px, py = xy(best)
    pz = floor[best][2]

    bear = math.atan2(cy - py, cx - px)
    tx, ty = px + T_OFFSET * math.cos(bear), py + T_OFFSET * math.sin(bear)
    # T faces P; the fallback is only used when she is within 0.5 y of P.
    to = math.atan2(py - ty, px - tx) % (2 * math.pi)
    tc = cell((tx, ty))
    tz = floor[tc][2] if floor.get(tc) else pz

    picked = {"park": [round(px, 2), round(py, 2), round(pz, 2)],
              "mt": [round(tx, 2), round(ty, 2), round(tz, 2), round(to, 4)],
              "ranged_ok": None, "healer_ok": None, "tank_ok": None}

    def on_floor(q):
        return floor.get(cell(q)) is not None

    def line_of_floor(a, b):
        steps = max(1, int(math.dist(a, b) / LOS_STEP))
        return all(on_floor((a[0] + (b[0] - a[0]) * t / steps, a[1] + (b[1] - a[1]) * t / steps))
                   for t in range(steps + 1))

    verify, failed = [], []
    for name, deg, sx, sy in check_points(px, py):
        verify.append(f".bgbots probe {MAP_ID} {sx:.2f} {sy:.2f} {pz:.2f} {px:.2f} {py:.2f} {pz:.2f}")
        if name == "tank" and not on_floor((sx, sy)):
            failed.append(("NO FLOOR at", name, deg, sx, sy))
        elif not line_of_floor((px, py), (sx, sy)):
            failed.append(("NO LINE OF FLOOR from P to", name, deg, sx, sy))
    with open(JSON_OUT, "w") as f:
        json.dump(picked, f, indent=2)
        f.write("\n")
    with open(VERIFY_CMDS, "w") as f:
        f.write("\n".join(verify) + "\n")

    print(f"P {picked['park']}  T(fallback) {picked['mt']}  open floor pts within {OPEN_RADIUS:.0f} y: {best_n}")
    print(f"wrote {JSON_OUT} and {VERIFY_CMDS} ({len(verify)} probes)\n")
    if failed:
        for what, name, deg, sx, sy in failed:
            print(f"{what} {name} {deg:3d} deg ({sx:.2f}, {sy:.2f})")
        sys.exit(f"pick: BLOCKED — {len(failed)} swept point(s) failed the floor / line-of-floor check (any ring "
                 "bearing or T direction can be latched); no C++ block printed")
    print("// ---- replace the HUHURAN_* lines in Aq40Helpers.h (Task 3) ----")
    print("constexpr bool HUHURAN_STATIONS_PROVISIONAL = false;")
    print(f"inline Position const HUHURAN_PARK({picked['park'][0]:.2f}f, {picked['park'][1]:.2f}f, "
          f"{picked['park'][2]:.2f}f);")
    print(f"inline Position const HUHURAN_MT_STATION({picked['mt'][0]:.2f}f, {picked['mt'][1]:.2f}f, "
          f"{picked['mt'][2]:.2f}f, {picked['mt'][3]:.4f}f);")


def cmd_verify(log_path):
    results = parse(log_path)
    expected = sum(n for _, _, n, _ in ROLES)
    if len(results) != expected:
        sys.exit(f"verify: expected {expected} probes ({RING_N} ranged + {RING_N} healer + {TANK_N} tank), "
                 f"log has {len(results)}")
    park_z = results[0]["from"][2]
    labels = [(name, k * step) for name, _, n, step in ROLES for k in range(n)]
    ok_count = {name: 0 for name, _, _, _ in ROLES}
    for (name, deg), r in zip(labels, results):
        ok = arrived(r, park_z)
        detour = ok and r["points"] > math.ceil(math.dist(r["from"][:2], r["to"][:2]) / SMOOTH_STEP) + DETOUR_SLACK_POINTS
        ok = ok and not detour
        tag = "OK" if ok else ("DETOUR" if detour else "CLIPPED")
        print(f"{name} {deg:3d} deg ({r['to'][0]:.2f}, {r['to'][1]:.2f}): {tag} {r['verdict']} points={r['points']}")
        ok_count[name] += ok
    print(f"ranged_ok={ok_count['ranged']}/{RING_N} healer_ok={ok_count['healer']}/{RING_N} "
          f"tank_ok={ok_count['tank']}/{TANK_N}")
    if os.path.exists(JSON_OUT):
        data = json.load(open(JSON_OUT))
        data["ranged_ok"], data["healer_ok"], data["tank_ok"] = ok_count["ranged"], ok_count["healer"], ok_count["tank"]
        with open(JSON_OUT, "w") as f:
            json.dump(data, f, indent=2)
            f.write("\n")
    sys.exit(0 if all(ok_count[name] == n for name, _, n, _ in ROLES) else 1)


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "gen":
        cmd_gen()
    elif len(sys.argv) == 3 and sys.argv[1] == "pick":
        cmd_pick(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "verify":
        cmd_verify(sys.argv[2])
    else:
        sys.exit(__doc__)
