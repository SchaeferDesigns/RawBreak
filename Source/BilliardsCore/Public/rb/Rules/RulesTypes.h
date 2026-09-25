#pragma once

// Rules engine vocabulary (rules.md 2, 10.1): disciplines, game state at SHOT START, declarations,
// fouls, options, outcomes, table landmarks and region predicates.
// Owner: WP-8 (rules facts & evaluation).
//
// Naming: rules.md writes EDiscipline, ECueBallNext, EShotKind, EGroup, EFoul, EOption, ENext; the
// core uses rb::rules::Discipline, CueBallNext, ShotKind, BallGroup, Foul, Option, NextAction
// (no E prefix; enumerator names unchanged).

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Ids.h"
#include "rb/Core/Tolerances.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec2.h"

#include <cstdint>

namespace rb::rules
{
	inline constexpr int kRulesBallCount = kPoolBallCount; // ids 0 (cue ball) .. 15

	enum class Discipline : std::uint8_t
	{
		EightBall,
		NineBall,
		TenBall,
		StraightPool, // 14.1 continuous
		Blackball,    // WPA ch. 8 (variant spec)
	};

	enum class CueBallNext : std::uint8_t
	{
		InPosition,
		InHandAnywhere,
		InHandAboveHeadString,
		InHandBaulk,
	};

	enum class ShotKind : std::uint8_t
	{
		Break,  // 8/9/10-ball break; 14.1 opening break
		Normal,
		PushOut,
		Safety,
	};

	enum class BallGroup : std::uint8_t
	{
		None,
		Solids,  // 1-7 (Blackball: reds)
		Stripes, // 9-15 (Blackball: yellows)
	};

	constexpr BallGroup GroupOf(int Ball)
	{
		return (Ball >= 1 && Ball <= 7) ? BallGroup::Solids : ((Ball >= 9 && Ball <= 15) ? BallGroup::Stripes : BallGroup::None);
	}

	struct Call
	{
		BallId Ball = kNoBall;
		PocketId Pocket = PocketId::None;
	};

	struct ShotDeclaration
	{
		ShotKind Kind = ShotKind::Normal;
		Call Called;                                    // where the discipline calls shots
		BallGroup ClaimedClearedGroup = BallGroup::None;// 8-ball open table, R 4.4 (auto-set when the 8 is called)
	};

	struct PlayerState
	{
		int Score = 0;               // 14.1 points (can be negative)
		int ConsecutiveFouls = 0;    // standard fouls in a row (R 3.13)
		BallGroup Group = BallGroup::None;
	};

	enum class BallStatusKind : std::uint8_t
	{
		NotUsed,
		OnTable,
		Pocketed,
		OutOfPlay,
	};

	struct BallStatus
	{
		BallStatusKind Kind = BallStatusKind::NotUsed;
		Vec2 Position; // plan center when OnTable [m]
	};

	// State at SHOT START (pitfall 1: evaluate against the start state).
	struct GameState
	{
		Discipline Game = Discipline::NineBall;
		int Shooter = 0;
		int RackBreaker = 0;
		BallStatus Balls[kRulesBallCount];
		CueBallNext CueBall = CueBallNext::InHandAboveHeadString;
		bool IsBreakShot = true;          // 8/9/10: first shot of the rack; 14.1: opening break
		bool PushOutAvailable = false;    // 9/10-ball: exactly the shot after a legal break
		bool TableOpen = true;            // 8-ball / Blackball
		bool FreeShot = false;            // Blackball free shot after a foul (3.2 suspended; CB in position or in hand in baulk)
		int VisitsRemaining = 0;          // FoulCueBallMode TwoVisits / FreeShotPlusVisit: extra visits of the shooter after this one
		PlayerState Players[2];           // index = player (or team in Doubles, MatchState::ActiveMember picks the member)
		// 8-ball LastPocketRule variant (rules.md 12.6): pocket of the most recently pocketed ball of each group on an
		// EARLIER shot, index = BallGroup (None unused). Maintained by ApplyShot (from Facts.Pocketed in time order).
		PocketId LastGroupBallPocket[3] = {PocketId::None, PocketId::None, PocketId::None};
	};

	enum class Foul : std::uint8_t
	{
		CueBallScratch,          // R 3.1
		CueBallOffTable,         // R 3.1
		WrongBallFirst,          // R 3.2
		NoRailAfterContact,      // R 3.3
		BreakTooFewRails,        // R 5.3(b), 6.3(b)
		NoFootOnFloor,           // R 3.4
		ObjectBallOffTable,      // R 3.5
		TouchedBall,             // R 3.6 (all-ball fouls)
		DoubleHit,               // R 3.7
		PushShot,                // R 3.8
		BallsStillMoving,        // R 3.9
		BadCueBallPlacement,     // R 3.10
		BadPlayAboveHeadStringP1,// R 3.11 para 1
		BadPlayAboveHeadStringP2,// R 3.11 para 2
		SlowPlay,                // R 3.14
		TemplateFoul,            // R 3.15
		IllegalScoop,            // variants (ScoopPolicy::Foul)
		BreakingFoul141,         // R 7.10
		ThreeConsecutiveFouls,   // R 3.13
		BlackballBreakFoul,      // R 8.5(b): nothing potted and < 2 balls across the center string
		PottedOpponentBallOnly,  // Blackball extra foul
		JumpedOverBall,          // Blackball R 8.13.3
		Count
	};

	struct FoulSet
	{
		std::uint32_t Bits = 0;

		constexpr void Add(Foul F) { Bits |= 1u << static_cast<unsigned>(F); }
		constexpr bool Has(Foul F) const { return ((Bits >> static_cast<unsigned>(F)) & 1u) != 0u; }
		constexpr bool IsEmpty() const { return Bits == 0u; }
	};

	enum class Option : std::uint8_t
	{
		AcceptTable,
		BallInHandAboveHeadString,
		RerackDeciderBreaks,
		RerackOffenderBreaks,
		Spot8ContinueFromPosition,
		Spot8BallInHandAboveHeadString,
		AcceptTableNoPushOut,    // 9-ball three-ball rule
		HandBackPushOutAllowed,  // 9-ball three-ball rule
		ShootFromPosition,       // after a push-out / 10-ball wrongful pocketing
		PassBack,
		RequireRebreak,          // 14.1 breaking foul
	};

	enum class NextAction : std::uint8_t
	{
		Continue,
		Pass,
		AwaitDecision,
		RackWon,
		MatchWon,
		RerackAndBreak,
	};

	enum class RackCommandKind : std::uint8_t
	{
		None,
		Rerack14, // 14.1: 14 balls, apex site empty
		Rerack15, // all 15 re-racked
	};

	enum class PlacementCommand : std::uint8_t
	{
		Keep,
		ToHeadSpot,
		ToCenterSpot,
		InHandAboveHeadString,
		IntoRack, // the 15th ball joins a full 15-ball re-rack
	};

	struct RackCommand
	{
		RackCommandKind Kind = RackCommandKind::None;
		BallId FifteenthBall = kNoBall;
		PlacementCommand FifteenthBallPlacement = PlacementCommand::Keep;
		PlacementCommand CueBallPlacement = PlacementCommand::Keep;
	};

	struct ShotOutcome
	{
		FoulSet Detected;                          // every foul found (UI, replay, statistics)
		Foul Enforced = Foul::Count;               // the one penalty applied (severity order rules.md 4.9)
		bool AnyFoul = false;
		NextAction Next = NextAction::Pass;
		int NextShooter = -1;                      // or the deciding player for AwaitDecision
		int Winner = -1;                           // RackWon / MatchWon
		CueBallNext NextCueBall = CueBallNext::InPosition;
		CueBallNext CueBallIfAccepted = CueBallNext::InPosition; // for AcceptTable options
		FixedVector<Option, 4> Options;
		FixedVector<BallId, kRulesBallCount> BallsToSpot; // in spotting order
		int ScoreDelta[2] = {0, 0};
		int FoulsAfter[2] = {0, 0};                // consecutive-foul counters after this shot
		BallGroup AssignShooterGroup = BallGroup::None;
		bool NextPushOutAvailable = false;
		bool NextFreeShot = false;                 // Blackball: the next shot is a free shot (R 8.x; placement in position or in baulk)
		int NextVisits = 0;                        // TwoVisits / FreeShotPlusVisit: visits granted to NextShooter after this one
		RackCommand Rack;
		const char* RuleRef = "";                  // e.g. "R 4.8(b)" for the HUD
	};

	// Pocket opening for cue-ball placement legality (F11, R 1.6 / 16.16): a center over the opening is
	// illegal. Copied from TableGeometry::PocketGeometry by rb::BuildRulesTable (rb/Shot/ShotRecordBuilder.h).
	struct PocketOpening
	{
		Vec2 JawPoint[2];         // virtual jaw points = mouth line endpoints (index = JawSide)
		Vec2 Axis;                // unit pocket axis pointing out of the table
		Vec2 CaptureCenter;       // C_cap
		double DropEdgeRadius = 0.0; // a_d = r_p + r_d
	};

	// What the rules need of the table and the balls (rules.md 2.1-2.2): landmarks, pocket openings and
	// PER-BALL radii (oversized / small bar cue balls, Blackball sets). Rules never include geometry or
	// physics headers; the adapter rb::BuildRulesTable fills this from the single geometry source.
	struct RulesTable
	{
		double Length = 2.54;
		double Width = 1.27;
		double NominalBallRadius = 0.028575;       // object-ball radius for rack lattices and the 14.1 outline (R of 5.1/5.2)
		double BallRadius[kRulesBallCount] = {0.028575, 0.028575, 0.028575, 0.028575, 0.028575, 0.028575, 0.028575, 0.028575, 0.028575,
			0.028575, 0.028575, 0.028575, 0.028575, 0.028575, 0.028575, 0.028575}; // R_b per ball id [m]
		double HeadStringX = -0.635;
		double FootStringX = 0.635;
		double BaulkX = -0.762;
		Vec2 HeadSpot{-0.635, 0.0};
		Vec2 FootSpot{0.635, 0.0};
		Vec2 CenterSpot{0.0, 0.0};
		int PocketCount = 0;                        // 0: no pocket-opening test (hand-built test tables)
		PocketOpening Pockets[kPocketCount];        // index = PocketId
	};

	// Landmarks for an L x W table with every ball of radius BallRadius and no pocket openings (tests).
	constexpr RulesTable MakeRulesTable(double Length, double Width, double BallRadius)
	{
		RulesTable T;
		T.Length = Length;
		T.Width = Width;
		T.NominalBallRadius = BallRadius;
		for (double& R : T.BallRadius)
		{
			R = BallRadius;
		}
		T.HeadStringX = -0.25 * Length;
		T.FootStringX = 0.25 * Length;
		T.BaulkX = -0.5 * Length + 0.2 * Length;
		T.HeadSpot = {-0.25 * Length, 0.0};
		T.FootSpot = {0.25 * Length, 0.0};
		T.CenterSpot = {0.0, 0.0};
		return T;
	}

	// Region predicates with eps_line (rules.md 2.2). The string itself is NOT "above".
	constexpr bool AboveHeadString(const Vec2& P, const RulesTable& T, double EpsLine) { return P.x < T.HeadStringX - EpsLine; }
	constexpr bool OnHeadString(const Vec2& P, const RulesTable& T, double EpsLine) { return Abs(P.x - T.HeadStringX) <= EpsLine; }
	constexpr bool BelowHeadString(const Vec2& P, const RulesTable& T, double EpsLine) { return P.x > T.HeadStringX + EpsLine; }
	constexpr bool InBaulk(const Vec2& P, const RulesTable& T, double EpsLine) { return P.x < T.BaulkX - EpsLine; }
}
