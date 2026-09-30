"""M2-D dev tool (Docs/ue-architecture.md 18.4): menu captures, the process-restart settings check and the L_Title validator.

Host side (plain Python; PowerShell, or Git Bash with MSYS_NO_PATHCONV=1):
  python Tools/unreal/editor/rb_dev_m2d.py --list                  prints the capture commands of Docs/images/dev/m2d/
  python Tools/unreal/editor/rb_dev_m2d.py --capture [name ...]    runs them through rbue.py (all when no name is given)
  python Tools/unreal/editor/rb_dev_m2d.py --restart-check         settings survive a process restart (two game processes)
Editor side:
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2d.py validates /Game/Generated/Maps/L_Title (rb_make_title.py)

Captures go through the headless capture with the M2-D dev switch -RbUiScreen=<Screen> (URbUiSubsystem): Title (no clean first
seconds), Title.Play, Pause, Settings.<Page>, KeyHints / KeyHints.Down (the hints never fade; .Down: the player gets down on the
shot). The in-game screens run on the test room (the dive bar joins when M2-A's L_DiveBar exists: add its rows here) with a
hot-seat match after the scripted break (?Rate=0 commits at once, so the pause block shows the match after a shot).

The restart check starts the game twice with a fresh -UserDir (the developer's own GameUserSettings.ini is never touched):
process 1 sets rows through rb.Settings.Set and saves (rb.Settings.Save, as the settings menu does when it closes), process 2
only dumps; every row of process 2 must equal process 1's (Custom mix, Reduced motion and its backup, controls, audio).
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys

TITLE = "/Game/Generated/Maps/L_Title"
ROOM = "/Game/Generated/Maps/L_M1_TestRoom"
OUT = "Docs/images/dev/m2d"
BREAK = "rb.Match.Break 9"                     # UE-6b dev command: the break at 9 m/s
PLACE = "RbPlaceCueBall -0.735 0.12"           # ball in hand behind the head string (the M1Rack spot)
MATCH = "?Mode=HotSeat?Seed=12?Rate=0?P1=Alex?P2=Sam"

# (png, map with options, exec commands, dev screen, resolution)
CAPTURES = [
	("title", TITLE, "", "Title", "1920x1080"),
	("title_play", TITLE, "", "Title.Play", "1920x1080"),
	("pause", ROOM + MATCH, BREAK, "Pause", "1920x1080"),
	("settings_graphics", ROOM + MATCH, BREAK, "Settings.Graphics", "1920x1080"),
	("settings_display", ROOM + MATCH, BREAK, "Settings.Display", "1920x1080"),
	("settings_camera", ROOM + MATCH, BREAK, "Settings.Camera", "1920x1080"),
	("settings_controls", ROOM + MATCH, BREAK, "Settings.Controls", "1920x1080"),
	("settings_audio", ROOM + MATCH, BREAK, "Settings.Audio", "1920x1080"),
	("settings_camera_1280x800", ROOM + MATCH, BREAK, "Settings.Camera", "1280x800"),   # UX-T07 / T26: the DPI rule
	("key_hints", ROOM + "?Mode=Practice", PLACE, "KeyHints.Down", "1920x1080"),
	("key_hints_walking", ROOM + "?Mode=Practice", PLACE, "KeyHints", "1920x1080"),
]

# Rows the restart check changes (id, value): a Custom mix based on Epic, Reduced motion on (its backup holds the camera rows
# set before it), controls, audio, display-free rows only (window mode / resolution would move the offscreen window).
RESTART_ROWS = [
	("gfx.preset", 3), ("gfx.shadows", 1), ("gfx.volumetricFog", 4), ("gfx.resolutionScale", 80), ("gfx.filmGrain", 0.25),
	("cam.look", 1), ("cam.fov", 63), ("cam.headBob", 0.6), ("cam.bodySway", 0.8), ("cam.mountShake", 0.5), ("cam.posture", 2),
	("cam.reducedMotion", 1),
	("ctl.mouseDpi", 1600), ("ctl.aimSpeed", 1.35), ("ctl.fineAim", 0.1), ("ctl.invertLookY", 1), ("ctl.keyHints", 0),
	("aud.master", 0.85), ("aud.music", 0.35), ("aud.interface", 0.2),
	("dsp.frameCap", 4), ("dsp.vsync", 1),
]

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
RBUE = os.path.join(REPO, "Tools", "unreal", "rbue.py")


def capture_args(name: str, level: str, cmds: str, screen: str, res: str) -> list:
	args = ["capture", "--map", level, "--out", f"{OUT}/{name}.png", "--res", res, f"--extra=-RbUiScreen={screen}"]
	if cmds:
		args += ["--exec-cmds", cmds]
	return args


def run_captures(wanted: list) -> int:
	failed = 0
	for spec in CAPTURES:
		if wanted and spec[0] not in wanted:
			continue
		code = subprocess.call([sys.executable, RBUE] + capture_args(*spec), cwd=REPO)
		print(f"[m2d] {spec[0]}: {'OK' if code == 0 else 'FAILED'}", flush=True)
		failed += 1 if code else 0
	return 1 if failed else 0


def game_run(user_dir: str, exec_cmds: str, log_name: str) -> list:
	"""One headless game process (L_Title, NullRHI) with its own user dir; returns the log lines."""
	ue_root = os.environ.get("RB_UE_ROOT", r"C:/Program Files/Epic Games/UE_5.8")
	editor_cmd = os.path.join(ue_root, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe")
	log = os.path.join(REPO, "Saved", "RbLogs", log_name)
	os.makedirs(os.path.dirname(log), exist_ok=True)
	cmd = [f'"{editor_cmd}"', f'"{os.path.join(REPO, "RawBreak.uproject")}"', TITLE, "-game", "-NullRHI", "-unattended", "-nop4", "-nosplash",
		"-NoSound", "-stdout", "-FullStdOutLogOutput", "-NoLogTimes", f'-UserDir="{user_dir}"', f'-ExecCmds="{exec_cmds}"', f'-abslog="{log}"']
	print("[m2d] " + " ".join(cmd), flush=True)
	proc = subprocess.run(" ".join(cmd), cwd=REPO, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=1800)
	return proc.stdout.splitlines()


def dump_rows(lines: list) -> dict:
	rows = {}
	for line in lines:
		m = re.search(r"rb\.Settings: (\w+\.\w+)\s+(\S+)\s+(.*)$", line)
		if m:
			rows[m.group(1)] = (float(m.group(2)), m.group(3).strip())
	return rows


def restart_check() -> int:
	user_dir = os.path.join(REPO, "Saved", "RbTests", "RestartCheck")
	shutil.rmtree(user_dir, ignore_errors=True)
	os.makedirs(user_dir, exist_ok=True)
	sets = ", ".join(f"rb.Settings.Set {row} {value}" for row, value in RESTART_ROWS)
	first = dump_rows(game_run(user_dir, f"{sets}, rb.Settings.Save, rb.Settings.Dump, quit", "m2d-restart-1.log"))
	second = dump_rows(game_run(user_dir, "rb.Settings.Dump, quit", "m2d-restart-2.log"))
	third = dump_rows(game_run(user_dir, "rb.Settings.Set cam.reducedMotion 0, rb.Settings.Dump, quit", "m2d-restart-3.log"))
	problems = []
	if len(first) < 40 or len(second) < 40:
		problems.append(f"dumps incomplete: {len(first)} / {len(second)} rows (log: Saved/RbLogs/m2d-restart-*.log)")
	for row, (value, text) in first.items():
		other = second.get(row)
		if other is None or abs(other[0] - value) > 1e-4 * max(1.0, abs(value)):
			problems.append(f"{row}: {value} ({text}) before the restart, {other} after")
	# Reduced motion was on at the save: switching it off in a third process restores the rows set before it.
	for row, expected in (("cam.headBob", 0.6), ("cam.bodySway", 0.8), ("cam.mountShake", 0.5), ("cam.posture", 2)):
		got = third.get(row, (None,))[0]
		if got is None or abs(got - expected) > 1e-4:
			problems.append(f"Reduced motion off after the restart: {row} = {got}, expected {expected}")
	for row, value in RESTART_ROWS:
		if row in ("cam.headBob", "cam.bodySway", "cam.mountShake", "cam.posture", "gfx.preset"):
			continue  # Reduced motion overrides them; the preset reads Custom after a row changed
		got = second.get(row, (None,))[0]
		if got is None or abs(got - value) > 1e-4 * max(1.0, abs(value)):
			problems.append(f"{row}: set {value}, read {got} after the restart")
	preset = second.get("gfx.preset", (None, ""))
	if preset[0] != 5.0:
		problems.append(f"gfx.preset after the restart: {preset}, expected Custom (5)")
	for line in problems:
		print(f"[m2d] RESTART FAIL {line}", flush=True)
	print(f"[m2d] restart check {'OK' if not problems else 'FAILED'}: {len(second)} rows compared", flush=True)
	shutil.rmtree(user_dir, ignore_errors=True)
	return 1 if problems else 0


def host_main() -> int:
	if "--restart-check" in sys.argv:
		return restart_check()
	if "--capture" not in sys.argv:
		for spec in CAPTURES:
			print("python Tools/unreal/rbue.py " + " ".join(f'"{a}"' if " " in a or "?" in a else a for a in capture_args(*spec)))
		return 0
	wanted = [a for a in sys.argv[sys.argv.index("--capture") + 1:] if not a.startswith("--")]
	return run_captures(wanted)


try:
	import unreal
except ImportError:  # host side (plain Python)
	unreal = None
if unreal is None or "--list" in sys.argv:
	sys.exit(host_main())

# --- editor side: the L_Title validator ---------------------------------------------------------------------------------
sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402


def validate_title() -> None:
	if not unreal.EditorAssetLibrary.does_asset_exist(TITLE):
		rb.fail(f"{TITLE} missing: run Tools/unreal/editor/rb_make_title.py")
	world = unreal.EditorLoadingAndSavingUtils.load_map(TITLE)
	if world is None:
		rb.fail(f"could not load {TITLE}")
	mode = world.get_world_settings().get_editor_property("default_game_mode")
	if mode is None or mode.get_name() != "RbTitleGameMode":
		rb.fail(f"L_Title World Settings game mode is {mode}, expected RbTitleGameMode")
	actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
	cameras = [a for a in actors if isinstance(a, unreal.CameraActor) and "RbCam_Title" in [str(t) for t in a.tags]]
	tables = [a for a in actors if isinstance(a, unreal.RbTable)]
	lights = [a for a in actors if isinstance(a, (unreal.SpotLight, unreal.RectLight, unreal.PointLight))]
	if len(cameras) != 1:
		rb.fail(f"L_Title needs exactly one camera tagged RbCam_Title, found {len(cameras)}")
	if len(tables) != 1 or "RbPlayerTable" not in [str(t) for t in tables[0].tags]:
		rb.fail("L_Title needs one ARbTable tagged RbPlayerTable")
	if not lights:
		rb.fail("L_Title has no lights")
	rb.log(f"L_Title OK: camera {cameras[0].get_actor_label()}, table {tables[0].get_editor_property('preset')}, {len(lights)} lights, {len(actors)} actors")


validate_title()
