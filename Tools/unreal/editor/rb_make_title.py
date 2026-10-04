"""The minimal title / venue-select level /Game/Generated/Maps/L_Title (M2-D; Docs/ue-architecture.md 18.4).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_title.py
  python Tools/unreal/editor/rb_dev_m2d.py --capture title title_play      (look at the result)

World Settings GameMode = ARbTitleGameMode, which shows SRbTitleScreen (RAW BREAK - Play: The Low Bridge Tavern / Test room
x Practice / Hot-seat, Settings, Quit) over the scene through the title camera (ARbLookDevCamera tagged RbCam_Title, Eyes
preset: the menu looks exactly like the game it opens into, ui-ux 6.3). The scene is a stand-in for the 3D menu "Closing Time"
(the dive bar after hours, ui-ux 6), which replaces it once the venue has its AfterHours state. A corner of a small American
bar at night, built from the engine's basic shapes and procedural materials (no textures, no downloads):
  * the 7-ft coin-op bar table (ARbTable SevenFootBar, the dive bar's oversized-cue-ball set) after an 8-ball break
    (ARbBallRackDemo frozen at rest - the house game), a chalk cube on the rail;
  * the three-shade pool-table lamp (green enamel on a bar, warm 2700 K bulbs: spot lights with volumetric scattering), the key
    light, 0.86 m above the bed as in the venue;
  * the room: black / oxblood checkered vinyl floor with scuffs, stained vertical board panelling to the chair rail, smoke-yellowed
    plaster above, the house chalkboard, the wall cue rack with the house cues;
  * the bar in the back left: counter and stools, a mirrored back bar with three shelves of backlit bottles (warm LED strips);
  * a red "COLD BEER" neon on the back wall (tube letters with a coloured spill light) and a lit beer clock (blue rim, no
    lettering, 1:50 - last call) on the side wall;
  * lived-in clutter: the drink rail with the players' bottles, stools pulled up, the triangle on its hook, the flyer board,
    the EXIT box;
  * haze (exponential height fog with volumetric fog).
The menu list sits over the dark left third of the frame (the table lives in the right two thirds; the back bar glows dimly
behind the list's ink gradient). Materials are small masters + instances under /Game/Generated/UI/Title/. Idempotent: a re-run
reloads the map, removes every actor and rebuilds it. Owner: M2-D.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

MAP = "/Game/Generated/Maps/L_Title"  # RbAssetPaths::TitleMap
MAT_DIR = "/Game/Generated/UI/Title"
CAMERA_TAG = "RbCam_Title"            # ARbTitleGameMode::TitleCameraTag

BED_Z = 74.3                          # 7-ft bar table bed height [cm] (venue-dive-bar VDB-T10)
LAMP_UNDERSIDE = 86.0                 # lamp underside above the bed [cm]
SHADE_X = (-65.0, 0.0, 65.0)          # the three shades along the table (lamp footprint x -0.65 .. 0.65 m)
LAMP_LUMENS = 120.0                   # per shade: the cloth ~3 EV over the room the Eyes camera meters on (dimmer than the venue's
                                      # 1100 lm bulbs: the title camera meters the dark room, not the cloth)

# Room extents [cm]: the back wall behind the foot of the table, the side wall on the camera's right, the bar in the back left.
WALL_X = 330.0
WALL_Y = 300.0
LEFT_Y = -440.0
FRONT_X = -470.0
CEILING_Z = 320.0

MASTER_NAMES = ("M_Title_Surface", "M_Title_Checker", "M_Title_Boards", "M_Title_Glass", "M_Title_Emissive")


# ---------------------------------------------------------------------------------------------------------------------------
# Level
# ---------------------------------------------------------------------------------------------------------------------------

def fresh_level(path: str) -> unreal.World:
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


# ---------------------------------------------------------------------------------------------------------------------------
# Materials: four small lit masters (world-space procedural detail, so every box gets its own variation) + one unlit emissive
# ---------------------------------------------------------------------------------------------------------------------------

MEL = unreal.MaterialEditingLibrary


def _node(mat, cls, x: int, y: int, **props):
	node = MEL.create_material_expression(mat, cls, x, y)
	for key, value in props.items():
		node.set_editor_property(key, value)
	return node


def _link(src, dst, pin: str = "", out: str = "") -> None:
	if not MEL.connect_material_expressions(src, out, dst, pin):
		rb.fail(f"material link {src.get_name()}.{out} -> {dst.get_name()}.{pin} failed")


def _scalar(mat, name: str, value: float, x: int, y: int):
	return _node(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def _vector(mat, name: str, value, x: int, y: int):
	return _node(mat, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
		default_value=unreal.LinearColor(value[0], value[1], value[2], 1.0))


def _const(mat, value: float, x: int, y: int):
	return _node(mat, unreal.MaterialExpressionConstant, x, y, r=value)


def _op(mat, cls, a, b, x: int, y: int, a_out: str = "", b_out: str = ""):
	node = _node(mat, cls, x, y)
	_link(a, node, "A", a_out)
	_link(b, node, "B", b_out)
	return node


def _lerp(mat, a, b, alpha, x: int, y: int):
	node = _node(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
	_link(a, node, "A")
	_link(b, node, "B")
	_link(alpha, node, "Alpha")
	return node


def _unary(mat, cls, src, x: int, y: int, **props):
	node = _node(mat, cls, x, y, **props)
	_link(src, node)
	return node


def _mask(mat, src, x: int, y: int, r=False, g=False, b=False):
	return _unary(mat, unreal.MaterialExpressionComponentMask, src, x, y, r=r, g=g, b=b, a=False)


def _noise(mat, position, x: int, y: int, levels: int = 5):
	"""Fractal gradient noise 0..1 at a (pre-scaled) position."""
	node = _node(mat, unreal.MaterialExpressionNoise, x, y, scale=1.0, quality=1, levels=levels, output_min=0.0, output_max=1.0,
		noise_function=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_ALU, level_scale=2.2)
	_link(position, node, "World Position")  # the Position input (absolute origin)
	return node


def _new_master(name: str) -> unreal.Material:
	path = f"{MAT_DIR}/{name}"
	rb.delete_asset_if_exists(path)
	mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
	if mat is None:
		rb.fail(f"could not create {path}")
	return mat


def _finish(mat) -> None:
	MEL.layout_material_expressions(mat)
	MEL.recompile_material(mat)
	unreal.EditorAssetLibrary.save_loaded_asset(mat)


def _grime(mat, x: int, y: int):
	"""World-space fractal noise (NoiseScale) and the smoke darkening toward the ceiling (GrimeTop above GrimeStart)."""
	wp = _node(mat, unreal.MaterialExpressionWorldPosition, x, y)
	scaled = _op(mat, unreal.MaterialExpressionMultiply, wp, _scalar(mat, "NoiseScale", 0.02, x, y + 80), x + 200, y)
	noise = _noise(mat, scaled, x + 400, y)
	z = _mask(mat, wp, x + 200, y + 200, b=True)
	above = _op(mat, unreal.MaterialExpressionSubtract, z, _scalar(mat, "GrimeStart", 180.0, x + 200, y + 280), x + 400, y + 200)
	frac = _op(mat, unreal.MaterialExpressionDivide, above, _scalar(mat, "GrimeRange", 160.0, x + 400, y + 280), x + 600, y + 200)
	top = _unary(mat, unreal.MaterialExpressionSaturate, frac, x + 800, y + 200)
	smoke = _op(mat, unreal.MaterialExpressionMultiply, top, _scalar(mat, "GrimeTop", 0.0, x + 800, y + 280), x + 1000, y + 200)
	keep = _op(mat, unreal.MaterialExpressionSubtract, _const(mat, 1.0, x + 1000, y + 280), smoke, x + 1200, y + 200)
	return noise, keep


def master_surface() -> unreal.Material:
	"""M_Title_Surface: Color <-> Color2 and Roughness <-> Roughness2 by world noise, smoke darkening up high; Metallic."""
	mat = _new_master("M_Title_Surface")
	mat.set_editor_property("used_with_nanite", True)  # also the stand-in laminate on the table's (Nanite) baked meshes
	noise, keep = _grime(mat, -1600, 0)
	color = _lerp(mat, _vector(mat, "Color", (0.05, 0.04, 0.03), -800, -300), _vector(mat, "Color2", (0.03, 0.025, 0.02), -800, -150), noise, -500, -200)
	base = _op(mat, unreal.MaterialExpressionMultiply, color, keep, -300, -200)
	rough = _lerp(mat, _scalar(mat, "Roughness", 0.6, -800, 300), _scalar(mat, "Roughness2", 0.8, -800, 380), noise, -500, 300)
	MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
	MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
	MEL.connect_material_property(_scalar(mat, "Metallic", 0.0, -500, 500), "", unreal.MaterialProperty.MP_METALLIC)
	_finish(mat)
	return mat


def master_checker() -> unreal.Material:
	"""M_Title_Checker: 12-inch vinyl composition tiles in two colours (world XY), scuffs and worn traffic lanes by noise."""
	mat = _new_master("M_Title_Checker")
	wp = _node(mat, unreal.MaterialExpressionWorldPosition, -2200, -400)
	xy = _mask(mat, wp, -2000, -400, r=True, g=True)
	cells = _unary(mat, unreal.MaterialExpressionFloor,
		_op(mat, unreal.MaterialExpressionDivide, xy, _scalar(mat, "TileSize", 30.48, -2000, -300), -1800, -400), -1600, -400)
	parity = _op(mat, unreal.MaterialExpressionAdd, _mask(mat, cells, -1400, -450, r=True), _mask(mat, cells, -1400, -350, g=True), -1200, -400)
	alpha = _op(mat, unreal.MaterialExpressionMultiply,
		_unary(mat, unreal.MaterialExpressionFrac, _op(mat, unreal.MaterialExpressionMultiply, parity, _const(mat, 0.5, -1200, -300), -1000, -400), -800, -400),
		_const(mat, 2.0, -800, -300), -600, -400)
	# Per-tile shade (tiles are never quite the same colour) and the grout lines.
	tile_pos = _node(mat, unreal.MaterialExpressionAppendVector, -1100, -150) # float3 for the noise
	_link(_op(mat, unreal.MaterialExpressionMultiply, cells, _const(mat, 7.31, -1400, -150), -1250, -150), tile_pos, "A")
	_link(_const(mat, 0.5, -1250, -80), tile_pos, "B")
	tile_noise = _noise(mat, tile_pos, -1000, -150, levels=1)
	tile_shade = _lerp(mat, _const(mat, 0.85, -800, -200), _const(mat, 1.1, -800, -120), tile_noise, -600, -150)
	edge = _unary(mat, unreal.MaterialExpressionFrac, _op(mat, unreal.MaterialExpressionDivide, xy, _scalar(mat, "TileSize", 30.48, -2000, -300), -1800, 0), -1600, 0)
	dist = _op(mat, unreal.MaterialExpressionMin, _mask(mat, edge, -1400, -40, r=True), _mask(mat, edge, -1400, 40, g=True), -1200, 0)
	grout = _unary(mat, unreal.MaterialExpressionSaturate,
		_op(mat, unreal.MaterialExpressionMultiply, dist, _const(mat, 90.0, -1200, 80), -1000, 0), -800, 0)
	grout_shade = _lerp(mat, _const(mat, 0.45, -600, -40), _const(mat, 1.0, -600, 40), grout, -400, 0)
	noise, keep = _grime(mat, -2200, 300)
	tiles = _lerp(mat, _vector(mat, "ColorA", (0.012, 0.011, 0.010), -600, -600), _vector(mat, "ColorB", (0.09, 0.012, 0.010), -600, -500), alpha, -400, -500)
	shaded = _op(mat, unreal.MaterialExpressionMultiply,
		_op(mat, unreal.MaterialExpressionMultiply, tiles, tile_shade, -200, -400), grout_shade, 0, -300)
	scuff = _lerp(mat, _const(mat, 1.0, -200, 150), _scalar(mat, "ScuffDark", 0.55, -200, 230), noise, 0, 150)
	base = _op(mat, unreal.MaterialExpressionMultiply, shaded, scuff, 200, -200)
	rough = _lerp(mat, _scalar(mat, "Roughness", 0.32, 0, 350), _scalar(mat, "Roughness2", 0.75, 0, 430), noise, 200, 350)
	MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
	MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
	_finish(mat)
	return mat


def master_boards() -> unreal.Material:
	"""M_Title_Boards: vertical tongue-and-groove boards along a wall (X + Y), dark seams, per-board tone and grain streaks."""
	mat = _new_master("M_Title_Boards")
	wp = _node(mat, unreal.MaterialExpressionWorldPosition, -2200, 0)
	along = _op(mat, unreal.MaterialExpressionAdd, _mask(mat, wp, -2000, -60, r=True), _mask(mat, wp, -2000, 60, g=True), -1800, 0)
	u = _op(mat, unreal.MaterialExpressionDivide, along, _scalar(mat, "BoardWidth", 9.5, -1800, 100), -1600, 0)
	board = _unary(mat, unreal.MaterialExpressionFloor, u, -1400, 0)
	seam_dist = _unary(mat, unreal.MaterialExpressionFrac, u, -1400, 150)
	seam = _unary(mat, unreal.MaterialExpressionSaturate,
		_op(mat, unreal.MaterialExpressionMultiply, seam_dist, _const(mat, 25.0, -1400, 250), -1200, 150), -1000, 150)
	z = _mask(mat, wp, -1800, 300, b=True)
	grain_pos = _node(mat, unreal.MaterialExpressionAppendVector, -1000, 350)
	_link(_op(mat, unreal.MaterialExpressionMultiply, board, _const(mat, 3.7, -1200, 300), -1100, 300), grain_pos, "A")
	_link(_op(mat, unreal.MaterialExpressionMultiply, z, _const(mat, 0.012, -1200, 420), -1100, 420), grain_pos, "B")
	grain_pos3 = _node(mat, unreal.MaterialExpressionAppendVector, -850, 350)
	_link(grain_pos, grain_pos3, "A")
	_link(_const(mat, 0.0, -1000, 450), grain_pos3, "B")
	grain = _noise(mat, grain_pos3, -700, 350, levels=4)
	noise, keep = _grime(mat, -2200, 700)
	tone = _lerp(mat, _vector(mat, "Color", (0.045, 0.022, 0.011), -600, -300), _vector(mat, "Color2", (0.075, 0.040, 0.020), -600, -150), grain, -400, -200)
	seamed = _op(mat, unreal.MaterialExpressionMultiply, tone,
		_lerp(mat, _scalar(mat, "SeamDark", 0.35, -400, 0), _const(mat, 1.0, -400, 80), seam, -200, 0), -100, -150)
	worn = _op(mat, unreal.MaterialExpressionMultiply, seamed,
		_lerp(mat, _const(mat, 1.0, -200, 200), _const(mat, 0.7, -200, 280), noise, 0, 200), 100, -100)
	base = _op(mat, unreal.MaterialExpressionMultiply, worn, keep, 300, -100)
	rough = _lerp(mat, _scalar(mat, "Roughness", 0.42, 0, 450), _scalar(mat, "Roughness2", 0.7, 0, 530), noise, 300, 450)
	MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
	MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
	_finish(mat)
	return mat


def master_glass() -> unreal.Material:
	"""M_Title_Glass: opaque stand-in for bottle glass and beer (smooth, a little emissive: the back-bar light through it)."""
	mat = _new_master("M_Title_Glass")
	color = _vector(mat, "Color", (0.2, 0.08, 0.02), -600, 0)
	MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
	MEL.connect_material_property(_scalar(mat, "Roughness", 0.06, -600, 200), "", unreal.MaterialProperty.MP_ROUGHNESS)
	MEL.connect_material_property(_const(mat, 0.9, -600, 300), "", unreal.MaterialProperty.MP_SPECULAR)
	glow = _op(mat, unreal.MaterialExpressionMultiply, color, _scalar(mat, "Glow", 0.5, -600, 100), -300, 100)
	MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
	_finish(mat)
	return mat


def master_emissive() -> unreal.Material:
	"""M_Title_Emissive: unlit, EmissiveColor = Color * Intensity (bulb openings, neon tubes, LED strips, the lightbox)."""
	mat = _new_master("M_Title_Emissive")
	mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
	glow = _op(mat, unreal.MaterialExpressionMultiply, _vector(mat, "Color", (1.0, 0.62, 0.30), -400, 0), _scalar(mat, "Intensity", 20.0, -400, 200), -150, 80)
	MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
	_finish(mat)
	return mat


def instance(name: str, parent: unreal.Material, vectors: dict | None = None, scalars: dict | None = None) -> unreal.MaterialInstanceConstant:
	path = f"{MAT_DIR}/MI_Title_{name}"
	if unreal.EditorAssetLibrary.does_asset_exist(path):
		mi = unreal.load_asset(path)
	else:
		mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(f"MI_Title_{name}", MAT_DIR, unreal.MaterialInstanceConstant,
			unreal.MaterialInstanceConstantFactoryNew())
		if mi is None:
			rb.fail(f"could not create {path}")
	MEL.set_material_instance_parent(mi, parent)
	MEL.clear_all_material_instance_parameters(mi)
	for key, value in (vectors or {}).items():
		MEL.set_material_instance_vector_parameter_value(mi, key, unreal.LinearColor(value[0], value[1], value[2], 1.0))
	for key, value in (scalars or {}).items():
		MEL.set_material_instance_scalar_parameter_value(mi, key, value)
	MEL.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi)
	return mi


def make_materials() -> dict:
	surface, checker, boards, glass, emissive = master_surface(), master_checker(), master_boards(), master_glass(), master_emissive()
	m = {}
	# Room
	m["Floor"] = instance("Floor", checker, {"ColorA": (0.010, 0.009, 0.008), "ColorB": (0.085, 0.011, 0.009)}, {"NoiseScale": 0.05, "ScuffDark": 0.86, "Roughness": 0.42, "Roughness2": 0.62})
	m["Panel"] = instance("Panel", boards, {"Color": (0.040, 0.019, 0.009), "Color2": (0.070, 0.036, 0.017)}, {"NoiseScale": 0.015})
	m["Plaster"] = instance("Plaster", surface, {"Color": (0.105, 0.085, 0.058), "Color2": (0.080, 0.062, 0.040)},
		{"Roughness": 0.8, "Roughness2": 0.95, "NoiseScale": 0.008, "GrimeTop": 0.55, "GrimeStart": 150.0, "GrimeRange": 170.0})
	m["Ceiling"] = instance("Ceiling", surface, {"Color": (0.050, 0.042, 0.032), "Color2": (0.028, 0.022, 0.016)},
		{"Roughness": 0.9, "Roughness2": 0.95, "NoiseScale": 0.01})
	m["Trim"] = instance("Trim", surface, {"Color": (0.014, 0.009, 0.006), "Color2": (0.022, 0.013, 0.008)}, {"Roughness": 0.35, "Roughness2": 0.6})
	m["Wood"] = instance("Wood", surface, {"Color": (0.060, 0.028, 0.012), "Color2": (0.035, 0.016, 0.007)},
		{"Roughness": 0.22, "Roughness2": 0.45, "NoiseScale": 0.05})
	m["Slate"] = instance("Slate", surface, {"Color": (0.020, 0.022, 0.021), "Color2": (0.045, 0.047, 0.045)}, {"Roughness": 0.85, "Roughness2": 0.95, "NoiseScale": 0.06})
	m["Chalk"] = instance("Chalk", surface, {"Color": (0.55, 0.55, 0.52), "Color2": (0.35, 0.35, 0.33)}, {"Roughness": 0.9, "Roughness2": 0.95, "NoiseScale": 0.3})
	# Fixtures
	m["Shade"] = instance("Shade", surface, {"Color": (0.010, 0.045, 0.022), "Color2": (0.008, 0.032, 0.016)}, {"Roughness": 0.25, "Roughness2": 0.4, "NoiseScale": 0.1})
	m["Metal"] = instance("Metal", surface, {"Color": (0.030, 0.028, 0.026), "Color2": (0.018, 0.017, 0.016)}, {"Roughness": 0.35, "Roughness2": 0.55, "Metallic": 1.0})
	m["Chrome"] = instance("Chrome", surface, {"Color": (0.55, 0.55, 0.56), "Color2": (0.40, 0.40, 0.41)}, {"Roughness": 0.12, "Roughness2": 0.3, "Metallic": 1.0})
	m["Mirror"] = instance("Mirror", surface, {"Color": (0.14, 0.14, 0.13), "Color2": (0.08, 0.075, 0.07)}, {"Roughness": 0.03, "Roughness2": 0.25, "Metallic": 1.0, "NoiseScale": 0.03})
	m["Vinyl"] = instance("Vinyl", surface, {"Color": (0.16, 0.012, 0.010), "Color2": (0.09, 0.008, 0.006)}, {"Roughness": 0.3, "Roughness2": 0.55, "NoiseScale": 0.08})
	m["Chalkcube"] = instance("Chalkcube", surface, {"Color": (0.02, 0.10, 0.35), "Color2": (0.015, 0.07, 0.25)}, {"Roughness": 0.85, "Roughness2": 0.95})
	# Stand-in for the coin-op cabinet's dark walnut laminate until M2-L's MI_RbRail_BlackLaminate exists (build_table).
	m["Laminate"] = instance("Laminate", surface, {"Color": (0.021, 0.0105, 0.0058), "Color2": (0.018, 0.009, 0.005)},
		{"Roughness": 0.3, "Roughness2": 0.4, "NoiseScale": 0.03})
	# Paper: posters and flyers (sun-faded colours; lit by the room only).
	m["PaperCream"] = instance("PaperCream", surface, {"Color": (0.42, 0.36, 0.25), "Color2": (0.30, 0.25, 0.17)}, {"Roughness": 0.9, "Roughness2": 0.95, "NoiseScale": 0.2})
	m["PaperRed"] = instance("PaperRed", surface, {"Color": (0.30, 0.035, 0.025), "Color2": (0.20, 0.03, 0.02)}, {"Roughness": 0.85, "Roughness2": 0.95, "NoiseScale": 0.2})
	m["PaperYellow"] = instance("PaperYellow", surface, {"Color": (0.45, 0.32, 0.05), "Color2": (0.32, 0.22, 0.04)}, {"Roughness": 0.85, "Roughness2": 0.95, "NoiseScale": 0.2})
	m["PaperBlack"] = instance("PaperBlack", surface, {"Color": (0.02, 0.02, 0.022), "Color2": (0.03, 0.03, 0.03)}, {"Roughness": 0.6, "Roughness2": 0.8, "NoiseScale": 0.2})
	m["Cork"] = instance("Cork", surface, {"Color": (0.16, 0.09, 0.045), "Color2": (0.10, 0.055, 0.03)}, {"Roughness": 0.9, "Roughness2": 0.97, "NoiseScale": 0.5})
	m["Rubber"] = instance("Rubber", surface, {"Color": (0.012, 0.012, 0.012), "Color2": (0.02, 0.02, 0.02)}, {"Roughness": 0.7, "Roughness2": 0.85})
	# Glass
	m["GlassAmber"] = instance("GlassAmber", glass, {"Color": (0.20, 0.07, 0.012)}, {"Glow": 0.9, "Roughness": 0.05})
	m["GlassGreen"] = instance("GlassGreen", glass, {"Color": (0.025, 0.12, 0.03)}, {"Glow": 0.9, "Roughness": 0.05})
	m["GlassClear"] = instance("GlassClear", glass, {"Color": (0.35, 0.33, 0.28)}, {"Glow": 0.35, "Roughness": 0.04})
	m["GlassBlue"] = instance("GlassBlue", glass, {"Color": (0.02, 0.04, 0.16)}, {"Glow": 0.9, "Roughness": 0.05})
	m["Beer"] = instance("Beer", glass, {"Color": (0.30, 0.12, 0.015)}, {"Glow": 0.25, "Roughness": 0.08})
	m["Foam"] = instance("Foam", surface, {"Color": (0.55, 0.50, 0.40), "Color2": (0.45, 0.40, 0.32)}, {"Roughness": 0.7, "Roughness2": 0.9, "NoiseScale": 0.4})
	# Light sources
	m["Bulb"] = instance("Bulb", emissive, {"Color": (1.0, 0.72, 0.42)}, {"Intensity": 45.0})
	m["Neon"] = instance("Neon", emissive, {"Color": (1.0, 0.10, 0.045)}, {"Intensity": 22.0})
	m["NeonGreen"] = instance("NeonGreen", emissive, {"Color": (0.10, 1.0, 0.25)}, {"Intensity": 30.0})
	m["Led"] = instance("Led", emissive, {"Color": (1.0, 0.62, 0.30)}, {"Intensity": 14.0})
	m["BoxBlue"] = instance("BoxBlue", emissive, {"Color": (0.10, 0.28, 1.0)}, {"Intensity": 5.0})
	m["BoxCream"] = instance("BoxCream", emissive, {"Color": (1.0, 0.85, 0.62)}, {"Intensity": 7.0})
	m["BoxRed"] = instance("BoxRed", emissive, {"Color": (1.0, 0.08, 0.05)}, {"Intensity": 5.0})
	return m


def remove_stale_instances(keep: dict) -> None:
	"""Drops material instances of earlier versions of this script (MI_Title_* not made by this run)."""
	names = {f"MI_Title_{name}" for name in keep} | set(MASTER_NAMES)
	for path in unreal.EditorAssetLibrary.list_assets(MAT_DIR, recursive=False, include_folder=False):
		asset_name = path.split("/")[-1].split(".")[0]
		if asset_name not in names:
			rb.delete_asset_if_exists(path.split(".")[0])


# ---------------------------------------------------------------------------------------------------------------------------
# Geometry helpers (engine basic shapes: 100 cm, pivot at the centre; cone / cylinder along +Z)
# ---------------------------------------------------------------------------------------------------------------------------

def box(label: str, center, size, material, rotation=(0.0, 0.0, 0.0), cast_shadow: bool = True) -> unreal.Actor:
	return shape("Cube", label, center, (size[0] / 100.0, size[1] / 100.0, size[2] / 100.0), material, rotation, cast_shadow)


def shape(mesh: str, label: str, center, scale, material, rotation=(0.0, 0.0, 0.0), cast_shadow: bool = True) -> unreal.Actor:
	actor = rb.spawn_mesh(f"/Engine/BasicShapes/{mesh}", center, scale, rotation, label)
	comp = actor.static_mesh_component
	comp.set_material(0, material)
	comp.set_editor_property("cast_shadow", cast_shadow)
	return actor


def cylinder(label: str, base, radius: float, height: float, material, cast_shadow: bool = True) -> unreal.Actor:
	"""An upright cylinder standing on base (x, y, z of its bottom)."""
	return shape("Cylinder", label, (base[0], base[1], base[2] + 0.5 * height), (radius / 50.0, radius / 50.0, height / 100.0), material, cast_shadow=cast_shadow)


def tube(label: str, a, b, radius: float, material) -> unreal.Actor:
	"""A thin cylinder from point a to point b (neon tubes)."""
	d = unreal.Vector(b[0] - a[0], b[1] - a[1], b[2] - a[2])
	length = d.length()
	rot = unreal.MathLibrary.make_rot_from_z(d)
	center = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, (a[2] + b[2]) * 0.5)
	actor = shape("Cylinder", label, center, (radius / 50.0, radius / 50.0, length / 100.0), material, (rot.pitch, rot.yaw, rot.roll), cast_shadow=False)
	comp = actor.static_mesh_component
	comp.set_editor_property("affect_distance_field_lighting", False)
	return actor


def bottle(label: str, base, height: float, radius: float, material, cast_shadow: bool = True) -> None:
	"""Body, shoulder and neck (a wine / liquor bottle when tall, a longneck when slim)."""
	body = 0.62 * height
	shoulder = 0.14 * height
	neck = height - body - shoulder
	cylinder(f"{label}_Body", base, radius, body, material, cast_shadow)
	shape("Cone", f"{label}_Shoulder", (base[0], base[1], base[2] + body + 0.5 * shoulder * 1.6),
		(radius / 50.0, radius / 50.0, shoulder * 1.6 / 100.0), material, cast_shadow=cast_shadow)
	cylinder(f"{label}_Neck", (base[0], base[1], base[2] + body + shoulder * 0.6), radius * 0.34, neck + shoulder * 0.4, material, cast_shadow)


def spot(label: str, location, rotation, lumens: float, kelvin: float, outer_deg: float, inner_deg: float, radius_cm: float,
		scattering: float = 1.0, shadows: bool = True) -> unreal.Actor:
	actor = rb.spawn(unreal.SpotLight, location, rotation, label)
	light = actor.spot_light_component
	light.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	light.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	light.set_editor_property("intensity", lumens)
	light.set_editor_property("use_temperature", True)
	light.set_editor_property("temperature", kelvin)
	light.set_editor_property("outer_cone_angle", outer_deg)
	light.set_editor_property("inner_cone_angle", inner_deg)
	light.set_editor_property("source_radius", radius_cm)
	light.set_editor_property("soft_source_radius", radius_cm)
	light.set_editor_property("attenuation_radius", 900.0)
	light.set_editor_property("volumetric_scattering_intensity", scattering)
	light.set_editor_property("cast_shadows", shadows)
	light.set_editor_property("cast_volumetric_shadow", scattering > 0.0)
	return actor


def rect(label: str, location, rotation, lumens: float, color, width: float, height: float, scattering: float = 0.0,
		shadows: bool = True, attenuation: float = 1200.0) -> unreal.Actor:
	actor = rb.spawn(unreal.RectLight, location, rotation, label)
	light = actor.rect_light_component
	light.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	light.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	light.set_editor_property("intensity", lumens)
	light.set_editor_property("light_color", unreal.Color(r=int(color[0] * 255), g=int(color[1] * 255), b=int(color[2] * 255), a=255))
	light.set_editor_property("source_width", width)
	light.set_editor_property("source_height", height)
	light.set_editor_property("attenuation_radius", attenuation)
	light.set_editor_property("volumetric_scattering_intensity", scattering)
	light.set_editor_property("cast_shadows", shadows)
	light.set_editor_property("cast_volumetric_shadow", shadows and scattering > 0.0)
	return actor


# Neon stroke font: polylines on a 2 x 3 grid (x right, y up), rounded corners as chamfers.
NEON_GLYPHS = {
	"C": [[(2.0, 0.5), (1.5, 0.0), (0.5, 0.0), (0.0, 0.5), (0.0, 2.5), (0.5, 3.0), (1.5, 3.0), (2.0, 2.5)]],
	"O": [[(0.5, 0.0), (1.5, 0.0), (2.0, 0.5), (2.0, 2.5), (1.5, 3.0), (0.5, 3.0), (0.0, 2.5), (0.0, 0.5), (0.5, 0.0)]],
	"L": [[(0.0, 3.0), (0.0, 0.0), (1.8, 0.0)]],
	"D": [[(0.0, 0.0), (1.3, 0.0), (2.0, 0.7), (2.0, 2.3), (1.3, 3.0), (0.0, 3.0), (0.0, 0.0)]],
	"B": [[(0.0, 0.0), (0.0, 3.0), (1.4, 3.0), (1.9, 2.55), (1.9, 1.95), (1.4, 1.5), (0.0, 1.5)],
		[(1.4, 1.5), (2.0, 1.05), (2.0, 0.45), (1.5, 0.0), (0.0, 0.0)]],
	"E": [[(1.9, 3.0), (0.0, 3.0), (0.0, 0.0), (1.9, 0.0)], [(0.0, 1.5), (1.4, 1.5)]],
	"R": [[(0.0, 0.0), (0.0, 3.0), (1.5, 3.0), (2.0, 2.5), (2.0, 2.0), (1.5, 1.5), (0.0, 1.5)], [(0.9, 1.5), (2.0, 0.0)]],
}


def neon_text(label: str, text: str, origin, unit: float, material, facing: str = "-X") -> None:
	"""Tube letters on a wall: origin = bottom left of the first letter; the text runs along +Y (a sign on a wall facing -X)
	or along +X (facing -Y... here +X runs left to right for a viewer looking toward +Y)."""
	radius = 0.55
	advance = 2.0 * unit + 0.9 * unit
	cursor = 0.0
	for index, char in enumerate(text):
		if char == " ":
			cursor += 1.4 * unit
			continue
		for stroke_index, stroke in enumerate(NEON_GLYPHS[char]):
			points = []
			for gx, gy in stroke:
				along = cursor + gx * unit
				up = gy * unit
				if facing == "-X":
					points.append((origin[0], origin[1] + along, origin[2] + up))
				else:
					points.append((origin[0] + along, origin[1], origin[2] + up))
			for seg in range(len(points) - 1):
				tube(f"{label}_{index}_{stroke_index}_{seg}", points[seg], points[seg + 1], radius, material)
			for point_index, point in enumerate(points):
				shape("Sphere", f"{label}_{index}_{stroke_index}_J{point_index}", point, (radius / 50.0,) * 3, material, cast_shadow=False)
		cursor += advance


def neon_width(text: str, unit: float) -> float:
	advance = 2.0 * unit + 0.9 * unit
	width = 0.0
	for char in text:
		width += 1.4 * unit if char == " " else advance
	return width - 0.9 * unit


# ---------------------------------------------------------------------------------------------------------------------------
# The scene
# ---------------------------------------------------------------------------------------------------------------------------

def build_room(m: dict) -> None:
	width_x = WALL_X - FRONT_X
	width_y = WALL_Y - LEFT_Y
	cx = 0.5 * (WALL_X + FRONT_X)
	cy = 0.5 * (WALL_Y + LEFT_Y)
	box("Floor", (cx, cy, -1.0), (width_x, width_y, 2.0), m["Floor"])
	box("Ceiling", (cx, cy, CEILING_Z + 2.0), (width_x, width_y, 4.0), m["Ceiling"])
	# Walls: boards to the chair rail (110 cm), plaster above; the back wall, the side wall on the right, the far left wall and
	# the wall behind the camera.
	walls = [
		("Back", (WALL_X + 3.0, cy, 0.0), (6.0, width_y), "-X"),
		("Side", (cx, WALL_Y + 3.0, 0.0), (width_x, 6.0), "-Y"),
		("Left", (cx, LEFT_Y - 3.0, 0.0), (width_x, 6.0), "+Y"),
		("Front", (FRONT_X - 3.0, cy, 0.0), (6.0, width_y), "+X"),
	]
	for name, (x, y, _z), (sx, sy), facing in walls:
		box(f"{name}WallPanel", (x, y, 55.0), (sx, sy, 110.0), m["Panel"])
		box(f"{name}WallUpper", (x, y, 110.0 + 0.5 * (CEILING_Z - 110.0)), (sx, sy, CEILING_Z - 110.0), m["Plaster"])
		inset = {"-X": (-4.0, 0.0), "-Y": (0.0, -4.0), "+Y": (0.0, 4.0), "+X": (4.0, 0.0)}[facing]
		rail = (sx if facing in ("-Y", "+Y") else 4.0, sy if facing in ("-X", "+X") else 4.0)
		box(f"{name}WallRail", (x + inset[0], y + inset[1], 111.0), (rail[0], rail[1], 6.0), m["Trim"])
		box(f"{name}WallBase", (x + inset[0] * 0.75, y + inset[1] * 0.75, 6.0), (rail[0], rail[1], 12.0), m["Trim"])


def build_wall_details(m: dict) -> None:
	# The house chalkboard on the back wall (scores, "NO JUMP SHOTS" scrawled as chalk strokes).
	board_y, board_z = 70.0, 172.0
	box("Chalkboard", (WALL_X - 1.0, board_y, board_z), (2.0, 96.0, 64.0), m["Slate"])
	for dz in (-33.0, 33.0):
		box(f"ChalkboardFrameH{int(dz)}", (WALL_X - 2.0, board_y, board_z + dz), (3.0, 102.0, 4.0), m["Wood"])
	for dy in (-49.0, 49.0):
		box(f"ChalkboardFrameV{int(dy)}", (WALL_X - 2.0, board_y + dy, board_z), (3.0, 4.0, 70.0), m["Wood"])
	for index, (dy, dz, length) in enumerate([(-30.0, 16.0, 30.0), (8.0, 16.0, 18.0), (-30.0, 2.0, 22.0), (-30.0, -12.0, 26.0), (4.0, -12.0, 12.0),
			(-36.0, -24.0, 8.0), (-24.0, -24.0, 8.0)]):
		box(f"ChalkLine{index}", (WALL_X - 2.2, board_y + dy + 0.5 * length, board_z + dz), (0.4, length, 1.6), m["Chalk"], cast_shadow=False)
	# The wall cue rack with the house cues on the side wall.
	rack_y = WALL_Y - 3.0
	box("CueRackTop", (130.0, rack_y - 1.0, 146.0), (150.0, 8.0, 7.0), m["Wood"])
	box("CueRackBottom", (130.0, rack_y - 2.0, 18.0), (150.0, 10.0, 5.0), m["Wood"])
	cue_mesh = "/Game/Generated/Cues/SM_Cue_House19oz"
	if unreal.EditorAssetLibrary.does_asset_exist(cue_mesh):
		for index in range(6):
			# The cue mesh: origin = tip, +X from the butt to the tip (RbCueMeshBuilder.h). Butt in the bottom rack, the shaft
			# through the top rack's slot, leaning 2 deg toward the wall: pitch +88, yaw 90 (+X up and toward +Y).
			x = 72.0 + index * 23.0
			butt = (x, rack_y - 4.0, 20.5)
			lean = math.radians(88.0)
			tip = (x, butt[1] + 147.3 * math.cos(lean), butt[2] + 147.3 * math.sin(lean))
			rb.spawn_mesh(cue_mesh, tip, (1.0, 1.0, 1.0), (88.0, 90.0, 0.0), f"HouseCue{index}")
	# A framed print (dark, behind glass) above the rack's left end.
	box("Frame0", (40.0, WALL_Y - 1.5, 222.0), (44.0, 3.0, 32.0), m["Trim"])
	box("Print0", (40.0, WALL_Y - 3.2, 222.0), (38.0, 0.6, 26.0), m["Mirror"])


def build_bar(m: dict) -> None:
	"""The bar in the back left: counter, stools, the mirrored back bar with three shelves of backlit bottles."""
	y0, y1 = LEFT_Y + 10.0, -95.0
	cy = 0.5 * (y0 + y1)
	length = y1 - y0
	# Counter (front at x = 222): panelled body, a dark varnished top with a rounded-looking nosing, a brass-ish foot rail.
	box("BarBody", (254.0, cy, 51.0), (60.0, length, 102.0), m["Panel"])
	box("BarTop", (250.0, cy, 104.5), (72.0, length + 6.0, 5.0), m["Wood"])
	box("BarKick", (226.0, cy, 6.0), (4.0, length, 12.0), m["Trim"])
	box("BarFootRail", (212.0, cy, 20.0), (4.0, length, 4.0), m["Chrome"])
	# Stools: red vinyl seats on chrome posts.
	for index, y in enumerate((cy - 110.0, cy - 20.0, cy + 70.0)):
		cylinder(f"Stool{index}_Base", (190.0, y, 0.0), 21.0, 2.5, m["Chrome"])
		cylinder(f"Stool{index}_Post", (190.0, y, 2.5), 2.6, 70.0, m["Chrome"])
		cylinder(f"Stool{index}_Ring", (190.0, y, 26.0), 17.0, 1.6, m["Chrome"])
		cylinder(f"Stool{index}_Seat", (190.0, y, 72.0), 19.0, 9.0, m["Vinyl"])
	# Back bar: mirror, three shelves with warm LED strips under each, bottles.
	box("BackBarMirror", (WALL_X - 1.2, cy, 172.0), (1.0, length - 20.0, 120.0), m["Mirror"])
	box("BackBarCabinet", (WALL_X - 22.0, cy, 55.0), (44.0, length, 110.0), m["Panel"])
	box("BackBarTop", (WALL_X - 23.0, cy, 111.5), (48.0, length + 4.0, 3.0), m["Wood"])
	materials = [m["GlassAmber"], m["GlassGreen"], m["GlassClear"], m["GlassAmber"], m["GlassBlue"], m["GlassClear"], m["GlassAmber"]]
	shelves = [(113.0, 0), (145.0, 1), (177.0, 2)]
	for shelf_z, row in shelves:
		if row > 0:
			box(f"Shelf{row}", (WALL_X - 11.0, cy, shelf_z - 1.2), (20.0, length - 30.0, 2.4), m["Wood"])
		box(f"ShelfLed{row}", (WALL_X - 2.5, cy, shelf_z + 0.6), (1.0, length - 34.0, 0.8), m["Led"], cast_shadow=False)
		count = 13
		for index in range(count):
			y = y0 + 22.0 + index * (length - 44.0) / (count - 1) + (3.0 if (index + row) % 2 else -2.0)
			tall = [30.0, 27.0, 33.0, 24.0, 29.0][(index * 3 + row) % 5]
			fat = [3.8, 3.4, 4.2, 3.2][(index + row * 2) % 4]
			bottle(f"Bottle{row}_{index}", (WALL_X - 11.0 + ((index * 7) % 5) - 2.0, y, shelf_z), tall, fat, materials[(index * 5 + row * 3) % len(materials)],
				cast_shadow=False)
	# A few bottles and glasses on the counter (last call).
	bottle("CounterBottle0", (238.0, cy + 40.0, 107.0), 24.0, 3.2, m["GlassGreen"])
	bottle("CounterBottle1", (244.0, cy + 58.0, 107.0), 24.0, 3.2, m["GlassAmber"])
	cylinder("CounterGlass0", (236.0, cy - 30.0, 107.0), 4.2, 14.0, m["Beer"])
	cylinder("CounterGlass0Foam", (236.0, cy - 30.0, 121.0), 4.25, 1.2, m["Foam"])
	# Light: the LED strips wash the bottles from behind (warm), a low downlight over the counter.
	rect("BackBarGlow", (WALL_X - 6.0, cy, 150.0), (0.0, 180.0, 0.0), 700.0, (1.0, 0.66, 0.36), length - 40.0, 70.0, scattering=0.25, shadows=False,
		attenuation=500.0)
	spot("BarDownlight", (250.0, cy, CEILING_Z - 2.0), (-90.0, 0.0, 0.0), 260.0, 2400.0, 50.0, 25.0, 4.0, scattering=0.6)


def build_signs(m: dict) -> None:
	# A red "COLD BEER" neon on the back wall, right of the chalkboard just above the chair rail (below the lamp in the view).
	text, unit = "COLD BEER", 5.6
	width = neon_width(text, unit)
	center_y, base_z = 196.0, 124.0
	neon_text("NeonColdBeer", text, (WALL_X - 5.0, center_y - 0.5 * width, base_z), unit, m["Neon"], facing="-X")
	box("NeonBacking", (WALL_X - 1.2, center_y, base_z + 1.5 * unit), (0.6, width + 16.0, 3.0 * unit + 14.0), m["Trim"], cast_shadow=False)
	rect("NeonSpill", (WALL_X - 16.0, center_y, base_z + 1.5 * unit), (0.0, 180.0, 0.0), 60.0, (1.0, 0.10, 0.05), width, 3.0 * unit, scattering=0.6,
		shadows=False, attenuation=420.0)
	build_beer_clock(m, 262.0, 180.0)


def build_beer_clock(m: dict, cx: float, cz: float) -> None:
	"""The lit beer clock of every American bar (a fictional brand: no lettering) on the side wall near the back corner: a blue
	lit rim, a cream face with a red banner, dark hands at 1:50 (last call). The wall faces -Y; a viewer facing it has +Z up and
	-X to the right, so a clock angle a (clockwise from 12) points along (-sin a, 0, cos a)."""
	face_y = WALL_Y - 9.0
	disc = lambda label, y, radius, material: shape("Cylinder", label, (cx, y, cz), (radius / 50.0, radius / 50.0, 0.004), material,
		(0.0, 0.0, 90.0), cast_shadow=False)
	shape("Cylinder", "ClockCase", (cx, WALL_Y - 4.5, cz), (21.0 / 50.0, 21.0 / 50.0, 0.09), m["Trim"], (0.0, 0.0, 90.0))
	disc("ClockRim", face_y, 20.0, m["BoxBlue"])
	disc("ClockFace", face_y - 0.2, 16.5, m["BoxCream"])
	box("ClockBanner", (cx, face_y - 0.45, cz - 7.5), (19.0, 0.3, 4.6), m["BoxRed"], cast_shadow=False)

	def at(angle_deg: float, radius: float, y: float):
		a = math.radians(angle_deg)
		return (cx - math.sin(a) * radius, y, cz + math.cos(a) * radius)

	for hour in range(12):
		inner = 13.2 if hour % 3 else 12.0
		tube(f"ClockTick{hour}", at(hour * 30.0, inner, face_y - 0.5), at(hour * 30.0, 15.2, face_y - 0.5), 0.45 if hour % 3 else 0.7, m["PaperBlack"])
	minute, hour = 50.0, 1.0 + 50.0 / 60.0
	tube("ClockHourHand", at(hour * 30.0 + 180.0, 2.0, face_y - 0.8), at(hour * 30.0, 8.5, face_y - 0.8), 0.8, m["PaperBlack"])
	tube("ClockMinuteHand", at(minute * 6.0 + 180.0, 2.5, face_y - 1.0), at(minute * 6.0, 13.5, face_y - 1.0), 0.55, m["PaperBlack"])
	shape("Sphere", "ClockPin", (cx, face_y - 1.1, cz), (0.02, 0.02, 0.02), m["Chrome"], cast_shadow=False)
	# Its light: the lit face washes the wall and the back corner (the blue of the rim dominates what spills).
	rect("ClockSpill", (cx, WALL_Y - 14.0, cz), (0.0, -90.0, 0.0), 110.0, (0.45, 0.6, 1.0), 34.0, 34.0, scattering=0.3, shadows=False,
		attenuation=450.0)


# ERbTablePart indices (Core/RbTypes.h): Bed, CushionCloth, RailCaps, Apron, PocketLiners, Sights, Legs.
PART_BED, PART_CUSHION, PART_RAILCAPS, PART_APRON, PART_LEGS = 0, 1, 2, 3, 6
DIVEBAR_CLOTH = "/Game/Generated/Materials/MI_RbCloth_BarGreen"        # RbAssetPaths::MatClothBarGreen (M2-L)
DIVEBAR_RAIL = "/Game/Generated/Materials/MI_RbRail_BlackLaminate"     # RbAssetPaths::MatRailBlackLaminate (M2-L)
M1_GREEN_CLOTH = "/Game/Generated/Materials/MI_RbCloth_Green"           # M1's green cloth instance
M1_PART_DEFAULTS = [f"/Game/Generated/Materials/{name}" for name in (
	"M_RbCloth", "M_RbCloth", "M_RbRailWood", "M_RbRailWood", "M_RbPocketLiner", "M_RbSight", "M_RbRailWood")]


def _material(path: str):
	return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None


def set_table_materials(table, m: dict) -> None:
	"""The dive bar's table look: M2-L's bar-green cloth and black laminate once they exist (the integration re-runs this
	script), until then M1's green cloth and a dark laminate stand-in. Python reads the soft pointers of PartMaterials back as
	None (their defaults are package paths), so the array is rebuilt from ARbTable's M1 defaults (RbTable.cpp) - only while
	it still has M1's seven parts; with more parts (M2-L appended some) the table keeps its own materials."""
	parts = list(table.get_editor_property("part_materials"))
	if len(parts) != len(M1_PART_DEFAULTS):
		rb.log(f"title table: {len(parts)} parts (M1 had {len(M1_PART_DEFAULTS)}): keeping the table's own materials")
		return
	cloth = _material(DIVEBAR_CLOTH) or _material(M1_GREEN_CLOTH)
	body = _material(DIVEBAR_RAIL) or m["Laminate"]
	overrides = {PART_BED: cloth, PART_CUSHION: cloth, PART_RAILCAPS: body, PART_APRON: body, PART_LEGS: body}
	result = []
	for index, default in enumerate(M1_PART_DEFAULTS):
		material = overrides.get(index) or _material(default)
		if material is None:
			rb.log(f"title table: part {index} has no material ({default}): keeping the table's own materials")
			return
		result.append(material)
	table.set_editor_property("part_materials", result)
	rb.log("title table materials: " + ", ".join(p.get_name() for p in result))


def stool(label: str, x: float, y: float, m: dict) -> None:
	cylinder(f"{label}_Base", (x, y, 0.0), 21.0, 2.5, m["Chrome"])
	cylinder(f"{label}_Post", (x, y, 2.5), 2.6, 70.0, m["Chrome"])
	cylinder(f"{label}_Ring", (x, y, 26.0), 17.0, 1.6, m["Chrome"])
	cylinder(f"{label}_Seat", (x, y, 72.0), 19.0, 9.0, m["Vinyl"])


def build_clutter(m: dict) -> None:
	"""What makes the corner lived-in: the drink rail under the neon with the players' bottles, a stool pulled up to the table,
	the rack on its hook, the flyer board, a green EXIT box over the back corner."""
	# Drink rail on the back wall under COLD BEER (the chair rail's height), with longnecks, a pint and a coaster stack.
	rail_x = WALL_X - 8.0
	box("DrinkRail", (rail_x, 200.0, 96.0), (16.0, 170.0, 3.0), m["Wood"])
	for index, y in enumerate((130.0, 270.0)):
		box(f"DrinkRailBracket{index}", (WALL_X - 3.0, y, 88.0), (6.0, 2.0, 14.0), m["Metal"])
	for index, (y, material, height) in enumerate([(142.0, m["GlassAmber"], 23.0), (151.0, m["GlassAmber"], 23.0), (205.0, m["GlassGreen"], 23.0),
			(258.0, m["GlassAmber"], 23.0)]):
		bottle(f"RailBottle{index}", (rail_x + (index % 2) * 3.0 - 1.5, y, 97.5), height, 3.1, material)
	cylinder("RailPint", (rail_x - 2.0, 228.0, 97.5), 4.3, 15.0, m["Beer"])
	cylinder("RailPintFoam", (rail_x - 2.0, 228.0, 112.5), 4.35, 1.0, m["Foam"])
	cylinder("RailCoasters", (rail_x - 3.0, 176.0, 97.5), 5.0, 1.4, m["PaperCream"])
	# A stool pulled up to the far corner of the table, a second one against the side wall.
	stool("TableStool0", 168.0, 176.0, m)
	stool("TableStool1", 250.0, 262.0, m)
	# The wooden triangle on its hook beside the cue rack (three rails, the apex up).
	# Equilateral, centroid (cx, cz): the base rail flat, the side rails pitched +-60 deg about the wall normal (Y).
	cx, cz, side = 232.0, 150.0, 34.0
	box("RackTriangle0", (cx, WALL_Y - 3.0, cz - side * 0.2887), (side, 2.4, 2.6), m["Wood"])
	box("RackTriangle1", (cx - side * 0.25, WALL_Y - 3.0, cz + side * 0.1443), (side, 2.4, 2.6), m["Wood"], (60.0, 0.0, 0.0))
	box("RackTriangle2", (cx + side * 0.25, WALL_Y - 3.0, cz + side * 0.1443), (side, 2.4, 2.6), m["Wood"], (-60.0, 0.0, 0.0))
	box("RackHook", (cx, WALL_Y - 2.0, cz + side * 0.577 + 1.0), (1.2, 4.0, 1.2), m["Metal"])
	# Flyer board on the back wall between the back bar and the chalkboard: cork, pinned sheets (league night, a band, lost
	# cue), slightly askew.
	fy, fz = -44.0, 176.0
	box("FlyerBoard", (WALL_X - 1.5, fy, fz), (3.0, 64.0, 50.0), m["Cork"])
	box("FlyerBoardFrame", (WALL_X - 1.0, fy, fz), (2.0, 68.0, 54.0), m["Trim"])
	for index, (dy, dz, w, h, material, tilt) in enumerate([(-18.0, 8.0, 21.0, 28.0, m["PaperCream"], 3.0), (5.0, 12.0, 18.0, 24.0, m["PaperYellow"], -4.0),
			(20.0, -8.0, 17.0, 22.0, m["PaperRed"], 2.0), (-12.0, -13.0, 22.0, 15.0, m["PaperBlack"], -2.0), (22.0, 14.0, 12.0, 9.0, m["PaperCream"], 6.0)]):
		box(f"Flyer{index}", (WALL_X - 3.4 - index * 0.12, fy + dy, fz + dz), (0.3, w, h), material, (0.0, 0.0, tilt), cast_shadow=False)
	# EXIT box over the back right corner (green, the fire code), with its faint spill.
	ex, ey, ez = WALL_X - 6.0, 262.0, 238.0
	box("ExitBox", (ex, ey, ez), (10.0, 34.0, 14.0), m["Trim"])
	box("ExitFace", (ex - 5.1, ey, ez), (0.3, 30.0, 10.0), m["NeonGreen"], cast_shadow=False)
	rect("ExitSpill", (ex - 10.0, ey, ez), (0.0, 180.0, 0.0), 8.0, (0.2, 1.0, 0.35), 30.0, 10.0, shadows=False, attenuation=200.0)


def build_table(m: dict) -> float:
	table = rb.spawn(unreal.RbTable, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), "Table")
	table.set_editor_property("preset", unreal.RbTablePreset.SEVEN_FOOT_BAR)
	table.set_editor_property("ball_set", unreal.RbBallSetPreset.OLD_BAR_OVERSIZED_CUE)
	table.set_editor_property("use_baked_meshes", True)
	table.set_editor_property("lamp_underside_height", LAMP_UNDERSIDE / 100.0)
	table.set_editor_property("table_index", 0)
	table.tags = ["RbPlayerTable"]
	set_table_materials(table, m)
	table.rebuild_table()
	bed = table.get_bed_center_world()
	bed_z = bed.z if bed.z > 10.0 else BED_Z

	demo = rb.spawn(unreal.RbBallRackDemo, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), "Balls")
	demo.set_editor_property("table", table)
	demo.set_editor_property("discipline", unreal.RbDiscipline.EIGHT_BALL)
	demo.set_editor_property("rack_seed", 11)
	demo.set_editor_property("play_break", True)
	demo.set_editor_property("break_speed", 8.5)
	demo.set_editor_property("break_aim_offset_deg", 0.6)
	demo.set_editor_property("cue_ball_offset", unreal.Vector2D(0.0, 0.09))
	demo.set_editor_property("freeze_at_shot_time", 30.0)

	# A chalk cube on the near rail (the table's top from its bounds: the rail caps).
	origin, extent = table.get_actor_bounds(False)
	top = origin.z + extent.z
	edge_y = origin.y - extent.y
	if top > bed_z and top < bed_z + 15.0:
		box("ChalkCube", (38.0, edge_y + 6.0, top + 1.1), (2.2, 2.2, 2.2), m["Chalkcube"], (0.0, 17.0, 0.0))
	else:
		rb.log(f"table top {top:.1f} cm not above the bed {bed_z:.1f}: no rail props")
	return bed_z


def build_lamp(m: dict, bed_z: float) -> None:
	shade_bottom = bed_z + LAMP_UNDERSIDE
	for index, x in enumerate(SHADE_X):
		# Shade: an engine cone (100 cm tall, 50 cm radius, pivot at the centre), 36 cm across, 16 cm tall, open side down.
		shape("Cone", f"LampShade{index}", (x, 0.0, shade_bottom + 8.0), (0.40, 0.40, 0.16), m["Shade"])
		shape("Cylinder", f"LampSocket{index}", (x, 0.0, shade_bottom + 17.0), (0.07, 0.07, 0.07), m["Metal"])
		# The bulb seen through the opening: an emissive disc just inside the rim (casts nothing).
		disc = shape("Cylinder", f"LampBulb{index}", (x, 0.0, shade_bottom - 0.4), (0.30, 0.30, 0.004), m["Bulb"], cast_shadow=False)
		disc.static_mesh_component.set_editor_property("affect_distance_field_lighting", False)
		disc.static_mesh_component.set_editor_property("visible_in_ray_tracing", False)
		# The light: a warm spot pointing down from the bulb (source 5 cm), wide cone like an enamel shade.
		spot(f"LampLight{index}", (x, 0.0, shade_bottom - 1.5), (-90.0, 0.0, 0.0), LAMP_LUMENS, 2700.0, 60.0, 30.0, 5.0, scattering=1.0)
	# Bar holding the shades, two chains / rods up into the dark.
	box("LampBar", (0.0, 0.0, shade_bottom + 26.0), (170.0, 3.0, 3.0), m["Metal"])
	for x in (-60.0, 60.0):
		box(f"LampRod{int(x)}", (x, 0.0, 0.5 * (shade_bottom + 26.0 + CEILING_Z)), (1.2, 1.2, CEILING_Z - shade_bottom - 26.0), m["Metal"])


def build_atmosphere() -> None:
	# The bar room behind the camera: back-bar light bouncing off the ceiling (the room's 20-40 lux ambient, venue-dive-bar 4.4).
	rect("RoomBounce", (FRONT_X + 40.0, -60.0, 240.0), (70.0, 0.0, 0.0), 2200.0, (1.0, 0.78, 0.55), 260.0, 80.0)
	fog = rb.spawn(unreal.ExponentialHeightFog, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), "Haze")
	fc = fog.get_editor_property("component")
	fc.set_editor_property("fog_density", 0.012)
	fc.set_editor_property("fog_height_falloff", 0.35)
	fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.004, 0.0035, 0.003, 1.0))
	fc.set_editor_property("enable_volumetric_fog", True)
	fc.set_editor_property("volumetric_fog_scattering_distribution", 0.35)
	fc.set_editor_property("volumetric_fog_albedo", unreal.Color(r=235, g=230, b=222, a=255))
	fc.set_editor_property("volumetric_fog_extinction_scale", 1.6)
	fc.set_editor_property("volumetric_fog_distance", 1200.0)


def build_grade() -> None:
	"""The eye's partial chromatic adaptation to the 2700 K tungsten light (the camera rig leaves white balance alone): warm, not
	orange. An unbound post-process volume that sets nothing else."""
	ppv = rb.spawn(unreal.PostProcessVolume, (0.0, 0.0, 150.0), (0.0, 0.0, 0.0), "EyeWhiteBalance")
	ppv.set_editor_property("unbound", True)
	settings = ppv.get_editor_property("settings")
	settings.set_editor_property("override_white_temp", True)
	settings.set_editor_property("white_temp", 5000.0)
	ppv.set_editor_property("settings", settings)


def build_camera(bed_z: float) -> None:
	# The title camera (Eyes preset: the menu looks like the game).
	eye = (-222.0, -138.0, 136.0)
	target = (0.0, -46.0, bed_z - 4.0)
	cam = rb.spawn(unreal.RbLookDevCamera, eye, rb.look_at_rotation(eye, target), CAMERA_TAG)
	cam.tags = [CAMERA_TAG]
	cam.set_editor_property("preset", unreal.RbCameraPreset.EYES)
	cam.set_editor_property("focus_distance_cm", math.dist(eye, (40.0, 0.0, bed_z)))


def build() -> None:
	world = fresh_level(MAP)
	world.get_world_settings().set_editor_property("default_game_mode", unreal.RbTitleGameMode)
	rb.ensure_dir(MAT_DIR)
	m = make_materials()
	remove_stale_instances(m)
	build_room(m)
	build_wall_details(m)
	build_bar(m)
	build_signs(m)
	build_clutter(m)
	bed_z = build_table(m)
	build_lamp(m, bed_z)
	build_atmosphere()
	build_grade()
	build_camera(bed_z)
	rb.save_current_level(MAP)
	if not unreal.EditorAssetLibrary.does_asset_exist(MAP):
		rb.fail(f"{MAP} was not written")


def main() -> None:
	rb.ensure_dir("/Game/Generated/Maps")
	build()
	rb.log(f"title level OK: {MAP}")


main()
