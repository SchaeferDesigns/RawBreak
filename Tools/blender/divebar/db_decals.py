"""First decal set of the dive bar (venue-dive-bar 7; Docs/ue-architecture.md 18.8): procedural atlases, no Higgsfield art in M2.
Runs INSIDE Blender 5.2 (numpy only; the maths and the atlas packing live in Tools/art/decal_prep.py, which also runs host-side):

	python Tools/blender/rbbl.py run Tools/blender/divebar/db_decals.py [-- --seed 1958]

Sets (4 x 4 cells of 512 px, 2048 px atlases T_DB_DecalAtlas_<Set>_{BC,N,R}.png under Art/DiveBar/Textures/Decals):
  Rings   dried-beer glass rings (1-3 overlapping, coffee-ring edges, gaps), white "heat" rings on lacquer, partial arcs
  Burns   old cigarette burns (charred core, brown halo, scorch), sanded-over burns, melted laminate craters (S5: all pre-2006)
  Scuffs  rubber heel marks, kick-scuff clusters, cue-butt dings in paint (the S10 band at the tight spots)
  Stains  dried spills with tide lines (sticky), drips; the roughness-only "sticky" variant uses this atlas
  Chalk   blue chalk powder smudges and fingerprints (rails, cue rack, chalkboard frame, the wall by the rack)
  Water   ceiling / wall leak stains with nested brown tide lines
Manifest: Art/DiveBar/Export/Decals/decals.json (cells, physical size of a cell in metres, roughness-only flag), read by
Tools/unreal/editor/rb_make_divebar_materials.py (MI_DB_Decal_<Set>_<nn>, and the stain bombing of the coated surfaces).
Deterministic (numpy Generator seeded from rb_bl.rng). Owner: M2-B.
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "common"))
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "art"))
import numpy as np  # noqa: E402

import rb_bl  # noqa: E402
import decal_prep as dp  # noqa: E402

N = 512
OUT_TEX = rb_bl.REPO / "Art" / "DiveBar" / "Textures" / "Decals"


def gen(name: str, seed: int, k: int) -> np.random.Generator:
	return np.random.default_rng(rb_bl.rng(name, k, seed).getrandbits(63))


def angular_noise(a: np.ndarray, rng: np.random.Generator, harmonics: int = 7, amp: float = 1.0) -> np.ndarray:
	out = np.zeros_like(a)
	for h in range(1, harmonics + 1):
		out += rng.normal(0.0, 1.0 / h) * np.sin(h * a + rng.uniform(0, 6.28))
	return out * amp


# ---- rings -----------------------------------------------------------------------------------------------------------


def ring_field(x, y, rng, r0, w_out, w_in, gaps: float, strength: float):
	cx, cy = rng.normal(0, 0.03), rng.normal(0, 0.03)
	r, a = dp.polar(x, y, cx, cy)
	wob = 1.0 + 0.015 * angular_noise(a, rng)
	d = r - r0 * wob
	prof = np.where(d > 0, np.exp(-(d / w_out) ** 2), np.exp(-(d / w_in) ** 2))
	along = 0.55 + 0.45 * np.sin(a * rng.integers(1, 4) + rng.uniform(0, 6.28)) * 0.5 + 0.25 * angular_noise(a, rng, 12, 0.5)
	gap = dp.smoothstep(gaps - 0.1, gaps + 0.1, 0.5 + 0.5 * np.sin(a * 2.0 + rng.uniform(0, 6.28) + 0.8 * angular_noise(a, rng, 5, 1.0)))
	return prof * np.clip(along, 0.15, 1.0) * gap * strength, r / (r0 + 1e-6)


def make_rings(seed: int) -> dict:
	at = dp.Atlas(N)
	x, y = dp.grid(N)
	for k in range(16):
		rng = gen("Rings", seed, k)
		grain = dp.fbm(N, rng, 8, 5)
		if k < 8:          # dried beer: 1-3 overlapping rings
			alpha = np.zeros((N, N), np.float32)
			fill = np.zeros((N, N), np.float32)
			count = 1 + (k % 3)
			for _ in range(count):
				off = rng.normal(0, 0.09, 2)
				f, rn = ring_field(x - off[0], y - off[1], rng, rng.uniform(0.55, 0.72), 0.018, 0.06, rng.uniform(-0.2, 0.35), rng.uniform(0.6, 1.0))
				alpha = np.maximum(alpha, f)
				fill = np.maximum(fill, (rn < 1.0) * 0.10 * grain)
			alpha = np.clip(alpha * (0.75 + 0.5 * grain) + fill, 0, 1)
			color = np.stack([0.17 - 0.07 * alpha, 0.10 - 0.05 * alpha, 0.035 - 0.02 * alpha], -1)
			at.put(k, f"beer_ring_{count}", color, alpha * 0.85, 0.5 + 0.25 * alpha, 0.55 + 0.1 * grain)
		elif k < 12:       # white heat / water rings on lacquer (cloudy finish, no pigment)
			f, rn = ring_field(x, y, rng, rng.uniform(0.5, 0.7), 0.05, 0.12, -0.5, 1.0)
			cloud = np.clip(f * (0.6 + 0.6 * grain), 0, 1)
			color = np.stack([0.62 + 0 * cloud, 0.60 + 0 * cloud, 0.55 + 0 * cloud], -1)
			at.put(k, "heat_ring", color, cloud * 0.45, 0.5 + 0 * cloud, 0.62 + 0.1 * grain)
		else:              # partial arcs (a glass half on a coaster) + a faint puddle mark
			f, rn = ring_field(x, y, rng, rng.uniform(0.6, 0.75), 0.02, 0.05, 0.45, 0.9)
			puddle = dp.smoothstep(0.62, 0.35, dp.fbm(N, rng, 4, 4) + 0.35 * (np.hypot(x, y) - 0.3))
			alpha = np.clip(f + 0.25 * puddle * grain, 0, 1)
			color = np.stack([0.15 - 0.05 * alpha, 0.09 - 0.04 * alpha, 0.03 + 0 * alpha], -1)
			at.put(k, "partial_ring", color, alpha * 0.8, 0.5 + 0.2 * alpha, 0.5 + 0.1 * grain)
	return at.write(OUT_TEX, "Rings", 4.0) | {"size_m": 0.11, "use": "bar top, rail caps, back ledge, C3 shelf, booth tables"}


# ---- burns -----------------------------------------------------------------------------------------------------------


def make_burns(seed: int) -> dict:
	at = dp.Atlas(N)
	x, y = dp.grid(N)
	for k in range(16):
		rng = gen("Burns", seed, k)
		grain = dp.fbm(N, rng, 10, 5)
		blister = dp.fbm(N, rng, 32, 3)
		ang = rng.uniform(0, math.pi)
		xr = x * math.cos(ang) + y * math.sin(ang)
		yr = -x * math.sin(ang) + y * math.cos(ang)
		if k < 8:          # cigarette laid down: an elongated burn, charred core, brown halo, yellow scorch
			length = rng.uniform(0.45, 0.8)
			width = rng.uniform(0.10, 0.17)
			e = np.sqrt((np.maximum(np.abs(xr) - length * 0.5, 0) / width) ** 2 + (yr / width) ** 2)
			e = e * (1.0 + 0.25 * (grain - 0.5))
			core = dp.smoothstep(0.75, 0.35, e) * (0.7 + 0.3 * blister)
			halo = dp.smoothstep(1.6, 0.6, e)
			scorch = dp.smoothstep(2.6, 1.0, e)
			color = np.stack([0.02 + 0.10 * (halo - core) + 0.18 * (scorch - halo), 0.012 + 0.05 * (halo - core) + 0.10 * (scorch - halo),
				0.008 + 0.015 * (halo - core) + 0.03 * (scorch - halo)], -1)
			alpha = np.clip(core + 0.75 * halo + 0.35 * scorch, 0, 1) * (0.85 + 0.15 * grain)
			height = 0.5 - 0.35 * core + 0.06 * (halo - core) * blister
			rough = 0.55 + 0.35 * core
			at.put(k, "cigarette_burn", np.clip(color, 0, 1), alpha, height, rough)
		elif k < 12:       # sanded over: faint, soft, brownish
			e = np.sqrt((xr / rng.uniform(0.35, 0.5)) ** 2 + (yr / 0.16) ** 2) * (1 + 0.3 * (grain - 0.5))
			halo = dp.smoothstep(1.3, 0.3, e)
			color = np.stack([0.10 + 0 * halo, 0.05 + 0 * halo, 0.02 + 0 * halo], -1)
			at.put(k, "sanded_burn", color, halo * 0.55, 0.5 - 0.05 * halo, 0.6 + 0.1 * halo)
		else:              # melted laminate / vinyl crater with a raised rim
			r = np.hypot(x, y) * (1.0 + 0.2 * (grain - 0.5))
			r0 = rng.uniform(0.18, 0.28)
			crater = dp.smoothstep(r0, r0 * 0.4, r)
			rim = np.exp(-((r - r0) / 0.05) ** 2)
			halo = dp.smoothstep(r0 * 2.2, r0, r)
			color = np.stack([0.03 + 0.12 * halo * (1 - crater), 0.02 + 0.06 * halo * (1 - crater), 0.01 + 0.02 * halo * (1 - crater)], -1)
			alpha = np.clip(crater + 0.8 * rim + 0.4 * halo, 0, 1)
			at.put(k, "melt_crater", color, alpha, 0.5 - 0.4 * crater + 0.15 * rim, 0.35 + 0.4 * crater)
	return at.write(OUT_TEX, "Burns", 8.0) | {"size_m": 0.04, "use": "rail caps, bar-top edge, booth tables, window sill, stool vinyl"}


# ---- scuffs ----------------------------------------------------------------------------------------------------------


def stroke_field(x, y, rng, width, length, curve):
	t0 = rng.uniform(0, 6.28)
	cx, cy = rng.normal(0, 0.1, 2)
	rr = 1.0 / max(1e-3, curve)
	# arc of radius rr through the centre
	ox, oy = cx - rr * math.cos(t0), cy - rr * math.sin(t0)
	r, a = dp.polar(x, y, ox, oy)
	d = np.abs(r - rr)
	da = np.angle(np.exp(1j * (a - t0)))
	along = dp.smoothstep(length / rr, length / rr * 0.6, np.abs(da))
	return dp.smoothstep(width, width * 0.2, d) * along, da * rr


def make_scuffs(seed: int) -> dict:
	at = dp.Atlas(N)
	x, y = dp.grid(N)
	for k in range(16):
		rng = gen("Scuffs", seed, k)
		grain = dp.fbm(N, rng, 16, 4)
		if k < 8:          # black rubber heel marks
			alpha = np.zeros((N, N), np.float32)
			for _ in range(rng.integers(1, 4)):
				f, s = stroke_field(x, y, rng, rng.uniform(0.03, 0.09), rng.uniform(0.3, 0.8), rng.uniform(0.3, 1.5))
				streak = 0.8 + 0.2 * np.sin(s * rng.uniform(8, 20)) * grain
				alpha = np.maximum(alpha, f * np.clip(streak * (0.6 + 0.6 * grain), 0.2, 1.0))
			color = np.stack([0.015 + 0 * alpha] * 3, -1)
			at.put(k, "heel_mark", color, alpha * 0.9, 0.5 + 0.05 * alpha, 0.45 + 0 * alpha)
		elif k < 12:       # kick-scuff clusters (dull grey-brown, many small strokes)
			alpha = np.zeros((N, N), np.float32)
			for _ in range(rng.integers(6, 14)):
				f, s = stroke_field(x, y, rng, rng.uniform(0.01, 0.03), rng.uniform(0.08, 0.3), rng.uniform(0.2, 3.0))
				alpha = np.maximum(alpha, f * rng.uniform(0.3, 1.0))
			color = np.stack([0.05 + 0 * alpha, 0.045 + 0 * alpha, 0.04 + 0 * alpha], -1)
			at.put(k, "kick_scuffs", color, alpha * (0.6 + 0.4 * grain), 0.5 - 0.05 * alpha, 0.7 + 0 * alpha)
		else:              # cue-butt dings: dents with crushed paint (lighter primer at the edge)
			alpha = np.zeros((N, N), np.float32)
			height = np.full((N, N), 0.5, np.float32)
			for _ in range(rng.integers(3, 9)):
				cx, cy = rng.uniform(-0.6, 0.6, 2)
				r = np.hypot(x - cx, (y - cy) * rng.uniform(0.8, 1.6))
				r0 = rng.uniform(0.04, 0.10)
				dent = dp.smoothstep(r0, 0.0, r)
				ring = np.exp(-((r - r0) / 0.02) ** 2)
				alpha = np.maximum(alpha, np.clip(dent * 0.7 + ring, 0, 1))
				height -= 0.3 * dent
			color = np.stack([0.42 + 0 * alpha, 0.40 + 0 * alpha, 0.36 + 0 * alpha], -1)
			at.put(k, "cue_dings", color, np.clip(alpha * 1.4, 0, 1) * (0.7 + 0.3 * grain), height, 0.75 + 0 * alpha)
	return at.write(OUT_TEX, "Scuffs", 6.0) | {"size_m": 0.25, "use": "floor heel marks, bar die / booth base kicks, S10 wall band"}


# ---- stains ----------------------------------------------------------------------------------------------------------


def make_stains(seed: int) -> dict:
	at = dp.Atlas(N)
	x, y = dp.grid(N)
	for k in range(16):
		rng = gen("Stains", seed, k)
		shape = dp.fbm(N, rng, 3, 5)
		grain = dp.fbm(N, rng, 20, 3)
		if k < 12:         # dried spill with a tide line
			r = np.hypot(x, y)
			field = shape + 0.55 * (0.75 - r) * rng.uniform(0.8, 1.3)
			level = rng.uniform(0.62, 0.72)
			inside = dp.smoothstep(level - 0.01, level + 0.02, field)
			tide = np.exp(-((field - level - 0.012) / 0.012) ** 2)
			alpha = np.clip(0.45 * inside * (0.6 + 0.4 * grain) + 0.55 * tide, 0, 1)
			color = np.stack([0.14 - 0.05 * tide, 0.085 - 0.035 * tide, 0.03 - 0.01 * tide], -1)
			at.put(k, "dried_spill", color, alpha, 0.5 + 0.08 * inside, 0.38 + 0.08 * grain)
		else:              # drip trails (walls): vertical streaks from a source at the top
			alpha = np.zeros((N, N), np.float32)
			for _ in range(rng.integers(2, 5)):
				cx = rng.uniform(-0.5, 0.5)
				w = rng.uniform(0.02, 0.06)
				end = rng.uniform(-0.2, 0.9)
				wob = 0.03 * np.sin(y * rng.uniform(4, 9) + rng.uniform(0, 6))
				# image rows grow downwards: the source is at the top (y = -1), the drip runs down to y = end
				streak = dp.smoothstep(w, 0.0, np.abs(x - cx - wob)) * dp.smoothstep(end + 0.1, end - 0.05, y) * dp.smoothstep(-1.0, -0.8, y)
				alpha = np.maximum(alpha, streak * (0.5 + 0.5 * grain))
			color = np.stack([0.16 + 0 * alpha, 0.10 + 0 * alpha, 0.04 + 0 * alpha], -1)
			at.put(k, "drip_trail", color, alpha * 0.7, 0.5 + 0.05 * alpha, 0.45 + 0 * alpha)
	return at.write(OUT_TEX, "Stains", 3.0) | {"size_m": 0.45, "use": "floor spills near the bar front and B2, drips on the paneling; roughness-only sticky variant"}


# ---- chalk -----------------------------------------------------------------------------------------------------------


def make_chalk(seed: int) -> dict:
	at = dp.Atlas(N)
	x, y = dp.grid(N)
	blue = np.array([0.055, 0.20, 0.55], np.float32)
	for k in range(16):
		rng = gen("Chalk", seed, k)
		powder = dp.fbm(N, rng, 48, 3)
		shape = dp.fbm(N, rng, 3, 4)
		if k < 8:          # powder smudges
			r = np.hypot(x * rng.uniform(0.7, 1.4), y)
			alpha = dp.smoothstep(0.75, 0.1, r + 0.5 * (shape - 0.5)) * dp.smoothstep(0.35, 0.75, powder)
			color = np.broadcast_to(blue, (N, N, 3)) * (0.8 + 0.4 * powder[..., None])
			at.put(k, "chalk_smudge", color, alpha * 0.8, 0.5 + 0.02 * alpha, 0.85 + 0 * alpha)
		else:              # fingerprints: ridges of a whorl, partial
			alpha = np.zeros((N, N), np.float32)
			for _ in range(rng.integers(1, 4)):
				cx, cy = rng.uniform(-0.4, 0.4, 2)
				ang = rng.uniform(0, math.pi)
				xr = (x - cx) * math.cos(ang) + (y - cy) * math.sin(ang)
				yr = -(x - cx) * math.sin(ang) + (y - cy) * math.cos(ang)
				e = np.sqrt((xr / 0.22) ** 2 + (yr / 0.30) ** 2)
				ridges = 0.5 + 0.5 * np.sin(e * 55.0 + 2.0 * np.arctan2(yr, xr) * 0.3)
				print_mask = dp.smoothstep(1.0, 0.7, e) * dp.smoothstep(0.3, 0.6, shape)
				alpha = np.maximum(alpha, print_mask * ridges)
			color = np.broadcast_to(blue, (N, N, 3)) * 0.9
			at.put(k, "chalk_fingerprint", color, alpha * 0.75, 0.5 + 0 * alpha, 0.8 + 0 * alpha)
	return at.write(OUT_TEX, "Chalk", 2.0) | {"size_m": 0.08, "use": "rails, cue rack, chalkboard frame, wall by the rack, floor around the table"}


# ---- water -----------------------------------------------------------------------------------------------------------


def make_water(seed: int) -> dict:
	at = dp.Atlas(N)
	x, y = dp.grid(N)
	for k in range(16):
		rng = gen("Water", seed, k)
		shape = dp.fbm(N, rng, 3, 5)
		grain = dp.fbm(N, rng, 24, 3)
		r = np.hypot(x, y)
		field = shape + 0.6 * (0.8 - r)
		alpha = np.zeros((N, N), np.float32)
		levels = sorted(rng.uniform(0.55, 0.85, rng.integers(2, 5)))
		for lv in levels:
			alpha = np.maximum(alpha, np.exp(-((field - lv) / 0.01) ** 2) * rng.uniform(0.5, 1.0))
		inside = dp.smoothstep(levels[0] - 0.02, levels[0] + 0.03, field)
		alpha = np.clip(alpha + 0.25 * inside * grain, 0, 1)
		color = np.stack([0.30 - 0.1 * alpha, 0.19 - 0.07 * alpha, 0.07 - 0.03 * alpha], -1)
		at.put(k, "leak_stain", color, alpha * 0.8, 0.5 + 0 * alpha, 0.8 + 0 * alpha)
	return at.write(OUT_TEX, "Water", 2.0) | {"size_m": 0.7, "use": "ceiling tiles above the corridor opening and booth B1, drip on the paneling"}


def main() -> None:
	a = rb_bl.args()
	sets = {}
	for name, fn in (("Rings", make_rings), ("Burns", make_burns), ("Scuffs", make_scuffs), ("Stains", make_stains), ("Chalk", make_chalk),
			("Water", make_water)):
		if a.only and name not in a.only:
			continue
		info = fn(a.seed)
		info["textures"] = {m: f"Art/DiveBar/Textures/Decals/T_DB_DecalAtlas_{name}_{m}.png" for m in ("BC", "N", "R")}
		info["roughness_only_variant"] = name == "Stains"
		sets[name] = info
		rb_bl.log(f"decal atlas {name}: {len(info['cells'])} cells, cell {info['size_m']} m")
	manifest = Path(a.out) / "Decals" / "decals.json"
	manifest.parent.mkdir(parents=True, exist_ok=True)
	merged = json.loads(manifest.read_text(encoding="utf-8")).get("sets", {}) if manifest.exists() else {}
	merged.update(sets)   # --only regenerates a subset and keeps the others
	manifest.write_text(json.dumps({"owner": "M2-B", "grid": 4, "sets": merged}, indent=1, sort_keys=True), encoding="utf-8")
	rb_bl.log(f"db_decals: {sorted(sets)} -> {manifest}")


main()
