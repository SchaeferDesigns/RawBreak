# Table and cloth look-dev: references and verdicts (M2-L)

Owner: M2-L. Plan: `Docs/ue-architecture.md` 18.7. Captures: `python Tools/unreal/capture_table.py` ->
`Docs/images/dev/m2l/{9ft,7ft}_{chin_on_cue,standing,pocket_closeup,cushion_grazing,rail_closeup,overhead,foot_end}.png` and the
comparison sheets `{9ft,7ft}_sheet.png` (the views next to the checklist of this file); every round keeps its own files
(`--suffix _r<N>`), the unsuffixed files are the final round. Look-dev levels: `Tools/unreal/editor/rb_dev_m2l.py`
(`/Game/Dev/M2L/L_TableLookDev_9ft` = the M1 room, `/Game/Dev/M2L/L_TableLookDev` = the 7-ft bar box under an emulation of the
dive bar's 3-shade lamp). Both carry a known-albedo card (0.80 / 0.18 / 0.04) on the bed. One round:
`rbue.py py rb_make_materials.py` (+ `rb_bake_table.py` after a geometry change), `rbue.py py rb_dev_m2l.py`,
`capture_table.py --suffix _r<N>`.

Reference photos are **listed, never committed** (their licences do not allow it). The owner's photos of his local pool bar replace
this list when they arrive (`Docs/references/owner/`, the owner's own photos may be committed).

## 1. Reference list (URL + what to compare)

| # | Source | What to compare |
|---|---|---|
| R1 | Diamond Pro-Am, manufacturer page: <https://diamondbilliards.com/pages/pro-am> | 9-ft pro table proportions: rail width vs bed, the flat-topped rail with a small outer round-over, mother-of-pearl sights, drop pockets with leather, the apron and legs |
| R2 | Diamond Pro-Am 9 ft retail listing (many angles, black finish): <https://dlbilliards.com/products/9ft-pro-am-pool-table-in-black> | pocket cut and liner at the corner pocket, cushion nose roll and the cloth fold at the facings, apron construction, levelers |
| R3 | Diamond Pro-Am, second listing: <https://weststatebilliards.com/product/pro-am-pool-table/> | standing view of the whole table, lamp pool on the cloth |
| R4 | Simonis 860 tournament cloth: <https://www.simoniscloth.com/product/simonis-860/>, colour card <https://seyberts.com/products/tournament-blue-860-simonis-cloth> | worsted cloth: tight weave, no nap, tournament blue hue and darkness, sheen at grazing angles, ball burns / chalk on a used 860 |
| R5 | Valley Panther ZD-11 (the coin-op "bar box" the fictional HALVERSON Stallion 7 stands for): <https://www.gameroomshop.com/products/valley-panther-black-cat-pool-table-zd-11-coin-operated> | cabinet box, black rail caps with white dot sights, corner / side castings in black plastic, aluminium trim bands, pedestal legs, push-chute coin mechanism, ball-return window on the foot end |
| R6 | Valley Panther ZD-12 (dealer): <https://www.betson.com/amusement-products/panther-zd12/> | foot-end face: coin slide, trap window, ball tray; castings from above |
| R7 | Valley woodgrain cabinet (Highland Maple listing): <https://www.gameandsportworld.com/products/valley-panther-highland-maple-pool-table-vp-hmt> | printed woodgrain laminate on the cabinet, trim, laminate corners |
| R8 | Valley parts catalogue (castings, liners, cabinet parts drawings): <http://www.valley-dynamoparts.com/docs/2019ValleyPoolWeb.pdf> | casting shapes and openings, gully / ball-return construction, what the throat below a pocket looks like |
| R9 | Championship Titan (napped bar cloth): <https://www.properpool.com/cloth/championship-titan-cloth> | napped woolen bar cloth: fuzz, nap direction, pilling, how bar green reads under a bar lamp |
| R10 | AzBilliards forum thread on bar-box cloth (photos of used Valley tables): <https://forums.azbilliards.com/threads/championship-cloth-question-for-valley-bar-box.268096/> | worn bar cloth: break-spot burns, the head-string lane, frayed pocket points, chalk at the corners |

## 2. Checklist per view

| View | Compared against | Checks |
|---|---|---|
| chin_on_cue | R1, R4, R9 | cloth fibre sheen at grazing angles, the far cloth lighter and hazier; cushion nose roll and the rubber lip; one lamp highlight per ball; the cloth albedo against the grey card (target 0.05-0.10 linear) |
| standing | R1, R3, R5 | proportions; lamp pool with falloff to the corners; wear lanes and chalk readable from 1.65 m; lacquer (9-ft) / black laminate (7-ft) |
| pocket_closeup | R2, R6, R8 | facings follow the cut, cloth folded at the facing; liner / leather / casting opening without gaps; bucket (9-ft) or gully throat (7-ft); frayed cloth and chalk at the mouth |
| cushion_grazing | R2, R4 | straight nose, rounded nose, rubber lip; no moire at 3 cm; sights flush |
| rail_closeup | R1, R5, R7 | rounded cap edge (real geometry), sight inlays, burns / rings on the bar table's caps; apron veneer (9-ft) / trim and laminate cabinet (7-ft) |
| overhead | R1, R5 | symmetric pockets, the wear map (break spot, head-string lane, rack impression), sight spacing |
| foot_end (extra) | R5, R6, R7 | 9-ft: foot rail, apron, legs and levelers; 7-ft: coin slide in its chrome plate, trap window, ball tray, cue-ball return ring, trim bands, the cabinet's worn corners and kick grime |

## 3. Decisions of M2-L

* **Body in C++, not Blender.** The plan allowed the non-physics body to come from Blender (`Tools/blender/table/**`). Every body part
  is built by `RbTableMeshBuilder` from the same `rb::TableGeometry` as the playfield instead: the castings' openings, the rail cuts
  and the cabinet outline follow the pocket geometry exactly (the >= 2 mm casting clearance is a unit test, not an import check),
  the bake is one step, and a TableSpec change re-shapes the body. `Tools/blender/table/tb_build_all.py` only validates the
  `rbsim --geometry` exports in `Art/Tables/` against the cabinet dimensions of venue-dive-bar 3.1; `rb_import_table.py` removes
  stale Blender imports and imports nothing.
* **Wear is placed from the table-local position** (`LocalPosition` of the baked parts = the table frame): break spot, head-string
  lane, rack impression, pockets and cabinet corners need no UV1 masks. The table dimensions the layers need are material
  parameters (`RbTableMeshBuilder::GetMaterialTableParameters`); the generated materials carry the two committed presets' values,
  and `ARbTable` gives any other preset a transient dynamic instance with its own values (and a table with `TableIndex` > 0 its own
  wear `Seed`), so the wear follows every preset and two tables in one room never wear identically
  (`RawBreak.Unit.Table.Look.MaterialParameters`, review).
* **Table materials carry their own `Age`** (dive bar 0.80; the M1 room's 9-ft club table 0.30, r5: 0.15 read as a CG-clean cloth)
  and never read `MPC_DB_Venue`.
* **Trap window**: an opaque dark gloss approximation in M2 (the trap contents are not rendered yet).
* **Look-dev exposure calibration** (`ARbLookDevCamera::ExposureBiasEv`, `rb_m1_layout.LOOKDEV_EXPOSURE_BIAS_EV`: 7-ft -1.5 EV, 9-ft
  -1.0 EV). Measured on the grey card in r4: the Eyes metering (histogram 70-95 %, centre-weighted) maps the lit cloth to about middle
  grey, so the 0.18 card rendered at sRGB ~225 (7-ft) / ~190 (9-ft) and the 0.04 card near middle grey; the tone curve's shoulder then
  turns the cloth pastel (the r0-r4 "painted plastic" look was mostly this). The dark 7-ft room adapts to ~EV100 5.5 at the standing
  view, 1.5 EV below venue-dive-bar 4.5's V03 band (6.5-7.5); with the bias the card reads sRGB ~175 under the lamp (cloth albedo from
  the card: ~0.07, inside the 0.05-0.10 target). The bias is look-dev only: the M1 acceptance cameras (A7) and the player's rig keep
  the Eyes law, so the finding goes to M2-F / M2-A (request in the package report). A view framed mostly on the dark body (the
  foot end) carries its own offset on top (`LOOKDEV_VIEW_EXPOSURE_OFFSET_EV`, r9; -2.25 EV since r11, chosen by measuring the
  cloth against the standing view's lamp-centre cloth).
* **Foot-end view** (extra, `ERbTableLookDevView::FootEnd`): the six planned views never showed the coin-op cabinet's foot end
  (coin slide, trap window, ball tray, cue-ball return); a seventh view bent over the foot end (eye 1.15 m above the floor) does.

## 4. Verdicts (final round = the unsuffixed captures: r8; 7-ft rail_closeup from r10; foot_end (both tables) from r11)

Claude's written verdict per view against the checklist of section 2 and the reference list of section 1 (web references; the
owner's verdict follows in the M2 playtest). "Reads as real" = a viewer who does not know it is a render would take the frame for a
photo of a real table in that room; the rooms themselves (the M1 grey box, the black 7-ft box) are not judged.

| View | 9-ft pro (M1 room) | 7-ft coin-op bar box (bar-lamp room) |
|---|---|---|
| chin_on_cue | **Reads as real.** Tournament blue at a photo-like depth, faint mottling and brushing streaks, the far cloth a little lighter; cushions with the nose highlight and the dark undercut face; one lamp highlight per section in the balls. The cushion faces are a touch too uniform. | **Reads as real.** Rich bar green under the lamp pool with the falloff to the far rail, crushed-nap burns at the head string, the lighter lane toward the rack; black caps with white dot sights. |
| standing | **Reads as real** as a well-kept club table: proportions of R1, lacquered walnut caps, the lamp's even WPA light; wear is subtle at Age 0.30 by design. | **Reads as real.** The bar look of R5 / R10: dark cabinet in a dark room, the lamp pool falling off toward the corner pockets, a visible break-box burn, the head-string lane and mottled worn nap; chalk at the corners is too faint to read from 1.65 m (R10 shows more). |
| pocket_closeup | **Plausible, not yet convincing**: the jaws, facings and the leather drop read right, but the black liner wall above the cloth is a perfect cylinder and the liner collar ring on the cap (black rubber, `M_RbCushionRubber`) is too clean (R2 shows a stitched, slightly rounded leather lip). | **Plausible**: casting, rubber liner and the dark gully throat are right in shape and tone; the casting opening has a sharp top edge where a real ABS casting has a moulded lip (not added: it would sit inside the physics' back wall, 18.7 hard rule). |
| cushion_grazing | **Reads as real**: straight nose line, rubber lip at the base, sheen at grazing, no moire at 3 cm. | **Reads as real**: felt-like nap without a weave, rubber lip, no moire. |
| rail_closeup | **Reads as real**: real rounded cap edge, mother-of-pearl sights flush, lacquer reflections with depth, veneered apron. | **Reads as real**: rounded black laminate with a matte burn smudge, satin black castings at the pockets, aluminium trim band with its grooves, woodgrain cabinet. |
| overhead | Correct and symmetric; the lamp sections reflect in the lacquer as pale bands (physically right, a known look of lacquered tables under box lamps). | Correct and symmetric; the wear map (break box, lane, cushion tracks) reads without looking painted; the rack impression is hidden under the rack. |
| foot_end (extra) | Legs with plinths and levelers, apron end: reads as real furniture; since r11 the cloth has the tournament blue of the standing / chin-on-cue views (r9 was still ~1 EV lighter). | Coin slide in the chrome plate, trap window, ball tray, cue-ball return ring, trim bands: reads as a coin-op cabinet. Since r11 the cloth has the standing view's bar green (r9 / r10: mint pastel, ~1.3 EV over); at that exposure the cabinet face, lit only by bounce light in the black look-dev room, is dark and the fittings read by their highlights - judging the cabinet face itself belongs to the DB-3 views in `L_DiveBar` (more ambient light). The trap window is an opaque dark approximation (no balls behind it yet), stickers / serial plate are M2-B / DB-5 text work. The chrome coin plate picks up the warm room and reads a little beige. |

Open (next look-dev round, after the owner's photos): moulded casting lips and a stitched leather pocket lip (both need a way to
round the top of the physics' back wall without moving it), a translucent trap window with the pocketed balls behind it, stickers
on the cabinet, the cue-ball oval "HALVERSON" stamp, owner-photo comparison replacing the web list.

## 5. Rounds

Sheets per round: `Docs/images/dev/m2l/{9ft,7ft}_sheet_r<N>.png` (a round that captured only some views shows the others as
missing); the full-resolution frames of the final round are the unsuffixed files.

| Round | What changed | What the captures showed (-> next round) |
|---|---|---|
| r0 | First bake of the look-dev body (rounded caps, nose roll, rubber strip, drop pockets / gully throats, cabinet, castings, trim, hardware, window) and the first wear layers | 7-ft cloth teal and flat, stains as clean dark discs, glass rings drawn like circles, castings read as grey metal; 9-ft rail wood like pine |
| r1 | Stains as uneven blotches with a tide ring, rings fainter, walnut stain darker (StainTint 0.14 / 0.10 / 0.08), bar green toward venue-dive-bar 3.2 | 9-ft walnut reads as walnut; bar cloth still "painted plastic" up close |
| r2 | Napped cloth mottle (tufts at 0.5-2 cm), pilling, the nap sheen with / against the nap | felt-like up close; everything still washed out |
| r3 | Pocket cavity darkening (liners, buckets, gully throats), casting / liner tones | pockets read as holes, not as lit cups; the cloth still pastel |
| r4 | Regenerated everything from the resumed branch (baseline of this session) | grey card measured: 0.18 card at sRGB ~225 (7-ft) / ~190 (9-ft), 0.04 card near middle grey: the Eyes metering over-exposes the table by 1.5-2 EV, the tone curve turns the cloth pastel; wear invisible at 1.65 m even at Age 0.80; burns on black laminate read as rings |
| r5 | Look-dev exposure calibration (7-ft -1.5 EV); worn nap stronger (lighter, greyer backing), cushion tracks, the break-box burn (centre and the two 9-ball break spots), rack impression, chalk, miscue marks, foot-spot sticker, stains; 9-ft Age 0.15 -> 0.30; glass rings as broken arcs | 7-ft reads as a bar table for the first time (rich green, lamp pool, break box and lane visible); castings still lighter than the laminate |
| r6 | 9-ft exposure -1.0 EV; burns on black laminate as matte brown-grey smudges; the extra foot-end view | 9-ft tournament blue at photo depth; foot end: coin slide, trap window, tray and return ring read as a cabinet; castings still grey |
| r7 | Satin ABS (roughness 0.26, subtler scuffs) | castings black like the caps; chalk at the corners too faint from 1.65 m |
| r8 | Corner chalk dust stronger (pocket mouths, larger smudges) | the verdicts of section 4; the foot-end views meter on the dark cabinet / apron and the cloth clips to pastel |
| r9 | Per-view exposure offset (`rb_m1_layout.LOOKDEV_VIEW_EXPOSURE_OFFSET_EV`, foot_end -1.25 EV; -0.75 EV still left the cloth ~1.3 EV above the standing view); only the foot-end views re-captured | cloth back to its chin-on-cue / standing tone in the foot-end views, the cabinet stays readable; regeneration idempotent (`rb_make_all.py --strict --compare`: metrics identical) |
| r10 (review) | Trim bands 4.5 / 3.5 cm -> 3.5 / 2.5 cm: the top band ran over the coin plate's top (plate 0.56-0.68 m, band from 0.6726 m), the bottom band over the ball tray opening (tray from 0.33 m, band to 0.335 m); `RawBreak.Unit.Table.Look.FootEndFit` checks every foot-end fitting lies between the bands. Re-captured 7-ft foot_end and rail_closeup only | the fittings sit between the bands; the rest of the 7-ft views are unchanged from r8 / r9 |
| r11 (review) | Foot-end exposure offset -1.25 -> -2.25 EV: measured on the r9 / r10 captures (relative luminance of the sRGB cloth samples) the foot-end cloth was ~1.3 EV (7-ft, mint pastel) / ~1.0 EV (9-ft) above the lamp-centre cloth of the standing view, not "back to its tone". Re-captured foot_end of both tables | cloth luminance 0.20 vs standing 0.18 (7-ft), 0.08 vs 0.10 (9-ft); the 9-ft apron and legs stay readable, the 7-ft cabinet face goes dark (bounce light only) with the fittings readable by their highlights |
