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

## Acceptance (18.5), run of 2026-10-04 (after the second review, see the end of this file)

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
| **T16** no clipping, dive-bar break at the breaker's ears | `Functional.Audio.RecordedBreak_DiveBar` (+ offline `Unit.Audio.AU_T16_*`) | true peak -1.93 dBTP before and after the master limiter (the limiter does not act); test room -1.16 dBTP |
| **T19** audio render time at the break's peak | `Functional.Audio.RecordedBreak_*` (+ offline `Unit.Audio.AU_T19_*`) | dive bar 27.2 % of a 10.67 ms block (CPU time of all 30 voices + the reverb feed, 0.105 s into the shot; machine at ~70 % load from other agents; 22.6 % on a quieter run), test room 16.9 % (<= 60 %); offline single thread 16.1 % CPU time, densest block 0.107 s into the shot |
| Every sound class in a recorded break (event log vs detected onsets, +-1.5 ms) | `Functional.Audio.RecordedBreak_DiveBar` | TipStrike 1/1, BallBall 39/41, BallSlate 2/3, BallCushion 25/25, BallJaw 1/1, BallLiner 2/2, PocketDrop 3/3, TrapClick 3/3; 76 of 79 impacts own an onset (the other 3 lie within 2 ms of a louder impact and share its onset: two in the break cluster, one 0.9 ms after a cushion hit), median timing error -0.014 ms |
| | `Functional.Audio.RecordedBreak_TestRoom` | TipStrike 1/1, BallBall 24/24, BallSlate 1/2, BallCushion 29/29, BallLiner 1/1, PocketDrop 1/1; 57 of 58, median -0.036 ms |
| Rolling / sliding on the cloth (AU-22/23) | both breaks | rolling band 1-2 s -36.8 dBFS (dive bar), gone after the balls stop |
| Coin-op gully runs (AU-36) | dive bar | 3 of 3 runs heard (120-900 Hz band 6-9 dB over the level before the drop) |
| Footsteps (AU-65, `OnFootstep` path) | both breaks | 3 of 3 heard in the Foley stem at their frames |
| Loose ball on the floor (AU-25, `OnImpact` / `OnRolling` / `OnReturned`) | both breaks | floor hit heard; floor rolling -73 dBFS (200-3000 Hz), stops at the pick-up |
| Room tone per venue | both breaks | dive bar 38.9 dB(A) at the listener (predicted 38.8; layers: HVAC bed, 2 diffusers, 2 cooler compressors, 3 neon signs at their default positions: the recorded breaks run in the test-room level); test room 36.1 dB(A) (direct part, the Ambience slider at 0.8). In `L_DiveBar` the layers sit at the level's `RbAudio_*` anchors (review 2: diffusers at RoomTone / RoomTone2, coolers at CoolerCompressor1 / 2, neon at N3 / N4 / N5 plus the window signs N1 / N2; `Unit.Audio.Venue_AmbienceAnchors`) |
| Convolution reverb per venue | both breaks | driven by each table's reverb feed at the radiated power (review): reverb return of the break 6.1 dB under the dry table stem in the dive bar (330 m3; 2.8 dB before the review, when the breaker's on-axis click fed the room +4.8 dB over its radiated power), 0.4 dB under it in the small test room (81 m3); RT60 of the probe tail 0.46 s (dive bar IR: 0.80 / 0.58 / 0.48 s low / mid / high, `Tools/audio/out/ref/IR_RB_DiveBar.json`); AU-0 calibration of the feed: reverb -0.96 / +0.09 dB vs the diffuse field, no dry output |
| Volumes | `Functional.Audio.MixReplayPauseVolumes` | Table 0.5: the shot -12.05 dB in the Table stem, -12.04 dB in its reverb return, -12.04 dB in the master; Ambience 0.8 -> 0.5: room tone -8.6 dB, its reverb return -8.0 dB (want -8.16) |
| Replay mix and slow motion | same | ambience -10.08 dB during a replay; x0.25 film style: impacts 3.99999 x farther apart, the click -0.08 samples from its frame (run-to-run +-0.25: two moving ball voices, see the test); rolling / sliding / gully noise one octave lower at x0.25 at the same level (`Unit.Audio.Dsp.VoiceRenderer_SlowMotionRolling`) |
| Pause | same | the held shot is silent (-255 dBFS) without a click (largest step 6e-4 vs 0.15 while running) and resumes; pause mix: World -13.1 dB and low-passed (centroid 570 -> 128 Hz), released over its 0.3 s (audio.md 7.3; review 2) |

## Recordings (Git LFS, 24-bit, 48 kHz, stereo)

| File | Content | `click_synth.py --analyze` (`--full-scale-pa 1`) |
|---|---|---|
| `au0_two_voices.wav` | AU-0: voice A left, voice B right; shot A (same click), shot B (0.5104 ms apart), then the gain probes | - |
| `divebar_break_master.wav` | live 8-ball break on the 7-ft bar box in the dive-bar acoustics at the breaker's ears; footsteps at 2.0 / 2.55 / 3.1 s, a loose ball hits the floor at 3.6 s and rolls at 9.3-10.1 s | true peak -1.93 dBTP, -32.4 LUFS (Wide mode, empty bar; audio.md target -29 LUFS incl. a 76 dBA crowd; -30.5 before the review's radiated-power reverb), centroid 661 Hz |
| `testroom_break_master.wav` | the same for the 9-ball break on the 9-ft table in the test room | -1.16 dBTP, -29.8 LUFS, centroid 870 Hz |
| `mix_replay_pause_master.wav` | live shot, sliders at 0.5, x0.25 replay, a shot paused and resumed, the pause mix | -1.31 dBTP, -29.7 LUFS |

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

## Review (2026-10-04, M2-C reviewer)

Fixed in `m2c-reviewed` (every item with a test):

| # | Finding | Fix | Test |
|---|---|---|---|
| R1 | The table voices sent their **directional** signal to the reverb: the breaker's on-axis click fed the room +4.8 dB over its radiated power, a thin cut heard side-on (dipole null, floor -20 dB) -15 dB under it, a 20 dB swing of the room's level by shot geometry. audio.md 3.6 / 6.4: sends at the radiated power. | `FRbShotAudioPlan::ReverbFeed` (every voice event, ball radiation with the power weights 1 / sqrt(2n + 1), no near field, no image; banks and continuous layers as on their voices; the stem's presentation envelope) rendered by one send-only, non-spatialised voice per table (`URbTableAudioComponent::GetReverbFeedVoice`); the table voices have no reverb send. One more voice per table (31 per T0 table, 5 per T1, 2 per T2). | `Unit.Audio.Plan_ReverbFeed_RadiatedPower` (weights, listener independence: feed energy -0.6 dB between the breaker and a listener in the first click's null, voices >= 10 dB apart), AU-0 probe "table reverb feed (calibration)", the volume test's reverb return |
| R2 | A loose ball's floor hit designed a new radiation-kernel set **on the game thread** (~30 ms hitch, the cold plan build of a break takes 340-380 ms for its 16 balls) for every mass value it was given (`MassKg` of the physics body: 0.17, 0.163, a float-rounded 0.170097), and grew the exact-key cache with each. | `RbAudioLive::LooseBallAcoustics`: the table's ball (`FRbAudioPlanBuilder::BallAcoustics`, prewarmed with the voices); `RbAudioLive::Prewarm` (floor contact shapes, the standard ball) from the plan prewarm. A warm floor hit renders in 0.02-0.04 ms. | `Unit.Audio.Live_FloorHitFootstep` (same cached kernels) |
| R3 | A replay on the voice that played the live shot continued the live shot's noise sequence: not "rendered identically" (18.5); AU-T13 only compared fresh voices. | The noise of every silent continuous kind restarts from the plan's seed when a plan starts. | `Unit.Audio.Dsp.VoiceRenderer_ReplayOnSameVoice` (bit-identical to a fresh voice) |
| R4 | Per-frame heap allocations: every `OnRolling` sample (every frame per rolling loose ball) and every mix-ramp frame queued a `TUniqueFunction` command; a live PCM was copied on the render thread. | Live rolling and live output gain are atomics read once per block; the PCM buffer is moved into the renderer. | existing loose-ball / mix checks (engine) |
| R5 | audio.md 5.2 film style: the rolling noise kept its full brightness in slow motion (a stretched rumble). | `FShapedNoise::SetPitch`: the band moves by s^0.5 (x0.25: an octave down) at the same RMS, filter state kept. | `Unit.Audio.Dsp.VoiceRenderer_SlowMotionRolling` (zero-crossing ratio 0.519, level -0.00 dB) |
| R6 | A venue change (`SetVenue`) during a shot left the table voices on the old reverb for good. | `SetRouting` marks the routing dirty; the voices are created again as soon as the table is quiet. | - |
| R7 | The footstep surface trace could hit the walker's own capsule / body. | The walker is ignored by the trace. | - |
| R8 | The slow-motion sub-sample check (+-0.25 samples) measured +0.22 in one run and -0.04 in the next with the same code: the click is two moving ball voices mixed with the pan gains of wherever they stand. | Bound +-0.5 samples with the reason in the test; AU-0 keeps the 0.05-sample scheduling proof. | - |

Open (not changed; for the owner / later packages): T1 rail banks are not reduced to 3 modes and T2 does not merge events < 5 ms
(audio.md 6.6: CPU only, the budget holds); the replay mix has no 4 kHz World low-pass (7.3); `URbAudioSettings::PauseLowPassHz` is
not read (the 800 Hz lives in the asset `FLT_RB_PauseLowPass`); loose-ball floor hits still send their directional signal to the
reverb (rare, short); session loudness of the empty dive bar -32.4 LUFS vs the -29 target with a crowd (the radiated-power reverb
is 3-5 dB lower than before; `ReverbSendScale` is the knob after the owner's listening test).

## Review 2 (2026-10-04, second M2-C reviewer, on top of R1-R8)

| # | Finding | Fix | Test |
|---|---|---|---|
| R9 | `DecimationFir` (the 4x decimation FIR of every runtime pulse) is looked up by the **audio render thread for every impact it renders** (`RuntimeForceHertz` / `RuntimeForceSine`), while the plan workers do the same from up to 8 threads: it was a `TMap` behind an `FCriticalSection` (a lock on the render thread) and returned a reference to the map's element, which moves when another rate is added (a dangling reference for a reader that holds it). | Lock-free, append-only list of entries at fixed addresses; the lock only serialises a miss (a new rate). | `Unit.Audio.Dsp.DecimationFir_StableConcurrent` (the 48 kHz reference keeps its address and taps after 22 more rates; 8 threads looking up and designing rates concurrently get complete, correct FIRs) |
| R10 | The room-tone layers never reached the dive-bar level's anchors: the subsystem looked for `RbAudio_Hvac` / `_Cooler` / `_Neon`, M2-A's generator tags `RbAudio_RoomTone`, `_RoomTone2`, `_CoolerCompressor1/2`, `_NeonN1..N5` (its `layout.json` "audio_anchors"; venue-dive-bar 10, audio.md 12.1 "neon signs N1-N5"). Every layer fell back to the profile's default position, the window signs N1 / N2 were silent. | `RbAmbienceAnchorKind` (kind by name prefix, both naming schemes) and `RbPlaceAmbience` (pure): (layer, anchor) pairs matched closest first, every designed layer keeps its level / seed / duty-cycle start at its anchor, further anchors of a kind play the nearest design with a derived seed, a kind without anchors keeps the defaults; the subsystem logs how many layers sit at anchors. | `Unit.Audio.Venue_AmbienceAnchors` (the full M2-A anchor list: diffusers at RoomTone2 / RoomTone, coolers at CoolerCompressor1 / 2, the three designed neons at N3 / N4 / N5, N1 / N2 as extra layers, 10 distinct seeds, independent of the actor order; no anchors -> defaults; one anchor -> the nearest design) |
| R11 | The mix-state ramps released 5 x faster than audio.md 7.3: the release rate used a fixed 60 dB span (the -12 dB pause mix let go in 60 ms instead of 0.3 s, the replay dip in 50 ms). | `URbAudioSubsystem::StepMixGain` (pure): the mix's whole depth in its attack time when it engages and in its release time when it lets go, linear in dB. | `Unit.Audio.VenueProfilesTiersVolumes` (-6 dB half way through the 0.2 s attack and the 0.3 s release, exactly unity at the end); engine pause mix -13.1 dB, released |
| R12 | The offline AU-T19 timed wall clock around the render **and** the 31 output arrays growing in the same block: on the shared machine it failed with 79-83 % at a quiet block 0.6 s into the shot (preemption and 31 reallocations), and even when it passed it did not measure the break cluster. | Thread CPU time (`FRbAudioRenderProfile::ThreadCpuMicros`, as the engine check) of the render calls only, outputs preallocated. | `Unit.Audio.AU_T19_OfflineRenderCost`: 16.1 % at 0.107 s (the break cluster), stable under load |
| R13 | Dead code with a wrong contract: `URbTableAudioComponent::BuildShotPlans` (public, documented "worker-safe", read the settings CDO, never set the sample rate, no caller); `TruePeakDbtp` filled a function-static FIR lazily (not thread-safe). | Removed; one-time static initialiser. | - |

Integration checks for the merge (M2-C's tests fire these paths themselves): M2-F's `OnFootstep`, M2-E's `OnImpact` / `OnRolling` /
`OnReturned` and its playback mapping (`OnPlaybackStarted` / `OnPlaybackClockChanged`), M2-A's `RbAudio_*` anchors in `L_DiveBar`
(the log line "N room-tone layers, M of them at the level's RbAudio_* anchors" should read 10 / 9).

Open after review 2: a replaced plan (and a played live PCM) is freed on the audio render thread when its last reference goes (a
few hundred KB per shot over the table's voices; deferred destruction if a render-thread profile ever shows it); the absolute A/V
anchor assumes the playback clock `FApp::GetCurrentTime()` is `FPlatformTime::Seconds()` at the frame start (the architect's
contract; a fixed-timestep / benchmark run shifts the absolute anchor, never the relative timing); `URbTableAudioComponent::SetProfile`
(T19 tests only) swaps the voices' profile pointer while they render.
