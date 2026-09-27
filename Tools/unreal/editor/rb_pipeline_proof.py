"""Pipeline proof (Docs/ue-architecture.md 9.6): creates a trivial test level headless.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_pipeline_proof.py
  python Tools/unreal/rbue.py capture --map /Game/Dev/PipelineProof/L_PipelineProof --camera ProofCam --out Docs/images/pipeline-proof.png

The level only uses engine content (basic shapes, sun + sky), so it proves the tooling independently of the game code:
editor Python in a commandlet -> .umap asset; -game -RenderOffscreen + URbHeadlessCaptureSubsystem -> PNG.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

LEVEL = "/Game/Dev/PipelineProof/L_PipelineProof"


def main() -> None:
	rb.ensure_dir("/Game/Dev/PipelineProof")
	rb.new_level(LEVEL)

	# Ground, a cube and a sphere (engine basic shapes: 100 cm primitives).
	rb.spawn_mesh("/Engine/BasicShapes/Plane.Plane", (0, 0, 0), scale=(40, 40, 1), label="Ground")
	rb.spawn_mesh("/Engine/BasicShapes/Cube.Cube", (0, -80, 50), label="Cube")
	rb.spawn_mesh("/Engine/BasicShapes/Sphere.Sphere", (0, 80, 50), label="Sphere")
	rb.spawn_mesh("/Engine/BasicShapes/Cylinder.Cylinder", (160, 0, 50), scale=(0.5, 0.5, 1.0), label="Cylinder")

	# Sun + sky (physically based, auto exposure handles the range).
	sun = rb.spawn(unreal.DirectionalLight, (0, 0, 500), (-40.0, 35.0, 0.0), "Sun")
	sun.light_component.set_editor_property("intensity", 10.0)  # lux
	sun.light_component.set_editor_property("atmosphere_sun_light", True)
	rb.spawn(unreal.SkyAtmosphere, (0, 0, 0), label="SkyAtmosphere")
	sky = rb.spawn(unreal.SkyLight, (0, 0, 300), label="SkyLight")
	sky.light_component.set_editor_property("real_time_capture", True)
	sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	rb.spawn(unreal.ExponentialHeightFog, (0, 0, 0), label="Fog")

	# Capture camera (found by rbue.py capture --camera ProofCam) and a PlayerStart at the same spot.
	eye = (-420.0, -260.0, 230.0)
	rot = rb.look_at_rotation(eye, (40.0, 0.0, 40.0))
	cam = rb.spawn(unreal.CameraActor, eye, rot, "ProofCam")
	cam.tags = ["ProofCam"]
	cam.camera_component.set_editor_property("field_of_view", 70.0)
	rb.spawn(unreal.PlayerStart, eye, rot, "PlayerStart")

	rb.save_current_level(LEVEL)
	if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
		rb.fail(f"{LEVEL} was not written")
	rb.log("pipeline proof level OK")


main()
