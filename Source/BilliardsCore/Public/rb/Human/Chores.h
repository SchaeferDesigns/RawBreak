#pragma once

// Chores that change simulated state (human-factors principle 5, 4.1, 4.6; HF-22, HF-55, HF-70..HF-76): durations
// are the sum of their physical steps, partial work leaves partial state (the revolver rule), a skipped chore gives
// the character's HABITUAL result bit-exactly (A / C / P modes), and a ritual (R mode) can match or beat the habit
// but never the habit-1 result. The animation and input side of the chores lives in the UE game module.
// Owner: WP-11 (player model). Part of rb::human.

#include "rb/Config.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Human/TipState.h"

#include <cstdint>

namespace rb::human
{
	enum class ChoreMode : std::uint8_t
	{
		Ritual,    // R: full interactive ritual (result from the player's motion, capped at the habit-1 result)
		Automatic, // A: the hands do it (hold to speed up 2-3x)
		Cut,       // C: 1.5 s cut
		Parallel,  // P: done while the opponent shoots
	};

	struct ChoreTiming
	{
		double CoinSeconds = 1.0;        // per coin (HF-70)
		double SlideSeconds = 2.0;       // push the slide, balls rumble
		double BallClearMin = 1.5;       // [s] per leftover ball rolled into a pocket (HF-71), habit 0 ...
		double BallClearMax = 3.0;       // ... wait: slow end of the range
		double SpotBallSeconds = 2.5;    // per spotted ball (HF-74: 2-3 s)
		double RackSecondsPer15 = 20.0;  // racking 15 balls from the tray (15-25 s)
		double WipeSeconds = 2.5;        // towel wipe of the cue ball (2-3 s)
		double CutSeconds = 1.5;         // C mode
	};

	// Coins one by one plus the slide: Coins x 1 s + 2 s (HF-70, HF-B04 "coin insertion is per coin").
	RB_API double CoinChoreDuration(int Coins, const ChoreTiming& Timing = ChoreTiming{});

	// Leftover balls cleared before the next rack: linear in the balls left (HF-71, HF-B04).
	RB_API double BallClearDuration(int BallsLeft, const ChoreTiming& Timing = ChoreTiming{});

	// Chalking chore (4.1): AutoChalkTwists twists (A / C / P) of TwistDuration(H_chalk) each; aborting after k twists keeps
	// exactly k twists of coverage (HF-B04). Returns the twists applied; Duration receives the seconds used.
	RB_API int PerformChalking(TipState& Tip, const ChalkCube& Cube, ChoreMode Mode, double ChalkHabit, double RitualSweep, int TwistsBeforeAbort,
		double& Duration, const TipParams& Params = TipParams{});

	// Rack quality -> micro-gaps (4.6): Mean = Jitter = 0.08 mm (1 - Q) + 0.005 mm Q; Q = H_rack in A / C mode, from the
	// push-and-lift motion in R mode (capped at the habit-1 result Q = 1), the NPC's personality otherwise.
	RB_API RackGapParams RackGapsForQuality(double Quality);

	// The habitual result of a skipped chore (principle 5): a pure function of the habit, independent of real time.
	constexpr double HabitualRackQuality(double RackHabit) { return RackHabit < 0.0 ? 0.0 : (RackHabit > 1.0 ? 1.0 : RackHabit); }

	// Ritual result capped at the habit-1 result: min(RitualQuality, 1) (principle 5, HF-B05).
	constexpr double CapRitualResult(double RitualQuality) { return RitualQuality < 0.0 ? 0.0 : (RitualQuality > 1.0 ? 1.0 : RitualQuality); }
}
