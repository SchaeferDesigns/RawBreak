# Python reference oracles

Throwaway research scripts written while producing and verifying `Docs/specs/*.md`. They compute the
expected values quoted in the specs' test tables (motion, cue strike, ball-ball throw, Mathavan/Han cushion
models, pocket geometry, compliant break solver, rail-speed calibration, rules/rack geometry).

- Plain Python 3 + NumPy; not part of the game build.
- Use them to regenerate or extend expected values when porting a test to `Tests/Core`.
- Third-party code (pooltool, FooBillard) is intentionally **not** included here; pooltool (Apache-2.0) may be
  installed separately as a cross-check oracle, but its sources are not vendored.


Subfolder `human-factors/`: oracles for `Docs/specs/human-factors.md` (stroke model, noise, chalk, tilted-table closed form, routine-shot budgets).
`streak.py` is the streak guard of the per-shot draws (product-owner decision Q1, human-factors 3.2; v1.3: exact-integer fallback, with the
v1.2 floating-point counter-example); `recompute_v12.py` feeds it
into the unchanged stroke / budget oracles and prints every draw-dependent expected value of human-factors v1.2 (HF-T03, T05-T07,
S01, S02, S04-S06, the 3.10 table). `noise.py` keeps the v1.1 bags only for reference.
