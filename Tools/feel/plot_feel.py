#!/usr/bin/env python3
"""M2-F trace plots (Docs/ue-architecture.md 18.3 F9). Owner: M2-F.

  python Tools/unreal/rbue.py test --filter RawBreak.Unit.       # writes Saved/RbFeel/*.csv (RbFeelTests, RbHumanMotionTests)
  python Tools/feel/plot_feel.py                                 # -> Docs/images/dev/m2f/{getdown_seeds,aim_trace,look_gate}.png

  getdown_seeds.png  P5: five seeded get-downs (side view of the eye path: the hip-hinge arc, the overshoot below the final eye height
                     and the settle), the eye height over time, the head leading (view rotation vs translation progress), and the
                     matching stand-ups (faster at the start).
  aim_trace.png      P3: azimuth over mouse travel, coarse (90 deg per 12.5 cm) and fine with Shift (x 0.075), and the optional
                     acceleration gain over hand speed (monotone, continuous, 1 at the 10 cm/s reference).
  look_gate.png      P1: the F1 / F2 trace - mouse travel, what the look intent gate lets through, and the view: nothing moves the
                     head from the contact through the follow-through, the release and a second of watching; the deliberate move
                     opens the gate after the dead zone and fades in.

Needs matplotlib (a dev tool; not part of the game or the pipeline).
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
SRC = REPO / "Saved" / "RbFeel"
OUT = REPO / "Docs" / "images" / "dev" / "m2f"

# Reference palette (dataviz skill, light mode): categorical slots in fixed order, neutral ink for text.
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK2 = "#52514e"
GRID = "#e4e3df"
MUTED = "#9a9892"

# The F6 posture of RbHumanMotionTests (standing eye -> down on the shot).
STAND = {"x": 0.0, "z": 165.0, "pitch": -5.0}
DOWN = {"x": 60.0, "z": 95.0, "pitch": -25.0}


def style(ax, title: str, xlabel: str, ylabel: str) -> None:
	ax.set_facecolor(SURFACE)
	ax.set_title(title, color=INK, fontsize=11, loc="left", pad=8)
	ax.set_xlabel(xlabel, color=INK2, fontsize=9)
	ax.set_ylabel(ylabel, color=INK2, fontsize=9)
	ax.tick_params(colors=INK2, labelsize=8, length=3)
	ax.grid(True, color=GRID, linewidth=0.8)
	ax.set_axisbelow(True)
	for side in ("top", "right"):
		ax.spines[side].set_visible(False)
	for side in ("left", "bottom"):
		ax.spines[side].set_color(MUTED)


def figure(rows: int, cols: int, size: tuple[float, float], title: str):
	fig, axes = plt.subplots(rows, cols, figsize=size, dpi=130)
	fig.patch.set_facecolor(SURFACE)
	fig.suptitle(title, color=INK, fontsize=13, x=0.01, ha="left")
	return fig, axes


def read(name: str) -> tuple[list[dict], list[str]]:
	rows, comments = [], []
	with open(SRC / name, newline="", encoding="utf-8") as f:
		lines = [line for line in f if line.strip()]
	comments = [line.strip() for line in lines if line.startswith("#")]
	for row in csv.DictReader(line for line in lines if not line.startswith("#")):
		rows.append({k: (float(v) if k not in ("change", "mode") else v) for k, v in row.items()})
	return rows, comments


def plot_getdown() -> Path:
	rows, _ = read("getdown_seeds.csv")
	seeds = sorted({int(r["seed"]) for r in rows})
	fig, ((a, b), (c, d)) = figure(2, 2, (12, 8), "P5 posture changes: five seeds, never two identical (RbHumanMotionTests F6)")
	for i, seed in enumerate(seeds):
		color = SERIES[i % len(SERIES)]
		down = [r for r in rows if r["change"] == "down" and int(r["seed"]) == seed]
		up = [r for r in rows if r["change"] == "up" and int(r["seed"]) == seed]
		label = f"seed {seed}"
		a.plot([r["x_cm"] for r in down], [r["z_cm"] for r in down], color=color, linewidth=1.6, label=label)
		b.plot([r["t"] for r in down], [r["z_cm"] for r in down], color=color, linewidth=1.6, label=label)
		d.plot([r["t"] for r in up], [r["z_cm"] for r in up], color=color, linewidth=1.6, label=label)
		if i == 0:
			# Progress of the translation (along the chord) and of the view rotation (pitch) of one get-down: the head leads.
			cx, cz = DOWN["x"] - STAND["x"], DOWN["z"] - STAND["z"]
			chord2 = cx * cx + cz * cz
			trans = [((r["x_cm"] - STAND["x"]) * cx + (r["z_cm"] - STAND["z"]) * cz) / chord2 for r in down]
			rot = [(r["pitch_deg"] - STAND["pitch"]) / (DOWN["pitch"] - STAND["pitch"]) for r in down]
			t = [r["t"] for r in down]
			c.plot(t, rot, color=SERIES[0], linewidth=1.8, label="view rotation (pitch)")
			c.plot(t, trans, color=SERIES[1], linewidth=1.8, label="eye translation (chord)")
			for series, name, col in ((rot, "rotation", SERIES[0]), (trans, "translation", SERIES[1])):
				t50 = next((tt for tt, v in zip(t, series) if v >= 0.5), None)
				if t50 is not None:
					c.axvline(t50, color=col, linewidth=0.8, linestyle=":")
					c.annotate(f"{name} 50 % at {t50:.3f} s", (t50, 0.5), xytext=(6, -14 if name == "rotation" else 8),
						textcoords="offset points", color=INK2, fontsize=8)
			c.axhline(0.5, color=MUTED, linewidth=0.8)
	a.axhline(DOWN["z"], color=MUTED, linewidth=0.8, linestyle="--")
	a.annotate("final eye height (overshoot below it, then a damped settle)", (2, DOWN["z"]), xytext=(0, -14), textcoords="offset points",
		color=INK2, fontsize=8)
	b.axhline(DOWN["z"], color=MUTED, linewidth=0.8, linestyle="--")
	d.axhline(STAND["z"], color=MUTED, linewidth=0.8, linestyle="--")
	style(a, "Get-down, side view of the eye: the hip hinge goes forward first", "forward x [cm]", "eye height z [cm]")
	style(b, "Get-down, eye height over time (0.8-1.5 s incl. the settle)", "time [s]", "eye height z [cm]")
	style(c, f"Get-down seed {seeds[0]}: the head leads the body", "time [s]", "progress (0 = standing, 1 = down)")
	style(d, "Stand-up, eye height over time (faster at the start)", "time [s]", "eye height z [cm]")
	# Zoom on the overshoot: the last 12 cm of the descent.
	b_inset = b.inset_axes([0.52, 0.42, 0.44, 0.5])
	for i, seed in enumerate(seeds):
		down = [r for r in rows if r["change"] == "down" and int(r["seed"]) == seed]
		b_inset.plot([r["t"] for r in down], [r["z_cm"] for r in down], color=SERIES[i % len(SERIES)], linewidth=1.2)
	b_inset.axhline(DOWN["z"], color=MUTED, linewidth=0.8, linestyle="--")
	b_inset.set_ylim(DOWN["z"] - 1.6, DOWN["z"] + 3.0)
	b_inset.set_xlim(0.5, 1.4)
	b_inset.tick_params(colors=INK2, labelsize=7)
	b_inset.set_title("overshoot 3-15 mm + settle", fontsize=8, color=INK2)
	b_inset.grid(True, color=GRID, linewidth=0.6)
	for ax in (a, b, d):
		ax.legend(frameon=False, fontsize=8, labelcolor=INK2, loc="upper right" if ax is not b else "lower left")
	c.legend(frameon=False, fontsize=8, labelcolor=INK2, loc="lower right")
	fig.tight_layout(rect=(0, 0, 1, 0.96))
	out = OUT / "getdown_seeds.png"
	fig.savefig(out, facecolor=SURFACE)
	plt.close(fig)
	return out


def plot_aim() -> Path:
	rows, _ = read("aim_trace.csv")
	accel, _ = read("aim_accel.csv")
	fig, (a, b) = figure(1, 2, (12, 4.8), "P3 aim in centimetres of mouse travel (RbFeelTests F3 / F4, 800 DPI, 60 fps)")
	for i, mode in enumerate(("coarse", "fine")):
		pts = [r for r in rows if r["mode"] == mode]
		label = "coarse: 7.2 deg/cm (90 deg per 12.5 cm)" if mode == "coarse" else "fine (Shift): x 0.075 = 6.75 deg per 12.5 cm"
		a.plot([r["mouse_cm"] for r in pts], [r["azimuth_deg"] for r in pts], color=SERIES[i], linewidth=1.8, marker="o", markersize=3, label=label)
	a.axhline(90.0, color=MUTED, linewidth=0.8, linestyle="--")
	a.axvline(12.5, color=MUTED, linewidth=0.8, linestyle="--")
	a.annotate("90 deg at 12.5 cm", (12.5, 90.0), xytext=(-92, 6), textcoords="offset points", color=INK2, fontsize=8)
	style(a, "Azimuth over mouse travel (frame splits 30 / 60 / 144 fps: bitwise equal)", "mouse travel [cm]", "cue azimuth change [deg]")
	a.legend(frameon=False, fontsize=8, labelcolor=INK2, loc="center right")
	levels = sorted({r["acceleration"] for r in accel})
	for i, level in enumerate(levels):
		pts = [r for r in accel if r["acceleration"] == level]
		b.plot([r["speed_cm_s"] for r in pts], [r["gain"] for r in pts], color=SERIES[i], linewidth=1.8, label=f"acceleration {level:.2f}")
	b.axvline(10.0, color=MUTED, linewidth=0.8, linestyle="--")
	b.annotate("reference hand speed 10 cm/s: gain 1", (10.0, 1.0), xytext=(8, 30), textcoords="offset points", color=INK2, fontsize=8)
	b.set_xscale("log")
	style(b, "Optional acceleration: gain over hand speed (0 = linear, the default)", "hand speed [cm/s] (log)", "gain")
	b.legend(frameon=False, fontsize=8, labelcolor=INK2, loc="upper left")
	fig.tight_layout(rect=(0, 0, 1, 0.93))
	out = OUT / "aim_trace.png"
	fig.savefig(out, facecolor=SURFACE)
	plt.close(fig)
	return out


def plot_gate() -> Path:
	rows, comments = read("look_gate.csv")
	marks = {}
	for c in comments:
		parts = c.lstrip("#").split()
		for key, value in zip(parts[0::2], parts[1::2]):
			marks[key] = float(value)
	t = [r["t"] for r in rows]
	contact = marks.get("contact", 0.0)
	i0 = min(range(len(t)), key=lambda i: abs(t[i] - contact))
	yaw0, pitch0 = rows[i0]["view_yaw_deg"], rows[i0]["view_pitch_deg"]
	fig, (a, b, c) = figure(3, 1, (12, 9.5), "P1 calm view after the contact: the look intent gate (RbFeelTests F1 / F2, human motion layer on)")
	a.plot(t, [r["mouse_y_cm"] for r in rows], color=SERIES[0], linewidth=1.8, label="mouse forward (the stroke, then residual motion)")
	a.plot(t, [r["mouse_x_cm"] for r in rows], color=SERIES[1], linewidth=1.8, label="mouse sideways (drift, then the deliberate 4 cm move)")
	style(a, "Mouse travel", "", "cumulative travel [cm]")
	a.set_ylim(-0.5, 19.0)
	a.legend(frameon=False, fontsize=8, labelcolor=INK2, loc="upper right")
	b.step(t, [r["stroke_held"] for r in rows], where="post", color=SERIES[2], linewidth=1.8, label="Stroke button held")
	b.step(t, [r["gate_open"] * 0.9 for r in rows], where="post", color=SERIES[6], linewidth=1.8, label="look gate open (x 0.9)")
	b.set_yticks([0, 1])
	style(b, "Stroke button and look gate", "", "state")
	b.legend(frameon=False, fontsize=8, labelcolor=INK2, loc="center right")
	c.plot(t, [r["gaze_target_yaw_deg"] for r in rows], color=SERIES[6], linewidth=1.8, label="head yaw from look input (gate output)")
	c.plot(t, [r["view_yaw_deg"] - yaw0 for r in rows], color=SERIES[0], linewidth=1.8, label="view yaw change since the contact")
	c.plot(t, [r["view_pitch_deg"] - pitch0 for r in rows], color=SERIES[1], linewidth=1.8, label="view pitch change since the contact")
	style(c, "The view: only the deliberate move turns the head, faded in", "time [s]", "degrees")
	c.legend(frameon=False, fontsize=8, labelcolor=INK2, loc="center left", bbox_to_anchor=(0.02, 0.55))
	for ax in (a, b, c):
		for key, label in (("contact", "contact"), ("release", "release"), ("move", "deliberate move")):
			if key in marks:
				ax.axvline(marks[key], color=MUTED, linewidth=0.9, linestyle="--")
				if ax is a:
					ax.annotate(label, (marks[key], ax.get_ylim()[1]), xytext=(3, -12), textcoords="offset points", color=INK2, fontsize=8)
	fig.tight_layout(rect=(0, 0, 1, 0.96))
	out = OUT / "look_gate.png"
	fig.savefig(out, facecolor=SURFACE)
	plt.close(fig)
	return out


def main() -> int:
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.parse_args()
	OUT.mkdir(parents=True, exist_ok=True)
	missing = [n for n in ("getdown_seeds.csv", "aim_trace.csv", "aim_accel.csv", "look_gate.csv") if not (SRC / n).exists()]
	if missing:
		print(f"[plot_feel] missing {', '.join(missing)} in {SRC}: run rbue.py test --filter RawBreak.Unit. first")
		return 1
	for out in (plot_getdown(), plot_aim(), plot_gate()):
		print(f"[plot_feel] {out}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
