# Tools/audio — physics-driven impact synthesis (prototype)

`click_synth.py` is the reference implementation of the table sounds specified in
[Docs/specs/audio.md](../../Docs/specs/audio.md) (section 3). It renders calibrated WAVs of ball-ball clicks and
cushion thuds from the same contact law the simulator uses (Hertz + Tsuji, `K = 8.0587e8 N/m^1.5`, `e = 0.95`), radiates
them with the exact sound field of a vibrating sphere (rigid-body "acceleration noise" plus Lamb's elastic modes), and
renders whole shots from an `rbsim` event log with sub-sample scheduling. It also holds:

* `runtime_render` — the exact time-domain algorithm the C++ DSP implements (audio.md 3.6), with golden vectors;
* the presentation stage of the mix (audio.md 4.2): plan-time gain envelope per dynamic-range mode (Wide / Normal /
  Night), ITU-R BS.1770-4 loudness and true-peak metering, LAFmax (sound-level-meter reading) of clicks.

## Run (repo root, Windows)

```bash
Tools/xref/.venv/Scripts/python.exe Tools/audio/click_synth.py                 # selftest + standard set + breaks (~1.5-2 min)
Tools/xref/.venv/Scripts/python.exe Tools/audio/click_synth.py --selftest      # numeric checks only (~5 s)
Tools/xref/.venv/Scripts/python.exe Tools/audio/click_synth.py --break Tools/rbsim/examples/break9.json
Tools/xref/.venv/Scripts/python.exe Tools/audio/click_synth.py --analyze my_recording.wav --full-scale-pa 1
```

Needs numpy + scipy only (the pooltool venv of `Tools/xref` has both). The dive-bar break is simulated with
`build/Tools/rbsim/<cfg>/rbsim.exe` the first time (arguments in `DIVEBAR_BREAK_ARGS`). `--analyze` prints peak, SEL,
LAFmax, spectral centroid/peak/band, decay times, true peak (dBTP) and integrated loudness (LUFS) of a WAV.

## Outputs

`Tools/audio/out/` (git-ignored, regenerated): `ballball_<v>ms_<listener>.wav`, `cushion_<v>ms.wav`,
`variant_*.wav`, `<break>_{dry,room}.wav` (physical, 100 Pa full scale), `<break>_{wide,normal,night}.wav`
(presentation of each dynamic-range mode, digital level), `analysis.json` (every metric of audio.md 3.7), `run.log`.

`Tools/audio/out/ref/` (committed, WAVs via Git LFS): the reference set for listening tests and for the C++ golden tests.

| File | Content |
|---|---|
| `ref_ballball_1ms_shooter.wav`, `ref_ballball_4ms_shooter.wav` | head-on stun click, standard phenolic pair, listener down on the shot 1 m behind the collision (mono, physical, exact model) |
| `ref_cushion_2ms_barbox.wav` | 2 m/s ball into a coin-op bar-box rail, listener 1 m in front (mono, physical) |
| `ref_divebar_break8_room.wav` | 8-ball break on the 7-ft bar box (oversized cue ball, house cue, 8 m/s, 3 balls down incl. gully runs and tray clicks), breaker's ears, stochastic dive-bar tail (stereo, 8 s, physical: 100 Pa = full scale) |
| `ref_divebar_break8_wide.wav` | the same break as the game would play it in the default headphone mode *Wide* (digital level, -1.5 dBTP; no room-tone bed) |
| `spectra_ballball.svg`, `spectra_cushion.svg` | 1/6-octave energy spectra of the standard set |
| `hertz_shape_e095.json` | self-similar contact pulse table (runtime representation, audio.md 3.2) |
| `ball_kernels_{std,oversized_cb,bar_ob}_48k.json` | per-Legendre-order far-field radiation FIR kernels (runtime representation, audio.md 3.6); the C++ DSP computes them at start-up, these files check the port |
| `golden_runtime_48k.json` | golden vectors of `runtime_render` (3 cases incl. fractional start times, side-on listener, oversized cue ball) plus the decimation FIR, cloth IIR and near-field leak constants (AU-T11) |

**Calibration:** physical files: sample value = sound pressure / 100 Pa (full scale = 134 dB SPL peak); soft clicks are
quiet on purpose, raise the monitor gain rather than normalising. Presentation files (`*_wide/normal/night`): the
digital signal of that mode (0 dBFS = 114 / 106 / 100 dB SPL, audio.md 4.2).

## Parameters

All model constants are at the top of `click_synth.py` and are tagged DERIVED (from the core specs / literature) or
ESTIMATE (to be fitted against recordings, audio.md 3.8): ball material (`E` derived from the core's Hertz `K`,
`nu` 0.35, loss factor 0.015), cloth image (0.9, 8 kHz corner), contact times of the soft contacts (`CONTACTS`),
structural modal banks (`RAIL_BARBOX`, `RAIL_PRO`, `BED_MODES`, `POCKET_MODES`, `CUE_MODES`) and their radiation
corners, rolling/gully noise levels, the dive-bar tail, and the presentation modes (`PRESENTATION_MODES`).
