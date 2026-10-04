#!/usr/bin/env python3
"""All dive-bar captures of a milestone (M2-A; Docs/ue-architecture.md 18.8, venue-dive-bar 4.5, 12.1-12.3). Host side, stdlib only.

  python Tools/unreal/capture_divebar.py --set db1|db2|db3|m2|menu|dev [--views V01,V03] [--res 1920x1080] [--quality High]
  python Tools/unreal/capture_divebar.py --set dev --views V03 --out-dir Docs/images/dev/m2a --suffix _iter3
  python Tools/unreal/capture_divebar.py --calibrate       # 6.1 emissive calibration -> Art/DiveBar/calibration.json
  python Tools/unreal/capture_divebar.py --lux             # VDB-T1 rendered white card -> Docs/images/divebar/db2/lux_report.txt
  python Tools/unreal/capture_divebar.py --set db0         # DB-0 axis test -> Docs/images/divebar/db0/axis_test.png

Runs `rbue.py capture --map /Game/Generated/Maps/L_DiveBar?<options> --camera RbCam_DB_<View> --warmup-seconds 12` per view (the Eyes
exposure adapts at 0.7 EV/s and needs ~12 s to come down to EV 3, venue-dive-bar 4.5), strict shader checks (a material that fails
to compile fails the capture), one Unreal process at a time, a failed capture retried once. The match runs in 9-ball practice
with a racked table; the cue ball sits on the head spot (V04 chin on cue looks down the long string at the rack). V12 uses the
Lights-Up state (-RbLightingState=LightsUp), V10 hides the ceiling (ARbVenueInfo hides actors tagged RbDB_Ceiling while the V10
camera is the view target).

After the captures: Docs/images/divebar/<set>/ev_report.txt (VDB-T2): per view the adapted EV100 read from the capture log's
"RbVenue EV" lines (ARbVenueInfo logs the eye-adaptation exposure of the main view during a capture) against the 4.5 band.
Sets: db1 = V10 + V01..V04, db2 = V01..V05 + V08, db3 = V02 V03 V04 V06 V07, m2 = V01..V09 + TH1..TH7 (the M2 acceptance
list 18.10 A5) + V10 V11 V12, menu = S0..S7, all = every camera. Owner: M2-A.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1].parent
RBUE = REPO / "Tools" / "unreal" / "rbue.py"
LAYOUT = REPO / "Art" / "DiveBar" / "layout.json"
MAP = "/Game/Generated/Maps/L_DiveBar"
OPTIONS = "?Mode=Practice?Game=NineBall?Seed=3?Rate=0"
WARMUP_SECONDS = 12.0

SETS = {
	"db0": [],  # the axis test (capture_axis_test)
	"db1": ["V10", "V01", "V02", "V03", "V04"],
	"db2": ["V01", "V02", "V03", "V04", "V05", "V08"],
	"db3": ["V02", "V03", "V04", "V06", "V07"],
	"m2": [f"V{i:02d}" for i in range(1, 13)] + [f"TH{i}" for i in range(1, 8)],
	"menu": [f"S{i}" for i in range(8)],
}
SETS["all"] = SETS["m2"] + SETS["menu"]
SETS["dev"] = SETS["m2"]


def camera_tag(view: str) -> str:
	if view.startswith("S"):
		return f"RbCam_Menu_{view}"
	return f"RbCam_DB_{view}"


def cameras() -> dict:
	data = json.loads(LAYOUT.read_text(encoding="utf-8"))
	out = {}
	for group in ("views", "trailer", "menu"):
		for c in data["cameras"][group]:
			out[c["tag"]] = c
	return out


def exec_cmds(quality: str, extra: str = "") -> str:
	# quality preset, then the cue ball in hand behind the head string (core -0.62, 0): the rack stays on the foot spot; extra look-dev
	# commands (e.g. "rb.Venue.GroupScale sconces 0") after them
	return f"rb.Quality {quality}, rb.Match.Place -0.62 0" + (f", {extra}" if extra else "")


def capture_axis_test(out_dir: Path, a) -> int:
	"""DB-0: /Game/Dev/M2A/L_AxisTest (rb_make_divebar_fx.py --axis-test builds it) -> <out_dir>/axis_test.png."""
	build = subprocess.run([sys.executable, str(RBUE), "py", "Tools/unreal/editor/rb_make_divebar_fx.py", "--", "--axis-test"], cwd=str(REPO))
	if build.returncode != 0:
		print("[capture_divebar] axis test map build failed")
		return 1
	out = (out_dir / "axis_test.png").resolve()
	cmd = [sys.executable, str(RBUE), "capture", "--map", "/Game/Dev/M2A/L_AxisTest", "--camera", "RbCam_AxisTest", "--res", a.res, "--warmup-seconds", "4",
		"--out", str(out), "--timeout", str(a.timeout)]
	ok = subprocess.run(cmd, cwd=str(REPO)).returncode == 0 and out.exists()
	print(f"[capture_divebar] axis test -> {out}: {'OK' if ok else 'FAILED'}")
	return 0 if ok else 1


def capture(view: str, out: Path, a) -> tuple[bool, str]:
	tag = camera_tag(view)
	cam = cameras().get(tag, {})
	# (a capture run logs the EV lines by itself: ARbVenueInfo turns the EV log on when -RBCapture= is present)
	extra = [f"--extra=-RbLightingState={cam['state']}"] if cam.get("state") else []
	cmd = [sys.executable, str(RBUE), "capture", "--map", MAP + OPTIONS, "--camera", tag, "--res", a.res, "--warmup-seconds", str(a.warmup_seconds),
		"--exec-cmds", exec_cmds(a.quality, a.exec_extra), "--out", str(out), "--timeout", str(a.timeout)] + extra
	log = ""
	for attempt in (1, 2):
		print(f"[capture_divebar] {tag} -> {out} (attempt {attempt})", flush=True)
		proc = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True, encoding="utf-8", errors="replace")
		m = re.search(r"log: (\S+\.log)", proc.stdout)
		log = m.group(1) if m else ""
		print("\n".join(l for l in proc.stdout.splitlines() if "[rbue]" in l or "RbCapture" in l or "RbVenue" in l)[-3000:], flush=True)
		if proc.returncode == 0 and out.exists():
			return True, log
	return False, log


def ev_from_log(log: str, view_tag: str) -> tuple[float, float] | None:
	"""(adapted EV100, camera EV) of the last line the venue info logged for this view. The adapted EV100 is the spec's convention
	(venue-dive-bar 4.4 / 4.5: EV100 = log2(8 L) of the luminance the eye adapted to, cloth 39.5 cd/m^2 -> 8.30): the metered average
	luminance of the adapted view (ev100_scene). The camera's own log2(1 / exposure) differs from it by UE's calibration (0.18 grey:
	log2(8 x 0.18) = 0.53 EV) plus the preset's exposure compensation; it is reported for information."""
	if not log or not Path(log).exists():
		return None
	last = None
	for line in Path(log).read_text(encoding="utf-8", errors="replace").splitlines():
		m = re.search(r"RbVenue EV: view=(\S+) state=(\S+) t=([\d.]+) exposure=(\S+) ev100_camera=(-?[\d.]+) avg_luminance=(\S+) ev100_scene=(-?[\d.]+)", line)
		if m and m.group(1) == view_tag:
			last = (float(m.group(7)), float(m.group(5)))
	return last


def read_png_rgb(path: Path) -> tuple[int, int, list]:
	"""Minimal PNG decoder (8-bit RGB / RGBA, non-interlaced; what the capture writes): (width, height, rows of (r, g, b))."""
	import struct
	import zlib
	data = path.read_bytes()
	pos, idat, w, h, ctype = 8, b"", 0, 0, 2
	while pos < len(data):
		length, kind = struct.unpack(">I4s", data[pos:pos + 8])
		chunk = data[pos + 8:pos + 8 + length]
		if kind == b"IHDR":
			w, h, depth, ctype = struct.unpack(">IIBB", chunk[:10])
			if depth != 8 or ctype not in (2, 6):
				raise ValueError("unsupported PNG")
		elif kind == b"IDAT":
			idat += chunk
		pos += 12 + length
	bpp = 3 if ctype == 2 else 4
	raw = zlib.decompress(idat)
	stride = w * bpp
	rows, prev = [], bytearray(stride)
	for y in range(h):
		f = raw[y * (stride + 1)]
		line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
		for i in range(stride):
			a = line[i - bpp] if i >= bpp else 0
			b = prev[i]
			c = prev[i - bpp] if i >= bpp else 0
			if f == 1:
				line[i] = (line[i] + a) & 255
			elif f == 2:
				line[i] = (line[i] + b) & 255
			elif f == 3:
				line[i] = (line[i] + (a + b) // 2) & 255
			elif f == 4:
				p_ = a + b - c
				pa, pb, pc = abs(p_ - a), abs(p_ - b), abs(p_ - c)
				line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
		rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
		prev = line
	return w, h, rows


def calibrate(a) -> int:
	"""6.1 emissive calibration: capture /Game/Dev/M2A/L_EmissiveCalib (rb_make_divebar_fx.py --calibration) and find the emissive
	luminance whose pixel equals the 100 cd/m^2 lit white card; EmissiveScale = that luminance / 100 -> Art/DiveBar/calibration.json."""
	import math
	out = (REPO / "Saved" / "RbCaptures" / "emissive_calibration.png").resolve()
	cmd = [sys.executable, str(RBUE), "capture", "--map", "/Game/Dev/M2A/L_EmissiveCalib", "--camera", "RbCam_Calib", "--res", "1280x720",
		"--warmup-seconds", "2", "--out", str(out)]
	if subprocess.run(cmd, cwd=str(REPO)).returncode != 0:
		print("[capture_divebar] calibration capture failed")
		return 1
	w, h, rows = read_png_rgb(out)
	nits = [None, 50.0, 70.7, 100.0, 141.4, 200.0]
	# the six 20 cm cards sit in a row at 25 cm pitch in the middle of the frame (rb_make_divebar_fx.py): sample their centres
	values = []
	for k in range(6):
		x = int(w * (180 + 152.5 * k) / 1280.0)
		patch = [rows[y][x + dx][1] for y in range(h // 2 - 5, h // 2 + 5) for dx in range(-5, 6)]
		values.append(sum(patch) / len(patch))
	white = values[0]
	# interpolate log2(luminance) over the emissive cards' pixel values (monotone tone curve)
	match = None
	for k in range(1, 5):
		v0, v1 = values[k], values[k + 1]
		if v0 <= white <= v1 and v1 > v0:
			t = (white - v0) / (v1 - v0)
			match = 2.0 ** (math.log2(nits[k]) + t * (math.log2(nits[k + 1]) - math.log2(nits[k])))
	if match is None:
		print(f"[capture_divebar] calibration: white card {white:.1f} outside the emissive cards {values[1:]}")
		return 1
	scale = round(match / 100.0, 3)  # authored cd/m^2 x scale = the emissive value that renders like a lit surface of that luminance
	data = {"EmissiveScale": scale, "method": "venue-dive-bar 6.1: a Lambertian white card (albedo 0.80) lit to 100 cd/m^2 by a 392.7 cd point "
		"light at 1 m vs emissive cards of 50-200 cd/m^2 (M_DBA_Fallback, EmissiveScale 1), manual exposure, same tone curve; the lit card "
		f"matches an emissive of {match:.1f} cd/m^2", "pixel_values": {"white_100": round(white, 1), **{f"emissive_{nits[k]:g}": round(values[k], 1)
		for k in range(1, 6)}}, "measured_by": "Tools/unreal/capture_divebar.py --calibrate", "engine": "UE 5.8.3, Substrate, Lumen HWRT"}
	(REPO / "Art" / "DiveBar" / "calibration.json").write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8")
	print(f"[capture_divebar] EmissiveScale {scale} (lit 100 cd/m^2 card = emissive {match:.1f} cd/m^2) -> Art/DiveBar/calibration.json")
	return 0


# VDB-T1 rendered white card (venue-dive-bar 4.4): the points of the 4.4 table measured with the lux rig of rb_make_divebar.py
# (same constants: LUX_REFS_NITS, offsets, camera height), lamp only (rb.Venue.LuxProbe switches every other group off), Lumen GI on.
LUX_POINTS = [  # name, core point (x, y, z above the cloth) [m], band [lux]
	("bed centre", (0.0, 0.0, 0.0), (750.0, 910.0)),
	("rail cap side mid (-y)", (0.0, -0.5905, 0.048), (450.0, 550.0)),
	("rail cap at a corner", (1.0985, -0.5905, 0.048), (95.0, 140.0)),
]
LUX_REFS_NITS = [12.5 * 2.0 ** (k / 2.0) for k in range(13)]
LUX_REF_OFFSET_X_M, LUX_REF_FIRST_M, LUX_REF_PITCH_M, LUX_CAMERA_HEIGHT_M, LUX_HFOV_DEG, LUX_ALBEDO = 0.10, -0.21, 0.035, 0.35, 70.0, 0.80


def lux_from_png(path: Path) -> tuple[float, dict]:
	"""Illuminance on the white card from the reference cards in the same frame: per channel, the card's pixel value is placed on
	the references' (pixel -> log2 luminance) curve, L = Rec. 709 luma of the per-channel luminances, E = pi L / albedo."""
	import math
	w, h, rows = read_png_rgb(path)
	f = (w / 2.0) / (LUX_CAMERA_HEIGHT_M * math.tan(math.radians(LUX_HFOV_DEG / 2.0)))  # pixels per metre in the card plane

	def patch(dx_m: float, dy_m: float, half_m: float) -> tuple:
		cx, cy = w / 2.0 + dy_m * f, h / 2.0 - dx_m * f   # image right = world +Y, image up = world +X (camera pitch -90, yaw 0)
		r = max(2, int(half_m * f))
		acc = [0.0, 0.0, 0.0]
		n = 0
		for y in range(int(cy) - r, int(cy) + r + 1):
			for x in range(int(cx) - r, int(cx) + r + 1):
				px = rows[y][x]
				for c in range(3):
					acc[c] += px[c]
				n += 1
		return tuple(a / n for a in acc)

	card = patch(0.0, 0.0, 0.02)
	refs = [patch(LUX_REF_OFFSET_X_M, LUX_REF_FIRST_M + LUX_REF_PITCH_M * k, 0.007) for k in range(len(LUX_REFS_NITS))]
	lum, extrapolated = [], []
	last = len(refs) - 2
	for c in range(3):
		v = card[c]
		got = None
		for k in range(len(refs) - 1):
			v0, v1 = refs[k][c], refs[k + 1][c]
			# inside a segment, or beyond the end segments (log-linear extrapolation; allowed for R / B only: G carries 72 % of the luma)
			if (v0 <= v <= v1 or (k == 0 and v < v0 and c != 1) or (k == last and v > v1 and c != 1)) and v1 > v0:
				t = (v - v0) / (v1 - v0)
				got = 2.0 ** (math.log2(LUX_REFS_NITS[k]) + t * (math.log2(LUX_REFS_NITS[k + 1]) - math.log2(LUX_REFS_NITS[k])))
				if t < 0.0 or t > 1.0:
					extrapolated.append("RGB"[c])
				break
		lum.append(got)
	detail = {"card_rgb": [round(x, 1) for x in card], "refs_g": [round(r[1], 1) for r in refs], "channel_nits": [None if x is None else round(x, 1) for x in lum],
		"extrapolated": extrapolated}
	if any(x is None for x in lum):
		return -1.0, detail
	luma = 0.2126 * lum[0] + 0.7152 * lum[1] + 0.0722 * lum[2]
	detail["luminance_nits"] = round(luma, 1)
	return math.pi * luma / LUX_ALBEDO, detail


def lux(a) -> int:
	"""VDB-T1, rendered half: one capture per LUX_POINTS entry through RbCam_DB_Lux -> lux_<n>.png + lux_report.txt (with the analytic /
	UE-direct table the probe command logs)."""
	out_dir = Path(a.out_dir) if a.out_dir else REPO / "Docs" / "images" / "divebar" / "db2"
	out_dir.mkdir(parents=True, exist_ok=True)
	lines, ok_all, analytic = [], True, []
	for i, (name, core, band) in enumerate(LUX_POINTS):
		out = (out_dir / f"lux_{i + 1}.png").resolve()
		cmd = [sys.executable, str(RBUE), "capture", "--map", MAP + OPTIONS, "--camera", "RbCam_DB_Lux", "--res", "1280x720", "--warmup-seconds", "8",
			"--exec-cmds", f"rb.Quality {a.quality}, rb.Venue.LuxProbe {core[0]} {core[1]} {core[2]}" + (f", {a.exec_extra}" if a.exec_extra else ""),
			"--out", str(out), "--timeout", str(a.timeout)]
		print(f"[capture_divebar] lux {name} -> {out}", flush=True)
		proc = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True, encoding="utf-8", errors="replace")
		m = re.search(r"log: (\S+\.log)", proc.stdout)
		if proc.returncode != 0 or not out.exists():
			lines.append(f"{name:26s} capture FAILED")
			ok_all = False
			continue
		if m and not analytic and Path(m.group(1)).exists():
			analytic = [l.split("RbVenue Lux: ", 1)[1] for l in Path(m.group(1)).read_text(encoding="utf-8", errors="replace").splitlines() if "RbVenue Lux: " in l]
		e, detail = lux_from_png(out)
		ok = band[0] <= e <= band[1]
		ok_all &= ok
		lines.append(f"{name:26s} core ({core[0]:+.4f}, {core[1]:+.4f}, {core[2]:.3f}) m: {e:7.1f} lux  band {band[0]:.0f}-{band[1]:.0f}  {'ok' if ok else 'OUT'}"
			f"   (card L {detail.get('luminance_nits', 'n/a')} cd/m^2, per channel {detail['channel_nits']}"
			f"{' (extrapolated ' + ''.join(detail['extrapolated']) + ')' if detail['extrapolated'] else ''}, card rgb {detail['card_rgb']}, lux_{i + 1}.png)")
	report = ["VDB-T1 lamp-only illuminance (venue-dive-bar 4.4), measured three ways:",
		"  1. rendered white card (this file's table): an 8 x 8 cm Lambertian card (albedo 0.80) on the point, every light group but the",
		f"     lamp off, Lumen GI on, manual exposure; its pixels are read against {len(LUX_REFS_NITS)} emissive reference cards in the same frame",
		f"     ({LUX_REFS_NITS[0]:.1f}-{LUX_REFS_NITS[-1]:.0f} cd/m^2 lit-equivalent through calibration.json's EmissiveScale), E = pi L / 0.80;",
		"  2. the analytic 4.4 model on the placed bulbs and 3. the UE lights' direct light (traced), from ARbVenueInfo::ComputeLuxReport.",
		"", "Rendered white card:"] + lines + ["", f"RESULT (rendered): {'PASS' if ok_all else 'FAIL'}", "", "ARbVenueInfo::ComputeLuxReport:"] + analytic
	(out_dir / "lux_report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
	print("\n".join(report))
	return 0 if ok_all else 1


def main() -> int:
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("--calibrate", action="store_true", help="6.1 emissive calibration -> Art/DiveBar/calibration.json")
	p.add_argument("--lux", action="store_true", help="VDB-T1 rendered white card -> <out-dir, default db2>/lux_report.txt")
	p.add_argument("--set", default="m2", choices=sorted(SETS))
	p.add_argument("--views", default="", help="comma-separated subset, e.g. V01,V03,TH1,S0")
	p.add_argument("--res", default="1920x1080")
	p.add_argument("--quality", default="High", choices=["Low", "Medium", "High", "Epic", "Cinematic"])
	p.add_argument("--warmup-seconds", type=float, default=WARMUP_SECONDS)
	p.add_argument("--timeout", type=float, default=3600)
	p.add_argument("--out-dir", default="", help="default Docs/images/divebar/<set>")
	p.add_argument("--suffix", default="")
	p.add_argument("--exec-extra", default="", help="extra console commands after the defaults, comma-separated (look-dev)")
	a = p.parse_args()
	if a.calibrate:
		return calibrate(a)
	if a.lux:
		return lux(a)
	views = [v.strip() for v in a.views.split(",") if v.strip()] or SETS[a.set]
	out_dir = Path(a.out_dir) if a.out_dir else REPO / "Docs" / "images" / "divebar" / a.set
	out_dir.mkdir(parents=True, exist_ok=True)
	if a.set == "db0":
		return capture_axis_test(out_dir, a)
	cams = cameras()
	failed, report = [], []
	for view in views:
		out = (out_dir / f"{view}{a.suffix}.png").resolve()
		ok, log = capture(view, out, a)
		if not ok:
			failed.append(view)
			continue
		tag = camera_tag(view)
		evs = ev_from_log(log, tag)
		band = cams.get(tag, {}).get("ev100")
		state = cams.get(tag, {}).get("state", "Open")
		if evs is None:
			report.append(f"{view:5s} {tag:16s} {state:9s} EV100   n/a")
			continue
		ev, cam_ev = evs
		if band:
			verdict = "ok" if band[0] <= ev <= band[1] else "OUT"
			report.append(f"{view:5s} {tag:16s} {state:9s} EV100 {ev:6.2f}  band {band[0]:.1f}-{band[1]:.1f}  {verdict:3s}  (camera {cam_ev:5.2f})")
		else:
			report.append(f"{view:5s} {tag:16s} {state:9s} EV100 {ev:6.2f}  (no band)       (camera {cam_ev:5.2f})")
	if report:
		path = out_dir / f"ev_report{a.suffix}.txt"
		path.write_text("VDB-T2 adapted EV100 per view (venue-dive-bar 4.5; Eyes preset, 12 s warm-up, capture log 'RbVenue EV'). EV100 = log2(8 L) of "
			"the metered luminance the view adapted to (the spec's convention, 4.4: cloth 39.5 cd/m^2 -> 8.30); camera = log2(1 / exposure) "
			"with UE's 18 % calibration and the preset's compensation (for information)\n" + "\n".join(report) + "\n", encoding="utf-8")
		print("\n".join(report))
	print(f"[capture_divebar] {'FAILED: ' + ', '.join(failed) if failed else 'all captures OK'}")
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
