#!/usr/bin/env python3
"""M2-L table look-dev captures (Docs/ue-architecture.md 18.7), host side. Owner: M2-L.

  python Tools/unreal/capture_table.py                          # both tables, all views -> Docs/images/dev/m2l/<table>_<view>.png
  python Tools/unreal/capture_table.py --tables 7ft --views chin_on_cue,standing
  python Tools/unreal/capture_table.py --sheet-only             # rebuild the comparison sheets from the existing captures

Every capture renders a look-dev level of Tools/unreal/editor/rb_dev_m2l.py (the 9-ft pro table in a copy of the M1 room, the 7-ft
coin-op bar box under the bar lamp) through one of its RbCam_TL_* ARbLookDevCameras (Eyes preset) with the full renderer
(rbue.py capture: strict, a material / shader compile error fails the run), 1920x1080, the High quality preset, an 8-ball rack and
the cue ball placed at the chin-on-cue position. The views: chin_on_cue, standing, pocket_closeup, cushion_grazing, rail_closeup,
overhead and the extra foot_end (rb_m1_layout.TABLE_VIEWS). Captures run one after the other (one Unreal process at a time on the shared GPU); a capture
that fails is retried once.

After the captures, a comparison sheet per table (<table>_sheet.png) lays the views out next to the reference checklist of
Docs/references/table-lookdev.md (what each view is compared against), and the known-albedo card of the look-dev levels is measured
in the chin-on-cue / standing captures when --card <x,y,x,y,x,y> pixel positions are given (PIL + numpy, optional).
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "Tools" / "unreal" / "editor"))
import rb_m1_layout as layout  # noqa: E402

RBUE = REPO / "Tools" / "unreal" / "rbue.py"
OUT_DIR = REPO / "Docs" / "images" / "dev" / "m2l"
WARMUP_SECONDS = 10.0   # the bar room adapts down from the default exposure (0.7 EV/s, Eyes)

# What each view is judged on (the reference list of Docs/references/table-lookdev.md, per view).
CHECKLIST = {
	"chin_on_cue": ["cloth fibre sheen at grazing angles, far cloth lighter / hazier", "cushion nose roll and rubber lip, facings at the jaws",
		"ball highlights: one per lamp section, soft haze", "cloth albedo vs the grey card (0.05-0.10 target)"],
	"standing": ["overall proportions, rail width vs bed", "lamp pool on the table, falloff to the corners", "wear lanes / chalk read from 1.65 m",
		"rail caps: lacquer (9-ft) / black laminate (7-ft)"],
	"pocket_closeup": ["jaw facings follow the cut, cloth folded at the facing", "liner / leather / casting opening, no gaps",
		"drop: bucket (9-ft) or gully throat (7-ft)", "frayed cloth and chalk at the mouth"],
	"cushion_grazing": ["nose line straight, rounded nose, rubber strip", "cloth sheen at 3 cm (no moire)", "sights flush in the cap"],
	"rail_closeup": ["rounded cap edge (real geometry)", "sight inlays, cap material wear (burns / rings on the bar table)",
		"apron veneer (9-ft) / trim + laminate cabinet (7-ft)"],
	"overhead": ["pocket geometry symmetric", "wear map: break spot, head-string lane, rack impression", "sight spacing"],
	"foot_end": ["foot rail and apron / cabinet end", "coin slide, trap window, ball tray, cue-ball return (7-ft)", "levelers, floor contact"],
}


def capture(map_url: str, camera: str, out: Path, quality: str, warmup: float, timeout: float) -> bool:
	exec_cmds = layout.capture_exec_cmds(quality)
	cmd = [sys.executable, str(RBUE), "capture", "--map", map_url, "--camera", camera, "--res", layout.CAPTURE_RES, "--warmup-seconds", str(warmup),
		"--exec-cmds", exec_cmds, "--out", str(out), "--timeout", str(timeout)]
	for attempt in (1, 2):
		print(f"[capture_table] {camera} -> {out} (attempt {attempt})", flush=True)
		if subprocess.run(cmd, cwd=str(REPO)).returncode == 0 and out.exists():
			return True
	return False


def make_sheet(prefix: str, out_dir: Path, suffix: str) -> Path | None:
	"""3-column grid of the views with their checklist lines (PIL; skipped without it)."""
	try:
		from PIL import Image, ImageDraw
	except ImportError:
		print("[capture_table] PIL not available - no comparison sheet")
		return None
	cell_w, cell_h, text_h = 640, 360, 92
	rows = (len(layout.TABLE_VIEWS) + 2) // 3
	sheet = Image.new("RGB", (3 * cell_w, rows * (cell_h + text_h)), (16, 16, 16))
	draw = ImageDraw.Draw(sheet)
	for i, (view, _, _) in enumerate(layout.TABLE_VIEWS):
		x, y = (i % 3) * cell_w, (i // 3) * (cell_h + text_h)
		path = out_dir / f"{prefix}_{view}{suffix}.png"
		if path.exists():
			sheet.paste(Image.open(path).convert("RGB").resize((cell_w, cell_h)), (x, y))
		else:
			draw.text((x + 10, y + 10), f"missing: {path.name}", fill=(255, 80, 80))
		draw.text((x + 8, y + cell_h + 4), f"{prefix} {view}", fill=(255, 255, 255))
		for k, line in enumerate(CHECKLIST.get(view, [])):
			draw.text((x + 8, y + cell_h + 20 + 16 * k), "- " + line, fill=(190, 190, 190))
	target = out_dir / f"{prefix}_sheet{suffix}.png"
	sheet.save(target)
	return target


def measure_card(path: Path, points: list) -> None:
	"""Cloth albedo from the grey card: points = [(x, y) of the 0.80, 0.18, 0.04 patches, (x, y) of the cloth] in pixels. Prints the
	cloth's linear value relative to the 0.18 patch (the albedo under the same light, before the fuzz / sheen of the view)."""
	try:
		import numpy as np
		from PIL import Image
	except ImportError:
		print("[capture_table] PIL / numpy not available - no card measurement")
		return
	img = np.asarray(Image.open(path).convert("RGB")).astype(float) / 255.0
	lin = np.where(img <= 0.04045, img / 12.92, ((img + 0.055) / 1.055) ** 2.4)

	def sample(x: int, y: int) -> np.ndarray:
		return lin[max(0, y - 3):y + 4, max(0, x - 3):x + 4].reshape(-1, 3).mean(0)

	values = [sample(*p) for p in points]
	lum = [float(v @ np.array([0.2126, 0.7152, 0.0722])) for v in values]
	grey = lum[1]
	for name, value, l in zip(["card 0.80", "card 0.18", "card 0.04", "cloth"], values, lum):
		print(f"[capture_table] {path.name} {name}: linear {value.round(4)} lum {l:.4f} -> albedo ~{0.18 * l / max(grey, 1e-6):.3f}")


def main() -> int:
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("--tables", default="", help="comma-separated table prefixes (9ft, 7ft; default: both)")
	p.add_argument("--views", default="", help="comma-separated view names (default: all)")
	p.add_argument("--quality", default="High", choices=["Low", "Medium", "High", "Epic", "Cinematic"])
	p.add_argument("--out-dir", default=str(OUT_DIR))
	p.add_argument("--suffix", default="", help="appended to the file names, e.g. _r2")
	p.add_argument("--warmup-seconds", type=float, default=WARMUP_SECONDS)
	p.add_argument("--timeout", type=float, default=3600)
	p.add_argument("--sheet-only", action="store_true")
	p.add_argument("--card", default="", help="x,y pixel positions of the three card patches and the cloth (8 numbers) for --card-image")
	p.add_argument("--card-image", default="", help="capture to measure the card in")
	a = p.parse_args()

	out_dir = Path(a.out_dir).resolve()
	out_dir.mkdir(parents=True, exist_ok=True)
	tables = {t.strip() for t in a.tables.split(",") if t.strip()}
	views = {v.strip() for v in a.views.split(",") if v.strip()}
	failed = []
	for prefix, _preset, map_path in layout.LOOKDEV_TABLES:
		if tables and prefix not in tables:
			continue
		if not a.sheet_only:
			for view, tag, _ in layout.TABLE_VIEWS:
				if views and view not in views:
					continue
				out = out_dir / f"{prefix}_{view}{a.suffix}.png"
				if not capture(map_path + layout.CAPTURE_OPTIONS, tag, out, a.quality, a.warmup_seconds, a.timeout):
					failed.append(f"{prefix}_{view}")
		sheet = make_sheet(prefix, out_dir, a.suffix)
		if sheet:
			print(f"[capture_table] sheet {sheet}")
	if a.card and a.card_image:
		numbers = [int(v) for v in a.card.split(",")]
		measure_card(Path(a.card_image), [(numbers[i], numbers[i + 1]) for i in range(0, len(numbers), 2)])
	print(f"[capture_table] {'FAILED: ' + ', '.join(failed) if failed else 'all captures OK'}")
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
