// WP-6b: the island hooks of Private/rb/Physics/SimInternal.h driven directly on a hand-built workspace (architecture 8.8, 8.11):
// membership (BFS over balls within delta_cl), mode, in-plane gravity, stepping up to the next event, merge, Zeno reset, budget,
// exit, Sampled recording. Standalone: only WP-6b's own bookkeeping and the WP-3 solver are checked (the loop's services are WP-6a's).
#include "rbtest.h"

#include "SimWorkspaceTestUtil.h"

#include "rb/Physics/Compliant.h"

using namespace isltest;

namespace
{
	// D-12: CB with pure topspin frozen to an OB, at the table center (no table feature within reach).
	std::unique_ptr<Scene> PressingScene()
	{
		std::unique_ptr<Scene> S = MakeScene();
		Place(*S, 0, {0.0, 0.0, kR}, {}, {0.0, 10.0, 0.0});
		Place(*S, 1, {2.0 * kR, 0.0, kR});
		return S;
	}

	rb::sim::IslandSeed PairSeed(int A, int B, bool Pressing = false)
	{
		rb::sim::IslandSeed Seed;
		Seed.BallA = A;
		Seed.BallB = B;
		Seed.Pressing = Pressing;
		return Seed;
	}
}

RB_TEST(Islands_StartIslandTakesTheMembersOutOfEventMode)
{
	std::unique_ptr<Scene> S = PressingScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
	const std::uint32_t V0 = Ws->Balls[0].Version;
	const std::uint32_t V1 = Ws->Balls[1].Version;
	rb::sim::StartIsland(*Ws, PairSeed(0, 1, true), 0.0);
	const rb::sim::IslandState& I = Ws->Island;
	RB_REQUIRE(I.Active);
	RB_CHECK(I.StartTime == 0.0 && I.RigidSince < 0.0);
	RB_CHECK(I.Solver.Mode() == rb::CliMode::Compliant);
	RB_CHECK(I.Solver.BodyCount() == 2);
	RB_CHECK(I.Solver.FeatureCount() == 0); // nothing of the table within reach at its center
	RB_CHECK(Ws->Balls[0].InIsland && Ws->Balls[1].InIsland);
	RB_CHECK(Ws->Balls[0].Version != V0 && Ws->Balls[1].Version != V1); // queued predictions are stale
	RB_CHECK(S->Result.Diagnostics.Islands == 1);
	RB_CHECK_NEAR(rb::sim::NextIslandStepTime(*Ws), S->Input.Params.Cli.TimeStep, 0.0);
	// The bodies start from the balls' event states, spec per ball, on the cloth.
	const rb::IslandBody& Cue = I.Solver.Body(I.Solver.FindBody(0));
	RB_CHECK(Cue.Position == Vec3(0.0, 0.0, kR));
	RB_CHECK(Cue.Omega == Vec3(0.0, 10.0, 0.0));
	RB_CHECK(Cue.ClothSupport);
	RB_CHECK(Cue.Mass == kM && Cue.Radius == kR);
	// Tracks: the analytic segment is closed at the island start and a Sampled piece is open.
	const std::vector<rb::TrajectorySegment>& Track = S->Result.Tracks[0].Segments;
	RB_REQUIRE(Track.size() == 2);
	RB_CHECK(Track[0].T1 == 0.0);
	RB_CHECK(Track[1].Kind == rb::SegmentKind::Sampled && Track[1].Motion.T0 == 0.0 && Track[1].T1 == rb::kInfinity);
}

RB_TEST(Islands_AdvanceStepsUpToTheNextEventAndEndsOnExit)
{
	std::unique_ptr<Scene> S = PressingScene();
	std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
	rb::sim::StartIsland(*Ws, PairSeed(0, 1, true), 0.0);
	// Up to (not beyond) the event time: the island stops at the last step end <= UntilTime.
	const double Until = 1e-4 + 0.5e-6;
	RB_CHECK(rb::sim::AdvanceIsland(*Ws, Until));
	RB_CHECK(Ws->Island.Active);
	RB_CHECK(Ws->Island.Solver.Time() <= Until && rb::sim::NextIslandStepTime(*Ws) > Until);
	RB_CHECK(Ws->IslandStepsUsed == Ws->Island.Solver.StepCount());
	// Without a pending event it runs to its exit: sustained push -> Rigid, then the pair separates.
	RB_CHECK(rb::sim::AdvanceIsland(*Ws, rb::kInfinity));
	RB_CHECK(!Ws->Island.Active);
	RB_CHECK(!Ws->Balls[0].InIsland && !Ws->Balls[1].InIsland);
	RB_CHECK(S->Result.Diagnostics.IslandRigidSwitches == 1);
	RB_CHECK(S->Result.Diagnostics.IslandSteps == Ws->IslandStepsUsed);
	RB_CHECK(Ws->IslandStepsUsed > 200 && Ws->IslandStepsUsed < 20000);
	// Adaptive Sampled recording: far fewer pieces than steps, contiguous, each at most SampleMaxInterval long.
	for (int b = 0; b < 2; ++b)
	{
		const std::vector<rb::TrajectorySegment>& Track = S->Result.Tracks[b].Segments;
		RB_REQUIRE(Track.size() >= 2);
		int Sampled = 0;
		for (std::size_t k = 1; k < Track.size(); ++k)
		{
			if (Track[k].Kind != rb::SegmentKind::Sampled)
			{
				continue;
			}
			++Sampled;
			RB_CHECK(Track[k - 1].T1 == Track[k].Motion.T0);
			if (Track[k].T1 < rb::kInfinity)
			{
				RB_CHECK(Track[k].T1 - Track[k].Motion.T0 <= S->Input.Params.Numerics.SampleMaxInterval + 1e-12);
			}
		}
		RB_CHECK(Sampled >= 1 && Sampled < Ws->IslandStepsUsed / 20);
	}
}

RB_TEST(Islands_BfsJoinsBallsWithinTheJoinDistanceOnly)
{
	// CB 1 m/s into OB1 (delta_cl = 0.40 mm at 1 m/s, collisions 3.9.2): OB2 0.1 mm behind OB1 joins, OB3 5 mm further does not;
	// a ball far away stays in event mode.
	std::unique_ptr<Scene> S = MakeScene();
	Place(*S, 0, {-2.0 * kR, 0.0, kR}, {1.0, 0.0, 0.0});
	Place(*S, 1, {0.0, 0.0, kR});
	Place(*S, 2, {2.0 * kR + 1e-4, 0.0, kR});
	Place(*S, 3, {4.0 * kR + 1e-4 + 5e-3, 0.0, kR});
	Place(*S, 4, {-0.5, 0.3, kR});
	std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
	const double Delta = rb::IslandJoinDistance(1.0, 0.5 * kM, S->Input.Params.Cli, S->Input.Params.Numerics.ContactTol);
	RB_CHECK(Delta > 1e-4 && Delta < 5e-3);
	rb::sim::StartIsland(*Ws, PairSeed(0, 1), 0.0);
	RB_CHECK(Ws->Island.Solver.BodyCount() == 3);
	RB_CHECK(Ws->Balls[2].InIsland);
	RB_CHECK(!Ws->Balls[3].InIsland && !Ws->Balls[4].InIsland);
	// A second seed during the active island merges into it (one island at a time, architecture 8.8).
	rb::sim::StartIsland(*Ws, PairSeed(3, 4), 0.0);
	RB_CHECK(Ws->Island.Solver.BodyCount() == 5);
	RB_CHECK(S->Result.Diagnostics.Islands == 1);
}

RB_TEST(Islands_ZenoHistoriesOfTheMembersAreCleared)
{
	// Re-entry guard (architecture 8.8): the Zeno histories of member pairs and member-feature slots are cleared at island start.
	std::unique_ptr<Scene> S = PressingScene();
	Place(*S, 2, {0.5, 0.3, kR});
	Place(*S, 3, {0.6, 0.3, kR});
	std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
	const auto Entry = [](int A, int B)
	{
		rb::sim::ZenoEntry Z;
		Z.BallA = static_cast<std::uint8_t>(A);
		Z.BallB = B < 0 ? rb::kNoBallSlot : static_cast<std::uint8_t>(B);
		Z.Count = 3;
		return Z;
	};
	Ws->Zeno.PushBack(Entry(0, 1));
	Ws->Zeno.PushBack(Entry(2, 3));
	Ws->Zeno.PushBack(Entry(1, -1));
	Ws->Zeno.PushBack(Entry(0, 2));
	rb::sim::StartIsland(*Ws, PairSeed(0, 1, true), 0.0);
	RB_REQUIRE(Ws->Zeno.Size() == 2);
	RB_CHECK(Ws->Zeno[0].BallA == 2 && Ws->Zeno[0].BallB == 3);
	RB_CHECK(Ws->Zeno[1].BallA == 0 && Ws->Zeno[1].BallB == 2); // ball 2 is no member
}

RB_TEST(Islands_TiltedTableIslandGetsTheInPlaneGravity)
{
	// Architecture 8.11: SetInPlaneGravity(InPlaneGravity(Params.Tilt, g)) right after Reset; a level table leaves it at (+-0, +-0),
	// which the solver ignores (bitwise the level island).
	for (int Tilted = 0; Tilted < 2; ++Tilted)
	{
		std::unique_ptr<Scene> S = PressingScene();
		if (Tilted != 0)
		{
			S->Input.Params.Tilt.Slope = rb::Vec2{1e-3, -0.5e-3};
		}
		std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
		rb::sim::StartIsland(*Ws, PairSeed(0, 1, true), 0.0);
		const Vec2 G = Ws->Island.Solver.InPlaneGravityAcceleration();
		const Vec2 Expected = rb::InPlaneGravity(S->Input.Params.Tilt, S->Input.Params.Gravity);
		RB_CHECK(G == Expected);
		RB_CHECK((Tilted != 0) == (G.x != 0.0 || G.y != 0.0));
	}
}

RB_TEST(Islands_RailTopSeedsStartRigid)
{
	// A rail-top island (collisions 6.2) starts in Rigid mode, with the rail-top features in reach; its member is supported by
	// the rail-top planes, not by the cloth.
	std::unique_ptr<Scene> S = MakeScene();
	const rb::TableGeometry& T = S->Table;
	const double Slope = (T.Spec.RailTopZ - T.Spec.CushionNoseHeight) / T.Spec.CushionWidth;
	const double PlaneZ = T.Spec.CushionNoseHeight + 0.02 * Slope;
	Place(*S, 0, {0.3, T.HalfWidth + 0.02, PlaneZ + kR * rb::Sqrt(1.0 + Slope * Slope)});
	std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
	const int Poly = rb::sim::FindRailTopPolygon(T, Vec2{0.3, T.HalfWidth + 0.02}, false, rb::RailTopKind::CushionTop, Vec2{}, 0.0);
	RB_REQUIRE(Poly >= 0);
	rb::sim::IslandSeed Seed;
	Seed.BallA = 0;
	Seed.Feature.Kind = rb::TableFeatureKind::RailTop;
	Seed.Feature.Index = static_cast<std::uint8_t>(Poly);
	rb::sim::StartIsland(*Ws, Seed, 0.25); // a rail-top feature seed is a rail-top island even without the flag
	const rb::sim::IslandState& I = Ws->Island;
	RB_REQUIRE(I.Active);
	RB_CHECK(I.Solver.Mode() == rb::CliMode::Rigid);
	RB_CHECK(I.RigidSince == 0.25);
	RB_CHECK(!I.Solver.Body(0).ClothSupport);
	RB_CHECK(rb::sim::NextIslandStepTime(*Ws) == 0.25 + S->Input.Params.Cli.RigidTimeStep);
	bool HasPlane = false;
	bool HasNose = false;
	for (int f = 0; f < I.Solver.FeatureCount(); ++f)
	{
		const rb::TableFeatureRef Ref = rb::sim::SourceOf(I.Solver.Feature(f));
		HasPlane = HasPlane || (Ref.Kind == rb::TableFeatureKind::RailTop && Ref.Index == Poly);
		HasNose = HasNose || Ref.Kind == rb::TableFeatureKind::NoseSegment;
	}
	RB_CHECK(HasPlane);
	RB_CHECK(S->Result.Diagnostics.IslandRigidSwitches == 1);
	(void)HasNose;
}

RB_TEST(Islands_StepBudgetStopsTheMembersWhereTheyAre)
{
	// Guard 8 (architecture 9): the per-shot MaxIslandSteps budget; AdvanceIsland returns false, the members are back in event
	// mode at rest (the loop then aborts the shot), IslandBudgetExceeded is set.
	std::unique_ptr<Scene> S = PressingScene();
	S->Input.Params.Numerics.MaxIslandSteps = 50;
	std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
	rb::sim::StartIsland(*Ws, PairSeed(0, 1, true), 0.0);
	RB_CHECK(!rb::sim::AdvanceIsland(*Ws, rb::kInfinity));
	RB_CHECK(Ws->IslandStepsUsed == 50);
	RB_CHECK(S->Result.Diagnostics.IslandBudgetExceeded);
	RB_CHECK(!Ws->Island.Active);
	RB_CHECK(!Ws->Balls[0].InIsland && !Ws->Balls[1].InIsland);
	RB_CHECK(rb::sim::NextIslandStepTime(*Ws) == rb::kInfinity);
}

RB_TEST(Islands_PocketInteriorSeedsAreResolvedWithoutAnIsland)
{
	// Liner and rim are not island features (Level B): a pressing / Zeno seed on them is resolved by the pocket state machine
	// (always separating), never by an island that could not hold the ball.
	std::unique_ptr<Scene> S = MakeScene();
	const rb::PocketGeometry& G = S->Table.Pockets[kFootLeft];
	// A ball inside the hole touching the back wall from inside, falling.
	const Vec2 Out = rb::Normalized(G.Axis);
	Place(*S, 0, {0.0, 0.0, kR});
	rb::BallState& In = S->Input.Balls[0].State;
	In.Position = rb::ToVec3(G.CaptureCenter + Out * (G.CaptureRadius - kR), -0.01);
	In.Velocity = {0.0, 0.0, -0.5};
	In.State = rb::MotionState::PocketFall;
	std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
	RB_REQUIRE(Ws->Balls[0].Seg.State == rb::MotionState::PocketFall);
	Ws->Balls[0].Context.Pocket = rb::PocketId::FootLeft;
	rb::sim::IslandSeed Seed;
	Seed.BallA = 0;
	Seed.Feature.Kind = rb::TableFeatureKind::LinerWall;
	Seed.Feature.Index = static_cast<std::uint8_t>(kFootLeft);
	Seed.Pressing = true;
	rb::sim::StartIsland(*Ws, Seed, 0.0);
	RB_CHECK(!Ws->Island.Active);
	RB_CHECK(S->Result.Diagnostics.Islands == 0);
}
