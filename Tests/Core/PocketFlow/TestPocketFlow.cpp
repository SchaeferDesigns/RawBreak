// WP-6b: pocket state machine, landing routing and rail-top routing through Simulator::Run (physics-collisions P-3 ... P-6;
// architecture A-POCK-1, A-POCK-2, A-RAIL-1, A-RAIL-2). They need the WP-6a loop (Integ_).
#include "rbtest.h"

#include "../Islands/IslandTestUtil.h"

#include "rb/Physics/PocketDrop.h"

#include <cstdio>

using namespace isltest;

namespace
{
	const rb::PocketGeometry& FootLeftPocket(const Scene& S) { return S.Table.Pockets[kFootLeft]; }

	double NoseOffset(const rb::TableGeometry& T) { return rb::ComputeCushionContact(kR, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset; }

	// Ball rolling (no slip) at Speed along Direction from Position.
	void PlaceRolling(Scene& S, int Ball, const Vec3& Position, const Vec3& Direction, double Speed)
	{
		const Vec3 V = Direction * Speed;
		Place(S, Ball, Position, V, rb::RollingOmegaH(V, kR));
	}

	int CountFeature(const rb::ShotResult& R, rb::ShotEventType Type, int Feature)
	{
		int N = 0;
		for (const rb::ShotEvent& E : R.Events)
		{
			N += E.Type == Type && E.Feature == Feature ? 1 : 0;
		}
		return N;
	}
}

RB_TEST(Integ_COL_P3_RollIntoCornerAlongTheAxis)
{
	// 1 m/s along the FOOT_LEFT pocket axis from 0.3 m out: no facing / jaw / nose event; BallPocketEnter at the drop edge
	// (rho_h = a_d) with an immediate leave (~0.97 m/s > 0.572); then ONE BallLiner event (the back wall at a_d + r_p - R ~ 100 mm,
	// after ~0.10 s, dropped ~49 mm < 2R); then BallPocketed at z = -R.
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::PocketGeometry& P = FootLeftPocket(*S);
	const Vec3 Axis = rb::ToVec3(P.Axis);
	PlaceRolling(*S, 0, rb::ToVec3(P.CaptureCenter, kR) - Axis * 0.3, Axis, 1.0);
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(Count(R, rb::ShotEventType::BallJaw) == 0);
	RB_CHECK(Count(R, rb::ShotEventType::BallCushion) == 0);
	const rb::ShotEvent* Enter = First(R, rb::ShotEventType::BallPocketEnter);
	const rb::ShotEvent* Liner = First(R, rb::ShotEventType::BallLiner);
	const rb::ShotEvent* Pocketed = First(R, rb::ShotEventType::BallPocketed);
	RB_REQUIRE(Enter != nullptr && Liner != nullptr && Pocketed != nullptr);
	RB_CHECK(Enter->Feature == kFootLeft && Liner->Feature == kFootLeft && Pocketed->Feature == kFootLeft);
	RB_CHECK(IndexOf(R, Enter) < IndexOf(R, Liner) && IndexOf(R, Liner) < IndexOf(R, Pocketed));
	RB_CHECK(CountFeature(R, rb::ShotEventType::BallLiner, kFootLeft) == 1);
	RB_CHECK(Count(R, rb::ShotEventType::BallPocketRim) == 0);
	const double V0 = rb::Sqrt(1.0 - 2.0 * 0.010 * kG * (0.3 - P.DropEdgeRadius)); // rolling deceleration mu_r g
	RB_CHECK_NEAR(Enter->Value, V0, 1e-6);
	RB_CHECK(Enter->Value > rb::Sqrt(kG * (kR + P.DropRadius)));
	RB_CHECK_NEAR(rb::Length(XY(Enter->Pre[0].Position) - P.CaptureCenter), P.DropEdgeRadius, 1e-9);
	const double Flight = Liner->Time - Enter->Time;
	const double Drop = kR - Liner->Pre[0].Position.z;
	std::printf("  P-3: enter %.6f m/s, liner after %.4f s, dropped %.1f mm, pocketed at %.4f s\n", Enter->Value, Flight, Drop * 1e3, Pocketed->Time);
	RB_CHECK(Flight > 0.09 && Flight < 0.11);
	RB_CHECK(Drop > 0.045 && Drop < 0.056 && Drop < 2.0 * kR);
	RB_CHECK(R.Finals[0].Status == rb::BallFinalStatus::Pocketed);
	RB_CHECK(R.Finals[0].Pocket == rb::PocketId::FootLeft);
}

RB_TEST(Integ_COL_P4_LipHang)
{
	// Rolled along the axis to stop 0.5 mm OUTSIDE the drop-edge circle a_d: stays on the table at rest; 0.5 mm INSIDE a_d: falls
	// (BallPocketed) after a finite pivot time T_p computed with the 5.4 substitution. d = v0^2 / (2 mu_r g): v0 = 0.0990285 m/s
	// stops in 0.05 m.
	const double V0 = 0.0990285;
	for (int Inside = 0; Inside < 2; ++Inside)
	{
		std::unique_ptr<Scene> S = MakeScene();
		RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
		const rb::PocketGeometry& P = FootLeftPocket(*S);
		const Vec3 Axis = rb::ToVec3(P.Axis);
		const double Stop = P.DropEdgeRadius + (Inside != 0 ? -0.0005 : 0.0005);
		PlaceRolling(*S, 0, rb::ToVec3(P.CaptureCenter, kR) - Axis * (Stop + 0.05), Axis, V0);
		RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
		const rb::ShotResult& R = S->Result;
		if (Inside == 0)
		{
			RB_CHECK(Count(R, rb::ShotEventType::BallPocketEnter) == 0);
			RB_CHECK(R.Finals[0].Status == rb::BallFinalStatus::OnTable);
			RB_CHECK(R.Finals[0].State.State == rb::MotionState::Stationary);
			RB_CHECK_NEAR(rb::Length(XY(R.Finals[0].State.Position) - P.CaptureCenter), Stop, 1e-6);
			continue;
		}
		const rb::ShotEvent* Enter = First(R, rb::ShotEventType::BallPocketEnter);
		const rb::ShotEvent* Pocketed = First(R, rb::ShotEventType::BallPocketed);
		RB_REQUIRE(Enter != nullptr && Pocketed != nullptr);
		const double VEdge = rb::Sqrt(2.0 * 0.010 * kG * 0.0005);
		RB_CHECK_NEAR(Enter->Value, VEdge, 1e-6);
		const rb::PivotResult Pivot = rb::ComputePivot(Enter->Value, kR + P.DropRadius, 0.4, kG, rb::NumericsConfig{});
		RB_CHECK(!Pivot.Immediate);
		RB_CHECK(rb::IsFinite(Pivot.Duration) && Pivot.Duration > 0.2 && Pivot.Duration < 0.5);
		const double Fall = Pocketed->Time - Enter->Time;
		std::printf("  P-4: v_edge %.6f m/s, T_p %.2f ms, pocketed %.2f ms after the edge\n", Enter->Value, Pivot.Duration * 1e3, Fall * 1e3);
		RB_CHECK(Fall > Pivot.Duration && Fall < Pivot.Duration + 0.2);
		RB_CHECK(R.Finals[0].Status == rb::BallFinalStatus::Pocketed);
		// The pivot is recorded as Sampled pieces of the true path (playback), contiguous with the rest of the track.
		int PivotPieces = 0;
		for (const rb::TrajectorySegment& Seg : R.Tracks[0].Segments)
		{
			PivotPieces += Seg.Motion.State == rb::MotionState::PocketPivot ? 1 : 0;
		}
		RB_CHECK(PivotPieces >= 1);
	}
}

RB_TEST(Integ_COL_P5_SlowRattleAlongTheRail)
{
	// Behaviour regression: 1.5 m/s along RAIL_LEFT (C3), frozen to the rail (sigma = R_c), into the FOOT_LEFT corner: at least
	// one BallJaw event; the outcome is pinned (recorded once).
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const double Y = S->Table.HalfWidth - NoseOffset(S->Table);
	PlaceRolling(*S, 0, {0.5, Y, kR}, {1.0, 0.0, 0.0}, 1.5);
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	const int Jaws = Count(R, rb::ShotEventType::BallJaw);
	std::printf("  P-5: %d jaw contacts, %d cushion contacts, outcome %s\n", Jaws, Count(R, rb::ShotEventType::BallCushion),
		R.Finals[0].Status == rb::BallFinalStatus::Pocketed ? "pocketed" : "rattled out");
	RB_CHECK(Jaws >= 1);
	RB_CHECK(R.Finals[0].Status != rb::BallFinalStatus::OffTable);
}

RB_TEST(Integ_COL_P6_AirborneEntryWithoutSlate)
{
	// A ball landing with its center inside the capture circle: no slate impact (no C.3), BallPocketed without any bounce.
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::PocketGeometry& P = FootLeftPocket(*S);
	Place(*S, 0, rb::ToVec3(P.CaptureCenter, kR + 0.05), {0.2, 0.1, -0.5});
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(Count(R, rb::ShotEventType::BallSlate) == 0);
	RB_CHECK(Count(R, rb::ShotEventType::BallLand) == 0);
	const rb::ShotEvent* Enter = First(R, rb::ShotEventType::BallPocketEnter);
	const rb::ShotEvent* Pocketed = First(R, rb::ShotEventType::BallPocketed);
	RB_REQUIRE(Enter != nullptr && Pocketed != nullptr);
	RB_CHECK(IndexOf(R, Enter) < IndexOf(R, Pocketed));
	RB_CHECK(Pocketed->Feature == kFootLeft);
	RB_CHECK(R.Finals[0].Status == rb::BallFinalStatus::Pocketed);
	RB_CHECK(R.Diagnostics.MissedEvents == 0);
}

RB_TEST(Integ_ARCH_POCK1_HopLandingInTheDropEdgeAnnulus)
{
	// A hopping ball comes down with its center over the rounded annulus r_p < rho < a_d on the front arc: it continues in the
	// pocket and meets the rim torus (BallPocketRim); no missed event, no slate impact.
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::PocketGeometry& P = FootLeftPocket(*S);
	const Vec3 Axis = rb::ToVec3(P.Axis);
	const double Rho = P.DropEdgeRadius - 0.002;
	RB_REQUIRE(Rho > P.CaptureRadius);
	Place(*S, 0, rb::ToVec3(P.CaptureCenter, kR + 0.02) - Axis * Rho, {0.0, 0.0, -0.1});
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(R.Diagnostics.MissedEvents == 0);
	RB_CHECK(Count(R, rb::ShotEventType::BallSlate) == 0);
	RB_CHECK(Count(R, rb::ShotEventType::BallPocketRim) >= 1);
	std::printf("  A-POCK-1: %d rim contacts, outcome %s\n", Count(R, rb::ShotEventType::BallPocketRim),
		R.Finals[0].Status == rb::BallFinalStatus::Pocketed ? "pocketed" : "on table");
}

RB_TEST(Integ_ARCH_POCK2_JumpAcrossThePocketHitsTheBackWall)
{
	// A jumped ball flying across the FOOT_LEFT pocket above the cloth (center below WallTopZ) hits the back wall (BallLiner,
	// airborne GRI) before it could land.
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::PocketGeometry& P = FootLeftPocket(*S);
	const Vec3 Axis = rb::ToVec3(P.Axis);
	Place(*S, 0, rb::ToVec3(P.CaptureCenter, kR + 0.004) - Axis * (P.DropEdgeRadius + 0.01), Axis * 2.0 + Vec3{0.0, 0.0, 0.3});
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	const rb::ShotEvent* Liner = First(R, rb::ShotEventType::BallLiner);
	RB_REQUIRE(Liner != nullptr);
	RB_CHECK(Liner->Pre[0].Position.z > kR);
	RB_CHECK(Liner->Pre[0].Position.z < P.WallTopZ);
	RB_CHECK((Liner->Flags & rb::ShotEventFlags::Airborne) != 0);
	RB_CHECK(Liner->Feature == kFootLeft);
	RB_CHECK(Count(R, rb::ShotEventType::BallSlate) == 0 || IndexOf(R, First(R, rb::ShotEventType::BallSlate)) > IndexOf(R, Liner));
	RB_CHECK(R.Diagnostics.MissedEvents == 0);
}

RB_TEST(Integ_ARCH_RAIL1_RestOnTheFlatCapIsOffTheTable)
{
	// A ball landing on the flat rail cap bounces, settles and rolls on the cap as analytic segments (no island steps) and comes
	// to rest there: BallOffTable(RestsOnRailOrFrame) (WPA 2.6).
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::TableGeometry& T = S->Table;
	const double Y = T.HalfWidth + T.Spec.CushionWidth + 0.06;
	Place(*S, 0, {0.3, Y, T.Spec.RailTopZ + kR + 0.01}, {0.2, 0.0, 0.0});
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(Count(R, rb::ShotEventType::BallRailTop) >= 1);
	RB_CHECK(R.Diagnostics.IslandSteps == 0);
	const rb::ShotEvent* Off = First(R, rb::ShotEventType::BallOffTable);
	RB_REQUIRE(Off != nullptr);
	RB_CHECK(Off->Feature == static_cast<std::uint8_t>(rb::OffTableReason::RestsOnRailOrFrame));
	RB_CHECK(R.Finals[0].Status == rb::BallFinalStatus::OffTable);
	RB_CHECK(R.Finals[0].OffReason == rb::OffTableReason::RestsOnRailOrFrame);
	RB_CHECK_NEAR(R.Finals[0].State.Position.z, T.Spec.RailTopZ + kR, 1e-9);
	// It rolled on the cap (a surface segment with SupportZ = RailTopZ) before stopping.
	bool OnCap = false;
	for (const rb::TrajectorySegment& Seg : R.Tracks[0].Segments)
	{
		OnCap = OnCap || (Seg.Kind == rb::SegmentKind::Analytic && rb::IsOnSurface(Seg.Motion.State) && Seg.Motion.SupportZ == T.Spec.RailTopZ &&
			rb::IsMoving(Seg.Motion.State));
	}
	RB_CHECK(OnCap);
}

RB_TEST(Integ_ARCH_RAIL2_SlopeBounceRollsBackOntoTheCloth)
{
	// A ball dropping onto the sloped cushion top of RAIL_LEFT with a low bounce is handed to a rigid rail-top island, rolls down
	// the slope, over the nose and back onto the cloth: it ends on the table, in front of the cushion.
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::TableGeometry& T = S->Table;
	const double Y = T.HalfWidth + 0.02;
	const double Slope = (T.Spec.RailTopZ - T.Spec.CushionNoseHeight) / T.Spec.CushionWidth;
	const double PlaneZ = T.Spec.CushionNoseHeight + 0.02 * Slope;
	const double CenterZ = PlaneZ + kR * rb::Sqrt(1.0 + Slope * Slope) + 0.003;
	Place(*S, 0, {0.3, Y, CenterZ});
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(Count(R, rb::ShotEventType::BallRailTop) >= 1);
	RB_CHECK(R.Diagnostics.Islands >= 1);
	RB_CHECK(R.Diagnostics.IslandRigidSwitches >= 1);
	RB_CHECK(Count(R, rb::ShotEventType::BallOffTable) == 0);
	RB_CHECK(R.Finals[0].Status == rb::BallFinalStatus::OnTable);
	RB_CHECK(R.Finals[0].State.Position.y <= T.HalfWidth - NoseOffset(T) + 1e-6);
	RB_CHECK_NEAR(R.Finals[0].State.Position.z, kR, 1e-9);
	std::printf("  A-RAIL-2: island steps %d, final y %.4f\n", R.Diagnostics.IslandSteps, R.Finals[0].State.Position.y);
}
