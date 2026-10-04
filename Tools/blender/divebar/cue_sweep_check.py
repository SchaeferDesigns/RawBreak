#!/usr/bin/env python3
"""3D cue-sweep statistics of The Low Bridge Tavern (venue-dive-bar 2.5, R-03, VDB-T3; Docs/ue-architecture.md 18.8 DB-1). Host side
(Python 3.11 + numpy; also runs inside Blender, which ships numpy):

  python Tools/blender/divebar/cue_sweep_check.py [--report Docs/images/divebar/db1/cue_sweep_report.txt] [--quick]

The model is the engine's (2.5 "Sweep model = the engine's", R-03), read from Art/DiveBar/layout.json:
  * samples: cue-ball centres on a 5 cm grid over the 7-ft playfield (40 x 20, inside the noses by the oversized cue ball's radius
    60.325 / 2 mm) x 72 shot directions (5 deg) = 57,600;
  * the cue: tip at the contact point on the cue ball, the body running back along the shot line at elevation
    max(4 deg, atan((rail top + 20 mm - contact z) / max(0.12 m, distance to 0.08 m past the nose))), the contact 15 mm above
    the ball centre when the rail term applies; tapered radius 6.5 -> 15.9 mm over the cue length + 1 mm margin; the swept body
    runs out to L + backswing with the butt radius beyond L (URbStrokeComponent::MaxBackswing 0.30 m; shortened 0.10 m);
    a capsule chain sampled every 2 cm against the 3D boxes and cylinders of layout.json "sweep_obstacles" (walls, doors, column
    C3 + shelf, jukebox, cue rack, chalkboard, back ledge, TV-2, lamp shades, ceiling);
  * body: a 0.20 m disc at 0.95 / 1.10 / 1.25 m behind the contact point must clear the obstacles flagged "body";
  * reach: the bridge point 0.20 m behind the ball more than 1.00 m inside the rail outline along the cue line.
Classes (first that fits): reach-limited; free (58 in, full backswing, stance clear); shortened backswing (58 in with >= 0.10 m);
52 in; 48 in (>= 0.10 m); body blocked (a cue fits, the stance does not); no cue fits level.
Prints the class shares against 2.5 (VDB-T3: +-0.5 points), the 58-in blockers, the perpendicular thresholds of 2.5 (ball-centre
distance from the nose for a free sweep, +-2 cm) and the foot-end facts, and the scripted cases that the in-engine check of
RawBreak.Functional.DiveBar.CueSweeps re-measures with RbCueClearance::SweepEnvironment. Exit code 1 when a check fails.
Owner: M2-A.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[3]

# Engine / equipment parameters (2.5; UE 5.5; EQP 8.3)
CUE_LENGTHS = {"58": 1.4732, "52": 1.3208, "48": 1.2192}
R_TIP, R_BUTT, MARGIN = 0.0065, 0.0159, 0.001
FULL, SHORT = 0.30, 0.10
BASE_ELEV = math.radians(4.0)
STEP = 0.02
BODY_R, BODY_D = 0.20, (0.95, 1.10, 1.25)
REACH_BRIDGE, REACH_MAX = 0.20, 1.00

# 2.5 targets
SPEC_SHARES = {"free": 68.1, "shortened": 4.9, "52": 2.7, "48": 1.1, "none": 0.3, "body": 0.4, "reach": 22.6}
SPEC_THRESHOLDS = {  # (obstacle, cue, backswing) -> ball-centre distance from the nose [m]
	("wall", "58", FULL): 0.432, ("wall", "52", FULL): 0.280, ("wall", "48", FULL): 0.174,
	("wall", "58", SHORT): 0.231, ("wall", "52", SHORT): 0.059, ("wall", "48", SHORT): 0.030,
	("rack", "58", FULL): 0.552, ("rack", "52", FULL): 0.400, ("rack", "48", FULL): 0.299,
	("rack", "58", SHORT): 0.352, ("rack", "52", SHORT): 0.198, ("rack", "48", SHORT): 0.083,
	("column", "58", FULL): 0.615, ("column", "52", FULL): 0.463, ("column", "48", FULL): 0.362,
	("column", "58", SHORT): 0.415, ("column", "52", SHORT): 0.263, ("column", "48", SHORT): 0.157,
}
SPEC_FOOT_FROZEN_BACKSWING = 0.224


class Venue:
	def __init__(self, layout: dict):
		t = layout["tables"][0]
		self.cx, self.cy = t["location_m"][0], t["location_m"][1]
		self.bed = t["bed_height_m"]
		(nx0, nx1), (ny0, ny1) = t["noses"]
		(ox0, ox1), (oy0, oy1) = t["outer"]
		self.hl, self.hw = (nx1 - nx0) / 2, (ny1 - ny0) / 2          # nose half extents (core)
		self.ol, self.ow = (ox1 - ox0) / 2, (oy1 - oy0) / 2          # outer half extents
		self.rail_top = t["rail_top_z_m"] - self.bed                 # above the bed
		self.R = 0.060325 / 2                                          # oversized cue ball
		obs = layout["sweep_obstacles"]
		self.boxes = []  # (id, lo[3], hi[3], body) in CORE frame (x = V x - cx, y = -(V y - cy), z above the bed)
		for b in obs["boxes"]:
			(x0, x1), (y0, y1), (z0, z1) = b["box"]
			lo = np.array([x0 - self.cx, -(y1 - self.cy), z0 - self.bed])
			hi = np.array([x1 - self.cx, -(y0 - self.cy), z1 - self.bed])
			self.boxes.append((b["id"], lo, hi, bool(b.get("body", True))))
		self.cyls = []  # (id, centre xy, radius, z0, z1, body)
		for c in obs["cylinders"]:
			self.cyls.append((c["id"], np.array([c["center"][0] - self.cx, -(c["center"][1] - self.cy)]), c["radius"], c["z"][0] - self.bed,
				c["z"][1] - self.bed, bool(c.get("body", True))))
		if obs.get("lamp_shades"):
			lamp = layout["lamp"]
			yaw = math.radians(lamp["yaw_deg"])
			lx, ly = lamp["centre"]
			for k in (-1, 0, 1):
				x = lx + k * lamp["shade_spacing"] * math.cos(yaw)
				y = ly + k * lamp["shade_spacing"] * math.sin(yaw)
				self.cyls.append((f"shade{k + 1}", np.array([x - self.cx, -(y - self.cy)]), lamp["shade_diameter"] / 2,
					lamp["shade_bottom_z"] - self.bed, lamp["shade_bottom_z"] + lamp["shade_depth"] - self.bed, False))

	# --- geometry ------------------------------------------------------------------------------------------------------

	def dist_to_nose(self, p, d):
		"""Distance from p (ball region, inside the noses) backward along -d (unit, plan) to the nose rectangle."""
		best = np.full(p.shape[0], np.inf)
		for axis, half in ((0, self.hl), (1, self.hw)):
			comp = -d[:, axis]
			with np.errstate(divide="ignore", invalid="ignore"):
				t_pos = np.where(comp > 1e-9, (half - p[:, axis]) / comp, np.inf)
				t_neg = np.where(comp < -1e-9, (-half - p[:, axis]) / comp, np.inf)
			best = np.minimum(best, np.minimum(t_pos, t_neg))
		return best

	def dist_inside_outline(self, p, d):
		"""Distance from p backward along -d to the rail outline (0 when p is outside it)."""
		inside = (np.abs(p[:, 0]) <= self.ol) & (np.abs(p[:, 1]) <= self.ow)
		best = np.full(p.shape[0], np.inf)
		for axis, half in ((0, self.ol), (1, self.ow)):
			comp = -d[:, axis]
			with np.errstate(divide="ignore", invalid="ignore"):
				t_pos = np.where(comp > 1e-9, (half - p[:, axis]) / comp, np.inf)
				t_neg = np.where(comp < -1e-9, (-half - p[:, axis]) / comp, np.inf)
			best = np.minimum(best, np.minimum(t_pos, t_neg))
		return np.where(inside, best, 0.0)

	def pose(self, ball, d):
		"""Contact point (3D) and elevation per sample: ball (N, 2) plan centres, d (N, 2) unit shot directions."""
		contact_xy = ball - d * self.R
		dn = self.dist_to_nose(contact_xy, d)
		run = np.maximum(0.12, dn + 0.08)
		rise = self.rail_top + 0.020 - (self.R + 0.015)
		rail = np.arctan2(rise, run)
		applies = rail > BASE_ELEV
		elev = np.where(applies, rail, BASE_ELEV)
		cz = np.where(applies, self.R + 0.015, self.R)
		return contact_xy, cz, elev

	def blocked(self, contact_xy, cz, elev, d, length, backswing, per_obstacle=False):
		"""True where the swept cue (capsule chain, 2 cm) hits an obstacle. per_obstacle: dict id -> bool array."""
		smax = length + backswing
		s = np.linspace(0.0, smax, int(math.ceil(smax / STEP)) + 1)  # every <= 2 cm, the butt end included
		r = R_TIP + (R_BUTT - R_TIP) * np.minimum(s, length) / length + MARGIN          # (S,)
		ce, se = np.cos(elev)[:, None], np.sin(elev)[:, None]
		px = contact_xy[:, 0:1] - d[:, 0:1] * ce * s[None, :]
		py = contact_xy[:, 1:2] - d[:, 1:2] * ce * s[None, :]
		pz = cz[:, None] + se * s[None, :]
		hit = np.zeros(contact_xy.shape[0], dtype=bool)
		out = {}
		for oid, lo, hi, _ in self.boxes:
			dx = np.maximum(np.maximum(lo[0] - px, px - hi[0]), 0.0)
			dy = np.maximum(np.maximum(lo[1] - py, py - hi[1]), 0.0)
			dz = np.maximum(np.maximum(lo[2] - pz, pz - hi[2]), 0.0)
			h = ((dx * dx + dy * dy + dz * dz) <= (r * r)[None, :]).any(axis=1)
			out[oid] = h
			hit |= h
		for oid, c, rad, z0, z1, _ in self.cyls:
			dr = np.maximum(np.hypot(px - c[0], py - c[1]) - rad, 0.0)
			dz = np.maximum(np.maximum(z0 - pz, pz - z1), 0.0)
			h = ((dr * dr + dz * dz) <= (r * r)[None, :]).any(axis=1)
			out[oid] = h
			hit |= h
		return (hit, out) if per_obstacle else hit

	def body_blocked(self, contact_xy, d, shift: float = 0.0):
		"""The stance disc at BODY_D behind the contact point (minus `shift`: a shorter cue brings the player closer)."""
		hit = np.zeros(contact_xy.shape[0], dtype=bool)
		for dist in (x - shift for x in BODY_D):
			c = contact_xy - d * dist
			for _, lo, hi, body in self.boxes:
				if not body:
					continue
				dx = np.maximum(np.maximum(lo[0] - c[:, 0], c[:, 0] - hi[0]), 0.0)
				dy = np.maximum(np.maximum(lo[1] - c[:, 1], c[:, 1] - hi[1]), 0.0)
				hit |= (dx * dx + dy * dy) < BODY_R * BODY_R
			for _, cc, rad, _, _, body in self.cyls:
				if body:
					hit |= np.hypot(c[:, 0] - cc[0], c[:, 1] - cc[1]) < BODY_R + rad
		return hit

	def reach_limited(self, ball, d):
		bridge = ball - d * REACH_BRIDGE
		return self.dist_inside_outline(bridge, d) > REACH_MAX


def samples(v: Venue, quick: bool = False):
	nx, ny = (40, 20) if not quick else (20, 10)
	step_x = 2 * (v.hl - v.R) / nx
	step_y = 2 * (v.hw - v.R) / ny
	xs = -(v.hl - v.R) + step_x * (np.arange(nx) + 0.5)
	ys = -(v.hw - v.R) + step_y * (np.arange(ny) + 0.5)
	angles = np.radians(np.arange(0, 360, 5 if not quick else 10))
	X, Y, A = np.meshgrid(xs, ys, angles, indexing="ij")
	ball = np.stack([X.ravel(), Y.ravel()], axis=1)
	d = np.stack([np.cos(A.ravel()), np.sin(A.ravel())], axis=1)
	return ball, d


def classify(v: Venue, ball, d):
	contact, cz, elev = v.pose(ball, d)
	reach = v.reach_limited(ball, d)
	# the stance follows the cue: with a shorter cue the player stands closer by the length difference (a 48-in cue at the wall
	# is played from where a 58-in cue could not be)
	body58 = v.body_blocked(contact, d)
	body52 = v.body_blocked(contact, d, CUE_LENGTHS["58"] - CUE_LENGTHS["52"])
	body48 = v.body_blocked(contact, d, CUE_LENGTHS["58"] - CUE_LENGTHS["48"])
	full58, per = v.blocked(contact, cz, elev, d, CUE_LENGTHS["58"], FULL, per_obstacle=True)
	short58 = v.blocked(contact, cz, elev, d, CUE_LENGTHS["58"], SHORT)
	s52 = v.blocked(contact, cz, elev, d, CUE_LENGTHS["52"], SHORT)
	s48 = v.blocked(contact, cz, elev, d, CUE_LENGTHS["48"], SHORT)
	cls = np.full(ball.shape[0], "none", dtype=object)
	fits_any = (~short58) | (~s52) | (~s48) | (~full58)
	cls[fits_any] = "body"
	cls[(~s48) & ~body48] = "48"
	cls[(~s52) & ~body52] = "52"
	cls[(~short58) & ~body58] = "shortened"
	cls[(~full58) & ~body58] = "free"
	cls[reach] = "reach"
	return cls, per, full58


def threshold(v: Venue, kind: str, cue: str, backswing: float) -> float:
	"""Perpendicular shot away from an obstacle: the smallest ball-centre distance from the nose with a clear sweep (bisection)."""
	if kind == "wall":      # right wall: V +Y = core -y; the cue points toward the wall, x beyond the cue rack (V X 14.60)
		x, sign = 14.60 - v.cx, -1.0
	elif kind == "rack":    # cue rack X 13.30 - 14.30: its middle
		x, sign = 13.80 - v.cx, -1.0
	else:                   # column C3 at the left side pocket: V -Y = core +y
		x, sign = 13.72 - v.cx, +1.0
	d = np.array([[0.0, -sign]])

	def clear(dist):
		ball = np.array([[x, sign * (v.hw - dist)]])
		contact, cz, elev = v.pose(ball, d)
		return not v.blocked(contact, cz, elev, d, CUE_LENGTHS[cue], backswing)[0]

	lo, hi = v.R, v.hw * 2 - v.R
	if clear(lo):
		return lo
	for _ in range(40):
		mid = 0.5 * (lo + hi)
		if clear(mid):
			hi = mid
		else:
			lo = mid
	return hi


def foot_frozen_backswing(v: Venue, off: float = 0.0) -> float:
	"""Ball on the long string at the foot end, `off` from frozen, shot toward the head: the longest clear backswing (58 in)."""
	ball = np.array([[v.hl - v.R - off, 0.0]])
	d = np.array([[-1.0, 0.0]])
	contact, cz, elev = v.pose(ball, d)
	lo, hi = 0.0, FULL
	if not v.blocked(contact, cz, elev, d, CUE_LENGTHS["58"], hi)[0]:
		return hi
	for _ in range(40):
		mid = 0.5 * (lo + hi)
		if v.blocked(contact, cz, elev, d, CUE_LENGTHS["58"], mid)[0]:
			hi = mid
		else:
			lo = mid
	return lo


def jukebox_case(v: Venue) -> dict:
	"""TS-4: the diagonal from the head-right corner pocket toward the table centre, shot away from the jukebox (58 in, full)."""
	corner = np.array([-v.hl, -v.hw])           # head (core -x), right (core -y: the wall side)
	u = -corner / np.linalg.norm(corner)          # toward the centre
	d = u[None, :]

	def clear(t):
		ball = (corner + u * t)[None, :]
		contact, cz, elev = v.pose(ball, d)
		return not v.blocked(contact, cz, elev, d, CUE_LENGTHS["58"], FULL)[0]
	lo, hi = v.R * 1.5, 1.0
	for _ in range(40):
		mid = 0.5 * (lo + hi)
		if clear(mid):
			hi = mid
		else:
			lo = mid
	return {"corner_core": corner.tolist(), "direction_core": u.tolist(), "threshold_m": round(hi, 4)}


# --- DB-1: the plan of 2.4 rendered from layout.json (V10 must match it) ------------------------------------------------------------


def plan_rows(L: dict) -> list:
	"""2.4's ASCII plan sampled from layout.json at cell centres: 1 column = 0.25 m in X (column k covers X 0.25 (k - 1) .. 0.25 k),
	1 row = 0.20 m in Y (rows at Y -0.10, 0.10, ... 7.50); the legend of 2.4, first match wins."""
	els = {e["id"]: e for e in L["elements"]}
	ops = {o["id"]: o for o in L["openings"]}
	sh, rear = L["shell"], L["shell"]["rear"]
	X1, Y1 = sh["main_room"]["x"][1], sh["main_room"]["y"][1]
	t = L["tables"][0]

	def inb(x, y, box, eps=1e-6):  # closed boxes; eps: row / column centres on a box edge (Y 6.10 = the rail outline) count as inside
		(x0, x1), (y0, y1) = box[0], box[1]
		return x0 - eps <= x <= x1 + eps and y0 - eps <= y <= y1 + eps

	def cell(x, y):
		if x < 0.0:  # front wall with the storefront and the door
			if inb(0, y, [[0, 0], ops["E01"]["y"]]):
				return "G"
			if inb(0, y, [[0, 0], ops["E02"]["y"]]):
				return "E"
			return "#"
		if x > rear["outer_wall_x"][1]:  # outside, beyond the rear addition's outer wall (2.4: blank; the left party-wall row runs on)
			return "#" if y < 0.0 else " "
		if y < 0.0 or (y > Y1 and x <= X1 + 0.2) or y > rear["right_wall_y"][1]:
			return "#"
		if X1 <= x <= rear["x"][0]:  # back wall with the corridor opening and the storage door
			if inb(0, y, [[0, 0], ops["E18"]["y"]]):
				return "="
			if inb(0, y, [[0, 0], ops["E19"]["y"]]):
				return "s"
			return "#"
		if x > rear["x"][0]:  # the 1961 rear addition
			if x > rear["x"][1]:
				return "X" if inb(0, y, [[0, 0], ops["E21"]["y"]]) else "#"
			if y < rear["corridor"]["y"][0] or y > rear["right_wall_y"][0]:
				return "#"
			if y <= rear["corridor"]["y"][1]:
				return "="
			for p in rear["partitions"]:
				if inb(x, y, p["box"]):
					if p["id"] == "corridor_wall" and any(inb(x, 0, [ops[d]["x"], [0, 0]]) for d in ("E20m", "E20w")):
						return "d"
					return "#"
			for key, ch in (("mens", "m"), ("womens", "w"), ("keg_cooler", "g")):
				r = rear["rooms"][key]
				if inb(x, y, [r["x"], r["y"]]):
					return ch
			return "#"
		# main room
		for c in sh["columns"]:
			if math.hypot(x - c["x"], y - c["y"]) <= 0.125:
				return "C"
		shelf = els["E15"]
		if math.hypot(x - shelf["center"][0], y - shelf["center"][1]) <= shelf["radius"]:
			return "c"
		if inb(x, y, t["noses"]):
			return "."
		if inb(x, y, t["outer"]):
			return "T"
		for eid, ch in (("E03", "A"), ("E04", "b"), ("E06", "B"), ("E09", "D"), ("E10", "J"), ("E11", "k"), ("E12", "K"), ("E16", "L"), ("E17", "L"),
				("E09t", "-")):
			box = els[eid]["box"]
			if eid in ("E11", "E12"):  # wall items: 0.2 m deep in the plan
				box = [box[0], [Y1 - 0.2, Y1]]
			if eid == "E09t":
				box = [box[0], [box[1][0] - 0.06, box[1][1] + 0.06]]
			if inb(x, y, box):
				return ch
		e7 = els["E07"]
		arr = e7["array"]
		for i in range(arr["count"]):
			if math.hypot(x - (arr["x0"] + arr["dx"] * i), y - arr["y"]) <= 0.19:
				return "o"
		for px, py in els["E17s"]["points"]:
			if math.hypot(x - px, y - py) <= 0.19:
				return "o"
		b = els["E08"]
		if b["y"][0] <= y <= b["y"][1] and b["sets"][0][0] <= x <= b["sets"][-1][1]:
			for x0, x1 in b["sets"]:
				cx, tw = 0.5 * (x0 + x1), b["table"]["size"][0]
				if abs(x - cx) <= tw / 2:
					return "t"
			return "H"
		return " "

	rows = []
	for i in range(39):
		y = -0.10 + 0.20 * i
		rows.append(("{:5.2f} ".format(y)) + "".join(cell(0.25 * k - 0.125, y) for k in range(83)))  # 2.4: 83 columns, X to 20.50
	return rows


def spec_plan_rows() -> list:
	text = (REPO / "Docs" / "specs" / "venue-dive-bar.md").read_text(encoding="utf-8")
	block = text.split("### 2.4", 1)[1].split("```", 2)[1]
	return [line for line in block.splitlines() if line[:6].strip().replace(".", "").replace("-", "").isdigit()]


def plan_check(L: dict, out: str | None) -> int:
	mine, spec = plan_rows(L), spec_plan_rows()
	total = diff = 0
	lines = ["DB-1 plan check: venue-dive-bar 2.4 (spec) vs the plan rendered from Art/DiveBar/layout.json (1 column = 0.25 m in X, 1 row = 0.20 m in Y)", ""]
	for m, s in zip(mine, spec):
		mc, sc = m[6:].rstrip(), s[6:].rstrip()
		width = max(len(mc), len(sc))
		marks = "".join("^" if (mc[k] if k < len(mc) else " ") != (sc[k] if k < len(sc) else " ") else " " for k in range(width))
		total += width
		diff += marks.count("^")
		lines.append("spec   " + s)
		lines.append("layout " + m)
		if marks.strip():
			lines.append("       " + " " * 6 + marks)
	share = 100.0 * diff / max(1, total)
	lines += ["", f"{diff} of {total} cells differ ({share:.2f} %)"]
	text = "\n".join(lines) + "\n"
	print("\n".join(lines[-1:]))
	if out:
		Path(out).parent.mkdir(parents=True, exist_ok=True)
		Path(out).write_text(text, encoding="utf-8")
	return 0 if share <= 2.0 else 1


def main() -> int:
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("--report", default=str(REPO / "Docs" / "images" / "divebar" / "db1" / "cue_sweep_report.txt"))
	p.add_argument("--quick", action="store_true")
	p.add_argument("--plan", nargs="?", const=str(REPO / "Docs" / "images" / "divebar" / "db1" / "plan_check.txt"), default=None,
		help="DB-1: render 2.4's plan from layout.json and compare it with the spec (report file)")
	a = p.parse_args()
	layout = json.loads((REPO / "Art" / "DiveBar" / "layout.json").read_text(encoding="utf-8"))
	if a.plan:
		return plan_check(layout, a.plan)
	v = Venue(layout)
	ball, d = samples(v, a.quick)
	cls, per, full58 = classify(v, ball, d)
	n = len(cls)
	lines = [f"VDB-T3 cue-sweep statistics (venue-dive-bar 2.5, engine sweep model) - {n} samples ({'quick' if a.quick else '5 cm x 5 deg'})", ""]
	ok = True
	lines.append("class                          share     2.5    diff")
	for k in ("free", "shortened", "52", "48", "none", "body", "reach"):
		share = 100.0 * float((cls == k).sum()) / n
		diff = share - SPEC_SHARES[k]
		good = abs(diff) <= 0.5
		ok &= good or a.quick
		lines.append(f"{k:28s} {share:6.2f} %  {SPEC_SHARES[k]:5.1f}  {diff:+5.2f} {'ok' if good else 'OUT'}")
	room = 100.0 * float(np.isin(cls, ["shortened", "52", "48", "none", "body"]).sum()) / n
	lines.append(f"{'room-shaped (sum)':28s} {room:6.2f} %  (2.5: about 9 %)")
	lines += ["", "58-in blocked with the full 0.30 m backswing, by blocker (overlapping; 2.5: right wall 6.1, cue rack 4.1, column 0.7, jukebox 0.5, back wall 0.3, back ledge 0.2, C3 shelf 0.1 %):"]
	for oid, h in sorted(per.items(), key=lambda kv: -kv[1].sum()):
		if h.any():
			lines.append(f"  {oid:14s} {100.0 * h.sum() / n:5.2f} %")
	lines += ["", "perpendicular thresholds: ball-centre distance from the nose for a free sweep (2.5, +-2 cm)"]
	thresholds = {}
	for (kind, cue, b), want in SPEC_THRESHOLDS.items():
		have = threshold(v, kind, cue, b)
		thresholds[f"{kind}_{cue}_{'full' if b == FULL else 'short'}"] = round(have, 4)
		good = abs(have - want) <= 0.02
		ok &= good
		lines.append(f"  {kind:6s} {cue} in, backswing {b:.2f} m: {have:.3f} m (2.5: {want:.3f}) {'ok' if good else 'OUT'}")
	frozen = foot_frozen_backswing(v, 0.0)
	off10 = foot_frozen_backswing(v, 0.10)
	good = abs(frozen - SPEC_FOOT_FROZEN_BACKSWING) <= 0.02 and off10 >= FULL - 1e-6
	ok &= good
	lines += ["", f"foot end: frozen ball, free backswing {frozen:.3f} m (2.5: 0.224); 0.10 m off the cushion: {off10:.3f} m (2.5: the full 0.30) "
		f"{'ok' if good else 'OUT'}"]
	juke = jukebox_case(v)
	lines.append(f"jukebox (TS-4): diagonal from the head-right corner, free 58-in full-backswing sweep from {juke['threshold_m']:.3f} m along the diagonal")
	cases = {"thresholds": thresholds, "foot_frozen_backswing_m": round(frozen, 4), "foot_off10_backswing_m": round(off10, 4), "jukebox": juke}
	lines += ["", "scripted cases for the in-engine check (RawBreak.Functional.DiveBar.CueSweeps):", json.dumps(cases, indent=1)]
	lines += ["", f"RESULT: {'OK' if ok else 'FAIL'}"]
	text = "\n".join(lines) + "\n"
	print(text)
	if a.report:
		Path(a.report).parent.mkdir(parents=True, exist_ok=True)
		Path(a.report).write_text(text, encoding="utf-8")
	return 0 if ok else 1


if __name__ == "__main__":
	sys.exit(main())
