#!/usr/bin/env python3
"""Fetches the OFL / Apache-2.0 fonts of the text textures into Art/Fonts/<family>/ (venue-dive-bar 8.2; ui-ux 4 shares Caveat and
Kalam from the same folder). Pinned: the first run writes Art/Fonts/fonts.lock.json (SHA-256 of every file), later runs fail when
a file differs. Every family gets a ledger row (source "font") in Docs/licenses/ledger/M2-B.csv and its licence file next to it.
Source: the Google Fonts repository on GitHub (fonts published under OFL-1.1 / Apache-2.0). Host side, stdlib only.

  python Tools/art/fetch_fonts.py            # download missing files, verify pins
  python Tools/art/fetch_fonts.py --verify   # only check the pins of the committed files
Owner: M2-B.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
import urllib.parse
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FONTS = REPO / "Art" / "Fonts"
LOCK = FONTS / "fonts.lock.json"
LEDGER = REPO / "Docs" / "licenses" / "ledger" / "M2-B.csv"
BASE = "https://raw.githubusercontent.com/google/fonts/main/"
UA = {"User-Agent": "RawBreak-fetch_fonts/1.0"}

# family folder -> (licence, repo dir, [font files], licence file, what it is used for)
FAMILIES = {
	"BebasNeue": ("OFL-1.1", "ofl/bebasneue", ["BebasNeue-Regular.ttf"], "OFL.txt", "tall condensed caps: beer / vodka labels, EXIT style"),
	"Oswald": ("OFL-1.1", "ofl/oswald", ["Oswald[wght].ttf"], "OFL.txt", "condensed sans: small print, prices, title strips"),
	"Pacifico": ("OFL-1.1", "ofl/pacifico", ["Pacifico-Regular.ttf"], "OFL.txt", "script: Old Castor, Marquee jukebox script"),
	"Lobster": ("OFL-1.1", "ofl/lobster", ["Lobster-Regular.ttf"], "OFL.txt", "bold script: Dockhand rum, Hollenbeck"),
	"Rye": ("OFL-1.1", "ofl/rye", ["Rye-Regular.ttf"], "OFL.txt", "western display: Ashby Ridge bourbon, El Tordo"),
	"Kalam": ("OFL-1.1", "ofl/kalam", ["Kalam-Regular.ttf", "Kalam-Bold.ttf"], "OFL.txt", "chalkboard hand (shared with the UI score slate)"),
	"Caveat": ("OFL-1.1", "ofl/caveat", ["Caveat[wght].ttf"], "OFL.txt", "handwriting: DEACON'S PICK strip, notes (shared with the UI)"),
	"CourierPrime": ("OFL-1.1", "ofl/courierprime", ["CourierPrime-Regular.ttf"], "OFL.txt", "typewriter: jukebox title strips"),
	"SpecialElite": ("Apache-2.0", "apache/specialelite", ["SpecialElite-Regular.ttf"], "LICENSE.txt", "worn typewriter: plates, stickers"),
	"PermanentMarker": ("Apache-2.0", "apache/permanentmarker", ["PermanentMarker-Regular.ttf"], "LICENSE.txt", "marker: tape notes, prices"),
}
LICENCE_URL = {"OFL-1.1": "https://openfontlicense.org", "Apache-2.0": "https://www.apache.org/licenses/LICENSE-2.0"}
LEDGER_COLUMNS = ["asset_id", "used_by", "source", "source_ref", "author", "licence", "licence_url", "date", "account", "sha256",
	"modified", "ai_generated", "steam_ai_disclosure", "trademark_check", "notes"]


def _get(url: str) -> bytes:
	with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as response:
		return response.read()


def _ledger(rows: list[dict]) -> None:
	existing = set()
	if LEDGER.exists():
		with LEDGER.open(newline="", encoding="utf-8") as handle:
			existing = {(r["asset_id"], r["source"]) for r in csv.DictReader(handle)}
	new_file = not LEDGER.exists()
	LEDGER.parent.mkdir(parents=True, exist_ok=True)
	with LEDGER.open("a", newline="", encoding="utf-8") as handle:
		writer = csv.DictWriter(handle, fieldnames=LEDGER_COLUMNS)
		if new_file:
			writer.writeheader()
		for row in rows:
			if (row["asset_id"], row["source"]) not in existing:
				writer.writerow(row)


def main() -> int:
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("--verify", action="store_true")
	a = p.parse_args()
	lock = json.loads(LOCK.read_text(encoding="utf-8")) if LOCK.exists() else {}
	failures, rows = [], []
	for family, (licence, repo_dir, files, licence_file, use) in FAMILIES.items():
		folder = FONTS / family
		folder.mkdir(parents=True, exist_ok=True)
		digests = {}
		for name in files + [licence_file]:
			path = folder / name
			if not path.exists():
				if a.verify:
					failures.append(f"{family}/{name}: missing")
					continue
				path.write_bytes(_get(BASE + repo_dir + "/" + urllib.parse.quote(name)))
			digests[name] = hashlib.sha256(path.read_bytes()).hexdigest()
		pinned = lock.get(family, {}).get("files")
		if pinned and pinned != digests:
			failures.append(f"{family}: files differ from the pinned SHA-256")
		lock[family] = {"files": digests, "licence": licence, "source": BASE + repo_dir, "use": use}
		rows.append({"asset_id": f"font_{family}", "used_by": "M2-B (Tools/art/text_textures.py); UI (Caveat, Kalam)", "source": "font",
			"source_ref": "https://github.com/google/fonts/tree/main/" + repo_dir, "author": "see the licence file in Art/Fonts/" + family,
			"licence": licence, "licence_url": LICENCE_URL[licence], "date": "2026-09-29", "account": "",
			"sha256": digests.get(files[0], ""), "modified": "n", "ai_generated": "n", "steam_ai_disclosure": "n", "trademark_check": "n/a",
			"notes": use})
		print(f"[fetch_fonts] {family}: {sorted(digests)}")
	if not a.verify:
		LOCK.write_text(json.dumps(lock, indent=1, sort_keys=True), encoding="utf-8")
		_ledger(rows)
	for line in failures:
		print(f"[fetch_fonts] FAIL {line}")
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
