"""UE-5a dev map: the cue poses the stroke component renders (Docs/ue-architecture.md 10, 11: rb_dev_<wp>.py).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_ue5a.py
  python Tools/unreal/rbue.py capture --map /Game/Dev/UE5a/L_StrokeDev --camera StrobeCam --out Docs/images/dev/UE-5a/stroke_strobe.png
  python Tools/unreal/rbue.py capture --map /Game/Dev/UE5a/L_StrokeDev --camera GapCam    --out Docs/images/dev/UE-5a/practice_vs_contact.png
  python Tools/unreal/rbue.py capture --map /Game/Dev/UE5a/L_StrokeDev --camera DriftCam  --out Docs/images/dev/UE-5a/samplehand_drift.png

Everything comes from URbStrokeComponent.DevRecordStrokePoses (the real component on a test clock, scripted strokes): the
level only draws the returned poses with engine basic shapes (a cylinder per cue pose, the tip dome as a small sphere),
so it needs neither UE-1's table nor UE-4's cue mesh. Three setups side by side (UE Y offsets):

  StrobeCam (Y = +300)      side view of a committed 2 m/s stroke at 60 fps: the tip dome of every frame of the last 0.35 s
                         before contact, blue -> orange, contact pose red with its shaft (spacing grows = the tip accelerates).
  GapCam    (Y = +60)    top-view close-up: a practice stroke (Commit not held) stops 4 mm short (blue), the committed
                         stroke touches the ball (red) - two setups 7 cm apart.
  DriftCam  (Y = +160)   top view: SampleHand drift of a 4 s address, exaggerated x20 (NoiseScale 20) so the grip drift
                         is visible, one pose every 0.1 s (grey), and the committed contact pose with the per-shot ramp (red).
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

DEV_DIR = "/Game/Dev/UE5a"
LEVEL = DEV_DIR + "/L_StrokeDev"
R_CM = 2.8575           # cue-ball radius
DOME_CM = 1.06          # tip dome radius (TipState default)
TIP_R_CM = 0.64         # cue radius at the tip (w_tip / 2)
BUTT_R_CM = 1.59
CUE_LEN_CM = 147.32


def make_color_material(name: str, color) -> str:
	path = f"{DEV_DIR}/{name}"
	rb.delete_asset_if_exists(path)
	tools = unreal.AssetToolsHelpers.get_asset_tools()
	mic = tools.create_asset(name, DEV_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	parent = unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
	if mic is None or parent is None:
		rb.fail(f"could not create {path}")
	if "Color" not in [str(n) for n in unreal.MaterialEditingLibrary.get_vector_parameter_names(parent)]:
		rb.fail("BasicShapeMaterial has no 'Color' parameter")
	unreal.MaterialEditingLibrary.set_material_instance_parent(mic, parent)
	unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mic, "Color", unreal.LinearColor(*color, 1.0))
	unreal.EditorAssetLibrary.save_loaded_asset(mic)
	return path


def vec(t) -> unreal.Vector:
	return unreal.Vector(t[0], t[1], t[2])


def draw_cue(pose: unreal.Transform, offset, material: str, label: str, shaft_cm: float = CUE_LEN_CM) -> None:
	"""One cue pose: tip dome centre at pose.translation, +X of the rotation = butt -> tip. The dome is drawn as a sphere of the
	dome radius (its surface is what touches the ball), the shaft as a cylinder of shaft_cm behind it (0 = dome only)."""
	loc = pose.translation
	d = pose.rotation.rotator().get_forward_vector()
	tip = unreal.Vector(loc.x + offset[0], loc.y + offset[1], loc.z + offset[2])
	rb.spawn_mesh("/Engine/BasicShapes/Sphere.Sphere", (tip.x, tip.y, tip.z), scale=(2 * DOME_CM / 100.0,) * 3,
		label=label + "_Dome", material_path=material)
	if shaft_cm <= 0.0:
		return
	# Shaft from inside the dome backwards (0.55 r_dome ahead of its centre, so the cylinder's end cap stays inside the sphere),
	# mean radius of the drawn length.
	start = unreal.Vector(tip.x + d.x * DOME_CM * 0.55, tip.y + d.y * DOME_CM * 0.55, tip.z + d.z * DOME_CM * 0.55)
	radius = TIP_R_CM + (BUTT_R_CM - TIP_R_CM) * 0.5 * shaft_cm / CUE_LEN_CM
	centre = unreal.Vector(start.x - d.x * shaft_cm * 0.5, start.y - d.y * shaft_cm * 0.5, start.z - d.z * shaft_cm * 0.5)
	z_rot = unreal.MathLibrary.make_rot_from_z(d)
	rb.spawn_mesh("/Engine/BasicShapes/Cylinder.Cylinder", (centre.x, centre.y, centre.z),
		scale=(2.0 * radius / 100.0, 2.0 * radius / 100.0, shaft_cm / 100.0), rotation=(z_rot.pitch, z_rot.yaw, z_rot.roll),
		label=label, material_path=material)


def ball(offset, material: str, label: str) -> None:
	rb.spawn_mesh("/Engine/BasicShapes/Sphere.Sphere", (offset[0], offset[1], offset[2] + R_CM), scale=(2 * R_CM / 100.0,) * 3,
		label=label, material_path=material)


def camera(label: str, eye, target, fov: float, rotation=None) -> None:
	rot = rotation if rotation is not None else rb.look_at_rotation(eye, target)
	cam = rb.spawn(unreal.CameraActor, eye, rot, label)
	cam.tags = [label]
	comp = cam.camera_component
	comp.set_editor_property("field_of_view", fov)
	comp.set_editor_property("constrain_aspect_ratio", False)


def lerp(a, b, t):
	return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def fresh_level(path: str) -> None:
	"""Recreates the level idempotently. On a re-run LevelEditorSubsystem.new_level logs an error for the just-deleted asset
	(still known to the registry), which rbue.py counts as a failure; a blank map saved over the path avoids that."""
	if not unreal.EditorAssetLibrary.does_asset_exist(path):
		rb.new_level(path)
		return
	world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
	if world is None or not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
		rb.fail(f"could not recreate level {path}")


def main() -> None:
	rb.ensure_dir(DEV_DIR)
	fresh_level(LEVEL)
	lib = unreal.RbStrokeComponent

	cloth = make_color_material("MI_Cloth", (0.03, 0.18, 0.08))
	white = make_color_material("MI_Ball", (0.85, 0.83, 0.78))
	red = make_color_material("MI_Contact", (0.8, 0.05, 0.03))
	grey = make_color_material("MI_Drift", (0.35, 0.35, 0.38))
	blue = make_color_material("MI_Practice", (0.05, 0.2, 0.9))
	strobe = [make_color_material(f"MI_Strobe{i}", lerp((0.1, 0.3, 1.0), (1.0, 0.55, 0.05), i / 5.0)) for i in range(6)]

	rb.spawn_mesh("/Engine/BasicShapes/Plane.Plane", (0, 150, 0), scale=(8, 8, 1), label="Cloth", material_path=cloth)

	# --- 1. strobe of a committed stroke (60 fps), side view -----------------------------------------------------------------
	poses, times = lib.dev_record_stroke_poses(2.0, 60.0, 1.0, 0.3, True, 7)
	if len(poses) < 10:
		rb.fail(f"committed stroke recorded {len(poses)} poses")
	t_contact = times[-1]
	strobe_base = (0.0, 300.0, 0.0)
	ball(strobe_base, white, "Strobe_Ball")
	strobe_poses = [(p, t) for p, t in zip(poses[:-1], times[:-1]) if t >= t_contact - 0.35]
	for i, (p, t) in enumerate(strobe_poses):
		k = min(5, int(6 * (t - (t_contact - 0.35)) / 0.35))
		draw_cue(p, strobe_base, strobe[k], f"Strobe_{i:02d}", shaft_cm=0.0)
	draw_cue(poses[-1], strobe_base, red, "Strobe_Contact", shaft_cm=40.0)
	rb.log(f"strobe: {len(strobe_poses)} frames before contact at t = {t_contact:.3f} s, contact tip {poses[-1].translation}")
	camera("StrobeCam", (-14.0, 205.0, 4.0), (-14.0, 300.0, 4.0), 32.0)

	# --- 2. practice stroke stop-short vs committed contact, tip close-up -----------------------------------------------------
	practice, _ = lib.dev_record_stroke_poses(2.0, 60.0, 1.0, 0.3, False, 7)
	committed, _ = lib.dev_record_stroke_poses(2.0, 60.0, 1.0, 0.3, True, 7)
	gap_a = (0.0, 56.5, 0.0)
	gap_b = (0.0, 63.5, 0.0)
	ball(gap_a, white, "Gap_BallPractice")
	ball(gap_b, white, "Gap_BallContact")
	draw_cue(practice[-1], gap_a, blue, "Gap_Practice", shaft_cm=20.0)
	draw_cue(committed[-1], gap_b, red, "Gap_Contact", shaft_cm=20.0)
	gap_cm = -(practice[-1].translation.x - committed[-1].translation.x)
	rb.log(f"practice tip stops {gap_cm * 10.0:.3f} mm behind the contact position (along X)")
	camera("GapCam", (-4.5, 60.0, 32.0), (-4.4, 60.0, 0.0), 28.0)  # top view: dome-to-ball gap along X

	# --- 3. SampleHand drift of a 4 s address (x15), top view -----------------------------------------------------------------
	drift, drift_t = lib.dev_record_stroke_poses(2.0, 10.0, 20.0, 4.0, True, 3)
	base = (0.0, 160.0, 0.0)
	ball(base, white, "Drift_Ball")
	down = [(p, t) for p, t in zip(drift[:-1], drift_t[:-1]) if 1.0 <= t <= 4.9]
	for i, (p, t) in enumerate(down):
		draw_cue(p, base, grey, f"Drift_{i:02d}")
	draw_cue(drift[-1], base, red, "Drift_Contact")
	rb.log(f"drift: {len(down)} address poses, contact at t = {drift_t[-1]:.3f} s")
	# Straight down with yaw 90 (image up = +Y), so the cue (along X) runs along the long image axis.
	camera("DriftCam", (-75.0, 160.0, 150.0), None, 59.0, rotation=(-90.0, 90.0, 0.0))

	# Light: sun + sky (physically based; auto exposure of the capture camera).
	sun = rb.spawn(unreal.DirectionalLight, (0, 0, 500), (-50.0, 30.0, 0.0), "Sun")
	sun.light_component.set_editor_property("intensity", 10.0)
	sun.light_component.set_editor_property("atmosphere_sun_light", True)
	rb.spawn(unreal.SkyAtmosphere, (0, 0, 0), label="SkyAtmosphere")
	sky = rb.spawn(unreal.SkyLight, (0, 0, 300), label="SkyLight")
	sky.light_component.set_editor_property("real_time_capture", True)
	sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	rb.spawn(unreal.PlayerStart, (-200.0, 0.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")

	rb.save_current_level(LEVEL)
	if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
		rb.fail(f"{LEVEL} was not written")
	rb.log("UE-5a stroke dev level OK")


main()
