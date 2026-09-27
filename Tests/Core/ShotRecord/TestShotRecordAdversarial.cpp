// WP-7 review: adversarial ShotRecordBuilder tests (rules.md 3.1-3.4, 4.11): a standalone rebuild from a log without motion
// transitions, ball-id permutations of the snapshots, a frozen cluster and a ball in the jaws, unpaired / interleaved tip
// events.

#include "rbtest.h"

#include "Playback/PlaybackTestUtil.h"

#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Shot/ShotRecordBuilder.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace playtest;
using rb::ShotEvent;
using rb::ShotEventType;
using rb::Vec2;

namespace
{
	constexpr double kBallR = 0.028575;

	struct Table
	{
		rb::TableGeometry Geometry;
		rb::SimInput Input;
		rb::ShotResult Result;

		Table()
		{
			rb::BuildTableGeometry(rb::kTableNineFootPro, Geometry);
			Input.Table = &Geometry;
			Input.Params = rb::MakePhysicsParams(Geometry.Spec);
			rb::ReserveShotResult(Result, rb::ResultCapacity{});
			rb::ResetShotResult(Result);
			Result.Status = rb::SimStatus::Ok;
			Result.StopTime = 2.0;
		}

		void Place(int Ball, const Vec2& P)
		{
			rb::SimBall& B = Input.Balls[Ball];
			B.InPlay = true;
			B.Spec = rb::kStandardPoolBall;
			B.State = rb::BallState{};
			B.State.Position = {P.x, P.y, kBallR};
			Result.Finals[Ball].Status = rb::BallFinalStatus::OnTable;
			Result.Finals[Ball].State = B.State;
		}

		ShotEvent Event(ShotEventType Type, double T, int A, int B = rb::kNoBall) const
		{
			ShotEvent E;
			E.Type = Type;
			E.Time = T;
			E.A = static_cast<rb::BallId>(A);
			E.B = static_cast<rb::BallId>(B);
			E.Pre[0].Position = Input.Balls[A].State.Position;
			if (B != rb::kNoBall)
			{
				E.Pre[1].Position = Input.Balls[B].State.Position;
			}
			return E;
		}
	};
}

// A standalone rebuild needs every record-relevant event in the log. Without RecordOptions::LogTransitions the log lacks
// the MotionTransition events that the record built during Run contains, so the rebuilt record is incomplete and must say so
// (it used to be reported complete).
RB_TEST(Record_Adv_RebuildWithoutTransitionLogIsTruncated)
{
	const std::unique_ptr<Table> F = std::make_unique<Table>();
	F->Place(0, {-0.6, 0.0});
	F->Place(1, {0.4, 0.0});
	F->Input.Strikes.PushBack(rb::StrikeRequest{});
	F->Result.Strikes.PushBack(rb::StrikeOutcome{});
	ShotEvent Begin = F->Event(ShotEventType::TipContactBegin, 0.0, 0);
	Begin.Feature = 0;
	ShotEvent End = Begin;
	End.Type = ShotEventType::TipContactEnd;
	End.Time = 1.0e-3;
	ShotEvent Transition = F->Event(ShotEventType::MotionTransition, 0.3, 0);
	Transition.From = rb::MotionState::Sliding;
	Transition.To = rb::MotionState::Rolling;
	F->Result.Events = {Begin, End, Transition, F->Event(ShotEventType::BallBall, 0.5, 0, 1)};

	rb::ShotRecord Full;
	rb::BuildShotRecord(F->Input, F->Result, Full);
	RB_CHECK(!Full.Truncated);
	RB_CHECK(Full.Events.size() == 4u);

	// The same shot logged without transitions: the log has no MotionTransition, the rebuild cannot know it is missing.
	F->Input.Record.LogTransitions = false;
	F->Result.Events.erase(F->Result.Events.begin() + 2);
	rb::ShotRecord Rebuilt;
	rb::BuildShotRecord(F->Input, F->Result, Rebuilt);
	RB_CHECK(Rebuilt.Events.size() == 3u);
	RB_CHECK(Rebuilt.Truncated);
}

// Relabelling object balls permutes the snapshots and nothing else (rules.md 3.1 / 3.4 frozen sets, per-ball radii): the
// predicates are per ball / per pair, independent of the ids.
RB_TEST(Record_Adv_PermutedObjectBallIdsPermuteTheSnapshots)
{
	// Cue ball frozen to two balls of a cluster, one of them frozen to the foot cushion, one ball against the incoming jaw
	// of the side pocket P1, one free ball, one resting pair elsewhere.
	const std::unique_ptr<Table> Base = std::make_unique<Table>();
	const rb::TableGeometry& G = Base->Geometry;
	const double Rc = rb::ComputeCushionContact(kBallR, G.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
	const double X = 0.5 * G.Spec.Length - Rc - 2.0 * kBallR;
	const rb::JawArc& Arc = G.JawArcs[2 * 1 + 0];
	const double Mid = Arc.AngleFrom + 0.5 * Arc.AngleSweep;
	const double RcArc = rb::ComputeCushionContact(kBallR, Arc.Height, 0.0, false).HorizontalOffset;
	const Vec2 Positions[6] = {
		{X, 0.0},                                                                     // cue ball
		{X + 2.0 * kBallR, 0.0},                                                      // frozen to the CB and the foot cushion
		{X, 2.0 * kBallR + 0.5e-4},                                                   // frozen to the CB (gap 0.05 mm)
		Arc.Center + Vec2{std::cos(Mid), std::sin(Mid)} * (Arc.Radius + RcArc),       // in the jaws of P1
		{-0.3, -0.2},                                                                 // free
		{-0.3 + 2.0 * kBallR + 2.0e-4, -0.2},                                         // 0.2 mm from the free ball: not frozen
	};
	const int Ids[6] = {0, 3, 7, 9, 12, 14};
	const int Permuted[6] = {0, 11, 2, 15, 5, 8};
	const std::unique_ptr<Table> P = std::make_unique<Table>();
	for (int i = 0; i < 6; ++i)
	{
		Base->Place(Ids[i], Positions[i]);
		P->Place(Permuted[i], Positions[i]);
	}
	rb::ShotRecord A;
	rb::ShotRecord B;
	rb::BuildShotRecord(Base->Input, Base->Result, A);
	rb::BuildShotRecord(P->Input, P->Result, B);

	const auto MapMask = [&](std::uint32_t Mask) {
		std::uint32_t Out = 0;
		for (int i = 0; i < 6; ++i)
		{
			if ((Mask & (1u << Ids[i])) != 0u)
			{
				Out |= 1u << Permuted[i];
			}
		}
		return Out;
	};
	RB_CHECK(A.Start.FrozenToCueBall == ((1u << 3) | (1u << 7)));
	RB_CHECK(B.Start.FrozenToCueBall == MapMask(A.Start.FrozenToCueBall));
	RB_CHECK(A.Start.FrozenToRail[3] == (1u << rb::RailFeatureOfCushion(rb::CushionId::Foot)));
	RB_CHECK(A.Start.FrozenToRail[9] == (1u << rb::RailFeatureOfJaw(rb::PocketId::SideRight, rb::JawSide::Incoming)));
	RB_CHECK(A.Start.FrozenToRail[12] == 0u && A.Start.FrozenToRail[14] == 0u && A.Start.FrozenToRail[0] == 0u);
	for (int i = 0; i < 6; ++i)
	{
		const int a = Ids[i];
		const int b = Permuted[i];
		RB_CHECK(A.Start.FrozenToRail[a] == B.Start.FrozenToRail[b]);
		RB_CHECK(SameBits(A.Start.Position[a].x, B.Start.Position[b].x) && SameBits(A.Start.Radius[a], B.Start.Radius[b]));
		RB_CHECK(A.Start.Presence[a] == B.Start.Presence[b]);
		RB_CHECK(A.End.Balls[a].Status == B.End.Balls[b].Status && A.End.Balls[a].FrozenToRail == B.End.Balls[b].FrozenToRail);
		RB_CHECK(B.End.Balls[b].FrozenToBalls == MapMask(A.End.Balls[a].FrozenToBalls));
	}
	RB_CHECK(A.End.Balls[0].FrozenToBalls == ((1u << 3) | (1u << 7)));
	RB_CHECK(A.End.Balls[3].FrozenToBalls == 1u && A.End.Balls[12].FrozenToBalls == 0u);
	RB_CHECK(!A.Truncated && !B.Truncated);
}

// Tip events that do not pair up (a nested begin from another source, an end without a begin, a strike index out of range)
// never lose the first interval and always mark the record incomplete; interleaved intervals of two strikes on the same
// ball stay separate per strike.
RB_TEST(Record_Adv_InconsistentTipEventsAreTruncatedNotLost)
{
	const std::unique_ptr<Table> F = std::make_unique<Table>();
	F->Place(0, {-0.6, 0.0});
	F->Place(1, {-0.6, 0.3});
	rb::StrikeRequest S0;
	S0.Ball = 0;
	rb::StrikeRequest S1;
	S1.Ball = 1;
	F->Input.Strikes.PushBack(S0);
	F->Input.Strikes.PushBack(S1);
	F->Result.Strikes.PushBack(rb::StrikeOutcome{});
	F->Result.Strikes.PushBack(rb::StrikeOutcome{});
	const auto Tip = [&](bool Begin, double T, int Ball, int Strike) {
		ShotEvent E = F->Event(Begin ? ShotEventType::TipContactBegin : ShotEventType::TipContactEnd, T, Ball);
		E.Feature = static_cast<std::uint8_t>(Strike);
		return E;
	};

	// Two strikes touching ball 0 in interleaved intervals: [0, 1 ms] by cue 0, [0.5 ms, 3 ms] by cue 1.
	F->Result.Events = {Tip(true, 0.0, 0, 0), Tip(true, 0.5e-3, 0, 1), Tip(false, 1.0e-3, 0, 0), Tip(false, 3.0e-3, 0, 1)};
	rb::ShotRecord R;
	rb::BuildShotRecord(F->Input, F->Result, R);
	RB_CHECK(!R.Truncated);
	RB_REQUIRE(R.Stroke.TipContacts.Size() == 2);
	RB_CHECK(R.Stroke.TipContacts[0].Strike == 0 && R.Stroke.TipContacts[0].End == 1.0e-3);
	RB_CHECK(R.Stroke.TipContacts[1].Strike == 1 && R.Stroke.TipContacts[1].Start == 0.5e-3 && R.Stroke.TipContacts[1].End == 3.0e-3);
	RB_REQUIRE(R.Stroke.NonTipContacts.Size() == 1); // cue 1 touched ball 0, which it did not strike
	RB_CHECK(R.Stroke.NonTipContacts[0].Ball == 0 && R.Stroke.NonTipContacts[0].Time == 0.5e-3);

	// An end without a begin, and a strike index beyond the strikes: incomplete, the valid interval kept.
	F->Result.Events = {Tip(false, 0.2e-3, 1, 1), Tip(true, 0.0, 0, 0), Tip(false, 1.0e-3, 0, 0), Tip(true, 0.4, 0, 7)};
	rb::BuildShotRecord(F->Input, F->Result, R);
	RB_CHECK(R.Truncated);
	RB_REQUIRE(R.Stroke.TipContacts.Size() == 1);
	RB_CHECK(R.Stroke.TipContacts[0].Ball == 0 && R.Stroke.TipContacts[0].Start == 0.0 && R.Stroke.TipContacts[0].End == 1.0e-3);
}
