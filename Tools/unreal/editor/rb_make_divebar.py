"""Builds the dive-bar level /Game/Generated/Maps/L_DiveBar (M2-A; Docs/ue-architecture.md 18.8, venue-dive-bar 13.8).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_divebar.py

From Art/DiveBar/layout.json + lights.json (venue frame V = UE world axes, metres x 100):
  * the level (rb_common.new_level, non-partitioned) + lighting sublevels L_DiveBar_Light_{Open,LightsUp} (AfterHours later);
  * the shell and every element of venue-dive-bar 2.3: the imported mesh when rb_import_divebar.py produced it, else a greybox
    box at true size (the level is playable at every stage); collision profiles RbVenueBlock / RbVenueProp, kick plates, the
    RbBallReturn volume behind the bar, floor physical material RbAssetPaths::PhysMatVct;
  * ARbTable at (1375.9, 542.7, 0) cm, yaw 0: SevenFootBar, OldBarOversizedCue, TableIndex 0, tag RbPlayerTable,
    bUseVenueCondition with the searched VenueSeed (roll-off toward the jukebox, 3.2), LampUndersideHeight 0.86 m, lamp
    footprint x [-0.650, 0.650] y [-0.210, 0.170] m;
  * ARbVenueInfo (Venue DiveBar, VenueSeed, Age 0.80), PlayerStart at the head end (1220, 543) facing +X, World Settings GameMode
    ARbGameMode, post-process volume (Eyes defaults), height fog + local fog volume (4.7);
  * lights of lights.json with the flags of 4.1-4.2 (volumetric shadow rule, neon proxies SpecularScale 0 and flux / pi);
  * ARbLookDevCamera actors tagged RbCam_DB_V01..V12, RbCam_DB_TH1..TH7, RbCam_Menu_S0..S7;
  * the validator ARbVenueInfo::ValidateVenueLevel (VDB-T10, VDB-T11, cameras, tables) - fails the run on FAIL lines.
Idempotent (recreates the level). Owner: M2-A. STUB of the M2 architect step: TODO(M2-A).
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

MAP = "/Game/Generated/Maps/L_DiveBar"  # RbAssetPaths::DiveBarMap


def main() -> None:
	rb.log(f"rb_make_divebar: not implemented yet (M2-A) - {MAP} not generated")


main()
