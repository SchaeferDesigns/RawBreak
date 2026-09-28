# RAW BREAK — UI/UX Specification

| | |
|---|---|
| Document | `Docs/specs/ui-ux.md` (prefix **UX**) |
| Status | Draft v1.1 (2026-09-28): complete screen and flow spec, visual tokens, settings catalogue, Unreal route, tests; **adversarially reviewed** against decisions, the realism plan, HF, rules, equipment, the dive-bar venue spec and the merged UE-2 / UE-6b / UE-8 code (section 24, findings R-01..R-26, all fixed in place). Engine facts checked against the installed UE 5.8.3 source (marked **[5.8 ✓]**); everything else that depends on third-party plugins or platform rules is marked **VERIFY**. |
| Scope | Every screen and every piece of information the player sees or hears as feedback: first launch, main menu, career hub (phone), match setup, in-game flows, replay, photo mode, pause, settings, notebook, save/load, Steam hooks, typography/colour tokens, localisation, accessibility, tests, work packages. |
| Sources of truth | `Docs/decisions.md` (no HUD by default, Eyes/Headcam, singleplayer + hot-seat, fictional brands, quality-first presets, money games, alcohol cosmetic, multi-table halls and balls off the table 2026-09-28); `Docs/specs/venue-dive-bar.md` (**VDB**: layout 2.3, lighting 4.x, chalkboard H13, text 8.4); `Docs/specs/ue5-realism-plan.md` (**UE**: camera 4.x, stroke input 5.4, rendering 6.x, scalability 9.4, Steam 10); `Docs/specs/human-factors.md` (**HF**: principles 1-9, chores HF-70..79, assists 5.4, diagnosis 3.9); `Docs/specs/rules.md` (**RUL**: 4.4-4.8, 7.2-7.3, 11, 16 items 2/9/12/16); `Docs/ue-architecture.md` (**ARCH**: 6.5 input, 6.6 overlay + mandatory lines, 6.7 replay, 8.1 presets); `Docs/trailer-plan.md` (the "is it real" hook). |
| Code (proposed) | `Source/RawBreak/{Public,Private}/UI/**` (Slate), `Settings/**` (registry), `Save/**`, `Platform/**` (Steam), `Content/UI/{Fonts,Glyphs,Icons}/**`, `Content/Localization/**`, `Tools/ui/**` (glyph + string checks). Nothing in this document changes files owned by running work packages; required contract changes are listed in 3.9 for the architect. |

Tags: **DERIVED** worked out here, **ESTIMATE** starting value to tune, **TUNING** gameplay value, **VERIFY** external fact to re-check, **[5.8 ✓]** verified in the installed engine source, **PO** product-owner question (section 22).

---

## 0. Decisions in this spec (summary)

1. **Zero screen-space pixels in normal live play.** Only five screen-space elements may ever appear while playing, each only while it has something to say: the **foul pips** (always while any foul count > 0 — decisions + RUL 16 item 12), **mandatory lines / decision card** (only while a rules obligation is pending), **subtitles/captions** (setting), the **glance card** (on demand) and **interaction prompts** (only for an action that is possible and meaningful right now, 3.5). Everything else is diegetic: chalk tallies, the character's phone, the notebook, voices, chalk on the tip. All of it belongs to the player's own match; other tables in the room are diegetic only (2.4).
2. **UI technology: Slate only, in C++.** No UMG widget blueprints, no CommonUI. Diegetic surfaces (phone, notebook, chalkboard) are Slate widgets rendered into render targets with `FWidgetRenderer` [5.8 ✓]. Reasons in 3.1.
3. **Main menu = "Closing Time"**: the dive bar after hours as a live 3D scene with camera stations. It reuses the first venue, warms its shaders, and starts Practice in place without loading (6.2).
4. **Career hub = the character's phone** (six apps: Messages, Tonight, Flyers, Money, People, Photos), readable by a raised pose with a focus FOV of 36°, and a flat fallback that renders the same widget in screen space (7).
5. **First launch** = 6 short steps + summary, ≤ 3 min with calibration, ≤ 45 s when skipped; shader warm-up runs in parallel (5).
6. **Settings are data-driven**: one registry row per option (≈ 110 rows), live apply with preview-behind-panel, per-page reset, preset → Custom on any change (13).
7. **Shot calling by gaze + gesture** (hold `C`, look at ball, look at pocket, release) with Q/E cycling as fallback; declarations (push-out / safety) on `X` (9.4-9.5).
8. **Replays have two looks**: *Phone* (handheld, the trailer's cold-open look; bar venues) and *Broadcast* (TV cameras, score bug; arena) (10).
9. **Visual language**: "after-hours documentary" — chalk-white type on smoke-black, one amber accent; IBM Plex Sans/Condensed/Mono for flat UI, Caveat (notebook) and Kalam (chalkboard) for handwriting; all SIL OFL (4).
10. **German and English from day one**, string tables in CSV, pseudo-localisation with +40 % length in CI (17).
11. **Steam**: achievements/stats, rich presence, Timeline markers and game phases, screenshot library, Auto-Cloud, Steam Input glyphs, Deck-safe text sizes (16).
12. **Engine finding (settings), already handled by UE-8:** in 5.8 the engine's GI groups 1-2 set `r.Lumen.HardwareRayTracing.HitLighting.Allowed=0` and only GI 3/Cine allow hit lighting (BaseScalability.ini) [5.8 ✓]. The merged UE-8 presets (preset level = `sg.*` level 0..4) solve it with `[RawBreak.ReflectionQuality@2..Cine]` rows that set `HitLighting.Allowed=1` and `LightingMode=2` at `ECVF_SetByGameOverride` priority. The settings registry must keep that order (engine group first, RAW BREAK rows after) whenever a single row changes, and UX-T02 reads the value back (13.3).

---

## 1. Principles (normative)

| ID | Principle | Concrete rule |
|---|---|---|
| UX-P1 | **Real footage first.** | In live play with no pending rules obligation, no fouls, nobody speaking, no key held and no actionable interactable gazed at, the frame contains zero UI pixels. The trailer's cold open must be capturable from normal play without a "clean" mode. |
| UX-P2 | **Rules obligations beat aesthetics.** | Foul counts > 0 incl. the two-foul warning, a pending decision with its options, ball in hand with its region, a declared push-out/safety, the call where calling is required, a running shot clock ≤ 10 s, and the enforced foul with its rule reference (RUL 16 items 2, 12; ARCH R-05) are shown on screen in **every** mode, including "Diegetic-only". |
| UX-P3 | **Diegetic → glance → screen.** | Each piece of information has a diegetic carrier first (section 2), the glance second, a screen element only when P2 demands it. |
| UX-P4 | **Assists are options.** | Default difficulty is *Real* (HF 5.4): no aim line, no ghost ball, no tip ring. Active assists are named on the glance card and the score slate ("Anna (Assisted)"). |
| UX-P5 | **Menus are always readable.** | Flat UI is composited by Slate after post-processing, so grain, DoF, motion blur, Headcam distortion, exposure and alcohol blur never touch it. Contrast ≥ 4.5:1 against the worst-case background (4.1), minimum text sizes of 4.2, UI at its own HDR UI luminance (default 203 nits, 13.4). |
| UX-P6 | **Respect time.** | No unskippable step except language and the first shader warm-up gate (skippable after 10 s). Every flow in this spec lists its input count; chores follow HF principle 5 (R/A/C/P). |
| UX-P7 | **One input model per context, all rebindable.** | Every action is a player-mappable Enhanced Input action [5.8 ✓]; every hold action has a toggle alternative; mouse-only, keyboard-only and controller-only are complete. |
| UX-P8 | **Hot-seat fairness.** | Both players see identical information; turn changes are announced; the stroke is never armed until the incoming player confirms (setting, default on). |
| UX-P9 | **Honest feedback.** | No invented numbers. Attribute values stay hidden (HF Q3); the stroke report uses the core's diagnosis shares (HF 3.9); AI strength is described in words. |
| UX-P10 | **Photosensitivity and comfort.** | Nothing flashes more than 3 times per second anywhere (neon, TV, lamp, UI); every camera effect has an off switch; *Reduced motion* is one master switch (13.10). |
| UX-P11 | **Venue language vs UI language.** | Rendered text (phone, notebook, chalk slate, menus) follows the UI language. Printed props follow the venue (US English in the dive bar, German in the Kneipe) and are never required to play; anything gameplay-relevant printed on a prop is duplicated on the phone or in the notebook. |

---

## 2. Information architecture

### 2.1 Information map (what the player needs, and where it lives)

| Information | Diegetic carrier (default) | Glance card | Screen element (P2) |
|---|---|---|---|
| Score / race / rack number | Score slate by the table (bar), wall scoreboard (hall), LED board (arena) | yes | — |
| Consecutive fouls per player | Slate: circled "F" per foul, wiped at reset; opponent/referee: "That's two." | yes | **Foul pips** while > 0 (9.1) |
| Two-foul warning | Voice line + second "F" | yes | **Foul pips** in red with text |
| Enforced foul + rule reference | Referee/opponent call ("Foul — no rail.") | yes | **Result line** 4 s after the shot (ARCH auto-glance) |
| Ball in hand + region | Opponent rolls/hands the cue ball over; coin-op: cue ball in the return | yes | **Mandatory line** until the stroke |
| Pending decision + options | Opponent/referee asks ("Your call — shoot or give it back?") | yes | **Decision card** (9.7) |
| Declared push-out / safety | Voice ("Push out.") | yes | **Mandatory line** until the stroke |
| Called ball + pocket (where required) | Pointing gesture + voice; 8-ball marker coaster at the pocket (HF-76) | yes | **Mandatory line** while down (explicit call mode only) |
| Frozen ball auto-declaration (RUL 4.11) | Referee call in the arena | yes | **Mandatory line** while down on that shot |
| Stalemate warning | Opponent/referee line | yes | **Mandatory line** with turns left |
| Shot clock (optional) | Referee counts / arena clock | yes | **Clock line** from 10 s |
| Whose turn (hot-seat) | Slate arrow, the cue handed over | yes | **Turn line** 3 s at the change |
| Lowest ball / group | The table itself | yes | — |
| Tip chalk state | Tip rendering (HF-21), dry click | — | — |
| Table roll-off, equipment faults, opponent habits | Notebook (knowledge, HF 5.1) | — | — |
| Mentor diagnosis | Mentor voice (in person) or text message (not in the bar) | Practice: stroke report | Subtitles |
| Money, invitations, time of night, events | Phone | — | — |
| Assist state | — | yes | Aim line / ghost ball / tip ring themselves (world-space) |

### 2.2 Screen map

```
Boot ─► splash (1.5 s) ─► FIRST LAUNCH (once; shader warm-up runs in parallel) ─► MAIN MENU "Closing Time"
MAIN MENU ─┬─ Continue ─────────────────────────────► CAREER (last venue, phone hub)
           ├─ Career ─► Slots ─► New career / Load ─► CAREER
           ├─ Play ───► Match setup (vs AI / hot-seat / custom) ─► MATCH
           ├─ Practice ─► (same level, in place) ──────► PRACTICE
           ├─ Replays & Photos ─► Gallery ─► Replay viewer / Photo viewer
           ├─ Settings ─► pages (13)
           ├─ Credits & licences
           └─ Quit (confirm)
In play:  LIVE ⇄ PAUSE ⇄ SETTINGS      LIVE ⇄ REPLAY ⇄ PHOTO MODE
          LIVE ⇄ PHONE (standing)      LIVE ⇄ NOTEBOOK (standing)      LIVE ⇄ DECISION CARD (rules)
```

### 2.3 Screen-space layers (Z order, back to front)

| Z | Layer | Contents | Pauses game | Takes input |
|---|---|---|---|---|
| 0 | World | 3D scene incl. diegetic surfaces and world-space assists | — | game |
| 10 | Live info | foul pips, mandatory lines, turn/result lines | no | no |
| 15 | Prompts | interaction prompts anchored to the projected object (3.5) | no | no |
| 20 | Glance card | on demand | no | no |
| 30 | Subtitles / captions | speech + sound captions | no | no |
| 40 | Decision card | rules decisions, challenge offers | no | Q/E/Enter (additive context) |
| 50 | Mode overlays | replay bar, broadcast bug, photo panel, flat phone/notebook, calibration panels | per mode | yes |
| 60 | Menus | main menu list, pause, settings, match setup, slots | pause/menu | yes |
| 70 | Dialogs | confirm, display-revert countdown, errors | inherits | yes (modal) |
| 90 | Debug | F2 physics block (ARCH 6.6) | no | no |

### 2.4 Several tables in one room, balls off the table (decisions 2026-09-28)

Venues can hold several tables, each with its own match (pool hall ~8; AI regulars really play at the others; later online players). The UI must never assume one table or one match per level:

* **Binding.** Every presentation object carries a match id: `FRbMatchPresentation` (C5) is published per director (one director per table, C4), and `URbUiSubsystem` binds the live-info layer, glance card, decision card, replay subsystem view and pause-menu match block to the **player's current match** (the table the player is assigned to; hot-seat: the shared table). Switching tables (challenge accepted at another table) rebinds in one frame.
* **Other tables are diegetic only.** Their scores live on their own slates / wall scoreboards (one `ARbScoreSlate` per table, 9.3), their fouls and calls are heard (ducked barks, captions only when directed at the player), never shown in screen space. The glance "Look" turns toward the slate of the player's own table.
* **Replays and photo mode.** `R` replays the last shots of the player's match only; watching an AI table live needs no UI. Photo mode freezes the player's table playback; other tables keep their frozen state for the still (their playback clocks are held too, C3).
* **Steam.** Rich presence and Timeline phases describe the player's match only.
* **Budget.** The player's own table keeps the full 1536 × 1280 slate; other tables farther than 6 m from the player use 1024 × 853 targets (update on change only), so a hall with 8 slates costs ≈ 10.5 + 7 × 4.7 ≈ 43 MB instead of 84 MB (19).

**Balls off the table.** A ball that leaves the table is handed to engine physics (floor bounce, rolls under stools). UI: the result line names the foul ("Foul — the 3 left the table (Rule 3.5)"; the cue ball: Rule 3.1); caption "[ball bounces across the floor]" with a direction arrow; the ball's return is a chore (9.12, `[F] Pick up the ball` where it lies, or the opponent/bartender brings it in A/C/P modes). The rules never wait for the chore: the respot or ball-in-hand state is already decided (RUL 4.7), the chore only moves the physical ball.

---

## 3. Unreal implementation route

### 3.1 Decision: Slate for everything (justification)

The pipeline is headless and code-first (ARCH 1: Claude drives Unreal without opening the editor; assets are generated by scripts; verification is by automation tests and headless screenshots). The M1 overlay is already Slate (`SRbInfoOverlay`, Build.cs has Slate/SlateCore, "UMG not needed").

| Criterion | Slate (C++) | UMG widget blueprints | UMG in pure C++ (`UUserWidget` + `WidgetTree->ConstructWidget`) | CommonUI |
|---|---|---|---|---|
| Authoring without the editor | yes, plain C++ | no: binary `.uasset` graphs, Designer | yes, but verbose UObject plumbing | UMG + data assets (button styles, controller data) + config (`CommonGameViewportClient`) |
| Review / diff | text diffs | binary | text | text + binary assets |
| Unit tests under NullRHI | construct widgets, `SlatePrepass`, read desired sizes, simulate key events | needs the widget asset loaded | yes | yes, plus router setup |
| Gamepad navigation | built in (`FNavigationConfig` [5.8 ✓]) | built in | built in | best (action router, analog cursor) |
| Platform glyphs, action bar | own registry (3.5) | own | own | built in, asset-driven |
| World-space UI | `FWidgetRenderer` → render target [5.8 ✓] | `UWidgetComponent` | `UWidgetComponent` | same as UMG |
| Animation | `FCurveSequence`, attributes | UMG animations (assets) | code tweens | UMG |
| Runtime cost | lowest | UObject per widget | UObject per widget | UMG + router |
| Consistency with existing code | same as `SRbInfoOverlay` | new paradigm | new paradigm | new paradigm + config |

What we give up and how it is replaced: CommonUI's input router → `URbUiSubsystem` screen stack + input-mode switching (3.3); CommonUI glyphs/action bar → `FRbGlyphRegistry` + `SRbActionBar`; UMG animations → `FCurveSequence`-driven attributes with motion tokens (4.5); `UWidgetComponent`/`UWidgetInteractionComponent` → `URbDiegeticScreenComponent` (3.6) with analytic ray–plane hit mapping (no `bSupportUVFromHitResults` config needed). Revisit CommonUI only for a console port.

### 3.2 Class catalogue (proposed; new work packages in section 21)

| Class | Kind | Responsibility |
|---|---|---|
| `RbUiTokens.h` | constexpr data | colour, type, spacing, motion tokens of section 4 (single source; unit-tested for contrast) |
| `FRbUiStyle` | `FSlateStyleSet` "RbUi" | fonts (from files via `FCompositeFont`/`FTypeface::AppendFont` [5.8 ✓]), text/button/slider/checkbox/combo/scrollbar styles built from tokens; registered in module startup |
| `FRbUiScale` | helper | **Layout scale is the engine's DPI scale only**: `URbDpiScalingRule::GetDPIScaleBasedOnSize` returns `ViewportHeight / 1080` (C2), and every widget added to the game viewport sits inside the engine's `SDPIScaler`. `FRbUiScale` therefore multiplies font sizes by `TextScale` (1.0-2.0) **only**; it never applies the viewport factor again (that would scale screen-space UI twice). Render-target widgets (phone, notebook, slate, TV) have no DPI scaler and are authored in target pixels; the flat phone/notebook (7.6, 14.4) wraps them in an explicit `SDPIScaler` |
| `URbUiSubsystem` | `ULocalPlayerSubsystem` | screen stack (`Push/Pop/Replace(TSharedRef<SRbScreen>)`), layers of 2.3 as `SOverlay` slots on the game viewport, input mode (`FInputModeGameOnly` / `GameAndUI` / `UIOnly`), cursor visibility, Enhanced Input context stack (3.4), pause requests, device family |
| `SRbScreen` | `SCompoundWidget` base | `ScreenId`, `OnActivated/OnDeactivated`, `HandleBack()`, `GetActionHints()`, `bPausesGame`, `bBlocksWorldInput`, `DesiredFocus()` |
| `FRbNavigationConfig` | `FNavigationConfig` | keyboard navigation with the arrow keys (WASD stays free for the game), D-pad + left stick, repeat 0.35 s / 0.08 s, Accept = Enter/Space/Gamepad face bottom, Back = Esc/Gamepad face right; **`bTabNavigation = false`** (the 5.8 default is true [5.8 ✓ `NavigationConfig.cpp`]; Tab is Glance in play and "Skip setup" in 5.1) |
| `FRbInputDeviceTracker` | `IInputProcessor` [5.8 ✓] | last-used device family (`KeyboardMouse`, `Xbox`, `PlayStation`, `SteamDeck`, `Generic`) with hysteresis: switch on a key/button press, stick > 0.3, or mouse travel > 8 px. Under Steam Input every pad reaches UE as an XInput pad, so the family comes from `ISteamInput::GetInputTypeForHandle(GetControllerForGamepadIndex(i))` [5.8 ✓, SDK 1.64] when Steam is running; without Steam only Xbox/Generic can be told apart (DualSense support without Steam: GameInput/RawInput plugin, VERIFY) |
| `FRbGlyphRegistry` | helper | `FKey` + family → brush: Steam glyph PNG via `ISteamInput::GetActionOriginFromXboxOrigin(handle, EXboxOrigin)` → `GetGlyphPNGForActionOrigin` [5.8 ✓, SDK 1.64] (works while UE reads the pad as XInput, no Steam Input action sets needed), else own SVGs (`FSlateVectorImageBrush` [5.8 ✓]) |
| Widgets `SRb*` | Slate | `SRbButton`, `SRbListItem`, `SRbStepper` (◀ value ▶, gamepad-friendly), `SRbSlider`, `SRbToggle`, `SRbDropdown` (on `SComboBox`), `SRbKeyBindButton` (on `SInputKeySelector` [5.8 ✓]), `SRbTabStrip`, `SRbOptionRow`, `SRbDescriptionPanel`, `SRbActionBar`, `SRbHoldToConfirm`, `SRbDialog`, `SRbScrim` (with `SBackgroundBlur` [5.8 ✓]) |
| `URbSettingsRegistry` | UObject singleton | the rows of section 13 (`FRbSettingDef`, 13.13); drives the settings screen, presets, persistence and tests |
| `URbDiegeticScreenComponent` | `UStaticMeshComponent` | owns a `UTextureRenderTarget2D`, an `FWidgetRenderer`, the widget, redraw policy, MID parameter `ScreenTex`, luminance, pointer mapping (3.6) |
| `URbSubtitleSubsystem` + `SRbSubtitles` | world subsystem + widget | queue, speaker names, captions, timing (4.2, 13.10, 17.2 L9) |
| `URbSaveSubsystem` | `UGameInstanceSubsystem` | slots, async save, backups, thumbnails (15) |
| `URbPlatformSubsystem` | `UGameInstanceSubsystem` | Steam: achievements, stats, rich presence, timeline, screenshots, overlay pause (16); no-op without Steam |

### 3.3 Screen stack and input modes

* Push rules: a screen with `bPausesGame` calls `SetGamePaused(true)` in single-player; the merged `URbShotPlaybackComponent` already holds the live clock while the world is paused and resumes without a time jump (UE-2: `SetPaused`, world-pause hold [code ✓], C3). Replays and photo mode call `SetPaused(true)` on the playback of the player's table instead.
* Input mode per top screen: menus `UIOnly` (cursor visible, focus on `DesiredFocus()`); decision card `GameAndUI` without cursor (the card only reads Q/E/Enter through the additive context); phone/notebook `GameOnly` (they are world objects; the component consumes their context).
* Focus rule: after every push/pop exactly one focusable widget holds user focus (test UX-T09). Mouse hover moves focus; a gamepad press re-shows the focus ring.
* Back: `Esc`/`B` pops the top screen; at the root of a flow it opens the confirm dialog, never quits silently.

### 3.4 Input contexts (runtime Enhanced Input, additions to ARCH 6.5)

| Context | Priority | Active when | Actions |
|---|---|---|---|
| `IMC_Global` | 5 | always (incl. modes and menus) | Pause only |
| `IMC_Play` | 5 | live play (standing, down, placement); removed in replay, photo, phone/notebook raised, menus | Glance, PinGlance, Replay, PhotoMode, Phone, Notebook |
| `IMC_Walk` | 0 | standing | Move, Look, GetDown, Interact, Call, Declare, Chalk |
| `IMC_Down` | 0 | down on the shot | Look(aim), Stroke, Commit, Elevation, TipOffset, FineAim, Settle, GetDown, Call cycle (Q/E) |
| `IMC_Placement` | 0 | ball in hand | Look(placement), Confirm, Wipe, Cancel |
| `IMC_Decision` | 10 | decision card | CycleOption (Q/E), Confirm |
| `IMC_Phone` / `IMC_Notebook` | 20 | raised | Navigate, Select, Back, Tab, Scroll, Lower (P / N, the same keys that raised it) |
| `IMC_Replay` / `IMC_Photo` | 30 | mode | see 10.4 / 11.3 |
| `IMC_UI` | 100 | menus | Back, TabLeft/Right, Reset, Preview (navigation itself is Slate) |

All actions carry `UPlayerMappableKeySettings` (Name, DisplayName, DisplayCategory) so `UEnhancedInputUserSettings` [5.8 ✓: `MapPlayerKey`, `UnMapPlayerKey`, `ResetAllPlayerKeysInRow`, `RegisterInputMappingContext`, key profiles] handles remapping and persistence. This needs `bEnableUserSettings` in the Enhanced Input developer settings (contract C2).

**Persistence risk (VERIFY in UI-2):** the user-settings save stores each mapping with `AssociatedInputActionSoft` (a soft object path, 5.8 [5.8 ✓]), but `URbInputSetup` creates the actions at runtime (transient objects, no asset path). Rows are keyed by `MappingName`, so re-registration after load may still restore them; UX-T03 tests exactly this (remap → quit → relaunch → binding present). Fallback if it fails: store `(MappingName, Slot, FKey)` triples in the RAW BREAK settings section and re-apply them with `MapPlayerKey` after `RegisterInputMappingContext` at startup. Actions are created with stable object names (`IA_<Name>`) under a stable outer in either case.

### 3.5 Glyphs and prompts

* Prompt format: `[glyph] Verb` (e.g., `[F] Insert coins`). XAG 101 requires the **text inside a glyph** to meet the 18 px body-height minimum, so the key label is ≥ 18 px tall and the glyph is **30 px high at 1080p** (scaled by the DPI rule and text size); lines that contain a glyph get a line height ≥ 36 px. (The earlier "cap height × 1.4" gave a 23.5 px keycap with a ~14 px letter.)
* Families: KeyboardMouse (own SVG keycaps with the key's localised label from `FKey::GetDisplayName`), Xbox-layout letters, PlayStation and Deck via Steam Input glyph PNGs only (official art, VERIFY licence note in Steamworks docs), Generic (numbered face buttons). No platform logos in our own art.
* **Interaction prompts** (layer 15) are a screen-space element and count against UX-P1, so they are contextual: a prompt exists only for an interactable whose action is possible **and** meaningful now (coin slide only when the next rack must be paid, cue-ball return only after a scratch, cue rack only when the player has no cue or asks for a short cue, triangle only when it is the player's rack chore, a jumped ball only while it lies off the table); decorative interactables never prompt. It appears after 0.6 s of gaze within 1.2 m, anchored beside the projected object (never on the table bed), and fades after 3 s. Setting *Interaction prompts* On / First time only / Off (13.9); Diegetic-only sets First time only.

### 3.6 Diegetic surfaces (render-to-texture)

`URbDiegeticScreenComponent` on the phone screen, notebook spread, score slate and TV:

1. **Render**: `FWidgetRenderer(bUseGammaCorrection = false)` draws the Slate widget in linear space into a target made by `FWidgetRenderer::CreateTargetFor(Size, TF_Trilinear, /*bUseGammaCorrection*/ false)`, i.e. RGBA8 with `SRGB = true` — the same convention `UWidgetComponent` uses [5.8 ✓ `WidgetRenderer.cpp`, `WidgetComponent.cpp`]. (Gamma-space rendering, `true`, into an sRGB target would encode twice and wash the text out.) No mips for phone/TV; slate and notebook are seen at an angle and need mips: a render target drawn by the Slate 3D renderer gets no automatic mip chain, so after each redraw a render-thread generate-mips pass runs on the target (`bAutoGenerateMips` + explicit generate, VERIFY the 5.8 call in UI-5). Redraw on model change, capped at 30 Hz (phone in use), 10 Hz (TV), on change only (slate, notebook).
2. **Material**: generated by the materials pipeline (UE-3 style Python): phone = emissive screen under a glass clear-coat layer (fingerprint roughness map, reflections of the bar); slate = RT as a chalk mask × chalk-dust noise, smudge offsets, 0.85 albedo; notebook = RT as a graphite mask (roughness 0.35, slight sheen) over paper (roughness 0.8).
   **Phone screen luminance (DERIVED, ESTIMATE curve).** Like a real phone, brightness follows an ambient-light sensor, never the camera exposure: driving it from the adapted EV would be a feedback loop, because the raised phone fills 86 % of the metered frame (7.2). The sensor value is the analytic illuminance at the phone position (`RbCameraMath::IlluminanceAt` over the venue's placed lights + the venue's ambient floor term, sampled at 2 Hz and smoothed over 1 s). White level `L_w(E)`, log-log interpolation through (E lux → cd/m²): (1 → 12), (10 → 25), (50 → 60), (200 → 140), (1000 → 350), (5000 → 600); accessibility boost ×1.5. In the Low Bridge (VDB 4.4: 15-45 lux in the pool room, 70-110 lux on the bar top) this gives **~30-90 cd/m²** (30 lux → 45 cd/m²), about 4-5 EV above the room's adapted mid-grey (EV100 ≈ 4 → `L = 2^EV / 8` = 2 cd/m²), i.e. 2-2.5 EV above diffuse white: as bright as a real phone at night, but inside the filmic tonemapper's shoulder instead of the clipped slab a fixed 120-350 cd/m² screen would give; the eye then adapts to the phone at the Eyes rate (1.5 EV/s up) as a real eye does. The phone OS uses a **dark theme below 200 lux** (light text on near-black), so the mean screen luminance is ~15 % of `L_w` and the room does not go black behind it.
3. **Pointer**: a flat screen is a rectangle in the component's local space; the view ray is intersected analytically with that plane → local (u, v) → widget coordinates; the surface widget implements `IRbSurfacePointerTarget` (hover item, click, wheel). Keyboard/gamepad navigation uses the same Slate navigation as flat screens.
4. **Flat fallback**: the same widget instance can be re-parented into layer 50 of the viewport (7.6, 14.4): one widget, two presentations.
5. **Cost**: ≤ 0.2 ms GPU per surface update (ESTIMATE; test UX-T17).

### 3.7 Fonts and assets without editor

Fonts, SVG icons and glyphs are raw files under `Content/UI/{Fonts,Glyphs,Icons}/` loaded by path at runtime (`FTypeface::AppendFont(Name, File, Hinting, LoadingPolicy)` [5.8 ✓], `FSlateVectorImageBrush(File, Size)`) and staged with `+DirectoriesToAlwaysStageAsUFS=(Path="UI")`; the string-table CSVs are not assets either and need `+DirectoriesToAlwaysStageAsUFS=(Path="Localization/StringTables")` (contract C2). Nothing needs a `.uasset`.

Font files are fetched by `Tools/ui/fetch_fonts.py` from pinned release URLs with SHA-256 checks, stored with their `OFL.txt`, and get a row in `Docs/licenses/asset-ledger.csv` (source `font`, VDB 13.9). **IBM Plex is OFL with the Reserved Font Name "Plex"** (verified in IBM's LICENSE.txt): the files ship unmodified — no subsetting, no glyph edits — or a modified copy must be renamed. Caveat and Kalam declare no Reserved Font Name (their OFL.txt). The chalk handwriting of the whole chalkboard (9.3) uses the same Kalam file as the venue's text tool (`Art/Fonts/`, VDB 8.2), so the live text and any baked chalk text match.

### 3.8 Headless verification

* **Unit** (`RawBreak.Unit.UI.*`, NullRHI): tokens, registry, formatting, layout via `SlatePrepass` (desired size ≤ allotted = no clipping), navigation by synthesised key events through `FSlateApplication::ProcessKeyDownEvent`.
* **Functional** (`RawBreak.Functional.UI.*`, PIE): flows through the director (decision, concede, stalemate, pause during playback).
* **Screenshots** (`RawBreak.Screenshot.UI.*` and `rbue.py capture`): the capture subsystem opens a screen by id before capturing (`-RbUiScreen=Settings.Graphics -RbUiState=Focused:gfx.upscaler -RbCulture=de`, contract C6) → `Docs/images/ui/<screen>_<state>_<res>_<culture>.png`, inspected by Claude; pixel baselines after approval.

### 3.9 Contract changes this spec needs (for the architect; not done here)

| # | Owner today | Change |
|---|---|---|
| C1 | UE-0 (Build.cs, .uproject) | `RawBreak` Build.cs: add `UMG` (only for `FWidgetRenderer`), `OnlineSubsystem`, `OnlineSubsystemUtils`; optional `Steamworks` behind `WITH_STEAMWORKS` for Timeline/Screens/Input glyphs. `RawBreak.uproject`: enable the plugins `OnlineSubsystem`, `OnlineSubsystemSteam` (present in 5.8 [5.8 ✓]) and `OnlineSubsystemNull` for dev builds. |
| C2 | UE-0 (Config) | `DefaultEngine.ini`: `[/Script/Engine.UserInterfaceSettings] UIScaleRule=Custom`, `CustomScalingRuleClass=/Script/RawBreak.RbDpiScalingRule` (a `UDPICustomScalingRule`, height/1080 [5.8 ✓]); `[/Script/EnhancedInput.EnhancedInputDeveloperSettings] bEnableUserSettings=True`; `[OnlineSubsystem] DefaultPlatformService=Steam`, `[OnlineSubsystemSteam] bEnabled=true, SteamDevAppId=480` (realism plan 10). `DefaultGame.ini` `[/Script/UnrealEd.ProjectPackagingSettings]`: `+DirectoriesToAlwaysStageAsUFS=(Path="UI")`, `+DirectoriesToAlwaysStageAsUFS=(Path="Localization/StringTables")`, `+CulturesToStage=en`, `+CulturesToStage=de`; `[Internationalization]` localisation path, `Config/Localization/RawBreak_*.ini`. |
| C3 | UE-2 (**done**) | Merged UE-2 already holds the live clock under world pause and has `SetPaused` [code ✓ `RbShotPlaybackComponent.h`]. Remaining: photo mode and replay call `SetPaused` on the player's table only; UX-T10 checks that the director commits only after the resumed playback finishes. |
| C4 | UE-6b | `ARbGameMode`/director: a `Menu` phase on the venue map with in-place transition to Practice (no map reload); **one director per table** (decisions 2026-09-28, 2.4) with a match id. Reuse the existing API [code ✓ `RbMatchDirector.h`]: `SetCalledShot(Ball, Pocket)`, `SetShotKind(ShotKind)` (push-out / safety), `ChooseOption`, `CycleOption`, `Confirm`. New: `RequestSpot()` (RUL 4.4), `ProposeStalemate` / `AnswerStalemate(bAccept)`, `Concede(Player)`, `ConfirmTurn()` (hot-seat hand-over gate, 9.11: the stroke stays locked until it is called); URL options `&Rules=&Venue=&P1=&P2=&A1=&A2=&Clock=` (`?Pressure=`, `?Attr=`, `?Noise=` exist). |
| C5 | UE-7 | `FRbOverlayModel` (text lines) evolves into `FRbMatchPresentation` (structured, one per match id: scores, per-player fouls, pending decision + options with rule refs, placement region, call, declarations, stalemate turns left, clock, last-shot facts, stroke shares) so each carrier formats its own text in both languages. |
| C6 | UE-0 | `URbHeadlessCaptureSubsystem`: `-RbUiScreen=`, `-RbUiState=`, `-RbCulture=`, `-RbTextScale=` switches; `rbue.py loc` subcommand (GatherText / export / import / compile, 17.2 L3). |
| C7 | UE-8 (**mostly done**) | Merged UE-8 [code ✓ `integ/m1`]: presets = `sg.*` level 0..4 plus `[RawBreak.<Option>@<level>]` rows applied after the groups at `ECVF_SetByGameOverride` (hit lighting from High up is already forced there). Remaining: the registry of section 13 wraps `URbGameUserSettings::SetQualityOption` / `SetScreenPercentage` and adds the new RB rows of 13.3 (upscaler, sharpening, reflection resolution, MegaLights, hero textures) as further `[RawBreak.*]` sections; RB rows stay applied when a single group changes. |
| C8 | UE-5a | Actions of 3.4 created in `URbInputSetup` with player-mappable settings and stable names; *Pull & release* and *Commit toggle* stroke variants (13.6); stick stroke with the address gap and release guard (13.7); stroke calibration hooks (5.8). |
| C9 | UE-3 | Ball material parameters for accessibility (13.10): `NumberScale` (1.0 / 1.35), `NumberCount` (2 / 6), `PatternCode` (0/1: per-colour pattern on solids and stripes, HF 5.4 "pattern-coded balls"), analytic like the numbers. |
| C10 | Venue WP (DB-B / DB-H) | A third lighting state **AfterHours** in `lights.json` (6.5) with its prop visibility set; the chalkboard H13 delivered as mesh + frame with a named face slot `Face` driven by `ARbScoreSlate` (9.3) instead of a baked text texture; menu station cameras `RbCam_Menu_S0..S7` placed by `rb_make_divebar.py` (6.3). |

---

## 4. Visual language and tokens

Mood: **"after-hours documentary"**. Flat UI looks like careful subtitles or a film's end titles: typographic, restrained, no gradients, bevels, fake wood or glossy buttons. Texture and grit belong to the diegetic surfaces (real chalk, graphite, glass), never to flat panels. One accent colour — the amber of a neon "OPEN" sign — marks focus and selection.

### 4.1 Colour tokens

Contrast ratios (WCAG 2.x relative luminance, DERIVED with a script) against `ink.900` / `ink.800` / `ink.700`:

| Token | Hex | Use | Contrast on 900 / 800 / 700 |
|---|---|---|---|
| `ink.900` | `#0E0F10` | base surface, scrims | — |
| `ink.800` | `#1A1C1E` | raised panel, rows | — |
| `ink.700` | `#26292C` | focused row background, inputs | — |
| `line.600` | `#3A3E42` | dividers (non-text, ≥ 3:1 not required) | — |
| `chalk.100` | `#EDEBE6` | primary text, icons | 16.1 / 14.4 / 12.3 |
| `chalk.300` | `#B8B4AA` | secondary text, descriptions | 9.3 / 8.3 / 7.1 |
| `chalk.500` | `#8A867D` | disabled, placeholder | 5.3 / 4.7 / 4.0 (disabled text is exempt) |
| `amber.400` | `#FFB02E` | focus bar, selection, sliders, key accents | 10.5 / 9.4 / 8.0 |
| `on.amber` | `#0E0F10` | text on amber | 10.5 |
| `signal.foul` | `#FF5A4F` | foul, warning, destructive: **non-text marks** (pips, bars, icons; always with text) | 6.2 / 5.6 / 4.8 |
| `signal.foul.text` | `#FF8A80` | foul/destructive **text**, only on opaque surfaces or `scrim.menu` | 8.4 / 7.5 / 6.4 |
| `signal.ok` | `#5BD98A` | legal / accepted (always with text/icon) | 10.7 / 9.6 / 8.2 |
| `signal.info` | `#7FB8FF` | links, neutral hints | 9.3 / 8.3 / 7.1 |

Scrim rules (DERIVED with a script: worst case = pure white behind the panel, blending in linear light; minimum `ink.900` opacity for 4.5:1 text over white: `chalk.100` 0.858, `signal.ok` 0.924, `amber.400` 0.927, `signal.info` 0.941, `chalk.300` 0.942, `signal.foul.text` 0.952, `signal.foul` 0.979):

| Surface | Opacity of `ink.900` | Allowed text | Why |
|---|---|---|---|
| `scrim.menu` (settings, pause, setup, slots, match-over card) | **0.96** + `SBackgroundBlur` strength 16 | all tokens except `signal.foul` (use `signal.foul.text`) | worst case over white at 0.96: `chalk.300` 5.4:1, `amber.400` 6.1:1, `signal.foul.text` 4.9:1 |
| `scrim.card` (glance card, decision card, live-info plates) | **0.88** | **`chalk.100` only** (5.06:1 worst case); colour appears only as non-text marks: `amber.400` focus bars (3.3:1 ≥ the 3:1 non-text minimum), `signal.foul` pips and bars with a 1.5 px `chalk.100` outline (the red alone reaches only 1.96:1 over white) | the 0.88 plate keeps the world visible behind small live elements |
| `scrim.subtitle` | player setting 0-100 %, default **0.60** | `chalk.100` + 2 px `ink.900` outline at 0.9 | outline keeps ≥ 4.5:1 at edges at any opacity |
| High-contrast mode | 1.00, text `#FFFFFF`, focus 4 px | all | 13.10 |

Colour-blind rule: `signal.foul` and `signal.ok` never appear as the only distinction; focus is shape + colour (4 px amber bar left of the row + the row background `ink.700`). HDR: all UI at the UI luminance (13.4); `amber.400` never above it.

### 4.2 Typography tokens

Families (all bundled unmodified, licences shipped in *Credits & licences*, 3.7): **IBM Plex Sans** (UI body), **IBM Plex Sans Condensed** (labels, titles, menu items), **IBM Plex Mono** (numbers, timecodes, key values) — SIL OFL 1.1 with Reserved Font Name "Plex"; **Caveat** (the character's notebook handwriting) — OFL; **Kalam** (chalk slate, bartender's handwriting) — OFL. A game logotype "RAW BREAK" is drawn as SVG, not a font. Glyph coverage for EN/DE incl. `ä ö ü Ä Ö Ü ß „ “ ‚ ‘ – — … € $ ¢ × ° ½` is enforced by UX-T06 (capital `ẞ` is not required: no string uses it and uppercase never comes from the string, L8; Kalam's and Caveat's Latin coverage is checked by the same test).

Sizes are **em size in pixels at 1080p and 100 % text size**; Slate `FSlateFontInfo::Size` is in points at 96 DPI, so `pt = px × 0.75`. Body height (XAG 101: descender to ascender) of Plex Sans ≈ 0.98 em (VERIFY with the font metrics in UX-T05).

| Token | Family / weight | px @1080p | pt | Line height | Tracking | Case | Use |
|---|---|---|---|---|---|---|---|
| `t.display` | Plex Sans Condensed SemiBold | 56 | 42 | 1.1 | 0 | Sentence | screen titles |
| `t.menu` | Plex Sans Condensed Medium | 40 | 30 | 1.2 | +2 % | Sentence | main-menu items |
| `t.title` | Plex Sans Condensed SemiBold | 32 | 24 | 1.25 | 0 | Sentence | section titles, card headers |
| `t.body` | Plex Sans Regular | 24 | 18 | 1.5 | 0 | Sentence | rows, descriptions, dialogs |
| `t.label` | Plex Sans Condensed Medium | 20 | 15 | 1.3 | +2 % | Sentence | tags, column heads, prompts |
| `t.label.caps` | Plex Sans Condensed SemiBold | 26 | 19.5 | 1.2 | +6 % | UPPER (style, ≤ 3 words) | score bug, REPLAY tag, setup step header |
| `t.caption` | Plex Sans Regular | 20 | 15 | 1.4 | 0 | Sentence | minimum for any text |
| `t.mono` | Plex Mono Regular | 22 | 16.5 | 1.3 | 0 | — | numbers, timecode, values |
| `t.subtitle` | Plex Sans Medium | 32 | 24 | 1.3 | 0 | Sentence | subtitles/captions (100 %; 100-200 %) |
| `t.pips` | Plex Sans Condensed SemiBold | 26 | 19.5 | 1.2 | +2 % | Sentence | foul pips, mandatory lines |
| `t.hand.notebook` | Caveat Regular/Bold | 42 RT-px | — | 1.25 | 0 | — | notebook RT (14.2) |
| `t.hand.slate` | Kalam Bold | 96 RT-px | — | 1.2 | 0 | — | score slate RT |

Resolution checks (DERIVED): at 1280×800 (Steam Deck, layout scale 0.741) `t.caption` = 14.8 px ≥ the 12 px Steam recommends and `t.body` = 17.8 px; at 3840×2160 everything doubles. PC minimum of XAG 101 (18 px body height at 1080p, measured descender to ascender on the rendered text) is met by every mixed-case token at 100 %. **All-caps text has no ascenders or descenders**, so its measured height is the cap height (Plex ≈ 0.70 em, VERIFY in UX-T05): a 20 px uppercase label would measure 14 px. Uppercase is therefore only allowed in `t.label.caps` (26 px em → ≈ 18.2 px cap height). Text size 100-200 % scales every token except the logotype; layouts must survive 200 % (UX-T07).

Paragraph rules: ≤ 80 characters per line, left-aligned (never justified), paragraph spacing 2 × line spacing, no ALL-CAPS sentences (labels only, and the uppercase comes from the style, never from the string — 17.2 L8).

### 4.3 Spacing, grid, safe zones

* 8 px grid at 1080p (4 px half-step), scaled by the layout scale.
* Title-safe margin 5 % (96 px horizontal, 54 px vertical at 1080p) for menus; 3.5 % for live-info elements. Setting *Safe zone* 90-100 % for TVs (13.4).
* Hit targets: rows 56 px high, buttons ≥ 48 px, sliders' thumb 28 px with a 48 px hit box.
* Settings content column max 1280 px wide at 1080p; description panel 440 px.

### 4.4 Iconography

Own line icons, 2 px stroke on a 24 px grid, SVG, `chalk.100`; no brand logos, no real controller logos in own art (3.5). Ball numbers in UI are drawn as the ball (a small circle in the WPA colour with the number in a white disc) **plus** the number as text, never colour alone.

### 4.5 Motion tokens

| Token | Duration | Curve | Reduced motion |
|---|---|---|---|
| `m.fade.in` / `m.fade.out` | 120 / 180 ms | ease-out / ease-in | same |
| `m.push` (screen change) | 220 ms, 24 px slide + fade | ease-out cubic | fade only, 120 ms |
| `m.glance` | in 150 ms, out 250 ms | linear opacity | same |
| `m.card.hold` | result line 4 s (ARCH 6.6), turn line 3 s | — | same |
| `m.station` (menu camera) | 1.2 s dolly | ease-in-out sine | 200 ms cut through black |
| `m.raise` (phone, notebook) | 0.35 s | ease-out | 0.1 s |
| `m.zoom` (reading zoom / glance FOV change) | 0.35 s | ease-in-out sine | cut (no animated FOV change) |
| `m.pulse` (two-foul warning) | one 600 ms opacity pulse 1.0→0.6→1.0, once | sine | none |

### 4.6 UI sound tokens

Quiet and physical; routed to the *UI* volume bus: focus = soft chalk tap (−30 dBFS), confirm = cue tip on the chalk cube, back = paper rustle, slider step = tick every 5 %, toggle = light switch, error = dull wooden knock. Phone and notebook use their own diegetic sounds (screen taps, haptic buzz, pencil). No UI sound during a live stroke.

---

## 5. First launch

### 5.1 Purpose and flow

Get a new player from boot to a good-looking, well-calibrated first shot in ≤ 3 minutes, and let anyone skip to the main menu in ≤ 45 s. Everything chosen here stays editable in Settings, and *Settings > Other > Run first-launch setup again* repeats it.

```
Boot ─► Splash 1.5 s ─► [W] shader warm-up starts (background, continues through steps 1-5)
  ─► 1 Language & text ─► 2 Display ─► 3 Graphics ─► 4 Look, sound & comfort ─► 5 Controls
  ─► (gate: warm-up ≥ 95 % or 60 s or player skips after 10 s)
  ─► 6 Stroke calibration (in the 3D scene) ─► Summary ─► MAIN MENU (first-visit variant)
```

State (`URbGameUserSettings::FirstLaunch`): `NotStarted | InProgress(Step) | Done(Version)`. Each completed step is saved at once, so a crash resumes at the step it happened in (UX-T13). A later major version can set `Version` to re-run only new steps.

Common layout of steps 1-5 (flat, `scrim.menu` over the live after-hours scene, which is already loading behind):

```
+------------------------------------------------------------------------------------------+
|  SETUP · 2 / 6                                                    Shaders 63 % ▮▮▮▮▮▯▯▯  |
|                                                                                          |
|  Display                                             | (description / live preview area) |
|  ─────────────                                       |                                   |
|  ▌ Brightness            ◀  2.20  ▶                  |   [ calibration image ]           |
|    HDR                   ◀  Off   ▶  (not available) |                                   |
|                                                      |   "Adjust until the left mark is  |
|                                                      |    barely visible."               |
|                                                      |                                   |
|  [Enter] Next   [Esc] Back   [Tab] Skip setup                     RAW BREAK  EN · DE     |
+------------------------------------------------------------------------------------------+
```

Inputs everywhere: mouse (click, wheel on steppers), keyboard (↑/↓ rows, ←/→ values, Enter next, Esc back, Tab skip the whole setup with confirm), controller (D-pad/LS rows and values, A next, B back, View = skip). Implementation: `SRbFirstLaunch` (one `SRbScreen` hosting step widgets), registry rows reused from section 13 so the setup and the settings screen can never disagree.

### 5.2 Shader warm-up [W]

* Uses the bundled PSO cache: `FShaderPipelineCache::NumPrecompilesRemaining()` [5.8 ✓] and PSO precaching (`PipelineStateCache::NumActivePrecacheRequests()` [5.8 ✓]) for the progress figure (progress = 1 − remaining / the maximum seen since the gate opened); batch mode *Fast* while in setup/menus, *Background* in play (`FShaderPipelineCache::SetBatchMode`). A bundled cache exists only once packaging and a `-logPSO` recording run exist (packaging milestone); until then the figure uses precache requests alone.
* Shown only as the small top-right line during steps 1-5. Before step 6 (the first 3D interaction) a gate waits until ≥ 95 % or 60 s; after 10 s the player may skip ("You may see short stutters for a few minutes").
* Re-run on driver change (`GRHIAdapterUserDriverVersion` stored [5.8 ✓]) and from *Settings > Graphics > Rebuild shader cache*.

### 5.3 Step 1 — Language & text (before any speech plays; XAG 104)

Rows: **Language** (Deutsch / English; preselected from the OS culture: `de*` → Deutsch, everything else → English); **Subtitles** On/Off (default **On**, PO Q4); **Text size** 100-200 % in 25 % steps with a live sample paragraph and a sample subtitle. Changing the language re-renders the whole flow immediately.

### 5.4 Step 2 — Display

* **SDR brightness**: a calibration panel rendered through the game's tonemapper: a dark chalkboard with two marks at 1.5 % and 3 % reflectance. "Adjust until the left mark is barely visible and the right one is clearly visible." Maps to `GEngine->DisplayGamma` [5.8 ✓] 1.8-2.6, default 2.2.
* **HDR** (row shown only if `UGameUserSettings::SupportsHDRDisplayOutput()` [5.8 ✓]): On/Off via `EnableHDRDisplayOutput(bEnable, Nits)` [5.8 ✓]; **Peak brightness**: a bright lamp symbol on white at rising luminance — "raise until the symbol just disappears" → `SetMaximumHDRDisplayNits(n)` + `SetHDRCalibrationUsed(true)` (400-10000 nits); **Scene brightness (paper white)** 100-400 nits (default **203**, BT.2408 and the engine default) → `SetHDRPaperWhiteNits`; **UI brightness** 100-400 nits (default 203) → `SetHDRUILuminanceSeparate(true)` + `SetHDRUILuminanceNits`. All through `UGameUserSettings` [5.8 ✓ `GameUserSettings.h`], then `ApplyNonResolutionSettings`: the engine's HDR sink (`UnrealEngine.cpp`) rewrites `r.HDR.PaperWhite`, `r.HDR.UI.Luminance` and `r.HDR.Display.OverrideOSMaxLuminance` from these user settings, so setting the cvars directly would be overwritten.
* Window mode and resolution are **not** asked (defaults: borderless fullscreen at desktop resolution); they live in Settings.

### 5.5 Step 3 — Graphics

1. **Detect** GPU name, vendor and dedicated VRAM (`GRHIAdapterName`, `GRHIVendorId`, `GRHIGlobals.GpuInfo.DedicatedVideoMemory` [5.8 ✓ `RHIGlobals.h`, D3D12]) and pick a preset from the table below by name match first, VRAM second. **Cinematic is never picked automatically.**

| Tier (ESTIMATE; the list lives in a data file so it can grow) | VRAM | Preset | Upscaler default |
|---|---|---|---|
| RTX 4080 / 5070 Ti class and above, RX 7900 XT / 9070 XT and above | ≥ 16 GB | Epic | DLSS Quality / FSR Quality |
| RTX 3070-3090, 4060 Ti-4070 Ti, 5060-5070, RX 6800-7800 XT | 8-12 GB | **High** (dev PC tier) | DLSS Quality / FSR Quality / XeSS Quality |
| RTX 2060-3060 Ti, RX 6600-6700 XT, Arc A750 / B580 | 6-12 GB | Medium | Balanced |
| older, integrated, Steam Deck | < 6 GB | Low | TSR/FSR 50-60 % |

2. **Optional benchmark** (20 s, button "Test my PC"): the after-hours bar, a scripted camera path around the table while a bundled deterministic replay of a 9-ball break plays. Measure GPU frame times; with the target frame time `T = 1000 / TargetFps` ms (TargetFps default 60; choices 30/60/90/120): P95 > T → recommend one preset lower; P95 < 0.6 T → offer one higher (never Cinematic). Result card: "High: 11.8 ms (P95) — smooth at 60 fps" / "Epic: 19.4 ms — below 60 fps".
3. Rows shown: Preset, Upscaler (+ mode), Target frame rate. Everything else stays in Settings.

### 5.6 Step 4 — Look, sound & comfort

* **Camera look**: Eyes (default) / Headcam, toggled on the live scene behind the panel (hold `Space`/`Y` to hide the panel and look; preview rule 13.2). One line each: "Eyes — how you see it: natural field of view, focus follows your aim." / "Headcam — a head-mounted action camera: wide lens, sensor noise, full head motion."
* **Field of view**: slider V 40-75° (Eyes default 50°), shown with the horizontal equivalent at the current aspect (UE 4.3: `tan(V/2) = tan(H/2) · H_px / W_px`). Optional helper "Suggest from my screen": screen diagonal `d` (inches) and viewing distance `D` (cm) → `W = d · a / √(1 + a²)`, `H_nat = 2 atan(W / 2D)` (T4: 27″ 16:9 at 70 cm → 46.2° horizontal = 27.0° vertical), with the note that the geometric value feels like a keyhole and 50° vertical is the realistic-feeling default.
* **Reduced motion** (master, 13.10), **Head bob** 0-100 %, **Motion blur** 0-100 %, **Film grain** 0-100 %.
* **Audio output**: Headphones (binaural) / Speakers, with a 3 s sample (a break in the bar); the step is titled "Look, sound & comfort".

### 5.7 Step 5 — Controls

* Detected device (keyboard + mouse / controller family); both can be set up.
* **Mouse DPI** (400 / 800 / 1000 / 1200 / 1600 / 2400 / 3200 / custom; default 800) as a starting value only — step 6 measures the real scale.
* **Handedness** Right / Left (mirrors the stance, bridge hand and camera side offset).
* **Commit input**: Hold (default) / Toggle (accessibility, 13.6).

### 5.8 Step 6 — Stroke calibration (in scene)

The player stands at the after-hours table (the M1 test room until the dive bar and its AfterHours state exist, C10) and is put down on a straight-in shot (cue ball 0.30 m from an object ball that is 0.30 m from a corner pocket); practice strokes stop short (ARCH 6.2). A small calibration panel (layer 50, bottom-left, 440 × 220 px) shows the step text, a counter and a live trace of the cue displacement over the last 2 s (the only time this trace exists).

| Sub-step | Instruction | Measurement | Result (DERIVED) |
|---|---|---|---|
| 6a Comfortable stroke | "Make 5 relaxed, full practice strokes." | forward displacement of each stroke in mouse counts, back turning point → front turning point; `D_c` = median | `MetresPerCount = 0.080 m / D_c` (TUNING: a relaxed full stroke counts as 8 cm of hand travel), clamped to the equivalent of 200-6400 DPI (3.97e-6 … 1.27e-4 m/count); replaces the DPI in `x_m = counts · MetresPerCount` (UE 5.4) |
| 6b Power stroke | "Make 3 strokes as fast as you can while staying in control. They won't hit." | peak hand speed per stroke with the same quadratic-fit estimator as the real stroke (UE 5.4, T9); `v_p` = median of 3 | `StrokeVelocityScale s = clamp(v_sat / v_p, 0.5, 2.0)` with `v_sat` = 2.0 m/s, applied as `v_m' = s · v_m` before the gain curve, so the player's fastest controlled stroke reaches the curve's saturation (`G_max · v_sat` = 10 m/s tip speed) |
| 6c Touch check (optional) | "Now pot the ball three times at the speed you think it needs." | tip speed at contact and its speed class (Dr. Dave classes, UE 5.4) | feedback line "Medium — 1.4 m/s"; if 2 of 3 strokes land above *medium*, offer the stroke curve *Fine* (G0 1.5: more hand travel per cm of cue); if below *slow*, offer *Short* (G0 2.5) (13.6) |

Worked example (DERIVED, test UX-T14): an 800-DPI mouse moved 8.0 cm gives `D_c` = 2520 counts → `MetresPerCount` = 0.080 / 2520 = 3.1746e-5 m (≈ 800 DPI, whose exact value is 3.1750e-5). `v_p` = 1.25 m/s → `s` = 1.6; a stroke at 1.25 m/s hand speed then gives `v_m'` = 2.0 m/s → tip 10 m/s; a soft 0.2 m/s hand stroke gives 0.32 m/s → tip 0.64 m/s ("slow"). Inputs: the normal stroke controls; `Esc` skips the step (defaults stay: DPI setting, `s` = 1). Controller: the stick stroke (13.7) is calibrated the same way, with normalised stick deflection instead of counts.

The **vision-centre drill** is *not* part of first launch: in the career the mentor runs it in the first session before any match counts (HF-18); for Practice/hot-seat it is at *Settings > Camera > Vision centre > Calibrate* (13.5).

### 5.9 Summary

A card listing the choices ("Deutsch · subtitles on · High · DLSS Quality · Eyes 50° · stroke calibrated") plus one pointer: "Colour-blind ball numbers, hold/toggle, assists: Settings > Accessibility". `Enter` → main menu in its first-visit variant: the camera slowly pushes toward the lit table, the phone on the bar counter buzzes (a message from Terri, the owner: "Door's unlocked. Table's yours after close."), and **Career** is focused.

---

## 6. Main menu

### 6.1 Concepts

| | A — "Parking lot" | B — "The apartment" | **C — "Closing Time"** |
|---|---|---|---|
| Scene | Inside a parked car at night outside the bar: rain on the windshield, wipers every 6 s, the bar's neon through wet glass, the cue case on the passenger seat | The character's small apartment: cue case on the bed, phone on the table, notebook, a TV playing old matches, city neon through the window | The dive bar after hours: bar stools upside down on the counter, one lamp over the 7-ft coin-op table, jukebox idle glow, the window neons (fictional brands, VDB N1/N2) seen mirrored through the glass block, rain heard outside, a neon hum |
| Menu metaphor | dashboard / phone / door | objects = menu (cue case = equipment, phone = career, TV = replays, door = play) | camera stations (phone on the counter = career, table = play/practice, TV over the bar = replays, door = quit) |
| New content | car interior (hard-surface, fictional design), street exterior, rain-on-glass shader | a second full interior | a third lighting state (AfterHours) + ~15 props for the existing venue (C10) |
| First impression | cinematic, dashcam "real footage" | narrative, but no pool on screen | **the real game at full quality in the first second** |
| Gameplay link | none until the walk inside | none | Practice starts **in place** — pick up the cue |
| Warm-up | shaders of a scene you never play | same | warms the first venue's PSOs, textures and Lumen caches |
| Risks | heaviest GPU load (translucency, rain) on the very first screen; motion from wipers | double environment upkeep | variety (mitigated: the menu shows the last visited venue after hours) |

### 6.2 Decision: C "Closing Time"

Reasons: (1) the trailer's hook "is this real?" works best when the first thing a player or a stream viewer sees is the real table, lamp and room — not a set built only for the menu; (2) zero extra environment: the dive bar must reach trailer quality anyway; (3) the menu level *is* the venue, so its shaders, textures and Lumen caches are warm and Practice starts without loading; (4) an attract mode comes for free (6.4); (5) the story carries it: the bartender lets the character practise after closing. Concept A is kept as a **V2 career intro** (first night: park, rain, walk in); it needs a street exterior and a car interior that do not exist yet (the venue has only a backplate behind the glass block, VDB 4.2 L33, TH-7), so it is costed separately. B is dropped (the phone replaces the apartment as the hub).

With more venues, the menu shows **the venue of the last session after hours** (the Kneipe after the last round: chairs on the tables, the "Stammtisch" sign); each venue provides an `AfterHours` lighting sublevel.

### 6.3 Layout and stations

```
Floor plan (Low Bridge Tavern, VDB 2.3-2.4; not to scale; X from the street (left) to the back wall, Y downwards)
 Y=0 (bar wall)                                                        X=16.46 (back wall)
 +--------------------------------------------------------------------------------+
 |      [back bar ........ TV-1 (S4) .......]      [left-wall ledge + 3 stools]    |== corridor ==> EXIT
 |G     [BAR COUNTER ....... phone (S1) ......]                                    |
 |G       o  o  o  o  o  o  o  o  o  #10 (Deacon)                                  |
 |G                                   S0 ►              C3 + shelf                 |
 |G                       C1        C2                                     back    |
 |G                                                  S2 ► ┌──────────┐     ledge   |
 |E S6                                                     │ 7-ft     │ lamp  TV-2  |
 |E     [booths B1  B2  B3]       [dart]   [jukebox]       │ table    │             |
 |A                                            [chalkboard] [cue rack + POOL neon]  |
 +--------------------------------------------------------------------------------+
 X=0 street: glass block G (window neons), door E, ATM A                 Y=7.32 (right wall)
```

Stations (placed by the venue generator as `RbCam_Menu_S0..S7`, C10; V frame, metres). Every station is an `ARbLookDevCamera`-style cine camera that applies the **Eyes** camera model (pupil A = 4 mm → `N = f / A`, auto exposure with the Eyes limits and rates, ARCH 6.3 / R-08) with a per-station vertical FOV; no cinematic f/2.8 bokeh, because the menu must look exactly like the game it opens into.

| Station | Camera → look-at | V-FOV | Shows |
|---|---|---|---|
| S0 Overview | (9.40, 3.20, 1.60) → (13.76, 5.43, 0.85) | 50° | from the bar end beside Deacon's stool: the lit table 4.9 m away, lamp, jukebox glow right, POOL neon and cue rack, TV-2 dark on the back wall |
| S1 Career | (9.15, 2.80, 1.45) → phone at (8.90, 2.05, 1.08) (bar top Z 1.07, clear of the service flap X 9.14-9.75) | 30° | the character's phone face-up on the bar top, screen lit (unread count) |
| S2 Play | (12.00, 5.43, 1.62) → (14.30, 5.43, 0.76) | 50° | the table from the head end (= VDB V03), house cues in the rack |
| S3 Practice | pawn eye point at the head end | player FOV | hand-over to the first-person pawn (in place) |
| S4 Replays | (6.80, 3.00, 1.55) → TV-1 (6.80, 0.35, 2.30) | 40° | TV-1 over the back bar playing the replay loop (the only TV on in AfterHours) |
| S5 Settings | current station | — | scrim over the current view |
| S6 Quit | (2.60, 5.20, 1.60) → door (0.00, 6.10, 1.30) | 50° | the front door (E02) with its mirrored gold-leaf transom and the draft curtain |
| S7 Credits | venue-placed | 40° | the Polaroid wall of league nights (VDB S14 / M14) |

```
+------------------------------------------------------------------------------------------+
|                                                                                          |
|   (live 3D: after-hours bar, camera at the current station, rain on the front window)   |
|                                                                                          |
|      RAW BREAK                        <- logotype SVG, chalk.100, 64 px cap height       |
|                                                                                          |
|    ▌ Continue        Night 12 · Low Bridge · $184 · 2 h ago       <- t.body, chalk.300   |
|      Career                                                                              |
|      Play                                                                                |
|      Practice                                                                            |
|      Replays & photos                                                                    |
|      Settings                                                                            |
|      Credits                                                                             |
|      Quit                                                                                |
|                                                                                          |
|    [↵] Select   [Esc] Back                                          v0.4.0 · EN          |
+------------------------------------------------------------------------------------------+
```

* List: left column 8 % from the left edge, vertically centred, `t.menu`; focus = 4 px amber bar + `chalk.100`, unfocused `chalk.300`. A soft gradient (`ink.900` 0.7 → 0 over the left 35 % of the screen) keeps the list readable over any station; no panel. The contrast test (UX-T04) samples the worst station frame behind the list.
* Focusing an item moves the camera to its station (`m.station`); selecting opens the sub-flow. *Continue* is hidden without a career save; *Play* opens a sub-list (vs AI · Hot-seat · Custom match…). If a station fails the contrast test UX-T04 (a bright neon or the lamp behind the list), the list gets a `scrim.menu` panel on that station instead of the gradient.
* **Clean first seconds** (UX-P1, UX-M01): on every launch the scene runs alone for 2.5 s (sound, neon hum, no logotype, no list), then logotype and list fade in over 400 ms; any input shows them at once.
* The beer clock and the phone's lock screen show an **after-hours time** (02:40 plus the real minutes since launch, holding at 03:59), never the real local time: a 3 pm wall clock in a closed night bar would break the fiction. The phone on the counter shows the unread count of the career's messages.

### 6.4 States

| State | Behaviour |
|---|---|
| First visit | 5.9 |
| Idle | camera micro-drift ±2 cm at S0 (off with Reduced motion); neon hum; rain heard outside (audio only) |
| Attract (60 s without input) | the list fades to 20 % opacity; a bundled replay (deterministic `FRbShot`: an 8-ball break — the house game — and a massé around a blocker; no jump shot, the Low Bridge's house rules ban them, VDB 1.1) plays on the real table in the Phone replay look (10.2); any input restores the list within 150 ms. The bundled shots are generated by a script for the menu venue's exact table and ball set (`SevenFootBar`, `OldBarOversizedCue`) and store the TableSpec/physics/rules hashes; a hash mismatch after a physics change fails the build (regenerate), so the attract replay can never desync |
| Sub-menu | the list is replaced by the sub-list with a back row |
| Transition to Practice | 0.8 s: the camera moves to the head-end eye point and hands over to the pawn's camera; the list fades out; the director starts Practice in place (contract C4) |
| Transition to Career | same venue: the bar lights come up ("opening time", 1.5 s exposure change) while NPCs stream in; another venue: fade to black, loading line "Heading to {Venue}…" |
| Error | modal dialog (save missing or corrupted → offer the backup, 15.5) |

### 6.5 Implementation

Map `/Game/Generated/Maps/L_DiveBar`, built by the venue's single generator `rb_make_divebar.py` (VDB 13.8; no sublevels invented here). The venue spec defines the lighting states *Open* and *Lights-Up* (VDB 4.6); the menu needs a third state **AfterHours** in the same mechanism (a state column in `lights.json` plus a visibility tag on props, switched at runtime by the venue's lighting-state component; request C10): troffers off; bar pendants L5-L8 off; table lamp L1-L3 on; back-bar strips L9-L10 at 30 %; coolers L11-L12 on (they never switch off); neon N1-N5 on; jukebox L18 idle (panels on, no music); TV-2 off; **TV-1 on** with the replay loop (S4); dart machine off; bar stools as the `AfterHours` prop set upside down on the counter; NPCs absent. Exposure stays physical (Eyes limits, EV100 ≥ 2.0). The menu runs `ARbGameMode` in phase `Menu` (C4) with an `ARbMenuDirector` that owns the station cameras (`RbCam_Menu_S0..S7`, 6.3) and the attract replays. Practice started from the menu keeps the AfterHours state (the bar is closed; Terri lets you play). Screen: `SRbMainMenu`. Menu frame cap 60 fps by default (13.4). Until the venue exists, the menu runs in `L_M1_TestRoom` with its lamp only (UI-4 is not blocked). Headless capture: `-RbUiScreen=MainMenu -RBCaptureCamera=RbCam_Menu_S0`.

---

## 7. Career hub: the character's phone

### 7.1 Purpose

Everything between shots that a real player does on a phone: messages from the mentor, bartenders, rivals and hustlers; what is on tonight; league and tournament flyers; money; people; photos and saved clips. It replaces a menu-driven "career screen" and keeps the no-HUD promise.

### 7.2 Physical model and readability (DERIVED)

* Fictional phone, no brand, 6.1″ 19.5:9 screen (0.139 × 0.064 m), render target **1080 × 2340**.
* **Raised pose** (`P` / D-pad up): the phone comes up in the grip hand to 0.25 m from the eye, screen facing the camera, in `m.raise`. While raised, the Eyes camera narrows its vertical FOV from the player's setting to **36°** (the eye "focuses"; Headcam uses the flat fallback of 7.6 because its distortion would bend the text).
* Screen height on the monitor: `(0.0695 / 0.25) / tan(18°)` = 0.278 / 0.325 = **0.856 of the screen height** → 924 px at 1080p → 0.395 screen px per phone px. For ≥ 18 px body height at 1080p, phone body text must be ≥ 46 phone px → phone OS type scale: body **48 px**, secondary 40 px (minimum), titles 72 px, big numbers 120 px (phone pixels). At 800p (Deck): 0.29 screen px per phone px → body 14 px ≥ 12 px.
* With *Reading zoom* **Off** the FOV stays at the player's value (e.g. V = 50°): the screen then covers `0.278 / tan 25°` = 0.60 of the screen height → 0.276 screen px per phone px → 48 px body = 13.2 px < 18 px. So *Reading zoom Off* switches the phone to the flat presentation (7.6) whenever the projected body height at the current FOV and resolution falls below 18 px (the same formula, evaluated live; UX-T21).
* Luminance and look: see 3.6 (emissive under glass, auto-brightness from the ambient illuminance at the phone — never from the camera exposure —, dark theme at night, fingerprint smudges, the lamp reflected in the glass).
* Not usable while down on a shot; allowed while standing, during the opponent's turn and between racks. Notifications arriving while the player is down are held until they stand up (UX-P1).

### 7.3 Phone OS layout (phone pixels, portrait)

```
 Lock screen                         Home                               Messages (thread list)
+----------------------------+      +----------------------------+     +----------------------------+
| 23:47            ▂▄▆ 72 %  |      | 23:47            ▂▄▆ 72 %  |     | ◀  Messages                |
|                            |      |                            |     |----------------------------|
|          23:47             |      |  [Msg •3] [Tonight] [Flyer]|     | (D) Deacon           23:40•|
|     Friday, 12 October     |      |                            |     |  Stay down on the cut.     |
|                            |      |  [Money]  [People]  [Photo]|     | (T) Terri (bar)      21:12 |
| ┌────────────────────────┐ |      |                            |     |  Table's free after 11.    |
| │ Deacon · now           │ |      |                            |     | (S) Sonny            19:03•|
| │ Stay down on the cut.  │ |      |                            |     |  $40, race to 3. Tonight?  |
| └────────────────────────┘ |      |                            |     | (N) Nina (league)       Tue|
| ┌────────────────────────┐ |      |                            |     |  Sign-up closes Sunday.    |
| │ Sonny · 19:03          │ |      |  wallpaper: last photo     |     |                            |
| │ $40, race to 3?        │ |      |  from photo mode           |     |                            |
| └────────────────────────┘ |      |                            |     |                            |
|        press to open       |      |                            |     |                            |
+----------------------------+      +----------------------------+     +----------------------------+
```

```
 Thread with an offer                Tonight                             Money
+----------------------------+      +----------------------------+     +----------------------------+
| ◀  Sonny                   |      | ◀  Tonight · Fri           |     | ◀  Money                   |
|----------------------------|      |----------------------------|     |----------------------------|
|  heard you been running    |      | ● Low Bridge  till 2:30    |     |   Cash on you              |
|  tables at the Low Bridge  |      |   here · coin-op · $1.50   |     |        $184                |
|  $40, race to 3. Tonight?  |      |   Sonny's in (Fridays)     |     |----------------------------|
|                            |      | ○ {PoolHall}  open till 3  |     |  Tonight                   |
|                            |      |   20 min · bus $2.50       |     |  +40.00 Sonny, race to 3   |
|                            |      |   9-ft · $8/h · league Tue |     |  −4.50  coin-op, 3 racks   |
|----------------------------|      |                            |     |  −7.00  2 drafts           |
|  [ Deal ]    [ $20 ]       |      |----------------------------|     |----------------------------|
|  [ Race to 5 ]  [ No ]     |      |  [ Go ]      [ Details ]   |     |  Owed to you: Big Lou $20  |
+----------------------------+      +----------------------------+     +----------------------------+
```

Names follow the venue spec written in parallel (`Docs/specs/venue-dive-bar.md` 1.1-1.2, all PROPOSED there): the Low Bridge Tavern, Terri (owner/bartender), Eddie "Deacon" Marsh (the mentor), Sonny Castellano (local hustler, Fridays), Big Lou, Nina (league captain). {PoolHall} is still a placeholder (PO Q3).

Apps (6, fixed 3 × 2 grid):

| App | Content | Actions |
|---|---|---|
| **Messages** | threads with the mentor, bartenders, the league secretary, rivals, hustlers; mentor lines when the mentor is not in the venue (HF 3.9) | quick replies (≤ 4 chips): accept / counter / decline; counters change the stake in $10 steps (never above cash on hand) and the race length |
| **Tonight** | venues open now with hours, travel time and cost, table type and price, events tonight (league night, tournament with entry fee), rumours ("Sonny's in tonight") | Go (travel; in-game time passes), Details |
| **Flyers** | photos the character took of paper flyers (league sign-ups, tournaments); the printed flyer is venue-language art, the app shows the localised details | Sign up (entry fee), Remind me |
| **Money** | cash on hand (big number), tonight's ledger, owed/owing side bets | read-only; in-game cash only, never real money (HF Q6) |
| **People** | everyone met: name, where they play, how they play in words ("plays like a league player; jabs when it matters"), link to the notebook page | Message, Open notebook page |
| **Photos** | photo-mode pictures and saved replay clips (10.5) | View, Play clip (replay viewer), Set as wallpaper, Delete (hold 1.2 s) |

Status bar: in-game time, cosmetic signal and battery (never runs out). No settings app (settings are a player-level concern in the pause menu).

### 7.4 States

`Pocketed` → (buzz) `Pocketed+Pending` → `Raising` → `Locked` (lock screen, up to 3 notifications) → `Home` → `App/…` → `Lowering`. New messages while raised: a banner slides in at the top of the phone screen (never on the monitor). A buzz = haptic sound + controller rumble (0.12 s, 30 %) + caption "[Phone buzzes]" when captions are on.

### 7.5 Inputs

| | Keyboard / mouse | Controller |
|---|---|---|
| Raise / lower | `P` (toggle) | D-pad up |
| Navigate | `W/S/↑/↓`; mouse pointer on the screen (ray–plane mapping, 3.6); wheel scrolls | LS / D-pad |
| Select | `Enter`, LMB | A |
| Back / home | `Esc` or RMB (back), `Home` (home screen) | B (back), Y (home) |
| Quick-reply chips | `1`-`4` | D-pad left/right + A |

### 7.6 Flat mode and implementation

*Phone display: In hand (default) / Flat.* Flat is forced above 125 % text size, with Headcam, and with *Reading zoom Off* when the in-hand text would measure < 18 px (7.2); it can also be chosen. The same `SRbPhoneOS` widget is re-parented into layer 50 inside an explicit `SDPIScaler` (render-target widgets are authored in phone pixels, 3.2 `FRbUiScale`), centred, 62 % of the screen height, over an opaque `ink.900` phone body (the phone's own dark UI supplies the contrast), at a scale where the phone's 48 px body becomes ≥ 24 px × text scale. Implementation: `ARbPhone` (mesh + `URbDiegeticScreenComponent`), `SRbPhoneOS` (status bar, app host, banners), `SRbPhoneApp_*`; data from the career subsystem through read-only view models; a mock data provider for tests and captures (`-RbUiScreen=Phone.Messages`).

---

## 8. Match setup

### 8.1 Quick match / hot-seat / custom (from the main menu)

A flat sheet whose layout follows a league score sheet (ruled lines, no texture).

```
+------------------------------------------------------------------------------------------+
|  NEW MATCH                                                                               |
|  ────────────────────────────────────────────┬────────────────────────────────────────── |
|  Mode           ◀ Hot-seat ▶                 │  At a glance                               |
|  Game           ◀ 9-ball ▶                   │  9-ball · WPA rules · race to 5            |
|  Rules          ◀ WPA ▶        [Edit…]       │  Alternate break · lag for the first       |
|  Race to        ◀ 5 ▶                        │  break · three-ball rule · push-out after  |
|  Break          ◀ Alternate ▶                │  the break · three fouls lose the rack     |
|  Lag            ◀ On ▶                       │                                            |
|  Venue / table  ◀ Low Bridge · 7-ft coin-op ▶│  Table: 80 × 40 in, oversized bar cue      |
|  Player 1       [ Anna        ]  ◀ Real ▶    │  ball (draws less)                         |
|  Player 2       [ Ben         ]  ◀ Assisted ▶│                                            |
|  Pressure       ◀ On ▶                       │  Assists are shown next to each name       |
|  Shot clock     ◀ Off ▶                      │  on the score slate.                       |
|  Chores         ◀ Brisk ▶                    │                                            |
|  ────────────────────────────────────────────┴────────────────────────────────────────── |
|  [↵] Start   [Esc] Back   [R] Reset                                                      |
+------------------------------------------------------------------------------------------+
```

| Row | Values | Default | Notes |
|---|---|---|---|
| Mode | Practice / Hot-seat / vs AI | vs AI | Practice: one shooter, rules on (ARCH 7.1) |
| Game | 9-ball / 8-ball / 10-ball / 14.1 | 9-ball | |
| Rules | WPA / WPA legacy / APA 8-ball / Blackball / Bar house rules | WPA | *Edit…* opens the `RulesConfig` switches of RUL 12.1 as rows; a changed preset shows "(custom)" |
| Race to / target | 1-21 racks; 14.1: 50 / 100 / 125 / 150 points | 5 / 100 | |
| Break | Alternate / Winner / Loser | Alternate | RUL 12.1 `BreakOrder` |
| Lag | On / Off | On | hidden in Practice (ARCH 5.2) |
| Venue / table | unlocked venues + the test room; halls: the table number | last used | shows table size and cue ball from the table preset (EQP 1, 6.2): "80 × 40 in" / "203 × 102 cm" by the *Units* setting; Low Bridge: oversized 60.3 mm / 221 g bar cue ball |
| Opponent (vs AI) | Tourist … Touring pro in words, plus named characters met in the career | League player | no numeric rating (UX-P9) |
| Player names (hot-seat) | text ≤ 16 chars | "Player 1/2" (localised) | Deck: floating keyboard (16.7) |
| Assists per player | Pure / Real / Assisted / Relaxed | Real | HF 5.4; guests at 50 in every attribute (HF Q4) |
| Pressure | On / Subtle / Off (both players) | On | HF-15 |
| Shot clock | Off / 35 s (+ one 25 s extension) | Off | RUL 4.10 |
| Chores | Full / Brisk / Minimal | from settings | HF principle 5 |
| Seed | auto / number (advanced foldout) | auto | reproducible matches for testing |

Input count: a default match starts with 2 presses (Play → Start). Implementation: `SRbMatchSetup` → `FRbMatchSetup` → `ARbGameMode` URL options (`?Mode=&Game=&Race=&Lag=&Seed=&Pressure=&Attr=&Noise=` exist [code ✓]; add `&Rules=&Venue=&P1=&P2=&A1=&A2=&Clock=`, contract C4).

### 8.2 Career: challenges are negotiated, not configured

In the career there is no setup sheet. A match comes from a phone message (7.3) or from an NPC walking up. The negotiation is a dialogue in the subtitle zone with up to 4 numbered choices (layer 40):

```
                     Sonny: "Forty bucks, race to three. You break."
          [1] Deal.   [2] Make it twenty.   [3] Race to five.   [4] Not tonight.
```

Rules: a stake never exceeds the cash on hand (the hustler may suggest more; the choice is greyed with "You have $184"); game and rules follow the venue's house rules, summarised in one line ("Bar rules: 8-ball, ball in hand behind the line after a scratch" — the bar-rule toggles of RUL 12.6); "Ask about the rules" plays a spoken summary with a subtitle list. A deal ends in a handshake; the stake (generic, non-copying bills, VDB Q5) is tucked under the head-rail corner casting, visible on the table, and the coin-op flow starts (9.12). Hot-seat matches never involve money. Challenges from AI regulars at other tables of a hall (2.4) use the same dialogue.

---

## 9. In-game flows

### 9.1 Live-info elements (layer 10): foul pips, mandatory lines, turn and result lines

Position: bottom-left inside the 3.5 % safe zone, stacked upwards, each line on its own `scrim.card` plate (radius 2 px, padding 8 × 14 px), `t.pips`. Nothing else ever sits in this corner.

```
                                                                    (world, no other UI)
  Ball in hand — behind the head string                  <- mandatory line (chalk.100)
▌ ●● Ben · 2 fouls — a third foul loses the rack         <- 4 px signal.foul bar, red pips outlined in chalk.100,
                                                            text chalk.100, one m.pulse
  ●  Anna · 1 foul                                       <- foul pip, chalk.100
```

**Foul pips** (decisions: "foul count always visible"; RUL 16 item 12): one line per player whose consecutive-foul count is > 0, in every mode including Diegetic-only, whenever the discipline's `ThreeFoulRule` is on (9-ball, 10-ball, 14.1; WPA 8-ball has it off, RUL 12.1 — there the pips are not shown and the enforced foul still gets its result line). Dots = count (● = 1, ●● = 2). At 2 the line gets a 4 px `signal.foul` bar on its left edge and red pips with a 1.5 px `chalk.100` outline, the text stays `chalk.100` (the 0.88 plate cannot carry red text at 4.5:1, 4.1), reads "a third foul loses the rack" (14.1: "a third foul costs 16 points and a re-rack", RUL 9.4, 13) and pulses once. 9/10-ball counts reset per rack, 14.1 never (RUL 16 item 12).

**Mandatory lines** (only while pending; text keys in `Ui.Live.*`):

| Condition (from `FRbMatchPresentation`, C5) | Line (EN) | Until |
|---|---|---|
| Ball in hand anywhere / behind the head string | "Ball in hand — anywhere" / "… — behind the head string" | the stroke |
| Spot request possible (RUL 4.4) | "All legal balls are behind the line — [Q] ask to spot the {Ball}" | the placement |
| Push-out / safety declared | "Push-out declared" / "Safety declared" | the stroke |
| Explicit call required and made | "Called: {Ball} → {Pocket}" (pocket names 9.4) | the stroke |
| Explicit call required, not made, player is down | "No call yet — [C] call your shot" | the call |
| Auto-declared frozen ball (RUL 4.11) | "Cue ball frozen to the {Ball}" / "{Ball} frozen to the rail" | the stroke |
| Stalemate warning (9.9) | "Stalemate warning — {N} more turns each" | resolved |
| Shot clock ≤ 10 s (RUL 4.10) | "{Seconds} s" in `t.mono` + "[T] Extension (+25 s)" once per rack (controller: tap View) | the stroke |
| Pending decision, other player (hot-seat) or AI deciding | "{Player} is choosing…" | decided |

**Turn line** (hot-seat, 3 s): "Ben — your shot". **Result line** (4 s after the balls stop; refines ARCH 6.6's auto-glance): always for an enforced foul — "Foul — no rail after contact (Rule 3.3) · Ben has ball in hand" (RUL 16 item 2); for a ball off the table "Foul — the 3 left the table (Rule 3.5)" (2.4); for non-foul results only with *Shot result line: Always* (default *Fouls only*, UX-P1). Non-foul outcomes the incoming player must act on (a pending decision) are covered by the decision card, not by the result line.

### 9.2 Glance (hold `Tab` / View)

Two styles, setting *Glance style: Auto (default) / Look / Card*:

* **Look (diegetic)**: while standing, the character turns the head to the score slate/scoreboard **of the player's own table** in 0.35 s, the Eyes FOV narrows to **30°** for legibility (`m.zoom`), and returns in 0.35 s on release. Venue rule (DERIVED, for the environment team): at 4.0 m and V = 30°, 1 mm of slate = 0.504 px at 1080p (0.448 px at 4.5 m), so the match section's letters must have a body height **≥ 50 mm** (≥ 22 px at 4.5 m) and the slate must be within **4.5 m** of every shooting position. Low Bridge check (VDB 2.3): the chalkboard E11 (X 12.30-13.20 on the right wall) is at most **4.3 m** from any standing position around the table (farthest: the foot-left corner on the column side, ≈ (15.4, 3.9)); from the wall-side aisle it is behind the player, where the line trace fails and *Auto* uses the card.
* **Card**: a flat card top-left (560 px wide at 1080p, `scrim.card`), fades with `m.glance`. Used when down on the shot (turning the head there is not a real option), when no slate is in view (line trace), with Reduced motion, in venues without a slate, and in style *Card*.
* *Auto* = Look when standing and possible, Card otherwise. `F1` pins the card (ARCH 6.5).

```
 9-BALL · WPA · RACE TO 5                          Hot-seat
 ──────────────────────────────────────────────────
 ▶ Anna  (Real)          3        ● 1 foul
   Ben   (Assisted)      2
 Rack 6 · Anna broke · lowest ball ④ 4
 Last: Ben — foul, no rail after contact (Rule 3.3) → ball in hand
 Practice · stroke: input 70 % · hand 20 % · chalk 10 %          <- Practice/Assisted only (9.14)
```

With *Ball number tags while glancing* (13.10) the glance also shows small number tags above every ball on the table while held.

### 9.3 The score slate (diegetic carrier)

In the Low Bridge this is the house-rules chalkboard on the right wall (VDB E11 / H13, 0.90 × 0.75 m, bottom edge 1.15 m). VDB 8.4 already makes its text data-driven (rules from the active `RulesConfig`, queue names from the match director), so the **whole board face is one live render target** owned by `ARbScoreSlate`; the venue delivers the mesh, frame, chalk tray and a `Face` material slot with the erased-text ghosting as a static layer (C10), and no chalk text is baked. Zones on the 1536 × 1280 target (1707 RT px per metre):

| Zone | Area (board) | Content | Size |
|---|---|---|---|
| House rules | left 0.50 m × top 0.45 m | 8 rule lines generated from `RulesConfig` (discipline, call mode, jump rule, loser racks, price) | Kalam 52 RT px em (30 mm), 36 mm pitch; decorative, never required to play (UX-P11; the rules are in *Help*) |
| Queue | right 0.40 m × top 0.45 m | "NEXT:" + waiting names from the director (quarters-on-the-rail order, HF-76) | Kalam 70 RT px em (41 mm) |
| Match | full width × bottom 0.30 m | header "9-BALL · RACE 5", two names with assists in brackets, tally marks per rack won (groups of five struck through), a circled **F** per consecutive foul (wiped when the counter resets), an arrow at the shooter in hot-seat | `t.hand.slate` Kalam Bold **96 RT px em (56 mm)**, 3 lines at 67 mm pitch = 0.20 m + margins |

Rendered by `ARbScoreSlate` (`URbDiegeticScreenComponent`, `SRbScoreSlate`, chalk material, 3.6); one per table (2.4). Updates are written by a hand: between racks by the loser (P-mode chore while the winner racks; C = cut; R = the player writes it with one click), foul marks by the opponent on their way to the table. Pool hall: a wall scoreboard; arena: an LED board with foul lights (later venues).

### 9.4 Shot calling by gesture

Applies in 8-ball, 10-ball and 14.1, where every shot after the break is called (RUL 4.5, 6.4, 8.4, 9.3). `CallMode` decides how: *Explicit* (Pure) needs the gesture before every called shot; *ObviousAssist* (Real/Assisted/Relaxed, HF 5.4) needs it only for shots that are not obvious (banks, kicks, combinations, caroms); bar house rules with `EightOnly` need it only for the 8 (RUL 12.6). 9-ball has no calls; pressing `C` there says once "No calls in 9-ball" (subtitle).

```
Standing ──hold C──► POINTING: the character raises the cue tip; the ball nearest the view ray inside a 3° cone is
                     selected (ties: nearest to the camera); the tip follows the gaze
          ──look at a pocket (6° cone)──► the tip points at the pocket
          ──release C on a pocket──► CALLED: voice "Seven. Corner." + subtitle; mandatory line (9.1); 8-ball: the marker
                                     coaster is placed next to the pocket for the 8 (HF-76)
          ──release C elsewhere──► cancelled, nothing changes
Down ─────Q/E──► cycles the pocket for the ball the cue currently aims at (ghost-ball target from the aim direction,
                 never drawn in Real); the mandatory line updates
```

* Pocket names (EN/DE): "corner, head left", "side, left", "corner, foot right" … from the shooter's current side of the table (left/right as seen by the shooter), and "Safety" as a pseudo-call.
* Assisted/Relaxed: while `C` is held a thin chalk-white ring (world-space, 2 mm line) marks the selected ball and pocket. Real/Pure: only the pointing gesture and the voice.
* *ObviousAssist* (casual default): no call needed for obvious shots; nothing is shown; the rules infer (RUL 4.5).
* Inputs: `C` hold + two looks + release (≤ 2 s). Controller: hold `Y`, right stick, release; while down, `Y` taps cycle the pocket.

### 9.5 Declarations: push-out and safety (`X` / controller RB while standing)

`X` toggles the one declaration the rules currently allow (`GetShotConstraints`: push-out in 9/10-ball in the shot after a legal break; safety in 8-ball and 14.1; never in 10-ball, RUL 4.5), sent to the director with the existing `SetShotKind` (C4): voice "Push out." / "Safe.", subtitle, mandatory line until the stroke. Pressing again withdraws it ("Scratch that — I'm shooting."). Not available → one subtitle "No push-out now." Must be declared before getting down (a declaration while down stands the player up first, so it is a visible, deliberate act).

### 9.6 Ball in hand

1. **Hand-over**: the opponent rolls the cue ball over (bar), the referee places it at the head spot (arena), or after a coin-op scratch the cue ball arrives in the return (HF-72) and the player walks to fetch it (chore speeds apply).
2. **Placing**: the cue ball sits in the player's hand and follows the placement point on the cloth (ARCH 6.2 `PlacingCueBall`; Look moves the point). The hand never lets go at an illegal spot: overlapping balls, off the bed, or outside the kitchen when the region is *behind the head string*. The refusal is physical (the hand hovers, a soft knock sound) plus the caption line "Not here — behind the head string only". Real tables have no drawn head string; Assisted/Relaxed draw it as a faint chalk line on the cloth.
3. **Confirm**: `LMB`/`Enter`/`F` (controller A) puts the ball down; `F` again while standing next to it picks it up again (RUL 4.4: adjustable until the stroke). `V` (hold, controller X hold) wipes the cue ball with the towel, 2.5 s (HF-40).
4. **Spot request** (RUL 4.4): when all legal balls are behind the head string, the mandatory line offers `Q`.

### 9.7 Decisions (decision card, layer 40)

Whenever the rules enter `AwaitDecision` (RUL 11.3), the deciding player gets a card at 62 % screen height, centred, `scrim.card`, 720 px wide. The opponent/referee asks aloud first; the card appears 0.5 s after the line starts. The decider may walk around and look while the card is shown minimised as a mandatory line ("Your choice: [Q/E] Shoot · Pass back — [Enter] confirm").

```
            ┌─ BEN PUSHED OUT ─────────────────────────────────────────────┐
            │  Anna, your choice:                                           │
            │                                                               │
            │   ▌ Shoot from here        Pass it back                       │
            │                                                               │
            │  After a push-out the other player chooses who shoots next.   │
            │  (Rule 5.4)                         [Q/E] Choose  [↵] Confirm │
            └───────────────────────────────────────────────────────────────┘
```

| Situation (RUL) | Options on the card (maps to the rules' option enum) |
|---|---|
| Lag won (`LagWinnerChooses`, 4.1, 11.3; also the new lag after a 14.1 stalemate, 9.6) | I break · You break (core `ChooseBreaker`, ARCH 5.2) |
| Push-out played (9/10-ball, 7.3 / 8.3) | Shoot from here (`ShootFromPosition`) · Pass it back (`PassBack`) |
| Illegal break under the three-ball rule (7.2) | Take the table — no push-out (`AcceptTableNoPushOut`) · Hand it back — breaker may push out (`HandBackPushOutAllowed`) |
| 8-ball illegal break / 8 on the break (6.2, `EightOnBreak`) | Take the table (`AcceptTable`) · Ball in hand behind the line (`BallInHandAboveHeadString`) · Re-rack, I break (`RerackDeciderBreaks`) · Re-rack, you break (`RerackOffenderBreaks`) · Spot the 8 and continue / with ball in hand (`Spot8…`) — only the options the rules offer |
| 14.1 breaking foul (9.2) | Take the table · Re-rack and re-break (`RequireRebreak`) |
| 10-ball wrongly pocketed ball / uncalled shot (8.4) | Shoot from here · Pass it back |
| Stalemate proposal (9.9) | Accept · Decline |

AI deciding: voice line ("I'll shoot it.") + mandatory line for 3 s. Hot-seat: the card names the decider; the other player's input is ignored. Inputs: `Q/E` or `1-4` or mouse click, `Enter`/`F`/LMB confirm; controller D-pad left/right, A. Input count: ≤ 2.

### 9.8 Three-foul warning

At a count of 2: the foul pip turns red and pulses once (9.1); the opponent (bar) or referee (hall/arena) says "That's two on you." with subtitle; the slate gets its second circled F. There is no blocking prompt: under WPA 2025 the displayed count *is* the warning (RUL 16 item 12, Reg 8). On the third foul: result line "Third foul in a row — the rack goes to Anna (Rule 3.13)", the slate tally, and the rack-over flow (9.15); 14.1: "−16 points, re-rack, Ben breaks" (RUL 9.4).

### 9.9 Stalemate

* Auto-warning after `N_stall` = 8 consecutive innings without a pocketed ball or foul (RUL 4.8, DERIVED heuristic): referee/opponent line "Nobody's getting anywhere. Three more each." + mandatory line with the turns left.
* No progress after 3 more turns each → "Stalemate — Anna broke this rack and breaks again" (8/9/10-ball) / "Stalemate — new lag, scores carry over" (14.1), re-rack via chores.
* Early agreement: *Pause > Match > Propose stalemate* → the other player gets the decision card (hot-seat) or the AI answers by profile (weak AIs accept, strong AIs decline when ahead).

### 9.10 Concede

*Pause > Match > Concede match* → dialog "Concede the match to Ben? This ends the match." with **hold 1.2 s to confirm** (`SRbHoldToConfirm`) → result "Anna concedes — Ben wins 3:2" (RUL 4.8, R 1.12). In a money game the stake is paid (handshake, the bills change hands); the notebook records it.

### 9.11 Hot-seat hand-over

When the turn passes: if down, the pawn stands up; the turn line "Ben — your shot"; with *Hot-seat hand-over: Confirm* (default On) the stroke stays locked until the incoming player presses `Enter`/A ("Ben, ready? [↵]", director `ConfirmTurn()`, C4), so the previous player's hand cannot trigger anything. The slate arrow moves. Assist preset and name come from the new shooter.

### 9.12 Coin-op and chores (HF-70..79)

World prompts follow 3.5 (gaze 0.6 s within 1.2 m). Chore speed comes from *Chores* (HF principle 5: R interactive, A automatic — hold `F` to speed up 2-3×, C cut 1.5 s through black, P done while the opponent plays).

| Chore | Prompt | R (interactive) | A / C / P | UI |
|---|---|---|---|---|
| Pay per rack (HF-70) | `[F] Insert coins ($1.50)` at the coin slide | click per quarter (1 s each), push the slide | hands do it / cut / opponent pays when the loser pays | none; the coins' clunk and the balls' rumble are the feedback; "[coins drop, balls roll out]" caption |
| Clear leftover balls (HF-71) | `[F] Clear the table` | pick each ball to a pocket | per HF timings | the bartender unlock case plays a voice line; never blocks the rules |
| Fetch the cue ball from the return (HF-72) | `[F] Take the cue ball` at the return opening (Low Bridge: wall side of the cabinet, VDB A3) | walk + take | automatic walk | "[cue ball rolls down the return]" caption |
| Pick up a ball that left the table (decisions 2026-09-28) | `[F] Pick up the ball` where it came to rest (under a stool, by the jukebox) | walk + bend + take, then hand it over / spot it | A: the character fetches it / C: cut / P: the opponent or the bartender brings it back | "[ball bounces across the floor]" caption with a direction arrow; the rules state (spot or ball in hand) is already decided, the chore only moves the physical ball |
| Rack (HF-74) | `[F] Rack` at the foot spot | place balls into the triangle, tighten, lift (tightness → HF-55) | habit result | the opponent may say "Rack's loose" (V2) |
| Chalk (HF-22) | `G` anywhere (cube in pocket), or `[F]` on a cube on the rail | each twist = one click (0.4 s), aim the sweep by mouse | habitual twists | none; the tip's blue is the feedback |
| House cue (HF-30/31) | `[F] Choose a cue` at the rack | pick, roll on the cloth to see the wobble, turn the bow up | auto roll test | none; weight stamp readable on the butt |
| Wave the NPC away (HF-77) | `[F] Wave` while looking at a person in your line | — | — | voice "Mind stepping back?" |
| Stop the lamp swing (HF-78, V2) | `[F] Stop the lamp` | reach up (1 s) | wait 5-10 s | none |

### 9.13 Mentor and diegetic feedback

* **Diagnosis** (HF 3.9): after a miss, the first counterfactual cause within 3 s → at most one mentor line per 3 shots, never during the opponent's turn. The mentor (Deacon, stool #10 in the Low Bridge, venue-dive-bar 1.2) speaks in person when present, otherwise texts ("Stay down on that one." + phone buzz, read later). Lines per cause: equipment ("Chalk up — you're hitting bare leather."), table ("This table rolls toward the jukebox. Play it."), hand drift/nerves ("Breathe. Hold it at the back."), tip placement/speed ("You hit that low."), input ("You were aimed thick."). Setting *Mentor: Off / Misses only (default) / Often*.
* **Opponents and bartenders** bark in character (ducked under the player's shot sounds), all subtitled when directed at the player; background walla is never subtitled.
* **Notebook**: facts learned by experience become notebook entries (14); a pencil scratch sound and, with captions on, "[You make a note]".
* **Audio tells** of sub-pixel channels (HF 3.7): heartbeat, breathing, tip-slip squeak — setting *Heartbeat & breath tells* (13.8); captions "[heartbeat]" when captions are on.

### 9.14 Stroke report and assist visuals

* **Stroke report** (HF 5.4: Practice, or always in Assisted/Relaxed): one line in the glance card and, after a miss in Practice, a 6 s line bottom-left "Missed thick · input 70 % · hand 20 % · chalk 10 %" (shares from `ComputeStrokeShares`). Never in Real/Pure matches.
* **Aim line / ghost ball** (Assisted short, Relaxed long): drawn in the world as a faint chalk-dust line on the cloth (projected decal, 1.5 mm wide, 35 % opacity, `chalk.100`) and a ghost-ball outline; no screen-space lines.
* **Tip ring** (Assisted faint, Relaxed on): a ring on the cue ball in the down view whose radius is the miscue limit `rho_max` at the current chalk state (HF 5.4); the tip marker turns `signal.foul` outside it.

### 9.15 Rack over and match over

* Rack over: line "Anna wins the rack · 4:2" (3 s), slate tally, the next rack's chores (clear, pay, rack) at the chosen speed.
* Match over: a flat result card (layer 60, `scrim.menu`): winner and score, per player pocketed balls, break-and-runs, fouls, longest run; money (career); actions Rematch · Watch replays · Main menu (hot-seat/quick match) or Continue (career, returns to the bar with the phone buzzing if something happened). Money games end with the cash changing hands in the world before the card.

---

## 10. Replay with broadcast overlay

### 10.1 Purpose

Watch the last shot(s) again — to learn (why did it miss?), to enjoy, and to capture clips. Built on `URbReplaySubsystem` (ARCH 6.7: last 32 shots, bitwise playback, own clock, views Shooter/Overhead/Rail/Follow).

### 10.2 Two looks

| | **Phone** (default in bar, Kneipe, pool hall) | **Broadcast** (default in the arena; selectable anywhere) |
|---|---|---|
| Idea | a friend filmed it on a phone — the trailer's cold-open look | TV coverage of a match |
| Camera | handheld rig: shake 0.3-0.6°, AE hunting (fast), focus pulls, phone lens (26 mm eq., deep DoF), 16:9 or 9:16 pillarbox (toggle) | tripod/crane cameras 25-40° long lens, overhead ~90°, smooth moves (UE 4.1 Broadcast preset) |
| Degradation | allowed here only (UE 4.7): mild compression blocking, rolling-shutter skew on fast pans, sharpening | broadcast grade, optional score bug |
| Overlay | a minimal fictional camera-app frame: red REC dot, `00:03` timecode, `1×` — or *Clean* | score bug, REPLAY tag, speed, timeline, optional facts strip |

Setting *Replay look: Auto (by venue) / Phone / Broadcast / Clean*; `L` switches live.

### 10.3 Layout (Broadcast)

```
+------------------------------------------------------------------------------------------+
| {League} │ ANNA  3 │ 2  BEN │ RACE 5 · 9-BALL                       ◉ REPLAY   0.25×     |
|                                                                                          |
|                                                                                          |
|                              (replay camera view)                                        |
|                                                                                          |
|                                                                                          |
|  CUE BALL 4.1 m/s · DRAW 0.30 R · CUT 32° · TIP 0.2 mm LOW       <- facts strip (F2/opt) |
|  |▼──────●───────●──●───────────────────────●──────────────────|  00:01.84 / 00:06.20    |
|   tip    7-ball  cushions                    7 pocketed                                  |
|  [Space] Pause  [Wheel] Speed  [V] Camera: Rail  [C] Save clip  [F9] Photo  [Esc] Back   |
+------------------------------------------------------------------------------------------+
```

Timeline ticks are the shot's events (tip contact, ball-ball, cushion, pocket, jump landing) from the stored result; the playhead is the replay clock. Facts come from the stored stroke record and result (tip speed, spin in R units, cut angle, the per-shot tip placement that is below one pixel live — the replay is the tell HF 3.7 names). Score bug `t.label.caps` (26 px) on opaque `ink.900`, 44 px high, top-left (real broadcast bugs are opaque; this also lets it use `amber.400` text); action bar bottom (hidden after 3 s without input, back on any input).

### 10.4 Inputs

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Enter replay (after a shot, when `IsReplayAllowed`) | `R` | R3 |
| Camera (Shooter, Overhead, Rail, Follow cue ball, Follow object ball, Broadcast main) | `V` / `1`-`6` | Y / D-pad up-down |
| Speed 0.05 / 0.1 / 0.25 / 0.5 / 1 / 2× | wheel, `-`/`+` | LB / RB |
| Pause / play | `Space` | A |
| Scrub (hold = continuous, Shift = 1 frame) | `←/→`, drag on the timeline | LS left/right, LT/RT analog |
| Restart | `Home` | View |
| Previous / next shot in the history | `PgUp/PgDn` | D-pad left/right |
| Look (Phone look: 9:16 toggle) | `L` (look), `O` (orientation) | X |
| Save clip | `C` | hold X |
| Photo mode from here | `F9` | L3 hold |
| Exit (restores the live table) | `Esc` | B |

### 10.5 Save clip

Saves the deterministic shot (the `FRbShot` with declaration and rules version/hash, RUL 16 item 18, plus the table preset / TableSpec hash, ball set and physics version, so a clip made on the bar box never plays on a 9-ft table) as `Saved/Replays/<yyyymmdd-hhmmss>_<discipline>_<venue>.rbreplay` (< 1 MB), adds it to the phone's Photos, and marks a Steam Timeline event (16.4) so Steam Game Recording can cut the video. In-game video export (Movie Render Queue, trailer kit) is Later.

### 10.6 States and implementation

`Live → ReplayRequested → Playing ⇄ Paused → (Photo) → Ending → Live` (the end restores `FRbTableState` and the view, ARCH 6.7). Refused while a live shot simulates or plays (ARCH `IsReplayAllowed`): one caption "Wait for the balls to stop". Widgets `SRbReplayBar`, `SRbBroadcastBug`, `SRbPhoneRecFrame`; cameras `ARbReplayCamera` (+ Phone rig and Broadcast main view as new modes). Auto-replay of highlights: *Off (default) / Great shots / Every miss* (13.9).

---

## 11. Photo mode

### 11.1 Purpose

Stills for players and marketing (trailer plan: "free trailer camera / photo mode"). The time is frozen at any moment — also mid-shot: the playback cursor stops, the simulation result is already final, so nothing about the game changes (UX-T10).

### 11.2 Layout

```
+------------------------------------------------------------------------------------------+
|                                                               ┌─ PHOTO ───────────────┐ |
|                                                               │ Camera                 │ |
|             (free camera view, effects applied live)          │  Focal length  35 mm   │ |
|                                                               │  Aperture      f/2.8   │ |
|                         ┼  (focus point, only while           │  Focus         Auto ▸  │ |
|                             adjusting focus)                  │  Exposure     +0.3 EV  │ |
|                                                               │ Look                   │ |
|                                                               │  Grade    Tungsten night│ |
|                                                               │  Grain         20 %    │ |
|                                                               │  Vignette      10 %    │ |
|                                                               │  Frame        3:2      │ |
|                                                               │ Scene                  │ |
|                                                               │  Hide player   Off     │ |
|                                                               │  Hide people   Off     │ |
|                                                               │ Capture  1× · PNG      │ |
|                                                               └────────────────────────┘ |
|  [↵] Take photo   [H] Hide panel   [G] Grid   [Esc] Exit                                 |
+------------------------------------------------------------------------------------------+
```

### 11.3 Controls and limits

| Item | Values | Notes |
|---|---|---|
| Camera movement | fly within a **3.0 m** sphere around the table centre (arena later: 6 m), collision radius 10 cm, never below the floor or through walls | WASD + mouse / LS + RS; `Space`/`Ctrl` or RT/LT up/down; Shift fast |
| Roll | ±45° | `Q/E`, controller LB/RB |
| Focal length | 12-300 mm full-frame equivalent (shown in mm) | wheel |
| Aperture | f/1.2-f/22 | DoF from the thin-lens model (UE 4.5) |
| Focus | Auto (object under the centre) / Manual distance / Pick (click a point) | |
| Exposure compensation | −3 … +3 EV | on top of the frozen auto exposure |
| Grade | Neutral · Phone · 35 mm film · Tungsten night · Black & white | LUTs generated by script |
| Grain, vignette | 0-100 % | |
| Frame overlay | None · 16:9 · 3:2 · 4:5 · 1:1 · 9:16 · 21:9, rule-of-thirds grid (`G`) | the saved image is cropped to the frame |
| Hide | player body/cue, NPCs, assist visuals | |
| Capture | 1× / 2× / 4× (`HighResShot` [5.8 ✓]); PNG (SDR) or EXR (HDR) | Cinematic preset: *Path traced* option, progressive samples with a counter and `Esc` to stop (UE 1: offline reference) |

Saved to `Saved/Photos/RawBreak_<yyyymmdd-hhmmss>.png`, added to the phone's Photos and the Steam screenshot library (16.5). The panel (`SRbPhotoPanel`, layer 50) is flat UI and never part of the photo. Entry: `F9` / L3 hold (≥ 0.6 s) from live play, replay or the pause menu; not while a decision card is open. Motion blur of moving balls in stills (sub-frame accumulation) is Later.

---

## 12. Pause menu

### 12.1 Purpose, layout, states

Pause the world (single-player and hot-seat), reach settings and match actions. Opens with `Esc` / Menu, also automatically on focus loss (setting) and when the Steam overlay opens (16.1). The live playback clock pauses (contract C3); a shot in flight keeps its result.

```
+------------------------------------------------------------------------------------------+
|  (live scene, blurred, scrim.menu)                                                       |
|                                                                                          |
|    PAUSED                                             9-BALL · WPA · RACE TO 5           |
|                                                       Anna 3 : 2 Ben   · Rack 6          |
|  ▌ Resume                                             Fouls: Anna 1                      |
|    Match ▸                                            Ball in hand — behind the line     |
|    Save now (career)                                                                     |
|    Settings                                                                              |
|    Photo mode                                         Last saved 2 min ago (career)      |
|    Controls                                                                              |
|    Help: rules & how it works                                                            |
|    Save and quit to menu                                                                 |
|    Quit to desktop                                                                       |
|                                                                                          |
|  [↵] Select   [Esc] Resume                                                               |
+------------------------------------------------------------------------------------------+
```

| Item | Content |
|---|---|
| Resume | back to play, focus restored to the game |
| Match ▸ | Propose stalemate (9.9) · Concede match (9.10) · Practice only: Restart rack, Re-rack as…, Set up a drill (Later) |
| Save now | career only: saves between shots (a request during a shot runs when the balls stop; HF principle 5) |
| Settings | section 13 (same screen as from the main menu; game stays paused) |
| Photo mode | section 11, from the paused moment |
| Controls | read-only reference of the current bindings for the active device family, grouped by context (3.4), with a link to rebinding |
| Help | discipline rules in short (generated from `RulesConfig`, localised), the "How it works" page (HF principle 8: streak guard disclosure, what imperfections are) |
| Save and quit | career: autosave then main menu; quick match/hot-seat: confirm "The match will be lost" |
| Quit to desktop | confirm dialog; career autosaves first |

Inputs: mouse, keyboard (↑/↓, Enter, Esc = resume), controller (D-pad, A, B = resume, Menu = resume). Implementation: `SRbPauseMenu` (`bPausesGame`), match actions call the director API (C4).

---

## 13. Settings

### 13.1 Purpose and layout

Every option individually adjustable (decisions 2026-09-27), readable over any scene (UX-P5), usable with mouse, keyboard or controller. Pages: **Graphics · Display · Camera · Controls · Controller · Audio · Gameplay · Accessibility · Language · Other**. The same screen opens from the main menu and the pause menu.

```
+------------------------------------------------------------------------------------------+
|  SETTINGS                                                    Preset: Custom (based on High)|
|  Graphics  Display  Camera  Controls  Controller  Audio  Gameplay  Accessibility  Lang  ▸ |
|  ════════                                                                                 |
|  Quality preset            ◀ High ▶               │ Reflections                           |
|  Upscaler                  ◀ DLSS ▶               │                                       |
|  Upscaler mode             ◀ Quality ▶            │ Hit lighting: the balls and the rails'|
|  ▌ Reflections             ◀ Hit lighting ▶       │ lacquer mirror the lamp, the room and |
|  Global illumination       ◀ Lumen high ▶         │ you, fully lit. Screen space loses    |
|  Shadows                   ◀ High ▶               │ everything behind the camera.         |
|  Volumetric haze           ◀ High ▶               │                                       |
|  Texture pool              ◀ 2500 MB ▶            │ GPU cost ▮▮▮▯   VRAM +150 MB           |
|  ⋮                                                │ Changes apply immediately.            |
|  VRAM ≈ 5.9 / 8.0 GB  ▮▮▮▮▮▮▮▯                     │                                       |
|  [↵] Change  [Q/E] Page  [R] Reset page  [Hold Space] Preview  [Ctrl+F] Search  [Esc] Back |
+------------------------------------------------------------------------------------------+
```

* Row = `SRbOptionRow`: label (`t.body`), control (stepper for enums, slider for ranges with the value in `t.mono`, toggle, key button, action button), a dot marking the default, a badge for *Requires restart* / *Stutters briefly* / *Not available: {reason}*.
* Description panel (right, 440 px): what it does in one or two sentences, the realism note for camera effects, a cost hint (GPU 0-3 bars, VRAM in MB; ESTIMATE until measured), apply behaviour.
* Controller: LB/RB pages, D-pad/LS rows and values, A activate, X reset page, hold Y preview, B back. Keyboard: `Q/E` or `Ctrl+Tab` pages, arrows, `Enter`, `R`, hold `Space`, `Ctrl+F` search (matches labels, descriptions and keywords in the current language and in English), `Esc`.

### 13.2 Apply model

| Kind | Rows | Behaviour |
|---|---|---|
| Live | almost all | applied on change; the scene behind the panel updates at once |
| Confirm (15 s revert) | window mode, resolution | dialog "Keep these display settings? Reverting in 15 s" |
| Stutters briefly | material quality, ray-tracing mode switch, hero texture size | applied on change with a one-line warning |
| Restart | RHI-level ray tracing support (`r.RayTracing`; runtime toggling VERIFY) | stored, applied on next launch, badge shown |

* **Preset logic**: rows flagged *preset-driven* take the preset's value when a preset is picked; any change to one of them turns the label into "Custom (based on {Preset})"; picking a preset again overwrites. Non-preset rows (camera, controls, audio, accessibility, gameplay) are never touched by presets.
* **Preview**: holding `Space`/Y fades the whole settings layer out (120 ms) to show the live view with the new values; in the main menu the view is the current station, in game the current camera.
* **Persistence**: engine rows in `GameUserSettings.ini` via `URbGameUserSettings` (UE-8, contract C7); RAW BREAK rows in its `[/Script/RawBreak.RbSettings]` section; key bindings in the Enhanced Input user settings save (3.4). Saved when leaving the screen and on quit; *Reset page* and *Reset all* (hold 1.2 s) restore defaults.
* **Availability**: rows whose feature is missing (no RTX GPU for DLSS, no HDR display) stay visible but disabled with the reason ("Needs an NVIDIA RTX GPU").

### 13.3 Graphics

UE scalability levels are 0 Low · 1 Medium · 2 High · 3 Epic · 4 Cinematic. **5.8 facts [5.8 ✓, `BaseScalability.ini`]:** `sg.GlobalIlluminationQuality` 0 = no Lumen GI, 1 = Lumen with the *irradiance field* final gather (`r.Lumen.FinalGatherMethod=0`, "targeted at mid range PC") — the plan's "Lumen Lite" — 2 = screen-probe gather, 3/Cine = screen probes with `r.Lumen.HardwareRayTracing.HitLighting.Allowed=1`; levels 1 and 2 set `HitLighting.Allowed=0`. `sg.ReflectionQuality` 0 = no SSR and no Lumen reflections (`r.SSR.Quality=0`), 1 = SSR only, 2 = Lumen reflections at half resolution (`r.Lumen.Reflections.DownsampleFactor=2`), 3/Cine = full resolution. `r.Lumen.HardwareRayTracing.LightingMode`: 0 surface cache, 1 hit lighting for GI + reflections, 2 hit lighting for reflections.

**How the presets are built (merged UE-8, `Config/DefaultScalability.ini` on `integ/m1` [code ✓]):** preset level *p* sets every `sg.*` group to *p*; the file overrides engine rows where plan 9.4 differs (Low GI = Lumen Lite, Medium reflections = Lumen surface cache, Low shadows stay on) and adds `[RawBreak.<Option>@<level>]` rows applied **after** the groups at `ECVF_SetByGameOverride` — among them `HitLighting.Allowed=1` + `LightingMode=2` from High up, so High needs no GI level 3. The registry (C7) wraps `URbGameUserSettings::SetQualityOption(ERbQualityOption, Level)` for the eleven existing options and `SetScreenPercentage`; every new row below marked **RB-new** becomes another `[RawBreak.*]` section in the same file, never a direct cvar write, so a later group change cannot undo it.

| Setting | Values | Low | Medium | High | Epic | Cinematic | Implementation |
|---|---|---|---|---|---|---|---|
| Quality preset | Low / Medium / High / Epic / Cinematic / Custom | — | — | — | — | — | `ApplyQualityPreset` (ARCH 8.1, UE-8) |
| Upscaler | Auto / DLSS / FSR / XeSS / TSR / Off (TAA at native) | TSR or FSR | vendor | vendor | vendor | vendor at native AA | **RB-new**. Auto = DLSS on RTX, FSR on AMD, XeSS on Intel, else TSR (`r.AntiAliasingMethod=4` [5.8 ✓]). Plugins: NVIDIA DLSS for UE 5.8, AMD FSR for UE 5.8, Intel XeSS (all VERIFY versions, licences and required notices; user action in UE 11.2) |
| Upscaler mode | Ultra performance / Performance / Balanced / Quality / Native AA (DLAA, FSR native, TSR 100 %) | Performance | Balanced | Quality | Quality | Native AA | plugin quality modes |
| Resolution scale (TSR/FSR/XeSS custom) | 33-200 % | 55 | 58 | 67 | 67 | 100 (up to 200 = supersampling) | UE-8 `[RawBreak.Preset@N] ScreenPercentage` 55 / 58.328 / 66.662 / 66.662 / 100, `SetScreenPercentage` |
| Sharpening | 0-100 % | 20 | 20 | 10 | 10 | 0 | **RB-new**: upscaler sharpness / `r.Tonemapper.Sharpen` [5.8 ✓] |
| Frame generation | Off / DLSS FG (RTX 40+) / DLSS multi FG (RTX 50+) / FSR FG | Off | Off | Off | Off | Off | plugin; description: "adds view latency; your stroke keeps its own input clock" |
| Low-latency mode | Off / On / On + boost | On | On | On | On | On | engine NVIDIA Reflex plugin [5.8 ✓ present; mode API VERIFY]; AMD Anti-Lag 2 VERIFY |
| Ray reconstruction | Off / On (DLSS RR) | — | Off | Off | On | On | DLSS plugin |
| Ray tracing | Auto / On / Off | Auto | Auto | Auto | Auto | Auto | Lumen HWRT at runtime: `r.Lumen.HardwareRayTracing` [5.8 ✓]; RHI support `r.RayTracing` = restart |
| Global illumination | Lumen lite / Lumen / Lumen high / Lumen cinematic | lite | lite | Lumen | high | cinematic | `ERbQualityOption::GlobalIllumination` = `sg.GlobalIlluminationQuality` 0 / 1 / 2 / 3 / 4 (UE-8 turns level 0 into Lumen Lite) |
| **Reflections** | Screen space / Lumen (surface cache) / Hit lighting — reflections / Hit lighting — GI + reflections | screen space | surface cache | hit — reflections | hit — reflections | hit — reflections | `ERbQualityOption::Reflections` = `sg.ReflectionQuality` 0..4 + UE-8 rows (`LightingMode` 0 / 0 / 2 / 2 / 2, `HitLighting.Allowed` 0 / 0 / 1 / 1 / 1). *GI + reflections* (`LightingMode` 1) is a manual choice only. **Open for UE-8:** engine level 0 sets `r.SSR.Quality=0`, so Low currently has no screen-space reflections at all; plan 9.4 asks for SSR on Low → add `r.SSR.Quality=2` to `[ReflectionQuality@0]` |
| Reflection resolution | Half / Full | — | Half | Half | Full | Full | follows `sg.ReflectionQuality` (`r.Lumen.Reflections.DownsampleFactor` [5.8 ✓] 2 / 2 / 1 / 1); a manual change is an **RB-new** row |
| Shadows | Low / Medium / High / Epic / Cinematic | L | M | H | E | C | `ERbQualityOption::Shadows` = `sg.ShadowQuality` 0..4; UE-8 keeps shadows on at Low (local VSM LOD bias +1, 1024 pages) and sharpens Cinematic (`r.Shadow.Virtual.ResolutionLodBiasLocal=-0.5` [5.8 ✓]) |
| MegaLights | Off / On | Off | Off | per venue (bar Off, hall On) | On | On | **RB-new**: `r.MegaLights.Allowed` [5.8 ✓]; needs the project flag `r.MegaLights.EnableForProject` (C2 follow-up); the key table lamp stays classic VSM (plan 6.1) |
| Volumetric haze | Off / Low / High / Ultra | Off | Low | High | High | Ultra | UE-8 `[RawBreak.VolumetricFog@N]`: `r.VolumetricFog` [5.8 ✓], grid pixel size — / 16 / 8 / 8 / 4 |
| Texture quality | Low … Cinematic | L | M | H | E | C | `ERbQualityOption::Textures` = `sg.TextureQuality` 0..4 (UE-8 puts pool and anisotropy into these rows) |
| Texture pool | Auto / 1000-6000 MB | 1000 | 1500 | 2500 | 3500 | 4500 | UE-8 `[TextureQuality@N] r.Streaming.PoolSize` [5.8 ✓]; Auto = min(preset, 45 % of `DedicatedVideoMemory`), a manual value is an **RB-new** override row |
| Hero textures (cloth, balls, rails) | 1k / 2k / 4k / 8k | 1k | 2k | 4k (8k sources capped) | 4k + 8k hero | 8k | **RB-new**, same mechanism as VDB 11.4: `r.Streaming.MipBias` plus the texture group `RB_Cinematic8K` whose LOD-group max size is 4096 below Epic (runtime change of LOD-group limits VERIFY; fallback: applied at next launch) |
| Anisotropic filtering | 4× / 8× / 16× | 4 | 8 | 16 | 16 | 16 | UE-8 `[TextureQuality@N] r.MaxAnisotropy` / `r.VT.MaxAnisotropy` [5.8 ✓] |
| Effects & materials (ball haze + SSS lobe, cloth fuzz, chalk dust) | Low … Cinematic | L | M | H | E | C | `ERbQualityOption::Effects` = `sg.EffectsQuality`, which also sets `r.MaterialQualityLevel` 0 / 2 / 1 / 1 / 1 (UE order 0 Low, 1 High, 2 Medium, 3 Epic). Epic/Cinematic stay at 1 because UE-3's Quality Switch nodes author the High pin; level 3 would fall back to the unwired Epic pin's *Default* input. Stutters briefly |
| Post-processing quality (DoF, bloom) | Low … Cinematic | L | M | H | E | C | `sg.PostProcessQuality` 0..4 |
| Shading | Low … Cinematic | L | M | H | E | C | `sg.ShadingQuality` 0..4 |
| View distance | Low … Cinematic | L | M | H | E | C | `sg.ViewDistanceQuality` 0..4 |
| Anti-aliasing quality (TSR) | Low … Cinematic | L | M | H | E | C | `sg.AntiAliasingQuality` 0..4 |
| Hair and fabric (MetaHuman, later) | Cards / Strands | Cards | Cards | Cards | Strands | Strands max | groom LOD |
| Path-traced photos | Off / On | — | — | — | Off | On | photo mode only (11.3); `r.PathTracing` is on by default in 5.8 [5.8 ✓] |
| VRAM estimate | read-only meter | | | | | | sum of per-row costs from UE 9.3 (ESTIMATE; replace with `stat RHI` measurements per preset) |
| Rebuild shader cache | action | | | | | | 5.2 |

Nanite, VSM, Substrate and HWRT-capable Lumen are project-wide and not settings.

### 13.4 Display

| Setting | Values | Default | Apply | Implementation |
|---|---|---|---|---|
| Window mode | Fullscreen / Borderless / Windowed | Borderless | Confirm 15 s | `SetFullscreenMode` [5.8 ✓] |
| Resolution | supported list | desktop | Confirm 15 s | `SetScreenResolution` [5.8 ✓] |
| V-Sync | Off / On | Off | Live | `SetVSyncEnabled` [5.8 ✓] |
| Frame rate limit | 30 / 40 / 45 / 50 / 60 / 72 / 75 / 90 / 100 / 120 / 144 / 165 / 200 / 240 / Unlimited / Match display | Match display | Live | `SetFrameRateLimit` [5.8 ✓] |
| Menu frame limit | 30 / 60 / 120 / Same as game | 60 | Live | applied on screens with `bPausesGame` and in the main menu |
| Background frame limit | Off / 15 / 30 | 15 | Live | when the window loses focus |
| HDR | Off / On (only if supported) | Off (first launch offers it) | Live | `EnableHDRDisplayOutput` [5.8 ✓] |
| HDR peak brightness | 400-10000 nits | the OS value, or calibrated | Live | `SetMaximumHDRDisplayNits` + `SetHDRCalibrationUsed(true)` [5.8 ✓]; never the cvar directly (5.4) |
| HDR scene brightness (paper white) | 100-400 nits | 203 | Live | `SetHDRPaperWhiteNits` [5.8 ✓] |
| HDR UI brightness | 100-400 nits | 203 | Live | `SetHDRUILuminanceSeparate(true)` + `SetHDRUILuminanceNits` [5.8 ✓] |
| Brightness (SDR) | 1.8-2.6 | 2.2 | Live | `GEngine->DisplayGamma` [5.8 ✓] |
| Safe zone | 90-100 % | 100 % | Live | UI margins |
| Calibrate display… | action | | | the panels of 5.4 |

### 13.5 Camera

All camera rows are per look where marked (Eyes and Headcam keep their own values). Defaults are the UE 4.9 table.

| Setting | Values | Default (Eyes / Headcam) | Notes / implementation |
|---|---|---|---|
| Camera look | Eyes / Headcam | Eyes | `ERbCameraPreset` |
| Field of view (vertical, horizontal shown) | 40-75° | 50° / 58.7° | per look; MaintainYFOV (ARCH 6.3) |
| Wide-angle correction (Panini) | Off / 0-100 % | Off | offered above 60°; **5.8 names are `r.LensDistortion.Panini.D/.S/.ScreenFit`** [5.8 ✓] (the plan's `r.Upscale.Panini.*` is outdated) |
| Depth of field | Off / 0-100 % | 100 % | scales the pupil diameter A (UE 4.5) |
| Motion blur | 0-100 % | 100 % | scales the shutter angle (108° / 180°); 0 = off |
| Film grain | 0-100 % | 100 % | scales g0 and g_max (UE 4.7) |
| Exposure adaptation | Realistic / Fast / Very fast | Realistic | 1.5/0.7 · 3.0/2.0 · 6/6 EV/s |
| Exposure compensation | −1 … +1 EV | 0 | |
| Head bob | 0-100 % | 100 % | of the preset's translation scale (UE 4.8) |
| Body sway & breathing | 0-100 % | 100 % | |
| Mount shake | 0-100 % | — / 100 % | Headcam only |
| Lens distortion | 0-100 % | — / 100 % | scales k1, k2; Headcam only |
| Chromatic aberration | 0-100 % | 0 / 100 % | |
| Vignette | 0-100 % | 100 % | |
| Lamp glare (bloom) | 0-100 % | 100 % | |
| Image sharpening | 0-100 % | 0 / 100 % | the action-cam look |
| Get-down transition | Natural / Quick / Cut | Natural | 0.8-1.5 s / 0.4 s / cut |
| Reading zoom | On / Off | On | FOV 36° for phone/notebook, 30° for the slate glance (`m.zoom`); Off switches phone and notebook to Flat when the in-hand text would measure < 18 px (7.2, 14.2) and the glance to Card |
| Drink effects | 0-100 % | 100 % | HF-20: cosmetic, bounded, standing/walking only |
| Vision centre | −35 … +35 mm, *Calibrate…* | 0 mm | drill: a straight-in practice shot; after each of 5 alignments `y_vc ← y_vc − 0.7 · Δφ · 0.46 m` (Δφ = measured aim error in rad, 0.46 m = eye-to-tip distance, UE 4.2; ESTIMATE gain); stores the player's value (the career's per-character correction is set by the mentor's drill, HF-18) |
| Handedness | Right / Left | Right | mirrors stance and side offset |
| Replay look | Auto / Phone / Broadcast / Clean | Auto | 10.2 |

### 13.6 Controls (keyboard and mouse)

| Setting | Values | Default | Notes |
|---|---|---|---|
| Mouse DPI (starting value) | 400-6400 | 800 | replaced by calibration |
| Stroke sensitivity | 50-200 % | 100 % | scales `MetresPerCount` relative to the calibration |
| Stroke curve | Fine (G0 1.5: more hand travel per cm of cue, for soft touch) / Default (G0 2.0) / Short (G0 2.5: less hand travel) / Linear (3.5 flat) / Custom (G0 1-4, v_knee 0.2-1.0 m/s, G_max 3-8, v_sat 1-3 m/s) | Default | UE 5.4 curve; Default = T10 values (G0 2.0, v_knee 0.5 m/s, G_max 5.0, v_sat 2.0 m/s, tip ≤ 12 m/s); the named presets change G0 only |
| Calibrate stroke… | action | | 5.8 |
| Stroke direction | Pull back = backswing / Inverted | Pull back | |
| Stroke input | Mouse stroke / Pull & release | Mouse stroke | Pull & release: hold Stroke, pull the mouse back 0-12 cm to set the speed (mapped through the curve at the equivalent hand speed), release = a forward stroke with 0.15 s constant acceleration; still goes through the human layer; disclosed in match info |
| Commit | Hold / Toggle | Hold | |
| Fine aim, Settle, Glance, Stroke | Hold / Toggle each | Hold | |
| Look sensitivity (standing) | 0.1-5.0× | 1.0 (≈ 0.07°/count at 800 DPI, ESTIMATE) | |
| Aim sensitivity (down) | 0.1-5.0× | 1.0 (≈ 0.02°/count, ESTIMATE) | |
| Fine-aim factor | 0.05-0.5 | 0.2 | ARCH 6.5 |
| Invert look Y / Invert elevation | Off / On | Off | |
| Tip-offset step | 0.5 / 1 / 2 mm | 1 mm | |
| Raw input | read-only: "Per-report timestamps" / "Reconstructed (fallback)" | | ARCH 6.2.1 |
| Key bindings | per context, primary + secondary slot | table below | `SRbKeyBindButton`; conflicts within one context prompt "Swap?"; *Reset* per row/page |

Default keyboard/mouse bindings (extends ARCH 6.5; no key twice within a context — UX-T03):

| Action | Context | Primary | Secondary |
|---|---|---|---|
| Move | Walk | W A S D | arrow keys |
| Look / aim / placement point | all | mouse | — |
| Get down / stand up | Walk, Down | RMB | — |
| Stroke (hold) | Down | LMB | — |
| Commit (hold) | Down | Space | — |
| Elevation | Down | wheel | PgUp / PgDn |
| Tip offset | Down | arrow keys | numpad 8/4/6/2 |
| Fine aim (hold) | Down | Left Shift | — |
| Settle (hold) | Down | Left Ctrl | — |
| Interact / confirm | Walk, Placement, Decision | F | Enter |
| Call (hold) | Walk | C | — |
| Cycle call / options | Down, Decision | Q / E | — |
| Spot request | Placement | Q | — |
| Declare push-out / safety | Walk | X | — |
| Shot-clock extension | Walk, Down | T | — |
| Chalk | Walk | G | — |
| Wipe cue ball (hold) | Placement | V | — |
| Dialogue choices | Dialogue | 1-4 | — |
| Glance (hold) | Play | Tab | — |
| Pin glance card | Play | F1 | — |
| Phone / Notebook | Play (standing); again in Phone / Notebook to lower | P / N | — |
| Replay | Play | R | — |
| Photo mode | Play, Replay | F9 | — |
| Pause | Global | Esc (fixed) | — |
| Physics debug block | Global (dev builds) | F2 | — |

Reserved and never bindable: `Esc`, `F12` (Steam screenshot), `Shift+Tab` (Steam overlay), `Alt+Enter`, `Alt+F4`.

### 13.7 Controller

Default layout (Xbox names; PlayStation/Deck glyphs via Steam Input):

| Input | Standing | Down on the shot | Ball in hand | Decision / dialogue |
|---|---|---|---|---|
| Left stick | move | aim (azimuth) | move the placement point | — |
| Right stick | look | X fine aim, Y elevation; **with LT held: Y = stroke, X = steering** | look | — |
| LT (hold) | — | stroke mode | — | — |
| RT (≥ threshold) | — | commit | — | — |
| A | get down (cue ball in reach, nothing gazed) | — | place | confirm |
| B | back | stand up | pick up again | — |
| X | interact; with no target: chalk | — | wipe (hold) | — |
| Y | call (hold) | cycle the called pocket | — | — |
| LB | — | fine aim (hold) | — | — |
| RB | declare push-out / safety | settle (hold) | — | — |
| D-pad | up phone, down notebook | tip offset (1 mm steps, repeat) | left: spot request | left/right choose |
| View | glance (hold ≥ 0.2 s); tap = shot-clock extension | glance (hold); tap = shot-clock extension | glance | — |
| Menu | pause | pause | pause | pause |
| R3 | replay | replay | — | — |
| L3 (hold 0.6 s) | photo mode | photo mode | — | — |

Stick stroke (DERIVED starting values, TUNING): position control while LT is held, `x_c = L_pad · (d − d_0)` with the spring-centred deflection `d ∈ [−1, 1]` (Y axis of the stroke stick), `L_pad` = 0.15 m and the **address gap `d_0` = 0.25**: at rest (d = 0) the tip hovers 3.75 cm behind the ball, and contact (x_c = 0) needs an active forward push to d = +0.25. Speed from the same quadratic fit on `d(t)` at the crossing, then `v_tip = G_pad(|ḋ|) · L_pad · ḋ` with `G_pad` = 1 below 4 s⁻¹ rising linearly to 4 at 12 s⁻¹; a full flick from −1 to +1 in 0.1 s (ḋ = 20 s⁻¹) gives 12 m/s (clamped), a slow push at 2 s⁻¹ gives 0.3 m/s.

Why the gap (the naive `x_c = L_pad · d` is broken): a thumb that lets go of a pulled-back stick lets the spring snap it to centre in ~30-50 ms (ḋ ≈ 20-30 s⁻¹, VERIFY per pad in UX-M06); with contact at d = 0 that release would be a full-power stroke. With the gap the spring return ends at |d| ≤ 0.1 (inside the dead zone) and never reaches the ball. Guards: a crossing counts only while LT is held and the stick Y deflection is still ≥ +0.25 30 ms after it (a thumb slipping off does not follow through); releasing LT mid-stroke is a practice stroke that stops short (ARCH 6.2). Stick samples are timestamped by the platform input layer (UE 5.4); calibrated like the mouse (5.8), with `d` instead of counts.

| Setting | Values | Default |
|---|---|---|
| Stroke stick | Right / Left | Right |
| Stroke curve (stick) | Soft / Default / Firm / Custom | Default |
| Inner dead zone (look / stroke stick) | 0-25 % | 8 % / 5 % |
| Outer dead zone | 80-100 % | 95 % |
| Look / aim sensitivity | 0.1-5.0× | 1.0 |
| Look response curve | Linear / Exponential | Exponential (2.0) |
| Invert look Y | Off / On | Off |
| Commit trigger threshold | 30-90 % | 50 % |
| Vibration | 0-100 % | 70 % |
| Adaptive trigger resistance (DualSense) | Off / On | On (support path via Steam Input or a plugin: VERIFY) |
| Button glyphs | Auto / Xbox / PlayStation / Steam Deck / Generic | Auto |
| Button bindings | per context, rebindable | table above |

### 13.8 Audio

| Setting | Values | Default |
|---|---|---|
| Master · Music (jukebox, menu) · Table & balls · Ambience · Voices · Interface | 0-100 % each | 100 · 70 · 100 · 80 · 100 · 60 |
| Output | Headphones (binaural) / Speakers (stereo) / Surround | from first launch (Speakers if skipped) |
| Dynamic range | Wide / Normal / Night | Wide (Headphones), Normal (Speakers) |
| Mono audio | Off / On | Off |
| Heartbeat & breath tells | On / Off | On — description: "these sounds carry information (tremor, nerves); captions can replace them" |
| Streamer mode | Off / On | Off — replaces any track whose licence is not cleared for streaming/video (third-party jukebox music) with original tracks; the Gemini-generated jukebox music (roadmap) is only shipped after Google's terms for commercial use are checked |
| Mute when unfocused | On / Off | On |
| Voice language | English (German later, PO Q2) | English |

### 13.9 Gameplay

| Setting | Values | Default | Source |
|---|---|---|---|
| Difficulty | Pure / Real / Assisted / Relaxed / Custom | Real | HF 5.4 (`DifficultyPreset`) |
| └ Aim line | Off / Short / Long | per preset | `AimLineAssist` |
| └ Tip ring | Off / Faint / On | per preset | `TipRingAssist` |
| └ Straighten my stroke | 0-100 % (= `SteeringGain` 0.25 → 0) | per preset | UE 5.4 `G_lat` |
| └ Imperfections | Sim / Scaled / Low / Off (noise 1 / 0.6 / 0.3 / 0) | per preset | `ImperfectionSetting` |
| └ Pressure | On / Subtle / Off | per preset | HF-15 |
| └ Any tip contact is a shot | On / Off | per preset | `AnyTipContactIsShot` |
| └ Body fouls | Live / Ghosted | per preset | `BodyFoulDisplay` |
| └ Call mode | Explicit / Obvious shots | per preset | RUL 4.5 |
| └ Stroke report | Practice only / Always | per preset | HF 5.4 |
| Chores | Full / Brisk / Minimal; "Full on a venue's first visit" On/Off | Brisk; On | HF principle 5, Q5 |
| Diegetic-only information | Off / On | Off | HF principle 9; On = glance style Look (Card only while down), result line for fouls only, interaction prompts first time only; P2 elements stay |
| Glance style | Auto / Look / Card | Auto | 9.2 |
| Shot result line | Fouls only / Always | Fouls only | 9.1 |
| Mentor | Off / Misses only / Often | Misses only | 9.13 |
| Interaction prompts | On / First time only / Off | On | 3.5 |
| Hot-seat hand-over | Confirm / Immediate | Confirm | 9.11 |
| Auto-replay | Off / Great shots / Every miss | Off | 10.6 |
| Side bets (career) | On / League prize money only | On | HF Q6 `MoneyGames` (PO Q6) |
| Drinks (props) | Beer & spirits / Soft drinks | Beer & spirits | HF-20 cosmetic (PO Q7) |
| Units | Auto (by language) / Metric / Imperial | Auto | facts strip, speeds |
| Pause when the window loses focus | On / Off | On | |

### 13.10 Accessibility

| Setting | Values | Default | Notes |
|---|---|---|---|
| Text size | 100-200 % (25 % steps) | 100 % | all tokens (4.2); phone/notebook switch to flat above 125 % |
| High-contrast interface | Off / On | Off | opaque panels, `#FFFFFF` text, 4 px focus |
| Subtitles | On / Off | On | speech directed at the player |
| └ Speaker names | On / Off | On | name on speaker change (XAG 104) |
| └ Size | 100-200 % | 100 % (= 32 px) | |
| └ Background opacity | 0-100 % | 60 % | |
| └ Background colour | Black / Dark grey / Dark blue | Black | |
| Captions for sounds | Off / On | Off | "[tip slips]", "[heartbeat]", "[phone buzzes]", "[balls rattle in the return]", "[chalk on the slate]" |
| └ Direction arrows | On / Off | On | "‹ [phone buzzes]" when off-screen |
| Ball numbers | Standard / Enlarged (×1.35) / On all sides (6 instead of 2) | Standard | ball material parameters `NumberScale`, `NumberCount` (analytic numbers, UE-3; C9) |
| Pattern-coded balls | Off / On | Off | HF 5.4 accessibility list: a distinct fine pattern per colour on solids and stripes (`PatternCode`, C9), for players who cannot separate e.g. the 3 and 7 on green cloth |
| Ball number tags while glancing | Off / On | Off | 9.2 |
| Colour filter | Off / Protanopia / Deuteranopia / Tritanopia; strength 0-10 | Off | `FSlateRenderer::SetColorVisionDeficiencyType(type, severity, bCorrect = true, false)` [5.8 ✓]; affects the whole image, so it is a player choice, not a default |
| **Reduced motion** (master) | Off / On | Off | sets head bob 0, body sway 30 %, mount shake 0, motion blur 0, get-down Quick, menu stations Cut, glance Card, reading zoom as a cut (`m.zoom`), drink effects 0, replay Phone shake 0, UI slides → fades; each row stays editable ("set by Reduced motion") |
| Reduce flicker | Off / On | Off | neon flicker off, TV shows a still, lamp swing damped (HF-78) |
| Hold or toggle | per action | Hold | links to 13.6 |
| Stroke input | Mouse stroke / Pull & release | Mouse stroke | link to 13.6 |
| Stroke smoothing (tremor) | Off / Low / High | Off | disclosed low-pass on the stroke input before the physics (HF 5.4); leaderboards group by it |
| One-handed layouts | Off / Mouse only / Keyboard only | Off | Mouse only: stroke LMB, commit RMB while down, get down/stand up middle click, side buttons confirm/glance; Keyboard only: Pull & release on Space, aim with arrows, elevation PgUp/PgDn |
| Imperfections · Pressure · Chores | as 13.9 | as 13.9 | mirrored rows (the same setting shown on both pages) |
| Phone display · Notebook display | In hand / Flat | In hand | 7.6, 14.4 |
| Screen reader for menus | Later | — | UE Slate accessibility (VERIFY) |

Photosensitivity is not a setting: nothing in RAW BREAK flashes more than 3 times per second (UX-P10).

### 13.11 Language

| Setting | Values | Default |
|---|---|---|
| Interface language | Deutsch / English | from the OS culture |
| Subtitle language | Same as interface / Deutsch / English | Same |
| Voice language | English (German dub: PO Q2) | English |
| Number, date and money format | follows the interface language | — |

### 13.12 Other

Run first-launch setup again · Rebuild shader cache · Reset all settings (hold 1.2 s) · Credits & licences (font OFL texts, engine and plugin notices) · Privacy ("RAW BREAK sends no telemetry", if that stays true; if a crash reporter ships (realism plan 10), it asks before sending and this page says what a report contains) · Open save folder.

### 13.13 Registry data model

```cpp
// Source/RawBreak/Public/Settings/RbSettingsRegistry.h (proposed)
struct FRbSettingDef
{
    FName Id;                                   // "gfx.reflections"; loc keys Settings.<Id>.Label/.Desc/.Value.<V>
    ERbSettingsPage Page; FName Section;
    ERbSettingType Type;                        // Enum, Int, Float, Bool, Action, KeyBinding, Text
    TArray<FName> EnumValues;
    double Min = 0, Max = 1, Step = 0.05; FName UnitKey;
    bool bPresetDriven = false;
    TStaticArray<FRbSettingValue, 5> PresetValues; // Low..Cinematic, used when bPresetDriven
    FRbSettingValue Default;                    // non-preset rows (may depend on platform: Deck)
    ERbApply Apply = ERbApply::Live;            // Live, ConfirmRevert15s, StuttersBriefly, Restart
    TFunction<FRbSettingValue()> Get;
    TFunction<void(const FRbSettingValue&)> Set;
    TFunction<bool(FText& OutReason)> IsAvailable;
    uint8 GpuCostBars = 0; int32 VramMB = 0;    // ESTIMATE hints, replaced by measurements
    TArray<FName> SearchKeywords;
    FName MirrorOf;                             // rows shown on two pages
};
```

The settings screen, the first-launch steps, presets, persistence and the tests (UX-T01/T02) all iterate this one list; adding an option is adding a row. Expected size ≈ 110 rows.

---

## 14. Notebook

### 14.1 Purpose

The character's knowledge, written by hand (HF 5.1: knowledge is not a stat; facts are recorded on first experience). It is how the game tells you that the bar ball draws less (HF-42), that the Low Bridge table rolls toward the jukebox (HF-50; Big Lou already knows), that house cue #3 is bowed (HF-30/31), how Sonny plays once the money is down (HF 5.5), and what the mentor taught you. It never shows attribute numbers (HF Q3).

### 14.2 Physical model and readability (DERIVED)

* A6 spiral notebook with a pencil; an open spread is 210 × 148 mm; render target **2048 × 1448** per spread (mips on).
* Held at 0.35 m in both hands (`N` / D-pad down), reading zoom to V = 36°: at 16:9, H = 2 atan(tan 18° · 16/9) = 60.0°; the spread covers `(0.105 / 0.35) / tan 30°` = **0.52 of the screen width** → 998 px at 1920 → 0.487 screen px per RT px. For ≥ 18 px body height at 1080p, handwriting needs ≥ 37 RT px body height → `t.hand.notebook` 42 RT px em (Caveat body height ≈ 0.9 em, VERIFY in UX-T05), line pitch 52 px, ≈ 25 lines per page. With *Reading zoom Off* at V = 50° (16:9) the spread covers only `0.3 / tan 39.7°` = 0.36 of the width → 0.34 screen px per RT px → ≈ 13 px body height, so the notebook opens in the readable (flat) view instead (same live check as the phone, 7.2).
* Material: paper (roughness 0.8, slight subsurface) with the RT as a graphite mask (roughness 0.35, faint sheen at grazing angles — real pencil shines), page curl as a two-bone bend, spiral binding.

### 14.3 Content

```
 ┌──────────────────────────────────────┬──────────────────────────────────────┐
 │ TABLES                     Fri 12.10 │  Low Bridge — cues and balls         │
 │ ────────────────────────────         │  ─────────────────────────────       │
 │ Low Bridge, the coin-op table        │  #3 bowed ~2 mm. Roll it bow up.     │
 │  Rolls toward the jukebox. Slow      │                                      │
 │  balls curl left near the foot rail. │  Cue ball: the big heavy bar ball.   │
 │   ┌───────────────────┐              │  Won't draw like mine — hit lower,   │
 │   │            ←←  ●  │  (sketch)    │  harder.                  ✎ new      │
 │   └───────────────────┘              │                                      │
 │  Coaster under the left foot leg.    │                                      │
 └──────────────────────────────────────┴──────────────────────────────────────┘
   tabs (sticky flags):  Tables · Cues & gear · People · Lessons · Drills · Nights
```

| Tab | Entries (written automatically on first experience) |
|---|---|
| Tables | per venue and table: roll-off direction with a sketch arrow (HF-50), tight/loose pockets, dead rail spots (HF-52, V2), the cue ball type (HF-42), dirty balls (HF-41) |
| Cues & gear | house cues by number: bow and how to hold it (HF-30/31), weight; own cue, tip and chalk condition notes; the low-deflection shaft's disclosed trade-off (HF Q7) |
| People | opponents: tendencies in words ("jabs when the money's up", "plays safe on the 9", "sandbags until the money is down" — HF 5.5), where they play, results against them |
| Lessons | mentor lessons and techniques learned (bridges, jump/massé, break stance; HF 5.3), the vision-centre drill result (HF-18) |
| Drills | drill ladders with best results (HF 5.2), no XP numbers |
| Nights | one diary line per night: date, venue, matches, money (auto) |

Rules: one fact is written once; a changed fact is struck through and rewritten (e.g., a coaster moved, V2 slope drift). Sketches are generated from the table geometry (outline in the table's L:W = 2:1), balls with numbers and arrows, drawn with a deterministic hand-jitter so the same entry always looks the same. New entries: pencil sound, a small "✎ new" mark, optional caption "[You make a note]"; opening jumps to the newest unread entry.

### 14.4 States and inputs

`Closed → Opening (0.35 s) → Reading(spread) ⇄ Turning (0.4 s) → Closing`; *Readable view* (flat) toggles in place. Allowed while standing, in the opponent's turn and between racks; never while down.

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Open / close | `N` / `Esc` | D-pad down / B |
| Turn page | `A/D`, `←/→`, click the page edge | LS left/right |
| Tab | `Q/E`, click a flag | LB / RB |
| Readable view (flat, typeset in `t.body`, scrollable) | `Space` | Y |
| Scroll (readable view) | wheel, `W/S` | RS |

Flat mode is forced above 125 % text size and with *Reading zoom Off* when the in-hand text would measure < 18 px (same rules as the phone); *Notebook display: In hand / Flat* in 13.10.

### 14.5 Implementation

`ARbNotebook` (mesh + `URbDiegeticScreenComponent`), `SRbNotebookSpread` (two pages, flags, sketch painter via Slate `OnPaint` line/spline elements), `URbNotebookSubsystem` (entries in the career save, unread flags, knowledge events from the career/venue systems). The next spread is rendered into a second RT before a page turn so turning never shows an empty page. Mock provider for captures (`-RbUiScreen=Notebook.Tables`).

---

## 15. Save and load

### 15.1 What is saved

| File | Contents | When |
|---|---|---|
| Career slot (3 slots) | character (name, handedness), hidden attributes/habits/XP, money, notebook, phone threads, people met, venue seeds and states, equipment state (cue, tip chalk map, chalk cube), in-game night/time, stats, current location; a match in progress between shots (rules state, `FRbTableState`, seeds, noise history — deterministic) | autosave + manual |
| Settings | `GameUserSettings.ini` + RAW BREAK section | on leaving settings, on quit |
| Key bindings | Enhanced Input user settings save | on change |
| Replays | `.rbreplay` per saved clip | on *Save clip* |
| Photos | PNG/EXR | on capture |

HF principle 5: saving is never restricted. The pause menu offers *Save now* in the career (between shots; a request during a shot is carried out when the balls stop). Loading is from the main menu. Optional *Ironman* career (one save, no loading of older autosaves) is a PO question (Q5).

### 15.2 Slot screen

```
+------------------------------------------------------------------------------------------+
|  CAREER                                                                                  |
|  ┌──────────────────────────────┐ ┌──────────────────────────────┐ ┌───────────────────┐ |
|  │ [thumbnail 480×270]          │ │ [thumbnail]                  │ │                   │ |
|  │ Anna                         │ │ Ben                          │ │   + New career    │ |
|  │ Night 12 · Low Bridge        │ │ Night 3 · Low Bridge         │ │                   │ |
|  │ $184 · 9 h 40 min played     │ │ $22 · 1 h 05 min played      │ │                   │ |
|  │ Saved today, 23:47           │ │ Saved 26.09.2026, 21:10      │ │                   │ |
|  └──────────────────────────────┘ └──────────────────────────────┘ └───────────────────┘ |
|  [↵] Continue   [A] Older autosaves…   [Del] Delete (hold)   [Esc] Back                  |
+------------------------------------------------------------------------------------------+
```

Thumbnail: a 480 × 270 capture of the last view at save time (embedded PNG), taken with `FScreenshotRequest::RequestScreenshot(false)` (scene only, no UI) at the next frame and downscaled/encoded on a worker thread; only autosaves between shots take one. Dates and money in the UI culture's format. *Delete* = hold 1.2 s, the player's own save only. *New career*: name (≤ 20 characters; Deck floating keyboard), handedness, (Ironman, PO Q5) → the career intro.

### 15.3 Autosave

Triggers: end of each rack and match, entering/leaving a venue, accepting/declining a challenge, every 5 min of real time while standing and not in a shot, quitting. Rolling 3 autosaves per slot plus the main file. Silent in live play (async, off the game thread); menus show "Saved 23:47"; a failure is reported at the next pause/menu ("Couldn't save — is the disk full?") and retried.

### 15.4 Files and format

`Saved/SaveGames/Career_<n>.sav`, `Career_<n>_auto<k>.sav` (k = 0-2). `URbCareerSave : USaveGame` → `SaveGameToMemory` [5.8 ✓] → wrapped with a header `{Magic 'RBSV', SaveVersion, GameVersion, CoreRulesHash, UTC time, playtime s, CRC32}` → `ISaveGameSystem::SaveGameAsync(false, Slot, PlatformUserId, MakeShared<const TArray<uint8>>(Wrapped), Callback)` [5.8 ✓ `SaveGameSystem.h`]; loading with `LoadGameAsync`, unwrapping, CRC check, then `LoadGameFromMemory`. (`UGameplayStatics::AsyncSaveGameToSlot` takes a `USaveGame` object and writes its own format, so it cannot carry the header.) `SaveGameToMemory` runs on the game thread (small career model, < 1 ms budget, UX-T15); file I/O is async.

### 15.5 Failure handling and migration

CRC mismatch, truncated file or a newer `SaveVersion` → dialog "This save can't be read" with the newest readable autosave offered. One migration function per `SaveVersion` step, unit-tested with committed fixture saves (UX-T15). Replays store the rules version and hash (RUL 16 item 18) and still play after rule updates.

### 15.6 Steam Cloud

Auto-Cloud (UE 10): `SaveGames/*.sav` and `Replays/*.rbreplay`; photos go to the Steam screenshot library instead of the cloud.

---

## 16. Steam integration

### 16.1 Overlay

Overlay open (`IOnlineExternalUI` change delegate) → push the pause screen in play (not in the main menu); `Shift+Tab` never bound. Deck's quick-access menu behaves the same.

### 16.2 Achievements (proposal, V1)

Principles: skill, story and curiosity; nothing that needs more than ~40 h of grinding; nothing missable forever; hot-seat counts where marked; no achievement requires a specific difficulty except those named "Pure". IDs are stable; names/descriptions are localised in Steamworks (EN/DE).

| ID | Name (EN) | Trigger (from core data) | Hidden |
|---|---|---|---|
| `ACH_FIRST_RACK` | Quarters Well Spent | win a rack at the dive bar | no |
| `ACH_BREAK_AND_RUN_9` | Break and Run | 9-ball: win a rack without the opponent shooting | no |
| `ACH_GOLDEN_BREAK` | Golden Break | 9 legally pocketed on the break | no |
| `ACH_TEN_BALL_RUNOUT` | Called It All | 10-ball: break and run with every ball called | no |
| `ACH_STRAIGHT_50` | Fifty and Counting | 14.1: a run of 50 | no |
| `ACH_THREE_FOULS` | Hat Trick (the bad kind) | lose a rack on three fouls | yes |
| `ACH_SCRATCH_ON_8` | Wrong Pocket, Right Ball | lose 8-ball by scratching on the 8 | yes |
| `ACH_JUMP` | Airborne | make a legal jump shot | no |
| `ACH_MASSE` | Curve Ball | make a massé around a blocker | no |
| `ACH_BANK_CALLED` | Off the Rail | make a called bank shot | no |
| `ACH_ROLL_OFF` | Read the Table | make a slow shot whose line only works because of table roll (counterfactual "table" step, HF 3.9) | no |
| `ACH_FIRST_BET` | Put Up or Shut Up | win your first side bet | no |
| `ACH_HUSTLED` | Came Back for More | win a money game after trailing 0:2 | no |
| `ACH_BIG_NIGHT` | Rent Money | win $500 in one in-game night | no |
| `ACH_QUARTERS_100` | Coin-op Regular | insert 100 quarters | no |
| `ACH_STRAIGHT_CUE` | Found a Straight One | pick a house cue with the roll test that is actually straight | no |
| `ACH_VISION_CENTRE` | Eyes Right | complete the vision-centre drill | no |
| `ACH_LEAGUE_NIGHT` | Sign Here | play your first league match | no |
| `ACH_LEAGUE_CHAMP` | League Champion | win a league season | no |
| `ACH_TOURNAMENT` | Last One Standing | win a tournament | no |
| `ACH_NOTEBOOK_25` | Taking Notes | 25 notebook entries | no |
| `ACH_HOTSEAT_10` | House Rules | finish 10 hot-seat matches (hot-seat) | no |
| `ACH_CLIP` | Did You See That? | save a replay clip | no |
| `ACH_PHOTO` | Closing Time | take a photo in photo mode after hours | no |
| `ACH_PURE_MATCH` | Nothing But You | win a career match on Pure | no |

### 16.3 Stats

`STAT_RACKS_WON`, `STAT_BALLS_POCKETED`, `STAT_QUARTERS`, `STAT_CASH_WON` (cents), `STAT_NOTEBOOK_ENTRIES`, `STAT_HOTSEAT_MATCHES`, `STAT_LONGEST_RUN_141`; progress achievements are unlocked from stats (Steam shows the progress bar).

### 16.4 Rich presence and Timeline

* Rich presence (`steam_display` tokens, localised token file EN/DE uploaded to Steamworks): `#Menu` "In the menu", `#Practice` "Practising at {venue}", `#HotSeat` "{discipline} hot-seat, {score}", `#Career` "{discipline} at {venue}, {score}", `#Money` "Playing for money at {venue}".
* Timeline (`ISteamTimeline` [5.8 ✓, SDK 1.64]): `SetTimelineGameMode` Playing / Staging (between racks) / Menus / LoadingScreen; per match `StartGamePhase` + `SetGamePhaseID(matchId)` + `AddGamePhaseTag` (discipline, venue) + `SetGamePhaseAttribute("Score", "5:3")`, `EndGamePhase` at match over; `AddInstantaneousTimelineEvent` for golden break, break-and-run, jump/massé made, money-game win, three-foul loss, and every *Save clip* (clip priority *Featured* for golden break / break-and-run / saved clips, *Standard* otherwise); `SetTimelineTooltip("Rack 6 · 3:2", 0.f)`; event icons are Steam's built-in `steam_*` icons or custom icons uploaded in Steamworks (VERIFY the list). This is the "footage" feature: Steam Game Recording cuts the clips.

### 16.5 Screenshots

Photo-mode captures are added with `ISteamScreenshots::AddScreenshotToLibrary(file, thumbnailFile, width, height)` and `SetLocation(handle, venue)` [5.8 ✓, SDK 1.64]; the thumbnail is 200 px wide (Steam requirement per the header). `F12` stays Steam's own screenshot.

### 16.6 Achievements plumbing

Through the Online Subsystem Steam achievements/stats interfaces (UE 10) from `URbPlatformSubsystem`; unlock events come from a small `URbAchievementRules` table evaluated on core facts (shot record, rules outcome, career events), never from UI code. Without Steam (OSS Null, dev builds) everything is a logged no-op (UX-T16).

### 16.7 Steam Input and Steam Deck

Glyphs (3.5); `ShowFloatingGamepadTextInput` [5.8 ✓ SDK] for name fields; `ISteamUtils::IsSteamRunningOnSteamDeck()` [5.8 ✓, SDK 1.64] → Deck defaults: Low preset with the Deck profile of UE 9.4 (1280 × 800, 30-40 fps cap, Headcam distortion off), controller glyphs Deck, text sizes verified at 800p (4.2; UX-T05), phone/notebook in hand remain ≥ 12 px (7.2, 14.2).

---

## 17. Localisation

### 17.1 Scope

German and English at launch (UE 10), both first-class: every string exists in both before a change is merged. The product owner is a German speaker and reviews the German.

### 17.2 Rules

| # | Rule |
|---|---|
| L1 | No user-visible literal in code. All UI text comes from string tables; `FText::FromString` only for data (player names). A static check (`Tools/ui/check_strings.py`) fails the build on `LOCTEXT`/`NSLOCTEXT` or string literals in `UI/**` outside the allow-list. |
| L2 | String tables are CSV (`Key,SourceString,Comment`) under `Content/Localization/StringTables/RbUi_<Area>.csv`, registered with `LOCTABLE_FROMFILE_GAME` at module start (the GatherText source gatherer understands `LOCTABLE_FROMFILE_*` and reads the CSV [5.8 ✓ `GatherTextFromSourceCommandlet.cpp`]; the CSVs are staged as non-asset files, C2). Areas: `Menu`, `Settings`, `Live`, `Match`, `Phone`, `Notebook`, `Replay`, `Photo`, `Career`, `Rules`, `Help`, `Subtitles`, `Captions`. |
| L3 | Headless pipeline: `rbue.py loc` (new subcommand, C6) runs the GatherText commandlet with `Config/Localization/RawBreak_{Gather,Export,Import,Compile}.ini` → `.po` per culture under `Content/Localization/RawBreak/<culture>/` → compiled `.locres` (contract C2). German `.po` authored by Claude, reviewed by the PO. |
| L4 | Keys: `Area.Screen.Element[.State]`, e.g. `Settings.Graphics.Reflections.Label`, `…Desc`, `…Value.HitReflections`; subtitle keys = voice line ids. |
| L5 | Formatting only with named arguments (`{Player}`, `{Count}`) and ICU plurals (`{Count}\|plural(one=foul,other=fouls)`); never concatenation; no grammatical gender on player names (write around it: "Foul von Anna" → "Anna: Foul"). |
| L6 | Numbers, money, dates, times via `FText::AsNumber / AsCurrencyBase / AsDate / AsTime` in the UI culture. Money carries its currency with the amount: USD in US venues, EUR in the Kneipe; the Money app shows both if both exist. German formats: `184 $`, `1.250,00 €`, `12.10.2026`, `23:47`. |
| L7 | Length budget: German runs ≈ 30 % longer; every container fits +40 % (pseudo-locale test UX-T07); only player names may truncate (≤ 16/20 chars with an ellipsis). |
| L8 | Case: sentence case everywhere; uppercase only via the `t.label` style on ≤ 3-word labels (the string stays mixed case; `ß` is never uppercased by hand). |
| L9 | Subtitles: ≤ 2 lines, ≤ 40 characters per line in both languages (XAG 104: "avoid long lines of text (more than 40 characters)") (manual line breaks at sensible points), duration `clamp(0.5 s + chars / 17 cps, 1.5 s, 7 s)`; the German translation is shortened rather than overflowing (XAG 104). |
| L10 | Diegetic text follows UX-P11: rendered surfaces use the UI language; printed props use the venue's language and are never needed to play. |
| L11 | Terms follow the glossary (17.3); rule references use WPA numbers ("Rule 3.3" / "Regel 3.3"). |
| L12 | Every font covers every character of every string table (UX-T06). |
| L13 | Pseudo-locale for tests: accented, bracketed, +40 % length (`[Ŕéƒļéçţîöñš·····]`), generated by `Tools/ui/pseudo_loc.py` into a dev-only culture (ICU `en-XA` if available in UE's ICU data, VERIFY; else a dev build maps it onto a spare culture). UE's built-in debug culture `LEET` (non-shipping, [5.8 ✓ `LeetCulture.cpp`]) is used in addition to find text that bypasses localisation (it does not lengthen strings). |

### 17.3 Glossary (EN → DE; DBU usage, VERIFY with the PO)

| English | Deutsch | English | Deutsch |
|---|---|---|---|
| cue ball | Spielball (UI), „die Weiße" (voice) | object ball | Objektkugel / Kugel |
| ball in hand | Ball in Hand (Lageverbesserung) | behind the head string / kitchen | im Kopffeld / hinter der Kopflinie |
| head string / foot spot | Kopflinie / Fußpunkt | rail / cushion | Bande |
| pocket (corner / side) | Tasche (Ecktasche / Mitteltasche) | break | Break (Anstoß) |
| lag | Ausstoßen | rack (game) / rack (triangle) | Spiel / Dreieck |
| push-out | Push-Out | pass back | zurückgeben |
| safety | Safety (Sicherheitsstoß) | call the shot | ansagen (Ansage) |
| foul / three fouls in a row | Foul / drei Fouls in Folge | stalemate | Unentschieden |
| concede | aufgeben (Aufgabe) | race to 5 | Race to 5 („auf 5 Gewinnspiele") |
| miscue | Kiks | tip (leather) | Pomeranze / Leder |
| chalk | Kreide | cue / house cue | Queue / Hausqueue |
| bridge / mechanical bridge | Brücke / Queuehilfe (VERIFY) | english / draw / follow / stun | Effet / Rückläufer / Nachläufer / Stoppball |
| bank shot | Bande / Doublette | jump shot / massé | Sprungball / Massé |
| side bet / money game | Wette / Geldspiel | hustler | Hustler |

---

## 18. Accessibility summary (mapping)

| Guideline | Where met |
|---|---|
| XAG 101 text size ≥ 18 px body height at 1080p (PC), scalable to 200 %, sans-serif, ≤ 80 chars per line, sentence case | 4.2, 13.10, UX-T05, UX-T07 |
| XAG 102 contrast ≥ 4.5:1 | 4.1 (tokens and scrim opacities derived for a white worst case), UX-T04 |
| XAG 104 subtitles: on or choosable before any speech, speaker names, ≤ 2 lines, ≤ 40 chars, adjustable size and background opacity, captions for important sounds, preview in context | 5.3, 13.10, L9, UX-T18 |
| Input: full remapping, hold/toggle, one-handed layouts, controller-only and mouse-only play | 13.6-13.7, 13.10, UX-T03, UX-T09 |
| Motion and photosensitivity: every camera effect off, Reduced motion master, no flashing > 3 Hz | UX-P10, 13.5, 13.10 |
| Colour: never colour alone; ball numbers as text; enlarged/all-side numbers; colour filter | 4.1, 4.4, 13.10 |
| Cognitive: rules help, "How it works" page, decision cards explain the options with the rule | 9.7, 12.1 |
| Difficulty and fatigue: assists, imperfections, pressure, chores, stroke smoothing — no content locked (HF principle 9) | 13.9-13.10 |

---

## 19. UI performance and memory budget

| Item | Budget | Check |
|---|---|---|
| Flat UI GPU in live play (only pips/lines/subtitles visible) | ≤ 0.1 ms at 1440p | UX-T17 |
| Flat UI GPU in menus incl. background blur | ≤ 0.6 ms (game paused) | UX-T17 |
| Slate game thread in live play / menus | ≤ 0.1 ms / ≤ 0.5 ms | UX-T17 |
| Diegetic surface update | ≤ 0.2 ms GPU each; phone ≤ 30 Hz | UX-T17 |
| Render targets (phone 1080×2340, notebook 2048×1448 + mips ×2 spreads, slate 1536×1280 + mips, TV 1280×720) | ≈ 10 + 32 + 10 + 4 = **≈ 56 MB** (dive bar); a hall adds ≈ 4.7 MB per further table slate at 1024×853 (2.4) | VRAM estimate row |
| Fonts (5 families, 11 faces) | ≈ 3 MB | — |

Hidden widgets use `EVisibility::Collapsed` (no prepass, no paint); the live-info layer has no widgets when there is nothing to show.

---

## 20. Tests and acceptance

### 20.1 Automated (`RawBreak.Unit.UI.*`, `RawBreak.Functional.UI.*`, `RawBreak.Screenshot.UI.*`, `Tools/ui/*`)

| ID | Test | Pass condition |
|---|---|---|
| UX-T01 | Registry completeness | every row has EN + DE label/description/values, a default (or 5 preset values), a working Set→Get round trip, and survives save → reload of the ini |
| UX-T02 | Presets | picking each preset sets every preset-driven row to the table value (13.3); changing one row → "Custom (based on …)"; High reads back `r.Lumen.HardwareRayTracing.LightingMode` = 2 and `HitLighting.Allowed` = 1 (UE-8 rows win over GI group 2); changing only *Global illumination* afterwards leaves both values unchanged (RB rows re-applied after the group); Epic/Cinematic read back `r.MaterialQualityLevel` = 1 |
| UX-T03 | Bindings | no key twice within a context for defaults; remap → conflict detected → swap works; reset restores defaults; persisted through `UEnhancedInputUserSettings` **across a restart with the runtime-created actions** (remap → quit → relaunch → binding present; else the fallback store of 3.4 is used and tested); reserved keys refused |
| UX-T04 | Contrast | every text token/surface pair used by `FRbUiStyle` ≥ 4.5:1 on its surface and on the white worst case with its scrim opacity (4.1), which fails any coloured text on `scrim.card` and `signal.foul` text on `scrim.menu`; non-text marks (focus bar, pips with outline) ≥ 3:1; main-menu list vs the brightest station frame (sampled from a capture per station) |
| UX-T05 | Text size | body height (font metrics via `FSlateFontMeasure`) of every mixed-case text style and the cap height of `t.label.caps` ≥ 18 px at 1080p/100 % and ≥ 12 px at 1280×800; key labels inside glyphs ≥ 18 px (3.5); phone and notebook body heights by the projection of 7.2/14.2 |
| UX-T06 | Glyph coverage | `Tools/ui/check_glyphs.py` (fontTools): every character of every string table and `.po` exists in the font assigned to its style, incl. `ä ö ü Ä Ö Ü ß „ “ – … € $ ¢ ×` in Plex, Caveat and Kalam; also checks that the shipped Plex files are byte-identical to the fetched release (Reserved Font Name, 3.7) |
| UX-T07 | No clipping | for every screen and state: `SlatePrepass` desired size ≤ allotted size in EN, DE and pseudo-locale (+40 %), at 1920×1080, 1280×800 and 3840×2160, at 100 % and 200 % text size |
| UX-T08 | Mandatory information | scripted matches: foul count > 0 visible in Hidden, Glance, Pinned and Diegetic-only; two-foul text at count 2; decision card during every `AwaitDecision` of RUL 11.3; ball-in-hand line until the stroke; result line with rule reference after every enforced foul; 8-ball shows no pips (three-foul rule off) |
| UX-T09 | Navigation | every screen fully operable by keyboard only, controller only and mouse only (synthesised events); after every push/pop exactly one focused widget; Back always returns; no path quits without a confirm |
| UX-T10 | Pause and photo mode during playback | pause/photo mid-shot freezes the playback clock (UE-2's world-pause hold / `SetPaused`); resume continues without a time jump; the director commits only after the resumed playback finishes; the committed shot's `ResultHash` is unchanged |
| UX-T11 | Replay overlay | timeline ticks = the shot's events in order; speed/scrub/restart never change the end state (bitwise, ARCH A10); refused while live playback runs |
| UX-T12 | Photo mode | camera clamped to 3.0 m and outside geometry; 1×/2×/4× captures have the requested pixel size; no UI pixels in the image; file added to the gallery list |
| UX-T13 | First launch | runs once; each step skippable; a crash (simulated) resumes at the step; *Run setup again* works; the language switch re-renders |
| UX-T14 | Stroke calibration | synthetic samples: `D_c` = 2520 counts → `MetresPerCount` = 3.17460e-5 (±1e-10); `v_p` = 1.25 m/s → `s` = 1.6; clamps at 200/6400 DPI and `s` ∈ [0.5, 2]; stick calibration analogous |
| UX-T15 | Save/load | round trip bitwise for the career model through `SaveGameAsync` / `LoadGameAsync` with the header; CRC failure → backup offered; migration fixtures v1…vN load; async save never blocks the game thread > 1 ms |
| UX-T16 | Platform | with OSS Null: every achievement/stat/timeline call is a logged no-op, no crash; with Steam (AppID 480 dev): overlay opens the pause screen (manual check list) |
| UX-T17 | Performance | budgets of section 19 in `stat unit`/Insights captures of live play, settings open and phone raised |
| UX-T18 | Subtitles | every subtitle line in both languages ≤ 2 × 40 chars; durations by L9; background opacity 0/60/100 % rendered; preview visible in the settings |
| UX-T19 | HDR | toggling HDR needs no restart; the registry writes only `UGameUserSettings` HDR setters (a grep test forbids direct `r.HDR.*` writes in `UI/**` and `Settings/**`); after `ApplyNonResolutionSettings` the cvars `r.HDR.PaperWhite` / `r.HDR.UI.Luminance` read back the chosen values; UI stays at the configured UI luminance (measured in an EXR capture); SDR fallback when unsupported |
| UX-T20 | Reduced motion | switching it sets exactly the listed values (13.10) and restores the previous ones when switched off |
| UX-T21 | Diegetic readability | captures of phone (Messages), notebook (Tables) and slate glance (from the farthest standing position of the venue, Low Bridge ≈ 4.3 m) at 1080p: measured body height ≥ 18 px; with *Reading zoom Off* at V = 50° phone and notebook switch to Flat; the phone's screen luminance in the Low Bridge lies in 25-100 cd/m² and does not depend on the camera exposure (raise the phone, let AE settle, luminance unchanged ± 2 %) |
| UX-T22 | String hygiene | `check_strings.py`: no literals in `UI/**`, every key used exists in every culture, no unused keys |
| UX-T23 | Diegetic-only mode | glance uses Look while standing, Card while down; non-foul result lines off; mandatory elements unchanged |
| UX-T24 | Director flows | lag-winner choice, concede, stalemate proposal/answer, push-out → pass back, three-ball-rule hand-back, 8-ball break options, spot request and the hot-seat `ConfirmTurn` gate all reach the rules through the UI API (functional PIE) |
| UX-T25 | Several tables | a test level with 2 tables and 2 directors: live info, glance, decision card, replay and pause block show only the player's match; switching the player's table rebinds in one frame; each table's slate shows its own match |
| UX-T26 | DPI and navigation | at 1280×800, 1920×1080 and 3840×2160 a 100 px-wide test widget in layer 60 measures 100 × (height/1080) px (engine DPI rule only, no double scale); a phone widget in its render target is unaffected by the DPI rule; Tab never moves focus in any screen |
| UX-T27 | Stick stroke guard | synthetic stick traces: release from d = −1 (spring return to 0 in 40 ms) → no contact; active push −1 → +1 in 0.1 s → contact at d = +0.25 with 12 m/s (clamped); LT released mid-stroke → practice stroke, no contact |

### 20.2 Manual acceptance (product owner)

| ID | Check |
|---|---|
| UX-M01 | Main menu: the first seconds read as "real footage"; the list is readable on every station |
| UX-M02 | First launch in ≤ 3 min with calibration; skipping reaches the menu in ≤ 45 s |
| UX-M03 | A 9-ball hot-seat match is playable without ever opening the glance card except by choice; foul pips and decisions are always understood |
| UX-M04 | Calling shots by gesture: ≥ 18 of 20 calls right on the first try in a test session |
| UX-M05 | Phone and notebook readable in hand at the player's resolution; flat mode as fallback |
| UX-M06 | Stroke calibration makes soft and break shots both feel controllable; on a controller, letting go of a pulled-back stick never strikes the ball |
| UX-M07 | German texts correct and natural (PO review), no truncation |
| UX-M08 | Reduced motion removes any discomfort reported with defaults |

---

## 21. Work packages (proposal, same process as UE-x: worktree, reviewer, merge)

| WP | Title | Owned files (proposed) | Depends on | Acceptance |
|---|---|---|---|---|
| UI-1 | Foundation | `UI/Style/*`, `UI/Core/*` (`URbUiSubsystem`, `SRbScreen`, navigation, device tracker, glyphs), `UI/Widgets/*`, `Content/UI/{Fonts,Glyphs,Icons}/*`, `Content/Localization/StringTables/*`, `Tools/ui/*`, `Tests/RbUiFoundationTests.cpp` | C1, C2 | UX-T04-T07, T09, T22, T26 |
| UI-2 | Settings | `Settings/RbSettingsRegistry.*`, `UI/Screens/SRbSettings*`, new `[RawBreak.*]` rows in `Config/DefaultScalability.ini` and the `URbGameUserSettings` extension (both with the UE-8 owner's sign-off), bindings UI | UI-1, C7 (UE-8 merged), C8 | UX-T01-T03, T19, T20 |
| UI-3 | In-game layers | `UI/Live/*` (pips, lines, glance card, decision card, subtitles/captions, call/declare prompts), `UI/Screens/SRbPauseMenu`, `ARbScoreSlate` + `SRbScoreSlate` (drives the venue's chalkboard `Face` slot, C10; a plain board mesh in the test room until then), per-match binding of 2.4 | UI-1, C3-C5, C9 (ball accessibility params) | UX-T08, T10, T18, T23, T24, T25, T27 |
| UI-4 | Front end | `SRbFirstLaunch`, `SRbMainMenu`, `ARbMenuDirector`, `SRbMatchSetup`, `SRbSaveSlots`, stroke/vision calibration flows, `URbSaveSubsystem` | UI-1, UI-2, C4; dive-bar venue with the AfterHours state and menu cameras (C10) — until then the test room (6.5), so UI-4 is not blocked | UX-T13-T15 |
| UI-5 | Diegetic surfaces | `URbDiegeticScreenComponent`, `ARbPhone` + `SRbPhoneOS` + apps (mock data first), `ARbNotebook` + `SRbNotebookSpread` + `URbNotebookSubsystem` | UI-1, C1 | UX-T21 |
| UI-6 | Replay & photo UI | `SRbReplayBar`, `SRbBroadcastBug`, `SRbPhoneRecFrame`, Phone replay rig, `SRbPhotoPanel`, `ARbPhotoCamera` | UI-1, UE-7 replay | UX-T11, T12 |
| UI-7 | Platform | `URbPlatformSubsystem`, `URbAchievementRules`, rich presence tokens, timeline, screenshots | C1, C2 (OSS config) | UX-T16 |

Order: UI-1 → (UI-2, UI-3, UI-5 in parallel) → UI-4, UI-6 → UI-7. UI-3 supersedes the M1 `SRbInfoOverlay` text panel (kept as the F2 debug block).

---

## 22. Open questions for the product owner

1. **Main menu**: confirm concept C "Closing Time" (after-hours dive bar), with A "Parking lot" as a V2 career intro?
2. **Mentor and voices**: the venue spec proposes Eddie "Deacon" Marsh (retired road player, stool #10) as the mentor — confirm? Voice language: English voices with German subtitles, or a German dub later?
3. **Names**: this spec adopts the names PROPOSED in `venue-dive-bar.md` (Low Bridge Tavern, Terri, Deacon, Sonny, Big Lou, Nina); the pool hall and the broadcast league ({PoolHall}, {League}) still need fictional names.
4. **Subtitles default On** — acceptable for the "real footage" look? (XAG 104 accepts either on-by-default or a choice before any speech; step 1 of first launch already asks, so Off would also be compliant.)
5. **Ironman career** option (one save, no older autosaves) for players who want money games to hurt?
6. **Side bets toggle**: offer "League prize money only" as a player setting (HF Q6 switch), e.g. for players who dislike betting?
7. **Drinks toggle**: allow soft drinks instead of beer as props (cosmetic only either way)?
8. **Replay look defaults**: Phone look in bars and halls, Broadcast in the arena?
9. **Glance default**: Auto (the head turns to the slate) or always the screen card?
10. **Hot-seat hand-over confirm** on by default?
11. **Fonts**: IBM Plex (UI) + Caveat (notebook) + Kalam (slate) — or a different handwriting?
12. **Controller support at launch**: full (required for a Deck "Verified" rating) — confirm V1 scope.
13. **Foul pips in 8-ball**: WPA 8-ball has no three-foul rule (RUL 12.1), so this spec shows no foul count there (only the enforced foul's result line). Keep it that way, or show the count in every discipline as the decision's wording ("foul count always visible") could also mean?
14. **Reading zoom**: raising the phone or notebook narrows the Eyes FOV to 36° (the only way the in-hand text reaches the 18 px minimum at 0.25-0.35 m, 7.2/14.2). Keep it as the default (Off = flat phone/notebook), or make the flat presentation the default and the in-hand phone an option?

---

## 23. Sources

* Microsoft, Xbox Accessibility Guideline 101 (text display: 18 px at 1080p on PC, 26 px console, 200 % scaling, line width/spacing): https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/101
* Microsoft, Xbox Accessibility Guideline 104 (subtitles and captions: speaker names, ≤ 40 chars per line, ≤ 2 lines, background opacity 0-100 %, preview): https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/104
* Microsoft, Xbox Accessibility Guideline 102 (contrast): https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/102
* Epic Games, CommonUI input technical guide / overview (UE 5.8 docs): https://dev.epicgames.com/documentation/en-us/unreal-engine/commonui-input-technical-guide-for-unreal-engine
* NVIDIA DLSS (plugin downloads, DLSS 4.5 for UE 5.8): https://developer.nvidia.com/rtx/dlss
* AMD FSR Unreal Engine plugin (GPUOpen): https://gpuopen.com/learn/amd-fsr4-ue5-7-update/
* DBU rules in German (terminology: Kopffeld, Kopflinie, Fußpunkt, Ball in Hand, Ausstoßen, Aufgabe, Unentschieden): https://billardregel.de/pool-billard/allgemeine-regeln/ and https://billardregel.de/pool-billard/9-ball/
* Installed engine source, UE 5.8.3 (`C:/Program Files/Epic Games/UE_5.8/Engine`): `Config/BaseScalability.ini` (Lumen per scalability level), `Renderer/Private/Lumen/LumenHardwareRayTracingCommon.cpp` (LightingMode 0/1/2, HitLighting.Allowed), `Renderer/Private/Lumen/Lumen.cpp` (FinalGatherMethod), `Renderer/Private/PostProcess/LensDistortion.cpp` (Panini cvars), `Plugins/EnhancedInput/.../EnhancedInputUserSettings.h`, `UMG/Public/Slate/WidgetRenderer.h`, `Engine/Classes/GameFramework/GameUserSettings.h`, `SlateCore/Public/Fonts/CompositeFont.h`, `SlateCore/Public/Rendering/SlateRenderer.h`, `RenderCore/Public/ShaderPipelineCache.h`, `ThirdParty/Steamworks/Steamv164/sdk/public/steam/{isteamtimeline,isteaminput,isteamutils,isteamscreenshots}.h`; added in the review: `Engine/Private/UnrealEngine.cpp` (HDR sink rewriting `r.HDR.PaperWhite` / `r.HDR.UI.Luminance` from the user settings), `RenderCore/Private/RenderCore.cpp`, `SlateRHIRenderer/Private/SlateRHIRenderer.cpp` (HDR cvars, defaults 203 nits), `UMG/Private/Slate/WidgetRenderer.cpp` + `UMG/Private/Components/WidgetComponent.cpp` (gamma / sRGB target convention), `Slate/Private/Framework/Application/NavigationConfig.cpp` (`bTabNavigation` default true), `Engine/Classes/Engine/{UserInterfaceSettings,DPICustomScalingRule}.h`, `Engine/Public/SaveGameSystem.h` (`SaveGameAsync`), `Engine/Classes/Kismet/GameplayStatics.h`, `RHI/Public/RHIGlobals.h` (`GpuInfo.DedicatedVideoMemory`, driver version), `Editor/UnrealEd/Private/Commandlets/GatherTextFromSourceCommandlet.cpp` (`LOCTABLE_FROMFILE`), `Core/Private/Internationalization/Cultures/LeetCulture.cpp`, `Plugins/Online/OnlineSubsystemSteam`.
* Project code (merged branches, read-only): `Source/RawBreak/Public/Balls/RbShotPlaybackComponent.h` (world-pause hold, `SetPaused`), `Source/RawBreak/Public/Game/RbMatchDirector.h` (`SetCalledShot`, `SetShotKind`, `ChooseOption`, `CycleOption`, `Confirm`, URL options), `integ/m1:Config/DefaultScalability.ini` and `integ/m1:Source/RawBreak/Public/Settings/RbGameUserSettings.h` (UE-8 presets, `ERbQualityOption`).
* Font licences: IBM Plex `LICENSE.txt` (OFL 1.1, Reserved Font Name "Plex"): https://github.com/IBM/plex/blob/master/LICENSE.txt ; Kalam and Caveat `OFL.txt` (no Reserved Font Name): https://github.com/google/fonts/tree/main/ofl/kalam , https://github.com/google/fonts/tree/main/ofl/caveat
* Project specs: decisions, UE (ue5-realism-plan), HF (human-factors), RUL (rules), EQP (equipment), ARCH (ue-architecture), trailer plan.

---

## 24. Review log (adversarial review, 2026-09-28)

Scope: consistency with `Docs/decisions.md` (incl. the 2026-09-28 entries), the realism plan (camera 4.x, rendering 6.x, 9.4), HF (chores, assists, accessibility 5.4), rules (RUL 4-12, 16), equipment (table and cue-ball presets), the parallel dive-bar venue spec (layout, lights, EV, chalkboard, text pipeline), the trailer's "is it real" hook, and the code already merged on `main` / `integ/m1` (UE-2 playback, UE-6b director, UE-8 presets). Every engine name used by a fix was checked in the installed UE 5.8.3 source; external facts (XAG 101/104, font licences) were checked on the web. All fixes are in place in the sections named; nothing outside this file was changed.

| # | Severity | Finding | Fix (where) |
|---|---|---|---|
| R-01 | high | The graphics matrix mapped presets to `sg.*` levels 1/2/3/3/4 and told UE-8 that High must use GI level 3, but the merged UE-8 builds presets as level = `sg.*` 0..4 plus `[RawBreak.*]` rows at `ECVF_SetByGameOverride`, and already forces hit lighting from High up. "Material quality Epic = 3" would select the unwired Epic Quality Switch pins (the engine's effects group sets 0/2/1/1/1), *Material quality* and *Effects* were two rows for one engine group, and the screen percentages differed (50/62 vs 55/58.3). | 0.12, 13.3 rewritten against `integ/m1` (new rows marked **RB-new**, all as `[RawBreak.*]` sections), C7, UI-2, UX-T02. New open item for UE-8: engine `ReflectionQuality@0` sets `r.SSR.Quality=0`, so Low has no SSR at all (plan 9.4 wants SSR). |
| R-02 | high | Contrast: `scrim.card` (0.88) allowed `amber.400` and `signal.*` text, which reach only 3.3:1 and 1.96:1 over the white worst case; `scrim.menu` 0.94 gives `chalk.300` 4.43:1 (< 4.5). The red two-foul line failed. | 4.1 (opacities derived by script; `scrim.menu` 0.96; `scrim.card` text `chalk.100` only; colour as outlined non-text marks; new `signal.foul.text` #FF8A80), 9.1, UX-T04. |
| R-03 | high | HDR paper white / UI luminance / peak were written as cvars, but 5.8's HDR sink (`UnrealEngine.cpp`) rewrites `r.HDR.PaperWhite` and `r.HDR.UI.Luminance` from `UGameUserSettings`, so the menu values would be lost. | 5.4, 13.4 use `SetHDRPaperWhiteNits`, `SetHDRUILuminanceSeparate` + `SetHDRUILuminanceNits`, `SetMaximumHDRDisplayNits` + `SetHDRCalibrationUsed`; defaults 203 nits (BT.2408, engine default); UX-P5, UX-T19. |
| R-04 | high | `FWidgetRenderer(bUseGammaCorrection = true)` into an RGBA8 sRGB target encodes gamma twice (washed-out phone, slate, notebook); render-target mips were assumed automatic. | 3.6: linear rendering + `CreateTargetFor(..., false)` (the `UWidgetComponent` convention), explicit mip generation (VERIFY call). |
| R-05 | high | Screen-space UI would be scaled twice: the custom DPI rule (height/1080) and `FRbUiScale` (height/1080 × text scale). 5.8's `FNavigationConfig` has `bTabNavigation = true`, colliding with Tab = Glance / Skip setup. | 3.2 (`FRbUiScale` = text scale only; explicit `SDPIScaler` for flat RT widgets; `bTabNavigation = false`), C2 (`CustomScalingRuleClass`), 7.6, UX-T26. |
| R-06 | high | Phone screen 120-350 cd/m² with auto-brightness from the camera EV: in the Low Bridge (room EV100 3-5, mid-grey 1-4 cd/m²) that is a clipped slab, and because the raised phone fills 86 % of the metered frame the rule is a feedback loop. | 3.6: auto-brightness from the analytic illuminance at the phone (ambient-sensor curve, ESTIMATE) → 30-90 cd/m² in the bar, dark OS theme below 200 lux; 7.2; UX-T21. |
| R-07 | high | Controller stick stroke put contact at stick centre: letting go of a pulled-back, spring-centred stick (snap-back ≈ 20-30 s⁻¹) would fire a near-power stroke. | 13.7: address gap `d_0` = 0.25, follow-through guard, LT release = practice stroke; C8; UX-T27, UX-M06. |
| R-08 | high | Decisions of 2026-09-28 were not reflected: the UI assumed one table / one match per level, and balls leaving the table had no UI. | New 2.4 (per-match binding, other tables diegetic only, one slate per table, budget), decision 0.1, C4 (one director per table), C5, 9.1 result line, 9.12 chore, 19, UX-T25. |
| R-09 | medium | Main menu contradicted the venue spec: mirrored floor plan (from the door the bar is on the left and the table at the back right, 13 m away), "chairs up on the tables" (the bar has booths and stools), rain on a glass-block front, the EXIT sign at the wrong door, TV-1 on while AfterHours switched TVs off, invented sublevels instead of the venue's single generator, "Eyes-like f/2.8" station cameras (ARCH R-08 requires the game's camera model), concept A "reusing the exterior" that does not exist. | 6.1-6.5: V-frame station table (`RbCam_Menu_S0..S7`), Eyes camera model, AfterHours defined per light ID of VDB 4.2, TV-1 as the only lit TV, test-room fallback; C10. |
| R-10 | medium | "Is it real": logotype and list from the first frame, a wall clock showing the real local time in a closed night bar, an attract jump shot in a bar whose house rules ban jumps, bundled replays not bound to the venue's table. | 6.3 clean first 2.5 s and after-hours clock, 6.4 attract content and hash-bound bundled shots, 10.5 replay files carry table/physics hashes. |
| R-11 | medium | Interaction prompts were a fifth screen-space element not covered by UX-P1 / decision 1 and, as specified (any interactable after 0.6 s gaze), would appear constantly around the table. | 0.1, UX-P1, 2.3 layer 15, 3.5 contextual rule (only actions that are possible and meaningful now). |
| R-12 | medium | Chalkboard: the venue already makes H13 data-driven (rules from `RulesConfig`, queue from the director) through its own text pipeline, while the UI added a match section in a second pipeline with no layout; board space and viewing distance were unchecked. | 9.3: the whole face is one live RT owned by `ARbScoreSlate` with three sized zones (match section Kalam 96 RT px = 56 mm em), venue delivers mesh and `Face` slot (C10), shared Kalam file (3.7); 9.2 distance check from VDB 2.3 (≤ 4.3 m). |
| R-13 | medium | Rules: "calls in 10-ball and 14.1 always; 8-ball Explicit" is wrong (8-ball calls every shot after the break; `CallMode` applies to all three; bar rules `EightOnly`); the lag winner's choice (RUL 11.3 `LagWinnerChooses`, also after a 14.1 stalemate) had no decision card. | 9.4, 9.7, UX-T24. |
| R-14 | medium | With *Reading zoom Off* the readability derivation fails (phone 13.2 px, notebook ≈ 13 px at V = 50°); the animated FOV change was not covered by Reduced motion. | 7.2, 7.6, 13.5, 14.2, 14.4 (automatic Flat below 18 px), 4.5 `m.zoom`, 13.10, UX-T21, PO Q14. |
| R-15 | medium | Contracts out of date or missing: C3 is already implemented by UE-2 (`SetPaused`, world-pause hold); C4 invented `SetDeclaration` / `ChooseDecision` although `SetCalledShot`, `SetShotKind`, `ChooseOption`, `CycleOption`, `Confirm` exist; `?Pressure=` exists; no contract for OSS plugins in the `.uproject`, OSS config, staging of string-table CSVs, `CulturesToStage`, `rbue.py loc`, ball accessibility material params, the venue AfterHours state / slate face / menu cameras, the hot-seat `ConfirmTurn` gate, `RequestSpot`. | 3.3, 3.9 C1-C10, 8.1, 9.5, 9.11, 21. |
| R-16 | medium | Save: `AsyncSaveGameToSlot` takes a `USaveGame` and writes its own format, so the `RBSV` header + CRC could not be written; thumbnail capture method unspecified (a synchronous readback would hitch). | 15.2 (`FScreenshotRequest`, worker encode), 15.4 (`ISaveGameSystem::SaveGameAsync` / `LoadGameAsync` [5.8 ✓]), UX-T15. |
| R-17 | medium | Enhanced Input user settings store mappings with `AssociatedInputActionSoft` (soft path) while `URbInputSetup` creates transient actions: persistence across restarts is unproven. | 3.4 VERIFY note + fallback store, stable action names (C8), UX-T03. |
| R-18 | medium | Under Steam Input every pad reaches UE as XInput, so the device tracker could not tell PlayStation/Deck apart and `GetGlyphPNGForActionOrigin` had no origin to ask for. | 3.2: `GetInputTypeForHandle` + `GetActionOriginFromXboxOrigin` [5.8 ✓ SDK 1.64]. |
| R-19 | low | XAG 104 limits subtitle lines to 40 characters (the spec used 42 while citing 40). | L9, 18, UX-T18. |
| R-20 | low | XAG 101 requires the text **inside** glyphs to meet 18 px (the cap × 1.4 keycap gave a ≈ 14 px letter), and all-caps labels measure only their cap height (20 px uppercase → 14 px). | 3.5 (30 px glyphs), 4.2 (`t.label` sentence case, `t.label.caps` 26 px), 10.3, UX-T05. |
| R-21 | low | IBM Plex's OFL declares the Reserved Font Name "Plex": subsetting or editing requires renaming; fonts had no fetch/ledger path; the required capital ẞ is not needed and may be missing in Kalam/Caveat. | 3.7 (`fetch_fonts.py`, ledger rows, ship unmodified), 4.2, UX-T06. |
| R-22 | low | Money app ledger contradicted the venue prices ($1.50 per rack, $3.50 draft). | 7.3 mock-up. |
| R-23 | low | `IMC_Global` kept Phone, Notebook, Glance and Replay live inside replay, photo mode and menus; no key to lower a raised phone in its own context; the shot-clock extension on the controller only worked standing. | 3.4 (`IMC_Global` = Pause, new `IMC_Play`), 13.6 binding contexts, 13.7 View tap while down. |
| R-24 | low | Stroke-curve presets named "Soft" (higher gain) and "Firm" contradicted the touch-check offer in 5.8. | 5.8 6c, 13.6: *Fine* (G0 1.5) / *Default* / *Short* (G0 2.5), G0 only. |
| R-25 | low | Smaller gaps: GPU/VRAM/driver accessors left as VERIFY; a bundled PSO cache needs packaging first; privacy line vs a possible crash reporter; `SetTimelineTooltip` needs a time delta; pause mock-up lacked *Save now*; broadcast score bug on a translucent plate; cue-ball return placed on the end apron (the Low Bridge has it on the wall side, VDB A3); HF 5.4 "pattern-coded balls" missing; Streamer-mode wording vs the planned own/Gemini music. | 5.2, 5.5 (`GRHIGlobals.GpuInfo.DedicatedVideoMemory`, `GRHIAdapterUserDriverVersion` [5.8 ✓]), 13.12, 16.4, 12.1, 10.3, 9.12, 13.10, 13.8. |
| R-26 | info | **Verified and unchanged:** WPA references (Rule 3.3, 3.5, 3.13, 5.4; push-out and safety windows; 14.1 third foul −16 and re-break; stalemate +3 turns; shot clock 35 s / 10 s warning / one 25 s extension; 9/10-ball foul counters reset per rack, 14.1 never; 8-ball has no three-foul rule, so PO Q13 stands); stroke-calibration arithmetic (2520 counts → 3.17460e-5 m, s = 1.6, 10 m/s, clamps); phone and notebook projections at V = 36°; token contrast figures of 4.1; render-target memory (≈ 56 MB); `TABLE_7FT_BAR` 80 × 40 in and the oversized 60.325 mm / 221 g cue ball (decisions, EQP 6.2, VDB 0.3); camera defaults (V 50° / 58.7°, AE 1.5/0.7 and 3.0/2.0 EV/s, Panini cvars `r.LensDistortion.Panini.*`); HF assist presets and chore defaults; engine and SDK names `SInputKeySelector`, `SBackgroundBlur`, `FSlateVectorImageBrush`, `FTypeface::AppendFont`, `SetColorVisionDeficiencyType`, `NumPrecompilesRemaining`, `NumActivePrecacheRequests`, `r.MegaLights.Allowed`, `r.Lumen.Reflections.DownsampleFactor`, `r.Shadow.Virtual.ResolutionLodBiasLocal`, `ISteamTimeline` modes and clip priorities, `AddScreenshotToLibrary` (thumbnail 200 px), `IsSteamRunningOnSteamDeck`, `ShowFloatingGamepadTextInput`, `LOCTABLE_FROMFILE_GAME` gathering. | — |

Remaining open items (not blockers): the VERIFY list in the body (DLSS/FSR/XeSS versions and notices, Reflex mode API, runtime change of texture LOD-group limits, render-target mip call, Enhanced Input persistence with transient actions, Steam glyph licence note, DualSense without Steam, `en-XA` availability), the UE-8 follow-up on SSR at Low (R-01), the venue requests of C10, and PO questions 1-14.
