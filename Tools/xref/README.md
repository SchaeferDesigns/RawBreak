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
