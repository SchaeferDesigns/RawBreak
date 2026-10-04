"""The body rig's content (M3-H; Docs/ue-architecture.md 19.4).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_import_body.py

To be implemented by M3-H:
  * copy the Epic template mannequin of UE 5.8 UNCHANGED from
    <Engine>/Templates/TemplateResources/High/Characters/Content/Mannequins (SKM_Manny_Simple, SKM_Quinn_Simple, SK_Mannequin and
    the materials / textures they reference) to /Game/Characters/Mannequins (RbAssetPaths::MannequinDir; the template feature pack's
    own path, so the assets' internal references stay valid) - ledger rows source `epic`, licence `UE-EULA`;
  * build the RAW BREAK body content under /Game/Generated/Body (RbAssetPaths::BodyDir): body materials (skin with SSS on the hands,
    the plain dark long-sleeve shirt, jeans, shoes) with region masks from the bone weights, cuff meshes from Tools/blender/body if
    the mask edge reads as paint.

Run by rb_make_all.py before rb_make_player.py. Plan-step stub: logs and does nothing (TODO(M3-H)). Owner: M3-H.
"""

from __future__ import annotations

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402


def main() -> None:
	rb.log("rb_import_body: not implemented yet (M3-H, Docs/ue-architecture.md 19.4)")


main()
