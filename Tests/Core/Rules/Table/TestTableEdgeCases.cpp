// Owner: WP-9 (rules table procedures & match). Adversarial table-procedure cases from the WP-9 review:
// spotting chains (object ball + cue ball, toward the head, near the foot rail), pitfall 26 rounding
// below tangency, spot-request ties, 14.1 re-rack plans with per-ball radii and head-string boundaries,
// lag stroke-foul attribution and the eps_lag boundary.

#include "rbtest.h"

#include "Rules/Match/RulesTestUtil.h"

#include "rb/Math/Scalar.h"
#include "rb/Rules/Lag.h"

using namespace rb;
using namespace rb::rules;
using namespace rb::rules::testhelp;

RB_TEST(Rules_Table_SpotPastObjectBallAndCueBallChain)
{
	// Foot spot taken by ball 5; the cue ball sits right behind it: the tangency point to 5 (0.69215) is
	// inside the cue ball's gap zone, so the 9 goes past the cue ball (2R + delta_cbGap from it).
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::NineBall);
	Place(S, 5, 0.635, 0.0);
	Place(S, 0, 0.72, 0.0);
	const RulesTolerances Tol;
	const Vec2 P = SpotBall(S, 9, Table, Tol);
	RB_CHECK_NEAR(P.x, 0.72 + 2.0 * kR + Tol.SpotCueBallGap, 1e-12);
	RB_CHECK(P.y == 0.0);
	// no overlap with anything, cue-ball gap kept
	RB_CHECK(Length(P - S.Balls[5].Position) >= 2.0 * kR - 1e-12);
	RB_CHECK(Length(P - S.Balls[0].Position) >= 2.0 * kR + Tol.SpotCueBallGap - 1e-12);
}

RB_TEST(Rules_Table_SpotTowardHeadSkipsBlockedLeftEnds)
{
	// Whole foot side blocked (G26 row) plus a ball at 0.53 whose zone covers the head-side tangency
	// point of the foot-spot ball (0.57785): the next candidate toward the head is 0.53 - 2R.
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::StraightPool);
	const int Ids[11] = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	for (int k = 0; k <= 10; ++k)
	{
		Place(S, Ids[k], 0.635 + k * 0.05715, 0.0);
	}
	Place(S, 13, 0.53, 0.0);
	const Vec2 P = SpotBall(S, 3, Table, RulesTolerances{});
	RB_CHECK_NEAR(P.x, 0.53 - 2.0 * kR, 1e-12);
	// ... but a ball that merely touches that tangency point does not block it
	S.Balls[13].Position = {0.57785 - 2.0 * kR, 0.0};
	const Vec2 Q = SpotBall(S, 3, Table, RulesTolerances{});
	RB_CHECK_NEAR(Q.x, 0.57785, 1e-9);
}

RB_TEST(Rules_Table_SpotRespectsFootRailLimit)
{
	// A foot-side tangency point beyond L/2 - R is not allowed: with the foot spot taken and the free
	// point behind the last ball past the rail limit, the ball goes toward the head.
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::StraightPool);
	// ten balls from the foot spot to ~1.2 m, spaced ~62.8 mm (< 4R: no ball fits between two of them);
	// the tangency point behind the last one is ~1.2 + 2R > L/2 - R = 1.241425
	for (int k = 0; k <= 9; ++k)
	{
		Place(S, 1 + k, 0.635 + k * (1.2 - 0.635) / 9.0 * 0.999999, 0.0);
	}
	const Vec2 P = SpotBall(S, 15, Table, RulesTolerances{});
	RB_CHECK(P.x <= 0.5 * Table.Length - kR);
	RB_CHECK(P.x < 0.635);
	RB_CHECK_NEAR(P.x, 0.635 - 2.0 * kR, 1e-12);
}

RB_TEST(Rules_Table_S21_ApexFreeDespiteRoundingBelowTangency)
{
	// Pitfall 26: the second-row balls of a 14-ball rack are 2R from the empty apex; in floating point the
	// distance may come out a hair below 2R. The apex must still be free.
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::StraightPool);
	const double Below = 1e-12;
	Place(S, 1, 0.635 + kSqrt3R - Below, kR);
	Place(S, 2, 0.635 + kSqrt3R - Below, -kR);
	RB_REQUIRE(Length(S.Balls[1].Position - Table.FootSpot) < 2.0 * kR);
	const Vec2 P = SpotBall(S, 6, Table, RulesTolerances{});
	RB_CHECK(P.x == 0.635 && P.y == 0.0);
	// a real overlap (well beyond eps_line) does block the apex
	S.Balls[1].Position = {0.635 + kSqrt3R - 1e-4, kR};
	const Vec2 Q = SpotBall(S, 6, Table, RulesTolerances{});
	RB_CHECK(Q.x > 0.635);
}

RB_TEST(Rules_Table_SpotRequestTieAndIllegalBalls)
{
	const RulesTable Table = NineFootTable();
	GameState S = EmptyState(Discipline::EightBall);
	S.CueBall = CueBallNext::InHandAboveHeadString;
	Place(S, 11, -0.80, 0.20);
	Place(S, 4, -0.80, -0.20); // equally near the head string: the lowest id
	Place(S, 2, -1.00, 0.00);
	const std::uint32_t Legal = (1u << 2) | (1u << 4) | (1u << 11);
	RB_CHECK(SpotRequestCandidate(S, Legal, Table, RulesTolerances{}) == 4);
	// an illegal ball (the 8 on an open table) nearer the string is not a candidate and does not block
	Place(S, 8, -0.70, 0.0);
	RB_CHECK(SpotRequestCandidate(S, Legal, Table, RulesTolerances{}) == 4);
	// a legal ball a hair above the string (beyond eps_line) is still above: it is the candidate
	Place(S, 9, -0.635 - 2e-6, 0.3);
	RB_CHECK(SpotRequestCandidate(S, Legal | (1u << 9), Table, RulesTolerances{}) == 9);
	// ... within eps_line it counts as ON the string: playable, no request at all
	S.Balls[9].Position = {-0.635 - 0.5e-6, 0.3};
	RB_CHECK(SpotRequestCandidate(S, Legal | (1u << 9), Table, RulesTolerances{}) == -1);
	// no legal ball on the table: no request
	RB_CHECK(SpotRequestCandidate(S, 1u << 13, Table, RulesTolerances{}) == -1);
}

RB_TEST(Rules_Table_Rerack14PerBallCueBallRadius)
{
	// An oversized bar cue ball (60.325 mm) at 1.05 R_nominal from the outline apex interferes; a nominal
	// one at the same center does not (InterferesWithRack uses the ball's own radius).
	RulesTable Big = NineFootTable();
	Big.BallRadius[0] = 0.0301625;
	const RulesTable Nominal = NineFootTable();
	const Vec2 Cue{0.577850 - 1.05 * kR, 0.0};
	const Vec2 Fifteenth{-0.2, 0.3};
	const RackCommand WithBig = PlanRerack14(Cue, 15, Fifteenth, Big, RulesTolerances{});
	const RackCommand WithNominal = PlanRerack14(Cue, 15, Fifteenth, Nominal, RulesTolerances{});
	RB_CHECK(WithBig.CueBallPlacement == PlacementCommand::InHandAboveHeadString); // 15th below HS (R 7.8(d))
	RB_CHECK(WithNominal.CueBallPlacement == PlacementCommand::Keep);
	RB_CHECK(PlanRerack15AfterFifteenthPocketed(Cue, Big).CueBallPlacement == PlacementCommand::InHandAboveHeadString);
	RB_CHECK(PlanRerack15AfterFifteenthPocketed(Cue, Nominal).CueBallPlacement == PlacementCommand::Keep);
	// the oversized cue ball also blocks the head spot from farther away (R_15 + R_cb)
	const Vec2 NearHead{-0.635 + 0.058, 0.0}; // 58 mm: clear for two nominal balls, not for 28.575 + 30.1625
	const RackCommand ToSpot = PlanRerack14(NearHead, 15, {0.70, 0.05}, Big, RulesTolerances{});
	RB_CHECK(ToSpot.FifteenthBallPlacement == PlacementCommand::ToCenterSpot);
	RB_CHECK(PlanRerack14(NearHead, 15, {0.70, 0.05}, Nominal, RulesTolerances{}).FifteenthBallPlacement == PlacementCommand::ToHeadSpot);
}

RB_TEST(Rules_Table_S23_HeadStringBoundaryForFifteenth)
{
	// Cue ball in the rack. The 15th exactly on the head string or within eps_line of it: "on or below"
	// -> cue ball in hand above HS. Strictly above (beyond eps_line): cue ball to the head spot.
	const RulesTable T = NineFootTable();
	const RulesTolerances Tol;
	const Vec2 Cue{0.75, 0.0};
	RB_CHECK(PlanRerack14(Cue, 15, {-0.635 - 0.5e-6, 0.3}, T, Tol).CueBallPlacement == PlacementCommand::InHandAboveHeadString);
	RB_CHECK(PlanRerack14(Cue, 15, {-0.635 + 0.5e-6, 0.3}, T, Tol).CueBallPlacement == PlacementCommand::InHandAboveHeadString);
	RB_CHECK(PlanRerack14(Cue, 15, {-0.635 - 2e-6, 0.3}, T, Tol).CueBallPlacement == PlacementCommand::ToHeadSpot);
	// 15th above the string and exactly tangent to a ball on the head spot: not blocking (strict overlap)
	RB_CHECK(PlanRerack14(Cue, 15, {-0.635 - 2.0 * kR - 1e-9, 0.0}, T, Tol).CueBallPlacement == PlacementCommand::ToHeadSpot);
	RB_CHECK(PlanRerack14(Cue, 15, {-0.635 - 2.0 * kR + 1e-6, 0.0}, T, Tol).CueBallPlacement == PlacementCommand::ToCenterSpot);
}

namespace
{
	ShotRecord TwoLagBalls()
	{
		ShotRecord R;
		Vec2 A;
		Vec2 B;
		LagStartPositions(NineFootTable(), A, B);
		for (int Ball = 0; Ball < 2; ++Ball)
		{
			R.Start.Presence[Ball] = BallPresence::OnTable;
			R.Start.Position[Ball] = Ball == 0 ? A : B;
			R.Start.Radius[Ball] = kR;
			StrokeInfo Stroke;
			Stroke.Ball = static_cast<BallId>(Ball);
			R.Stroke.Strokes.PushBack(Stroke);
			TipContact Tip;
			Tip.Ball = static_cast<BallId>(Ball);
			Tip.Strike = Ball;
			Tip.Start = 0.0;
			Tip.End = 0.0012;
			R.Stroke.TipContacts.PushBack(Tip);
			RecordEvent Foot;
			Foot.Type = RecordEventType::BallCushion;
			Foot.A = static_cast<BallId>(Ball);
			Foot.Feature = static_cast<std::uint8_t>(CushionId::Foot);
			Foot.Time = 1.0;
			Foot.Sequence = static_cast<std::uint32_t>(Ball);
			R.Events.push_back(Foot);
			R.End.Balls[Ball].Status = BallEndStatus::OnTable;
		}
		return R;
	}

	void RestAtDistance(ShotRecord& R, int Ball, double Distance)
	{
		R.End.Balls[Ball].Position = {-1.27 + kR + Distance, Ball == 0 ? -0.3 : 0.3};
	}

	LagResult Judge(const ShotRecord& R)
	{
		const RulesTable T = NineFootTable();
		const RulesTolerances Tol;
		return EvaluateLag(DeriveLagBallFacts(R, 0, T, Tol), DeriveLagBallFacts(R, 1, T, Tol), Tol);
	}
}

RB_TEST(Rules_Match_LagCueTouchesOtherLagBall)
{
	// (h): A's follow-through touches B's lag ball -> A's lag is bad, B's is not (B did nothing wrong).
	ShotRecord R = TwoLagBalls();
	RestAtDistance(R, 0, 0.02);
	RestAtDistance(R, 1, 0.40);
	TipContact Stray;
	Stray.Ball = 1;
	Stray.Strike = 0;
	Stray.Start = 0.2;
	Stray.End = 0.201;
	R.Stroke.TipContacts.PushBack(Stray);
	NonTipContact Mirror; // the simulator also reports it as a CueTip non-tip contact on ball 1
	Mirror.Ball = 1;
	Mirror.Source = NonTipSource::CueTip;
	Mirror.Time = 0.2;
	R.Stroke.NonTipContacts.PushBack(Mirror);
	const LagResult Result = Judge(R);
	RB_CHECK(Result.First.Bad && Result.First.OtherFoul);
	RB_CHECK(!Result.Second.Bad);
	RB_CHECK(Result.Outcome == LagOutcome::SecondWins);
}

RB_TEST(Rules_Match_LagTieBoundaryAndSupportedBall)
{
	// |dA - dB| within eps_lag (0.4 mm) re-lags; beyond it (0.6 mm) the nearer ball wins.
	ShotRecord R = TwoLagBalls();
	RestAtDistance(R, 0, 0.05);
	RestAtDistance(R, 1, 0.05 + 0.4e-3);
	RB_CHECK(Judge(R).Outcome == LagOutcome::Relag);
	RestAtDistance(R, 1, 0.05 + 0.6e-3);
	RB_CHECK(Judge(R).Outcome == LagOutcome::FirstWins);

	// A ball left hanging over a pocket, supported by the other lag ball, counts as pocketed (c).
	SupportedBall Hanging;
	Hanging.Ball = 0;
	Hanging.Pocket = PocketId::HeadRight;
	Hanging.Supporters = 1u << 1;
	R.End.Supported.PushBack(Hanging);
	const LagResult Result = Judge(R);
	RB_CHECK(Result.First.Bad && Result.First.PocketedOrOffTable);
	RB_CHECK(Result.Outcome == LagOutcome::SecondWins);
}
