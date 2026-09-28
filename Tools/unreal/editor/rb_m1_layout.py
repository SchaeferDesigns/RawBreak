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


def capture_exec_cmds(quality: str = "High") -> str:
	"""Console commands of a capture run: quality preset, then ball in hand at the chin-on-cue position."""
	return f"rb.Quality {quality}, rb.Match.Place {CUE_BALL_CORE[0]} {CUE_BALL_CORE[1]}"
