#pragma once

// Lag for the first break (rules.md 4.1): both lag balls are simulated together in ONE simulation
// (SimInput::Strikes holds two StrikeRequests, both tip contacts at t = 0); the facts of each ball come
// from the shared ShotRecord: its line crossings, cushion / jaw contacts, end status, and its own tip
// contacts (StrokeRecord::TipContacts filtered by Ball: double hit / push = bad lag (h)) and NonTip
// contacts. The distance metric uses the ball's own radius (Record.Start.Radius).
// Owner: WP-9 (rules table procedures & match).

#include "rb/Config.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Vec2.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Shot/ShotRecord.h"

#include <cstdint>

namespace rb::rules
{
	struct LagBallFacts
	{
		bool Bad = false;
		double Distance = 0.0;          // d = (x_rest - R_b) - (-L/2) [m]
		bool CrossedLongString = false; // (a)
		int FootCushionContacts = 0;    // (b) must be exactly 1 (cushion C2)
		bool PocketedOrOffTable = false;// (c)
		bool SideCushionContact = false;// (d) C0, C1, C3, C4 or side-pocket jaws (INTERPRETATION)
		bool PastHeadCushionNose = false;// (e) x_rest - R_b < -L/2
		bool OtherFoul = false;         // double hit, push, touched ball, ...
	};

	enum class LagOutcome : std::uint8_t
	{
		FirstWins,
		SecondWins,
		Relag,
	};

	struct LagResult
	{
		LagOutcome Outcome = LagOutcome::Relag;
		LagBallFacts First;
		LagBallFacts Second;
	};

	RB_API LagBallFacts DeriveLagBallFacts(const ShotRecord& Record, int Ball, const RulesTable& Table, const RulesTolerances& Tolerances);
	RB_API LagResult EvaluateLag(const LagBallFacts& First, const LagBallFacts& Second, const RulesTolerances& Tolerances);

	// Default placement (x_HS - R - 0.01, -W/4) and (x_HS - R - 0.01, +W/4) (DERIVED).
	RB_API void LagStartPositions(const RulesTable& Table, Vec2& First, Vec2& Second);
}
