"""Physical materials of the off-table ball hand-off (M2-E; Docs/ue-architecture.md 18.6.1, venue-dive-bar 13.5).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_physics.py

Creates under /Game/Generated/Physics (paths in RbAssetPaths): PM_RbBall (phenolic resin), PM_RbSurface_Vct (vinyl tile on
concrete: friction 0.5, restitution 0.35, ESTIMATE), PM_RbSurface_Rubber (0.8 / 0.15), PM_RbSurface_Wood, PM_RbSurface_Concrete,
each with its surface type (Config/DefaultEngine.ini PhysicalSurfaces RbBall / RbVct / RbRubber / RbWood / RbConcrete / RbCloth
= SurfaceType1..6, RbAssetPaths::Surface) so the loose-ball impacts carry the surface to the audio (AU-25). Values are tuned against real bounce heights of a dropped pool ball (ESTIMATE
until measured). Idempotent. Owner: M2-E. STUB of the M2 architect step: TODO(M2-E).
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402


def main() -> None:
	rb.log("rb_make_physics: not implemented yet (M2-E)")


main()
