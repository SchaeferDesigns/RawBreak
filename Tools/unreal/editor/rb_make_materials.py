"""Generates every M_Rb* / MI_Rb* table-family material, MPC_RbBalls and the textures they sample (Docs/ue-architecture.md 8.2,
18.7; ue5-realism-plan 6.x; venue-dive-bar 6.4). Owner: M2-L (UE-3 in M1). Run inside Unreal (headless):

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_materials.py

Substrate materials (the project runs Substrate with the Adaptive GBuffer, 2.2) whose non-trivial logic sits in HLSL files
under Shaders/Private, included by Custom nodes (/RawBreak/Private/*.ush via the RawBreakShaders module):

  M_RbBall           phenolic resin: F0 0.049 (n = 1.57), clear primary lobe + haze lobe (Quality Switch: off on Low), SSS
                     (High+), analytic number circles / stripe band / glyphs from ball-local coordinates (RbBall.ush), glyph
                     atlas T_RbBallGlyphs, dotted-cue-ball option, rotation smear, analytic sphere normal
  M_RbCloth          worsted wool: fuzz / sheen for grazing angles, fine weave normal T_RbClothWeave_N (1 mm plain weave,
                     anisotropic filtering, mips), normal variance composited into the roughness mips, slight anisotropy,
                     analytic ball occlusion from MPC_RbBalls (RbBallOcclusion.ush) as material AO (BallOcclusion 0..1)
  M_RbRailWood       lacquer clear coat (Substrate vertical layering, amber transmittance) over procedural wood (RbSurfaces.ush)
  M_RbCushionRubber  black rubber           M_RbPocketLiner / M_RbLeather   leather + fuzz, pebbled grain
  M_RbBrass          brass metal (pocket irons / castings)
  M_RbSight          mother of pearl: thin-film F0 (Substrate ThinFilm, 300-600 nm) + the platelets' interference tint in the
                     albedo (RbNacreAlbedo: pastel patches that shift with the view angle)
  M_RbCue            cue sections (tip, ferrule, maple shaft, collar, forearm, wrap, sleeve, bumper) under a satin clear coat;
                     section index from UV1.x of UE-4's cue mesh (static switch SectionsFromUV1), seamless grain
  M_RbRoomWall / M_RbRoomFloor   neutral painted plaster / sealed concrete
  M_RbLampDiffuser   emissive diffuser, hidden from ray-traced reflections and the Lumen surface cache (plan pitfall 9)
  MI_RbCloth_Green   classic green cloth (M_RbCloth defaults to tournament blue for the 9-ft pro table)
  MI_RbCue_LocalSections  M_RbCue with the sections along local X (meshes without UV1: dev swatches)

M2-L look-dev (18.7; HLSL of the new layers in Shaders/Private/RbTableLook.ush, placed from the part's LocalPosition = the
table-local frame, every layer scaled by the material's own Age scalar - the dive bar's 0.80 is carried here, never read from
MPC_DB_Venue):
  M_RbCloth          + wear (RbClothWear): chalk dust at the pockets and the break area, ball burns, worn lanes, the rack
                     impression, faint ball tracks, frayed pocket points, stains (Age > 0.4), pilling and the nap direction of
                     napped bar cloth (Napped = 1). Worsted 9-ft defaults: Age 0.15.
  MI_RbCloth_BarGreen  napped bar cloth of the coin-op table (venue-dive-bar 3.2 / 6.4: #2F6B40, fuzz 0.35, Age 0.80)
  M_RbRailWood       CC0 walnut veneer scan (Poly Haven walnut_veneer, Art/Tables/cc0_inputs.json) stained under the clear coat;
                     MI_RbRailWood_Legs with the grain vertical (procedural wood when the CC0 inputs are missing)
  M_RbLaminate       single-slab laminate with burns, glass rings, scratches, worn-through cabinet corners and kick grime:
                     MI_RbRail_BlackLaminate (bar-table rail caps), MI_RbLaminate_Walnut (the cabinet's printed walnut, Poly
                     Haven walnut_veneer_02 as the print)
  M_RbPocketLiner    CC0 leather scan (Poly Haven brown_leather) for the 9-ft drop pockets
  M_RbCushionRubber  + rubber bloom with Age; MI_RbCushionRubber_Old (bar table)
  M_RbPlasticABS     black satin ABS of the coin-op castings (scuffs, chips); MI_RbSight_WhitePlastic (bar-table sights)
  M_RbAluminium / M_RbChrome / M_RbSteel   brushed trim, chrome coin mechanism / door / return ring, steel levelers
  M_RbPlexi          scratched plexiglass of the ball-trap window (opaque dark approximation, see Docs/references/table-lookdev.md)
  M_RbBall           + worn-ball layer (RbBallWear in RbBall.ush: grime, collision scuffs, chalk, per-ball roughness), off by
                     default; MI_RbBall_DiveBar (venue-dive-bar 6.4: roughness 0.10-0.15 per ball, haze 0.3, yellowed white)

Textures (/Game/Generated/Materials/Textures), generated here in pure Python and imported from Saved/RbGenerated:
  T_RbBallGlyphs     1024^2 single-channel signed distance field, 4 x 4 cells: cell n = the label of ball n inscribed in the
                     number circle, 6 and 9 underscored, cell 0 empty. Glyph outlines: Roboto Bold / Bold Condensed (Apache
                     License 2.0, shipped with the engine under Engine/Content/Slate/Fonts), parsed from the TTF here.
  T_RbClothWeave_N   512^2 tileable plain-weave normal map (16 x 16 yarns = 8 x 8 mm, twisted worsted yarns)
  T_RbClothWeave_M   R cavity, G roughness (+ the weave normal's variance per mip: Composite Texture), B fibre tone

Idempotent: textures are re-imported in place, materials are cleared and rebuilt (asset identity and references kept), the
collection keeps its parameter ids (materials reference them by id). Prints the material parameter values (A2 metrics).
"""

from __future__ import annotations

import hashlib
import json
import math
import os
import struct
import sys
import zlib

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

MAT_DIR = "/Game/Generated/Materials"
TEX_DIR = MAT_DIR + "/Textures"
MPC_PATH = MAT_DIR + "/MPC_RbBalls"
GEN_DIR = os.path.join(os.path.abspath(unreal.Paths.project_saved_dir()), "RbGenerated", "Textures")
FONT_DIR = os.path.join(os.path.abspath(unreal.Paths.engine_content_dir()), "Slate", "Fonts")

INC_BALL = "/RawBreak/Private/RbBall.ush"
INC_OCC = "/RawBreak/Private/RbBallOcclusion.ush"
INC_SURF = "/RawBreak/Private/RbSurfaces.ush"
INC_LOOK = "/RawBreak/Private/RbTableLook.ush"

# CC0 inputs of the table materials (Art/Tables/cc0_inputs.json, pinned by Art/Tables/cc0_inputs.lock.json, fetched by
# Tools/art/fetch_cc0.py into the git-ignored Art/Third; ledger Docs/licenses/ledger/M2-L.csv).
PROJECT_DIR = os.path.abspath(unreal.Paths.project_dir())
CC0_DIR = os.path.join(PROJECT_DIR, "Art", "Third", "polyhaven")
CC0_LOCK = os.path.join(PROJECT_DIR, "Art", "Tables", "cc0_inputs.lock.json")
CC0_TEXTURES = [
	# (Poly Haven id, file, texture asset, kind)
	("walnut_veneer", "walnut_veneer_diff_2k.png", "T_RbWalnutVeneer_D", "color"),
	("walnut_veneer", "walnut_veneer_nor_dx_2k.png", "T_RbWalnutVeneer_N", "normal"),
	("walnut_veneer", "walnut_veneer_rough_2k.png", "T_RbWalnutVeneer_R", "mask"),
	("walnut_veneer_02", "walnut_veneer_02_diff_2k.png", "T_RbWalnutPrint_D", "color"),
	("walnut_veneer_02", "walnut_veneer_02_rough_2k.png", "T_RbWalnutPrint_R", "mask"),
	("brown_leather", "brown_leather_albedo_2k.png", "T_RbLeather_D", "color"),
	("brown_leather", "brown_leather_nor_dx_2k.png", "T_RbLeather_N", "normal"),
	("brown_leather", "brown_leather_rough_2k.png", "T_RbLeather_R", "mask"),
]

# Table dimensions for the wear placement (TableSpec: playing half length / width [m]; outer half size of the coin-op cabinet
# = playing half + RailWidthTotal [cm], its plan-corner radius and bed height [cm]; RbTableMeshBuilder / venue-dive-bar 3.1).
NINE_FOOT_HALF = (1.27, 0.635)
SEVEN_FOOT_BAR_HALF = (1.016, 0.508)
SEVEN_FOOT_BAR_OUTER_CM = (118.11, 67.31)
SEVEN_FOOT_BAR_CORNER_CM = 4.0
SEVEN_FOOT_BAR_BED_CM = 74.3
DIVE_BAR_AGE = 0.80          # venue-dive-bar 6.2 (the table carries its own Age scalar)
TOURNAMENT_AGE = 0.30        # the M1 test room's 9-ft table: a kept club table, not new (M2-L r5: 0.15 read as a CG-clean cloth)
BAR_GREEN = (0.022, 0.108, 0.040)   # venue-dive-bar 3.2 bar green (#2F6B40 new) as a used, darkened cloth (linear, card-checked)

MEL = unreal.MaterialEditingLibrary
BALL_RADIUS_CM = 2.8575  # 57.15 mm (WPA), MPC default

# Ball layout (plan 6.2, ESTIMATE until measured on a reference set): number circle ~23 mm, stripe band ~35 mm.
STRIPE_HALF_ANGLE_DEG = 38.0
CIRCLE_HALF_ANGLE_DEG = 24.0

# Cloth dye colours (linear, ESTIMATE from swatches): tournament blue (M_RbCloth default) and classic green (MI).
CLOTH_BLUE = (0.016, 0.085, 0.235)
CLOTH_GREEN = (0.030, 0.150, 0.040)

# --------------------------------------------------------------------------------------------------------------------
# PNG writer (stdlib only)
# --------------------------------------------------------------------------------------------------------------------


def write_png(path: str, width: int, height: int, channels: int, pixels: bytearray) -> None:
	"""8-bit PNG, rows top to bottom, channels 1 (grey), 3 (RGB) or 4 (RGBA)."""
	color_type = {1: 0, 3: 2, 4: 6}[channels]
	stride = width * channels
	raw = bytearray()
	for y in range(height):
		raw.append(0)
		raw.extend(pixels[y * stride:(y + 1) * stride])

	def chunk(tag: bytes, data: bytes) -> bytes:
		return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

	png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0))
	png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "wb") as f:
		f.write(png)


# --------------------------------------------------------------------------------------------------------------------
# TrueType outlines (glyf / loca / cmap format 4 / hmtx), enough for the digits of Roboto
# --------------------------------------------------------------------------------------------------------------------


class TrueTypeFont:
	def __init__(self, path: str):
		with open(path, "rb") as f:
			self.data = f.read()
		num_tables = struct.unpack(">H", self.data[4:6])[0]
		self.tables = {}
		for i in range(num_tables):
			tag, _, offset, length = struct.unpack(">4sIII", self.data[12 + 16 * i:28 + 16 * i])
			self.tables[tag.decode("latin-1")] = (offset, length)
		head = self.tables["head"][0]
		self.units_per_em = struct.unpack(">H", self.data[head + 18:head + 20])[0]
		self.loca_long = struct.unpack(">h", self.data[head + 50:head + 52])[0] == 1
		self.num_glyphs = struct.unpack(">H", self.data[self.tables["maxp"][0] + 4:self.tables["maxp"][0] + 6])[0]
		hhea = self.tables["hhea"][0]
		self.num_hmetrics = struct.unpack(">H", self.data[hhea + 34:hhea + 36])[0]
		self.cmap = self._read_cmap()

	def _u16(self, o: int) -> int:
		return struct.unpack(">H", self.data[o:o + 2])[0]

	def _i16(self, o: int) -> int:
		return struct.unpack(">h", self.data[o:o + 2])[0]

	def _read_cmap(self) -> dict:
		base = self.tables["cmap"][0]
		count = self._u16(base + 2)
		sub = None
		for i in range(count):
			platform, encoding, offset = struct.unpack(">HHI", self.data[base + 4 + 8 * i:base + 12 + 8 * i])
			if self._u16(base + offset) == 4 and (platform, encoding) in ((3, 1), (0, 3), (0, 4)):
				sub = base + offset
				break
		if sub is None:
			rb.fail("font has no format-4 cmap")
		seg_count = self._u16(sub + 6) // 2
		ends = sub + 14
		starts = ends + 2 * seg_count + 2
		deltas = starts + 2 * seg_count
		ranges = deltas + 2 * seg_count
		out = {}
		for ch in "0123456789":
			c = ord(ch)
			for i in range(seg_count):
				end = self._u16(ends + 2 * i)
				start = self._u16(starts + 2 * i)
				if start <= c <= end:
					delta = self._u16(deltas + 2 * i)
					rng = self._u16(ranges + 2 * i)
					if rng == 0:
						gid = (c + delta) & 0xFFFF
					else:
						gid = self._u16(ranges + 2 * i + rng + 2 * (c - start))
						gid = (gid + delta) & 0xFFFF if gid else 0
					out[ch] = gid
					break
		return out

	def advance(self, gid: int) -> int:
		hmtx = self.tables["hmtx"][0]
		return self._u16(hmtx + 4 * min(gid, self.num_hmetrics - 1))

	def _glyph_offset(self, gid: int) -> tuple:
		loca = self.tables["loca"][0]
		if self.loca_long:
			a, b = struct.unpack(">II", self.data[loca + 4 * gid:loca + 4 * gid + 8])
		else:
			a, b = (2 * v for v in struct.unpack(">HH", self.data[loca + 2 * gid:loca + 2 * gid + 4]))
		glyf = self.tables["glyf"][0]
		return glyf + a, b - a

	def contours(self, gid: int, dx: float = 0.0, dy: float = 0.0) -> list:
		"""Contours of a glyph: lists of (x, y, on_curve) in font units, translated by (dx, dy)."""
		off, length = self._glyph_offset(gid)
		if length == 0:
			return []
		n_contours = self._i16(off)
		if n_contours < 0:
			return self._composite(off, dx, dy)
		p = off + 10
		ends = [self._u16(p + 2 * i) for i in range(n_contours)]
		p += 2 * n_contours
		p += 2 + self._u16(p)  # instructions
		n_points = ends[-1] + 1 if ends else 0
		flags = []
		while len(flags) < n_points:
			flag = self.data[p]
			p += 1
			flags.append(flag)
			if flag & 8:
				repeat = self.data[p]
				p += 1
				flags.extend([flag] * repeat)
		xs, ys = [], []
		value = 0
		for flag in flags:
			if flag & 2:
				d = self.data[p]
				p += 1
				value += d if flag & 16 else -d
			elif not flag & 16:
				value += self._i16(p)
				p += 2
			xs.append(value)
		value = 0
		for flag in flags:
			if flag & 4:
				d = self.data[p]
				p += 1
				value += d if flag & 32 else -d
			elif not flag & 32:
				value += self._i16(p)
				p += 2
			ys.append(value)
		out, start = [], 0
		for end in ends:
			out.append([(xs[i] + dx, ys[i] + dy, bool(flags[i] & 1)) for i in range(start, end + 1)])
			start = end + 1
		return out

	def _composite(self, off: int, dx: float, dy: float) -> list:
		p = off + 10
		out = []
		while True:
			flags, gid = self._u16(p), self._u16(p + 2)
			p += 4
			if flags & 1:
				a, b = self._i16(p), self._i16(p + 2)
				p += 4
			else:
				a, b = struct.unpack(">bb", self.data[p:p + 2])
				p += 2
			if flags & 8:
				p += 2
			elif flags & 0x40:
				p += 4
			elif flags & 0x80:
				p += 8
			out += self.contours(gid, dx + (a if flags & 2 else 0), dy + (b if flags & 2 else 0))
			if not flags & 0x20:
				break
		return out


def flatten_contour(points: list, steps: int = 8) -> list:
	"""TrueType quadratic contour -> closed polyline [(x, y), ...] (first point repeated at the end)."""
	expanded = []
	n = len(points)
	for i in range(n):
		cur, nxt = points[i], points[(i + 1) % n]
		expanded.append(cur)
		if not cur[2] and not nxt[2]:
			expanded.append(((cur[0] + nxt[0]) * 0.5, (cur[1] + nxt[1]) * 0.5, True))
	k = next((i for i, p in enumerate(expanded) if p[2]), None)
	if k is None:
		return []
	expanded = expanded[k:] + expanded[:k]
	m = len(expanded)
	out = [(expanded[0][0], expanded[0][1])]
	i = 1
	while i <= m:
		p = expanded[i % m]
		if p[2]:
			out.append((p[0], p[1]))
			i += 1
		else:
			end = expanded[(i + 1) % m]
			x0, y0 = out[-1]
			for s in range(1, steps + 1):
				t = s / steps
				u = 1.0 - t
				out.append((u * u * x0 + 2 * u * t * p[0] + t * t * end[0], u * u * y0 + 2 * u * t * p[1] + t * t * end[1]))
			i += 2
	return out


# --------------------------------------------------------------------------------------------------------------------
# Glyph atlas (signed distance field)
# --------------------------------------------------------------------------------------------------------------------

ATLAS_CELL = 256          # px per cell (4 x 4 cells -> 1024^2)
SDF_SPREAD = 16.0         # px: distance range encoded in 0..1 (0.5 = outline)
DIGIT_HEIGHT_1 = 0.48     # ink height of a one-digit label [cell units]; the circle is inscribed in the cell
DIGIT_HEIGHT_2 = 0.44     # two-digit labels (condensed face)
MAX_LABEL_WIDTH = 0.64
UNDERLINE = (0.765, 0.815)   # underscore of 6 and 9 [cell v, down]
UNDERLINE_WIDTH = 0.30


def label_polylines(font_regular: TrueTypeFont, font_condensed: TrueTypeFont, number: int) -> list:
	"""Closed polylines (cell pixel coordinates, y down) of ball number's label."""
	text = str(number)
	font = font_condensed if len(text) > 1 else font_regular
	contours, pen = [], 0.0
	for ch in text:
		gid = font.cmap[ch]
		contours += font.contours(gid, pen, 0.0)
		pen += font.advance(gid) * (0.96 if len(text) > 1 else 1.0)   # slightly tight tracking for two digits
	polys = [flatten_contour(c) for c in contours]
	polys = [p for p in polys if len(p) > 2]
	xs = [x for p in polys for x, _ in p]
	ys = [y for p in polys for _, y in p]
	x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
	height = DIGIT_HEIGHT_2 if len(text) > 1 else DIGIT_HEIGHT_1
	scale = height / (y1 - y0)
	scale = min(scale, MAX_LABEL_WIDTH / (x1 - x0))
	underlined = number in (6, 9)
	# Labels centred in the circle; 6 / 9 shifted up so digits + underscore are centred together. Horizontally the
	# optical centre: halfway between the bounding box centre and the ink centroid (a "1" would otherwise sit left-heavy).
	centre_v = 0.455 if underlined else 0.5
	area, moment = 0.0, 0.0
	for p in polys:
		for (xa, ya), (xb, yb) in zip(p, p[1:]):
			cross = xa * yb - xb * ya
			area += cross
			moment += (xa + xb) * cross
	ink_cx = moment / (3.0 * area) if abs(area) > 1e-9 else (x0 + x1) * 0.5
	cx, cy = 0.5 * ((x0 + x1) * 0.5 + ink_cx), (y0 + y1) * 0.5
	c = float(ATLAS_CELL)
	out = [[((0.5 + (x - cx) * scale) * c, (centre_v - (y - cy) * scale) * c) for x, y in p] for p in polys]
	if underlined:
		u0, u1 = 0.5 - UNDERLINE_WIDTH * 0.5, 0.5 + UNDERLINE_WIDTH * 0.5
		v0, v1 = UNDERLINE
		# Same winding as the glyph outlines after the y flip (TrueType outer contours are clockwise in y-up).
		out.append([(u0 * c, v0 * c), (u1 * c, v0 * c), (u1 * c, v1 * c), (u0 * c, v1 * c), (u0 * c, v0 * c)])
	return out


def sdf_cell(polys: list) -> list:
	"""Signed distance field of the polygons in one cell (non-zero winding), values 0..255 (128 = outline)."""
	n = ATLAS_CELL
	segments = []
	for p in polys:
		for i in range(len(p) - 1):
			(ax, ay), (bx, by) = p[i], p[i + 1]
			if ax != bx or ay != by:
				segments.append((ax, ay, bx, by))
	# Inside test per row (pixel centres), non-zero winding.
	inside = bytearray(n * n)
	for y in range(n):
		yc = y + 0.5
		crossings = []
		for ax, ay, bx, by in segments:
			if (ay <= yc < by) or (by <= yc < ay):
				x = ax + (yc - ay) * (bx - ax) / (by - ay)
				crossings.append((x, 1 if by > ay else -1))
		crossings.sort()
		winding, k = 0, 0
		for x in range(n):
			xc = x + 0.5
			while k < len(crossings) and crossings[k][0] < xc:
				winding += crossings[k][1]
				k += 1
			if winding != 0:
				inside[y * n + x] = 1
	# Unsigned distance to the outline within the spread, per segment bounding box.
	spread = SDF_SPREAD
	dist = [spread] * (n * n)
	for ax, ay, bx, by in segments:
		dx, dy = bx - ax, by - ay
		len2 = dx * dx + dy * dy
		xa = max(0, int(min(ax, bx) - spread))
		xb = min(n - 1, int(max(ax, bx) + spread) + 1)
		ya = max(0, int(min(ay, by) - spread))
		yb = min(n - 1, int(max(ay, by) + spread) + 1)
		for y in range(ya, yb + 1):
			py = y + 0.5 - ay
			row = y * n
			for x in range(xa, xb + 1):
				px = x + 0.5 - ax
				t = (px * dx + py * dy) / len2
				if t < 0.0:
					t = 0.0
				elif t > 1.0:
					t = 1.0
				ex = px - t * dx
				ey = py - t * dy
				d = math.sqrt(ex * ex + ey * ey)
				if d < dist[row + x]:
					dist[row + x] = d
	out = [0] * (n * n)
	for i in range(n * n):
		sd = dist[i] if inside[i] else -dist[i]
		out[i] = max(0, min(255, int(round((0.5 + sd / (2.0 * spread)) * 255.0))))
	return out


def check_ink_share_table(coverage: dict) -> None:
	"""The rotation smear of RbBall.ush averages the number circle with a per-label ink share (RbInkShareTable); it must
	match the atlas generated here."""
	path = os.path.join(unreal.Paths.project_dir(), "Shaders", "Private", "RbBall.ush")
	with open(path, "r", encoding="utf-8") as f:
		text = f.read()
	start = text.find("RbInkShareTable[16] = {")
	end = text.find("}", start)
	if start < 0 or end < 0:
		rb.fail(f"RbInkShareTable not found in {path}")
	table = [float(v) for v in text[text.find("{", start) + 1:end].replace("\n", " ").split(",") if v.strip()]
	if len(table) != 16:
		rb.fail(f"RbInkShareTable has {len(table)} entries, expected 16")
	for number, share in coverage.items():
		if abs(table[number] - share) > 0.01:
			rb.fail(f"RbInkShareTable[{number}] = {table[number]:.3f} but the atlas measures {share:.3f}: update RbBall.ush")


def build_glyph_atlas(path: str) -> dict:
	regular = TrueTypeFont(os.path.join(FONT_DIR, "Roboto-Bold.ttf"))
	condensed = TrueTypeFont(os.path.join(FONT_DIR, "Roboto-BoldCondensed.ttf"))
	size = 4 * ATLAS_CELL
	pixels = bytearray(size * size)   # cell 0 (cue ball) stays 0 = far outside
	coverage = {}
	for number in range(1, 16):
		cell = sdf_cell(label_polylines(regular, condensed, number))
		cx, cy = (number % 4) * ATLAS_CELL, (number // 4) * ATLAS_CELL
		ink = 0
		for y in range(ATLAS_CELL):
			row = (cy + y) * size + cx
			values = cell[y * ATLAS_CELL:(y + 1) * ATLAS_CELL]
			pixels[row:row + ATLAS_CELL] = bytes(values)
			ink += sum(1 for v in values if v > 127)
		# Share of the number circle (inscribed disc, pi/4 of the cell) covered by ink - RbBall.ush RbInkShare().
		coverage[number] = ink / (math.pi * 0.25 * ATLAS_CELL * ATLAS_CELL)
	write_png(path, size, size, 1, pixels)
	return coverage


# --------------------------------------------------------------------------------------------------------------------
# Cloth weave (plain weave of twisted worsted yarns), tileable
# --------------------------------------------------------------------------------------------------------------------

WEAVE_SIZE = 512
WEAVE_YARNS = 16            # yarns per tile and direction; 0.5 mm spacing -> tile 8 mm (material WeaveScale 125 / m)
WEAVE_TILE_M = 0.008


def _hash01(i: int, j: int) -> float:
	h = (i * 374761393 + j * 668265263) & 0xFFFFFFFF
	h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
	return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0x1000000)


def build_cloth_weave(normal_path: str, mask_path: str) -> None:
	n = WEAVE_SIZE
	pitch = n / WEAVE_YARNS
	half = pitch * 0.5
	yarn_w = 0.90                    # yarn width / pitch (small gaps between yarns)
	lift = 0.30                      # over / under undulation relative to the yarn height
	twist = 3.0                      # fibre twist ridges per yarn pitch along the yarn (kept well below the texel Nyquist)
	heights = [0.0] * (n * n)
	tone = [0.0] * (n * n)
	for y in range(n):
		v = y + 0.5
		j = int(v // pitch) % WEAVE_YARNS
		b = (v - (int(v // pitch) + 0.5) * pitch) / half          # -1..1 across the weft yarn
		for x in range(n):
			u = x + 0.5
			k = int(u // pitch) % WEAVE_YARNS
			a = (u - (int(u // pitch) + 0.5) * pitch) / half      # -1..1 across the warp yarn
			best, best_tone = -0.45, 0.35
			# Warp yarn k runs along v; over the weft where (k + j) is even.
			ta = a / yarn_w
			if abs(ta) < 1.0:
				prof = math.sqrt(1.0 - ta * ta)
				hw = 0.55 * prof + lift * math.cos(math.pi * (v / pitch - 0.5 + k))
				hw += 0.03 * prof * math.sin(2.0 * math.pi * (v / pitch * twist + a * 0.9))
				if hw > best:
					best, best_tone = hw, _hash01(k, 7)
			tb = b / yarn_w
			if abs(tb) < 1.0:
				prof = math.sqrt(1.0 - tb * tb)
				hf = 0.55 * prof - lift * math.cos(math.pi * (u / pitch - 0.5 + j))
				hf += 0.03 * prof * math.sin(2.0 * math.pi * (u / pitch * twist + b * 0.9))
				if hf > best:
					best, best_tone = hf, _hash01(31, j)
			heights[y * n + x] = best
			tone[y * n + x] = best_tone
	h_min, h_max = min(heights), max(heights)
	# Height in pixels for the slopes: a flattened yarn ~0.35 of its half width high.
	height_px = 0.35 * half / max(1e-6, (h_max - h_min)) * 1.6
	normal = bytearray(n * n * 3)
	mask = bytearray(n * n * 3)
	for y in range(n):
		ym, yp = ((y - 1) % n) * n, ((y + 1) % n) * n
		for x in range(n):
			i = y * n + x
			dhdu = (heights[y * n + (x + 1) % n] - heights[y * n + (x - 1) % n]) * 0.5 * height_px
			dhdv = (heights[yp + x] - heights[ym + x]) * 0.5 * height_px
			nx, ny, nz = -dhdu, -dhdv, 1.0
			inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
			normal[3 * i] = int(round((nx * inv * 0.5 + 0.5) * 255.0))
			normal[3 * i + 1] = int(round((ny * inv * 0.5 + 0.5) * 255.0))
			normal[3 * i + 2] = int(round((nz * inv * 0.5 + 0.5) * 255.0))
			cavity = (heights[i] - h_min) / (h_max - h_min)
			mask[3 * i] = int(round((0.35 + 0.65 * cavity) * 255.0))
			mask[3 * i + 1] = int(round((0.80 + 0.06 * (tone[i] - 0.5)) * 255.0))   # roughness (+ composite)
			mask[3 * i + 2] = int(round(tone[i] * 255.0))
	write_png(normal_path, n, n, 3, normal)
	write_png(mask_path, n, n, 3, mask)


# --------------------------------------------------------------------------------------------------------------------
# Texture import
# --------------------------------------------------------------------------------------------------------------------


def import_texture(png: str, name: str, props: dict) -> unreal.Texture2D:
	rb.ensure_dir(TEX_DIR)
	task = unreal.AssetImportTask()
	task.set_editor_property("filename", png)
	task.set_editor_property("destination_path", TEX_DIR)
	task.set_editor_property("destination_name", name)
	task.set_editor_property("automated", True)
	task.set_editor_property("replace_existing", True)
	task.set_editor_property("replace_existing_settings", True)
	task.set_editor_property("save", False)
	unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
	texture = unreal.load_asset(f"{TEX_DIR}/{name}")
	if texture is None:
		rb.fail(f"import of {png} failed")
	for key, value in props.items():
		texture.set_editor_property(key, value)
	unreal.EditorAssetLibrary.save_loaded_asset(texture, False)
	return texture


def make_textures() -> dict:
	os.makedirs(GEN_DIR, exist_ok=True)
	atlas_png = os.path.join(GEN_DIR, "T_RbBallGlyphs.png")
	coverage = build_glyph_atlas(atlas_png)
	rb.log("glyph ink share of the number circle: " + ", ".join(f"{n}: {c:.3f}" for n, c in sorted(coverage.items())))
	check_ink_share_table(coverage)
	normal_png = os.path.join(GEN_DIR, "T_RbClothWeave_N.png")
	mask_png = os.path.join(GEN_DIR, "T_RbClothWeave_M.png")
	build_cloth_weave(normal_png, mask_png)

	atlas = import_texture(atlas_png, "T_RbBallGlyphs", {
		"compression_settings": unreal.TextureCompressionSettings.TC_GRAYSCALE,
		"srgb": False,
		"lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD,
		"address_x": unreal.TextureAddress.TA_CLAMP,
		"address_y": unreal.TextureAddress.TA_CLAMP,
	})
	weave_n = import_texture(normal_png, "T_RbClothWeave_N", {
		"compression_settings": unreal.TextureCompressionSettings.TC_NORMALMAP,
		"srgb": False,
		"flip_green_channel": False,
		"lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP,
		"filter": unreal.TextureFilter.TF_DEFAULT,          # the group's anisotropic filtering
	})
	weave_m = import_texture(mask_png, "T_RbClothWeave_M", {
		"compression_settings": unreal.TextureCompressionSettings.TC_DEFAULT,
		"srgb": False,
		"lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD,
		"filter": unreal.TextureFilter.TF_DEFAULT,
		"composite_texture": weave_n,
		"composite_texture_mode": unreal.CompositeTextureMode.CTM_NORMAL_ROUGHNESS_TO_GREEN,
		"composite_power": 1.0,
	})
	return {"atlas": atlas, "weave_n": weave_n, "weave_m": weave_m}


def _sha256(path: str) -> str:
	h = hashlib.sha256()
	with open(path, "rb") as f:
		for block in iter(lambda: f.read(1 << 20), b""):
			h.update(block)
	return h.hexdigest()


def make_cc0_textures() -> dict:
	"""Imports the pinned CC0 scans of Art/Tables/cc0_inputs.json (SHA-256 checked against the lock file; an input that does not
	match its pin is refused). Without the raw downloads (a fresh clone before Tools/art/fetch_cc0.py) the committed texture
	assets are kept as they are; without those either, the materials fall back to their procedural surfaces (warning)."""
	with open(CC0_LOCK, "r", encoding="utf-8") as f:
		lock = json.load(f)
	out = {}
	for asset_id, filename, name, kind in CC0_TEXTURES:
		src = os.path.join(CC0_DIR, asset_id, filename)
		existing = f"{TEX_DIR}/{name}"
		if not os.path.exists(src):
			if unreal.EditorAssetLibrary.does_asset_exist(existing):
				rb.log(f"{name}: raw CC0 input {src} missing - keeping the committed texture")
				out[name] = unreal.load_asset(existing)
			else:
				unreal.log_warning(f"[rb] {name}: CC0 input {src} missing (run Tools/art/fetch_cc0.py) - procedural fallback")
				out[name] = None
			continue
		pinned = lock.get(f"polyhaven/{asset_id}", {}).get("files", {}).get(filename)
		if pinned is None or _sha256(src) != pinned:
			rb.fail(f"{src}: not pinned in {CC0_LOCK} or SHA-256 mismatch (refusing an unpinned CC0 input)")
		if kind == "normal":
			props = {"compression_settings": unreal.TextureCompressionSettings.TC_NORMALMAP, "srgb": False, "flip_green_channel": False,
				"lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP}
		elif kind == "mask":
			props = {"compression_settings": unreal.TextureCompressionSettings.TC_GRAYSCALE, "srgb": False,
				"lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD}
		else:
			props = {"compression_settings": unreal.TextureCompressionSettings.TC_DEFAULT, "srgb": True,
				"lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD}
		out[name] = import_texture(src, name, props)
	return out


# --------------------------------------------------------------------------------------------------------------------
# Material parameter collection
# --------------------------------------------------------------------------------------------------------------------


def make_mpc() -> unreal.MaterialParameterCollection:
	name = MPC_PATH.rsplit("/", 1)[1]
	mpc = unreal.load_asset(MPC_PATH) if unreal.EditorAssetLibrary.does_asset_exist(MPC_PATH) else None
	if mpc is None:
		tools = unreal.AssetToolsHelpers.get_asset_tools()
		mpc = tools.create_asset(name, MAT_DIR, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
		if mpc is None:
			rb.fail(f"could not create {MPC_PATH}")
	hidden = unreal.LinearColor(0.0, 0.0, -100000.0, 0.0)
	# Keep existing entries (their ids are referenced by the materials); add what is missing, reset the defaults.
	vectors = list(mpc.get_editor_property("vector_parameters"))
	by_name = {str(v.get_editor_property("parameter_name")): v for v in vectors}
	out_vectors = []
	for i in range(16):
		pname = f"Ball{i:02d}"
		entry = by_name.get(pname)
		if entry is None:
			entry = unreal.CollectionVectorParameter()
			entry.set_editor_property("parameter_name", pname)
		entry.set_editor_property("default_value", hidden)
		out_vectors.append(entry)
	scalars = list(mpc.get_editor_property("scalar_parameters"))
	radius = next((s for s in scalars if str(s.get_editor_property("parameter_name")) == "BallRadiusCm"), None)
	if radius is None:
		radius = unreal.CollectionScalarParameter()
		radius.set_editor_property("parameter_name", "BallRadiusCm")
	radius.set_editor_property("default_value", BALL_RADIUS_CM)
	mpc.set_editor_property("vector_parameters", out_vectors)
	mpc.set_editor_property("scalar_parameters", [radius])
	unreal.EditorAssetLibrary.save_loaded_asset(mpc, False)
	return mpc


# --------------------------------------------------------------------------------------------------------------------
# Material graph helpers
# --------------------------------------------------------------------------------------------------------------------


class Graph:
	"""Builds one material: nodes are laid out in columns by the caller's x (only for readability in the editor)."""

	def __init__(self, name: str, **material_props):
		self.path = f"{MAT_DIR}/{name}"
		material = unreal.load_asset(self.path) if unreal.EditorAssetLibrary.does_asset_exist(self.path) else None
		if material is not None and not isinstance(material, unreal.Material):
			rb.delete_asset_if_exists(self.path)
			material = None
		if material is None:
			tools = unreal.AssetToolsHelpers.get_asset_tools()
			material = tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
			if material is None:
				rb.fail(f"could not create {self.path}")
		else:
			MEL.delete_all_material_expressions(material)
		self.m = material
		self.y = 0
		defaults = {"tangent_space_normal": True, "two_sided": False, "blend_mode": unreal.BlendMode.BLEND_OPAQUE}
		defaults.update(material_props)
		for key, value in defaults.items():
			self.m.set_editor_property(key, value)
		self.params = []

	def node(self, cls, x: int = -600, **props):
		self.y += 120
		expression = MEL.create_material_expression(self.m, cls, x, self.y)
		if expression is None:
			rb.fail(f"{self.path}: could not create {cls}")
		for key, value in props.items():
			expression.set_editor_property(key, value)
		return expression

	def link(self, src, dst, pin: str = "", src_pin: str = "") -> None:
		for candidate in (pin, pin.replace(" ", "")):
			if MEL.connect_material_expressions(src, src_pin, dst, candidate):
				return
		rb.fail(f"{self.path}: could not connect {src.get_name()}.{src_pin} -> {dst.get_name()}.{pin}")

	def const(self, value, x: int = -900):
		if isinstance(value, (tuple, list)):
			return self.node(unreal.MaterialExpressionConstant3Vector, x, constant=unreal.LinearColor(value[0], value[1], value[2], 1.0))
		return self.node(unreal.MaterialExpressionConstant, x, r=float(value))

	def scalar(self, name: str, value: float, group: str = "RawBreak", x: int = -1400):
		self.params.append((name, value))
		return self.node(unreal.MaterialExpressionScalarParameter, x, parameter_name=name, default_value=float(value), group=group)

	def vector(self, name: str, rgb, group: str = "RawBreak", x: int = -1400):
		"""Vector parameter + RGB mask (returns the mask)."""
		self.params.append((name, tuple(rgb)))
		p = self.node(unreal.MaterialExpressionVectorParameter, x, parameter_name=name,
			default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0), group=group)
		return self.mask(p, "rgb", x + 200)

	def mask(self, src, channels: str, x: int = -1000, src_pin: str = ""):
		m = self.node(unreal.MaterialExpressionComponentMask, x, r="r" in channels, g="g" in channels, b="b" in channels, a="a" in channels)
		self.link(src, m, "", src_pin)
		return m

	def texcoord(self, index: int = 0, x: int = -1600):
		return self.node(unreal.MaterialExpressionTextureCoordinate, x, coordinate_index=index)

	def binary(self, cls, a, b, x: int = -800):
		node = self.node(cls, x)
		self.link(a, node, "A")
		self.link(b, node, "B")
		return node

	def custom(self, description: str, code: str, inputs: list, include: str, output=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
			extra_outputs: list | None = None, x: int = -500):
		"""inputs: [(name, expression, output_pin)], extra_outputs: [(name, CustomMaterialOutputType)]."""
		node = self.node(unreal.MaterialExpressionCustom, x, description=description, code=code, output_type=output,
			include_file_paths=[include])
		custom_inputs = []
		for name, _, _ in inputs:
			ci = unreal.CustomInput()
			ci.set_editor_property("input_name", name)
			custom_inputs.append(ci)
		node.set_editor_property("inputs", custom_inputs)
		if extra_outputs:
			outs = []
			for name, typ in extra_outputs:
				co = unreal.CustomOutput()
				co.set_editor_property("output_name", name)
				co.set_editor_property("output_type", typ)
				outs.append(co)
			node.set_editor_property("additional_outputs", outs)
		for name, expression, pin in inputs:
			self.link(expression, node, name, pin)
		return node

	def quality(self, default, low=None, medium=None, x: int = -300):
		q = self.node(unreal.MaterialExpressionQualitySwitch, x)
		self.link(default, q, "Default")
		if low is not None:
			self.link(low, q, "Low")
		if medium is not None:
			self.link(medium, q, "Medium")
		return q

	def slab(self, x: int = -100, sss=unreal.MaterialSubSurfaceType.MSS_NONE, **pins):
		"""Substrate slab; pins: python-name -> expression or (expression, output pin)."""
		slab = self.node(unreal.MaterialExpressionSubstrateSlabBSDF, x, sub_surface_type=sss)
		names = {"albedo": "Diffuse Albedo", "f0": "F0", "f90": "F90", "roughness": "Roughness", "anisotropy": "Anisotropy",
			"normal": "Normal", "tangent": "Tangent", "mfp": "SSS MFP", "emissive": "Emissive Color", "second_roughness": "Second Roughness",
			"second_weight": "Second Roughness Weight", "fuzz_roughness": "Fuzz Roughness", "fuzz_amount": "Fuzz Amount",
			"fuzz_color": "Fuzz Color"}
		for key, value in pins.items():
			expression, out_pin = value if isinstance(value, tuple) else (value, "")
			self.link(expression, slab, names[key], out_pin)
		return slab

	def front(self, bsdf, ao=None, ao_pin: str = "") -> None:
		if not MEL.connect_material_property(bsdf, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
			rb.fail(f"{self.path}: could not connect the front material")
		if ao is not None and not MEL.connect_material_property(ao, ao_pin, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION):
			rb.fail(f"{self.path}: could not connect the ambient occlusion")

	def finish(self) -> str:
		MEL.layout_material_expressions(self.m)
		MEL.recompile_material(self.m)
		unreal.EditorAssetLibrary.save_loaded_asset(self.m, False)
		rb.log(f"{self.path}: " + ", ".join(f"{n}={v}" for n, v in self.params))
		return self.path


# --------------------------------------------------------------------------------------------------------------------
# Materials
# --------------------------------------------------------------------------------------------------------------------


def make_ball(textures: dict) -> str:
	g = Graph("M_RbBall")
	local = g.node(unreal.MaterialExpressionLocalPosition, -1800)
	number = g.scalar("BallNumber", 1.0, "Ball")
	color = g.vector("BallColor", (0.75, 0.50, 0.02), "Ball")
	white = g.vector("WhiteColor", (0.80, 0.78, 0.72), "Ball")
	omega = g.node(unreal.MaterialExpressionVectorParameter, -1400, parameter_name="BallOmegaLocal",
		default_value=unreal.LinearColor(0.0, 0.0, 0.0, 0.0), group="Ball")
	g.params.append(("BallOmegaLocal", (0.0, 0.0, 0.0)))
	omega_rgb = g.mask(omega, "rgb", -1200)
	exposure = g.scalar("ExposureTime", 1.0 / 120.0, "Ball")
	stripe = g.scalar("StripeHalfAngleDeg", STRIPE_HALF_ANGLE_DEG, "Layout")
	circle = g.scalar("CircleHalfAngleDeg", CIRCLE_HALF_ANGLE_DEG, "Layout")
	dots = g.scalar("CueBallDots", 0.0, "Layout")
	atlas = g.node(unreal.MaterialExpressionTextureObjectParameter, -1400, parameter_name="GlyphAtlas", texture=textures["atlas"],
		sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, group="Layout")
	albedo = g.custom("RbBallAlbedo",
		"return RbBallAlbedo(P, BallNumber, BaseColor, White, OmegaLocal, ExposureTime, StripeHalfAngleDeg, CircleHalfAngleDeg, "
		"CueBallDots, GlyphAtlas, GlyphAtlasSampler);",
		[("P", local, ""), ("BallNumber", number, ""), ("BaseColor", color, ""), ("White", white, ""), ("OmegaLocal", omega_rgb, ""),
			("ExposureTime", exposure, ""), ("StripeHalfAngleDeg", stripe, ""), ("CircleHalfAngleDeg", circle, ""),
			("CueBallDots", dots, ""), ("GlyphAtlas", atlas, "")], INC_BALL)

	# Worn bar balls (M2-L, venue-dive-bar 6.4): grime, collision scuffs, chalk and a per-ball roughness in
	# [RoughnessMin, RoughnessMax] seeded by the number. Defaults 0 = a clean set (the M1 balls are unchanged).
	roughness = g.scalar("Roughness", 0.04, "Resin")         # Ra 0.03 um: optically smooth
	worn = g.custom("RbBallWear",
		"float R = Roughness;\nconst float3 A = RbBallWear(P, BallNumber, Albedo, Dirt, Chalk, RoughnessMin, RoughnessMax, R);\n"
		"OutRoughness = R;\nreturn A;",
		[("P", local, ""), ("BallNumber", number, ""), ("Albedo", albedo, ""), ("Dirt", g.scalar("Dirt", 0.0, "Wear"), ""),
			("Chalk", g.scalar("Chalk", 0.0, "Wear"), ""), ("RoughnessMin", g.scalar("RoughnessMin", 0.0, "Wear"), ""),
			("RoughnessMax", g.scalar("RoughnessMax", 0.0, "Wear"), ""), ("Roughness", roughness, "")], INC_BALL,
		extra_outputs=[("OutRoughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])

	# Resin SSS (plan 6.2: MFP 0.5-2 mm, light balls further); High / Epic only (plan 9.4).
	radius = g.scalar("BallRadiusCm", BALL_RADIUS_CM, "Ball")
	mfp_fraction = g.scalar("MfpFraction", 0.035, "Resin")
	mfp = g.custom("RbBallMfp", "return RbBallMfp(Albedo, BallRadiusCm, MfpFraction);",
		[("Albedo", worn, ""), ("BallRadiusCm", radius, ""), ("MfpFraction", mfp_fraction, "")], INC_BALL)
	zero3 = g.const((0.0, 0.0, 0.0))
	zero = g.const(0.0)
	mfp_q = g.quality(mfp, low=zero3, medium=zero3)

	# Analytic sphere normal (plan 6.7): the local position direction, transformed to world (world-space normal input).
	n_local = g.node(unreal.MaterialExpressionNormalize, -1500)
	g.link(local, n_local, "VectorInput")
	n_world = g.node(unreal.MaterialExpressionTransform, -1300, transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
		transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
	g.link(n_local, n_world, "")
	normal = g.node(unreal.MaterialExpressionNormalize, -1100)
	g.link(n_world, normal, "VectorInput")

	f0 = g.scalar("F0", 0.049, "Resin")                     # n = 1.57 (T1)
	haze_r = g.scalar("HazeRoughness", 0.20, "Resin")
	haze_w = g.scalar("HazeWeight", 0.18, "Resin")
	haze_q = g.quality(haze_w, low=zero)                     # haze lobe off on Low (plan 9.4)
	slab = g.slab(sss=unreal.MaterialSubSurfaceType.MSS_DIFFUSION, albedo=worn, f0=f0, roughness=(worn, "OutRoughness"), second_roughness=haze_r,
		second_weight=haze_q, mfp=mfp_q, normal=normal)
	g.m.set_editor_property("tangent_space_normal", False)
	g.front(slab)
	return g.finish()


def make_cloth(textures: dict, mpc) -> str:
	g = Graph("M_RbCloth", used_with_nanite=True)
	uv = g.texcoord(0)                                            # metres (UE-1 table meshes)
	scale = g.scalar("WeaveScale", 1.0 / WEAVE_TILE_M, "Weave")
	uv_w = g.binary(unreal.MaterialExpressionMultiply, uv, scale)
	n_tex = g.node(unreal.MaterialExpressionTextureSampleParameter2D, -1000, parameter_name="WeaveNormal", texture=textures["weave_n"],
		sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, group="Weave")
	g.link(uv_w, n_tex, "UVs")
	m_tex = g.node(unreal.MaterialExpressionTextureSampleParameter2D, -1000, parameter_name="WeaveMask", texture=textures["weave_m"],
		sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, group="Weave")
	g.link(uv_w, m_tex, "UVs")
	color = g.vector("ClothColor", CLOTH_BLUE, "Cloth")   # tournament blue, luminance 0.086 (18.7: 0.05-0.10, card-checked)
	strength = g.scalar("WeaveNormalStrength", 0.55, "Weave")
	lighten = g.scalar("FuzzLighten", 1.35, "Cloth")
	surface = g.custom("RbCloth",
		f"const float Resolved = RbClothWeaveResolved(UV, {WEAVE_TILE_M / (WEAVE_YARNS / 2)!r});\n"
		"Normal = RbClothNormal(NormalTS, WeaveNormalStrength, Resolved);\n"
		"Roughness = saturate(Mask.g + 0.06 * (1.0 - Resolved));\n"
		"const float3 Albedo = RbClothAlbedo(ClothColor, Mask.r, Mask.b, UV, Resolved * WeaveContrast);\n"
		"FuzzColor = RbClothFuzzColor(Albedo, FuzzLighten);\n"
		"return Albedo;",
		[("NormalTS", n_tex, "RGB"), ("Mask", m_tex, "RGB"), ("ClothColor", color, ""), ("UV", uv, ""), ("WeaveNormalStrength", strength, ""),
			("FuzzLighten", lighten, ""), ("WeaveContrast", g.scalar("WeaveContrast", 1.0, "Weave"), "")], INC_SURF,
		extra_outputs=[("Normal", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("Roughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
			("FuzzColor", unreal.CustomMaterialOutputType.CMOT_FLOAT3)])
	fuzz = g.scalar("FuzzAmount", 0.25, "Cloth")
	fuzz_r = g.scalar("FuzzRoughness", 0.5, "Cloth")
	aniso = g.scalar("Anisotropy", 0.15, "Cloth")
	f0 = g.scalar("F0", 0.04, "Cloth")

	# M2-L wear (RbTableLook.ush): placed from the table-local position; the view direction in the same frame for the nap.
	local = g.node(unreal.MaterialExpressionLocalPosition, -1800)
	cam = g.node(unreal.MaterialExpressionTransform, -1300, transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
		transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
	g.link(g.node(unreal.MaterialExpressionCameraVectorWS, -1500), cam, "")
	wear = g.custom("RbClothWear",
		"float3 A = Albedo;\nfloat R = Roughness;\nfloat3 N = NormalTS;\nfloat F = Fuzz;\nfloat3 FC = FuzzColor;\n"
		"RbClothWear(P, normalize(Cam), HalfLength, HalfWidth, CushionWidth, FaceBase, Age, Napped, NapSheen, Seed, A, R, N, F, FC);\n"
		"OutRoughness = R;\nOutNormal = N;\nOutFuzz = F;\nOutFuzzColor = FC;\nreturn A;",
		[("Albedo", surface, "return"), ("Roughness", surface, "Roughness"), ("NormalTS", surface, "Normal"), ("Fuzz", fuzz, ""),
			("FuzzColor", surface, "FuzzColor"), ("P", local, ""), ("Cam", cam, ""),
			("HalfLength", g.scalar("HalfLength", NINE_FOOT_HALF[0], "Table"), ""), ("HalfWidth", g.scalar("HalfWidth", NINE_FOOT_HALF[1], "Table"), ""),
			("CushionWidth", g.scalar("CushionWidth", 0.0508, "Table"), ""), ("FaceBase", g.scalar("FaceBase", 0.02032, "Table"), ""),
			("Age", g.scalar("Age", TOURNAMENT_AGE, "Wear"), ""), ("Napped", g.scalar("Napped", 0.0, "Wear"), ""),
			("NapSheen", g.scalar("NapSheen", 0.30, "Wear"), ""), ("Seed", g.scalar("Seed", 3.0, "Wear"), "")], INC_LOOK,
		extra_outputs=[("OutRoughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1), ("OutNormal", unreal.CustomMaterialOutputType.CMOT_FLOAT3),
			("OutFuzz", unreal.CustomMaterialOutputType.CMOT_FLOAT1), ("OutFuzzColor", unreal.CustomMaterialOutputType.CMOT_FLOAT3)])
	slab = g.slab(albedo=(wear, "return"), f0=f0, roughness=(wear, "OutRoughness"), normal=(wear, "OutNormal"), anisotropy=aniso,
		fuzz_amount=(wear, "OutFuzz"), fuzz_roughness=fuzz_r, fuzz_color=(wear, "OutFuzzColor"))

	# Analytic ball occlusion (plan 6.5) -> material AO (indirect diffuse only).
	world = g.node(unreal.MaterialExpressionWorldPosition, -1600)
	vnormal = g.node(unreal.MaterialExpressionVertexNormalWS, -1600)
	radius = g.node(unreal.MaterialExpressionCollectionParameter, -1600, collection=mpc)
	radius.set_editor_property("parameter_name", "BallRadiusCm")
	balls = []
	for i in range(16):
		p = g.node(unreal.MaterialExpressionCollectionParameter, -1600, collection=mpc)
		p.set_editor_property("parameter_name", f"Ball{i:02d}")
		balls.append((f"B{i}", p, ""))
	listing = ", ".join(f"B{i}" for i in range(16))
	strength = g.scalar("BallOcclusion", 1.0, "Cloth")      # 0 = off (look-dev A/B of the analytic occlusion)
	occ = g.custom("RbBallOcclusion",
		f"float4 Balls[16] = {{ {listing} }};\nreturn lerp(1.0, RbBallOcclusion(P, N, Balls, R), Strength);",
		[("P", world, ""), ("N", vnormal, ""), ("R", radius, ""), ("Strength", strength, "")] + balls, INC_OCC,
		output=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
	g.front(slab, ao=occ)
	return g.finish()


def make_cloth_instance(name: str, color) -> str:
	path = f"{MAT_DIR}/{name}"
	mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
	if mi is None:
		tools = unreal.AssetToolsHelpers.get_asset_tools()
		mi = tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	MEL.set_material_instance_parent(mi, unreal.load_asset(f"{MAT_DIR}/M_RbCloth"))
	MEL.set_material_instance_vector_parameter_value(mi, "ClothColor", unreal.LinearColor(color[0], color[1], color[2], 1.0))
	MEL.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
	rb.log(f"{path}: ClothColor={color}")
	return path


def texture_param(g: Graph, name: str, texture, uv, kind: str, group: str):
	sampler = {"color": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "normal": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
		"mask": unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE}[kind]
	node = g.node(unreal.MaterialExpressionTextureSampleParameter2D, -1000, parameter_name=name, texture=texture, sampler_type=sampler, group=group)
	g.link(uv, node, "UVs")
	return node


def make_rail_wood(cc0: dict) -> str:
	"""Lacquered rail wood (plan 6.4): clear-coat slab (F0 0.04, gloss, 80 um, slight amber) vertically layered over wood. M2-L: the
	wood is the CC0 walnut veneer scan (grain along U = along the rail when GrainAlongU = 1; the legs' instance turns it vertical),
	stained darker by StainTint; the procedural figure of M1 when the scan is missing."""
	g = Graph("M_RbRailWood", used_with_nanite=True)
	uv = g.texcoord(0)
	aniso = g.scalar("GrainAnisotropy", 0.30, "Wood")
	if cc0.get("T_RbWalnutVeneer_D") is not None:
		along = g.scalar("GrainAlongU", 1.0, "Wood")
		size = g.scalar("VeneerSizeM", 0.85, "Wood")
		tuv = g.custom("RbVeneerUV", "return lerp(UV, UV.yx, step(0.5, Along)) / max(Size, 0.01);",
			[("UV", uv, ""), ("Along", along, ""), ("Size", size, "")], INC_SURF, output=unreal.CustomMaterialOutputType.CMOT_FLOAT2)
		diff = texture_param(g, "VeneerColor", cc0["T_RbWalnutVeneer_D"], tuv, "color", "Wood")
		nrm = texture_param(g, "VeneerNormal", cc0["T_RbWalnutVeneer_N"], tuv, "normal", "Wood") if cc0.get("T_RbWalnutVeneer_N") else None
		rgh = texture_param(g, "VeneerRoughness", cc0["T_RbWalnutVeneer_R"], tuv, "mask", "Wood") if cc0.get("T_RbWalnutVeneer_R") else None
		stain = g.vector("StainTint", (0.14, 0.10, 0.08), "Wood")   # dark stained walnut under the coat (linear, ESTIMATE; r1: 0.25 read as pine)
		inputs = [("Veneer", diff, "RGB"), ("Stain", stain, ""), ("Along", along, ""), ("Strength", g.scalar("VeneerNormalStrength", 0.5, "Wood"), "")]
		inputs += [("NormalTex", nrm, "RGB")] if nrm else []
		inputs += [("RoughTex", rgh, "R")] if rgh else []
		wood = g.custom("RbVeneer",
			("float3 N = " + ("NormalTex" if nrm else "float3(0.0, 0.0, 1.0)") + ";\n"
			"N = Along > 0.5 ? float3(N.y, N.x, N.z) : N;   // swapped UVs mirror the tangent frame\n"
			"Normal = normalize(float3(N.xy * Strength, max(N.z, 1e-3)));\n"
			"Roughness = lerp(0.38, 0.62, " + ("RoughTex" if rgh else "0.5") + ");\n"
			"return Veneer * Stain;"),
			inputs, INC_SURF,
			extra_outputs=[("Normal", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("Roughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
		base = g.slab(albedo=wood, f0=g.const(0.04), roughness=(wood, "Roughness"), normal=(wood, "Normal"), anisotropy=aniso)
	else:
		light = g.vector("WoodLight", (0.105, 0.058, 0.032), "Wood")   # stained walnut (linear, ESTIMATE)
		dark = g.vector("WoodDark", (0.046, 0.022, 0.011), "Wood")
		rings = g.scalar("RingsPerMetre", 160.0, "Wood")
		wood_r = g.scalar("WoodRoughness", 0.50, "Wood")
		wood = g.custom("RbWood", "return RbWoodSurface(UV, Light, Dark, RingsPerMetre, Roughness);",
			[("UV", uv, ""), ("Light", light, ""), ("Dark", dark, ""), ("RingsPerMetre", rings, ""), ("Roughness", wood_r, "")], INC_SURF,
			output=unreal.CustomMaterialOutputType.CMOT_FLOAT4)
		base = g.slab(albedo=g.mask(wood, "rgb"), f0=g.const(0.04), roughness=g.mask(wood, "a"), anisotropy=aniso)
	coat = make_coat(g, (0.93, 0.86, 0.72), 0.008, 0.07)
	layer = g.node(unreal.MaterialExpressionSubstrateVerticalLayering, 100)
	g.link(coat[0], layer, "Top")
	g.link(base, layer, "Bottom")
	g.link(coat[1], layer, "Top Thickness", "Thickness")
	g.front(layer)
	return g.finish()


def make_coat(g: Graph, transmittance, thickness_cm: float, roughness: float, prefix: str = "Coat"):
	"""Clear coat slab: F0 0.04 (n = 1.5), no scattering (albedo 0), coloured transmittance through its thickness."""
	trans = g.vector(prefix + "Transmittance", transmittance, "Coat")
	thick = g.scalar(prefix + "ThicknessCm", thickness_cm, "Coat")
	to_mfp = g.node(unreal.MaterialExpressionSubstrateTransmittanceToMFP, -300)
	g.link(trans, to_mfp, "Transmittance Color")
	g.link(thick, to_mfp, "Thickness")
	rough = g.scalar(prefix + "Roughness", roughness, "Coat")
	coat = g.slab(-100, sss=unreal.MaterialSubSurfaceType.MSS_SIMPLE_VOLUME, albedo=g.const((0.0, 0.0, 0.0)), f0=g.const(0.04),
		roughness=rough, mfp=(to_mfp, "MFP"))
	return coat, to_mfp


def make_simple(name: str, albedo, roughness: float, f0: float = 0.04, nanite: bool = True) -> str:
	g = Graph(name, used_with_nanite=nanite)
	slab = g.slab(albedo=g.vector("Albedo", albedo), f0=g.scalar("F0", f0), roughness=g.scalar("Roughness", roughness))
	g.front(slab)
	return g.finish()


def make_leather(name: str, color, roughness: float) -> str:
	"""Leather (plan 6.4): slab + fuzz 0.1, dark brown, pebbled grain normal."""
	g = Graph(name, used_with_nanite=True)
	uv = g.texcoord(0)
	strength = g.scalar("GrainStrength", 0.12, "Leather")
	grain = g.custom("RbLeather", "const float4 L = RbLeather(UV, Strength);\nCavity = L.w;\nreturn L.xyz;",
		[("UV", uv, ""), ("Strength", strength, "")], INC_SURF,
		extra_outputs=[("Cavity", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
	col = g.vector("LeatherColor", color, "Leather")
	albedo = g.node(unreal.MaterialExpressionMultiply, -300)
	g.link(col, albedo, "A")
	g.link(grain, albedo, "B", "Cavity")
	fuzz_col = g.binary(unreal.MaterialExpressionMultiply, col, g.const(1.6))
	slab = g.slab(albedo=albedo, f0=g.const(0.04), roughness=g.scalar("Roughness", roughness, "Leather"), normal=(grain, "return"),
		fuzz_amount=g.scalar("FuzzAmount", 0.10, "Leather"), fuzz_roughness=g.const(0.6), fuzz_color=fuzz_col)
	g.front(slab)
	return g.finish()


def make_pocket_leather(cc0: dict) -> str:
	"""9-ft leather drop pockets (plan 6.4: slab + fuzz 0.1, dark brown, roughness 0.5-0.7): the CC0 brown_leather scan, darkened;
	the procedural pebbled leather of M1 when the scan is missing."""
	if cc0.get("T_RbLeather_D") is None:
		return make_leather("M_RbPocketLiner", (0.045, 0.028, 0.018), 0.55)
	g = Graph("M_RbPocketLiner", used_with_nanite=True)
	uv = g.binary(unreal.MaterialExpressionDivide, g.texcoord(0), g.scalar("LeatherSizeM", 0.45, "Leather"))
	diff = texture_param(g, "LeatherColor", cc0["T_RbLeather_D"], uv, "color", "Leather")
	nrm = texture_param(g, "LeatherNormal", cc0["T_RbLeather_N"], uv, "normal", "Leather")
	rgh = texture_param(g, "LeatherRoughness", cc0["T_RbLeather_R"], uv, "mask", "Leather")
	tint = g.vector("LeatherTint", (0.50, 0.55, 0.62), "Leather")   # the scan (mean 0.084, 0.028, 0.006) toward a deep brown
	local = g.node(unreal.MaterialExpressionLocalPosition, -1800)
	surf = g.custom("RbPocketLeather",
		"Normal = normalize(float3(N.xy * Strength, max(N.z, 1e-3)));\n"
		"const float K = RbPocketCavity(P, CavityDepthCm, CavityFloor);\n"
		"Roughness = lerp(0.85, lerp(0.45, 0.75, R), K);\nF0K = 0.04 * K;\nF90K = K;\nreturn C * Tint * K;",
		[("C", diff, "RGB"), ("N", nrm, "RGB"), ("R", rgh, "R"), ("Tint", tint, ""), ("Strength", g.scalar("NormalStrength", 0.8, "Leather"), ""),
			("P", local, ""), ("CavityDepthCm", g.scalar("CavityDepthCm", 7.0, "Pocket"), ""), ("CavityFloor", g.scalar("CavityFloor", 0.25, "Pocket"), "")],
		INC_LOOK, extra_outputs=[("Normal", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("Roughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
			("F0K", unreal.CustomMaterialOutputType.CMOT_FLOAT1), ("F90K", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
	fuzz_col = g.binary(unreal.MaterialExpressionMultiply, surf, g.const(1.6))
	slab = g.slab(albedo=surf, f0=(surf, "F0K"), f90=(surf, "F90K"), roughness=(surf, "Roughness"), normal=(surf, "Normal"),
		fuzz_amount=g.scalar("FuzzAmount", 0.10, "Leather"), fuzz_roughness=g.const(0.6), fuzz_color=fuzz_col)
	g.front(slab)
	return g.finish()


def make_laminate(cc0: dict) -> str:
	"""Bar-table laminate (plan 6.4 "bar-table laminate": single slab, roughness 0.3-0.5; venue-dive-bar 3.1 / 6.4): a flat colour
	(black rail caps) or a printed woodgrain (the cabinet's "walnut", the CC0 walnut_veneer_02 scan as the print, UsePrint = 1),
	with burns, glass rings, scratches, and on the cabinet the worn-through corners and kick grime (RbLaminateWear)."""
	g = Graph("M_RbLaminate", used_with_nanite=True)
	uv = g.texcoord(0)
	local = g.node(unreal.MaterialExpressionLocalPosition, -1800)
	inputs = [("UV", uv, ""), ("P", local, ""), ("BaseColor", g.vector("BaseColor", (0.022, 0.021, 0.021), "Laminate"), ""),
		("BaseRoughness", g.scalar("Roughness", 0.40, "Laminate"), ""), ("UsePrint", g.scalar("UsePrint", 0.0, "Laminate"), ""),
		("PrintTint", g.vector("PrintTint", (0.42, 0.33, 0.27), "Laminate"), ""),
		("Age", g.scalar("Age", DIVE_BAR_AGE, "Wear"), ""), ("Burns", g.scalar("Burns", 0.35, "Wear"), ""),
		("Rings", g.scalar("Rings", 0.30, "Wear"), ""), ("Scratches", g.scalar("Scratches", 0.45, "Wear"), ""),
		("CornerWear", g.scalar("CornerWear", 0.0, "Wear"), ""),
		("HalfOuterCm", g.mask(g.node(unreal.MaterialExpressionVectorParameter, -1400, parameter_name="HalfOuterCm",
			default_value=unreal.LinearColor(SEVEN_FOOT_BAR_OUTER_CM[0], SEVEN_FOOT_BAR_OUTER_CM[1], 0.0, 0.0), group="Table"), "rg"), ""),
		("CornerRadiusCm", g.scalar("CornerRadiusCm", SEVEN_FOOT_BAR_CORNER_CM, "Table"), ""),
		("BedHeightCm", g.scalar("BedHeightCm", SEVEN_FOOT_BAR_BED_CM, "Table"), ""), ("Seed", g.scalar("Seed", 5.0, "Wear"), "")]
	g.params.append(("HalfOuterCm", SEVEN_FOOT_BAR_OUTER_CM))
	print_code = "float3 PrintC = float3(0.2, 0.12, 0.07);\nfloat PrintR = 0.5;\n"
	if cc0.get("T_RbWalnutPrint_D") is not None:
		puv = g.binary(unreal.MaterialExpressionDivide, uv, g.scalar("PrintSizeM", 0.9, "Laminate"))
		inputs.append(("PrintTex", texture_param(g, "PrintColor", cc0["T_RbWalnutPrint_D"], puv, "color", "Laminate"), "RGB"))
		print_code = "float3 PrintC = PrintTex;\nfloat PrintR = 0.5;\n"
		if cc0.get("T_RbWalnutPrint_R") is not None:
			inputs.append(("PrintRough", texture_param(g, "PrintRoughness", cc0["T_RbWalnutPrint_R"], puv, "mask", "Laminate"), "R"))
			print_code = "float3 PrintC = PrintTex;\nfloat PrintR = PrintRough;\n"
	surf = g.custom("RbLaminate",
		print_code +
		"float3 A = lerp(BaseColor, PrintC * PrintTint, UsePrint);\n"
		"float R = saturate(BaseRoughness + UsePrint * 0.12 * (PrintR - 0.5));\n"
		"float3 N = float3(0.0, 0.0, 1.0);\n"
		"RbLaminateWear(UV, P, Age, Burns, Rings, Scratches, CornerWear, HalfOuterCm, CornerRadiusCm, BedHeightCm, Seed, A, R, N);\n"
		"Roughness = R;\nNormal = N;\nreturn A;",
		inputs, INC_LOOK,
		extra_outputs=[("Normal", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("Roughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
	slab = g.slab(albedo=surf, f0=g.const(0.04), roughness=(surf, "Roughness"), normal=(surf, "Normal"))
	g.front(slab)
	return g.finish()


def make_plastic() -> str:
	"""Black satin ABS of the coin-op castings (venue-dive-bar 3.1: "black ABS, satin, chipped"; RbPlasticWear)."""
	g = Graph("M_RbPlasticABS", used_with_nanite=True)
	surf = g.custom("RbPlastic",
		"float3 A = Albedo;\nfloat R = BaseRoughness;\nfloat3 N = float3(0.0, 0.0, 1.0);\n"
		"RbPlasticWear(UV, Age, Chips, Seed, A, R, N);\nRoughness = R;\nNormal = N;\nreturn A;",
		[("UV", g.texcoord(0), ""), ("Albedo", g.vector("Albedo", (0.022, 0.022, 0.024), "Plastic"), ""),
			("BaseRoughness", g.scalar("Roughness", 0.26, "Plastic"), ""), ("Age", g.scalar("Age", DIVE_BAR_AGE, "Wear"), ""),
			("Chips", g.scalar("Chips", 0.35, "Wear"), ""), ("Seed", g.scalar("Seed", 2.0, "Wear"), "")], INC_LOOK,
		extra_outputs=[("Normal", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("Roughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
	slab = g.slab(albedo=surf, f0=g.const(0.045), roughness=(surf, "Roughness"), normal=(surf, "Normal"))
	g.front(slab)
	return g.finish()


def make_metal(name: str, f0, roughness: float, anisotropy: float, pitting: float, age: float) -> str:
	"""Metal slab (venue-dive-bar 6.3 metals: F0 linear, roughness, anisotropy for brushed aluminium) with fingerprints / smudges,
	pitting and oxide dullness scaled by Age (RbMetalWear)."""
	g = Graph(name, used_with_nanite=True)
	f0v = g.vector("F0", f0, "Metal")
	wear = g.custom("RbMetal", "float R = BaseRoughness;\nconst float K = RbMetalWear(UV, Age, Pitting, Seed, R);\nRoughness = R;\nreturn F0 * K;",
		[("UV", g.texcoord(0), ""), ("F0", f0v, ""), ("BaseRoughness", g.scalar("Roughness", roughness, "Metal"), ""),
			("Age", g.scalar("Age", age, "Wear"), ""), ("Pitting", g.scalar("Pitting", pitting, "Wear"), ""), ("Seed", g.scalar("Seed", 4.0, "Wear"), "")],
		INC_LOOK, extra_outputs=[("Roughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
	slab = g.slab(albedo=g.const((0.0, 0.0, 0.0)), f0=wear, f90=g.const((1.0, 1.0, 1.0)), roughness=(wear, "Roughness"),
		anisotropy=g.scalar("Anisotropy", anisotropy, "Metal"))
	g.front(slab)
	return g.finish()


def make_plexi() -> str:
	"""Ball-trap window: scratched, grimy clear acrylic in front of the dark trap. M2 approximation: an opaque dark gloss slab (the
	balls in the trap are not rendered yet; a translucent pane and the trap contents are a later step, Docs/references/table-lookdev.md)."""
	g = Graph("M_RbPlexi", used_with_nanite=True)
	rough = g.custom("RbPlexi", "return RbPlexiRoughness(UV, Age, BaseRoughness, Seed);",
		[("UV", g.texcoord(0), ""), ("Age", g.scalar("Age", DIVE_BAR_AGE, "Wear"), ""), ("BaseRoughness", g.scalar("Roughness", 0.04, "Plexi"), ""),
			("Seed", g.scalar("Seed", 6.0, "Wear"), "")], INC_LOOK, output=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
	slab = g.slab(albedo=g.vector("Albedo", (0.006, 0.006, 0.007), "Plexi"), f0=g.const(0.04), roughness=rough)
	g.front(slab)
	return g.finish()


def make_rubber() -> str:
	"""Cushion rubber / pocket liner rubber (plan 6.4: albedo 0.02-0.04, roughness 0.6) with the pale bloom of old rubber (Age) and
	the pocket cavity (RbPocketCavity: below the cloth the liners and gully throats darken with depth; the cushion strip is above)."""
	g = Graph("M_RbCushionRubber", used_with_nanite=True)
	local = g.node(unreal.MaterialExpressionLocalPosition, -1800)
	surf = g.custom("RbRubber",
		"float R = BaseRoughness;\nconst float3 A = RbRubberWear(UV, Age, Albedo, R);\n"
		"const float K = RbPocketCavity(P, CavityDepthCm, CavityFloor);\nRoughness = lerp(0.85, R, K);\nF0K = 0.04 * K;\nF90K = K;\nreturn A * K;",
		[("UV", g.texcoord(0), ""), ("Albedo", g.vector("Albedo", (0.022, 0.022, 0.022), "Rubber"), ""),
			("BaseRoughness", g.scalar("Roughness", 0.60, "Rubber"), ""), ("Age", g.scalar("Age", TOURNAMENT_AGE, "Wear"), ""), ("P", local, ""),
			("CavityDepthCm", g.scalar("CavityDepthCm", 4.0, "Pocket"), ""), ("CavityFloor", g.scalar("CavityFloor", 0.20, "Pocket"), "")], INC_LOOK,
		extra_outputs=[("Roughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1), ("F0K", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
			("F90K", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
	slab = g.slab(albedo=surf, f0=(surf, "F0K"), f90=(surf, "F90K"), roughness=(surf, "Roughness"))
	g.front(slab)
	return g.finish()


def make_instance(name: str, parent: str, scalars: dict | None = None, vectors: dict | None = None) -> str:
	"""A material instance constant of a generated parent (idempotent: parent and every listed parameter are reset each run)."""
	path = f"{MAT_DIR}/{name}"
	mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
	if mi is not None and not isinstance(mi, unreal.MaterialInstanceConstant):
		rb.delete_asset_if_exists(path)
		mi = None
	if mi is None:
		tools = unreal.AssetToolsHelpers.get_asset_tools()
		mi = tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
		if mi is None:
			rb.fail(f"could not create {path}")
	parent_asset = unreal.load_asset(f"{MAT_DIR}/{parent}")
	if parent_asset is None:
		rb.fail(f"{name}: parent {parent} missing")
	MEL.set_material_instance_parent(mi, parent_asset)
	MEL.clear_all_material_instance_parameters(mi)
	for key, value in (scalars or {}).items():
		MEL.set_material_instance_scalar_parameter_value(mi, key, float(value))
	for key, value in (vectors or {}).items():
		MEL.set_material_instance_vector_parameter_value(mi, key, unreal.LinearColor(value[0], value[1], value[2], value[3] if len(value) > 3 else 1.0))
	MEL.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
	rb.log(f"{path} ({parent}): " + ", ".join(f"{k}={v}" for k, v in {**(scalars or {}), **(vectors or {})}.items()))
	return path


def make_brass() -> str:
	"""Brass metal slab (plan 6.4): F0 (0.910, 0.778, 0.423), roughness 0.2-0.4 with handling smudges."""
	g = Graph("M_RbBrass", used_with_nanite=True)
	world = g.node(unreal.MaterialExpressionWorldPosition, -1600)
	base_r = g.scalar("Roughness", 0.28, "Metal")
	rough = g.custom("RbBrassRoughness", "return RbBrassRoughness(P, BaseRoughness);", [("P", world, ""), ("BaseRoughness", base_r, "")],
		INC_SURF, output=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
	slab = g.slab(albedo=g.const((0.0, 0.0, 0.0)), f0=g.vector("F0", (0.910, 0.778, 0.423), "Metal"), f90=g.const((1.0, 1.0, 1.0)),
		roughness=rough)
	g.front(slab)
	return g.finish()


def make_sight() -> str:
	"""Mother-of-pearl sights (plan 6.4): slab + Substrate thin film 300-600 nm (iridescence), low roughness."""
	# UE-1 bakes the thin sights without Nanite today; Nanite usage stays allowed so a later bake setting cannot fall back to
	# the default material.
	g = Graph("M_RbSight", used_with_nanite=True)
	uv = g.texcoord(0)
	nacre = g.custom("RbNacre", "const float2 N = RbNacre(UV);\nTint = N.y;\nreturn N.x;", [("UV", uv, "")], INC_SURF,
		output=unreal.CustomMaterialOutputType.CMOT_FLOAT1, extra_outputs=[("Tint", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
	film = g.node(unreal.MaterialExpressionSubstrateThinFilm, -300)
	film_ior = g.scalar("FilmIor", 1.55, "Pearl")
	g.link(g.scalar("NacreF0", 0.05, "Pearl"), film, "F0")
	g.link(nacre, film, "Thickness", "return")
	g.link(film_ior, film, "IOR")
	pearl_a = g.vector("PearlColorA", (0.82, 0.81, 0.78), "Pearl")
	pearl_b = g.vector("PearlColorB", (0.78, 0.76, 0.79), "Pearl")
	base = g.node(unreal.MaterialExpressionLinearInterpolate, -500)
	g.link(pearl_a, base, "A")
	g.link(pearl_b, base, "B")
	g.link(nacre, base, "Alpha", "Tint")
	# Platelet interference colour in the diffuse-looking reflection (RbNacreAlbedo): pastel, shifting with the view angle.
	albedo = g.custom("RbNacreAlbedo", "return RbNacreAlbedo(Base, N, V, UV, FilmIor, Strength);",
		[("Base", base, ""), ("N", g.node(unreal.MaterialExpressionVertexNormalWS, -700), ""),
			("V", g.node(unreal.MaterialExpressionCameraVectorWS, -700), ""), ("UV", uv, ""), ("FilmIor", film_ior, ""),
			("Strength", g.scalar("Iridescence", 0.12, "Pearl"), "")], INC_SURF, x=-300)
	slab = g.slab(albedo=albedo, f0=(film, "Specular Color"), f90=(film, "Edge Specular Color"), roughness=g.scalar("Roughness", 0.10, "Pearl"))
	g.front(slab)
	return g.finish()


def make_cue() -> str:
	"""Cue (plan 6.4), satin clear coat on the wooden sections. Mesh contract of UE-4 (RbCueMeshBuilder.h): local frame with the
	origin at the tip dome centre and +X toward the tip, UV1.x = section index (ERbCueSection: tip, ferrule, shaft, joint,
	forearm, wrap, sleeve, bumper). The static switch SectionsFromUV1 (default on) reads that index; off, the sections come from
	the distance behind the tip along local X (the Sections parameters of a 58 in cue) for meshes without UV1 (dev swatches)."""
	g = Graph("M_RbCue")
	local = g.node(unreal.MaterialExpressionLocalPosition, -1800)
	params = [("TipEnd", 0.25), ("FerruleEnd", 2.75), ("JointStart", 74.0), ("JointEnd", 76.5), ("WrapStart", 100.0), ("WrapEnd", 125.0),
		("BumperStart", 146.0)]
	uv1 = g.mask(g.texcoord(1), "r", -1400)
	switch = g.node(unreal.MaterialExpressionStaticSwitchParameter, -1200, parameter_name="SectionsFromUV1", default_value=True,
		group="Sections")
	g.params.append(("SectionsFromUV1", True))
	g.link(uv1, switch, "True")
	g.link(g.const(-1.0), switch, "False")
	inputs = [("P", local, ""), ("SectionFromUV", switch, "")] + [(n, g.scalar(n, v, "Sections"), "") for n, v in params]
	maple = g.vector("MapleColor", (0.56, 0.40, 0.22), "Wood")
	butt = g.vector("ButtColor", (0.10, 0.035, 0.015), "Wood")
	wrap = g.vector("WrapColor", (0.018, 0.018, 0.020), "Wood")
	surface = g.custom("RbCue",
		"const float Section = SectionFromUV >= 0.0 ? clamp(round(SectionFromUV), 0.0, 7.0)\n"
		"	: RbCueSection(P.x, TipEnd, FerruleEnd, JointStart, JointEnd, WrapStart, WrapEnd, BumperStart);\n"
		"// Grain coordinates [m]: along the cue, and across it on a fixed plane through the axis (the growth rings of a turned\n"
		"// billet are nearly planar), so the pattern has no seam around the cue.\n"
		"const float2 UV = float2(-P.x * 0.01, dot(P.yz, float2(0.8, 0.6)) * 0.01);\n"
		"const float4 S = RbCueSurface(Section, UV, Maple, Butt, Wrap);\n"
		"Roughness = S.a;\n"
		"CoatCoverage = (Section > 1.5 && Section < 2.5) || (Section > 3.5 && Section < 4.5) || (Section > 5.5 && Section < 6.5) ? 1.0 : 0.0;\n"
		"return S.rgb;",
		inputs + [("Maple", maple, ""), ("Butt", butt, ""), ("Wrap", wrap, "")], INC_SURF,
		extra_outputs=[("Roughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1), ("CoatCoverage", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
	cc = g.node(unreal.MaterialExpressionSubstrateSimpleClearCoatBSDF, -100)
	g.link(surface, cc, "Diffuse Albedo", "return")
	g.link(g.const(0.04), cc, "F0")
	g.link(surface, cc, "Roughness", "Roughness")
	g.link(surface, cc, "Clear Coat Coverage", "CoatCoverage")
	g.link(g.scalar("CoatRoughness", 0.20, "Coat"), cc, "Clear Coat Roughness")   # satin shaft (plan 6.4: 0.15-0.25)
	g.front(cc)
	return g.finish()


# Section boundaries of MI_RbCue_LocalSections [local units behind the +X end]: every section along a 100-unit mesh (the engine
# cube, local X -50..50), for the dev swatch of the sections-from-local-X path.
CUE_SWATCH_SECTIONS = [("TipEnd", -48.0), ("FerruleEnd", -45.0), ("JointStart", 5.0), ("JointEnd", 8.0), ("WrapStart", 22.0),
	("WrapEnd", 38.0), ("BumperStart", 47.0)]


def make_cue_swatch_instance() -> str:
	"""MI_RbCue_LocalSections: M_RbCue with the sections from local X (SectionsFromUV1 off) for meshes without UE-4's UV1."""
	path = f"{MAT_DIR}/MI_RbCue_LocalSections"
	mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
	if mi is None:
		tools = unreal.AssetToolsHelpers.get_asset_tools()
		mi = tools.create_asset("MI_RbCue_LocalSections", MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	MEL.set_material_instance_parent(mi, unreal.load_asset(f"{MAT_DIR}/M_RbCue"))
	MEL.set_material_instance_static_switch_parameter_value(mi, "SectionsFromUV1", False)
	for name, value in CUE_SWATCH_SECTIONS:
		MEL.set_material_instance_scalar_parameter_value(mi, name, value)
	MEL.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
	rb.log(f"{path}: SectionsFromUV1=False, " + ", ".join(f"{n}={v}" for n, v in CUE_SWATCH_SECTIONS))
	return path


def make_room_wall() -> str:
	g = Graph("M_RbRoomWall", used_with_nanite=True)
	world = g.node(unreal.MaterialExpressionWorldPosition, -1600)
	tone = g.custom("RbPaintedWall", "return RbPaintedWall(P);", [("P", world, "")], INC_SURF, output=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
	albedo = g.binary(unreal.MaterialExpressionMultiply, g.vector("Albedo", (0.42, 0.40, 0.37), "Wall"), tone)
	slab = g.slab(albedo=albedo, f0=g.const(0.04), roughness=g.scalar("Roughness", 0.88, "Wall"))
	g.front(slab)
	return g.finish()


def make_room_floor() -> str:
	g = Graph("M_RbRoomFloor", used_with_nanite=True)
	world = g.node(unreal.MaterialExpressionWorldPosition, -1600)
	base_r = g.scalar("Roughness", 0.55, "Floor")
	floor = g.custom("RbConcreteFloor", "return RbConcreteFloor(P, BaseRoughness);", [("P", world, ""), ("BaseRoughness", base_r, "")],
		INC_SURF, output=unreal.CustomMaterialOutputType.CMOT_FLOAT2)
	albedo = g.binary(unreal.MaterialExpressionMultiply, g.vector("Albedo", (0.20, 0.19, 0.18), "Floor"), g.mask(floor, "r"))
	slab = g.slab(albedo=albedo, f0=g.const(0.04), roughness=g.mask(floor, "g"))
	g.front(slab)
	return g.finish()


def make_lamp_diffuser() -> str:
	"""Opal lamp diffuser: diffuse + emissive luminance [cd/m^2] of a 4000 K source, camera-only (RbLampEmissive)."""
	g = Graph("M_RbLampDiffuser", used_with_nanite=True)
	color = g.vector("LampColor", (1.414, 0.924, 0.533), "Lamp")      # 4000 K, luminance 1 (linear sRGB)
	lum = g.scalar("Luminance", 2000.0, "Lamp")
	emissive = g.binary(unreal.MaterialExpressionMultiply, color, lum)
	camera_only = g.custom("RbLampEmissive", "return RbLampEmissive(Emissive);", [("Emissive", emissive, "")], INC_SURF)
	slab = g.slab(albedo=g.vector("Albedo", (0.80, 0.80, 0.78), "Lamp"), f0=g.const(0.04), roughness=g.const(0.6), emissive=camera_only)
	g.front(slab)
	return g.finish()


# --------------------------------------------------------------------------------------------------------------------
# Verification (what C++ writes must exist in the generated assets)
# --------------------------------------------------------------------------------------------------------------------


def verify(paths: list) -> None:
	for path in paths:
		if not unreal.EditorAssetLibrary.does_asset_exist(path):
			rb.fail(f"{path} missing after generation")
	ball = unreal.load_asset(f"{MAT_DIR}/M_RbBall")
	scalars = {str(n) for n in MEL.get_scalar_parameter_names(ball)}
	vectors = {str(n) for n in MEL.get_vector_parameter_names(ball)}
	for name in ("BallNumber", "ExposureTime", "BallRadiusCm"):
		if name not in scalars:
			rb.fail(f"M_RbBall lacks scalar {name} ({sorted(scalars)})")
	for name in ("BallColor", "BallOmegaLocal"):
		if name not in vectors:
			rb.fail(f"M_RbBall lacks vector {name} ({sorted(vectors)})")
	mpc = unreal.load_asset(MPC_PATH)
	names = {str(v.get_editor_property("parameter_name")) for v in mpc.get_editor_property("vector_parameters")}
	if names != {f"Ball{i:02d}" for i in range(16)}:
		rb.fail(f"MPC_RbBalls vectors {sorted(names)}")


def make_table_instances() -> list:
	"""The table-family instances of M2-L: the 9-ft legs, and the dive bar's coin-op table (venue-dive-bar 6.4)."""
	hl, hw = SEVEN_FOOT_BAR_HALF
	return [
		make_instance("MI_RbRailWood_Legs", "M_RbRailWood", {"GrainAlongU": 0.0}),
		make_instance("MI_RbCloth_BarGreen", "M_RbCloth", {"Napped": 1.0, "Age": DIVE_BAR_AGE, "NapSheen": 0.25, "FuzzAmount": 0.35, "FuzzRoughness": 0.55,
			"WeaveNormalStrength": 0.10, "WeaveContrast": 0.25, "Anisotropy": 0.05, "HalfLength": hl, "HalfWidth": hw, "Seed": 11.0},
			{"ClothColor": BAR_GREEN}),
		make_instance("MI_RbRail_BlackLaminate", "M_RbLaminate", {"UsePrint": 0.0, "Roughness": 0.16, "Burns": 0.45, "Rings": 0.18, "Scratches": 0.50,
			"CornerWear": 0.0, "Seed": 17.0}, {"BaseColor": (0.018, 0.017, 0.017)}),
		make_instance("MI_RbLaminate_Walnut", "M_RbLaminate", {"UsePrint": 1.0, "Roughness": 0.36, "Burns": 0.06, "Rings": 0.10, "Scratches": 0.35,
			"CornerWear": 1.0, "Seed": 23.0}, {"PrintTint": (0.40, 0.31, 0.25)}),
		make_instance("MI_RbCushionRubber_Old", "M_RbCushionRubber", {"Age": DIVE_BAR_AGE, "Roughness": 0.70, "CavityDepthCm": 2.5, "CavityFloor": 0.12},
			{"Albedo": (0.018, 0.018, 0.019)}),
		make_instance("MI_RbSight_WhitePlastic", "M_RbPlasticABS", {"Roughness": 0.30, "Age": 0.6, "Chips": 0.0, "Seed": 9.0},
			{"Albedo": (0.58, 0.56, 0.49)}),
		make_instance("MI_RbBall_DiveBar", "M_RbBall", {"RoughnessMin": 0.10, "RoughnessMax": 0.15, "HazeWeight": 0.30, "Dirt": 0.65, "Chalk": 0.6},
			{"WhiteColor": (0.72, 0.70, 0.64)}),
	]


def verify_table_defaults(paths: list) -> None:
	"""Every default part material of RbTableMeshBuilder::GetDefaultMaterialPath (both committed presets) must be generated here."""
	names = {p.rsplit("/", 1)[1] for p in paths}
	for required in ("M_RbCloth", "MI_RbCloth_BarGreen", "M_RbRailWood", "MI_RbRailWood_Legs", "MI_RbRail_BlackLaminate", "MI_RbLaminate_Walnut",
			"M_RbCushionRubber", "MI_RbCushionRubber_Old", "M_RbSight", "MI_RbSight_WhitePlastic", "M_RbPocketLiner", "M_RbPlasticABS",
			"M_RbAluminium", "M_RbChrome", "M_RbSteel", "M_RbPlexi", "MI_RbBall_DiveBar"):
		if required not in names:
			rb.fail(f"{required} (a default table material) was not generated")


def main() -> None:
	rb.ensure_dir(MAT_DIR)
	textures = make_textures()
	cc0 = make_cc0_textures()
	mpc = make_mpc()
	paths = [
		make_ball(textures),
		make_cloth(textures, mpc),
		make_rail_wood(cc0),
		make_rubber(),
		make_pocket_leather(cc0),
		make_leather("M_RbLeather", (0.060, 0.034, 0.020), 0.50),
		make_brass(),
		make_sight(),
		make_cue(),
		make_room_wall(),
		make_room_floor(),
		make_lamp_diffuser(),
		make_laminate(cc0),
		make_plastic(),
		make_metal("M_RbAluminium", (0.91, 0.92, 0.92), 0.35, 0.55, 0.0, DIVE_BAR_AGE),
		make_metal("M_RbChrome", (0.55, 0.56, 0.55), 0.08, 0.0, 1.0, DIVE_BAR_AGE),
		make_metal("M_RbSteel", (0.56, 0.57, 0.58), 0.32, 0.0, 0.3, TOURNAMENT_AGE),
		make_plexi(),
		MPC_PATH,
	]
	paths.append(make_cloth_instance("MI_RbCloth_Green", CLOTH_GREEN))
	paths.append(make_cue_swatch_instance())
	paths += make_table_instances()
	verify(paths)
	verify_table_defaults(paths)
	rb.log(f"table-family materials OK ({len(paths)} assets)")


main()
