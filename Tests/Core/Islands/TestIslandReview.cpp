// WP-6b review (2026-09-27): regression and adversarial tests of the island hooks (architecture 8.8, 8.9, 9, 11). The Islands_ tests
// drive the hooks on a hand-built workspace (standalone); the Integ_ ones need the WP-6a loop.
#include "rbtest.h"

#include "SimWorkspaceTestUtil.h"

#include "rb/Geometry/RackLayout.h"
#include "rb/Physics/Compliant.h"

#include <cstdio>
#include <cstring>
#include <memory>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#define RB_ISLAND_TEST_ALLOC_HOOK 1
#else
#define RB_ISLAND_TEST_ALLOC_HOOK 0
#endif

using namespace isltest;

namespace
{
#if RB_ISLAND_TEST_ALLOC_HOOK
	// Debug CRT allocation hook (MSVC): counts heap allocations while installed.
	int g_IslandTestAllocations = 0;
	int CountAllocations(int AllocType, void*, size_t, int, long, const unsigned char*, int)
	{
		if (AllocType == _HOOK_ALLOC || AllocType == _HOOK_REALLOC)
		{
			++g_IslandTestAllocations;
		}
		return 1;
	}
#endif

	bool StrictlyInside(const rb::RailTopPolygon& Poly, const Vec2& P, double Margin)
	{
		for (int e = 0; e < Poly.VertexCount; ++e)
		{
			const Vec2 A = Poly.Vertices[e];
			const Vec2 B = Poly.Vertices[(e + 1) % Poly.VertexCount];
			if (rb::Cross(B - A, P - A) < Margin * rb::Length(B - A))
			{
				return false;
			}
		}
		return !Poly.HasCut || rb::Length(P - Poly.CutCenter) > Poly.CutRadius + Margin;
	}

	// A point on a flat cap polygon that the cloth-level pocket regions also claim: inside a pocket's drop-edge circle a_d (which
	// reaches r_d beyond the cut onto the cap) or over a pocket opening (which covers cap surrounds behind a corner pocket).
	bool CapPointInAPocketRegion(const rb::TableGeometry& T, int& Polygon, Vec2& P, Vec2& Tangent)
	{
		for (int i = 0; i < T.RailTops.Size(); ++i)
		{
			const rb::RailTopPolygon& Poly = T.RailTops[i];
			if (Poly.Kind != rb::RailTopKind::RailCap || !Poly.HasCut)
			{
				continue;
			}
			for (int d = 1; d <= 4; ++d)
			{
				for (int a = 0; a < 72; ++a)
				{
					const Vec2 U{rb::Cos(a * rb::kTwoPi / 72.0), rb::Sin(a * rb::kTwoPi / 72.0)};
					const Vec2 Q = Poly.CutCenter + U * (Poly.CutRadius + 1e-3 * d);
					if (StrictlyInside(Poly, Q, 5e-4) &&
						(rb::sim::PocketContaining(T, rb::PocketModel::GeometricLevelA, Q) >= 0 || rb::IsOverPocketOpening(T, Q)))
					{
						Polygon = i;
						P = Q;
						Tangent = Vec2{-U.y, U.x};
						return true;
					}
				}
			}
		}
		return false;
	}

	rb::sim::IslandSeed PairSeed(int A, int B, bool Pressing)
	{
		rb::sim::IslandSeed Seed;
		Seed.BallA = A;
		Seed.BallB = B;
		Seed.Pressing = Pressing;
		return Seed;
	}

	// The prior-art 9 parameter recipe of the WP-6a benchmark sets.
	rb::PhysicsParams ValRecipe()
	{
		rb::PhysicsParams P;
		P.Origin = rb::ParamsOrigin::Explicit;
		P.Gravity = kGVal;
		P.Cloth = rb::ClothParams{0.2, 0.010, 10.0};
		P.BallBall.Restitution = 0.95;
		return P;
	}
}

RB_TEST(Islands_RailTopMemberOnACapSurroundReturnsToTheCap)
{
	// Review fix: a rail-top member that settles on the flat cap is handed to an analytic cap segment (architecture 15 row 25). The
	// exit test also required the member to be outside every pocket's drop-edge circle and outside the pocket openings - cloth-level
	// regions that reach onto the cap around the cuts - so a ball rolling or spinning out there stayed in the 20 us rigid island until
	// it came to rest (98 000 steps for one ball dropped on a corner surround in a random rail-top sweep).
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	int Polygon = -1;
	Vec2 P;
	Vec2 Tangent;
	RB_REQUIRE(CapPointInAPocketRegion(S->Table, Polygon, P, Tangent));
	Place(*S, 0, {0.0, 0.0, kR});
	std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
	rb::BallState OnCap;
	OnCap.Position = rb::ToVec3(P, S->Table.Spec.RailTopZ + kR);
	OnCap.Velocity = rb::ToVec3(Tangent * 0.01, 0.0);
	OnCap.Omega = rb::RollingOmegaH(OnCap.Velocity, kR);
	OnCap.State = rb::MotionState::Airborne;
	// As RailTopContact seeds it: a low rebound on the plane, the ball's context still the cloth's.
	rb::sim::IslandSeed Seed;
	Seed.BallA = 0;
	Seed.Feature.Kind = rb::TableFeatureKind::RailTop;
	Seed.Feature.Index = static_cast<std::uint8_t>(Polygon);
	Seed.RailTop = true;
	rb::sim::StartIslandWithState(*Ws, Seed, 1.0, OnCap);
	RB_REQUIRE(Ws->Island.Active);
	RB_CHECK(rb::sim::AdvanceIsland(*Ws, 1.01)); // 500 rigid steps of room
	RB_CHECK(!Ws->Island.Active);
	RB_CHECK(!Ws->Balls[0].InIsland);
	RB_CHECK(Ws->Balls[0].Context.Support == rb::SupportKind::RailCap);
	RB_CHECK(Ws->IslandStepsUsed <= 5);
}

RB_TEST(Islands_NoHeapAllocationInTheHooks)
{
	// Code rule: no heap allocation in the event loop. The WP-6b hooks on reserved workspaces (islands from start to exit incl.
	// Sampled recording and feature queries, a drop edge with its pivot, a landing, a rail-top island over a cut rim) under the MSVC
	// Debug CRT allocation hook; other builds only run the calls.
	std::unique_ptr<Scene> Pressing = MakeScene();
	Place(*Pressing, 0, {0.0, 0.0, kR}, {}, {0.0, 10.0, 0.0});
	Place(*Pressing, 1, {2.0 * kR, 0.0, kR});
	std::unique_ptr<rb::sim::Workspace> WsPressing = MakeWorkspace(*Pressing);
	std::unique_ptr<Scene> Rail = MakeScene();
	const rb::TableGeometry& T = Rail->Table;
	const double Rc = rb::ComputeCushionContact(kR, T.Spec.CushionNoseHeight, 0.0, false).HorizontalOffset;
	Place(*Rail, 0, {0.9, T.HalfWidth - Rc, kR}, {-0.3, 0.0, 0.0}, {-10.0, -0.3 / kR, 0.0});
	std::unique_ptr<rb::sim::Workspace> WsRail = MakeWorkspace(*Rail);
	std::unique_ptr<Scene> Edge = MakeScene();
	const rb::PocketGeometry& G = Edge->Table.Pockets[kFootLeft];
	const Vec3 In = rb::ToVec3(G.Axis, 0.0) * 0.2;
	Place(*Edge, 0, rb::ToVec3(G.CaptureCenter - G.Axis * G.DropEdgeRadius, kR), In, rb::RollingOmegaH(In, kR));
	Place(*Edge, 1, {0.2, 0.1, kR + 0.05}, {0.1, 0.0, 0.0});
	std::unique_ptr<rb::sim::Workspace> WsEdge = MakeWorkspace(*Edge);
	int Polygon = -1;
	Vec2 P;
	Vec2 Tangent;
	RB_REQUIRE(CapPointInAPocketRegion(T, Polygon, P, Tangent));
	std::unique_ptr<Scene> Cap = MakeScene();
	Place(*Cap, 0, {0.0, 0.0, kR});
	std::unique_ptr<rb::sim::Workspace> WsCap = MakeWorkspace(*Cap);
	const rb::RailTopPolygon& Poly = T.RailTops[Polygon];
	rb::BallState AtCut;
	const Vec2 Radial = rb::Normalized(P - Poly.CutCenter);
	AtCut.Position = rb::ToVec3(Poly.CutCenter + Radial * Poly.CutRadius, T.Spec.RailTopZ + kR);
	AtCut.Velocity = rb::ToVec3(-Radial * 0.2, 0.0);
	AtCut.Omega = rb::RollingOmegaH(AtCut.Velocity, kR);
	AtCut.State = rb::MotionState::Rolling;
	WsCap->Balls[0].Context.Support = rb::SupportKind::RailCap;
	WsCap->Balls[0].Context.SupportPolygon = static_cast<std::uint8_t>(Polygon);
	WsCap->Balls[0].Seg = rb::MakeSegment(AtCut, 0.0, Cap->Input.Balls[0].Spec, rb::RailCapSurface(Cap->Input.Params.PocketContacts), T.Spec.RailTopZ,
		Cap->Input.Params.Gravity);
	rb::TableFeatureRef Drop;
	Drop.Kind = rb::TableFeatureKind::DropEdge;
	Drop.Index = static_cast<std::uint8_t>(kFootLeft);
	rb::TableFeatureRef CutExit;
	CutExit.Kind = rb::TableFeatureKind::SupportExit;
	CutExit.Index = static_cast<std::uint8_t>(Polygon);
	CutExit.SubIndex = static_cast<std::uint8_t>(rb::kCutRimEdge);
#if RB_ISLAND_TEST_ALLOC_HOOK
	g_IslandTestAllocations = 0;
	const _CRT_ALLOC_HOOK Previous = _CrtSetAllocHook(&CountAllocations);
#endif
	rb::sim::StartIsland(*WsPressing, PairSeed(0, 1, true), 0.0);
	const bool PressingDone = rb::sim::AdvanceIsland(*WsPressing, rb::kInfinity);
	rb::sim::IslandSeed CushionSeed;
	CushionSeed.BallA = 0;
	CushionSeed.Feature.Kind = rb::TableFeatureKind::NoseSegment;
	CushionSeed.Feature.Index = static_cast<std::uint8_t>(rb::CushionId::LeftFoot);
	CushionSeed.Pressing = true;
	rb::sim::StartIsland(*WsRail, CushionSeed, 0.0);
	const bool RailDone = rb::sim::AdvanceIsland(*WsRail, 0.05);
	const bool Dropped = rb::sim::ProcessPocketEvent(*WsEdge, 0, Drop, 0.0);
	rb::sim::RouteLanding(*WsEdge, 1, WsEdge->Balls[1].Seg.T0 + WsEdge->Balls[1].Seg.TauEnd);
	const bool CutDone = rb::sim::ProcessRailTopEvent(*WsCap, 0, CutExit, 0.0);
	const bool CutAdvanced = rb::sim::AdvanceIsland(*WsCap, 0.05);
#if RB_ISLAND_TEST_ALLOC_HOOK
	_CrtSetAllocHook(Previous);
	RB_CHECK(g_IslandTestAllocations == 0);
#endif
	RB_CHECK(PressingDone && RailDone && Dropped && CutDone && CutAdvanced);
	RB_CHECK(WsPressing->IslandStepsUsed > 200 && WsRail->IslandStepsUsed > 100 && WsCap->IslandStepsUsed > 0);
}

RB_TEST(Integ_WP6b_ZenoGuardLoggedOncePerTrigger)
{
	// Review fix: the loop owns the Zeno detector and logs ZenoGuard when it hands a Zeno seed over; StartIsland logged it a second
	// time (70 ZenoGuard events for 35 triggers in this chase). The set-up is VAL ROB-15's: a cue ball with pure topspin 1 um behind
	// an object ball, the micro-impact, pressing and graze guards off, the Zeno detector at its defaults.
	std::unique_ptr<Scene> S = MakeScene(rb::kTableNineFootPro, kGVal);
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	S->Input.Params = ValRecipe();
	S->Input.Params.Numerics.RestSpeed = 0.0;
	S->Input.Params.Numerics.ApproachSpeedTol = 0.0;
	S->Input.Params.Numerics.TangencyTolPerLength = 0.0;
	Place(*S, 0, {-0.8, 0.0, kR}, {}, {0.0, 10.0, 0.0});
	Place(*S, 1, {-0.8 + 2.0 * kR + 1e-6, 0.0, kR});
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(R.Diagnostics.ZenoTriggers >= 1);
	RB_CHECK(Count(R, rb::ShotEventType::ZenoGuard) == R.Diagnostics.ZenoTriggers);
}

RB_TEST(Integ_WP6b_ZeroGapRackIsBitIdenticalUnderBallIdPermutation)
{
	// Adversarial (architecture 8.8 id independence, 11 item 2; the cluster half of prior-art BRK-04): a 15-ball zero-gap rack broken
	// at 13 m/s with the object-ball ids permuted gives bit-identical final states after mapping the ids back.
	auto Rack = [](bool Permuted, int* Map)
	{
		std::unique_ptr<Scene> S = MakeScene(rb::kTableNineFootPro, kGVal);
		rb::RackSite Sites[rb::kMaxRackSites];
		const double D = 2.0 * kR;
		const int N = rb::BuildRackLattice(rb::RackShape::Triangle15, rb::RackApexX(rb::RackShape::Triangle15, rb::RackAnchor::ApexOnFootSpot,
			S->Table.Landmarks.FootSpot.x, D), D, Sites);
		for (int k = 0; k < N; ++k)
		{
			const int Id = Permuted ? 1 + ((k * 7 + 3) % 15) : k + 1;
			Map[k + 1] = Id;
			Place(*S, Id, rb::ToVec3(Sites[k].Position, kR));
		}
		Map[0] = 0;
		Place(*S, 0, {S->Table.Landmarks.HeadStringX, 0.0, kR}, {13.0, 0.0, 0.0});
		S->Input.Record.Trajectories = false;
		return S;
	};
	int Identity[16] = {};
	int Map[16] = {};
	std::unique_ptr<Scene> A = Rack(false, Identity);
	std::unique_ptr<Scene> B = Rack(true, Map);
	RB_REQUIRE(Run(*A) == rb::SimStatus::Ok);
	RB_REQUIRE(Run(*B) == rb::SimStatus::Ok);
	RB_CHECK(A->Result.Diagnostics.Islands >= 1);
	int Different = 0;
	for (int b = 0; b <= 15; ++b)
	{
		const rb::BallFinal& FA = A->Result.Finals[b];
		const rb::BallFinal& FB = B->Result.Finals[Map[b]];
		Different += !SameState(FA.State, FB.State) || FA.Status != FB.Status || FA.Pocket != FB.Pocket ? 1 : 0;
	}
	RB_CHECK(Different == 0);
	RB_CHECK(A->Result.Diagnostics.EventsProcessed == B->Result.Diagnostics.EventsProcessed);
	RB_CHECK(A->Result.Diagnostics.IslandSteps == B->Result.Diagnostics.IslandSteps);
}

RB_TEST(Integ_WP6b_RecordingCapacityNeverChangesThePhysics)
{
	// Adversarial (buffer overflow handling, architecture 12 "recording switchable and bounded"): islands (a zero-gap break), a pivot
	// and a liner run recorded into tiny capacities (3 segments per ball, 8 events, 1 cue-tip piece) or with every recording switch
	// off overflow gracefully (TrajectoryOverflow / EventLogOverflow) and give bit-identical final states, event counts and island
	// steps: the Sampled recording of islands and pivots never feeds back into the simulation.
	rb::ResultCapacity Tiny;
	Tiny.MaxLoggedEvents = 8;
	Tiny.MaxSegmentsPerBall = 3;
	Tiny.MaxRecordEvents = 8;
	Tiny.MaxCueTipSegments = 1;
	for (int Shot = 0; Shot < 3; ++Shot)
	{
		std::unique_ptr<Scene> S = MakeScene(rb::kTableNineFootPro, kGVal);
		RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
		S->Input.Params = ValRecipe();
		const rb::PocketGeometry& G = S->Table.Pockets[Shot == 1 ? kFootLeft : 0];
		if (Shot == 0)
		{
			rb::RackSite Sites[rb::kMaxRackSites];
			const double D = 2.0 * kR;
			const int N = rb::BuildRackLattice(rb::RackShape::Triangle15, rb::RackApexX(rb::RackShape::Triangle15, rb::RackAnchor::ApexOnFootSpot,
				S->Table.Landmarks.FootSpot.x, D), D, Sites);
			for (int k = 0; k < N; ++k)
			{
				Place(*S, k + 1, rb::ToVec3(Sites[k].Position, kR));
			}
			Place(*S, 0, {S->Table.Landmarks.HeadStringX, 0.01, kR});
			rb::StrikeRequest Strike;
			Strike.Ball = 0;
			Strike.Input.Speed = 11.0;
			Strike.Input.Cue = rb::kCuePlaying19oz;
			S->Input.Strikes.PushBack(Strike);
		}
		else if (Shot == 1)
		{
			// P-4's ball stopping 0.5 mm inside a_d: a long pivot.
			const Vec3 V = rb::ToVec3(G.Axis, 0.0) * 0.0990285;
			Place(*S, 0, rb::ToVec3(G.CaptureCenter - G.Axis * (G.DropEdgeRadius - 0.0005 + 0.05), kR), V, rb::RollingOmegaH(V, kR));
		}
		else
		{
			const double Angle = 0.48;
			const Vec2 Dir{G.Axis.x * rb::Cos(Angle) - G.Axis.y * rb::Sin(Angle), G.Axis.x * rb::Sin(Angle) + G.Axis.y * rb::Cos(Angle)};
			const Vec3 V = rb::ToVec3(Dir * 5.25, 0.0);
			Place(*S, 0, rb::ToVec3(G.CaptureCenter - Dir * 0.35, kR), V, rb::RollingOmegaH(V, kR));
		}
		rb::Simulator Full;
		std::unique_ptr<rb::ShotResult> ReferencePtr = std::make_unique<rb::ShotResult>();
		const rb::ShotResult& Reference = *ReferencePtr;
		RB_REQUIRE(Full.Run(S->Input, *ReferencePtr) == rb::SimStatus::Ok);
		for (int Variant = 0; Variant < 2; ++Variant)
		{
			std::unique_ptr<rb::SimInput> InPtr = std::make_unique<rb::SimInput>(S->Input);
			rb::SimInput& In = *InPtr;
			if (Variant == 1)
			{
				In.Record.Trajectories = false;
				In.Record.EventStates = false;
				In.Record.LogTransitions = false;
				In.Record.LogObservers = false;
				In.Record.ShotRecord = false;
			}
			rb::Simulator Small(Variant == 0 ? Tiny : rb::ResultCapacity{});
			std::unique_ptr<rb::ShotResult> RPtr = std::make_unique<rb::ShotResult>();
			rb::ShotResult& R = *RPtr;
			RB_REQUIRE(Small.Run(In, R) == rb::SimStatus::Ok);
			if (Variant == 0)
			{
				std::size_t LongestTrack = 0;
				for (int b = 0; b < rb::kMaxBalls; ++b)
				{
					LongestTrack = Reference.Tracks[b].Segments.size() > LongestTrack ? Reference.Tracks[b].Segments.size() : LongestTrack;
				}
				RB_CHECK(R.Diagnostics.TrajectoryOverflow == (LongestTrack > 3));
				RB_CHECK(R.Diagnostics.EventLogOverflow == (Reference.Events.size() > 8));
				RB_CHECK(LongestTrack > 3 && Reference.Diagnostics.EventsProcessed > 0);
			}
			RB_CHECK(R.Diagnostics.EventsProcessed == Reference.Diagnostics.EventsProcessed);
			RB_CHECK(R.Diagnostics.IslandSteps == Reference.Diagnostics.IslandSteps);
			for (int b = 0; b < rb::kMaxBalls; ++b)
			{
				RB_CHECK(SameState(R.Finals[b].State, Reference.Finals[b].State) && R.Finals[b].Status == Reference.Finals[b].Status);
			}
		}
	}
}

RB_TEST(Integ_WP6b_TipEndAtTheBudgetStopCarriesTheEnvelopeGap)
{
	// Review fix: a tip contact interval still open when its tip leaves the island (here: the island step budget stops the shot while
	// the cue still pushes the frozen cue ball, A-ISL-3's set-up) is closed with the envelope data of every other tip record (rules F7:
	// B = f and Value = the gap to f) and the touched ball's island state; the gap was missing (0) and the state was the ball's stale
	// pre-island segment.
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	S->Input.Params.Numerics.MaxIslandSteps = 200;
	Place(*S, 0, {0.0, 0.0, kR});
	Place(*S, 1, {2.0 * kR, 0.0, kR});
	rb::StrikeRequest Strike;
	Strike.Ball = 0;
	Strike.Input.Speed = 2.0;
	Strike.Input.Azimuth = 0.0;
	Strike.Input.Cue = rb::kCuePlaying19oz;
	Strike.Input.SquirtEnabled = false;
	S->Input.Strikes.PushBack(Strike);
	RB_CHECK(Run(*S) == rb::SimStatus::Aborted);
	const rb::ShotResult& R = S->Result;
	RB_CHECK(R.Diagnostics.IslandBudgetExceeded);
	const rb::ShotEvent* End = nullptr;
	for (const rb::ShotEvent& E : R.Events)
	{
		End = E.Type == rb::ShotEventType::TipContactEnd && E.A == 0 ? &E : End;
	}
	RB_REQUIRE(End != nullptr);
	RB_CHECK(End->B == 1);
	RB_CHECK(End->Value != 0.0 && rb::Abs(End->Value) < 1e-3);
	// The cue ball's state at the stop: it has been pushed into the object ball (x > 0), not its pre-island segment's.
	const rb::BallFinal& Cue = R.Finals[0];
	RB_CHECK_NEAR(End->Pre[0].Position.x, Cue.State.Position.x, 1e-12);
}
