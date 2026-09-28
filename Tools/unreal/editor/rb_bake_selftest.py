"""Pipeline check of the C++ bake path from editor Python (Docs/ue-architecture.md 9.6):
FDynamicMesh3 -> URbAssetBakeLibrary::WriteStaticMesh -> saved UStaticMesh asset, called headless.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_bake_selftest.py
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

ASSET = "/Game/Dev/PipelineProof/SM_BakeSelfTest"


def main() -> None:
	rb.ensure_dir("/Game/Dev/PipelineProof")
	if not unreal.RbAssetBakeLibrary.bake_self_test(ASSET):
		rb.fail("bake_self_test returned False")
	mesh = unreal.load_asset(ASSET)
	if mesh is None or not isinstance(mesh, unreal.StaticMesh):
		rb.fail(f"{ASSET} did not load as a StaticMesh")
	bounds = mesh.get_bounds()
	rb.log(f"baked {ASSET}: box extent {bounds.box_extent}")
	if abs(bounds.box_extent.x - 50.0) > 0.01:
		rb.fail(f"unexpected extent {bounds.box_extent}")
	nanite = unreal.load_asset(ASSET + "_Nanite")
	if nanite is None or not isinstance(nanite, unreal.StaticMesh):
		rb.fail(f"{ASSET}_Nanite did not load as a StaticMesh")
	rb.log("bake self-test OK (plain + Nanite with a full-detail fallback)")


main()
