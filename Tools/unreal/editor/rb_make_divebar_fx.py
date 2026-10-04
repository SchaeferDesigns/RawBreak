"""FX of the dive bar (M2-A; Docs/ue-architecture.md 18.8, venue-dive-bar 4.2 L35, 4.7, 6.1 emissive calibration).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_divebar_fx.py [-- --calibration | --axis-test]

Creates under /Game/Generated/Venues/DiveBar/FX:
  M_DBA_DustMote        dust motes in the lamp cone (4.7): translucent additive unlit material for the mote cloud FX_DustMotes of
                        db_arch.py. Vertex shader: each 0.2 mm card is billboarded to the camera (world-position offset), drifts
                        (Brownian ~2 mm/s, a slow updraft that wraps with a fade), world size 0.15-0.6 mm with a 1.2-pixel floor whose
                        energy is kept (coverage = (size / drawn size)^2). Pixel shader: the lamp's three bulbs light it analytically
                        with the 4.4 model (I0 cos(theta) inside the 50.2 deg cut-off) and a Henyey-Greenstein phase g = 0.8, so the
                        motes glint when the view looks toward the lamp and vanish when it looks down on the cloth - as in reality.
                        The bulb positions come from Art/DiveBar/lights.json (L1-L3).
  M_DBA_LF_GlassBlock   light function of the passing headlights L35: the 203 mm glass-block cells with their fluted lenses
                        projected across the spot cone (bright lensed cells, dark mortar lines), so the sweep throws moving caustics on
                        the ceiling and the right wall (R9).
--calibration: also builds /Game/Dev/M2A/L_EmissiveCalib (git-ignored): a white Lambertian card (albedo 0.80) lit to exactly
100 cd/m^2 by a point light (E = 100 pi / 0.8 lux) next to emissive cards of 50 / 71 / 100 / 141 / 200 cd/m^2 (M_DBA_Fallback
"Emissive" = nits x EmissiveScale), a manual-exposure camera tagged RbCam_Calib; Tools/unreal/capture_divebar.py --calibrate captures
it and writes Art/DiveBar/calibration.json (EmissiveScale, 6.1).
--axis-test: only builds /Game/Dev/M2A/L_AxisTest (git-ignored; DB-0 picture of SM_DB_AxisTest against UE's axes, camera
RbCam_AxisTest; capture_divebar.py --set db0).
Idempotent. Owner: M2-A.
"""

from __future__ import annotations

import json
import os
import sys

import unreal

EDITOR_DIR = os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor")
sys.path.insert(0, EDITOR_DIR)
import rb_common as rb  # noqa: E402
import rb_import_divebar as imp  # noqa: E402

FX_DIR = "/Game/Generated/Venues/DiveBar/FX"
REPO = os.path.normpath(os.path.abspath(unreal.Paths.project_dir()))
MEL = unreal.MaterialEditingLibrary
CALIB_MAP = "/Game/Dev/M2A/L_EmissiveCalib"

MOTE_WPO = r"""
float3 P = Pos / 100.0;
float3 C = Cam / 100.0;
float ph = UV1.x;
float t = Time;
float3 drift = 0.02 * float3(sin(t * 0.131 + ph * 6.2832), sin(t * 0.117 + ph * 12.566), 0.6 * sin(t * 0.071 + ph * 3.1));
float rise = frac(t * RiseRate + ph);
drift.z += (rise - 0.5) * RiseRange;
float3 Pm = P + drift;
float dist = max(0.05, length(C - Pm));
float s = lerp(SizeMin, SizeMax, UV1.y);
float size = max(s, dist * MinAngle);
float3 F = normalize(C - Pm);
float3 R = cross(float3(0, 0, 1), F);
R = length(R) < 1e-4 ? float3(1, 0, 0) : normalize(R);
float3 U = cross(F, R);
float2 c = UV0 - 0.5;
float3 target = Pm + (R * c.x + U * c.y) * size;
return (target - P) * 100.0;
"""

MOTE_EMISSIVE = r"""
float3 P = Pos / 100.0;
float3 C = Cam / 100.0;
float dist = max(0.05, length(C - P));
float s = lerp(SizeMin, SizeMax, UV1.y);
float size = max(s, dist * MinAngle);
float cov = (s * s) / (size * size);
float E = 0.0;
float3 Ld = 0.0;
float3 B[3] = { Bulb0, Bulb1, Bulb2 };
for (int k = 0; k < 3; k++) {
	float3 v = P - B[k] / 100.0;
	float d = max(0.05, length(v));
	float ct = -v.z / d;
	float inside = smoothstep(CosCut - 0.03, CosCut + 0.02, ct);
	float e = I0 * max(ct, 0.0) * inside / (d * d);
	E += e;
	Ld += (v / d) * e;
}
float3 L = Ld / max(1e-6, length(Ld));
float cosphi = dot(L, normalize(C - P));
float g = PhaseG;
float p = (1.0 - g * g) / (12.566 * pow(max(1e-4, 1.0 + g * g - 2.0 * g * cosphi), 1.5));
float2 uvc = UV0 - 0.5;
float disc = saturate(1.0 - length(uvc) * 2.0);
float rise = frac(Time * RiseRate + UV1.x);
float wrapfade = sin(3.14159 * rise);
float3 q = abs(P - BoxCentre / 100.0);
float boxfade = saturate(min(min(BoxHalf.x - q.x, BoxHalf.y - q.y), BoxHalf.z - q.z) / 0.25);
return MoteColor * (E * Albedo * p * cov * disc * disc * wrapfade * boxfade * Intensity * EmissiveScale);
"""

LF_GLASS = r"""
// UVs run 0..1 across the spot cone; ~16 cells of 203 mm span it at the storefront.
float2 g = UV * Cells;
float2 f = frac(g);
float2 e = min(f, 1.0 - f);
float mortar = smoothstep(0.02, 0.07, min(e.x, e.y));
float flute = 0.55 + 0.45 * cos(f.x * 6.2832 * 4.0 + sin(f.y * 6.2832) * 0.6);
float lens = 1.0 - 0.8 * length(f - 0.5);
float cone = saturate(1.0 - length(UV - 0.5) * 2.0);
return float3(1, 1, 1) * mortar * (0.35 + 0.65 * flute * lens) * (0.4 + 0.6 * cone) * Gain;
"""


def _node(m, cls, x, y, **props):
	e = MEL.create_material_expression(m, cls, x, y)
	for k, v in props.items():
		e.set_editor_property(k, v)
	return e


def _custom(m, code, inputs, x, y, out_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, desc=""):
	c = _node(m, unreal.MaterialExpressionCustom, x, y, code=code, output_type=out_type, description=desc)
	cis = []
	for name, _, _ in inputs:
		ci = unreal.CustomInput()
		ci.set_editor_property("input_name", name)
		cis.append(ci)
	c.set_editor_property("inputs", cis)
	for name, expr, pin in inputs:
		if not MEL.connect_material_expressions(expr, pin, c, name):
			rb.fail(f"{m.get_name()}: could not connect {name}")
	return c


def _fresh_material(name: str):
	path = f"{FX_DIR}/{name}"
	if unreal.EditorAssetLibrary.does_asset_exist(path):
		unreal.EditorAssetLibrary.delete_asset(path)
	rb.ensure_dir(FX_DIR)
	return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FX_DIR, unreal.Material, unreal.MaterialFactoryNew())


def lamp_bulbs() -> list:
	with open(os.path.join(REPO, "Art", "DiveBar", "lights.json"), "r", encoding="utf-8") as handle:
		lights = {j["id"]: j for j in json.load(handle)["lights"]}
	return [tuple(100.0 * c for c in lights[k]["pos"]) for k in ("L1", "L2", "L3")]


def make_dust_mote_material() -> None:
	m = _fresh_material("M_DBA_DustMote")
	m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
	m.set_editor_property("two_sided", True)
	m.set_editor_property("used_with_static_lighting", False)
	y = 0

	def scalar(name, value):
		nonlocal y
		y += 90
		return _node(m, unreal.MaterialExpressionScalarParameter, -1600, y, parameter_name=name, default_value=float(value), group="Motes")

	def vector(name, v):
		nonlocal y
		y += 90
		return _node(m, unreal.MaterialExpressionVectorParameter, -1600, y, parameter_name=name,
			default_value=unreal.LinearColor(v[0], v[1], v[2], 1.0), group="Motes")

	pos = _node(m, unreal.MaterialExpressionWorldPosition, -1600, -200,
		world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
	cam = _node(m, unreal.MaterialExpressionCameraPositionWS, -1600, -300)
	uv0 = _node(m, unreal.MaterialExpressionTextureCoordinate, -1600, -400, coordinate_index=0)
	uv1 = _node(m, unreal.MaterialExpressionTextureCoordinate, -1600, -500, coordinate_index=1)
	time = _node(m, unreal.MaterialExpressionTime, -1600, -600)
	size_min, size_max = scalar("SizeMin", 0.00015), scalar("SizeMax", 0.0006)
	min_angle = scalar("MinAngle", 0.0011)            # ~1.4 px at 1080p / 50 deg vertical
	rise_rate, rise_range = scalar("RiseRate", 0.01), scalar("RiseRange", 0.35)
	wpo = _custom(m, MOTE_WPO, [("Pos", pos, ""), ("Cam", cam, ""), ("UV0", uv0, ""), ("UV1", uv1, ""), ("Time", time, ""), ("SizeMin", size_min, ""),
		("SizeMax", size_max, ""), ("MinAngle", min_angle, ""), ("RiseRate", rise_rate, ""), ("RiseRange", rise_range, "")], -900, -200, desc="RbMoteWPO")
	b = lamp_bulbs()
	bulbs = [vector(f"Bulb{k}", b[k]) for k in range(3)]
	pos_px = _node(m, unreal.MaterialExpressionWorldPosition, -1600, 1200)
	lamp = json.load(open(os.path.join(REPO, "Art", "DiveBar", "layout.json"), encoding="utf-8"))["lamp"]
	cx, cy = lamp["centre"]
	emis = _custom(m, MOTE_EMISSIVE, [("Pos", pos_px, ""), ("Cam", cam, ""), ("UV0", uv0, ""), ("UV1", uv1, ""), ("Time", time, ""),
		("SizeMin", size_min, ""), ("SizeMax", size_max, ""), ("MinAngle", min_angle, ""), ("RiseRate", rise_rate, ""),
		("Bulb0", bulbs[0], "RGB"), ("Bulb1", bulbs[1], "RGB"), ("Bulb2", bulbs[2], "RGB"), ("I0", scalar("I0", 355.9), ""),
		("CosCut", scalar("CosCut", 0.6401), ""), ("PhaseG", scalar("PhaseG", 0.8), ""), ("Albedo", scalar("Albedo", 0.8), ""),
		("Intensity", scalar("Intensity", 1.0), ""), ("EmissiveScale", scalar("EmissiveScale", imp.emissive_scale()), ""),
		("MoteColor", vector("MoteColor", (1.0, 0.93, 0.82)), "RGB"), ("BoxCentre", vector("BoxCentre", (100 * cx, 100 * cy, 120.0)), "RGB"),
		("BoxHalf", vector("BoxHalf", (1.2, 0.7, 0.55)), "RGB")], -900, 600, desc="RbMoteEmissive")
	unlit = _node(m, unreal.MaterialExpressionSubstrateUnlitBSDF, -300, 400)
	if not any(MEL.connect_material_expressions(emis, "", unlit, pin) for pin in ("Emissive Color", "EmissiveColor", "Emissive")):
		rb.fail("M_DBA_DustMote: emissive pin")
	if not MEL.connect_material_property(unlit, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
		rb.fail("M_DBA_DustMote: front material")
	if not MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET):
		rb.fail("M_DBA_DustMote: world position offset")
	MEL.layout_material_expressions(m)
	MEL.recompile_material(m)
	unreal.EditorAssetLibrary.save_loaded_asset(m, False)
	rb.log("M_DBA_DustMote created")


def make_glass_block_light_function() -> None:
	m = _fresh_material("M_DBA_LF_GlassBlock")
	m.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
	uv = _node(m, unreal.MaterialExpressionTextureCoordinate, -900, 0, coordinate_index=0)
	cells = _node(m, unreal.MaterialExpressionScalarParameter, -900, 100, parameter_name="Cells", default_value=16.0)
	gain = _node(m, unreal.MaterialExpressionScalarParameter, -900, 200, parameter_name="Gain", default_value=1.6)
	c = _custom(m, LF_GLASS, [("UV", uv, ""), ("Cells", cells, ""), ("Gain", gain, "")], -500, 0, desc="RbGlassBlockLF")
	lf = None
	for cls_name in ("MaterialExpressionSubstrateLightFunction",):
		cls = getattr(unreal, cls_name, None)
		if cls is not None:
			lf = _node(m, cls, -200, 0)
	if lf is not None:
		if not any(MEL.connect_material_expressions(c, "", lf, pin) for pin in ("Color", "Light Function Color", "")):
			rb.fail("M_DBA_LF_GlassBlock: light function color pin")
		if not MEL.connect_material_property(lf, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
			rb.fail("M_DBA_LF_GlassBlock: front material")
	elif not MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
		rb.fail("M_DBA_LF_GlassBlock: emissive")
	MEL.layout_material_expressions(m)
	MEL.recompile_material(m)
	unreal.EditorAssetLibrary.save_loaded_asset(m, False)
	rb.log("M_DBA_LF_GlassBlock created")


def assign_mote_material() -> None:
	mesh_path = "/Game/Generated/Venues/DiveBar/FX/SM_DB_FX_DustMotes"
	if not unreal.EditorAssetLibrary.does_asset_exist(mesh_path):
		rb.log("FX_DustMotes not imported yet (db_arch.py + rb_import_divebar.py) - material not assigned")
		return
	mesh = unreal.load_asset(mesh_path)
	slots = mesh.get_editor_property("static_materials")
	mat = unreal.load_asset(f"{FX_DIR}/M_DBA_DustMote")
	for i, sm in enumerate(slots):
		sm.set_editor_property("material_interface", mat)
		slots[i] = sm
	mesh.set_editor_property("static_materials", slots)
	unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
	rb.log("M_DBA_DustMote assigned to SM_DB_FX_DustMotes")


def build_calibration_map() -> None:
	"""6.1: a 100 cd/m^2 white card (albedo 0.8, lit by a point light) next to emissive cards of 50..200 cd/m^2."""
	rb.ensure_dir("/Game/Dev/M2A")
	if unreal.EditorAssetLibrary.does_asset_exist(CALIB_MAP):
		world = unreal.EditorLoadingAndSavingUtils.load_map(CALIB_MAP)
		eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
		for a in eas.get_all_level_actors():
			if not isinstance(a, (unreal.WorldSettings, unreal.Brush)):
				eas.destroy_actor(a)
	else:
		world = rb.new_level(CALIB_MAP)
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase)  # no RAW BREAK match / table here
	cube = unreal.load_asset("/Engine/BasicShapes/Cube")
	master = imp.make_fallback_master()
	white = imp.make_instance("/Game/Dev/M2A", "MI_Calib_White", master, {"Pattern": 0, "Roughness": 0.5, "F0": 0.0, "DustAmount": 0.0, "Age": 0.0,
		"EmissiveScale": 1.0}, {"BaseColor": (0.8, 0.8, 0.8)})
	cards = [("White", 0.0, white)]
	for k, nits in enumerate((50.0, 70.7, 100.0, 141.4, 200.0)):
		mi = imp.make_instance("/Game/Dev/M2A", f"MI_Calib_E{k}", master, {"Pattern": 0, "Roughness": 1.0, "F0": 0.0, "Emissive": nits, "DustAmount": 0.0,
			"Age": 0.0, "EmissiveScale": 1.0}, {"BaseColor": (0.0, 0.0, 0.0), "EmissiveColor": (1.0, 1.0, 1.0)})
		cards.append((f"E{int(round(nits))}", 25.0 * (k + 1), mi))
	# cards 20 x 20 cm, facing -X, in a row along Y at x = 0; the camera at x = -150 looks +X
	for k, (name, _, mi) in enumerate(cards):
		a = rb.spawn(unreal.StaticMeshActor, (0.0, -75.0 + 25.0 * k, 100.0), (0.0, 0.0, 0.0), f"Card_{name}")
		comp = a.static_mesh_component
		comp.set_static_mesh(cube)
		comp.set_material(0, mi)
		a.set_actor_scale3d(unreal.Vector(0.02, 0.2, 0.2))
	# point light 1 m in front of the white card on its normal: E = I / d^2 -> I = 100 pi / 0.8 cd = 392.7 cd at d = 1 m
	light = rb.spawn(unreal.PointLight, (-101.0, -75.0, 100.0), (0.0, 0.0, 0.0), "CalibLight")
	lc = light.get_component_by_class(unreal.PointLightComponent)
	lc.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
	lc.set_editor_property("intensity", 392.7)
	lc.set_editor_property("attenuation_radius", 300.0)
	lc.set_editor_property("source_radius", 0.5)
	lc.set_editor_property("cast_shadows", False)
	lc.set_editor_property("use_temperature", False)
	lc.set_editor_property("light_color", unreal.Color(255, 255, 255, 255))
	lc.set_editor_property("indirect_lighting_intensity", 0.0)
	# the light must not reach the emissive cards' fronts noticeably: they are black (albedo 0), so only their emission counts
	cam = rb.spawn(unreal.CameraActor, (-150.0, 0.0, 100.0), (0.0, 0.0, 0.0), "RbCam_Calib")
	cam.tags = ["RbCam_Calib"]
	cc = cam.camera_component
	cc.set_editor_property("field_of_view", 70.0)
	pp = cc.get_editor_property("post_process_settings")
	pp.set_editor_property("override_auto_exposure_method", True)
	pp.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
	pp.set_editor_property("override_auto_exposure_bias", True)
	pp.set_editor_property("auto_exposure_bias", -8.5)  # compensation: 100 cd/m2 -> ~0.28 linear
	pp.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
	pp.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
	pp.set_editor_property("override_bloom_intensity", True)
	pp.set_editor_property("bloom_intensity", 0.0)
	pp.set_editor_property("override_vignette_intensity", True)
	pp.set_editor_property("vignette_intensity", 0.0)
	pp.set_editor_property("override_film_grain_intensity", True)
	pp.set_editor_property("film_grain_intensity", 0.0)
	cc.set_editor_property("post_process_settings", pp)
	cc.set_editor_property("post_process_blend_weight", 1.0)
	rb.save_current_level(CALIB_MAP)
	rb.log(f"calibration map {CALIB_MAP} built")


AXIS_MAP = "/Game/Dev/M2A/L_AxisTest"


def build_axis_test_map() -> None:
	"""DB-0 (venue-dive-bar 13.4): SM_DB_AxisTest at the origin on a 10 cm grid floor, UE's +X (red) / +Y (green) axes as 1.5 m floor
	bars with text labels, a 1 m reference post, a soft key light and a camera tagged RbCam_AxisTest (capture_divebar.py --set db0
	-> Docs/images/divebar/db0/axis_test.png). The arrow of the asset must point along the red bar, its +Y marker toward the green."""
	mesh_path = "/Game/Generated/Venues/DiveBar/Arch/AxisTest/SM_DB_AxisTest"
	if not unreal.EditorAssetLibrary.does_asset_exist(mesh_path):
		rb.fail(f"{mesh_path} missing: run db_axis_test.py and rb_import_divebar.py first")
	rb.ensure_dir("/Game/Dev/M2A")
	if unreal.EditorAssetLibrary.does_asset_exist(AXIS_MAP):
		unreal.EditorLoadingAndSavingUtils.load_map(AXIS_MAP)
		eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
		for a in eas.get_all_level_actors():
			if not isinstance(a, (unreal.WorldSettings, unreal.Brush)):
				eas.destroy_actor(a)
	else:
		rb.new_level(AXIS_MAP)
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase)
	cube = unreal.load_asset("/Engine/BasicShapes/Cube")
	master = imp.make_fallback_master()

	def mi(name, color, emissive=0.0):
		return imp.make_instance("/Game/Dev/M2A", name, master, {"Pattern": 0, "Roughness": 0.7, "F0": 0.04, "DustAmount": 0.0, "Age": 0.0,
			"Emissive": emissive, "EmissiveScale": 1.0}, {"BaseColor": color, "EmissiveColor": color})

	def box(label, centre, size, material):
		a = rb.spawn(unreal.StaticMeshActor, centre, (0.0, 0.0, 0.0), label)
		a.static_mesh_component.set_static_mesh(cube)
		a.static_mesh_component.set_material(0, material)
		a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
		return a

	floor, line = mi("MI_Axis_Floor", (0.30, 0.30, 0.30)), mi("MI_Axis_Grid", (0.08, 0.08, 0.08))
	red, green, post = mi("MI_Axis_X", (0.8, 0.05, 0.03), 4.0), mi("MI_Axis_Y", (0.05, 0.6, 0.08), 4.0), mi("MI_Axis_Post", (0.75, 0.75, 0.70))
	box("Floor", (0.0, 0.0, -1.0), (600.0, 600.0, 2.0), floor)
	for k in range(-20, 21):  # 10 cm grid, every metre bolder
		w = 0.8 if k % 10 else 2.0
		box(f"GridX{k}", (0.0, 10.0 * k, 0.05), (400.0, w, 0.1), line)
		box(f"GridY{k}", (10.0 * k, 0.0, 0.05), (w, 400.0, 0.1), line)
	box("AxisX", (125.0, 0.0, 0.3), (150.0, 3.0, 0.6), red)
	box("AxisY", (0.0, 125.0, 0.3), (3.0, 150.0, 0.6), green)
	box("Post1m", (-90.0, -90.0, 50.0), (4.0, 4.0, 100.0), post)
	for text, loc, color in (("+X", (210.0, 0.0, 2.0), unreal.Color(r=255, g=40, b=30, a=255)), ("+Y", (0.0, 210.0, 2.0), unreal.Color(r=40, g=220, b=60, a=255)),
			("1 m", (-90.0, -90.0, 106.0), unreal.Color(r=230, g=230, b=230, a=255))):
		# floor labels lie face up, reading along -Y with their top toward -X (upright from the camera); the post label faces the camera
		t = rb.spawn(unreal.TextRenderActor, loc, (90.0, 0.0, 0.0) if loc[2] < 10 else (0.0, 39.0, 0.0), f"Label{text}")
		tc = t.get_component_by_class(unreal.TextRenderComponent)
		tc.set_editor_property("text", text)
		tc.set_editor_property("world_size", 24.0)
		tc.set_editor_property("text_render_color", color)
		tc.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
	a = rb.spawn(unreal.StaticMeshActor, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), "SM_DB_AxisTest")
	a.static_mesh_component.set_static_mesh(unreal.load_asset(mesh_path))
	light = rb.spawn(unreal.DirectionalLight, (0.0, 0.0, 400.0), (-50.0, 140.0, 0.0), "Key")
	lc = light.get_component_by_class(unreal.DirectionalLightComponent)
	lc.set_editor_property("intensity", 8.0)
	rb.spawn(unreal.SkyLight, (0.0, 0.0, 300.0), (0.0, 0.0, 0.0), "Sky").get_component_by_class(unreal.SkyLightComponent).set_editor_property("intensity", 1.0)
	cam = rb.spawn(unreal.CameraActor, (300.0, 230.0, 230.0), (-30.0, -142.0, 0.0), "RbCam_AxisTest")
	cam.tags = ["RbCam_AxisTest"]
	cam.camera_component.set_editor_property("field_of_view", 60.0)
	rb.save_current_level(AXIS_MAP)
	rb.log(f"axis test map {AXIS_MAP} built")


def main() -> None:
	args = [a for a in sys.argv[1:] if a and not a.lower().endswith(".py")]
	if "--axis-test" in args:
		build_axis_test_map()
		return
	make_dust_mote_material()
	make_glass_block_light_function()
	assign_mote_material()
	if "--calibration" in args:
		build_calibration_map()


main()
