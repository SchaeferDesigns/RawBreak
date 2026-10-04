#!/usr/bin/env python3
"""The dive bar's baked floor masks (venue-dive-bar 6.1 "baked 2K masks until the RVT path is proven", R1, S17, 2.6 paths):
T_DB_FloorMasks.png, RGBA over the room in world XY (1 px = 1 cm): R traffic lanes (dull wax, ground-in dirt), G dried beer
(sticky), B fresh spills (puddles, mirror-like), A blue chalk dust around the table.

Mapping (M_DB_Floor): u = X / 20.48 m, v = Y / 10.24 m (venue frame V: origin at the inside corner of the front and the left wall;
the room is X 0 - 20.32 incl. the rear addition, Y 0 - 7.32). Paths and positions are transcribed from venue-dive-bar 2.3 / 2.6
(P1 door -> along the bar -> bar end, P2 bar end -> past C3 -> corridor, P3 / P5 bartender flap -> around the table -> storage
door, P4 booths <-> bar, the worn ring 0.4 - 0.9 m around the table, the stool zone at the bar front).
Host Python (numpy; Pillow not needed). Deterministic. Owner: M2-B.

  python Tools/art/floor_masks.py      # -> Art/DiveBar/Textures/Floor/T_DB_FloorMasks.png
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import decal_prep as dp  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "Art" / "DiveBar" / "Textures" / "Floor" / "T_DB_FloorMasks.png"
W, H = 2048, 1024
SX, SY = 20.48, 10.24          # metres covered

TABLE = (12.578, 14.940, 4.754, 6.100)   # outer X / Y range (E13)
# (points [m], width [m], strength)
PATHS = [
	([(0.4, 6.1), (1.6, 5.2), (3.0, 3.6), (6.0, 3.5), (9.9, 3.6)], 1.2, 1.0),          # P1 door -> along the bar -> bar end
	([(9.9, 3.6), (11.4, 2.6), (13.2, 2.2), (15.4, 1.5), (16.5, 0.85)], 0.9, 0.85),     # P2 bar end -> past C3 -> corridor
	([(9.6, 2.0), (11.6, 3.4), (12.2, 4.4), (12.3, 5.5), (12.4, 6.5)], 0.7, 0.6),       # P3 flap -> around the head end
	([(11.6, 3.4), (13.8, 4.15), (15.6, 4.3), (16.4, 5.0)], 0.7, 0.7),                 # P5 flap -> past C3 -> storage door
	([(3.1, 6.1), (3.2, 4.4), (3.6, 3.2)], 0.6, 0.55),                                   # P4 booth B1 <-> bar
	([(5.0, 6.1), (5.0, 4.4), (5.2, 3.2)], 0.6, 0.6),                                    # P4 booth B2
	([(6.9, 6.1), (6.9, 4.4), (7.0, 3.2)], 0.6, 0.55),                                   # P4 booth B3
	([(16.5, 0.85), (20.0, 0.85)], 0.8, 0.7),                                            # corridor
]


def seg_dist(px, py, a, b):
	ax, ay = a
	bx, by = b
	vx, vy = bx - ax, by - ay
	t = np.clip(((px - ax) * vx + (py - ay) * vy) / max(1e-9, vx * vx + vy * vy), 0.0, 1.0)
	return np.hypot(px - (ax + t * vx), py - (ay + t * vy))


def rect_dist(px, py, x0, x1, y0, y1):
	dx = np.maximum(np.maximum(x0 - px, 0.0), px - x1)
	dy = np.maximum(np.maximum(y0 - py, 0.0), py - y1)
	return np.hypot(dx, dy)


def main() -> int:
	rng = np.random.default_rng(1958)
	xs = (np.arange(W, dtype=np.float32) + 0.5) / W * SX
	ys = (np.arange(H, dtype=np.float32) + 0.5) / H * SY
	px, py = np.meshgrid(xs, ys)
	noise = dp.fbm(W, rng, 16, 5)[:H, :W] if H <= W else dp.fbm(H, rng, 16, 5)[:H, :W]
	fine = dp.fbm(W, rng, 64, 3)[:H, :W]

	traffic = np.zeros((H, W), np.float32)
	for pts, width, strength in PATHS:
		d = np.full((H, W), 1e9, np.float32)
		for a, b in zip(pts[:-1], pts[1:]):
			d = np.minimum(d, seg_dist(px, py, a, b))
		traffic = np.maximum(traffic, strength * np.exp(-(d / (0.5 * width)) ** 2))
	# stool zone at the bar front (people stand and sit here all night)
	traffic = np.maximum(traffic, 0.85 * dp.smoothstep(3.3, 2.5, py) * dp.smoothstep(2.35, 2.5, py) * dp.smoothstep(1.9, 2.3, px) * dp.smoothstep(9.9, 9.5, px))
	# S17: the worn ring 0.4 - 0.9 m around the table (shooters walk round it)
	dt = rect_dist(px, py, *TABLE)
	ring = dp.smoothstep(0.15, 0.45, dt) * dp.smoothstep(1.2, 0.8, dt)
	traffic = np.maximum(traffic, 0.9 * ring)
	# nobody walks along the walls, under the booths or the bar: keep the wax glossy there
	wall = np.minimum.reduce([px, py, 7.32 - py, np.abs(16.46 - px) + 10.0 * (px > 16.66)])
	traffic *= dp.smoothstep(0.08, 0.45, wall)
	traffic *= 0.75 + 0.5 * noise
	traffic = np.clip(traffic, 0.0, 1.0)

	sticky = np.zeros((H, W), np.float32)
	for (cx, cy, r, s) in [(4.2, 2.8, 0.6, 1.0), (6.3, 2.9, 0.7, 1.0), (8.4, 2.75, 0.5, 0.9), (5.0, 5.8, 0.5, 0.8), (2.6, 2.9, 0.4, 0.7),
			(16.1, 6.4, 0.4, 0.7), (13.7, 3.9, 0.35, 0.6), (9.4, 3.2, 0.5, 0.6)]:
		blob = np.exp(-(((px - cx) / r) ** 2 + ((py - cy) / (r * 0.7)) ** 2)) * s
		sticky = np.maximum(sticky, blob)
	sticky = np.clip(dp.smoothstep(0.45, 0.75, sticky * (0.55 + 0.9 * noise)), 0, 1)

	spill = np.zeros((H, W), np.float32)
	for (cx, cy, r) in [(6.45, 2.95, 0.20), (5.05, 5.85, 0.13)]:
		d = np.hypot(px - cx, (py - cy) * 1.3) / r + 0.6 * (fine - 0.5)
		spill = np.maximum(spill, dp.smoothstep(1.0, 0.85, d))

	chalk = dp.smoothstep(0.7, 0.05, dt) * (dt > 0.0)
	corners = np.zeros((H, W), np.float32)
	for cx in (TABLE[0], TABLE[1]):
		for cy in (TABLE[2], TABLE[3]):
			corners = np.maximum(corners, np.exp(-(np.hypot(px - cx, py - cy) / 0.45) ** 2))
	chalk = np.clip((0.35 * chalk + 0.8 * corners) * dp.smoothstep(0.45, 0.8, fine + 0.3 * noise), 0, 1)

	rgba = np.stack([traffic, sticky, spill, chalk], axis=-1)
	dp.write_png(OUT, dp.to_u8(rgba))
	print(f"[floor_masks] {OUT} ({W} x {H}, {SX} x {SY} m)")
	return 0


if __name__ == "__main__":
	sys.exit(main())
