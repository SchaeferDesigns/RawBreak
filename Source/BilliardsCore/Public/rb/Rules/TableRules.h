#pragma once

// Table procedures of the rules: rack generation (fill rules + seeded randomness), spotting balls,
// the spot request, the 14.1 rack outline and continuation re-rack plans (rules.md 4.3, 4.4, 5,
// 9.5). Owner: WP-9 (rules table procedures & match).

#include "rb/Config.h"
#include "rb/Core/Error.h"
#include "rb/Core/Tolerances.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Math/Vec2.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"

#include <cstdint>

namespace rb::rules
{
	// Rack layout for one rack: which ball sits where (5.1). Deterministic for a given seed; the seed
	// is stored in the replay / network state (pitfall 14).
	struct RackAssignment
	{
		RackShape Shape = RackShape::Triangle15;
		RackAnchor Anchor = RackAnchor::ApexOnFootSpot;
		int SiteCount = 0;
		BallId BallAtSite[kMaxRackSites] = {kNoBall, kNoBall, kNoBall, kNoBall, kNoBall, kNoBall, kNoBall, kNoBall, kNoBall, kNoBall, kNoBall,
			kNoBall, kNoBall, kNoBall, kNoBall}; // kNoBall = empty site (14.1 re-rack apex)
		bool Racked[kRulesBallCount] = {};    // ball id -> part of this rack
		Vec2 Position[kRulesBallCount];       // ball id -> center including micro-gaps [m]
	};

	// 8-ball: 8 at (r2, k1), back corners one solid + one stripe, apex any but the 8; 9-ball: 1 at the
	// apex, 9 at the center (anchor per NineBallRack); 10-ball: 1 at the apex on the foot spot, 10 at
	// (r2, k1); 14.1: random, ApexEmpty for the 14-ball continuation rack; Blackball: black on the foot
	// spot at (r2, k1). Lattice diameter = 2 x the largest Table.BallRadius of the racked balls (no
	// overlap with worn / mixed balls). Micro-gaps via rb::ApplyRackGaps with the same seed stream and
	// the anchor site (RackAnchorSiteIndex) fixed, so the foot-spot ball stays on the spot.
	// Blackball: the colour pattern of the WPA diagram is not in the rule text (rules.md 14 #10); the
	// fill is the 8-ball one (black at (r2, k1), one ball of each group in the back corners, rest random).
	// 14.1 with ApexEmpty racks 14 of the 15 balls (the ball drawn last stays unracked); a continuation
	// rack of a GIVEN ball set uses GenerateStraightPoolRack. ApexEmpty is InvalidArgument for other
	// disciplines. On any error Out is reset (nothing racked).
	RB_API ErrorCode GenerateRack(Discipline Game, const RulesConfig& Config, const RulesTable& Table, std::uint64_t Seed, bool ApexEmpty,
		const RackGapParams& Gaps, RackAssignment& Out);

	// 14.1 rack of exactly the object balls in BallMask (bit per ball id 1..15) at random on the Triangle15
	// lattice (apex on the foot spot), the apex site left empty when ApexEmpty (9.5: Rerack14 racks the 14
	// pocketed balls, Rerack15 and the opening break all 15). If the mask holds more balls than free sites,
	// the balls drawn last stay unracked. Same seed stream, lattice and micro-gaps as GenerateRack.
	RB_API ErrorCode GenerateStraightPoolRack(const RulesTable& Table, std::uint64_t Seed, bool ApexEmpty, std::uint32_t BallMask,
		const RackGapParams& Gaps, RackAssignment& Out);

	// SpotBall (4.3, exact, no iteration): long string, foot-spot side first, never touching the CB
	// (gap SpotCueBallGap), exact tangency to object balls (tolerance eps_line, pitfall 26), then
	// toward the head. Uses the OnTable positions in State and PER-BALL radii: the blocked half-width of
	// ball j is h_j = sqrt(D_j^2 - y_j^2) with D_j = R_b + R_j (+ SpotCueBallGap for the cue ball), and the
	// allowed range is |x| <= L/2 - R_b.
	RB_API Vec2 SpotBall(const GameState& State, int Ball, const RulesTable& Table, const RulesTolerances& Tolerances);

	// Spots Balls[0..Count) in the given order (callers pass ascending ids, Blackball its own order),
	// updating State (Kind = OnTable, Position).
	RB_API void SpotBalls(GameState& State, const BallId* Balls, int Count, const RulesTable& Table, const RulesTolerances& Tolerances);

	// Spot request (4.4): CB in hand above HS and every legal object ball above HS -> the legal ball
	// nearest the head string (largest x; equally near: the lowest id, the shooter may pick another);
	// -1 if the request is not available (no legal ball on the table, or one on / below the string).
	RB_API int SpotRequestCandidate(const GameState& State, std::uint32_t LegalMask, const RulesTable& Table, const RulesTolerances& Tolerances);

	// 14.1 rack outline (5.2): tight 15-ball triangle offset outward by R, apex on the foot spot.
	struct RackOutline
	{
		Vec2 Apex;      // (x_FS - 2R, 0)
		Vec2 BackLeft;  // (x_FS + 4 sqrt3 R + R, +(4R + sqrt3 R))
		Vec2 BackRight;
	};

	RB_API RackOutline StraightPoolRackOutline(const RulesTable& Table);               // uses Table.NominalBallRadius
	RB_API bool InterferesWithRack(const Vec2& P, double Radius, const RulesTable& Table); // dist(P, outline) < Radius (the ball's own R)
	RB_API bool BlocksSpot(const Vec2& Spot, double SpotRadius, const Vec2& Other, double OtherRadius); // |Other - Spot| < SpotRadius + OtherRadius

	// 14.1 continuation re-racks (9.5, Table 1); radii from Table.BallRadius (cue ball id 0).
	// PlanRerack14: Rerack14 with the 15th ball and the cue ball kept / moved to the head or center
	// spot / cue ball in hand above HS; both in the rack -> Rerack15 (15th IntoRack, CB in hand above HS).
	// PlanRerack15AfterFifteenthPocketed: Rerack15 (FifteenthBall = kNoBall, IntoRack), CB in hand above
	// HS if it interferes with the rack, else kept.
	RB_API RackCommand PlanRerack14(const Vec2& CueBall, int FifteenthBall, const Vec2& FifteenthBallPosition, const RulesTable& Table,
		const RulesTolerances& Tolerances);
	RB_API RackCommand PlanRerack15AfterFifteenthPocketed(const Vec2& CueBall, const RulesTable& Table);

	// F11 predicates (R 1.6, 3.10, 16.16): center over a pocket opening (beyond a mouth line between the
	// virtual jaw points, or inside a drop-edge circle; false if Table.PocketCount == 0), and full cue-ball
	// placement legality: |p.x| <= L/2 - R_cb, |p.y| <= W/2 - R_cb, not over an opening, distance to every
	// OnTable ball >= R_cb + R_j - eps_overlap, and the region (strictly above the head string / in baulk).
	RB_API bool OverPocketOpening(const Vec2& P, const RulesTable& Table);
	RB_API bool CueBallPlacementLegal(const GameState& State, const Vec2& P, CueBallNext Region, const RulesTable& Table, const RulesTolerances& Tolerances);
}
