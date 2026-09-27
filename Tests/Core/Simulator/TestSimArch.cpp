// Owner: WP-6a (simulator core loop). Architecture tests A-SIM-1..9 (Docs/architecture.md 17 WP-6a), human-factors HF-B14, and
// further loop checks (tip-contact intervals, frozen target, continuesInitialFreeze, recording capacities). Tests that need WP-6b's
// islands / pockets or WP-7's record builder and orientation law carry the Integ_ prefix.

#include "rbtest.h"

#include "Simulator/SimTestUtil.h"

#include "rb/Physics/BallBall.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Slate.h"
#include "rb/Rules/Lag.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Shot/ShotRecord.h"

#include <cmath>

using namespace rb;
using namespace simtest;

namespace
{
	bool SameState(const BallState& A, const BallState& B)
	{
		return A.Position == B.Position && A.Velocity == B.Velocity && A.Omega == B.Omega && A.State == B.State;
	}

	bool SameFinal(const BallFinal& A, const BallFinal& B)
	{
		return A.Status == B.Status && SameState(A.State, B.State) && A.Time == B.Time && A.Pocket == B.Pocket;
	}

	int IndexOf(const ShotResult& R, const ShotEvent* E) { return static_cast<int>(E - R.Events.data()); }

	// A cut shot with rail contacts and line crossings (A-SIM-5, A-SIM-9).
	void CutShot(SimInput& In)
	{
		Place(In, 0, {-0.8, 0.05, kR});
		Place(In, 1, {-0.2, -0.02, kR});
		Place(In, 2, {0.6, 0.3, kR});
		In.Strikes.PushBack(Strike(0, 2.5, std::atan2(-0.02 - 0.05 + 0.03, 0.6), 0.0, 0.1, 0.2));
	}
}

// A-SIM-1: Run rejects ParamsOrigin::Unset, bad inertia factors, strikes on moving balls (and the other input errors of 8.4).
RB_TEST(ARCH_SIM1_RunRejectsInvalidInput)
{
	const TableGeometry& T = NineFoot();
	Simulator Sim;
	ShotResult& R = ResultSlot();
	const auto Expect = [&](SimInput& In, ErrorCode Code)
	{
		RB_CHECK(Sim.Run(In, R) == SimStatus::InvalidInput);
		RB_CHECK(R.Diagnostics.InputError == Code);
		RB_CHECK(R.Events.empty() && R.Diagnostics.EventsProcessed == 0 && R.Strikes.IsEmpty());
	};
	const auto Valid = [&]() -> SimInput&
	{
		SimInput& In = NewInput(T, ColParams());
		Place(In, 0, {-0.6, 0.0, kR});
		Place(In, 1, {0.3, 0.1, kR});
		In.Strikes.PushBack(Strike(0, 2.0, 0.0));
		return In;
	};
	RB_CHECK(Sim.Run(Valid(), R) == SimStatus::Ok && R.Diagnostics.InputError == ErrorCode::Ok);

	SimInput* In = &Valid();
	In->Params.Origin = ParamsOrigin::Unset; // single parameter source (architecture 11 item 6)
	Expect(*In, ErrorCode::InvalidParameter);
	In = &Valid();
	In->Params = PhysicsParams{};
	Expect(*In, ErrorCode::InvalidParameter);
	In = &Valid();
	In->Balls[1].Spec.Inertia = 0.7 * In->Balls[1].Spec.Mass * kR * kR; // k = 0.7 > 2/3
	Expect(*In, ErrorCode::InvalidParameter);
	In = &Valid();
	In->Balls[1].Spec.Inertia = 0.0;
	Expect(*In, ErrorCode::InvalidParameter);
	In = &Valid();
	In->Balls[1].Spec.Radius = -kR;
	Expect(*In, ErrorCode::InvalidParameter);
	In = &Valid();
	In->Balls[0].State.Velocity = {0.1, 0.0, 0.0}; // strike on a moving ball
	In->Balls[0].State.State = MotionState::Sliding;
	Expect(*In, ErrorCode::BallNotAtRest);
	In = &Valid();
	In->Strikes.PushBack(Strike(0, 1.0, 0.5)); // two strikes on one ball
	Expect(*In, ErrorCode::InvalidArgument);
	In = &Valid();
	In->Strikes[0].Ball = 5; // not in play
	Expect(*In, ErrorCode::InvalidArgument);
	In = &Valid();
	In->Strikes[0].Input.Speed = 20.0;
	Expect(*In, ErrorCode::CueSpeedOutOfRange);
	In = &Valid();
	In->Table = nullptr;
	Expect(*In, ErrorCode::InvalidArgument);
	In = &Valid();
	In->Balls[1].State.Position = {-0.6 + 2.0 * kR - 2e-6, 0.0, kR}; // overlap > OverlapGuard
	Expect(*In, ErrorCode::InvalidState);
	In = &Valid();
	In->Balls[1].State.Position = {2.0, 0.0, kR}; // outside the table
	Expect(*In, ErrorCode::InvalidState);
	In = &Valid();
	In->Balls[1].State.Position = {T.HalfLength + 0.05, 0.0, kR}; // on the cloth height under the foot rail
	Expect(*In, ErrorCode::InvalidState);
	In = &Valid();
	In->Balls[1].State.Position.x = std::nan("");
	Expect(*In, ErrorCode::InvalidState);
	In = &Valid();
	In->Balls[1].State.State = MotionState::Pocketed;
	Expect(*In, ErrorCode::InvalidState);
	// The TiltParams rule with each ball's own k: 6.9 mm/m passes for k = 2/5 (ValidatePhysicsParams) but not for a ball with k = 0.1.
	In = &Valid();
	In->Params.Tilt.Slope = {0.0, 6.9e-3};
	RB_CHECK(ValidatePhysicsParams(In->Params) == ErrorCode::Ok);
	RB_CHECK(Sim.Run(*In, R) == SimStatus::Ok);
	In->Balls[1].Spec.Inertia = 0.1 * In->Balls[1].Spec.Mass * kR * kR;
	Expect(*In, ErrorCode::InvalidParameter);
	// WP-1 request: non-positive tilt tolerance / refresh interval.
	PhysicsParams P = ColParams();
	P.Tilt.Tolerance = 0.0;
	RB_CHECK(ValidatePhysicsParams(P) == ErrorCode::InvalidParameter);
	P = ColParams();
	P.Tilt.RefreshMaxInterval = -1.0;
	RB_CHECK(ValidatePhysicsParams(P) == ErrorCode::InvalidParameter);
	P = ColParams();
	P.Tilt.NapResistance = 1.0;
	RB_CHECK(ValidatePhysicsParams(P) == ErrorCode::InvalidParameter);
	P = ColParams();
	P.Cli.TsujiAlpha = std::nan("");
	RB_CHECK(ValidatePhysicsParams(P) == ErrorCode::InvalidParameter);
	P.Cli.TsujiAlpha = -1.0; // derived at Run start
	RB_CHECK(ValidatePhysicsParams(P) == ErrorCode::Ok);
	RB_CHECK(ValidatePhysicsParams(MakePhysicsParams(kTableSevenFootBar, TableCondition{{2.5e-3, 0.0}, 1.3, true})) == ErrorCode::Ok);
}

// A-SIM-2 (physics part): the lag - two strikes in one simulation, both at t = 0, each with its own CueStrike, tip interval and
// strike outcome; both balls travel to the foot cushion.
RB_TEST(ARCH_SIM2_LagTwoStrikes)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	const double X = T.Landmarks.HeadStringX - kR - 0.01;
	Place(In, 0, {X, -T.HalfWidth / 2.0, kR});
	Place(In, 1, {X, T.HalfWidth / 2.0, kR});
	In.Strikes.PushBack(Strike(0, 1.2, 0.0));
	In.Strikes.PushBack(Strike(1, 1.1, 0.0));
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_REQUIRE(R.Strikes.Size() == 2);
	RB_CHECK(R.Strikes[0].Ball == 0 && R.Strikes[1].Ball == 1);
	RB_CHECK(R.Strikes[0].Result.Error == ErrorCode::Ok && R.Strikes[1].Result.Error == ErrorCode::Ok);
	int Strikes = 0;
	int Begins = 0;
	int Ends = 0;
	for (const ShotEvent& E : R.Events)
	{
		if (E.Type == ShotEventType::CueStrike)
		{
			RB_CHECK(E.Time == 0.0 && E.Feature == E.A); // strike k on ball k here
			++Strikes;
		}
		if (E.Type == ShotEventType::TipContactBegin)
		{
			RB_CHECK(E.Time == 0.0 && E.Feature == E.A && E.B == kNoBall);
			++Begins;
		}
		if (E.Type == ShotEventType::TipContactEnd)
		{
			RB_CHECK_NEAR(E.Time, kCuePlaying19oz.ContactTime, 0.0);
			++Ends;
		}
	}
	RB_CHECK(Strikes == 2 && Begins == 2 && Ends == 2);
	RB_CHECK(Count(R, ShotEventType::BallBall) == 0);
	for (int b = 0; b < 2; ++b)
	{
		int Foot = 0;
		for (const ShotEvent& E : R.Events)
		{
			Foot += E.Type == ShotEventType::BallCushion && E.A == b && E.Feature == static_cast<std::uint8_t>(CushionId::Foot) ? 1 : 0;
		}
		RB_CHECK(Foot == 1);
		RB_CHECK(R.Finals[b].Status == BallFinalStatus::OnTable);
	}
	RB_CHECK(R.CueTips.size() == 2); // one follow-through piece per cue
}

// A-SIM-2 (record part): per-ball tip contacts and per-ball lag facts from the shared record (WP-7 record builder, WP-9 lag).
RB_TEST(Integ_ARCH_SIM2_LagPerBallFacts)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	const double X = T.Landmarks.HeadStringX - kR - 0.01;
	Place(In, 0, {X, -T.HalfWidth / 2.0, kR});
	Place(In, 1, {X, T.HalfWidth / 2.0, kR});
	In.Strikes.PushBack(Strike(0, 1.2, 0.0));
	In.Strikes.PushBack(Strike(1, 1.1, 0.0));
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_CHECK(R.Record.Stroke.Strokes.Size() == 2);
	int PerBall[2] = {};
	for (const TipContact& C : R.Record.Stroke.TipContacts)
	{
		if (C.Ball >= 0 && C.Ball < 2)
		{
			++PerBall[C.Ball];
			RB_CHECK(C.Strike == C.Ball);
		}
	}
	RB_CHECK(PerBall[0] == 1 && PerBall[1] == 1);
	const rules::RulesTable Rules = rules::MakeRulesTable(T.Spec.Length, T.Spec.Width, kR);
	for (int b = 0; b < 2; ++b)
	{
		const rules::LagBallFacts F = rules::DeriveLagBallFacts(R.Record, b, Rules, RulesTolerances{});
		RB_CHECK(F.FootCushionContacts == 1);
		RB_CHECK(!F.OtherFoul && !F.PocketedOrOffTable && !F.CrossedLongString);
	}
}

// A-SIM-3: an observer at exactly an event time is emitted after the event (for the replaced segment: recomputed on [t, ...) with
// IncludeFrom); an observer of a segment that an event replaced before it happens is never emitted.
RB_TEST(ARCH_SIM3_ObserverOrderAndRecompute)
{
	const TableGeometry& T = NineFoot();
	const double Threshold = T.Landmarks.HeadStringX + NumericsConfig{}.LineCrossEps;
	{
		// The object ball rests exactly on the head string + eps_line; hit head-on it crosses at the contact time.
		SimInput& In = NewInput(T, ColParams());
		Place(In, 1, {Threshold, 0.1, kR});
		PlaceRolling(In, 0, {Threshold - 0.3, 0.1, kR}, {1.0, 0.0, 0.0});
		Simulator Sim;
		ShotResult& R = ResultSlot();
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		const ShotEvent* Hit = First(R, ShotEventType::BallBall);
		const ShotEvent* Cross = First(R, ShotEventType::BallLineCross, 1);
		RB_REQUIRE(Hit != nullptr && Cross != nullptr);
		RB_CHECK(Cross->Time == Hit->Time);
		RB_CHECK(IndexOf(R, Cross) > IndexOf(R, Hit)); // after the event
		RB_CHECK(Cross->Feature == static_cast<std::uint8_t>(TableLine::HeadString) && Cross->SubFeature == 0);
		RB_CHECK(Count(R, ShotEventType::BallLineCross, 1) >= 1);
		double Last = 0.0;
		for (const ShotEvent& E : R.Events)
		{
			RB_CHECK(E.Time >= Last); // the log is time ordered
			Last = E.Time;
		}
	}
	{
		// Ball 1 rolls toward the foot string but is hit back before it gets there: its predicted crossing is dropped; ball 2, which
		// takes over its motion, crosses instead.
		SimInput& In = NewInput(T, ColParams());
		PlaceRolling(In, 1, {0.3, -0.2, kR}, {0.5, 0.0, 0.0});
		Place(In, 2, {0.45, -0.2, kR}, {-1.0, 0.0, 0.0});
		Simulator Sim;
		ShotResult& R = ResultSlot();
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		RB_REQUIRE(Count(R, ShotEventType::BallBall) >= 1);
		int FootByOne = 0;
		int FootByTwo = 0;
		for (const ShotEvent& E : R.Events)
		{
			if (E.Type == ShotEventType::BallLineCross && E.Feature == static_cast<std::uint8_t>(TableLine::FootString))
			{
				FootByOne += E.A == 1 ? 1 : 0;
				FootByTwo += E.A == 2 ? 1 : 0;
			}
		}
		RB_CHECK(FootByOne == 0);
		RB_CHECK(FootByTwo == 1);
	}
}

// A-SIM-4 (physics part): the follow-through tip touches a ball other than the struck one: TipRecontact and a tip interval on that
// ball (right English a = 0.5: the tip travels 19.6 mm right of the cue-ball line, the squirting cue ball passes the object ball).
RB_TEST(ARCH_SIM4_FollowThroughTouchesOtherBall)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	Place(In, 0, {-0.5, 0.0, kR});
	Place(In, 1, {-0.45, -0.0575875, kR});
	In.Strikes.PushBack(Strike(0, 2.0, 0.0, 0.0, 0.5, 0.0));
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	const ShotEvent* Touch = First(R, ShotEventType::TipRecontact, 1);
	RB_REQUIRE(Touch != nullptr);
	RB_CHECK(Touch->Feature == 0 && Touch->NormalImpulse > 0.0 && Touch->Time > 0.0 && Touch->Time < 0.2);
	const ShotEvent* Begin = nullptr;
	for (const ShotEvent& E : R.Events)
	{
		if (E.Type == ShotEventType::TipContactBegin && E.A == 1)
		{
			Begin = &E;
		}
	}
	RB_REQUIRE(Begin != nullptr);
	RB_CHECK(Begin->Time == Touch->Time && Begin->Feature == 0);
	RB_CHECK(Count(R, ShotEventType::TipContactEnd, 1) == 1);
	for (const ShotEvent& E : R.Events)
	{
		RB_CHECK(!(E.Type == ShotEventType::BallBall && E.Time <= Touch->Time)); // the cue ball did not hit it first
	}
	RB_CHECK(Count(R, ShotEventType::TipRecontact, 0) == 0); // no double hit
	RB_CHECK(R.CueTips.size() == 2);                          // the tip path changed at the touch
	RB_CHECK(R.CueTips[0].T1 == Touch->Time);
}

// A-SIM-4 (record part): the touch becomes NonTipContact{CueTip} of ball 1 in the record (WP-7).
RB_TEST(Integ_ARCH_SIM4_CueTipNonTipContactInRecord)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	Place(In, 0, {-0.5, 0.0, kR});
	Place(In, 1, {-0.45, -0.0575875, kR});
	In.Strikes.PushBack(Strike(0, 2.0, 0.0, 0.0, 0.5, 0.0));
	In.Record.LogObservers = false;
	In.Record.Trajectories = false;
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	bool Found = false;
	for (const NonTipContact& C : R.Record.Stroke.NonTipContacts)
	{
		Found = Found || (C.Ball == 1 && C.Source == NonTipSource::CueTip);
	}
	RB_CHECK(Found);
}

// A-SIM-5 (physics part): the physics outcome does not depend on the logging switches; the switched-off logs only lose their own
// event types.
RB_TEST(ARCH_SIM5_OutcomeIndependentOfLoggingSwitches)
{
	const TableGeometry& T = NineFoot();
	Simulator Sim;
	ShotResult& Base = ResultSlot(1);
	SimInput& In = NewInput(T, ColParams());
	CutShot(In);
	RB_REQUIRE(Sim.Run(In, Base) == SimStatus::Ok);
	RB_REQUIRE(Count(Base, ShotEventType::BallBall) >= 1 && Count(Base, ShotEventType::BallLineCross) >= 1);
	ShotResult& R = ResultSlot();
	for (int Mask = 0; Mask < 16; ++Mask)
	{
		In.Record.Trajectories = (Mask & 1) != 0;
		In.Record.EventStates = (Mask & 2) != 0;
		In.Record.LogTransitions = (Mask & 4) != 0;
		In.Record.LogObservers = (Mask & 8) != 0;
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		RB_CHECK(R.StopTime == Base.StopTime);
		RB_CHECK(R.Diagnostics.EventsProcessed == Base.Diagnostics.EventsProcessed);
		for (int b = 0; b < kMaxBalls; ++b)
		{
			RB_CHECK(R.Finals[b].Status == Base.Finals[b].Status && SameState(R.Finals[b].State, Base.Finals[b].State) && R.Finals[b].Time == Base.Finals[b].Time);
		}
		// The log is the base log minus the switched-off types.
		std::size_t k = 0;
		for (const ShotEvent& E : Base.Events)
		{
			const bool Transition = E.Type == ShotEventType::MotionTransition || E.Type == ShotEventType::TiltRefresh;
			const bool Observer = E.Type == ShotEventType::BallLineCross || E.Type == ShotEventType::BallJumpedOver;
			if ((Transition && !In.Record.LogTransitions) || (Observer && !In.Record.LogObservers))
			{
				continue;
			}
			RB_REQUIRE(k < R.Events.size());
			const ShotEvent& X = R.Events[k++];
			RB_CHECK(X.Time == E.Time && X.Type == E.Type && X.A == E.A && X.B == E.B && X.Feature == E.Feature && X.NormalImpulse == E.NormalImpulse);
		}
		RB_CHECK(k == R.Events.size());
		RB_CHECK(R.Tracks[0].Segments.empty() != In.Record.Trajectories);
	}
}

// A-SIM-5 (record part): the rules record is identical for all logging switches, incl. BallJumpedOver with trajectories off
// (a jump shot over a ball; WP-6b landing routing, WP-7 record builder).
RB_TEST(Integ_ARCH_SIM5_RecordIndependentOfLoggingSwitches)
{
	const TableGeometry& T = NineFoot();
	Simulator Sim;
	ShotResult& Base = ResultSlot(1);
	SimInput& In = NewInput(T, ColParams());
	Place(In, 0, {-0.6, 0.0, kR});
	Place(In, 1, {-0.45, 0.0, kR});
	Place(In, 2, {0.3, 0.0, kR});
	StrikeRequest Jump = Strike(0, 4.0, 0.0, 50.0 * kDegToRad);
	Jump.Input.Cue = kCueJump9oz;
	In.Strikes.PushBack(Jump);
	RB_REQUIRE(Sim.Run(In, Base) == SimStatus::Ok);
	int Jumped = 0;
	for (const RecordEvent& E : Base.Record.Events)
	{
		Jumped += E.Type == RecordEventType::BallJumpedOver ? 1 : 0;
	}
	RB_CHECK(Jumped == 1);
	ShotResult& R = ResultSlot();
	for (int Mask = 0; Mask < 16; ++Mask)
	{
		In.Record.Trajectories = (Mask & 1) != 0;
		In.Record.EventStates = (Mask & 2) != 0;
		In.Record.LogTransitions = (Mask & 4) != 0;
		In.Record.LogObservers = (Mask & 8) != 0;
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		RB_REQUIRE(R.Record.Events.size() == Base.Record.Events.size());
		for (std::size_t k = 0; k < R.Record.Events.size(); ++k)
		{
			const RecordEvent& A = R.Record.Events[k];
			const RecordEvent& B = Base.Record.Events[k];
			RB_CHECK(A.Time == B.Time && A.Type == B.Type && A.A == B.A && A.B == B.B && A.Feature == B.Feature && A.Sequence == B.Sequence);
		}
	}
}

// A-SIM-6 (loop part): exactly simultaneous events on disjoint balls commute (permuted ids give bitwise the same final states);
// exactly simultaneous contacts sharing a ball are never resolved by impulses but handed to one island together.
RB_TEST(ARCH_SIM6_ExactSimultaneity)
{
	const TableGeometry& T = NineFoot();
	Simulator Sim;
	{
		// Two mirror-image head-on hits at exactly the same time; ids of the pairs swapped in the second run.
		const int Ids[2][4] = {{0, 1, 2, 3}, {2, 3, 0, 1}};
		ShotResult* Runs[2] = {&ResultSlot(0), &ResultSlot(1)};
		for (int Run = 0; Run < 2; ++Run)
		{
			SimInput& In = NewInput(T, ColParams());
			PlaceRolling(In, Ids[Run][0], {-0.5, 0.3, kR}, {1.0, 0.0, 0.0});
			Place(In, Ids[Run][1], {-0.3, 0.3, kR});
			PlaceRolling(In, Ids[Run][2], {-0.5, -0.3, kR}, {1.0, 0.0, 0.0});
			Place(In, Ids[Run][3], {-0.3, -0.3, kR});
			RB_REQUIRE(Sim.Run(In, *Runs[Run]) == SimStatus::Ok);
		}
		const ShotResult& A = *Runs[0];
		const ShotResult& B = *Runs[1];
		int Hits = 0;
		double HitTime = -1.0;
		for (const ShotEvent& E : A.Events)
		{
			if (E.Type == ShotEventType::BallBall)
			{
				Hits += HitTime < 0.0 || E.Time == HitTime ? 1 : 0;
				HitTime = HitTime < 0.0 ? E.Time : HitTime;
			}
		}
		RB_CHECK(Hits >= 2); // the two first contacts at exactly the same time
		for (int k = 0; k < 4; ++k)
		{
			RB_CHECK(SameFinal(A.Finals[Ids[0][k]], B.Finals[Ids[1][k]]));
		}
		RB_CHECK(A.Diagnostics.IslandHandOffs == 0);
	}
	{
		// Balls 0 and 2 reach ball 1 from both sides at exactly the same time: one group, two hand-offs, no impulse.
		SimInput& In = NewInput(T, ColParams());
		PlaceRolling(In, 0, {-0.3, 0.2, kR}, {1.0, 0.0, 0.0});
		Place(In, 1, {0.0, 0.2, kR});
		PlaceRolling(In, 2, {0.3, 0.2, kR}, {-1.0, 0.0, 0.0});
		ShotResult& R = ResultSlot();
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		RB_CHECK(R.Diagnostics.IslandHandOffs >= 2); // both members of the group (and, with no island, what follows)
		RB_CHECK(Count(R, ShotEventType::BallBall) == 0 || R.Diagnostics.Islands > 0);
	}
}

// A-SIM-6 (island part): the shared simultaneous contacts resolved in one island are symmetric and id-independent (WP-6b).
RB_TEST(Integ_ARCH_SIM6_SharedSimultaneousEventsIslanded)
{
	const TableGeometry& T = NineFoot();
	Simulator Sim;
	const int Ids[2][3] = {{0, 1, 2}, {2, 1, 0}};
	ShotResult* Runs[2] = {&ResultSlot(0), &ResultSlot(1)};
	for (int Run = 0; Run < 2; ++Run)
	{
		SimInput& In = NewInput(T, ColParams());
		PlaceRolling(In, Ids[Run][0], {-0.3, 0.2, kR}, {1.0, 0.0, 0.0});
		Place(In, Ids[Run][1], {0.0, 0.2, kR});
		PlaceRolling(In, Ids[Run][2], {0.3, 0.2, kR}, {-1.0, 0.0, 0.0});
		RB_REQUIRE(Sim.Run(In, *Runs[Run]) == SimStatus::Ok);
		RB_CHECK(Runs[Run]->Diagnostics.Islands >= 1);
		RB_CHECK(Runs[Run]->Diagnostics.OverlapWarnings == 0);
	}
	for (int k = 0; k < 3; ++k)
	{
		RB_CHECK(SameFinal(Runs[0]->Finals[Ids[0][k]], Runs[1]->Finals[Ids[1][k]]));
	}
	RB_CHECK_NEAR(Runs[0]->Finals[0].State.Position.x, -Runs[0]->Finals[2].State.Position.x, 1e-9);
	RB_CHECK_NEAR(Runs[0]->Finals[1].State.Position.x, 0.0, 1e-9);
}

// A-SIM-7: TiltRefresh - exact node states (human-factors HF-T16 nodes of HF-T15 (a)), version bump and re-prediction (a later
// contact is still found), stale refreshes skipped, logged only with LogTransitions, counted.
RB_TEST(ARCH_SIM7_TiltRefreshNodesAndInvalidation)
{
	const TableGeometry& T = NineFoot();
	PhysicsParams P = ColParams();
	P.Cloth.RollingResistance = 0.01;
	P.Tilt.Slope = {0.0, 1e-3};
	RB_REQUIRE(P.Tilt.Tolerance == 5e-5);
	const Vec3 Start{-0.9, 0.2, kR};
	SimInput& In = NewInput(T, P);
	PlaceRolling(In, 0, Start, {0.5, 0.0, 0.0});
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	const double Nodes[9] = {1.047533, 2.004048, 2.861368, 3.608756, 4.231390, 4.707615, 5.005814, 5.110593, 5.123343};
	RB_CHECK(R.Diagnostics.TiltRefreshes == 9);
	RB_REQUIRE(Count(R, ShotEventType::TiltRefresh, 0) == 9);
	int k = 0;
	const double K = P.Cloth.RollingResistance * P.Gravity;
	const Vec2 G = InPlaneGravity(P.Tilt, P.Gravity) / (1.0 + kSolidSphereInertiaFactor);
	for (const ShotEvent& E : R.Events)
	{
		if (E.Type != ShotEventType::TiltRefresh)
		{
			continue;
		}
		RB_CHECK_NEAR(E.Time, Nodes[k], 1e-6);
		RB_CHECK(E.From == MotionState::Rolling && E.To == MotionState::Rolling);
		const PursuitState Exact = EvaluatePursuit({0.5, 0.0}, G, K, E.Time); // the node is on the exact solution
		RB_CHECK_NEAR(E.Pre[0].Velocity.x, Exact.X.x, 1e-12);
		RB_CHECK_NEAR(E.Pre[0].Velocity.y, Exact.X.y, 1e-12);
		RB_CHECK_NEAR(E.Pre[0].Position.x, Start.x + Exact.Integral.x, 1e-11);
		RB_CHECK_NEAR(E.Pre[0].Position.y, Start.y + Exact.Integral.y, 1e-11);
		++k;
	}
	const BallFinal& F = R.Finals[0];
	RB_CHECK_NEAR(F.Time, 5.124727634, 1e-8);
	RB_CHECK_NEAR(F.State.Position.x - Start.x, 1.276273166, 1e-8);
	RB_CHECK_NEAR(F.State.Position.y - Start.y, -0.045756497, 1e-8);
	RB_CHECK(Count(R, ShotEventType::BallLineCross, 0) == 3); // baulk line, head string and center string, once each
	RB_CHECK(R.Tracks[0].Segments.size() == 11);            // 10 chain pieces + the final Stationary segment

	// Without LogTransitions: no TiltRefresh in the log, the same count and the same outcome.
	In.Record.LogTransitions = false;
	ShotResult& Quiet = ResultSlot(1);
	RB_REQUIRE(Sim.Run(In, Quiet) == SimStatus::Ok);
	RB_CHECK(Count(Quiet, ShotEventType::TiltRefresh) == 0 && Quiet.Diagnostics.TiltRefreshes == 9);
	RB_CHECK(SameFinal(Quiet.Finals[0], F));

	// A ball in the path between the 2nd and 3rd node: the pending refresh becomes stale and is skipped, the contact is found from
	// the re-predicted piece.
	BallState AtHit;
	RB_REQUIRE(TrackState(R, 0, 2.5, AtHit));
	SimInput& Hit = NewInput(T, P);
	PlaceRolling(Hit, 0, Start, {0.5, 0.0, 0.0});
	Place(Hit, 1, AtHit.Position + Normalized(AtHit.Velocity) * (2.0 * kR));
	ShotResult& H = ResultSlot(2);
	RB_REQUIRE(Sim.Run(Hit, H) == SimStatus::Ok);
	const ShotEvent* Contact = First(H, ShotEventType::BallBall);
	RB_REQUIRE(Contact != nullptr);
	RB_CHECK_NEAR(Contact->Time, 2.5, 1e-6);
	RB_CHECK(H.Diagnostics.StaleEventsSkipped > 0);
	for (const ShotEvent& E : H.Events)
	{
		RB_CHECK(!(E.Type == ShotEventType::TiltRefresh && E.A == 0 && std::fabs(E.Time - Nodes[2]) < 1e-6)); // the old node is gone
	}
	RB_CHECK(H.Diagnostics.IslandHandOffs == 0); // a refresh never islands
}

// A-SIM-8: ChalkCling - the per-contact k_cling from the marks, with Trajectories = false (the orientation of marked balls is kept by
// the loop); off -> k_venue for every contact.
RB_TEST(ARCH_SIM8_ChalkClingPerContact)
{
	const TableGeometry& T = NineFoot();
	const double Cut = 30.0 * kDegToRad;
	const Vec3 N{std::cos(Cut), -std::sin(Cut), 0.0}; // contact normal from the cue ball to the object ball
	const auto Run = [&](bool Cling, double Venue, bool Marked, ShotResult& R)
	{
		PhysicsParams P = ColParams();
		P.ChalkCling = Cling;
		P.BallBall.ClingFactor = Venue;
		SimInput& In = NewInput(T, P);
		In.Record.Trajectories = false;
		PlaceRolling(In, 0, {-0.5, 0.0, kR}, {1.0, 0.0, 0.0});
		Place(In, 1, {-0.2, -2.0 * kR * std::sin(Cut), kR});
		if (Marked)
		{
			In.Balls[1].ChalkMarks.PushBack(ChalkMark{-N, 1.0, 2.5e-3}); // a full mark exactly at the contact point
		}
		Simulator Sim;
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	};
	const auto Expected = [&](const ShotEvent& E, double Cling)
	{
		BallBallParams Params = ColParams().BallBall;
		Params.ClingFactor = Cling;
		const ImpactBody A{E.Pre[0].Position, E.Pre[0].Velocity, E.Pre[0].Omega, kR, kDefaultBallMass, MakeBallSpec(kR, kDefaultBallMass).Inertia};
		const ImpactBody B{E.Pre[1].Position, E.Pre[1].Velocity, E.Pre[1].Omega, kR, kDefaultBallMass, MakeBallSpec(kR, kDefaultBallMass).Inertia};
		return ResolveBallBall(A, B, Params, NumericsConfig{}.RestSpeed, NumericsConfig{}.EpsV).TangentImpulse;
	};
	ShotResult& R = ResultSlot();
	Run(true, 1.0, true, R);
	const ShotEvent* Hit = First(R, ShotEventType::BallBall);
	RB_REQUIRE(Hit != nullptr);
	RB_CHECK_NEAR(Hit->Normal.x, N.x, 1e-9);
	RB_CHECK(Hit->TangentImpulse == Expected(*Hit, 2.5)); // chi = 1: the chalked-contact value
	RB_CHECK(Hit->TangentImpulse != Expected(*Hit, 1.0));
	Run(false, 1.0, true, R); // off: k_venue although marked
	Hit = First(R, ShotEventType::BallBall);
	RB_REQUIRE(Hit != nullptr);
	RB_CHECK(Hit->TangentImpulse == Expected(*Hit, 1.0));
	Run(false, 1.3, false, R); // a dive-bar venue: k_venue 1.3 on every contact
	Hit = First(R, ShotEventType::BallBall);
	RB_REQUIRE(Hit != nullptr);
	RB_CHECK(Hit->TangentImpulse == Expected(*Hit, 1.3));
	Run(true, 1.3, false, R); // cling on, no marks: k_venue exactly
	Hit = First(R, ShotEventType::BallBall);
	RB_REQUIRE(Hit != nullptr);
	RB_CHECK(Hit->TangentImpulse == Expected(*Hit, 1.3));
}

// A-SIM-8 (orientation part): a mark on top of a rolling cue ball reaches the contact point after a quarter turn (d = pi R / 2): the
// orientation law of Playback.h kept by the loop without trajectories (WP-7).
RB_TEST(Integ_ARCH_SIM8_MarkFollowsRollingBall)
{
	const TableGeometry& T = NineFoot();
	PhysicsParams P = ColParams();
	P.ChalkCling = true;
	SimInput& In = NewInput(T, P);
	In.Record.Trajectories = false;
	const double Travel = 0.5 * kPi * kR;
	PlaceRolling(In, 0, {-0.5, 0.0, kR}, {0.4, 0.0, 0.0});
	In.Balls[0].ChalkMarks.PushBack(ChalkMark{{0.0, 0.0, 1.0}, 1.0, 2.5e-3});
	Place(In, 1, {-0.5 + Travel + 2.0 * kR, 0.0, kR});
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	const ShotEvent* Hit = First(R, ShotEventType::BallBall);
	RB_REQUIRE(Hit != nullptr);
	BallBallParams Params = P.BallBall;
	Params.ClingFactor = 2.5;
	const BallSpec Spec = MakeBallSpec(kR, kDefaultBallMass);
	const ImpactBody A{Hit->Pre[0].Position, Hit->Pre[0].Velocity, Hit->Pre[0].Omega, kR, Spec.Mass, Spec.Inertia};
	const ImpactBody B{Hit->Pre[1].Position, Hit->Pre[1].Velocity, Hit->Pre[1].Omega, kR, Spec.Mass, Spec.Inertia};
	RB_CHECK_NEAR(Hit->TangentImpulse, ResolveBallBall(A, B, Params, NumericsConfig{}.RestSpeed, NumericsConfig{}.EpsV).TangentImpulse, 1e-9);
}

// A-SIM-9 (loop part): on a level table the resolver state (BallStateForEvent) is bitwise the segment state (BallStateAt); inside a
// tilt chain piece a contact that approaches by the piece velocity but not by the exact velocity is routed to an island as a pressing
// contact - never an impulse, never dropped.
RB_TEST(ARCH_SIM9_ExactReanchor)
{
	const TableGeometry& T = NineFoot();
	Simulator Sim;
	{
		SimInput& In = NewInput(T, ColParams());
		CutShot(In);
		ShotResult& R = ResultSlot();
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		int Checked = 0;
		for (const ShotEvent& E : R.Events)
		{
			if (E.Type != ShotEventType::BallBall && E.Type != ShotEventType::BallCushion)
			{
				continue;
			}
			const int Balls[2] = {E.A, E.B};
			for (int m = 0; m < 2; ++m)
			{
				if (Balls[m] < 0)
				{
					continue;
				}
				for (const TrajectorySegment& S : R.Tracks[Balls[m]].Segments)
				{
					if (S.T1 == E.Time && S.Kind == SegmentKind::Analytic)
					{
						RB_CHECK(SameState(E.Pre[m], EvaluateSegment(S.Motion, E.Time - S.Motion.T0)));
						++Checked;
					}
				}
			}
		}
		RB_CHECK(Checked >= 3);
	}
	{
		// The tail piece of a slow tilted roll: its velocity is up to tens of degrees off the exact one. An object ball placed so
		// that the piece approaches it while the exact velocity separates from it.
		PhysicsParams P = ColParams();
		P.Tilt.Slope = {0.0, 2e-3};
		SimInput& In = NewInput(T, P);
		PlaceRolling(In, 0, {-0.8, 0.2, kR}, {0.3, 0.0, 0.0});
		ShotResult& Roll = ResultSlot(1);
		RB_REQUIRE(Sim.Run(In, Roll) == SimStatus::Ok);
		const auto& Segments = Roll.Tracks[0].Segments;
		RB_REQUIRE(Segments.size() >= 3);
		const MotionSegment& Tail = Segments[Segments.size() - 2].Motion;
		RB_REQUIRE(Tail.Tilt.Active && !Tail.Tilt.EndsInRefresh);
		// Mid-piece the piece velocity lags the exact one (it turns toward the drive later): the object ball goes on the outside of
		// the curve, away from the path already travelled.
		const double Tau = 0.5 * Tail.TauEnd;
		const Vec3 Piece = VelocityAt(Tail, Tau);
		const Vec3 Exact = EvaluateSegmentForEvent(Tail, Tau).Velocity;
		RB_REQUIRE(Cross(XY(Exact), XY(Piece)) > 0.0);
		const Vec3 Normal = Normalized(Normalized(Piece) - Normalized(Exact));
		RB_REQUIRE(Dot(Piece, Normal) > 1e-7 && Dot(Exact, Normal) < 0.0);
		Place(In, 1, PositionAt(Tail, Tau) + Normal * (2.0 * kR + 1e-12));
		ShotResult& R = ResultSlot();
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		RB_CHECK(R.Diagnostics.PressingContacts == 1);
		RB_CHECK(R.Diagnostics.IslandHandOffs == 1);
		RB_CHECK(Count(R, ShotEventType::BallBall) == 0 || R.Diagnostics.Islands == 1); // no impulse from the event loop
	}
}

// A-SIM-9 (island part): the re-anchored pressing contact is resolved by the island: the balls touch without overlap (WP-6b).
RB_TEST(Integ_ARCH_SIM9_ReanchorPressingIslandResolves)
{
	const TableGeometry& T = NineFoot();
	PhysicsParams P = ColParams();
	P.Tilt.Slope = {0.0, 2e-3};
	SimInput& In = NewInput(T, P);
	PlaceRolling(In, 0, {-0.8, 0.2, kR}, {0.3, 0.0, 0.0});
	Simulator Sim;
	ShotResult& Roll = ResultSlot(1);
	RB_REQUIRE(Sim.Run(In, Roll) == SimStatus::Ok);
	const auto& Segments = Roll.Tracks[0].Segments;
	RB_REQUIRE(Segments.size() >= 3);
	const MotionSegment& Tail = Segments[Segments.size() - 2].Motion;
	const double Tau = 0.5 * Tail.TauEnd;
	const Vec3 Normal = Normalized(Normalized(VelocityAt(Tail, Tau)) - Normalized(EvaluateSegmentForEvent(Tail, Tau).Velocity));
	Place(In, 1, PositionAt(Tail, Tau) + Normal * (2.0 * kR + 1e-12));
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_CHECK(Count(R, ShotEventType::IslandBegin) == 1);
	RB_CHECK(R.Diagnostics.OverlapWarnings == 0);
	RB_CHECK(Length(R.Finals[1].State.Position - R.Finals[0].State.Position) >= 2.0 * kR - 1e-9);
}

// HF-B14: events inside a tilt segment use the exact pursuit velocity - a slow roll (< 4 cm/s) kissing a ball at Slope 2 mm/m gives
// the same object-ball direction as an RK4 reference within 0.1 deg.
RB_TEST(Integ_HF_B14_ExactVelocityInsideTiltPiece)
{
	const TableGeometry& T = NineFoot();
	PhysicsParams P = ColParams();
	P.Tilt.Slope = {0.0, 2e-3};
	const double K = P.Cloth.RollingResistance * P.Gravity;
	const Vec2 G = InPlaneGravity(P.Tilt, P.Gravity) / (1.0 + kSolidSphereInertiaFactor);
	struct Rk
	{
		Vec2 X;
		Vec2 V;
	};
	const auto Derivative = [&](const Rk& S) { return Rk{S.V, G - Normalized(S.V) * K}; };
	const auto Step = [&](const Rk& S, double H)
	{
		const Rk K1 = Derivative(S);
		const Rk K2 = Derivative({S.X + K1.X * (0.5 * H), S.V + K1.V * (0.5 * H)});
		const Rk K3 = Derivative({S.X + K2.X * (0.5 * H), S.V + K2.V * (0.5 * H)});
		const Rk K4 = Derivative({S.X + K3.X * H, S.V + K3.V * H});
		return Rk{S.X + (K1.X + K2.X * 2.0 + K3.X * 2.0 + K4.X) * (H / 6.0), S.V + (K1.V + K2.V * 2.0 + K3.V * 2.0 + K4.V) * (H / 6.0)};
	};
	const Rk Start{{-0.8, 0.1}, {0.25, 0.0}};
	const double H = 1e-4;
	// Where the reference roll is slower than 3.5 cm/s: the object ball sits there, 30 deg off the rolling direction.
	Rk S = Start;
	while (Length(S.V) > 0.035)
	{
		S = Step(S, H);
	}
	const Vec2 D = Normalized(S.V);
	const double Cut = 30.0 * kDegToRad;
	const Vec2 N{D.x * std::cos(Cut) - D.y * std::sin(Cut), D.x * std::sin(Cut) + D.y * std::cos(Cut)};
	const Vec2 Object = S.X + N * (2.0 * kR);
	// RK4 reference contact: first |x - Object| <= 2R, located by bisection on the step.
	Rk A = Start;
	for (;;)
	{
		const Rk B = Step(A, H);
		if (Length(B.X - Object) <= 2.0 * kR)
		{
			double Lo = 0.0;
			double Hi = H;
			for (int i = 0; i < 60; ++i)
			{
				const double Mid = 0.5 * (Lo + Hi);
				(Length(Step(A, Mid).X - Object) <= 2.0 * kR ? Hi : Lo) = Mid;
			}
			A = Step(A, Hi);
			break;
		}
		A = B;
	}
	const BallSpec Spec = MakeBallSpec(kR, kDefaultBallMass);
	const Vec3 CueVelocity = ToVec3(A.V);
	const ImpactBody Cue{ToVec3(A.X, kR), CueVelocity, RollingOmegaH(CueVelocity, kR), kR, Spec.Mass, Spec.Inertia};
	const ImpactBody Ball{ToVec3(Object, kR), {}, {}, kR, Spec.Mass, Spec.Inertia};
	const BallBallImpulse Reference = ResolveBallBall(Cue, Ball, P.BallBall, P.Numerics.RestSpeed, P.Numerics.EpsV);
	BallState After;
	After.Position = ToVec3(Object, kR);
	After.Velocity = Reference.Velocity2;
	After.Omega = Reference.Omega2;
	ApplyTableReaction(After, true, Spec, P.Cloth, P.Slate, P.Gravity, P.Numerics);
	const double ReferenceAngle = std::atan2(After.Velocity.y, After.Velocity.x);

	SimInput& In = NewInput(T, P);
	PlaceRolling(In, 0, ToVec3(Start.X, kR), ToVec3(Start.V));
	Place(In, 1, ToVec3(Object, kR));
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	const ShotEvent* Hit = First(R, ShotEventType::BallBall);
	RB_REQUIRE(Hit != nullptr);
	RB_CHECK(Length(Hit->Pre[0].Velocity) < 0.04);
	const double Angle = std::atan2(Hit->Post[1].Velocity.y, Hit->Post[1].Velocity.x);
	RB_CHECK_NEAR(Angle * kRadToDeg, ReferenceAngle * kRadToDeg, 0.1);
}

// Tip-contact interval of a normal stroke (architecture 8.6): TipContactBegin at 0, TipContactEnd at the cue's ContactTime, both on
// the struck ball with no frozen target; one follow-through piece ending at the cue's stop.
RB_TEST(Sim_NormalStrokeTipInterval)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	Place(In, 0, {-0.6, 0.0, kR});
	In.Strikes.PushBack(Strike(0, 2.0, 0.3));
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_REQUIRE(R.Events.size() >= 3);
	RB_CHECK(R.Events[0].Type == ShotEventType::CueStrike && R.Events[1].Type == ShotEventType::TipContactBegin);
	RB_CHECK(R.Events[0].NormalSpeed == 2.0 && R.Events[0].Value == R.Strikes[0].Result.CueSpeedAfter);
	const ShotEvent* End = First(R, ShotEventType::TipContactEnd);
	RB_REQUIRE(End != nullptr);
	RB_CHECK(End->Time == kCuePlaying19oz.ContactTime && End->A == 0 && End->B == kNoBall && End->Value == 0.0 && End->SubFeature == 0);
	RB_REQUIRE(R.CueTips.size() == 1);
	RB_CHECK(R.CueTips[0].Path.StruckBall == 0 && R.CueTips[0].T1 == R.CueTips[0].Path.StopTime && R.CueTips[0].Path.StopTime > 0.0);
	RB_CHECK(Count(R, ShotEventType::TipRecontact) == 0);
}

// The frozen target f of RUL F7 (architecture 8.6): the ball frozen to the struck ball with the largest positive n_hat . d, on the tip
// events with the gap to it; shooting away from a frozen ball has no target. A frozen strike goes to an island at t = 0 (the tip is
// within delta_cl).
RB_TEST(Sim_FrozenTargetOnTipEvents)
{
	const TableGeometry& T = NineFoot();
	Simulator Sim;
	ShotResult& R = ResultSlot();
	SimInput& In = NewInput(T, ColParams());
	Place(In, 0, {-0.6, 0.0, kR});
	Place(In, 1, {-0.6 + 2.0 * kR + 5e-5, 0.0, kR}); // 0.05 mm: frozen within eps_frozen
	Place(In, 2, {-0.6, 2.0 * kR, kR});
	In.Strikes.PushBack(Strike(0, 1.0, 0.1));
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	const ShotEvent* Begin = First(R, ShotEventType::TipContactBegin);
	RB_REQUIRE(Begin != nullptr);
	RB_CHECK(Begin->B == 1);
	RB_CHECK_NEAR(Begin->Value, 5e-5, 1e-12);
	RB_CHECK(R.Diagnostics.IslandHandOffs >= 1); // CB into the frozen OB with the tip still on the CB: the F7 island

	In.Strikes[0].Input.Azimuth = kPi; // away from ball 1, ball 2 perpendicular: no target
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	Begin = First(R, ShotEventType::TipContactBegin);
	RB_REQUIRE(Begin != nullptr);
	RB_CHECK(Begin->B == kNoBall && Begin->Value == 0.0);
}

// continuesInitialFreeze (rules.md 3.3): a ball frozen to C4 driven straight into it keeps the flag on that contact; after it has left
// by more than eps_leave (across the table and back) the next contact with C4 has no flag.
RB_TEST(Sim_ContinuesInitialFreezeFlag)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	const double Y = T.HalfWidth - ComputeCushionContact(kR, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
	Place(In, 0, {-0.6, Y, kR});
	In.Strikes.PushBack(Strike(0, 3.0, 0.5 * kPi));
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	// At t = 0 the ball is squeezed between the tip and the rail (a zero-time rail / tip / rail sequence, all still frozen); after
	// crossing the table and back, it has left the rail.
	int Frozen = 0;
	int Later = 0;
	for (const ShotEvent& E : R.Events)
	{
		if (E.Type == ShotEventType::BallCushion && E.Feature == static_cast<std::uint8_t>(CushionId::LeftHead))
		{
			const bool Flag = (E.Flags & ShotEventFlags::ContinuesInitialFreeze) != 0;
			RB_CHECK(Flag == (E.Time == 0.0));
			Frozen += E.Time == 0.0 ? 1 : 0;
			Later += E.Time > 0.0 ? 1 : 0;
		}
	}
	RB_CHECK(Frozen >= 1 && Later >= 1);
	RB_CHECK(First(R, ShotEventType::BallCushion)->Time == 0.0);
}

// Recording capacities (ShotResult.h): a full event log, track or cue-tip list sets its overflow flag and never reallocates; the
// physics outcome is unchanged.
RB_TEST(Sim_ResultCapacityOverflowsAreFlagged)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	CutShot(In);
	Simulator Full;
	ShotResult& Base = ResultSlot(1);
	RB_REQUIRE(Full.Run(In, Base) == SimStatus::Ok);
	ResultCapacity Small;
	Small.MaxLoggedEvents = 3;
	Small.MaxSegmentsPerBall = 2;
	Small.MaxCueTipSegments = 1;
	Simulator Tight(Small);
	ShotResult Fresh; // reserved by Run to the small capacities
	RB_REQUIRE(Tight.Run(In, Fresh) == SimStatus::Ok);
	RB_CHECK(Fresh.Events.size() == 3 && Fresh.Diagnostics.EventLogOverflow);
	RB_CHECK(Fresh.Tracks[0].Segments.size() == 2 && Fresh.Diagnostics.TrajectoryOverflow);
	RB_CHECK(!Fresh.Diagnostics.CueTipOverflow && Fresh.CueTips.size() == 1);
	RB_CHECK(Fresh.StopTime == Base.StopTime);
	for (int b = 0; b < kMaxBalls; ++b)
	{
		RB_CHECK(SameFinal(Fresh.Finals[b], Base.Finals[b]));
	}
	const std::size_t Capacity = Fresh.Events.capacity();
	RB_REQUIRE(Tight.Run(In, Fresh) == SimStatus::Ok);
	RB_CHECK(Fresh.Events.capacity() == Capacity); // reused without reallocation
}

// A strike hop (elevated cue): BallAirborne at t = 0 with the apex height of the flight (8.6); the landing belongs to WP-6b.
RB_TEST(Sim_StrikeHopLogsBallAirborne)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	Place(In, 0, {-0.6, 0.0, kR});
	StrikeRequest Jump = Strike(0, 4.0, 0.0, 45.0 * kDegToRad);
	Jump.Input.Cue = kCueJump9oz;
	In.Strikes.PushBack(Jump);
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_REQUIRE(R.Strikes[0].Result.State.State == MotionState::Airborne);
	const ShotEvent* Air = First(R, ShotEventType::BallAirborne, 0);
	RB_REQUIRE(Air != nullptr);
	const Vec3 V = R.Strikes[0].Result.State.Velocity;
	RB_CHECK(Air->Time == 0.0);
	RB_CHECK_NEAR(Air->Value, kR + V.z * V.z / (2.0 * kGCol), 1e-12);
}
