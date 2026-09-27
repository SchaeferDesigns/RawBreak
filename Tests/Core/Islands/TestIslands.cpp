// WP-6b: island spec tests through Simulator::Run (physics-collisions D-12b, Z-3; prior-art ROB-01, ROB-02, ROB-03, ROB-09,
// BRK-01; architecture A-ISL-1 ... A-ISL-4). They need the WP-6a loop (Integ_).
#include "rbtest.h"

#include "IslandTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Physics/Compliant.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

using namespace isltest;

namespace
{
	// R_c for the pool ball against the 9FT_PRO nose (collisions 4.1).
	double NoseOffset(const rb::TableGeometry& T) { return rb::ComputeCushionContact(kR, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset; }

	// D-12 set-up: CB at the origin with pure topspin w = (0, 10, 0) rad/s (sliding, A = +1/2 mu_s g x_hat) frozen to an OB at rest.
	std::unique_ptr<Scene> PressingPair(double Topspin = 10.0)
	{
		std::unique_ptr<Scene> S = MakeScene();
		Place(*S, 0, {0.0, 0.0, kR}, {}, {0.0, Topspin, 0.0});
		Place(*S, 1, {2.0 * kR, 0.0, kR});
		return S;
	}

	// Z-3 set-up: a ball touching RAIL_LEFT (C3, y = +W/2) at the contact distance R_c, rolling slowly along the rail (toward the
	// side pocket, stopping well before it) with a spin whose slip points away from the rail, so that cloth friction presses it
	// into the cushion (u_hat with an inward component).
	std::unique_ptr<Scene> SpinPressedIntoRail()
	{
		std::unique_ptr<Scene> S = MakeScene();
		const double Y = S->Table.HalfWidth - NoseOffset(S->Table);
		Place(*S, 0, {0.9, Y, kR}, {-0.3, 0.0, 0.0}, {-10.0, -0.3 / kR, 0.0});
		return S;
	}

	// Island intervals [IslandBegin, IslandEnd] of a result.
	std::vector<std::pair<double, double>> IslandIntervals(const rb::ShotResult& R)
	{
		std::vector<std::pair<double, double>> Out;
		double Begin = -1.0;
		for (const rb::ShotEvent& E : R.Events)
		{
			if (E.Type == rb::ShotEventType::IslandBegin)
			{
				Begin = E.Time;
			}
			else if (E.Type == rb::ShotEventType::IslandEnd && Begin >= 0.0)
			{
				Out.push_back({Begin, E.Time});
				Begin = -1.0;
			}
		}
		return Out;
	}

	bool InsideAnyIsland(const std::vector<std::pair<double, double>>& Intervals, double T)
	{
		for (const std::pair<double, double>& I : Intervals)
		{
			if (T > I.first && T < I.second)
			{
				return true;
			}
		}
		return false;
	}

	double MomentumX(const rb::ShotResult& R, double T, std::uint32_t Balls)
	{
		double P = 0.0;
		for (int b = 0; b < rb::kMaxBalls; ++b)
		{
			if (((Balls >> b) & 1u) == 0)
			{
				continue;
			}
			// Velocity of the segment that contains T (Analytic only: the island boundaries are Analytic on both sides).
			for (const rb::TrajectorySegment& Seg : R.Tracks[b].Segments)
			{
				if (Seg.Kind == rb::SegmentKind::Analytic && T >= Seg.Motion.T0 && T <= Seg.T1)
				{
					P += kM * rb::EvaluateSegment(Seg.Motion, T - Seg.Motion.T0).Velocity.x;
					break;
				}
			}
		}
		return P;
	}
}

RB_TEST(Integ_COL_D12b_PressingPairIsland)
{
	// Pressing contact (f(0) = 0, f'(0) = 0, f''(0) < 0): routed to a 2-ball CLI island; no interpenetration beyond the Hertz
	// overlap; the pair ends rolling forward together, then separates.
	std::unique_ptr<Scene> S = PressingPair();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(R.Diagnostics.Islands >= 1);
	RB_CHECK(R.Diagnostics.IslandRigidSwitches >= 1); // the sustained push ends the Hertz transient (architecture 8.8)
	RB_CHECK(!R.Diagnostics.IslandBudgetExceeded);
	const rb::ShotEvent* Begin = First(R, rb::ShotEventType::IslandBegin);
	RB_REQUIRE(Begin != nullptr);
	RB_CHECK(Begin->Time == 0.0);
	RB_CHECK((Begin->Flags & rb::ShotEventFlags::Pressing) != 0);
	RB_CHECK(Count(R, rb::ShotEventType::IslandEnd) >= 1);
	// One first-touch record for the sustained contact, flagged as coming from the island.
	int Records = 0;
	for (const rb::ShotEvent& E : R.Events)
	{
		if (E.Type == rb::ShotEventType::BallBall && (E.Flags & rb::ShotEventFlags::FromIsland) != 0)
		{
			++Records;
		}
	}
	RB_CHECK(Records == 1);
	// No interpenetration beyond the Hertz static overlap (~0.1 um) anywhere on the recorded tracks.
	const std::vector<double> Times = TrackTimes(R, 3u, R.StopTime);
	RB_CHECK(MinPairGap(R, 0, 1, kR, kR, Times) > -1e-6);
	// Both end on the table, the OB ahead of the CB and moved forward.
	RB_CHECK(R.Finals[0].Status == rb::BallFinalStatus::OnTable && R.Finals[1].Status == rb::BallFinalStatus::OnTable);
	RB_CHECK(R.Finals[1].State.Position.x > 2.0 * kR + 1e-3);
	RB_CHECK(R.Finals[1].State.Position.x - R.Finals[0].State.Position.x >= 2.0 * kR - 1e-9);
	RB_CHECK(R.Diagnostics.OverlapWarnings == 0);
}

RB_TEST(Integ_COL_Z3_SpinPressedIntoCushion)
{
	// A sliding ball whose slip acceleration presses it into a cushion: < 12 events before the pressing rule / Zeno island takes
	// over, the center never closer to the nose line than R_c - 1 um, the ball ends rolling along the rail or leaves it, < 100
	// events in total.
	std::unique_ptr<Scene> S = SpinPressedIntoRail();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(R.Diagnostics.Islands >= 1);
	RB_CHECK(R.Diagnostics.EventsProcessed < 100);
	const rb::ShotEvent* Begin = First(R, rb::ShotEventType::IslandBegin);
	RB_REQUIRE(Begin != nullptr);
	int Before = 0;
	for (const rb::ShotEvent& E : R.Events)
	{
		if (&E == Begin)
		{
			break;
		}
		Before += (E.Type == rb::ShotEventType::BallCushion) ? 1 : 0;
	}
	RB_CHECK(Before < 12);
	const double Limit = S->Table.HalfWidth - NoseOffset(S->Table);
	double MaxY = -rb::kInfinity;
	for (double T : TrackTimes(R, 1u, R.StopTime))
	{
		Vec3 P;
		if (TrackPosition(R, 0, T, P))
		{
			MaxY = rb::Max(MaxY, P.y);
		}
	}
	std::printf("  Z-3: %d events, closest approach to the nose contact line %.3g m\n", R.Diagnostics.EventsProcessed, MaxY - Limit);
	RB_CHECK(MaxY <= Limit + 1e-6);
	// It ends on the table, having rolled along the rail (touching it) rather than bouncing off.
	RB_CHECK(R.Finals[0].Status == rb::BallFinalStatus::OnTable);
	RB_CHECK(R.Finals[0].State.Position.x < 0.9 - 0.1);
	RB_CHECK(R.Finals[0].State.Position.y > Limit - 1e-3);
}

RB_TEST(Integ_VAL_ROB01_NewtonsCradle)
{
	// 5 balls frozen along x (gap 0), CB 2 m/s along the line: < 1000 events, momentum conserved (rel 1e-9), no overlap, the last
	// ball carries the largest speed. The momentum check needs an impact without external impulses: on the cloth, the cloth
	// friction of the ~1 ms island (with the extra normal load of the ball-ball friction's vertical components) changes the
	// momentum by ~1e-2 relative, so it is checked on the same cradle with frictionless balls and a negligible cloth friction
	// (the collision itself); the realistic run checks termination, overlaps and the speed ordering.
	for (int Ideal = 0; Ideal < 2; ++Ideal)
	{
		std::unique_ptr<Scene> S = MakeScene(rb::kTableNineFootPro, kGVal);
		RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
		if (Ideal != 0)
		{
			S->Input.Params.BallBall.Friction = rb::BallBallFrictionModel::None;
			S->Input.Params.Cloth = rb::ClothParams{1e-12, 1e-12, 10.0};
			S->Input.Params.Numerics.TimeHorizon = 0.3; // the balls slide on for ages: stop after the impact
		}
		const double X0 = -0.3;
		Place(*S, 0, {X0 - 0.2, 0.0, kR}, {2.0, 0.0, 0.0});
		for (int k = 1; k <= 5; ++k)
		{
			Place(*S, k, {X0 + 2.0 * kR * (k - 1), 0.0, kR});
		}
		const rb::SimStatus Status = Run(*S);
		const rb::ShotResult& R = S->Result;
		RB_CHECK(Status == (Ideal != 0 ? rb::SimStatus::HorizonReached : rb::SimStatus::Ok));
		RB_CHECK(R.Diagnostics.EventsProcessed < 1000);
		RB_CHECK(R.Diagnostics.Islands >= 1);
		const rb::ShotEvent* Begin = First(R, rb::ShotEventType::IslandBegin);
		const rb::ShotEvent* End = First(R, rb::ShotEventType::IslandEnd);
		RB_REQUIRE(Begin != nullptr && End != nullptr);
		const std::uint32_t All = 0x3Fu;
		const double PBefore = MomentumX(R, Begin->Time, All);
		const double PAfter = MomentumX(R, End->Time, All);
		std::printf("  ROB-01 (%s): island %.3f ms, momentum %.12f -> %.12f\n", Ideal != 0 ? "ideal" : "cloth", (End->Time - Begin->Time) * 1e3, PBefore, PAfter);
		if (Ideal != 0)
		{
			RB_CHECK(rb::Abs(PAfter - PBefore) <= 1e-9 * rb::Abs(PBefore));
			continue;
		}
	// The last ball is the fastest right after the island.
	double Fastest = 0.0;
	int FastestBall = -1;
	for (int b = 0; b <= 5; ++b)
	{
		const double V = rb::Abs(MomentumX(R, End->Time, 1u << b)) / kM;
		if (V > Fastest)
		{
			Fastest = V;
			FastestBall = b;
		}
	}
	RB_CHECK(FastestBall == 5);
	// No overlap between event-mode segments (ROB-11: at every event boundary outside the islands, where the Hertz compression of
	// a contact is physical) and at the end.
	const std::vector<std::pair<double, double>> Islands = IslandIntervals(R);
	for (int b = 0; b < 5; ++b)
	{
		const std::vector<double> Times = TrackTimes(R, (1u << b) | (1u << (b + 1)), R.StopTime);
		std::vector<double> Outside;
		for (double T : Times)
		{
			if (!InsideAnyIsland(Islands, T))
			{
				Outside.push_back(T);
			}
		}
		RB_CHECK(MinPairGap(R, b, b + 1, kR, kR, Outside) >= -1e-9);
	}
	}
}

RB_TEST(Integ_VAL_ROB02_ZeroGapRackBreak)
{
	// 15-ball rack with exactly zero gaps, break at 13 m/s: terminates in < 10 000 events (the time budget is the _Slow_ variant).
	std::unique_ptr<Scene> S = MakeScene(rb::kTableNineFootPro, kGVal);
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	rb::RackSite Sites[rb::kMaxRackSites];
	const double D = 2.0 * kR;
	const int N = rb::BuildRackLattice(rb::RackShape::Triangle15, rb::RackApexX(rb::RackShape::Triangle15, rb::RackAnchor::ApexOnFootSpot,
		S->Table.Landmarks.FootSpot.x, D), D, Sites);
	RB_REQUIRE(N == 15);
	for (int k = 0; k < N; ++k)
	{
		Place(*S, k + 1, rb::ToVec3(Sites[k].Position, kR));
	}
	Place(*S, 0, {S->Table.Landmarks.HeadStringX, 0.0, kR}, {13.0, 0.0, 0.0});
	const rb::SimStatus Status = Run(*S);
	RB_CHECK(Status == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(R.Diagnostics.EventsProcessed < 10000);
	RB_CHECK(!R.Diagnostics.IslandBudgetExceeded);
	std::printf("  ROB-02: %d events, %d islands, %d island steps, stop %.2f s\n", R.Diagnostics.EventsProcessed, R.Diagnostics.Islands,
		R.Diagnostics.IslandSteps, R.StopTime);
	for (int b = 0; b <= 15; ++b)
	{
		RB_CHECK(rb::IsFinite(R.Finals[b].State.Position.x) && rb::IsFinite(R.Finals[b].State.Position.y));
	}
}

RB_TEST(Integ_VAL_ROB02_Slow_ZeroGapRackBreakTime)
{
	// ROB-02 time budget: within the P2 budget x 5 (2 ms x 5 = 10 ms) per break (Release).
	double Worst = 0.0;
	for (int Repeat = 0; Repeat < 5; ++Repeat)
	{
		std::unique_ptr<Scene> S = MakeScene(rb::kTableNineFootPro, kGVal);
		rb::RackSite Sites[rb::kMaxRackSites];
		const double D = 2.0 * kR;
		const int N = rb::BuildRackLattice(rb::RackShape::Triangle15, rb::RackApexX(rb::RackShape::Triangle15, rb::RackAnchor::ApexOnFootSpot,
			S->Table.Landmarks.FootSpot.x, D), D, Sites);
		for (int k = 0; k < N; ++k)
		{
			Place(*S, k + 1, rb::ToVec3(Sites[k].Position, kR));
		}
		Place(*S, 0, {S->Table.Landmarks.HeadStringX, 0.0, kR}, {13.0, 0.0, 0.0});
		S->Input.Record.Trajectories = false;
		S->Input.Record.EventStates = false;
		rb::Simulator Sim;
		const auto Start = std::chrono::steady_clock::now();
		Sim.Run(S->Input, S->Result);
		const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
		Worst = rb::Max(Worst, Ms);
		RB_CHECK(S->Result.Status == rb::SimStatus::Ok); // the break really ran
	}
	std::printf("  ROB-02 break: worst %.3f ms\n", Worst);
#ifdef NDEBUG
	RB_CHECK(Worst < 10.0); // a Release budget: Debug builds only print it
#endif
}

RB_TEST(Integ_VAL_ROB03_PocketlessCornerNeverEscapes)
{
	// Pocketless 4.5 x 4.5 m square (pooltool #217): a ball 1 m from both rails shot at 45 deg into the corner at 2 m/s, plus
	// random sub-um offsets: it never leaves the table and both cushions are hit, in either order.
	rb::TableSpec Spec = rb::kTableNineFootPro;
	Spec.Preset = rb::TablePreset::Custom;
	Spec.Length = 4.5;
	Spec.Width = 4.5;
	Spec.HasPockets = false;
	rb::Rng Random(0x6B03u);
	const int Runs = 1001;
	int Failures = 0;
	for (int Run1 = 0; Run1 < Runs; ++Run1)
	{
		std::unique_ptr<Scene> S = MakeScene(Spec, kGVal);
		RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
		const double Dx = Run1 == 0 ? 0.0 : Random.NextUniform(-1e-6, 1e-6);
		const double Dy = Run1 == 0 ? 0.0 : Random.NextUniform(-1e-6, 1e-6);
		const double V = 2.0 / rb::Sqrt(2.0);
		Place(*S, 0, {S->Table.HalfLength - 1.0 + Dx, S->Table.HalfWidth - 1.0 + Dy, kR}, {V, V, 0.0}, {-V / kR, V / kR, 0.0});
		S->Input.Record.Trajectories = false;
		rb::Simulator Sim;
		if (Sim.Run(S->Input, S->Result) != rb::SimStatus::Ok)
		{
			++Failures;
			continue;
		}
		const rb::ShotResult& R = S->Result;
		bool Foot = false;
		bool Left = false;
		for (const rb::ShotEvent& E : R.Events)
		{
			if (E.Type == rb::ShotEventType::BallCushion)
			{
				Foot = Foot || E.Feature == static_cast<std::uint8_t>(rb::CushionId::Foot);
				Left = Left || E.Feature == static_cast<std::uint8_t>(rb::CushionId::LeftFoot);
			}
		}
		const rb::Vec3 P = R.Finals[0].State.Position;
		const bool Inside = R.Finals[0].Status == rb::BallFinalStatus::OnTable && rb::Abs(P.x) < S->Table.HalfLength && rb::Abs(P.y) < S->Table.HalfWidth;
		if (!Foot || !Left || !Inside || Count(R, rb::ShotEventType::BallOffTable) != 0)
		{
			++Failures;
		}
	}
	RB_CHECK(Failures == 0);
}

RB_TEST(Integ_VAL_ROB09_SymmetricDoubleHit)
{
	// CB straight at the contact point of two frozen OBs side by side (y-symmetric): deterministic, and with the cluster solver
	// the outcome is y-symmetric within 1e-9 m/s.
	auto Make = []()
	{
		std::unique_ptr<Scene> S = MakeScene(rb::kTableNineFootPro, kGVal);
		Place(*S, 1, {0.3, kR, kR});
		Place(*S, 2, {0.3, -kR, kR});
		Place(*S, 0, {-0.2, 0.0, kR}, {1.5, 0.0, 0.0});
		return S;
	};
	std::unique_ptr<Scene> A = Make();
	std::unique_ptr<Scene> B = Make();
	RB_REQUIRE(Run(*A) == rb::SimStatus::Ok);
	RB_REQUIRE(Run(*B) == rb::SimStatus::Ok);
	RB_CHECK(A->Result.Diagnostics.Islands >= 1);
	const rb::ShotEvent* End = First(A->Result, rb::ShotEventType::IslandEnd);
	RB_REQUIRE(End != nullptr);
	// Right after the island: mirror symmetric velocities.
	rb::BallState S1;
	rb::BallState S2;
	rb::BallState S0;
	for (int b = 0; b < 3; ++b)
	{
		for (const rb::TrajectorySegment& Seg : A->Result.Tracks[b].Segments)
		{
			if (Seg.Kind == rb::SegmentKind::Analytic && Seg.Motion.T0 == End->Time)
			{
				(b == 0 ? S0 : (b == 1 ? S1 : S2)) = rb::EvaluateSegment(Seg.Motion, 0.0);
			}
		}
	}
	RB_CHECK_NEAR(S1.Velocity.x, S2.Velocity.x, 1e-9);
	RB_CHECK_NEAR(S1.Velocity.y, -S2.Velocity.y, 1e-9);
	RB_CHECK(S1.Velocity.y > 0.0);
	RB_CHECK_NEAR(S0.Velocity.y, 0.0, 1e-9);
	RB_CHECK_NEAR(A->Result.Finals[1].State.Position.y, -A->Result.Finals[2].State.Position.y, 1e-9);
	RB_CHECK_NEAR(A->Result.Finals[1].State.Position.x, A->Result.Finals[2].State.Position.x, 1e-9);
	// Deterministic: bitwise identical results.
	RB_CHECK(A->Result.Events.size() == B->Result.Events.size());
	for (int b = 0; b < 3; ++b)
	{
		RB_CHECK(std::memcmp(&A->Result.Finals[b].State.Position, &B->Result.Finals[b].State.Position, sizeof(rb::Vec3)) == 0);
	}
}

RB_TEST(Integ_VAL_BRK01_NineBallBreak)
{
	// 9-ball rack with the B2 gap generator (prior-art 5.6 mixture), CB 10.7 m/s square stun hit on the 1-ball: terminates, no NaN,
	// <= 5000 events, no overlaps at event boundaries (ROB-11), mechanical energy non-increasing between event-mode instants after
	// the stick event.
	std::unique_ptr<Scene> S = MakeScene(rb::kTableNineFootPro, kGVal);
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	rb::RackSite Sites[rb::kMaxRackSites];
	const double D = 2.0 * kR;
	const rb::RackShape Shape = rb::RackShape::Diamond9;
	const rb::RackAnchor Anchor = rb::RackAnchor::CenterOnFootSpot;
	const int N = rb::BuildRackLattice(Shape, rb::RackApexX(Shape, Anchor, S->Table.Landmarks.FootSpot.x, D), D, Sites);
	RB_REQUIRE(N == 9);
	Vec2 Positions[9];
	for (int k = 0; k < N; ++k)
	{
		Positions[k] = Sites[k].Position;
	}
	rb::ApplyRackGaps(Positions, N, rb::RackAnchorSiteIndex(Shape, Anchor), D, rb::kRackGapMixture, 0xB2u);
	for (int k = 0; k < N; ++k)
	{
		Place(*S, k + 1, rb::ToVec3(Positions[k], kR));
	}
	Place(*S, 0, {S->Table.Landmarks.HeadStringX, Positions[0].y, kR}, {10.7, 0.0, 0.0});
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(R.Diagnostics.EventsProcessed <= 5000);
	RB_CHECK(!R.Diagnostics.IslandBudgetExceeded);
	RB_CHECK(R.Diagnostics.MissedEvents == 0);
	for (int b = 0; b <= 9; ++b)
	{
		const rb::BallState& F = R.Finals[b].State;
		RB_CHECK(rb::IsFinite(F.Position.x) && rb::IsFinite(F.Position.y) && rb::IsFinite(F.Position.z));
	}
	// Overlaps and energy at the event-mode instants (starts of Analytic segments), after the stick event.
	std::vector<double> Times;
	for (int b = 0; b <= 9; ++b)
	{
		for (const rb::TrajectorySegment& Seg : R.Tracks[b].Segments)
		{
			if (Seg.Kind == rb::SegmentKind::Analytic && Seg.Motion.T0 > 0.0)
			{
				Times.push_back(Seg.Motion.T0);
			}
		}
	}
	double MinGap = rb::kInfinity;
	for (double T : Times)
	{
		bool InIsland = false;
		double Start = -1.0;
		for (const rb::ShotEvent& E : R.Events)
		{
			if (E.Time > T)
			{
				break;
			}
			if (E.Type == rb::ShotEventType::IslandBegin)
			{
				Start = E.Time;
				InIsland = true;
			}
			if (E.Type == rb::ShotEventType::IslandEnd && E.Time < T)
			{
				InIsland = false;
			}
		}
		if (InIsland && Start < T)
		{
			continue; // Hertz compression inside islands is physical
		}
		for (int a = 0; a <= 9; ++a)
		{
			for (int b = a + 1; b <= 9; ++b)
			{
				Vec3 PA;
				Vec3 PB;
				if (R.Finals[a].Status == rb::BallFinalStatus::OnTable && R.Finals[b].Status == rb::BallFinalStatus::OnTable && TrackPosition(R, a, T, PA) &&
					TrackPosition(R, b, T, PB))
				{
					MinGap = rb::Min(MinGap, rb::Length(PB - PA) - D);
				}
			}
		}
	}
	RB_CHECK(MinGap >= -1e-9);
	std::printf("  BRK-01: %d events, %d islands, %d island steps, min event-mode gap %.3g m, %d pocketed\n", R.Diagnostics.EventsProcessed,
		R.Diagnostics.Islands, R.Diagnostics.IslandSteps, MinGap, Count(R, rb::ShotEventType::BallPocketed));
}

RB_TEST(Integ_ARCH_ISL1_FrozenRailNoTunnel)
{
	// CB at 5 m/s into an OB frozen to the rail 5 cm from the corner jaw: the OB is squeezed along the rail into the jaw /
	// facing; it never tunnels through them (its center stays on the table side of every nose, jaw and facing contact
	// distance while it is on the cloth).
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::TableGeometry& T = S->Table;
	const double Rc = NoseOffset(T);
	// FOOT_LEFT (P3) outgoing jaw starts C3 (y = +W/2); the OB 5 cm before its nose tangent point.
	const rb::JawArc& Jaw = T.JawArcs[2 * kFootLeft + static_cast<int>(rb::JawSide::Outgoing)];
	const double XOb = Jaw.TangentOnNose.x - 0.05;
	const double YOb = T.HalfWidth - Rc;
	Place(*S, 1, {XOb, YOb, kR});
	const Vec3 Aim = rb::Normalized(Vec3{1.0, 0.35, 0.0});
	const Vec3 Cb = Vec3{XOb, YOb, kR} - Aim * (2.0 * kR + 0.25);
	Place(*S, 0, Cb, Aim * 5.0);
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(R.Diagnostics.Islands >= 1);
	// While on the cloth: never beyond the nose contact line, and never closer to the jaw arc center than r_j + R_c.
	double Worst = rb::kInfinity;
	for (double Time : TrackTimes(R, 2u, R.StopTime))
	{
		Vec3 P;
		if (!TrackPosition(R, 1, Time, P) || P.z > kR + 1e-6 || P.z < kR - 1e-6)
		{
			continue;
		}
		if (P.x <= Jaw.TangentOnNose.x)
		{
			Worst = rb::Min(Worst, (T.HalfWidth - Rc) - P.y);
		}
		const double JawGap = rb::Length(XY(P) - Jaw.Center) - (Jaw.Radius + Rc);
		const double Angle = rb::Atan2(P.y - Jaw.Center.y, P.x - Jaw.Center.x) - Jaw.AngleFrom;
		const double Rel = Angle - rb::kTwoPi * rb::Floor(Angle / rb::kTwoPi);
		if (Rel <= Jaw.AngleSweep)
		{
			Worst = rb::Min(Worst, JawGap);
		}
	}
	std::printf("  A-ISL-1: deepest cushion / jaw compression %.3g m, %d feature joins, OB final %s\n", Worst, R.Diagnostics.FeatureJoins,
		R.Finals[1].Status == rb::BallFinalStatus::Pocketed ? "pocketed" : "on table");
	// Inside the island the cushion and the jaw are compliant (k_c = 1e6 N/m): they deflect by at most v sqrt(m / k_c) (2.1 mm at
	// 5 m/s), and never let the ball through (tunnelling would put the center beyond the nose / jaw by ~R).
	const double Deflection = 5.0 * rb::Sqrt(kM / S->Input.Params.Cli.CushionStiffness);
	RB_CHECK(Worst > -Deflection);
	const rb::Vec3 F = R.Finals[1].State.Position;
	if (R.Finals[1].Status == rb::BallFinalStatus::OnTable)
	{
		RB_CHECK(F.y <= T.HalfWidth - Rc + 1e-6 || rb::IsOverPocketOpening(T, XY(F)));
	}
	RB_CHECK(R.Finals[1].Status != rb::BallFinalStatus::OffTable);
}

RB_TEST(Integ_ARCH_ISL2_Slow_PressingIslandsCpu)
{
	// D-12b and Z-3 CPU < 1 ms each (Release): sustained contacts run in the 20 us rigid mode. The CPU time of a run is the
	// median of 9 (a single wall-clock sample measures scheduler noise on a loaded machine, not the simulation).
	constexpr int kRepeats = 9;
	double Times[2][kRepeats] = {};
	for (int Repeat = 0; Repeat < kRepeats; ++Repeat)
	{
		for (int Case = 0; Case < 2; ++Case)
		{
			std::unique_ptr<Scene> S = Case == 0 ? PressingPair() : SpinPressedIntoRail();
			S->Input.Record.Trajectories = false;
			rb::Simulator Sim;
			const auto Start = std::chrono::steady_clock::now();
			Sim.Run(S->Input, S->Result);
			Times[Case][Repeat] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
			RB_CHECK(S->Result.Status == rb::SimStatus::Ok);
			RB_CHECK(S->Result.Diagnostics.IslandSteps > 1000); // the sustained contacts really ran as islands
		}
	}
	double Median[2] = {};
	for (int Case = 0; Case < 2; ++Case)
	{
		std::sort(Times[Case], Times[Case] + kRepeats);
		Median[Case] = Times[Case][kRepeats / 2];
	}
	std::printf("  A-ISL-2: D-12b %.3f ms, Z-3 %.3f ms (median of %d)\n", Median[0], Median[1], kRepeats);
#ifdef NDEBUG
	RB_CHECK(Median[0] < 1.0);
	RB_CHECK(Median[1] < 1.0);
#endif
}

RB_TEST(Integ_ARCH_ISL3_FrozenCueBallTipIntervals)
{
	// Frozen CB struck toward the OB (RUL G17): the CB enters an island at t = 0+ with the cue tip as a participant; the tip
	// contact interval that began at the strike is closed by the island's positive tip force, with the envelope data (B = f, the
	// gap to it, the "touched another" bit).
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	Place(*S, 0, {0.0, 0.0, kR});
	Place(*S, 1, {2.0 * kR, 0.0, kR});
	rb::StrikeRequest Strike;
	Strike.Ball = 0;
	Strike.Input.Speed = 2.0;
	Strike.Input.Azimuth = 0.0;
	Strike.Input.Cue = rb::kCuePlaying19oz;
	Strike.Input.SquirtEnabled = false;
	S->Input.Strikes.PushBack(Strike);
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(R.Diagnostics.Islands >= 1);
	const rb::ShotEvent* Begin = First(R, rb::ShotEventType::IslandBegin);
	RB_REQUIRE(Begin != nullptr);
	RB_CHECK(Begin->Time == 0.0);
	// The interval on the CB ends from the island (positive tip force returned to zero), with B = f = the OB.
	const rb::ShotEvent* End = nullptr;
	for (const rb::ShotEvent& E : R.Events)
	{
		if (E.Type == rb::ShotEventType::TipContactEnd && E.A == 0 && (E.Flags & rb::ShotEventFlags::FromIsland) != 0)
		{
			End = &E;
			break;
		}
	}
	RB_REQUIRE(End != nullptr);
	RB_CHECK(End->Time > 0.0 && End->Time < 0.01);
	RB_CHECK(End->Feature == 0);
	RB_CHECK(End->B == 1);
	RB_CHECK(End->Value > -1e-3 && End->Value < 5e-3); // gap CB - f at the end (within the F7 envelope)
	// The OB is driven forward, the CB follows.
	RB_CHECK(R.Finals[1].State.Position.x > R.Finals[0].State.Position.x);
	RB_CHECK(Count(R, rb::ShotEventType::TipContactBegin, 0) >= 1);
}

RB_TEST(Integ_ARCH_ISL4_RigidIslandWithinSegmentCapacity)
{
	// A long sustained island (a pressing pair pushed for tens of ms in Rigid mode) is recorded with adaptive Sampled segments
	// within ResultCapacity::MaxSegmentsPerBall (no overflow; far fewer than one segment per step).
	std::unique_ptr<Scene> S = PressingPair(40.0);
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	const rb::ShotEvent* Begin = First(R, rb::ShotEventType::IslandBegin);
	const rb::ShotEvent* Rigid = First(R, rb::ShotEventType::IslandRigid);
	const rb::ShotEvent* End = First(R, rb::ShotEventType::IslandEnd);
	RB_REQUIRE(Begin != nullptr && Rigid != nullptr && End != nullptr);
	std::printf("  A-ISL-4: island %.1f ms (rigid from %.2f ms), %d steps, %zu / %zu segments\n", (End->Time - Begin->Time) * 1e3,
		(Rigid->Time - Begin->Time) * 1e3, R.Diagnostics.IslandSteps, R.Tracks[0].Segments.size(), R.Tracks[1].Segments.size());
	RB_CHECK(End->Time - Rigid->Time > 0.02);
	RB_CHECK(!R.Diagnostics.TrajectoryOverflow);
	const int Capacity = rb::ResultCapacity{}.MaxSegmentsPerBall;
	int Sampled = 0;
	for (int b = 0; b < 2; ++b)
	{
		RB_CHECK(static_cast<int>(R.Tracks[b].Segments.size()) < Capacity);
		for (const rb::TrajectorySegment& Seg : R.Tracks[b].Segments)
		{
			Sampled += Seg.Kind == rb::SegmentKind::Sampled ? 1 : 0;
			if (Seg.Kind == rb::SegmentKind::Sampled)
			{
				RB_CHECK(Seg.T1 - Seg.Motion.T0 <= S->Input.Params.Numerics.SampleMaxInterval + 1e-9);
			}
		}
	}
	RB_CHECK(Sampled >= 2 && Sampled < R.Diagnostics.IslandSteps / 10);
	// Contiguous tracks across the island boundaries.
	for (int b = 0; b < 2; ++b)
	{
		const std::vector<rb::TrajectorySegment>& Segs = R.Tracks[b].Segments;
		for (std::size_t k = 1; k < Segs.size(); ++k)
		{
			RB_CHECK(Segs[k - 1].T1 == Segs[k].Motion.T0);
		}
	}
}

RB_TEST(Integ_WP6b_Slow_RandomShotsRobustness)
{
	// Robustness sweep (not a spec ID): 300 random shots into loose / zero-gap racks and scattered balls (random speed 1-11 m/s,
	// elevation up to 0.3 rad, tip offsets): every shot ends Ok, with finite states, no missed event, no island budget overrun and
	// no two balls left overlapping on the table.
	rb::Rng Random(12345);
	int Islands = 0;
	int MaxSteps = 0;
	for (int Shot = 0; Shot < 300; ++Shot)
	{
		std::unique_ptr<Scene> S = MakeScene();
		const rb::TableGeometry& T = S->Table;
		const bool Rack = (Shot % 3) != 2;
		if (Rack)
		{
			rb::RackSite Sites[rb::kMaxRackSites];
			const double D = 2.0 * kR;
			const int N = rb::BuildRackLattice(rb::RackShape::Triangle15, rb::RackApexX(rb::RackShape::Triangle15, rb::RackAnchor::ApexOnFootSpot,
				T.Landmarks.FootSpot.x, D), D, Sites);
			rb::Vec2 Positions[15];
			for (int k = 0; k < N; ++k)
			{
				Positions[k] = Sites[k].Position;
			}
			rb::ApplyRackGaps(Positions, N, 0, D, Shot % 2 == 0 ? rb::kRackGapMixture : rb::kRackGapNone, 1000u + static_cast<unsigned>(Shot));
			for (int k = 0; k < N; ++k)
			{
				Place(*S, k + 1, rb::ToVec3(Positions[k], kR));
			}
		}
		else
		{
			for (int k = 1; k <= 8; ++k)
			{
				for (int Try = 0; Try < 50; ++Try)
				{
					const rb::Vec3 P{Random.NextUniform(-T.HalfLength + 0.03, T.HalfLength - 0.03), Random.NextUniform(-T.HalfWidth + 0.03, T.HalfWidth - 0.03), kR};
					bool Free = rb::Length(rb::XY(P) - rb::Vec2{T.Landmarks.HeadStringX, 0.0}) > 0.3;
					for (int j = 1; j < k && Free; ++j)
					{
						Free = !S->Input.Balls[j].InPlay || rb::Length(S->Input.Balls[j].State.Position - P) > 2.0 * kR + 1e-4;
					}
					if (Free)
					{
						Place(*S, k, P);
						break;
					}
				}
			}
		}
		Place(*S, 0, {T.Landmarks.HeadStringX + Random.NextUniform(-0.2, 0.2), Random.NextUniform(-0.3, 0.3), kR});
		rb::StrikeRequest Strike;
		Strike.Ball = 0;
		Strike.Input.Speed = Random.NextUniform(1.0, 11.0);
		Strike.Input.Azimuth = Rack ? Random.NextUniform(-0.1, 0.1) : Random.NextUniform(-3.1, 3.1);
		Strike.Input.Elevation = Random.NextUniform(0.0, 0.3);
		Strike.Input.OffsetA = Random.NextUniform(-0.4, 0.4);
		Strike.Input.OffsetB = Random.NextUniform(-0.4, 0.4);
		Strike.Input.Cue = rb::kCuePlaying19oz;
		S->Input.Strikes.PushBack(Strike);
		const rb::SimStatus Status = Run(*S);
		const rb::ShotResult& R = S->Result;
		RB_CHECK(Status == rb::SimStatus::Ok);
		RB_CHECK(R.Diagnostics.MissedEvents == 0);
		RB_CHECK(!R.Diagnostics.IslandBudgetExceeded);
		Islands += R.Diagnostics.Islands;
		MaxSteps = R.Diagnostics.IslandSteps > MaxSteps ? R.Diagnostics.IslandSteps : MaxSteps;
		for (int b = 0; b < rb::kMaxBalls; ++b)
		{
			if (R.Finals[b].Status != rb::BallFinalStatus::OnTable)
			{
				continue;
			}
			RB_CHECK(rb::IsFinite(R.Finals[b].State.Position.x) && rb::IsFinite(R.Finals[b].State.Position.y));
			for (int c = b + 1; c < rb::kMaxBalls; ++c)
			{
				if (R.Finals[c].Status == rb::BallFinalStatus::OnTable)
				{
					RB_CHECK(rb::Length(R.Finals[b].State.Position - R.Finals[c].State.Position) >= 2.0 * kR - 1e-6);
				}
			}
		}
	}
	std::printf("  WP-6b robustness: 300 shots, %d islands, max %d island steps per shot\n", Islands, MaxSteps);
	RB_CHECK(Islands > 100);
}
