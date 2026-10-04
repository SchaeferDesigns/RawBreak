"""M1 test-room layout shared by the level generator (rb_make_test_room.py, runs inside Unreal) and the host-side capture
script (Tools/unreal/capture_m1.py). Plain constants, no `unreal` import. Owner: UE-8.

Frames: UE world cm with the table at the origin (bed centre on the cloth at z = bed height); core table frame in metres
(+x foot, +y left seen from the head end, Docs/ue-architecture.md 4).
"""

MAP = "/Game/Generated/Maps/L_M1_TestRoom"  # RbAssetPaths::M1TestRoomMap
DEV_DIR = "/Game/Dev/UE8"
DEV_MAP = f"{DEV_DIR}/L_UE8_TestRoom"       # the same room with flat dev table materials (git-ignored scratch)

# Capture camera tags (RbAssetPaths::CaptureCamera).
CAM_OVERHEAD = "RbCam_Overhead"
CAM_CHIN_ON_CUE = "RbCam_ChinOnCue"
CAM_BALL_CLOSEUP = "RbCam_BallCloseUp"
CAM_ROOM = "RbCam_RoomOverview"

# Chin-on-cue look-dev shot: cue ball behind the head string (legal break placement), aimed at the rack apex on the foot spot.
CUE_BALL_CORE = (-0.70, 0.12)   # [m] core table frame
AIM_POINT_CORE = (0.635, 0.0)   # [m] foot spot (apex ball)
CUE_ELEVATION_DEG = 4.0

# The M1 acceptance captures (A7): 8-ball rack so all 16 balls (15 racked + cue ball) are on the table, the cue ball placed at
# CUE_BALL_CORE, High preset, 1920x1080 (Docs/ue-architecture.md 12).
CAPTURE_OPTIONS = "?Mode=Practice?Game=EightBall?Seed=3?Rate=0"
CAPTURE_RES = "1920x1080"
CAPTURE_WARMUP_SECONDS = 8.0     # auto exposure adapts at 1.5 / 0.7 EV/s (Eyes), Lumen and TSR converge
CAPTURES = [
	# (camera tag, output file name)
	(CAM_OVERHEAD, "overhead.png"),
	(CAM_CHIN_ON_CUE, "chin_on_cue.png"),
	(CAM_BALL_CLOSEUP, "ball_closeup.png"),
	(CAM_ROOM, "room.png"),
]


# The player's own first-person views (M1 integration): the pawn's camera rig (Eyes preset), no look-dev camera; the rb.Player.*
# console commands (UE-5b) put the pawn somewhere and get it down on the shot, the rb.Match.* commands (UE-6b) place the cue ball
# and break (live playback rate 0 = the break is committed at once, the still shows its end).
PLAYER_OPTIONS = "?Mode=Practice?Game=NineBall?Seed=3?Rate=0"
PLAYER_PLACE = f"rb.Match.Place {CUE_BALL_CORE[0]} {CUE_BALL_CORE[1]}"
PLAYER_STAND = f"rb.Player.Teleport -1.85 {CUE_BALL_CORE[1]} 0 -22"
PLAYER_CAPTURES = [
	# (output file name, commands after the quality preset)
	# Walking: standing eye (1.65 m) behind the head rail, looking down the table at the rack, cue ball in hand placed.
	("standing.png", f"{PLAYER_PLACE}, {PLAYER_STAND}"),
	# Down on the shot with the cue (chin over the cue, aimed at the apex ball on the foot spot).
	("down_on_shot.png", f"{PLAYER_PLACE}, {PLAYER_STAND}, rb.Player.AimAt {AIM_POINT_CORE[0]} {AIM_POINT_CORE[1]}, rb.Player.GetDown"),
	# After a 9 m/s break: standing at the head end, looking over the spread balls.
	("after_break.png", f"{PLAYER_PLACE}, rb.Match.Break 9, rb.Player.Teleport -1.85 0.0 0 -30"),
]


# --- M2-L table look-dev (Docs/ue-architecture.md 18.7) ------------------------------------------------------------------------
# Seven ARbLookDevCameras per look-dev level (tags RbCam_TL_<View>): the chin-on-cue placement and the five TableView placements
# (ARbLookDevCamera::ComputeTableViewPose, computed from the table's own geometry). Both look-dev levels are written by
# rb_dev_m2l.py (git-ignored scratch): the 9-ft pro table in a copy of the M1 room, the 7-ft coin-op bar box in a dark room under
# the bar lamp until L_DiveBar exists; both carry a known-albedo card (0.80 / 0.18 / 0.04) on the bed. Output names:
# Docs/images/dev/m2l/<table>_<view>.png (capture_table.py).
M2L_DEV_DIR = "/Game/Dev/M2L"
M2L_LOOKDEV_MAP = f"{M2L_DEV_DIR}/L_TableLookDev"            # the 7-ft bar box under a bar lamp
M2L_LOOKDEV_MAP_9FT = f"{M2L_DEV_DIR}/L_TableLookDev_9ft"    # the 9-ft pro table in the M1 room
ALBEDO_CARD = [(0.80, 0.80, 0.80), (0.18, 0.18, 0.18), (0.04, 0.04, 0.04)]   # linear albedo of the three patches (left to right)
TABLE_VIEWS = [
	# (view name = file suffix, camera tag, ERbTableLookDevView enumerator or None for the chin-on-cue placement)
	("chin_on_cue", "RbCam_TL_ChinOnCue", None),
	("standing", "RbCam_TL_Standing", "STANDING"),
	("pocket_closeup", "RbCam_TL_PocketCloseUp", "POCKET_CLOSE_UP"),
	("cushion_grazing", "RbCam_TL_CushionGrazing", "CUSHION_GRAZING"),
	("rail_closeup", "RbCam_TL_RailCloseUp", "RAIL_CLOSE_UP"),
	("overhead", "RbCam_TL_Overhead", "OVERHEAD"),
	("foot_end", "RbCam_TL_FootEnd", "FOOT_END"),     # M2-L extra: the coin-op cabinet's foot end (coin slide, trap window, tray)
]
# Playing lengths [m] of the look-dev presets (TableSpec; the chin-on-cue aim point is the foot spot at L / 4).
TABLE_LENGTH_M = {"NINE_FOOT_PRO": 2.54, "SEVEN_FOOT_BAR": 2.032}
LOOKDEV_TABLES = [
	# (file prefix, ERbTablePreset enumerator, map)
	("9ft", "NINE_FOOT_PRO", M2L_LOOKDEV_MAP_9FT),
	("7ft", "SEVEN_FOOT_BAR", M2L_LOOKDEV_MAP),
]
# 7-ft look-dev room: The Low Bridge's 3-shade bar lamp (venue-dive-bar 4.4) emulated by the test room's lamp sections - three
# 0.32 m sections 0.46 m apart, their bottoms 0.86 m above the bed, 1100 lm bulbs x 0.60 downward efficiency, warm white; a dark
# room around it (the dive bar's pool room is lit by the lamp, walls and floor dark).
BAR_LAMP = {
	"lamp_height_above_bed": 86.0,
	"lamp_column_x": [-46.0, 0.0, 46.0],
	"lamp_row_y": [0.0],
	"lamp_section_size": (32.0, 32.0),
	"lamp_column_lumens": [660.0, 660.0, 660.0],
	"lamp_louvre_angle_deg": 40.0,
	"lamp_louvre_length": 23.0,
	"lamp_temperature_k": 2900.0,
	"white_balance_temp_k": 3900.0,     # the eye keeps some of the warm bulb light in a bar
	"ambient_panel_lumens": 90.0,
	"wall_albedo": (0.055, 0.045, 0.040),
	"floor_albedo": (0.050, 0.045, 0.042),
	"ceiling_albedo": (0.25, 0.25, 0.24),
}
# Exposure calibration of the table look-dev cameras [EV] (ARbLookDevCamera::ExposureBiasEv). The Eyes metering (histogram 70-95 %,
# centre-weighted) maps the lit cloth to about middle grey, so the dark-albedo cloth renders 1.5-2 EV brighter than a photo exposed on
# a grey card (r4: the 0.18 card at sRGB ~225 on the 7-ft, ~190 on the 9-ft; the 0.04 card near middle grey) and the tone curve's
# shoulder turns the cloth pastel. 7-ft: the dark look-dev room adapts to ~EV100 5.5 at the standing view, 1.5 EV below
# venue-dive-bar 4.5's V03 band (6.5-7.5); -1.5 puts the captures into the venue's band (card ~sRGB 175 under the lamp). 9-ft (the
# M1 room): -1.0 so the cards read like a photo of a lit table. The M1 acceptance cameras (A7) stay unbiased.
LOOKDEV_EXPOSURE_BIAS_EV = {"9ft": -1.0, "7ft": -1.5}
# Per-view offset on top of the table's bias [EV]. r8: the foot-end view is filled by the dark cabinet / apron, the metering lifts the
# frame and the cloth in its top third clips to pastel (r9: -0.75 EV still left the cloth ~1.3 EV above the standing view); -1.25 EV
# brings the cloth near the chin-on-cue / standing tone while the cabinet stays readable.
LOOKDEV_VIEW_EXPOSURE_OFFSET_EV = {"foot_end": -1.25}


def table_capture_options(preset: str) -> str:
	"""URL options of a table look-dev capture: an 8-ball rack (all 16 balls), practice, no live playback."""
	return CAPTURE_OPTIONS


def player_exec_cmds(commands: str, quality: str = "High") -> str:
	return f"rb.Quality {quality}, {commands}"


def capture_exec_cmds(quality: str = "High") -> str:
	"""Console commands of a capture run: quality preset, then ball in hand at the chin-on-cue position."""
	return f"rb.Quality {quality}, rb.Match.Place {CUE_BALL_CORE[0]} {CUE_BALL_CORE[1]}"
