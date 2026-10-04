"""Venue master materials, instances and the venue MPC (M2-B; Docs/ue-architecture.md 18.8, venue-dive-bar 6, 7).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_divebar_materials.py

Creates under /Game/Generated/Venues/DiveBar:
  Materials/MPC_DB_Venue      Age 0.80, GrimeTint, DustColor, NicotineTint, StickyAmount 0.6, EmissiveScale (venue-dive-bar 6.2;
                              EmissiveScale from Art/DiveBar/calibration.json when M2-A has measured it, else 1.0)
  Materials/M_DB_Opaque       slab + the wear stack (edge wear -> substrate, cavity / kick grime, dust, touch polish, nicotine),
                              recolourable CC0 scan inputs on UV0 (world scale), per-asset wear mask T_DB_<Asset>_WM on UV1,
                              procedural patterns (PatternMode 1 = 1970s grooved paneling, 2 = fissured ceiling tile)
  Materials/M_DB_Coated       the same base under a clear coat (Substrate vertical layering; coat haze and amber from Age, worn
                              through by touch polish / edge wear), glass rings and cigarette burns bombed from the decal atlases on
                              horizontal faces (RingDensity, BurnDensity)
  Materials/M_DB_Metal        metal F0 per metal + pitting, patina in cavities, fingerprints, desilvering (mirrors), edge wear to
                              the dielectric substrate, dust
  Materials/M_DB_Vinyl        slab + sheen (fuzz), age cracks at high convexity, leather micro normal
  Materials/M_DB_Glass        colored-transmittance translucency (bottles, glasses, shelves, cooler doors, liquids), dirt, dust,
                              fingerprints
  Materials/M_DB_Emissive     luminance in cd/m^2 x EmissiveScale, optional texture (labels, title strips) and a slow colour cycle
                              (>= 4 s, venue-dive-bar 4.1: never a flash)
  Materials/M_DB_Floor        procedural VCT (305 mm tiles, per-tile hash, seams, replaced patch, beige corridor), wax / traffic /
                              sticky / spill / chalk from the baked room masks T_DB_FloorMasks (world XY; the RVT path later)
  Decals/M_DB_Decal           DBuffer decal from the 4 x 4 atlases of db_decals.py (cell from the decal colour or CellIndex)
  MI_DB_* (INSTANCES below)   every surface of venue-dive-bar 6.3 plus the prop surfaces of 5.1 / 5.2 and the neon gases of 4.3
  Textures/**                 CC0 inputs (Tools/art/fetch_cc0.py --prepare), labels (Tools/art/text_textures.py), decal atlases
                              (db_decals.py), floor masks (Tools/art/floor_masks.py), neutral defaults
The table-family materials (M_Rb*, MI_RbCloth_BarGreen, MI_RbBall_DiveBar, MI_RbRail_BlackLaminate, the coin-op cabinet) are
M2-L's (rb_make_materials.py), never created here; table materials never reference MPC_DB_Venue.

Per-asset instances (the importer's hook, M2-A): `asset_materials(json_path)` returns {slot name: material path} for one Blender
export: a child MI_DB_<Surface>__<Asset> of the shared instance carrying the asset's wear mask (UV1), or the shared instance itself
for surfaces without a wear mask (glass, emissive). Import with `sys.path.insert(0, <editor dir>); import rb_make_divebar_materials`
(main() only runs as __main__).
Idempotent. Owner: M2-B.
"""

from __future__ import annotations

import json
import math
import os
import struct
import sys
import zlib

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

ROOT = "/Game/Generated/Venues/DiveBar"
MAT_DIR = ROOT + "/Materials"
TEX_DIR = ROOT + "/Textures"
DECAL_DIR = ROOT + "/Decals"
PROPS_DIR = ROOT + "/Props"
MPC_PATH = MAT_DIR + "/MPC_DB_Venue"
INC = "/RawBreak/Private/Venue/RbVenueWear.ush"
PROJECT = os.path.abspath(unreal.Paths.project_dir())
ART = os.path.join(PROJECT, "Art", "DiveBar")
GEN_DIR = os.path.join(os.path.abspath(unreal.Paths.project_saved_dir()), "RbGenerated", "Venue")

MEL = unreal.MaterialEditingLibrary
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4

VENUE_AGE = 0.80

# ---- colour helpers (linear sRGB) ------------------------------------------------------------------------------------


def srgb(r: int, g: int, b: int) -> tuple:
	def lin(c: float) -> float:
		c /= 255.0
		return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
	return (round(lin(r), 5), round(lin(g), 5), round(lin(b), 5))


def lum1(rgb) -> tuple:
	"""Normalises a colour to luminance 1 (emissive colours are multiplied by a luminance in cd/m^2)."""
	lum = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]
	return tuple(round(c / lum, 4) for c in rgb)


# Black-body colours (approximate linear sRGB, luminance 1).
K2200 = lum1(srgb(255, 147, 44))
K2400 = lum1(srgb(255, 157, 62))
K2700 = lum1(srgb(255, 169, 87))
K3000 = lum1(srgb(255, 180, 107))
K3500 = lum1(srgb(255, 196, 137))
K5000 = lum1(srgb(255, 228, 206))
K6500 = (1.0, 1.0, 1.0)


# --------------------------------------------------------------------------------------------------------------------
# textures
# --------------------------------------------------------------------------------------------------------------------


def write_png(path: str, width: int, height: int, channels: int, pixels: bytes) -> None:
	color_type = {1: 0, 3: 2, 4: 6}[channels]
	stride = width * channels
	raw = bytearray()
	for y in range(height):
		raw.append(0)
		raw.extend(pixels[y * stride:(y + 1) * stride])

	def chunk(tag: bytes, data: bytes) -> bytes:
		return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

	png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0))
	png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "wb") as f:
		f.write(png)


KIND_PROPS = {
	"srgb": {"compression_settings": unreal.TextureCompressionSettings.TC_DEFAULT, "srgb": True},
	"normal": {"compression_settings": unreal.TextureCompressionSettings.TC_NORMALMAP, "srgb": False, "flip_green_channel": False,
		"lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP},
	"linear": {"compression_settings": unreal.TextureCompressionSettings.TC_MASKS, "srgb": False},
	"gray": {"compression_settings": unreal.TextureCompressionSettings.TC_GRAYSCALE, "srgb": False},
	"srgba": {"compression_settings": unreal.TextureCompressionSettings.TC_DEFAULT, "srgb": True},
}


def import_texture(path: str, dest: str, name: str, kind: str, clamp: bool = False) -> unreal.Texture2D:
	if not os.path.exists(path):
		rb.fail(f"texture source missing: {path}")
	rb.ensure_dir(dest)
	task = unreal.AssetImportTask()
	task.set_editor_property("filename", path)
	task.set_editor_property("destination_path", dest)
	task.set_editor_property("destination_name", name)
	task.set_editor_property("automated", True)
	task.set_editor_property("replace_existing", True)
	task.set_editor_property("replace_existing_settings", True)
	task.set_editor_property("save", False)
	unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
	texture = unreal.load_asset(f"{dest}/{name}")
	if texture is None:
		rb.fail(f"import of {path} failed")
	props = dict(KIND_PROPS[kind])
	props.setdefault("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
	props["virtual_texture_streaming"] = False      # sampled in Custom nodes (texture bombing); VT for >= 2K later (11.4)
	if clamp:
		props["address_x"] = unreal.TextureAddress.TA_CLAMP
		props["address_y"] = unreal.TextureAddress.TA_CLAMP
	for key, value in props.items():
		texture.set_editor_property(key, value)
	unreal.EditorAssetLibrary.save_loaded_asset(texture, False)
	return texture


def make_default_textures() -> dict:
	out = {}
	specs = {
		"T_DB_White": ("srgb", 3, bytes([255, 255, 255]) * 16),
		"T_DB_Flat_N": ("normal", 3, bytes([128, 128, 255]) * 16),
		"T_DB_Mask_Neutral": ("linear", 3, bytes([255, 128, 128]) * 16),          # AO 1, roughness 0.5, height 0.5
		"T_DB_WM_Neutral": ("linear", 4, bytes([0, 255, 0, 128]) * 16),           # no convexity, no occlusion, no touch
		"T_DB_Black": ("gray", 1, bytes([0]) * 16),
	}
	for name, (kind, channels, pixels) in specs.items():
		png = os.path.join(GEN_DIR, name + ".png")
		write_png(png, 4, 4, channels, pixels)
		tex = import_texture(png, TEX_DIR + "/Defaults", name, kind)
		tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
		unreal.EditorAssetLibrary.save_loaded_asset(tex, False)
		out[name] = tex
	# the jukebox's light pipes: moulded ribs (8 per tile, a bright crest and a darker trough) with a slow unevenness of the LEDs
	# behind them (vertical in V; constant in U). Deterministic.
	w, h = 4, 128
	rows = []
	for j in range(h):
		v = j / h
		rib = (0.5 + 0.5 * math.cos(2.0 * math.pi * 8.0 * v)) ** 0.6
		led = 0.85 + 0.15 * math.sin(2.0 * math.pi * v + 0.7)
		lin = max(0.0, min(1.0, (0.35 + 0.65 * rib) * led))
		s = int(round(255.0 * (lin ** (1.0 / 2.2))))
		rows.append(bytes([s, s, s]) * w)
	png = os.path.join(GEN_DIR, "T_DB_JukeboxRibs.png")
	write_png(png, w, h, 3, b"".join(rows))
	out["T_DB_JukeboxRibs"] = import_texture(png, TEX_DIR + "/Defaults", "T_DB_JukeboxRibs", "srgb")
	return out


def make_cc0_textures() -> dict:
	manifest_path = os.path.join(ART, "Textures", "CC0", "cc0_prepared.json")
	if not os.path.exists(manifest_path):
		rb.fail("Art/DiveBar/Textures/CC0/cc0_prepared.json missing (python Tools/art/fetch_cc0.py ... --prepare)")
	with open(manifest_path, "r", encoding="utf-8") as handle:
		manifest = json.load(handle)
	out = {}
	for asset_id, info in sorted(manifest.items()):
		for role, filename in sorted(info["maps"].items()):
			kind = {"BC": "srgb", "N": "normal", "M": "linear", "O": "gray"}[role]
			name = f"T_DB_CC0_{asset_id}_{role}"
			out[name] = import_texture(os.path.join(ART, "Textures", "CC0", asset_id, filename), TEX_DIR + "/CC0", name, kind)
		out[f"size:{asset_id}"] = (info.get("size_m") or [1.0, 1.0])[0]
	rb.log(f"CC0 textures: {len([k for k in out if not k.startswith('size:')])}")
	return out


def make_art_textures() -> dict:
	out = {}
	labels = os.path.join(ART, "Textures", "Labels", "T_DB_Labels_BC.png")
	out["T_DB_Labels_BC"] = import_texture(labels, TEX_DIR + "/Labels", "T_DB_Labels_BC", "srgb")
	floor = os.path.join(ART, "Textures", "Floor", "T_DB_FloorMasks.png")
	out["T_DB_FloorMasks"] = import_texture(floor, TEX_DIR + "/Floor", "T_DB_FloorMasks", "linear", clamp=True)
	decals = os.path.join(ART, "Export", "Decals", "decals.json")
	with open(decals, "r", encoding="utf-8") as handle:
		sets = json.load(handle)["sets"]
	for set_name in sorted(sets):
		for role, kind in (("BC", "srgba"), ("N", "normal"), ("R", "linear")):
			name = f"T_DB_DecalAtlas_{set_name}_{role}"
			out[name] = import_texture(os.path.join(ART, "Textures", "Decals", name + ".png"), TEX_DIR + "/Decals", name, kind, clamp=True)
	out["decal_sets"] = sets
	return out


# --------------------------------------------------------------------------------------------------------------------
# MPC
# --------------------------------------------------------------------------------------------------------------------


def emissive_scale() -> float:
	path = os.path.join(ART, "calibration.json")
	if os.path.exists(path):
		with open(path, "r", encoding="utf-8") as handle:
			return float(json.load(handle).get("EmissiveScale", 1.0))
	return 1.0


def make_mpc() -> unreal.MaterialParameterCollection:
	rb.ensure_dir(MAT_DIR)
	mpc = unreal.load_asset(MPC_PATH) if unreal.EditorAssetLibrary.does_asset_exist(MPC_PATH) else None
	if mpc is None:
		mpc = unreal.AssetToolsHelpers.get_asset_tools().create_asset("MPC_DB_Venue", MAT_DIR, unreal.MaterialParameterCollection,
			unreal.MaterialParameterCollectionFactoryNew())
	scalars_want = {"Age": VENUE_AGE, "StickyAmount": 0.6, "EmissiveScale": emissive_scale()}
	vectors_want = {"GrimeTint": (0.09, 0.07, 0.05), "DustColor": (0.35, 0.33, 0.30), "NicotineTint": (0.78, 0.66, 0.42)}
	scalars = {str(s.get_editor_property("parameter_name")): s for s in mpc.get_editor_property("scalar_parameters")}
	vectors = {str(s.get_editor_property("parameter_name")): s for s in mpc.get_editor_property("vector_parameters")}
	out_s, out_v = [], []
	for name, value in scalars_want.items():
		entry = scalars.get(name) or unreal.CollectionScalarParameter()
		entry.set_editor_property("parameter_name", name)
		entry.set_editor_property("default_value", float(value))
		out_s.append(entry)
	for name, rgb in vectors_want.items():
		entry = vectors.get(name) or unreal.CollectionVectorParameter()
		entry.set_editor_property("parameter_name", name)
		entry.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
		out_v.append(entry)
	mpc.set_editor_property("scalar_parameters", out_s)
	mpc.set_editor_property("vector_parameters", out_v)
	unreal.EditorAssetLibrary.save_loaded_asset(mpc, False)
	rb.log(f"{MPC_PATH}: " + ", ".join(f"{k}={v}" for k, v in {**scalars_want, **vectors_want}.items()))
	return mpc


# --------------------------------------------------------------------------------------------------------------------
# graph helper (the pattern of rb_make_materials.py; materials are cleared and rebuilt, asset identity kept)
# --------------------------------------------------------------------------------------------------------------------


class Graph:
	def __init__(self, path: str, **material_props):
		self.path = path
		folder, name = path.rsplit("/", 1)
		rb.ensure_dir(folder)
		material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
		if material is not None and not isinstance(material, unreal.Material):
			rb.delete_asset_if_exists(path)
			material = None
		if material is None:
			material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
			if material is None:
				rb.fail(f"could not create {path}")
		else:
			# Point the front material at a plain slab before clearing: the cleared graph keeps a stale front link until the new
			# one is made, and a stale vertical-layering node gathers no shading model ("Material information is invalid" on
			# every node created while rebuilding M_DB_Coated). A slab always reports DefaultLit.
			placeholder = MEL.create_material_expression(material, unreal.MaterialExpressionSubstrateSlabBSDF, 0, 0)
			MEL.connect_material_property(placeholder, "", unreal.MaterialProperty.MP_FRONT_MATERIAL)
			MEL.delete_all_material_expressions(material)
		self.m = material
		self.y = 0
		defaults = {"tangent_space_normal": True, "two_sided": False, "blend_mode": unreal.BlendMode.BLEND_OPAQUE}
		defaults.update(material_props)
		for key, value in defaults.items():
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

	def scalar(self, name: str, value: float, group: str = "Venue", x: int = -1400):
		self.params.append((name, value))
		return self.node(unreal.MaterialExpressionScalarParameter, x, parameter_name=name, default_value=float(value), group=group)

	def vector(self, name: str, rgb, group: str = "Venue", x: int = -1400):
		self.params.append((name, tuple(rgb)))
		p = self.node(unreal.MaterialExpressionVectorParameter, x, parameter_name=name,
			default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0), group=group)
		return self.mask(p, "rgb", x + 200)

	def mask(self, src, channels: str, x: int = -1000, src_pin: str = ""):
		m = self.node(unreal.MaterialExpressionComponentMask, x, r="r" in channels, g="g" in channels, b="b" in channels, a="a" in channels)
		self.link(src, m, "", src_pin)
		return m

	def texcoord(self, index: int = 0, x: int = -1800):
		return self.node(unreal.MaterialExpressionTextureCoordinate, x, coordinate_index=index)

	def binary(self, cls, a, b, x: int = -800):
		node = self.node(cls, x)
		self.link(a, node, "A")
		self.link(b, node, "B")
		return node

	def tex(self, name: str, texture, uv, sampler, group: str = "Textures", x: int = -1100):
		t = self.node(unreal.MaterialExpressionTextureSampleParameter2D, x, parameter_name=name, texture=texture, sampler_type=sampler,
			group=group)
		self.link(uv, t, "UVs")
		self.params.append((name, texture.get_name()))
		return t

	def tex_object(self, name: str, texture, sampler, group: str = "Textures", x: int = -1100):
		self.params.append((name, texture.get_name()))
		return self.node(unreal.MaterialExpressionTextureObjectParameter, x, parameter_name=name, texture=texture, sampler_type=sampler,
			group=group)

	def mpc(self, collection, name: str, x: int = -1600):
		p = self.node(unreal.MaterialExpressionCollectionParameter, x, collection=collection)
		p.set_editor_property("parameter_name", name)
		return p

	def custom(self, description: str, code: str, inputs: list, output=F3, extra_outputs: list | None = None, x: int = -500):
		node = self.node(unreal.MaterialExpressionCustom, x, description=description, code=code, output_type=output,
			include_file_paths=[INC])
		custom_inputs = []
		for name, _, _ in inputs:
			ci = unreal.CustomInput()
			ci.set_editor_property("input_name", name)
			custom_inputs.append(ci)
		node.set_editor_property("inputs", custom_inputs)
		if extra_outputs:
			outs = []
			for name, typ in extra_outputs:
				co = unreal.CustomOutput()
				co.set_editor_property("output_name", name)
				co.set_editor_property("output_type", typ)
				outs.append(co)
			node.set_editor_property("additional_outputs", outs)
		for name, expression, pin in inputs:
			self.link(expression, node, name, pin)
		return node

	def slab(self, x: int = -100, sss=unreal.MaterialSubSurfaceType.MSS_NONE, **pins):
		slab = self.node(unreal.MaterialExpressionSubstrateSlabBSDF, x, sub_surface_type=sss)
		names = {"albedo": "Diffuse Albedo", "f0": "F0", "f90": "F90", "roughness": "Roughness", "anisotropy": "Anisotropy",
			"normal": "Normal", "tangent": "Tangent", "mfp": "SSS MFP", "emissive": "Emissive Color", "second_roughness": "Second Roughness",
			"second_weight": "Second Roughness Weight", "fuzz_roughness": "Fuzz Roughness", "fuzz_amount": "Fuzz Amount",
			"fuzz_color": "Fuzz Color", "thickness": "Thickness"}
		for key, value in pins.items():
			expression, out_pin = value if isinstance(value, tuple) else (value, "")
			self.link(expression, slab, names[key], out_pin)
		return slab

	def front(self, bsdf, ao=None, ao_pin: str = "") -> None:
		if not MEL.connect_material_property(bsdf, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
			rb.fail(f"{self.path}: could not connect the front material")
		if ao is not None and not MEL.connect_material_property(ao, ao_pin, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION):
			rb.fail(f"{self.path}: could not connect the ambient occlusion")

	def finish(self) -> str:
		MEL.layout_material_expressions(self.m)
		MEL.recompile_material(self.m)
		unreal.EditorAssetLibrary.save_loaded_asset(self.m, False)
		rb.log(f"{self.path}: {len(self.params)} parameters")
		return self.path


SC = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
SL = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
SN = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
SG = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE
SM = unreal.MaterialSamplerType.SAMPLERTYPE_MASKS


# --------------------------------------------------------------------------------------------------------------------
# the shared front half of the surface masters: UVs, scan textures, wear mask, MPC, the wear stack
# --------------------------------------------------------------------------------------------------------------------


UV_CODE = """
float2 U = UV;
if (WorldUV > 0.5)
{
	// world-aligned box projection in metres (greybox / dev boxes: engine cubes have 0..1 UVs per face); V runs down like a texture
	const float3 P = WorldPos * 0.01;
	const float3 A = abs(WorldN);
	U = (A.z >= A.x && A.z >= A.y) ? P.xy : ((A.x >= A.y) ? float2(P.y, -P.z) : float2(P.x, -P.z));
}
Meters = U;
U = lerp(U, float2(U.y, -U.x), step(0.5, Rot));
return U / max(Tile, 1e-4);
"""


def surface_inputs(g: Graph, tex: dict, mpc, grunge_default: str = "T_DB_CC0_SurfaceImperfections003_O") -> dict:
	uv0 = g.texcoord(0)
	uv1 = g.texcoord(1)
	world = g.node(unreal.MaterialExpressionWorldPosition, -1600)
	wnormal = g.node(unreal.MaterialExpressionVertexNormalWS, -1600)
	tile = g.scalar("TileSizeM", 1.0, "Scan")
	rot = g.scalar("UVRotate90", 0.0, "Scan")
	world_uv = g.scalar("WorldUV", 0.0, "Scan")
	uv_t = g.custom("RbDBTileUV", UV_CODE, [("UV", uv0, ""), ("Tile", tile, ""), ("Rot", rot, ""), ("WorldPos", world, ""), ("WorldN", wnormal, ""),
		("WorldUV", world_uv, "")], F2, [("Meters", F2)], x=-1500)
	meters = g.mask(uv_t, "rg", src_pin="Meters")
	base = g.tex("BaseColorTex", tex["T_DB_White"], uv_t, SC)
	normal = g.tex("NormalTex", tex["T_DB_Flat_N"], uv_t, SN)
	masks = g.tex("MaskTex", tex["T_DB_Mask_Neutral"], uv_t, SM)
	wm = g.tex("WearMask", tex["T_DB_WM_Neutral"], uv1, SM, "Wear")
	gscale = g.scalar("GrungeScaleM", 1.37, "Wear")
	uv_g = g.binary(unreal.MaterialExpressionDivide, meters, gscale)
	grunge = g.tex("GrungeTex", tex[grunge_default], uv_g, SG, "Wear")
	sscale = g.scalar("ScuffScaleM", 0.9, "Wear")
	uv_s = g.binary(unreal.MaterialExpressionDivide, meters, sscale)
	scuff = g.tex("ScuffTex", tex["T_DB_CC0_Scratches002_O"], uv_s, SG, "Wear")
	return {
		"uv0": meters, "uv_t": uv_t, "base": base, "normal": normal, "masks": masks, "wm": wm, "grunge": grunge, "scuff": scuff,
		"world": world,
		"wnormal": wnormal,
		"age": g.mpc(mpc, "Age"), "grime": g.mask(g.mpc(mpc, "GrimeTint"), "rgb"), "dust": g.mask(g.mpc(mpc, "DustColor"), "rgb"),
		"nic": g.mask(g.mpc(mpc, "NicotineTint"), "rgb"),
	}


def wear_params(g: Graph, dust_k: float = 0.3) -> dict:
	return {
		"AgeBias": g.scalar("AgeBias", 0.0, "Wear"), "EdgeWearWidth": g.scalar("EdgeWearWidth", 1.0, "Wear"),
		"TouchPolish": g.scalar("TouchPolish", 1.0, "Wear"), "DustK": g.scalar("DustK", dust_k, "Wear"),
		"Nicotine": g.scalar("NicotineAmount", 0.0, "Wear"),
	}


SURFACE_CODE = """
const float3 Scan = BaseTex;
float3 Base = RbVBaseColor(Scan, Tint, Strength, Recolor, MeanLum);
float Rough = saturate(lerp(RoughConst, MaskIn.g, RoughTexWeight) + RoughOffset);
float3 N = float3(NormalIn.xy * NormalStrength, NormalIn.z);
const float Fp = RbVFootprint2(UV);
if (PatternMode > 0.5 && PatternMode < 1.5)
{
	const float4 Pg = RbVPanelGrooves(UV * PatternScale, Fp);
	Base *= Pg.x * lerp(0.88, 1.12, Pg.z);
	Rough = lerp(Rough, 0.85, Pg.y);
	N = normalize(N + float3(Pg.w, 0.0, 0.0));
}
else if (PatternMode > 1.5 && PatternMode < 2.5)
{
	const float2 Cf = RbVCeilingFissures(UV * PatternScale, Fp);
	Base *= Cf.x;
	Rough = saturate(Rough + 0.1 * Cf.y);
	const float3 Ct = RbVCeilingTile(WorldPos * 0.01);
	Base *= Ct.x;
	Base *= lerp(float3(1.0, 1.0, 1.0), NicotineTint, Ct.y * 0.6);
	Base = lerp(Base, Base * float3(0.50, 0.38, 0.24), Ct.z);
}
Base *= lerp(1.0, MaskIn.r, AOStrength);
float OutR, Reveal, DustW, Polish;
const float3 C = RbVenueWear(Base, Rough, Under, UnderRough, WM, WorldPos, WorldN, Grunge.r, Scuff.r, Age, AgeBias, EdgeWearWidth,
	TouchPolish, DustK, Nicotine, GrimeTint, DustColor, NicotineTint, OutR, Reveal, DustW, Polish);
Roughness = OutR;
Fuzz = DustW * 0.35;
NormalOut = normalize(lerp(N, float3(0.0, 0.0, 1.0), saturate(DustW * 0.7 + Reveal * 0.3)));
RevealOut = Reveal;
PolishOut = Polish;
return C;
"""


def surface_custom(g: Graph, s: dict, w: dict, extra_inputs=None, description: str = "RbDBSurface"):
	p = {
		"Tint": g.vector("BaseTint", (1.0, 1.0, 1.0), "Scan"), "Strength": g.scalar("BaseColorStrength", 1.0, "Scan"),
		"Recolor": g.scalar("Recolor", 0.0, "Scan"), "MeanLum": g.scalar("TexMeanLum", 0.25, "Scan"),
		"RoughConst": g.scalar("Roughness", 0.5, "Scan"), "RoughTexWeight": g.scalar("RoughnessTexWeight", 1.0, "Scan"),
		"RoughOffset": g.scalar("RoughnessOffset", 0.0, "Scan"), "NormalStrength": g.scalar("NormalStrength", 1.0, "Scan"),
		"AOStrength": g.scalar("TexAOStrength", 0.5, "Scan"), "PatternMode": g.scalar("PatternMode", 0.0, "Pattern"),
		"PatternScale": g.scalar("PatternScale", 1.0, "Pattern"),
		"Under": g.vector("UnderColor", (0.30, 0.28, 0.25), "Wear"), "UnderRough": g.scalar("UnderRoughness", 0.7, "Wear"),
	}
	inputs = [("BaseTex", s["base"], "RGB"), ("MaskIn", s["masks"], "RGB"), ("NormalIn", s["normal"], "RGB"), ("UV", s["uv0"], ""),
		("WM", s["wm"], "RGBA"), ("WorldPos", s["world"], ""), ("WorldN", s["wnormal"], ""), ("Grunge", s["grunge"], "RGB"),
		("Scuff", s["scuff"], "RGB"), ("Age", s["age"], ""), ("GrimeTint", s["grime"], ""), ("DustColor", s["dust"], ""),
		("NicotineTint", s["nic"], "")]
	inputs += [(k, v, "") for k, v in p.items()] + [(k, v, "") for k, v in w.items()]
	inputs += extra_inputs or []
	return g.custom(description, SURFACE_CODE, inputs, F3, [("Roughness", F1), ("Fuzz", F1), ("NormalOut", F3), ("RevealOut", F1),
		("PolishOut", F1)])


# --------------------------------------------------------------------------------------------------------------------
# masters
# --------------------------------------------------------------------------------------------------------------------


def surface_props() -> dict:
	return {"used_with_nanite": True, "used_with_instanced_static_meshes": True}


def make_opaque(tex: dict, mpc) -> str:
	g = Graph(MAT_DIR + "/M_DB_Opaque", **surface_props())
	s = surface_inputs(g, tex, mpc)
	c = surface_custom(g, s, wear_params(g))
	f0 = g.scalar("F0", 0.04, "Scan")
	slab = g.slab(albedo=(c, "return"), f0=f0, roughness=(c, "Roughness"), normal=(c, "NormalOut"), fuzz_amount=(c, "Fuzz"),
		fuzz_roughness=g.const(0.8), fuzz_color=s["dust"])
	g.front(slab, ao=g.mask(s["wm"], "g"))
	return g.finish()


def make_coated(tex: dict, mpc) -> str:
	g = Graph(MAT_DIR + "/M_DB_Coated", **surface_props())
	s = surface_inputs(g, tex, mpc)
	c = surface_custom(g, s, wear_params(g), description="RbDBCoatedBase")
	# stains (rings, burns) bombed from the decal atlases on horizontal faces (world XY: static props, unique per placement)
	rings = g.tex_object("RingAtlas", tex["T_DB_DecalAtlas_Rings_BC"], SC, "Stains")
	burns = g.tex_object("BurnAtlas", tex["T_DB_DecalAtlas_Burns_BC"], SC, "Stains")
	stain = g.custom("RbDBStains",
		"const float Up = saturate((WorldN.z - 0.85) / 0.1);\n"
		"const float2 P = WorldPos.xy * 0.01 + WorldPos.z * 0.0037;\n"
		"float4 R = RbVStainBomb(P, 0.21, RingDensity, 1.0, 0.11, 0.0, 16.0, RingAtlas, RingAtlasSampler);\n"
		"float4 B = RbVStainBomb(P + 0.37, 0.17, BurnDensity, 2.0, 0.04, 0.0, 12.0, BurnAtlas, BurnAtlasSampler);\n"
		"R.a *= Up; B.a *= Up;\n"
		"float3 Col = lerp(Albedo, Albedo * 0.7 + R.rgb * 0.18, saturate(R.a));\n"
		"Col = lerp(Col, B.rgb, saturate(B.a));\n"
		"StainRough = lerp(lerp(Rough, 0.55, saturate(R.a)), 0.85, saturate(B.a));\n"
		"CoatCut = saturate(B.a * 1.3);\n"
		"RingHaze = saturate(R.a);\n"
		"return Col;",
		[("Albedo", c, "return"), ("Rough", c, "Roughness"), ("WorldPos", s["world"], ""), ("WorldN", s["wnormal"], ""),
			("RingDensity", g.scalar("RingDensity", 0.0, "Stains"), ""), ("BurnDensity", g.scalar("BurnDensity", 0.0, "Stains"), ""),
			("RingAtlas", rings, ""), ("BurnAtlas", burns, "")], F3, [("StainRough", F1), ("CoatCut", F1), ("RingHaze", F1)])
	base = g.slab(albedo=(stain, "return"), f0=g.scalar("F0", 0.04, "Scan"), roughness=(stain, "StainRough"), normal=(c, "NormalOut"),
		fuzz_amount=(c, "Fuzz"), fuzz_roughness=g.const(0.8), fuzz_color=s["dust"])
	# the coat: amber, hazier with age, worn through where polished / chipped / burnt
	coat = g.custom("RbDBCoat",
		"const float A = saturate(Age + AgeBias);\n"
		"const float Worn = saturate((Polish - 0.6) / 0.2) * TouchWearThrough + Reveal + CoatCut;\n"
		"CoatRough = saturate(CoatRoughness + CoatAgeHaze * A * (0.5 + Grunge.r) + 0.3 * RingHaze);\n"
		"return CoatThicknessCm * saturate(1.0 - Worn);",
		[("Age", s["age"], ""), ("AgeBias", g.scalar("CoatAgeBias", 0.0, "Coat"), ""), ("Polish", c, "PolishOut"), ("Reveal", c, "RevealOut"),
			("CoatCut", stain, "CoatCut"), ("RingHaze", stain, "RingHaze"), ("Grunge", s["grunge"], "RGB"),
			("TouchWearThrough", g.scalar("TouchWearThrough", 1.0, "Coat"), ""),
			("CoatRoughness", g.scalar("CoatRoughness", 0.07, "Coat"), ""), ("CoatAgeHaze", g.scalar("CoatAgeHaze", 0.2, "Coat"), ""),
			("CoatThicknessCm", g.scalar("CoatThicknessCm", 0.01, "Coat"), "")],
		F1, [("CoatRough", F1)])
	to_mfp = g.node(unreal.MaterialExpressionSubstrateTransmittanceToMFP, -300)
	# Substrate evaluates the transmittance colour when it compiles the layer: it must be a parameter, not shader math (the
	# ageing of the lacquer is its haze above; the amber is the instance's CoatTransmittance)
	g.link(g.vector("CoatTransmittance", (0.93, 0.84, 0.66), "Coat"), to_mfp, "Transmittance Color")
	g.link(coat, to_mfp, "Thickness", "return")
	top = g.slab(-100, sss=unreal.MaterialSubSurfaceType.MSS_SIMPLE_VOLUME, albedo=g.const((0.0, 0.0, 0.0)), f0=g.const(0.04),
		roughness=(coat, "CoatRough"), mfp=(to_mfp, "MFP"))
	layer = g.node(unreal.MaterialExpressionSubstrateVerticalLayering, 100)
	g.link(top, layer, "Top")
	g.link(base, layer, "Bottom")
	g.link(to_mfp, layer, "Top Thickness", "Thickness")
	g.front(layer, ao=g.mask(s["wm"], "g"))
	return g.finish()


def make_metal(tex: dict, mpc) -> str:
	g = Graph(MAT_DIR + "/M_DB_Metal", **surface_props())
	s = surface_inputs(g, tex, mpc)
	w = wear_params(g, 0.3)
	fscale = g.scalar("FingerprintScaleM", 0.35, "Metal")
	finger = g.tex("FingerprintTex", tex["T_DB_CC0_Fingerprints002_O"], g.binary(unreal.MaterialExpressionDivide, s["uv0"], fscale), SG, "Metal")
	metal = g.custom("RbDBMetal",
		"const float A = saturate(Age + AgeBias);\n"
		"const float3 Pm = WorldPos * 0.01;\n"
		"const float Fp = RbVFootprint3(Pm);\n"
		"const float3 M = RbVMetalAge(Pm, WM.g, WM.r, A, Pitting, PatinaAmount, Finger.r * FingerprintAmount, Fp);\n"
		"float Rw, Reveal, DustW, Polish;\n"
		"const float BaseR = saturate(lerp(MetalRoughness, MaskIn.g, RoughTexWeight) + M.y);\n"
		"const float3 Cover = RbVenueWear(float3(0.0, 0.0, 0.0), BaseR, Under, UnderRough, WM, WorldPos, WorldN, Grunge.r, Scuff.r, Age, AgeBias,\n"
		"	EdgeWearWidth, TouchPolish, DustK, Nicotine, GrimeTint, DustColor, NicotineTint, Rw, Reveal, DustW, Polish);\n"
		"const float Desilver = DesilverAmount * saturate((WM.b * (0.55 + Grunge.r) - 0.42) * 3.0);\n"
		"const float Dielectric = saturate(Reveal + DustW * 1.2 + M.z + 3.0 * RbVLuminance(Cover) + Desilver);\n"
		"F0Out = lerp(MetalF0 * M.x, float3(0.04, 0.04, 0.04), Dielectric);\n"
		"Roughness = lerp(saturate(BaseR - 0.12 * Polish), Rw, saturate(DustW + Reveal));\n"
		"Aniso = BrushedAniso * (1.0 - Dielectric);\n"
		"float3 Alb = lerp(Cover, PatinaColor, saturate(M.z * 1.5));\n"
		"Alb = lerp(Alb, float3(0.03, 0.03, 0.03), Desilver);\n"
		"Fuzz = DustW * 0.35;\n"
		"return Alb;",
		[("WorldPos", s["world"], ""), ("WorldN", s["wnormal"], ""), ("WM", s["wm"], "RGBA"), ("MaskIn", s["masks"], "RGB"),
			("Grunge", s["grunge"], "RGB"), ("Scuff", s["scuff"], "RGB"), ("Finger", finger, "RGB"), ("Age", s["age"], ""),
			("GrimeTint", s["grime"], ""), ("DustColor", s["dust"], ""), ("NicotineTint", s["nic"], ""),
			("MetalF0", g.vector("MetalF0", (0.55, 0.56, 0.55), "Metal"), ""), ("MetalRoughness", g.scalar("MetalRoughness", 0.1, "Metal"), ""),
			("RoughTexWeight", g.scalar("RoughnessTexWeight", 0.0, "Metal"), ""), ("Pitting", g.scalar("Pitting", 0.2, "Metal"), ""),
			("PatinaColor", g.vector("PatinaColor", (0.05, 0.04, 0.03), "Metal"), ""),
			("PatinaAmount", g.scalar("PatinaAmount", 0.2, "Metal"), ""),
			("FingerprintAmount", g.scalar("FingerprintAmount", 0.3, "Metal"), ""),
			("DesilverAmount", g.scalar("DesilverAmount", 0.0, "Metal"), ""), ("BrushedAniso", g.scalar("BrushedAniso", 0.0, "Metal"), ""),
			("Under", g.vector("UnderColor", (0.16, 0.09, 0.05), "Wear"), ""), ("UnderRough", g.scalar("UnderRoughness", 0.8, "Wear"), "")]
		+ [(k, v, "") for k, v in w.items()],
		F3, [("F0Out", F3), ("Roughness", F1), ("Aniso", F1), ("Fuzz", F1)])
	slab = g.slab(albedo=(metal, "return"), f0=(metal, "F0Out"), f90=g.const((1.0, 1.0, 1.0)), roughness=(metal, "Roughness"),
		anisotropy=(metal, "Aniso"), normal=s["normal"], fuzz_amount=(metal, "Fuzz"), fuzz_roughness=g.const(0.8), fuzz_color=s["dust"])
	g.front(slab, ao=g.mask(s["wm"], "g"))
	return g.finish()


def make_vinyl(tex: dict, mpc) -> str:
	g = Graph(MAT_DIR + "/M_DB_Vinyl", **surface_props())
	s = surface_inputs(g, tex, mpc)
	c = surface_custom(g, s, wear_params(g, 0.15), description="RbDBVinylBase")
	crack = g.custom("RbDBVinylCracks",
		"const float2 P = WorldPos.xy * 0.01 + WorldPos.z * 0.013;\n"
		"const float Fp = RbVFootprint2(P);\n"
		"const float K = RbVVinylCracks(P, WM.r, saturate(Age + AgeBias), CrackAmount, CrackScaleM, Fp);\n"
		"CrackRough = saturate(Rough + 0.35 * K);\n"
		"return Albedo * (1.0 - 0.65 * K);",
		[("Albedo", c, "return"), ("Rough", c, "Roughness"), ("WorldPos", s["world"], ""), ("WM", s["wm"], "RGBA"), ("Age", s["age"], ""),
			("AgeBias", g.scalar("CrackAgeBias", 0.0, "Vinyl"), ""), ("CrackAmount", g.scalar("CrackAmount", 0.6, "Vinyl"), ""),
			("CrackScaleM", g.scalar("CrackScaleM", 0.018, "Vinyl"), "")], F3, [("CrackRough", F1)])
	sheen = g.scalar("SheenAmount", 0.06, "Vinyl")
	fuzz = g.binary(unreal.MaterialExpressionAdd, sheen, g.mask(c, "r", src_pin="Fuzz"))
	slab = g.slab(albedo=(crack, "return"), f0=g.scalar("F0", 0.045, "Scan"), roughness=(crack, "CrackRough"), normal=(c, "NormalOut"),
		fuzz_amount=fuzz, fuzz_roughness=g.const(0.55), fuzz_color=g.vector("SheenColor", (0.35, 0.30, 0.28), "Vinyl"))
	g.front(slab, ao=g.mask(s["wm"], "g"))
	return g.finish()


def make_glass(tex: dict, mpc) -> str:
	# used_with_nanite: glass panes are sections of Nanite props (back bar shelves, cooler doors, the jukebox window); Nanite
	# draws translucent sections through its fallback mesh, the flag only keeps the default material away
	g = Graph(MAT_DIR + "/M_DB_Glass", blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT_COLORED_TRANSMITTANCE,
		translucency_lighting_mode=unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING, used_with_instanced_static_meshes=True,
		used_with_nanite=True, two_sided=False)
	uv0 = g.texcoord(0)
	world = g.node(unreal.MaterialExpressionWorldPosition, -1600)
	wn = g.node(unreal.MaterialExpressionVertexNormalWS, -1600)
	fscale = g.scalar("FingerprintScaleM", 0.25, "Glass")
	finger = g.tex("FingerprintTex", tex["T_DB_CC0_Fingerprints002_O"], g.binary(unreal.MaterialExpressionDivide, uv0, fscale), SG, "Glass")
	gscale = g.scalar("GrungeScaleM", 0.8, "Glass")
	grunge = g.tex("GrungeTex", tex["T_DB_CC0_SurfaceImperfections003_O"], g.binary(unreal.MaterialExpressionDivide, uv0, gscale), SG, "Glass")
	glass = g.custom("RbDBGlass",
		"const float A = saturate(Age + AgeBias);\n"
		"const float Up = saturate((WorldN.z - 0.5) / 0.5);\n"
		"const float Dust = saturate(A * DustK * Up * (0.5 + Grunge.r));\n"
		"const float Dirt = saturate(DirtAmount * A * (0.3 + 0.9 * Grunge.r) + FingerprintAmount * Finger.r);\n"
		"Roughness = saturate(GlassRoughness + 0.35 * Dirt + 0.6 * Dust);\n"
		"Cover = saturate(Dust * 0.9 + Dirt * 0.25);\n"
		"return lerp(float3(0.0, 0.0, 0.0), lerp(float3(0.28, 0.26, 0.23), DustColor, Dust), Cover);",
		[("WorldN", wn, ""), ("Age", g.mpc(mpc, "Age"), ""), ("DustColor", g.mask(g.mpc(mpc, "DustColor"), "rgb"), ""),
			("Grunge", grunge, "RGB"), ("Finger", finger, "RGB"), ("AgeBias", g.scalar("AgeBias", 0.0, "Wear"), ""),
			("DustK", g.scalar("DustK", 0.3, "Wear"), ""), ("DirtAmount", g.scalar("DirtAmount", 0.3, "Glass"), ""),
			("FingerprintAmount", g.scalar("FingerprintAmount", 0.3, "Glass"), ""),
			("GlassRoughness", g.scalar("GlassRoughness", 0.03, "Glass"), "")],
		F3, [("Roughness", F1), ("Cover", F1)])
	to_mfp = g.node(unreal.MaterialExpressionSubstrateTransmittanceToMFP, -300)
	g.link(g.vector("GlassTint", (0.92, 0.96, 0.94), "Glass"), to_mfp, "Transmittance Color")
	# the UE 5.8 slab has no Thickness pin: a lone slab is SUBSTRATE_LAYER_DEFAULT_THICKNESS_CM (0.01 cm) thick, so the MFP is derived
	# for that thickness and GlassTint is the colour of ONE crossing of the surface (an MFP derived for the real 0.3 - 4 cm made the
	# glass and the liquids look clear); GlassThicknessCm stays documentation in the instances
	g.link(g.const(0.01), to_mfp, "Thickness")
	slab = g.slab(sss=unreal.MaterialSubSurfaceType.MSS_SIMPLE_VOLUME, albedo=(glass, "return"), f0=g.scalar("F0", 0.04, "Glass"),
		roughness=(glass, "Roughness"), mfp=(to_mfp, "MFP"))
	g.front(slab)
	return g.finish()


def make_emissive(tex: dict, mpc) -> str:
	g = Graph(MAT_DIR + "/M_DB_Emissive", **surface_props())
	uv0 = g.texcoord(0)
	tile = g.scalar("TileSizeM", 1.0, "Emissive")
	etex = g.tex("EmissiveTex", tex["T_DB_White"], g.binary(unreal.MaterialExpressionDivide, uv0, tile), SC, "Emissive")
	time = g.node(unreal.MaterialExpressionTime, -1600)
	em = g.custom("RbDBEmissive", "return RbVEmissive(ColorA, ColorB, ColorC, Luminance, Scale, CyclePeriod, T, Tex);",
		[("ColorA", g.vector("EmissiveColor", (1.0, 1.0, 1.0), "Emissive"), ""), ("ColorB", g.vector("CycleColorB", (1.0, 1.0, 1.0), "Emissive"), ""),
			("ColorC", g.vector("CycleColorC", (1.0, 1.0, 1.0), "Emissive"), ""), ("Luminance", g.scalar("Luminance", 100.0, "Emissive"), ""),
			("Scale", g.mpc(mpc, "EmissiveScale"), ""), ("CyclePeriod", g.scalar("CyclePeriod", 0.0, "Emissive"), ""), ("T", time, ""),
			("Tex", etex, "RGB")], F3)
	slab = g.slab(albedo=g.vector("OffAlbedo", (0.04, 0.04, 0.04), "Emissive"), f0=g.const(0.04),
		roughness=g.scalar("CoatRoughness", 0.12, "Emissive"), emissive=em)
	g.front(slab)
	return g.finish()


def make_floor(tex: dict, mpc) -> str:
	g = Graph(MAT_DIR + "/M_DB_Floor", **surface_props())
	world = g.node(unreal.MaterialExpressionWorldPosition, -1800)
	xy = g.custom("RbDBFloorUV", "return P.xy * 0.01;", [("P", world, "")], F2, x=-1600)
	room_uv = g.custom("RbDBFloorMaskUV", "return XY / float2(RoomX, RoomY);",
		[("XY", xy, ""), ("RoomX", g.scalar("MaskSizeXM", 20.48, "Floor"), ""), ("RoomY", g.scalar("MaskSizeYM", 10.24, "Floor"), "")], F2, x=-1400)
	masks = g.tex("FloorMasks", tex["T_DB_FloorMasks"], room_uv, SM, "Floor")
	micro_uv = g.binary(unreal.MaterialExpressionDivide, xy, g.scalar("MicroTileM", 1.99, "Floor"))
	micro_n = g.tex("MicroNormal", tex["T_DB_CC0_old_linoleum_flooring_01_N"], micro_uv, SN, "Floor")
	micro_m = g.tex("MicroMask", tex["T_DB_CC0_old_linoleum_flooring_01_M"], micro_uv, SM, "Floor")
	floor = g.custom("RbDBFloor",
		"float3 SeamN;\n"
		"float R;\n"
		"const float3 C = RbVctFloor(XY, ColorA, ColorB, PatchColor, float4(PatchX0, PatchY0, PatchX1, PatchY1), CorridorX, CorridorColor, Masks, MicroM.g, Age, Sticky,\n"
		"	GrimeTint, ChalkColor, R, SeamN);\n"
		"Roughness = R;\n"
		"const float3 Mn = float3(MicroN.xy * MicroStrength, MicroN.z);\n"
		"NormalOut = normalize(float3(SeamN.xy + Mn.xy, SeamN.z * Mn.z));\n"
		"return C;",
		[("XY", xy, ""), ("Masks", masks, "RGBA"), ("MicroN", micro_n, "RGB"), ("MicroM", micro_m, "RGB"), ("Age", g.mpc(mpc, "Age"), ""),
			("Sticky", g.mpc(mpc, "StickyAmount"), ""), ("GrimeTint", g.mask(g.mpc(mpc, "GrimeTint"), "rgb"), ""),
			("ColorA", g.vector("TileColorA", srgb(90, 31, 27), "Floor"), ""), ("ColorB", g.vector("TileColorB", srgb(26, 26, 26), "Floor"), ""),
			("PatchColor", g.vector("PatchColor", srgb(128, 52, 44), "Floor"), ""),
			("PatchX0", g.scalar("PatchX0", 5.18, "Floor"), ""), ("PatchY0", g.scalar("PatchY0", 2.44, "Floor"), ""),
			("PatchX1", g.scalar("PatchX1", 6.10, "Floor"), ""), ("PatchY1", g.scalar("PatchY1", 3.36, "Floor"), ""),
			("CorridorX", g.scalar("CorridorX", 16.56, "Floor"), ""), ("CorridorColor", g.vector("CorridorColor", srgb(176, 160, 130), "Floor"), ""),
			("ChalkColor", g.vector("ChalkColor", (0.15, 0.21, 0.31), "Floor"), ""), ("MicroStrength", g.scalar("MicroNormalStrength", 0.35, "Floor"), "")],
		F3, [("Roughness", F1), ("NormalOut", F3)])
	slab = g.slab(albedo=(floor, "return"), f0=g.const(0.045), roughness=(floor, "Roughness"), normal=(floor, "NormalOut"))
	g.front(slab)
	return g.finish()


def make_decal(tex: dict) -> str:
	g = Graph(DECAL_DIR + "/M_DB_Decal", material_domain=unreal.MaterialDomain.MD_DEFERRED_DECAL,
		blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT)
	uv0 = g.texcoord(0)
	color_src = g.node(unreal.MaterialExpressionDecalColor, -1600) if hasattr(unreal, "MaterialExpressionDecalColor") else g.const((0.0, 0.0, 0.0))
	cell_uv = g.custom("RbDBDecalCell",
		"const float Cell = (UseDecalColor > 0.5) ? floor(saturate(DecalCol.r) * 15.999) : CellIndex;\n"
		"const float2 C = float2(fmod(Cell, 4.0), floor(Cell / 4.0));\n"
		"return (C + saturate(UV)) / 4.0;",
		[("UV", uv0, ""), ("DecalCol", color_src, ""), ("CellIndex", g.scalar("CellIndex", 0.0, "Decal"), ""),
			("UseDecalColor", g.scalar("CellFromDecalColor", 1.0, "Decal"), "")], F2, x=-1400)
	bc = g.tex("DecalBC", tex["T_DB_DecalAtlas_Rings_BC"], cell_uv, SC, "Decal")
	n_tex = g.tex("DecalN", tex["T_DB_DecalAtlas_Rings_N"], cell_uv, SN, "Decal")
	n = g.custom("RbDBDecalNormal", "return normalize(float3(N.xy * Strength, N.z));", [("N", n_tex, "RGB"),
		("Strength", g.scalar("NormalStrength", 1.0, "Decal"), "")], F3, x=-900)
	r = g.tex("DecalR", tex["T_DB_DecalAtlas_Rings_R"], cell_uv, SM, "Decal")
	out = g.custom("RbDBDecal",
		"Coverage = saturate(BC.a * Opacity);\n"
		"Rough = saturate(R.r + RoughnessOffset);\n"
		"return BC.rgb * Tint;",
		[("BC", bc, "RGBA"), ("R", r, "RGB"), ("Opacity", g.scalar("Opacity", 1.0, "Decal"), ""),
			("RoughnessOffset", g.scalar("RoughnessOffset", 0.0, "Decal"), ""), ("Tint", g.vector("Tint", (1.0, 1.0, 1.0), "Decal"), "")],
		F3, [("Coverage", F1), ("Rough", F1)])
	slab = g.slab(albedo=(out, "return"), f0=g.const(0.04), roughness=(out, "Rough"), normal=n)
	decal = g.node(unreal.MaterialExpressionSubstrateConvertToDecal, 100)
	g.link(slab, decal, "Decal Material")
	g.link(out, decal, "Coverage", "Coverage")
	g.front(decal)
	return g.finish()


# --------------------------------------------------------------------------------------------------------------------
# instances (venue-dive-bar 6.3, 5.1 / 5.2, 4.2 / 4.3)
# --------------------------------------------------------------------------------------------------------------------


def cc0(asset_id: str, maps=("BC", "N", "M")) -> dict:
	"""Texture parameters of a prepared CC0 set (world-scale tile = the scan's real size)."""
	out = {}
	role_param = {"BC": "BaseColorTex", "N": "NormalTex", "M": "MaskTex"}
	for role in maps:
		out["tex:" + role_param[role]] = f"T_DB_CC0_{asset_id}_{role}"
	out["size:"] = asset_id
	return out


O, C, M, V, G, E, FL = "M_DB_Opaque", "M_DB_Coated", "M_DB_Metal", "M_DB_Vinyl", "M_DB_Glass", "M_DB_Emissive", "M_DB_Floor"

INSTANCES = {
	# ---- opaque (paint, plaster, brick, block, plastics, paper, rubber, foam) ----
	"MI_DB_Brick_PaintedGreen": (O, {**cc0("painted_brick"), "BaseTint": srgb(46, 70, 52), "Recolor": 1.0, "TexMeanLum": 0.32,
		"UnderColor": srgb(120, 52, 38), "UnderRoughness": 0.85, "NicotineAmount": 0.5, "TexAOStrength": 0.8, "DustK": 0.2}),
	"MI_DB_Plaster_Cream": (O, {**cc0("painted_plaster_wall"), "BaseTint": srgb(196, 178, 140), "Recolor": 1.0, "TexMeanLum": 0.37,
		"NicotineAmount": 0.9, "UnderColor": srgb(215, 210, 200), "DustK": 0.2}),
	"MI_DB_Plaster_Oxblood": (O, {**cc0("painted_plaster_wall"), "BaseTint": srgb(86, 28, 24), "Recolor": 1.0, "TexMeanLum": 0.37,
		"NicotineAmount": 0.5, "UnderColor": srgb(200, 190, 175), "DustK": 0.2}),
	"MI_DB_Block_Painted": (O, {**cc0("concrete_block_wall"), "BaseTint": srgb(170, 156, 128), "Recolor": 1.0, "TexMeanLum": 0.05,
		"NicotineAmount": 0.4, "UnderColor": srgb(120, 118, 112), "TexAOStrength": 0.9}),
	"MI_DB_CeilingTile": (O, {"BaseTint": srgb(122, 110, 84), "Roughness": 0.95, "RoughnessTexWeight": 0.0, "PatternMode": 2.0,
		"NicotineAmount": 1.0, "DustK": 0.0, "EdgeWearWidth": 0.5}),
	"MI_DB_Tin_Painted": (O, {**cc0("rusty_painted_metal"), "BaseTint": srgb(190, 180, 160), "Recolor": 0.6, "TexMeanLum": 0.07,
		"UnderColor": srgb(90, 45, 25), "EdgeWearWidth": 1.6, "DustK": 1.0, "NicotineAmount": 0.8}),
	"MI_DB_Paint_BlackSteel": (O, {**cc0("PaintedMetal009"), "BaseTint": srgb(22, 22, 24), "Recolor": 1.0, "TexMeanLum": 0.26,
		"UnderColor": srgb(96, 50, 30), "UnderRoughness": 0.8, "EdgeWearWidth": 1.5, "RoughnessTexWeight": 0.0, "Roughness": 0.48}),
	"MI_DB_Paint_Cabinet": (O, {"BaseTint": srgb(20, 20, 23), "Roughness": 0.32, "RoughnessTexWeight": 0.0, "UnderColor": srgb(70, 70, 72),
		"EdgeWearWidth": 1.2, "DustK": 0.6}),
	"MI_DB_Plywood_Painted": (O, {**cc0("Wood089"), "BaseTint": srgb(34, 27, 22), "Recolor": 1.0, "TexMeanLum": 0.14,
		"UnderColor": srgb(160, 125, 85), "EdgeWearWidth": 1.4, "Roughness": 0.55, "RoughnessTexWeight": 0.3}),
	"MI_DB_Steel_Black": (O, {"BaseTint": srgb(18, 18, 18), "Roughness": 0.42, "RoughnessTexWeight": 0.0, "UnderColor": srgb(80, 80, 82),
		"UnderRoughness": 0.35, "EdgeWearWidth": 1.2, "DustK": 0.8, "F0": 0.05}),
	"MI_DB_Plastic_Black": (O, {"BaseTint": srgb(14, 14, 14), "Roughness": 0.45, "RoughnessTexWeight": 0.0, "UnderColor": srgb(40, 40, 40)}),
	"MI_DB_Rubber_Mat": (O, {**cc0("Rubber004"), "BaseTint": (1.0, 1.0, 1.0), "Roughness": 0.8}),
	"MI_DB_Rubber_Black": (O, {"BaseTint": srgb(16, 16, 16), "Roughness": 0.8, "RoughnessTexWeight": 0.0}),
	"MI_DB_Foam_Exposed": (O, {**cc0("Foam002"), "TileSizeM": 0.25, "BaseTint": srgb(230, 200, 130), "Recolor": 0.7, "TexMeanLum": 0.18,
		"Roughness": 0.95, "RoughnessTexWeight": 0.0, "AgeBias": 0.1}),
	"MI_DB_Tape": (O, {"BaseTint": srgb(120, 124, 124), "Roughness": 0.42, "RoughnessTexWeight": 0.0, "NormalTex": "T_DB_CC0_Rubber004_N",
		"TileSizeM": 0.12, "NormalStrength": 0.4, "AgeBias": -0.2, "EdgeWearWidth": 0.4}),
	"MI_DB_Chalk_Blue": (O, {"BaseTint": (0.05, 0.17, 0.52), "Roughness": 0.95, "RoughnessTexWeight": 0.0, "AgeBias": -0.5, "DustK": 0.0}),
	"MI_DB_Felt_Green": (O, {"BaseTint": srgb(22, 58, 34), "Roughness": 0.95, "RoughnessTexWeight": 0.0, "DustK": 0.6}),
	"MI_DB_Label_Atlas": (O, {"BaseColorTex": "T_DB_Labels_BC", "TileSizeM": 1.0, "Roughness": 0.55, "RoughnessTexWeight": 0.0,
		"AgeBias": -0.25, "EdgeWearWidth": 0.6, "DustK": 0.4}),
	"MI_DB_Label_Gloss": (O, {"BaseColorTex": "T_DB_Labels_BC", "TileSizeM": 1.0, "Roughness": 0.22, "RoughnessTexWeight": 0.0,
		"AgeBias": -0.3, "EdgeWearWidth": 0.5, "DustK": 0.5}),
	"MI_DB_Chalkboard": (O, {"BaseTint": srgb(32, 38, 34), "Roughness": 0.9, "RoughnessTexWeight": 0.0, "DustK": 0.2}),
	"MI_DB_Beer_Foam": (O, {"BaseTint": srgb(225, 210, 170), "Roughness": 0.7, "RoughnessTexWeight": 0.0, "AgeBias": -0.8}),
	"MI_DB_Paper_Coaster": (O, {"BaseTint": srgb(215, 205, 180), "Roughness": 0.85, "RoughnessTexWeight": 0.0}),
	# beer-case cardboard (C11; the SHOT & A BEER sign's torn edges)
	"MI_DB_Cardboard": (O, {**cc0("Cardboard002"), "TileSizeM": 0.6, "BaseTint": (0.95, 0.9, 0.85), "Roughness": 0.85, "RoughnessTexWeight": 0.4,
		"AgeBias": 0.1, "EdgeWearWidth": 0.6}),
	"MI_DB_Plastic_White": (O, {"BaseTint": srgb(222, 218, 205), "Roughness": 0.35, "RoughnessTexWeight": 0.0, "UnderColor": srgb(180, 175, 160),
		"AgeBias": -0.1}),
	"MI_DB_Plastic_Red": (O, {"BaseTint": srgb(150, 18, 20), "Roughness": 0.32, "RoughnessTexWeight": 0.0, "UnderColor": srgb(120, 30, 30)}),
	"MI_DB_Plastic_Blue": (O, {"BaseTint": srgb(20, 50, 140), "Roughness": 0.32, "RoughnessTexWeight": 0.0, "UnderColor": srgb(40, 60, 120)}),
	"MI_DB_Tape_Electrical": (O, {"BaseTint": srgb(16, 16, 17), "Roughness": 0.3, "RoughnessTexWeight": 0.0, "AgeBias": -0.1,
		"EdgeWearWidth": 0.3}),
	# ---- coated (lacquered / varnished wood, laminate, enamel) ----
	"MI_DB_Wood_Lacquered_Bar": (C, {**cc0("lacquered_cherry_wood"), "BaseTint": (0.62, 0.48, 0.40), "RingDensity": 0.30, "BurnDensity": 0.05,
		"CoatThicknessCm": 0.03, "CoatRoughness": 0.06, "CoatAgeHaze": 0.28, "CoatTransmittance": (0.92, 0.78, 0.52), "DustK": 0.1,
		"UnderColor": srgb(110, 70, 45)}),
	"MI_DB_Wood_PlankWall": (C, {**cc0("wood_plank_wall"), "BaseTint": (0.55, 0.45, 0.38), "CoatThicknessCm": 0.006, "CoatRoughness": 0.35,
		"CoatAgeHaze": 0.2, "TexAOStrength": 0.8, "DustK": 0.3, "UnderColor": srgb(120, 85, 55)}),
	"MI_DB_Wood_Stained": (C, {**cc0("fine_grained_wood"), "TileSizeM": 0.6, "BaseTint": srgb(96, 64, 42), "Recolor": 1.0, "TexMeanLum": 0.034, "CoatThicknessCm": 0.008,
		"CoatRoughness": 0.28, "CoatAgeHaze": 0.2, "UnderColor": srgb(150, 110, 70), "RingDensity": 0.15}),
	"MI_DB_Laminate_Walnut": (C, {**cc0("rosewood_veneer1"), "TileSizeM": 2.43, "BaseTint": srgb(92, 56, 34), "Recolor": 0.85, "TexMeanLum": 0.057,
		"CoatThicknessCm": 0.01, "CoatRoughness": 0.3, "CoatAgeHaze": 0.15, "UnderColor": srgb(140, 105, 70), "RingDensity": 0.35,
		"BurnDensity": 0.12, "EdgeWearWidth": 1.3}),
	"MI_DB_Paneling_Dark": (C, {**cc0("fine_grained_wood"), "TileSizeM": 0.6, "UVRotate90": 1.0, "BaseTint": srgb(70, 46, 30), "Recolor": 1.0, "TexMeanLum": 0.034,
		"PatternMode": 1.0, "CoatThicknessCm": 0.006, "CoatRoughness": 0.38, "CoatAgeHaze": 0.2, "NicotineAmount": 0.3,
		"UnderColor": srgb(120, 90, 60)}),
	"MI_DB_Wood_Ledge": (C, {**cc0("fine_grained_wood"), "TileSizeM": 0.6, "BaseTint": srgb(110, 75, 48), "Recolor": 1.0, "TexMeanLum": 0.034, "CoatThicknessCm": 0.008,
		"CoatRoughness": 0.30, "CoatAgeHaze": 0.3, "UnderColor": srgb(150, 110, 70), "RingDensity": 0.55, "BurnDensity": 0.10, "DustK": 0.15}),
	"MI_DB_CueShaft_Maple": (C, {**cc0("fine_grained_wood"), "TileSizeM": 0.35, "BaseTint": srgb(200, 166, 118), "Recolor": 0.8, "TexMeanLum": 0.034, "CoatThicknessCm": 0.004,
		"CoatRoughness": 0.25, "CoatAgeHaze": 0.1, "CoatTransmittance": (0.96, 0.92, 0.84), "UnderColor": srgb(200, 180, 150), "DustK": 0.0}),
	"MI_DB_Ceramic_Glazed": (C, {"BaseTint": srgb(214, 204, 180), "Roughness": 0.4, "RoughnessTexWeight": 0.0, "CoatThicknessCm": 0.02,
		"CoatRoughness": 0.06, "CoatAgeHaze": 0.15, "CoatTransmittance": (0.98, 0.97, 0.94), "UnderColor": srgb(120, 100, 85),
		"EdgeWearWidth": 1.2, "DustK": 1.0, "TouchWearThrough": 0.0}),
	"MI_DB_Enamel_Green": (C, {"BaseTint": srgb(20, 62, 36), "Roughness": 0.3, "RoughnessTexWeight": 0.0, "CoatThicknessCm": 0.02,
		"CoatRoughness": 0.1, "CoatAgeHaze": 0.15, "CoatTransmittance": (0.97, 0.95, 0.9), "UnderColor": srgb(40, 40, 42), "EdgeWearWidth": 1.4,
		"DustK": 1.0, "TouchWearThrough": 0.0}),
	# the bar mini pendants' cones (L5-L8): black enamel, worn through to grey steel on the rolled rim
	"MI_DB_Enamel_Black": (C, {"BaseTint": srgb(15, 15, 16), "Roughness": 0.3, "RoughnessTexWeight": 0.0, "CoatThicknessCm": 0.02,
		"CoatRoughness": 0.12, "CoatAgeHaze": 0.2, "CoatTransmittance": (0.97, 0.95, 0.9), "UnderColor": srgb(70, 70, 72), "EdgeWearWidth": 1.4,
		"DustK": 1.0, "TouchWearThrough": 0.0}),
	"MI_DB_Enamel_WhiteInt": (C, {"BaseTint": srgb(222, 218, 205), "Roughness": 0.3, "RoughnessTexWeight": 0.0, "CoatThicknessCm": 0.02,
		"CoatRoughness": 0.12, "CoatAgeHaze": 0.2, "CoatTransmittance": (0.97, 0.94, 0.86), "UnderColor": srgb(40, 40, 42),
		"NicotineAmount": 0.7, "DustK": 0.0, "TouchWearThrough": 0.0}),
	# ---- metals (F0 linear, venue-dive-bar 6.3) ----
	"MI_DB_Chrome": (M, {"MetalF0": (0.55, 0.56, 0.55), "MetalRoughness": 0.06, "Pitting": 0.25, "FingerprintAmount": 0.35, "PatinaAmount": 0.05}),
	"MI_DB_Chrome_Pitted": (M, {"MetalF0": (0.55, 0.56, 0.55), "MetalRoughness": 0.09, "Pitting": 1.0, "FingerprintAmount": 0.3,
		"PatinaAmount": 0.25, "PatinaColor": srgb(70, 40, 25), "EdgeWearWidth": 1.2}),
	"MI_DB_Brass_Worn": (M, {"MetalF0": (0.910, 0.778, 0.423), "MetalRoughness": 0.3, "Pitting": 0.1, "PatinaAmount": 0.9,
		"PatinaColor": srgb(40, 34, 20), "FingerprintAmount": 0.2, "UnderColor": srgb(60, 45, 20)}),
	"MI_DB_Steel_Zinc": (M, {"MetalF0": (0.66, 0.66, 0.63), "MetalRoughness": 0.4, "PatinaAmount": 0.35, "PatinaColor": srgb(150, 150, 140),
		"DustK": 0.8}),
	"MI_DB_Alu_Trim": (M, {"MetalF0": (0.91, 0.92, 0.92), "MetalRoughness": 0.35, "MaskTex": "T_DB_CC0_Metal009_M", "RoughnessTexWeight": 0.6,
		"BrushedAniso": 0.6, "TileSizeM": 0.5, "PatinaAmount": 0.1}),
	"MI_DB_Steel_Stainless": (M, {"MetalF0": (0.56, 0.57, 0.58), "MetalRoughness": 0.22, "FingerprintAmount": 0.5, "BrushedAniso": 0.3}),
	"MI_DB_Aluminium": (M, {"MetalF0": (0.91, 0.92, 0.92), "MetalRoughness": 0.25, "PatinaAmount": 0.15, "PatinaColor": srgb(120, 118, 110),
		"FingerprintAmount": 0.2}),
	"MI_DB_CuproNickel": (M, {"MetalF0": (0.66, 0.62, 0.55), "MetalRoughness": 0.28, "PatinaAmount": 0.6, "PatinaColor": srgb(50, 45, 35)}),
	"MI_DB_Mirror_Aged": (M, {"MetalF0": (0.96, 0.95, 0.93), "MetalRoughness": 0.015, "Pitting": 0.0, "DesilverAmount": 1.0,
		"FingerprintAmount": 0.2, "TouchPolish": 0.0, "PatinaAmount": 0.0, "DustK": 0.0, "EdgeWearWidth": 0.0, "AgeBias": -0.3}),
	# ---- vinyl ----
	# vinyl does not chip: the edge band only wears through to the dark fabric backing on the most-rubbed rolls
	"MI_DB_Vinyl_Oxblood": (V, {"NormalTex": "T_DB_CC0_leather_red_02_N", "TileSizeM": 0.6, "NormalStrength": 0.5,
		"BaseTint": srgb(82, 18, 17), "BaseColorStrength": 0.0, "Roughness": 0.36, "RoughnessTexWeight": 0.0, "UnderColor": srgb(62, 44, 34),
		"CrackAmount": 0.9, "DustK": 0.1, "EdgeWearWidth": 0.35}),
	"MI_DB_Vinyl_Black": (V, {"NormalTex": "T_DB_CC0_leather_red_02_N", "TileSizeM": 0.6, "NormalStrength": 0.5, "BaseTint": srgb(20, 20, 21),
		"BaseColorStrength": 0.0, "Roughness": 0.33, "RoughnessTexWeight": 0.0, "CrackAmount": 0.35, "AgeBias": -0.25, "EdgeWearWidth": 0.35,
		"UnderColor": srgb(40, 34, 30)}),
	"MI_DB_Vinyl_Booth": (V, {**cc0("fabric_leather_02"), "TileSizeM": 0.5, "BaseTint": srgb(96, 20, 20), "Recolor": 1.0, "TexMeanLum": 0.10,
		"Roughness": 0.38, "RoughnessTexWeight": 0.4, "NormalStrength": 0.7, "CrackAmount": 1.0, "UnderColor": srgb(62, 44, 34),
		"EdgeWearWidth": 0.4}),
	# ---- glass and liquids ----
	"MI_DB_Glass_Clear": (G, {"GlassTint": (0.96, 0.98, 0.97), "GlassThicknessCm": 0.3, "GlassRoughness": 0.02, "DirtAmount": 0.3,
		"FingerprintAmount": 0.35}),
	"MI_DB_Glass_Amber": (G, {"GlassTint": (0.55, 0.26, 0.06), "GlassThicknessCm": 0.35, "GlassRoughness": 0.02, "DirtAmount": 0.35}),
	"MI_DB_Glass_Green": (G, {"GlassTint": (0.28, 0.58, 0.30), "GlassThicknessCm": 0.35, "GlassRoughness": 0.02, "DirtAmount": 0.35}),
	"MI_DB_Glass_Shelf": (G, {"GlassTint": (0.88, 0.95, 0.91), "GlassThicknessCm": 0.6, "GlassRoughness": 0.02, "DirtAmount": 0.4,
		"DustK": 1.0, "FingerprintAmount": 0.1}),
	"MI_DB_Glass_Cooler": (G, {"GlassTint": (0.93, 0.96, 0.97), "GlassThicknessCm": 0.4, "GlassRoughness": 0.06, "DirtAmount": 0.15,
		"FingerprintAmount": 0.3, "GrungeScaleM": 0.35}),
	"MI_DB_Glass_Ashtray": (G, {"GlassTint": (0.78, 0.80, 0.76), "GlassThicknessCm": 1.2, "GlassRoughness": 0.05, "DirtAmount": 0.9,
		"FingerprintAmount": 0.6, "DustK": 0.6}),
	"MI_DB_Plexi_Scratched": (G, {"GlassTint": (0.94, 0.94, 0.92), "GlassThicknessCm": 0.3, "GlassRoughness": 0.08, "DirtAmount": 0.6,
		"FingerprintAmount": 0.5}),
	"MI_DB_Liquid_Whiskey": (G, {"GlassTint": (0.60, 0.26, 0.05), "GlassThicknessCm": 3.0, "GlassRoughness": 0.0, "DirtAmount": 0.0,
		"FingerprintAmount": 0.0, "DustK": 0.0, "F0": 0.02}),
	"MI_DB_Liquid_Beer": (G, {"GlassTint": (0.80, 0.50, 0.10), "GlassThicknessCm": 4.0, "GlassRoughness": 0.0, "DirtAmount": 0.0,
		"FingerprintAmount": 0.0, "DustK": 0.0, "F0": 0.02}),
	"MI_DB_Liquid_Clear": (G, {"GlassTint": (0.98, 0.98, 0.98), "GlassThicknessCm": 3.0, "GlassRoughness": 0.0, "DirtAmount": 0.0,
		"FingerprintAmount": 0.0, "DustK": 0.0, "F0": 0.02}),
	"MI_DB_Liquid_Rum": (G, {"GlassTint": (0.55, 0.28, 0.10), "GlassThicknessCm": 3.0, "GlassRoughness": 0.0, "DirtAmount": 0.0,
		"FingerprintAmount": 0.0, "DustK": 0.0, "F0": 0.02}),
	"MI_DB_Liquid_Red": (G, {"GlassTint": (0.75, 0.10, 0.05), "GlassThicknessCm": 3.0, "GlassRoughness": 0.0, "DirtAmount": 0.0,
		"FingerprintAmount": 0.0, "DustK": 0.0, "F0": 0.02}),
	"MI_DB_Liquid_Green": (G, {"GlassTint": (0.45, 0.85, 0.55), "GlassThicknessCm": 3.0, "GlassRoughness": 0.0, "DirtAmount": 0.0,
		"FingerprintAmount": 0.0, "DustK": 0.0, "F0": 0.02}),
	# ---- emissive (cd/m^2; venue-dive-bar 4.2 / 4.3) ----
	"MI_DB_Emissive_LampBadge": (E, {"EmissiveTex": "T_DB_Labels_BC", "TileSizeM": 1.0, "EmissiveColor": K3500, "Luminance": 150.0}),
	"MI_DB_Emissive_Bulb2700": (E, {"EmissiveColor": K2700, "Luminance": 30000.0, "OffAlbedo": (0.8, 0.8, 0.78), "CoatRoughness": 0.5}),
	"MI_DB_Emissive_Bulb3000": (E, {"EmissiveColor": K3000, "Luminance": 30000.0, "OffAlbedo": (0.8, 0.8, 0.78), "CoatRoughness": 0.5}),
	# the bar pendants' amber ST64 filament bulbs (300 lm over ~0.013 m^2 of glass ~ 7000 cd/m^2; amber glass when off)
	"MI_DB_Emissive_Bulb2200": (E, {"EmissiveColor": K2200, "Luminance": 6000.0, "OffAlbedo": (0.45, 0.28, 0.10), "CoatRoughness": 0.05}),
	# the booth sconces' dusty frosted tulip glass lit from inside (250 lm over ~0.06 m^2 ~ 1300 cd/m^2; thick dust and nicotine film: ~500; L13-L15)
	"MI_DB_Emissive_SconceGlass": (E, {"EmissiveColor": K2400, "Luminance": 500.0, "OffAlbedo": (0.72, 0.70, 0.64), "CoatRoughness": 0.45}),
	# channel-max colours: a luminance-normalised saturated blue is ~6x hotter in its channel and tone-maps to white
	"MI_DB_Emissive_JukeboxPanels": (E, {"EmissiveColor": (1.0, 0.30, 0.02), "CycleColorB": (1.0, 0.04, 0.03),
		"CycleColorC": (0.20, 0.10, 1.0), "CyclePeriod": 7.5, "Luminance": 1.3, "CoatRoughness": 0.15, "EmissiveTex": "T_DB_JukeboxRibs",
		"TileSizeM": 0.12, "OffAlbedo": (0.10, 0.09, 0.09)}),
	"MI_DB_Emissive_JukeboxStrips": (E, {"EmissiveTex": "T_DB_Labels_BC", "TileSizeM": 1.0, "EmissiveColor": K3500, "Luminance": 22.0}),
	"MI_DB_Emissive_JukeboxMarquee": (E, {"EmissiveTex": "T_DB_Labels_BC", "TileSizeM": 1.0, "EmissiveColor": K6500, "Luminance": 70.0}),
	"MI_DB_Emissive_DartMarquee": (E, {"EmissiveTex": "T_DB_Labels_BC", "TileSizeM": 1.0, "EmissiveColor": K5000, "Luminance": 60.0}),
	"MI_DB_Emissive_LedRed": (E, {"EmissiveColor": lum1((1.0, 0.06, 0.02)), "Luminance": 30.0, "OffAlbedo": (0.05, 0.01, 0.01)}),
	# the dart machine's 7-segment readout (label dart_score: red digits on black; the texture carries the colour)
	"MI_DB_Emissive_DartScore": (E, {"EmissiveTex": "T_DB_Labels_BC", "TileSizeM": 1.0, "EmissiveColor": (1.0, 1.0, 1.0), "Luminance": 45.0,
		"OffAlbedo": (0.02, 0.02, 0.02), "CoatRoughness": 0.08}),
	# the bottle backlight LED strip seen from below (L9 / L10 flux is the level's rect lights: 1500 lm over 0.02 x 3.5 m ~ 6800 cd/m^2)
	"MI_DB_Emissive_LedStrip": (E, {"EmissiveColor": K2700, "Luminance": 6800.0, "OffAlbedo": (0.7, 0.7, 0.68), "CoatRoughness": 0.4}),
	"MI_DB_Emissive_CoolerPanel": (E, {"EmissiveColor": K5000, "Luminance": 400.0, "OffAlbedo": (0.7, 0.7, 0.7)}),
	"MI_DB_Emissive_Exit": (E, {"EmissiveColor": lum1((1.0, 0.04, 0.02)), "Luminance": 40.0}),
	"MI_DB_Neon_ClearRed": (E, {"EmissiveColor": lum1((1.0, 0.05, 0.02)), "Luminance": 2160.0, "OffAlbedo": (0.15, 0.03, 0.03)}),
	"MI_DB_Neon_StdBlue": (E, {"EmissiveColor": lum1((0.15, 0.35, 1.0)), "Luminance": 2560.0, "OffAlbedo": (0.1, 0.1, 0.12)}),
	"MI_DB_Neon_RubyRed": (E, {"EmissiveColor": lum1((1.0, 0.02, 0.10)), "Luminance": 800.0, "OffAlbedo": (0.2, 0.02, 0.04)}),
	"MI_DB_Neon_CobaltBlue": (E, {"EmissiveColor": lum1((0.10, 0.20, 1.0)), "Luminance": 1600.0, "OffAlbedo": (0.03, 0.05, 0.2)}),
	"MI_DB_Neon_Green": (E, {"EmissiveColor": lum1((0.20, 1.0, 0.30)), "Luminance": 10026.0, "OffAlbedo": (0.1, 0.12, 0.1)}),
	"MI_DB_Neon_White": (E, {"EmissiveColor": lum1((1.0, 0.95, 0.90)), "Luminance": 6503.0, "OffAlbedo": (0.3, 0.3, 0.3)}),
	"MI_DB_Neon_NoviolGold": (E, {"EmissiveColor": lum1((1.0, 0.65, 0.15)), "Luminance": 6500.0, "OffAlbedo": (0.25, 0.2, 0.05)}),
	# ---- floor ----
	# the linoleum scan is only a faint micro break-up: its embossed pattern must not read on VCT
	"MI_DB_VCT_Oxblood": (FL, {"TileColorA": srgb(72, 30, 26), "TileColorB": srgb(28, 27, 25), "PatchColor": srgb(104, 46, 38),
		"MicroNormalStrength": 0.08, "MicroTileM": 0.83}),
	"MI_DB_VCT_Black": (FL, {"TileColorA": srgb(26, 26, 26), "TileColorB": srgb(20, 20, 20), "MicroNormalStrength": 0.08, "MicroTileM": 0.83}),
	"MI_DB_VCT_Beige": (FL, {"TileColorA": srgb(176, 160, 130), "TileColorB": srgb(168, 152, 124), "CorridorX": -1.0,
		"MicroNormalStrength": 0.08, "MicroTileM": 0.83}),
}


def _mi(path: str, parent_path: str):
	folder, name = path.rsplit("/", 1)
	rb.ensure_dir(folder)
	mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
	if mi is None:
		mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.MaterialInstanceConstant,
			unreal.MaterialInstanceConstantFactoryNew())
	MEL.set_material_instance_parent(mi, unreal.load_asset(parent_path))
	MEL.clear_all_material_instance_parameters(mi)
	return mi


def set_params(mi, params: dict, tex: dict) -> None:
	for key, value in params.items():
		if key == "size:":
			size = tex.get(f"size:{value}")
			if size and "TileSizeM" not in params:
				MEL.set_material_instance_scalar_parameter_value(mi, "TileSizeM", float(size))
			continue
		if key.startswith("tex:"):
			key, value = key[4:], value
		if isinstance(value, str):
			texture = tex.get(value) or unreal.load_asset(value)
			if texture is None:
				rb.fail(f"{mi.get_name()}: texture {value} missing")
			# UE 5.8 returns False here even when the value was set: verify by reading it back.
			MEL.set_material_instance_texture_parameter_value(mi, key, texture)
			if MEL.get_material_instance_texture_parameter_value(mi, key) != texture:
				rb.fail(f"{mi.get_name()}: no texture parameter {key}")
		elif isinstance(value, (tuple, list)):
			MEL.set_material_instance_vector_parameter_value(mi, key, unreal.LinearColor(value[0], value[1], value[2], 1.0))
		else:
			MEL.set_material_instance_scalar_parameter_value(mi, key, float(value))


def make_instances(tex: dict) -> list:
	paths = []
	for name, (parent, params) in sorted(INSTANCES.items()):
		path = f"{MAT_DIR}/{name}"
		mi = _mi(path, f"{MAT_DIR}/{parent}")
		set_params(mi, params, tex)
		MEL.update_material_instance(mi)
		unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
		paths.append(path)
	rb.log(f"{len(paths)} MI_DB_* instances")
	return paths


# Per-set look of the projected decals (venue-dive-bar 7) and the extra variants: dried spills keep only a faint tide-line relief,
# floor chalk is a pale powder haze, the S10 cue-butt dings are dented wood / paint (not white rings).
DECAL_BASE = {
	"Stains": {"NormalStrength": 0.25, "Opacity": 0.85},
	"Water": {"NormalStrength": 0.2},
	"Scuffs": {"NormalStrength": 0.6},
}
DECAL_VARIANTS = {
	"Chalk": [("Floor", {"Opacity": 0.45, "Tint": (1.25, 1.15, 1.0), "NormalStrength": 0.0, "RoughnessOffset": 0.1})],
	"Scuffs": [("Dings", {"Tint": (0.30, 0.22, 0.16), "Opacity": 0.55, "NormalStrength": 1.0})],
}


def make_decal_instances(tex: dict) -> list:
	paths = []
	sets = tex["decal_sets"]
	for set_name in sorted(sets):
		info = sets[set_name]
		variants = [("", dict(DECAL_BASE.get(set_name, {})))]
		if info.get("roughness_only_variant"):
			variants.append(("Sticky", {"Tint": (0.55, 0.5, 0.45), "RoughnessOffset": -0.2, "Opacity": 0.8, "NormalStrength": 0.0}))
		variants += DECAL_VARIANTS.get(set_name, [])
		for suffix, extra in variants:
			name = f"MI_DB_Decal_{set_name}{'_' + suffix if suffix else ''}_01"
			path = f"{DECAL_DIR}/{name}"
			mi = _mi(path, f"{DECAL_DIR}/M_DB_Decal")
			set_params(mi, {"DecalBC": f"T_DB_DecalAtlas_{set_name}_BC", "DecalN": f"T_DB_DecalAtlas_{set_name}_N",
				"DecalR": f"T_DB_DecalAtlas_{set_name}_R", **extra}, tex)
			MEL.update_material_instance(mi)
			unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
			paths.append(path)
	rb.log(f"{len(paths)} decal instances (cell = decal colour R x 16, or CellIndex)")
	return paths


# --------------------------------------------------------------------------------------------------------------------
# per-asset instances (the importer hook)
# --------------------------------------------------------------------------------------------------------------------

WEAR_MASTERS = {"M_DB_Opaque", "M_DB_Coated", "M_DB_Metal", "M_DB_Vinyl"}


def asset_materials(json_path: str) -> dict:
	"""slot name -> material path for one exported asset (Art/DiveBar/Export/**/<Asset>/<Asset>.json). Imports the asset's wear mask
	and creates MI_DB_<Surface>__<Asset> children (WearMask set) under the asset's Props/<Family>/Materials folder."""
	with open(json_path, "r", encoding="utf-8") as handle:
		meta = json.load(handle)
	asset_id = meta["asset_id"]
	family = meta.get("family", "Misc")
	wm_rel = meta.get("wear_mask", "")
	wm_tex = None
	if wm_rel:
		wm_tex = import_texture(os.path.join(PROJECT, wm_rel), f"{TEX_DIR}/Props/{family}", f"T_DB_{asset_id}_WM", "linear")
	out = {}
	for slot in meta.get("material_slots", []):
		shared = f"{MAT_DIR}/{slot}"
		if not unreal.EditorAssetLibrary.does_asset_exist(shared):
			rb.fail(f"{asset_id}: slot {slot} has no material instance (add it to INSTANCES in rb_make_divebar_materials.py)")
		parent = unreal.load_asset(shared)
		master = parent.get_editor_property("parent").get_name() if isinstance(parent, unreal.MaterialInstanceConstant) else ""
		if wm_tex is None or master not in WEAR_MASTERS:
			out[slot] = shared
			continue
		child = f"{PROPS_DIR}/{family}/Materials/{slot}__{asset_id}"
		mi = _mi(child, shared)
		MEL.set_material_instance_texture_parameter_value(mi, "WearMask", wm_tex)
		MEL.update_material_instance(mi)
		unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
		out[slot] = child
	return out


def verify(paths: list) -> None:
	for path in paths:
		if not unreal.EditorAssetLibrary.does_asset_exist(path):
			rb.fail(f"{path} missing after generation")
	mpc = unreal.load_asset(MPC_PATH)
	names = {str(s.get_editor_property("parameter_name")): s.get_editor_property("default_value") for s in mpc.get_editor_property("scalar_parameters")}
	if abs(names.get("Age", -1.0) - VENUE_AGE) > 1e-6:
		rb.fail(f"MPC_DB_Venue Age {names.get('Age')} != {VENUE_AGE}")
	# every master reads the venue Age through the MPC (the Age system of 6.2)
	for master in ("M_DB_Opaque", "M_DB_Coated", "M_DB_Metal", "M_DB_Vinyl", "M_DB_Glass", "M_DB_Floor"):
		mat = unreal.load_asset(f"{MAT_DIR}/{master}")
		if "AgeBias" not in {str(n) for n in MEL.get_scalar_parameter_names(mat)} and master != "M_DB_Floor":
			rb.fail(f"{master} lacks AgeBias")


def main() -> None:
	for folder in (MAT_DIR, TEX_DIR, DECAL_DIR):
		rb.ensure_dir(folder)
	tex = make_default_textures()
	tex.update(make_cc0_textures())
	tex.update(make_art_textures())
	mpc = make_mpc()
	paths = [MPC_PATH, make_opaque(tex, mpc), make_coated(tex, mpc), make_metal(tex, mpc), make_vinyl(tex, mpc), make_glass(tex, mpc),
		make_emissive(tex, mpc), make_floor(tex, mpc), make_decal(tex)]
	paths += make_instances(tex)
	paths += make_decal_instances(tex)
	verify(paths)
	rb.log(f"rb_make_divebar_materials: OK ({len(paths)} assets, Age {VENUE_AGE}, EmissiveScale {emissive_scale()})")


if __name__ == "__main__":
	main()
