# Audio v1 (M2-C): results, recordings, plots

Plan: `Docs/ue-architecture.md` 18.5. Spec: `Docs/specs/audio.md`. AU-0 spike: [au0.md](au0.md).
Everything is synthesised at runtime from the shot's events (no samples): `Source/RawBreakAudioDsp` (engine-agnostic DSP, the
C++ port of `Tools/audio/click_synth.py` `runtime_render`) and `Source/RawBreak/*/Audio` (voices, plans, room tone, mix).

## How to reproduce

```
python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_audio.py          # submixes, limiter, pause filter, reverbs, IRs
python Tools/unreal/rbue.py test --filter RawBreak.Unit.Audio                 # offline (no device needed)
python Tools/unreal/rbue.py test --filter RawBreak.Functional.Audio --sound --extra=-muteaudio
python Tools/audio/m2_report.py                                               # -> Docs/images/dev/m2c/*.png
Tools/xref/.venv/Scripts/python.exe Tools/audio/click_synth.py --analyze Docs/audio/m2/divebar_break_master.wav --full-scale-pa 1
```

Without `--sound` the four engine tests skip with a warning (no audio device under `-NoSound`), so the default full suite stays
green on any machine. A device underrun on an overloaded machine shows up as a capture gap and fails the engine tests with that
message: re-run on a quieter machine.

## Acceptance (18.5), run of 2026-10-04

| Item | Test | Result |
|---|---|---|
| AU-0 spike documented | `Functional.Audio.AU0_Timing`, [au0.md](au0.md) | one generator call per 512-frame block, `L_src` 0, 0 capture gaps; 512 x 2 kept, no 8-channel fallback |
| AU-T01..T07 (contact law, Lamb solver, radiation, causality) | `Unit.Audio.Dsp.AU_T01..T07` | pass |
| AU-T08 offline | `Unit.Audio.Dsp.AU_T08_SubSampleScheduling` | pass (24.0000 / 24.4992 / 5.0016 samples, exact and runtime) |
| **AU-T08 engine** | `Functional.Audio.AU0_Timing` | **24.4993 samples** for 0.5104 ms in two voices (24.50 +- 0.05); Python phase-slope check 24.4992 |
| **AU-T21 engine** | `Functional.Audio.AU0_Timing` | **0.0000 samples** skew, the second voice started 3 game frames later |
| AU-T09, T10, T11 (level law, runtime vs exact, golden vectors <= -100 dB) | `Unit.Audio.Dsp.AU_T09..T11` | pass |
| AU-T12 event coverage of `divebar_break8.json` | `Unit.Audio.AU_T12_EventCoverage_DiveBarBreak8` | pass |
| AU-T13 replay determinism (bit-identical) | `Unit.Audio.AU_T13_ReplayDeterminism` | pass |
| **T16** no clipping, dive-bar break at the breaker's ears | `Functional.Audio.RecordedBreak_DiveBar` (+ offline `Unit.Audio.AU_T16_*`) | true peak -1.95 dBTP before and after the master limiter (the limiter does not act); test room -1.03 dBTP |
| **T19** audio render time at the break's peak | `Functional.Audio.RecordedBreak_*` (+ offline `Unit.Audio.AU_T19_*`) | dive bar 20.8 % of a 10.67 ms block (CPU time of all 30 voices, 0.105 s into the shot), test room 13.6 % (<= 60 %) |
| Every sound class in a recorded break (event log vs detected onsets, +-1.5 ms) | `Functional.Audio.RecordedBreak_DiveBar` | TipStrike 1/1, BallBall 39/41, BallSlate 2/3, BallCushion 25/25, BallJaw 1/1, BallLiner 2/2, PocketDrop 3/3, TrapClick 3/3; 76 of 79 impacts own an onset (the other 3 lie within 2 ms of a louder impact and share its onset: two in the break cluster, one 0.9 ms after a cushion hit), median timing error -0.02 ms |
| | `Functional.Audio.RecordedBreak_TestRoom` | TipStrike 1/1, BallBall 24/24, BallSlate 1/2, BallCushion 29/29, BallLiner 1/1, PocketDrop 1/1; 57 of 58, median -0.04 ms |
| Rolling / sliding on the cloth (AU-22/23) | both breaks | rolling band 1-2 s -36.8 dBFS (dive bar), gone after the balls stop |
| Coin-op gully runs (AU-36) | dive bar | 3 of 3 runs heard (120-900 Hz band 6-9 dB over the level before the drop) |
| Footsteps (AU-65, `OnFootstep` path) | both breaks | 3 of 3 heard in the Foley stem at their frames |
| Loose ball on the floor (AU-25, `OnImpact` / `OnRolling` / `OnReturned`) | both breaks | floor hit heard; floor rolling -73 dBFS (200-3000 Hz), stops at the pick-up |
| Room tone per venue | both breaks | dive bar 38.9 dB(A) at the listener (layers: HVAC bed, 2 diffusers, 2 cooler compressors, 3 neon signs); test room 36.1 dB(A) (direct part, the Ambience slider at 0.8) |
| Convolution reverb per venue | both breaks | reverb return of the break 2.8 dB under the dry table stem in the dive bar (330 m3), 3.3 dB over it in the small test room (81 m3); RT60 of the probe tail 0.47 s (dive bar IR: 0.80 / 0.58 / 0.48 s low / mid / high, `Tools/audio/out/ref/IR_RB_DiveBar.json`) |
| Volumes | `Functional.Audio.MixReplayPauseVolumes` | Table 0.5: the shot -12.0 dB in the Table stem, -12.3 dB in its reverb return, -12.2 dB in the master; Ambience 0.8 -> 0.5: room tone -8.7 dB, its reverb return -8.0 dB (want -8.16) |
| Replay mix and slow motion | same | ambience -10.2 dB during a replay; x0.25 film style: impacts 4.00000 x farther apart, the click within 0.05 samples of its frame |
| Pause | same | the held shot is silent (-256 dBFS) without a click (largest step 5e-4 vs 0.15 while running) and resumes; pause mix: World -13 dB and low-passed (centroid 556 -> 127 Hz), released after |

## Recordings (Git LFS, 24-bit, 48 kHz, stereo)

| File | Content | `click_synth.py --analyze` (`--full-scale-pa 1`) |
|---|---|---|
| `au0_two_voices.wav` | AU-0: voice A left, voice B right; shot A (same click), shot B (0.5104 ms apart), then the gain probes | - |
| `divebar_break_master.wav` | live 8-ball break on the 7-ft bar box in the dive-bar acoustics at the breaker's ears; footsteps at 2.0 / 2.55 / 3.1 s, a loose ball hits the floor at 3.6 s and rolls at 9.3-10.1 s | true peak -1.79 dBTP, -30.5 LUFS (Wide mode, empty bar; audio.md target -29 LUFS incl. a 76 dBA crowd), centroid 653 Hz |
| `testroom_break_master.wav` | the same for the 9-ball break on the 9-ft table in the test room | -0.95 dBTP, -26.3 LUFS, centroid 870 Hz |
| `mix_replay_pause_master.wav` | live shot, sliders at 0.5, x0.25 replay, a shot paused and resumed, the pause mix | -1.28 dBTP, -26.5 LUFS |

The stems (Table, Foley, Ambience, Reverb, World as float WAVs) and the event logs (`<venue>_events.csv`: every planned impact
with its scheduled frame and the nearest detected onset) stay local in `Saved/RbAudio/m2c/`.

## Plots (`Docs/images/dev/m2c/`, made by `Tools/audio/m2_report.py`)

| File | Shows |
|---|---|
| `divebar_break.png`, `testroom_break.png` | spectrogram of the master; 20 ms levels of the stems; the event raster (every planned impact by class at its scheduled time, filled = an onset within 1.5 ms), footsteps, the loose-ball hit and roll, the gully runs |
| `divebar_break_cluster.png`, `testroom_break_cluster.png` | the first 0.5 s of the Table stem with every scheduled impact; onset detector minus schedule in samples |
| `au0_alignment.png` | the two AU-0 shots sample by sample: identical in both voices (0 samples), 24.4992 samples apart |
| `mix_replay_pause.png` | spectrogram and level of the mix run with its phases: sliders, replay, pause hold, pause mix |
| `room_tone.png` | long-term spectra of both venues' room tone (the dive bar's 120 Hz neon hum and its harmonics, the compressors) |

## Known limits of v1

* No listening test by the owner yet (audio.md Q5 / Q6); every rail, cabinet, soft-contact, rolling and room-tone level is an
  ESTIMATE of audio.md, tuned by measurement only.
* The room-tone level check reads the direct part (Ambience stem); the room tone's reverb return adds to it at the listener
  (the positional layers are far beyond the critical distance of the small, live dive bar).
* AU-T18 (acoustic A/V latency) and the null device are not measured (au0.md).
* Library layers of the spec (leather tip thock, crowd, jukebox, voices, UI sounds) are M3+; M2's tip strike, footsteps and
  floor sounds are synthesised.
