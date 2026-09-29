#pragma once

// The planner's cheap static evaluator (Docs/architecture.md 7.6): no simulation, only ghost-ball geometry, pocket acceptance
// windows, obstruction, a make chance from an aim sigma, the quality of the next shot, and a two-player runout / safety model
// that turns a rules state into the probability that a player wins the rack. The planner values every rollout's end state
// with it; the game can use it as the "leave quality" of the XP rules (human-factors 5.2). All model constants TUNING.
// Owner: WP-12 (AI opponent). Part of rb::ai.
//
// Frames and units: the core frame (plan view), metres, radians. Pure functions: no allocation, no state, deterministic.

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Ids.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Vec2.h"
#include "rb/Rules/Match.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"

#include <cstdint>

namespace rb::ai
{
	// erf(X) (Abramowitz-Stegun 7.1.26, |error| <= 1.5e-7) through rb::Exp: deterministic like every transcendental of the core.
	RB_API double Erf(double X);

	// Where an object ball is aimed to drop in a pocket, and how far its centre line may miss.
	struct PocketAim
	{
		Vec2 Point;               // aim point: mouth midpoint + 0.25 R along the pocket axis [m]
		double HalfWindow = 0.0;  // allowed lateral miss of the ball's centre line at the aim point [m] (0 = no way in)
		double Approach = 0.0;    // angle between the travel direction and the pocket axis [rad]
		bool Valid = false;       // HalfWindow > 0
	};

	// Acceptance window (TUNING): corner pockets h0 = max(4 mm, Mouth / 2 - R) up to 45 deg off the axis, falling linearly to 0 at
	// 78 deg; side pockets h0 = max(4 mm, Mouth / 2 - R) (cos(approach) - 0.34) / 0.66 (0 beyond about 70 deg).
	RB_API PocketAim PocketAimFor(const TableGeometry& Table, int Pocket, const Vec2& Object, double Radius);

	// Plan positions and radii of the balls on the table (from a rules state).
	struct BallLayout
	{
		Vec2 Position[kMaxBalls];
		double Radius[kMaxBalls] = {};
		std::uint32_t OnTable = 0; // bit per ball id
	};

	// The OnTable balls of State with the radii of Table (the cue ball only when OnTable, i.e. not in hand).
	RB_API BallLayout MakeBallLayout(const rules::GameState& State, const rules::RulesTable& Table);

	// A ball of radius Radius moving on the segment A -> B touches no ball of Layout outside ExcludeMask (bit per id).
	// Tolerance: a gap below 1 mm counts as a touch (the static model is conservative).
	RB_API bool PathClear(const BallLayout& Layout, const Vec2& A, const Vec2& B, double Radius, std::uint32_t ExcludeMask);

	// Ghost-ball geometry of a cut: the cue ball's centre at contact, the cut angle between the cue ball's path and the object
	// ball's line (0 = full), the distances, and the azimuth that aims the cue ball at the ghost ball.
	struct ShotGeometry
	{
		Vec2 Ghost;               // cue-ball centre at contact [m]
		double Cut = 0.0;         // [rad], signed: > 0 when the object ball goes to the LEFT of the cue ball's path
		double CueDistance = 0.0; // |Ghost - Cue| [m]
		double ObjectDistance = 0.0; // |Target - Object| [m]
		double Azimuth = 0.0;     // [rad] of Ghost - Cue
		bool Feasible = false;    // |Cut| <= MaxCut and both distances positive
	};

	RB_API ShotGeometry CutGeometry(const Vec2& Cue, double CueRadius, const Vec2& Object, double ObjectRadius, const Vec2& Target, double MaxCut);

	// Make chance of a cut: erf(dphi / (sigma sqrt 2)), dphi = w (R_c + R_o) cos(cut) / max(d1, 5 cm), w = atan(HalfWindow /
	// max(ObjectDistance, 1 cm)): the cue-ball direction error that still sends the object ball inside the window (a lateral
	// error e at the object ball turns the line of centres by e / ((R_c + R_o) cos(cut))). WindowScale < 1 for banks and
	// combinations. AimSigma <= 0: 1 if the window is open.
	RB_API double PotChance(const ShotGeometry& Shot, double HalfWindow, double RadiusSum, double AimSigma, double WindowScale = 1.0);

	// What the planner assumes about one player (index = rules player).
	struct EvalPlayer
	{
		double AimSigma = 0.3e-2;   // [rad] 1-sigma cue-ball direction error
		double RunoutRate = 0.5;    // q: next ball made and position kept from a typical position
		double SafetyQuality = 0.3; // [0, 1]
		double KickSkill = 0.5;     // [0, 1] legal contact when hooked
		bool PlaysSafeties = true;
		int PositionDepth = 1;      // 0: ignores the next shot (uses q); 1: next shot; >= 2: two-shot pattern
	};

	struct EvalContext
	{
		const TableGeometry* Table = nullptr;
		const rules::MatchConfig* Match = nullptr; // rules config, rules table, discipline
		EvalPlayer Players[2];
	};

	// The quality of the next shot of Player (normally State.Shooter) on the table of State.
	struct NextShotInfo
	{
		double PotChance = 0.0;      // best direct pot of a legal ball (incl. obstruction; in hand: the best legal placement)
		double Pattern = 1.0;        // PositionDepth >= 2: best make chance on the ball after it, from the ghost-ball position
		double Visibility = 0.0;     // [0, 1] share of the legal balls' contact range the cue ball can reach in a straight line
		BallId Ball = kNoBall;       // the best shot
		PocketId Pocket = PocketId::None;
		Vec2 CuePosition;            // the cue ball (in hand: the placement)
		bool InHand = false;
	};

	RB_API NextShotInfo BestNextShot(const EvalContext& Context, const rules::GameState& State, int Player);

	// Balls Player still has to pocket to win the rack: 9/10-ball the object balls on the table; 8-ball / Blackball the own
	// group + 1 (open table: the smaller group + 1); 14.1 min(14, points to go).
	RB_API int BallsToWin(const rules::GameState& State, int Player, const rules::RulesConfig& Config);

	// P(the player to move, State.Shooter, wins the rack): a turn model (each player makes his next ball with his runout rate q
	// and keeps the table, a miss hands the other player a typical table; the ball counts capped at a 3-ball horizon, 14.1 a
	// points race on a relative horizon; Docs/architecture.md 7.6) in which the mover's actual next shot p1 = BestNextShot
	// replaces his first q: the attempt value p1 W_cont + (1 - p1)(1 - P_opp) (depth >= 2: a two-shot chain); a hooked mover
	// fouls with (1 - visibility)(1 - KickSkill) (the third foul loses the rack where the three-foul rule applies; 14.1: its
	// points); players who play safeties take the max with a safety value. The cue ball in hand counts as a free placement.
	RB_API double MoverWinProbability(const EvalContext& Context, const rules::GameState& State);

	// The same value for a TYPICAL first shot (make chance q, full visibility): what the player to move can expect when the table
	// is not known, e.g. after a missed pot (the planner's miss value).
	RB_API double TypicalWinProbability(const EvalContext& Context, const rules::GameState& State);

	// P(Player wins the rack) for any match phase: AwaitShot -> MoverWinProbability (or its complement; in hand behind the head
	// string with every legal ball behind it: the value after the rules' spot request, 4.4); AwaitDecision -> the
	// decider takes the option that is best for him (ApplyOption on a copy); RackSetup -> the breaker's typical value (14.1, the
	// three-foul re-rack: the points race with the scores after the penalty and the other player to move); RackOver / MatchOver
	// -> 0.5 (the caller knows the winner from the outcome).
	RB_API double StateValue(const EvalContext& Context, const rules::MatchState& State, int Player);
}
