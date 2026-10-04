"""M2-L look-dev rooms (Docs/ue-architecture.md 18.7). Owner: M2-L.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2l.py

Writes (git-ignored scratch under /Game/Dev/M2L):
  * L_TableLookDev: the 7-ft coin-op bar box ("HALVERSON Stallion 7", venue-dive-bar 3.1) with the OldBarOversizedCue ball set and
    the dive-bar table condition, a placed ARbBallSet with MI_RbBall_DiveBar, in a dark room under an emulation of The Low Bridge's
    3-shade bar lamp (rb_m1_layout.BAR_LAMP), until L_DiveBar exists;
  * L_TableLookDev_9ft: the 9-ft pro table in a copy of the M1 room.
Both with a PlayerStart, the seven RbCam_TL_* look-dev cameras and the known-albedo card on the bed. Captures:
Tools/unreal/capture_table.py. Idempotent.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402
import rb_m1_layout as layout  # noqa: E402
import rb_make_test_room as room  # noqa: E402  (build() only: its main() runs as __main__ alone)

MEL = unreal.MaterialEditingLibrary


def dev_instance(name: str, parent: str, vectors: dict) -> str:
	path = f"{layout.M2L_DEV_DIR}/{name}"
	mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
	if mi is None:
		tools = unreal.AssetToolsHelpers.get_asset_tools()
		mi = tools.create_asset(name, layout.M2L_DEV_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
		if mi is None:
			rb.fail(f"could not create {path}")
	parent_asset = unreal.load_asset(parent)
	if parent_asset is None:
		rb.fail(f"missing parent material {parent} (run rb_make_materials.py)")
	MEL.set_material_instance_parent(mi, parent_asset)
	for key, value in vectors.items():
		MEL.set_material_instance_vector_parameter_value(mi, key, unreal.LinearColor(value[0], value[1], value[2], 1.0))
	MEL.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
	return path


def main() -> None:
	rb.ensure_dir(layout.M2L_DEV_DIR)
	lamp = dict(layout.BAR_LAMP)
	wall = dev_instance("MI_M2L_DarkWall", "/Game/Generated/Materials/M_RbRoomWall", {"Albedo": lamp.pop("wall_albedo")})
	floor = dev_instance("MI_M2L_DarkFloor", "/Game/Generated/Materials/M_RbRoomFloor", {"Albedo": lamp.pop("floor_albedo")})
	lamp["wall_material_override"] = wall
	lamp["floor_material_override"] = floor
	room.build(layout.M2L_LOOKDEV_MAP, preset="SEVEN_FOOT_BAR", ball_set="OLD_BAR_OVERSIZED_CUE", room=lamp,
		ball_material="/Game/Generated/Materials/MI_RbBall_DiveBar", m1=False, lookdev=True,
		table_props={"use_venue_condition": True, "venue_kind": unreal.RbVenueKind.DIVE_BAR, "venue_seed": 7},
		exposure_bias_ev=layout.LOOKDEV_EXPOSURE_BIAS_EV["7ft"])
	rb.log(f"M2-L look-dev room OK: {layout.M2L_LOOKDEV_MAP}")
	# The 9-ft pro table in a copy of the M1 room (same lamp, walls and post-process as L_M1_TestRoom).
	room.build(layout.M2L_LOOKDEV_MAP_9FT, m1=False, lookdev=True, exposure_bias_ev=layout.LOOKDEV_EXPOSURE_BIAS_EV["9ft"])
	rb.log(f"M2-L look-dev room OK: {layout.M2L_LOOKDEV_MAP_9FT}")


main()
