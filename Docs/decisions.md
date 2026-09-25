# Product decisions

| Date | Topic | Decision |
|---|---|---|
| 2026-09-25 | Name | **RAW BREAK** (runner-ups: Point of Cue, Down the Cue, Last Rack). Formal DPMA/EUIPO/USPTO trademark check before Steam launch (WWE holds "RAW" marks). |
| 2026-09-25 | Engine | Unreal Engine 5.8, C++; physics + rules in the engine-agnostic `BilliardsCore`. |
| 2026-09-25 | Venues | American dive bar first (7-ft coin-op table, oversized/heavier bar cue ball, neon); German Kneipe later as its own venue; then pool hall and arena. |
| 2026-09-25 | Camera | Default look "natural eyes" (natural FOV, DOF while aiming, realistic exposure). "Headcam" (wide angle, lens distortion, sensor noise — Bodycam-like) as a menu option. |
| 2026-09-25 | Multiplayer | Singleplayer first: career vs AI + local hot-seat. Online later (the deterministic core keeps that possible). |
| 2026-09-25 | Brands | Fictional brands only (cues, tables, beer, venues). |
| 2026-09-25 | Rules baseline | WPA World Standardized Rules (2025-09-15) for 8-, 9-, 10-ball and 14.1; league/bar/blackball variants as config switches. |
| 2026-09-25 | Trailer | Claude owns the launch trailer end-to-end (concept, shots, edit, sound); plan + required capture tech in [trailer-plan.md](trailer-plan.md). |
| 2026-09-26 | Human factors | Intentional, visible-cause imperfections per [human-factors.md](specs/human-factors.md). Alcohol cosmetic only; skill numbers hidden (progression is felt, not shown); no fixed noise bags (deterministic redraw scheme); hot-seat guests at 50 in every attribute; chores Full on the first visit of a venue, Brisk afterwards; low-deflection shaft is a disclosed physical trade-off. |
| 2026-09-26 | Money games | Hustling and side bets with in-game cash are in (no real money). Accepts a possible "simulated gambling" content descriptor. |
