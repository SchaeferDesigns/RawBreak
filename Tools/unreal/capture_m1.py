#!/usr/bin/env python3
"""M1 acceptance captures (Docs/ue-architecture.md 12 A7), host side, Python 3.11 stdlib only. Owner: UE-8.

  python Tools/unreal/capture_m1.py                      # Docs/images/m1/{overhead,chin_on_cue,ball_closeup,room}.png
  python Tools/unreal/capture_m1.py --dev                # the dev room (flat table materials) -> Docs/images/dev/UE-8/
  python Tools/unreal/capture_m1.py --only RbCam_Overhead --quality Epic --out-dir Saved/RbCaptures
  python Tools/unreal/capture_m1.py --set player         # the player's own views: Docs/images/m1/{standing,down_on_shot,after_break}.png

Every capture renders L_M1_TestRoom through one of its ARbLookDevCameras (Eyes preset) with the full renderer
(rbue.py capture: strict, a material / shader compile error fails the run), 1920x1080, the High quality preset (rb.Quality),
an 8-ball rack (all 16 balls) and the cue ball placed at the chin-on-cue position (rb.Match.Place). The layout constants are
shared with the level generator (Tools/unreal/editor/rb_m1_layout.py). Captures run one after the other (one Unreal process at
a time on the shared GPU); a capture that fails is retried once.
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


def capture(map_url: str, camera: str, out: Path, quality: str, warmup_seconds: float, timeout: float, exec_cmds: str = "") -> bool:
	cmd = [sys.executable, str(RBUE), "capture", "--map", map_url, "--camera", camera, "--res", layout.CAPTURE_RES,
		"--warmup-seconds", str(warmup_seconds), "--exec-cmds", exec_cmds or layout.capture_exec_cmds(quality), "--out", str(out),
		"--timeout", str(timeout)]
	for attempt in (1, 2):
		print(f"[capture_m1] {camera} -> {out} (attempt {attempt})", flush=True)
		if subprocess.run(cmd, cwd=str(REPO)).returncode == 0 and out.exists():
			return True
	return False


def main() -> int:
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("--dev", action="store_true", help="capture the dev room (/Game/Dev/UE8, flat table materials)")
	p.add_argument("--quality", default="High", choices=["Low", "Medium", "High", "Epic", "Cinematic"])
	p.add_argument("--only", default="", help="comma-separated camera tags (default: all four)")
	p.add_argument("--out-dir", default="", help="default Docs/images/m1 (dev: Docs/images/dev/UE-8)")
	p.add_argument("--suffix", default="", help="appended to the file names, e.g. _epic")
	p.add_argument("--warmup-seconds", type=float, default=layout.CAPTURE_WARMUP_SECONDS)
	p.add_argument("--timeout", type=float, default=3600)
	p.add_argument("--set", default="all", choices=["all", "lookdev", "player"],
		help="lookdev = the four A7 look-dev cameras, player = the pawn's first-person views (M1 integration)")
	a = p.parse_args()

	map_url = (layout.DEV_MAP if a.dev else layout.MAP) + layout.CAPTURE_OPTIONS
	out_dir = Path(a.out_dir) if a.out_dir else REPO / ("Docs/images/dev/UE-8" if a.dev else "Docs/images/m1")
	only = {t.strip() for t in a.only.split(",") if t.strip()}
	failed = []
	for camera, name in (layout.CAPTURES if a.set in ("all", "lookdev") else []):
		if only and camera not in only:
			continue
		out = (out_dir / name).with_name(Path(name).stem + a.suffix + ".png")
		if not capture(map_url, camera, out.resolve(), a.quality, a.warmup_seconds, a.timeout):
			failed.append(camera)
	# The player's own views: through the pawn's camera (no --camera), the M1 map in 9-ball practice.
	player_url = (layout.DEV_MAP if a.dev else layout.MAP) + layout.PLAYER_OPTIONS
	for name, commands in (layout.PLAYER_CAPTURES if a.set in ("all", "player") else []):
		if only and Path(name).stem not in only:
			continue
		out = (out_dir / name).with_name(Path(name).stem + a.suffix + ".png")
		if not capture(player_url, "", out.resolve(), a.quality, a.warmup_seconds, a.timeout, layout.player_exec_cmds(commands, a.quality)):
			failed.append(name)
	print(f"[capture_m1] {'FAILED: ' + ', '.join(failed) if failed else 'all captures OK'}")
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
