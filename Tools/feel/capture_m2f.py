#!/usr/bin/env python3
"""M2-F feel captures (Docs/ue-architecture.md 18.3 F9), host side, Python 3.11 stdlib only. Owner: M2-F.

  python Tools/feel/capture_m2f.py                       # Docs/images/dev/m2f/{ball_in_hand_legal,ball_in_hand_refused,down_after_contact}.png
  python Tools/feel/capture_m2f.py --only ball_in_hand_legal --quality Epic --out-dir Saved/RbCaptures

Every capture renders the generated test room L_M1_TestRoom through the PLAYER's own camera (the rig, Eyes preset: what the player
sees, no look-dev camera) with the full renderer (rbue.py capture: strict, a material / shader compile error fails the run),
1920x1080, the High quality preset, 9-ball practice (the break: ball in hand behind the head string). The rb.Player.* console
commands (RbPlayerCharacter.cpp) put the pawn at the head end and drive it; rb.Player.WhenReady times an action against the
capture's warm-up clock (compilers idle, then WARMUP_SECONDS of game time), so the still shows a moment of it:

  ball_in_hand_legal    the hand carries the cue ball 4 cm over a legal spot in the kitchen; the soft contact preview on the cloth
                        under exactly the target, the lamp's shadow of ball and hand
  ball_in_hand_refused  after a scratch (ball in hand anywhere) Confirm with the cue ball held right over the 1-ball: the spot is
                        illegal, the hand hesitates (a small lift and shake) and does not lower - captured REFUSE_OFFSET s into the
                        hesitation
  down_after_contact    down on the shot at the rack, a 6 m/s scripted break through the stroke component (Commit + Stroke held,
                        released 0.3 s after the contact): the view AFTER_CONTACT s after the contact, still down, calm on the shot

Captures run one after the other (one Unreal process at a time on the shared GPU); a failed capture is retried once. The last
rb.Player.Dump / rb.Player.BallInHand lines of each capture log show the state that was captured.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RBUE = REPO / "Tools" / "unreal" / "rbue.py"

MAP = "/Game/Generated/Maps/L_M1_TestRoom"                # RbAssetPaths::M1TestRoomMap
OPTIONS = "?Mode=Practice?Game=NineBall?Seed=3?Rate=1"    # live playback in real time (the watching after the contact)
RES = "1920x1080"
WARMUP_SECONDS = 8.0                                        # auto exposure (Eyes 0.7 EV/s down), Lumen, TSR
REFUSE_OFFSET = 0.22                                        # [s] into the hesitation (its lift peaks at 0.225 s)
STROKE_LEAD = 1.4                                           # [s] from the scripted stroke start to the contact (backswing, pause)
AFTER_CONTACT = 0.9                                         # [s] the view after the contact

# Core table frame [m] (+x foot, +y left seen from the head end); the 9-ft table's head string is at x = -0.635.
STAND = "rb.Player.Teleport -1.78 0.32 -8 -30"              # head end, a little left of the long axis, looking down the table
LEGAL_SPOT = (-0.80, 0.12)                                  # in the kitchen
SCRATCH_LAYOUT = {0: (-0.98, 0.42), 1: (-0.62, 0.06), 2: (-0.30, -0.22), 9: (0.45, 0.18)}  # the cue ball scratches in the head-left corner
ILLEGAL_SPOT = SCRATCH_LAYOUT[1]                            # on the 1-ball


def captures() -> list[tuple[str, str]]:
	# The look point is set once the lean over the table has settled (the hand then needs ~1.5 s to carry the ball there).
	legal = f"{STAND}, rb.Player.WhenReady 1.0 rb.Player.LookAt {LEGAL_SPOT[0]} {LEGAL_SPOT[1]}"
	# Refused: a scratch (committed at once) gives ball in hand anywhere; the hand holds the cue ball right over the 1-ball, where
	# it cannot go, and Confirm is refused.
	layout = " ".join(f"{i} {x} {y}" for i, (x, y) in SCRATCH_LAYOUT.items())
	refused = (f"rb.Match.Rate 0, rb.Match.Place -0.70 0.12, rb.Match.Layout {layout}, rb.Match.StrikeAt 1.6 -1.27 0.635, rb.Match.Rate 1, "
		f"{STAND}, rb.Player.WhenReady 1.0 rb.Player.LookAt {ILLEGAL_SPOT[0]} {ILLEGAL_SPOT[1]}, "
		f"rb.Player.WhenReady {WARMUP_SECONDS - REFUSE_OFFSET - 0.05:.2f} rb.Player.Confirm, rb.Player.WhenReady {WARMUP_SECONDS - 0.03:.2f} rb.Match.Dump")
	stroke_at = WARMUP_SECONDS - AFTER_CONTACT - STROKE_LEAD
	down = ("rb.Match.Place -0.70 0.12, rb.Player.Teleport -1.85 0.12 0 -22, rb.Player.AimAt 0.635 0.0 3, rb.Player.GetDown, "
		f"rb.Player.WhenReady {stroke_at:.2f} rb.Player.Stroke 6")
	dump = f", rb.Player.WhenReady {WARMUP_SECONDS - 0.03:.2f} rb.Player.Dump, rb.Player.WhenReady {WARMUP_SECONDS - 0.03:.2f} rb.Player.BallInHand"
	return [
		("ball_in_hand_legal.png", legal + dump),
		("ball_in_hand_refused.png", refused + dump),
		("down_after_contact.png", down + dump),
	]


def capture(out: Path, commands: str, quality: str, timeout: float) -> bool:
	cmd = [sys.executable, str(RBUE), "capture", "--map", MAP + OPTIONS, "--res", RES, "--warmup-seconds", str(WARMUP_SECONDS),
		"--exec-cmds", f"rb.Quality {quality}, {commands}", "--out", str(out), "--timeout", str(timeout)]
	for attempt in (1, 2):
		print(f"[capture_m2f] {out.name} (attempt {attempt})", flush=True)
		if subprocess.run(cmd, cwd=str(REPO)).returncode == 0 and out.exists():
			return True
	return False


def main() -> int:
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("--quality", default="High", choices=["Low", "Medium", "High", "Epic", "Cinematic"])
	p.add_argument("--only", default="", help="comma-separated capture names without .png (default: all three)")
	p.add_argument("--out-dir", default=str(REPO / "Docs" / "images" / "dev" / "m2f"))
	p.add_argument("--suffix", default="", help="appended to the file names, e.g. _epic")
	p.add_argument("--timeout", type=float, default=3600)
	a = p.parse_args()
	only = {t.strip() for t in a.only.split(",") if t.strip()}
	out_dir = Path(a.out_dir)
	out_dir.mkdir(parents=True, exist_ok=True)
	failed = []
	for name, commands in captures():
		stem = Path(name).stem
		if only and stem not in only:
			continue
		out = (out_dir / f"{stem}{a.suffix}.png").resolve()
		if not capture(out, commands, a.quality, a.timeout):
			failed.append(stem)
	print(f"[capture_m2f] {'FAILED: ' + ', '.join(failed) if failed else 'all captures OK'}")
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
