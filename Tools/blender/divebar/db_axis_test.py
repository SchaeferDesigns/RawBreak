"""DB-0 axis / scale test asset SM_DB_AxisTest (venue-dive-bar 12.1 DB-0, 13.4; Docs/ue-architecture.md 18.8). Runs INSIDE Blender:

  python Tools/blender/rbbl.py run Tools/blender/divebar/db_axis_test.py

A 1.000 m cube with its pivot on the floor (z 0..1) and three markers that never leave the 1.000 m bounds:
  * an ARROW on the top face pointing +X (a 10 mm relief: the cube body is 0.990 m high, the arrow reaches z = 1.000);
  * the letters "UP" as a 10 mm relief on the +X face, upright (the body ends at x = +0.490, the letters reach x = +0.500);
  * a +Y marker: a 100 x 100 mm relief square in the top face's +X +Y quadrant (reads "which side is +Y" without text).
So the bounds are exactly 1.000 x 1.000 x 1.000 m, and the Unreal test (RawBreak.Unit.Venue.AxisTest) reads the mesh vertices to
prove: bounds 100.0 +- 0.1 cm, pivot on the floor, the arrow head (the wide part of the top relief) at +X, the +Y marker at +Y
(a mirrored import fails), the letters on the +X face. Owner: M2-A.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "common"))
import rb_bl  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

ASSET = "AxisTest"
RELIEF = 0.010


def main() -> None:
	a = rb_bl.args()
	rb_bl.reset_scene()
	b = rb_bl.Builder("AxisTest")
	# Body: x -0.5 .. 0.49, y -0.5 .. 0.5, z 0 .. 0.99 (the reliefs fill the last 10 mm on +X and +Z).
	b.box(-0.5, 0.5 - RELIEF, -0.5, 0.5, 0.0, 1.0 - RELIEF, "MI_DB_AxisTest_Body")
	# Arrow on the top face pointing +X: shaft x -0.40 .. 0.10 (half width 0.06), head x 0.10 .. 0.45 (half width 0.20 at its base).
	top0, top1 = 1.0 - RELIEF, 1.0
	shaft = [(-0.40, -0.06), (0.10, -0.06), (0.10, 0.06), (-0.40, 0.06)]
	b.prism(shaft, top0, top1, "MI_DB_AxisTest_Arrow")
	head = [(0.10, -0.20), (0.45, 0.0), (0.10, 0.20)]
	b.prism(head, top0, top1, "MI_DB_AxisTest_Arrow")
	# +Y marker: square in the +X +Y quadrant of the top face.
	b.prism([(0.30, 0.33), (0.40, 0.33), (0.40, 0.43), (0.30, 0.43)], top0, top1, "MI_DB_AxisTest_Marker")
	body = b.build()

	# "UP" letters: Blender text -> mesh, 0.30 m cap height, extruded RELIEF, stood upright on the +X face (readable from +X).
	import bpy
	curve = bpy.data.curves.new("UpText", type="FONT")
	curve.body = "UP"
	curve.size = 0.40
	curve.extrude = RELIEF / 2.0
	curve.align_x = "CENTER"
	curve.align_y = "CENTER"
	text = bpy.data.objects.new("UpText", curve)
	bpy.context.scene.collection.objects.link(text)
	bpy.context.view_layer.objects.active = text
	text.select_set(True)
	bpy.ops.object.convert(target="MESH")
	text = bpy.context.view_layer.objects.active
	# Text lies in its XY plane reading along +X with the extrusion along Z (centred). Stand it up on the +X face so that it reads
	# in UNREAL (left-handed: seen from +X looking toward -X, "right" is -Y): text X -> world -Y, text Y -> +Z, text Z -> +X. The
	# map has determinant -1 (it reads mirrored in Blender's right-handed viewport), so the winding is reversed afterwards.
	rot = Matrix(((0.0, 0.0, 1.0), (-1.0, 0.0, 0.0), (0.0, 1.0, 0.0))).to_4x4()
	text.data.transform(rot)
	rb_bl.flip_winding(text.data)
	text.data.transform(Matrix.Translation(Vector((0.5 - RELIEF / 2.0, 0.0, 0.5))))
	text.data.materials.clear()
	text.data.materials.append(rb_bl.material("MI_DB_AxisTest_Arrow"))
	rb_bl.uv_world_scale(text)

	collision = rb_bl.Collision("SM_DB_AxisTest")
	collision.box(-0.5, 0.5, -0.5, 0.5, 0.0, 1.0)
	meta = {
		"family": "AxisTest",
		"ue_folder": "Arch/AxisTest",
		"profile": "RbVenueBlock",
		"acoustic_material": "none",
		"priority": "hero",
		"pivot": "floor centre",
		"notes": "DB-0 axis / scale test (not placed in L_DiveBar)",
	}
	rb_bl.export_asset(ASSET, [body, text], a.out, meta, collision=collision, target_m=(1.0, 1.0, 1.0), tolerance_m=0.0005, package="M2-A")


main()
