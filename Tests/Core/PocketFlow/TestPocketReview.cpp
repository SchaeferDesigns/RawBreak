// WP-6b review (2026-09-27): regression tests of the pocket-interior and rail-top routing fixes (architecture 8.9; collisions 5.3,
// 5.4, 6.2). Each failed before its fix. The PocketFlow_ tests drive the hooks on a hand-built workspace (standalone: only WP-6b's own
// decisions are observable); the Integ_ ones need the WP-6a loop.
#include "rbtest.h"

#include "../Islands/SimWorkspaceTestUtil.h"

#include "rb/Physics/Compliant.h"
#include "rb/Physics/Detect.h"

#include <cstdio>

using namespace isltest;

namespace
{
	rb::TableFeatureRef Ref(rb::TableFeatureKind Kind, int Index, int Sub = 0)
	{
		rb::TableFeatureRef R;
		R.Kind = Kind;
		R.Index = static_cast<std::uint8_t>(Index);
		R.SubIndex = static_cast<std::uint8_t>(Sub);
		return R;
	}

	void PlaceRolling(Scene& S, int Ball, const Vec3& Position, const Vec3& Velocity) { Place(S, Ball, Position, Velocity, rb::RollingOmegaH(Velocity, kR)); }

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

	// The prior-art 9 parameter recipe of the WP-6a benchmark sets (g 9.81, mu_s 0.2, mu_r 0.01, alpha_sp 10, e_b 0.95; every other
	// value the PhysicsParams default).
	rb::PhysicsParams ValRecipe()
	{
		rb::PhysicsParams P;
		P.Origin = rb::ParamsOrigin::Explicit;
		P.Gravity = kGVal;
		P.Cloth = rb::ClothParams{0.2, 0.010, 10.0};
		P.BallBall.Restitution = 0.95;
		return P;
	}

	// A ball on the flat cap (polygon Cap) at P, rolling with velocity V, in the workspace (the state RailTopContact / ContinueOnCap
	// leave it in).
	void PutOnCap(Scene& S, rb::sim::Workspace& Ws, int Cap, const Vec2& P, const Vec3& V, double T0)
	{
		rb::BallState OnCap;
		OnCap.Position = rb::ToVec3(P, S.Table.Spec.RailTopZ + kR);
		OnCap.Velocity = V;
		OnCap.Omega = rb::RollingOmegaH(V, kR);
		OnCap.State = rb::MotionState::Rolling;
		rb::sim::BallSlot& B = Ws.Balls[0];
		B.Context.Support = rb::SupportKind::RailCap;
		B.Context.SupportPolygon = static_cast<std::uint8_t>(Cap);
		B.Seg = rb::MakeSegment(OnCap, T0, S.Input.Balls[0].Spec, rb::RailCapSurface(S.Input.Params.PocketContacts), S.Table.Spec.RailTopZ, S.Input.Params.Gravity);
	}
}

RB_TEST(PocketFlow_WallContactsLeaveAtTheMinimumAngle)
{
	// Review fix (pocket-interior chatter): a ball leaving a wall-like pocket contact points into the hole by at least
	// kPocketWallMinExitAngle from the tangent of its circle about the pocket axis, its horizontal speed and v_z unchanged; a velocity
	// already turned in by more (or thrown back out by more) is unchanged.
	std::unique_ptr<Scene> S = MakeScene();
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	const rb::PocketGeometry& G = S->Table.Pockets[kFootLeft];
	const Vec2 Out = G.Axis; // from the axis toward the back wall
	const Vec2 Tangent{-Out.y, Out.x};
	const Vec3 Position = rb::ToVec3(G.CaptureCenter + Out * (G.CaptureRadius - kR), -0.01);
	const double Sin = rb::Sin(rb::sim::kPocketWallMinExitAngle);
	const double Cos = rb::Cos(rb::sim::kPocketWallMinExitAngle);
	for (int Side = -1; Side <= 1; Side += 2)
	{
		// Grazing, slightly inward (1 deg) and slightly outward (-2 deg): turned to exactly the minimum angle, same side of the tangent.
		for (double Degrees : {0.0, 1.0, -2.0})
		{
			const double A = Degrees * rb::kDegToRad;
			const Vec2 H = Tangent * (Side * 1.3 * rb::Cos(A)) - Out * (1.3 * rb::Sin(A));
			const Vec3 V = rb::sim::TurnOffPocketWall(G, Position, rb::ToVec3(H, -0.7));
			RB_CHECK_NEAR(rb::Length(rb::XY(V)), 1.3, 1e-12);
			RB_CHECK(V.z == -0.7);
			RB_CHECK_NEAR(-rb::Dot(rb::XY(V), Out), 1.3 * Sin, 1e-12);
			RB_CHECK_NEAR(rb::Dot(rb::XY(V), Tangent), Side * 1.3 * Cos, 1e-12);
		}
	}
	// Turned in by 30 deg (a real impact's rebound) or thrown out by 30 deg: unchanged.
	for (double Degrees : {30.0, -30.0})
	{
		const double A = Degrees * rb::kDegToRad;
		const Vec3 V = rb::ToVec3(Tangent * rb::Cos(A) - Out * rb::Sin(A), 0.2);
		const Vec3 W = rb::sim::TurnOffPocketWall(G, Position, V);
		RB_CHECK(W == V);
	}
	// No horizontal motion, or a center on the axis: unchanged.
	RB_CHECK(rb::sim::TurnOffPocketWall(G, Position, Vec3{0.0, 0.0, -1.0}) == Vec3(0.0, 0.0, -1.0));
	RB_CHECK(rb::sim::TurnOffPocketWall(G, rb::ToVec3(G.CaptureCenter, -0.01), Vec3{1.0, 0.0, 0.0}) == Vec3(1.0, 0.0, 0.0));
}

RB_TEST(PocketFlow_PocketExitBehindTheBackWallIsAMissedWallContact)
{
	// Review fix: PocketExit (center outside a_d, z > R) is an exit over the table only on the front arc; behind it the back wall
	// stands up to WallTopZ, so a center below that top that reaches a_d there has passed a missed wall contact (the facing / back
	// wall junction): MissedEvents, and the ball is not released into the rail. On the front arc the exit is ordinary.
	for (int Back = 0; Back < 2; ++Back)
	{
		std::unique_ptr<Scene> S = MakeScene();
		RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
		const rb::PocketGeometry& G = S->Table.Pockets[kFootLeft];
		const Vec2 Dir = Back != 0 ? G.Axis : -G.Axis;
		Place(*S, 0, {0.0, 0.0, kR});
		std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
		rb::BallState Fall;
		Fall.Position = rb::ToVec3(G.CaptureCenter + Dir * G.DropEdgeRadius, kR + 0.004);
		Fall.Velocity = rb::ToVec3(Dir * 0.8, 0.1);
		Fall.State = rb::MotionState::PocketFall;
		RB_REQUIRE(Fall.Position.z < G.WallTopZ);
		RB_REQUIRE(rb::sim::OnFrontArc(G, rb::XY(Fall.Position)) == (Back == 0));
		rb::sim::BallSlot& B = Ws->Balls[0];
		B.Seg = rb::MakeSegment(Fall, 0.0, S->Input.Balls[0].Spec, S->Input.Params.Cloth, 0.0, S->Input.Params.Gravity);
		B.Context.Pocket = rb::PocketId::FootLeft;
		RB_CHECK(rb::sim::ProcessPocketEvent(*Ws, 0, Ref(rb::TableFeatureKind::PocketExit, kFootLeft), 0.0));
		RB_CHECK(S->Result.Diagnostics.MissedEvents == (Back != 0 ? 1 : 0));
		// Ordinary exit: the ball leaves the pocket context (flies over the shelf); behind the wall it stays this pocket's.
		RB_CHECK((B.Context.Pocket == rb::PocketId::None) == (Back == 0));
	}
}

RB_TEST(PocketFlow_CapExitOverTheCutOrAFacingEdgeRollsInARigidIsland)
{
	// Review fix: a ball rolling on the flat cap off into the pocket cut (or over a cap edge above a facing) is handed to a rigid
	// rail-top island with the cap plane and that physical edge; a direct flight from the edge ping-ponged at one instant with the
	// cap's plane contact (whose contact point is on the same boundary) until the event cap.
	std::unique_ptr<Scene> Probe = MakeScene();
	RB_REQUIRE(Probe->Built == rb::ErrorCode::Ok);
	const rb::TableGeometry& T = Probe->Table;
	int Cuts = 0;
	int Facings = 0;
	for (int i = 0; i < T.RailTops.Size(); ++i)
	{
		const rb::RailTopPolygon& Poly = T.RailTops[i];
		if (Poly.Kind != rb::RailTopKind::RailCap)
		{
			continue;
		}
		for (int e = -1; e < Poly.VertexCount; ++e)
		{
			const bool Cut = e < 0;
			if ((Cut && !Poly.HasCut) || (!Cut && Poly.Edges[e] != rb::RailEdgeKind::Facing))
			{
				continue;
			}
			// On the boundary, moving out of the polygon: toward the cut center, or across the edge's middle.
			Vec2 P;
			Vec2 Dir;
			if (Cut)
			{
				bool Found = false;
				for (int a = 0; a < 72 && !Found; ++a)
				{
					const Vec2 U{rb::Cos(a * rb::kTwoPi / 72.0), rb::Sin(a * rb::kTwoPi / 72.0)};
					Found = StrictlyInside(Poly, Poly.CutCenter + U * (Poly.CutRadius + 0.01), 1e-4);
					P = Poly.CutCenter + U * Poly.CutRadius;
					Dir = -U;
				}
				if (!Found)
				{
					continue;
				}
			}
			else
			{
				const Vec2 A = Poly.Vertices[e];
				const Vec2 B = Poly.Vertices[(e + 1) % Poly.VertexCount];
				P = (A + B) * 0.5;
				Dir = rb::Normalized(Vec2{(B - A).y, -(B - A).x}); // outward (CCW polygon)
				Vec3 From[2];
				Vec3 To[2];
				if (rb::RailTopEdgePieces(T, i, e, From, To) == 0)
				{
					continue;
				}
			}
			std::unique_ptr<Scene> S = MakeScene();
			Place(*S, 0, {0.0, 0.0, kR});
			std::unique_ptr<rb::sim::Workspace> Ws = MakeWorkspace(*S);
			PutOnCap(*S, *Ws, i, P, rb::ToVec3(Dir * 0.25, 0.0), 1.0);
			RB_REQUIRE(rb::sim::ProcessRailTopEvent(*Ws, 0, Ref(rb::TableFeatureKind::SupportExit, i, Cut ? rb::kCutRimEdge : e), 1.0));
			const rb::sim::IslandState& I = Ws->Island;
			RB_CHECK(I.Active);
			RB_CHECK(I.Solver.Mode() == rb::CliMode::Rigid);
			RB_CHECK(Ws->Balls[0].InIsland);
			RB_REQUIRE(I.Solver.BodyCount() == 1);
			RB_CHECK(!I.Solver.Body(0).ClothSupport);
			bool Plane = false;
			bool Edge = false;
			for (int f = 0; f < I.Solver.FeatureCount(); ++f)
			{
				const rb::TableFeatureRef Source = rb::sim::SourceOf(I.Solver.Feature(f));
				Plane = Plane || (Source.Kind == rb::TableFeatureKind::RailTop && Source.Index == i);
				Edge = Edge || (Source.Kind == rb::TableFeatureKind::RailTopEdge && Source.Index == i && Source.SubIndex == (Cut ? rb::kCutRimEdge : e));
			}
			RB_CHECK(Plane && Edge);
			Cuts += Cut ? 1 : 0;
			Facings += Cut ? 0 : 1;
		}
	}
	std::printf("  cap exits into an island: %d cut rims, %d facing edges\n", Cuts, Facings);
	RB_CHECK(Cuts > 0);
}

RB_TEST(Integ_WP6b_BallRunningAroundTheLinerIsAPolygonNotAChatter)
{
	// Review fix: a fast ball entering a corner pocket off its axis runs around the liner. With the v_rest separation of grazing
	// contacts it re-met the curved wall every 2 atan(v_rest / v_t) of arc: 472 BallLiner events and 0.8 ms for this one shot, and a
	// jump or a jaw rattle reached the 20 000 event cap (SimStatus::Aborted). Every wall-like contact now leaves at least
	// kPocketWallMinExitAngle into the hole: a polygon of at most 36 contacts per turn.
	std::unique_ptr<Scene> S = MakeScene(rb::kTableNineFootPro, kGVal);
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	S->Input.Params = ValRecipe();
	const rb::PocketGeometry& G = S->Table.Pockets[0];
	const double Angle = 0.48;
	const Vec2 Dir{G.Axis.x * rb::Cos(Angle) - G.Axis.y * rb::Sin(Angle), G.Axis.x * rb::Sin(Angle) + G.Axis.y * rb::Cos(Angle)};
	PlaceRolling(*S, 0, rb::ToVec3(G.CaptureCenter - Dir * 0.35, kR), rb::ToVec3(Dir * 5.25, 0.0));
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	const int Walls = Count(R, rb::ShotEventType::BallLiner) + Count(R, rb::ShotEventType::BallPocketRim);
	std::printf("  liner run: %d pocket-wall contacts, %d events\n", Walls, R.Diagnostics.EventsProcessed);
	RB_CHECK(R.Finals[0].Status == rb::BallFinalStatus::Pocketed);
	RB_CHECK(Walls >= 1 && Walls <= 40);
	RB_CHECK(R.Diagnostics.EventsProcessed <= 60);
	// Every liner contact leaves into the hole by the minimum angle (or at least v_rest where the ball is too slow for it).
	for (const rb::ShotEvent& E : R.Events)
	{
		if (E.Type != rb::ShotEventType::BallLiner)
		{
			continue;
		}
		const rb::PocketGeometry& P = S->Table.Pockets[E.Feature];
		const Vec2 Out = rb::Normalized(rb::XY(E.Post[0].Position) - P.CaptureCenter);
		const Vec2 H = rb::XY(E.Post[0].Velocity);
		const double Inward = -rb::Dot(H, Out);
		RB_CHECK(Inward >= rb::Min(rb::Length(H) * rb::Sin(rb::sim::kPocketWallMinExitAngle), S->Input.Params.Numerics.RestSpeed) - 1e-12);
	}
}

RB_TEST(Integ_WP6b_RollingOffTheCapIntoACutOrOverAFacingEdgeTerminates)
{
	// Review fix: balls rolling on the flat cap into a pocket cut or over a cap edge above a facing. The flight from the boundary
	// re-met the cap plane at the same instant, forever: 76 of 228 of these runs ended at the 20 000 event cap (Aborted).
	std::unique_ptr<Scene> Probe = MakeScene(rb::kTableNineFootPro, kGVal);
	RB_REQUIRE(Probe->Built == rb::ErrorCode::Ok);
	const rb::TableGeometry& T = Probe->Table;
	int Runs = 0;
	int Bad = 0;
	int WorstEvents = 0;
	for (int i = 0; i < T.RailTops.Size(); ++i)
	{
		const rb::RailTopPolygon& Poly = T.RailTops[i];
		if (Poly.Kind != rb::RailTopKind::RailCap)
		{
			continue;
		}
		for (int e = -24; e < Poly.VertexCount; ++e)
		{
			Vec2 Start;
			Vec2 Dir;
			if (e < 0)
			{
				// Toward the cut center from 1 cm outside the cut circle, 24 directions.
				if (!Poly.HasCut)
				{
					continue;
				}
				const Vec2 U{rb::Cos((e + 24) * rb::kTwoPi / 24.0), rb::Sin((e + 24) * rb::kTwoPi / 24.0)};
				Start = Poly.CutCenter + U * (Poly.CutRadius + 0.01);
				Dir = -U;
			}
			else
			{
				if (Poly.Edges[e] != rb::RailEdgeKind::Facing)
				{
					continue;
				}
				const Vec2 A = Poly.Vertices[e];
				const Vec2 B = Poly.Vertices[(e + 1) % Poly.VertexCount];
				Dir = rb::Normalized(Vec2{(B - A).y, -(B - A).x});
				Start = (A + B) * 0.5 - Dir * 0.01;
			}
			if (!StrictlyInside(Poly, Start, 1e-4))
			{
				continue;
			}
			for (int Speed = 1; Speed <= 3; ++Speed)
			{
				std::unique_ptr<Scene> S = MakeScene(rb::kTableNineFootPro, kGVal);
				S->Input.Params = ValRecipe();
				PlaceRolling(*S, 0, rb::ToVec3(Start, T.Spec.RailTopZ + kR + 0.002), rb::ToVec3(Dir * (0.25 * Speed), 0.0));
				S->Input.Record.Trajectories = false;
				const rb::SimStatus Status = Run(*S);
				++Runs;
				const int Events = S->Result.Diagnostics.EventsProcessed;
				WorstEvents = Events > WorstEvents ? Events : WorstEvents;
				if (Status != rb::SimStatus::Ok || Events > 200 || !rb::IsFinite(S->Result.Finals[0].State.Position.x))
				{
					++Bad;
				}
			}
		}
	}
	std::printf("  cap exits: %d runs, %d bad, at most %d events\n", Runs, Bad, WorstEvents);
	RB_CHECK(Runs > 100);
	RB_CHECK(Bad == 0);
}

RB_TEST(Integ_WP6b_BallBehindTheBackWallIsPocketedNotReleasedIntoTheRail)
{
	// Review fix (found by VAL ROB-14's jaw sweep on TABLE_7FT_78): a 3.9 m/s rolling ball rattles off a side-pocket jaw and rim near
	// the end of the front arc into the back sector, already beyond the liner's contact radius, so no wall event fires (the facing /
	// back-wall junction, WP-5 / WP-2). Its PocketExit at a_d behind the wall released it into the rail: it rolled over the cap and
	// off the table. Then the missed wall contact was counted and the ball pocketed. WP-10 (VAL ROB-11) closed the junction: the
	// facing's back-end edge (Detect.h PredictFacingEndEdge) now turns the ball before it reaches the back sector, so no event is
	// missed any more; the outcome is unchanged (pocketed, never released into the rail).
	std::unique_ptr<Scene> S = MakeScene(rb::kTableSevenFoot78, kGVal);
	RB_REQUIRE(S->Built == rb::ErrorCode::Ok);
	S->Input.Params = ValRecipe();
	PlaceRolling(*S, 0, {0.82496493382352054, 0.21715373230760571, kR}, {2.5901379971211136, 2.9412094333952181, 0.0});
	RB_REQUIRE(Run(*S) == rb::SimStatus::Ok);
	const rb::ShotResult& R = S->Result;
	int EdgeContacts = 0;
	for (const rb::ShotEvent& E : R.Events)
	{
		EdgeContacts += E.Type == rb::ShotEventType::BallJaw && (E.SubFeature >> 4) == 2 ? 1 : 0;
	}
	std::printf("  back-wall junction: %d facing-edge contacts, %d missed events, final status %d\n", EdgeContacts, R.Diagnostics.MissedEvents,
		static_cast<int>(R.Finals[0].Status));
	RB_CHECK(Count(R, rb::ShotEventType::BallOffTable) == 0);
	RB_CHECK(R.Finals[0].Status == rb::BallFinalStatus::Pocketed);
	RB_CHECK(R.Diagnostics.MissedEvents == 0);
	RB_CHECK(EdgeContacts >= 1);
	RB_CHECK(Count(R, rb::ShotEventType::BallPocketExit) == 0);
}
