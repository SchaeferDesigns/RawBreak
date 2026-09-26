#pragma once

// Chalk marks on balls (human-factors 4.3, HF-40): deposited at each tip contact in the ball's body frame, faded by
// sliding and rolling, removed by the wipe chore. The per-contact cling that the marks cause is PHYSICS
// (rb/Physics/BallBall.h ChalkMarkWeight / ContactClingFactor, PhysicsParams::ChalkCling, SimBall::ChalkMarks).
// Owner: WP-11 (player model). Part of rb::human.

#include "rb/Config.h"
#include "rb/Math/Quat.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallBall.h"
#include "rb/Physics/ShotResult.h"

namespace rb::human
{
	struct MarkParams
	{
		double Radius = 2.5e-3;        // r_mark [m]
		double MiscueRadius = 4.0e-3;  // [m]
		double BaseStrength = 0.3;     // Strength = 0.3 + 0.7 c(q, beta) (miscue: 1)
		double CoverageStrength = 0.7;
		double SlideFadeLength = 0.5;  // [m] Strength *= exp(-d_slide / 0.5 m - d_roll / 20 m) per shot
		double RollFadeLength = 20.0;  // [m]
		double DropBelow = 0.05;       // marks weaker than this are removed
	};

	// Deposit at a tip contact: BodyDir = Rotate(Conjugate(Orientation), ContactDir) (ContactDir = unit Q / R of MOT B.3 in
	// the world frame, Orientation = the ball's orientation at the contact), Strength 0.3 + 0.7 TipCoverage, Radius 2.5 mm;
	// miscue: Strength 1, Radius 4 mm. A full list replaces its weakest mark (ties: the oldest).
	RB_API void DepositChalkMark(BallChalkMarks& Marks, const Quat& Orientation, const Vec3& ContactDir, double TipCoverage, bool Miscue, const MarkParams& Params);

	// Fade after a shot by the ball's sliding and rolling distances [m]; marks below DropBelow are removed (order kept).
	RB_API void FadeChalkMarks(BallChalkMarks& Marks, double SlideDistance, double RollDistance, const MarkParams& Params);

	// Wipe chore (2-3 s towel animation): all marks removed.
	inline void WipeChalkMarks(BallChalkMarks& Marks) { Marks.Clear(); }

	struct TravelDistances
	{
		double Slide = 0.0; // [m] path length in Sliding segments
		double Roll = 0.0;  // [m] path length in Rolling segments
	};

	// Path lengths of one ball by motion state from the recorded track (needs RecordOptions::Trajectories; Sampled island
	// pieces count as sliding).
	RB_API TravelDistances ComputeTravelDistances(const ShotResult& Result, int Ball);
}
