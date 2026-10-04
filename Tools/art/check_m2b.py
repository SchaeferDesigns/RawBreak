#!/usr/bin/env python3
"""Host-side acceptance checks of M2-B's committed outputs (Docs/ue-architecture.md 18.8; venue-dive-bar 13.4, 13.9, 16). Runs in
seconds without Unreal or Blender (Python 3.11; numpy + Pillow for the decal-normal check, skipped without them; the font / texture
files must be checked out: git lfs pull):

  python Tools/art/check_m2b.py          # exit code 0 = every check passed, 1 = problems (listed)

VDB-T4 (metadata half; the generators assert while exporting, the importers re-check in Unreal): every
  Art/DiveBar/Export/Props/<Asset>/<Asset>.json reproduces its target_dimensions_m within tolerance_m, the tolerance is never looser
  than the spec's (hero +-2 mm, others +-1 cm), every recorded spec check is inside its tolerance, the FBX / wear mask exist, the
  axis convention is the one the importers mirror ("-y").
VDB-T7 (without the OCR pass): a ledger row for every exported mesh and every external input, no NC licence; every CC0 input pinned
  in the lock file, prepared, and ledgered as CC0-1.0 from Poly Haven / ambientCG; every font file matches its SHA-256 pin, has its
  licence file and a ledger row; the text-texture source names no real brand.
Contracts: every material slot is an MI_DB_* instance that rb_make_divebar_materials.py creates (M2-A's importer assigns by name);
  every instance key is a parameter of its master (the masters are built through a recording stand-in of the editor API, so the
  check follows the generator's code); every texture an instance names exists; the decal placements name existing decal instances
  and encode their atlas cell the way M_DB_Decal decodes it; the decal normal atlases are DirectX (green sign against the height
  slope); the back bar's prop instances name exported assets.
Owner: M2-B.
"""

from __future__ import annotations

import csv
import hashlib
import json
import math
import re
import sys
import types
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ART = REPO / "Art" / "DiveBar"
PROPS = ART / "Export" / "Props"
EDITOR = REPO / "Tools" / "unreal" / "editor"
HERO_TOL, MID_TOL = 0.002, 0.01
LICENCES_OK = {"own", "CC0-1.0", "OFL-1.1", "Apache-2.0"}
NC_MARKERS = ("-NC", "NC-", "NonCommercial", "Non-Commercial")
# Real brands of the product classes in the scene (beer, spirits, tables, jukeboxes, darts, chalk; venue-dive-bar 8.1). Multi-word or
# distinctive names only: common words ("Valley", "Master", "Diamond", "Patron") would match our own fictional text.
REAL_BRANDS = ["Budweiser", "Bud Light", "Miller Lite", "Miller High Life", "Coors", "Pabst", "Heineken", "Corona", "Stella Artois", "Guinness",
	"Michelob", "Busch Light", "Yuengling", "Jack Daniel", "Jim Beam", "Maker's Mark", "Wild Turkey", "Smirnoff", "Absolut", "Bacardi",
	"Captain Morgan", "Jose Cuervo", "Fireball", "Jagermeister", "Rumple Minze", "Tanqueray", "Beefeater", "Bombay Sapphire", "Hennessy",
	"Brunswick", "Valley-Dynamo", "Dynamo", "Olhausen", "Predator", "Rock-Ola", "Seeburg", "Wurlitzer", "TouchTunes", "Arachnid",
	"Medalist", "Blue Diamond", "Kamui", "Silver Cup"]


class Problems(list):
	def add(self, area: str, msg: str) -> None:
		self.append(f"[{area}] {msg}")


# ------------------------------------------------------------------------------------------------------------------------------
# rb_make_divebar_materials.py without Unreal: a stand-in `unreal` module, the masters built through a recording graph
# ------------------------------------------------------------------------------------------------------------------------------


class _Dummy:
	"""Absorbs any attribute access / call of the editor API."""

	def __getattr__(self, name):
		if name.startswith("__"):
			raise AttributeError(name)
		return _Dummy()

	def __call__(self, *args, **kwargs):
		return _Dummy()


def load_materials_module():
	fake = types.ModuleType("unreal")
	fake.__getattr__ = lambda name: _Dummy()  # type: ignore[attr-defined]
	paths = types.SimpleNamespace(project_dir=lambda: str(REPO), project_saved_dir=lambda: str(REPO / "Saved"))
	fake.Paths = paths  # type: ignore[attr-defined]
	sys.modules["unreal"] = fake
	sys.path.insert(0, str(EDITOR))
	import rb_make_divebar_materials as mats  # noqa: PLC0415

	return mats


def record_masters(mats) -> dict:
	"""master name -> {parameter name: kind} from the real make_* functions."""
	recorded: dict = {}

	class Recorder(mats.Graph):
		def __init__(self, path: str, **_props):  # noqa: D401 - no asset, no editor
			self.path, self.m, self.y, self.params = path, _Dummy(), 0, []
			self.kinds = recorded.setdefault(path.rsplit("/", 1)[1], {})

		def node(self, cls, x: int = -600, **props):
			return _Dummy()

		def link(self, *args, **kwargs) -> None:
			return None

		def front(self, *args, **kwargs) -> None:
			return None

		def finish(self) -> str:
			return self.path

		def scalar(self, name, value, group="Venue", x=-1400):
			self.kinds[name] = "scalar"
			return super().scalar(name, value, group, x)

		def vector(self, name, rgb, group="Venue", x=-1400):
			self.kinds[name] = "vector"
			return super().vector(name, rgb, group, x)

		def tex(self, name, texture, uv, sampler, group="Textures", x=-1100):
			self.kinds[name] = "texture"
			return super().tex(name, texture, uv, sampler, group, x)

		def tex_object(self, name, texture, sampler, group="Textures", x=-1100):
			self.kinds[name] = "texture"
			return super().tex_object(name, texture, sampler, group, x)

	class AnyTex(dict):
		def __missing__(self, key):
			return _Dummy()

	saved = mats.Graph
	mats.Graph = Recorder
	try:
		for make in (mats.make_opaque, mats.make_coated, mats.make_metal, mats.make_vinyl, mats.make_glass, mats.make_emissive, mats.make_floor):
			make(AnyTex(), _Dummy())
		mats.make_decal(AnyTex())
	finally:
		mats.Graph = saved
	return recorded


def default_texture_names() -> set:
	return {"T_DB_White", "T_DB_Flat_N", "T_DB_Mask_Neutral", "T_DB_WM_Neutral", "T_DB_Black", "T_DB_JukeboxRibs", "T_DB_Labels_BC",
		"T_DB_FloorMasks"}


# ------------------------------------------------------------------------------------------------------------------------------
# checks
# ------------------------------------------------------------------------------------------------------------------------------


def load_assets() -> dict:
	out = {}
	for path in sorted(PROPS.glob("*/*.json")):
		if path.stem == path.parent.name:
			out[path.stem] = (path, json.loads(path.read_text(encoding="utf-8")))
	return out


def check_metadata(assets: dict, p: Problems) -> None:
	"""VDB-T4 (metadata) and the importer contract of each asset JSON."""
	for asset_id, (path, m) in assets.items():
		for key in ("asset_id", "fbx", "bounds_min_m", "bounds_max_m", "target_dimensions_m", "tolerance_m", "material_slots",
				"ledger_asset_id", "ue_dir", "collision_profile"):
			if key not in m:
				p.add("T4", f"{asset_id}: metadata lacks {key}")
		if m.get("asset_id") != asset_id:
			p.add("T4", f"{path.name}: asset_id {m.get('asset_id')} != folder {asset_id}")
		if "-y" not in str(m.get("axis_convention", "")).replace(" ", ""):
			p.add("T4", f"{asset_id}: axis_convention must state the (x, -y, z) mapping the importers mirror")
		if not (path.parent / m.get("fbx", "?")).is_file():
			p.add("T4", f"{asset_id}: FBX {m.get('fbx')} missing")
		if m.get("wear_mask") and not (REPO / m["wear_mask"]).is_file():
			p.add("T4", f"{asset_id}: wear mask {m['wear_mask']} missing")
		tol = float(m.get("tolerance_m", 1.0))
		limit = HERO_TOL if m.get("priority") == "hero" else MID_TOL
		if tol > limit + 1e-9:
			p.add("T4", f"{asset_id}: tolerance {tol} m is looser than the spec's {limit} m ({m.get('priority')})")
		size = [m["bounds_max_m"][i] - m["bounds_min_m"][i] for i in range(3)]
		for axis in range(3):
			if abs(size[axis] - m["target_dimensions_m"][axis]) > tol + 1e-6:
				p.add("T4", f"{asset_id}: {'XYZ'[axis]} {size[axis]:.4f} m vs target {m['target_dimensions_m'][axis]:.4f} m (+-{tol})")
		for check in m.get("spec_checks", []):
			ct = float(check.get("tolerance_m", tol))
			if ct > limit + 1e-9:
				p.add("T4", f"{asset_id}: spec check '{check.get('what')}' tolerance {ct} looser than {limit}")
			have, want = check.get("measured_m"), check.get("spec_m")
			pairs = zip(have, want) if isinstance(have, list) else [(have, want)]
			for h, w in pairs:
				if w is not None and h is not None and abs(float(h) - float(w)) > ct + 1e-6:
					p.add("T4", f"{asset_id}: spec check '{check.get('what')}' {h} vs {w} (+-{ct})")
		if int(m.get("collision_hulls", 0)) == 0 and m.get("collision_profile") == "RbVenueBlock":
			p.add("T4", f"{asset_id}: blocks the pawn (RbVenueBlock) without any UCX hull")


def check_ledger(assets: dict, rows: dict, p: Problems) -> None:
	"""VDB-T7: rows for meshes and external inputs, licences."""
	for asset_id, (_, m) in assets.items():
		for key in [m.get("ledger_asset_id", f"SM_DB_{asset_id}")] + [str(x) for x in m.get("external_inputs", [])]:
			found = rows.get(key, [])
			if not found:
				p.add("T7", f"{asset_id}: no ledger row for {key}")
			for row in found:
				licence = row.get("licence", "")
				if any(t.lower() in licence.lower() for t in NC_MARKERS) or licence not in LICENCES_OK:
					p.add("T7", f"{key}: licence {licence!r} not allowed")


def check_cc0(mats, rows: dict, p: Problems) -> dict:
	inputs = json.loads((ART / "cc0_inputs.json").read_text(encoding="utf-8"))
	lock = json.loads((ART / "cc0_inputs.lock.json").read_text(encoding="utf-8"))
	prepared = json.loads((ART / "Textures" / "CC0" / "cc0_prepared.json").read_text(encoding="utf-8"))
	listed = set()
	for entry in inputs:
		key = f"{entry['source']}/{entry['id']}"
		listed.add(entry["id"])
		if entry["source"] not in mats.CC0_SOURCES:
			p.add("T7", f"CC0 input {key}: source not allowed")
		if not lock.get(key, {}).get("files"):
			p.add("T7", f"CC0 input {key}: no SHA-256 pins in cc0_inputs.lock.json")
		if entry["id"] not in prepared:
			p.add("T7", f"CC0 input {key}: not prepared (fetch_cc0.py --prepare)")
	for asset_id, info in prepared.items():
		if asset_id not in listed:
			p.add("T7", f"prepared CC0 set {asset_id} is not in cc0_inputs.json")
		for role, name in info.get("maps", {}).items():
			if not (ART / "Textures" / "CC0" / asset_id / name).is_file():
				p.add("T7", f"prepared CC0 file {asset_id}/{name} missing")
	for problem in mats.cc0_ledger_problems(prepared, rows):
		p.add("T7", problem)
	return prepared


def check_fonts(rows: dict, p: Problems) -> None:
	fonts = REPO / "Art" / "Fonts"
	lock = json.loads((fonts / "fonts.lock.json").read_text(encoding="utf-8"))
	for family, info in sorted(lock.items()):
		licence_files = [n for n in info["files"] if n.upper().startswith(("OFL", "LICENSE"))]
		if not licence_files:
			p.add("T7", f"font {family}: no licence file pinned")
		for name, sha in sorted(info["files"].items()):
			path = fonts / family / name
			if not path.is_file():
				p.add("T7", f"font {family}/{name} missing")
			elif hashlib.sha256(path.read_bytes()).hexdigest() != sha:
				p.add("T7", f"font {family}/{name}: SHA-256 differs from the pin (LFS pointer? git lfs pull)")
		found = rows.get(f"font_{family}", [])
		if not any(r.get("licence") == info.get("licence") for r in found):
			p.add("T7", f"font {family}: no ledger row with licence {info.get('licence')}")


def check_brands(p: Problems) -> None:
	source = (REPO / "Tools" / "art" / "text_textures.py").read_text(encoding="utf-8")
	literals = " | ".join(a or b for a, b in re.findall(r"\"([^\"\n]*)\"|'([^'\n]*)'", source))
	for brand in REAL_BRANDS:
		if re.search(r"(?<![A-Za-z])" + re.escape(brand) + r"(?![A-Za-z])", literals, re.IGNORECASE):
			p.add("T7", f"text_textures.py names the real brand {brand!r}")


def check_materials(mats, assets: dict, prepared: dict, p: Problems) -> None:
	recorded = record_masters(mats)
	textures = default_texture_names()
	for asset_id, info in prepared.items():
		textures.update(f"T_DB_CC0_{asset_id}_{role}" for role in info.get("maps", {}))
	decal_sets = json.loads((ART / "Export" / "Decals" / "decals.json").read_text(encoding="utf-8"))["sets"]
	for set_name in decal_sets:
		textures.update(f"T_DB_DecalAtlas_{set_name}_{role}" for role in ("BC", "N", "R"))
	for name, (parent, params) in sorted(mats.INSTANCES.items()):
		kinds = recorded.get(parent)
		if kinds is None:
			p.add("MAT", f"{name}: master {parent} is not built by rb_make_divebar_materials.py")
			continue
		for key, value in params.items():
			if key == "size:":
				if value not in prepared:
					p.add("MAT", f"{name}: CC0 set {value} not prepared")
				continue
			if key in mats.PSEUDO_KEYS:
				continue
			param = key[4:] if key.startswith("tex:") else key
			kind = kinds.get(param)
			if kind is None:
				p.add("MAT", f"{name}: {parent} has no parameter {param}")
			elif isinstance(value, str) != (kind == "texture"):
				p.add("MAT", f"{name}: {param} is a {kind} parameter, value {value!r}")
			if isinstance(value, str) and value not in textures:
				p.add("MAT", f"{name}: texture {value} is not made by the generator")
	decal_master = recorded.get("M_DB_Decal", {})
	for extra in [mats.DECAL_BASE.get(s, {}) for s in decal_sets] + [v for vs in mats.DECAL_VARIANTS.values() for _, v in vs]:
		for key in extra:
			if key not in decal_master:
				p.add("MAT", f"decal instance parameter {key} is not on M_DB_Decal")
	# material slots: the instances the importers look up by name
	for asset_id, (_, m) in assets.items():
		for slot in m.get("material_slots", []):
			if slot not in mats.INSTANCES:
				p.add("MAT", f"{asset_id}: material slot {slot} has no MI_DB_* instance in rb_make_divebar_materials.INSTANCES")
	# decal placements (the level hand-off to M2-A)
	placements = json.loads((ART / "Export" / "Decals" / "decal_placements.json").read_text(encoding="utf-8"))["decals"]
	labels = set()
	for d in placements:
		set_name, variant = d.get("set"), d.get("variant", "")
		allowed = {""} | ({"Sticky"} if decal_sets.get(set_name, {}).get("roughness_only_variant") else set())
		allowed |= {v for v, _ in mats.DECAL_VARIANTS.get(set_name, [])}
		if set_name not in decal_sets or variant not in allowed:
			p.add("DECAL", f"{d.get('label')}: no decal instance for set {set_name} variant {variant!r}")
		want = f"{mats.DECAL_DIR}/MI_DB_Decal_{set_name}{'_' + variant if variant else ''}_01"
		if d.get("material") != want:
			p.add("DECAL", f"{d.get('label')}: material {d.get('material')} != {want}")
		cell = int(d.get("cell", -1))
		if not 0 <= cell <= 15 or math.floor(min(max(float(d.get("decal_color_r", -1)), 0.0), 1.0) * 15.999) != cell:
			p.add("DECAL", f"{d.get('label')}: decal colour R {d.get('decal_color_r')} does not decode to cell {cell} (M_DB_Decal)")
		if d.get("label") in labels:
			p.add("DECAL", f"duplicate decal label {d.get('label')}")
		labels.add(d.get("label"))
		if len(d.get("decal_size_cm", [])) != 3 or min(d["decal_size_cm"]) <= 0.0:
			p.add("DECAL", f"{d.get('label')}: bad decal_size_cm {d.get('decal_size_cm')}")


def check_decal_normals(p: Problems) -> None:
	"""The decal normal atlases are DirectX (Unreal): green falls where the height rises down the image (the atlas's R texture keeps
	the height in G). Needs numpy + Pillow (as Tools/art/text_textures.py); skipped without them."""
	try:
		import numpy as np  # noqa: PLC0415
		from PIL import Image  # noqa: PLC0415
	except ImportError:
		print("check_m2b: numpy / Pillow missing - decal normal convention not checked")
		return
	sets = json.loads((ART / "Export" / "Decals" / "decals.json").read_text(encoding="utf-8"))["sets"]
	for set_name, info in sorted(sets.items()):
		n = np.asarray(Image.open(REPO / info["textures"]["N"]).convert("RGB"), dtype=np.float32) / 127.5 - 1.0
		h = np.asarray(Image.open(REPO / info["textures"]["R"]).convert("RGB"), dtype=np.float32)[..., 1] / 255.0
		for axis, channel, what in ((0, 1, "green (rows)"), (1, 0, "red (columns)")):
			d = np.gradient(h, axis=axis)
			mask = np.abs(d) > 1e-3
			if mask.sum() < 100:
				continue
			corr = float(np.corrcoef(n[..., channel][mask], d[mask])[0, 1])
			if corr > -0.5:
				p.add("DECAL", f"T_DB_DecalAtlas_{set_name}_N: {what} correlates {corr:+.2f} with the height slope (DirectX wants < 0)")


def check_instances(assets: dict, p: Problems) -> None:
	for asset_id, (_, m) in assets.items():
		for inst in m.get("prop_instances", []):
			sub = str(inst.get("asset", "")).replace("SM_DB_", "")
			if sub not in assets:
				p.add("INST", f"{asset_id}: prop instance asset {inst.get('asset')} is not exported")
			if len(inst.get("location_ue_m", [])) != 3:
				p.add("INST", f"{asset_id}: prop instance without location_ue_m")


def main() -> int:
	p = Problems()
	mats = load_materials_module()
	rows = mats.ledger_rows(str(REPO))
	assets = load_assets()
	if not assets:
		p.add("T4", f"no asset metadata under {PROPS}")
	check_metadata(assets, p)
	check_ledger(assets, rows, p)
	prepared = check_cc0(mats, rows, p)
	check_fonts(rows, p)
	check_brands(p)
	check_materials(mats, assets, prepared, p)
	check_decal_normals(p)
	check_instances(assets, p)
	for line in p:
		print(line)
	print(f"check_m2b: {len(assets)} assets, {len(mats.INSTANCES)} instances, {len(p)} problem(s)")
	return 1 if p else 0


if __name__ == "__main__":
	sys.exit(main())
