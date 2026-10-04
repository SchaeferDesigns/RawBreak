# AU-0 spike: sample-accurate table voices in UE 5.8.3

Owner: M2-C. Spec: `Docs/specs/audio.md` 5.1 (scheduling), 7.4 (voices), 14 (AU-T08, AU-T21); plan: `Docs/ue-architecture.md` 18.5.
Test: `RawBreak.Functional.Audio.AU0_Timing` (`Source/RawBreakEditor/Private/Tests/RbAudioFunctionalTest.cpp`).
Recording: `Docs/audio/m2/au0_two_voices.wav` (24-bit stereo, 48 kHz). Plot: `Docs/images/dev/m2c/au0_alignment.png`.

```
python Tools/unreal/rbue.py test --filter RawBreak.Functional.Audio.AU0 --sound --extra=-muteaudio
python Tools/audio/m2_report.py          # re-plots the recording, cross-checks the delays in Python
```

## Question

Can every sound source of a table be its own `USynthComponent` voice and still be scheduled to the sample? audio.md 5.1 says
yes if (1) the generator is called once per device block, (2) `FAudioDevice::GetAudioClock()` read inside the callback is the
start of the block being rendered, and (3) the delay `L_src` between the frame a voice renders and the frame the mixer outputs
is a constant. If not, the fallback is one 8-channel generator per table (4 quadrants x 2 heights), which loses per-ball
positioning.

## Setup

| Item | Value |
|---|---|
| Engine / device | UE 5.8.3 editor, Windows WASAPI output device, `-muteaudio` (the mixer runs in real time on the device callback, the final output is muted) |
| Audio block (`DefaultEngine.ini`, audio block) | `AudioSampleRate=48000`, `AudioCallbackBufferFrameSize=512`, `AudioNumBuffersToEnqueue=2`, 96 channels, 4 source workers |
| Voice | `URbImpactVoiceComponent` (`USynthComponent` + `ISoundGenerator`): `GetDesiredNumSamplesToRenderPerCallback()` = 512 x channels, pitch 1 (no resampling), `bAlwaysPlay`, `PlayWhenSilent` virtualisation, `bIsUISound`, no priority attenuation; the block's device frame = `llround(GetAudioClock() x fs)` read in `OnGenerateAudio` |
| Clock | one `RbAudio::FShotAudioClock` shared by the voices; the first callback after `StartShot` anchors the shot in device frames (atomic), every voice renders its events at fractional device frames (overlap-add, band-limited pulses) |
| Recording | `FRbSubmixCapture` on `SUBM_RB_Table`: a submix buffer listener that stamps every buffer with its device frame and counts gaps |
| Procedure | voice A starts; voice B starts **3 game frames later**. Shot A: the same click at shot time 0.100 s in A (left test channel) and B (right). Shot B: the click at 0.1000 s in A and at 0.1000 s + 0.5104 ms in B. Then five gain probes (pan law, 1 / r, mono upmix, reverb chain) |

## Results (run of 2026-10-04 after the second review, `Saved/RbLogs/test-20261004-175732.log`; the first run 11:32 gave the same values)

| Check | Result | Requirement |
|---|---|---|
| Callbacks | 512 frames per call, 48000 Hz, one call per device block for every voice | one per block |
| Capture | 484 352 frames from device frame 159 744, **0 gaps** | no gaps |
| **AU-T21** inter-voice skew (same click, voices started 3 game frames apart) | **0.0000 samples** (C++ group delay); 0.0000 in the Python cross-check (cross-spectrum phase slope 0.5-6 kHz) | 0 samples |
| **AU-T08** 0.5104 ms in two voices | **24.4993 samples** (exact 24.4992); Python cross-check 24.4992 | 24.50 +- 0.05 |
| `L_src` (scheduled frame vs recorded frame) | **0 samples** (-0.0000) | constant |
| Anchor lead | 261 / 26 frames past LeadMin (one block) for shots A / B | >= 0 |
| Click level | peak 0.1080 = offline render x output gain 0.1525 (-3.00 dB stereo test-channel path, compensated) | as rendered |

**Gain structure of the engine** (the probes; the voices' compensation constants in `URbAudioSettings` must cancel them):

| Path | Measured | Consequence |
|---|---|---|
| positional mono voice at its reference distance, straight ahead | -6.01 dB per channel | `PanCompensation` = 2.0 (+6.02 dB) |
| 1 m -> 2 m | -6.02 dB | the custom attenuation curve is exactly `RefDistance / r` |
| positional mono 1 m to the right | L -223.7 dB / R -3.00 dB | equal-power panning (`PanningMethod=EqualPower`; the engine default is linear: a centred source 3 dB weaker than one at the side) |
| non-spatialised mono | -6.01 dB per channel | `MonoChannelUpmixMethod=EqualPower` |
| stereo test channel | -3.00 dB | `NonSpatialCompensation` = sqrt 2 |
| a table voice 2 m ahead (reference distance, reverb send, compensation as `URbTableAudioComponent` sets them) | direct +0.02 / +0.02 dB vs the physical pressure; reverb -0.96 / +0.87 dB vs the room's diffuse field | the voices are physically calibrated |
| reverb chain (send 1.0 into `SUBM_RB_Reverb_TestRoom`) | tail / click energy 1.998 vs the IR's channel-0 energy 1.807: +0.44 dB | IR normalisation 0 dB holds |

## Decisions

1. **One voice per sound source** (30 per T0 table, audio.md 6.6). The 8-channel fallback of 5.1 is not needed.
2. **512 x 2 buffers kept** (the audio block of `DefaultEngine.ini`): one generator call per 512-frame block, no capture gaps in
   any run, render cost of the dive-bar break peak 18 % of a block (AU-T19). 1024 x 1 is not needed.
3. **`L_src` = 0**: frame `g` of a voice's block is heard at device frame `BlockFrame + g`. No setting is kept for it;
   `URbAudioSettings::OutputLatencySeconds` (0.036 s = 512 x 2 / 48 kHz + the 10 ms WASAPI period + the 5 ms lookahead of
   `DYN_RB_MasterLimiter`) only moves the absolute A/V anchor, never the relative timing.
4. **Engine settings the voices need** (all in the audio block or in the voice component, found by this spike):
   * `[/Script/Engine.AudioSettings]` quality level with `MaxChannels=96`: the engine's default level allows 32 channels, fewer
     than one T0 table's 30 voices + the room tone + footsteps (a voice past the limit never gets a mixer source);
   * equal-power panning and mono upmix, with the compensation constants above;
   * `PlayWhenSilent` virtualisation and `bIsUISound` (the voices keep rendering while silent and while the game is paused: the
     held shot clock silences the table, the pause mix filters the world);
   * headless tests set `au.DisableAppVolume 1`: an unfocused editor plays at the unfocused volume 0 and the device then starts
     no source at all.
5. Sends: the reverb send is taken **pre distance attenuation** (the diffuse field does not fall with distance) and is scaled
   by the voice's bus volume (`SetReverbSendGain`), because the reverb submix is shared by all buses.
6. (Review) The **table voices have no reverb send**: their signal is the listener's directional one (a click heard in its
   dipole null is ~20 dB down, on its axis +4.8 dB over its radiated power). One **reverb-feed voice per table** (non-spatialised
   mono, send-only: `bEnableBaseSubmix` off) renders `FRbShotAudioPlan::ReverbFeed`, every event with the ball radiation at its
   radiated power (weights 1 / sqrt(2n + 1)), into the venue reverb (audio.md 3.6 / 6.4). Probe "table reverb feed (calibration)":
   no dry output (-205.6 dB), reverb -0.96 / +0.03 dB vs the diffuse field (L / R), the same as the positional probe, because a
   non-spatialised mono source reaches each channel at the same -6.01 dB as a centred positional one.

## Not covered

* The **null device** (no output hardware) was not measured separately: the headless runs use the WASAPI device with
  `-muteaudio`; without `--sound` (`-NoSound`) the engine tests skip with a warning. The null device drives the same mixer
  render callback from a timer thread, so the clock logic is the same; a machine without an output device should re-run
  `RawBreak.Functional.Audio.*` once.
* AU-T18 (the WASAPI device period as an output latency) is taken from the log, not measured acoustically; the owner's A/V
  offset setting (+-150 ms) covers the rest.
