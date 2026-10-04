#!/usr/bin/env python3
"""Room impulse responses of the venues, synthesised from the room model (Docs/specs/audio.md 6.4, venue-dive-bar 10; M2-C).

Every venue gets one stereo IR for the convolution reverb submix SUBM_RB_Reverb_<Venue> (rb_make_audio.py imports it into
/Game/Generated/Audio/IR/IR_RB_<Venue>). The IR is the room's REFLECTED sound only (the dry voices carry the direct sound):

  * early reflections: image sources of the shoebox room up to order 3 (Allen & Berkley), per octave band with the
    reflection factor sqrt(1 - alpha) of every wall the path touches, amplitude 1 / r (a source of unit pressure at 1 m),
    a fractional delay (windowed sinc), for two receivers +-8.75 cm apart (the ears: natural interaural delays);
  * late tail: decorrelated Gaussian noise per ear and band with the band's RT60 (Sabine, from the surface absorption and the
    furnishing of the room, audio.md 6.4), faded in over the mixing time sqrt(V) ms;
  * level: in every band the total reflected energy equals the diffuse field of the room relative to the direct sound at 1 m,
    E_rev / E_dir(1 m) = 16 pi / (Q A), A = 0.161 V / T60 (Sabine), Q = 1 (a ball radiates into the whole room). The engine
    sends the voices' signal referred to 1 m (RefDistance x the voice output), the IR asset uses normalisation 0 dB.
Bands: three crossover bands (< 250 Hz, 250 Hz-2.5 kHz, > 2.5 kHz; linear-phase complementary crossovers) carry the low /
mid / high RT60 of the spec. Deterministic (fixed seeds).

  Tools/xref/.venv/Scripts/python.exe Tools/audio/ir_synth.py            # all venues -> Tools/audio/out/ir/IR_RB_<Venue>.wav + .json
  Tools/xref/.venv/Scripts/python.exe Tools/audio/ir_synth.py --check    # prints the measured RT60 per band (Schroeder) and levels

Outputs 32-bit float stereo WAVs at 48 kHz: Tools/audio/out/ir/ (scratch), with --ref into Tools/audio/out/ref/ (committed, LFS;
Tools/unreal/editor/rb_make_audio.py imports those, so the Unreal pipeline never needs numpy).
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "out", "ir")
FS = 48000
C0 = 343.2
EAR_HALF = 0.0875
BAND_EDGES = (250.0, 2500.0)   # low | mid | high crossovers [Hz]

# Venue models (metres, the venue's own frame). Absorption per band (low, mid, high); "extra" = furnishing / people as an
# absorption area [m^2 Sabine] per band. The tail RT60 is the spec's target; the surfaces are set so Sabine agrees with it
# (checked in --check); the image sources use the surface values.
VENUES = {
    "DiveBar": dict(
        # venue-dive-bar 2.2: main room 16.46 x 7.32 x 2.74 m; the source is the 7-ft table's centre (V 13.759, 5.427, bed 0.743),
        # the receiver the breaker's head at the head end (0.55 m behind the head spot, eye 0.36 m above the cloth).
        size=(16.46, 7.32, 2.74),
        source=(13.759, 5.427, 0.77),
        receiver=(12.70, 5.427, 1.10),
        rt60=(0.8, 0.6, 0.5),
        # lay-in mineral-fibre tiles (ceiling), VCT on concrete (floor), painted brick / block + 1970s panelling (walls)
        surfaces=dict(floor=(0.02, 0.03, 0.03), ceiling=(0.35, 0.55, 0.65), walls=(0.08, 0.06, 0.05)),
        extra=None,           # solved: furnishing (booths, stools, the bar) takes the rest of the Sabine area
        seed=11,
    ),
    "TestRoom": dict(
        # The M1 test room (ARbTestRoom): 6.0 x 4.8 x 2.8 m centred on the table; concrete floor, painted block, acoustic panels.
        size=(6.0, 4.8, 2.8),
        source=(3.0, 2.4, 0.79),
        receiver=(1.70, 2.4, 1.12),
        rt60=(0.7, 0.55, 0.45),
        surfaces=dict(floor=(0.01, 0.02, 0.02), ceiling=(0.30, 0.45, 0.55), walls=(0.05, 0.05, 0.05)),
        extra=None,
        seed=12,
    ),
}


def sabine_area(v, rt60):
    return 0.161 * v / rt60


def band_filters(n):
    """Complementary linear-phase crossover gains on an rfft grid of n points (sum = 1): raised-cosine transitions of
    +-1/3 octave around each edge."""
    f = np.fft.rfftfreq(n, 1.0 / FS)
    gains = []
    lo_edge, hi_edge = BAND_EDGES

    def step(edge):
        # 0 below edge / 2^(1/3), 1 above edge * 2^(1/3), raised cosine in log frequency between.
        a, b = edge * 2 ** (-1 / 3), edge * 2 ** (1 / 3)
        x = np.clip((np.log(np.maximum(f, 1e-9)) - math.log(a)) / (math.log(b) - math.log(a)), 0.0, 1.0)
        return 0.5 - 0.5 * np.cos(np.pi * x)

    s1, s2 = step(lo_edge), step(hi_edge)
    gains.append(1.0 - s1)
    gains.append(s1 - s2)
    gains.append(s2)
    return gains


def image_sources(size, src, order):
    """Shoebox image sources up to `order` reflections: (position, wall hit counts per surface)."""
    lx, ly, lz = size
    out = []
    rng = range(-order, order + 1)
    for nx in rng:
        for ny in rng:
            for nz in rng:
                for px in (0, 1):
                    for py in (0, 1):
                        for pz in (0, 1):
                            x = (1 - 2 * px) * src[0] + 2 * nx * lx
                            y = (1 - 2 * py) * src[1] + 2 * ny * ly
                            z = (1 - 2 * pz) * src[2] + 2 * nz * lz
                            # reflections off each pair of walls (Allen & Berkley): |nx - px| + |nx| etc.
                            hx = abs(nx - px) + abs(nx)
                            hy = abs(ny - py) + abs(ny)
                            hz_floor = abs(nz - pz)
                            hz_ceil = abs(nz)
                            total = hx + hy + hz_floor + hz_ceil
                            if total == 0 or total > order:
                                continue
                            out.append(((x, y, z), hx + hy, hz_floor, hz_ceil))
    return out


def frac_delay_kernel(frac, taps=16):
    k = np.arange(taps) - taps // 2 + 1
    x = k - frac
    h = np.sinc(x) * (0.5 + 0.5 * np.cos(np.pi * x / (taps // 2 + 1)))
    return k, h


def synth_ir(name, cfg, length=None):
    lx, ly, lz = cfg["size"]
    vol = lx * ly * lz
    area_surf = 2 * (lx * ly + lx * lz + ly * lz)
    rt = np.array(cfg["rt60"], dtype=float)
    a_target = sabine_area(vol, rt)                      # per band
    s = cfg["surfaces"]
    a_surf = np.array(s["floor"]) * lx * ly + np.array(s["ceiling"]) * lx * ly + np.array(s["walls"]) * 2 * (lx * lz + ly * lz)
    a_extra = np.maximum(a_target - a_surf, 0.0)         # furnishing / people (the solved remainder)
    ratio = 16 * math.pi / a_target                      # E_rev / E_dir(1 m) per band (Q = 1)
    length = length or float(1.5 * rt.max())
    n = int(length * FS)
    nfft = 1 << int(math.ceil(math.log2(n + 64)))
    gains = band_filters(nfft)
    rng = np.random.default_rng(cfg["seed"])
    src = np.array(cfg["source"])
    rec = np.array(cfg["receiver"])
    ears = [rec + np.array([0.0, EAR_HALF, 0.0]), rec - np.array([0.0, EAR_HALF, 0.0])]
    images = image_sources(cfg["size"], src, 3)
    t_mix = math.sqrt(vol) * 1e-3                        # [s]
    ir = np.zeros((n, 2))
    early_energy = np.zeros((2, 3))
    for ch, ear in enumerate(ears):
        bands = np.zeros((3, nfft))
        for pos, walls, floors, ceils in images:
            r = float(np.linalg.norm(np.array(pos) - ear))
            refl = (np.sqrt(1.0 - np.array(s["walls"])) ** walls) * (np.sqrt(1.0 - np.array(s["floor"])) ** floors) \
                * (np.sqrt(1.0 - np.array(s["ceiling"])) ** ceils)
            # scattering of the furnishing: the reflections lose the extra area's share too (mean free path attenuation)
            refl = refl * np.exp(-0.5 * a_extra / max(area_surf, 1.0) * (r / (4 * vol / area_surf)))
            amp = refl / r
            d = r / C0 * FS
            i0 = int(math.floor(d))
            k, h = frac_delay_kernel(d - i0)
            idx = i0 + k
            ok = (idx >= 0) & (idx < nfft)
            for b in range(3):
                bands[b, idx[ok]] += amp[b] * h[ok]
        # late tail per band: exponential decay with the band's RT60, faded in over the mixing time
        t = np.arange(nfft) / FS
        fade = np.clip((t - 0.5 * t_mix) / (0.5 * t_mix), 0.0, 1.0) ** 2
        tails = []
        for b in range(3):
            noise = rng.standard_normal(nfft)
            spec = np.fft.rfft(noise) * gains[b]
            band_noise = np.fft.irfft(spec, nfft)
            env = np.exp(-6.907755 * t / rt[b]) * fade
            tails.append(band_noise * env)
        # band-limit the early part and scale the tail so the band's total reflected energy = the diffuse-field ratio
        for b in range(3):
            eb = np.fft.irfft(np.fft.rfft(bands[b]) * gains[b], nfft)
            e_early = float(np.sum(eb[:n] ** 2))
            early_energy[ch, b] = e_early
            # the band's share of a unit-energy direct sound: the crossover band's fraction of white noise energy
            share = float(np.mean(gains[b] ** 2) / sum(np.mean(g ** 2) for g in gains))
            want = ratio[b] * share
            e_tail = float(np.sum(tails[b][:n] ** 2))
            scale = math.sqrt(max(want - e_early, 0.25 * want) / max(e_tail, 1e-30))
            ir[:, ch] += eb[:n] + scale * tails[b][:n]
    # soft end fade (the last 10 %)
    m = int(0.1 * n)
    ir[-m:, :] *= np.linspace(1.0, 0.0, m)[:, None] ** 2
    meta = dict(venue=name, fs=FS, frames=n, channels=2, volume_m3=vol, rt60_target=list(rt), sabine_area_m2=list(a_target),
                surface_area_m2=list(a_surf), furnishing_area_m2=list(a_extra), diffuse_ratio_1m=list(ratio), mixing_time_s=t_mix,
                image_sources=len(images), early_energy=early_energy.tolist(), source=list(cfg["source"]), receiver=list(cfg["receiver"]),
                description="Reflected sound of the venue for a source of unit pressure at 1 m (Tools/audio/ir_synth.py); normalisation 0 dB.")
    return ir, meta


def schroeder_rt60(x, fs=FS, lo_db=-5.0, hi_db=-25.0):
    e = np.cumsum((x ** 2)[::-1])[::-1]
    l = 10 * np.log10(np.maximum(e / e[0], 1e-30))
    i5 = int(np.argmax(l <= lo_db))
    i25 = int(np.argmax(l <= hi_db))
    return 60.0 / (lo_db - hi_db) * (i25 - i5) / fs if i25 > i5 else 0.0


def write_wav_float(path, x):
    x = np.asarray(x, dtype="<f4")
    ch = x.shape[1] if x.ndim > 1 else 1
    data = x.tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 3, ch, FS, FS * ch * 4, ch * 4, 32))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--venue", default="", help="one venue (default: all)")
    ap.add_argument("--check", action="store_true", help="print the measured band RT60 (Schroeder) and levels")
    ap.add_argument("--ref", action="store_true", help="write into Tools/audio/out/ref (the committed IRs rb_make_audio.py imports)")
    a = ap.parse_args()
    out_dir = os.path.join(HERE, "out", "ref") if a.ref else OUT_DIR
    os.makedirs(out_dir, exist_ok=True)
    for name, cfg in VENUES.items():
        if a.venue and a.venue != name:
            continue
        ir, meta = synth_ir(name, cfg)
        gains = band_filters(1 << int(math.ceil(math.log2(len(ir)))))
        measured = []
        late = []
        nfft = len(gains[0]) * 2 - 2
        for b in range(3):
            xb = np.fft.irfft(np.fft.rfft(ir[:, 0], nfft) * gains[b], nfft)[: len(ir)]
            measured.append(round(schroeder_rt60(xb), 3))
            late.append(round(schroeder_rt60(xb, lo_db=-15.0, hi_db=-35.0), 3))
        meta["rt60_measured_schroeder"] = measured      # T20 from -5 dB: includes the (faster) early decay
        meta["rt60_measured_late"] = late               # -15 ... -35 dB: the diffuse tail
        meta["energy_total"] = float(np.sum(ir[:, 0] ** 2))
        wav = os.path.join(out_dir, f"IR_RB_{name}.wav")
        write_wav_float(wav, ir)
        with open(os.path.join(out_dir, f"IR_RB_{name}.json"), "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=1)
        print(f"{name}: {meta['frames']} frames, RT60 target {[float(x) for x in meta['rt60_target']]} T20 {measured} late {late}, reflected energy "
              f"{meta['energy_total']:.3f} (diffuse ratio mid {meta['diffuse_ratio_1m'][1]:.3f}), furnishing {np.round(meta['furnishing_area_m2'], 1)} m^2 "
              f"-> {wav}")
        if a.check:
            peak = float(np.max(np.abs(ir)))
            print(f"  peak {peak:.4f}, first arrival {int(np.argmax(np.abs(ir[:, 0]) > peak * 0.05)) / FS * 1e3:.2f} ms")


if __name__ == "__main__":
    main()
