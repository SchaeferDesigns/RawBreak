// Owner: WP-6a (simulator core loop). Guards and off-table routing of the event loop: physics-collisions 9.6 (Z-1, Z-2, Z-4,
// O-1, O-2) and prior-art 9 (ROB-04, ROB-05, ROB-06, ROB-15). Architecture 8.4 (main loop, event cap, horizon), 8.5 (dispatch),
// 9 (Zeno guards and termination).

#include "rbtest.h"

#include "Simulator/SimTestUtil.h"

#include <cmath>

using namespace rb;
using namespace simtest;

namespace
{
	// y of a ball center touching the RAIL_LEFT nose (C3 / C4, y = +W/2) on the cloth: W/2 - R_c.
	double LeftRailContactY(const TableGeometry& T, double Radius = kR)
	{
		return T.HalfWidth - ComputeCushionContact(Radius, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
	}
}

// COL Z-1: two stationary touching balls and a stationary ball frozen to a cushion produce no event at all.
RB_TEST(COL_Z1_RestingContactsProduceNoEvents)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ColParams());
	Place(In, 1, {0.2, 0.1, kR});
	Place(In, 2, {0.2 + 2.0 * kR, 0.1, kR});
	Place(In, 3, {-0.6, LeftRailContactY(T), kR});
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_CHECK(R.Events.empty());
	RB_CHECK(R.Diagnostics.EventsProcessed == 0);
	RB_CHECK(R.Diagnostics.OverlapWarnings == 0);
	RB_CHECK(R.StopTime == 0.0);
	for (int b = 1; b <= 3; ++b)
	{
		RB_CHECK(R.Finals[b].Status == BallFinalStatus::OnTable);
		RB_CHECK(R.Finals[b].State.Position == In.Balls[b].State.Position);
		RB_CHECK(R.Finals[b].State.State == MotionState::Stationary);
	}
	RB_CHECK(R.BallsInPlay == ((1u << 1) | (1u << 2) | (1u << 3)));
}

// COL Z-2: a ball rolling exactly parallel to and touching RAIL_LEFT produces no cushion event until it stops.
RB_TEST(COL_Z2_RollingAlongRailNoCushionEvents)
{
	const TableGeometry& T = NineFoot();
	const PhysicsParams P = ColParams();
	SimInput& In = NewInput(T, P);
	const double Y = LeftRailContactY(T);
	const double V0 = 0.3;
	PlaceRolling(In, 1, {-1.0, Y, kR}, {V0, 0.0, 0.0});
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_CHECK(Count(R, ShotEventType::BallCushion) == 0);
	RB_CHECK(Count(R, ShotEventType::BallJaw) == 0);
	RB_CHECK(R.Diagnostics.IslandHandOffs == 0);
	const BallFinal& F = R.Finals[1];
	RB_CHECK(F.Status == BallFinalStatus::OnTable && F.State.State == MotionState::Stationary);
	RB_CHECK(F.State.Position.y == Y); // exactly along the rail
	const double Distance = V0 * V0 / (2.0 * P.Cloth.RollingResistance * P.Gravity);
	RB_CHECK_NEAR(F.State.Position.x, -1.0 + Distance, 1e-12);
	RB_CHECK_NEAR(F.Time, V0 / (P.Cloth.RollingResistance * P.Gravity), 1e-12);
	RB_CHECK_NEAR(R.StopTime, F.Time, 0.0);
}

// COL Z-4: a pathological setup beyond the 20 000-event cap (a tilt chain with 10 us pieces: 1e5 refreshes per second) aborts
// cleanly: aborted = true, every ball stopped where it was, no hang.
RB_TEST(COL_Z4_EventCapAbortsAndStopsAllBalls)
{
	const TableGeometry& T = NineFoot();
	PhysicsParams P = ColParams();
	P.Tilt.Slope = {0.0, 2e-3};
	P.Tilt.RefreshMaxInterval = 1e-5;
	RB_REQUIRE(P.Numerics.MaxEvents == 20000);
	SimInput& In = NewInput(T, P);
	PlaceRolling(In, 1, {-1.0, 0.2, kR}, {1.0, 0.0, 0.0});
	PlaceRolling(In, 2, {0.5, -0.3, kR}, {-0.5, 0.2, 0.0});
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Aborted);
	RB_CHECK(R.Diagnostics.EventsProcessed == 20000);
	RB_CHECK(R.Diagnostics.TiltRefreshes >= 19990);
	RB_CHECK(R.StopTime > 0.05 && R.StopTime < 0.2);
	for (int b = 1; b <= 2; ++b)
	{
		const BallFinal& F = R.Finals[b];
		RB_CHECK(F.Status == BallFinalStatus::OnTable);
		RB_CHECK(F.State.State == MotionState::Stationary);
		RB_CHECK(F.State.Velocity == Vec3{} && F.State.Omega == Vec3{});
		RB_CHECK(F.Time == R.StopTime);
	}
	RB_CHECK(R.Diagnostics.TrajectoryOverflow); // 20 000 pieces > MaxSegmentsPerBall: flagged, never reallocated
	RB_CHECK(static_cast<int>(R.Tracks[1].Segments.size()) == ResultCapacity{}.MaxSegmentsPerBall);
	RB_CHECK(R.Finals[1].State.Position.x > -1.0 + 0.05 && R.Finals[1].State.Position.x < -1.0 + 0.2); // stopped where it was
	RB_CHECK(R.Record.Truncated); // the rules replay an aborted shot
}

// COL O-1: a ball launched with v = (0, 3, 2.5) from (0, 0.5, R) toward RAIL_LEFT flies over the rail (nose line y = 0.635 at
// t = 0.045 s, z = 0.13115 m), touches no nose and no rail top, and crosses y = W/2 + RAIL_WIDTH_TOTAL = 0.8128 at t = 0.104267 s
// with z = 0.23593 m -> BallOffTable(Floor).
RB_TEST(COL_O1_FlightOverTheRailLeavesTheTable)
{
	const TableGeometry& T = NineFoot();
	const PhysicsParams P = ColParams();
	SimInput& In = NewInput(T, P);
	Place(In, 0, {0.0, 0.5, kR}, {0.0, 3.0, 2.5});
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_CHECK(Count(R, ShotEventType::BallCushion) == 0);
	RB_CHECK(Count(R, ShotEventType::BallJaw) == 0);
	RB_CHECK(Count(R, ShotEventType::BallRailTop) == 0);
	RB_CHECK(Count(R, ShotEventType::BallLiner) == 0);
	const ShotEvent* Off = First(R, ShotEventType::BallOffTable, 0);
	RB_REQUIRE(Off != nullptr);
	RB_CHECK(Off->Feature == static_cast<std::uint8_t>(OffTableReason::Floor));
	const double Boundary = T.HalfWidth + T.Spec.RailWidthTotal;
	RB_CHECK_NEAR(Boundary, 0.8128, 1e-12);
	RB_CHECK_NEAR(Off->Time, (Boundary - 0.5) / 3.0, 1e-12);
	RB_CHECK_NEAR(Off->Time, 0.104267, 1e-6);
	const BallFinal& F = R.Finals[0];
	RB_CHECK(F.Status == BallFinalStatus::OffTable && F.OffReason == OffTableReason::Floor);
	RB_CHECK_NEAR(F.State.Position.y, Boundary, 1e-12);
	RB_CHECK_NEAR(F.State.Position.z, 0.23593, 1e-5);
	RB_CHECK(F.Time == Off->Time && R.StopTime == Off->Time);
	BallState AtNose;
	RB_REQUIRE(TrackState(R, 0, 0.045, AtNose));
	RB_CHECK_NEAR(AtNose.Position.y, 0.635, 1e-12);
	RB_CHECK_NEAR(AtNose.Position.z, 0.13115, 1e-5);
	RB_CHECK(R.Tracks[0].Segments.back().Kind == SegmentKind::Terminal);
}

// COL O-2: v_z0 = 4.5 m/s from the cloth under a bar lamp (0.84 m, footprint over the table): apex z_max = R + 1.0325 m above
// the lamp -> BallExternalContact(Lamp) + BallOffTable(ExternalObjectRebound) at the apex.
RB_TEST(COL_O2_LampApexOffTable)
{
	const TableGeometry& T = Table(kTableSevenFootBar);
	const PhysicsParams P = ColParams();
	SimInput& In = NewInput(T, P);
	In.Environment.LampUndersideZ = 0.84;
	In.Environment.LampFootprint = Aabb2{{-2.0, -2.0}, {2.0, 2.0}};
	Place(In, 0, {0.2, 0.1, kR}, {0.0, 0.0, 4.5});
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	const ShotEvent* Lamp = First(R, ShotEventType::BallExternalContact, 0);
	const ShotEvent* Off = First(R, ShotEventType::BallOffTable, 0);
	RB_REQUIRE(Lamp != nullptr && Off != nullptr);
	RB_CHECK(Lamp->Feature == static_cast<std::uint8_t>(ExternalObject::Lamp));
	RB_CHECK(Off->Feature == static_cast<std::uint8_t>(OffTableReason::ExternalObjectRebound));
	RB_CHECK(Lamp < Off); // contact first, then off the table
	RB_CHECK_NEAR(Lamp->Time, 4.5 / P.Gravity, 1e-12);
	const BallFinal& F = R.Finals[0];
	RB_CHECK(F.Status == BallFinalStatus::OffTable && F.OffReason == OffTableReason::ExternalObjectRebound);
	RB_CHECK_NEAR(F.State.Position.z - kR, 1.0325, 1e-4);
	RB_CHECK_NEAR(F.State.Position.z, kR + 4.5 * 4.5 / (2.0 * P.Gravity), 1e-12);

	// A lamp out of reach: the ball lands (WP-6b routing) instead; no lamp event at all.
	In.Environment.LampUndersideZ = 1.65;
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_CHECK(Count(R, ShotEventType::BallExternalContact) == 0);
}

// VAL ROB-04: a ball placed exactly touching a cushion (and 1e-12 m off / into it) and shot into it at 1 m/s gets an immediate
// cushion event, rebounds, and never tunnels.
RB_TEST(VAL_ROB04_TouchingCushionImmediateEvent)
{
	const TableGeometry& T = NineFoot();
	const double Offsets[3] = {0.0, -1e-12, 1e-12}; // exactly touching, 1e-12 m away, 1e-12 m into the nose
	for (double Offset : Offsets)
	{
		SimInput& In = NewInput(T, ValParams());
		const double Y = LeftRailContactY(T) - Offset;
		PlaceRolling(In, 0, {-0.6, Y, kR}, {0.0, 1.0, 0.0});
		Simulator Sim;
		ShotResult& R = ResultSlot();
		RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
		const ShotEvent* Hit = First(R, ShotEventType::BallCushion, 0);
		RB_REQUIRE(Hit != nullptr);
		RB_CHECK(Hit->Time <= 1e-12);
		RB_CHECK(Hit->Feature == static_cast<std::uint8_t>(CushionId::LeftHead));
		RB_CHECK(Hit->Post[0].Velocity.y < -0.5); // rebounds
		RB_CHECK(R.Diagnostics.OverlapWarnings == 0);
		for (const ShotEvent& E : R.Events)
		{
			if (E.A == 0)
			{
				RB_CHECK(NoseGap(T, static_cast<int>(CushionId::LeftHead), E.Pre[0].Position, kR) >= -1e-9); // no tunnelling
			}
		}
		RB_CHECK(R.Finals[0].State.Position.y < Y);
		RB_CHECK(R.Finals[0].Status == BallFinalStatus::OnTable);
	}
}

// VAL ROB-05: rolling head-on into a rail at 0.05 m/s with natural topspin: a finite number of cushion events (<= 50) and the ball
// ends at rest. The resting contact ("restingOnCushion") is the COL mechanism (architecture section 15 row 12): an approach below
// v_rest is an e = 0 micro-impact flagged Resting, and the topspin that then presses the ball into the rail is a pressing contact for
// an island (never an impulse). Mathavan 2010 reverses enough of the topspin at 0.05 m/s that the ball rolls back and stops a few mm
// from the rail (no creep back into it); the creep regime of prior-art 5.2 appears below v_rest, checked in the second part.
RB_TEST(VAL_ROB05_SlowRollIntoRailFiniteEvents)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ValParams());
	const double Y = LeftRailContactY(T);
	PlaceRolling(In, 0, {-0.6, Y, kR}, {0.0, 0.05, 0.0}); // touching the nose, rolling into it at 0.05 m/s
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	const int Hits = Count(R, ShotEventType::BallCushion, 0);
	RB_CHECK(Hits >= 1 && Hits <= 50);
	RB_CHECK(R.Diagnostics.EventsProcessed < 200);
	const BallFinal& F = R.Finals[0];
	RB_CHECK(F.Status == BallFinalStatus::OnTable && F.State.State == MotionState::Stationary);
	const double Gap = NoseGap(T, static_cast<int>(CushionId::LeftHead), F.State.Position, kR);
	RB_CHECK(Gap >= 0.0 && Gap < 0.01);
	RB_CHECK(R.Diagnostics.OverlapWarnings == 0);

	// Below v_rest: the e = 0 micro-impact (Resting flag); the topspin then presses the ball into the rail: a pressing contact
	// handed to an island at the same place.
	SimInput& Slow = NewInput(T, ValParams());
	PlaceRolling(Slow, 0, {-0.6, Y, kR}, {0.0, 1.5e-3, 0.0});
	RB_REQUIRE(Sim.Run(Slow, R) == SimStatus::Ok);
	const ShotEvent* Rest = First(R, ShotEventType::BallCushion, 0);
	RB_REQUIRE(Rest != nullptr);
	RB_CHECK((Rest->Flags & ShotEventFlags::Resting) != 0);
	RB_CHECK(Rest->Post[0].Velocity.y == 0.0);
	RB_CHECK(R.Diagnostics.PressingContacts >= 1 && R.Diagnostics.IslandHandOffs >= 1);
}

// VAL ROB-05 (rest part): the resting contact ends at rest touching the rail (the pressing island of WP-6b runs until the ball
// rests).
RB_TEST(Integ_VAL_ROB05_RestingContactEndsAtRail)
{
	const TableGeometry& T = NineFoot();
	SimInput& In = NewInput(T, ValParams());
	PlaceRolling(In, 0, {-0.6, LeftRailContactY(T), kR}, {0.0, 1.5e-3, 0.0});
	Simulator Sim;
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(In, R) == SimStatus::Ok);
	RB_CHECK(Count(R, ShotEventType::BallCushion, 0) <= 50);
	const BallFinal& F = R.Finals[0];
	RB_CHECK(F.Status == BallFinalStatus::OnTable && F.State.State == MotionState::Stationary);
	RB_CHECK(std::fabs(NoseGap(T, static_cast<int>(CushionId::LeftHead), F.State.Position, kR)) <= 1e-6); // at rest touching the rail
	RB_CHECK(R.Diagnostics.OverlapWarnings == 0);
}

// VAL ROB-06: mu_s = 0, mu_r = 0, alpha_sp = 0 and e = 1.2 are rejected by the API (no hang, no exception).
RB_TEST(VAL_ROB06_DegenerateParametersRejected)
{
	const PhysicsParams Good = ValParams();
	RB_REQUIRE(ValidatePhysicsParams(Good) == ErrorCode::Ok);
	PhysicsParams Bad[10];
	for (PhysicsParams& P : Bad)
	{
		P = Good;
	}
	Bad[0].Cloth.SlidingFriction = 0.0;
	Bad[1].Cloth.RollingResistance = 0.0;
	Bad[2].Cloth.SpinDeceleration = 0.0;
	Bad[3].BallBall.Restitution = 1.2;
	Bad[4].Slate.Restitution = 1.2;
	Bad[5].Cushion.Restitution.Max = 1.2;
	Bad[6].Cloth.SlidingFriction = std::nan("");
	Bad[7].Gravity = -9.81;
	Bad[8].PocketContacts.LinerRestitution = 1.2;
	Bad[9].Cloth.RollingResistance = -0.01;
	const TableGeometry& T = NineFoot();
	Simulator Sim;
	ShotResult& R = ResultSlot();
	for (const PhysicsParams& P : Bad)
	{
		RB_CHECK(ValidatePhysicsParams(P) == ErrorCode::InvalidParameter);
		SimInput& In = NewInput(T, P);
		PlaceRolling(In, 0, {0.0, 0.0, kR}, {1.0, 0.0, 0.0});
		RB_CHECK(Sim.Run(In, R) == SimStatus::InvalidInput);
		RB_CHECK(R.Diagnostics.InputError == ErrorCode::InvalidParameter);
		RB_CHECK(R.Events.empty() && R.Diagnostics.EventsProcessed == 0);
	}
}

// VAL ROB-15: an artificial Zeno setup with the guards disabled (no micro-impact rule, no Zeno detector, no pressing tolerance, no
// graze band): a cue ball with pure topspin 1 um behind an object ball drives into it again and again, each approach e_b times the
// last (a geometric chain accumulating in finite time, collisions 7.3). It hits the event cap and returns cleanly: Aborted, every
// ball stopped, the record truncated, nothing logged after the stop. With the default guards the Zeno detector ends the chain.
RB_TEST(VAL_ROB15_EventCapTruncatesZenoChase)
{
	const TableGeometry& T = NineFoot();
	PhysicsParams Guarded = ValParams();
	PhysicsParams P = Guarded;
	P.Numerics.RestSpeed = 0.0;             // guard 3 off
	P.Numerics.ZenoContactCount = 1000;     // guard 5 off (beyond the history)
	P.Numerics.ApproachSpeedTol = 0.0;      // pressing rule only at exactly zero speed
	P.Numerics.TangencyTolPerLength = 0.0;  // no graze band
	const auto Setup = [&](const PhysicsParams& Params, int Cap) -> SimInput&
	{
		PhysicsParams Capped = Params;
		Capped.Numerics.MaxEvents = Cap;
		SimInput& In = NewInput(T, Capped);
		Place(In, 0, {-0.8, 0.0, kR}, {}, {0.0, 10.0, 0.0}); // pure topspin: slides forward at mu_s g
		Place(In, 1, {-0.8 + 2.0 * kR + 1e-6, 0.0, kR});
		return In;
	};
	Simulator Sim;
	ShotResult& Free = ResultSlot(1);
	RB_REQUIRE(Sim.Run(Setup(P, 100000), Free) == SimStatus::Ok);
	int Contacts = 0;
	double Previous = 0.0;
	bool Geometric = true;
	for (const ShotEvent& E : Free.Events)
	{
		if (E.Type == ShotEventType::BallBall && E.Time < 0.01)
		{
			Geometric = Geometric && (Contacts == 0 || Contacts >= 20 || E.NormalSpeed < Previous); // (rounding at the 1e-8 m/s tail)
			Previous = E.NormalSpeed;
			++Contacts;
		}
	}
	RB_CHECK(Contacts >= 20 && Geometric); // a chattering chain within 10 ms, each approach slower than the last
	const int Cap = Free.Diagnostics.EventsProcessed / 2;
	RB_REQUIRE(Cap >= 10);
	ShotResult& R = ResultSlot();
	RB_REQUIRE(Sim.Run(Setup(P, Cap), R) == SimStatus::Aborted);
	RB_CHECK(R.Diagnostics.EventsProcessed == Cap);
	RB_CHECK(R.Record.Truncated);
	for (int b = 0; b <= 1; ++b)
	{
		RB_CHECK(R.Finals[b].State.State == MotionState::Stationary && R.Finals[b].State.Velocity == Vec3{});
		RB_CHECK(R.Finals[b].Time == R.StopTime);
	}
	for (const ShotEvent& E : R.Events)
	{
		RB_CHECK(E.Time <= R.StopTime);
	}

	// The Zeno detector alone back on (its defaults: 8 contacts of the pair within 10 ms): the 8th contact goes to an island.
	// (Integration round 2: with the real WP-6b island and guard 3 still off, the rigid island's contacts bounce (e > 0 at every
	// speed), the island exits and the topspin chain starts again, so the detector fires once per chain - never an impulse on
	// the 8th contact, and ZenoGuard logged exactly once per trigger. The stub island of WP-6a swallowed the balls after the first.)
	Guarded = P;
	Guarded.Numerics.ZenoContactCount = NumericsConfig{}.ZenoContactCount;
	ShotResult& G = ResultSlot(2);
	RB_REQUIRE(Sim.Run(Setup(Guarded, 20000), G) == SimStatus::Ok);
	RB_CHECK(G.Diagnostics.ZenoTriggers >= 1);
	RB_CHECK(Count(G, ShotEventType::ZenoGuard) == G.Diagnostics.ZenoTriggers);
	// Every guard at its default (guard 3 back on): the micro-impact rule and the rigid island end the chain after one trigger.
	ShotResult& D = ResultSlot(3);
	RB_REQUIRE(Sim.Run(Setup(ValParams(), 20000), D) == SimStatus::Ok);
	RB_CHECK(D.Diagnostics.ZenoTriggers <= 1);
	RB_CHECK(Count(D, ShotEventType::ZenoGuard) == D.Diagnostics.ZenoTriggers);
	const ShotEvent* Zeno = First(G, ShotEventType::ZenoGuard);
	RB_REQUIRE(Zeno != nullptr);
	int Before = 0;
	for (const ShotEvent& E : G.Events)
	{
		Before += E.Type == ShotEventType::BallBall && E.Time < Zeno->Time ? 1 : 0;
	}
	RB_CHECK(Before == Guarded.Numerics.ZenoContactCount - 1); // the 8th contact went to the island, not to an impulse
	RB_CHECK(G.Diagnostics.IslandHandOffs >= 1);
}
