// WP-3: architecture tests of the compliant island (Docs/architecture.md 8.8, 8.11, 17: A-CLI-1 ... A-CLI-5).
#include "rbtest.h"

#include "CompliantTestUtil.h"

#include "rb/Physics/CueStrike.h"

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

	double PairGap(const rb::CompliantIsland& Island, int BallA, int BallB)
	{
		const rb::IslandBody& A = Island.Body(Island.FindBody(BallA));
		const rb::IslandBody& B = Island.Body(Island.FindBody(BallB));
		return rb::Length(B.Position - A.Position) - (A.Radius + B.Radius);
	}

	// Rolling contact of a ball on the cloth: slip speed |v + R (z x w)| (horizontal).
	double ClothSlip(const rb::IslandBody& B)
	{
		const rb::Vec3 U = rb::SlipVelocity(B.Velocity, B.Omega, B.Radius);
		return rb::Sqrt(U.x * U.x + U.y * U.y);
	}

	// Two balls head-on with no friction / cloth: restitution for Tsuji alpha Alpha at 1 m/s (the table's reference).
	double HeadOnRestitution(double Alpha)
	{
		rb::CompliantIsland Island;
		MakeChain(Island, 2, 1.0, Alpha);
		RunToExit(Island, 100000);
		return VelocityOf(Island, 1) - VelocityOf(Island, 0);
	}

	double BisectAlpha(double Restitution)
	{
		double Lo = 0.0;
		double Hi = 0.6;
		for (int k = 0; k < 40; ++k)
		{
			const double Mid = 0.5 * (Lo + Hi);
			if (HeadOnRestitution(Mid) > Restitution)
			{
				Lo = Mid;
			}
			else
			{
				Hi = Mid;
			}
		}
		return 0.5 * (Lo + Hi);
	}

	// Cushion nose line along x at y = 0 (the table on the -y side), height h, as a rail feature of the island.
	rb::IslandFeature NoseLine(double Height, std::uint8_t Source = 0)
	{
		rb::IslandFeature F;
		F.Kind = rb::IslandFeatureKind::EdgeLine;
		F.SourceKind = 1;
		F.SourceIndex = Source;
		F.Point = {-1.0, 0.0, Height};
		F.Direction = {1.0, 0.0, 0.0};
		F.Normal = {0.0, -1.0, 0.0};
		F.Length = 2.0;
		F.Restitution = rb::CushionRestitutionLaw{0.9, 0.0, 1.0, 0.9}; // constant e = 0.9
		F.Friction = 0.14;
		return F;
	}
}

RB_TEST(ARCH_CLI1_PressingPairSwitchesToRigidAndEnds)
{
	// D-12 set-up: CB at rest with pure topspin w = (0, 10, 0) rad/s frozen to an OB at rest; the slip drives the CB into the
	// OB (pressing contact). The island must detect the sustained contact, switch to Rigid, and end (contacts open or rest)
	// within a bounded number of steps without interpenetration beyond the Hertz overlap.
	rb::CompliantIsland Island;
	Island.Reset(0.0, rb::CliMode::Compliant, TableCli(), BallModel(0.95), kCloth, kG, rb::NumericsConfig{});
	Island.AddBody(Body(0, {0.0, 0.0, kR}, {}, {0.0, 10.0, 0.0}, true));
	Island.AddBody(Body(1, {2.0 * kR, 0.0, kR}, {}, {}, true));
	rb::IslandRecordList Records;
	int BallBallRecords = 0;
	double MaxOverlap = 0.0;
	int CompliantSteps = 0;
	const rb::NumericsConfig Numerics;
	const int CompliantLimit = static_cast<int>(Numerics.CompliantMaxDuration / 1e-6);
	for (; CompliantSteps < CompliantLimit && !Island.SustainedContact(); ++CompliantSteps)
	{
		Records.Clear();
		Island.Step(Records);
		BallBallRecords += Records.Size();
		MaxOverlap = rb::Max(MaxOverlap, -PairGap(Island, 0, 1));
		RB_REQUIRE(!Island.CanExit());
	}
	RB_CHECK(Island.SustainedContact()); // detected long before the 50 ms cap
	RB_CHECK(CompliantSteps < 2000);
	RB_CHECK(BallBallRecords == 1);
	Island.SetMode(rb::CliMode::Rigid);
	RB_CHECK(Island.Mode() == rb::CliMode::Rigid);
	int RigidSteps = 0;
	bool Ended = false;
	for (; RigidSteps < 20000 && !Ended; ++RigidSteps)
	{
		Records.Clear();
		Island.Step(Records);
		BallBallRecords += Records.Size();
		MaxOverlap = rb::Max(MaxOverlap, -PairGap(Island, 0, 1));
		Ended = Island.CanExit() || (Island.BodyAtRest(0) && Island.BodyAtRest(1));
	}
	RB_CHECK(Ended);
	RB_CHECK(RigidSteps < 5000); // ~0.05 s until the CB rolls, then the contact opens
	RB_CHECK(BallBallRecords == 1); // one sustained contact, no chatter records
	RB_CHECK(MaxOverlap < 1e-6);    // Hertz static overlap ~0.1 um; rigid projection keeps the gap at 0
	// The pair leaves together, both rolling forward.
	const rb::IslandBody& Cue = Island.Body(Island.FindBody(0));
	const rb::IslandBody& Object = Island.Body(Island.FindBody(1));
	RB_CHECK(Cue.Velocity.x > 0.02 && Cue.Velocity.x < 0.05);
	RB_CHECK_NEAR(Cue.Velocity.x, Object.Velocity.x, 1e-6);
	RB_CHECK(Object.Velocity.x >= Cue.Velocity.x - 1e-9); // not approaching
	RB_CHECK(ClothSlip(Cue) < 1e-9 && ClothSlip(Object) < 1e-9);
	RB_CHECK(PairGap(Island, 0, 1) >= -rb::NumericsConfig{}.ContactTol);
}

RB_TEST(ARCH_CLI2_TsujiTableMatchesBisection)
{
	// alpha_T(e) of the table vs a bisection with the island integrator itself (2 balls head-on at 1 m/s) to 5e-5, at the
	// named restitutions and between table nodes; the spec's values are 0.03689 (0.95) and 0.05242 (0.93) for its integrator.
	for (double E : {0.93, 0.95, 0.98, 0.935, 0.955, 0.925})
	{
		RB_CHECK_NEAR(rb::TsujiAlphaForRestitution(E), BisectAlpha(E), 5e-5);
	}
	RB_CHECK_NEAR(rb::TsujiAlphaForRestitution(0.95), 0.03689, 5e-5);
	RB_CHECK_NEAR(rb::TsujiAlphaForRestitution(0.93), 0.05242, 5e-5);
	RB_CHECK(rb::TsujiAlphaForRestitution(1.0) == 0.0);
	RB_CHECK(rb::TsujiAlphaForRestitution(1.2) == 0.0);
	RB_CHECK(rb::TsujiAlphaForRestitution(0.2) == rb::TsujiAlphaForRestitution(0.5));
	// Monotone decreasing over the whole range.
	double Previous = rb::TsujiAlphaForRestitution(0.5);
	for (int k = 1; k <= 500; ++k)
	{
		const double Alpha = rb::TsujiAlphaForRestitution(0.5 + 0.001 * k);
		RB_CHECK(Alpha < Previous);
		Previous = Alpha;
	}
}

namespace
{
	struct TipRun
	{
		double Duration = 0.0; // TipBegin -> TipEnd [s]
		double BallSpeed = 0.0;
		int Begins = 0;
		int Ends = 0;
	};

	// Free ball at rest (no cloth, no gravity) hit through its centre by a tip participant at V.
	TipRun RunTip(double V, double TipRestitution, double ContactTime)
	{
		const rb::CueSpec Cue = rb::kCuePlaying19oz;
		rb::CompliantIsland Island;
		Island.Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(0.95), kCloth, 0.0, rb::NumericsConfig{});
		Island.AddBody(Body(3, {0.0, 0.0, kR}));
		rb::IslandTip Tip;
		Tip.Path.Strike = 0;
		Tip.Path.StruckBall = 3;
		Tip.Path.Start = {-(kR + Cue.TipDomeRadius), 0.0, kR};
		Tip.Path.Direction = {1.0, 0.0, 0.0};
		Tip.Path.Speed0 = V;
		Tip.Path.Deceleration = 0.0;
		Tip.Path.StartTime = 0.0;
		Tip.Path.StopTime = rb::kInfinity;
		Tip.Path.DomeRadius = Cue.TipDomeRadius;
		Tip.Mass = Cue.Mass;
		Tip.Stiffness = rb::CueTipContactStiffness(ContactTime, kM, Cue.Mass);
		Tip.Restitution = TipRestitution;
		Tip.Friction = Cue.TipFriction;
		Island.SetTip(0, Tip);
		rb::IslandRecordList Records;
		TipRun Run;
		double Begin = -1.0;
		for (int s = 0; s < 20000 && Run.Ends == 0; ++s)
		{
			Records.Clear();
			Island.Step(Records);
			for (const rb::IslandContactRecord& R : Records)
			{
				RB_CHECK(R.BallA == 3 && R.Strike == 0);
				if (R.Kind == rb::IslandRecordKind::TipBegin)
				{
					++Run.Begins;
					Begin = R.Time;
				}
				else if (R.Kind == rb::IslandRecordKind::TipEnd)
				{
					++Run.Ends;
					Run.Duration = R.Time - Begin;
				}
			}
		}
		Run.BallSpeed = Island.Body(0).Velocity.x;
		return Run;
	}
}

RB_TEST(ARCH_CLI3_TipParticipantDurationAndImpulse)
{
	const rb::CueSpec Cue = rb::kCuePlaying19oz;
	const double V = 2.0;
	// Undamped tip: the contact lasts the half period pi sqrt(m_eff / k) = ContactTime (one step of discretisation).
	const TipRun Elastic = RunTip(V, 1.0, Cue.ContactTime);
	RB_CHECK(Elastic.Begins == 1 && Elastic.Ends == 1);
	RB_CHECK_NEAR(Elastic.Duration, Cue.ContactTime, 2e-6);
	RB_CHECK_NEAR(Elastic.BallSpeed, 2.0 * V / (1.0 + kM / Cue.Mass), 1e-3 * V);
	// Damped leather tip (e_tip 0.73): the clipped dashpot releases a little early (~7 % for zeta = 0.11), and the impulse is
	// the rigid-body (1 + e) V / (1/M + 1/m) of MOT B.5 within 2 % (the clipped dashpot's e is slightly above e_tip).
	const TipRun Leather = RunTip(V, Cue.TipRestitution, Cue.ContactTime);
	RB_CHECK(Leather.Begins == 1 && Leather.Ends == 1);
	RB_CHECK_NEAR(Leather.Duration, Cue.ContactTime, 0.1 * Cue.ContactTime);
	const double Impulse = (1.0 + Cue.TipRestitution) * V / (1.0 / Cue.Mass + 1.0 / kM);
	RB_CHECK_NEAR(Leather.BallSpeed * kM, Impulse, 0.02 * Impulse);
	RB_CHECK(Leather.BallSpeed * kM >= Impulse);
}

RB_TEST(Integ_ARCH_CLI3_TipImpulseMatchesStrikeCueBall)
{
	// The island tip gives the same impulse as the event-mode strike (WP-1 StrikeCueBall, centre hit, no squirt) within 2 %.
	const rb::CueSpec Cue = rb::kCuePlaying19oz;
	rb::CueStrikeInput Input;
	Input.Speed = 2.0;
	Input.Cue = Cue;
	Input.SquirtEnabled = false;
	const rb::BallSpec Spec = rb::MakeBallSpec(kR, kM);
	rb::BallState Ball;
	Ball.Position = {0.0, 0.0, kR};
	const rb::StrikeResult Strike = rb::StrikeCueBall(Input, Ball, Spec, kCloth, rb::SlateParams{}, rb::PinchParams{}, kG, rb::NumericsConfig{});
	RB_REQUIRE(Strike.Error == rb::ErrorCode::Ok);
	const TipRun Run = RunTip(2.0, Cue.TipRestitution, Cue.ContactTime);
	RB_CHECK_NEAR(Run.BallSpeed * kM, Strike.Impulse, 0.02 * Strike.Impulse);
}

RB_TEST(ARCH_CLI4_BallOnSlopedPlaneRollsOffOverEdge)
{
	// Sloped cushion top (COL 6.2): from the nose line (y = 0, z = h) back to the cushion back (y = 50.8 mm, z = 48 mm); the
	// table is on the -y side. A ball placed on the slope rolls down (rigid mode, as rail-top islands start), rolls over the
	// nose EdgeLine and leaves toward the table.
	const double H = 0.03629025;
	const double Back = 0.0508;
	const double Top = 0.048;
	const rb::Vec3 Normal = rb::Normalized(rb::Vec3{0.0, -(Top - H), Back});
	rb::IslandFeature Plane;
	Plane.Kind = rb::IslandFeatureKind::Plane;
	Plane.SourceKind = 2;
	Plane.Point = {0.0, 0.0, H};
	Plane.Direction = Normal;
	Plane.VertexCount = 4;
	Plane.Vertices[0] = {-0.5, 0.0};
	Plane.Vertices[1] = {0.5, 0.0};
	Plane.Vertices[2] = {0.5, Back};
	Plane.Vertices[3] = {-0.5, Back};
	Plane.Restitution = rb::CushionRestitutionLaw{0.5, 0.0, 1.0, 0.5};
	Plane.Friction = 0.3;
	Plane.RollingResistance = 0.010;
	Plane.SpinDeceleration = 10.0;
	const rb::IslandFeature Nose = NoseLine(H, 1);

	rb::CompliantIsland Island;
	Island.Reset(0.0, rb::CliMode::Rigid, TableCli(), BallModel(0.95), kCloth, kG, rb::NumericsConfig{});
	RB_REQUIRE(Island.AddFeature(Plane));
	RB_REQUIRE(Island.AddFeature(Nose));
	RB_CHECK(Island.AddFeature(Nose)); // same source: ignored
	RB_CHECK(Island.FeatureCount() == 2);
	const rb::Vec3 Contact{0.0, 0.03, H + 0.03 * (Top - H) / Back};
	Island.AddBody(Body(5, Contact + Normal * kR, {}, {}, false));
	rb::IslandRecordList Records;
	bool TouchedPlane = false;
	bool TouchedNose = false;
	int Steps = 0;
	for (; Steps < 20000; ++Steps)
	{
		Records.Clear();
		Island.Step(Records);
		for (const rb::IslandContactRecord& R : Records)
		{
			RB_CHECK(R.Kind == rb::IslandRecordKind::BallFeature && R.BallA == 5);
			TouchedPlane = TouchedPlane || R.Feature == 0;
			TouchedNose = TouchedNose || R.Feature == 1;
		}
		if (Island.CanExit())
		{
			break;
		}
	}
	RB_CHECK(Steps < 20000);
	RB_CHECK(TouchedPlane && TouchedNose);
	const rb::IslandBody& B = Island.Body(0);
	RB_CHECK(B.Position.y < 0.0);  // over the table side of the nose line
	RB_CHECK(B.Velocity.y < -0.1); // moving toward the table
	RB_CHECK(B.Velocity.z < 0.0);  // falling toward the cloth
	RB_CHECK(rb::Length(B.Position - rb::Vec3{B.Position.x, 0.0, H}) >= kR - 1e-9); // clear of the nose edge
	RB_CHECK(rb::Abs(B.Position.x) < 1e-9 && rb::Abs(B.Velocity.x) < 1e-9); // straight down the slope
}

RB_TEST(ARCH_CLI5_TiltedIsland)
{
	const rb::NumericsConfig Numerics;
	const rb::Vec2 Gt{0.004 * kG, -0.003 * kG}; // |s| = 5 mm/m = 0.5 mu_r
	// (a) g_t acts on every body: two free bodies (no support) in both modes.
	for (rb::CliMode Mode : {rb::CliMode::Compliant, rb::CliMode::Rigid})
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, Mode, PureCli(), BallModel(0.95), kCloth, kG, Numerics);
		Island.SetInPlaneGravity(Gt);
		RB_CHECK(Island.InPlaneGravityAcceleration() == Gt);
		Island.AddBody(Body(0, {0.0, 0.0, 0.5}, {0.3, 0.0, 0.0}));
		Island.AddBody(Body(1, {0.5, 0.2, 0.5}, {}, {1.0, 2.0, 3.0}));
		rb::IslandRecordList Records;
		const int N = 1000;
		for (int s = 0; s < N; ++s)
		{
			Island.Step(Records);
		}
		const double T = Island.Time();
		RB_CHECK_NEAR(Island.Body(0).Velocity.x, 0.3 + Gt.x * T, 1e-12);
		RB_CHECK_NEAR(Island.Body(0).Velocity.y, Gt.y * T, 1e-12);
		RB_CHECK_NEAR(Island.Body(1).Velocity.x, Gt.x * T, 1e-12);
		RB_CHECK_NEAR(Island.Body(1).Velocity.z, -kG * T, 1e-12);
	}
	// (b) A body at rest on the cloth stays exactly at rest within the validity rule |s| <= 0.7 mu_r (both modes).
	for (rb::CliMode Mode : {rb::CliMode::Compliant, rb::CliMode::Rigid})
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, Mode, TableCli(), BallModel(0.95), kCloth, kG, Numerics);
		Island.SetInPlaneGravity(rb::Vec2{0.7 * kCloth.RollingResistance * kG, 0.0});
		const rb::Vec3 Start{0.1, -0.2, kR};
		Island.AddBody(Body(0, Start, {}, {}, true));
		rb::IslandRecordList Records;
		for (int s = 0; s < 3000; ++s)
		{
			Island.Step(Records);
			RB_REQUIRE(Island.BodyAtRest(0));
		}
		RB_CHECK(Island.Body(0).Position == Start);
		RB_CHECK(Island.CanExit());
	}
	// (c) A rolling ball decelerates at mu_r g - g_t/(1 + k) along the slope drive (support model = event-mode rolling law).
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, rb::CliMode::Compliant, TableCli(), BallModel(0.95), kCloth, kG, Numerics);
		const rb::Vec2 Down{0.5 * kCloth.RollingResistance * kG, 0.0};
		Island.SetInPlaneGravity(Down);
		Island.AddBody(Body(0, {0.0, 0.0, kR}, {0.5, 0.0, 0.0}, {0.0, 0.5 / kR, 0.0}, true));
		rb::IslandRecordList Records;
		for (int s = 0; s < 20000; ++s)
		{
			Island.Step(Records);
		}
		const double Expected = 0.5 + (Down.x / 1.4 - kCloth.RollingResistance * kG) * Island.Time();
		RB_CHECK_NEAR(Island.Body(0).Velocity.x, Expected, 1e-9);
		RB_CHECK(ClothSlip(Island.Body(0)) < 1e-12);
	}
	// (d) A ball pushed against a cushion nose by the slope (1 mm/s into it, g_t toward the cushion) comes to rest there and
	// the island exits. Rigid (the sustained phase): the micro-impact is inelastic, the ball rests at the nose and the island
	// ends on rest. Compliant: the contact bounces (e = 0.9) and the island exits as soon as it opens, with the ball creeping
	// away so slowly that event mode stops it within microns (v^2 / 2 (mu_r g - |g_t| / (1 + k))). A ball already at rest
	// against the nose stays there and the island exits after ExitZeroForceSteps.
	const double NoseH = 0.03629025;
	const double Rc = rb::Sqrt(kR * kR - (NoseH - kR) * (NoseH - kR)); // horizontal centre-to-nose distance at contact
	const rb::Vec2 IntoCushion{0.0, 0.5 * kCloth.RollingResistance * kG};
	auto NoseGap = [&](const rb::IslandBody& B) { return rb::Sqrt(B.Position.y * B.Position.y + rb::Square(B.Position.z - NoseH)) - kR; };
	for (rb::CliMode Mode : {rb::CliMode::Rigid, rb::CliMode::Compliant})
	{
		for (double Approach : {1e-3, 0.0})
		{
			rb::CompliantIsland Island;
			Island.Reset(0.0, Mode, TableCli(), BallModel(0.95), kCloth, kG, Numerics);
			Island.SetInPlaneGravity(IntoCushion);
			Island.AddFeature(NoseLine(NoseH));
			Island.AddBody(Body(0, {0.0, -Rc, kR}, {0.0, Approach, 0.0}, {-Approach / kR, 0.0, 0.0}, true));
			rb::IslandRecordList Records;
			bool Exit = false;
			bool Rest = false;
			int Steps = 0;
			for (; Steps < 200000 && !Exit && !Rest; ++Steps)
			{
				Records.Clear();
				Island.Step(Records);
				Exit = Island.CanExit();
				Rest = Island.BodyAtRest(0);
			}
			const rb::IslandBody& B = Island.Body(0);
			RB_CHECK(Exit || Rest);
			RB_CHECK(NoseGap(B) > -1e-6 && NoseGap(B) < 1e-6);
			if (Mode == rb::CliMode::Rigid || Approach == 0.0)
			{
				RB_CHECK(Rest);
				RB_CHECK(rb::Length(B.Velocity) == 0.0);
				RB_CHECK(Steps < 200);
			}
			else
			{
				RB_CHECK(Exit);
				RB_CHECK(B.Velocity.y < 0.0 && B.Velocity.y > -Approach); // separating, slower than it came
				const double StopDistance = rb::Square(B.Velocity.y) / (2.0 * (kCloth.RollingResistance * kG - IntoCushion.y / 1.4));
				RB_CHECK(StopDistance < 1e-5);
			}
		}
	}
	// (e) SetInPlaneGravity not called, or called with (0, 0) (also -0): bitwise the level island, incl. signed zeros.
	{
		rb::CompliantIsland Plain;
		rb::CompliantIsland Zero;
		rb::CompliantIsland NegativeZero;
		rb::CompliantIsland Tilted;
		rb::CompliantIsland* All[4] = {&Plain, &Zero, &NegativeZero, &Tilted};
		for (rb::CompliantIsland* Island : All)
		{
			Island->Reset(0.0, rb::CliMode::Compliant, TableCli(), BallModel(0.95), kCloth, kG, Numerics);
			Island->AddFeature(NoseLine(0.03629025));
			Island->AddBody(Body(0, {0.0, -0.1, kR}, {0.0, 1.2, -0.0}, {-0.0, 0.0, 30.0}, true));
			Island->AddBody(Body(1, {0.02, -0.2, kR}, {-0.0, 2.0, 0.0}, {-2.0 / kR, -0.0, -0.0}, true));
		}
		Zero.SetInPlaneGravity(rb::Vec2{0.0, 0.0});
		NegativeZero.SetInPlaneGravity(rb::Vec2{-0.0, -0.0});
		Tilted.SetInPlaneGravity(rb::Vec2{0.0, 1e-3 * kG});
		rb::IslandRecordList Records;
		for (int s = 0; s < 150000; ++s)
		{
			for (rb::CompliantIsland* Island : All)
			{
				Records.Clear();
				Island->Step(Records);
			}
			if (s == 70000)
			{
				for (rb::CompliantIsland* Island : All)
				{
					Island->SetMode(rb::CliMode::Rigid);
				}
			}
		}
		for (int i = 0; i < 2; ++i)
		{
			for (const rb::CompliantIsland* Island : {&Zero, &NegativeZero})
			{
				RB_CHECK(std::memcmp(&Plain.Body(i).Position, &Island->Body(i).Position, sizeof(rb::Vec3)) == 0);
				RB_CHECK(std::memcmp(&Plain.Body(i).Velocity, &Island->Body(i).Velocity, sizeof(rb::Vec3)) == 0);
				RB_CHECK(std::memcmp(&Plain.Body(i).Omega, &Island->Body(i).Omega, sizeof(rb::Vec3)) == 0);
			}
			RB_CHECK(!(Plain.Body(i).Position == Tilted.Body(i).Position));
		}
	}
}
