"""Bakes the ball mesh (UE-2; Docs/ue-architecture.md 5.4, ue5-realism-plan 6.7 / T20):
RbBallMeshBuilder unit sphere (128 segments x 64 rings, analytic normals + tangents, UV0 lat/long, UV1 octahedral)
-> URbAssetBakeLibrary.bake_ball_mesh -> /Game/Generated/Balls/SM_RbBall (Nanite off, no collision).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_bake_ball.py [-- <segments> <rings>]

Idempotent: overwrites the asset. ARbBallSet scales the unit sphere by each ball's own radius [cm].
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

ASSET = "/Game/Generated/Balls/SM_RbBall"  # RbAssetPaths::BallMesh


def _args() -> tuple[int, int]:
	numbers = [a for a in sys.argv[1:] if a.lstrip("-").isdigit()]
	segments = int(numbers[0]) if len(numbers) > 0 else 128
	rings = int(numbers[1]) if len(numbers) > 1 else 64
	return segments, rings


def _triangles(mesh: unreal.StaticMesh) -> int:
	"""LOD0 triangle count, 0 if this engine version does not expose it to Python (then the C++ test checks it)."""
	try:
		return int(mesh.get_num_triangles(0))
	except AttributeError:
		rb.log("StaticMesh.get_num_triangles not exposed - triangle count left to RawBreak.Unit.Balls.Mesh_T20_Sagitta")
		return 0


def main() -> None:
	segments, rings = _args()
	rb.ensure_dir("/Game/Generated/Balls")
	if not unreal.RbAssetBakeLibrary.bake_ball_mesh(segments, rings):
		rb.fail("bake_ball_mesh returned False")
	mesh = unreal.load_asset(ASSET)
	if mesh is None or not isinstance(mesh, unreal.StaticMesh):
		rb.fail(f"{ASSET} did not load as a StaticMesh")

	extent = mesh.get_bounds().box_extent
	if max(abs(extent.x - 1.0), abs(extent.y - 1.0), abs(extent.z - 1.0)) > 1e-4:
		rb.fail(f"{ASSET}: unit sphere expected, bounds extent {extent}")
	segments = max(8, (segments + 3) // 4 * 4)  # RbBallMeshBuilder::Sanitize
	rings = max(4, (rings + 1) // 2 * 2)
	expected = 2 * segments * (rings - 1)
	triangles = _triangles(mesh)
	if triangles and triangles != expected:
		rb.fail(f"{ASSET}: {triangles} triangles, expected {expected}")
	nanite = mesh.get_editor_property("nanite_settings")
	if nanite.enabled:
		rb.fail(f"{ASSET}: Nanite must be off (128-segment LOD0, plan 6.7)")
	rb.log(f"ball mesh OK: {ASSET}, {segments} x {rings}, {triangles} triangles, extent {extent}")


main()
