"""UE-6b dev map (Docs/ue-architecture.md 10, 11): the match director's authoritative table state, seen from above.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue6b.py
  (PowerShell; the map URL carries the match options)
  python Tools/unreal/rbue.py capture --map "/Game/Dev/UE6b/L_UE6b_Match?Mode=HotSeat?Seed=12?Rate=0" --camera UE6bTop
      --exec-cmds "rb.Match.DrawTableState 1, rb.Match.Place -0.755 0.08" --out Docs/images/dev/UE-6b/rack.png
  ... --exec-cmds "rb.Match.DrawTableState 1, rb.Match.Break 9, rb.Match.Dump" --out Docs/images/dev/UE-6b/after_break.png
  ... --exec-cmds "rb.Match.DrawTableState 1, rb.Match.Layout 0 0.85 -0.215 1 -0.9 0.4 9 1.15 -0.515,
      rb.Match.StrikeAt 2.0 1.15 -0.515 -0.3, rb.Match.Dump" --out Docs/images/dev/UE-6b/nine_spotted.png  (spotted 9, R-12)

The level has no table of its own: ARbGameMode (World Settings) spawns ARbTable, ARbBallSet and ARbCue at the origin and
starts the match from the URL options; the console variable rb.Match.DrawTableState draws the director's FRbTableState
(ball discs in the ball colours, stripes ringed white, seven-segment ids), the playing surface nose to nose, the head
string (yellow while the cue ball is in hand above it), the spots and every pocket opening (mouth line + drop-edge circle)
as debug lines. The rb.Match.* console commands (Place, Break, Strike, StrikeAt, Layout, Confirm, Dump) drive the match for
the captures. Scratch content under
/Game/Dev (git-ignored); the script is idempotent.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

LEVEL = "/Game/Dev/UE6b/L_UE6b_Match"
BED_Z = 76.5  # cm, only for the camera height: the table itself comes from rb::TableSpec at runtime


def fresh_level(path: str) -> unreal.World:
	"""An empty level at path. rb_common.new_level cannot replace an existing map in the commandlet (the level editor
	refuses the destination, which logs an error), so a re-run loads the map and removes its actors instead."""
	if not unreal.EditorAssetLibrary.does_asset_exist(path):
		return rb.new_level(path)
	world = unreal.EditorLoadingAndSavingUtils.load_map(path)
	if world is None:
		rb.fail(f"could not load {path}")
	eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
	for actor in eas.get_all_level_actors():
		if not isinstance(actor, unreal.WorldSettings):
			eas.destroy_actor(actor)
	return world


def main() -> None:
	rb.ensure_dir("/Game/Dev/UE6b")
	world = fresh_level(LEVEL)

	# The game mode of this map: ARbGameMode (the M1 map of UE-8 does the same in its World Settings).
	settings = world.get_world_settings()
	settings.set_editor_property("default_game_mode", unreal.RbGameMode)

	# Neutral floor and soft light, fixed exposure (the capture shows debug lines, not look-dev).
	rb.spawn_mesh("/Engine/BasicShapes/Plane.Plane", (0, 0, 0), scale=(8, 5, 1), label="Floor")
	sun = rb.spawn(unreal.DirectionalLight, (0, 0, 500), (-70.0, 30.0, 0.0), "Sun")
	sun.light_component.set_editor_property("intensity", 1.0)  # lux: ~0.15 cd/m2 on the grey floor = mid grey at EV100 0
	ppv = rb.spawn(unreal.PostProcessVolume, (0, 0, 0), label="Exposure")
	ppv.set_editor_property("unbound", True)
	pp = ppv.get_editor_property("settings")
	pp.set_editor_property("override_auto_exposure_method", True)
	pp.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
	pp.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
	pp.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
	pp.set_editor_property("override_auto_exposure_bias", True)
	pp.set_editor_property("auto_exposure_bias", 0.0)
	pp.set_editor_property("override_bloom_intensity", True)
	pp.set_editor_property("bloom_intensity", 0.0)
	pp.set_editor_property("override_vignette_intensity", True)
	pp.set_editor_property("vignette_intensity", 0.0)
	ppv.set_editor_property("settings", pp)

	# Top-down capture camera: screen right = core +x (foot), screen up = core +y (the head end on the left). Perspective with a
	# narrow lens from high up (the balls lie in one plane, so the scale is uniform): about 3.1 m of the bed plane across the image.
	height = 440.0
	cam = rb.spawn(unreal.CameraActor, (0.0, 0.0, BED_Z + height), (-90.0, -90.0, 0.0), "UE6bTop")
	cam.tags = ["UE6bTop"]
	comp = cam.camera_component
	comp.set_editor_property("projection_mode", unreal.CameraProjectionMode.PERSPECTIVE)
	comp.set_editor_property("field_of_view", 2.0 * math.degrees(math.atan(155.0 / height)))
	comp.set_editor_property("constrain_aspect_ratio", False)

	# PlayerStart at the head end (the pawn is hidden by the capture camera).
	rb.spawn(unreal.PlayerStart, (-260.0, 0.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")

	rb.save_current_level(LEVEL)
	if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
		rb.fail(f"{LEVEL} was not written")
	rb.log("UE-6b dev level OK")


main()
