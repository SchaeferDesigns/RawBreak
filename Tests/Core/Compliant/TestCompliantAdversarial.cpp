// WP-3 review: adversarial tests of the compliant / rigid island that the spec does not list (conservation laws, exact mirror
// symmetry, bitwise determinism, degenerate islands, energy in rigid mode, tip momentum, exit of resting clusters), and the
// regression tests of the review fixes (pair-role swap at a rebuild, tip records with a full record list, first touch inside
// the touching band in Rigid mode, no heap allocation).
#include "rbtest.h"

#include "CompliantTestUtil.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/Cue.h"
#include "rb/Math/Quat.h"

#include <cstring>
#include <limits>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace rbcl;

namespace
{
	bool SameBits(const rb::Vec3& A, const rb::Vec3& B) { return std::memcmp(&A, &B, sizeof(rb::Vec3)) == 0; }

	rb::Vec3 Mirror(const rb::Vec3& V) { return rb::Vec3{V.x, -V.y, V.z}; }
	rb::Vec3 MirrorSpin(const rb::Vec3& W) { return rb::Vec3{-W.x, W.y, -W.z}; }

	struct Totals
	{
		rb::Vec3 Momentum;
		rb::Vec3 AngularMomentum; // about the origin
		double Energy = 0.0;
		double LinearScale = 0.0;
		double AngularScale = 0.0;
	};

	Totals Measure(const rb::CompliantIsland& Island)
	{
		Totals T;
		for (int i = 0; i < Island.BodyCount(); ++i)
		{
			const rb::IslandBody& B = Island.Body(i);
			T.Momentum += B.Velocity * B.Mass;
			T.AngularMomentum += rb::Cross(B.Position, B.Velocity) * B.Mass + B.Omega * B.Inertia;
			T.Energy += 0.5 * B.Mass * rb::LengthSquared(B.Velocity) + 0.5 * B.Inertia * rb::LengthSquared(B.Omega);
			T.LinearScale += B.Mass * rb::Length(B.Velocity);
			T.AngularScale += B.Mass * rb::Length(rb::Cross(B.Position, B.Velocity)) + B.Inertia * rb::Length(B.Omega);
		}
		return T;
	}

	// A random cluster of Count balls: each new ball touches (gap 0-20 um) a random earlier one; all move toward the cluster's
	// first ball and spin. Cloth: the cluster lies on the cloth (z = R, horizontal normals).
	void RandomCluster(rb::Rng& G, int Count, rb::IslandBody* Out, bool Cloth)
	{
		const rb::Vec3 Origin{G.NextUniform(-0.05, 0.05), G.NextUniform(-0.05, 0.05), kR};
		int Made = 0;
		while (Made < Count)
		{
			rb::Vec3 P = Origin;
			if (Made > 0)
			{
				const int Anchor = static_cast<int>(G.NextUniform(0.0, Made - 1e-9));
				rb::Vec3 Dir{G.NextNormal(), G.NextNormal(), Cloth ? 0.0 : 0.3 * G.NextNormal()};
				Dir = rb::Normalized(Dir);
				P = Out[Anchor].Position + Dir * (2.0 * kR + G.NextUniform(0.0, 20e-6));
			}
			bool Clear = true;
			for (int k = 0; k < Made; ++k)
			{
				Clear = Clear && rb::Length(P - Out[k].Position) >= 2.0 * kR;
			}
			if (!Clear)
			{
				continue;
			}
			rb::Vec3 V{G.NextUniform(-0.5, 0.5), G.NextUniform(-0.5, 0.5), 0.0};
			const rb::Vec3 W{G.NextUniform(-100.0, 100.0), G.NextUniform(-100.0, 100.0), G.NextUniform(-100.0, 100.0)};
			if (!Cloth)
			{
				V.z = G.NextUniform(-0.2, 0.2);
			}
			if (Made > 0)
			{
				V = V + rb::Normalized(Origin - P) * G.NextUniform(0.5, 3.0); // into the cluster
			}
			Out[Made] = Body(Made, P, V, W, Cloth);
			++Made;
		}
	}
}

RB_TEST(Compliant_Adv_RandomClustersConserveMomentumAndDissipate)
{
	// Free clusters (no cloth, no gravity) with Hertz + Tsuji + frozen Coulomb friction: linear momentum and angular momentum
	// about the origin are conserved (the lever arms meet at the overlap midpoint, so every pair force has zero net moment; the
	// semi-implicit Euler step conserves r x p exactly), and the kinetic energy at the exit is not above the start.
	rb::Rng G(0xC1A5ull);
	double WorstP = 0.0;
	double WorstL = 0.0;
	double WorstE = -1.0;
	int Contacts = 0;
	for (int Case = 0; Case < 12; ++Case)
	{
		rb::IslandBody Balls[6];
		RandomCluster(G, 6, Balls, false);
		rb::CompliantIsland Island;
		Island.Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(G.NextUniform(0.9, 0.98)), kCloth, 0.0, rb::NumericsConfig{});
		for (const rb::IslandBody& B : Balls)
		{
			Island.AddBody(B);
		}
		const Totals T0 = Measure(Island);
		int Records = 0;
		RunToExit(Island, 150000, nullptr, &Records);
		Contacts += Records;
		const Totals T1 = Measure(Island);
		WorstP = rb::Max(WorstP, rb::Length(T1.Momentum - T0.Momentum) / T0.LinearScale);
		WorstL = rb::Max(WorstL, rb::Length(T1.AngularMomentum - T0.AngularMomentum) / T0.AngularScale);
		WorstE = rb::Max(WorstE, (T1.Energy - T0.Energy) / T0.Energy);
	}
	RB_CHECK(Contacts > 20);
	RB_CHECK(WorstP <= 1e-12);
	RB_CHECK(WorstL <= 1e-10);
	RB_CHECK(WorstE <= 0.0);
}

RB_TEST(Compliant_Adv_MirrorImageIsBitExact)
{
	// y -> -y of a whole asymmetric scene (bodies on the cloth with gravity, the tilt, a cushion nose on each side, compliant then
	// rigid): every output bit mirrors. The (x, |y|, z, y) key order is its own mirror image and every force commutes with the
	// sign flip, so this holds for any configuration, not only the symmetric break of CL-7.
	rb::Rng G(0x5EC7ull);
	for (int Case = 0; Case < 4; ++Case)
	{
		rb::IslandBody Balls[5];
		RandomCluster(G, 5, Balls, true);
		const rb::Vec2 Tilt{G.NextUniform(-0.03, 0.03), G.NextUniform(-0.03, 0.03)};
		rb::IslandFeature Near;
		Near.Kind = rb::IslandFeatureKind::EdgeLine;
		Near.SourceKind = 1;
		Near.SourceIndex = 2;
		Near.Point = {-1.0, 0.2, 0.03629025};
		Near.Direction = {1.0, 0.0, 0.0};
		Near.Normal = {0.0, -1.0, 0.0};
		Near.Length = 2.0;
		Near.Restitution = rb::CushionRestitutionLaw{0.9, 0.0, 1.0, 0.9};
		Near.Friction = 0.14;
		rb::IslandFeature Far = Near;
		Far.SourceIndex = 5;
		Far.Point = {-1.0, -0.21, 0.03629025};
		Far.Normal = {0.0, 1.0, 0.0};
		rb::CompliantIsland Island;
		rb::CompliantIsland Image;
		rb::CompliantIsland Again;
		for (rb::CompliantIsland* I : {&Island, &Image, &Again})
		{
			const bool Mirrored = I == &Image;
			I->Reset(0.0, rb::CliMode::Compliant, PureCli(rb::TsujiAlphaForRestitution(0.95)), BallModel(), kCloth, kG, rb::NumericsConfig{});
			I->SetInPlaneGravity(Mirrored ? rb::Vec2{Tilt.x, -Tilt.y} : Tilt);
			for (const rb::IslandFeature& F : {Near, Far})
			{
				rb::IslandFeature M = F;
				if (Mirrored)
				{
					M.Point = Mirror(F.Point);
					M.Normal = Mirror(F.Normal);
				}
				I->AddFeature(M);
			}
			for (const rb::IslandBody& B : Balls)
			{
				rb::IslandBody M = B;
				M.ClothSupport = true;
				if (Mirrored)
				{
					M.Position = Mirror(B.Position);
					M.Velocity = Mirror(B.Velocity);
					M.Omega = MirrorSpin(B.Omega);
				}
				I->AddBody(M);
			}
		}
		rb::IslandRecordList Records;
		for (int s = 0; s < 30000; ++s)
		{
			for (rb::CompliantIsland* I : {&Island, &Image, &Again})
			{
				Records.Clear();
				I->Step(Records);
				if (s == 20000)
				{
					I->SetMode(rb::CliMode::Rigid);
				}
			}
		}
		for (int i = 0; i < 5; ++i)
		{
			const rb::IslandBody& A = Island.Body(i);
			const rb::IslandBody& M = Image.Body(i);
			const rb::IslandBody& R = Again.Body(i);
			RB_CHECK(SameBits(Mirror(A.Position), M.Position) && SameBits(Mirror(A.Velocity), M.Velocity) && SameBits(MirrorSpin(A.Omega), M.Omega));
			RB_CHECK(SameBits(A.Position, R.Position) && SameBits(A.Velocity, R.Velocity) && SameBits(A.Omega, R.Omega)); // run twice
		}
	}
}

RB_TEST(Compliant_Adv_RigidModeEnergyNeverGrows)
{
	// Rigid mode (sequential impulses with Newton restitution, exact Coulomb friction, position projection) on free clusters:
	// the kinetic energy never grows from one step to the next, and the linear momentum is conserved (equal and opposite
	// impulses; the projection moves positions only).
	rb::Rng G(0x51D1ull);
	double WorstStep = -1.0;
	double WorstMomentum = 0.0;
	for (int Case = 0; Case < 12; ++Case)
	{
		rb::IslandBody Balls[6];
		RandomCluster(G, 6, Balls, false);
		rb::CompliantIsland Island;
		Island.Reset(0.0, rb::CliMode::Rigid, PureCli(), BallModel(G.NextUniform(0.0, 1.0)), kCloth, 0.0, rb::NumericsConfig{});
		for (const rb::IslandBody& B : Balls)
		{
			Island.AddBody(B);
		}
		rb::IslandRecordList Records;
		const Totals Start = Measure(Island);
		double Previous = Start.Energy;
		for (int s = 0; s < 3000; ++s)
		{
			Records.Clear();
			Island.Step(Records);
			const Totals Now = Measure(Island);
			WorstStep = rb::Max(WorstStep, (Now.Energy - Previous) / Previous);
			WorstMomentum = rb::Max(WorstMomentum, rb::Length(Now.Momentum - Start.Momentum) / Start.LinearScale);
			Previous = Now.Energy;
		}
	}
	RB_CHECK(WorstStep <= 1e-12);
	RB_CHECK(WorstMomentum <= 1e-12);
}

RB_TEST(Compliant_Adv_TipAndBallShareMomentum)
{
	// A free tip participant (no hand deceleration) and a free ball: M V + m v along d is conserved through the contact in both
	// modes (the tip's reaction is the contact force along d).
	const rb::CueSpec Cue = rb::kCuePlaying19oz;
	for (rb::CliMode Mode : {rb::CliMode::Compliant, rb::CliMode::Rigid})
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, Mode, PureCli(), BallModel(), kCloth, 0.0, rb::NumericsConfig{});
		Island.AddBody(Body(2, {0.0, 0.0, kR}));
		rb::IslandTip Tip;
		Tip.Path.Strike = 0;
		Tip.Path.StruckBall = 2;
		Tip.Path.Start = {-(kR + Cue.TipDomeRadius) - 1e-5, 0.0, kR};
		Tip.Path.Direction = {1.0, 0.0, 0.0};
		Tip.Path.Speed0 = 3.0;
		Tip.Path.StopTime = rb::kInfinity;
		Tip.Path.DomeRadius = Cue.TipDomeRadius;
		Tip.Mass = Cue.Mass;
		Tip.Stiffness = rb::CueTipContactStiffness(Cue.ContactTime, kM, Cue.Mass);
		Tip.Restitution = Cue.TipRestitution;
		Tip.Friction = Cue.TipFriction;
		RB_REQUIRE(Island.SetTip(0, Tip));
		rb::IslandRecordList Records;
		int Begins = 0;
		int Ends = 0;
		for (int s = 0; s < 20000 && Ends == 0; ++s)
		{
			Records.Clear();
			Island.Step(Records);
			for (const rb::IslandContactRecord& R : Records)
			{
				Begins += R.Kind == rb::IslandRecordKind::TipBegin ? 1 : 0;
				Ends += R.Kind == rb::IslandRecordKind::TipEnd ? 1 : 0;
			}
		}
		RB_CHECK(Begins == 1 && Ends == 1);
		rb::CueTipPath After;
		RB_REQUIRE(Island.RemoveTip(0, After));
		const double Before = Cue.Mass * 3.0;
		const double Total = Cue.Mass * After.Speed0 + kM * Island.Body(0).Velocity.x;
		RB_CHECK_NEAR(Total, Before, 1e-12 * Before);
		RB_CHECK(Island.Body(0).Velocity.x > After.Speed0); // the ball leaves the tip
		const double E0 = 0.5 * Cue.Mass * 9.0;
		const double E1 = 0.5 * Cue.Mass * After.Speed0 * After.Speed0 + 0.5 * kM * rb::LengthSquared(Island.Body(0).Velocity);
		RB_CHECK(E1 <= E0);
	}
}

RB_TEST(Compliant_Adv_DegenerateIslands)
{
	// An empty island steps and exits; a lone resting ball on the cloth exits after ExitZeroForceSteps; a racked cluster at rest
	// (exactly touching, no force) on a level table exits after ExitZeroForceSteps and nothing moves.
	const rb::NumericsConfig Numerics;
	rb::CliParams Table = PureCli(rb::TsujiAlphaForRestitution(0.95));
	Table.ClothSupport = true;
	{
		rb::CompliantIsland Empty;
		Empty.Reset(2.0, rb::CliMode::Compliant, Table, BallModel(), kCloth, kG, Numerics);
		RB_CHECK(RunToExit(Empty, 100) == Table.ExitZeroForceSteps);
		RB_CHECK_NEAR(Empty.Time(), 2.0 + Table.ExitZeroForceSteps * Table.TimeStep, 1e-15);
		RB_CHECK(Empty.GapToIsland({}, kR) == rb::kInfinity);
		RB_CHECK(!Empty.SustainedContact());
	}
	for (rb::CliMode Mode : {rb::CliMode::Compliant, rb::CliMode::Rigid})
	{
		rb::CompliantIsland Rack;
		Rack.Reset(0.0, Mode, Table, BallModel(), kCloth, kG, Numerics);
		rb::Vec3 Start[15];
		int Id = 0;
		for (int Row = 0; Row < 5; ++Row)
		{
			for (int i = 0; i <= Row; ++i)
			{
				Start[Id] = {-Row * rb::Sqrt(3.0) * kR, (2.0 * i - Row) * kR, kR};
				Rack.AddBody(Body(Id, Start[Id], {}, {}, true));
				++Id;
			}
		}
		RB_CHECK(RunToExit(Rack, 100) == Table.ExitZeroForceSteps);
		for (int i = 0; i < 15; ++i)
		{
			RB_CHECK(rb::Length(Rack.Body(i).Position - Start[i]) <= 1e-15); // rounding-level overlaps of the lattice only
			RB_CHECK(Rack.BodyAtRest(i));
		}
	}
}

RB_TEST(Compliant_Adv_ExtremeImpactSpeeds)
{
	// 0.01 m/s and 20 m/s head-on hits (5 balls, no friction): finite, momentum exact, no tunnelling (the balls end in order),
	// and the island still exits.
	for (double V0 : {0.01, 20.0})
	{
		rb::CompliantIsland Island;
		MakeChain(Island, 5, V0, kAlpha095);
		RB_REQUIRE(RunToExit(Island, 400000) > 0);
		double Momentum = 0.0;
		for (int i = 0; i < 5; ++i)
		{
			Momentum += VelocityOf(Island, i);
			RB_CHECK(rb::IsFinite(VelocityOf(Island, i)));
			if (i > 0)
			{
				RB_CHECK(Island.Body(Island.FindBody(i)).Position.x - Island.Body(Island.FindBody(i - 1)).Position.x >= 2.0 * kR - 1e-9);
			}
		}
		RB_CHECK_NEAR(Momentum, V0, 1e-12 * V0);
	}
}

namespace
{
	bool SameBodies(const rb::CompliantIsland& A, const rb::CompliantIsland& B)
	{
		if (A.BodyCount() != B.BodyCount())
		{
			return false;
		}
		for (int i = 0; i < A.BodyCount(); ++i)
		{
			const rb::IslandBody& P = A.Body(i);
			const rb::IslandBody& Q = B.Body(i);
			if (!SameBits(P.Position, Q.Position) || !SameBits(P.Velocity, Q.Velocity) || !SameBits(P.Omega, Q.Omega))
			{
				return false;
			}
		}
		return true;
	}

	// A table feature far away from every body: adding it only forces a candidate rebuild at the next step.
	rb::IslandFeature FarFeature()
	{
		rb::IslandFeature F;
		F.Kind = rb::IslandFeatureKind::EdgeLine;
		F.SourceKind = 1;
		F.SourceIndex = 17;
		F.Point = {10.0, 10.0, 0.03629025};
		F.Direction = {1.0, 0.0, 0.0};
		F.Normal = {0.0, -1.0, 0.0};
		F.Length = 1.0;
		return F;
	}

	// 24 balls (kMaxBalls) in a triangular lattice of 4 rows x 6, spacing Spacing, ids 0..23 row by row, each moving toward the
	// pack centre at Converge times its offset. Returns the centre of ball 0 (row 0, column 0: the -x / -y corner).
	rb::Vec3 AddLattice(rb::CompliantIsland& Island, double Spacing, double Converge)
	{
		rb::Vec3 Centre;
		rb::Vec3 Positions[24];
		for (int Row = 0; Row < 4; ++Row)
		{
			for (int Col = 0; Col < 6; ++Col)
			{
				const rb::Vec3 P{Col * Spacing + (Row % 2 == 1 ? 0.5 * Spacing : 0.0), Row * Spacing * 0.5 * rb::Sqrt(3.0), kR};
				Positions[Row * 6 + Col] = P;
				Centre += P * (1.0 / 24.0);
			}
		}
		for (int i = 0; i < 24; ++i)
		{
			Island.AddBody(Body(i, Positions[i], (Centre - Positions[i]) * Converge));
		}
		return Positions[0];
	}

	// A free tip (no deceleration) overlapping the ball at BallCentre by 1 um from the -x side and moving into it at 1 m/s.
	rb::IslandTip OverlappingTip(const rb::Vec3& BallCentre, int StruckBall)
	{
		const rb::CueSpec Cue = rb::kCuePlaying19oz;
		rb::IslandTip Tip;
		Tip.Path.Strike = 0;
		Tip.Path.StruckBall = static_cast<rb::BallId>(StruckBall);
		Tip.Path.Start = BallCentre - rb::Vec3{kR + Cue.TipDomeRadius - 1e-6, 0.0, 0.0};
		Tip.Path.Direction = {1.0, 0.0, 0.0};
		Tip.Path.Speed0 = 1.0;
		Tip.Path.StopTime = rb::kInfinity;
		Tip.Path.DomeRadius = Cue.TipDomeRadius;
		Tip.Mass = Cue.Mass;
		Tip.Stiffness = rb::CueTipContactStiffness(Cue.ContactTime, kM, Cue.Mass);
		Tip.Restitution = Cue.TipRestitution;
		Tip.Friction = Cue.TipFriction;
		return Tip;
	}

	struct RecordTally
	{
		int Begins = 0;
		int Ends = 0;
		bool EndWithoutBegin = false;
		int PairRecords = 0;
	};

	void Tally(const rb::IslandRecordList& Records, RecordTally& T)
	{
		for (const rb::IslandContactRecord& R : Records)
		{
			if (R.Kind == rb::IslandRecordKind::TipBegin)
			{
				++T.Begins;
			}
			else if (R.Kind == rb::IslandRecordKind::TipEnd)
			{
				T.EndWithoutBegin = T.EndWithoutBegin || T.Ends >= T.Begins;
				++T.Ends;
			}
			else
			{
				++T.PairRecords;
			}
		}
	}
}

RB_TEST(Compliant_Adv_RebuildWithSwappedPairRolesIsInvisible)
{
	// Review fix: a CB whose spin about x makes its cloth slip drive it along +y into an OB beside it (rigid sustained contact),
	// and whose side spin drags the OB along -x past the CB's x by contact friction: the lexicographic order of the centres
	// changes, so the canonical roles of the pair (A = smaller centre) swap at the next candidate rebuild. The warm-start tangential
	// impulse is stored as the impulse on A and must change sign with the roles, so that a rebuild never changes the result:
	// island X is forced to rebuild right after the crossing (a far-away feature is added), island Y is not; both stay bitwise
	// identical to the end. Before the fix the carried warm start pushed the wrong way and X diverged at once.
	const double Dx = 2e-5;
	rb::CliParams Table = PureCli(rb::TsujiAlphaForRestitution(0.95));
	Table.ClothSupport = true;
	rb::CompliantIsland X;
	rb::CompliantIsland Y;
	for (rb::CompliantIsland* I : {&X, &Y})
	{
		I->Reset(0.0, rb::CliMode::Rigid, Table, BallModel(0.95), kCloth, kG, rb::NumericsConfig{});
		I->AddBody(Body(0, {0.0, 0.0, kR}, {}, {-10.0, 0.0, 30.0}, true));
		I->AddBody(Body(1, {Dx, rb::Sqrt(4.0 * kR * kR - Dx * Dx), kR}, {}, {}, true));
	}
	rb::IslandRecordList Records;
	int ForcedAt = -1;
	int Steps = 0;
	bool Same = true;
	for (; Steps < 20000 && Same && !Y.CanExit(); ++Steps)
	{
		for (rb::CompliantIsland* I : {&X, &Y})
		{
			Records.Clear();
			I->Step(Records);
		}
		Same = SameBodies(X, Y);
		if (ForcedAt < 0 && X.Body(1).Position.x < X.Body(0).Position.x - 1e-7)
		{
			RB_CHECK(X.AddFeature(FarFeature()));
			ForcedAt = Steps;
		}
	}
	RB_REQUIRE(ForcedAt > 0);
	RB_CHECK(Same);
	RB_CHECK(Steps > ForcedAt + 500); // the pressed contact lasted well beyond the forced rebuild
	RB_CHECK(X.Body(1).Position.x < X.Body(0).Position.x - 1e-5);
}

RB_TEST(Compliant_Adv_TipRecordsSurviveAFullRecordList)
{
	// Review fix: a tip transition that did not fit into the step's record list was lost (a TipEnd without its TipBegin, or
	// the reverse), while pair records stay armed and are retried. Now (a) pair records leave room for the tip records of the
	// step, and (b) a tip transition that still does not fit is detected again at the next step, so Begin / End stay paired.
	// (a) 24 balls pressed together (2 um overlaps; rigid: also converging) give more first-touch records in the first step than
	// a list holds, while the tip touches ball 0 in the same step.
	for (rb::CliMode Mode : {rb::CliMode::Compliant, rb::CliMode::Rigid})
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, Mode, PureCli(), BallModel(0.95), kCloth, 0.0, rb::NumericsConfig{});
		const rb::Vec3 Corner = AddLattice(Island, 2.0 * kR - 2e-6, Mode == rb::CliMode::Rigid ? 0.5 : 0.0);
		RB_REQUIRE(Island.SetTip(0, OverlappingTip(Corner, 0)));
		rb::IslandRecordList Records;
		RecordTally T;
		int FirstStepPairs = -1;
		bool FirstStepBegin = false;
		for (int s = 0; s < 5000 && T.Ends == 0; ++s)
		{
			Records.Clear();
			Island.Step(Records);
			const int Before = T.Begins;
			Tally(Records, T);
			if (s == 0)
			{
				FirstStepPairs = T.PairRecords;
				FirstStepBegin = T.Begins > Before;
			}
		}
		RB_CHECK(FirstStepPairs >= 20 && FirstStepPairs < rb::kMaxIslandRecordsPerStep); // the list was the bottleneck
		RB_CHECK(T.PairRecords > rb::kMaxIslandRecordsPerStep);                         // the rest came in later steps
		RB_CHECK(FirstStepBegin);
		RB_CHECK(T.Begins == 1 && T.Ends == 1 && !T.EndWithoutBegin);
	}
	// (b) The caller's list is full on every odd step (nothing can be appended there): every tip transition and every pair
	// record is delivered at a following step instead (compliant contacts last many steps); none is lost.
	{
		rb::CompliantIsland Reference;
		rb::CompliantIsland Starved;
		for (rb::CompliantIsland* I : {&Reference, &Starved})
		{
			I->Reset(0.0, rb::CliMode::Compliant, PureCli(), BallModel(0.95), kCloth, 0.0, rb::NumericsConfig{});
			I->AddBody(Body(0, {0.0, 0.0, kR}));
			I->AddBody(Body(1, {2.0 * kR, 0.0, kR}));
			I->AddBody(Body(2, {4.0 * kR + 5e-6, 0.0, kR}));
			RB_REQUIRE(I->SetTip(0, OverlappingTip({0.0, 0.0, kR}, 0)));
		}
		rb::IslandRecordList Full;
		while (Full.PushBack(rb::IslandContactRecord{}))
		{
		}
		RecordTally R;
		RecordTally S;
		rb::IslandRecordList Records;
		for (int s = 0; s < 5000; ++s)
		{
			Records.Clear();
			Reference.Step(Records);
			Tally(Records, R);
			if (s % 2 == 1)
			{
				rb::IslandRecordList Blocked = Full;
				Starved.Step(Blocked);
				RB_CHECK(Blocked.Size() == rb::kMaxIslandRecordsPerStep);
			}
			else
			{
				Records.Clear();
				Starved.Step(Records);
				Tally(Records, S);
			}
		}
		RB_CHECK(R.Begins == 1 && R.Ends == 1 && !R.EndWithoutBegin);
		RB_CHECK(S.Begins == R.Begins && S.Ends == R.Ends && !S.EndWithoutBegin);
		RB_CHECK(R.PairRecords == 2 && S.PairRecords == R.PairRecords);
	}
}

namespace
{
	// Cushion nose line along x at y = 0 (table on the -y side) at h = 0.635 D, constant e = 0.9, mu_w = 0.14.
	rb::IslandFeature RailNose()
	{
		rb::IslandFeature F;
		F.Kind = rb::IslandFeatureKind::EdgeLine;
		F.SourceKind = 1;
		F.SourceIndex = 0;
		F.Point = {-1.0, 0.0, 0.03629025};
		F.Direction = {1.0, 0.0, 0.0};
		F.Normal = {0.0, -1.0, 0.0};
		F.Length = 2.0;
		F.Restitution = rb::CushionRestitutionLaw{0.9, 0.0, 1.0, 0.9};
		F.Friction = 0.14;
		return F;
	}

	// Runs an island (switching Compliant -> Rigid on a sustained contact) until it can exit or its bodies rest.
	int RunSettled(rb::CompliantIsland& Island, int MaxSteps)
	{
		rb::IslandRecordList Records;
		for (int s = 1; s <= MaxSteps; ++s)
		{
			Records.Clear();
			Island.Step(Records);
			if (Island.SustainedContact())
			{
				Island.SetMode(rb::CliMode::Rigid);
			}
			bool Rest = true;
			for (int i = 0; i < Island.BodyCount(); ++i)
			{
				Rest = Rest && Island.BodyAtRest(i);
			}
			if (Island.CanExit() || Rest)
			{
				return s;
			}
		}
		return -1;
	}
}

RB_TEST(Compliant_Adv_RigidFirstTouchFreezesFriction)
{
	// Review fix: in Rigid mode a pair that ended a step within the touching band without having been in that step's solve was
	// marked "in contact" without freezing its friction and restitution, so it kept mu = 0 and e = 0 for as long as it stayed
	// touching. Rail-top islands start Rigid, and a rigid island's resting neighbours start that way: a ball with running English
	// slip-pressed along a rail it exactly touches (Z-3 set-up) slid along a frictionless rail. Now the rail friction acts from
	// the first touch, whether the island starts Rigid with the ball touching (gap 0 or inside the band), Rigid with a tiny
	// overlap (in the solve at once), or Compliant (switching to Rigid on the sustained contact): all agree.
	const double NoseH = 0.03629025;
	const double Rc = rb::Sqrt(kR * kR - (NoseH - kR) * (NoseH - kR)); // horizontal centre-to-nose distance at contact
	rb::CliParams Table = PureCli(rb::TsujiAlphaForRestitution(0.95));
	Table.ClothSupport = true;
	auto RailRun = [&](rb::CliMode Mode, double Offset)
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, Mode, Table, BallModel(0.95), kCloth, kG, rb::NumericsConfig{});
		Island.AddFeature(RailNose());
		Island.AddBody(Body(0, {0.0, -Rc - Offset, kR}, {0.5, 0.0, 0.0}, {-20.0, 0.5 / kR, 0.0}, true));
		RB_CHECK(RunSettled(Island, 100000) > 0);
		return Island.Body(0).Velocity.x;
	};
	const double Reference = RailRun(rb::CliMode::Compliant, 0.0);
	RB_CHECK(Reference < 0.49); // the rail friction took ~5 % of the speed while the slip pressed the ball into it
	for (double Offset : {0.0, 5e-10, -1e-10})
	{
		RB_CHECK_NEAR(RailRun(rb::CliMode::Rigid, Offset), Reference, 1e-4);
	}
	// Ball-ball: the D-12 pressing pair (CB with topspin frozen to an OB, gap inside the band) started directly in Rigid mode
	// leaves at the speed of the Compliant -> Rigid run (ball-ball friction frozen at the first touch).
	auto PairRun = [&](rb::CliMode Mode)
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, Mode, Table, BallModel(0.95), kCloth, kG, rb::NumericsConfig{});
		Island.AddBody(Body(0, {0.0, 0.0, kR}, {}, {0.0, 10.0, 0.0}, true));
		Island.AddBody(Body(1, {2.0 * kR + 1e-12, 0.0, kR}, {}, {}, true));
		RB_CHECK(RunSettled(Island, 100000) > 0);
		return Island.Body(1).Velocity.x;
	};
	RB_CHECK_NEAR(PairRun(rb::CliMode::Rigid), PairRun(rb::CliMode::Compliant), 1e-4);
}

RB_TEST(Compliant_Adv_NoHeapAllocationInHotPaths)
{
	// Code rule: nothing in the event loop allocates. Every WP-3 entry point the loop calls (ball-ball impulse, cling weights,
	// island reset / add / step in both modes incl. candidate rebuilds, tips, features and the capacity fallback, exit tests,
	// removals) runs under the MSVC debug heap's allocation counter, which must not move (Debug builds; elsewhere the scenario
	// still runs as a smoke test).
#if defined(_MSC_VER) && defined(_DEBUG)
	_CrtMemState Before;
	_CrtMemCheckpoint(&Before);
#endif
	const rb::NumericsConfig Numerics;
	double Sink = 0.0;
	{
		rb::ImpactBody A;
		A.Position = {0.0, 0.0, kR};
		A.Velocity = {2.0, 0.1, 0.0};
		A.Omega = {3.0, 40.0, -20.0};
		A.Radius = kR;
		A.Mass = kM;
		A.Inertia = kI;
		rb::ImpactBody B = A;
		B.Position = {2.0 * kR * rb::Cos(0.4), 2.0 * kR * rb::Sin(0.4), kR};
		B.Velocity = {};
		B.Omega = {};
		for (int k = 0; k < 100; ++k)
		{
			Sink += rb::ResolveBallBall(A, B, BallModel(0.95), Numerics.RestSpeed, Numerics.EpsV).NormalImpulse;
		}
		rb::BallChalkMarks Marks;
		rb::ChalkMark Mark;
		Mark.BodyDir = {1.0, 0.0, 0.0};
		Mark.Strength = 0.8;
		Mark.Radius = 2.5e-3;
		Marks.PushBack(Mark);
		Sink += rb::ContactClingFactor(rb::ChalkMarkWeight(Marks, rb::Quat::Identity(), kR, {1.0, 0.1, 0.0}), 0.0, BallModel(0.95));
	}
	rb::CliParams Table = PureCli(rb::TsujiAlphaForRestitution(0.95));
	Table.ClothSupport = true;
	for (rb::CliMode Mode : {rb::CliMode::Compliant, rb::CliMode::Rigid})
	{
		rb::CompliantIsland Island;
		Island.Reset(0.0, Mode, Table, BallModel(0.95), kCloth, kG, Numerics);
		Island.SetInPlaneGravity(rb::Vec2{1e-3 * kG, 0.0});
		Island.AddFeature(RailNose());
		const rb::Vec3 Corner = AddLattice(Island, 2.0 * kR - 2e-6, 0.5);
		Island.SetTip(0, OverlappingTip(Corner, 0));
		rb::IslandRecordList Records;
		for (int s = 0; s < 300; ++s)
		{
			Records.Clear();
			Island.Step(Records);
			Sink += Island.CanExit() ? 1.0 : 0.0;
			Sink += Island.SustainedContact() ? 1.0 : 0.0;
			Sink += Island.GapToIsland({1.0, 1.0, kR}, kR);
			if (s == 150)
			{
				Island.SetMode(Mode == rb::CliMode::Rigid ? rb::CliMode::Compliant : rb::CliMode::Rigid);
				rb::IslandBody Out;
				Island.RemoveBody(7, Out);
				Island.AddBody(Out);
				Island.AddFeature(FarFeature());
			}
		}
		rb::CueTipPath Path;
		Island.RemoveTip(0, Path);
		Sink += Path.Speed0;
	}
#if defined(_MSC_VER) && defined(_DEBUG)
	_CrtMemState After;
	_CrtMemCheckpoint(&After);
	RB_CHECK(After.lTotalCount == Before.lTotalCount);
	RB_CHECK(After.lCounts[_NORMAL_BLOCK] == Before.lCounts[_NORMAL_BLOCK]);
#endif
	RB_CHECK(rb::IsFinite(Sink));
}

RB_TEST(Compliant_Adv_IdPermutationBitIdenticalBothModes)
{
	// CL-8 beyond the symmetric rack: random clusters on the tilted cloth against a cushion nose, started Compliant (switching
	// to Rigid half-way) or Rigid, with the ball ids and the insertion order permuted: bit-identical bodies after mapping the
	// ids back (geometric contact order, canonical pair roles, per-body support contacts, id-free warm starts).
	rb::Rng G(0x1D5EEDull);
	const int Ids[6] = {17, 4, 9, 0, 23, 11};
	const int Order[6] = {5, 2, 4, 0, 3, 1};
	rb::CliParams Table = PureCli(rb::TsujiAlphaForRestitution(0.95));
	Table.ClothSupport = true;
	for (int Case = 0; Case < 6; ++Case)
	{
		rb::IslandBody Balls[6];
		RandomCluster(G, 6, Balls, true);
		const rb::Vec2 Tilt{G.NextUniform(-0.03, 0.03), G.NextUniform(-0.03, 0.03)};
		rb::IslandFeature Near = RailNose();
		Near.Point.y = Balls[0].Position.y + 0.08;
		const rb::CliMode Start = Case % 2 == 0 ? rb::CliMode::Compliant : rb::CliMode::Rigid;
		rb::CompliantIsland A;
		rb::CompliantIsland B;
		for (int Variant = 0; Variant < 2; ++Variant)
		{
			rb::CompliantIsland& I = Variant == 0 ? A : B;
			I.Reset(0.0, Start, Table, BallModel(), kCloth, kG, rb::NumericsConfig{});
			I.SetInPlaneGravity(Tilt);
			I.AddFeature(Near);
			for (int k = 0; k < 6; ++k)
			{
				const int Slot = Variant == 0 ? k : Order[k];
				rb::IslandBody Body = Balls[Slot];
				Body.Ball = Variant == 0 ? Slot : Ids[Slot];
				Body.ClothSupport = true;
				I.AddBody(Body);
			}
		}
		rb::IslandRecordList Records;
		int RecordsA = 0;
		int RecordsB = 0;
		for (int s = 0; s < 8000; ++s)
		{
			Records.Clear();
			A.Step(Records);
			RecordsA += Records.Size();
			Records.Clear();
			B.Step(Records);
			RecordsB += Records.Size();
			if (s == 4000 && Start == rb::CliMode::Compliant)
			{
				A.SetMode(rb::CliMode::Rigid);
				B.SetMode(rb::CliMode::Rigid);
			}
		}
		RB_CHECK(RecordsA == RecordsB && RecordsA > 0);
		for (int Slot = 0; Slot < 6; ++Slot)
		{
			const rb::IslandBody& X = A.Body(A.FindBody(Slot));
			const rb::IslandBody& Y = B.Body(B.FindBody(Ids[Slot]));
			RB_CHECK(SameBits(X.Position, Y.Position) && SameBits(X.Velocity, Y.Velocity) && SameBits(X.Omega, Y.Omega));
		}
	}
}

RB_TEST(Compliant_Adv_NonFiniteBodyIsIsolated)
{
	// A corrupt (NaN / Inf) body far from a cluster: no crash (the candidate list and the sort never see a non-finite key), the
	// other bodies evolve bit-identically to the island without it, and the island never reports an exit it cannot justify.
	const double Nan = std::numeric_limits<double>::quiet_NaN();
	for (rb::CliMode Mode : {rb::CliMode::Compliant, rb::CliMode::Rigid})
	{
		for (int Kind = 0; Kind < 3; ++Kind)
		{
			rb::CompliantIsland Clean;
			rb::CompliantIsland Corrupt;
			for (rb::CompliantIsland* I : {&Clean, &Corrupt})
			{
				I->Reset(0.0, Mode, PureCli(), BallModel(0.95), kCloth, 0.0, rb::NumericsConfig{});
				I->AddFeature(RailNose());
				I->AddBody(Body(0, {0.0, -0.1, kR}, {0.0, 1.0, 0.0}, {5.0, 0.0, 20.0}));
				I->AddBody(Body(1, {2.0 * kR, -0.1, kR}));
			}
			rb::IslandBody Bad = Body(2, {0.5, -0.5, kR}, {1.0, 0.0, 0.0});
			if (Kind == 0)
			{
				Bad.Velocity.x = Nan;
			}
			else if (Kind == 1)
			{
				Bad.Position.y = Nan;
			}
			else
			{
				Bad.Omega.z = rb::kInfinity;
				Bad.Velocity.y = -rb::kInfinity;
			}
			RB_REQUIRE(Corrupt.AddBody(Bad));
			rb::IslandRecordList Records;
			for (int s = 0; s < 4000; ++s)
			{
				Records.Clear();
				Clean.Step(Records);
				Records.Clear();
				Corrupt.Step(Records);
			}
			for (int i = 0; i < 2; ++i)
			{
				const rb::IslandBody& X = Clean.Body(i);
				const rb::IslandBody& Y = Corrupt.Body(i);
				RB_CHECK(SameBits(X.Position, Y.Position) && SameBits(X.Velocity, Y.Velocity) && SameBits(X.Omega, Y.Omega));
			}
			RB_CHECK(!Corrupt.CanExit());
		}
	}
}
