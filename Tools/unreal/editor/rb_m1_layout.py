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


def player_exec_cmds(commands: str, quality: str = "High") -> str:
	return f"rb.Quality {quality}, {commands}"


def capture_exec_cmds(quality: str = "High") -> str:
	"""Console commands of a capture run: quality preset, then ball in hand at the chin-on-cue position."""
	return f"rb.Quality {quality}, rb.Match.Place {CUE_BALL_CORE[0]} {CUE_BALL_CORE[1]}"
