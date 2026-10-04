#!/usr/bin/env python3
"""M2-C audio report: spectrograms and timing plots of the engine recordings (Docs/ue-architecture.md 18.5).

Reads what `RawBreak.Functional.Audio.*` (rbue.py test --sound) writes:
  Docs/audio/m2/{divebar,testroom}_break_master.wav, mix_replay_pause_master.wav, au0_two_voices.wav   (24-bit, committed)
  Saved/RbAudio/m2c/<venue>_{table,foley,ambience,reverb,world}.wav, <venue>_events.csv, <venue>_meta.txt, au0_meta.txt,
  mix_meta.txt                                                                                         (float32 stems, local)
and writes Docs/images/dev/m2c/*.png:
  <venue>_break.png          master spectrogram, stem levels, the event raster (every planned impact at its scheduled time,
                             filled = an onset was detected within 1.5 ms), footsteps, the loose-ball floor hit / roll, gully runs
  <venue>_break_cluster.png  the first 0.5 s of the table stem with the scheduled impacts (onset detector vs the schedule; the
                             sample accuracy itself is AU-0's measurement)
  au0_alignment.png          AU-0: the same click in two voices (0 samples skew) and 0.5104 ms apart (24.50 samples)
  mix_replay_pause.png       live shot, x0.25 replay, pause hold / resume, the pause mix
  room_tone.png              long-term spectra of both venues' room tone (ambience stems)

Run from the repo root with any Python that has numpy + matplotlib (the system Python of the dev machine):
  python Tools/audio/m2_report.py [--out Docs/images/dev/m2c]
Owner: M2-C.
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

import numpy as np

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.lines import Line2D  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "Docs/audio/m2"
STEMS = ROOT / "Saved/RbAudio/m2c"

CLASS_ORDER = ["TipStrike", "TipRecontact", "BallBall", "BallCushion", "BallJaw", "BallRailCap", "BallSlate", "BallLiner",
               "PocketDrop", "TrapClick", "Footstep", "FloorHit"]
CLASS_COLOURS = {
    "TipStrike": "#e6194b", "TipRecontact": "#f58231", "BallBall": "#4363d8", "BallCushion": "#3cb44b", "BallJaw": "#911eb4",
    "BallRailCap": "#46f0f0", "BallSlate": "#f032e6", "BallLiner": "#9a6324", "PocketDrop": "#000075", "TrapClick": "#808000",
    "Footstep": "#469990", "FloorHit": "#800000",
}
STEM_COLOURS = {"table": "#4363d8", "foley": "#469990", "ambience": "#808080", "reverb": "#f58231", "world": "#000000"}


# ---------------------------------------------------------------------------------------------------------------- I/O

def read_wav(path: Path) -> tuple[np.ndarray, int]:
    """(frames x channels float64, fs) of a PCM 16 / 24 / 32-bit or IEEE float32 WAV."""
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")
    pos = 12
    fmt = None
    pcm = None
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
            if fmt[0] == 0xFFFE and len(body) >= 26:  # WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes
                fmt = (struct.unpack("<H", body[24:26])[0],) + fmt[1:]
        elif tag == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    if fmt is None or pcm is None:
        raise ValueError(f"{path}: no fmt / data chunk")
    code, channels, fs, _, _, bits = fmt
    if code == 3 and bits == 32:
        x = np.frombuffer(pcm, dtype="<f4").astype(np.float64)
    elif code == 1 and bits == 16:
        x = np.frombuffer(pcm, dtype="<i2").astype(np.float64) / 32768.0
    elif code == 1 and bits == 24:
        b = np.frombuffer(pcm[:len(pcm) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v.astype(np.float64) / 8388608.0
    elif code == 1 and bits == 32:
        x = np.frombuffer(pcm, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"{path}: unsupported WAV format {code} / {bits} bit")
    return x[: len(x) // channels * channels].reshape(-1, channels), fs


def read_meta(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip()
    return out


def read_events(path: Path) -> list[dict]:
    rows = []
    if not path.exists():
        return rows
    lines = path.read_text(encoding="utf-8").splitlines()
    head = lines[0].split(",")
    for line in lines[1:]:
        if line.strip():
            rows.append(dict(zip(head, line.split(","))))
    return rows


# ------------------------------------------------------------------------------------------------------------ analysis

def db(x, floor=-200.0):
    return np.maximum(20.0 * np.log10(np.maximum(np.abs(x), 1e-12)), floor)


def stft_db(x: np.ndarray, fs: int, nfft: int = 2048, hop: int = 256):
    win = np.hanning(nfft)
    pad = np.concatenate([np.zeros(nfft // 2), x, np.zeros(nfft // 2)])
    n = 1 + (len(pad) - nfft) // hop
    idx = np.arange(nfft)[None, :] + hop * np.arange(n)[:, None]
    frames = pad[idx] * win[None, :]
    spec = np.abs(np.fft.rfft(frames, axis=1)) / (np.sum(win) / 2.0)
    t = np.arange(n) * hop / fs
    f = np.fft.rfftfreq(nfft, 1.0 / fs)
    return t, f, 20.0 * np.log10(np.maximum(spec.T, 1e-10))


def short_rms_db(x: np.ndarray, fs: int, win_s: float = 0.02):
    n = max(1, int(win_s * fs))
    m = len(x) // n
    seg = x[: m * n].reshape(m, n)
    r = np.sqrt(np.mean(seg ** 2, axis=1))
    return (np.arange(m) + 0.5) * n / fs, db(r, -160.0)


def welch_db(x: np.ndarray, fs: int, nfft: int = 16384):
    win = np.hanning(nfft)
    hop = nfft // 2
    n = max(1, 1 + (len(x) - nfft) // hop)
    acc = np.zeros(nfft // 2 + 1)
    for i in range(n):
        seg = x[i * hop: i * hop + nfft]
        if len(seg) < nfft:
            seg = np.pad(seg, (0, nfft - len(seg)))
        acc += np.abs(np.fft.rfft(seg * win)) ** 2
    psd = acc / n / (fs * np.sum(win ** 2))  # one-sided-ish power spectral density per Hz
    psd[1:-1] *= 2.0
    return np.fft.rfftfreq(nfft, 1.0 / fs), 10.0 * np.log10(np.maximum(psd, 1e-30))


def mono(x: np.ndarray) -> np.ndarray:
    return x.mean(axis=1) if x.ndim == 2 else x


# --------------------------------------------------------------------------------------------------------------- plots

def style_axes(ax):
    ax.grid(True, which="major", alpha=0.25, linewidth=0.6)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)


def plot_spectrogram(ax, x, fs, t0, t1, offset_s, vmin=-130.0, vmax=-20.0, fmin=30.0):
    i0, i1 = max(0, int((t0 + offset_s) * fs)), min(len(x), int((t1 + offset_s) * fs))
    t, f, s = stft_db(x[i0:i1], fs)
    t = t + i0 / fs - offset_s
    keep = f >= fmin
    m = ax.pcolormesh(t, f[keep], s[keep], shading="auto", cmap="magma", vmin=vmin, vmax=vmax, rasterized=True)
    ax.set_yscale("log")
    ax.set_ylim(fmin, fs / 2)
    ax.set_ylabel("Hz")
    return m


def break_report(venue: str, out: Path) -> dict:
    meta = read_meta(STEMS / f"{venue}_meta.txt")
    master_path = DOCS / f"{venue}_break_master.wav"
    if not meta or not master_path.exists():
        print(f"[m2_report] {venue}: recording missing (run rbue.py test --filter RawBreak.Functional.Audio --sound)")
        return {}
    fs = int(float(meta["fs"]))
    master, _ = read_wav(master_path)
    first = int(meta["table_first"])
    strike = (int(meta["anchor_frame"]) - first) / fs  # capture seconds of the strike (shot time 0)
    stop = float(meta.get("stop_time", "8"))
    events = read_events(STEMS / f"{venue}_events.csv")
    stems = {}
    for name in ("table", "foley", "ambience", "reverb", "world"):
        p = STEMS / f"{venue}_{name}.wav"
        if p.exists():
            stems[name] = mono(read_wav(p)[0])
    footsteps = [int(v) / fs - strike for k, v in sorted(meta.items()) if k.startswith("footstep_")]
    floor_hit = int(meta["floor_hit"]) / fs - strike if "floor_hit" in meta else None
    roll = (int(meta["floor_roll_0"]) / fs - strike, int(meta["floor_roll_1"]) / fs - strike) if "floor_roll_0" in meta else None
    gullies = []
    for k, v in sorted(meta.items()):
        if k.startswith("gully_"):
            a, b = (float(z) for z in v.split(","))
            gullies.append((a / fs - strike, b / fs - strike))

    t_end = max(stop + 2.0, 6.0)
    if roll:
        t_end = max(t_end, roll[1] + 0.6)
    t_end = min(len(master) / fs - strike, t_end)
    t_beg = -0.8
    x = mono(master)

    fig = plt.figure(figsize=(16, 11), dpi=110)
    gs = fig.add_gridspec(3, 2, width_ratios=[60, 1], height_ratios=[4.2, 2.6, 2.4], hspace=0.12, wspace=0.02)
    ax_s = fig.add_subplot(gs[0, 0])
    m = plot_spectrogram(ax_s, x, fs, t_beg, t_end, strike)
    cb = fig.colorbar(m, cax=fig.add_subplot(gs[0, 1]))
    cb.set_label("dBFS (STFT, 2048-pt Hann)")
    ax_s.set_xlim(t_beg, t_end)
    ax_s.set_title(f"{venue}: recorded live break at the breaker's ears (master after the limiter) - {master_path.name}", loc="left",
                   fontsize=12)
    ax_s.tick_params(labelbottom=False)

    ax_l = fig.add_subplot(gs[1, 0], sharex=ax_s)
    for name, s in stems.items():
        t, lv = short_rms_db(s, fs)
        ax_l.plot(t - strike, lv, color=STEM_COLOURS.get(name, "k"), linewidth=0.9, label=f"{name} stem")
    t, lv = short_rms_db(x, fs)
    ax_l.plot(t - strike, lv, color="#e6194b", linewidth=0.6, alpha=0.6, label="master")
    for a, b in gullies:
        ax_l.axvspan(a, b, color="#808000", alpha=0.12)
    if roll:
        ax_l.axvspan(roll[0], roll[1], color="#800000", alpha=0.12)
    ax_l.set_ylim(-125, -5)
    ax_l.set_ylabel("RMS 20 ms [dBFS]")
    ax_l.legend(loc="upper right", ncol=6, fontsize=8, frameon=False)
    style_axes(ax_l)
    ax_l.tick_params(labelbottom=False)

    ax_e = fig.add_subplot(gs[2, 0], sharex=ax_s)
    present = []
    rows = {}
    for e in events:
        k = e["kind"]
        rows.setdefault(k, []).append(e)
    for k in CLASS_ORDER:
        if k in rows or (k == "Footstep" and footsteps) or (k == "FloorHit" and floor_hit is not None):
            present.append(k)
    for k in rows:
        if k not in present:
            present.append(k)
    ylab = []
    stats = {}
    for yi, k in enumerate(present):
        c = CLASS_COLOURS.get(k, "#333333")
        if k == "Footstep":
            ts, ms = footsteps, [True] * len(footsteps)
            label = f"Footstep ({len(ts)}, OnFootstep path)"
        elif k == "FloorHit":
            ts, ms = [floor_hit], [True]
            label = "Loose ball floor hit + roll (AU-25)"
        else:
            ev = rows[k]
            ts = [(float(e["expected_sample"]) / fs) - strike for e in ev]
            ms = [e["matched"] == "1" for e in ev]
            stats[k] = (sum(ms), len(ms))
            label = f"{k} ({sum(ms)}/{len(ms)} heard)"
        ts = np.asarray(ts)
        ms = np.asarray(ms, dtype=bool)
        ax_e.scatter(ts[ms], np.full(ms.sum(), yi), marker="|", s=160, color=c, linewidths=1.6)
        if (~ms).any():
            ax_e.scatter(ts[~ms], np.full((~ms).sum(), yi), marker="o", s=40, facecolors="none", edgecolors=c, linewidths=1.2)
        ylab.append(label)
    for a, b in gullies:
        ax_e.axvspan(a, b, color="#808000", alpha=0.12)
    if roll:
        ax_e.axvspan(roll[0], roll[1], color="#800000", alpha=0.12)
    ax_e.set_yticks(range(len(present)))
    ax_e.set_yticklabels(ylab, fontsize=8)
    ax_e.set_ylim(-0.7, len(present) - 0.3)
    ax_e.invert_yaxis()
    ax_e.set_xlabel("seconds from the tip strike (shot time 0 at the breaker's ears)")
    style_axes(ax_e)
    handles = [Line2D([0], [0], marker="|", color="k", linestyle="none", markersize=10, label="scheduled, onset found within 1.5 ms"),
               Line2D([0], [0], marker="o", color="k", markerfacecolor="none", linestyle="none", label="scheduled, masked (no own onset)"),
               plt.Rectangle((0, 0), 1, 1, color="#808000", alpha=0.25, label="gully run (coin-op)"),
               plt.Rectangle((0, 0), 1, 1, color="#800000", alpha=0.25, label="loose ball rolling on the floor")]
    ax_e.legend(handles=handles, loc="upper center", bbox_to_anchor=(0.5, -0.22), fontsize=8, frameon=False, ncol=4)
    out.mkdir(parents=True, exist_ok=True)
    p = out / f"{venue}_break.png"
    fig.savefig(p, bbox_inches="tight")
    plt.close(fig)
    print(f"[m2_report] wrote {p}")

    # The break cluster: the table stem around the strike with every scheduled impact.
    if "table" in stems:
        tb = stems["table"]
        fig, axes = plt.subplots(2, 1, figsize=(16, 7.5), dpi=110, sharex=True, gridspec_kw={"height_ratios": [3, 1.3], "hspace": 0.08})
        z0, z1 = -0.02, 0.5
        i0, i1 = int((strike + z0) * fs), int((strike + z1) * fs)
        tt = np.arange(i0, i1) / fs - strike
        axes[0].plot(tt * 1e3, tb[i0:i1], color="#4363d8", linewidth=0.5)
        errs = []
        for e in events:
            te = float(e["expected_sample"]) / fs - strike
            if z0 <= te <= z1:
                axes[0].axvline(te * 1e3, color=CLASS_COLOURS.get(e["kind"], "#333"), alpha=0.35, linewidth=0.8)
                if e["matched"] == "1":
                    errs.append((te * 1e3, float(e["error_ms"]), e["kind"]))
        axes[0].set_ylabel("table stem (full scale)")
        axes[0].set_title(f"{venue}: the break cluster, table stem (pre-reverb) with every scheduled impact (lines, colour = class)", loc="left",
                          fontsize=12)
        style_axes(axes[0])
        if errs:
            ex = np.array([a for a, _, _ in errs])
            ey = np.array([b for _, b, _ in errs])
            cs = [CLASS_COLOURS.get(k, "#333") for _, _, k in errs]
            axes[1].scatter(ex, ey * fs / 1e3, c=cs, s=14)
            axes[1].axhline(0.0, color="k", linewidth=0.6)
            axes[1].set_ylabel("onset - scheduled\n[samples]")
            axes[1].set_ylim(-0.0015 * fs, 0.0015 * fs)
        axes[1].set_xlabel("ms from the tip strike")
        style_axes(axes[1])
        used = sorted({e["kind"] for e in events if z0 <= float(e["expected_sample"]) / fs - strike <= z1}, key=lambda k: CLASS_ORDER.index(k)
                      if k in CLASS_ORDER else 99)
        axes[0].legend(handles=[Line2D([0], [0], color=CLASS_COLOURS.get(k, "#333"), label=k) for k in used], loc="upper right", fontsize=8,
                       frameon=False, ncol=len(used))
        p = out / f"{venue}_break_cluster.png"
        fig.savefig(p, bbox_inches="tight")
        plt.close(fig)
        print(f"[m2_report] wrote {p}")
    matched = sum(1 for e in events if e["matched"] == "1")
    errs_ms = [float(e["error_ms"]) for e in events if e["matched"] == "1"]
    return {"venue": venue, "impacts": len(events), "matched": matched, "median_error_ms": float(np.median(errs_ms)) if errs_ms else float("nan"),
            "per_class": stats, "footsteps": len(footsteps), "gullies": len(gullies)}


def group_delay_samples(a: np.ndarray, b: np.ndarray, fs: int, f_lo: float = 500.0, f_hi: float = 6000.0) -> float:
    """Delay of b relative to a [samples] from the cross-spectrum phase slope over f_lo..f_hi (audio.md AU-T08 method)."""
    n = 1 << int(math.ceil(math.log2(max(len(a), len(b)) * 4)))
    A = np.fft.rfft(a, n)
    B = np.fft.rfft(b, n)
    f = np.fft.rfftfreq(n, 1.0 / fs)
    band = (f >= f_lo) & (f <= f_hi)
    cross = B[band] * np.conj(A[band])
    w = np.abs(cross)
    phase = np.unwrap(np.angle(cross))
    omega = 2.0 * np.pi * f[band] / fs  # rad per sample
    # Weighted least squares through the origin: phase = -omega x delay.
    return float(-np.sum(w * omega * phase) / np.sum(w * omega * omega))


def au0_report(out: Path) -> dict:
    wav = DOCS / "au0_two_voices.wav"
    meta = read_meta(STEMS / "au0_meta.txt")
    if not wav.exists() or not meta:
        print("[m2_report] au0: capture or au0_meta.txt missing")
        return {}
    x, fs = read_wav(wav)
    marks = {k: float(v) for k, v in meta.items() if k.startswith("shot_")}
    fig, axes = plt.subplots(1, 2, figsize=(16, 5.8), dpi=110)
    res = {}
    cases = [("shot_a", ["shot_a"], "the same click in voice A (left) and voice B (right; B started 3 game frames after A)"),
             ("shot_b", ["shot_b_left", "shot_b_right"], "clicks 0.5104 ms apart: voice A (left), voice B (right)")]
    for ax, (key, keys, title) in zip(axes, cases):
        centre = marks[keys[0]]
        span = 60
        i0 = int(math.floor(centre)) - span
        i1 = int(math.floor(centre)) + span + (40 if len(keys) > 1 else 0)
        n = np.arange(i0, i1)
        if x.shape[1] > 1:
            ax.plot(n - centre, x[i0:i1, 0], color="#4363d8", linewidth=4.0, alpha=0.35, label="left = voice A")
            ax.plot(n - centre, x[i0:i1, 1], color="#e6194b", linewidth=1.0, marker=".", markersize=3, label="right = voice B")
            d = group_delay_samples(x[i0:i1, 0], x[i0:i1, 1], fs)
            res[key] = d
        for ki, k in enumerate(keys):
            ax.axvline(marks[k] - centre, color="k", linestyle="--", linewidth=0.8)
            ax.annotate(f"{k.replace('shot_', '')} scheduled {marks[k] - centre:+.3f}", (marks[k] - centre, -0.045 - 0.03 * ki), fontsize=8,
                        xytext=(4, 0), textcoords="offset points", bbox=dict(boxstyle="round,pad=0.15", facecolor="white", edgecolor="none", alpha=0.8))
        want = 0.0 if len(keys) == 1 else marks[keys[1]] - marks[keys[0]]
        tol = "AU-T21: 0 samples" if len(keys) == 1 else "AU-T08: 24.50 +- 0.05 samples"
        ax.text(0.02, 0.97, f"measured B - A: {res.get(key, float('nan')):+.4f} samples\nscheduled: {want:+.4f}   ({tol})", transform=ax.transAxes,
                va="top", fontsize=9, family="monospace", bbox=dict(boxstyle="round,pad=0.3", facecolor="white", edgecolor="#bbbbbb"))
        ax.set_xlabel(f"samples from the scheduled frame of {keys[0]} (capture frame {marks[keys[0]]:.3f})")
        ax.set_title(title, loc="left", fontsize=10)
        ax.legend(fontsize=8, frameon=False, loc="lower right")
        style_axes(ax)
    fig.suptitle("AU-0: two USynthComponent voices of one shot clock, recorded sample-exactly from the Table submix (48 kHz, 512-frame "
                 "blocks); delay = cross-spectrum phase slope 0.5-6 kHz", x=0.01, ha="left", fontsize=12)
    out.mkdir(parents=True, exist_ok=True)
    p = out / "au0_alignment.png"
    fig.savefig(p, bbox_inches="tight")
    plt.close(fig)
    print(f"[m2_report] wrote {p}")
    return res


def mix_report(out: Path) -> dict:
    wav = DOCS / "mix_replay_pause_master.wav"
    meta = read_meta(STEMS / "mix_meta.txt")
    if not wav.exists() or not meta:
        print("[m2_report] mix: capture or mix_meta.txt missing")
        return {}
    x, fs = read_wav(wav)
    xm = mono(x)
    marks = {k: int(v) / fs for k, v in meta.items() if k != "fs"}
    fig = plt.figure(figsize=(16, 8), dpi=110)
    gs = fig.add_gridspec(2, 2, width_ratios=[60, 1], height_ratios=[3, 1.6], hspace=0.1, wspace=0.02)
    ax = fig.add_subplot(gs[0, 0])
    m = plot_spectrogram(ax, xm, fs, 0.0, len(xm) / fs, 0.0, vmin=-140.0, vmax=-30.0)
    fig.colorbar(m, cax=fig.add_subplot(gs[0, 1])).set_label("dBFS")
    ax.set_title("volumes, replay and pause mix (master): live shot, x0.25 film-style replay, pause hold / resume, pause mix on / off",
                 loc="left", fontsize=12)
    ax.tick_params(labelbottom=False)
    ax2 = fig.add_subplot(gs[1, 0], sharex=ax)
    t, lv = short_rms_db(xm, fs)
    ax2.plot(t, lv, color="#e6194b", linewidth=0.8)
    ax2.set_ylabel("RMS 20 ms [dBFS]")
    ax2.set_ylim(-150, -10)
    ax2.set_xlabel("seconds of the capture")
    style_axes(ax2)
    # The phases of RawBreak.Functional.Audio.MixReplayPauseVolumes (mix_meta.txt: capture frames).
    if "pause" in marks:
        marks["pause_shot"] = marks["pause"] - 0.8  # the paused shot starts 0.8 s before the hold
    spans = [("live_a", "quiet_b", "live shot A, sliders 1.0", "#3cb44b"),
             ("quiet_b", "replay", "Table / Ambience sliders 0.5: live shot B -12 dB", "#911eb4"),
             ("replay", "pause_shot", "replay of B at x0.25 (film style), ambience -10 dB", "#4363d8"),
             ("pause_shot", "pause", "shot C", "#9a6324"),
             ("pause", "resume", "C held (pause)", "#808080"),
             ("mix_on", "mix_off", "pause mix: World -12 dB, 800 Hz low-pass", "#f58231")]
    for i, (a, b, lab, c) in enumerate(spans):
        if a in marks and b in marks and marks[b] > marks[a]:
            ax2.axvspan(marks[a], marks[b], color=c, alpha=0.16)
            ax2.text(marks[a] + 0.05, -16 - 9 * (i % 2), lab, fontsize=8, color=c)
            ax.axvline(marks[a], color=c, linewidth=1.0)
    p = out / "mix_replay_pause.png"
    fig.savefig(p, bbox_inches="tight")
    plt.close(fig)
    print(f"[m2_report] wrote {p}")
    return {"marks_s": marks}


def room_tone_report(out: Path) -> dict:
    fig, ax = plt.subplots(figsize=(16, 6), dpi=110)
    res = {}
    for venue, c in (("divebar", "#e6194b"), ("testroom", "#4363d8")):
        p = STEMS / f"{venue}_ambience.wav"
        meta = read_meta(STEMS / f"{venue}_meta.txt")
        if not p.exists() or not meta:
            continue
        x, fs = read_wav(p)
        seg = mono(x)  # the Ambience submix: the venue's room-tone layers only (the table and foley have their own submixes)
        f, d = welch_db(seg, fs, nfft=8192)
        keep = f >= 20
        ax.plot(f[keep], d[keep], color=c, linewidth=0.5, alpha=0.45)
        # 1/6-octave smoothing for the shape.
        p_lin = 10.0 ** (d / 10.0)
        sm = np.empty_like(d)
        for i, fc in enumerate(f):
            lo, hi = fc * 2 ** (-1 / 12), fc * 2 ** (1 / 12)
            j0, j1 = np.searchsorted(f, lo), max(np.searchsorted(f, hi), np.searchsorted(f, lo) + 1)
            sm[i] = 10.0 * np.log10(np.mean(p_lin[j0:j1]) + 1e-30)
        ax.plot(f[keep], sm[keep], color=c, linewidth=1.8, label=f"{venue} ambience stem ({len(seg) / fs:.1f} s; thin: 5.9 Hz bins, thick: 1/6 octave)")
        res[venue] = float(10 * np.log10(np.mean(seg ** 2) + 1e-30))
    ax.set_xscale("log")
    ax.set_xlim(20, 24000)
    ax.set_ylim(-190, -75)
    ax.set_xlabel("Hz")
    ax.set_ylabel("PSD [dBFS / Hz]")
    ax.set_title("room tone per venue (synthesised layers: test room HVAC bed; dive bar HVAC, cooler compressors, neon hum)", loc="left", fontsize=12)
    ax.legend(fontsize=9, frameon=False)
    style_axes(ax)
    p = out / "room_tone.png"
    fig.savefig(p, bbox_inches="tight")
    plt.close(fig)
    print(f"[m2_report] wrote {p}")
    return res


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "Docs/images/dev/m2c"))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    summary = {}
    for venue in ("divebar", "testroom"):
        summary[venue] = break_report(venue, out)
    summary["au0"] = au0_report(out)
    summary["mix"] = mix_report(out)
    summary["room_tone_dbfs"] = room_tone_report(out)
    for k, v in summary.items():
        print(f"[m2_report] {k}: {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
