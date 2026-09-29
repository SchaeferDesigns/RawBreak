#!/usr/bin/env python3
"""RAW BREAK headless Blender runner (host side, Python 3.11 stdlib only; venue-dive-bar 13.1-13.2, Docs/ue-architecture.md 18.8).

  python Tools/blender/rbbl.py run Tools/blender/divebar/db_axis_test.py [-- --seed 1958 --out Art/DiveBar/Export]
  python Tools/blender/rbbl.py all                  # Tools/blender/divebar/db_build_all.py (every venue generator, in order)

Runs Blender 5.2 in the background with a factory start-up (no user prefs / add-ons, reproducible), no audio and
--python-exit-code 1, logs to Saved/RbLogs/blender-<script>-<time>.log and fails (exit != 0) on a non-zero Blender exit code, a
Python traceback or the explicit marker RBBL_FAIL (Tools/blender/common/rb_bl.py fail()). Kills the process tree on timeout.
Never leaves a Blender process running. Environment: RB_BLENDER overrides the executable.
Owner: M2-A (implemented by the M2 architect step; M2-A extends it).
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
BLENDER = Path(os.environ.get("RB_BLENDER", r"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe"))
LOG_DIR = REPO / "Saved/RbLogs"
FAIL = re.compile(r"(RBBL_FAIL|Traceback \(most recent call last\)|Error: Python script failed)")


def _log_path(stem: str) -> Path:
	LOG_DIR.mkdir(parents=True, exist_ok=True)
	return LOG_DIR / f"blender-{stem}-{_dt.datetime.now().strftime('%Y%m%d-%H%M%S')}.log"


def _kill_tree(pid: int) -> None:
	subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True)


def run_script(script: Path, args: list[str], timeout: float) -> int:
	script = script.resolve()
	if not script.exists():
		print(f"[rbbl] no such script: {script}")
		return 2
	if not BLENDER.exists():
		print(f"[rbbl] Blender not found: {BLENDER} (set RB_BLENDER)")
		return 2
	cmd = [str(BLENDER), "-b", "--factory-startup", "-noaudio", "--python-exit-code", "1", "--python", str(script), "--"] + args
	print("[rbbl] " + " ".join(cmd), flush=True)
	log = _log_path(script.stem)
	start = time.monotonic()
	proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=str(REPO), text=True, encoding="utf-8",
		errors="replace", bufsize=1)
	lines: list[str] = []
	code = 0
	try:
		assert proc.stdout is not None
		for line in proc.stdout:
			lines.append(line.rstrip("\n"))
			if "[rb]" in line or FAIL.search(line) or "Error" in line:
				print(line.rstrip("\n"), flush=True)
			if time.monotonic() - start > timeout:
				print(f"[rbbl] TIMEOUT after {timeout:.0f} s - killing process tree", flush=True)
				_kill_tree(proc.pid)
				code = 124
				break
		if code == 0:
			code = proc.wait(timeout=max(1.0, timeout - (time.monotonic() - start)))
	except subprocess.TimeoutExpired:
		_kill_tree(proc.pid)
		code = 124
	log.write_text("\n".join(lines), encoding="utf-8")
	failures = [l for l in lines if FAIL.search(l)]
	if failures and code == 0:
		code = 1
	print(f"[rbbl] {'OK' if code == 0 else 'FAILED'} ({code}) after {time.monotonic() - start:.1f} s  log: {log}", flush=True)
	return code


def main() -> int:
	for stream in (sys.stdout, sys.stderr):
		if hasattr(stream, "reconfigure"):
			stream.reconfigure(encoding="utf-8", errors="replace")
	# Everything after the first "--" goes to the Blender script unchanged.
	argv = sys.argv[1:]
	script_args: list[str] = []
	if "--" in argv:
		script_args = argv[argv.index("--") + 1:]
		argv = argv[:argv.index("--")]
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = p.add_subparsers(dest="command", required=True)
	r = sub.add_parser("run")
	r.add_argument("script")
	r.add_argument("--timeout", type=float, default=3600)
	a_all = sub.add_parser("all")
	a_all.add_argument("--timeout", type=float, default=7200)
	a = p.parse_args(argv)
	if a.command == "run":
		return run_script(Path(a.script), script_args, a.timeout)
	return run_script(REPO / "Tools/blender/divebar/db_build_all.py", script_args, a.timeout)


if __name__ == "__main__":
	sys.exit(main())
