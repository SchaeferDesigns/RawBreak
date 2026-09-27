"""UE-6a dev map: an empty dark stage with an overhead capture camera for the simulation service's debug drawing.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue6a.py
  python Tools/unreal/rbue.py capture --map /Game/Dev/UE6a/L_UE6a_SimDemo --camera UE6aTop --exec-cmds "rb.SimDemo break9" \
      --out Docs/images/dev/ue6a/break9_overhead.png

The shot is not baked into the level: at game start the dev console command rb.SimDemo (Source/RawBreak/Private/Simulation/
RbShotDebugDraw.cpp) builds the scenario, submits it to the world's URbSimulationSubsystem (the real worker path with the
same-frame hand-off) and draws the handed-off FRbShot - table outline from rb::TableGeometry, every ball's path - as
persistent debug lines. No ARbTable is placed, so the table frame is the world origin raised by the bed height (76.5 cm).

Camera: straight down with the core frame upright in the image (core +x = foot to the right, core +y up), manual exposure
(the stage is unlit, the debug lines are unlit), so the colours come out as drawn.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

LEVEL = "/Game/Dev/UE6a/L_UE6a_SimDemo"
BED_Z = 76.5  # cm, TableSpec::BedHeight of the 9-ft pro table


def manual_exposure(cam) -> None:
	comp = cam.camera_component
	pps = comp.get_editor_property("post_process_settings")
	pps.set_editor_property("override_auto_exposure_method", True)
	pps.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
	pps.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
	pps.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
	pps.set_editor_property("override_auto_exposure_bias", True)
	pps.set_editor_property("auto_exposure_bias", 0.0)
	pps.set_editor_property("override_motion_blur_amount", True)
	pps.set_editor_property("motion_blur_amount", 0.0)
	pps.set_editor_property("override_vignette_intensity", True)
	pps.set_editor_property("vignette_intensity", 0.0)
	pps.set_editor_property("override_bloom_intensity", True)
	pps.set_editor_property("bloom_intensity", 0.0)
	comp.set_editor_property("post_process_settings", pps)
	comp.set_editor_property("post_process_blend_weight", 1.0)


def main() -> None:
	rb.ensure_dir("/Game/Dev/UE6a")
	rb.new_level(LEVEL)

	# Overhead: 3.4 m above the cloth, 50 deg horizontal FOV -> 3.17 m x 1.78 m at 16:9 (table incl. rails 2.90 x 1.63 m).
	top = rb.spawn(unreal.CameraActor, (0.0, 0.0, BED_Z + 340.0), (-90.0, -90.0, 0.0), "UE6aTop")
	top.tags = ["UE6aTop"]
	top.camera_component.set_editor_property("field_of_view", 50.0)
	manual_exposure(top)

	# Rack close-up: the foot half of the table (rack at the foot spot x = +0.635 m), 1.6 m above the cloth.
	rack = rb.spawn(unreal.CameraActor, (80.0, 0.0, BED_Z + 160.0), (-90.0, -90.0, 0.0), "UE6aRack")
	rack.tags = ["UE6aRack"]
	rack.camera_component.set_editor_property("field_of_view", 50.0)
	manual_exposure(rack)

	# The default pawn spawns here, out of both views (the capture hides it anyway).
	rb.spawn(unreal.PlayerStart, (-400.0, 0.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")

	rb.save_current_level(LEVEL)
	if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
		rb.fail(f"{LEVEL} was not written")
	rb.log("UE-6a dev map OK")


main()
