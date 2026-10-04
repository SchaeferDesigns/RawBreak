"""Body step of the Blender pipeline (runs INSIDE Blender through Tools/blender/divebar/db_build_all.py, or alone:
`python Tools/blender/rbbl.py run Tools/blender/body/bd_build_all.py`). Owner: M3-H. Docs/ue-architecture.md 19.4.

M3-H may generate body helpers here (shirt cuffs, region masks of the template mannequin exported by rb_import_body.py) and export
them to Art/Body/Export/<Asset>/ (FBX + <Asset>.json, like the venue generators). Plan-step stub: logs and does nothing (TODO(M3-H)).
"""

from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "common"))
import rb_bl  # noqa: E402


def main() -> None:
	rb_bl.log("bd_build_all: not implemented yet (M3-H, Docs/ue-architecture.md 19.4)")


main()
