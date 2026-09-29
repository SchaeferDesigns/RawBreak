"""Generated audio assets (M2-C; Docs/ue-architecture.md 18.5, Docs/specs/audio.md 7-8).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_audio.py

Creates under /Game/Generated/Audio (RbAssetPaths::AudioDir):
  * the submix graph of audio.md 7.1 (SUBM_RB_Master with the limiter, World / Table / Foley / Ambience / Crowd / Voice /
    Jukebox, Reverb_<Venue>, UI, MenuMusic) - the volume sliders drive their output volumes (FRbAudioVolumes);
  * attenuation (ATT_RB_TableVoice: natural sound, 1 m reference, no Doppler) and concurrency (CON_RB_TableVoice: never stolen)
    settings of the table voices;
  * the convolution reverb presets CRV_RB_<Venue> with impulse responses synthesised by Tools/audio/ir_synth.py from the room
    geometry (dive bar RT60 0.8 / 0.6 / 0.5 s; the test room its own);
  * no library samples in M2: every M2 sound is synthesised at runtime (RawBreakAudioDsp) or rendered offline by
    Tools/audio into generated waves (room-tone beds, footstep banks) - no external audio downloads.
Idempotent. Owner: M2-C. STUB of the M2 architect step: TODO(M2-C).
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402


def main() -> None:
	rb.log("rb_make_audio: not implemented yet (M2-C)")


main()
