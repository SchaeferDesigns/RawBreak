"""Physical materials of the off-table ball hand-off (M2-E; Docs/ue-architecture.md 18.6.1, venue-dive-bar 13.5).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_physics.py

Creates under /Game/Generated/Physics (paths in RbAssetPaths): PM_RbBall (phenolic resin), PM_RbSurface_Vct (vinyl tile on
concrete: friction 0.5, restitution 0.35, ESTIMATE), PM_RbSurface_Rubber (0.8 / 0.15), PM_RbSurface_Wood, PM_RbSurface_Concrete,
each with its surface type (Config/DefaultEngine.ini PhysicalSurfaces RbBall / RbVct / RbRubber / RbWood / RbConcrete / RbCloth
= SurfaceType1..6, RbAssetPaths::Surface) so the loose-ball impacts carry the surface to the audio (AU-25). Values are tuned against
real bounce heights of a dropped pool ball (ESTIMATE until measured). Idempotent. Owner: M2-E.

Contact model: the ball's material MULTIPLIES (friction 1.0, restitution 0.975, combine mode Multiply, which wins over the default
Average in Chaos: the higher ECombineMode of the pair is used), so the SURFACE's material states the ball-on-surface pair directly
(VCT: a ball dropped from the bed height bounces to 0.35^2 = 12 % of it; two loose balls: e = 0.95, near the phenolic ball-ball
value). Sleep thresholds: the engine defaults; ARbLooseBall adds rolling resistance and its own rest rule.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

DIR = "/Game/Generated/Physics"  # RbAssetPaths::PhysMatDir

# name, friction, restitution, surface type (SurfaceType1..6 = RbBall, RbVct, RbRubber, RbWood, RbConcrete, RbCloth), multiplies,
# density [g/cm^3] (informational: the loose ball's mass is set from the ball set), note
MATERIALS = [
	("PM_RbBall", 1.00, 0.975, 1, True, 1.75, "phenolic resin ball; multiplies the surface's values (ball-ball e 0.95)"),
	("PM_RbSurface_Vct", 0.50, 0.35, 2, False, 1.9, "vinyl composition tile on concrete (venue-dive-bar 13.5, ESTIMATE)"),
	("PM_RbSurface_Rubber", 0.80, 0.15, 3, False, 1.2, "rubber floor mat (venue-dive-bar 13.5, ESTIMATE)"),
	("PM_RbSurface_Wood", 0.45, 0.45, 4, False, 0.7, "varnished wood: ledges, bar die, booth plinths (ESTIMATE)"),
	("PM_RbSurface_Concrete", 0.55, 0.40, 5, False, 2.4, "sealed concrete: test-room floor, block walls (ESTIMATE)"),
]


def _surface(index: int):
	return getattr(unreal.PhysicalSurface, f"SURFACE_TYPE{index}")


def _make(name: str, friction: float, restitution: float, surface: int, multiplies: bool, density: float, note: str) -> None:
	path = f"{DIR}/{name}"
	material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
	if material is not None and not isinstance(material, unreal.PhysicalMaterial):
		rb.delete_asset_if_exists(path)
		material = None
	if material is None:
		factory = unreal.PhysicalMaterialFactoryNew()
		try:
			factory.set_editor_property("physical_material_class", unreal.PhysicalMaterial)
		except Exception:  # noqa: BLE001 - older factories have no class property
			pass
		material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DIR, unreal.PhysicalMaterial, factory)
		if material is None:
			rb.fail(f"could not create {path}")
	material.set_editor_property("friction", friction)
	material.set_editor_property("static_friction", 0.0)
	material.set_editor_property("restitution", restitution)
	material.set_editor_property("density", density)
	material.set_editor_property("surface_type", _surface(surface))
	material.set_editor_property("override_friction_combine_mode", multiplies)
	material.set_editor_property("override_restitution_combine_mode", multiplies)
	if multiplies:
		material.set_editor_property("friction_combine_mode", unreal.FrictionCombineMode.MULTIPLY)
		material.set_editor_property("restitution_combine_mode", unreal.FrictionCombineMode.MULTIPLY)
	unreal.EditorAssetLibrary.save_loaded_asset(material)

	# Read back (A2 metrics): what C++ and the level generators rely on.
	check = unreal.load_asset(path)
	got = (round(check.get_editor_property("friction"), 4), round(check.get_editor_property("restitution"), 4), check.get_editor_property("surface_type"))
	if got[0] != round(friction, 4) or got[1] != round(restitution, 4) or got[2] != _surface(surface):
		rb.fail(f"{path}: read back {got}")
	mode = check.get_editor_property("restitution_combine_mode") if multiplies else "default"
	rb.log(f"physics {name}: friction {friction:.3f} restitution {restitution:.3f} surface SurfaceType{surface} combine {mode} ({note})")


def main() -> None:
	rb.ensure_dir(DIR)
	for entry in MATERIALS:
		_make(*entry)
	rb.log(f"physical materials OK: {len(MATERIALS)} in {DIR}")


main()
