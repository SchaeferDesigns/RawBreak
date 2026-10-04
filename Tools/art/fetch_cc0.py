#!/usr/bin/env python3
"""Fetches CC0 scan textures by ID from Poly Haven and ambientCG (venue-dive-bar 6.3 / 13.1 / 13.9; Docs/ue-architecture.md 18.9).
The ONLY external asset sources allowed in M2 (both publish every asset under CC0 1.0). Host side, Python 3.11 stdlib only.

  python Tools/art/fetch_cc0.py --list Art/DiveBar/cc0_inputs.json --package M2-B
  python Tools/art/fetch_cc0.py --list Art/Tables/cc0_inputs.json --package M2-L --verify     # hashes only, no download
  python Tools/art/fetch_cc0.py --list Art/DiveBar/cc0_inputs.json --package M2-B --verify --prepare Art/DiveBar/Textures/CC0

--prepare DIR (M2-B, needs Pillow): converts the pinned raw files into the engine inputs DIR/<id>/T_DB_CC0_<id>_{BC,N,M}.jpg at the
entry's prep_px ("BC" sRGB colour / mask, "N" DirectX normal, "M" linear R = AO, G = roughness, B = height) and writes
DIR/cc0_prepared.json (maps + real-world tile size in metres from the source's API, the UV0 world-scale tiling of the materials).
The prepared files are committed (LFS, small); Art/Third stays ignored.

List file (JSON array; owned by the package that uses the inputs):
  [{"source": "polyhaven", "id": "rosewood_veneer1", "res": "2k", "maps": ["Diffuse", "nor_dx", "Rough", "AO"], "format": "png"},
   {"source": "ambientcg", "id": "Rubber004", "res": "2K", "format": "PNG"}]
Downloads into Art/Third/<source>/<id>/ (git-IGNORED: several GB, re-fetchable), pins every file's SHA-256 in
<list>.lock.json (committed; a later run fails if a file changed upstream) and appends one row per asset to the package's ledger
fragment Docs/licenses/ledger/<package>.csv (merged into Docs/licenses/asset-ledger.csv at integration). DirectX normals only
(Poly Haven nor_dx, ambientCG NormalDX). Owner: M2-B (working version from the M2 architect step; M2-B extends it).
"""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import hashlib
import io
import json
import sys
import urllib.request
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
UA = {"User-Agent": "RawBreak-fetch_cc0/1.0 (+CC0 inputs, venue-dive-bar 13.1)"}
LEDGER_COLUMNS = ["asset_id", "used_by", "source", "source_ref", "author", "licence", "licence_url", "date", "account", "sha256",
	"modified", "ai_generated", "steam_ai_disclosure", "trademark_check", "notes"]
LICENCE_URL = {"polyhaven": "https://polyhaven.com/license", "ambientcg": "https://docs.ambientcg.com/license/"}


def _get(url: str) -> bytes:
	with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as response:
		return response.read()


def _json(url: str):
	return json.loads(_get(url).decode("utf-8"))


def _sha256(data: bytes) -> str:
	return hashlib.sha256(data).hexdigest()


def fetch_polyhaven(entry: dict, dest: Path) -> tuple[dict[str, str], str, str]:
	asset_id, res, fmt = entry["id"], entry.get("res", "2k"), entry.get("format", "png")
	files = _json(f"https://api.polyhaven.com/files/{asset_id}")
	info = _json(f"https://api.polyhaven.com/info/{asset_id}")
	hashes: dict[str, str] = {}
	for map_name in entry.get("maps", ["Diffuse", "nor_dx", "Rough", "AO"]):
		item = files[map_name][res][fmt]
		data = _get(item["url"])
		name = item["url"].rsplit("/", 1)[-1]
		(dest / name).write_bytes(data)
		hashes[name] = _sha256(data)
	authors = ", ".join(sorted(info.get("authors", {}).keys()))
	dims = info.get("dimensions") or []
	if len(dims) >= 2 and dims[0]:
		entry["_size_m"] = [round(float(dims[0]) / 1000.0, 4), round(float(dims[1]) / 1000.0, 4)]   # the API gives millimetres
	return hashes, f"https://polyhaven.com/a/{asset_id}", authors


def fetch_ambientcg(entry: dict, dest: Path) -> tuple[dict[str, str], str, str]:
	asset_id, res, fmt = entry["id"], entry.get("res", "2K"), entry.get("format", "PNG")
	data = _json(f"https://ambientcg.com/api/v2/full_json?id={asset_id}&include=downloadData")
	asset = data["foundAssets"][0]
	if asset.get("dimensionX"):
		entry["_size_m"] = [round(float(asset["dimensionX"]) / 100.0, 4), round(float(asset.get("dimensionY") or asset["dimensionX"]) / 100.0, 4)]
	downloads = asset["downloadFolders"]["default"]["downloadFiletypeCategories"]["zip"]["downloads"]
	pick = next(d for d in downloads if d["attribute"] == f"{res}-{fmt}")
	archive = _get(pick["fullDownloadPath"])
	hashes: dict[str, str] = {}
	with zipfile.ZipFile(io.BytesIO(archive)) as zf:
		for member in sorted(zf.namelist()):
			# Texture maps only (the zips also carry .blend / .usdc / .mtlx / preview files); DirectX normals only.
			if member.endswith("/") or "NormalGL" in member or not member.lower().endswith((".jpg", ".png", ".exr", ".tif")):
				continue
			if Path(member).stem == asset_id:  # the preview thumbnail
				continue
			content = zf.read(member)
			(dest / Path(member).name).write_bytes(content)
			hashes[Path(member).name] = _sha256(content)
	return hashes, f"https://ambientcg.com/a/{asset_id}", "ambientCG (Lennart Demes)"


def append_ledger(fragment: Path, rows: list[dict]) -> None:
	fragment.parent.mkdir(parents=True, exist_ok=True)
	existing: set[tuple[str, str]] = set()
	if fragment.exists():
		with fragment.open(newline="", encoding="utf-8") as handle:
			existing = {(r["asset_id"], r["source"]) for r in csv.DictReader(handle)}
	new_file = not fragment.exists()
	with fragment.open("a", newline="", encoding="utf-8") as handle:
		writer = csv.DictWriter(handle, fieldnames=LEDGER_COLUMNS)
		if new_file:
			writer.writeheader()
		for row in rows:
			if (row["asset_id"], row["source"]) not in existing:
				writer.writerow(row)


# --------------------------------------------------------------------------------------------------------------------
# --prepare: raw CC0 maps -> the engine inputs of the venue materials (M2-B)
# --------------------------------------------------------------------------------------------------------------------

# Raw file name fragments per role (Poly Haven "<id>_<map>_<res>.jpg", ambientCG "<id>_<res>-<fmt>_<Map>.jpg").
ROLE_KEYS = {
	"color": ("_diff_", "_col_", "_Color."),
	"normal": ("_nor_dx_", "_NormalDX."),
	"rough": ("_rough_", "_Roughness."),
	"ao": ("_ao_", "_AmbientOcclusion."),
	"height": ("_disp_", "_Displacement."),
	"opacity": ("_Opacity.",),
}


def _find_role(files: list[Path], role: str) -> Path | None:
	for path in files:
		name = path.name
		if any(key.lower() in name.lower() for key in ROLE_KEYS[role]):
			return path
	return None


def prepare(entries: list[dict], lock: dict, raw_root: Path, out_root: Path) -> list[str]:
	"""Writes DIR/<id>/T_DB_CC0_<id>_{BC,N,M}.jpg + DIR/cc0_prepared.json. Returns failures."""
	from PIL import Image  # host Python with Pillow (Tools/art only)

	failures: list[str] = []
	manifest: dict[str, dict] = {}
	for entry in entries:
		source, asset_id = entry["source"], entry["id"]
		key = f"{source}/{asset_id}"
		raw = raw_root / source / asset_id
		files = sorted(raw.glob("*")) if raw.exists() else []
		if not files:
			failures.append(f"{key}: no raw files in {raw} (run without --verify first)")
			continue
		px = int(entry.get("prep_px", 1024))
		dest = out_root / asset_id
		dest.mkdir(parents=True, exist_ok=True)
		maps: dict[str, str] = {}
		color = _find_role(files, "color")
		if color is not None:
			img = Image.open(color).convert("RGB").resize((px, px), Image.LANCZOS)
			name = f"T_DB_CC0_{asset_id}_BC.jpg"
			img.save(dest / name, quality=92, subsampling=0)
			maps["BC"] = name
		normal = _find_role(files, "normal")
		if normal is not None:
			img = Image.open(normal).convert("RGB").resize((px, px), Image.LANCZOS)
			name = f"T_DB_CC0_{asset_id}_N.jpg"
			img.save(dest / name, quality=95, subsampling=0)
			maps["N"] = name
		rough, ao, height = _find_role(files, "rough"), _find_role(files, "ao"), _find_role(files, "height")
		if rough is not None or ao is not None or height is not None:
			def channel(path, default):
				if path is None:
					return Image.new("L", (px, px), default)
				return Image.open(path).convert("L").resize((px, px), Image.LANCZOS)
			packed = Image.merge("RGB", (channel(ao, 255), channel(rough, 128), channel(height, 128)))
			name = f"T_DB_CC0_{asset_id}_M.jpg"
			packed.save(dest / name, quality=95, subsampling=0)
			maps["M"] = name
		opacity = _find_role(files, "opacity")
		if opacity is not None:
			img = Image.open(opacity).convert("L").resize((px, px), Image.LANCZOS)
			name = f"T_DB_CC0_{asset_id}_O.jpg"
			img.save(dest / name, quality=95)
			maps["O"] = name
		if not maps:
			failures.append(f"{key}: no usable maps among {[f.name for f in files]}")
			continue
		manifest[asset_id] = {"source": source, "maps": maps, "px": px, "size_m": lock.get(key, {}).get("size_m"),
			"ref": lock.get(key, {}).get("ref", ""), "use": entry.get("use", "")}
		print(f"[fetch_cc0] prepared {key}: {sorted(maps)} at {px} px, size {manifest[asset_id]['size_m']}")
	(out_root / "cc0_prepared.json").write_text(json.dumps(manifest, indent=1, sort_keys=True), encoding="utf-8")
	return failures


def main() -> int:
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("--list", required=True)
	p.add_argument("--package", required=True, help="owning work package, e.g. M2-B (ledger fragment + used_by)")
	p.add_argument("--dest", default=str(REPO / "Art" / "Third"))
	p.add_argument("--verify", action="store_true", help="only check the pinned hashes of already downloaded files")
	p.add_argument("--prepare", default="", help="write the engine inputs (BC / N / M) of every entry into this folder (M2-B)")
	a = p.parse_args()

	list_path = Path(a.list)
	lock_path = list_path.with_suffix(".lock.json")
	entries = json.loads(list_path.read_text(encoding="utf-8"))
	lock = json.loads(lock_path.read_text(encoding="utf-8")) if lock_path.exists() else {}
	rows, failures = [], []
	for entry in entries:
		source, asset_id = entry["source"], entry["id"]
		if source not in LICENCE_URL:
			failures.append(f"{asset_id}: source {source} is not allowed (Poly Haven / ambientCG only)")
			continue
		dest = Path(a.dest) / source / asset_id
		dest.mkdir(parents=True, exist_ok=True)
		key = f"{source}/{asset_id}"
		if a.verify:
			for name, digest in lock.get(key, {}).get("files", {}).items():
				path = dest / name
				if not path.exists() or _sha256(path.read_bytes()) != digest:
					failures.append(f"{key}/{name}: missing or hash mismatch")
			continue
		hashes, ref, author = (fetch_polyhaven if source == "polyhaven" else fetch_ambientcg)(entry, dest)
		pinned = lock.get(key, {}).get("files")
		if pinned and pinned != hashes:
			failures.append(f"{key}: upstream files changed since they were pinned")
		lock[key] = {"files": hashes, "ref": ref}
		if entry.get("_size_m"):
			lock[key]["size_m"] = entry["_size_m"]
		print(f"[fetch_cc0] {key}: {len(hashes)} file(s)")
		rows.append({"asset_id": asset_id, "used_by": a.package, "source": source, "source_ref": ref, "author": author,
			"licence": "CC0-1.0", "licence_url": LICENCE_URL[source], "date": _dt.date.today().isoformat(), "account": "",
			"sha256": _sha256(json.dumps(hashes, sort_keys=True).encode("utf-8")), "modified": "n", "ai_generated": "n",
			"steam_ai_disclosure": "n", "trademark_check": "n/a", "notes": f"{entry.get('res', '')} {entry.get('format', '')}".strip()})
	if not a.verify:
		lock_path.write_text(json.dumps(lock, indent=1, sort_keys=True), encoding="utf-8")
		append_ledger(REPO / "Docs" / "licenses" / "ledger" / f"{a.package}.csv", rows)
	if a.prepare and not failures:
		failures += prepare(entries, lock, Path(a.dest), Path(a.prepare) if Path(a.prepare).is_absolute() else REPO / a.prepare)
	for line in failures:
		print(f"[fetch_cc0] FAIL {line}")
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
