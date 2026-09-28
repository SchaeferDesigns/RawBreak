# RAW BREAK - Venue Spec: "The Low Bridge Tavern" (American dive bar, vertical slice)

| Field | Value |
|---|---|
| Spec ID | `venue-dive-bar` (prefix **VDB**) |
| File | `Docs/specs/venue-dive-bar.md` |
| Status | Draft v1.1 (2026-09-28): v1 plus the adversarial review (section 20: layout, cue-sweep numbers against the engine's real sweep parameters, volumetric-shadow and neon-proxy physics, UI/audio hand-offs, brand screening). Build spec for the vertical-slice venue. Nothing built yet. |
| Builds on | UE = [ue5-realism-plan.md](ue5-realism-plan.md) (camera 4.x, rendering 6.x, environments 7.x, audio 8.x, scalability 9.x); HF = [human-factors.md](human-factors.md) (HF-23, HF-30..32, HF-40..43, HF-50, HF-70..79, AI profiles 5.5); EQP = [equipment.md](equipment.md) (`TABLE_7FT_BAR`, ball sets 6.2/6.3, short cues 8.3, bar lighting 10.2); ARCH-UE = [../ue-architecture.md](../ue-architecture.md) (headless pipeline 9, asset paths, UE-1 table bake, UE-3 materials); [../trailer-plan.md](../trailer-plan.md); [../decisions.md](../decisions.md). |
| Consumers | Environment/art agents (Blender + Unreal generators), lighting/look-dev, audio (emitter anchors, acoustic materials; [audio.md](audio.md) 6.4-6.5), gameplay (chores, NPC spots, cue sweeps), UI (main menu "Closing Time", score slate, TV replays; [ui-ux.md](ui-ux.md) 6, 9), trailer. |
| Hard rules inherited | Fictional brands only. Self-built venue (no interior packs; Fab only as a logged last resort for a single item). Quality-first presets (Epic/Cinematic not capped to the RTX 3070 Ti). No HUD by default. Alcohol cosmetic. Every asset in `Docs/licenses/asset-ledger.csv`. Meshy never for table/balls/cue/pockets/architecture. |

Tags: **DERIVED** (computed here, method shown or script noted), **ESTIMATE** (reasoned default, tune against reference), **VERIFY** (tool/engine/licence fact to re-check in the installed 5.8 build or the current licence text), **PROPOSED** (a product decision made here that the product owner may overrule; listed again in section 18).

---

## 0. Decisions in one page

1. **Identity (PROPOSED):** *The Low Bridge Tavern*, 214 Harbor Street, Port Castor, Ohio (fictional Lake Erie rust-belt town). Open since 1958, present day (2026), **night only** for the slice. Layered decades: 1908 building, 1958 bar, 1970s paneling and drop ceiling, 1984 coin-op table, 1996 CD jukebox, 2000s neon, 2010s flat TVs. The regulars map 1:1 onto the HF 5.5 AI profiles.
2. **Room:** 16.46 x 7.32 m (54 x 24 ft) storefront + a full-width 1961 rear addition (restroom corridor, restrooms, keg cooler). Suspended ceiling at **2.74 m** (9 ft), beam soffit 2.44 m, one missing tile showing the 1908 pressed tin at 3.35 m.
3. **Table:** `TABLE_7FT_BAR` (80 x 40 in playfield, 93 x 53 in outside) in a fictional 1984 coin-op cabinet with the **oversized 60.325 mm / 221 g cue ball** (`OldBarOversizedCue`, per decisions.md) and the size-gauge cue-ball return. Cloth centre at venue (13.759, 5.427, 0.743) m, long axis along the room; `ARbTable` actor at (13.759, 5.427, 0), yaw 0.
4. **Tight on purpose (DERIVED with the engine's own sweep parameters, 2.5):** 1.22 m from the rail to the right wall (cue rack cuts it to 1.10 m), a steel column 1.04 m off the left side pocket, the jukebox 1.02 m off the head-right corner, a 1.52 m foot end where a full 58-in cue fits with the engine's full 0.30 m practice backswing once the ball is >= 0.10 m off the foot cushion (frozen: 0.22 m backswing). About **9 %** of all (cue-ball position, shot direction) pairs are shaped by the room: 5 % only need a shortened backswing (the butt taps the wall), 4 % need a short cue, a different stance or a jacked-up cue.
5. **Light is below WPA by design (DERIVED):** one 3-shade brewery-promo lamp, 3 x 1100 lm 2700 K LED bulbs (one mismatched 3000 K), shade bottom 0.86 m above the bed: **~830 lux** at the bed centre, **~210 lux** at the corner pocket points, **~115 lux** on the corner rail caps (WPA wants >= 520 lux everywhere). Room 5-40 lux. Cloth EV100 = 8.3, room views EV100 3-5: a 3-5 EV contrast drives the visible eye adaptation.
6. **Look = physical light + dense, worn, specific clutter.** Neon at measured tube luminances (red 2160, blue 2560, gold 6500 cd/m^2), cold beer-cooler glow against warm tungsten tones, green bounce from the cloth on the ceiling, dust motes only inside the lamp cone (the key bulbs cast volumetric shadows, 4.7), sticky floor sheen, burns that all pre-date Ohio's 2006 smoking ban. Three lighting states: Open, Lights-Up and AfterHours (the main menu, [ui-ux.md](ui-ux.md) 6).
7. **Production:** Blender 5.2 headless generators (architecture, furniture, table cabinet from TableSpec JSON, lathe props, neon from SVG), Unreal procedural placement from one `layout.json`, Meshy only for organic props (stools, taxidermy, cloth items, trophy figures), Higgsfield only for 2D art (brand art, decals, posters, photos), **all text rendered by our own scripts with OFL/Apache fonts** (AI images garble text), CC0 scans from Poly Haven and ambientCG as material inputs.
8. **Materials:** 8 Substrate master materials with one venue-wide `Age` slider (dive bar 0.80) plus per-asset bias; wear from baked convexity/AO masks, dust from world-up, hand polish from authored touch masks.
9. **Budget (High, RTX 3070 Ti, 1440p DLSS Q):** 90 fps target (P95 GPU <= 11.1 ms), hard floor the plan's 60 fps (P95 <= 16.7 ms), venue texture streaming <= 1.6 GB, <= 120 projected decals, <= 300 non-Nanite draws in view.
10. **Proof of realism:** a blind "real photo or game?" test at milestone DB-6 against licensed stock photos; trailer-hook cameras are fixed in the level from DB-1 on.

---

## 1. Identity: who drinks here and what the objects say

### 1.1 The bar

| Item | Value |
|---|---|
| Name | **The Low Bridge Tavern** ("the Bridge"). Named after the railroad underpass on Canal Street (clearance 10'-6"), whose LOW CLEARANCE sign was knocked down by a truck in 1983 and now hangs inside. |
| Address | 214 Harbor Street, Port Castor, Ohio (fictional; web search found neither the town nor the bar name, 2026-09-28). |
| Building | 1908 two-storey brick storefront (apartments above, not playable), single-storey rear addition from 1961. |
| Owner / bartender | **Terri Wisniewski** (58), granddaughter of the founder Stan Wisniewski (railroad brakeman, opened 1958). Holds the coin-op key (HF-71/73), racks for you if you tip, turns the fluorescents on at last call. |
| Hours in the slice | Tuesday league night and Friday money night, 20:00-02:30. Night only (PROPOSED). |
| Price | $1.50 per rack (6 quarters, HF-70). Domestic draft $3.50, "shot and a beer" $6. Cash only, ATM inside ($3.00 fee). |
| House rules (chalkboard) | 8-ball, call your pocket, winner stays, loser racks, quarters on the rail hold your spot, no jump shots, don't sit on the table, one foot on the floor. Text is generated from the active rules config (section 8.4), so the sign never contradicts the game. |

### 1.2 The regulars (NPC roster, mapped to HF 5.5)

| Character | Age | Where he/she sits | AI profile (HF 5.5) | Role |
|---|---|---|---|---|
| Eddie "Deacon" Marsh | 74 | Stool #10 (brass plate "RESERVED - DEACON - SINCE 1979") | League player skill, retired | **Mentor**: all diegetic feedback lines (HF 1.7, 3.9), vision-centre drill (HF-18). Former road player, retired machinist. |
| Big Lou Pruitt | 50s | Column shelf (W1) | Bar regular (~400) | Hits hard, **knows the table rolls toward the jukebox** (HF-50 knowledge). |
| Nina Okafor | 30s | Left-wall ledge | League player (~500) | Tuesday league captain of the "Low Bridge Bombers". |
| Sonny Castellano | 40s | Back ledge (W2), Fridays only | Local hustler (~600) | Money games, sandbagging, loose racks (HF 4.6, Q6). |
| College kids | 20s | Booths, weekends | Tourist (~250) | Loud, slow, block the dart lane. |
| Ray | 60s | Stool #3 | none (ambient) | Never plays, watches the TV, comments on nothing. |

NPC bodies need the MetaHuman step (ARCH-UE 15); until then the NPC slots exist as markers (section 2.6) and voices/footsteps only.

### 1.3 The story told by objects (the "is it real" layer)

Every object below exists for a reason a regular could tell you. Build them; they are the difference between a set and a place.

| # | Object | What it tells |
|---|---|---|
| S1 | Yellow **LOW CLEARANCE 10'-6"** road sign over the corridor opening (itself only 2.13 m / 7 ft high) | The bar's name; a joke every new customer walks under. |
| S2 | Framed black-and-white photo of Stan behind the bar, 1958; the first beer coaster framed beside it | Family business, three generations. |
| S3 | League trophies 1987-2019, dusty, on the top shelf; a hand-written card "2020 - NO SEASON" | Real time passed here. |
| S4 | Mug club: 30 ceramic mugs with names above the back bar; #17 "WALT" has a black ribbon | Community, loss. |
| S5 | Cigarette burns on the table rails, bar top edge and booth tables; nicotine-yellow ceiling tiles; "NO SMOKING - Ohio law" sign by the door; an old glass ashtray now used as the coin dish | Smoking ended in 2006-2007 (Ohio Smoke-Free Workplace Act, voter-approved 2006-11-07, in force 2006-12-07, enforced from 2007-05-03); nothing was renovated since. No tobacco products appear in the scene (PROPOSED: rating and realism). |
| S6 | The 1984 coin-op table that still takes the **big cue ball** (never converted to magnetic), laminate re-glued at one corner, "BIG LOU 8-BALL CHAMP 2019" scratched into the rail | The table is older than most players. |
| S7 | The 1996 CD jukebox the owner refuses to replace; typed title strips, one hand-written ("DEACON'S PICK") | Taste and stubbornness. |
| S8 | One dead letter in the HOLLENBECK Light neon (steady off, never flickering) | Nobody fixes things here. |
| S9 | One missing ceiling tile over the bar showing the pressed tin, old knob-and-tube wiring and dust | The building is older than the bar. |
| S10 | A band of dents and blue chalk marks on the wall at 0.80-1.00 m height exactly at the tight spots; blue chalk fingerprints around the cue rack | Years of cue butts; the tight spots are real. |
| S11 | Payphone in the corridor with "OUT OF ORDER" tape, dated 2011 in marker | Time frozen. |
| S12 | Christmas lights (C9) along the back-bar mirror, lit all year | Nobody takes them down. |
| S13 | Free popcorn machine; jar of pickled eggs with a price card | Dive-bar staples. |
| S14 | Polaroid wall of league nights (fictional people) | The regulars had a life before you arrived. |
| S15 | A folded coaster shimmed under the head-right table leg | Someone tried to level the table - and failed (HF-50 tell). |
| S16 | Layered band stickers on the jukebox, dart machine and corridor door frame, some half torn off | Decades of local bands. |
| S17 | A worn ring in the floor finish 0.4-0.9 m around the table; a dull traffic lane from door to bar to corridor | People walk the same paths for 60 years. |
| S18 | The bar TV mounted in front of the top shelf; the bottles behind it are dusty and never used | Improvised later additions. |
| S19 | "CASH ONLY - ATM INSIDE" sign; ATM with a taped "$3.00 FEE" note; the beer clock runs 10 minutes fast ("bar time", so last call is never late) | Economy and habits of the place. |
| S20 | House-rules chalkboard with the queue of names and quarters on the rail | The social contract of bar pool. |

---

## 2. Floor plan

### 2.1 Venue frame

- **Venue frame V** = UE world axes for the level: origin at the inside corner of the front (street) wall and the left (bar) wall at finished floor level; **+X into the room** (depth), **+Y to the right** as seen from the entrance, **+Z up**. Units in this spec: metres; UE = 100 x (cm), no extra rotation.
- Table mapping (ARCH-UE 4 / UE 5.6): core +x (toward the foot rail) = V +X; core +y = V **-Y**. So `RAIL_RIGHT` / `POCKET_*_RIGHT` are on the **wall side** (high Y, cue rack) and `RAIL_LEFT` / `POCKET_*_LEFT` on the **column side** (low Y).
- Cloth origin (core origin) at **V (13.759, 5.427, 0.743)**, yaw 0. `ARbTable`'s actor location is the floor point (Root = floor, `ClothOrigin` = +`TableSpec::BedHeight`, checked in `Source/RawBreak/Public/Table/RbTable.h`), so the generator places the actor at **V (13.759, 5.427, 0.000)**, yaw 0, scale 1, `Preset = SevenFootBar`, `BallSet = OldBarOversizedCue`. The bed height 0.743 m comes from `kTableSevenFootBar` and is never re-typed in the venue files (the level validator compares it, VDB-T10).
- One table here, but `layout.json` stores tables as an array (`tables[0]`, `TableIndex` 0) and nothing in the venue scripts assumes a single table or match per level (decisions 2026-09-28, multi-table halls).

### 2.2 Shell and heights

| Element | Value |
|---|---|
| Main room (interior faces) | X 0 - 16.46, Y 0 - 7.32 (54 x 24 ft, 120.5 m^2) |
| Rear addition (1961, full width) | X 16.66 - 20.12 inside (block walls 0.20 m, outer wall to X 20.32), Y 0 - 7.32: corridor Y 0.30 - 1.40 (open to the main room through E18), men's room X 16.66 - 18.24 and women's room X 18.36 - 20.12 (both Y 1.52 - 4.27), **keg cooler + ice machine** X 16.66 - 20.12, Y 4.47 - 7.12 (behind the storage door E19, not playable) |
| Walls | Front (X = 0): brick 0.33 m. Left (Y = 0) and right (Y = 7.32): brick party walls 0.33 m, no windows. Back (X = 16.46 - 16.66): painted concrete block 0.20 m. Rear addition: block. |
| Suspended ceiling | **2.74 m** (9'-0"), 2 x 4 ft lay-in tiles (0.61 x 1.22 m, long side along X), 24 mm (15/16") T-bar grid |
| Beam soffit | Along the column line Y 3.48 - 3.84, Z 2.44 - 2.74, full room depth (hides the steel beam) |
| Pressed tin (original) | Z 3.35 m, visible only through the missing tile at X 6.10 - 7.32, Y 1.22 - 1.83 |
| Rear addition ceiling | 2.44 m, 2 x 2 ft tiles |
| Columns | 114 mm (4.5 in) steel lally columns, painted black: **C1 (4.57, 3.66)**, **C2 (9.14, 3.66)**, **C3 (13.72, 3.66)** |
| Floor | 305 mm (12 in) vinyl composition tile (VCT) on concrete, top at Z = 0 |

### 2.3 Element positions (all V frame, metres; boxes are X-range x Y-range x Z-range)

| ID | Element | Position / extent | Notes |
|---|---|---|---|
| E01 | Storefront opening (glass block) | Y 0.51 - 4.77, Z 0.61 - 2.44 (21 x 9 blocks of 203 mm) | 8 in glass block infill from the 1970s; a **clear pane** Y 1.32 - 3.96, Z 1.02 - 1.63 holds the two window neons (facing the street, seen mirrored from inside). Cast-iron radiator below the sill (X 0.05 - 0.20, Y 1.5 - 2.7, Z 0.15 - 0.75). |
| E02 | Entrance door | Y 5.64 - 6.55 (0.91 x 2.13 m), steel frame, wired-glass vision panel 0.25 x 0.76 m, push bar | Transom Z 2.13 - 2.44: glass painted black with gold-leaf "LOW BRIDGE TAVERN" facing the street (mirrored from inside). Heavy velour draft curtain on a rod at X 0.15, pushed aside. Walk-off rubber mat X 0.1 - 1.3. **EXIT sign #2** (L25b) hung from the ceiling in front of the transom at X 0.35, Y 6.10, Z 2.40 - 2.58, double-faced: a bar of ~90 people is an assembly occupancy (NFPA 101), every exit is signed; it also marks the Quit station (UX 6.3). |
| E03 | ATM | X 0.45 - 0.95, Y 6.90 - 7.32, Z 0 - 1.45 | Screen faces -Y. |
| E04 | Back bar (bartender side) | X 1.83 - 9.45, Y 0 - 0.66; counter Z 0 - 0.91; mirror + 3 glass shelves Z 1.10 - 2.20; mug rack X 1.95 - 5.95, Z 2.20 - 2.60 | Under-counter 3-door cooler X 2.20 - 4.03, POS X 5.1 - 5.5, ice bin X 6.0 - 6.6, 2-door cooler X 7.30 - 8.50. TV-1 (55") on a ceiling mount at X 6.8 (screen X 6.19 - 7.41), Y 0.35, Z 1.95 - 2.66, covering part of the top shelf (S18). Neon N3 on the wall above the mirror at X 7.65 - 9.35, Z 2.20 - 2.62 (the only free wall above the mirror; the mug rack and TV-1 take the rest). |
| E05 | Bartender aisle | Y 0.66 - 1.60 (0.94 m) | Interlocking rubber anti-fatigue mats. |
| E06 | Bar counter | X 1.83 - 9.75; top Y 1.60 - 2.39 at **Z 1.07**; die (front face) at Y 2.13; padded vinyl armrest on the customer edge; brass foot rail Ø 51 mm at Y 2.30, Z 0.20 | Lift-up service flap X 9.14 - 9.75. Tap tower (6 handles) at X 5.2 on the bartender edge. Popcorn machine on the bar at X 1.9 - 2.4. |
| E07 | Bar stools #1 - #10 | Centres X = 2.30 + 0.70 i (i = 0..9, last at 8.60), Y 2.62; seat Z 0.76, seat Ø 0.38 | #10 = Deacon's stool (brass plate). Random yaw and +-3 cm offsets (section 9.2). |
| E08 | Booths B1 - B3 | Sets at X 2.13 - 4.06, 4.06 - 5.99, 5.99 - 7.92; from the right wall to Y 6.25 | Double-sided bench backs between sets. Seat Z 0.46, back top Z 1.07, seat depth 0.51; table 0.76 x 1.07 m at Z 0.76. Sconce per booth at Z 1.50 on the wall. Walleye plaque (M13) above B1, beer clock (M24) above B2 at Z 1.85 - 2.20, **Polaroid + flyer wall** (S14, M14) on the right wall X 6.00 - 8.50, Z 1.20 - 2.40 (above B3 up to the dart machine; menu Credits station S7 looks at it). Bench plinths are closed to the floor (a loose ball cannot vanish under them, 13.5). |
| E09 | Dart machine | X 8.57 - 9.23, Y 6.66 - 7.32, Z 0 - 2.13; board face at Y 7.02, **bull centre Z 1.73** (5'8") | Soft-tip: **throw line 2.44 m (8 ft)** from the board face = tape on the floor at Y 4.58, X 8.60 - 9.20. The thrower stands next to column C2. |
| E10 | Jukebox | X 10.90 - 11.70, Y 6.62 - 7.32, Z 0 - 1.47 | Front faces -Y. 1.02 m diagonally from the head-right table corner. |
| E11 | Chalkboard (rules + queue) | X 12.30 - 13.20 on the right wall, Z 1.15 - 1.90 | Chalk tray with 2 chalk sticks and an eraser. |
| E12 | Cue rack (wall) | X 13.30 - 14.30 on the right wall, cues from Z 0.25 to 1.60, protrude 0.12 m (to Y 7.20) | 8 house cues (4 x 57 in, 2 x 52 in, 1 x 48 in, 1 x 36 in), mechanical bridge on two hooks. POOL neon above (Z 1.95 - 2.40). |
| E13 | **Pool table** | Outer X 12.578 - 14.940, Y 4.754 - 6.100; noses X 12.743 - 14.775, Y 4.919 - 5.935; rail top Z 0.791 | Head end toward the bar (-X), foot end toward the back wall. Head string X 13.251, foot spot (14.267, 5.427). |
| E14 | Table lamp | Centred over the cloth, long axis along X, bulbs at X 13.299 / 13.759 / 14.219, Z 1.753; shade bottoms Z 1.603 | Deliberately 1.5 deg yaw and +2 cm Y off (section 9.2). `ARbTable::LampUndersideHeight` = **0.86 m** (shade bottom above the bed) for the physics' off-table apex check. The core also has `EnvironmentSpec::LampFootprint` (plan AABB, default infinite = the whole table is under the lamp): the real footprint in the core frame is x in [-0.650, +0.650], y in [-0.210, +0.170] m (three 0.36 m shades over 1.28 m, the +2 cm V-Y offset = -2 cm core y, the 1.5 deg yaw adds 1.7 cm at the ends). `ARbTable` does not expose it yet: hand-off H-2 (section 20). |
| E15 | Column C3 drink shelf | Round 0.40 m plywood disc clamped at Z 1.12 | Holds drinks, the coin-dish ashtray, a chalk cube; hook for the plastic triangle rack below it (Z 1.40). |
| E16 | Back ledge (drink rail) | X 16.26 - 16.46, Y 5.60 - 7.10, top Z 1.07, board 38 mm (underside Z 1.032) on steel brackets | A level cue (4-6 deg) passes under it; the rail-bridge elevation of a ball frozen to the foot cushion (~9 deg) meets it, so the foot-right quarter of the foot end is tighter than the centre (2.5). Blocks jacked-up and masse cues. TV-2 (43") above it at Y 5.9 - 6.85, Z 1.95 - 2.50, facing -X. |
| E17 | Left-wall ledge + 3 spectator stools | Ledge X 10.40 - 15.80, Y 0 - 0.25, Z 1.07; stools at X 11.2, 12.6, 14.0, Y 0.55 | Watchers face the table (HF-15 crowd term). |
| E18 | Corridor opening | Back wall Y 0.30 - 1.40, Z 0 - 2.13 (cased, no door) | S1 sign above it. The corridor runs to X 20.12. |
| E19 | Storage door "EMPLOYEES ONLY" | Back wall Y 4.55 - 5.46, 0.91 x 2.13 m, swings **into** the keg cooler, door closer, push plate (touch mask) | Keg cooler and ice machine behind (not playable). It sits behind the foot end: the bartender with kegs, ice buckets and cases passes behind a shooter at the foot end (TS-6, path P5). |
| E20 | Restroom doors | Corridor wall Y 1.52: men's X 17.00 - 17.81, women's X 18.70 - 19.51 | Closed in the slice; light leaks under the doors (section 4.2). |
| E21 | Back exit | Corridor end X 20.12, Y 0.35 - 1.25, steel door with push bar | **EXIT sign** (L25a) above at Z 2.20 - 2.38. DERIVED sight line: the sign itself is visible through the corridor opening only from Y < ~1.8 m in front of the table zone (the left-wall ledge, V08); from the table and from TH-1 the opening shows the corridor's left wall lit red by the sign, and the payphone. |
| E22 | Payphone (dead) | Corridor left wall (Y 0.30) at X 17.4, handset Z 1.35 | S11. |
| E23 | Ceiling troffers (off at night) | 4 x 2 x 4 ft at X 3.0, 7.3, 11.6, 15.2, Y 1.8 / 5.5 alternating | "Lights up" state only (section 4.6). |
| E24 | Ceiling fan (slow) | X 3.0, Y 5.2, blades Z 2.30, 4 blades, **40 rpm** (blade pass 2.7 Hz < 3 Hz) | Cast Shadow **off** on the blades: the window neons, the headlight sweeps (L35) and TV-1 all have lines of sight through the blade disc, so a shadowing fan would strobe on the ceiling (4.7). |

### 2.4 Plan (DERIVED from 2.3 by a renderer that samples the element boxes at cell centres; 1 column = 0.25 m in X, digits mark whole metres; 1 row = 0.20 m in Y)

```
Y\X    0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0 
-0.10 ###################################################################################
 0.10 #       bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb    LLLLLLLLLLLLLLLLLLLLL   ############### 
 0.30 #       bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb                            ==============# 
 0.50 #       bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb      oo    o    oo         ==============X 
 0.70 G                                            o     o               ==============X 
 0.90 G                                                                  ==============X 
 1.10 G                                                                  ==============X 
 1.30 G                                                                  ==============# 
 1.50 G                                                                  ##ddd####ddd### 
 1.70 G       BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB                           #mmmmmm#wwwwww# 
 1.90 G       BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB                           #mmmmmm#wwwwww# 
 2.10 G       BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB                           #mmmmmm#wwwwww# 
 2.30 G       BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB                           #mmmmmm#wwwwww# 
 2.50 G         o oo o  o  o  o oo o  o  o                               #mmmmmm#wwwwww# 
 2.70 G         o oo o  o  o  o oo o  o  o                               #mmmmmm#wwwwww# 
 2.90 G                                                                  #mmmmmm#wwwwww# 
 3.10 G                                                                  #mmmmmm#wwwwww# 
 3.30 G                                                                  #mmmmmm#wwwwww# 
 3.50 G                                                      c           #mmmmmm#wwwwww# 
 3.70 G                  C                 C                 Cc          #mmmmmm#wwwwww# 
 3.90 G                                                                  #mmmmmm#wwwwww# 
 4.10 G                                                                  #mmmmmm#wwwwww# 
 4.30 G                                                                  ############### 
 4.50 G                                  ---                             #ggggggggggggg# 
 4.70 G                                                                  sggggggggggggg# 
 4.90 #                                                  TTTTTTTTTT      sggggggggggggg# 
 5.10 #                                                  T........T      sggggggggggggg# 
 5.30 #                                                  T........T      sggggggggggggg# 
 5.50 #                                                  T........T      #ggggggggggggg# 
 5.70 E                                                  T........T     L#ggggggggggggg# 
 5.90 E                                                  T........T     L#ggggggggggggg# 
 6.10 E                                                  TTTTTTTTTT     L#ggggggggggggg# 
 6.30 E         HHtttHHHHHtttHHHHtttHHH                                 L#ggggggggggggg# 
 6.50 E         HHtttHHHHHtttHHHHtttHHH                                 L#ggggggggggggg# 
 6.70 #         HHtttHHHHHtttHHHHtttHHH  DDD       JJJ                  L#ggggggggggggg# 
 6.90 #  AA     HHtttHHHHHtttHHHHtttHHH  DDD       JJJ                  L#ggggggggggggg# 
 7.10 #  AA     HHtttHHHHHtttHHHHtttHHH  DDD       JJJ  kkkkKKKK         #ggggggggggggg# 
 7.30 #  AA     HHtttHHHHHtttHHHHtttHHH  DDD       JJJ  kkkkKKKK         ############### 
 7.50 ################################################################################## 
```

Legend: `#` wall, `G` glass-block storefront, `E` entrance door, `A` ATM, `b` back bar, `B` bar counter, `o` stools (bar stools #1-#10 at Y 2.62, spectator stools at Y 0.55), `H`/`t` booth benches/tables, `C` columns C1-C3 (`c` = drink shelf on C3), `D` dart machine, `-` dart throw line, `J` jukebox, `T`/`.` table rails/playfield, `k` chalkboard, `K` cue rack, `L` drink ledges (left wall and back wall), `=` corridor (and the opening E18), `d` restroom doors, `X` back exit, `s` storage door E19, `m`/`w` men's/women's rooms, `g` keg cooler. Street (Harbor St.) is left of X = 0. Regenerate it from `layout.json` in DB-1 (V10 must match it).

### 2.5 Clearances around the table (DERIVED)

**Sweep model = the engine's.** The numbers below use the parameters the game itself will sweep with, so VDB-T3 can reproduce them: `URbStrokeComponent::MaxBackswing` **0.30 m** ("also the clearance sweep's backswing"), cue tip / butt radius 6.5 / 15.9 mm with a linear taper (UE 5.5), margin 1 mm (`FRbCueClearanceInput::Margin`), the cue in 3D: tip at the contact point on the oversized cue ball (60.325 mm), elevation 4 deg for balls away from the rail and the rail-bridge elevation (cue 20 mm above the rail cap at 0.08 m past the nose, contact 15 mm above the ball centre: ~9 deg for a frozen ball) near it. Obstacles in 3D: walls, closed doors, column C3 and its shelf (Z 1.10 - 1.12, r 0.20 m), jukebox (Z 0 - 1.47), cue rack (Z 0.25 - 1.60, to Y 7.20), chalkboard, back ledge (Z 1.032 - 1.07), TV-2, lamp shades, ceiling. Body: a 0.20 m disc at 0.95 - 1.25 m behind the contact point must clear walls and furniture. Grid 5 cm over the playfield x 72 directions (5 deg) = 57,600 samples. Reproducible: the review's scratch script is summarised in section 20 (R-03); the DB-B package commits it as `Tools/blender/divebar/cue_sweep_check.py`, which reads `layout.json`.

v1 of this section used a 2D model with a 0.15 m backstroke; the same obstacles with that backstroke give 6.1 % room-shaped shots (v1 said 5.8 %), so the models agree and the difference below is the engine's longer backswing.

| Measure | Value |
|---|---|
| Foot rail outer -> back wall | **1.520 m** (nose -> wall 1.685 m). Ball on the long string at the foot end: full 58-in cue with the full 0.30 m backswing once the ball is >= 0.10 m off the foot cushion; frozen to the cushion the free backswing is **0.224 m** (the butt taps the wall on a long practice stroke). Foot-right quarter (Y > 5.60, under the back ledge): frozen balls get only 0.02 m before the elevated butt meets the ledge -> jack up over it or take the 52-in cue. |
| Right rail outer -> right wall | **1.220 m** (nose -> wall 1.385 m); **1.100 m** in front of the cue rack (X 13.30 - 14.30) |
| Left rail outer -> column C3 surface | **1.037 m** at the left side pocket (nose -> column 1.202 m) |
| Head-right corner -> jukebox corner | **1.020 m** diagonal |
| Head rail outer -> bar service flap | 2.83 m along X (open; break from the head end with a full cue) |
| Perpendicular shot toward the right wall: ball-centre distance from the right nose for a free sweep | full 0.30 m backswing: >= **0.432 m** (58-in), 0.280 m (52-in), 0.174 m (48-in); shortened 0.10 m backswing: >= 0.231 / 0.059 / 0.030 m |
| ... toward the cue rack (X 13.30 - 14.30) | full backswing: >= 0.552 / 0.400 / 0.299 m; 0.10 m backswing: >= 0.352 / 0.198 / 0.083 m |
| ... toward column C3 (only within its +-0.08 m "shadow" and the shelf's 0.20 m radius above Z 1.10) | full backswing: >= 0.615 / 0.463 / 0.362 m; 0.10 m backswing: >= 0.415 / 0.263 / 0.157 m |

The 57-in house cue is 2.5 cm shorter than the 58-in player cue; its thresholds are 2.5 cm smaller.

| Result class (all samples, first class that fits) | Share |
|---|---|
| Free: 58-in cue, full 0.30 m backswing, stance clear | 68.1 % |
| Shortened backswing (58-in fits with >= 0.10 m but not 0.30 m) | **4.9 %** |
| Needs the 52-in short cue (with >= 0.10 m backswing) | **2.7 %** |
| Needs the 48-in short cue | **1.1 %** |
| No cue fits level (jack up, different shot, or mechanical bridge from another side) | 0.3 % |
| Body blocked (cue fits, stance does not) | 0.4 % |
| Reach-limited (bridge hand > 1.00 m inside the rail outline along the cue line) | 22.6 % (29.3 % at 0.90 m reach, 16.0 % at 1.15 m) - an avatar parameter, not a venue property; handled by leaning / one leg up or the mechanical bridge. The M1 pawn has no such limit yet (`MaxReach` 1.5 m, plan distance to the cue ball), so this row matters from the MetaHuman IK on. |

58-in blocked with the full backswing, by blocker (overlapping): right wall 6.1 %, cue rack 4.1 %, column 0.7 %, jukebox 0.5 %, back wall 0.3 %, back ledge 0.2 %, C3 shelf 0.1 %. So **about 9 % of shots are shaped by the room**; of these, 5 % only need a shorter backswing (a real bar player does that without thinking) and 4 % need a short cue, another stance or a jacked-up cue - a handful per rack, the realistic bar-box experience (EQP 8.3, HF-32) without making the table a chore.

**Hand-off H-1 (UE-4 / UE-5a, section 20):** with the planned logic (UE 5.5 and the `RbCueClearance` header: one sweep that includes the full backswing, then the minimum-elevation search) a failed full-backswing sweep goes straight to "raise the elevation / offer a short cue", which would push the 4.9 % "shortened backswing" shots onto short cues. Proposal: when the 0.30 m sweep fails but a >= 0.10 m sweep passes, clamp `MaxBackswing` for this shot to the free length (a butt tap on the obstacle at the back of the stroke is the HF-32 tell; the shorter run-up limits power by itself).

### 2.6 Tight spots, waiting spots and chore anchors

| ID | Where | What happens | Hooks |
|---|---|---|---|
| TS-1 | Wall side (right long rail), whole length | Perpendicular shots: within 0.43 m of the cushion the backswing gets shorter; within 0.23 m the 58-in cue does not fit even with a 0.10 m backswing -> 52-in; within 0.06 m -> 48-in (2.5) | HF-32 short-cue offer, cue sweep (UE 5.5); wall scuff band decal S10 |
| TS-2 | In front of the cue rack (X 13.30 - 14.30) | The rack's own cues narrow the aisle to 1.10 m; butt knocks a house cue (clatter sound) | Cue sweep hits `CueRack` component -> audio + animation |
| TS-3 | Column C3 at the left side pocket | Narrow shadow: jack up, change angle, or short cue | Column drink shelf is a waiting spot (W1) - an NPC may stand in the line (HF-77) |
| TS-4 | Jukebox at the head-right corner | Diagonal shots from the head-right pocket area | Notebook: "Mind the jukebox" |
| TS-5 | The lamp | Masse / jump strokes along the long axis near the table centre put the butt into the shades (shade bottom Z 1.603, bar Z 1.88) | HF-78 lamp swing (pendulum period 2.0 s, DERIVED `T = 2 pi sqrt(0.99 m / g)`), physics off-table apex check via `LampUndersideHeight` |
| TS-6 | Foot end | Full cue with full backswing from 0.10 m off the cushion; frozen balls get a 0.22 m backswing (centre) or hit the back ledge (foot-right quarter); masse/jump hits the ledge; the bartender comes out of the storage door (E19) right behind you | Traffic path P5 (below) |
| W1 | Column C3 shelf (13.72, 3.9) | Opponent waiting spot, sometimes in your line | HF-77 |
| W2 | Back ledge (16.1, 6.4) | Hustler's spot on money nights | HF-77, HF 5.5 |
| W3 | Left-wall ledge stools | Spectators | HF-15 crowd term = min(1, watchers/10) |
| W4 | Stool #10 | Mentor | HF 3.9 lines, at most one per 3 shots |
| A1 | Coin slide, foot-end face, centre Y 5.427, Z 0.62 | Pay per rack | HF-70 (1 s per coin + 2 s) |
| A2 | Ball tray below the trap window, foot end | Balls roll out after paying | HF-71, HF-74 |
| A3 | Cue-ball return cup in the **foot-end apron**, wall-side end: Y 5.93 - 6.03, opening Z 0.38 - 0.46 (EQP 5: separate opening on the end apron; UX 9.12 prompt "at the end-apron opening"). The side-face variant of v1 (wall side at X 14.55 - 14.70) stays a generator option `cue_return = end_apron \| wall_side`; the owner's reference photos decide (17) | Scratch: walk round to the foot end (past the storage door) to fetch it | HF-72 (2-6 s) |
| A4 | Coin door (locked), wall-side face at X 14.20 - 14.45 | Bartender key: unlock for a spot or re-rack that needs a pocketed ball (HF-71), a jammed cue ball (HF-73) - she has to squeeze into the wall aisle | HF-71, HF-73; bartender path flap -> A4 ~7.4 m (~5.3 s walk at 1.4 m/s) |
| A5 | Triangle rack on the hook under the C3 shelf | Racking chore starts by fetching it | HF-74, HF-55 |
| A6 | Quarters-on-the-rail spot: right rail cap near the foot-right corner (X 14.3 - 14.6) | Queue of coin stacks, one per waiting player | HF-76 |
| A7 | Drink spots: C3 shelf, back ledge, the table's head rail (bad etiquette, a pint left there by NPCs) | Drinks near the shot | HF-79 (Later) |

Traffic paths (for NPC navigation and wear masks): **P1** door -> along the bar (Y 3.0 - 4.5) -> bar end; **P2** bar end -> between the left-wall ledge and column C3 (Y 1.0 - 3.3) -> corridor opening; **P3** bartender flap -> around the head end -> foot end (chores); **P4** booths <-> bar; **P5** bartender flap -> between the table and column C3 (the 1.04 m gap, TS-3) -> storage door E19 (kegs, ice, cases; about one trip per 20-30 min of in-game time on busy nights, ESTIMATE). Shooters on the left rail swing their butt into P2 and P5 (people have to wait or duck: realism).

---

## 3. The coin-op table

### 3.1 Cabinet (fictional "HALVERSON Stallion 7", 1984)

| Part | Spec | Production |
|---|---|---|
| Playfield (bed, cloth, cushions, facings, rails, sights, pockets) | `TABLE_7FT_BAR` from `rb::TableGeometry` via the UE-1 bake (`/Game/Generated/Tables/SevenFootBar/SM_Table_<Part>`), cloth preset `NAPPED_BAR` | EXIST (UE-1, UE-3). Never hand-modelled (pitfall 16). |
| Cabinet box | Outside 2.362 x 1.346 m; box from Z 0.30 to the rail underside; sides and ends in woodgrain laminate ("walnut", worn through at the corners), 3-groove aluminium trim bands | BL-TS (section 13.4) |
| Rail caps | 18 white plastic round sights Ø 12.7 mm; caps in black laminate with burns and ring stains; "BIG LOU" scratch on the right rail | UE-1 rail geometry + dive-bar material instance + decals |
| Corner / side castings | Black ABS, satin, chipped; openings follow the pocket geometry with >= 2 mm clearance to jaws and capture volumes (automated fit check) | BL-TS |
| Pockets | No nets: balls drop into a gully; you hear them roll to the trap (corner 1.5-3 s, cue ball to the return 2-6 s) | Audio splines (3.3) |
| Legs | 4 square pedestal legs 0.20 x 0.20 m, Z 0 - 0.30, inset 0.10 m, screw levelers Ø 50 mm; folded coaster under the head-right leg (S15) | BL |
| Coin mechanism (push chute) | Chrome plate 0.20 x 0.12 m on the foot-end face, centre Y 5.427, Z 0.62; slide with **6 quarter slots** protrudes 0.06 m; price sticker "$1.50 - 6 QUARTERS"; locked coin door 0.25 x 0.20 m on the **wall-side** face near the foot end (X 14.20 - 14.45, Z 0.40 - 0.60, key, "HALVERSON" stamp) | BL + TXT |
| Ball trap window | Clear scratched plexiglass on the foot-end face, Y 4.99 - 5.87 (the trap holds 15 balls in a row, 15 x 57 mm = 0.86 m), Z 0.44 - 0.51; you can read which balls are down (diegetic info) | BL (ESTIMATE, confirm with reference photos) |
| Ball tray | Open tray below the window, Z 0.33 - 0.42, where the balls fall when the slide is pushed | BL (ESTIMATE) |
| Cue-ball separator | Size gauge where the merged gully enters the trap row: the 60.325 mm cue ball cannot pass under the gauge bar (clear height ~58.5 mm above the channel floor, ESTIMATE) and drops down a short chute to the return cup in the foot-end apron (A3); object balls (57.00 - 57.15 mm) roll under it into the trap | Behaviour in gameplay; geometry only visible through the return cup |
| Plates and stickers | Serial plate in the trap area "MODEL ST-7 SER 84-1172"; league sticker "CASTOR VALLEY 8-BALL LEAGUE - SANCTIONED TABLE 2019"; "DO NOT SIT ON TABLE"; operator sticker "Castor Coin & Amusement - service 419-555-0143" (555-01xx is reserved for fiction) | TXT + HIGGS |

### 3.2 Balls and cloth (venue presets)

- Ball set **`OldBarOversizedCue`** (EQP 6.2/6.3): object balls sampled per venue seed (163 +- 3 g), cue ball 60.325 mm / 221 g with a small red oval "HALVERSON" stamp (TXT, analytic like the numbers). Dirty set: cling `k_venue` 1.3 (HF-41), ball roughness 0.10-0.15 with haze weight 0.3 (UE 6.2 "worn bar balls"). One mismatched 3-ball (slightly different red, S-detail "the replacement 3").
- Cloth: napped bar cloth, bar green (ESTIMATE start sRGB #2F6B40 new, faded toward #3E6F4A on the worn lanes). Pre-authored initial wear for the UE-3 wear/chalk map (UE 6.3): heavy break-spot burn, head-string lane, pilling along the long string, frayed points at all six pockets, rack triangle impression, chalk smudges near the corners.
- Table condition: `rb::human::MakeVenueTableCondition(VenueSeed, 0, VenueKind::DiveBar, FirstCareerTable, ChalkCling = false)` (checked in `rb/Human/Venue.h`: dive bar |s| U[0.5, 2.5] mm/m, first career table U[0.5, 1] mm/m, ball cling 1.3) and `BallSetSeed = VenueBallSetSeed(VenueSeed, 0)`. Pick the venue seed by search so the **downhill** direction lies within +-25 deg of the jukebox direction (from the cloth centre to the jukebox centre (11.30, 6.97): V azimuth 147.9 deg from +X toward +Y, core -147.9 deg; about 1 seed in 7 qualifies). Check it by behaviour, not by the sign convention of the slope vector (a slope of <= 2.5 mm/m cannot start a resting ball against rolling resistance mu_r ~ 0.012): roll a ball at 0.5 m/s along the long string once toward the foot and once toward the head (rbsim `--table 7ft-bar --balls oldbar --param tilt.slope_x=<s_x> --param tilt.slope_y=<s_y>` with the seeded `SeedTableSlope` values, or in the level); both must end displaced toward core -y (the wall/jukebox side), and the roll toward the foot must come up shorter (VDB-T10). Check both `FirstCareerTable` values, since only the magnitude range changes. This makes the notebook line "rolls toward the jukebox" and the coaster shim (S15) truthful.
- `ARbTable` has `BallSetSeed` but no venue seed or condition input yet (the M1 context uses a level table): hand-off H-2 (section 20).

### 3.3 Coin-op mechanics (timings, states, audio anchors)

| Step | Timing (ESTIMATE unless HF) | Anchor |
|---|---|---|
| Insert coins | 1 s per coin (HF-70); coins visibly sit in the slots | A1 |
| Push the slide | 0.6 s push (resistance ramp), clunk | A1 |
| Balls release | 1.8 - 2.5 s rumble, balls clack into the tray | A2 |
| Pocketed object ball | Drop thunk in the casting, roll along the gully 1.5 - 3 s, click into the trap row | gully splines below |
| Scratch | Cue ball rolls to the separator, diverted, 2 - 6 s to the return cup (HF-72) | A3 |
| Spot / WPA re-rack needs a pocketed ball | Bartender walks over (~5 s), unlocks the coin door with the key, reaches into the trap, hands the ball over (HF-71; 8-12 s total, ESTIMATE); the play state never waits on it (UX 9.12) | A4 |
| Game over, balls left | Loser pockets the rest by hand, 1.5 - 3 s per ball (HF-71) | playfield |
| Fault (V2) | Cue ball jammed at the gauge bar, or an object ball pushed into the cue-ball chute behind it; bartender with key (HF-73, at most one per night) | A4 |

Gully splines for 3D audio and the gully-run sound AU-36 ([audio.md](audio.md) 2.3; V frame, under the table, every segment falls, 1.5-3 deg): corner pockets -> long gullies along both long sides at Y 4.95 / 5.90, Z 0.55 -> 0.46 at X 14.70 -> cross channel at X 14.70 flowing toward +Y (Z 0.46) -> separator (14.72, 5.90, 0.455) -> trap row X 14.80 from its entry at Y 5.87 (Z 0.44) to Y 4.99 (Z 0.42), behind the window; side pockets drop straight into the long gullies; cue-ball chute separator -> return cup (14.93, 5.98, floor Z 0.38). v1 had the cup (Z 0.50) above the separator (Z 0.44), which a rolling ball cannot reach. Rolling speed 0.6 - 1.0 m/s.

---

## 4. Lighting plan (physical units)

### 4.1 Principles

- Every light has a real counterpart with a real flux, colour temperature and emitter size (UE pitfall 10: never a zero source radius).
- Emissive surfaces carry their real luminance (cd/m^2). Emissive meshes that duplicate an analytic light (bulb glass) are hidden from ray tracing (UE pitfall 9); emissive surfaces that are only *seen* (neon tubes, screens, jukebox panels) stay visible in reflections, because real balls reflect them.
- Proxy lights that stand in for a non-rectangular emitter (neon) get **Specular Scale 0** (`ULightComponent::SpecularScale`, [5.8 ✓]), otherwise the balls show a rectangular highlight that no real sign makes; their reflection comes from the emissive mesh via Lumen. No double counting: the emissive tubes must not also light the room through Lumen GI while the proxy does. Tube primitives get *Affect Dynamic Indirect Lighting* off; VERIFY in DB-2 that the HWRT hit-lighting reflections still show them. Fallback: keep them in Lumen and cut the proxy flux by the GI share measured with the proxy off.
- **Volumetric fog sees every light, through walls, unless told otherwise.** In 5.8 every local light has `VolumetricScatteringIntensity` 1.0 by default and `bCastVolumetricShadow` **false** (only directional and sky lights set it; checked in `LightComponent.cpp` / `DirectionalLightComponent.cpp`). Without the flag the table bulbs would light the haze as a sphere, above the shades as well as in the cone, and the street lights would light the fog inside through the brick. Rule (checked by VDB-T11): every light with scattering > 0 has `bCastVolumetricShadow` on; lights outside the room (L33, L34) and lights inside enclosures that are not meant to show a beam (coolers, popcorn, restroom leaks, jukebox, dart machine, TVs) have scattering 0.
- No light may change faster than 3 Hz or flash (photosensitivity; UE 4.7 "no strobing ever"). Dead neon letters are steadily off.
- Night only. Three lighting states (4.6): **Open** (default), **Lights-Up** (closing time) and **AfterHours** (the main menu, UX 6.5).

### 4.2 Light list (state Open)

| ID | Light | UE type / key settings | Photometric value | CCT | Shadows (High / Epic / Cinematic) |
|---|---|---|---|---|---|
| L1-L3 | Table lamp bulbs (A19 LED, frosted) | Point, **1100 lm** each, Source Radius 3.0 cm, inverse-square, Contact Shadow Length 0.02, Volumetric Scattering 1.0 with **Cast Volumetric Shadow on** (the shades cut the haze cone, 4.1); bulbs sit 0.15 m above the shade rim inside modelled opaque shades (white enamel interior, rho 0.8) - **no IES**, the shade geometry shapes the beam (cut-off 50.2 deg, DERIVED atan(0.18/0.15)) | 3 x 1100 lm | 2700 / 2700 / **3000** K (the mismatched replacement bulb) | VSM / VSM / VSM (+RT shadows Cinematic) |
| L4 | Lamp centre badge (backlit brand plate) | Emissive only | 150 cd/m^2 | 3500 K | - |
| L5-L8 | Bar mini pendants (black enamel cones, exposed filament bulbs) at X 3.0 / 5.0 / 7.0 / 9.0, Y 1.95, bulb Z 1.95 | Point, 300 lm, Source Radius 1.5 cm, Source Length 3 cm | 4 x 300 lm | 2200 K | on (0.5 res scale) / on / on |
| L9-L10 | Back-bar bottle backlight: LED strips under the shelves | Rect 0.02 x 3.5 m facing down/forward, Specular 1 | 2 x 1500 lm | 2700 K | off / off / on |
| L11 | 3-door back-bar cooler interior | Rect 1.6 x 0.6 m facing +Y + emissive back panel | interior seen through glass **~400 cd/m^2** | 5000 K (cold contrast) | off |
| L12 | 2-door cooler | as L11, 1.1 x 0.6 m | ~400 cd/m^2 | 5000 K | off |
| L13-L15 | Booth sconces (dusty frosted glass) at Z 1.50 | Point, 250 lm, Source Radius 3 cm | 3 x 250 lm | 2400 K | off / on / on |
| L16 | TV-1 55" (bar) | Rect 1.21 x 0.68 m, **Source Texture = the TV's media texture**: [5.8 ✓] `UMediaTexture` updates invalidate their rect-light atlas slot (`RectLightAtlas::FAtlasTextureInvalidationScope` in `MediaTexture.cpp`; scene captures do the same). A Slate render target (the replay feed of UX S4 on TV-1) does not, so while TV-1 shows a render target, colour/intensity come from the target's 1 x 1 mip at <= 10 Hz, smoothed. Volumetric scattering 0 | peak 250 cd/m^2, average ~80 cd/m^2 (-> ~200 lm) | content | off |
| L17 | TV-2 43" (pool room, back wall) | as L16, 0.95 x 0.54 m | average ~80 cd/m^2 (~130 lm) | content | off |
| L18 | Jukebox panels + title strips | Rect 0.6 x 0.9 m, Specular 0, scattering 0; emissive panels with the typical slow colour cycle (period >= 4 s, smooth, never a flash) | panels 120 cd/m^2 (amber/red/blue), title strips 200 cd/m^2; AfterHours idle: panels 30 % | 5000 K strips | off |
| L19 | Dart machine marquee + board LEDs | Rect 0.5 x 0.3 m, Specular 0, scattering 0; emissive; attract-mode LED chase <= 2 steps/s, luminance change of the whole marquee < 10 % per step | marquee 250 cd/m^2 | mixed | off |
| L20-L24 | Neon signs N1-N5 (table 4.3) | per sign: emissive tubes + one Rect proxy sized to the sign, **Specular 0**, proxy flux = tube flux / pi (4.3), Volumetric Scattering 0.5 + Cast Volumetric Shadow | see 4.3 | per gas | off / MegaLights / MegaLights |
| L25a, L25b | EXIT signs (LED): L25a over the back exit (E21), L25b double-faced at the front door (E02) | Emissive letters + Rect 0.30 x 0.15 per face, Specular 0 | letters **40 cd/m^2** (UL 924 / NFPA 101 minimum 8.6 cd/m^2; 150 mm letters, 19 mm strokes) | red | off |
| L26 | Corridor ceiling fixture ("jelly jar", CFL) | Point 800 lm, Source Radius 4 cm | 800 lm | 3000 K with a slight green tint (CFL) | on / on / on |
| L27-L28 | Light leaking under the restroom doors | Rect 0.80 x 0.01 m at the floor behind each door, scattering 0 | ~20 lm each | 4000 K | off |
| L29 | Christmas lights, 25 x C9 LED along the back-bar mirror top | emissive bulbs + 3 unshadowed points | 25 x 5 lm | multi | off |
| L30 | Popcorn machine kettle lamp | Point 400 lm inside the glass cabinet | 400 lm | 2700 K | off / on / on |
| L31 | ATM screen | emissive | 150 cd/m^2 | 6500 K | - |
| L32 | Beer clock (lit, above B2) | emissive face | 100 cd/m^2 | 3500 K | - |
| L33 | Street through the glass block (LED street light, 4000 K) | 2 Rect lights 1.0 x 1.5 m outside the storefront, Specular 0, scattering 0, + HDRI backplate card (Poly Haven `street_lamp`, blurred behind the glass) | ~10 lux on the facade (ESTIMATE, US residential street) | 4000 K | off |
| L34 | Sodium lamp across the street (seen through the door glass) | Emissive backplate element only | - | 2000 K | - |
| L35 | Passing headlights | Spot, moving, through the glass block with a **light function** (203 mm glass-block cell pattern + lens distortion), sweep 1.5-3 s, seeded intervals **20-120 s**; the glass-block mesh casts no shadow for it (the light function is the pattern), scattering 0.3 + Cast Volumetric Shadow (faint moving beams) | peak ~30 lux at the inner glass face | 4500 K | on (moving caustics on ceiling/right wall) |

### 4.3 Neon signs (DERIVED from measured tube luminances)

Tube surface luminance for 15 mm tubes at 50-60 mA (industry averages, signsofthetimes.com "Neon Visibility"): clear red 2160, standard blue 2560, ruby red 800, cobalt blue 1600, standard green 10,026, white 6500 K 6503, Noviol gold 6500 cd/m^2. Luminous flux per metre of a Lambertian cylinder: `Phi/len = pi * L * (pi * d)` -> red **320 lm/m**, blue 379, ruby 118, cobalt 237, green 1484, white 963, gold 962 lm/m.

| ID | Sign (fictional brands, section 8) | Place | Tubes | Flux (DERIVED) |
|---|---|---|---|---|
| N1 | "Old Castor" script + "LAGER" | Window clear pane, faces the street (read mirrored inside; black block-out paint on crossovers visible) | 2.4 m clear red + 1.1 m Noviol gold | 770 + 1060 lm |
| N2 | "OPEN" with border | Window clear pane, faces the street | 1.2 m clear red + 1.8 m standard blue | 380 + 680 lm |
| N3 | "HOLLENBECK Light" - **the "t" is dead** (S8) | Back-bar wall above the mirror at X 7.65 - 9.35, Z 2.20 - 2.62 (E04), faces in | 2.2 m standard blue + 1.0 m white (minus 0.12 m dead) | ~830 + 850 lm |
| N4 | "POOL" with a cue and ball | Right wall above the cue rack | 1.3 m clear red + 0.9 m Noviol gold | 420 + 870 lm |
| N5 | "Lantern Flats IPA" | Back wall between the corridor opening and the storage door, Y 2.40 - 3.40, Z 1.45 - 1.90 (above TV-2 only 0.24 m of wall is left below the ceiling); fully inside the V03 frame (upper left corner) and the TH-1 frame | 1.5 m cobalt blue + 0.8 m ruby red | 360 + 95 lm |

Each sign: emissive tubes (15 mm glass, electrodes at the ends, GTO wire, a black "transformer" box, chains or standoffs) and one proxy rect light facing the room, Specular 0.

**Proxy flux (DERIVED).** A tube of length l seen from the side has intensity `I = L d l` (luminance times projected area); its total flux is `Phi = pi^2 L d l`, so `I = Phi / pi^2`. A one-sided Lambertian rect light of flux `Phi_rect` has `I0 = Phi_rect / pi`. Equal intensity toward the room therefore needs **`Phi_rect = Phi_tubes / pi` (0.32 Phi)**; v1's "about half" was 1.6x too bright on axis. Resulting sign intensities and on-axis illuminance (sign treated as a point, ESTIMATE within 20 % beyond 1 m):

| Sign | I [cd] | E at 1 m / 2 m / 3 m [lux] |
|---|---|---|
| N1 Old Castor | 185 | 185 / 46 / 21 (the window booths and the front of the bar) |
| N2 OPEN | 108 | 108 / 27 / 12 |
| N3 Hollenbeck | 170 | 170 / 43 / 19 (bar top and stools #8-#10) |
| N4 POOL | 130 | on the wall-side rail cap ~26 lux at the side pocket (1.9 m, oblique), ~16 lux at the wall-side corners (+14 % over the lamp-only 117 lux of 4.4) |
| N5 Lantern Flats | 46 | 46 / 12 / 5 |

So the window area, the booths nearest to it and the end of the bar read as neon-lit (10-50 lux of coloured light), and the POOL sign visibly tints the wall-side rail caps red-gold.

### 4.4 Table lamp and lux targets (DERIVED, deliberately below WPA)

Lamp: 1.22 m black steel bar (brand badge in the middle; the end shades overhang it, overall length 1.28 m), three 0.36 m spun-steel dome shades (outside dark green enamel with the "Old Castor" logo decal, inside white), 0.23 m deep, centres 0.46 m apart, shade bottoms **0.86 m above the bed** (EQP 10.2: bar lamps hang 0.79-0.91 m above the bed; WPA wants >= 1.016 m), bulb centres 1.01 m above the bed, hung by two chains 0.86 m long from eye bolts through the ceiling tiles into the joists above (a lay-in tile cannot carry a lamp; tiles cracked around the bolts; the chains swing about the tile holes, so the pendulum length of TS-5 is measured from the tile plane).

Model (for the analytic lux probe, `RbCameraMath::IlluminanceAt` style): each bulb+shade = `I(theta) = I0 cos(theta)` for theta <= 50.2 deg, zero beyond; downward efficiency eta = 0.60 (direct cone + enamel bounce, ESTIMATE); `I0 = eta Phi / (pi (1 - cos^2 theta_c))` = 355.9 cd for Phi = 1100 lm.

| Point (table frame, before occlusion) | E [lux] (DERIVED) | Target band for acceptance |
|---|---|---|
| Bed centre | 828 | 750 - 910 |
| Head / foot spot | 664 | 600 - 730 |
| Side pocket (nose, middle) | 549 | 490 - 610 |
| Corner pocket (nose point) | 212 | 170 - 250 |
| Rail cap, side (mid width) | 501 | 450 - 550 |
| Rail cap, end centre | 258 | 220 - 300 |
| Rail cap at a corner | 117 | 95 - 140 |
| Bed + rails minimum / maximum / uniformity U0 | 99 / 828 / 0.12 | min >= 90 (corner balls must stay readable); WPA (>= 520 everywhere) intentionally **failed** |
| Floor 0.5 m off the side rail / end rail (direct spill, before rail occlusion) | 156 / 84 | the "pool of light" on the floor |

The bands are **lamp-only** values: VDB-T1 measures them with every other light off (Lumen GI on). With all lights on, the POOL neon adds ~15-26 lux on the wall-side rail caps (4.3) and the room bounce 5-10 %, so the all-lights report is informational only. If Lumen under-resolves the bounce inside the small enamel shades (0.36 m; surface-cache cards may be too coarse) and the lamp-only values fall below the bands, add one hidden, unshadowed-by-its-own-shade rect light per shade mouth (0.36 m disc approximated by a 0.32 x 0.32 m rect, Specular 0, flux = the measured shortfall) instead of raising the bulb flux, which would also brighten the highlight in the balls.

Cloth luminance at the centre `L = rho E / pi` = 0.15 x 828 / pi = **39.5 cd/m^2 -> EV100 8.30**. Sensitivity: 1600 lm bulbs would give 1204 lux (EV 8.84); 800 lm bulbs ~600 lux. The bulbs are 1100 lm because real 75-W-equivalent LEDs are, and because 830 lux at the centre with a 7:1 centre-to-corner ratio *is* the bar look.

Other lux targets (ESTIMATE, horizontal unless noted): walkway floor front room 8-20; floor at the bar front 25-40; bar top 70-110; booth tables 40-70 (neon-tinted); pool room floor outside the lamp spill 15-45; walls beside the table at 1.2 m (vertical) 30-60; dart board 150-250 (own light); back wall above the ledge 15-30; corridor floor 50-90; ceiling above the lamp 10-25 with a **green tint from the cloth bounce** (look-dev check: the ceiling tile above the table must read greener than the tiles above the bar).

### 4.5 Exposure targets (Eyes preset, adapted, ESTIMATE; measured with the capture log's adapted EV)

| View (section 12.2) | EV100 target | Why |
|---|---|---|
| V04 chin on cue | 7.8 - 8.4 | cloth fills the frame |
| V03 standing at the head end | 6.5 - 7.5 | table + dark room, centre-weighted |
| V02 from Deacon's stool | 4.5 - 5.5 | table small in frame, bar in front |
| V05 back bar | 4.5 - 5.5 | backlit bottles (~250 lux vertical), cooler 400 cd/m^2 |
| V08 corridor | 4.0 - 5.0 | CFL-lit slot, EXIT red |
| V01 entrance | 3.0 - 4.0 | darkest view; stays above the Eyes minimum EV100 2.0 (UE 4.4) so the room reads dark but not black |
| Lights-Up state, V01 | 5.5 - 6.5 | harsh fluorescent "closing time": 4 x 7000 lm x utilisation 0.55 / 120.5 m^2 = ~128 lux average, incident EV100 = log2(128 x 100 / 250) = 5.7 (DERIVED; v1 said 6.5 - 7.5, an office-level value) |
| AfterHours, menu S0 (UX 6.3) | 3.0 - 4.0 | lit table far away, room darker than Open (pendants, sconces, TVs off) |
| AfterHours, menu S1 / S6 / S7 | 2.0 - 3.0 | the darkest frames of the game: at the Eyes minimum EV100 2.0, the rest is carried by grain and the neon; UX-T04 checks the menu contrast over them |

Standing up from the shot (V04 -> V02) is a 3-4 EV change; at the Eyes adaptation speed of 0.7 EV/s down this takes ~5 s - the visible re-adaptation is intended (UE 4.4). The same rate governs captures: `capture_divebar.py` passes `rbue.py capture --warmup-seconds 12` for every view (the default 4 s is too short to adapt down to EV 3), and VDB-T2 reads the adapted EV from the capture log.

### 4.6 Lighting states

- **Open** (default): 4.2 as listed; troffers off.
- **Lights-Up**: at "last call" (2:00 in-game) or from photo mode: 4 x 2 x 4 ft troffers (3 x T8 each, ~7000 lm per fixture, 4000 K, one tube pinkish from age) switch on over 0.8 s (ballast warm-up, no flicker), neons stay on, jukebox off. Tests the exposure system and shows the room's grime without mercy.
- **AfterHours** (main menu "Closing Time", UX 6.1-6.5): table lamp L1-L3, back-bar strips L9-L10, coolers L11-L12, neons L20-L24, EXIT signs, corridor L26, Christmas lights L29, street L33-L35 on; bar pendants L5-L8, sconces L13-L15, popcorn L30, TV-2 and the troffers off; TV-1 dark except while the Replays station S4 plays a replay; jukebox idle (panels 30 %, title strips on); NPC slots empty. Props sublevel `_Props_AfterHours`: bar stools #1-#9 upside down on the bar top (seat down), booth cushions stacked, a mop bucket and wet-floor sign at the bar front, the register drawer open, the character's phone on the bar top at (8.90, 2.05, 1.07) (UX S1), the beer clock showing the after-hours time the UI drives (UX 6.3). Rain: a rain-on-glass layer on the clear window pane and the door's vision panel (animated droplet normal + streaks, M_DB_Glass option) and a wet-street backplate.
- **Level structure** (UX 6.5): `/Game/Generated/Maps/L_DiveBar` (non-partitioned, like every generated level, `rb_common.new_level`) with sublevels `_Geo`, `_Light_Night` (Open), `_Light_LightsUp`, `_Light_AfterHours`, `_Props_AfterHours`, `_NPC`. **Every state change is a ramp** of >= 0.8 s per light (troffer warm-up, the UX 6.4 "opening time" 1.5 s): no light may step on or off within one frame.

### 4.7 Haze, dust and photosensitivity

- **Volumetric fog** (Exponential Height Fog + Volumetric Fog), indoor only: scattering sigma_s = 0.003 1/m, absorption 0.0003 1/m, albedo (0.95, 0.92, 0.88), phase g = 0.6. **Local Fog Volume** (`ALocalFogVolume`, [5.8 ✓]) above the table: an ellipsoid by non-uniform actor scale, semi-axes 1.8 x 1.2 x 0.6 m, centred over the cloth at Z 1.4 (covers Z 0.8 - 2.0), `RadialFogExtinction` for sigma_s = 0.010 1/m, `HeightFogExtinction` 0, `FogPhaseG` 0.6, `FogAlbedo` (0.95, 0.92, 0.88): chalk dust and warm air over the lamp. It reaches the volumetric fog only through `r.LocalFogVolume.RenderIntoVolumetricFog` (default 1), which clamps its density at `r.LocalFogVolume.MaxDensityIntoVolumetricFog` (default 0.01): if the calibrated value needs more, raise the cvar in the venue's scalability rows rather than the height fog. Map everything to UE parameters by measurement, not by assumed units (VERIFY the 5.8 `FogDensity` / extinction scaling): acceptance = in V03 the lamp cone's in-scattered luminance against the dark back wall is **0.2 - 0.5 cd/m^2** (about 1 % of the cloth), i.e. visible but never "smoky". No smoke: Ohio banned it (S5).
- Grid: `r.VolumetricFog.GridPixelSize` 8 (High) / 4 (Epic, Cinematic), `r.VolumetricFog.GridSizeZ` 128 / 256, `r.VolumetricFog.TemporalReprojection` 1 (cvar names [5.8 ✓] in `VolumetricFog.cpp`). Light volumetric scattering: key lamp 1.0, neons 0.5, headlights 0.3, each with Cast Volumetric Shadow; **all other lights 0** (4.1; saves the volumetric shadow cost of the bar pendants and sconces).
- **Dust motes** `NS_DB_DustMotes` (Niagara GPU sprites): High 800 / Epic 2500 / Cinematic 6000 particles in a 2.4 x 1.4 x 1.1 m box around the lamp cone; world size 0.15-0.6 mm; Brownian drift 2 mm/s RMS plus an updraft over each bulb of 3 cm/s; lifetime 20-40 s; lit translucent with a forward-scattering boost (Henyey-Greenstein g = 0.8), fading to zero outside the cone. Motes glint when the view looks toward the lamp and vanish when looking down on the table - exactly as in reality.
- **Chalk puff** `NS_DB_ChalkPuff`: 40 blue-white particles per chalking, 0.05-0.3 mm, settle at 5-10 cm/s, stamp the cloth/rail chalk map where they land.
- Photosensitivity: no light, emissive or TV content may exceed 3 flashes/s; headlight sweeps >= 20 s apart; the fan runs at a 2.7 Hz blade pass and casts no shadow (E24); jukebox and dart-machine animations follow L18 / L19; the TV video is checked with a Harding-style flash test before import (section 13.7).

---

## 5. Props list

Routes: **BL** procedural Blender 5.2 script; **BL-TS** Blender from the TableSpec JSON; **UE-P** Unreal procedural (editor-Python placement/ISM, material-procedural, or UE-1 C++ bake); **MESHY** Meshy geometry -> Blender cleanup -> our materials; **HIGGS** Higgsfield 2D art (decal, label, poster, photo, concept) - never full PBR texture sets; **TXT** our text textures (Pillow + OFL/Apache fonts); **CC0** CC0 scan textures as material inputs (6.3); **CC0-M** Poly Haven CC0 model (fallback only, logged, max 5 generic utility items); **EXIST** already built by a UE work package.

Priority: **H** hero (survives a 0.25 m close-up at 4K, texel rule 11.5), **M** mid (1 m), **C** clutter (2 m, density matters more than detail).

### 5.1 Hero

| ID | Prop | Count | Size (m) | Route | Materials (section 6) | Notes |
|---|---|---|---|---|---|---|
| H01 | Coin-op cabinet "HALVERSON Stallion 7" | 1 | 2.362 x 1.346 x 0.791 | BL-TS + EXIST playfield | MI_DB_Laminate_Walnut, MI_DB_Alu_Trim, MI_DB_ABS_Casting, MI_DB_Chrome, MI_DB_Plexi_Scratched | 3.1; burns/rings/stickers as mesh decals |
| H02 | Ball set `OldBarOversizedCue` | 16 | Ø 57.15 / 60.325 mm | EXIST + material variant | MI_RbBall_DiveBar | 3.2; cue-ball stamp TXT |
| H03 | Table lamp, 3-shade brewery promo | 1 | 1.28 x 0.36 x 0.28 (bar 1.22 m) + chains | BL | MI_DB_Enamel_Green, MI_DB_Enamel_WhiteInt, MI_DB_Steel_Black, chain MI_DB_Steel_Zinc | logo HIGGS + TXT; dust on top surfaces (Age) |
| H04 | House cues (4 x 57, 2 x 52, 1 x 48, 1 x 36 in) + mechanical bridge | 8 + 1 | 1.448 / 1.321 / 1.219 / 0.914 m | EXIST cue generator (UE-4) with house-cue variants; bridge head BL | MI_RbCue variants, electrical tape on short-cue butts, weight stamp TXT "19 OZ" | HF-30 seeded defects; short cues must exist before the venue ships (UE pitfall 25) |
| H05 | Wall cue rack | 1 | 1.0 x 0.12 x 1.40 | BL | MI_DB_Wood_Stained (CC0 `fine_grained_wood`), felt pads | chalk fingerprints decal around it (S10) |
| H06 | Chalk cubes (bar: dried, cupped) | 4 | 0.022 cube | BL (cup depth from a wear parameter, HF-23) | MI_DB_Chalk_Blue, wrapper TXT "RAIL RAT" (the bar-cube grade of HF 4.1; v1's "Crestline" is too close to the real Predator "Crest" chalk) | one on each long rail, one on the C3 shelf, one under the table |
| H07 | Quarters | <= 40 instances | Ø 24.26 x 1.75 mm, 119 reeds | BL + UE-P stacks | MI_DB_CuproNickel (patina) | generic relief normal map made by us (no copy of a mint design); queue stacks at A6, coins in the slide, the coin dish |
| H08 | Bar counter | 1 | 7.92 x 0.79 x 1.07 | BL | top MI_DB_Wood_Lacquered_Bar (CC0 `lacquered_cherry_wood`), die MI_DB_Wood_PlankWall (CC0 `wood_plank_wall`), armrest MI_DB_Vinyl_Oxblood, foot rail MI_DB_Brass_Worn | touch-polish mask on the armrest and the bar edge; carved initials (geometry + decal) |
| H09 | Back bar (counter, mirror, shelves, mug rack) | 1 | 7.62 x 0.66 x 2.60 | BL | MI_DB_Mirror_Aged (desilvering at the edges), MI_DB_Glass_Shelf, MI_DB_Wood_Stained | shelves sag 3-8 mm under bottles (DERIVED from a simple beam model in the generator) |
| H10 | Bar stools (A torn, B duct-taped, C newer mismatched) | 10 | seat Ø 0.38, H 0.76 | MESHY (chrome swivel base + seat geometry); fallback BL | MI_DB_Vinyl_Oxblood / _Black, MI_DB_Chrome_Pitted, MI_DB_Foam_Exposed (CC0 `Foam002`), duct tape MI_DB_Tape | Meshy test 2026-09-27: geometry good, vinyl texturing unusable -> our materials only (hybrid) |
| H11 | Floor (VCT) | venue | 0.305 m tiles | UE-P material on a BL slab | MI_DB_VCT (6.2) | oxblood (#5A1F1B) / black (#1A1A1A) checker, a lighter-red patch of 9 replaced tiles at the bar front, beige VCT in the corridor; RVT wear/sticky |
| H12 | CD jukebox "Marquee Starlite CD-100" (1996) | 1 | 0.80 x 0.70 x 1.47 | BL + HIGGS album art + TXT title strips | MI_DB_Chrome, MI_DB_Paint_Cabinet, MI_DB_Plexi_Emissive | 100 CD title strips, band stickers (S16), "DEACON'S PICK" hand-written |
| H13 | House rules + queue chalkboard = the **score slate** | 1 | 0.90 x 0.75 | BL frame + board mesh with one 0-1 UV rectangle for the writing area; the text is **not baked**: `ARbScoreSlate` renders rules, queue and match section at runtime into a 1536 x 1280 render target (UX 9.3, 3.6, Kalam) | MI_DB_Chalkboard (RT as chalk mask x chalk-dust noise, baked ghosting of erased text underneath) | 8.4 |
| H14 | Hero drinks (pints, bottles, cans) | 6 + 8 + 6 | lathe | BL + HIGGS/TXT labels | MI_DB_Glass_Thin, MI_DB_Beer (liquid + foam), condensation mask | in V02/V03/V04 frames and on the C3 shelf |
| H15 | Column C3 + drink shelf + rack hook | 1 | Ø 0.114 x 2.74; shelf Ø 0.40 | BL | MI_DB_Paint_BlackSteel (chips to rust at kick height), shelf MI_DB_Plywood_Painted | ring stains, the ashtray coin dish |
| H16 | Paneled walls near the table (with dings band) | pool room | - | BL arch + decals | MI_DB_Paneling_Dark (CC0 `dark_paneled_wood`) | S10 band exactly at TS-1..TS-4 |

### 5.2 Mid

| ID | Prop | Count | Route | Materials / notes |
|---|---|---|---|---|
| M01 | Architecture shell (walls, soffit, columns C1/C2, doors, glass block, transom, corridor, restroom doors, missing-tile void with pressed tin) | 1 | BL (arch generator from `layout.json`) + UE-P ceiling tiles (ISM) | painted brick (CC0 `painted_brick`), plaster (CC0 `painted_plaster_wall` / `PaintedPlaster017`), block wall (CC0 `concrete_block_wall`), ceiling tiles (CC0 `OfficeCeiling001`-`006`, pick the 2 x 4 fissured one), pressed tin (BL heightfield) |
| M02 | Booths + tables | 3 | BL + Blender cloth sim for cushion sag and tufting | MI_DB_Vinyl_Oxblood (detail CC0 `fabric_leather_02`), table laminate + chrome edge band, T-base |
| M03 | Dart machine "Hawkline 360" + 6 soft-tip darts | 1 | BL + HIGGS marquee + TXT | emissive marquee, LED score display, band stickers |
| M04 | TVs (55", 43") + mounts | 2 | BL + video (13.7) | MI_DB_Screen with one texture parameter that takes either the media texture or a render target (TV-1 shows replays at menu station S4, UX 6.3; `URbDiegeticScreenComponent`); subpixel option at < 0.5 m, dusty top edge |
| M05 | Back-bar coolers (3-door, 2-door) + contents | 2 | BL + UE-P contents | cold interior, fogged glass bottom edge, magnets/stickers on the doors |
| M06 | Beer tap tower (6 handles) + drip tray | 1 | BL + HIGGS handle art | chrome, sticky drip tray |
| M07 | Neon signs N1-N5 | 5 | BL neon generator from our SVG paths (tube sweep 15 mm, bend radii >= 20 mm, electrodes, block-out paint, GTO wire, transformer box) | designs drawn as SVG by us, style informed by HIGGS concepts; MI_DB_Neon_<Gas> |
| M08 | POS register + receipt printer + card reader | 1 | BL | screen emissive 120 cd/m^2 |
| M09 | ATM "Tellerline" | 1 | BL + TXT screen | "$3.00 FEE" tape note |
| M10 | Popcorn machine | 1 | BL + TXT | L30 lamp, kernels (UE-P scatter) |
| M11 | Mug club rack + 30 mugs | 1 + 30 | BL lathe (3 mug shapes) + TXT names + UE-P arrangement | #17 black ribbon (S4) |
| M12 | Trophy shelf (14 trophies 1987-2019) | 14 | BL columns/bases/plates (TXT engraving) + MESHY figurines (pool player, eagle) | dust heavy (top shelf, Age +0.2) |
| M13 | Walleye plaque (taxidermy, Lake Erie) | 1 | MESHY + our materials | above booth B1 |
| M14 | Frames + Polaroid wall | ~40 | BL frames + HIGGS photos (fictional people) + TXT captions; UE-P wall layout | Polaroid/flyer wall on the right wall X 6.00 - 8.50, Z 1.20 - 2.40 (E08; menu Credits station S7); other frames around the room. Photos low-res, washed-out, yellowed (people are generated and must not resemble real persons - prompt rules 8.3, 13.6) |
| M15 | Posters and flyers | ~25 | BL (curled paper, tape, staples) + HIGGS art + TXT text | league night, karaoke Thursday, band gigs, "WE CARD", lost dog |
| M16 | Ceiling items: troffers, HVAC diffusers (6), smoke detectors (2), speakers (2), emergency light, fire extinguisher | ~15 | BL | dust on top, nicotine tint on diffusers |
| M17 | Payphone (dead) | 1 | BL + TXT note | S11 |
| M18 | Christmas lights C9 x 25 | 1 string | BL spline | L29 |
| M19 | Radiator under the glass block | 1 | BL | chipped silver paint |
| M20 | Door hardware + draft curtain | 1 | BL + cloth sim | velour, dusty hem |
| M21 | Trash cans (3), mop bucket, wet-floor sign (brand-free) | 5 | BL + TXT | corridor |
| M22 | Left-wall ledge + 3 spectator stools (older wooden stools) | 1 + 3 | BL | worn seat tops |
| M23 | Back ledge (drink rail) | 1 | BL | ring stains, sticky |
| M24 | Lighted beer clock ("Hollenbeck Light") | 1 | BL + HIGGS | hour and minute hands are separate meshes turned at runtime from a time source the game supplies: career = in-game time + 10 min ("bar time", S19); menu = the after-hours time of UX 6.3 (never the real clock) |
| M25 | LOW CLEARANCE 10'-6" road sign | 1 | BL + TXT (MUTCD-style yellow sign, public-domain style) | CC0 `rusty_painted_metal`, bent corner, bolt holes |
| M26 | Coat hooks + 2 jackets + a cap | 1 | MESHY or Blender cloth sim | on the right wall by the door |
| M27 | Ceiling fan | 1 | BL | 60 rpm |

### 5.3 Clutter

| ID | Prop | Count | Route | Notes |
|---|---|---|---|---|
| C01 | Coasters (4 designs, some swollen from moisture) | ~60 | BL + HIGGS/TXT | also the 8-ball pocket marker and the leg shim (S15) |
| C02 | Back-bar bottles | ~150 of ~20 designs | BL lathe library (8 silhouettes) + HIGGS/TXT labels; UE-P ISM with per-instance label index, fill level, dust | pour spouts on the speed rail; fill level varies (not all full) |
| C03 | Bottles and cans inside the coolers | ~120 | UE-P | behind fogged glass, low detail |
| C04 | Napkins (stacks, crumpled), straws, stir sticks, rubber bar mats (CC0 `Rubber004`), bar rags | ~40 | BL (+ cloth sim for crumpled/rags) | rags may be MESHY |
| C05 | Pickled egg jar, jerky clip strip, tip jar with folded generic bills | 3 | BL + HIGGS labels | bills: generic, not a copy of real currency (18) |
| C06 | Receipts, pens, a phone left on the bar, keys, a wallet | ~12 | BL (wallet MESHY) | on the bar in V02 |
| C07 | Floor litter: bottle caps, a straw wrapper, a coaster, a napkin | ~25 | BL + UE-P seeded scatter along P1/P4 and the bar front | never on the table zone floor where the player walks (no visual clutter under the stance) |
| C08 | Glass ashtray used as the coin dish | 1 | BL lathe | S5 |
| C09 | Extension cords, cable runs, cable staples | ~6 runs | BL spline | behind the jukebox and TVs |
| C10 | Outlet and switch plates, painted over | ~20 | BL | paint drips on the plates |
| C11 | Cardboard beer cases in the corridor | 6 | BL + HIGGS print | CC0 `Cardboard002` |
| C12 | Stickers (as mesh decals) | ~80 | HIGGS + TXT | 7 |
| C13 | Plastic triangle rack (worn) | 1 | BL | on the C3 hook |
| C14 | Chalk bits, dust bunnies, cobwebs (corners, under the table and booths) | ~20 | BL cards (Nanite masked kept small) | only where the camera goes low (V06) |
| C15 | Newspaper "Port Castor Ledger" on a booth, league sheet on a clipboard | 2 | TXT | fictional names only |

---

## 6. Materials

### 6.1 Master materials (Substrate, generated like UE-3: Python + HLSL includes, no hand graphs)

| Master | Substrate setup | Used for |
|---|---|---|
| `M_DB_Opaque` | Slab + wear stack (6.2) | paint, plaster, brick, block, ABS, rubber, paper, cardboard |
| `M_DB_Coated` | Vertical layering: clear coat over base slab; coat roughness and amber absorption driven by Age | lacquered wood, laminate, varnished paneling, painted enamel |
| `M_DB_Metal` | Metal slab (F0 per metal) + oxide/patina + fingerprints | chrome, brass, aluminium, zinc chain, cupronickel, steel |
| `M_DB_Vinyl` | Slab + light sheen (fuzz 0.05), crack mask at high convexity and authored stress lines, foam layer revealed by a tear mask | stools, booths, armrest |
| `M_DB_Glass` | Thin translucent (Substrate thin surface; VERIFY the 5.8 Substrate translucency mode name), roughness from dirt/fingerprint masks, optional condensation normal, optional rain layer (AfterHours window, 4.6); liquid as separate mesh `M_DB_Liquid` | bottles, glasses, cooler doors, windows, plexi |
| `M_DB_Emissive` | Unlit emissive in cd/m^2 + glass/plexi coat slab | neon (per gas), screens, jukebox panels, EXIT |
| `M_DB_Floor` | Procedural VCT (per-tile hash: colour +-4 %, roughness +-0.05, 0.3 mm height steps), wax layer, RVT inputs (traffic, sticky, spills) | floor |
| `M_DB_Decal` | DBuffer decal: BaseColor + Normal + Roughness, plus a roughness-only "sticky" variant | projected decals |

**Project settings this needs (hand-off H-3, section 20; `Config/` is owned by UE-0/UE-8):** `r.VirtualTextures=True` (not set in `Config/DefaultEngine.ini` on 2026-09-28) for the streaming virtual textures of 11.4 and the floor's runtime virtual texture; it is a project-wide shader recompile, so it goes in before DB-3. Until it is on, `M_DB_Floor` reads its traffic/sticky/spill masks from one baked 2K mask over the room (UV1 of the floor slab) and the SVT rule of 11.4 is skipped. The venue HLSL include lives in `Shaders/Private/Venue/RbVenueWear.ush` (included as `/RawBreak/Private/Venue/RbVenueWear.ush`), a subfolder, because `Shaders/Private/*.ush` belongs to UE-3.

Emissive units: author in cd/m^2 and calibrate once in DB-2 with a known surface (VERIFY 5.8 emissive scaling with "Extend default luminance range" on: place an emissive card at 100 cd/m^2, confirm the HDR pixel luminance, store the factor in `MPC_DB_Venue.EmissiveScale`).

### 6.2 The Age system (per-venue slider + per-asset bias)

`MPC_DB_Venue`: `Age` (dive bar **0.80**; Kneipe 0.60, pool hall 0.40, arena 0.05), `GrimeTint` (linear 0.09, 0.07, 0.05), `DustColor` (0.35, 0.33, 0.30), `NicotineTint` (0.78, 0.66, 0.42), `StickyAmount` 0.6, `EmissiveScale`. Per material instance: `AgeBias` (-0.3..+0.3), `EdgeWearWidth`, `TouchPolish` on/off. Effective age `A = saturate(Age + AgeBias)`.

Per asset the generator bakes **`T_DB_<Asset>_WM`** (wear mask, UV1, linear): R = convexity (Cycles Pointiness / edge-angle attribute remapped to 0..1), G = ambient occlusion (64 samples, 0.15 m distance), B = authored touch mask (hand contact: bar edge, armrest, rail caps, door push plate, stool edges, cue-rack slots), A = height above floor normalised per asset (for kick grime).

Layer formulas (applied in this order, DERIVED as simple monotone maps; constants TUNING):

```
edge wear   t_e  = 1 - 0.35 A (1 + bias);  w_e = smoothstep(t_e - 0.05, t_e + 0.05, C * N_grunge)
            -> reveals the substrate layer (wood under paint, metal under chrome/paint, primer under laminate)
cavity grime g   = A * (1 - AO)^1.5 * 0.8
            -> base *= (1 - 0.6 g), tint toward GrimeTint, roughness += 0.2 g
kick grime  k    = A * saturate(1 - z_world / 0.30 m) * N_scuff          (0 - 0.30 m above the floor)
dust        d    = A * k_d * saturate((N_world.z - 0.6) / 0.4) * (1 - touch) (k_d = 1 on unreachable tops, 0.3 elsewhere)
            -> base = lerp(base, DustColor, 0.7 d), roughness -> 0.9, fuzz 0.3 d
touch polish p   = touch * A
            -> roughness -= 0.15 p, base *= (1 - 0.15 p); on coated surfaces the coat is worn through where p > 0.6
nicotine    n    = A * NicotineMask (ceiling tiles, upper walls, diffusers; stronger above the bar)
            -> base *= lerp(1, NicotineTint, n)
```

`N_grunge`, `N_scuff` are tiling CC0 masks (6.3). Reading the frame: new-looking things are wrong here; every surface answers "who touched this and how often".

### 6.3 Surfaces and CC0 inputs (all CC0 1.0; asset IDs checked against the Poly Haven and ambientCG APIs on 2026-09-28)

| Surface | Material instance | CC0 input (source: ID) | Notes |
|---|---|---|---|
| VCT floor micro-surface | MI_DB_VCT_Oxblood / _Black / _Beige | Poly Haven `old_linoleum_flooring_01` (normal/roughness detail only) | colours procedural; wax lane mask from traffic paths P1-P4 |
| Floor grime and scuffs | (masks) | ambientCG `SurfaceImperfections003`, `Smear006`, Poly Haven `dirty_tiles` (roughness break-up) | heel marks as decals (7) |
| Rubber bar mats | MI_DB_Rubber_Mat | ambientCG `Rubber004` | perforated geometry (BL) |
| Wood paneling (1970s grooved) | MI_DB_Paneling_Dark | Poly Haven `dark_paneled_wood` | coated master, satin |
| Bar top | MI_DB_Wood_Lacquered_Bar | Poly Haven `lacquered_cherry_wood` | thick amber coat, rings/burns/carvings |
| Bar die | MI_DB_Wood_PlankWall | Poly Haven `wood_plank_wall` | kick grime strong |
| Cue rack, shelves, trophy bases | MI_DB_Wood_Stained | Poly Haven `fine_grained_wood`, `kitchen_wood` | |
| Laminate (table cabinet "walnut", booth tables) | MI_DB_Laminate_Walnut | Poly Haven `rosewood_veneer1` as the printed grain, recoloured | laminate edge chips reveal brown kraft core (procedural) |
| Painted brick (front room right wall, front wall) | MI_DB_Brick_PaintedGreen | Poly Haven `painted_brick`, `painted_worn_brick` | dark green, chipped |
| Upper walls (plaster/drywall) | MI_DB_Plaster_Cream / _Oxblood | ambientCG `PaintedPlaster017`, Poly Haven `painted_plaster_wall` | cream above the bar (nicotine), oxblood in the pool room |
| Concrete block (back wall, corridor) | MI_DB_Block_Painted | Poly Haven `concrete_block_wall` | beige in the corridor |
| Ceiling tiles | MI_DB_CeilingTile | ambientCG `OfficeCeiling00x` (choose the 2 x 4 fissured one in DB-1 look-dev) | nicotine gradient, water-stain decals |
| Pressed tin | MI_DB_Tin_Painted | Poly Haven `rusty_painted_metal` (edges only) | pattern generated in BL |
| Vinyl (stools, booths, armrest) | MI_DB_Vinyl_Oxblood / _Black | Poly Haven `fabric_leather_02` (booth tufting), `leather_red_02` (micro normal) | our colour, cracks, tape |
| Exposed foam | MI_DB_Foam_Exposed | ambientCG `Foam002` | inside tears only |
| Brushed aluminium trim | MI_DB_Alu_Trim | ambientCG `Metal009` / `Metal011` (brushed) | |
| Painted metal (column, sign, radiator) | MI_DB_Paint_BlackSteel etc. | ambientCG `PaintedMetal009`, Poly Haven `rusty_painted_metal` | |
| Plywood (shelf, backs) | MI_DB_Plywood_Painted | ambientCG `Wood089` | |
| Cardboard | MI_DB_Cardboard | ambientCG `Cardboard002` | |
| Fingerprints/smears (glass, chrome, screens) | (masks) | ambientCG `Fingerprints001`-`009`, `Smear001`-`008` | |
| Scratches (plexi, chrome, TV bezel) | (masks) | ambientCG `Scratches001`-`005` | |
| Leak stains (ceiling, wall drips) | (decals) | ambientCG `Leaking001`-`006` | |
| Tape (duct, masking, electrical) | MI_DB_Tape | ambientCG `Tape001`-`006` | |
| Exterior backplate | - | Poly Haven HDRI `street_lamp` | blurred behind glass block and door glass only |

Metals (F0 linear, UE 6.4 / physicallybased.info): chrome (0.55, 0.56, 0.55) roughness 0.05-0.15 with pitting; brass (0.910, 0.778, 0.423) roughness 0.3 + dark patina in cavities; aluminium (0.91, 0.92, 0.92) brushed roughness 0.35 anisotropic; zinc-plated chain (0.66, 0.66, 0.63) roughness 0.4.

### 6.4 Venue-specific instances of the table/ball/cue materials (owned by UE-3; values handed over)

- `MI_RbBall_DiveBar`: roughness 0.10-0.15 (per ball seeded), haze weight 0.3, dirt mask, slightly yellowed cue ball (albedo 0.72, 0.70, 0.64).
- `MI_RbCloth_BarGreen`: napped bar cloth, fuzz 0.35, initial wear map per 3.2.
- `MI_RbRail_BlackLaminate`: single slab roughness 0.4, burns/rings via mesh decals (UE 6.4 "bar-table laminate").
- House cues: satin shaft coat 0.25, chalk-blue smudges near the tip, dents, faded weight stamps.

---

## 7. Decals

Rule: decals that sit on one asset are **baked into that asset's unique UV1 masks or placed as mesh decals** (merged, cheap); projected DBuffer decal actors only for floor, walls and ceiling (budget <= 120 in view, 11). Decal art: HIGGS generations (black-and-white or on neutral grey backgrounds) -> `Tools/art/decal_prep.py` (alpha from background removal, height from luminance, normal via Sobel, roughness from the stain type) -> atlases `T_DB_DecalAtlas_<Set>_BC/_N/_R` (2K, 4 x 4 or 8 x 8 cells).

| Set | Items | Where | Count | Source |
|---|---|---|---|---|
| Burns | cigarette burns (fresh-looking none: all old, sanded-over, melted laminate), a larger burn on booth B3's table | table rail caps (8), bar top edge (12), booth tables (6), window sill (3), stool vinyl (2 melted holes) | ~31 | HIGGS + our height |
| Rings | glass ring stains (dried beer, 1-3 overlapping, some white "heat" rings on the lacquer) | bar top (~40), rail caps (~10), back ledge, C3 shelf, booth tables | ~70 | HIGGS + procedural rings (radius 30-45 mm) |
| Sticky / spills | dried beer (roughness 0.35-0.45, darker), fresh spill puddles (roughness 0.05) near the bar front and B2 | floor | ~12 | procedural (RVT stamp) + HIGGS shapes |
| Scuffs | heel marks on VCT, kick marks on the bar die and booth bases, cue-butt dings band on walls (0.80-1.00 m) at TS-1..TS-4 | floor, walls | ~60 | HIGGS + ambientCG `Scratches00x` |
| Chalk | blue chalk dust on the floor around the table (densest at the corners), blue fingerprints on rails, cue rack, chalkboard frame, the wall by the rack; rack outline on the cloth is UE-3's | floor, rails, walls | ~40 | procedural + HIGGS |
| Water | ceiling leak stains (brown rings) above the corridor opening and above booth B1; one sagging tile; a drip trail down the paneling | ceiling, wall | ~6 | ambientCG `Leaking00x` |
| Stickers | band stickers, bumper stickers, "NO JUMP SHOTS", "DO NOT SIT ON TABLE", "CASH ONLY", operator sticker, league sticker, torn sticker residue (paper fibres, glue haze) | jukebox, dart machine, corridor door frame, mirror corner, cooler doors, table cabinet | ~80 | HIGGS art + TXT text |
| Graffiti | marker tags and initials in the corridor, carved initials in the bar top and a booth table, "BIG LOU 2019" in the rail | corridor, bar, booth, rail | ~25 | HIGGS (style) + TXT (letters) + geometry for carvings |
| Tape | duct tape on stool B, electrical tape on the short cues, masking tape on the payphone, tape residue on the door glass | props | ~15 | ambientCG `Tape00x` + geometry |
| Paint | paint drips on outlet plates, overpainted edges at the paneling/ceiling line, touch-up patches in a mismatched colour next to the cue rack | walls | ~20 | procedural |

---

## 8. Fictional brands, text and art

### 8.1 Brand list (all PROPOSED; every name gets a knock-out screening - USPTO/EUIPO/DPMA - before the store page, together with the "RAW BREAK" trademark work; web searches on 2026-09-28 found no beer/table brand with these names. The review's searches renamed three names that sat next to real brands in the same product class: "Crestline" chalk -> **Rail Rat** (Predator sells "Crest" chalk; Rail Rat is already the bar-cube grade in HF 4.1), "Harrowgate" gin -> **Wexmoor** (several Harrogate gins exist), "Bell Hollow" bourbon with a bell emblem -> **Ashby Ridge** (Bell's whisky uses a bell; "Hollow" and "Ridge" are crowded bourbon words, screening mandatory). Spirits names collide easily: screen them first.)

| Category | Name | Look | Used on |
|---|---|---|---|
| Table maker | **HALVERSON** Amusement Co. (Rockford, IL), model "Stallion 7" | 1980s chrome script | cabinet plates, coin door, cue-ball stamp |
| Regional lager (the bar's beer) | **Old Castor Lager** ("Brewed in Port Castor since 1889") | red/cream label, beaver-and-river emblem | lamp (H03), neon N1, tap handle, cans, coasters, beer clock |
| Mass-market light beer | **Hollenbeck Light** | blue/silver | neon N3 (dead "t"), tap, bottles, beer clock M24 |
| Craft IPA (2010s addition) | **Lantern Flats IPA** | teal/orange, lantern | neon N5, tap, cans |
| Bourbon | **Ashby Ridge** Kentucky Straight Bourbon | cream label, ridge line and a horse barn (no bell) | back bar |
| Vodka | **Korvin** Vodka | white/red, plastic bottle ("well") | back bar, speed rail |
| Spiced rum | **Dockhand** Spiced Rum | sailor silhouette | back bar |
| Gin | **Wexmoor** Dry Gin | green glass | back bar |
| Tequila | **El Tordo** Blanco | agave/bird | back bar |
| Cinnamon whisky | **Ember** Cinnamon Whisky (screen early: "Blue Run Ember" bourbon exists) | red | back bar, shot glasses |
| Peppermint schnapps | **Frostmint** | green/white | back bar |
| Jukebox | **Marquee Phonograph Co.**, "Starlite CD-100" | 1990s chrome + neon-pink script | H12 |
| Dart machine | **Hawkline** "360" | red/black hawk | M03 |
| ATM | **Tellerline** | generic blue | M09 |
| Chalk | **Rail Rat** Billiard Chalk (bar cube, HF 4.1 grade table) | blue wrapper, rat silhouette | H06 |
| Pool league | **Castor Valley 8-Ball League** (CV8L); team "Low Bridge Bombers" | patch-style | sticker, trophies, posters, league sheet |
| Operator | **Castor Coin & Amusement** | label maker style | table and dart stickers |
| Sports on TV | **Summit Sports Network**; fictional minor-league teams "Harbor City Gulls" vs "Tri-State Foxes" | broadcast graphics | TV video (13.7) |
| Newspaper | **Port Castor Ledger** | serif masthead | C15 |
| Bands (jukebox, stickers, flyers) | ~40 fictional acts, e.g. The Rust Belt Saints, Delia Cruz & the Late Shift, Highway 9 Outlaws, Nine Ball Nancy, The Dry County Band, Canal Street Ghosts, Mercy Lane, The Keel River Boys, Static Parish, Lou & the Lake Effect | per genre (bar rock, country, soul, 80s metal) | title strips, album art, stickers, gig flyers; must match the jukebox music list of the audio plan (tracks generated by the product owner, roadmap window 3) |

**Tobacco:** none in the scene (PROPOSED). The smoking history is told by burns, stains and the ban sign only (S5). No cigarette brand is needed.

### 8.2 Text is ours, art may be Higgsfield

- AI image models garble letters. Therefore **all readable text** (labels, prices, names, rules, stickers' words, plates, title strips) is rendered by `Tools/art/text_textures.py` (Pillow) with licensed fonts committed under `Art/Fonts/<family>/` with their licence file and a ledger row: OFL fonts such as Bebas Neue, Oswald, Pacifico, Lobster, Courier Prime, Caveat, Gochi Hand, **Kalam** (the chalkboard hand; the UI's live score slate uses the same file, UX 4); Apache-2.0 fonts such as Permanent Marker, Special Elite, Rock Salt (VERIFY each licence at download; fetch with pinned URLs and SHA-256 like `Tools/ui/fetch_fonts.py`, UX 4). The UI spec shares `Art/Fonts/` for Caveat and Kalam, so there is one copy of each font.
- Higgsfield generates the **art layer** (emblems, label illustrations, poster imagery, sticker graphics, album covers, Polaroid photos, decal shapes, concept images for neon/labels, Meshy reference images). Output owned by the user, commercial use allowed (Higgsfield may train on stored content); Steam requires disclosure of pre-generated AI content (13.6, 17).
- Composition: art (PNG) + text layer (our render) -> label/sticker texture, printed-paper look (halftone optional for posters, fading by Age and distance to windows).

### 8.3 Higgsfield prompt rules (to keep art usable)

1. Flat, front-on, evenly lit, on a neutral grey or pure white background, no perspective, no photographic lighting (so it can be used as albedo art).
2. No text in the prompt output ("no letters, no words"); text is added afterwards.
3. No real brands, logos, mascots, celebrities or recognisable persons; people in photos are generic, mixed ages, 1980s-2020s clothing; images are small and washed-out in the scene.
4. Record prompt, model, seed/generation id and date in the ledger row.

### 8.4 Text generated from game data

- House rules chalkboard (H13): rendered **at runtime** by the UI's `ARbScoreSlate` (UX 9.3) from the active rules config (discipline, call mode, jump rule, rack rule, price) so it always matches the match flow; the queue column shows names of waiting NPCs (from the match director), and the match section shows racks and the circled **F** foul marks (the diegetic half of "foul count always visible"). The venue supplies the board mesh, frame, chalk tray and the chalk material; `text_textures.py` renders only the baked ghosting of old, erased writing underneath, in Kalam.
- Title strips (H12) and the jukebox music list come from one `Art/DiveBar/jukebox.json` shared with the audio plan.
- Price cards, mug names, trophy plates, league standings (fictional names; standings include the player's career name once earned - diegetic progression, HF 5.1).

---

## 9. The details that sell "real"

### 9.1 Checklist (each item is a look-dev check at DB-5/DB-6)

| # | Detail | How it is done |
|---|---|---|
| R1 | Sticky floor sheen | VCT wax lanes: traffic paths dull (roughness 0.55-0.65, ground-in dirt), edges near walls and under booths still glossy (0.2-0.3) - wear is *inverse* on waxed floors; dried beer patches 0.35-0.45 and darker; two fresh puddles 0.05 reflecting neon; footstep audio adds a tacky peel on sticky patches |
| R2 | Dust in the lamp light | 4.7 dust motes + local fog; dust layer on every unreachable top surface (lamp shades, jukebox top, TV tops, frames, trophies, the pressed-tin void) |
| R3 | Ring stains on the rail | rail-cap mesh decals + the drink left on the head rail by an NPC |
| R4 | Coins on the table edge | quarter stacks at A6, one stack per waiting player, slightly offset coins (stack jitter 0.5-1.5 mm) |
| R5 | The chalk cube | cupped face (HF-23), blue powder on the rail around it, paper wrapper torn, a cube on the floor under the table |
| R6 | Coin-op mechanics | slide resistance, 6 visible coins, rumble timing, ball trap window shows the pocketed balls, cue ball reappears in the return cup in the foot-end apron |
| R7 | Green ceiling | cloth bounce tints the ceiling above the table |
| R8 | Mirrored neon | window neons read backwards from inside; black block-out paint visible |
| R9 | Moving caustics | headlights through the glass block every 20-120 s |
| R10 | Cold vs warm | 5000 K coolers against 2200-2700 K tungsten tones; the TV's blue flicker on Ray's face |
| R11 | Light leaks | under the restroom doors; gaps in the ceiling grid over the lamp (tiles pushed up by the chains) |
| R12 | Imperfect alignment | 9.2 |
| R13 | Grime where hands go | touch masks (6.2): dark polished armrest, bar edge, door push plate, rail caps near the pockets |
| R14 | Layered decades | S-list: 1908 tin, 1958 photo, 1970s paneling/ceiling, 1984 table, 1996 jukebox, 2000s neon, 2010s TVs, 2020s card reader and phone |
| R15 | Density ("information overload", UE 3.1) | wall area above 1.0 m in the bar zone: >= 1 item (sign, frame, sticker, fixture) per m^2; back bar >= 120 bottles visible; >= 150 distinct props visible in V01 |
| R16 | Real-world scale | every dimension in this spec is a real one; generators assert sizes (13.4); nothing is scaled "to look right" |
| R17 | Sound of the room (hand-off to the audio plan) | 10 |

### 9.2 Imperfection rules for every generator ("the 3 mm rule")

Nothing in a 60-year-old bar is straight, level, parallel or evenly spaced. Generators apply deterministic, seeded jitter (seed = hash(asset id, instance)):

| Property | Jitter (ESTIMATE) |
|---|---|
| Furniture placement | position +-3 cm, yaw +-3..8 deg (stools up to 25 deg), never on the grid |
| Wall-hung frames/posters | tilt +-0.5..2.5 deg, spacing irregular, 1 in 10 hangs visibly crooked (4-6 deg) |
| Panels, boards | out-of-plane bow up to 2 mm per metre, gaps 0.5-2 mm |
| Shelves | sag 3-8 mm under bottles (beam model in the generator) |
| Ceiling tiles | 1 in 12 slightly lifted/tilted (<= 8 mm), 3 stained, 1 missing (S9), cracked tiles at the lamp eye bolts |
| Repeated items (bottles, mugs, coasters) | spacing +-10 %, rotation about the vertical axis random, fill levels vary |
| Lamp | 1.5 deg yaw, +2 cm Y off centre (lux targets 4.4 allow for it) |
| Bevels | every hard edge bevelled 0.5-3 mm (no knife edges), worn edges rounder |

---

## 10. Audio anchors (hand-off; [audio.md](audio.md) owns content and mixing, its 12.1 maps these elements to sound layers)

| Emitter | Position (V) | Behaviour |
|---|---|---|
| Room tone + HVAC | diffusers (M16) | constant, 2 layers |
| Cooler compressors | E04 coolers | cycle 8-15 min on / 5-10 off, start/stop clunks |
| Ice machine | storage door E19 (behind) | ice drop every 4-7 min |
| Jukebox | H12 (2 internal speakers) + 2 ceiling speakers (M16) | music from `jukebox.json`; silence and CD-changer mechanics between songs |
| TVs | TV-1, TV-2 | low commentary walla (unintelligible), crowd swells |
| Coin-op | A1-A4, gully splines (3.3) | 3.3 events |
| Dart machine | M03 | hit sounds, attract-mode jingle every ~10 min when idle |
| Street | door E02, glass block | muffled traffic; door opening: street noise +12 dB, air draft (curtain moves) |
| Restrooms | E20 | hand dryer / flush occasionally, door slam |
| Reverb | whole room | RT60 0.8 / 0.6 / 0.4 s low / mid / high (audio.md 6.4 owns the value); V1 IR synthesised from this room's geometry (`Tools/audio/ir_synth.py`, image sources + Sabine tail), later a recorded IR |
| Acoustic zones | `layout.json` volumes | main room, bar counter, restroom corridor, keg cooler (closed), outside (street), as Audio Gameplay Volumes (audio.md 6.5) |

For the IR synthesis every generated asset's metadata JSON carries an `acoustic_material` tag (`brick_painted`, `block_painted`, `vct_on_concrete`, `ceiling_tile_mineral`, `wood_panel`, `glass`, `glass_block`, `vinyl_upholstery`, `fabric_curtain`, `steel`, `rubber_mat`), and `db_arch.py` exports the room surfaces with their areas. Footstep surfaces: the main floor is **VCT on concrete** (a hard tile with sticky patches near the bar, R1), the corridor beige VCT, rubber mats behind the bar; audio.md 12.1 currently says "worn wood" and should follow this (hand-off H-6).

---

## 11. Performance budget

### 11.1 Targets

| Tier | Target | Notes |
|---|---|---|
| High (RTX 3070 Ti, 1440p, DLSS Quality) | target **>= 90 fps: mean GPU <= 10.3 ms, P95 <= 11.1 ms**; hard floor (ship gate) the plan's P95 <= 16.7 ms; VRAM <= 6.8 GB process total, venue texture streaming <= 1.6 GB of the 2.5 GB pool | UE 9.1 asks 90+ fps in the bar and 60 fps as the floor; missing 90 triggers the levers below, missing 60 blocks DB-6. Headless captures have no DLSS until the plugin is in (ARCH 14): measure with TSR at the same internal resolution (~67 %) and add the DLSS row as an estimate |
| Epic | quality first, not bound to the dev PC (UE 9.1) | 4K hero textures, MegaLights secondaries, more dust |
| Cinematic | no budget (photo mode, replays, trailer) | 8K hero surfaces, RT shadows everywhere, path-tracer captures |

### 11.2 GPU budget, High (ESTIMATE; replace with Unreal Insights / ProfileGPU in DB-6)

| Pass | ms |
|---|---|
| Nanite visibility + base pass | 1.7 |
| Shadows (VSM: 3 key bulbs + 4 bar pendants + corridor + headlight; volumetric shadows for the 3 key bulbs, the neons and the headlight) | 1.3 |
| Lumen GI (HWRT, surface cache) | 2.6 |
| Lumen reflections (HWRT, hit lighting; mirror + balls + bottles) | 1.8 |
| Translucency (bottles, glasses, cooler doors) + volumetric fog + local fog + dust | 0.9 |
| First-person body (hands SSS, hair cards; UE 9.2 row, missing in v1) | 0.3 |
| Post (DoF, motion blur, bloom, grain) | 0.8 |
| DLSS SR | 1.0 |
| UI / misc | 0.1 |
| **Sum / headroom to 11.1 ms** | **10.5 / 0.6** (v1 summed to 11.0 without the body: 0.1 ms headroom is not a budget). Levers in order: fog grid 8 -> 12 px, back-bar translucency LOD (opaque proxies beyond the front row), bar-pendant shadows off, Lumen reflections at half resolution on the mirror, dust 800 -> 400 |

### 11.3 Geometry, draw calls, lights

| Item | Budget |
|---|---|
| Unique static meshes | <= 350 |
| Instances total | <= 4000 (ISM/HISM for bottles, coasters, mugs, ceiling tiles, coins, cans) |
| Nanite | all opaque statics; masked only for cobwebs/cards; not for translucent |
| Nanite source triangles per asset | hero <= 2 M (Meshy remesh target 150-500 k), mid <= 300 k, clutter <= 30 k; level total <= 60 M |
| Nanite fallback | table + props within 2 m of the table: 100 % (ray tracing traces the fallback - ARCH-UE 2.2; balls reflect these); others `FallbackRelativeError` 1.0 (~10-25 %) |
| Non-Nanite draws in view | <= 300 (glass, liquids, neon glass coat, particles, projected decals) |
| Projected decal actors | <= 120 in view (the rest baked/mesh decals) |
| Ray-tracing instances | <= 3000 (UE guidance < 100 k); clutter < 5 cm outside the table zone excluded from RT |
| Lights | Open: 36 light components (L1-L35 with L25 as two signs, L29 as 3 points, L33 as 2 rects; L4, L31, L32, L34 are emissive only) + 4 troffers in Lights-Up; shadowed on High: 9; volumetric-shadowed: 3 key bulbs, 5 neon proxies, the headlight; MegaLights for secondaries on Epic+ (the key lamp stays classic VSM, UE pitfall 11) |
| Media | 2 TV videos 1280 x 720 H.264 (Low/Medium: 1 shared video, the other TV shows a still) |

### 11.4 Textures per preset

Authored (maximum) resolutions: hero unique maps 4096 (8192 only for Cinematic hero surfaces: bar top, rail caps, jukebox front, floor detail), mid 2048, clutter 1024, decal atlases 2048, tiling CC0 inputs 2048-4096. Streaming Virtual Textures for every texture >= 2048 (Bodycam converted ~90 % to VT, UE 3.1).

| Preset | Effective hero / mid / clutter | Mechanism |
|---|---|---|
| Low | 1024 / 512 / 256 | `r.Streaming.MipBias` 2 ([5.8 ✓] `TextureStreamingHelpers.cpp`), pool 1000 MB |
| Medium | 2048 / 1024 / 512 | MipBias 1, pool 1500 MB |
| High | 4096 / 2048 / 1024 (8K sources capped at 4K) | MipBias 0, pool 2500 MB; the 8K textures sit in their own texture group |
| Epic | 4096 + 8K hero surfaces | pool 3500 MB |
| Cinematic | all full | pool 4500 MB |

Texture group for the 8K surfaces: `TEXTUREGROUP_Project01`, named `RB_Cinematic8K` through `[EnumRemap] TEXTUREGROUP_Project01.DisplayName=RB_Cinematic8K` in `DefaultEngine.ini` (both [5.8 ✓]: `TextureDefines.h`, `BaseEngine.ini`). A texture group's `MaxLODSize` / `LODBias` live in the device profile, not in a scalability level, so a per-preset cap of this one group is not stock (VERIFY in DB-6): either (a) `LODBias` 1 for the group in the Windows profile and a game-code switch on Epic/Cinematic that sets it to 0 through `UTextureLODSettings` before the level streams, or (b) keep the group uncapped and let the 2500 MB pool on High drop its top mips (check with `stat streaming` that nothing else starves). Choose in DB-6 by measurement.

### 11.5 Texel density rule (DERIVED)

A surface seen at distance d needs `rho = p / d` texels per metre, with p = pixels per radian at the image centre (UE 4.3): 1544 px/rad at 1440p Eyes (V = 50 deg), 3088 at 4K. So d = 0.3 m -> 5150 px/m (1440p) / 10,300 px/m (4K); d = 1 m -> 1544 / 3088; d = 3 m -> 515 / 1029. Unique textures cannot reach 10 k px/m on a 2.4 m rail, so hero surfaces are **layered**: unique UV1 masks at 512-1024 px/m (wear, stains, burns) + world-scale UV0 tiling detail at 4096 px/m + a micro-normal. Tiny hero props (chalk, coins, coin-slide plate, stamps) get unique maps at >= 5000 px/m (a 22 mm chalk face = 128-256 px).

### 11.6 Per-preset venue switches

| Switch | Low | Medium | High | Epic | Cinematic |
|---|---|---|---|---|---|
| Volumetric fog / local fog | off (fake cone card) | low | on (8 px) | on (4 px) | high (4 px, 256 z) |
| Dust motes | 0 | 200 | 800 | 2500 | 6000 |
| Secondary light shadows | off | off | pendants + corridor | MegaLights all | RT all |
| Back-bar translucency | opaque proxy bottles | thin translucent, no reflections | translucent + Lumen reflections on the front row | all | all |
| Mirror | SSR | Lumen surface cache | HWRT hit lighting | same | same, full res |
| Decal cull distance | 6 m | 10 m | 20 m | all | all |
| Clutter < 5 cm cull distance | 3 m | 6 m | 12 m | all | all |
| TV video | 1 shared | 2 | 2 | 2 | 2 (1080p) |

---

## 12. Build order, milestones, screenshot checkpoints

### 12.1 Milestones

| MS | Content | Exit criteria | Screenshots (`Docs/images/divebar/<ms>/`) |
|---|---|---|---|
| **DB-0** Pipeline proof | Blender runner + common lib, axis/scale test asset `SM_DB_AxisTest` (1.000 m cube + arrow + "UP"), one real prop end-to-end (bar stool, BL fallback version), `M_DB_Opaque` with Age, `Docs/licenses/asset-ledger.csv` created, import script refuses assets without a ledger row | UE bounds of the test cube 100.0 +- 0.1 cm, arrow points +X, pivot at floor; ledger check fails on purpose once (negative test) | `db0/axis_test.png`, `db0/stool.png` |
| **DB-1** Greybox + table | Shell from `layout.json` (walls, ceiling, soffit, columns, doors, openings), greybox boxes for every E-element at true size, `TABLE_7FT_BAR` placed, collisions, PlayerStart, NPC markers, all `RbCam_DB_*` cameras (12.2, 12.3) and the menu stations `RbCam_Menu_S0..S7` (poses from UX 6.3), sublevels of 4.6 | walkable (pawn capsule r 0.25 m through the 1.04 m and 1.10 m gaps); 2.5 clearances reproduced by `cue_sweep_check.py` on `layout.json` and, once UE-4's `RbCueClearance::SweepEnvironment` is implemented (a stub on 2026-09-28), in-engine at 12 scripted positions (VDB-T3); overhead V10 matches 2.4 | `db1/V10_plan.png`, `db1/V01..V04.png` (greybox) |
| **DB-2** Light and exposure | Final light rig (4.2), emissive calibration, fog + dust, lighting states Open / Lights-Up / AfterHours | lux probe within 4.4 bands (VDB-T1), EV targets 4.5 met (VDB-T2), photosensitivity check (VDB-T8), light-flag validator (VDB-T11) | `db2/V01..V05,V08.png` + `db2/lux_report.txt` |
| **DB-3** Hero pass | H01-H16 final; masters 6.1 complete; CC0 inputs; first HIGGS brand art | "is it real" pre-check on V02, V03, V04, V06, V07 (internal review by Claude against reference photos) | `db3/V02,V03,V04,V06,V07.png` |
| **DB-4** Mid pass | M01-M27; neon generator; jukebox; dart machine; TV media | all mid props at 1 m pass look-dev; no placeholder in any V-view | `db4/V01..V09.png` |
| **DB-5** Clutter, decals, story | C01-C15, decals section 7, S1-S20 all present, R1-R16 check | density R15 counted by a script (props per view); story list ticked | `db5/V01..V12.png` |
| **DB-6** Polish, performance, blind test | profiling vs 11.2, per-preset switches, Epic/Cinematic captures, path-tracer reference of V03, **blind test** (12.4) | High budget met (VDB-T5); blind test pass criterion (VDB-T6) | `db6/*` incl. `db6/pt_V03.png` (path tracer) |
| **DB-7** Trailer-hook set | TH-1..TH-7 cameras verified with the handheld phone rig and macro cams (trailer tech), Sequencer shots saved (no trailer production - gated by the product owner) | each TH view renders in MRQ at 4K with the Cinematic preset | `db7/TH1..TH7.png` |

Order inside the milestones follows dependency: shell -> table -> lamp -> bar -> back bar -> stools -> floor/wall materials -> neon/practicals -> booths/jukebox/dart -> ceiling -> clutter -> decals -> story -> polish.

### 12.2 "Is it real" test views (cameras `RbCam_DB_Vxx`, placed in DB-1, Eyes preset unless noted; positions V frame, m)

All capture cameras are `ARbLookDevCamera` actors (a cine camera that applies the game's camera preset; ARCH 9.4: never plain `ACameraActor`s with engine-default exposure and DoF), tagged `RbCam_DB_V01..V12` / `RbCam_DB_TH1..TH7` / `RbCam_Menu_S0..S7`. The tags go into `RbAssetPaths::CaptureCamera` (UE-0 contract, additions allowed; hand-off H-3).

| View | Camera position | Look at | Purpose |
|---|---|---|---|
| V01 Entrance | (0.90, 6.00, 1.65) | (12.0, 4.5, 1.0) | full depth: neon windows, bar, table glowing at the back |
| V02 Deacon's stool | (8.60, 2.62, 1.55) | (13.76, 5.43, 0.85) | the table as a regular sees it; pint in the foreground |
| V03 Head end, standing | (12.00, 5.43, 1.62) | (14.30, 5.43, 0.76) | shooter's approach: table, lamp edge, TV-2, back ledge, the storage door, N5 neon (-30 deg, inside the frame; the corridor opening at -46 deg lies just outside the 79 deg frame; TH-1 frames it) |
| V04 Chin on cue | (12.78, 5.43, 0.90) (cue ball on the head spot, shot +X) | (14.27, 5.43, 0.77) | the most important realism view (grazing cloth, UE 6.3) |
| V05 Back bar | (5.20, 2.90, 1.60) | (5.20, 0.30, 1.50) | bottles, mirror, coolers, TV-1, mug club |
| V06 Ball macro | (14.50, 5.18, 0.80) (outside the 8- and 9-ball rack footprints) | (14.27, 5.43, 0.77) | racked balls, lamp in the reflections, dirty balls |
| V07 Coin slide | (15.40, 5.60, 1.10) | (14.94, 5.43, 0.60) | coin-op ritual, trap window |
| V08 Corridor | (15.80, 1.00, 1.60) | (20.10, 0.85, 1.70) | payphone, stickers, EXIT, light under doors |
| V09 Wall side | (13.90, 6.95, 1.50) | (13.90, 5.43, 0.76) | tight aisle, cue rack, short cue in use |
| V10 Plan | orthographic from above, actors tagged `RbDB_Ceiling` (tiles, grid, troffers, fan, lamp chains) hidden for the capture (the level is not World Partition, so there are no data layers), width 21 m | - | layout verification (not realism) |
| V11 Window from inside | (2.50, 3.50, 1.50) | (0.00, 3.00, 1.45) | mirrored neon, glass block, radiator |
| V12 Lights-Up | as V01, state Lights-Up | - | exposure system, grime |

### 12.3 Trailer-hook cameras (`RbCam_DB_THx`; trailer-plan.md; production itself is gated by the product owner)

| ID | Shot | Camera (V frame, m) | Lens / rig |
|---|---|---|---|
| TH-1 | Cold open: "phone" POV behind the shooter, someone says "watch this" | (11.80, 5.90, 1.45) -> (14.27, 5.00, 1.00); DERIVED framing at 69 x 43 deg (16:9): the corridor opening sits 27 deg left and 15 deg up (the EXIT sign itself is hidden at this angle, E21: the opening shows the corridor wall lit red and the payphone), TV-2 26 deg right and 19 deg up (inside the frame edges at +-34.7 / +-21.3 deg), the table fills the lower centre, the jukebox glow spills in from behind-right | handheld phone rig, 26 mm-equivalent (H-FOV ~69 deg), 30 fps, fast AE, phone noise |
| TH-2 | The impossible shot from the side: a masse here (the Low Bridge's house rules say "no jump shots", 1.1, and the board shows the active rules; a jump version needs a rules config without that line) | (13.20, 3.90, 1.25) at the C3 shelf -> across the table to the POOL neon and cue rack | phone rig held by a spectator |
| TH-3 | Break macro at ball level | (13.95, 5.20, 0.775) -> rack apex (14.27, 5.43, 0.772), 3/4 front view 0.39 m from the apex | 100 mm macro, f/2.8, 1000+ fps re-render |
| TH-4 | Reverse from the bar with neon bokeh | (8.90, 2.62, 1.55) -> table | 50 mm, f/1.8 |
| TH-5 | Coin slide + hands | (15.35, 5.30, 0.95) -> (14.94, 5.43, 0.62) | 35 mm macro |
| TH-6 | Pocket drop + gully roll | above the foot-right pocket (14.78, 5.94, 1.05) looking down | 35 mm, then audio-only roll |
| TH-7 | Door opens, street light and noise flood in, curtain moves | inside at (1.60, 6.40, 1.60) -> the door (0.0, 6.1, 1.4); no exterior facade needed (backplate only) | handheld |

### 12.4 Blind test protocol (DB-6)

- Images: 12 real dive-bar photos (licensed stock, e.g. Unsplash/Pexels licence, used only as internal test material, ledger rows "reference-only") and 12 game captures in matching categories (table from the shooter, table from the bar, back bar, neon window, corridor, close-ups). All at 1920 x 1080, same phone-style JPEG treatment (quality 85), game captures with the phone camera preset.
- Raters: >= 15 people who did not see development screenshots (friends/classmates of the product owner), each image shown 3 s then "real or game?" + confidence 1-3, randomised order.
- **Pass (VDB-T6):** game images judged "real" in >= 40 % of all ratings (indistinguishable would give the same rate as the real photos, typically 60-80 %), **and** >= half the real photos' own "real" rate (the control: a rater pool that calls everything fake cannot pass the game by default), and at least 3 game images judged real by >= 50 % of raters. With 15 raters x 12 images = 180 ratings the 95 % interval of a rate is about +-7 points, so report the interval with the result. Every game image below 25 % gets a written failure analysis and a fix item.
- Privacy: answers are anonymous (no names, e-mail addresses or IPs stored; the local HTML page writes one results file the owner exports), so no personal data is collected.

---

## 13. Blender-to-Unreal pipeline

### 13.1 Folders and files (new; generators are the source of truth, outputs are regenerated, never hand-edited)

```
Tools/blender/
  rbbl.py                    host runner: runs Blender 5.2 headless, logs to Saved/RbLogs/blender-<script>-<time>.log,
                             fails on a non-zero exit code or the marker RBBL_FAIL (like rbue.py)
  common/rb_bl.py            units, naming, bevel/jitter helpers, UCX builder, UV0 world-scale + UV1 unique unwrap,
                             Cycles bakes (WM, AO), FBX export, metadata JSON, validation
  divebar/db_arch.py         shell from Art/DiveBar/layout.json
  divebar/db_table_cabinet.py  BL-TS: cabinet from Art/DiveBar/tablespec_seven_foot_bar.json
  divebar/db_<family>.py     bar, backbar, booth, stool_fallback, lamp, jukebox, dart, neon (SVG -> tubes), lathe_props,
                             signs_frames, ceiling_items, clutter, ...
  divebar/db_build_all.py    runs every generator in dependency order
  divebar/cue_sweep_check.py 3D cue-sweep statistics of 2.5 from layout.json (engine sweep parameters)
  meshy/cleanup.py           MESHY import -> scale -> pivot -> decimate -> UV1 -> material zones -> WM bake -> export
Tools/art/
  text_textures.py           TXT: labels, plates, chalkboard, title strips (fonts in Art/Fonts, OFL/Apache)
  decal_prep.py              HIGGS decal -> alpha, height, normal, roughness, atlas packing
  fetch_cc0.py               downloads the CC0 inputs of 6.3 by ID (Poly Haven / ambientCG APIs), pins resolution + SHA-256,
                             writes ledger rows; re-running it restores Art/Third exactly
Art/DiveBar/
  layout.json                every placement (asset id, transform, seed, variant) - transcribed from section 2
  lights.json                every light of 4.2 (type, position, photometry, CCT, shadows per preset)
  jukebox.json               title strips + track list (shared with the audio plan)
  tablespec_seven_foot_bar.json   exported by rbsim (below): TableGeometry of TABLE_7FT_BAR (noses, jaws, facings, pockets,
                             rail tops, sights, profile, nose outline)
  Export/<Asset>/SM_DB_<Asset>.fbx + <Asset>.json      (LFS)
  Textures/<Asset>/T_DB_<Asset>_<Map>.png|exr           (LFS)
Art/Third/{polyhaven,ambientcg}/<id>/                   raw CC0 downloads - git-IGNORED (re-fetchable by ID + hash; 20+ scan sets
                                                        at 2-4K are several GB, far beyond a sensible LFS quota)
Art/Meshy/raw/<task-id>/   original GLB + prompt + reference image + task JSON (licence proof, LFS)
Art/Higgsfield/raw/<gen-id>/  original PNG + prompt + metadata (LFS)
Tools/unreal/editor/
  rb_import_divebar.py       Interchange import of Art/DiveBar/Export -> /Game/Generated/Venues/DiveBar/...
  rb_make_divebar_materials.py  M_DB_* masters + MI_DB_* + MPC_DB_Venue (HLSL in Shaders/Private/Venue/RbVenueWear.ush)
  rb_make_divebar.py         level /Game/Generated/Maps/L_DiveBar + sublevels (4.6) from layout.json + lights.json,
                             capture and menu-station cameras, light-flag validator (VDB-T11)
Tools/unreal/capture_divebar.py   all V/TH captures of a milestone
```

`Tools/unreal/**`, `Shaders/**` and `Content/**` are owned by the UE work packages; the files above are **new** and must be assigned to a work package (15) - nothing in this spec edits existing UE files.

**Table geometry export exists already** (no new exporter needed): `rbsim --geometry` writes the single-source-of-truth `TableGeometry` for render-mesh generators (`WriteGeometry` in `Tools/rbsim/Main.cpp`; run on 2026-09-28 with the Debug build: `name` TABLE_7FT_BAR, length 2.032, width 1.016, noseHeight 0.03629, railTopZ 0.048, railWidthTotal 0.1651, 18 sights, 6 pockets with mouths and capture centres):

```
build/Tools/rbsim/Debug/rbsim.exe --table 7ft-bar --balls oldbar --geometry --no-trajectories --no-states ^
    --out Art/DiveBar/tablespec_seven_foot_bar.json
```

The JSON has no bed height and no outside size: outside = length/width + 2 railWidthTotal (2.3622 x 1.3462 m, asserted by the cabinet generator against 3.1), and the bed height 0.743 m is carried in `layout.json` and compared with `FRbTableContext::BedHeight()` by the level validator (VDB-T10). Optional one-line addition for the rbsim owner: a `bedHeight` field in `WriteGeometry`.

### 13.2 Headless Blender

```
"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup -noaudio --python-exit-code 1 ^
    --python Tools/blender/divebar/db_bar.py -- --seed 1958 --out Art/DiveBar/Export
```

- `--factory-startup` for reproducibility (no user prefs/add-ons), `--python-exit-code 1` so exceptions fail the run; `rbbl.py` wraps it.
- Deterministic: `random.Random(seed)` per asset/instance, no wall-clock, sorted iteration; running twice gives the same metadata (vertex counts, bounds, hashes).
- Bakes with Cycles on the GPU (OptiX, RTX 3070 Ti), AO 64 samples, output 16-bit PNG (linear).

### 13.3 Naming

| Kind | Pattern | Example |
|---|---|---|
| Static mesh | `SM_DB_<Family>_<Variant>[_<nn>]` | `SM_DB_BarStool_B_01`, `SM_DB_Neon_OldCastor` |
| Master material | `M_DB_<Master>` | `M_DB_Coated` |
| Material instance | `MI_DB_<Surface>_<Variant>` | `MI_DB_Vinyl_Oxblood` |
| Texture | `T_DB_<Asset>_<Map>`; maps `_BC` (sRGB), `_N` (DirectX, green down), `_ORM` (AO, rough, metal; linear), `_H`, `_WM` (wear mask), `_E` (emissive), `_O` (opacity) | `T_DB_Jukebox_WM` |
| Decal | `MI_DB_Decal_<Set>_<nn>`, atlases `T_DB_DecalAtlas_<Set>_*` | `MI_DB_Decal_Rings_03` |
| Collision | `UCX_<MeshName>_<nn>` in the FBX | `UCX_SM_DB_Jukebox_00` |
| Niagara | `NS_DB_<Name>` | `NS_DB_DustMotes` |
| Level, cameras, lights | `L_DiveBar`; camera tags `RbCam_DB_V01..V12`, `RbCam_DB_TH1..TH7`; light labels `LT_DB_<ID>` | `LT_DB_L1` |
| UE folders | `/Game/Generated/Venues/DiveBar/{Meshes,Materials,Textures,Decals,FX,Media}` | |

Normal maps: CC0 sources provide both conventions (Poly Haven `nor_dx`/`nor_gl`, ambientCG `NormalDX`/`NormalGL`) -> always take DX; Blender bakes are OpenGL -> the exporter flips green.

### 13.4 Scale, pivots, validation

- Blender scene: metric, unit scale 1.0, 1 BU = 1 m. FBX export: `apply_unit_scale=True`, `apply_scale_options='FBX_SCALE_UNITS'`, `use_mesh_modifiers=True`, `mesh_smooth_type='FACE'`, `add_leaf_bones=False`, `bake_anim=False`, `use_custom_props=True`, triangulate on export. Axis settings are fixed by the DB-0 axis test (starting point: Blender defaults forward -Z / up Y; UE must show the model front on +X - VERIFY once, then freeze).
- Pivots: floor items at the floor-contact centre; wall items at the wall plane, bottom centre; hanging items at the ceiling attachment point; the lamp at its chain anchors (so the swing pivot is right).
- Every asset JSON lists target dimensions from this spec; the exporter **asserts** bounds within +-2 mm (hero) / +-1 cm (others), and the importer re-checks in UE (VDB-T4).
- BL-TS: the cabinet generator reads the TableSpec JSON (outside dims, rail width, pocket centres and mouth lines, sight positions) and asserts >= 2 mm clearance between castings and the pocket jaw/capture geometry.

### 13.5 UVs, collision, LOD/Nanite

- UV0 = world-scale (1 UV unit = 1 m) for tiling CC0 inputs; UV1 = unique, non-overlapping, 4-8 px padding at the target density (hero 1024 px/m, mid 512, clutter 256) for WM/stain/burn masks.
- Collision: simple convex `UCX_` hulls generated by the script (boxes; cylinders as 12-sided hulls) for furniture and props; walls/floor/ceiling as boxes; **no complex-as-simple** except the table (UE-1). Collision profiles: `RbVenueBlock` (blocks pawn and the cue sweep channel: walls, columns, jukebox, stools, cue rack, lamp, dart machine, ledges), `RbVenueProp` (clutter: no pawn block, query only; small drinks on rails later for HF-79). The cue sweep channel does not exist yet (`RbCueClearance::SweepEnvironment` is a stub on 2026-09-28, and `Config/DefaultEngine.ini` defines no custom channels or profiles): hand-off H-1/H-3 asks UE-4 and the config owner for one trace channel `RbCueSweep` and the two profiles; the venue generator only references the profile names.
- **Balls that leave the table** (decisions 2026-09-28: engine physics takes over, the ball bounces on the floor, rolls under stools, and a pick-up chore returns it). So the collision is also a ball course: (1) floor = its own simple box with physical material `PM_DB_VCT` (friction 0.5, restitution 0.35, ESTIMATE for phenolic on vinyl tile over concrete), rubber mats `PM_DB_Rubber` (0.8 / 0.15), wood ledges `PM_DB_Wood`; (2) stool and table-cabinet hulls model the real legs and bases (a 57-60 mm ball rolls under a stool, between the cabinet legs, Z 0 - 0.30, and under the booth tables), never one box over the whole footprint; (3) every floor-level gap the pawn cannot reach is closed for balls by an invisible kick-plate hull (under the back-bar coolers, behind the jukebox and the ATM, under the radiator's back); the booth benches and the jukebox stand on closed plinths anyway; (4) behind the bar counter (bartender side) is a `RbBallReturn` volume: a ball that ends there is handed back by Terri with a voice line. The ball's own profile and channel belong to the UE package that implements the off-table hand-over.
- Nanite on for all opaque statics (fallback rules 11.3); translucent/glass meshes non-Nanite with 3 auto LODs (50 / 25 / 12 %, screen sizes 0.5 / 0.25 / 0.12) generated in UE by the import script.
- Distance fields on (Lumen / fog / Lite fallbacks).

### 13.6 Meshy and Higgsfield flows

- **Meshy** (Pro plan, owned output; never table/balls/cue/pockets/architecture): reference image = our own (a Blender greybox render at the target proportions and/or a Higgsfield concept) -> Meshy image-to-3D -> download GLB + task JSON into `Art/Meshy/raw/<task-id>/` -> `Tools/blender/meshy/cleanup.py`: apply transforms, scale to the spec dimensions, pivot, decimate to budget, recompute normals, UV1 smart project, split material zones by albedo clustering (k-means on sampled albedo -> slots chrome / vinyl / rubber / foam), keep Meshy albedo only for metal zones that pass a de-light check (luminance must not correlate with the normal's up component, |r| < 0.2), bake WM, export. Vinyl/leather/fabric always use our materials (Meshy test 2026-09-27).
- **Higgsfield** (2D only): prompt rules 8.3 -> PNG into `Art/Higgsfield/raw/<gen-id>/` -> `decal_prep.py` or label composition with TXT -> textures. People in photos: generic, generated; reject any output that resembles a real person or brand.
- **Steam:** Meshy and Higgsfield outputs are "pre-generated AI content" -> the ledger flags them so the Steam content survey disclosure can be filled from it.

### 13.7 Video for TVs

- Content: a fictional sports broadcast (Summit Sports Network, teams 8.1) rendered by us in Unreal from a simple stadium/graphics scene, or a Higgsfield video only if its terms allow commercial use (VERIFY at use) and it contains no real teams/logos. 1280 x 720, 30 fps, 2-3 min loop, seamless.
- Photosensitivity pre-check: frame-to-frame mean luminance change > 10 % of the maximum counts as a flash; reject if > 3 flashes/s anywhere (Harding-style guideline).
- Import as a Media Source + Media Texture (UE Media Framework), looping, muted (audio from the sound plan).

### 13.8 Import via editor Python

- `rb_import_divebar.py` reads `Art/DiveBar/Export/manifest.json`, imports each FBX with `unreal.AssetImportTask` (`automated=True`, `replace_existing=True`, Interchange pipeline with materials off - VERIFY the 5.8 Interchange option objects), then sets `nanite_settings` (enabled, fallback), collision from UCX (no auto collision), LOD group, `visible_in_ray_tracing` (false for tiny clutter outside the table zone), assigns `MI_DB_*` by slot name. Textures: `_BC` sRGB BC7 (hero) / BC1 (others), `_N` BC5 normal map, `_ORM`/`_WM` masks linear, `_H` grayscale, virtual texture streaming for >= 2048, texture group per class.
- `rb_make_divebar.py` builds `L_DiveBar` and its sublevels (4.6): spawns from `layout.json` (ISM for repeated items), lights from `lights.json` (with `bCastVolumetricShadow` / `VolumetricScatteringIntensity` / `SpecularScale` per 4.1-4.2), post-process volume (Eyes defaults from UE 4), fog + local fog volume, Niagara, capture and menu-station cameras (`ARbLookDevCamera`, tags of 12.2), `ARbTable` at (1375.9, 542.7, 0) cm, yaw 0 (`Preset` SevenFootBar, `BallSet` OldBarOversizedCue, `BallSetSeed` = `VenueBallSetSeed(VenueSeed, 0)`, `LampUndersideHeight` 0.86 m; venue seed and lamp footprint once `ARbTable` exposes them, H-2), NPC and waiting-spot markers, PlayerStart at the head end (12.20, 5.43) facing +X, World Settings GameMode `ARbGameMode`. Every generated level is recreated from scratch by `rb_common.new_level` (ARCH 9.3), so the script is idempotent.
- Captures via `rbue.py capture` with strict shader checks (ARCH-UE 9.4).

### 13.9 Licence ledger `Docs/licenses/asset-ledger.csv` (created in DB-0)

Columns: `asset_id, used_by (UE package or file), source (polyhaven | ambientcg | meshy | higgsfield | own | font | fab | stock-reference), source_ref (URL or task/generation id), author, licence (CC0-1.0 | Meshy-paid-owned | Higgsfield-owned | OFL-1.1 | Apache-2.0 | Fab-Standard | Unsplash/Pexels-reference-only), licence_url, date (ISO), account, sha256 (of the downloaded/generated original), modified (y/n + how), ai_generated (y/n), steam_ai_disclosure (y/n), trademark_check (n/a | pending | cleared), notes`.
Rules: the import script fails on any asset without a row; `fetch_cc0.py` writes rows automatically; stock photos are reference-only and never shipped; Fab only as a logged last resort (decisions 2026-09-27). `Art/Third/` goes into `.gitignore` (DB-A); Meshy and Higgsfield originals stay in LFS because they cannot be re-generated and are the licence proof. The audio and UI specs write to the same ledger (audio.md 9, UX 4: source `font`), so the columns above are the shared set; add columns, never rename them.

---

## 14. German Kneipe reuse notes

| Reused as is | Reused with parameters | New for the Kneipe |
|---|---|---|
| Pipeline (13), master materials + Age system (Kneipe Age 0.60), decal system and atlases (burns, rings, scuffs, stains), lux/EV verification, dust/fog setup, text-texture tool, imperfection rules (9.2), NPC marker system, coin-op gameplay mechanics (Euro coins) | Bar counter + back-bar generators ("Tresen" with a Zapfanlage, Pils/Weizen glasses), stools (wooden Barhocker instead of chrome/vinyl), booth generator -> "Eckbank" corner bench, paneling (oak "Eiche rustikal"), ceiling (wood-panel or beam ceiling instead of lay-in tiles), table cabinet generator (German coin-op ~7 ft, 1 or 2 EUR per game, optically separated cue ball per EQP 6.2 / 15 Q1), neon generator (German fictional brands), lamp (single long fixture) | Glassware with a legal fill line ("Eichstrich", 0.3/0.4/0.5 l), Bierdeckel with pencil tally marks, Stammtisch sign, net curtains (Gardinen) or bullseye glass, E-dart machine (very common), cigarette vending machine and "Raucherkneipe" question (smoking rules vary by Bundesland - the product owner decides; Baden-Wuerttemberg has small-bar exceptions - VERIFY), green ISO 7010 "Notausgang" exit sign instead of the red US EXIT, Schuko sockets and switches, Euro prices, German menu board, slot machines only as unplugged props or not at all (gambling imagery vs USK), Euro coin depiction rules (VERIFY) |

Practical: keep every dive-bar generator parameterised by `venue.json` (units, style set, age, brand set, electrical standard) from the start, so the Kneipe is a new parameter set plus a few new generators, not a copy.

---

## 15. Suggested work packages (disjoint file ownership, per the project's process)

| WP | Scope | Owns | Depends on |
|---|---|---|---|
| DB-A Pipeline | rbbl.py, common lib, axis test, import script skeleton, ledger + check, fetch_cc0 | `Tools/blender/rbbl.py`, `Tools/blender/common/*`, `Tools/art/fetch_cc0.py`, `Tools/unreal/editor/rb_import_divebar.py`, `Docs/licenses/asset-ledger.csv` (shared with audio/UI: append-only), the `Art/Third/` line in `.gitignore` | M1 merged |
| DB-B Layout + shell + level | layout.json, lights.json, db_arch.py, rb_make_divebar.py (sublevels, AfterHours props, capture + menu-station cameras), cue-sweep check, table-geometry JSON | `Art/DiveBar/layout.json`, `lights.json`, `tablespec_seven_foot_bar.json`, `Tools/blender/divebar/db_arch.py`, `cue_sweep_check.py`, `Tools/unreal/editor/rb_make_divebar.py`, `Tools/unreal/capture_divebar.py` | DB-A; UX 6.3 station poses |
| DB-C Materials | masters, Age MPC, HLSL wear include, all MI_DB_* | `Tools/unreal/editor/rb_make_divebar_materials.py`, `Shaders/Private/Venue/*.ush` (subfolder: `Shaders/Private/*.ush` is UE-3's) | DB-A; H-3 (virtual textures) before DB-3 |
| DB-D Table + lamp | cabinet from the rbsim geometry JSON (13.1; no new exporter), lamp, coin-op parts | `Tools/blender/divebar/db_table_cabinet.py`, `db_lamp.py` | DB-A, DB-B (geometry JSON), UE-1 |
| DB-E Bar + seating | bar, back bar, booths, stool fallback, ledges | `Tools/blender/divebar/db_bar.py`, `db_backbar.py`, `db_booth.py`, `db_stool_fallback.py` | DB-A |
| DB-F Practicals | neon generator + SVGs, jukebox, dart, TVs + media, coolers, ATM, popcorn, ceiling items | `db_neon.py`, `Art/DiveBar/neon/*.svg`, `db_jukebox.py`, `db_dart.py`, ... | DB-A |
| DB-G Props, text, art | lathe library, clutter, frames/posters, text textures, decal prep, HIGGS generations | `db_lathe_props.py`, `db_clutter.py`, `Tools/art/text_textures.py`, `decal_prep.py`, `Art/Higgsfield/**`, `Art/Fonts/**` | DB-A |
| DB-H Light, FX, verification | fog/dust Niagara, lux/EV checks, photosensitivity check, light-flag validator, captures, performance | FX assets via its own generator script (`Tools/unreal/editor/rb_make_divebar_fx.py`), test code in a new test file | DB-B, DB-C |
| DB-M Meshy props | stools, taxidermy, figurines, cloth items (after the Pro month starts) | `Tools/blender/meshy/*`, `Art/Meshy/**` | DB-A, product owner action |

---

## 16. Acceptance tests

| ID | Test | Pass |
|---|---|---|
| VDB-T1 | Lux probe on bed and rails (analytic model of 4.4 on the placed lights, plus an in-engine check with a Lambertian white card, `L = rho E / pi`, **lamp-only**: every other light off, Lumen on) | every point in its 4.4 band; min >= 90 lux; WPA check reported as "failed by design"; an all-lights report is attached for information |
| VDB-T2 | Adapted EV100 per view (capture log, `--warmup-seconds 12`), states Open, Lights-Up, AfterHours | within the 4.5 bands |
| VDB-T3 | Cue sweeps at 12 scripted positions (4 at TS-1, 2 at TS-2, 2 at TS-3, 1 at TS-4, 1 at TS-6, 2 open controls) with the engine parameters of 2.5 (backswing 0.30 m, r_b 15.9 mm, margin 1 mm); first against `cue_sweep_check.py`, then in-engine once `RbCueClearance::SweepEnvironment` exists | the 58-in cue is blocked exactly where 2.5 predicts (+-2 cm), the shortened-backswing length matches (+-2 cm, if H-1 is adopted), the 52/48-in offers appear, open controls pass; the class shares of 2.5 reproduce within +-0.5 points |
| VDB-T4 | Asset dimension check (importer) | bounds within +-2 mm (hero) / +-1 cm (others) of the spec |
| VDB-T5 | Performance run (Gauntlet or scripted camera path through V01-V09), High, 1440p, DLSS Q (TSR at the same internal resolution until the DLSS plugin is in), RTX 3070 Ti | target: mean GPU <= 10.3 ms and P95 <= 11.1 ms; gate: P95 <= 16.7 ms; VRAM <= 6.8 GB; no hitch > 50 ms after PSO warm-up |
| VDB-T6 | Blind test (12.4) | >= 40 % "real" ratings for game images, >= half the real photos' "real" rate, and >= 3 game images at >= 50 %; 95 % intervals reported |
| VDB-T7 | Ledger completeness | every imported asset has a row; no NC licences; no real brand names in any texture (OCR pass over all `_BC` textures against a list of real beer/liquor/table/cue brands) |
| VDB-T8 | Photosensitivity | no emissive/light/TV sequence exceeds 3 flashes/s; headlight intervals >= 20 s |
| VDB-T9 | Story completeness | S1-S20 present and visible from at least one V-view each |
| VDB-T10 | Physics consistency | `ARbTable::LampUndersideHeight` == lamp generator value (0.86 m); lamp footprint handed over once exposed (H-2); table preset SevenFootBar, ball set OldBarOversizedCue, actor at (13.759, 5.427, 0) yaw 0, `layout.json` bed height == `FRbTableContext::BedHeight()` (0.743); roll-off azimuth within +-25 deg of the jukebox direction, confirmed by the two-roll behaviour test of 3.2 |
| VDB-T11 | Light flags (level validator in `rb_make_divebar.py`) | every light with `VolumetricScatteringIntensity` > 0 has `bCastVolumetricShadow`; L33/L34 and enclosed lights have scattering 0; neon proxies have `SpecularScale` 0 and flux = tube flux / pi (+-5 %); no source radius / length of 0; every state change ramps >= 0.8 s |
| VDB-T12 | AfterHours / menu hand-off (UX 6.3-6.5) | the three lighting sublevels load and switch without a one-frame step; `RbCam_Menu_S0..S7` exist with the UX poses; the phone anchor, the Polaroid wall (S7) and the TV-1 render-target input work; the beer clock hands follow the supplied time |

---

## 17. What the product owner needs to do (and when)

| When | Action | Why |
|---|---|---|
| Now (before DB-1) | Confirm or change the identity (bar name, town, night-only, no tobacco, oversized cue ball, green cloth) - section 18 | everything downstream uses these names |
| Before DB-3 (hero pass) | **Buy the Meshy Pro plan for one month** and add the Meshy MCP + API key yourself (Claude never handles keys) | stools, taxidermy, trophy figures, cloth items (H10, M12, M13, M26) |
| Before DB-3 | Check the **Higgsfield plan/credit balance**: budget ~150-250 image generations (brand art ~25, decals ~60, posters/stickers ~40, album art ~40, photos ~20, concepts ~20) + optional TV video (~6 short clips, only if commercial use is allowed) | 2D art layer |
| Before DB-3, optional but valuable | **Reference photos** if you can find a coin-op table (Billard-Cafe/Kneipe near you): overall photo, coin slide close-up, ball window, cue-ball return (and on which face it comes out: end apron or long side, 2.6 A3), corner casting, rail cap with sights, and with a tape measure: bed height, corner and side pocket mouth, throat 2 in back (EQP 15 Q3) - plus a phone video of paying and the balls rumbling out (sound) | replaces ESTIMATEs in 3.1; German tables differ in detail but the mechanics and wear are the same |
| DB-3 to DB-5 | Generate the jukebox music with Gemini from Claude's prompts **after checking Google's terms for commercial use** (roadmap window 3); the track list becomes `jukebox.json` | title strips must match the music |
| DB-6 | Find **15+ blind-test raters** (friends/classmates who have not seen screenshots) and run the 5-minute test Claude prepares (a local HTML page) | the "is it real" proof |
| Before the store page / trailer | Trademark knock-out screening of all fictional names (8.1) with the lawyer who clears "RAW BREAK"; spirits names first (the review already had to rename three) | legal safety |
| Before the store page | Steam AI-content disclosure (Meshy, Higgsfield, generated music) - Claude prepares the text from the ledger | Steam rules since 2026-01-16 |
| Only if a single item fails procedurally | Log into Fab with your Epic account for one logged fallback asset | decisions 2026-09-27 (last resort) |
| Later (NPCs) | MetaHuman creation step for bartender and regulars (ARCH-UE 15) | a bar without people looks staged |

---

## 18. Open questions for the product owner

1. **Identity:** "The Low Bridge Tavern", Port Castor, Ohio, present day, night only - OK? (Alternative: a 1990s period setting; it would change phones, TVs, card reader and the smoking story.)
2. **No tobacco in the scene** (history told by burns and the ban sign only) - OK? (Keeps rating descriptors to alcohol and simulated gambling.)
3. **Oversized 60.3 mm / 221 g cue ball** on a 1984 table as decided, or the modern magnetic 57 mm ball (more common in US bars today; EQP 6.2 default)? This spec follows decisions.md (oversized).
4. **Cloth colour:** bar green (proposed) vs burgundy or blue (burgundy hides the 3/7 balls for colour-blind players).
5. **Money on screen:** folded generic, non-copying banknotes in the tip jar and in money games - acceptable, or avoid visible paper money altogether? (US law restricts realistic currency images; generic designs avoid the issue.)
6. **People:** is it OK that the slice's first captures show the bar at a quiet moment (bartender + 2-4 regulars) until MetaHuman NPCs exist?
7. **Scale of tightness:** ~9 % of shots shaped by the room with the engine's real 0.30 m backswing (2.5): 5 % only need a shorter backswing, 4 % a short cue / other stance / jacked-up cue. More (a second wall-side obstacle) or less (move the cue rack away from the table zone: about -1.3 points, the wall stays)?

---

## 19. Sources

- Valley coin-op ball return and cue-ball separation (size gauge bar for oversized balls, magnetic separator): AzBilliards forum "need some help with Valley ball return", https://forums.azbilliards.com/threads/need-some-help-with-valley-ball-return.8696/ ; HowStuffWorks "How does the ball return work on a coin-operated pool table?", https://electronics.howstuffworks.com/question495.htm (oversized cue ball ~2 3/8 in, separated before the storage compartment, exits at an opening on the side).
- Valley-Dynamo "FAQ guide to older Valley pool tables" (push chute, trap sized to 15 balls, coin door, ball view door, clean-out door, corner castings, laminate), https://www.valley-dynamoparts.com/docs/OldPoolFAQ-Vmk43a.pdf
- Neon tube surface luminances (15 mm, 50-60 mA): Signs of the Times, "Neon Visibility", https://signsofthetimes.com/content/neon-visibility
- Exit signs: NFPA 101 / UL 924 minimum 8.6 cd/m^2, 6 in letters with 3/4 in strokes: https://kordfire.com/exit-sign-requirements-nfpa-101-explained/ , https://www.energystar.gov/ia/partners/prod_development/revisions/downloads/exit_signs/exit_sign_draft2_manny2.pdf
- Bar counter 41-42 in, stool seat 28-30 in, stool spacing 26-32 in: https://www.webstaurantstore.com/article/1045/bar-stool-dimensions.html , https://designingidea.com/bar-dimensions/
- Ohio Smoke-Free Workplace Act (Issue 5 passed 2006-11-07, effective 2006-12-07, enforcement 2007-05-03, no bar exemption): https://odh.ohio.gov/know-our-programs/smoke-free-workplace-program/smokefreeworkplaceactandprogram , https://www.ohiobar.org/public-resources/commonly-asked-law-questions-results/labor--employment/what-you-should-know-about-ohios-smoking-ban/
- CC0 texture IDs: Poly Haven API https://api.polyhaven.com/assets?t=textures and ?t=hdris; ambientCG API https://ambientcg.com/api/v2/full_json (queried 2026-09-28).
- Project specs: ue5-realism-plan.md (4.3-4.7, 6.1-6.7, 7.1-7.3, 9), equipment.md (2, 5.2, 6.2-6.3, 8.2-8.5, 10), human-factors.md (2.1-2.4, 4.5, 5.5), ue-architecture.md (2, 9, 11, 15); Meshy test notes (project memory, 2026-09-27).
- Soft-tip darts: bull 1.73 m (5 ft 8 in), throw line 2.44 m (8 ft) from the board face (common soft-tip standard; steel-tip uses 2.37 m) - ESTIMATE, confirm with the dart machine reference used for M03.
- Review sources (2026-09-28): Valley ball return (cue ball separated and returned to one end of the table): AzBilliards thread above and https://www.billiardsforum.com/pool-table-repair/broken-valley-coin-op-pool-table ; Predator "Crest" chalk: https://www.predatorcues.com/usa/predator-crest-billiard-chalk.html ; Harrogate gins: https://theginisin.com/distilleries/the-harrogate-distillery/ ; "Blue Run Ember" bourbon: https://www.breakingbourbon.com/review/blue-run-ember-bourbon ; "Tanner's Creek" bourbon (why not "Tanner Fork"): https://www.tannerscreekwhiskey.com/ .
- Checked in the installed UE 5.8.3 source (`C:\Program Files\Epic Games\UE_5.8\Engine`): `ULightComponentBase::bCastVolumetricShadow` (not set for local lights, default false), `VolumetricScatteringIntensity` default 1.0, `ULightComponent::SpecularScale`, `URectLightComponent::SourceTexture`, `RectLightAtlas::FAtlasTextureInvalidationScope` used by `MediaTexture.cpp` and `SceneCaptureRendering.cpp`, `ALocalFogVolume` / `ULocalFogVolumeComponent` (`RadialFogExtinction`, `HeightFogExtinction`, `FogPhaseG`, `FogAlbedo`), `r.LocalFogVolume.RenderIntoVolumetricFog` / `MaxDensityIntoVolumetricFog`, `r.VolumetricFog.GridPixelSize` / `GridSizeZ` / `TemporalReprojection`, `r.Streaming.MipBias`, `TEXTUREGROUP_Project01..32` + `[EnumRemap]`.
- Checked in this repository: `ARbTable` (Root = floor, ClothOrigin = +BedHeight; `LampUndersideHeight`, `BallSetSeed`), `rb::EnvironmentSpec::LampFootprint`, `rb::human::MakeVenueTableCondition` / `SeedTableSlope` / `VenueBallSetSeed`, `kTableSevenFootBar` (bed 0.743 m), `URbStrokeComponent::MaxBackswing` 0.30 m, `FRbCueClearanceInput` (Backswing 0.25, Margin 0.001), `RbCueClearance::SweepEnvironment` (stub), `rbsim --geometry` / `--param tilt.slope_x|y`, `rb_common.new_level` (non-partitioned), `rbue.py capture --warmup-seconds`, `Config/DefaultEngine.ini` (no `r.VirtualTextures`, no custom collision channels).
- CC0 IDs of 6.3 re-queried on 2026-09-28 (Poly Haven and ambientCG APIs): all 16 Poly Haven IDs and all 22 ambientCG IDs exist.

---

## 20. Review log (adversarial review, 2026-09-28)

Reviewer: Claude (review of Draft v1 against decisions.md, the realism plan, human-factors, equipment, ue-architecture, trailer-plan, ui-ux and audio specs, the installed UE 5.8.3 source and this repository's code). Everything below is fixed in place above; hand-offs are requests to files this spec does not own.

### 20.1 Findings and fixes

| # | Severity | Finding | Fix (section) |
|---|---|---|---|
| R-01 | high | **The storage door opened into the men's room.** E19 (back wall, Y 2.59 - 3.50) led into the rear addition, whose men's room covers X 16.66 - 18.24, Y 1.52 - 4.27. TS-6 claimed keg traffic behind the foot end, but that door was nowhere near the foot end. | Rear addition made full width with a keg cooler (Y 4.47 - 7.12); E19 moved behind the foot end (Y 4.55 - 5.46, swings inward); back ledge shortened to Y 5.60 - 7.10; path P5; plan regenerated from the element boxes (2.2, 2.3, 2.4, 2.6) |
| R-02 | high | **Cue-sweep numbers used a 0.15 m backstroke; the engine sweeps with 0.30 m** (`URbStrokeComponent::MaxBackswing`, "also the clearance sweep's backswing"; plan 5.5 says 0.25). "Full cue fits by 3.3 cm at the foot end" was false in-engine (blocked by about 9 cm), and VDB-T3 (+-2 cm) could never pass. The 2D model also ignored the drink ledge at cue-butt height. | 2.5 recomputed in 3D with the engine parameters (tip on the ball surface, rail-bridge elevation, ledge, shelf, jukebox and rack heights): 9.3 % room-shaped (4.9 % shortened backswing, 3.8 % short cue, 0.3 % no level cue, 0.4 % body), new perpendicular thresholds, foot-end values 0.224 / 0.30 m; decisions item 4, TS-1, TS-6, VDB-T3, Q7; hand-off H-1 |
| R-03 | info | Reproducibility of 2.5: the review re-implemented the sweep (scratch script: 5 cm x 72-direction grid, capsule chain sampled every 2 cm, tapered radius + 1 mm margin vs 3D boxes and cylinders; elevation max(4 deg, atan((rail top + 20 mm - contact z) / max(0.12 m, distance to 0.08 m past the nose))), contact 15 mm above the ball centre when the rail term applies; body disc r 0.20 m at 0.95 / 1.10 / 1.25 m; reach = bridge point 0.20 m behind the ball more than 1.00 m inside the rail outline). With v1's 0.15 m backstroke it gives 6.1 % (v1: 5.8 %), so v1's model was sound; only its parameter was not the engine's. | DB-B commits `cue_sweep_check.py` with this model reading `layout.json` (13.1, 15) |
| R-04 | high | **Volumetric haze would glow around the lamp as a sphere.** In 5.8, local lights default to `VolumetricScatteringIntensity` 1.0 and `bCastVolumetricShadow` false, so the shades would not cut the haze cone, and lights behind walls (street lights, restroom leaks) would light the fog inside the room. | Rule in 4.1, flags per light in 4.2 and 4.7 (key bulbs, neons and headlight with volumetric shadows, all others 0), validator VDB-T11, budget line 11.2 |
| R-05 | medium | **Neon proxy flux was 1.6x too high on axis** ("about half the flux"), and "10-30 lux at 1-2 m" understated the bright signs 3-6x. | DERIVED `Phi_rect = Phi_tubes / pi` from the cylinder intensity `I = L d l = Phi / pi^2`; per-sign intensity/illuminance table; N4's effect on the wall-side rail caps (4.3) |
| R-06 | medium | **Double lighting from emissive neon** (tubes in Lumen GI + proxy rect) was not addressed. | Tubes: Affect Dynamic Indirect Lighting off, VERIFY that hit-lit reflections keep them, fallback (4.1) |
| R-07 | medium | **Two neon signs had no wall to hang on:** N5 "above TV-2" left 0.24 m below the ceiling; N3 "above the mirror" collided with the mug rack. | N5 on the back wall Y 2.40 - 3.40 (in the V03 and TH-1 frames), N3 at X 7.65 - 9.35 beside TV-1, mug rack limited to X 1.95 - 5.95 (E04, 4.3) |
| R-08 | medium | **The coin-op ball path ran uphill:** the return cup (Z 0.50) sat above the separator (Z 0.44). The cue-ball return on the long side also contradicted EQP 5 and UX 9.12 ("end apron"). | Falling gully chain with cross channel, separator and trap row; cup in the foot-end apron (floor Z 0.38); side variant kept as a generator option, the owner's photos decide (3.1, 3.3, 2.6 A3, 17) |
| R-09 | medium | **Ceiling fan strobing:** 60 rpm (5-blade pass ~5 Hz > 3 Hz) with the window neons, headlight sweeps and TV-1 all shining through the blade disc; the "fan never between a light and a surface" claim was not true. | 4 blades at 40 rpm (2.7 Hz), blades cast no shadow (E24, 4.7) |
| R-10 | medium | **UI requests missing:** AfterHours lighting state, sublevels, menu station cameras, phone anchor, TV-1 as a replay screen, runtime beer-clock hands, the chalkboard as the live score slate (UX 6.3-6.5, 9.3). v1 baked the chalkboard text (TXT), which would have hidden foul marks and match scores. | AfterHours state + props + rain (4.6), EV rows (4.5), H13 / M04 / M14 / M24, 8.2 fonts (Kalam), 8.4, cameras (12.1, 12.2), VDB-T12 |
| R-11 | medium | **"Needs an owner: TableSpec exporter"** is not needed: `rbsim --geometry` already writes the single-source-of-truth geometry for render-mesh generators; verified by running it for `7ft-bar`. | Command and field list in 13.1; bed height via `layout.json` + validator; DB-D no longer owns an exporter (15) |
| R-12 | medium | **ARbTable placement was a VERIFY:** the header states Root = floor and ClothOrigin = +BedHeight. The physics lamp footprint (`EnvironmentSpec::LampFootprint`, default: the whole table under the lamp) was not handed over. The venue-seed call did not use the real API. | Actor at (13.759, 5.427, 0), yaw 0 (2.1, 13.8); footprint values (E14); `MakeVenueTableCondition` / `VenueBallSetSeed` with the real signature, a seed search, and a two-roll behaviour test that does not depend on the slope-vector sign (3.2, VDB-T10); hand-off H-2 |
| R-13 | medium | **GPU budget** summed to 11.0 of 11.1 ms and left out the first-person body (0.3 ms in UE 9.2). | Rebalanced to 10.5 ms with the body; target mean 10.3 / P95 11.1 ms, gate 16.7 ms (the plan's 60 fps floor); ordered levers; TSR stand-in while there is no DLSS plugin (11.1, 11.2, VDB-T5) |
| R-14 | medium | **Virtual textures are not enabled** in `Config/DefaultEngine.ini`, but 11.4 (SVT) and the floor RVT depend on them. | Hand-off H-3 with a baked-mask fallback (6.1) |
| R-15 | medium | **Brand screening:** "Crestline" chalk sits next to Predator's real "Crest" chalk (and the HF spec already names the bar cube "Rail Rat"); "Harrowgate" gin next to several Harrogate gins; "Bell Hollow" bourbon with a bell emblem next to Bell's whisky. | Renamed to Rail Rat, Wexmoor, Ashby Ridge (no bell); Ember flagged; spirits screened first (8.1, H06, 17) |
| R-16 | medium | **Off-table balls** (decision 2026-09-28) were not in the collision rules: stool hulls as boxes would stop a ball that should roll under them, and unreachable gaps would lose balls. | Physical materials, leg-accurate hulls, kick plates, a bartender return volume (13.5) |
| R-17 | low | Lights-Up EV target 6.5 - 7.5 was office-level; 4 x 7000 lm in 120 m^2 gives ~128 lux (EV 5.7). | 5.5 - 6.5 with the derivation (4.5) |
| R-18 | low | "EXIT sign visible from the table through the corridor opening": geometrically it is visible only from Y < ~1.8 m; TH-1 claimed the EXIT glow in frame. A bar of this size also needs a signed main exit (NFPA 101 assembly occupancy), which the Quit station (UX S6) can use. | E21 and TH-1 corrected; EXIT sign L25b at the front door (E02, 4.2) |
| R-19 | low | Captures: the default 4 s warm-up cannot adapt the Eyes exposure (0.7 EV/s) down to EV 3; captures must use `ARbLookDevCamera`s (ARCH 9.4); V10 referred to a data layer in a non-partitioned level. | `--warmup-seconds 12` (4.5), camera rule and tags (12.2), tag-based hiding (V10) |
| R-20 | low | Raw CC0 downloads in LFS (several GB); `Shaders/Private/*.ush` belongs to UE-3; the ledger is shared with the audio and UI specs. | `Art/Third/` git-ignored with SHA-256 pins and a sha256 ledger column; shaders in `Shaders/Private/Venue/`; ledger append-only (13.1, 13.9, 15) |
| R-21 | low | Wrong internal references (4.5 "section 12.3", 5 "texel rule 13.5", M14 "prompt rule 13.6", DB-1 "(12.3, 12.4)", DB-6 "(12.5)"); lamp size 1.22 m vs 1.28 m over the shades; beer clock "12 minutes slow" vs the UI's clock rule and the real "bar time" habit (bar clocks run fast); Christmas lights "in September"; the blind-test pass had no real-photo control. | All corrected (4.5, 5, 12, H03, M24, S12, S19, 12.4) |
| R-22 | info | Verified and unchanged: table outline and noses, head string / foot spot, rail-to-wall, column and jukebox distances, the lux values of 4.4 (bed centre 828, corner nose 212, floor spill 156 / 84: re-derived), cloth EV 8.30, neon lm/m, pendulum period, dart geometry, US quarter, 555-01xx numbers, every Poly Haven / ambientCG ID, the Ohio smoking-ban dates, the Steam AI-disclosure date (matches audio.md 9.8). | - |

### 20.2 Hand-offs to other owners (nothing here edits their files)

| ID | To | Request |
|---|---|---|
| H-1 | UE-4 (cue) / UE-5a (stroke) | (a) Shortened backswing: when the 0.30 m sweep fails but a >= 0.10 m sweep passes, clamp `MaxBackswing` for that shot to the free length (butt tap = HF-32 tell) before offering a short cue. (b) A trace channel `RbCueSweep` for `SweepEnvironment` (with H-3). |
| H-2 | UE-1 / UE-6a (`ARbTable`, table context) | Expose `LampFootprint` (core-frame AABB -> `EnvironmentSpec::LampFootprint`; dive bar x [-0.650, 0.650], y [-0.210, 0.170] m) and the venue inputs `VenueSeed`, `VenueKind`, `TableIndex`, `bFirstCareerTable` (-> `MakeVenueTableCondition`, `VenueBallSetSeed`). Header additions only. |
| H-3 | UE-0 / UE-8 (config, `RbAssetPaths`) | `r.VirtualTextures=True` before DB-3; `[EnumRemap] TEXTUREGROUP_Project01.DisplayName=RB_Cinematic8K`; collision profiles `RbVenueBlock`, `RbVenueProp` and the `RbCueSweep` channel; capture tags `RbCam_DB_*` and `RbCam_Menu_*` in `RbAssetPaths::CaptureCamera`. |
| H-4 | rbsim owner | Optional: `bedHeight` in `WriteGeometry`. |
| H-5 | UI (ui-ux.md) | Station poses are taken from UX 6.3 as written; the venue adds the phone anchor, the S7 wall position (E08) and the AfterHours state (4.6). The cue-ball return is an end-apron cup (matches UX 9.12). |
| H-6 | Audio (audio.md) | The main floor is VCT on concrete (audio.md 12.1 says "worn wood"); acoustic material tags and zones come from the venue metadata (10). |
