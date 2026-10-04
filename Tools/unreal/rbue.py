#!/usr/bin/env python3
"""RAW BREAK headless Unreal pipeline runner (host side, Python 3.11 stdlib only).

Every Unreal step of the project runs through this script so that no human ever has to open the editor
(Docs/ue-architecture.md section 9; M2 additions 18.9 / 18.10). Sub-commands:

  build     UnrealBuildTool build of a target            rbue.py build [--target RawBreakEditor] [--config Development]
  py        run an editor Python script headless         rbue.py py Tools/unreal/editor/rb_make_test_room.py [-- script args]
  capture   render views of a map to PNGs                rbue.py capture --map /Game/Dev/PipelineProof/L_PipelineProof --out Docs/images/x.png
                                                         [--camera A,B,C --out Docs/images/set/{camera}.png] [--show-ui] [--hide-tags T]
  test      run UE Automation tests headless             rbue.py test --filter RawBreak. [--render] [--sound]
  game      launch the game (windowed, for a human)      rbue.py game [--map ...]
  perf      frame-time log of a map (M2-A9)              rbue.py perf --map /Game/Generated/Maps/L_DiveBar --res 2560x1440 --out Docs/perf/x.json
  package   cooked, staged Win64 build (M2-A10)          rbue.py package [--config Development] [--label M2]
  ledger    check / merge the licence ledger             rbue.py ledger [--merge] [--check]
  core      build + run the BilliardsCore tests (CMake)  rbue.py core [--slow] [--config Debug] [-- MOT_ -Integ_]
  owners    18.2 file-ownership check of a package       rbue.py owners --package M2-F --branch m2f   |   rbue.py owners --who <path>
  selftest  checks of this script's pure helpers         rbue.py selftest

Logs go to Saved/RbLogs/<command>-<timestamp>.log (UE's -abslog). Exit code 0 = success.
Environment: RB_UE_ROOT overrides the engine root (default C:/Program Files/Epic Games/UE_5.8), RB_BUILDS_DIR the root of
packaged builds (default <main checkout>/../RawBreak_Builds). Owner: UE-0 / M2-0 (architect).
"""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
UPROJECT = REPO / "RawBreak.uproject"
UE_ROOT = Path(os.environ.get("RB_UE_ROOT", r"C:/Program Files/Epic Games/UE_5.8"))
BUILD_BAT = UE_ROOT / "Engine/Build/BatchFiles/Build.bat"
RUN_UAT = UE_ROOT / "Engine/Build/BatchFiles/RunUAT.bat"
EDITOR_CMD = UE_ROOT / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
EDITOR = UE_ROOT / "Engine/Binaries/Win64/UnrealEditor.exe"
LOG_DIR = REPO / "Saved/RbLogs"

# Flags shared by every unattended editor / game process.
COMMON = ["-unattended", "-nop4", "-nosplash", "-NoSound", "-stdout", "-FullStdOutLogOutput", "-NoLogTimes"]


def _log_path(kind: str) -> Path:
	LOG_DIR.mkdir(parents=True, exist_ok=True)
	stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
	return LOG_DIR / f"{kind}-{stamp}.log"


def _kill_tree(pid: int) -> None:
	subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True)


def _q(token: str) -> str:
	"""Quotes a whole argument if it contains spaces (UE parses its own command line; no CRT escaping)."""
	return f'"{token}"' if " " in token and '"' not in token else token


def _unmsys(value: str) -> str:
	"""Undoes Git-Bash (MSYS) path conversion of UE package paths: 'C:/Program Files/Git/Game/X' -> '/Game/X'."""
	m = re.match(r"^[A-Za-z]:[/\\].*?[/\\]Git([/\\](Game|Engine)[/\\].*)$", value)
	return m.group(1).replace("\\", "/") if m else value


def _run(cmd: list[str], timeout: float, echo_filter: re.Pattern | None = None, cwd: Path | None = None) -> tuple[int, list[str]]:
	"""Runs cmd (tokens already in UE command-line form, see _q), streams matching lines, returns (exit code, lines).
	Kills the whole process tree on timeout. On Windows the command line is passed verbatim to CreateProcess, so
	-Key="value with spaces" reaches UE exactly as written (subprocess list quoting would escape the quotes).
	The output is read by a thread and the timeout is the wait for the PROCESS, not a check per output line (review: a process
	that hangs WITHOUT printing - a deadlocked editor, a PIE test waiting forever, a modal prompt of a cook - blocked the line loop
	for good, so the timeout never fired and the Unreal process stayed alive on the shared machine; likewise a grandchild that
	inherited the pipe kept the loop waiting after the process itself had exited)."""
	line = " ".join(_q(c) for c in cmd)
	print("[rbue] " + line, flush=True)
	start = time.monotonic()
	proc = subprocess.Popen(line if os.name == "nt" else cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=str(cwd or REPO),
		text=True, encoding="utf-8", errors="replace", bufsize=1)
	lines: list[str] = []

	def read_output() -> None:
		assert proc.stdout is not None
		for out_line in proc.stdout:
			lines.append(out_line.rstrip("\n"))
			if echo_filter is None or echo_filter.search(out_line):
				print(out_line.rstrip("\n"), flush=True)

	reader = threading.Thread(target=read_output, name="rbue-output", daemon=True)
	reader.start()
	try:
		proc.wait(timeout=timeout)
	except subprocess.TimeoutExpired:
		print(f"[rbue] TIMEOUT after {timeout:.0f} s - killing process tree", flush=True)
		_kill_tree(proc.pid)
		reader.join(timeout=30.0)
		return 124, list(lines)
	# The process has exited: drain what is left in the pipe (a grandchild that still holds it open is not waited for).
	reader.join(timeout=15.0)
	print(f"[rbue] exit {proc.returncode} after {time.monotonic() - start:.1f} s", flush=True)
	return proc.returncode, list(lines)


# A material whose shader fails to compile renders with the engine's default material instead of failing the run, so a
# capture would still "succeed" (e.g. a broken Custom-node include of the ball shader). Captures fail on these lines unless
# --allow-shader-errors is given (review R-10).
SHADER_ERRORS = re.compile(r"(Failed to compile Material|Default Material will be used|LogShaderCompilers: Error|LogMaterial: Error)")

# Lines worth echoing from a long editor log (errors, our own log categories, python output, progress).
ECHO = re.compile(r"(Error|error:|LogPython|LogRawBreak|LogRb|Automation|Test Completed|TEST COMPLETE|Compiling \d+ shaders|"
	r"shaders left|Result:|Warning: .*Rb|FAILED|PASSED|Success)", re.IGNORECASE)

EV_LINE = re.compile(r"RbCapture: EV100 (\S+) (\S+)")


def compiler_warnings(lines: list[str]) -> list[str]:
	"""The compiler warnings of a UBT log (MSVC 'file(line): warning C4xxx: ...', unique): M2-A1 wants 0 from project code, so a
	green build with warnings shows them in its summary (an incremental build only reports the files it recompiled)."""
	return sorted(set(l.strip() for l in lines if re.search(r"\): warning [A-Z]+\d+:", l)))


def cmd_build(a: argparse.Namespace) -> int:
	cmd = [str(BUILD_BAT), a.target, "Win64", a.config, f"-Project={UPROJECT}", "-WaitMutex", "-NoHotReload"]
	if a.clean:
		cmd.append("-Clean")
	code, lines = _run(cmd, a.timeout, re.compile(r"(error|warning|Result:|Total execution)", re.IGNORECASE))
	_log_path("build").write_text("\n".join(lines), encoding="utf-8")
	errors = [l for l in lines if re.search(r"\berror\b", l, re.IGNORECASE) and "0 error" not in l]
	if code == 0 and not any("Result: Succeeded" in l for l in lines):
		code = 1
	warnings = compiler_warnings(lines)
	for warning in warnings[:20]:
		print(f"[rbue] compiler warning: {warning}")
	print(f"[rbue] build {'OK' if code == 0 else 'FAILED'} ({len(errors)} error lines, {len(warnings)} compiler warning(s))")
	return code


def cmd_py(a: argparse.Namespace) -> int:
	script = Path(a.script).resolve()
	if not script.exists():
		print(f"[rbue] no such script: {script}")
		return 2
	log = _log_path("py-" + script.stem)
	script_arg = str(script).replace("\\", "/")
	if a.args:
		script_arg += " " + " ".join(a.args)
	cmd = [str(EDITOR_CMD), str(UPROJECT), "-run=pythonscript", f'-script="{script_arg}"', f'-abslog="{log}"'] + COMMON
	if not a.rhi:
		cmd.append("-NullRHI")
	code, lines = _run(cmd, a.timeout, ECHO)
	# The commandlet returns 0 even when the script raised: scan for Python errors and our explicit failure marker.
	py_errors = [l for l in lines if "LogPython: Error" in l or "RBUE_FAIL" in l]
	if py_errors:
		print(f"[rbue] python reported {len(py_errors)} error line(s)")
		code = code or 1
	print(f"[rbue] log: {log}")
	return code


def _common(a: argparse.Namespace) -> list[str]:
	"""COMMON without -NoSound when --sound is given (audio tests need a device or the mixer's null device, audio.md 8.8), plus
	the raw extra UE arguments of --extra (e.g. --extra -RbUiScreen=Pause; M2, Docs/ue-architecture.md 18.9). --sound mutes the
	device's final output (-MuteAudio: the mixer still renders every block, the submix recordings of the audio tests are
	unaffected) unless --audible is given too (review: a test run on the shared machine must not play breaks through the owner's
	speakers)."""
	sound = getattr(a, "sound", False)
	flags = [f for f in COMMON if not (sound and f == "-NoSound")]
	extra = list(getattr(a, "extra", None) or [])
	if sound and not getattr(a, "audible", False) and not any(e.lower() == "-muteaudio" for e in extra):
		flags.append("-MuteAudio")
	return flags + extra


def _split_list(value: str) -> list[str]:
	"""Mirrors URbHeadlessCaptureSubsystem::ParseList (',' / ';' separated, trimmed, empty entries dropped)."""
	return [part.strip() for part in re.split(r"[,;]", value.replace('"', "")) if part.strip()]


def resolve_output(pattern: str, camera: str, count: int) -> str:
	"""Mirrors URbHeadlessCaptureSubsystem::ResolveOutputPath: {camera} token, else <stem>_<camera> for several cameras."""
	safe = re.sub(r'[\\/:*?"<>| ]', "_", camera or "player")
	if "{camera}" in pattern:
		return pattern.replace("{camera}", safe)
	if count <= 1:
		return pattern
	path = Path(pattern)
	return str(path.with_name(f"{path.stem}_{safe}{path.suffix or '.png'}"))


def _game_target(a: argparse.Namespace) -> list[str]:
	"""The executable + project (editor binary in -game mode, uncooked) or a packaged game executable (--exe)."""
	exe = getattr(a, "exe", "")
	if exe:
		return [str(Path(exe).resolve())]
	return [str(EDITOR_CMD), str(UPROJECT)]


def _map_arg(a: argparse.Namespace) -> list[str]:
	return [_unmsys(a.map)] if a.map else []


def cmd_capture(a: argparse.Namespace) -> int:
	cameras = _split_list(a.camera)
	pattern = str(Path(a.out).resolve())
	outs = [Path(resolve_output(pattern, c, len(cameras))) for c in (cameras or [""])]
	for out in outs:
		out.parent.mkdir(parents=True, exist_ok=True)
		if out.exists():
			out.unlink()
	log = _log_path("capture")
	resx, resy = a.res.lower().split("x")
	cmd = _game_target(a) + _map_arg(a) + (["-game"] if not a.exe else []) + ["-RenderOffscreen", "-Windowed", f"-ResX={resx}", f"-ResY={resy}",
		"-ForceRes", f'-RBCapture="{pattern}"', f"-RBCaptureWarmup={a.warmup}", f"-RBCaptureWarmupSeconds={a.warmup_seconds}",
		f"-RBCaptureTimeout={max(60.0, a.timeout - 60):.0f}", f'-abslog="{log}"'] + _common(a)
	if cameras:
		cmd.append(f'-RBCaptureCamera="{",".join(cameras)}"')
	if a.show_ui:
		cmd.append("-RBCaptureShowUI")
	if a.hide_tags:
		cmd.append(f'-RBCaptureHideTags="{",".join(_split_list(a.hide_tags))}"')
	if a.exec_cmds:
		cmd.append(f'-ExecCmds="{a.exec_cmds}"')
	code, lines = _run(cmd, a.timeout, ECHO)
	missing = [str(out) for out in outs if not (out.exists() and out.stat().st_size > 0)]
	ok = not missing and not any("RbCapture: FAILED" in l for l in lines)
	shader_errors = [l for l in lines if SHADER_ERRORS.search(l)]
	if shader_errors:
		print(f"[rbue] {len(shader_errors)} material / shader compile error line(s), e.g.: {shader_errors[0]}")
		if not a.allow_shader_errors:
			ok = False
	for match in (EV_LINE.search(l) for l in lines):
		if match:
			print(f"[rbue] EV100 {match.group(1)}: {match.group(2)}")
	for path in missing:
		print(f"[rbue] missing output: {path}")
	print(f"[rbue] capture {'OK' if ok else 'FAILED'}: {len(outs) - len(missing)}/{len(outs)} view(s)  log: {log}")
	return 0 if ok else (code or 1)


TEST_RESULT = re.compile(r"Test Completed\. Result=\{(\w+)\}(?:.*?Path=\{([^}]*)\})?")
TEST_FOUND = re.compile(r"Found (\d+) automation tests based on")
TEST_EXIT = re.compile(r"\*\*\*\* TEST COMPLETE\. EXIT CODE: (-?\d+) \*\*\*\*")
# Tests that need the audio device (M2-C, rbue.py test --sound): without it they fail with "no audio device".
AUDIO_TESTS = "RawBreak.Functional.Audio."


def test_run_problems(lines: list[str], sound: bool = False) -> tuple[int, int, list[str]]:
	"""(passed, failed, problems) of an automation run's log lines. Besides failed tests, a run is only green when every test the
	filter found completed and the automation shut down normally (review: an editor that crashed or hung after N green tests used
	to count as "N passed, 0 failed" = exit 0, e.g. a crash inside a LevelSmoke PIE session at the integration round)."""
	matches = [m for m in (TEST_RESULT.search(l) for l in lines) if m]
	results = [m.group(1) for m in matches]
	passed = sum(1 for r in results if r in ("Success", "Passed"))
	failed_paths = [m.group(2) or "?" for m in matches if m.group(1) in ("Fail", "Failed")]
	failed = len(failed_paths)
	found = next((int(m.group(1)) for m in (TEST_FOUND.search(l) for l in lines) if m), None)
	exit_code = next((int(m.group(1)) for m in (TEST_EXIT.search(l) for l in lines) if m), None)
	problems = []
	if failed:
		problems.append(f"{failed} test(s) failed: {', '.join(failed_paths[:10])}{' ...' if failed > 10 else ''}")
		if not sound and any(p.startswith(AUDIO_TESTS) for p in failed_paths):
			problems.append(f"{AUDIO_TESTS}* need the audio device: run them (or the whole suite) with rbue.py test --sound (output muted)")
	if found is None:
		problems.append("no 'Found N automation tests' line: the automation run did not start")
	elif found == 0:
		problems.append("the filter matched no test")
	elif len(results) != found:
		problems.append(f"only {len(results)} of the {found} tests found completed (the editor crashed, hung or quit early)")
	if exit_code is None:
		problems.append("no 'TEST COMPLETE' line: the automation did not shut down normally (crash?)")
	elif exit_code != 0 and not failed:  # a failed test alone already gives -1
		problems.append(f"automation exit code {exit_code} without a failed test (an error outside the tests)")
	other = len(results) - passed - failed
	if other:
		problems.append(f"{other} test(s) neither passed nor failed (skipped / not run)")
	return passed, failed, problems


def cmd_test(a: argparse.Namespace) -> int:
	log = _log_path("test")
	report = REPO / "Saved/RbLogs/AutomationReport"
	cmd = [str(EDITOR_CMD), str(UPROJECT), f'-ExecCmds="Automation RunTests {a.filter}; Quit"', '-TestExit="Automation Test Queue Empty"',
		f'-ReportExportPath="{report}"', f'-abslog="{log}"'] + _common(a)
	cmd += ["-RenderOffscreen"] if a.render else ["-NullRHI"]
	code, lines = _run(cmd, a.timeout, ECHO)
	passed, failed, problems = test_run_problems(lines, a.sound)
	# Warnings a test raised itself (AddWarning; captured log warnings end in "[log]") do not fail it but are requests, e.g.
	# Contracts.Packaging "generated but not in MapsToCook yet" (18.12).
	test_warnings = list(dict.fromkeys(l.split("Warning:", 1)[1].strip() for l in lines
		if "LogAutomationController: Warning:" in l and not l.rstrip().endswith("[log]")))
	for warning in test_warnings[:20]:
		print(f"[rbue] test warning: {warning}")
	for problem in problems:
		print(f"[rbue] test run problem: {problem}")
	print(f"[rbue] tests passed {passed}, failed {failed}, test warnings {len(test_warnings)}  log: {log}")
	if not problems:
		return 0
	return 1 if failed else (code or 3)


def cmd_game(a: argparse.Namespace) -> int:
	cmd = [str(EDITOR), str(UPROJECT)] + ([_unmsys(a.map)] if a.map else []) + ["-game", "-Windowed", "-ResX=1920", "-ResY=1080", "-log"]
	line = " ".join(_q(c) for c in cmd)
	print("[rbue] " + line)
	subprocess.Popen(line if os.name == "nt" else cmd, cwd=str(REPO))
	return 0


# --- perf (M2-A9) ---------------------------------------------------------------------------------------------------------------

# Targets of Docs/ue-architecture.md 18.10 M2-A9 (venue-dive-bar 11: High, 1440p, TSR at the DLSS-Q internal resolution).
PERF_TARGETS = {"gpu_mean": 10.3, "gpu_p95": 11.1, "game_max": 6.0, "frame_p95_floor": 1000.0 / 60.0}


def perf_verdict(report: dict) -> list[tuple[str, float, float, bool]]:
	"""(check, value, limit, ok) rows of a perf report against PERF_TARGETS."""
	rows = [
		("GPU mean [ms]", report["gpu_ms"]["mean"], PERF_TARGETS["gpu_mean"]),
		("GPU p95 [ms]", report["gpu_ms"]["p95"], PERF_TARGETS["gpu_p95"]),
		("game thread max [ms]", report["game_ms"]["max"], PERF_TARGETS["game_max"]),
		("frame p95 [ms] (floor)", report["frame_ms"]["p95"], PERF_TARGETS["frame_p95_floor"]),
	]
	return [(name, float(value if value is not None else float("nan")), limit, value is not None and value <= limit) for name, value, limit in rows]


def gpu_busy_percent(samples: int = 6, interval: float = 0.5) -> float | None:
	"""Mean GPU utilisation (nvidia-smi) over a few samples; None without nvidia-smi. Taken BEFORE the game starts: anything
	else on the GPU (another agent's capture, a live wallpaper, a screen recorder, an emulator) inflates the GPU times of the
	recording, so the report carries it and a busy GPU is flagged."""
	values = []
	for index in range(samples):
		try:
			result = subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"], capture_output=True,
				text=True, timeout=10)
			values.append(float(result.stdout.strip().splitlines()[0]))
		except (OSError, ValueError, IndexError, subprocess.SubprocessError):
			return None
		if index + 1 < samples:
			time.sleep(interval)
	return sum(values) / len(values)


# GPU utilisation before a recording above which the perf numbers are not comparable (logged as a warning in the report).
PERF_BUSY_GPU_PERCENT = 10.0


def _perf_json(report: dict) -> str:
	"""The report in the recorder's layout: one line per key, one line per sample series (diff-friendly, compact)."""
	def block(data: dict, indent: str) -> list[str]:
		keys = list(data)
		out = []
		for index, key in enumerate(keys):
			value = data[key]
			comma = "," if index + 1 < len(keys) else ""
			if isinstance(value, dict) and key == "samples":
				out += [f"{indent}{json.dumps(key)}: {{"] + block(value, indent + " ") + [f"{indent}}}{comma}"]
			else:
				out.append(f"{indent}{json.dumps(key)}: {json.dumps(value)}{comma}")
		return out
	return "\n".join(["{"] + block(report, " ") + ["}"]) + "\n"


def cmd_perf(a: argparse.Namespace) -> int:
	out = Path(a.out).resolve()
	out.parent.mkdir(parents=True, exist_ok=True)
	if out.exists():
		out.unlink()
	busy = gpu_busy_percent()
	if busy is not None and busy > PERF_BUSY_GPU_PERCENT:
		print(f"[rbue] WARNING: the GPU is {busy:.0f} % busy before the recording (other processes): the GPU times will be too high")
	log = _log_path("perf")
	resx, resy = a.res.lower().split("x")
	startup = [c for c in [f"rb.Quality {a.quality}" if a.quality else "", f"r.ScreenPercentage {a.screen_percentage}" if a.screen_percentage else "",
		a.exec_cmds] if c]
	cmd = _game_target(a) + _map_arg(a) + (["-game"] if not a.exe else []) + ["-RenderOffscreen", "-Windowed", f"-ResX={resx}", f"-ResY={resy}",
		"-ForceRes", f'-RBPerf="{out}"', f"-RBPerfFrames={a.frames}", f"-RBCaptureWarmup={a.warmup}", f"-RBCaptureWarmupSeconds={a.warmup_seconds}",
		f"-RBCaptureTimeout={max(60.0, a.timeout - 60):.0f}", f'-abslog="{log}"'] + _common(a)
	if a.camera:
		cmd.append(f'-RBCaptureCamera="{a.camera}"')
	if a.perf_exec:
		cmd.append(f'-RBPerfExec="{a.perf_exec}"')
	if startup:
		cmd.append(f'-ExecCmds="{", ".join(startup)}"')
	code, lines = _run(cmd, a.timeout, ECHO)
	if not out.exists():
		print(f"[rbue] perf FAILED: no report {out}  log: {log}")
		return code or 1
	# Sampled again once the game has exited: another process that started rendering DURING the recording (an agent's capture)
	# shows up here, not in the sample before (review).
	busy_after = gpu_busy_percent()
	if busy_after is not None and busy_after > PERF_BUSY_GPU_PERCENT:
		print(f"[rbue] WARNING: the GPU is {busy_after:.0f} % busy after the recording (other processes): the GPU times may be too high")
	report = json.loads(out.read_text(encoding="utf-8"))
	report["host"] = {"gpu_busy_before_percent": busy, "gpu_busy_after_percent": busy_after,
		"gpu_busy_warning": any(value is not None and value > PERF_BUSY_GPU_PERCENT for value in (busy, busy_after)),
		"exe": a.exe or "editor -game", "quality": a.quality, "date": _dt.datetime.now().isoformat(timespec="seconds")}
	out.write_text(_perf_json(report), encoding="utf-8")
	print(f"[rbue] perf {report['map']} {report['width']}x{report['height']} r.ScreenPercentage {report['screen_percentage']} "
		f"({report['frames']} frames, exec {report['exec']}, {report['adapter']}; GPU busy before / after: "
		f"{'n/a' if busy is None else f'{busy:.0f} %'} / {'n/a' if busy_after is None else f'{busy_after:.0f} %'})")
	for series in ("frame_ms", "game_ms", "render_ms", "rhi_ms", "gpu_ms"):
		s = report[series]
		print(f"[rbue]   {series:10s} mean {s['mean']:7.2f}  median {s['median']:7.2f}  p95 {s['p95']:7.2f}  p99 {s['p99']:7.2f}  max {s['max']:7.2f}")
	print(f"[rbue]   frames over 16.7 ms: {report['frames_over_16_7ms']}, over 33.3 ms: {report['frames_over_33_3ms']}")
	if report.get("scalability"):  # what was really rendered (rb.Quality sets the sg.* groups; --quality is only the request)
		print("[rbue]   scalability in effect: " + ", ".join(f"{k.removeprefix('sg.').removesuffix('Quality')} {v}" for k, v in report["scalability"].items()))
	verdict = perf_verdict(report)
	for name, value, limit, ok in verdict:
		print(f"[rbue]   {name:24s} {value:7.2f} <= {limit:5.2f}  {'ok' if ok else 'OVER'}")
	print(f"[rbue] perf report: {out}  log: {log}")
	if a.gate and not all(ok for *_, ok in verdict):
		return 1
	return 0


# --- package (M2-A10) -----------------------------------------------------------------------------------------------------------

def _main_checkout() -> Path:
	"""The main working tree (worktrees share its .git): packaged builds go next to it, never into an agent worktree."""
	try:
		common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=str(REPO), capture_output=True,
			text=True, check=True).stdout.strip()
		return Path(common).parent
	except (OSError, subprocess.CalledProcessError):
		return REPO


def playable_maps() -> list[str]:
	"""The generated levels the game opens by itself (RbAssetPaths.h: M1TestRoomMap, DiveBarMap, TitleMap, ...), read from the
	header so the list has one source."""
	header = (REPO / "Source/RawBreak/Public/Core/RbAssetPaths.h").read_text(encoding="utf-8")
	return re.findall(r'inline const TCHAR\* const \w+Map = TEXT\("(/Game/Generated/Maps/[^"]+)"\)', header)


def generated_maps() -> list[str]:
	"""Every generated level on disk (Content/Generated/Maps/**/*.umap) incl. the venues' sublevels. DirectoriesToAlwaysCook
	(/Game/Generated) cooks only .uasset files, never a .umap (UE 5.8 CookOnTheFlyServer), so each of them must be in MapsToCook
	(mirror of RbContractTests::GeneratedMapPackages; review)."""
	root = REPO / "Content"
	return sorted("/Game/" + path.relative_to(root).with_suffix("").as_posix() for path in (root / "Generated/Maps").rglob("*.umap"))


def _map_exists(game_path: str) -> bool:
	return game_path.startswith("/Game/") and (REPO / "Content" / (game_path[len("/Game/"):] + ".umap")).exists()


def title_map() -> str:
	"""RbAssetPaths::TitleMap (M2-D's title / venue select)."""
	header = (REPO / "Source/RawBreak/Public/Core/RbAssetPaths.h").read_text(encoding="utf-8")
	match = re.search(r'inline const TCHAR\* const TitleMap = TEXT\("([^"]+)"\)', header)
	return match.group(1) if match else ""


def cook_list_problems(game_ini: str | None = None, engine_ini: str | None = None, exists=_map_exists, maps: list[str] | None = None,
	title: str | None = None) -> list[str]:
	"""Contracts.Packaging on the host: every generated map (the playable ones and their sublevels, see generated_maps) is in
	MapsToCook, every listed map exists, and the build starts at an existing map - the title once it is generated (M2-A10: the
	owner plays title -> dive bar). Arguments for the selftest."""
	game_ini = game_ini if game_ini is not None else (REPO / "Config/DefaultGame.ini").read_text(encoding="utf-8")
	engine_ini = engine_ini if engine_ini is not None else (REPO / "Config/DefaultEngine.ini").read_text(encoding="utf-8")
	maps = maps if maps is not None else sorted(set(playable_maps()) | set(generated_maps()))
	title = title if title is not None else title_map()
	listed = re.findall(r'^\+MapsToCook=\(FilePath="?([^")]+)"?\)', game_ini, re.MULTILINE)
	problems = []
	for game_path in maps:
		if exists(game_path) and game_path not in listed:
			problems.append(f"{game_path} is generated but not in DefaultGame.ini MapsToCook (architect: add it at the merge)")
	for game_path in listed:
		if not exists(game_path):
			problems.append(f"{game_path} is in MapsToCook but does not exist (the cook would fail)")
	match = re.search(r"^GameDefaultMap=([^\r\n]+)$", engine_ini, re.MULTILINE)
	default_map = match.group(1).strip().split(".")[0] if match else ""
	if not default_map or not exists(default_map):
		problems.append(f"DefaultEngine.ini GameDefaultMap '{default_map}' does not exist (the packaged build would start in no level)")
	elif title and exists(title) and default_map != title:
		problems.append(f"{title} is generated but GameDefaultMap is still {default_map} (architect: switch it at the merge of M2-D)")
	return problems


def cmd_package(a: argparse.Namespace) -> int:
	problems = cook_list_problems()
	for line in problems:
		print(f"[rbue] package: {line}")
	if problems:
		print("[rbue] package FAILED: cook list")
		return 1
	root = Path(os.environ.get("RB_BUILDS_DIR", str(_main_checkout().parent / "RawBreak_Builds")))
	out_dir = Path(a.out_dir).resolve() if a.out_dir else root / a.label
	# Both targets are built first through rbue.py build (UBT with -WaitMutex): BuildCookRun's own -build step fails at once with
	# "ConflictingInstance" while any other UBT runs on the shared machine. The editor target too (review): the cook runs
	# UnrealEditor-Cmd with -nocompileeditor, and stale project editor modules (e.g. right after a merge) make the unattended cook
	# fail on "modules are missing or built with a different engine version" instead of cooking with the current code.
	for target in ("RawBreakEditor", "RawBreak"):
		code = cmd_build(argparse.Namespace(target=target, config="Development" if target == "RawBreakEditor" else a.config, clean=False,
			timeout=a.timeout))
		if code:
			print(f"[rbue] package FAILED: {target} build")
			return code
	log = _log_path("package")
	cmd = [str(RUN_UAT), "BuildCookRun", f"-project={UPROJECT}", "-noP4", "-platform=Win64", f"-clientconfig={a.config}", "-cook",
		"-stage", "-pak", "-archive", f"-archivedirectory={out_dir}", "-unattended", "-utf8output", "-nocompileeditor"]
	if a.clean:
		cmd.append("-clean")
	code, lines = _run(cmd, a.timeout, re.compile(r"(error|warning:|BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting|Cook|Stage|Archive)", re.IGNORECASE))
	log.write_text("\n".join(lines), encoding="utf-8")
	exe = out_dir / "Windows" / "RawBreak.exe"
	cook_errors = [l for l in lines if re.search(r"LogCook: Error|Error: .*Cook", l)]
	ok = code == 0 and exe.exists() and not cook_errors
	for line in cook_errors[:10]:
		print(f"[rbue] {line}")
	print(f"[rbue] package {'OK' if ok else 'FAILED'}: {exe if exe.exists() else out_dir}  log: {log}")
	return 0 if ok else (code or 1)


# --- licence ledger (Docs/licenses; venue-dive-bar 13.9, Docs/ue-architecture.md 18.9) ------------------------------------------

LEDGER = REPO / "Docs/licenses/asset-ledger.csv"
LEDGER_FRAGMENTS = REPO / "Docs/licenses/ledger"
LEDGER_COLUMNS = ["asset_id", "used_by", "source", "source_ref", "author", "licence", "licence_url", "date", "account", "sha256",
	"modified", "ai_generated", "steam_ai_disclosure", "trademark_check", "notes"]
LEDGER_SOURCES = {"polyhaven", "ambientcg", "meshy", "higgsfield", "own", "font", "fab", "stock-reference"}
LEDGER_LICENCES = {"CC0-1.0", "Meshy-paid-owned", "Higgsfield-owned", "OFL-1.1", "Apache-2.0", "Fab-Standard", "Unsplash/Pexels-reference-only", "own"}
# source -> the only licence it may carry (sources not listed accept any licence of LEDGER_LICENCES)
LEDGER_SOURCE_LICENCE = {"polyhaven": "CC0-1.0", "ambientcg": "CC0-1.0", "meshy": "Meshy-paid-owned", "higgsfield": "Higgsfield-owned",
	"stock-reference": "Unsplash/Pexels-reference-only", "own": "own"}
NON_COMMERCIAL = re.compile(r"(?i)(\bNC\b|-NC\b|\bND\b|-ND\b|non-?commercial|no-?deriv)")


def _read_ledger(path: Path) -> tuple[list[str], list[dict]]:
	with path.open(newline="", encoding="utf-8") as handle:
		reader = csv.DictReader(handle)
		return list(reader.fieldnames or []), [dict(row) for row in reader]


def ledger_errors(name: str, header: list[str], rows: list[dict], allow_ai: bool) -> list[str]:
	"""Validation of one ledger file (the merged ledger or a package fragment)."""
	errors = []
	missing = [c for c in LEDGER_COLUMNS if c not in header]
	if missing:
		errors.append(f"{name}: missing column(s) {missing} (columns are only ever added, never renamed)")
		return errors
	if header[:len(LEDGER_COLUMNS)] != LEDGER_COLUMNS:
		errors.append(f"{name}: the shared columns must come first in this order: {LEDGER_COLUMNS}")
	seen: set[tuple[str, str]] = set()
	for number, row in enumerate(rows, start=2):
		where = f"{name}:{number} ({row.get('asset_id', '')})"
		for column in ("asset_id", "used_by", "source", "licence", "date", "ai_generated"):
			if not (row.get(column) or "").strip():
				errors.append(f"{where}: empty {column}")
		key = (row.get("asset_id", ""), row.get("source", ""))
		if key in seen:
			errors.append(f"{where}: duplicate asset_id + source")
		seen.add(key)
		source, licence = row.get("source", ""), row.get("licence", "")
		if source not in LEDGER_SOURCES:
			errors.append(f"{where}: source '{source}' not allowed ({sorted(LEDGER_SOURCES)})")
		if NON_COMMERCIAL.search(licence) or NON_COMMERCIAL.search(row.get("licence_url", "")):
			errors.append(f"{where}: non-commercial / no-derivatives licence '{licence}' is never allowed")
		elif licence not in LEDGER_LICENCES:
			errors.append(f"{where}: licence '{licence}' not allowed ({sorted(LEDGER_LICENCES)})")
		elif source in LEDGER_SOURCE_LICENCE and licence != LEDGER_SOURCE_LICENCE[source]:
			errors.append(f"{where}: source {source} must carry licence {LEDGER_SOURCE_LICENCE[source]}, not {licence}")
		ai = (row.get("ai_generated") or "").strip().lower()
		if ai not in ("y", "n"):
			errors.append(f"{where}: ai_generated must be y or n")
		elif ai == "y" and not allow_ai:
			errors.append(f"{where}: ai_generated = y, but M2 allows no AI-generated asset (Meshy / Higgsfield come in DB-5)")
		if source in ("meshy", "higgsfield") and ai != "y":
			errors.append(f"{where}: {source} output must be ai_generated = y (Steam disclosure)")
		if ai == "y" and (row.get("steam_ai_disclosure") or "").strip().lower() != "y":
			errors.append(f"{where}: AI-generated assets need steam_ai_disclosure = y")
		if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", (row.get("date") or "").strip()):
			errors.append(f"{where}: date must be ISO YYYY-MM-DD")
		sha = (row.get("sha256") or "").strip()
		if sha and not re.fullmatch(r"[0-9a-f]{64}", sha):
			errors.append(f"{where}: sha256 must be 64 lower-case hex digits")
		if not re.match(r"(?i)^[yn]\b", (row.get("modified") or "n").strip() or "n"):
			errors.append(f"{where}: modified must start with y or n")
		if (row.get("trademark_check") or "").strip() not in ("", "n/a", "pending", "cleared"):
			errors.append(f"{where}: trademark_check must be n/a, pending or cleared (venue-dive-bar 13.9)")
	return errors


def cmd_ledger(a: argparse.Namespace) -> int:
	errors: list[str] = []
	header, merged = _read_ledger(LEDGER) if LEDGER.exists() else (list(LEDGER_COLUMNS), [])
	errors += ledger_errors(LEDGER.name, header, merged, a.allow_ai)
	fragments = sorted(LEDGER_FRAGMENTS.glob("*.csv")) if LEDGER_FRAGMENTS.exists() else []
	fragment_rows: list[dict] = []
	for path in fragments:
		f_header, rows = _read_ledger(path)
		errors += ledger_errors(f"ledger/{path.name}", f_header, rows, a.allow_ai)
		fragment_rows += rows
	key = lambda row: (row.get("asset_id", ""), row.get("source", ""))  # noqa: E731
	merged_by_key = {key(row): row for row in merged}
	pending, conflicts = [], []
	for row in fragment_rows:
		existing = merged_by_key.get(key(row))
		if existing is None:
			pending.append(row)
		elif any((existing.get(c) or "") != (row.get(c) or "") for c in LEDGER_COLUMNS):
			conflicts.append(f"{row.get('asset_id')} ({row.get('source')}): fragment row differs from the merged ledger")
	fragment_keys: dict[tuple[str, str], dict] = {}
	for row in fragment_rows:
		other = fragment_keys.get(key(row))
		if other is not None and any((other.get(c) or "") != (row.get(c) or "") for c in LEDGER_COLUMNS):
			conflicts.append(f"{row.get('asset_id')} ({row.get('source')}): two fragments disagree")
		fragment_keys[key(row)] = row
	errors += conflicts
	print(f"[rbue] ledger: {len(merged)} merged row(s), {len(fragments)} fragment(s) with {len(fragment_rows)} row(s), {len(pending)} not merged yet")
	if a.merge and not errors:
		extra = [c for c in header if c not in LEDGER_COLUMNS]
		rows = merged + [dict(r) for r in {key(r): r for r in pending}.values()]
		rows.sort(key=lambda r: (r.get("used_by", ""), r.get("source", ""), r.get("asset_id", "")))
		with LEDGER.open("w", newline="", encoding="utf-8") as handle:
			writer = csv.DictWriter(handle, fieldnames=LEDGER_COLUMNS + extra, extrasaction="ignore", lineterminator="\n")
			writer.writeheader()
			writer.writerows(rows)
		print(f"[rbue] ledger: merged {len(pending)} row(s) -> {LEDGER}")
		pending = []
	for line in errors:
		print(f"[rbue] ledger ERROR {line}")
	if a.check and pending:
		print(f"[rbue] ledger ERROR {len(pending)} fragment row(s) not in {LEDGER.name} (run rbue.py ledger --merge)")
		return 1
	return 1 if errors else 0


# --- file ownership of the M2 packages (Docs/ue-architecture.md 18.2; the architect's merge check, 18.12) ------------------------
#
# Every M2 package edits only the files it owns; everything else is a request in its report. `rbue.py owners` checks a package
# branch (or the working tree) against this table before the architect merges it. A path belongs to the package whose pattern
# matches it most specifically: an exact path beats any glob, otherwise the longer literal prefix (the text before the first
# wildcard) wins, so "Game/RbTestRoom.*" (M2-L) beats "Game/**" (M2-E). Patterns: '**' any depth, '*' / '?' inside one path
# segment, '{a,b}' alternatives. Blocks: parts of shared files owned by another package than the file (18.2 exceptions).

_SRC = "Source/RawBreak/{Public,Private}/"
_UNIT = "Source/RawBreak/Private/Tests/"
_PIE = "Source/RawBreakEditor/Private/Tests/"
_ED = "Tools/unreal/editor/"


def _tests(unit: list[str], pie: list[str]) -> list[str]:
	return [f"{_UNIT}{name}.cpp" for name in unit] + [f"{_PIE}{name}.cpp" for name in pie]


def _package_common(key: str, package: str) -> list[str]:
	"""What every package owns: its dev map script, dev screenshots and ledger fragment."""
	return [f"{_ED}rb_dev_{key}.py", f"Docs/images/dev/{key}/**", f"Docs/licenses/ledger/{package}.csv"]


OWNERS: dict[str, list[str]] = {
	"M2-F": [f"{_SRC}{d}/**" for d in ("Input", "Player", "Camera", "Cue")] + [
		f"{_SRC}Math/RbStrokeMath.*", "Source/RawBreak/Private/Math/RbCameraMath_Camera.cpp",
		"Source/RawBreakEditor/Private/RbAssetBake_Player.cpp", "Source/RawBreakEditor/Private/RbAssetBake_Cue.cpp",
		f"{_ED}rb_make_player.py", f"{_ED}rb_bake_cue.py", "Tools/feel/**", "Config/DefaultInput.ini",
		"Content/Generated/Player/**", "Content/Generated/Cues/**"]
		+ _tests(["RbStrokeTests", "RbStrokeMathTests", "RbRawInputTests", "RbCameraRigTests", "RbCameraMathTests", "RbCueTests", "RbFeelTests",
			"RbHumanMotionTests", "RbBallInHandTests"], ["RbFeelFlowTest"]) + _package_common("m2f", "M2-F"),
	"M2-L": [f"{_SRC}Table/**", "Source/RawBreakEditor/Private/RbAssetBake_Table.cpp", "Shaders/Private/*.ush",
		"Source/RawBreak/Private/Math/RbCameraMath_Render.cpp", f"{_SRC}Game/RbTestRoom.*", f"{_SRC}Dev/RbLookDevCamera.*",
		f"{_ED}rb_make_materials.py", f"{_ED}rb_bake_table.py", f"{_ED}rb_import_table.py", f"{_ED}rb_make_test_room.py", f"{_ED}rb_m1_layout.py",
		"Tools/unreal/capture_m1.py", "Tools/unreal/capture_table.py", "Tools/blender/table/**", "Art/Tables/**",
		"Content/Generated/Tables/**", "Content/Generated/Materials/**", "Content/Generated/Maps/L_M1_TestRoom*",
		"Docs/images/m1/**", "Docs/references/table-lookdev.md"]
		+ _tests(["RbTableTests", "RbRenderMathTests", "RbRoomTests", "RbTableLookTests"], []) + _package_common("m2l", "M2-L"),
	"M2-A": [f"{_SRC}Venue/**", "Tools/blender/rbbl.py", "Tools/blender/common/**",
		"Tools/blender/divebar/{db_axis_test,db_arch,db_neon,cue_sweep_check}.py",
		"Art/DiveBar/{layout,lights,calibration}.json", "Art/DiveBar/neon/**", "Art/DiveBar/Export/{AxisTest,Arch,Neon}/**",
		f"{_ED}rb_import_divebar.py", f"{_ED}rb_make_divebar.py", f"{_ED}rb_make_divebar_fx.py", "Tools/unreal/capture_divebar.py",
		"Content/Generated/Maps/L_DiveBar*", "Content/Generated/Venues/DiveBar/{Arch,Lighting,FX}/**", "Docs/images/divebar/**",
		f"{_PIE}RbDiveBar*.cpp"]
		+ _tests(["RbVenueTests"], []) + _package_common("m2a", "M2-A"),
	"M2-B": ["Tools/blender/divebar/{db_bar,db_backbar,db_booth,db_stool,db_ledges,db_lamp,db_cue_rack,db_jukebox,db_dart,"
		"db_lathe_props,db_props_common,db_decals,db_signs}.py", "Art/DiveBar/Export/**", "Art/DiveBar/Textures/**", "Art/DiveBar/cc0_inputs*",
		"Tools/art/**", "Art/Fonts/**", f"{_ED}rb_make_divebar_materials.py", "Shaders/Private/Venue/*.ush",
		"Content/Generated/Venues/DiveBar/{Props,Materials,Textures,Decals}/**"] + _package_common("m2b", "M2-B"),
	"M2-C": ["Source/RawBreakAudioDsp/**", f"{_SRC}Audio/**", "Tools/audio/**", f"{_ED}rb_make_audio.py", "Content/Generated/Audio/**",
		"Docs/audio/m2/**", f"{_UNIT}RbAudio*.cpp", f"{_PIE}RbAudio*.cpp"] + _package_common("m2c", "M2-C"),
	"M2-D": [f"{_SRC}UI/**", f"{_SRC}Settings/**", "Config/DefaultScalability.ini", f"{_ED}rb_make_title.py",
		"Content/Generated/Maps/L_Title*", "Content/Generated/UI/**"]
		+ _tests(["RbSettingsTests", "RbOverlayTests", "RbUiTests"], ["RbMenuFlowTest"]) + _package_common("m2d", "M2-D"),
	"M2-E": [f"{_SRC}{d}/**" for d in ("Balls", "Game", "Interaction", "Replay", "Simulation")] + [
		f"{_SRC}Dev/RbCheatManager.*", "Source/RawBreakEditor/Private/RbAssetBake_Ball.cpp", f"{_ED}rb_bake_ball.py", f"{_ED}rb_make_physics.py",
		"Content/Generated/Physics/**", "Content/Generated/Balls/**"]
		+ _tests(["RbBallTests", "RbPlaybackTests", "RbMatchTests", "RbSimulationTests", "RbLooseBallTests", "RbMultiTableTests"],
			["RbMatchFlowTest", "RbReplayTest", "RbM1FlowTest", "RbM1RackTest", "RbLooseBallFunctionalTest", "RbMultiTableFunctionalTest"])
		+ _package_common("m2e", "M2-E"),
	"M2-0": ["RawBreak.uproject", "Source/*.Target.cs", "Source/RawBreak/RawBreak.Build.cs", "Source/RawBreakEditor/RawBreakEditor.Build.cs",
		"Source/RawBreakAudioDsp/RawBreakAudioDsp.Build.cs", "Source/RawBreakShaders/RawBreakShaders.Build.cs",
		"Source/BilliardsCore/BilliardsCore.Build.cs", "Config/DefaultEngine.ini", "Config/DefaultGame.ini", f"{_SRC}Core/**",
		"Source/RawBreak/Public/RawBreak.h", "Source/RawBreak/Private/RawBreakModule.cpp", f"{_SRC}Dev/RbHeadlessCaptureSubsystem.*",
		f"{_UNIT}RbTestFlags.h", f"{_UNIT}RbCoordsTests.cpp", f"{_UNIT}RbContractTests.cpp", f"{_PIE}RbPieSmokeTest.cpp",
		"Source/RawBreakEditor/**", "Tools/unreal/rbue.py", f"{_ED}rb_common.py", f"{_ED}rb_make_all.py", f"{_ED}rb_pipeline_proof.py",
		f"{_ED}rb_bake_selftest.py", "Tools/blender/divebar/db_build_all.py", "Docs/licenses/asset-ledger.csv", ".gitignore", "Docs/perf/**",
		"Docs/images/dev/m20/**", "Docs/ue-architecture.md"],
}

# Shared files with a block owned by another package: (file, block owner, regex of the block; MULTILINE | DOTALL).
OWNER_BLOCKS: list[tuple[str, str, str]] = [
	("Config/DefaultEngine.ini", "M2-C", r"^; --- audio block.*?^; --- end of the audio block[^\n]*$"),
	("Source/RawBreak/Public/Core/RbTypes.h", "M2-L", r"(?:^//[^\n]*\n)*^UENUM\([^)]*\)\s*\n^enum class ERbTablePart\b.*?^\};"),
	("Source/RawBreak/Private/Core/RbTypes.cpp", "M2-L", r"^\tconst TCHAR\* ToString\(ERbTablePart Part\)\n\t\{.*?^\t\}$"),
]


def _expand_braces(pattern: str) -> list[str]:
	match = re.search(r"\{([^{}]*)\}", pattern)
	if not match:
		return [pattern]
	out = []
	for option in match.group(1).split(","):
		out += _expand_braces(pattern[:match.start()] + option + pattern[match.end():])
	return out


def _glob_regex(pattern: str) -> re.Pattern:
	out, i = "", 0
	while i < len(pattern):
		if pattern.startswith("**", i):
			out += ".*"
			i += 2
		elif pattern[i] == "*":
			out += "[^/]*"
			i += 1
		elif pattern[i] == "?":
			out += "[^/]"
			i += 1
		else:
			out += re.escape(pattern[i])
			i += 1
	return re.compile(out + r"\Z")


def _specificity(pattern: str) -> int:
	wild = re.search(r"[*?]", pattern)
	return 100000 + len(pattern) if not wild else wild.start()


_OWNER_RULES = [(package, expanded, _glob_regex(expanded), _specificity(expanded))
	for package, patterns in OWNERS.items() for pattern in patterns for expanded in _expand_braces(pattern)]


def owner_of(path: str) -> tuple[str, str]:
	"""(package, matching pattern) of a repo-relative path; ('', '') when nobody owns it, ('?', patterns) when two packages tie."""
	path = path.replace("\\", "/")
	best: list[tuple[int, str, str]] = []
	for package, pattern, regex, score in _OWNER_RULES:
		if regex.match(path):
			if not best or score > best[0][0]:
				best = [(score, package, pattern)]
			elif score == best[0][0] and package != best[0][1]:
				best.append((score, package, pattern))
	if not best:
		return "", ""
	if len(best) > 1:
		return "?", " / ".join(f"{p} {pat}" for _, p, pat in best)
	return best[0][1], best[0][2]


def _split_blocks(path: str, text: str | None) -> tuple[str | None, dict[str, str]]:
	"""The text of a shared file with its foreign blocks cut out, and each block's text by owner."""
	if text is None:
		return None, {}
	blocks: dict[str, str] = {}
	for file, owner, regex in OWNER_BLOCKS:
		if file != path:
			continue
		match = re.search(regex, text, re.MULTILINE | re.DOTALL)
		if match:
			blocks[owner] = blocks.get(owner, "") + match.group(0)
			text = text[:match.start()] + f"<<block {owner}>>" + text[match.end():]
	return text, blocks


def touched_owners(path: str, before: str | None, after: str | None) -> set[str]:
	"""The owners whose part of path changed between two versions (None = the file does not exist)."""
	owner = owner_of(path)[0] or "(unowned)"
	if not any(file == path for file, _, _ in OWNER_BLOCKS) or before is None or after is None:
		return {owner}
	rest_before, blocks_before = _split_blocks(path, before)
	rest_after, blocks_after = _split_blocks(path, after)
	touched = set() if rest_before == rest_after else {owner}
	for block_owner in set(blocks_before) | set(blocks_after):
		if blocks_before.get(block_owner) != blocks_after.get(block_owner):
			touched.add(block_owner)
	return touched or {owner}


def generator_list(path: Path) -> list[str]:
	"""The scripts of a GENERATORS list ('(script, owner),' rows) of rb_make_all.py / db_build_all.py, read as text: both run inside
	Unreal / Blender and cannot be imported here."""
	return re.findall(r'^\t\("([^"]+\.py)", "[^"]+"\),', path.read_text(encoding="utf-8"), re.MULTILINE)


def _git(*args: str) -> str:
	return subprocess.run(["git", *args], cwd=str(REPO), capture_output=True, text=True, encoding="utf-8", errors="replace", check=True).stdout


def _git_text(rev: str, path: str) -> str | None:
	result = subprocess.run(["git", "show", f"{rev}:{path}"], cwd=str(REPO), capture_output=True, text=True, encoding="utf-8", errors="replace")
	return result.stdout if result.returncode == 0 else None


def cmd_owners(a: argparse.Namespace) -> int:
	if a.who:
		for path in a.who:
			package, pattern = owner_of(Path(path).resolve().relative_to(REPO).as_posix() if Path(path).is_absolute() else path)
			print(f"[rbue] {path}: {package or 'nobody (architect decides)'}  {pattern}")
		return 0
	if a.package not in OWNERS:
		print(f"[rbue] owners: --package must be one of {sorted(OWNERS)}")
		return 2
	head = a.branch or ""
	base = _git("merge-base", a.base, head or "HEAD").strip()
	if head:
		paths = [p for p in _git("diff", "--name-only", "--no-renames", base, head).splitlines() if p]
	else:  # the working tree (committed + uncommitted + untracked) against the base
		paths = sorted(set(p for p in _git("diff", "--name-only", "--no-renames", base).splitlines() if p)
			| set(p for p in _git("ls-files", "--others", "--exclude-standard").splitlines() if p))
	violations: list[tuple[str, str]] = []
	block_files = {file for file, _, _ in OWNER_BLOCKS}
	for path in paths:
		before = after = None
		if path in block_files:  # only shared files need their content (the block check); everything else by path
			before = _git_text(base, path)
			if head:
				after = _git_text(head, path)
			else:
				file = REPO / path
				after = file.read_text(encoding="utf-8", errors="replace") if file.exists() else None
		for owner in sorted(touched_owners(path, before, after)):
			if owner != a.package:
				violations.append((path, owner))
	print(f"[rbue] owners: {a.package} {head or 'working tree'} vs {a.base} (merge base {base[:10]}): {len(paths)} file(s) changed")
	for path, owner in violations:
		what = "nobody owns it (architect decides)" if owner == "(unowned)" else f"owned by {owner}"
		print(f"[rbue]   NOT OWNED {path}: {what}")
	print(f"[rbue] owners {'OK' if not violations else f'FAILED: {len(violations)} file(s) outside {a.package} (requests)'}")
	return 1 if violations else 0


# --- core tests (CMake) ---------------------------------------------------------------------------------------------------------

def core_filters(filters: list[str], slow: bool) -> list[str]:
	"""Arguments of the core test binary: the given filters (Tests/Core/TestMain.cpp: every include must match, '-' excludes) plus
	'-_Slow_' unless --slow, also when filters are given (review: `core -- MOT_` must not start the opt-in slow MOT_ tests)."""
	return list(filters) + ([] if slow or "-_Slow_" in filters else ["-_Slow_"])


def cmd_core(a: argparse.Namespace) -> int:
	build = (REPO / a.build_dir).resolve()
	cmake = shutil.which("cmake") or "cmake"
	ctest = shutil.which("ctest") or "ctest"
	if not (build / "CMakeCache.txt").exists():
		code, _ = _run([cmake, "-S", str(REPO), "-B", str(build), "-G", "Visual Studio 17 2022", "-A", "x64"], a.timeout)
		if code:
			return code
	code, lines = _run([cmake, "--build", str(build), "--config", a.config, "--parallel"], a.timeout,
		re.compile(r"(error|warning C|Build succeeded|FAILED)", re.IGNORECASE))
	if code:
		return code
	log = _log_path("core")
	exe = build / "Tests" / "Core" / a.config / "BilliardsCoreTests.exe"
	if exe.exists():
		# The test binary directly (Tests/Core/TestMain.cpp): its filters, every failing test and check, and its own count. The
		# default run excludes the opt-in `_Slow_` tests (PERF / CPU-time gates / statistical sets, Docs/architecture.md 18: nightly
		# in Release on an idle machine); --slow runs them too.
		code, lines = _run([str(exe)] + core_filters(a.filters, a.slow), a.timeout, re.compile(r"(\[FAIL\]|FAILED|test\(s\) run)"))
	else:
		code, lines = _run([ctest, "--test-dir", str(build), "-C", a.config, "-V"], a.timeout,
			re.compile(r"(tests passed|tests failed|\[FAIL\]|FAILED|test\(s\) run)"))
	log.write_text("\n".join(lines), encoding="utf-8")
	summary = [l for l in lines if "test(s) run" in l]
	failed = [l.strip() for l in lines if l.startswith("[FAIL]")]
	print(f"[rbue] core: {summary[-1].strip() if summary else 'no test summary'}{f' - failed: {failed}' if failed else ''}  log: {log}")
	return code


# --- selftest ---------------------------------------------------------------------------------------------------------------------

def cmd_selftest(a: argparse.Namespace) -> int:
	"""Pure checks of this script's helpers (capture output names, perf verdict, ledger validation incl. the negative cases,
	package ownership incl. the shared-file blocks). Runs in a second, needs git only for the no-tie check over tracked files."""
	failures: list[str] = []

	def check(name: str, got, want) -> None:
		if got != want:
			failures.append(f"{name}: got {got!r}, want {want!r}")

	# Capture outputs (mirror of URbHeadlessCaptureSubsystem, RawBreak.Unit.Pipeline.*).
	check("split", _split_list(' RbCam_DB_V01, RbCam_DB_V02 ,,RbCam_DB_TH1;"RbCam_Menu_S0" '), ["RbCam_DB_V01", "RbCam_DB_V02", "RbCam_DB_TH1", "RbCam_Menu_S0"])
	check("output token", resolve_output("C:/x/db2/{camera}.png", "RbCam_DB_V01", 6), "C:/x/db2/RbCam_DB_V01.png")
	check("output single", resolve_output("C:/x/shot.png", "RbCam_Overhead", 1), "C:/x/shot.png")
	check("output several", resolve_output("C:/x/shot.png", "RbCam_Overhead", 2), str(Path("C:/x/shot_RbCam_Overhead.png")))
	check("output unsafe", resolve_output("C:/x/{camera}.png", "a:b c/d", 2), "C:/x/a_b_c_d.png")

	# Perf verdict (M2-A9 targets).
	def report(gpu_mean: float, gpu_p95: float, game_max: float, frame_p95: float) -> dict:
		return {"gpu_ms": {"mean": gpu_mean, "p95": gpu_p95}, "game_ms": {"max": game_max}, "frame_ms": {"p95": frame_p95}}
	check("perf ok", [ok for *_, ok in perf_verdict(report(9.8, 10.9, 4.0, 12.0))], [True, True, True, True])
	check("perf over", [ok for *_, ok in perf_verdict(report(23.7, 24.9, 6.5, 30.2))], [False, False, False, False])
	check("perf missing gpu", perf_verdict(report(None, 1.0, 1.0, 1.0))[0][3], False)  # type: ignore[arg-type]
	sample_report = {"map": "/Game/X", "frames": 2, "gpu_ms": {"mean": 1.5, "p95": 2.0}, "samples": {"gpu_ms": [1.0, 2.0], "game_ms": [0.5, 0.25]},
		"host": {"gpu_busy_before_percent": None, "gpu_busy_warning": False}}
	check("perf json round trip", json.loads(_perf_json(sample_report)), sample_report)

	# Playable maps (the cook-list preflight of `package`) come from RbAssetPaths.h.
	check("playable maps", sorted(playable_maps()), ["/Game/Generated/Maps/L_DiveBar", "/Game/Generated/Maps/L_M1_TestRoom", "/Game/Generated/Maps/L_Title"])
	check("title map", title_map(), "/Game/Generated/Maps/L_Title")
	room, bar, title = "/Game/Generated/Maps/L_M1_TestRoom", "/Game/Generated/Maps/L_DiveBar", "/Game/Generated/Maps/L_Title"
	def cook(listed: list[str], generated: set[str], default_map: str) -> list[str]:
		game = "\n".join(f'+MapsToCook=(FilePath="{m}")' for m in listed) + "\n"
		engine = f"[/Script/EngineSettings.GameMapsSettings]\nGameDefaultMap={default_map}.{default_map.rsplit('/', 1)[-1]}\n"
		return cook_list_problems(game, engine, lambda m: m in generated, [room, bar, title], title)
	check("cook: M1 state", cook([room], {room}, room), [])
	check("cook: after the M2 merges", len(cook([room, bar, title], {room, bar, title}, title)), 0)
	check("cook: dive bar generated, not listed", len(cook([room], {room, bar}, room)), 1)
	check("cook: listed, not generated", len(cook([room, bar], {room}, room)), 1)
	check("cook: title generated and listed, default map not switched", len(cook([room, title], {room, title}, room)), 1)
	check("cook: default map missing", len(cook([room], {room}, title)), 1)
	sub = "/Game/Generated/Maps/L_DiveBar_Light_Open"
	def cook_sub(listed: list[str], generated: set[str]) -> list[str]:
		game = "\n".join(f'+MapsToCook=(FilePath="{m}")' for m in listed) + "\n"
		engine = f"GameDefaultMap={room}.L_M1_TestRoom\n"
		return cook_list_problems(game, engine, lambda m: m in generated, [room, bar, title, sub], title)
	check("cook: sublevel generated, not listed", len(cook_sub([room, bar], {room, bar, sub})), 1)
	check("cook: sublevel listed", cook_sub([room, bar, sub], {room, bar, sub}), [])
	check("generated maps on disk include the test room", room in generated_maps(), True)

	# Core test filters: the opt-in _Slow_ tests stay out unless --slow, with or without filters.
	check("core filters default", core_filters([], False), ["-_Slow_"])
	check("core filters with a filter", core_filters(["MOT_"], False), ["MOT_", "-_Slow_"])
	check("core filters --slow", core_filters(["MOT_"], True), ["MOT_"])
	check("core filters explicit exclude", core_filters(["-_Slow_"], False), ["-_Slow_"])

	# Build summary: compiler warnings are counted (M2-A1: 0 from project code).
	check("compiler warnings", compiler_warnings([
		r"C:\x\Source\RawBreak\Private\A.cpp(12): warning C4456: declaration of 'X' hides previous local declaration",
		r"C:\x\Source\RawBreak\Private\A.cpp(12): warning C4456: declaration of 'X' hides previous local declaration",
		"Result: Succeeded", "0 warning(s)", "LogInit: Warning: not a compiler line"]),
		[r"C:\x\Source\RawBreak\Private\A.cpp(12): warning C4456: declaration of 'X' hides previous local declaration"])

	# The process runner: a normal exit, and a child that hangs WITHOUT printing is killed at the timeout (review: the old per-line
	# check never fired for a silent hang).
	check("run exit code and output", _run([sys.executable, "-c", "print('rb'); raise SystemExit(3)"], 60.0), (3, ["rb"]))
	started = time.monotonic()
	code, _ = _run([sys.executable, "-c", "import time; time.sleep(120)"], 2.0)
	check("run kills a silent hang at the timeout", (code, time.monotonic() - started < 60.0), (124, True))

	# Automation run verdict (cmd_test): every found test completed, the automation shut down normally, nothing failed.
	def run_log(found: int | None, results: list[tuple[str, str]], exit_code: int | None) -> list[str]:
		out = [] if found is None else [f"LogAutomationCommandLine: Display: Found {found} automation tests based on 'RawBreak.'"]
		out += [f"LogAutomationController: Display: Test Completed. Result={{{r}}} Name={{{p.rsplit('.', 1)[-1]}}} Path={{{p}}}" for r, p in results]
		return out + ([] if exit_code is None else [f"LogAutomationCommandLine: Display: **** TEST COMPLETE. EXIT CODE: {exit_code} ****"])
	two_green = [("Success", "RawBreak.Unit.A.X"), ("Success", "RawBreak.Functional.LevelSmoke.L_M1_TestRoom")]
	check("test run green", test_run_problems(run_log(2, two_green, 0)), (2, 0, []))
	check("test run failed test", test_run_problems(run_log(2, [two_green[0], ("Fail", "RawBreak.Unit.B.Y")], -1))[:2], (1, 1))
	check("test run crash after one test (no TEST COMPLETE)", len(test_run_problems(run_log(2, two_green[:1], None))[2]), 2)
	check("test run did not start", len(test_run_problems(run_log(None, [], None))[2]), 2)
	check("test run matched nothing", len(test_run_problems(run_log(0, [], 0))[2]), 1)
	check("test run critical error", len(test_run_problems(run_log(2, two_green, -1))[2]), 1)
	audio_fail = run_log(1, [("Fail", "RawBreak.Functional.Audio.AU0_Timing")], -1)
	check("test run audio hint without --sound", any("--sound" in p for p in test_run_problems(audio_fail, False)[2]), True)
	check("test run no audio hint with --sound", any("--sound" in p for p in test_run_problems(audio_fail, True)[2]), False)
	check("--sound mutes by default", _common(argparse.Namespace(sound=True, audible=False, extra=[]))[-1], "-MuteAudio")
	check("--sound --audible", "-MuteAudio" in _common(argparse.Namespace(sound=True, audible=True, extra=[])), False)
	check("--sound with an explicit -muteaudio", _common(argparse.Namespace(sound=True, audible=False, extra=["-muteaudio"])).count("-MuteAudio"), 0)
	check("no --sound keeps -NoSound", "-NoSound" in _common(argparse.Namespace(sound=False, extra=[])), True)

	# Ledger validation: a clean CC0 row, then one broken field per case.
	good = {"asset_id": "rosewood_veneer1", "used_by": "M2-L", "source": "polyhaven", "source_ref": "https://polyhaven.com/a/rosewood_veneer1",
		"author": "Rob Tuytel", "licence": "CC0-1.0", "licence_url": "https://polyhaven.com/license", "date": "2026-09-29", "account": "",
		"sha256": "0" * 64, "modified": "n", "ai_generated": "n", "steam_ai_disclosure": "n", "trademark_check": "n/a", "notes": "2k png"}
	check("ledger good row", ledger_errors("t", LEDGER_COLUMNS, [good], False), [])
	negative = {
		"non-commercial": {"licence": "CC-BY-NC-4.0"},
		"licence not allowed": {"licence": "CC-BY-4.0"},
		"polyhaven not CC0": {"source": "polyhaven", "licence": "OFL-1.1"},
		"source not allowed": {"source": "sketchfab"},
		"ai in M2": {"ai_generated": "y", "steam_ai_disclosure": "y"},
		"meshy without ai flag": {"source": "meshy", "licence": "Meshy-paid-owned"},
		"bad date": {"date": "29.09.2026"},
		"bad sha": {"sha256": "XYZ"},
		"empty used_by": {"used_by": ""},
		"empty date": {"date": ""},
		"trademark check": {"trademark_check": "ok"},
	}
	for name, change in negative.items():
		if not ledger_errors("t", LEDGER_COLUMNS, [{**good, **change}], False):
			failures.append(f"ledger negative case '{name}' was accepted")
	if not ledger_errors("t", LEDGER_COLUMNS, [good, dict(good)], False):
		failures.append("ledger duplicate asset_id + source was accepted")
	if not ledger_errors("t", LEDGER_COLUMNS[:-1], [good], False):
		failures.append("ledger with a missing column was accepted")
	check("ledger ai allowed from DB-5", ledger_errors("t", LEDGER_COLUMNS, [{**good, "source": "meshy", "licence": "Meshy-paid-owned",
		"ai_generated": "y", "steam_ai_disclosure": "y"}], True), [])

	# Ownership (18.2).
	expected_owner = {
		"Source/RawBreak/Private/Input/RbAimResponse.cpp": "M2-F", "Source/RawBreak/Private/Math/RbStrokeMath.cpp": "M2-F",
		"Config/DefaultInput.ini": "M2-F", "Source/RawBreak/Private/Tests/RbFeelTests.cpp": "M2-F",
		"Source/RawBreak/Public/Game/RbTestRoom.h": "M2-L", "Source/RawBreak/Private/Dev/RbLookDevCamera.cpp": "M2-L",
		"Source/RawBreakEditor/Private/RbAssetBake_Table.cpp": "M2-L", "Shaders/Private/RbBall.ush": "M2-L",
		"Content/Generated/Maps/L_M1_TestRoom.umap": "M2-L", "Art/Tables/tablespec_seven_foot_bar.json": "M2-L",
		"Source/RawBreak/Private/Venue/RbVenueInfo.cpp": "M2-A", "Tools/blender/divebar/db_arch.py": "M2-A",
		"Art/DiveBar/Export/AxisTest/SM_DB_AxisTest.fbx": "M2-A", "Content/Generated/Maps/L_DiveBar_Lighting.umap": "M2-A",
		"Content/Generated/Venues/DiveBar/Arch/SM_DB_Wall.uasset": "M2-A", "Source/RawBreakEditor/Private/Tests/RbDiveBarRackTest.cpp": "M2-A",
		"Tools/blender/divebar/db_stool.py": "M2-B", "Tools/blender/divebar/db_signs.py": "M2-B", "Art/DiveBar/Export/Stool_A/Stool_A.json": "M2-B", "Shaders/Private/Venue/RbVenueWear.ush": "M2-B",
		"Content/Generated/Venues/DiveBar/Props/SM_DB_Stool_A.uasset": "M2-B", "Docs/licenses/ledger/M2-B.csv": "M2-B", "Tools/art/fetch_cc0.py": "M2-B",
		"Source/RawBreakAudioDsp/Private/RbAudio/RbImpactSynth.cpp": "M2-C", "Source/RawBreak/Private/Tests/RbAudioDspTests.cpp": "M2-C",
		"Source/RawBreak/Public/UI/Core/RbUiSubsystem.h": "M2-D", "Content/Generated/Maps/L_Title.umap": "M2-D", "Config/DefaultScalability.ini": "M2-D",
		"Source/RawBreak/Private/Game/RbGameMode.cpp": "M2-E", "Source/RawBreak/Private/Dev/RbCheatManager.cpp": "M2-E",
		"Source/RawBreakEditor/Private/Tests/RbM1RackTest.cpp": "M2-E", "Tools/unreal/editor/rb_dev_m2e.py": "M2-E",
		"Source/RawBreak/Public/Core/RbAssetPaths.h": "M2-0", "Source/RawBreakAudioDsp/RawBreakAudioDsp.Build.cs": "M2-0",
		"Source/RawBreakEditor/Private/RbAssetBake_Common.cpp": "M2-0", "Source/RawBreak/Private/Dev/RbHeadlessCaptureSubsystem.cpp": "M2-0",
		"Tools/blender/divebar/db_build_all.py": "M2-0", "Source/RawBreakEditor/Private/Tests/RbPieSmokeTest.cpp": "M2-0",
		"Source/BilliardsCore/Private/rb/Physics/Solver.cpp": "", "Source/RawBreak/Public/Math/RbCameraMath.h": "", "Docs/decisions.md": "",
	}
	for path, want in expected_owner.items():
		check(f"owner {path}", owner_of(path)[0], want)

	# Every content generator a package owns is in the architect's regeneration lists, and every listed one has an owner (M2-A2:
	# a clean Content/Generated is rebuilt by `rbbl.py all` + rb_make_all.py; a generator missing there silently drops its assets
	# - review: M2-A's rb_make_divebar_fx.py was owned but not run, so the dust motes / light function vanished on regeneration).
	owned = {pattern for package, pattern, _, _ in _OWNER_RULES if package != "M2-0"}
	ue_listed = generator_list(REPO / f"{_ED}rb_make_all.py")
	ue_owned = {p[len(_ED):] for p in owned if re.fullmatch(r"Tools/unreal/editor/rb_(make|bake|import)_\w+\.py", p)}
	check("rb_make_all.py runs every owned UE generator", sorted(ue_owned - set(ue_listed)), [])
	check("rb_make_all.py generators have owners", [s for s in ue_listed if not owner_of(_ED + s)[0]], [])
	bl_listed = generator_list(REPO / "Tools/blender/divebar/db_build_all.py")
	bl_owned = {p[len("Tools/blender/"):] for p in owned if re.fullmatch(r"Tools/blender/divebar/db_\w+\.py", p)} - {"divebar/db_props_common.py"}
	check("db_build_all.py runs every owned Blender generator", sorted(bl_owned - set(bl_listed)), [])
	check("db_build_all.py generators have owners", [s for s in bl_listed if not owner_of("Tools/blender/" + s)[0]], [])
	try:
		ties = [f"{p}: {owner_of(p)[1]}" for p in _git("ls-files").splitlines() if owner_of(p)[0] == "?"]
		check("tracked files with two equally specific owners", ties, [])
	except (OSError, subprocess.CalledProcessError) as error:
		failures.append(f"git ls-files: {error}")

	# Shared-file blocks: an edit inside the block belongs to the block's owner only.
	engine = (REPO / "Config/DefaultEngine.ini").read_text(encoding="utf-8")
	check("engine ini: audio block edit", touched_owners("Config/DefaultEngine.ini", engine, engine.replace("AudioMaxChannels=96", "AudioMaxChannels=128")), {"M2-C"})
	check("engine ini: renderer edit", touched_owners("Config/DefaultEngine.ini", engine, engine.replace("r.VirtualTextures=True", "r.VirtualTextures=False")), {"M2-0"})
	check("engine ini: both", touched_owners("Config/DefaultEngine.ini", engine, engine.replace("AudioMaxChannels=96", "AudioMaxChannels=128")
		.replace("r.VirtualTextures=True", "r.VirtualTextures=False")), {"M2-0", "M2-C"})
	types_h = (REPO / "Source/RawBreak/Public/Core/RbTypes.h").read_text(encoding="utf-8")
	check("RbTypes.h: table part appended", touched_owners("Source/RawBreak/Public/Core/RbTypes.h", types_h,
		types_h.replace("\tCount UMETA(Hidden)\n};", "\tCabinet,      // test\n\tCount UMETA(Hidden)\n};", 1)), {"M2-L"})
	check("RbTypes.h: venue added", touched_owners("Source/RawBreak/Public/Core/RbTypes.h", types_h, types_h.replace("\tDiveBar,  //", "\tPoolHall,\n\tDiveBar,  //", 1)), {"M2-0"})
	types_cpp = (REPO / "Source/RawBreak/Private/Core/RbTypes.cpp").read_text(encoding="utf-8")
	check("RbTypes.cpp: ToString case", touched_owners("Source/RawBreak/Private/Core/RbTypes.cpp", types_cpp,
		types_cpp.replace("\t\tcase ERbTablePart::Count: break;", "\t\tcase ERbTablePart::Legs: return TEXT(\"Legs\");\n\t\tcase ERbTablePart::Count: break;", 1)), {"M2-L"})
	check("new file of another package", touched_owners("Source/RawBreak/Private/UI/New.cpp", None, "x"), {"M2-D"})

	for line in failures:
		print(f"[rbue] selftest FAIL {line}")
	print(f"[rbue] selftest {'OK' if not failures else f'FAILED ({len(failures)})'}")
	return 1 if failures else 0


def main() -> int:
	# UE logs contain non-ASCII text (localized engine messages, paths): never die on the Windows console code page (UE-4 / UE-7
	# review requests; PYTHONIOENCODING=utf-8 is no longer needed).
	for stream in (sys.stdout, sys.stderr):
		if hasattr(stream, "reconfigure"):
			stream.reconfigure(encoding="utf-8", errors="replace")
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = p.add_subparsers(dest="command", required=True)

	b = sub.add_parser("build")
	b.add_argument("--target", default="RawBreakEditor")
	b.add_argument("--config", default="Development")
	b.add_argument("--clean", action="store_true")
	b.add_argument("--timeout", type=float, default=3600)
	b.set_defaults(func=cmd_build)

	y = sub.add_parser("py")
	y.add_argument("script")
	y.add_argument("args", nargs="*")
	y.add_argument("--rhi", action="store_true", help="start with a real RHI (default -NullRHI)")
	y.add_argument("--timeout", type=float, default=3600)
	y.set_defaults(func=cmd_py)

	c = sub.add_parser("capture")
	c.add_argument("--map", required=True)
	c.add_argument("--out", required=True, help="PNG path; with several cameras it may contain {camera} (else <stem>_<camera>.png)")
	c.add_argument("--camera", default="", help="tag / name / label of a CameraActor, or a comma-separated list rendered in ONE process")
	c.add_argument("--res", default="1920x1080")
	c.add_argument("--warmup", type=int, default=90, help="frames rendered per view after shaders finished compiling")
	c.add_argument("--warmup-seconds", type=float, default=4.0, help="minimum game time of the warm-up per view (auto exposure converges per second)")
	c.add_argument("--show-ui", action="store_true", help="screenshot with the viewport's Slate UI (overlay, menus, key hints)")
	c.add_argument("--hide-tags", default="", help="comma-separated actor tags hidden for the capture (e.g. RbDB_Ceiling for the plan view)")
	c.add_argument("--allow-shader-errors", action="store_true", help="do not fail on material / shader compile errors")
	c.add_argument("--exec-cmds", default="")
	c.add_argument("--exe", default="", help="a packaged RawBreak.exe instead of the editor binary in -game mode")
	c.add_argument("--extra", nargs="*", default=[], help="raw extra UE arguments, e.g. --extra=-RbUiScreen=Pause (M2)")
	c.add_argument("--timeout", type=float, default=7200)
	c.set_defaults(func=cmd_capture)

	t = sub.add_parser("test")
	t.add_argument("--filter", default="RawBreak.")
	t.add_argument("--render", action="store_true", help="real RHI offscreen (functional / screenshot tests)")
	t.add_argument("--sound", action="store_true", help="keep the audio device (drops -NoSound, output muted): RawBreak.Functional.Audio.* (M2-C)")
	t.add_argument("--audible", action="store_true", help="with --sound: do not mute the device's output (a human listens)")
	t.add_argument("--extra", nargs="*", default=[], help="raw extra UE arguments, e.g. --extra=-RbSomething=1")
	t.add_argument("--timeout", type=float, default=3600)
	t.set_defaults(func=cmd_test)

	g = sub.add_parser("game")
	g.add_argument("--map", default="")
	g.set_defaults(func=cmd_game)

	f = sub.add_parser("perf", help="frame-time log (M2-A9)")
	f.add_argument("--map", required=True, help="map URL, e.g. /Game/Generated/Maps/L_DiveBar?Mode=Practice?Seed=3")
	f.add_argument("--out", required=True, help="JSON report path")
	f.add_argument("--res", default="2560x1440")
	f.add_argument("--quality", default="High", help="rb.Quality preset run at start-up ('' = leave the settings alone)")
	f.add_argument("--screen-percentage", default="66.67", help="TSR at the DLSS-Quality internal resolution ('' = the preset's)")
	f.add_argument("--frames", type=int, default=600)
	f.add_argument("--perf-exec", default="", help="console commands (';'-separated) run when the recording starts, e.g. 'rb.Match.Break 9'")
	f.add_argument("--exec-cmds", default="", help="extra start-up console commands (-ExecCmds)")
	f.add_argument("--camera", default="", help="record through this CameraActor (tag / name) instead of the player's view")
	f.add_argument("--warmup", type=int, default=120)
	f.add_argument("--warmup-seconds", type=float, default=8.0)
	f.add_argument("--exe", default="", help="a packaged RawBreak.exe (M2-A9 wants the packaged Development build)")
	f.add_argument("--gate", action="store_true", help="exit 1 when a target is missed (M2 only logs)")
	f.add_argument("--extra", nargs="*", default=[])
	f.add_argument("--timeout", type=float, default=3600)
	f.set_defaults(func=cmd_perf)

	k = sub.add_parser("package", help="BuildCookRun -> <RB_BUILDS_DIR>/<label>/Windows/RawBreak.exe (M2-A10)")
	k.add_argument("--config", default="Development")
	k.add_argument("--label", default="M2")
	k.add_argument("--out-dir", default="")
	k.add_argument("--clean", action="store_true")
	k.add_argument("--timeout", type=float, default=4 * 3600)
	k.set_defaults(func=cmd_package)

	l = sub.add_parser("ledger", help="validate Docs/licenses (asset-ledger.csv + ledger/*.csv fragments)")
	l.add_argument("--merge", action="store_true", help="append the fragment rows that are not merged yet (architect, at integration)")
	l.add_argument("--check", action="store_true", help="fail when a fragment row is not merged yet")
	l.add_argument("--allow-ai", action="store_true", help="accept ai_generated = y (from DB-5 on; M2 has none)")
	l.set_defaults(func=cmd_ledger)

	o = sub.add_parser("core", help="CMake build + ctest of BilliardsCore (Tests/Core)")
	o.add_argument("--config", default="Release")
	o.add_argument("--build-dir", default="build")
	o.add_argument("--timeout", type=float, default=3600)
	o.add_argument("--slow", action="store_true", help="include the opt-in _Slow_ tests (timing gates: only meaningful on an idle machine)")
	o.add_argument("filters", nargs="*", help="test-name filters of Tests/Core/TestMain.cpp, e.g. MOT_ (after --); -_Slow_ is added unless --slow")
	o.set_defaults(func=cmd_core)

	w = sub.add_parser("owners", help="check that a package branch only touches the files it owns (18.2), or name a path's owner")
	w.add_argument("--package", default="", help=f"one of {', '.join(OWNERS)}")
	w.add_argument("--branch", default="", help="the package branch (default: this working tree incl. uncommitted and untracked files)")
	w.add_argument("--base", default="main", help="compared against the merge base with this branch")
	w.add_argument("--who", nargs="*", default=[], help="print the owner of these repo-relative paths instead")
	w.set_defaults(func=cmd_owners)

	s = sub.add_parser("selftest", help="checks of rbue.py's own pure helpers (no Unreal)")
	s.set_defaults(func=cmd_selftest)

	a = p.parse_args()
	return a.func(a)


if __name__ == "__main__":
	sys.exit(main())
