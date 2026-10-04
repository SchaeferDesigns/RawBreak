"""Pipeline proof (Docs/ue-architecture.md 9.6, M2 capture features 18.9): creates a trivial test level headless.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_pipeline_proof.py
  python Tools/unreal/rbue.py capture --map /Game/Dev/PipelineProof/L_PipelineProof --camera ProofCam --out Docs/images/pipeline-proof.png
  # M2: several views in one process, a hidden "ceiling" (tag RbDB_Ceiling, like the dive bar's plan view V10), EV100 per view
  python Tools/unreal/rbue.py capture --map /Game/Dev/PipelineProof/L_PipelineProof --camera ProofCam,ProofCamTop \
      --hide-tags RbDB_Ceiling --out Docs/images/dev/m20/proof_{camera}.png

The level only uses engine content (basic shapes, sun + sky), so it proves the tooling independently of the game code:
editor Python in a commandlet -> .umap asset; -game -RenderOffscreen + URbHeadlessCaptureSubsystem -> PNG. The canopy above the
shapes carries the tag RbDB_Ceiling (RbAssetPaths::Tag::DiveBarCeiling): it shades the shapes and hides them from the top camera
unless the capture hides it (-RBCaptureHideTags). Owner: UE-0 / M2-0.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

LEVEL = "/Game/Dev/PipelineProof/L_PipelineProof"
CEILING_TAG = "RbDB_Ceiling"  # RbAssetPaths::Tag::DiveBarCeiling


def main() -> None:
	rb.ensure_dir("/Game/Dev/PipelineProof")
	rb.new_level(LEVEL)

	# Ground, a cube and a sphere (engine basic shapes: 100 cm primitives).
	rb.spawn_mesh("/Engine/BasicShapes/Plane.Plane", (0, 0, 0), scale=(40, 40, 1), label="Ground")
	rb.spawn_mesh("/Engine/BasicShapes/Cube.Cube", (0, -80, 50), label="Cube")
	rb.spawn_mesh("/Engine/BasicShapes/Sphere.Sphere", (0, 80, 50), label="Sphere")
	rb.spawn_mesh("/Engine/BasicShapes/Cylinder.Cylinder", (160, 0, 50), scale=(0.5, 0.5, 1.0), label="Cylinder")

	# M2: a flat canopy over the shapes, tagged like the dive bar's ceiling (hidden by rbue.py capture --hide-tags RbDB_Ceiling).
	canopy = rb.spawn_mesh("/Engine/BasicShapes/Cube.Cube", (40, 0, 320), scale=(5.0, 4.0, 0.1), label="Canopy")
	canopy.tags = [CEILING_TAG]

	# Sun + sky (physically based, auto exposure handles the range).
	sun = rb.spawn(unreal.DirectionalLight, (0, 0, 500), (-40.0, 35.0, 0.0), "Sun")
	sun.light_component.set_editor_property("intensity", 10.0)  # lux
	sun.light_component.set_editor_property("atmosphere_sun_light", True)
	rb.spawn(unreal.SkyAtmosphere, (0, 0, 0), label="SkyAtmosphere")
	sky = rb.spawn(unreal.SkyLight, (0, 0, 300), label="SkyLight")
	sky.light_component.set_editor_property("real_time_capture", True)
	sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	rb.spawn(unreal.ExponentialHeightFog, (0, 0, 0), label="Fog")

	# Capture cameras (found by rbue.py capture --camera ProofCam / ProofCamTop) and a PlayerStart at the first one.
	eye = (-420.0, -260.0, 230.0)
	rot = rb.look_at_rotation(eye, (40.0, 0.0, 40.0))
	cam = rb.spawn(unreal.CameraActor, eye, rot, "ProofCam")
	cam.tags = ["ProofCam"]
	cam.camera_component.set_editor_property("field_of_view", 70.0)
	top = rb.spawn(unreal.CameraActor, (40.0, 0.0, 900.0), (-90.0, 0.0, 0.0), "ProofCamTop")
	top.tags = ["ProofCamTop"]
	top.camera_component.set_editor_property("field_of_view", 50.0)
	rb.spawn(unreal.PlayerStart, eye, rot, "PlayerStart")

	rb.save_current_level(LEVEL)
	if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
		rb.fail(f"{LEVEL} was not written")
	rb.log("pipeline proof level OK")


main()
