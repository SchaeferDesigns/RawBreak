# Launch trailer plan

Owner: Claude (creative direction, shot list, capture, edit, sound). Produced at the end of development,
but the capture tech below must be designed in from the start.

**Gate:** trailer production starts only after the product owner has played the game, is happy with it,
and explicitly green-lights the trailer. Real-world reference footage is undecided — the trailer must work without it.

## Creative direction (first draft)

The viral hook is the same one that made *Bodycam* and *Unrecord* explode: **"Wait — is this real footage?"**

1. **Cold open (0–3 s):** shaky phone-style first-person footage in a dim American dive bar. Chalk squeaks,
   someone off-screen says "watch this". No logo, no UI, no music. It must pass as a real clip in the first
   two seconds of a TikTok/Shorts scroll.
2. **The shot (3–8 s):** an impossible-but-real shot (jump over a blocker, or a massé around one) — lands.
   Hard cut to black on the pocket drop sound.
3. **Beat drop — the break (8–15 s):** extreme slow motion at ball level: cue tip compresses, the rack
   explodes, balls deform the light in their reflections. Real physics, re-rendered at 1000+ fps.
4. **Montage on the beat (15–60 s):** rattle-and-reject in a tight pocket, a swerve curve, bank and kick
   shots, hill-hill 9-ball tension in a smoky pool hall, the German Kneipe, the arena spotlight walk-in.
   Every cut on a transient.
5. **Reveal:** "This is not a video." → **RAW BREAK** → "Every shot simulated. Every rule real." →
   *Wishlist on Steam*.

Deliverables: 60–90 s Steam trailer (16:9), 30 s cut-down, 15 s vertical (9:16) versions for
TikTok/Reels/Shorts. Optional, only if real reference footage gets filmed: a "real vs. game" side-by-side clip.

## Tech requirements to build in NOW

| Requirement | Why |
|---|---|
| **Deterministic replays**: a shot is stored as (table state + strike + parameters) and re-simulated bit-identically | Re-render any shot later from any camera, at any resolution, in any slow-motion rate |
| **Render time decoupled from physics time** (`StateAt(ball, t)` playback) | Slow motion without re-simulating; exact frame-accurate sync with audio events |
| **Stroke/body recording** (player input + animation) | First-person takes can be replayed and rendered offline with the path tracer |
| **Cinematic camera kit**: handheld phone rig (shake, AE/focus hunting, phone lens), macro ball-level cam, dolly/crane, broadcast cams, Sequencer integration | The "is it real" look and hero angles |
| **Offline quality path**: Movie Render Queue + path tracer, EXR/ProRes output | Trailer frames far beyond real-time quality |
| **Audio from physics events** (impact speed, materials) as separate stems | Frame-exact SFX, clean mix |
| **Free trailer camera / photo mode** in the game | Also a marketing feature for players (shareable clips) |

## Post pipeline

Movie Render Queue (EXR/ProRes) → ffmpeg scripts (cuts on beat markers, grade/LUT, titles, loudness
normalisation, per-platform encodes). Music must be licence-clean for commercial use (commissioned or a
properly licensed track) — decision closer to launch.
