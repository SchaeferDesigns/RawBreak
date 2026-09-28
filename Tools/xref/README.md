# pooltool cross-check (xref)

[pooltool](https://github.com/ekiefl/pooltool) (Apache-2.0) is used **only** as an independent oracle to cross-check
BilliardsCore results (validation tests XREF-*). None of its code is copied into the game.

## Setup (once)

```bash
python -m venv Tools/xref/.venv
Tools/xref/.venv/Scripts/python.exe -m pip install "pooltool-billiards==0.6.0" --extra-index-url https://archive.panda3d.org/
```

The extra index is Panda3D's official archive; pooltool 0.6.0 pins a Panda3D development build for its (unused here)
3D viewer. The venv is git-ignored. The first simulation takes ~20 s because numba JIT-compiles pooltool's physics.

## Running XREF-01 / XREF-02 (prior-art 9.12; nightly)

```bash
cmake --build build --config Release --target rbsim
Tools/xref/.venv/Scripts/python.exe Tools/xref/xref.py --case 01 --shots 1000 --rbsim build/Tools/rbsim/Release/rbsim.exe
Tools/xref/.venv/Scripts/python.exe Tools/xref/xref.py --case 02 --shots 1000 --rbsim build/Tools/rbsim/Release/rbsim.exe
```

About 3-4 minutes per case. Options: `--show SHOT` prints both event lists and the one-step results of one shot,
`--details N` the states of the N worst-resolved contacts, `--report FILE` writes every shot and step as JSON,
`--no-steps` skips the one-step comparison, `--spacer M` sets pooltool's contact spacer.

Two comparisons (see the module docstring for the frames, the shot generator and the expected model differences):

* **whole shots**, as XREF-01 / XREF-02 specify (event sequence, event times, final positions). Shots are chaotic, so a
  difference of 1e-8 m at one contact (pooltool's spacer, the construction of a jaw) grows to millimetres within a few
  contacts; these criteria are reported but cannot be met by two correct implementations.
* **one step**: from pooltool's state of all balls after each of its events, the core must find the same next event
  (type, balls, time within 1e-6 s), and the post-contact velocities and spins are compared. This isolates motion,
  detection and resolution. Contacts with a facing are left out (a model difference: the core's facings are undercut).

Results on 2026-09-28 (WP-10): XREF-01 one step 99.97 % (time error median 1e-16 s), resolution median 9e-8 m/s
(ball-ball, pooltool's spacer) and 3e-16 m/s (cushions); XREF-02 one step 100 %, resolution differs by the documented
model differences (pooltool drops v_z after contacts; its ball-ball friction impulse is mu e v_n).
