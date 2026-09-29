"""Venue master materials, instances and the venue MPC (M2-B; Docs/ue-architecture.md 18.8, venue-dive-bar 6).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_divebar_materials.py

Creates under /Game/Generated/Venues/DiveBar/Materials:
  * MPC_DB_Venue (Age 0.80, GrimeTint, DustColor, NicotineTint, StickyAmount, EmissiveScale; venue-dive-bar 6.2);
  * the Substrate masters M_DB_Opaque, M_DB_Coated, M_DB_Metal, M_DB_Vinyl, M_DB_Glass, M_DB_Emissive, M_DB_Floor, M_DB_Decal
    (6.1) whose non-trivial logic lives in Shaders/Private/Venue/RbVenueWear.ush (Custom nodes, include
    /RawBreak/Private/Venue/RbVenueWear.ush) - the wear stack of 6.2 (edge wear, cavity grime, kick grime, dust, touch polish,
    nicotine) from the per-asset wear mask T_DB_<Asset>_WM (UV1);
  * the MI_DB_* instances of 6.3 with the CC0 scan inputs fetched by Tools/art/fetch_cc0.py (Poly Haven / ambientCG only) and
    the procedural decal materials of the first decal set (7: burns, rings, scuffs, chalk; text-free in M2).
The table-family materials (M_Rb*, MI_RbCloth_BarGreen, MI_RbBall_DiveBar, MI_RbRail_BlackLaminate, the coin-op cabinet) are
M2-L's (rb_make_materials.py), never created here.
Idempotent. Owner: M2-B. STUB of the M2 architect step: TODO(M2-B).
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402


def main() -> None:
	rb.log("rb_make_divebar_materials: not implemented yet (M2-B)")


main()
