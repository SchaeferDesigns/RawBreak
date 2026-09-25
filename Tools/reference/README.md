# Python reference oracles

Throwaway research scripts written while producing and verifying `Docs/specs/*.md`. They compute the
expected values quoted in the specs' test tables (motion, cue strike, ball-ball throw, Mathavan/Han cushion
models, pocket geometry, compliant break solver, rail-speed calibration, rules/rack geometry).

- Plain Python 3 + NumPy; not part of the game build.
- Use them to regenerate or extend expected values when porting a test to `Tests/Core`.
- Third-party code (pooltool, FooBillard) is intentionally **not** included here; pooltool (Apache-2.0) may be
  installed separately as a cross-check oracle, but its sources are not vendored.
