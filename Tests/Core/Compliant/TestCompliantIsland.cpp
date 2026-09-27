// WP-3: unit tests of the compliant island API (physics-collisions 3.9.2 joining / exit / records, 3.9.3 constants,
// 3.9.6 records, pitfalls 16-17; architecture 8.8 bounded features, tips, id independence with features).
#include "rbtest.h"

#include "CompliantTestUtil.h"

#include <chrono>
#include <cstdio>
#include <cstring>

using namespace rbcl;

namespace
{
	rb::CliParams TableCli()
	{
		rb::CliParams P = PureCli(rb::TsujiAlphaForRestitution(0.95));
		P.ClothSupport = true;
		return P;
	}

	rb::CushionRestitutionLaw ConstantLaw(double E) { return rb::CushionRestitutionLaw{E, 0.0, 1.0, E}; }

	// Steps once and returns the velocity change of body 0.
	rb::Vec3 KickOf(rb::CompliantIsland& Island)
	{
		const rb::Vec3 Before = Island.Body(0).Velocity;
		rb::IslandRecordList Records;
		Island.Step(Records);
		return Island.Body(0).Velocity - Before;
	}
}

RB_TEST(Compliant_HertzConstants)
{
	// 3.9.3: T_H = 378 / 329 / 238 / 207 us at 0.5 / 1 / 5 / 10 m/s (m* = m/2, K = 8.0587e8).
	const double K = 8.0587e8;
	const double MStar = 0.5 * kM;
	RB_CHECK_NEAR(rb::HertzContactTime(0.5, MStar, K) * 1e6, 378.0, 0.5);
	RB_CHECK_NEAR(rb::HertzContactTime(1.0, MStar, K) * 1e6, 329.0, 0.5);
	RB_CHECK_NEAR(rb::HertzContactTime(5.0, MStar, K) * 1e6, 238.0, 0.5);
	RB_CHECK_NEAR(rb::HertzContactTime(10.0, MStar, K) * 1e6, 207.0, 0.5);
	RB_CHECK(rb::HertzContactTime(0.0, MStar, K) == rb::kInfinity);
	// K from Marlow via TP B.29: 2270 N at 2 R 3.49e-3 compression.
	RB_CHECK_NEAR(2270.0 / rb::Pow(2.0 * kR * 3.49e-3, 1.5), 8.0587e8, 1e4);
	// delta_cl = max(eps_touch, 1.2 v T_H(v)): 0.40 mm at 1 m/s, about 2.1 mm at 8 m/s.
	const rb::CliParams P = PureCli();
	RB_CHECK_NEAR(rb::IslandJoinDistance(1.0, MStar, P, 1e-9), 0.40e-3, 0.01e-3);
	RB_CHECK_NEAR(rb::IslandJoinDistance(8.0, MStar, P, 1e-9), 2.1e-3, 0.05e-3);
	RB_CHECK(rb::IslandJoinDistance(0.0, MStar, P, 1e-9) == 1e-9);
	RB_CHECK(rb::IslandJoinDistance(1e-12, MStar, P, 1e-9) == 1e-9);
	// eta = alpha sqrt(m* K); linear tip stiffness with half period T: pi^2 m_eff / T^2.
	RB_CHECK_NEAR(rb::TsujiDamping(0.03689, MStar, K), 0.03689 * rb::Sqrt(MStar * K), 1e-12);
	const double MEff = kM * 0.5387 / (kM + 0.5387);
	RB_CHECK_NEAR(rb::CueTipContactStiffness(1e-3, kM, 0.5387), rb::kPi * rb::kPi * MEff / 1e-6, 1e-6);
}

RB_TEST(Compliant_FirstTouchRecord)
{
	// 3.9.6: one BallBall record at the first positive force, time-stamped at that step, normal from the lower id.
	rb::CompliantIsland Island;
	Island.Reset(0.25, rb::CliMode::Compliant, PureCli(), BallModel(0.95, rb::BallBallFrictionModel::None), kCloth, 0.0, rb::NumericsConfig{});
	Island.AddBody(Body(7, {0.0, 0.0, kR}, {1.0, 0.0, 0.0}));
	Island.AddBody(Body(2, {2.0 * kR, 0.0, kR}));
	rb::IslandRecordList All;
	int Count = 0;
	RB_REQUIRE(RunToExit(Island, 100000, &All, &Count) > 0);
	RB_REQUIRE(Count == 1);
	const rb::IslandContactRecord& First = All[0];
	RB_CHECK(First.Kind == rb::IslandRecordKind::BallBall);
	RB_CHECK(First.BallA == 2 && First.BallB == 7); // lower id first
	RB_CHECK_NEAR(First.Normal.x, -1.0, 1e-12);     // from the lower id (ball 2) to ball 7
	RB_CHECK(First.Time >= 0.25 && First.Time < 0.25 + 1e-5);
	RB_CHECK(First.NormalSpeed > 0.99 && First.NormalSpeed <= 1.0);
	RB_CHECK(First.Feature == -1 && First.Strike == -1);

}

RB_TEST(Compliant_RearmAfterLeaveDistance)
{
	// A slow ball bouncing between two heavy walls of balls: every separation beyond LeaveDistance re-arms the pair.
	rb::NumericsConfig Numerics;
	rb::CompliantIsland Island;
	Island.Reset(0.0, rb::CliMode::Compliant, PureCli(0.0), BallModel(1.0, rb::BallBallFrictionModel::None), kCloth, 0.0, Numerics);
	rb::IslandBody Wall = Body(0, {0.0, 0.0, kR});
	Wall.Mass = 1e6;
	Wall.Inertia = 0.4 * Wall.Mass * kR * kR;
	Island.AddBody(Wall);
	rb::IslandBody Other = Wall;
	Other.Ball = 1;
	Other.Position = {4.0 * kR + 2.0e-3, 0.0, kR}; // the middle ball has 1 mm of play on each side (> LeaveDistance)
	Island.AddBody(Other);
	Island.AddBody(Body(2, {2.0 * kR + 1.0e-3, 0.0, kR}, {1.0, 0.0, 0.0}));
	rb::IslandRecordList Records;
	int WithWall0 = 0;
	int WithWall1 = 0;
	for (int s = 0; s < 20000; ++s)
	{
		Records.Clear();
		Island.Step(Records);
		for (const rb::IslandContactRecord& R : Records)
		{
			RB_CHECK(R.Kind == rb::IslandRecordKind::BallBall && R.BallB == 2);
			WithWall0 += R.BallA == 0 ? 1 : 0;
			WithWall1 += R.BallA == 1 ? 1 : 0;
		}
	}
	// 20 ms at ~1 m/s over 2 mm of play: about 5 round trips.
	RB_CHECK(WithWall1 >= 4 && WithWall1 <= 6);
	RB_CHECK(WithWall0 >= 4 && WithWall0 <= 6);

	// With 0.1 mm of play the separation stays below LeaveDistance (0.5 mm) even with the ~0.15 mm Hertz compression at the
	// other wall: the pairs are never re-armed, one record per wall.
	rb::CompliantIsland Tight;
	Tight.Reset(0.0, rb::CliMode::Compliant, PureCli(0.0), BallModel(1.0, rb::BallBallFrictionModel::None), kCloth, 0.0, Numerics);
	Tight.AddBody(Wall);
	Other.Position = {4.0 * kR + 0.2e-3, 0.0, kR};
	Tight.AddBody(Other);
	Tight.AddBody(Body(2, {2.0 * kR + 0.1e-3, 0.0, kR}, {1.0, 0.0, 0.0}));
	int Total = 0;
	for (int s = 0; s < 20000; ++s)
	{
		Records.Clear();
		Tight.Step(Records);
		Total += Records.Size();
	}
	RB_CHECK(Total == 2);
}

RB_TEST(Compliant_ExitNeedsGeometricSeparation)
{
	// Pitfall 17: the clipped Tsuji force reaches 0 while the balls still overlap (0.4-1.5 um); the island must not exit
	// before the gap is >= 0 (the 1 um overlap guard of 3.6 would trip).
	for (double V0 : {0.05, 0.3, 1.0})
	{
		rb::CompliantIsland Island;
		MakeChain(Island, 2, V0, kAlpha095);
		rb::IslandRecordList Records;
		bool Exited = false;
		bool OverlappedWhileSeparating = false;
		for (int s = 0; s < 100000 && !Exited; ++s)
		{
			Records.Clear();
			Island.Step(Records);
			const double Gap = rb::Length(Island.Body(1).Position - Island.Body(0).Position) - 2.0 * kR;
			const double Rate = VelocityOf(Island, 1) - VelocityOf(Island, 0);
			OverlappedWhileSeparating = OverlappedWhileSeparating || (Gap < -1e-7 && Rate > 0.9 * 0.95 * V0);
			Exited = Island.CanExit();
			if (Exited)
			{
				RB_CHECK(Gap >= -rb::NumericsConfig{}.ContactTol);
			}
		}
		RB_CHECK(OverlappedWhileSeparating); // the case the gap rule exists for
		RB_CHECK(Exited);
	}
}

RB_TEST(Compliant_PressingRetriggerBlocksExit)
{
	// A ball touching a support plane from above with zero normal speed and gravity pressing it in must not exit (the event
	// mode's pressing rule would fire at once); without gravity it may.
	rb::IslandFeature Plane;
	Plane.Kind = rb::IslandFeatureKind::Plane;
	Plane.Point = {0.0, 0.0, 0.048};
	Plane.Direction = {0.0, 0.0, 1.0};
	Plane.VertexCount = 4;
	Plane.Vertices[0] = {-1.0, -1.0};
	Plane.Vertices[1] = {1.0, -1.0};
	Plane.Vertices[2] = {1.0, 1.0};
	Plane.Vertices[3] = {-1.0, 1.0};
	Plane.Restitution = ConstantLaw(0.5);
	rb::CliParams P = PureCli();
	P.ExitZeroForceSteps = 0;
	for (double Gravity : {kG, 0.0})
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, rb::CliMode::Compliant, P, BallModel(), kCloth, Gravity, rb::NumericsConfig{});
		Island.AddFeature(Plane);
		Island.AddBody(Body(0, {0.0, 0.0, 0.048 + kR}, {0.3, 0.0, 0.0}));
		RB_CHECK(Island.CanExit() == (Gravity == 0.0));
	}
	// Ball-ball: two touching balls, the lower one accelerating into the upper (free fall onto a resting ball on the cloth).
	rb::CompliantIsland Pair;
	rb::CliParams Cloth = TableCli();
	Cloth.ExitZeroForceSteps = 0;
	Pair.Reset(0.0, rb::CliMode::Compliant, Cloth, BallModel(), kCloth, kG, rb::NumericsConfig{});
	Pair.AddBody(Body(0, {0.0, 0.0, kR}, {}, {}, true));
	Pair.AddBody(Body(1, {0.0, 0.0, 3.0 * kR}, {}, {}, false)); // resting on top of ball 0, falling onto it
	RB_CHECK(!Pair.CanExit());
}

RB_TEST(Compliant_ClothSupportMatchesMotionLaws)
{
	// Island support model vs the motion spec (A.5, A.8): a stun ball slides to rolling at (5/7) v0 after 2 v0 / (7 mu_s g),
	// then rolls at -mu_r g; w_z decays at alpha_sp. Compliant and rigid modes.
	for (rb::CliMode Mode : {rb::CliMode::Compliant, rb::CliMode::Rigid})
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, Mode, TableCli(), BallModel(), kCloth, kG, rb::NumericsConfig{});
		Island.AddBody(Body(0, {0.0, 0.0, kR}, {2.0, 0.0, 0.0}, {0.0, 0.0, 5.0}, true));
		const double Dt = Mode == rb::CliMode::Rigid ? 20e-6 : 1e-6;
		const double SlideEnd = 2.0 * 2.0 / (7.0 * 0.2 * kG);
		rb::IslandRecordList Records;
		while (Island.Time() < SlideEnd + 0.01)
		{
			Island.Step(Records);
		}
		const rb::IslandBody& B = Island.Body(0);
		const double T = Island.Time();
		RB_CHECK_NEAR(B.Velocity.x, 2.0 * 5.0 / 7.0 - kCloth.RollingResistance * kG * (T - SlideEnd), 2.0 * 0.2 * kG * Dt);
		RB_CHECK_NEAR(B.Omega.y * kR, B.Velocity.x, 1e-12);
		RB_CHECK_NEAR(B.Omega.z, 5.0 - kCloth.SpinDeceleration * T, 1e-9);
		RB_CHECK(B.Position.z == kR && B.Velocity.z == 0.0);
		RB_CHECK_NEAR(B.Velocity.y, 0.0, 1e-15);
	}
}

RB_TEST(Compliant_FeatureGeometry)
{
	const rb::NumericsConfig Numerics;
	auto Fresh = [&](rb::CompliantIsland& Island, const rb::IslandFeature& F, const rb::Vec3& Position)
	{
		Island.Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(), kCloth, 0.0, Numerics);
		Island.AddFeature(F);
		Island.AddBody(Body(0, Position));
	};
	// EdgeLine at the centre height, overlapped by 10 um: pushed along the horizontal normal; beyond the end cap: radial push.
	rb::IslandFeature Edge;
	Edge.Kind = rb::IslandFeatureKind::EdgeLine;
	Edge.Point = {0.0, 0.0, kR};
	Edge.Direction = {1.0, 0.0, 0.0};
	Edge.Normal = {0.0, -1.0, 0.0};
	Edge.Length = 0.1;
	Edge.Restitution = ConstantLaw(0.9);
	rb::CompliantIsland Island;
	Fresh(Island, Edge, {0.05, -(kR - 10e-6), kR});
	rb::Vec3 Kick = KickOf(Island);
	RB_CHECK(Kick.y < 0.0 && rb::Abs(Kick.x) < 1e-15 && rb::Abs(Kick.z) < 1e-15);
	RB_CHECK_NEAR(-Kick.y * kM / 1e-6, 1e6 * 10e-6, 1e-6); // k_c delta
	Fresh(Island, Edge, {-(kR - 10e-6) * 0.6, -(kR - 10e-6) * 0.8, kR});
	Kick = KickOf(Island);
	RB_CHECK(Kick.x < 0.0 && Kick.y < 0.0);
	RB_CHECK_NEAR(Kick.y / Kick.x, 0.8 / 0.6, 1e-9); // end cap is a point
	Fresh(Island, Edge, {0.05, -(kR + 10e-6), kR});
	RB_CHECK(rb::Length(KickOf(Island)) == 0.0);
	// JawCircle exposed only between AngleFrom and AngleFrom + AngleSweep (here the -y half: from -pi to 0).
	rb::IslandFeature Jaw;
	Jaw.Kind = rb::IslandFeatureKind::JawCircle;
	Jaw.Point = {0.0, 0.0, kR};
	Jaw.Radius = 0.004;
	Jaw.AngleFrom = -rb::kPi;
	Jaw.AngleSweep = rb::kPi;
	Jaw.Restitution = ConstantLaw(0.9);
	Fresh(Island, Jaw, {0.0, -(0.004 + kR - 10e-6), kR});
	Kick = KickOf(Island);
	RB_CHECK(Kick.y < 0.0 && rb::Abs(Kick.x) < 1e-15);
	Fresh(Island, Jaw, {0.0, 0.004 + kR - 10e-6, kR}); // unexposed side
	RB_CHECK(rb::Length(KickOf(Island)) == 0.0);
	// FacingPlane valid only over its segment and height range.
	rb::IslandFeature Facing;
	Facing.Kind = rb::IslandFeatureKind::FacingPlane;
	Facing.Point = {0.0, 0.0, 0.036};
	Facing.Direction = {1.0, 0.0, 0.0};
	Facing.Normal = rb::Normalized(rb::Vec3{0.0, -1.0, -0.2});
	Facing.Length = 0.08;
	Facing.ZMin = -0.01;
	Facing.ZMax = 0.036;
	Facing.Restitution = ConstantLaw(0.9);
	const rb::Vec3 OnFace = rb::Vec3{0.04, -0.2 * (0.02 - 0.036), 0.02} + Facing.Normal * (kR - 10e-6); // face point + normal offset
	Fresh(Island, Facing, OnFace);
	Kick = KickOf(Island);
	RB_CHECK_NEAR(rb::Dot(rb::Normalized(Kick), Facing.Normal), 1.0, 1e-12);
	Fresh(Island, Facing, OnFace + rb::Vec3{0.06, 0.0, 0.0}); // beyond the segment
	RB_CHECK(rb::Length(KickOf(Island)) == 0.0);
	// Plane minus a cut disc.
	rb::IslandFeature Plane;
	Plane.Kind = rb::IslandFeatureKind::Plane;
	Plane.Point = {0.0, 0.0, 0.048};
	Plane.Direction = {0.0, 0.0, 1.0};
	Plane.VertexCount = 3;
	Plane.Vertices[0] = {0.0, 0.0};
	Plane.Vertices[1] = {0.2, 0.0};
	Plane.Vertices[2] = {0.0, 0.2};
	Plane.HasCut = true;
	Plane.CutCenter = {0.05, 0.05};
	Plane.CutRadius = 0.02;
	Plane.Restitution = ConstantLaw(0.5);
	Fresh(Island, Plane, {0.02, 0.1, 0.048 + kR - 10e-6});
	RB_CHECK(KickOf(Island).z > 0.0);
	Fresh(Island, Plane, {0.05, 0.05, 0.048 + kR - 10e-6}); // over the cut
	RB_CHECK(rb::Length(KickOf(Island)) == 0.0);
	Fresh(Island, Plane, {0.15, 0.15, 0.048 + kR - 10e-6}); // outside the triangle
	RB_CHECK(rb::Length(KickOf(Island)) == 0.0);
}

RB_TEST(Compliant_CushionContactRestitution)
{
	// Linear compliant cushion with c_c from e (collisions 3.9.2): a ball hitting a nose at its centre height (horizontal
	// normal, no cloth, no friction effect for a head-on hit) rebounds with about e (the clipped dashpot is a little livelier).
	rb::IslandFeature Edge;
	Edge.Kind = rb::IslandFeatureKind::EdgeLine;
	Edge.Point = {-1.0, 0.0, kR};
	Edge.Direction = {1.0, 0.0, 0.0};
	Edge.Normal = {0.0, -1.0, 0.0};
	Edge.Length = 2.0;
	Edge.Restitution = ConstantLaw(0.9);
	Edge.Friction = 0.14;
	for (double V : {0.5, 2.0})
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(), kCloth, 0.0, rb::NumericsConfig{});
		Island.AddFeature(Edge);
		Island.AddBody(Body(0, {0.0, -kR, kR}, {0.0, V, 0.0}));
		rb::IslandRecordList All;
		int Count = 0;
		RB_REQUIRE(RunToExit(Island, 100000, &All, &Count) > 0);
		RB_CHECK(Count == 1 && All[0].Kind == rb::IslandRecordKind::BallFeature && All[0].Feature == 0);
		RB_CHECK_NEAR(All[0].Normal.y, -1.0, 1e-12);
		RB_CHECK_NEAR(-Island.Body(0).Velocity.y / V, 0.9, 0.03);
		RB_CHECK(-Island.Body(0).Velocity.y / V >= 0.9);
	}
}

RB_TEST(Compliant_RollingCushionReboundNearMathavan)
{
	// A rolling ball hitting a nose (h = 0.635 D) perpendicularly on the cloth inside an island (linear compliant cushion with
	// e = 0.98, mu_w 0.14, cloth friction with the carried normal impulse): the rebound reproduces the rigid Mathavan result
	// of COL M-5 (-v_Y'/V0 = 0.9599, R w_X' = -0.24558 V0) closely, for any V0: the island's cushion / cloth coupling is the
	// constrained-slate physics of 4.5, not a free-ball impulse.
	const double H = 0.635 * 2.0 * kR;
	const double Rc = rb::Sqrt(kR * kR - (H - kR) * (H - kR));
	rb::IslandFeature Nose;
	Nose.Kind = rb::IslandFeatureKind::EdgeLine;
	Nose.Point = {-1.0, 0.0, H};
	Nose.Direction = {1.0, 0.0, 0.0};
	Nose.Normal = {0.0, -1.0, 0.0};
	Nose.Length = 2.0;
	Nose.Restitution = ConstantLaw(0.98);
	Nose.Friction = 0.14;
	for (double V : {0.5, 1.0, 2.0})
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, rb::CliMode::Compliant, TableCli(), BallModel(), kCloth, kG, rb::NumericsConfig{});
		Island.AddFeature(Nose);
		Island.AddBody(Body(0, {0.0, -Rc, kR}, {0.0, V, 0.0}, {-V / kR, 0.0, 0.0}, true));
		RB_REQUIRE(RunToExit(Island, 100000) > 0);
		const rb::IslandBody& B = Island.Body(0);
		RB_CHECK_NEAR(-B.Velocity.y / V, 0.9599, 0.005);
		RB_CHECK_NEAR(kR * B.Omega.x / V, -0.24558, 0.01);
		RB_CHECK(B.Velocity.z == 0.0 && B.Position.z == kR); // the cloth carries the downward nose force
		RB_CHECK(rb::Abs(B.Velocity.x) < 1e-12);
	}
}

RB_TEST(Integ_Compliant_CushionRestitutionLawAtFirstTouch)
{
	// The island freezes c_c from e_c(v_perp at first touch) of the element's law (WP-4 CushionRestitution, collisions 4.8):
	// a head-on hit on a nose at the centre height rebounds with about e_c(0.5) = 0.97 and e_c(3) = 0.90.
	rb::IslandFeature Edge;
	Edge.Kind = rb::IslandFeatureKind::EdgeLine;
	Edge.Point = {-1.0, 0.0, kR};
	Edge.Direction = {1.0, 0.0, 0.0};
	Edge.Normal = {0.0, -1.0, 0.0};
	Edge.Length = 2.0;
	Edge.Restitution = rb::CushionRestitutionLaw{0.97, 0.035, 1.0, 0.60};
	const double Speeds[2] = {0.5, 3.0};
	const double Expected[2] = {0.97, 0.90};
	for (int k = 0; k < 2; ++k)
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(), kCloth, 0.0, rb::NumericsConfig{});
		Island.AddFeature(Edge);
		Island.AddBody(Body(0, {0.0, -kR, kR}, {0.0, Speeds[k], 0.0}));
		RB_REQUIRE(RunToExit(Island, 100000) > 0);
		const double Ratio = -Island.Body(0).Velocity.y / Speeds[k];
		RB_CHECK(Ratio >= Expected[k] && Ratio <= Expected[k] + 0.03);
	}
}

RB_TEST(Compliant_BodyAndTipManagement)
{
	rb::CompliantIsland Island;
	Island.Reset(1.0, rb::CliMode::Compliant, PureCli(), BallModel(), kCloth, 0.0, rb::NumericsConfig{});
	RB_CHECK(Island.Time() == 1.0 && Island.StepCount() == 0 && Island.GapToIsland({0.0, 0.0, 0.0}, kR) == rb::kInfinity);
	RB_CHECK(Island.AddBody(Body(4, {0.0, 0.0, kR}, {0.1, 0.0, 0.0})));
	RB_CHECK(!Island.AddBody(Body(4, {1.0, 0.0, kR}))); // duplicate id
	RB_CHECK(!Island.AddBody(Body(rb::kMaxBalls, {1.0, 0.0, kR})));
	RB_CHECK(Island.AddBody(Body(9, {0.3, 0.0, kR})));
	RB_CHECK_NEAR(Island.GapToIsland({0.1, 0.0, kR}, kR), 0.1 - 2.0 * kR, 1e-15);
	RB_CHECK(Island.FindBody(9) == 1 && Island.FindBody(3) == -1);
	rb::IslandBody Out;
	RB_CHECK(!Island.RemoveBody(3, Out));
	RB_CHECK(Island.RemoveBody(4, Out) && Out.Ball == 4 && Out.Velocity.x == 0.1);
	RB_CHECK(Island.BodyCount() == 1 && Island.FindBody(9) == 0);
	RB_CHECK(Island.BodyAtRest(0) && !Island.BodyAtRest(1));

	// Tip: the path at the island time; RemoveTip returns the current state as a new analytic path.
	rb::IslandTip Tip;
	Tip.Path.Strike = 1;
	Tip.Path.StruckBall = 9;
	Tip.Path.Start = {-1.0, 0.0, kR};
	Tip.Path.Direction = {1.0, 0.0, 0.0};
	Tip.Path.Speed0 = 2.0;
	Tip.Path.Deceleration = 10.0;
	Tip.Path.StartTime = 0.9;
	Tip.Path.StopTime = 1.1;
	Tip.Path.DomeRadius = 0.0106;
	Tip.Mass = 0.54;
	Tip.Stiffness = 1e5;
	Tip.Restitution = 0.73;
	Tip.Friction = 0.6;
	RB_CHECK(!Island.SetTip(2, Tip));
	RB_CHECK(Island.SetTip(1, Tip) && Island.HasTip(1) && !Island.HasTip(0));
	rb::IslandRecordList Records;
	for (int s = 0; s < 1000; ++s)
	{
		Island.Step(Records);
	}
	rb::CueTipPath Path;
	RB_CHECK(Island.RemoveTip(1, Path) && !Island.HasTip(1));
	RB_CHECK(!Island.RemoveTip(1, Path));
	const double T = Island.Time();
	RB_CHECK_NEAR(Path.StartTime, T, 1e-15);
	RB_CHECK_NEAR(Path.Speed0, 2.0 - 10.0 * (T - 0.9), 1e-9);
	RB_CHECK_NEAR(Path.Start.x, -1.0 + 2.0 * (T - 0.9) - 5.0 * (T - 0.9) * (T - 0.9), 1e-6);
	RB_CHECK_NEAR(Path.StopTime, 1.1, 1e-9);
	RB_CHECK(Path.Strike == 1 && Path.StruckBall == 9 && Path.DomeRadius == 0.0106);
}

RB_TEST(Compliant_SustainedContactDetector)
{
	// A fast impact is never "sustained"; a resting ball pressed onto a support (gravity on a plane) is, after SustainedSteps.
	rb::CompliantIsland Impact;
	MakeChain(Impact, 2, 1.0, kAlpha095);
	rb::IslandRecordList Records;
	for (int s = 0; s < 400; ++s)
	{
		Impact.Step(Records);
		RB_REQUIRE(!Impact.SustainedContact());
	}
	rb::IslandFeature Plane;
	Plane.Kind = rb::IslandFeatureKind::Plane;
	Plane.Point = {0.0, 0.0, 0.0};
	Plane.Direction = {0.0, 0.0, 1.0};
	Plane.VertexCount = 4;
	Plane.Vertices[0] = {-1.0, -1.0};
	Plane.Vertices[1] = {1.0, -1.0};
	Plane.Vertices[2] = {1.0, 1.0};
	Plane.Vertices[3] = {-1.0, 1.0};
	Plane.Restitution = ConstantLaw(0.5);
	rb::CompliantIsland Resting;
	Resting.Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(), kCloth, kG, rb::NumericsConfig{});
	Resting.AddFeature(Plane);
	Resting.AddBody(Body(0, {0.0, 0.0, kR}));
	int Steps = 0;
	while (!Resting.SustainedContact() && Steps < 5000)
	{
		Resting.Step(Records);
		++Steps;
	}
	RB_CHECK(Resting.SustainedContact());
	RB_CHECK(Steps >= 200 && Steps < 2000);
	Resting.SetMode(rb::CliMode::Rigid);
	RB_CHECK(!Resting.SustainedContact()); // compliant-mode detector only
}

RB_TEST(Compliant_IdIndependenceWithFeaturesAndCloth)
{
	// A cluster on the cloth pressed into a cushion nose by a cue ball with running English, ids and insertion order permuted:
	// bit-identical states (geometric contact key incl. features, canonical pair roles, per-body support).
	const double H = 0.03629025;
	rb::IslandFeature Nose;
	Nose.Kind = rb::IslandFeatureKind::EdgeLine;
	Nose.SourceKind = 1;
	Nose.SourceIndex = 3;
	Nose.Point = {-1.0, 0.0, H};
	Nose.Direction = {1.0, 0.0, 0.0};
	Nose.Normal = {0.0, -1.0, 0.0};
	Nose.Length = 2.0;
	Nose.Restitution = ConstantLaw(0.9);
	Nose.Friction = 0.14;
	const double Rc = rb::Sqrt(kR * kR - (H - kR) * (H - kR));
	const rb::Vec3 Positions[4] = {{0.0, -Rc, kR}, {2.0 * kR, -Rc, kR}, {kR, -Rc - rb::Sqrt(3.0) * kR, kR}, {kR - 0.01, -Rc - rb::Sqrt(3.0) * kR - 0.1, kR}};
	const rb::Vec3 Velocities[4] = {{}, {}, {}, {0.1, 3.0, 0.0}};
	const rb::Vec3 Spins[4] = {{}, {}, {}, {-3.0 / kR, 0.0, 40.0}};
	auto Run = [&](const int* Ids, const int* Order, rb::CompliantIsland& Island)
	{
		Island.Reset(0.0, rb::CliMode::Compliant, TableCli(), BallModel(), kCloth, kG, rb::NumericsConfig{});
		Island.AddFeature(Nose);
		for (int k = 0; k < 4; ++k)
		{
			const int Slot = Order[k];
			Island.AddBody(Body(Ids[Slot], Positions[Slot], Velocities[Slot], Spins[Slot], true));
		}
		rb::IslandRecordList Records;
		for (int s = 0; s < 60000; ++s)
		{
			Records.Clear();
			Island.Step(Records);
			if (s == 40000)
			{
				Island.SetMode(rb::CliMode::Rigid);
			}
		}
	};
	const int IdsA[4] = {0, 1, 2, 3};
	const int OrderA[4] = {0, 1, 2, 3};
	const int IdsB[4] = {11, 3, 7, 0};
	const int OrderB[4] = {3, 1, 0, 2};
	rb::CompliantIsland A;
	rb::CompliantIsland B;
	Run(IdsA, OrderA, A);
	Run(IdsB, OrderB, B);
	bool Moved = false;
	for (int Slot = 0; Slot < 4; ++Slot)
	{
		const rb::IslandBody& X = A.Body(A.FindBody(IdsA[Slot]));
		const rb::IslandBody& Y = B.Body(B.FindBody(IdsB[Slot]));
		RB_CHECK(std::memcmp(&X.Position, &Y.Position, sizeof(rb::Vec3)) == 0);
		RB_CHECK(std::memcmp(&X.Velocity, &Y.Velocity, sizeof(rb::Vec3)) == 0);
		RB_CHECK(std::memcmp(&X.Omega, &Y.Omega, sizeof(rb::Vec3)) == 0);
		Moved = Moved || !(X.Position == Positions[Slot]);
	}
	RB_CHECK(Moved);
}

RB_TEST(Compliant_FullTableClusterCandidateList)
{
	// 22 balls in a tight hexagonal pack (a snooker-sized cluster) hit at 10 m/s: the candidate list holds every contact,
	// momentum is conserved and every ball leaves the pack.
	rb::CompliantIsland Island;
	Island.Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(), kCloth, 0.0, rb::NumericsConfig{});
	int Id = 0;
	for (int Row = 0; Row < 6 && Id < 21; ++Row)
	{
		for (int i = 0; i <= Row && Id < 21; ++i)
		{
			Island.AddBody(Body(Id++, {-Row * rb::Sqrt(3.0) * kR, (2.0 * i - Row) * kR, kR}));
		}
	}
	Island.AddBody(Body(Id, {2.0 * kR + 1e-5, 0.0, kR}, {-10.0, 0.0, 0.0}));
	RB_REQUIRE(Island.BodyCount() == 22);
	RB_REQUIRE(RunToExit(Island, 400000) > 0);
	rb::Vec3 Momentum;
	for (int i = 0; i < Island.BodyCount(); ++i)
	{
		Momentum += Island.Body(i).Velocity * kM;
		RB_CHECK(rb::Length(Island.Body(i).Velocity) > 0.0);
	}
	RB_CHECK(rb::Length(Momentum - rb::Vec3{-10.0 * kM, 0.0, 0.0}) <= 1e-12 * 10.0 * kM);
}

RB_TEST(Compliant_CandidateCapacityFallback)
{
	// More candidate pairs within the skin than kMaxIslandContacts: the list falls back to the pairs within LeaveDistance
	// (rebuilt every step) and the island still supports every ball: 20 balls in a row (1 mm gaps) 1.5 mm above 9 coincident
	// support planes (different table sources) = 199 candidates at the 2 mm skin, 180 contacts once they rest.
	rb::CompliantIsland Island;
	Island.Reset(0.0, rb::CliMode::Rigid, PureCli(), BallModel(), kCloth, kG, rb::NumericsConfig{});
	for (int k = 0; k < 9; ++k)
	{
		rb::IslandFeature Plane;
		Plane.Kind = rb::IslandFeatureKind::Plane;
		Plane.SourceKind = 5;
		Plane.SourceIndex = static_cast<std::uint8_t>(k);
		Plane.Point = {0.0, 0.0, 0.048};
		Plane.Direction = {0.0, 0.0, 1.0};
		Plane.VertexCount = 4;
		Plane.Vertices[0] = {-2.0, -1.0};
		Plane.Vertices[1] = {2.0, -1.0};
		Plane.Vertices[2] = {2.0, 1.0};
		Plane.Vertices[3] = {-2.0, 1.0};
		Plane.Restitution = ConstantLaw(0.0);
		Plane.Friction = 0.3;
		Plane.RollingResistance = 0.01;
		RB_REQUIRE(Island.AddFeature(Plane));
	}
	for (int i = 0; i < 20; ++i)
	{
		Island.AddBody(Body(i, {i * (2.0 * kR + 1e-3), 0.0, 0.048 + kR + 1.5e-3}));
	}
	rb::IslandRecordList Records;
	int Rested = 0;
	for (int s = 0; s < 2000; ++s)
	{
		Records.Clear();
		Island.Step(Records);
	}
	for (int i = 0; i < Island.BodyCount(); ++i)
	{
		const rb::IslandBody& B = Island.Body(i);
		RB_CHECK(B.Position.z > 0.048 + kR - 1e-6);
		Rested += Island.BodyAtRest(i) ? 1 : 0;
	}
	RB_CHECK(Rested == 20);
}

RB_TEST(Compliant_Slow_PressingPairBenchmark)
{
	// PERF note (Release): the D-12 pressing pair (CB with 10 rad/s topspin frozen to an OB): compliant until sustained, then
	// rigid until the contact opens (target of D-12b / Z-3 in WP-6b: < 1 ms per island).
	rb::CompliantIsland Island;
	Island.Reset(0.0, rb::CliMode::Compliant, TableCli(), BallModel(), kCloth, kG, rb::NumericsConfig{});
	Island.AddBody(Body(0, {0.0, 0.0, kR}, {}, {0.0, 10.0, 0.0}, true));
	Island.AddBody(Body(1, {2.0 * kR, 0.0, kR}, {}, {}, true));
	rb::IslandRecordList Records;
	const auto Start = std::chrono::steady_clock::now();
	int Compliant = 0;
	while (!Island.SustainedContact() && Compliant < 50000)
	{
		Island.Step(Records);
		++Compliant;
	}
	Island.SetMode(rb::CliMode::Rigid);
	int Rigid = 0;
	while (!Island.CanExit() && Rigid < 100000)
	{
		Island.Step(Records);
		++Rigid;
	}
	const double Micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - Start).count();
	std::printf("  pressing pair: %d compliant + %d rigid steps (%.1f ms simulated), %.1f us CPU\n", Compliant, Rigid, Island.Time() * 1e3, Micros);
	RB_CHECK(Island.CanExit());
}

RB_TEST(Compliant_Slow_BreakBenchmark)
{
	// PERF note (Release): the CL-7 break (16 balls, 8 m/s, zero gaps) in the compliant island.
	rb::CompliantIsland Island;
	Island.Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(), kCloth, 0.0, rb::NumericsConfig{});
	Island.AddBody(Body(0, {2.0 * kR, 0.0, kR}, {-8.0, 0.0, 0.0}));
	int Slot = 1;
	for (int Row = 0; Row < 5; ++Row)
	{
		for (int i = 0; i <= Row; ++i)
		{
			Island.AddBody(Body(Slot++, {-Row * rb::Sqrt(3.0) * kR, (2.0 * i - Row) * kR, kR}));
		}
	}
	const auto Start = std::chrono::steady_clock::now();
	const int Steps = RunToExit(Island, 200000);
	const double Micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - Start).count();
	std::printf("  break: %d steps (%.3f ms simulated), %.1f us CPU, %.0f ns per step\n", Steps, Island.Time() * 1e3, Micros, 1e3 * Micros / Steps);
	RB_CHECK(Steps > 0);
}
