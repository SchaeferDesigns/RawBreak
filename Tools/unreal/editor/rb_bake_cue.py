"""Bakes the cue meshes (UE-4; Docs/ue-architecture.md 5.6, ue5-realism-plan 5.5 / 6.4):
RbCueMeshBuilder lathe of rb::GetCueSpec(preset) with the default rb::human::CueBodyState (taper r_t 6.5 mm -> r_b 15.9 mm,
ferrule / joint at the body's lengths) -> URbAssetBakeLibrary.bake_cue_mesh -> /Game/Generated/Cues/SM_Cue_<Preset>
(Nanite off, no collision, M_RbCue when UE-3 generated it, else no material: ARbCue assigns one at runtime).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_bake_cue.py [-- <Preset> ...]     (default: every preset)

Idempotent: overwrites the assets. Checks every asset: loads as a StaticMesh, bounds = the cue (length along X from the tip
dome apex to the butt, radius r(L) around the axis), two UV channels, Nanite off.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

CUE_DIR = "/Game/Generated/Cues"  # RbAssetPaths::CueMeshDir

# rb::GetCueSpec (rb/Equipment/Cue.h): length [m], tip dome radius [m], tip diameter [m].
PRESETS = {
	"Playing19oz": (1.4732, 0.0106, 0.01275),
	"Break21oz": (1.4732, 0.0106, 0.0135),
	"Jump9oz": (1.016, 0.0106, 0.01375),
	"House19oz": (1.4478, 0.0106, 0.0125),
}
BUTT_RADIUS_CM = 1.59  # rb::human::CueBodyState::ButtRadius
TIP_RADIUS_CM = 0.65
FILLET_CM = 0.3        # FRbCueMeshOptions::BumperFilletCm


def _enum_value(name: str):
	"""unreal.RbCuePreset member of an ERbCuePreset name (Python spells it PLAYING19OZ / PLAYING19_OZ ...)."""
	key = name.upper()
	for attr in dir(unreal.RbCuePreset):
		if attr.replace("_", "") == key:
			return getattr(unreal.RbCuePreset, attr)
	rb.fail(f"unreal.RbCuePreset has no member for {name}")
	return None


def _presets() -> list[str]:
	names = [a for a in sys.argv[1:] if a in PRESETS]
	return names or list(PRESETS)


def _uv_channels(mesh: unreal.StaticMesh) -> int:
	"""LOD0 UV channel count, 0 if not exposed to Python (then RawBreak.Unit.Cue.Mesh_Baked checks it)."""
	try:
		return int(unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_num_uv_channels(mesh, 0))
	except AttributeError:
		return 0


def _check(name: str) -> None:
	length_m, dome_m, width_m = PRESETS[name]
	path = f"{CUE_DIR}/SM_Cue_{name}"
	mesh = unreal.load_asset(path)
	if mesh is None or not isinstance(mesh, unreal.StaticMesh):
		rb.fail(f"{path} did not load as a StaticMesh")
	box = mesh.get_bounding_box()
	dome = 100.0 * dome_m
	rim = math.sqrt(dome * dome - (50.0 * width_m) ** 2)
	length = 100.0 * length_m
	taper_end = TIP_RADIUS_CM + (BUTT_RADIUS_CM - TIP_RADIUS_CM) * (length - FILLET_CM) / length
	expected = {
		"max.x": (box.max.x, dome),
		"min.x": (box.min.x, rim - length),
		"max.y": (box.max.y, taper_end),
		"max.z": (box.max.z, taper_end),
	}
	for key, (value, want) in expected.items():
		if abs(value - want) > 2e-3:
			rb.fail(f"{path}: bounds {key} = {value:.4f} cm, expected {want:.4f} cm")
	channels = _uv_channels(mesh)
	if channels and channels != 2:
		rb.fail(f"{path}: {channels} UV channels, expected 2 (UV0 axis / azimuth, UV1 section)")
	if mesh.get_editor_property("nanite_settings").enabled:
		rb.fail(f"{path}: Nanite must be off")
	rb.log(f"cue mesh OK: {path}, length {box.max.x - box.min.x:.3f} cm, radius {box.max.y:.4f} cm")


def main() -> None:
	rb.ensure_dir(CUE_DIR)
	for name in _presets():
		if not unreal.RbAssetBakeLibrary.bake_cue_mesh(_enum_value(name)):
			rb.fail(f"bake_cue_mesh({name}) returned False")
		_check(name)
	rb.log("cue bake OK")


main()
