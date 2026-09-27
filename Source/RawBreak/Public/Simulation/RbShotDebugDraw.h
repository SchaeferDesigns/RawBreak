#pragma once

// Debug drawing of a simulated shot (dev captures of UE-6a; later the F2 block's path view and replay debugging): the
// table outline from rb::TableGeometry and every ball's path as debug lines. Nothing is drawn when ENABLE_DRAW_DEBUG is
// off (shipping).
//
// Dev console command (non-shipping builds):
//   rb.SimDemo [break9|twoball] [UntilSeconds]
// builds the scenario (RbSimScenarios), submits it to the world's URbSimulationSubsystem at the start of the next world
// tick (where the pawn submits at the tip contact: the real worker path with the same-frame hand-off) and draws the
// handed-off shot up to UntilSeconds (default: the whole shot) as persistent lines.
// The table frame is the first ARbTable's cloth origin, or the world origin raised by the bed height without one.
// Owner: UE-6a.

#include "CoreMinimal.h"

#include "Simulation/RbShot.h"

class UWorld;

namespace RbShotDebugDraw
{
	// Nose outline (cushions + jaws), outer rail boundary, pocket holes (capture circles), sights, head string, head and
	// foot spots. TableToWorld maps the table-local UE frame (FRbCoords: cm, origin = bed centre on the cloth) to the
	// world, e.g. ARbTable::GetTableToWorld(). LifeTime < 0: persistent lines.
	RAWBREAK_API void DrawTable(const UWorld* World, const FTransform& TableToWorld, const rb::TableGeometry& Geometry, float LifeTime = -1.0f);

	// Every ball in play: its path from t = 0 to min(UntilTime, StopTime) (sampled every 5 ms plus every segment
	// boundary, at the ball centre), its start position (grey circle), its position at the end time (circle in the ball's
	// colour; a cross for a ball that was pocketed or left the table), and the cue of each strike at contact.
	RAWBREAK_API void DrawShot(const UWorld* World, const FTransform& TableToWorld, const FRbShot& Shot, double UntilTime = TNumericLimits<double>::Max(),
		float LifeTime = -1.0f);

	// Display colour of a ball id (WPA-like: 1 yellow, 2 blue, 3 red ...; the 8 dark grey so it shows on black).
	RAWBREAK_API FColor BallColor(int32 Id);
}
