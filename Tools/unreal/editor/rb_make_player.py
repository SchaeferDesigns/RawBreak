"""First-person player assets (M2-F; Docs/ue-architecture.md 18.3, P2 diegetic ball in hand).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_player.py

Creates under /Game/Generated/Player (RbAssetPaths::PlayerDir):

  M_RbHand            skin / nail / shirt-sleeve surfaces of the stand-in hand, chosen by the mesh's vertex-colour part masks
                      (R = sleeve, G = nail; 0 / 1 only): Substrate slab, skin with subsurface diffusion (MFP ~1 mm red), the
                      sleeve with a cotton fuzz lobe. Parameters SkinAlbedo / NailAlbedo / SleeveAlbedo (linear) and roughness.
  M_RbContactPreview  the contact preview of the carried ball (URbBallInHandComponent): a soft dark disc on the cloth under exactly
                      the target, on the engine plane (UV 0..1 across the disc), translucent unlit, opacity = Strength x a
                      gaussian core that fades to 0 at the rim (Strength 0.55 carrying .. 0.9 touching, set per frame).
  SM_RbHand_Carry     URbAssetBakeLibrary.bake_hand_carry_mesh (RbAssetBake_Player.cpp): the procedural right hand that holds a
                      2 1/4 in ball from above, the ball centre at the mesh origin, fingers along +X, the bare forearm rising toward
                      -X into a rolled cuff;
  SM_RbArm_Carry      the shirt sleeve (1 m along +X) the component stretches from the cuff to the shoulder (same bake call).

Idempotent: materials are cleared and rebuilt in place (references kept), the mesh is re-baked. Checks the assets and prints the
parameters and the hand's bounds (A2 metrics). Owner: M2-F.
"""

from __future__ import annotations

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

PLAYER_DIR = "/Game/Generated/Player"               # RbAssetPaths::PlayerDir
HAND_MESH = PLAYER_DIR + "/SM_RbHand_Carry"         # RbAssetPaths::HandCarryMesh
ARM_MESH = PLAYER_DIR + "/SM_RbArm_Carry"           # RbHandMesh::ArmMeshPath
HAND_MATERIAL = PLAYER_DIR + "/M_RbHand"
PREVIEW_MATERIAL = PLAYER_DIR + "/M_RbContactPreview"

MEL = unreal.MaterialEditingLibrary
BALL_RADIUS_CM = 2.8575

# Surfaces (linear albedo, ESTIMATE from skin / fabric measurements: light-medium skin ~0.4 red, dark cotton shirt ~0.03).
SKIN_ALBEDO = (0.42, 0.26, 0.19)
NAIL_ALBEDO = (0.56, 0.42, 0.38)
SLEEVE_ALBEDO = (0.026, 0.028, 0.034)
SKIN_MFP_CM = (0.12, 0.045, 0.025)   # subsurface mean free path (red scatters furthest)


class Graph:
	"""One material under PLAYER_DIR, rebuilt in place (the same asset identity: meshes and components keep their reference)."""

	def __init__(self, name: str, **props):
		self.path = f"{PLAYER_DIR}/{name}"
		material = unreal.load_asset(self.path) if unreal.EditorAssetLibrary.does_asset_exist(self.path) else None
		if material is not None and not isinstance(material, unreal.Material):
			rb.delete_asset_if_exists(self.path)
			material = None
		if material is None:
			tools = unreal.AssetToolsHelpers.get_asset_tools()
			material = tools.create_asset(name, PLAYER_DIR, unreal.Material, unreal.MaterialFactoryNew())
			if material is None:
				rb.fail(f"could not create {self.path}")
		else:
			MEL.delete_all_material_expressions(material)
		self.m = material
		self.y = 0
		for key, value in props.items():
			self.m.set_editor_property(key, value)
		self.params = []

	def node(self, cls, x: int = -600, **props):
		self.y += 120
		expression = MEL.create_material_expression(self.m, cls, x, self.y)
		if expression is None:
			rb.fail(f"{self.path}: could not create {cls}")
		for key, value in props.items():
			expression.set_editor_property(key, value)
		return expression

	def link(self, src, dst, pin: str = "", src_pin: str = "") -> None:
		for candidate in (pin, pin.replace(" ", "")):
			if MEL.connect_material_expressions(src, src_pin, dst, candidate):
				return
		rb.fail(f"{self.path}: could not connect {src.get_name()}.{src_pin} -> {dst.get_name()}.{pin}")

	def const(self, value, x: int = -900):
		if isinstance(value, (tuple, list)):
			return self.node(unreal.MaterialExpressionConstant3Vector, x, constant=unreal.LinearColor(value[0], value[1], value[2], 1.0))
		return self.node(unreal.MaterialExpressionConstant, x, r=float(value))

	def scalar(self, name: str, value: float, group: str = "RawBreak", x: int = -1400):
		self.params.append((name, value))
		return self.node(unreal.MaterialExpressionScalarParameter, x, parameter_name=name, default_value=float(value), group=group)

	def vector(self, name: str, rgb, group: str = "RawBreak", x: int = -1400):
		"""Vector parameter + RGB mask (returns the mask)."""
		self.params.append((name, tuple(rgb)))
		p = self.node(unreal.MaterialExpressionVectorParameter, x, parameter_name=name,
			default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0), group=group)
		return self.mask(p, "rgb", x + 200)

	def mask(self, src, channels: str, x: int = -1000, src_pin: str = ""):
		m = self.node(unreal.MaterialExpressionComponentMask, x, r="r" in channels, g="g" in channels, b="b" in channels, a="a" in channels)
		self.link(src, m, "", src_pin)
		return m

	def lerp(self, a, b, alpha, x: int = -700):
		node = self.node(unreal.MaterialExpressionLinearInterpolate, x)
		self.link(a, node, "A")
		self.link(b, node, "B")
		self.link(alpha, node, "Alpha")
		return node

	def multiply(self, a, b, x: int = -700):
		node = self.node(unreal.MaterialExpressionMultiply, x)
		self.link(a, node, "A")
		self.link(b, node, "B")
		return node

	def inline(self, description: str, code: str, inputs: list, output=unreal.CustomMaterialOutputType.CMOT_FLOAT1, x: int = -500):
		"""Custom node without an include: inputs [(name, expression, output_pin)]."""
		node = self.node(unreal.MaterialExpressionCustom, x, description=description, code=code, output_type=output)
		custom_inputs = []
		for name, _, _ in inputs:
			ci = unreal.CustomInput()
			ci.set_editor_property("input_name", name)
			custom_inputs.append(ci)
		node.set_editor_property("inputs", custom_inputs)
		for name, expression, pin in inputs:
			self.link(expression, node, name, pin)
		return node

	def output(self, expression, prop, src_pin: str = "") -> None:
		if not MEL.connect_material_property(expression, src_pin, prop):
			rb.fail(f"{self.path}: could not connect {expression.get_name()} -> {prop}")

	def finish(self) -> str:
		MEL.layout_material_expressions(self.m)
		MEL.recompile_material(self.m)
		unreal.EditorAssetLibrary.save_loaded_asset(self.m, False)
		rb.log(f"{self.path}: " + ", ".join(f"{n}={v}" for n, v in self.params))
		return self.path


def make_hand_material() -> str:
	"""M_RbHand: the part masks of the vertex colour pick skin / nail / sleeve; skin scatters (Substrate diffusion), the sleeve is
	cotton (fuzz lobe). Opaque, not Nanite (a small moving mesh)."""
	g = Graph("M_RbHand", two_sided=False, blend_mode=unreal.BlendMode.BLEND_OPAQUE, tangent_space_normal=True)
	vc = g.node(unreal.MaterialExpressionVertexColor, -1800)
	sleeve = g.mask(vc, "r", -1600)
	nail = g.mask(vc, "g", -1600)
	skin_albedo = g.vector("SkinAlbedo", SKIN_ALBEDO, "Hand")
	nail_albedo = g.vector("NailAlbedo", NAIL_ALBEDO, "Hand")
	sleeve_albedo = g.vector("SleeveAlbedo", SLEEVE_ALBEDO, "Sleeve")
	albedo = g.lerp(g.lerp(skin_albedo, nail_albedo, nail, -1000), sleeve_albedo, sleeve, -800)
	roughness = g.lerp(g.lerp(g.scalar("SkinRoughness", 0.52, "Hand"), g.scalar("NailRoughness", 0.32, "Hand"), nail, -1000),
		g.scalar("SleeveRoughness", 0.85, "Sleeve"), sleeve, -800)
	# Skin F0 0.028 (n = 1.4); cloth scatters, its specular is the fuzz.
	f0 = g.lerp(g.const(0.028), g.const(0.02), sleeve, -800)
	skin_mfp = g.vector("SkinMfpCm", SKIN_MFP_CM, "Hand")
	mfp = g.lerp(skin_mfp, g.const((0.0, 0.0, 0.0)), sleeve, -800)
	fuzz_amount = g.multiply(sleeve, g.scalar("SleeveFuzz", 0.55, "Sleeve"), -800)
	fuzz_color = g.multiply(sleeve_albedo, g.const(2.5), -800)
	slab = g.node(unreal.MaterialExpressionSubstrateSlabBSDF, -100, sub_surface_type=unreal.MaterialSubSurfaceType.MSS_DIFFUSION)
	for pin, expression in (("Diffuse Albedo", albedo), ("F0", f0), ("Roughness", roughness), ("SSS MFP", mfp), ("Fuzz Amount", fuzz_amount),
			("Fuzz Color", fuzz_color), ("Fuzz Roughness", g.const(0.6))):
		g.link(expression, slab, pin)
	g.output(slab, unreal.MaterialProperty.MP_FRONT_MATERIAL)
	return g.finish()


def make_contact_preview_material() -> str:
	"""M_RbContactPreview: translucent unlit black with a gaussian-cored opacity disc (UV 0..1 across the plane, rim at r = 1)."""
	g = Graph("M_RbContactPreview", two_sided=False, blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT, tangent_space_normal=True)
	for key, value in (("shading_model", unreal.MaterialShadingModel.MSM_UNLIT), ("translucency_lighting_mode",
			unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)):
		try:
			g.m.set_editor_property(key, value)
		except Exception as error:  # noqa: BLE001 - a property the engine version does not expose is not needed
			rb.log(f"M_RbContactPreview: {key} not set ({error})")
	uv = g.node(unreal.MaterialExpressionTextureCoordinate, -1600, coordinate_index=0)
	strength = g.scalar("Strength", 0.55, "Preview")
	peak = g.scalar("PeakOpacity", 0.85, "Preview")
	sharpness = g.scalar("CoreSharpness", 22.0, "Preview")
	halo = g.scalar("HaloWeight", 0.35, "Preview")
	# A dark core where the ball will touch (radius ~0.3 R: the contact shadow of a resting ball) in a soft halo (~1.3 R).
	opacity = g.inline("RbContactPreview",
		"const float R = length(UV - 0.5) * 2.0;\n"
		"const float Core = exp(-R * R * Sharpness);\n"
		"const float Halo = exp(-R * R * 3.0);\n"
		"const float Rim = saturate((1.0 - R) * 4.0);\n"
		"return saturate(Strength * Peak * ((1.0 - HaloWeight) * Core + HaloWeight * Halo) * Rim);",
		[("UV", uv, ""), ("Strength", strength, ""), ("Peak", peak, ""), ("Sharpness", sharpness, ""), ("HaloWeight", halo, "")])
	unlit = g.node(unreal.MaterialExpressionSubstrateUnlitBSDF, -100)
	g.link(g.const((0.0, 0.0, 0.0)), unlit, "Emissive Color")
	g.output(unlit, unreal.MaterialProperty.MP_FRONT_MATERIAL)
	g.output(opacity, unreal.MaterialProperty.MP_OPACITY)
	return g.finish()


def bake_hand() -> None:
	if not unreal.RbAssetBakeLibrary.bake_hand_carry_mesh():
		rb.fail("bake_hand_carry_mesh returned False")


def verify() -> None:
	for path in (HAND_MATERIAL, PREVIEW_MATERIAL, HAND_MESH, ARM_MESH):
		if not unreal.EditorAssetLibrary.does_asset_exist(path):
			rb.fail(f"{path} missing after generation")
	preview = unreal.load_asset(PREVIEW_MATERIAL)
	scalars = {str(n) for n in MEL.get_scalar_parameter_names(preview)}
	if "Strength" not in scalars:
		rb.fail(f"M_RbContactPreview lacks the scalar Strength (URbBallInHandComponent sets it): {sorted(scalars)}")
	boxes = {}
	for path in (HAND_MESH, ARM_MESH):
		mesh = unreal.load_asset(path)
		if mesh is None or not isinstance(mesh, unreal.StaticMesh):
			rb.fail(f"{path} did not load as a StaticMesh")
		material = mesh.get_material(0)
		if material is None or material.get_path_name().split(".")[0] != HAND_MATERIAL:
			rb.fail(f"{path}: slot 0 is {material.get_path_name() if material else None}, expected {HAND_MATERIAL}")
		if mesh.get_editor_property("nanite_settings").enabled:
			rb.fail(f"{path}: Nanite must be off (a small moving mesh)")
		box = mesh.get_bounding_box()
		boxes[path] = box
		rb.log(f"{path}: bounds ({box.min.x:.2f}, {box.min.y:.2f}, {box.min.z:.2f}) .. ({box.max.x:.2f}, {box.max.y:.2f}, {box.max.z:.2f}) cm")
	# The hand holds the ball from above: the palm over the ball's top, the finger tips below the equator but above the cloth
	# (z > -R), the forearm and cuff ~18 cm behind / ~15 cm above the ball.
	hand = boxes[HAND_MESH]
	if hand.min.z <= -BALL_RADIUS_CM:
		rb.fail(f"{HAND_MESH}: reaches {hand.min.z:.2f} cm below the ball centre (the cloth is at -{BALL_RADIUS_CM} cm)")
	if hand.max.z < BALL_RADIUS_CM + 8.0 or hand.min.x > -18.0:
		rb.fail(f"{HAND_MESH}: bounds {hand.min} .. {hand.max} do not look like a hand over the ball with a forearm toward -X")
	arm = boxes[ARM_MESH]
	if abs(arm.max.x - arm.min.x - 100.0) > 10.0 or arm.max.y > 6.0:
		rb.fail(f"{ARM_MESH}: bounds {arm.min} .. {arm.max}: expected a ~1 m sleeve along +X")


def main() -> None:
	rb.ensure_dir(PLAYER_DIR)
	make_hand_material()
	make_contact_preview_material()
	bake_hand()
	verify()
	rb.log("rb_make_player OK (M_RbHand, M_RbContactPreview, SM_RbHand_Carry)")


main()
