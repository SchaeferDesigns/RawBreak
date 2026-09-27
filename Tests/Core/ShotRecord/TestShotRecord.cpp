// ShotRecordBuilder (rules.md 3.1-3.4, 4.11; Docs/architecture.md 8.6, 8.10): A-REC-1 (per-strike tip intervals merged
// from record events; a tip contact on a non-struck ball -> NonTipContact{CueTip}), the event mapping, the start and end
// snapshots with the frozen declarations, capacity and truncation rules (WP-7). The rules side (DeriveShotFacts,
// DeriveLagBallFacts: WP-8 / WP-9, merged) judges the built records.

#include "rbtest.h"

#include "Playback/PlaybackTestUtil.h"

#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Rules/Lag.h"
#include "rb/Rules/ShotFacts.h"
#include "rb/Shot/ShotRecordBuilder.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace playtest;
using rb::RecordEvent;
using rb::RecordEventType;
using rb::ShotEvent;
using rb::ShotEventType;
using rb::Vec2;
using rb::Vec3;

namespace
{
	constexpr double kBallR = 0.028575;

	struct Fixture
	{
		rb::TableGeometry Geometry;
		rb::SimInput Input;
		rb::ShotResult Result;

		Fixture()
		{
			rb::BuildTableGeometry(rb::kTableNineFootPro, Geometry);
			Input.Table = &Geometry;
			Input.Params = rb::MakePhysicsParams(Geometry.Spec);
			Place(0, {-0.635, 0.0});
			Place(1, {0.5, 0.1});
			Place(3, {0.3, -0.3});
			rb::StrikeRequest Strike;
			Strike.Ball = 0;
			Strike.Input.Speed = 2.0;
			Strike.Input.Elevation = 0.05;
			Strike.Input.TipTouchesCloth = true;
			Input.Strikes.PushBack(Strike);
			rb::ReserveShotResult(Result, rb::ResultCapacity{});
			rb::ResetShotResult(Result);
			Result.Status = rb::SimStatus::Ok;
			Result.StopTime = 3.0;
			rb::StrikeOutcome Outcome;
			Outcome.Ball = 0;
			Outcome.Result.Miscue = true;
			Result.Strikes.PushBack(Outcome);
			for (int b = 0; b < rb::kMaxBalls; ++b)
			{
				if (Input.Balls[b].InPlay)
				{
					Result.Finals[b].Status = rb::BallFinalStatus::OnTable;
					Result.Finals[b].State = Input.Balls[b].State;
				}
			}
		}

		void Place(int Ball, const Vec2& P, const rb::BallSpec& Spec = rb::kStandardPoolBall)
		{
			rb::SimBall& B = Input.Balls[Ball];
			B.InPlay = true;
			B.Spec = Spec;
			B.State = rb::BallState{};
			B.State.Position = {P.x, P.y, Spec.Radius};
		}

		ShotEvent Event(ShotEventType Type, double T, int A, int B = rb::kNoBall)
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

		ShotEvent Tip(bool Begin, double T, int Ball, int Strike = 0, int Frozen = rb::kNoBall, double Gap = 0.0, bool Other = false)
		{
			ShotEvent E = Event(Begin ? ShotEventType::TipContactBegin : ShotEventType::TipContactEnd, T, Ball, Frozen);
			E.Feature = static_cast<std::uint8_t>(Strike);
			E.Value = Gap;
			E.SubFeature = Other ? 1 : 0;
			return E;
		}

		rb::rules::RulesTable Rules() const
		{
			double Radii[rb::kMaxBalls] = {};
			for (int b = 0; b < rb::kMaxBalls; ++b)
			{
				Radii[b] = Input.Balls[b].InPlay ? Input.Balls[b].Spec.Radius : 0.0;
			}
			return rb::BuildRulesTable(Geometry, kBallR, Radii, rb::kMaxBalls);
		}
	};

	bool SameRecordEvent(const RecordEvent& A, const RecordEvent& B)
	{
		return SameBits(A.Time, B.Time) && A.Sequence == B.Sequence && A.Type == B.Type && A.A == B.A && A.B == B.B && A.Feature == B.Feature &&
			A.Side == B.Side && A.ContinuesInitialFreeze == B.ContinuesInitialFreeze && A.OtherContactBefore == B.OtherContactBefore && A.From == B.From &&
			A.To == B.To && SameBits(A.PositionA.x, B.PositionA.x) && SameBits(A.PositionA.y, B.PositionA.y) && SameBits(A.PositionB.x, B.PositionB.x) &&
			SameBits(A.PositionB.y, B.PositionB.y) && SameBits(A.Normal, B.Normal) && SameBits(A.CutAngle, B.CutAngle) && SameBits(A.ZMax, B.ZMax) &&
			SameBits(A.Value, B.Value);
	}
}

// A-REC-1: tip intervals per (strike, ball) from the record events, merged below one contact time; a tip contact on a
// ball the strike did not strike -> NonTipContact{CueTip}; the rules then see the double hit and the touched ball.
RB_TEST(ARCH_REC1_TipIntervalsMergedFromRecordEvents)
{
	const std::unique_ptr<Fixture> F = std::make_unique<Fixture>();
	rb::NonTipContact Hand;
	Hand.Ball = 1;
	Hand.Source = rb::NonTipSource::BridgeHand;
	Hand.Time = -0.2;
	F->Input.Context.NonTipContacts.PushBack(Hand);

	std::vector<ShotEvent>& Log = F->Result.Events;
	Log.push_back(F->Tip(true, 0.0, 0));
	Log.push_back(F->Tip(false, 1.0e-3, 0));
	Log.push_back(F->Tip(true, 1.5e-3, 0)); // 0.5 ms after the end: closer than ContactTime (1 ms) -> one long contact
	Log.push_back(F->Tip(false, 2.5e-3, 0));
	Log.push_back(F->Tip(true, 5.0e-3, 3)); // the follow-through touches ball 3
	Log.push_back(F->Tip(false, 5.5e-3, 3));
	Log.push_back(F->Tip(true, 6.0e-3, 0)); // a real second hit
	Log.push_back(F->Tip(false, 7.0e-3, 0));
	Log.push_back(F->Event(ShotEventType::BallBall, 0.4, 0, 1));
	ShotEvent Strike = F->Event(ShotEventType::CueStrike, 0.0, 0); // not record-relevant
	Log.insert(Log.begin(), Strike);

	rb::ShotRecord Record;
	rb::BuildShotRecord(F->Input, F->Result, Record);
	RB_CHECK(!Record.Truncated);
	RB_CHECK(Record.Events.size() == 9u);

	const rb::StrokeRecord& S = Record.Stroke;
	RB_REQUIRE(S.Strokes.Size() == 1);
	RB_CHECK(S.Strokes[0].Ball == 0);
	RB_CHECK(S.Strokes[0].Miscue);
	RB_CHECK(S.Strokes[0].TipClothContact);
	RB_CHECK(S.Strokes[0].CueElevation == 0.05);
	RB_REQUIRE(S.TipContacts.Size() == 3);
	RB_CHECK(S.TipContacts[0].Ball == 0 && S.TipContacts[0].Start == 0.0 && S.TipContacts[0].End == 2.5e-3 && S.TipContacts[0].Strike == 0);
	RB_CHECK(S.TipContacts[1].Ball == 3 && S.TipContacts[1].Start == 5.0e-3 && S.TipContacts[1].End == 5.5e-3);
	RB_CHECK(S.TipContacts[2].Ball == 0 && S.TipContacts[2].Start == 6.0e-3 && S.TipContacts[2].End == 7.0e-3);
	RB_REQUIRE(S.NonTipContacts.Size() == 2);
	RB_CHECK(S.NonTipContacts[0].Source == rb::NonTipSource::BridgeHand && S.NonTipContacts[0].Ball == 1);
	RB_CHECK(S.NonTipContacts[1].Source == rb::NonTipSource::CueTip && S.NonTipContacts[1].Ball == 3 && S.NonTipContacts[1].Time == 5.0e-3);
	RB_CHECK(!S.Overflow);

	// The incremental path of the simulator (Begin / Append / Finish) builds the same record.
	rb::ShotRecord Incremental;
	Incremental.Events.reserve(64);
	rb::BeginShotRecord(F->Input, Incremental);
	for (const ShotEvent& E : Log)
	{
		rb::AppendRecordEvent(E, Incremental);
	}
	rb::FinishShotRecord(F->Input, F->Result, Incremental);
	RB_REQUIRE(Incremental.Events.size() == Record.Events.size());
	for (std::size_t i = 0; i < Record.Events.size(); ++i)
	{
		RB_CHECK(SameRecordEvent(Incremental.Events[i], Record.Events[i]));
	}
	RB_CHECK(Incremental.Stroke.TipContacts.Size() == 3 && Incremental.Stroke.NonTipContacts.Size() == 2);

	// The rules (WP-8): two cue-ball tip intervals -> double hit; touched ball; miscue.
	rb::rules::ShotFacts Facts;
	rb::rules::DeriveShotFacts(Record, F->Rules(), rb::RulesTolerances{}, rb::kInfinity, Facts);
	RB_CHECK(Facts.DoubleHit);
	RB_CHECK(!Facts.PushShot);
	RB_CHECK(Facts.NonTipBallContact);
	RB_CHECK(Facts.Miscue);
	RB_CHECK(Facts.EarliestContact == 1);
}

// A-REC-1: the frozen-ball envelope data travels through the merged intervals (both ends are exact record events):
// a cue ball frozen to OB 1 pushed through it within d_sep is neither a double hit nor a push; beyond d_sep it is both.
RB_TEST(ARCH_REC1_FrozenEnvelopeDataAtBothEndsOfMergedIntervals)
{
	for (const bool Beyond : {false, true})
	{
		const std::unique_ptr<Fixture> F = std::make_unique<Fixture>();
		F->Place(1, {-0.635 + 2.0 * kBallR, 0.0}); // frozen to the cue ball
		std::vector<ShotEvent>& Log = F->Result.Events;
		Log.push_back(F->Tip(true, 0.0, 0, 0, 1, 0.0));
		Log.push_back(F->Event(ShotEventType::BallBall, 1.0e-4, 0, 1));
		Log.push_back(F->Tip(false, 1.0e-3, 0, 0, 1, 1.0e-4));
		Log.push_back(F->Tip(true, 3.0e-3, 0, 0, 1, 2.0e-3));
		Log.push_back(F->Tip(true, 3.2e-3, 0, 0, 1, 2.1e-3)); // nested begin: ignored
		Log.push_back(F->Tip(false, 9.0e-3, 0, 0, 1, Beyond ? 6.0e-3 : 4.0e-3)); // 6 ms contact
		rb::ShotRecord Record;
		rb::BuildShotRecord(F->Input, F->Result, Record);
		RB_CHECK(!Record.Truncated);
		RB_CHECK((Record.Start.FrozenToCueBall & (1u << 1)) != 0u);
		RB_REQUIRE(Record.Stroke.TipContacts.Size() == 2);
		RB_CHECK(Record.Stroke.TipContacts[1].Start == 3.0e-3 && Record.Stroke.TipContacts[1].End == 9.0e-3);
		RB_CHECK(Record.Stroke.NonTipContacts.IsEmpty());
		rb::rules::ShotFacts Facts;
		rb::rules::DeriveShotFacts(Record, F->Rules(), rb::RulesTolerances{}, rb::kInfinity, Facts);
		RB_CHECK(Facts.DoubleHit == Beyond);
		RB_CHECK(Facts.PushShot == Beyond);
	}
}

// A-REC-1 / A-SIM-2 data: the lag's two strikes keep their tip intervals per strike; strike 0's follow-through touching
// the other lag ball is a CueTip non-tip contact that the lag rules attribute to the striker (WP-9 DeriveLagBallFacts).
RB_TEST(ARCH_REC1_LagTipContactsPerStrike)
{
	const std::unique_ptr<Fixture> F = std::make_unique<Fixture>();
	F->Input.Balls[3] = rb::SimBall{};
	F->Place(0, {-0.70, -0.3175});
	F->Place(1, {-0.70, 0.3175});
	F->Result.Finals[0].State = F->Input.Balls[0].State;
	F->Result.Finals[1].State = F->Input.Balls[1].State;
	F->Result.Finals[3] = rb::BallFinal{};
	rb::StrikeRequest Second;
	Second.Ball = 1;
	Second.Input.Speed = 2.0;
	F->Input.Strikes.PushBack(Second);
	rb::StrikeOutcome Outcome;
	Outcome.Ball = 1;
	F->Result.Strikes.PushBack(Outcome);

	std::vector<ShotEvent>& Log = F->Result.Events;
	Log.push_back(F->Tip(true, 0.0, 0, 0));
	Log.push_back(F->Tip(true, 0.0, 1, 1));
	Log.push_back(F->Tip(false, 1.0e-3, 0, 0));
	Log.push_back(F->Tip(false, 1.0e-3, 1, 1));
	Log.push_back(F->Tip(true, 0.08, 1, 0)); // cue 0 touches lag ball 1
	Log.push_back(F->Tip(false, 0.081, 1, 0));
	rb::ShotRecord Record;
	rb::BuildShotRecord(F->Input, F->Result, Record);
	RB_CHECK(!Record.Truncated);
	RB_REQUIRE(Record.Stroke.Strokes.Size() == 2);
	RB_CHECK(Record.Stroke.Strokes[0].Ball == 0 && Record.Stroke.Strokes[1].Ball == 1);
	RB_CHECK(!Record.Stroke.Strokes[1].Miscue);
	RB_REQUIRE(Record.Stroke.TipContacts.Size() == 3);
	RB_CHECK(Record.Stroke.TipContacts[0].Ball == 0 && Record.Stroke.TipContacts[0].Strike == 0);
	RB_CHECK(Record.Stroke.TipContacts[1].Ball == 1 && Record.Stroke.TipContacts[1].Strike == 1);
	RB_CHECK(Record.Stroke.TipContacts[2].Ball == 1 && Record.Stroke.TipContacts[2].Strike == 0);
	RB_REQUIRE(Record.Stroke.NonTipContacts.Size() == 1);
	RB_CHECK(Record.Stroke.NonTipContacts[0].Ball == 1 && Record.Stroke.NonTipContacts[0].Source == rb::NonTipSource::CueTip);

	const rb::rules::RulesTable Table = F->Rules();
	const rb::rules::LagBallFacts Lag0 = rb::rules::DeriveLagBallFacts(Record, 0, Table, rb::RulesTolerances{});
	const rb::rules::LagBallFacts Lag1 = rb::rules::DeriveLagBallFacts(Record, 1, Table, rb::RulesTolerances{});
	RB_CHECK(Lag0.OtherFoul);  // its cue touched the other ball
	RB_CHECK(!Lag1.OtherFoul); // one own tip contact, the CueTip contact is not its foul
}

// rules.md 3.3: which physics events enter the record and how their fields map.
RB_TEST(Record_EventRelevanceAndMapping)
{
	using T = ShotEventType;
	const T Relevant[] = {T::TipContactBegin, T::TipContactEnd, T::BallBall, T::BallCushion, T::BallJaw, T::BallRailTop, T::BallAirborne, T::BallLand,
		T::BallPocketEnter, T::BallPocketRim, T::BallLiner, T::BallPocketExit, T::BallPocketed, T::BallOffTable, T::BallExternalContact,
		T::MotionTransition, T::BallLineCross, T::BallJumpedOver};
	const T Irrelevant[] = {T::CueStrike, T::TipRecontact, T::BallSlate, T::IslandBegin, T::IslandRigid, T::IslandEnd, T::ZenoGuard, T::Diagnostic,
		T::TiltRefresh};
	RecordEvent R;
	for (const T Type : Relevant)
	{
		ShotEvent E;
		E.Type = Type;
		RB_CHECK(rb::IsRecordRelevant(Type));
		RB_CHECK(rb::ToRecordEvent(E, 5, R));
		RB_CHECK(R.Sequence == 5u);
	}
	for (const T Type : Irrelevant)
	{
		ShotEvent E;
		E.Type = Type;
		RB_CHECK(!rb::IsRecordRelevant(Type));
		RB_CHECK(!rb::ToRecordEvent(E, 0, R));
	}

	ShotEvent Ball;
	Ball.Type = T::BallBall;
	Ball.Time = 0.25;
	Ball.A = 0;
	Ball.B = 7;
	Ball.Normal = {0.6, 0.8, 0.0};
	Ball.CutAngle = 0.4;
	Ball.Pre[0].Position = {0.1, 0.2, kBallR};
	Ball.Pre[1].Position = {0.13, 0.24, kBallR};
	RB_REQUIRE(rb::ToRecordEvent(Ball, 3, R));
	RB_CHECK(R.Type == RecordEventType::BallBall && R.A == 0 && R.B == 7 && R.Time == 0.25 && R.CutAngle == 0.4);
	RB_CHECK(R.PositionA.x == 0.1 && R.PositionA.y == 0.2 && R.PositionB.x == 0.13 && R.PositionB.y == 0.24);
	RB_CHECK(SameBits(R.Normal, Ball.Normal));

	ShotEvent Jaw;
	Jaw.Type = T::BallJaw;
	Jaw.A = 4;
	Jaw.Feature = static_cast<std::uint8_t>(rb::PocketId::FootLeft);
	Jaw.SubFeature = static_cast<std::uint8_t>(static_cast<int>(rb::JawSide::Outgoing) | (1 << 4)); // facing face
	Jaw.Flags = rb::ShotEventFlags::ContinuesInitialFreeze;
	RB_REQUIRE(rb::ToRecordEvent(Jaw, 0, R));
	RB_CHECK(R.Type == RecordEventType::BallJaw && R.Feature == 3 && R.Side == 1 && R.ContinuesInitialFreeze);

	ShotEvent Cushion;
	Cushion.Type = T::BallCushion;
	Cushion.Feature = 2;
	RB_REQUIRE(rb::ToRecordEvent(Cushion, 0, R));
	RB_CHECK(R.Type == RecordEventType::BallCushion && R.Feature == 2 && !R.ContinuesInitialFreeze);

	ShotEvent Rim;
	Rim.Type = T::BallPocketRim;
	Rim.Feature = 5;
	RB_REQUIRE(rb::ToRecordEvent(Rim, 0, R));
	RB_CHECK(R.Type == RecordEventType::BallLiner && R.Feature == 5); // the pocket interior is a rail

	ShotEvent Cross;
	Cross.Type = T::BallLineCross;
	Cross.Feature = static_cast<std::uint8_t>(rb::TableLine::HeadString);
	Cross.SubFeature = 1;
	RB_REQUIRE(rb::ToRecordEvent(Cross, 0, R));
	RB_CHECK(R.Type == RecordEventType::BallLineCross && R.Side == -1);
	Cross.SubFeature = 0;
	RB_REQUIRE(rb::ToRecordEvent(Cross, 0, R));
	RB_CHECK(R.Side == 1);

	ShotEvent Up;
	Up.Type = T::BallAirborne;
	Up.Value = 0.071;
	RB_REQUIRE(rb::ToRecordEvent(Up, 0, R));
	RB_CHECK(R.Type == RecordEventType::BallAirborne && R.ZMax == 0.071);
	Up.Type = T::BallLand;
	RB_REQUIRE(rb::ToRecordEvent(Up, 0, R));
	RB_CHECK(R.Type == RecordEventType::BallLand && R.ZMax == 0.071);

	ShotEvent Trans;
	Trans.Type = T::MotionTransition;
	Trans.From = rb::MotionState::Sliding;
	Trans.To = rb::MotionState::Rolling;
	RB_REQUIRE(rb::ToRecordEvent(Trans, 0, R));
	RB_CHECK(R.From == rb::MotionState::Sliding && R.To == rb::MotionState::Rolling);

	ShotEvent Tip;
	Tip.Type = T::TipContactEnd;
	Tip.A = 0;
	Tip.B = 2;
	Tip.Feature = 1;
	Tip.Value = 0.003;
	Tip.SubFeature = 1;
	RB_REQUIRE(rb::ToRecordEvent(Tip, 0, R));
	RB_CHECK(R.Type == RecordEventType::TipBallEnd && R.Feature == 1 && R.B == 2 && R.Value == 0.003 && R.OtherContactBefore);
	Tip.B = rb::kNoBall;
	Tip.SubFeature = 0;
	RB_REQUIRE(rb::ToRecordEvent(Tip, 0, R));
	RB_CHECK(R.Value == 0.0 && !R.OtherContactBefore);

	ShotEvent Off;
	Off.Type = T::BallOffTable;
	Off.Feature = static_cast<std::uint8_t>(rb::OffTableReason::RestsOnRailOrFrame);
	RB_REQUIRE(rb::ToRecordEvent(Off, 0, R));
	RB_CHECK(R.Type == RecordEventType::BallOffTable && R.Feature == 1);
	ShotEvent Jump;
	Jump.Type = T::BallJumpedOver;
	Jump.A = 0;
	Jump.B = 9;
	RB_REQUIRE(rb::ToRecordEvent(Jump, 0, R));
	RB_CHECK(R.Type == RecordEventType::BallJumpedOver && R.A == 0 && R.B == 9);
}

// Appends never reallocate (overflow -> Truncated), sequence numbers are the append index, FinishShotRecord sorts stably
// by (Time, Sequence) and fills the pocket of a rail-top pocket surround from the geometry.
RB_TEST(Record_CapacitySortingAndRailTopSurround)
{
	const std::unique_ptr<Fixture> F = std::make_unique<Fixture>();
	rb::ShotRecord Record;
	Record.Events.reserve(3);
	rb::BeginShotRecord(F->Input, Record);
	const std::size_t Capacity = Record.Events.capacity();
	RB_CHECK(!rb::AppendRecordEvent(F->Event(ShotEventType::IslandBegin, 0.1, 0), Record)); // irrelevant: no change
	RB_CHECK(!Record.Truncated);
	for (std::size_t i = 0; i < Capacity; ++i)
	{
		RB_CHECK(rb::AppendRecordEvent(F->Event(ShotEventType::BallCushion, 0.5 - 0.1 * static_cast<double>(i % 2), 1), Record));
	}
	RB_CHECK(Record.Events.capacity() == Capacity);
	RB_CHECK(!rb::AppendRecordEvent(F->Event(ShotEventType::BallCushion, 0.9, 1), Record));
	RB_CHECK(Record.Truncated);
	RB_CHECK(Record.Events.capacity() == Capacity);
	rb::FinishShotRecord(F->Input, F->Result, Record);
	for (std::size_t i = 1; i < Record.Events.size(); ++i)
	{
		const RecordEvent& A = Record.Events[i - 1];
		const RecordEvent& B = Record.Events[i];
		RB_CHECK(A.Time < B.Time || (A.Time == B.Time && A.Sequence < B.Sequence));
	}
	RB_CHECK(Record.Truncated);

	// Rail-top pocket surround: Side = its pocket.
	int Surround = -1;
	for (int i = 0; i < F->Geometry.RailTops.Size(); ++i)
	{
		if (F->Geometry.RailTops[i].Cushion == rb::CushionId::None && F->Geometry.RailTops[i].Pocket != rb::PocketId::None)
		{
			Surround = i;
			break;
		}
	}
	RB_REQUIRE(Surround >= 0);
	std::vector<ShotEvent>& Log = F->Result.Events;
	ShotEvent Top = F->Event(ShotEventType::BallRailTop, 0.7, 3);
	Top.Feature = 0xFF;
	Top.Value = static_cast<double>(Surround);
	Log.push_back(Top);
	Log.push_back(F->Event(ShotEventType::BallBall, 0.2, 0, 3)); // out of order in the log
	rb::ShotRecord Rebuilt;
	rb::BuildShotRecord(F->Input, F->Result, Rebuilt);
	RB_REQUIRE(Rebuilt.Events.size() == 2u);
	RB_CHECK(Rebuilt.Events[0].Type == RecordEventType::BallBall && Rebuilt.Events[0].Sequence == 1u);
	RB_CHECK(Rebuilt.Events[1].Type == RecordEventType::BallRailTop && Rebuilt.Events[1].Feature == 0xFF);
	RB_CHECK(Rebuilt.Events[1].Side == static_cast<int>(F->Geometry.RailTops[Surround].Pocket));
}

// rules.md 3.1 / 4.11: presence, radii, rest, in-hand placement, and the auto-declared frozen sets (nose lines at R_c,
// jaw arcs, facings, cue-ball contacts) with eps_frozen.
RB_TEST(Record_StartSnapshotFrozenDeclarations)
{
	const std::unique_ptr<Fixture> F = std::make_unique<Fixture>();
	const rb::TableGeometry& G = F->Geometry;
	const double Rc = rb::ComputeCushionContact(kBallR, G.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
	const double Eps = F->Input.Context.FrozenTolerance;

	// Oversized cue ball frozen to OB 2 (gap 5e-5), OB 4 just not (2e-4).
	F->Place(0, {-0.4, 0.0}, rb::kOversizedCueBall);
	const double Rcb = rb::kOversizedCueBall.Radius;
	F->Place(2, {-0.4 + Rcb + kBallR + 0.5 * Eps, 0.0});
	F->Place(4, {-0.4, Rcb + kBallR + 2.0 * Eps});
	// OB 5 frozen to the foot cushion C2, OB 6 0.2 mm away from it.
	F->Place(5, {0.5 * G.Spec.Length - Rc - 0.3 * Eps, 0.2});
	F->Place(6, {0.5 * G.Spec.Length - Rc - 2.0 * Eps, -0.2});
	// OB 7 against the exposed arc of the incoming jaw of the side pocket P1 (mid-arc direction).
	const rb::JawArc& Arc = G.JawArcs[2 * 1 + 0];
	const double Mid = Arc.AngleFrom + 0.5 * Arc.AngleSweep;
	const double RcArc = rb::ComputeCushionContact(kBallR, Arc.Height, 0.0, false).HorizontalOffset;
	const Vec2 ArcPoint = Arc.Center + Vec2{std::cos(Mid), std::sin(Mid)} * (Arc.Radius + RcArc);
	F->Place(7, ArcPoint);
	// OB 8 on the shelf of the corner pocket P3 against its outgoing facing.
	const rb::Facing& Face = G.Facings[2 * 3 + 1];
	const double Sf = rb::FacingContactOffset(kBallR, Face.TopHeight, Face.Backdraft);
	F->Place(8, Face.Start + Face.Direction * (0.5 * Face.Length) + Face.PocketNormal * Sf);

	rb::ShotStartSnapshot S;
	rb::BuildShotStartSnapshot(F->Input, S);
	RB_CHECK(S.AllBallsAtRest);
	RB_CHECK(S.Presence[0] == rb::BallPresence::OnTable && S.Presence[9] == rb::BallPresence::NotUsed);
	RB_CHECK(S.Radius[0] == Rcb && S.Radius[2] == kBallR && S.Radius[9] == 0.0);
	RB_CHECK(S.Position[5].x == F->Input.Balls[5].State.Position.x);
	RB_CHECK(S.FrozenToCueBall == (1u << 2));
	RB_CHECK(S.FrozenToRail[5] == (1u << rb::RailFeatureOfCushion(rb::CushionId::Foot)));
	RB_CHECK(S.FrozenToRail[6] == 0u);
	RB_CHECK(S.FrozenToRail[7] == (1u << rb::RailFeatureOfJaw(rb::PocketId::SideRight, rb::JawSide::Incoming)));
	RB_CHECK((S.FrozenToRail[8] & (1u << rb::RailFeatureOfJaw(rb::PocketId::FootLeft, rb::JawSide::Outgoing))) != 0u);
	RB_CHECK(S.FrozenToRail[1] == 0u && S.FrozenToRail[0] == 0u);
	RB_CHECK(!S.PlacementOverPocket && S.InHand == rb::CueBallInHand::No);
	// The public predicate (shared with the event loop's initial-freeze tracking) is the snapshot's.
	for (int b = 0; b < 9; ++b)
	{
		const rb::SimBall& Ball = F->Input.Balls[b];
		if (Ball.InPlay)
		{
			RB_CHECK(rb::FrozenRailFeatures(G, F->Input.Params.Cushion, rb::XY(Ball.State.Position), Ball.Spec.Radius, Eps) == S.FrozenToRail[b]);
		}
	}
	RB_CHECK(rb::FrozenRailFeatures(G, F->Input.Params.Cushion, rb::XY(F->Input.Balls[5].State.Position), 0.0, Eps) == 0u);

	// In hand over a pocket opening; a moving ball.
	F->Input.Context.InHand = rb::CueBallInHand::Anywhere;
	F->Input.Context.PlacedPosition = G.Pockets[3].CaptureCenter;
	F->Input.Context.ShotClockElapsed = 12.5;
	F->Input.Context.FootOnFloor = false;
	F->Input.Balls[3].State = SurfaceState({0.3, -0.3, kBallR}, {0.0, 0.0, 0.0}, {0.0, 0.0, 3.0}); // spinning in place
	rb::BuildShotStartSnapshot(F->Input, S);
	RB_CHECK(S.PlacementOverPocket);
	RB_CHECK(S.InHand == rb::CueBallInHand::Anywhere && S.ShotClockElapsed == 12.5 && !S.FootOnFloor);
	RB_CHECK(!S.AllBallsAtRest);
	F->Input.Context.PlacedPosition = {-0.8, 0.1};
	rb::BuildShotStartSnapshot(F->Input, S);
	RB_CHECK(!S.PlacementOverPocket);

	// No table: no rail sets, the rest still works.
	F->Input.Table = nullptr;
	rb::BuildShotStartSnapshot(F->Input, S);
	RB_CHECK(S.FrozenToRail[5] == 0u && S.FrozenToCueBall == (1u << 2));
}

// rules.md 3.4: the end snapshot from the finals (frozen sets at rest), pocketed / off-table balls, and truncation when a
// ball has no final status, a tip interval stays open or the simulation failed.
RB_TEST(Record_EndSnapshotAndTruncation)
{
	const std::unique_ptr<Fixture> F = std::make_unique<Fixture>();
	const double Rc = rb::ComputeCushionContact(kBallR, F->Geometry.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
	// Final positions: OB 1 frozen to the head cushion, CB touching it; OB 3 pocketed in P2.
	rb::BallFinal& One = F->Result.Finals[1];
	One.State.Position = {-0.5 * F->Geometry.Spec.Length + Rc, 0.3, kBallR};
	rb::BallFinal& Cue = F->Result.Finals[0];
	Cue.State.Position = {One.State.Position.x + 2.0 * kBallR, 0.3, kBallR};
	rb::BallFinal& Three = F->Result.Finals[3];
	Three.Status = rb::BallFinalStatus::Pocketed;
	Three.Pocket = rb::PocketId::FootRight;
	F->Result.StopTime = 4.25;

	rb::ShotRecord Record;
	rb::BuildShotRecord(F->Input, F->Result, Record);
	RB_CHECK(!Record.Truncated);
	const rb::ShotEndSnapshot& E = Record.End;
	RB_CHECK(E.StopTime == 4.25);
	RB_CHECK(E.Balls[1].Status == rb::BallEndStatus::OnTable);
	RB_CHECK(E.Balls[1].FrozenToRail == (1u << rb::RailFeatureOfCushion(rb::CushionId::Head)));
	RB_CHECK(E.Balls[1].FrozenToBalls == 1u);
	RB_CHECK(E.Balls[0].FrozenToBalls == (1u << 1));
	RB_CHECK(E.Balls[0].Position.x == Cue.State.Position.x);
	RB_CHECK(E.Balls[3].Status == rb::BallEndStatus::Pocketed && E.Balls[3].Pocket == rb::PocketId::FootRight);
	RB_CHECK(E.Balls[5].Status == rb::BallEndStatus::NotUsed);

	// Failure states.
	const rb::BallFinal Saved = One;
	F->Result.Status = rb::SimStatus::Aborted;
	rb::BuildShotRecord(F->Input, F->Result, Record);
	RB_CHECK(Record.Truncated);
	F->Result.Status = rb::SimStatus::Ok;
	F->Result.Finals[1] = rb::BallFinal{};
	rb::BuildShotRecord(F->Input, F->Result, Record);
	RB_CHECK(Record.Truncated);
	RB_CHECK(Record.End.Balls[1].Status == rb::BallEndStatus::OnTable);
	F->Result.Finals[1] = Saved;
	F->Result.Events.push_back(F->Tip(true, 0.0, 0));
	rb::BuildShotRecord(F->Input, F->Result, Record);
	RB_CHECK(Record.Truncated); // unpaired begin
	RB_REQUIRE(Record.Stroke.TipContacts.Size() == 1);
	RB_CHECK(Record.Stroke.TipContacts[0].End == F->Result.StopTime);
	F->Result.Events.push_back(F->Tip(false, 1.0e-3, 0));
	rb::BuildShotRecord(F->Input, F->Result, Record);
	RB_CHECK(!Record.Truncated);
	// A standalone rebuild needs the complete log.
	F->Input.Record.LogObservers = false;
	rb::BuildShotRecord(F->Input, F->Result, Record);
	RB_CHECK(Record.Truncated);
	F->Input.Record.LogObservers = true;
	F->Result.Diagnostics.EventLogOverflow = true;
	rb::BuildShotRecord(F->Input, F->Result, Record);
	RB_CHECK(Record.Truncated);
}

// More tip intervals than the record's list holds: the list keeps the first ones and the record is Truncated.
RB_TEST(Record_TipContactOverflowTruncates)
{
	const std::unique_ptr<Fixture> F = std::make_unique<Fixture>();
	for (int i = 0; i < rb::kMaxTipContacts + 3; ++i)
	{
		const double T = 0.01 * static_cast<double>(i);
		F->Result.Events.push_back(F->Tip(true, T, 0));
		F->Result.Events.push_back(F->Tip(false, T + 1.0e-3, 0));
	}
	rb::ShotRecord Record;
	rb::BuildShotRecord(F->Input, F->Result, Record);
	RB_CHECK(Record.Stroke.TipContacts.Size() == rb::kMaxTipContacts);
	RB_CHECK(Record.Stroke.Overflow);
	RB_CHECK(Record.Truncated);
	RB_CHECK(Record.Stroke.TipContacts[0].Start == 0.0);
}
