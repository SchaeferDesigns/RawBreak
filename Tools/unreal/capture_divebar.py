#!/usr/bin/env python3
"""All dive-bar captures of a milestone (M2-A; Docs/ue-architecture.md 18.8, venue-dive-bar 12.1-12.3). Host side.

  python Tools/unreal/capture_divebar.py --set db1|db2|db3|m2 [--views V01,V03] [--res 1920x1080]

Runs `rbue.py capture --map /Game/Generated/Maps/L_DiveBar --camera RbCam_DB_<View> --warmup-seconds 12` per view (the Eyes
exposure needs ~12 s to adapt down to EV 3, venue-dive-bar 4.5), strict shader checks, into Docs/images/divebar/<set>/<View>.png,
and collects the adapted EV100 of every view from the capture log into Docs/images/divebar/<set>/ev_report.txt (VDB-T2).
V10 (plan) hides the actors tagged RbDB_Ceiling. Sets: db1 = V10 + V01..V04 (greybox), db2 = V01..V05, V08 + lux report,
db3 = V02, V03, V04, V06, V07, m2 = V01..V09 + TH1..TH7. Owner: M2-A. STUB of the M2 architect step: TODO(M2-A).
"""

from __future__ import annotations

import argparse
import sys


def main() -> int:
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("--set", default="m2")
	p.add_argument("--views", default="")
	p.add_argument("--res", default="1920x1080")
	p.parse_args()
	print("[capture_divebar] not implemented yet (M2-A)")
	return 2


if __name__ == "__main__":
	sys.exit(main())
