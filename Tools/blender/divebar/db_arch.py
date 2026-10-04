"""Architecture of The Low Bridge Tavern from Art/DiveBar/layout.json (venue-dive-bar 2.2 / 2.3 / 5.2 M01, 13; Docs/ue-architecture.md
18.8 M2-A). Runs INSIDE Blender:

  python Tools/blender/rbbl.py run Tools/blender/divebar/db_arch.py [-- --only Arch_Walls ...]

Exports (Art/DiveBar/Export/Arch/<Asset>/, ue_folder Arch; architecture in VENUE coordinates, pivot at the venue origin, placed at
the origin by rb_make_divebar.py; the small repeated fixtures in LOCAL coordinates with their pivot at the ceiling / wall anchor):
  Arch_Floor       floor slab of the main room (VCT) and the 1961 rear addition (beige VCT); physical material PM_RbSurface_Vct
  Arch_Walls       front / party / back walls with their openings, the rear addition (corridor, restrooms, keg cooler), the 1970s
                   paneling (4 x 8 ft sheets) with chair rail in the pool room, plaster / painted brick / painted block by zone
                   (6.3), rubber cove base, the storefront sill
  Arch_Ceiling     2 x 4 ft lay-in tiles on a 24 mm T-bar grid at 2.74 m (1 in 12 lifted <= 8 mm, the missing tile over the bar,
                   cells left for troffers and diffusers), the beam soffit, the corridor's 2 x 2 ceiling at 2.44 m (tag RbDB_Ceiling)
  Arch_Plenum      the 1908 pressed tin at 3.35 m, joists, knob-and-tube wiring and dust above the missing tile (S9)
  Arch_Columns     steel lally columns C1-C3 with base and cap plates (the drink shelf on C3 is M2-B's)
  Arch_Doors       entrance door (steel, wired-glass vision panel, push bar, transom), storage door, restroom doors, back exit,
                   the cased corridor opening
  Arch_GlassBlock  the 1970s glass-block storefront infill (21 x 9 blocks of 203 mm) with the clear pane for the window neons
  Arch_Street      night-street backplate outside the storefront (emissive card, L34 sodium lamp)
  Troffer          2 x 4 ft troffer (3 x T8, prismatic lens), local, pivot at the ceiling plane centre
  HvacDiffuser     2 x 2 ft louvered diffuser, local, pivot at the ceiling plane centre
  ExitSign         double-faced LED EXIT sign (letters as geometry), local, pivot at the top centre
  GB_*             greybox primitives for elements whose real mesh does not exist yet (unit box, cylinder, lamp shade, fan blade)

Slot names are the venue material instances (M2-B's MI_DB_*; the importer uses an M2-A fallback until they exist). Every asset
gets a JSON (bounds, triangles, slots, acoustic surfaces with areas for the IR synthesis of audio.md, venue-dive-bar 10) and a
ledger row (source own). Deterministic (rb_bl.rng). Owner: M2-A.
"""

from __future__ import annotations

import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "common"))
import rb_bl  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

PACKAGE = "M2-A"
FOLDER = "Arch"

# Slot names (M2-B's instances where they exist; MI_DB_Paint_White / _GlassBlock / _Street / _Troffer_Lens are M2-A requests).
M_VCT = "MI_DB_VCT_Oxblood"
M_VCT_BEIGE = "MI_DB_VCT_Beige"
M_BRICK = "MI_DB_Brick_PaintedGreen"
M_PLASTER_CREAM = "MI_DB_Plaster_Cream"
M_PLASTER_OX = "MI_DB_Plaster_Oxblood"
M_BLOCK = "MI_DB_Block_Painted"
M_PANEL = "MI_DB_Paneling_Dark"
M_WOOD = "MI_DB_Wood_Stained"
M_RUBBER = "MI_DB_Rubber_Black"
M_TILE = "MI_DB_CeilingTile"
M_WHITE = "MI_DB_Paint_White"
M_TIN = "MI_DB_Tin_Painted"
M_BLACKSTEEL = "MI_DB_Paint_BlackSteel"
M_STEEL = "MI_DB_Steel_Black"
M_CABINET = "MI_DB_Paint_Cabinet"
M_ALU = "MI_DB_Alu_Trim"
M_STAINLESS = "MI_DB_Steel_Stainless"
M_GLASS = "MI_DB_Glass_Clear"
M_GLASSBLOCK = "MI_DB_GlassBlock"
M_MORTAR = "MI_DB_Mortar"
M_STREET = "MI_DB_Street_Backplate"
M_LENS = "MI_DB_Troffer_Lens"
M_EXIT = "MI_DB_Emissive_Exit"
M_PLYWOOD = "MI_DB_Plywood_Painted"
M_ZINC = "MI_DB_Steel_Zinc"
M_CERAMIC = "MI_DB_Paint_White"

EPS = 1e-6


# ------------------------------------------------------------------------------------------------------------------------------
# helpers
# ------------------------------------------------------------------------------------------------------------------------------


def quad(b: rb_bl.Builder, pts, normal, mat: str) -> None:
	"""Quad (or polygon) with the winding chosen so its normal points along `normal`."""
	p = [Vector(x) for x in pts]
	n = (p[1] - p[0]).cross(p[2] - p[0])
	if n.dot(Vector(normal)) < 0.0:
		p = list(reversed(p))
	b.poly([tuple(v) for v in p], mat)


def wall_point(axis: str, u: float, z: float, n: float):
	"""axis 'x': the wall's normal is X (plane y-z, u = y); 'y': normal Y (plane x-z, u = x)."""
	return (n, u, z) if axis == "x" else (u, n, z)


def wall_normal(axis: str, sign: float):
	return (sign, 0.0, 0.0) if axis == "x" else (0.0, sign, 0.0)


class Wall:
	"""A straight wall slab between the planes n_in (room face) and n_out, over u_range x z_range, minus rectangular openings
	(u0, u1, z0, z1). Emits only exposed faces (room face, back face, jambs / heads / sills / ends / top) of a non-uniform cell grid,
	so band splits (material zones) never create hidden faces. mat_in(u, z) picks the room-face material per cell."""

	def __init__(self, axis, n_in, n_out, u_range, z_range, openings=(), splits_u=(), splits_z=(), mat_in=None, mat_out=M_BRICK,
			mat_edge=None, skip_bottom=True, acoustic="brick_painted"):
		self.axis, self.n_in, self.n_out = axis, n_in, n_out
		self.u_range, self.z_range = u_range, z_range
		self.openings = list(openings)
		self.mat_in = mat_in or (lambda u, z: M_BRICK)
		self.mat_out = mat_out
		self.mat_edge = mat_edge
		self.skip_bottom = skip_bottom
		self.acoustic = acoustic
		us = {u_range[0], u_range[1]} | {v for o in self.openings for v in (o[0], o[1])} | set(splits_u)
		zs = {z_range[0], z_range[1]} | {v for o in self.openings for v in (o[2], o[3])} | set(splits_z)
		self.us = sorted(u for u in us if u_range[0] - EPS <= u <= u_range[1] + EPS)
		self.zs = sorted(z for z in zs if z_range[0] - EPS <= z <= z_range[1] + EPS)
		nu, nz = len(self.us) - 1, len(self.zs) - 1
		self.filled = [[self._solid(i, k) for k in range(nz)] for i in range(nu)]

	def _solid(self, i: int, k: int) -> bool:
		uc = 0.5 * (self.us[i] + self.us[i + 1])
		zc = 0.5 * (self.zs[k] + self.zs[k + 1])
		return not any(o[0] < uc < o[1] and o[2] < zc < o[3] for o in self.openings)

	def room_area(self, pred=None) -> float:
		area = 0.0
		for i in range(len(self.us) - 1):
			for k in range(len(self.zs) - 1):
				if self.filled[i][k]:
					uc, zc = 0.5 * (self.us[i] + self.us[i + 1]), 0.5 * (self.zs[k] + self.zs[k + 1])
					if pred is None or pred(uc, zc):
						area += (self.us[i + 1] - self.us[i]) * (self.zs[k + 1] - self.zs[k])
		return area

	def emit(self, b: rb_bl.Builder) -> None:
		a = self.axis
		sign_in = 1.0 if self.n_in > self.n_out else -1.0  # the room face's normal sign along the wall axis
		nu, nz = len(self.us) - 1, len(self.zs) - 1
		for i in range(nu):
			for k in range(nz):
				if not self.filled[i][k]:
					continue
				u0, u1, z0, z1 = self.us[i], self.us[i + 1], self.zs[k], self.zs[k + 1]
				uc, zc = 0.5 * (u0 + u1), 0.5 * (z0 + z1)
				mi = self.mat_in(uc, zc)
				edge = self.mat_edge or mi
				P = lambda u, z, n: wall_point(a, u, z, n)  # noqa: E731
				quad(b, [P(u0, z0, self.n_in), P(u1, z0, self.n_in), P(u1, z1, self.n_in), P(u0, z1, self.n_in)], wall_normal(a, sign_in), mi)
				quad(b, [P(u0, z0, self.n_out), P(u1, z0, self.n_out), P(u1, z1, self.n_out), P(u0, z1, self.n_out)], wall_normal(a, -sign_in),
					self.mat_out)
				uvec = (0.0, 1.0, 0.0) if a == "x" else (1.0, 0.0, 0.0)
				if i == 0 or not self.filled[i - 1][k]:
					quad(b, [P(u0, z0, self.n_in), P(u0, z0, self.n_out), P(u0, z1, self.n_out), P(u0, z1, self.n_in)], tuple(-c for c in uvec), edge)
				if i == nu - 1 or not self.filled[i + 1][k]:
					quad(b, [P(u1, z0, self.n_in), P(u1, z0, self.n_out), P(u1, z1, self.n_out), P(u1, z1, self.n_in)], uvec, edge)
				if (k == 0 and not self.skip_bottom) or (k > 0 and not self.filled[i][k - 1]):
					quad(b, [P(u0, z0, self.n_in), P(u1, z0, self.n_in), P(u1, z0, self.n_out), P(u0, z0, self.n_out)], (0.0, 0.0, -1.0), edge)
				if k == nz - 1 or not self.filled[i][k + 1]:
					quad(b, [P(u0, z1, self.n_in), P(u1, z1, self.n_in), P(u1, z1, self.n_out), P(u0, z1, self.n_out)], (0.0, 0.0, 1.0), edge)

	def collision(self, col: rb_bl.Collision) -> None:
		"""Convex boxes over the solid cells (merged along z per u column)."""
		nu, nz = len(self.us) - 1, len(self.zs) - 1
		lo_n, hi_n = min(self.n_in, self.n_out), max(self.n_in, self.n_out)
		for i in range(nu):
			k = 0
			while k < nz:
				if not self.filled[i][k]:
					k += 1
					continue
				k0 = k
				while k < nz and self.filled[i][k]:
					k += 1
				u0, u1, z0, z1 = self.us[i], self.us[i + 1], self.zs[k0], self.zs[k]
				if self.axis == "x":
					col.box(lo_n, hi_n, u0, u1, z0, z1)
				else:
					col.box(u0, u1, lo_n, hi_n, z0, z1)


def box_faces_area(size) -> float:
	x, y, z = size
	return 2.0 * (x * y + y * z + x * z)


def meta_base(family: str, notes: str, **extra) -> dict:
	m = {"family": family, "ue_folder": FOLDER, "profile": "RbVenueBlock", "priority": "mid", "pivot": "venue origin", "frame": "venue",
		"owner": PACKAGE, "notes": notes, "generator": "db_arch.py"}
	m.update(extra)
	return m


def export(asset: str, objects, out, meta, collision=None, target=None, tol=0.01):
	if rb_bl.args().only and asset not in rb_bl.args().only:
		return
	rb_bl.export_asset(asset, objects, pathlib.Path(out) / FOLDER, meta, collision=collision, target_m=target, tolerance_m=tol, package=PACKAGE)


def want(asset: str) -> bool:
	only = rb_bl.args().only
	return not only or asset in only


# ------------------------------------------------------------------------------------------------------------------------------
# floor
# ------------------------------------------------------------------------------------------------------------------------------


def build_floor(L: dict, out) -> None:
	if not want("Arch_Floor"):
		return
	rb_bl.reset_scene()
	s = L["shell"]
	x1, y1 = s["main_room"]["x"][1], s["main_room"]["y"][1]
	t = s["floor"]["thickness"]
	rear = s["rear"]
	b = rb_bl.Builder("Arch_Floor")
	# Main room top (VCT; M_DB_Floor draws the 12 in checker, seams, the replaced patch and the traffic lanes from world XY).
	# Subdivided into 1.22 m strips so Nanite / Lumen cards stay well conditioned on a 16 x 7 m face.
	xs = [0.0] + [v for v in frange(1.22, x1, 1.22)] + [x1]
	ys = [0.0] + [v for v in frange(1.22, y1, 1.22)] + [y1]
	for i in range(len(xs) - 1):
		for j in range(len(ys) - 1):
			quad(b, [(xs[i], ys[j], 0.0), (xs[i + 1], ys[j], 0.0), (xs[i + 1], ys[j + 1], 0.0), (xs[i], ys[j + 1], 0.0)], (0, 0, 1), M_VCT)
	# Door thresholds through the wall thickness: entrance (aluminium saddle), corridor opening, storage door (VCT / beige).
	e = {o["id"]: o for o in L["openings"]}
	d = e["E02"]
	b.box(-0.33, 0.0, d["y"][0], d["y"][1], -0.01, 0.006, M_ALU, skip=("-z",))
	for oid, mat in (("E18", M_VCT_BEIGE), ("E19", M_VCT)):
		o = e[oid]
		quad(b, [(16.46, o["y"][0], 0.0), (16.66, o["y"][0], 0.0), (16.66, o["y"][1], 0.0), (16.46, o["y"][1], 0.0)], (0, 0, 1), mat)
	# Rear addition (1961): beige VCT in the corridor, restrooms and the keg cooler (behind closed doors).
	rx0, rx1 = rear["x"]
	for room in (rear["corridor"], rear["rooms"]["mens"], rear["rooms"]["womens"], rear["rooms"]["keg_cooler"]):
		quad(b, [(rx0, room["y"][0], 0.0), (rx1, room["y"][0], 0.0), (rx1, room["y"][1], 0.0), (rx0, room["y"][1], 0.0)], (0, 0, 1), M_VCT_BEIGE)
	# Slab body (sides / bottom, never seen) under everything.
	b.box(-0.33, 20.32, -0.33, 7.65, -t, -0.0005, M_VCT_BEIGE, skip=("+z",))
	obj = b.build(uv_world=True)
	col = rb_bl.Collision("SM_DB_Arch_Floor")
	col.box(-0.33, 20.32, -0.33, 7.65, -t, 0.0)
	area_main = x1 * y1
	area_rear = (rx1 - rx0) * (7.12 - 0.30)
	meta = meta_base("Architecture", "floor slab: VCT on concrete (main room), beige VCT (rear addition)",
		phys_material=s["floor"]["phys_material"], acoustic_material="vct_on_concrete",
		acoustic_surfaces=[{"material": "vct_on_concrete", "area_m2": round(area_main + area_rear, 2), "zone": "all"}])
	export("Arch_Floor", [obj], out, meta, collision=col)


def frange(a: float, b: float, step: float):
	v = a
	while v < b - 1e-6:
		yield round(v, 6)
		v += step


# ------------------------------------------------------------------------------------------------------------------------------
# walls
# ------------------------------------------------------------------------------------------------------------------------------


def build_walls(L: dict, out) -> None:
	if not want("Arch_Walls"):
		return
	rb_bl.reset_scene()
	s = L["shell"]
	X1, Y1 = s["main_room"]["x"][1], s["main_room"]["y"][1]
	ZT = s["structure_z"]
	ZC = s["ceiling_z"]
	up = s["upper_walls"]
	zb = up["z"][0]                    # 1.22: paneling / wainscot top
	bar_x = up["bar_zone_x"][1]        # 10.10: bar zone | pool room
	o = {op["id"]: op for op in L["openings"]}
	b = rb_bl.Builder("Arch_Walls")
	col = rb_bl.Collision("SM_DB_Arch_Walls")
	walls = []

	# Front wall (x -0.33 .. 0): painted green brick, glass-block opening E01, entrance E02 + transom.
	e01, e02 = o["E01"], o["E02"]
	front = Wall("x", 0.0, -0.33, (-0.33, Y1 + 0.33), (0.0, ZT), openings=[
		(e01["y"][0], e01["y"][1], e01["z"][0], e01["z"][1]),
		(e02["y"][0], e02["y"][1], 0.0, e02["transom"]["z"][1]),
	], splits_z=(ZC,), mat_in=lambda u, z: M_BRICK, mat_out=M_BRICK)
	walls.append(("front", front, "brick_painted"))

	# Left party wall (y -0.33 .. 0): bar zone brick below the wainscot line / cream plaster above; pool room oxblood plaster
	# above the paneling (the paneling covers the wall below 1.22 m).
	def left_right_mat(u, z):
		if z > ZC:
			return M_BRICK
		if u < bar_x:
			return M_BRICK if z < zb else M_PLASTER_CREAM
		return M_PLASTER_OX

	left = Wall("y", 0.0, -0.33, (0.0, X1), (0.0, ZT), splits_u=(bar_x,), splits_z=(zb, ZC), mat_in=left_right_mat)
	walls.append(("left", left, "brick_painted"))

	# Right party wall (y 7.32 .. 7.65): painted brick full height in the front room (booths, dart machine, Polaroid wall),
	# paneling + oxblood plaster in the pool room.
	def right_mat(u, z):
		if z > ZC or u < bar_x:
			return M_BRICK
		return M_PLASTER_OX

	right = Wall("y", Y1, Y1 + 0.33, (0.0, X1), (0.0, ZT), splits_u=(bar_x,), splits_z=(zb, ZC), mat_in=right_mat)
	walls.append(("right", right, "brick_painted"))

	# Back wall (x 16.46 .. 16.66, 1961 painted block): corridor opening E18, storage door E19; paneling below 1.22 m on the room side.
	e18, e19 = o["E18"], o["E19"]
	back = Wall("x", 16.46, 16.66, (-0.33, Y1 + 0.33), (0.0, ZT), openings=[
		(e18["y"][0], e18["y"][1], 0.0, e18["z"][1]),
		(e19["y"][0], e19["y"][1], 0.0, e19["z"][1]),
	], splits_z=(zb, ZC), mat_in=lambda u, z: M_BLOCK, mat_out=M_BLOCK, acoustic="block_painted")
	walls.append(("back", back, "block_painted"))

	# Rear addition (1961, painted block, ceiling 2.44 m): corridor walls, restroom / keg partitions, outer wall with the back exit.
	rear = s["rear"]
	rz = rear["ceiling_z"]
	rx0, rx1 = rear["x"]
	e21 = o["E21"]
	walls.append(("rear_left", Wall("y", 0.30, -0.33, (16.66, 20.32), (0.0, rz + 0.30), mat_in=lambda u, z: M_BLOCK, mat_out=M_BRICK), "block_painted"))
	walls.append(("rear_right", Wall("y", 7.12, 7.65, (16.66, 20.32), (0.0, rz + 0.30), mat_in=lambda u, z: M_BLOCK, mat_out=M_BRICK), "block_painted"))
	walls.append(("rear_outer", Wall("x", 20.12, 20.32, (0.30, 7.12), (0.0, rz + 0.30), openings=[(e21["y"][0], e21["y"][1], 0.0, e21["z"][1])],
		mat_in=lambda u, z: M_BLOCK, mat_out=M_BRICK), "block_painted"))
	doors_corr = [op for op in L["openings"] if op.get("wall") == "corridor_wall"]
	walls.append(("corridor_wall", Wall("y", 1.40, 1.52, (rx0, rx1), (0.0, rz), openings=[(d["x"][0], d["x"][1], 0.0, d["z"][1]) for d in doors_corr],
		mat_in=lambda u, z: M_BLOCK, mat_out=M_BLOCK), "block_painted"))
	for part in rear["partitions"]:
		if part["id"] == "corridor_wall":
			continue
		(px0, px1), (py0, py1), (pz0, pz1) = part["box"]
		b.box(px0, px1, py0, py1, pz0, pz1, M_BLOCK)
		col.box(px0, px1, py0, py1, pz0, pz1)
	# Rear roof slab over the whole addition (closes the plenum; never seen).
	b.box(16.66, 20.12, 0.30, 7.12, rz + 0.30, rz + 0.40, M_BLOCK)

	areas = []
	for name, w, acoustic in walls:
		w.emit(b)
		w.collision(col)
		areas.append({"wall": name, "material": acoustic, "area_m2": round(w.room_area(lambda u, z: z <= ZC), 2)})

	# --- pool-room paneling (1970s grooved 4 x 8 ft sheets, M_DB_Opaque PatternMode 1 draws the grooves) + chair rail ------------
	r = rb_bl.rng("Arch_Paneling", 0, 1958)
	pz0, pz1 = 0.0, zb
	thick = 0.006
	sheet = 1.22
	gap = 0.0015

	def panel_run(axis, n_face, direction, u0, u1, cut=()):
		"""Sheets from u0 to u1 standing proud of the wall face n_face (direction = +1 / -1 toward the room)."""
		u = u0
		while u < u1 - 1e-4:
			ue = min(u + sheet, u1)
			for (c0, c1) in cut:
				if u < c0 < ue:
					ue = c0
			a0, a1 = u + gap / 2, ue - gap / 2
			inside = any(c0 - 1e-6 <= 0.5 * (a0 + a1) <= c1 + 1e-6 for c0, c1 in cut)
			if a1 - a0 > 0.01 and not inside:
				bow = rb_bl.jitter(r, 0.0015)
				n0, n1 = n_face, n_face + direction * (thick + abs(bow))
				if axis == "x":
					b.box(min(n0, n1), max(n0, n1), a0, a1, pz0 + 0.002, pz1, M_PANEL)
				else:
					b.box(a0, a1, min(n0, n1), max(n0, n1), pz0 + 0.002, pz1, M_PANEL)
			u = ue
			for (c0, c1) in cut:
				if abs(u - c0) < 1e-6:
					u = c1
	# left wall (y = 0, room +Y), right wall (y = 7.32, room -Y), back wall (x = 16.46, room -X)
	panel_run("y", 0.0, +1, bar_x, X1)
	panel_run("y", Y1, -1, bar_x, X1)
	panel_run("x", 16.46, -1, 0.0, Y1, cut=[(e18["y"][0] - 0.07, e18["y"][1] + 0.07), (e19["y"][0] - 0.05, e19["y"][1] + 0.05)])
	# Chair rail (wood cap) along the paneling top.
	cr0, cr1, crd = zb - 0.012, zb + 0.030, 0.022

	def rail(axis, n_face, direction, u0, u1):
		n1 = n_face + direction * crd
		if axis == "x":
			b.box(min(n_face, n1), max(n_face, n1), u0, u1, cr0, cr1, M_WOOD)
		else:
			b.box(u0, u1, min(n_face, n1), max(n_face, n1), cr0, cr1, M_WOOD)
	rail("y", 0.0, +1, bar_x, X1)
	rail("y", Y1, -1, bar_x, X1)
	rail("x", 16.46, -1, 0.0, e18["y"][0] - 0.07)
	rail("x", 16.46, -1, e18["y"][1] + 0.07, e19["y"][0] - 0.05)
	rail("x", 16.46, -1, e19["y"][1] + 0.05, Y1)
	# Paneling end trims at the bar-zone boundary (x = 10.10) on both party walls.
	b.box(bar_x - 0.018, bar_x + 0.004, 0.0, 0.012, 0.0, zb + 0.03, M_WOOD)
	b.box(bar_x - 0.018, bar_x + 0.004, Y1 - 0.012, Y1, 0.0, zb + 0.03, M_WOOD)

	# --- rubber cove base (0.10 m) on every room face at the floor, cut at the openings ------------------------------------------
	bh, bd = s["baseboard"]["height"], 0.004

	def base(axis, n_face, direction, u0, u1, cuts=()):
		segs = [(u0, u1)]
		for c0, c1 in cuts:
			nxt = []
			for a0, a1 in segs:
				if c1 <= a0 or c0 >= a1:
					nxt.append((a0, a1))
				else:
					if c0 > a0:
						nxt.append((a0, c0))
					if c1 < a1:
						nxt.append((c1, a1))
			segs = nxt
		for a0, a1 in segs:
			n1 = n_face + direction * bd
			if axis == "x":
				b.box(min(n_face, n1), max(n_face, n1), a0, a1, 0.0, bh, M_RUBBER, skip=("-z",))
			else:
				b.box(a0, a1, min(n_face, n1), max(n_face, n1), 0.0, bh, M_RUBBER, skip=("-z",))
	pt = thick + 0.002
	base("x", 0.0, +1, 0.0, Y1, cuts=[(e02["y"][0], e02["y"][1])])
	base("y", 0.0, +1, 0.0, bar_x)
	base("y", pt, +1, bar_x, X1)
	base("y", Y1, -1, 0.0, bar_x)
	base("y", Y1 - pt, -1, bar_x, X1)
	base("x", 16.46 - pt, -1, 0.0, Y1, cuts=[(e18["y"][0] - 0.07, e18["y"][1] + 0.07), (e19["y"][0] - 0.05, e19["y"][1] + 0.05)])
	base("y", 0.30, +1, 16.66, 20.12)
	base("y", 1.40, -1, 16.66, 20.12, cuts=[(d["x"][0] - 0.05, d["x"][1] + 0.05) for d in doors_corr])
	base("x", 20.12, -1, 0.30, 1.40, cuts=[(e21["y"][0] - 0.05, e21["y"][1] + 0.05)])

	# --- storefront sill (0.30 m deep painted wood) and the radiator niche's sill line ---------------------------------------------
	sill = e01.get("sill_depth", 0.30)
	sz = e01["z"][0]
	b.box(0.035 - sill, 0.035, e01["y"][0] - 0.03, e01["y"][1] + 0.03, sz - 0.03, sz + 0.003, M_WOOD)
	b.box(0.0, 0.015, e01["y"][0] - 0.01, e01["y"][1] + 0.01, sz - 0.12, sz - 0.03, M_WOOD, skip=("-x",))

	obj = b.build(uv_world=True)
	meta = meta_base("Architecture", "shell walls with openings, pool-room paneling, chair rail, cove base, storefront sill",
		acoustic_material="brick_painted", acoustic_surfaces=areas + [
			{"wall": "paneling", "material": "wood_panel", "area_m2": round((2 * (X1 - bar_x) + Y1 - 2.0) * zb, 2)}])
	export("Arch_Walls", [obj], out, meta, collision=col)


# ------------------------------------------------------------------------------------------------------------------------------
# ceiling
# ------------------------------------------------------------------------------------------------------------------------------


def ceiling_cells(L: dict):
	"""The 2 x 4 grid of the main room: list of (x0, x1, y0, y1, kind) with kind tile / missing / troffer / diffuser / half."""
	s = L["shell"]
	X1, Y1 = s["main_room"]["x"][1], s["main_room"]["y"][1]
	tx, ty = s["ceiling_grid"]["tile"]
	soffit = s["soffit"]["y"]
	void = s["pressed_tin"]["void"]
	elems = {e["id"]: e for e in L["elements"]}
	troffers = [(c[0], c[1]) for c in elems["E23"]["cells"]]
	diffusers = [(c[0], c[1]) for c in elems["M16d"]["cells"]]
	cells = []
	xs = list(frange(0.0, X1, tx)) + [X1]
	ys = list(frange(0.0, Y1, ty)) + [Y1]
	for i in range(len(xs) - 1):
		for j in range(len(ys) - 1):
			x0, x1, y0, y1 = xs[i], xs[i + 1], ys[j], ys[j + 1]
			# the soffit cuts the rows it crosses
			if y1 <= soffit[0] + EPS or y0 >= soffit[1] - EPS:
				pass
			elif y0 < soffit[0] < y1:
				y1 = soffit[0]
			elif y0 < soffit[1] < y1:
				y0 = soffit[1]
			else:
				continue
			cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
			if abs(x0 - void["x"][0]) < 1e-3 and abs(y0 - void["y"][0]) < 1e-3:
				cells.append((x0, x1, y0, y1, "missing"))
				continue
			if any(abs(cx - tx_) < 0.05 and abs(cy - ty_) < 0.05 for tx_, ty_ in troffers):
				cells.append((x0, x1, y0, y1, "troffer"))
				continue
			diff = [(dx, dy) for dx, dy in diffusers if x0 < dx < x1 and y0 < dy < y1]
			if diff:
				dx, dy = diff[0]
				xm = 0.5 * (x0 + x1)
				if dx > xm:
					cells.append((x0, xm, y0, y1, "half"))
					cells.append((xm, x1, y0, y1, "diffuser"))
				else:
					cells.append((x0, xm, y0, y1, "diffuser"))
					cells.append((xm, x1, y0, y1, "half"))
				continue
			cells.append((x0, x1, y0, y1, "tile"))
	return cells


def build_ceiling(L: dict, out) -> None:
	if not want("Arch_Ceiling"):
		return
	rb_bl.reset_scene()
	s = L["shell"]
	X1, Y1 = s["main_room"]["x"][1], s["main_room"]["y"][1]
	ZC = s["ceiling_z"]
	grid = s["ceiling_grid"]
	tw = grid["tbar_width"]
	flange = 0.004
	tile_t = 0.016
	b = rb_bl.Builder("Arch_Ceiling")
	r = rb_bl.rng("Arch_Ceiling", 0, 1958)
	cells = ceiling_cells(L)
	lamp = L["lamp"]
	lamp_x, lamp_y = lamp["centre"]
	count = {"tile": 0, "lifted": 0}
	for idx, (x0, x1, y0, y1, kind) in enumerate(cells):
		if kind not in ("tile", "half"):
			continue
		count["tile"] += 1
		inset = tw / 2 - 0.002
		z0 = ZC + flange
		lift = 0.0
		tilt = (0.0, 0.0)
		near_lamp = x0 - 0.1 < lamp_x + 0.7 and x1 + 0.1 > lamp_x - 0.7 and y0 < lamp_y + 0.3 and y1 > lamp_y - 0.3
		if (r.random() < 1.0 / 12.0 or near_lamp) and kind == "tile":
			lift = (0.002 + 0.006 * r.random()) if not near_lamp else 0.006
			tilt = (rb_bl.jitter(r, 0.004), rb_bl.jitter(r, 0.004))
			count["lifted"] += 1
		# a slightly sagging / tilted tile: corners at individual heights
		ax0, ax1, ay0, ay1 = x0 + inset, x1 - inset, y0 + inset, y1 - inset
		zc = [z0 + lift + max(0.0, tilt[0]) * (k % 2) + max(0.0, tilt[1]) * (k // 2) for k in range(4)]
		# bottom face (seen), top face, sides
		p = [(ax0, ay0, zc[0]), (ax1, ay0, zc[1]), (ax1, ay1, zc[3]), (ax0, ay1, zc[2])]
		quad(b, p, (0, 0, -1), M_TILE)
		quad(b, [(v[0], v[1], v[2] + tile_t) for v in p], (0, 0, 1), M_TILE)
		for a_, b_ in ((0, 1), (1, 2), (2, 3), (3, 0)):
			pa, pb = p[a_], p[b_]
			n = Vector((pb[1] - pa[1], -(pb[0] - pa[0]), 0.0))
			c = Vector(((ax0 + ax1) / 2, (ay0 + ay1) / 2, 0))
			mid = Vector(((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2, 0))
			if n.dot(mid - c) < 0:
				n = -n
			quad(b, [pa, pb, (pb[0], pb[1], pb[2] + tile_t), (pa[0], pa[1], pa[2] + tile_t)], tuple(n), M_TILE)
	# T-bar grid: mains along X every 0.61 m (y lines), cross tees every 1.22 m (x lines) + the half-cell tees at the diffusers,
	# visible flange 24 mm + a web above; wall angle along the perimeter and the soffit.
	tx, ty = grid["tile"]
	soffit = s["soffit"]["y"]
	web_h = 0.035

	def tee_x(x, y0, y1):   # a tee running along Y at x
		b.box(x - tw / 2, x + tw / 2, y0, y1, ZC, ZC + flange, M_WHITE, skip=("+z",))
		b.box(x - 0.003, x + 0.003, y0, y1, ZC + flange, ZC + web_h, M_WHITE)

	def tee_y(y, x0, x1):   # a tee running along X at y
		b.box(x0, x1, y - tw / 2, y + tw / 2, ZC, ZC + flange, M_WHITE, skip=("+z",))
		b.box(x0, x1, y - 0.003, y + 0.003, ZC + flange, ZC + web_h, M_WHITE)

	segments_y = [(0.0, soffit[0]), (soffit[1], Y1)]
	for yy in frange(ty, Y1, ty):
		if soffit[0] - 0.01 <= yy <= soffit[1] + 0.01:
			continue
		tee_y(yy, 0.0, X1)
	for xx in frange(tx, X1, tx):
		for y0, y1 in segments_y:
			tee_x(xx, y0, y1)
	# half-cell cross tees (diffuser cells)
	for (x0, x1, y0, y1, kind) in cells:
		if kind == "diffuser":
			for xe in (x0, x1):
				if abs((xe / tx) - round(xe / tx)) > 1e-3:
					tee_x(xe, y0, y1)
	# wall angle (L 24 mm) around the room and along the soffit
	wa = 0.022
	for (x0, x1, y0, y1) in ((0.0, X1, 0.0, wa), (0.0, X1, Y1 - wa, Y1), (0.0, wa, 0.0, Y1), (X1 - wa, X1, 0.0, Y1),
			(0.0, X1, soffit[0] - wa, soffit[0]), (0.0, X1, soffit[1], soffit[1] + wa)):
		b.box(x0, x1, y0, y1, ZC, ZC + 0.002, M_WHITE)
	# Beam soffit (drywall box, cream plaster) Y 3.48 - 3.84, Z 2.44 - 2.74, full depth.
	sz0 = s["soffit"]["z"][0]
	b.box(0.0, X1, soffit[0], soffit[1], sz0, ZC + 0.05, M_PLASTER_CREAM, skip=("+z",))
	# Plenum lid over the tiles (dark; the pressed tin of Arch_Plenum closes the void area above it anyway).
	# Rear corridor 2 x 2 ceiling at 2.44 m (tiles + grid), restrooms / keg: plain board.
	rear = s["rear"]
	rz = rear["ceiling_z"]
	cx0, cx1 = rear["x"]
	cy0, cy1 = rear["corridor"]["y"]
	for x0 in frange(cx0, cx1, 0.61):
		x1_ = min(x0 + 0.61, cx1)
		for y0 in frange(cy0, cy1, 0.61):
			y1_ = min(y0 + 0.61, cy1)
			b.box(x0 + 0.011, x1_ - 0.011, y0 + 0.011, y1_ - 0.011, rz + flange, rz + flange + tile_t, M_TILE)
	for x0 in frange(cx0 + 0.61, cx1, 0.61):
		b.box(x0 - tw / 2, x0 + tw / 2, cy0, cy1, rz, rz + flange, M_WHITE, skip=("+z",))
	b.box(cx0, cx1, cy0 + 0.61 - tw / 2, cy0 + 0.61 + tw / 2, rz, rz + flange, M_WHITE, skip=("+z",))
	b.box(cx0, cx1, 1.52, 7.12, rz, rz + 0.02, M_TILE)
	# The corridor opening's head (back wall) above the throat: the corridor ceiling sits under the back wall's lintel.
	obj = b.build(uv_world=True)
	col = rb_bl.Collision("SM_DB_Arch_Ceiling")
	col.box(0.0, X1, 0.0, Y1, ZC, ZC + 0.06)
	col.box(0.0, X1, soffit[0], soffit[1], sz0, ZC)
	meta = meta_base("Architecture", f"suspended ceiling: {count['tile']} lay-in tiles ({count['lifted']} lifted), grid, soffit, corridor ceiling",
		tags=["RbDB_Ceiling"], acoustic_material="ceiling_tile_mineral", cast_shadow=True,
		acoustic_surfaces=[{"material": "ceiling_tile_mineral", "area_m2": round(X1 * Y1 - X1 * (soffit[1] - soffit[0]), 2)},
			{"material": "gypsum", "area_m2": round(X1 * (soffit[1] - soffit[0] + 2 * (ZC - sz0)), 2)}],
		ceiling_cells=[[round(c[0], 4), round(c[1], 4), round(c[2], 4), round(c[3], 4), c[4]] for c in cells if c[4] != "tile"])
	export("Arch_Ceiling", [obj], out, meta, collision=col)


def build_plenum(L: dict, out) -> None:
	"""S9: the 1908 pressed tin at 3.35 m, old joists and knob-and-tube wiring, seen only through the missing tile."""
	if not want("Arch_Plenum"):
		return
	rb_bl.reset_scene()
	s = L["shell"]
	X1, Y1 = s["main_room"]["x"][1], s["main_room"]["y"][1]
	zt = s["pressed_tin"]["z"]
	void = s["pressed_tin"]["void"]
	b = rb_bl.Builder("Arch_Plenum")
	r = rb_bl.rng("Arch_Plenum", 0, 1958)
	# Tin: 24 in (0.61 m) square panels with a raised field, a centre boss and a bead border (geometry relief, 3-6 mm), over a
	# region around the void; elsewhere a plain lid at the same height.
	vx0, vx1 = void["x"][0] - 1.83, void["x"][1] + 1.83
	vy0, vy1 = 0.0, void["y"][1] + 1.22
	quad(b, [(0.0, 0.0, zt + 0.01), (X1, 0.0, zt + 0.01), (X1, Y1, zt + 0.01), (0.0, Y1, zt + 0.01)], (0, 0, -1), M_TIN)
	p = 0.3048
	x = vx0
	while x < vx1 - 1e-6:
		y = vy0
		while y < vy1 - 1e-6:
			x1_, y1_ = min(x + p, vx1), min(y + p, vy1)
			b.box(x + 0.006, x1_ - 0.006, y + 0.006, y1_ - 0.006, zt - 0.004, zt + 0.009, M_TIN, skip=("+z",))
			cx, cy = 0.5 * (x + x1_), 0.5 * (y + y1_)
			b.box(cx - 0.09, cx + 0.09, cy - 0.09, cy + 0.09, zt - 0.009, zt - 0.004, M_TIN, skip=("+z",))
			b.cylinder((cx, cy, zt - 0.011), 0.035, 0.006, M_TIN, axis="z", segments=16)
			y = y1_
		x = x1_
	# Grid hanger wires (12 ga, every 1.22 m along the mains) from the tin down to the T-bars, and an old BX cable run.
	zc = s["ceiling_z"]
	for hx in frange(vx0 + 0.61, vx1, 1.22):
		for hy in frange(0.61, vy1, 1.22):
			b.cylinder((hx + 0.01, hy, 0.5 * (zt + zc + 0.03)), 0.0013, zt - zc - 0.03, M_ZINC, segments=5, caps=False)
	b.cylinder((0.5 * (vx0 + vx1), 1.05, zt - 0.03), 0.008, vx1 - vx0, M_ZINC, axis="x", segments=8)
	# Knob-and-tube: porcelain knobs on a joist line and two cloth-covered wires sagging between them.
	for wire in range(2):
		yw = 1.40 + 0.18 * wire
		prev = None
		kx = vx0 + 0.3
		while kx < vx1 - 0.3:
			kz = zt - 0.05
			b.cylinder((kx, yw, kz + 0.018), 0.012, 0.036, M_CERAMIC, axis="z", segments=12)
			if prev is not None:
				n = 8
				for k in range(n):
					t0, t1 = k / n, (k + 1) / n
					sag = lambda t: 0.035 * 4 * t * (1 - t)  # noqa: E731
					p0 = (prev + (kx - prev) * t0, yw, kz - sag(t0))
					p1 = (prev + (kx - prev) * t1, yw, kz - sag(t1))
					mid = ((p0[0] + p1[0]) / 2, yw, (p0[2] + p1[2]) / 2)
					length = math.dist(p0, p1)
					ang = math.atan2(p1[2] - p0[2], p1[0] - p0[0])
					start = b.face_count()
					b.cylinder((0, 0, 0), 0.004, length, "MI_DB_Rubber_Black", axis="x", segments=6, caps=False)
					b.transform(Matrix.Translation(Vector(mid)) @ Matrix.Rotation(-ang, 4, "Y"), start)
			prev = kx
			kx += 0.406 * (1.0 + 0.1 * rb_bl.jitter(r, 1.0))
	# Old dust and debris on top of the neighbouring tiles is the tiles' material (Age dust on up-facing faces).
	obj = b.build(uv_world=True)
	meta = meta_base("Architecture", "S9: pressed tin at 3.35 m, joists, knob-and-tube wiring above the missing tile", tags=["RbDB_Ceiling"],
		profile="RbVenueProp", acoustic_material="steel")
	export("Arch_Plenum", [obj], out, meta)


# ------------------------------------------------------------------------------------------------------------------------------
# columns
# ------------------------------------------------------------------------------------------------------------------------------


def build_columns(L: dict, out) -> None:
	if not want("Arch_Columns"):
		return
	rb_bl.reset_scene()
	s = L["shell"]
	b = rb_bl.Builder("Arch_Columns")
	col = rb_bl.Collision("SM_DB_Arch_Columns")
	for c in s["columns"]:
		x, y, d = c["x"], c["y"], c["d"]
		z0, z1 = c["z"]
		b.cylinder((x, y, 0.5 * (z0 + z1)), d / 2, z1 - z0, M_BLACKSTEEL, segments=28)
		# base plate with a grout pad and 4 bolts, cap plate under the soffit
		b.box(x - 0.13, x + 0.13, y - 0.13, y + 0.13, 0.0, 0.012, M_BLACKSTEEL, skip=("-z",))
		for sx in (-1, 1):
			for sy in (-1, 1):
				b.cylinder((x + sx * 0.095, y + sy * 0.095, 0.02), 0.009, 0.016, M_BLACKSTEEL, segments=8)
		b.box(x - 0.10, x + 0.10, y - 0.10, y + 0.10, z1 - 0.012, z1, M_BLACKSTEEL, skip=("+z",))
		col.cylinder((x, y, 0.5 * (z0 + z1)), d / 2, z1 - z0)
	obj = b.build(uv_world=True)
	meta = meta_base("Architecture", "steel lally columns C1-C3 (114 mm), base and cap plates", acoustic_material="steel")
	export("Arch_Columns", [obj], out, meta, collision=col)


# ------------------------------------------------------------------------------------------------------------------------------
# doors
# ------------------------------------------------------------------------------------------------------------------------------


def door_frame(b, axis, n_face, n_back, u0, u1, z1, face=0.05, mat=M_STEEL):
	"""Hollow-metal frame around an opening (u0..u1, 0..z1) through the wall between n_face and n_back: jambs, head, stops."""
	lo, hi = min(n_face, n_back), max(n_face, n_back)
	dirn = 1.0 if n_face > n_back else -1.0
	P = lambda u0_, u1_, z0_, z1_, n0_, n1_: (n0_, n1_, u0_, u1_, z0_, z1_) if axis == "x" else (u0_, u1_, n0_, n1_, z0_, z1_)  # noqa: E731
	for (a0, a1) in ((u0, u0 + face), (u1 - face, u1)):
		x0, x1, y0, y1, zz0, zz1 = P(a0, a1, 0.0, z1, lo - 0.0, hi)
		b.box(x0, x1, y0, y1, zz0, zz1, mat, skip=("-z",))
	x0, x1, y0, y1, zz0, zz1 = P(u0, u1, z1 - face, z1, lo, hi)
	b.box(x0, x1, y0, y1, zz0, zz1, mat)
	# face trim 12 mm proud on the room side
	nf = n_face + dirn * 0.012
	for (a0, a1, zz0_, zz1_) in ((u0 - 0.02, u0 + face, 0.0, z1 + 0.02), (u1 - face, u1 + 0.02, 0.0, z1 + 0.02), (u0 - 0.02, u1 + 0.02, z1 - face, z1 + 0.02)):
		x0, x1, y0, y1, zz0, zz1 = P(a0, a1, zz0_, zz1_, min(n_face, nf), max(n_face, nf))
		b.box(x0, x1, y0, y1, zz0, zz1, mat, skip=("-z",))


def door_leaf(b, axis, n_center, u0, u1, z0, z1, thick, mat, room_dir, vision=None, push_bar=False, push_plate=False, knob=None):
	"""A closed door leaf (optionally with a vision panel of wired glass, a push bar / push plate on the room side, a knob)."""
	P = lambda u0_, u1_, z0_, z1_, n0_, n1_: (n0_, n1_, u0_, u1_, z0_, z1_) if axis == "x" else (u0_, u1_, n0_, n1_, z0_, z1_)  # noqa: E731
	n0, n1 = n_center - thick / 2, n_center + thick / 2
	if vision:
		vu0, vu1, vz0, vz1 = vision
		for (a0, a1, c0, c1) in ((u0, vu0, z0, z1), (vu1, u1, z0, z1), (vu0, vu1, z0, vz0), (vu0, vu1, vz1, z1)):
			x0, x1, y0, y1, zz0, zz1 = P(a0, a1, c0, c1, n0, n1)
			b.box(x0, x1, y0, y1, zz0, zz1, mat)
		x0, x1, y0, y1, zz0, zz1 = P(vu0, vu1, vz0, vz1, n_center - 0.003, n_center + 0.003)
		b.box(x0, x1, y0, y1, zz0, zz1, M_GLASS)
		# glazing stops
		for side in (-1, 1):
			nn = n_center + side * thick / 2
			for (a0, a1, c0, c1) in ((vu0 - 0.015, vu0, vz0 - 0.015, vz1 + 0.015), (vu1, vu1 + 0.015, vz0 - 0.015, vz1 + 0.015),
					(vu0, vu1, vz0 - 0.015, vz0), (vu0, vu1, vz1, vz1 + 0.015)):
				x0, x1, y0, y1, zz0, zz1 = P(a0, a1, c0, c1, min(nn, nn + side * 0.006), max(nn, nn + side * 0.006))
				b.box(x0, x1, y0, y1, zz0, zz1, mat)
	else:
		x0, x1, y0, y1, zz0, zz1 = P(u0, u1, z0, z1, n0, n1)
		b.box(x0, x1, y0, y1, zz0, zz1, mat)
	face = n_center + room_dir * thick / 2
	if push_bar:
		# exit device: a 0.85 m bar at 1.0 m on two end brackets
		um = 0.5 * (u0 + u1)
		for a0, a1 in ((u0 + 0.05, u0 + 0.12), (u1 - 0.12, u1 - 0.05)):
			x0, x1, y0, y1, zz0, zz1 = P(a0, a1, 0.94, 1.06, min(face, face + room_dir * 0.07), max(face, face + room_dir * 0.07))
			b.box(x0, x1, y0, y1, zz0, zz1, M_STAINLESS)
		x0, x1, y0, y1, zz0, zz1 = P(u0 + 0.10, u1 - 0.10, 0.975, 1.03, min(face + room_dir * 0.04, face + room_dir * 0.075),
			max(face + room_dir * 0.04, face + room_dir * 0.075))
		b.box(x0, x1, y0, y1, zz0, zz1, M_STAINLESS)
		del um
	if push_plate:
		pu = u1 - 0.18 if room_dir > 0 else u0 + 0.08
		x0, x1, y0, y1, zz0, zz1 = P(pu, pu + 0.10, 1.05, 1.45, min(face, face + room_dir * 0.002), max(face, face + room_dir * 0.002))
		b.box(x0, x1, y0, y1, zz0, zz1, M_STAINLESS)
	if knob is not None:
		ku = knob
		for side in (-1, 1):
			nn = n_center + side * (thick / 2 + 0.03)
			c = (nn, ku, 0.97) if axis == "x" else (ku, nn, 0.97)
			b.cylinder(c, 0.028, 0.03, M_STAINLESS, axis=axis, segments=16)
			c2 = (n_center + side * (thick / 2 + 0.008), ku, 0.97) if axis == "x" else (ku, n_center + side * (thick / 2 + 0.008), 0.97)
			b.cylinder(c2, 0.034, 0.016, M_STAINLESS, axis=axis, segments=16)


def build_doors(L: dict, out) -> None:
	if not want("Arch_Doors"):
		return
	rb_bl.reset_scene()
	o = {op["id"]: op for op in L["openings"]}
	b = rb_bl.Builder("Arch_Doors")
	col = rb_bl.Collision("SM_DB_Arch_Doors")
	# E02 entrance: frame through the 0.33 m brick, steel leaf flush with the street face (opens out), wired-glass vision panel,
	# push bar inside, transom bar at 2.13 m and the black-painted transom glass (gold-leaf lettering is M2-B's text work).
	e = o["E02"]
	u0, u1 = e["y"]
	tz = e["transom"]["z"]
	door_frame(b, "x", 0.0, -0.33, u0, u1, tz[1], face=0.05)
	b.box(-0.33, 0.0, u0, u1, tz[0], tz[0] + 0.05, M_STEEL)                   # transom bar
	b.box(-0.30, -0.29, u0 + 0.05, u1 - 0.05, tz[0] + 0.05, tz[1] - 0.05, M_CABINET)  # black-painted transom glass
	vw, vh = e["vision_panel"]
	vis_u0 = u0 + 0.05 + 0.5 * ((u1 - u0 - 0.10) - vw) + 0.10
	door_leaf(b, "x", -0.26, u0 + 0.052, u1 - 0.052, 0.012, tz[0] - 0.003, 0.045, M_CABINET, +1,
		vision=(vis_u0, vis_u0 + vw, 1.10, 1.10 + vh), push_bar=True)
	col.box(-0.33, 0.0, u0, u1, 0.0, tz[1])
	# E19 storage door (wood, swings into the keg cooler; push plate on the pool-room face) in a steel frame.
	e = o["E19"]
	u0, u1 = e["y"]
	door_frame(b, "x", 16.46, 16.66, u0, u1, e["z"][1], face=0.05)
	door_leaf(b, "x", 16.52, u0 + 0.052, u1 - 0.052, 0.006, e["z"][1] - 0.003, 0.045, M_WOOD, -1, push_plate=True)
	col.box(16.46, 16.66, u0, u1, 0.0, e["z"][1])
	# E18 corridor opening: cased (wood casing both faces + jamb lining), no door.
	e = o["E18"]
	u0, u1 = e["y"]
	z1 = e["z"][1]
	for nf, d in ((16.46, -1), (16.66, +1)):
		nn = nf + d * 0.018
		lo, hi = min(nf, nn), max(nf, nn)
		b.box(lo, hi, u0 - 0.07, u0, 0.0, z1 + 0.07, M_WOOD, skip=("-z",))
		b.box(lo, hi, u1, u1 + 0.07, 0.0, z1 + 0.07, M_WOOD, skip=("-z",))
		b.box(lo, hi, u0 - 0.07, u1 + 0.07, z1, z1 + 0.07, M_WOOD)
	b.box(16.46, 16.66, u0, u0 + 0.018, 0.0, z1, M_WOOD, skip=("-z",))
	b.box(16.46, 16.66, u1 - 0.018, u1, 0.0, z1, M_WOOD, skip=("-z",))
	b.box(16.46, 16.66, u0, u1, z1 - 0.018, z1, M_WOOD)
	# E20 restroom doors (wood, closed, 10 mm gap at the floor for the light leak L27 / L28), steel frames in the 0.12 m block.
	for oid in ("E20m", "E20w"):
		e = o[oid]
		u0, u1 = e["x"]
		door_frame(b, "y", 1.40, 1.52, u0, u1, e["z"][1], face=0.045)
		door_leaf(b, "y", 1.47, u0 + 0.047, u1 - 0.047, 0.010, e["z"][1] - 0.003, 0.04, M_WOOD, -1, knob=u1 - 0.12)
		col.box(u0, u1, 1.40, 1.52, 0.0, e["z"][1])
	# E21 back exit (steel door, push bar) in the outer block wall.
	e = o["E21"]
	u0, u1 = e["y"]
	door_frame(b, "x", 20.12, 20.32, u0, u1, e["z"][1], face=0.05)
	door_leaf(b, "x", 20.17, u0 + 0.052, u1 - 0.052, 0.012, e["z"][1] - 0.003, 0.045, M_CABINET, -1, push_bar=True)
	col.box(20.12, 20.32, u0, u1, 0.0, e["z"][1])
	obj = b.build(uv_world=True)
	meta = meta_base("Architecture", "doors: entrance (steel, vision panel, push bar, transom), storage, restrooms, back exit; cased corridor opening",
		acoustic_material="steel", acoustic_surfaces=[{"material": "steel", "area_m2": round(0.91 * 2.13 * 2, 2)},
			{"material": "wood_panel", "area_m2": round(0.91 * 2.13 + 0.81 * 2.03 * 2, 2)}])
	export("Arch_Doors", [obj], out, meta, collision=col)


# ------------------------------------------------------------------------------------------------------------------------------
# glass block storefront + street backplate
# ------------------------------------------------------------------------------------------------------------------------------


def build_glass_block(L: dict, out) -> None:
	if not want("Arch_GlassBlock"):
		return
	rb_bl.reset_scene()
	o = {op["id"]: op for op in L["openings"]}
	e = o["E01"]
	y0, y1 = e["y"]
	z0, z1 = e["z"]
	pane = e["clear_pane"]
	blk = e["block"]
	b = rb_bl.Builder("Arch_GlassBlock")
	r = rb_bl.rng("Arch_GlassBlock", 0, 1958)
	nx = max(1, int(round((y1 - y0) / blk)))
	nz = max(1, int(round((z1 - z0) / blk)))
	py = (y1 - y0) / nx
	pz = (z1 - z0) / nz
	joint = 0.010
	depth0, depth1 = -0.24, -0.14  # the 100 mm block sits in the middle of the 0.33 m wall
	count = 0
	for i in range(nx):
		for k in range(nz):
			cy0, cy1 = y0 + i * py, y0 + (i + 1) * py
			cz0, cz1 = z0 + k * pz, z0 + (k + 1) * pz
			yc, zc = 0.5 * (cy0 + cy1), 0.5 * (cz0 + cz1)
			if pane["y"][0] - 1e-3 < yc < pane["y"][1] + 1e-3 and pane["z"][0] - 1e-3 < zc < pane["z"][1] + 1e-3:
				continue
			count += 1
			a0, a1, c0, c1 = cy0 + joint / 2, cy1 - joint / 2, cz0 + joint / 2, cz1 - joint / 2
			# Flat faces with a slight per-block tilt (old mortar work; the flutes / waves are the material's normal detail): planar
			# quads, so the reflections of the room lights never break up into triangle facets.
			tilt = rb_bl.jitter(r, 0.0012)
			for face_x, sign in ((depth1, +1), (depth0, -1)):
				pts = [(face_x - tilt * 0.5 * (1 if k in (1, 2) else -1) * 0.0 + tilt * (0.5 if k in (1, 2) else -0.5), a0 if k in (0, 3) else a1,
					c0 if k in (0, 1) else c1) for k in range(4)]
				quad(b, pts, (sign, 0, 0), M_GLASSBLOCK)
			# block sides (the glass edges, seen in the joints)
			b.box(depth0, depth1, a0, a1, c0, c1, M_GLASSBLOCK, skip=("-x", "+x"))
	# Mortar bed behind the joints (recessed 8 mm from the block faces on both sides) around the clear pane.
	m0, m1 = depth0 + 0.008, depth1 - 0.008
	py0, py1, pz0, pz1 = pane["y"][0] - 0.03, pane["y"][1] + 0.03, pane["z"][0] - 0.03, pane["z"][1] + 0.03
	b.box(m0, m1, y0, py0, z0, z1, M_MORTAR)
	b.box(m0, m1, py1, y1, z0, z1, M_MORTAR)
	b.box(m0, m1, py0, py1, z0, pz0, M_MORTAR)
	b.box(m0, m1, py0, py1, pz1, z1, M_MORTAR)
	# Clear pane: a steel frame with a single glass sheet in the middle of the wall depth (the window neons hang behind it).
	for (a0, a1, c0, c1) in ((pane["y"][0] - 0.03, pane["y"][0], pane["z"][0] - 0.03, pane["z"][1] + 0.03),
			(pane["y"][1], pane["y"][1] + 0.03, pane["z"][0] - 0.03, pane["z"][1] + 0.03),
			(pane["y"][0], pane["y"][1], pane["z"][0] - 0.03, pane["z"][0]), (pane["y"][0], pane["y"][1], pane["z"][1], pane["z"][1] + 0.03)):
		b.box(depth0 - 0.01, depth1 + 0.01, a0, a1, c0, c1, M_STEEL)
	b.box(-0.20, -0.194, pane["y"][0], pane["y"][1], pane["z"][0], pane["z"][1], M_GLASS)
	obj = b.build(uv_world=True)
	col = rb_bl.Collision("SM_DB_Arch_GlassBlock")
	col.box(-0.33, 0.0, y0, y1, z0, z1)
	meta = meta_base("Architecture", f"glass-block storefront infill: {count} blocks of 203 mm, clear pane for the window neons",
		acoustic_material="glass_block", cast_shadow=False, translucent=False,
		acoustic_surfaces=[{"material": "glass_block", "area_m2": round((y1 - y0) * (z1 - z0), 2)}])
	export("Arch_GlassBlock", [obj], out, meta, collision=col)


def build_street(L: dict, out) -> None:
	"""Night street behind the storefront (L33 / L34 backplate; the glass block and the door glass show it)."""
	if not want("Arch_Street"):
		return
	rb_bl.reset_scene()
	b = rb_bl.Builder("Arch_Street")
	# Sidewalk and curb under the storefront, then the backplate card 5 m out (the far side of Harbor Street).
	b.box(-3.0, -0.33, -0.5, 8.2, -0.12, -0.02, "MI_DB_Concrete_Sidewalk")
	b.box(-3.25, -3.0, -0.5, 8.2, -0.25, -0.05, "MI_DB_Concrete_Sidewalk")
	quad(b, [(-9.0, -4.0, -0.5), (-9.0, 11.5, -0.5), (-9.0, 11.5, 6.0), (-9.0, -4.0, 6.0)], (1, 0, 0), M_STREET)
	quad(b, [(-9.0, -4.0, -0.25), (-3.25, -4.0, -0.25), (-3.25, 11.5, -0.25), (-9.0, 11.5, -0.25)], (0, 0, 1), "MI_DB_Asphalt_Wet")
	obj = b.build(uv_world=True)
	meta = meta_base("Architecture", "night street backplate (sodium lamp L34, wet asphalt, sidewalk)", profile="RbVenueProp",
		cast_shadow=False, acoustic_material="none")
	export("Arch_Street", [obj], out, meta)


# ------------------------------------------------------------------------------------------------------------------------------
# ceiling fixtures (local assets)
# ------------------------------------------------------------------------------------------------------------------------------


def build_troffer(out) -> None:
	if not want("Troffer"):
		return
	rb_bl.reset_scene()
	b = rb_bl.Builder("Troffer")
	w, lgt, dep = 0.600, 1.210, 0.11
	# pivot at the ceiling plane centre; the lens 8 mm above the grid flange, a 25 mm white door frame
	b.box(-lgt / 2, lgt / 2, -w / 2, w / 2, 0.0, 0.012, M_WHITE, skip=("+z",))
	b.box(-lgt / 2 + 0.03, lgt / 2 - 0.03, -w / 2 + 0.03, w / 2 - 0.03, 0.006, 0.010, M_LENS)
	b.box(-lgt / 2, lgt / 2, -w / 2, w / 2, 0.012, dep, M_WHITE, skip=("-z",))
	obj = b.build(uv_world=True)
	meta = {"family": "Lighting", "ue_folder": "Lighting", "profile": "RbVenueProp", "frame": "local", "pivot": "ceiling_center",
		"notes": "2 x 4 ft troffer (Lights-Up state, E23); lens emissive follows the light's state ramp", "owner": PACKAGE,
		"generator": "db_arch.py", "acoustic_material": "steel", "tags": ["RbDB_Ceiling"]}
	rb_bl.export_asset("Troffer", [obj], pathlib.Path(out) / FOLDER, meta, target_m=(lgt, w, dep), tolerance_m=0.002, package=PACKAGE)


def build_diffuser(out) -> None:
	if not want("HvacDiffuser"):
		return
	rb_bl.reset_scene()
	b = rb_bl.Builder("HvacDiffuser")
	s = 0.596
	b.box(-s / 2, s / 2, -s / 2, s / 2, 0.0, 0.01, M_WHITE, skip=("+z",))
	# stepped louvre cone: 4 nested frames
	for k in range(4):
		h = s / 2 - 0.035 - k * 0.055
		if h <= 0.03:
			break
		z = 0.012 + k * 0.012
		for (x0, x1, y0, y1) in ((-h, h, -h, -h + 0.02), (-h, h, h - 0.02, h), (-h, -h + 0.02, -h, h), (h - 0.02, h, -h, h)):
			b.box(x0, x1, y0, y1, z, z + 0.012, M_WHITE)
	b.box(-0.08, 0.08, -0.08, 0.08, 0.06, 0.07, M_WHITE)
	b.box(-s / 2 + 0.03, s / 2 - 0.03, -s / 2 + 0.03, s / 2 - 0.03, 0.08, 0.085, "MI_DB_Steel_Black")
	obj = b.build(uv_world=True)
	meta = {"family": "Lighting", "ue_folder": "Arch", "profile": "RbVenueProp", "frame": "local", "pivot": "ceiling_center",
		"notes": "2 x 2 ft louvered supply diffuser (M16), nicotine and dust by the material", "owner": PACKAGE, "generator": "db_arch.py",
		"acoustic_material": "steel", "tags": ["RbDB_Ceiling"]}
	rb_bl.export_asset("HvacDiffuser", [obj], pathlib.Path(out) / FOLDER, meta, package=PACKAGE)


# Simple stroke glyphs for the EXIT letters (unit cap height, boxes of stroke width w).
EXIT_GLYPHS = {
	"E": [((0, 0), (0, 1)), ((0, 1), (0.62, 1)), ((0, 0.52), (0.52, 0.52)), ((0, 0), (0.62, 0))],
	"X": [((0, 0), (0.66, 1)), ((0, 1), (0.66, 0))],
	"I": [((0.0, 0), (0.0, 1))],
	"T": [((0, 1), (0.66, 1)), ((0.33, 1), (0.33, 0))],
}


def build_exit_sign(out) -> None:
	if not want("ExitSign"):
		return
	rb_bl.reset_scene()
	b = rb_bl.Builder("ExitSign")
	W, H, D = 0.33, 0.19, 0.055
	# housing (white thermoplastic), pivot at the top centre, faces +X and -X (double-faced, L25b) - the level rotates it
	b.box(-D / 2, D / 2, -W / 2, W / 2, -H, 0.0, M_WHITE)
	cap, sw = 0.150, 0.019
	widths = {"E": 0.62, "X": 0.66, "I": 0.0, "T": 0.66}
	gap = 0.30
	total = sum(widths[c] for c in "EXIT") + gap * 3
	scale = cap
	u = -0.5 * total * scale
	for ch in "EXIT":
		for (p0, p1) in EXIT_GLYPHS[ch]:
			for face in (+1, -1):
				# the viewer of the +X face has +Y on the left (UE): the text runs along -Y there, along +Y on the -X face
				ax = face * (D / 2 + 0.0015)
				a = Vector((ax, -face * (u + p0[0] * scale), -H / 2 - cap / 2 + p0[1] * cap))
				c = Vector((ax, -face * (u + p1[0] * scale), -H / 2 - cap / 2 + p1[1] * cap))
				d = c - a
				length = d.length
				mid = (a + c) / 2
				ang = math.atan2(d.z, d.y)
				start = b.face_count()
				b.box(-0.0015, 0.0015, -length / 2 - sw / 2, length / 2 + sw / 2, -sw / 2, sw / 2, M_EXIT)
				b.transform(Matrix.Translation(mid) @ Matrix.Rotation(ang, 4, "X"), start)
		u += (widths[ch] + gap) * scale
	# mounting canopy + two stems to the ceiling
	b.box(-0.02, 0.02, -0.05, 0.05, 0.0, 0.02, M_WHITE)
	obj = b.build(uv_world=True)
	meta = {"family": "Lighting", "ue_folder": "Lighting", "profile": "RbVenueProp", "frame": "local", "pivot": "top_center",
		"notes": "double-faced LED EXIT sign (L25, 40 cd/m2 letters, 150 mm)", "owner": PACKAGE, "generator": "db_arch.py",
		"acoustic_material": "none", "cast_shadow": False}
	rb_bl.export_asset("ExitSign", [obj], pathlib.Path(out) / FOLDER, meta, package=PACKAGE)


# ------------------------------------------------------------------------------------------------------------------------------
# greybox primitives (rb_make_divebar.py composes greyboxes from these until M2-B's meshes exist)
# ------------------------------------------------------------------------------------------------------------------------------


def build_greybox(out) -> None:
	prims = {}

	def gb(name, fn, notes, target=None):
		if not want(name):
			return
		rb_bl.reset_scene()
		b = rb_bl.Builder(name)
		fn(b)
		obj = b.build(uv_world=True)
		meta = {"family": "Greybox", "ue_folder": "Arch/Greybox", "profile": "RbVenueBlock", "frame": "local", "pivot": "center",
			"notes": notes, "owner": PACKAGE, "generator": "db_arch.py", "acoustic_material": "none"}
		col = rb_bl.Collision(f"SM_DB_{name}")
		if name in ("GB_Box",):
			col.box(-0.5, 0.5, -0.5, 0.5, -0.5, 0.5)
		elif name in ("GB_Cylinder",):
			col.cylinder((0, 0, 0), 0.5, 1.0)
		rb_bl.export_asset(name, [obj], pathlib.Path(out) / FOLDER, meta, collision=col if col.objects else None, target_m=target,
			tolerance_m=0.001, package=PACKAGE)
		prims[name] = True

	# unit box / cylinder with 1 mm bevels (the "3 mm rule" at unit scale is applied by the bevel on the scaled greybox: none)
	gb("GB_Box", lambda b: b.box(-0.5, 0.5, -0.5, 0.5, -0.5, 0.5, "MI_DB_Greybox"), "unit box, centred", (1.0, 1.0, 1.0))
	gb("GB_Cylinder", lambda b: b.cylinder((0, 0, 0), 0.5, 1.0, "MI_DB_Greybox", segments=32), "unit cylinder along Z, centred", (1.0, 1.0, 1.0))

	def shade(b):
		# spun dome shade of the 3-shade lamp (H03 stand-in): 0.36 m rim, 0.23 m deep, open at the bottom; outer green enamel,
		# inner white enamel (both sides modelled, 1.2 mm steel), socket cup on top. Pivot: the bulb centre (0.15 m above the rim).
		segs = 40
		prof = []
		for k in range(15):
			t = k / 14.0
			rr = 0.035 + 0.145 * (math.cos(t * math.pi / 2) ** 0.9)
			zz = -0.15 + 0.23 * (math.sin(t * math.pi / 2) ** 1.2)
			prof.append((rr, zz))
		for side, off, mat in ((+1, 0.0, "MI_DB_Enamel_Green"), (-1, -0.0012, "MI_DB_Enamel_WhiteInt")):
			for k in range(len(prof) - 1):
				(r0, z0), (r1, z1) = prof[k], prof[k + 1]
				for i in range(segs):
					a0, a1 = 2 * math.pi * i / segs, 2 * math.pi * (i + 1) / segs
					p = [((r0 + off) * math.cos(a0), (r0 + off) * math.sin(a0), z0), ((r0 + off) * math.cos(a1), (r0 + off) * math.sin(a1), z0),
						((r1 + off) * math.cos(a1), (r1 + off) * math.sin(a1), z1), ((r1 + off) * math.cos(a0), (r1 + off) * math.sin(a0), z1)]
					am = 0.5 * (a0 + a1)
					n = (side * math.cos(am), side * math.sin(am), side * 0.3)
					quad(b, p, n, mat)
		# rolled rim
		for i in range(segs):
			a0, a1 = 2 * math.pi * i / segs, 2 * math.pi * (i + 1) / segs
			p = [(0.18 * math.cos(a0), 0.18 * math.sin(a0), -0.15), (0.1788 * math.cos(a0), 0.1788 * math.sin(a0), -0.15),
				(0.1788 * math.cos(a1), 0.1788 * math.sin(a1), -0.15), (0.18 * math.cos(a1), 0.18 * math.sin(a1), -0.15)]
			quad(b, p, (0, 0, -1), "MI_DB_Enamel_Green")
		b.cylinder((0, 0, 0.09), 0.036, 0.04, "MI_DB_Steel_Black", segments=20)
		b.cylinder((0, 0, 0.02), 0.018, 0.05, "MI_DB_Plastic_Black", segments=16)
	gb("GB_Shade", shade, "lamp shade stand-in (H03): 0.36 m spun dome, green enamel outside, white inside; pivot at the bulb centre")

	def bulb(b):
		# A19 frosted bulb (hidden from ray tracing in the level; the analytic light is the source)
		segs = 20
		prof = [(0.0, -0.055), (0.018, -0.052), (0.028, -0.043), (0.030, -0.030), (0.029, -0.015), (0.024, 0.0), (0.016, 0.018), (0.013, 0.03),
			(0.013, 0.05)]
		for k in range(len(prof) - 1):
			(r0, z0), (r1, z1) = prof[k], prof[k + 1]
			for i in range(segs):
				a0, a1 = 2 * math.pi * i / segs, 2 * math.pi * (i + 1) / segs
				p = [(r0 * math.cos(a0), r0 * math.sin(a0), z0), (r0 * math.cos(a1), r0 * math.sin(a1), z0), (r1 * math.cos(a1), r1 * math.sin(a1), z1),
					(r1 * math.cos(a0), r1 * math.sin(a0), z1)]
				am = 0.5 * (a0 + a1)
				quad(b, [q for q in p], (math.cos(am), math.sin(am), 0.0), "MI_DB_Emissive_Bulb2700")
	gb("GB_Bulb", bulb, "A19 frosted bulb (emissive), pivot at the filament centre")

	def blade(b):
		# ceiling-fan blade (4 per fan, E24): 0.55 m x 0.13 m board with a steel iron, pitched 12 deg
		start = b.face_count()
		b.box(0.10, 0.66, -0.065, 0.065, -0.005, 0.005, "MI_DB_Wood_Stained")
		b.transform(Matrix.Rotation(math.radians(12.0), 4, "X"), start)
		b.box(0.05, 0.20, -0.015, 0.015, 0.005, 0.012, "MI_DB_Steel_Black")
	gb("GB_FanBlade", blade, "ceiling-fan blade with iron (E24), pivot at the fan axis")

	def lathe(b, prof, mat, segs=32, inward=False):
		"""Surface of revolution about Z through the (r, z) profile; normals radially out (or in)."""
		s = -1.0 if inward else 1.0
		for k in range(len(prof) - 1):
			(r0, z0), (r1, z1) = prof[k], prof[k + 1]
			if max(r0, r1) < 1e-6:
				continue
			for i in range(segs):
				a0, a1 = 2 * math.pi * i / segs, 2 * math.pi * (i + 1) / segs
				p = [(r0 * math.cos(a0), r0 * math.sin(a0), z0), (r0 * math.cos(a1), r0 * math.sin(a1), z0),
					(r1 * math.cos(a1), r1 * math.sin(a1), z1), (r1 * math.cos(a0), r1 * math.sin(a0), z1)]
				am = 0.5 * (a0 + a1)
				nz = -(r1 - r0) * (1.0 if z1 > z0 else -1.0)
				quad(b, p, (s * math.cos(am), s * math.sin(am), s * 0.5 * nz), mat)

	def pendant(b):
		# bar mini pendant (L5-L8, 4.2): black enamel cone shade 0.185 m rim, white inside, socket housing on top; the exposed
		# filament bulb (GB_Bulb with the 2200 K emissive) sits at the pivot, its lower half below the rim. Pivot: the bulb centre.
		outer = [(0.030, 0.090), (0.042, 0.066), (0.078, -0.015), (0.091, -0.055), (0.0925, -0.062)]
		lathe(b, outer, "MI_DB_Enamel_Black", segs=36)
		lathe(b, [(r - 0.0012, z) for r, z in outer], "MI_DB_Enamel_WhiteInt", segs=36, inward=True)
		b.cylinder((0, 0, 0.105), 0.031, 0.03, "MI_DB_Steel_Black", segments=24)
		b.cylinder((0, 0, 0.050), 0.017, 0.06, "MI_DB_Plastic_Black", segments=16)
	gb("GB_Pendant", pendant, "bar mini pendant shade (L5-L8): black enamel cone, white interior, socket; pivot at the bulb centre")

	def sconce_glass(b):
		# booth sconce (L13-L15): a frosted glass globe on a brass fitter; the level puts it on a wall plate + arm. Pivot: the globe
		# centre (the light's position).
		lathe(b, [(0.0, -0.078), (0.028, -0.073), (0.052, -0.058), (0.068, -0.033), (0.074, -0.004), (0.071, 0.024), (0.058, 0.048),
			(0.042, 0.064), (0.034, 0.072)], "MI_DB_Glass_Frosted", segs=32)
		b.cylinder((0, 0, 0.080), 0.038, 0.018, "MI_DB_Brass_Worn", segments=24)
	gb("GB_SconceGlass", sconce_glass, "booth sconce frosted globe on a brass fitter (L13-L15); pivot at the globe centre")


def build_dust_motes(L: dict, out, count: int = 2500) -> None:
	"""FX_DustMotes (4.7): a cloud of tiny mote cards in a 2.4 x 1.4 x 1.1 m box around the lamp cone. Every card's 4 vertices sit
	0.1 mm around the mote centre (the material M_DBA_DustMote billboards them to the camera by world-position offset, drifts them
	(Brownian 2 mm/s, updraft over the bulbs) and lights them analytically from the lamp's cone with a forward-scattering phase);
	UV0 = card corner (0 / 1), UV1 = (random phase, random size). Local frame: pivot at the box centre, X along the table."""
	if not want("FX_DustMotes"):
		return
	rb_bl.reset_scene()
	r = rb_bl.rng("FX_DustMotes", 0, 1958)
	b = rb_bl.Builder("FX_DustMotes")
	hx, hy, hz = 1.2, 0.7, 0.55
	e = 0.0001
	for k in range(count):
		c = Vector((rb_bl.jitter(r, hx), rb_bl.jitter(r, hy), rb_bl.jitter(r, hz)))
		start = b.face_count()
		b.poly([(c.x - e, c.y, c.z - e), (c.x + e, c.y, c.z - e), (c.x + e, c.y, c.z + e), (c.x - e, c.y, c.z + e)], "MI_DB_FX_DustMote")
		del start
	obj = b.build(uv_world=False, weld=False)
	mesh = obj.data
	uv0 = mesh.uv_layers.new(name="UV0")
	uv1 = mesh.uv_layers.new(name="UV1")
	r2 = rb_bl.rng("FX_DustMotes", 1, 1958)
	corners = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
	for poly in mesh.polygons:
		phase, size = r2.random(), r2.random()
		for i, li in enumerate(poly.loop_indices):
			uv0.data[li].uv = corners[i % 4]
			uv1.data[li].uv = (phase, size)
	meta = {"family": "FX", "ue_folder": "FX", "profile": "RbVenueProp", "frame": "local", "pivot": "box centre", "owner": PACKAGE,
		"generator": "db_arch.py", "translucent": True, "lods": False, "cast_shadow": False, "acoustic_material": "none",
		"motes": count, "box_m": [2 * hx, 2 * hy, 2 * hz], "notes": "dust motes in the lamp cone (4.7); material M_DBA_DustMote (rb_make_divebar_fx.py)"}
	rb_bl.export_asset("FX_DustMotes", [obj], pathlib.Path(out) / FOLDER, meta, package=PACKAGE)


def main() -> None:
	a = rb_bl.args()
	L = rb_bl.load_json("Art/DiveBar/layout.json")
	build_dust_motes(L, a.out)
	build_floor(L, a.out)
	build_walls(L, a.out)
	build_ceiling(L, a.out)
	build_plenum(L, a.out)
	build_columns(L, a.out)
	build_doors(L, a.out)
	build_glass_block(L, a.out)
	build_street(L, a.out)
	build_troffer(a.out)
	build_diffuser(a.out)
	build_exit_sign(a.out)
	build_greybox(a.out)
	rb_bl.log("db_arch: done")


main()
