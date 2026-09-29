#!/usr/bin/env python3
"""RAW BREAK headless Unreal pipeline runner (host side, Python 3.11 stdlib only).

Every Unreal step of the project runs through this script so that no human ever has to open the editor
(Docs/ue-architecture.md section 9). Sub-commands:

  build     UnrealBuildTool build of a target            rbue.py build [--target RawBreakEditor] [--config Development]
  py        run an editor Python script headless         rbue.py py Tools/unreal/editor/rb_make_test_room.py [-- script args]
  capture   render one frame of a map to a PNG           rbue.py capture --map /Game/Dev/PipelineProof/L_PipelineProof --out Docs/images/x.png
  test      run UE Automation tests headless             rbue.py test --filter RawBreak. [--render] [--sound]
  game      launch the game (windowed, for a human)      rbue.py game [--map ...]

Logs go to Saved/RbLogs/<command>-<timestamp>.log (UE's -abslog). Exit code 0 = success.
Environment: RB_UE_ROOT overrides the engine root (default C:/Program Files/Epic Games/UE_5.8).
"""

from __future__ import annotations

import argparse
import datetime as _dt
import os
import re
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
UPROJECT = REPO / "RawBreak.uproject"
UE_ROOT = Path(os.environ.get("RB_UE_ROOT", r"C:/Program Files/Epic Games/UE_5.8"))
BUILD_BAT = UE_ROOT / "Engine/Build/BatchFiles/Build.bat"
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


def _run(cmd: list[str], timeout: float, echo_filter: re.Pattern | None = None) -> tuple[int, list[str]]:
	"""Runs cmd (tokens already in UE command-line form, see _q), streams matching lines, returns (exit code, lines).
	Kills the whole process tree on timeout. On Windows the command line is passed verbatim to CreateProcess, so
	-Key="value with spaces" reaches UE exactly as written (subprocess list quoting would escape the quotes)."""
	line = " ".join(_q(c) for c in cmd)
	print("[rbue] " + line, flush=True)
	start = time.monotonic()
	proc = subprocess.Popen(line if os.name == "nt" else cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=str(REPO),
		text=True, encoding="utf-8", errors="replace", bufsize=1)
	lines: list[str] = []
	try:
		assert proc.stdout is not None
		for out_line in proc.stdout:
			lines.append(out_line.rstrip("\n"))
			if echo_filter is None or echo_filter.search(out_line):
				print(out_line.rstrip("\n"), flush=True)
			if time.monotonic() - start > timeout:
				print(f"[rbue] TIMEOUT after {timeout:.0f} s - killing process tree", flush=True)
				_kill_tree(proc.pid)
				return 124, lines
		proc.wait(timeout=max(1.0, timeout - (time.monotonic() - start)))
	except subprocess.TimeoutExpired:
		print(f"[rbue] TIMEOUT after {timeout:.0f} s - killing process tree", flush=True)
		_kill_tree(proc.pid)
		return 124, lines
	print(f"[rbue] exit {proc.returncode} after {time.monotonic() - start:.1f} s", flush=True)
	return proc.returncode, lines


# A material whose shader fails to compile renders with the engine's default material instead of failing the run, so a
# capture would still "succeed" (e.g. a broken Custom-node include of the ball shader). Captures fail on these lines unless
# --allow-shader-errors is given (review R-10).
SHADER_ERRORS = re.compile(r"(Failed to compile Material|Default Material will be used|LogShaderCompilers: Error|LogMaterial: Error)")

# Lines worth echoing from a long editor log (errors, our own log categories, python output, progress).
ECHO = re.compile(r"(Error|error:|LogPython|LogRawBreak|LogRb|Automation|Test Completed|TEST COMPLETE|Compiling \d+ shaders|"
	r"shaders left|Result:|Warning: .*Rb|FAILED|PASSED|Success)", re.IGNORECASE)


def cmd_build(a: argparse.Namespace) -> int:
	cmd = [str(BUILD_BAT), a.target, "Win64", a.config, f"-Project={UPROJECT}", "-WaitMutex", "-NoHotReload"]
	if a.clean:
		cmd.append("-Clean")
	code, lines = _run(cmd, a.timeout, re.compile(r"(error|warning|Result:|Total execution)", re.IGNORECASE))
	_log_path("build").write_text("\n".join(lines), encoding="utf-8")
	errors = [l for l in lines if re.search(r"\berror\b", l, re.IGNORECASE) and "0 error" not in l]
	if code == 0 and not any("Result: Succeeded" in l for l in lines):
		code = 1
	print(f"[rbue] build {'OK' if code == 0 else 'FAILED'} ({len(errors)} error lines)")
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
	the raw extra UE arguments of --extra (e.g. --extra -RbUiScreen=Pause; M2, Docs/ue-architecture.md 18.9)."""
	flags = [f for f in COMMON if not (getattr(a, "sound", False) and f == "-NoSound")]
	return flags + list(getattr(a, "extra", None) or [])


def cmd_capture(a: argparse.Namespace) -> int:
	out = Path(a.out).resolve()
	out.parent.mkdir(parents=True, exist_ok=True)
	if out.exists():
		out.unlink()
	log = _log_path("capture")
	resx, resy = a.res.lower().split("x")
	cmd = [str(EDITOR_CMD), str(UPROJECT), _unmsys(a.map), "-game", "-RenderOffscreen", "-Windowed", f"-ResX={resx}", f"-ResY={resy}", "-ForceRes",
		f'-RBCapture="{out}"', f"-RBCaptureWarmup={a.warmup}", f"-RBCaptureWarmupSeconds={a.warmup_seconds}", f'-abslog="{log}"'] + _common(a)
	if a.camera:
		cmd.append(f'-RBCaptureCamera="{a.camera}"')
	if a.exec_cmds:
		cmd.append(f'-ExecCmds="{a.exec_cmds}"')
	code, lines = _run(cmd, a.timeout, ECHO)
	ok = out.exists() and out.stat().st_size > 0
	shader_errors = [l for l in lines if SHADER_ERRORS.search(l)]
	if shader_errors:
		print(f"[rbue] {len(shader_errors)} material / shader compile error line(s), e.g.: {shader_errors[0]}")
		if not a.allow_shader_errors:
			ok = False
	print(f"[rbue] capture {'OK' if ok else 'FAILED'}: {out}  log: {log}")
	return 0 if ok else (code or 1)


def cmd_test(a: argparse.Namespace) -> int:
	log = _log_path("test")
	report = REPO / "Saved/RbLogs/AutomationReport"
	cmd = [str(EDITOR_CMD), str(UPROJECT), f'-ExecCmds="Automation RunTests {a.filter}; Quit"', '-TestExit="Automation Test Queue Empty"',
		f'-ReportExportPath="{report}"', f'-abslog="{log}"'] + _common(a)
	cmd += ["-RenderOffscreen"] if a.render else ["-NullRHI"]
	code, lines = _run(cmd, a.timeout, ECHO)
	failed = [l for l in lines if re.search(r"Test Completed\. Result=\{(Fail|Failed)\}", l)]
	passed = [l for l in lines if re.search(r"Test Completed\. Result=\{(Success|Passed)\}", l)]
	print(f"[rbue] tests passed {len(passed)}, failed {len(failed)}  log: {log}")
	if not passed and not failed:
		return code or 3
	return 1 if failed else 0


def cmd_game(a: argparse.Namespace) -> int:
	cmd = [str(EDITOR), str(UPROJECT)] + ([_unmsys(a.map)] if a.map else []) + ["-game", "-Windowed", "-ResX=1920", "-ResY=1080", "-log"]
	line = " ".join(_q(c) for c in cmd)
	print("[rbue] " + line)
	subprocess.Popen(line if os.name == "nt" else cmd, cwd=str(REPO))
	return 0


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
	c.add_argument("--out", required=True)
	c.add_argument("--camera", default="", help="actor name/label or tag of a CameraActor to view through")
	c.add_argument("--res", default="1920x1080")
	c.add_argument("--warmup", type=int, default=90, help="frames rendered after shaders finished compiling")
	c.add_argument("--warmup-seconds", type=float, default=4.0, help="minimum game time of the warm-up (auto exposure converges per second)")
	c.add_argument("--allow-shader-errors", action="store_true", help="do not fail on material / shader compile errors")
	c.add_argument("--exec-cmds", default="")
	c.add_argument("--extra", nargs="*", default=[], help="raw extra UE arguments, e.g. --extra=-RbUiScreen=Pause (M2)")
	c.add_argument("--timeout", type=float, default=7200)
	c.set_defaults(func=cmd_capture)

	t = sub.add_parser("test")
	t.add_argument("--filter", default="RawBreak.")
	t.add_argument("--render", action="store_true", help="real RHI offscreen (functional / screenshot tests)")
	t.add_argument("--sound", action="store_true", help="keep the audio device (drops -NoSound): RawBreak.Functional.Audio.* (M2-C)")
	t.add_argument("--extra", nargs="*", default=[], help="raw extra UE arguments, e.g. --extra=-RbSomething=1")
	t.add_argument("--timeout", type=float, default=3600)
	t.set_defaults(func=cmd_test)

	g = sub.add_parser("game")
	g.add_argument("--map", default="")
	g.set_defaults(func=cmd_game)

	a = p.parse_args()
	return a.func(a)


if __name__ == "__main__":
	sys.exit(main())
