#include "rb/Core/FpGuard.h"
// Owner: WP-6b (islands, pockets & rail-top routing). Spec: Docs/architecture.md 8.9 (rail top, review item 4), 15 row 25;
// physics-collisions 6.2, 6.3.
//
// Routing (architecture 8.9):
//  * an airborne ball hits a rail-top plane or edge -> GRI (e_rt, mu_rt), BallRailTop;
//  * a low bounce (< h_min) on the sloped cushion top or on an edge -> rigid island with the rail-top Plane / EdgeLine features;
//    a low bounce on the flat cap -> analytic surface segment on the cap (SupportZ = RailTopZ, rail-cap friction);
//  * on the flat cap, SupportExit across a Seam -> context update; across CushionBack -> rigid island (the slope); across an
//    OuterEdge -> BallOffTable(Floor); across a Facing edge or into the cut disc -> Airborne (into the pocket opening / hole);
//  * at rest on the cap -> BallOffTable(RestsOnRailOrFrame) (WPA 2.6).
#include "SimIslandInternal.h"

#include "rb/Math/Scalar.h"
#include "rb/Physics/Cushion.h"

namespace rb::sim
{
	namespace
	{
		const TableGeometry& TableOf(const Workspace& Ws) { return *Ws.Input->Table; }
		const BallSpec& SpecOf(const Workspace& Ws, int Ball) { return Ws.Input->Balls[Ball].Spec; }

		// The ball leaves its support and flies from state S (z kept; no snap onto a surface).
		void Fly(Workspace& Ws, int Ball, BallState S, double Time)
		{
			BallSlot& B = Ws.Balls[Ball];
			S.State = MotionState::Airborne;
			B.Context.Support = SupportKind::Cloth;
			B.Context.Pocket = PocketId::None;
			B.BounceIndex = 0;
			B.SequenceMaxZ = S.Velocity.z > 0.0 ? S.Position.z + S.Velocity.z * S.Velocity.z / (2.0 * Ws.Params.Gravity) : S.Position.z;
			ReplaceSegment(Ws, Ball, S, Time);
		}

		void RailTopContact(Workspace& Ws, int Ball, const TableFeatureRef& Feature, double Time)
		{
			const TableGeometry& Table = TableOf(Ws);
			BallSlot& B = Ws.Balls[Ball];
			const BallSpec& Spec = SpecOf(Ws, Ball);
			const RailTopPolygon& Poly = Table.RailTops[Feature.Index];
			const MotionState Before = B.Seg.State;

			BallState S = BallStateForEvent(Ws, Ball, Time);
			if (IsOnSurface(S.State))
			{
				S.State = MotionState::Airborne; // GRI (never the on-cloth models): rail-top contacts are airborne contacts
			}
			const FixedContact Contact = MakeFixedContact(Feature, Table, S, Spec, Ws.Detection);
			const CushionImpactResult R =
				ResolveFixedContact(Contact, S, Spec, Ws.Params.Cushion, Ws.Params.PocketContacts, Ws.Params.Cloth, Ws.Params.Numerics);
			BallState After = S;
			After.Velocity = R.Velocity;
			After.Omega = R.Omega;

			ShotEvent E = MakeBallEvent(ShotEventType::BallRailTop, Time, Ball, S);
			E.Feature = static_cast<std::uint8_t>(Poly.Cushion);
			E.SubFeature = static_cast<std::uint8_t>(Poly.Kind);
			E.Value = static_cast<double>(Feature.Index);
			E.Normal = Contact.Normal;
			E.NormalSpeed = R.NormalSpeed;
			E.NormalImpulse = R.NormalImpulse;
			E.Flags = static_cast<std::uint8_t>(ShotEventFlags::Airborne | (R.Stick ? ShotEventFlags::Stick : 0) | (R.Resting ? ShotEventFlags::Resting : 0));
			E.Post[0] = After;
			EmitEvent(Ws, E);
			NoteRailContact(Ws, Ball);

			const bool Low = IsLowRebound(After.Velocity, Contact.Normal, Ws.Params.Gravity, Ws.Params.Slate.MinBounceHeight);
			if (Low)
			{
				// The bounce is below h_min: it settles on the surface (the slate's C.4 guard, applied along the contact normal), so
				// that the rigid island starts in contact instead of with a hop that ends it at once.
				const double Rebound = Dot(After.Velocity, Contact.Normal);
				if (Rebound > 0.0)
				{
					After.Velocity -= Contact.Normal * Rebound;
				}
				const bool FlatPlane = Feature.Kind == TableFeatureKind::RailTop && Poly.Kind == RailTopKind::RailCap;
				if (FlatPlane)
				{
					// Settles on the flat cap: an analytic surface segment (architecture 15 row 25).
					ContinueOnCap(Ws, Ball, After, Feature.Index, Time);
					return;
				}
				// Sloped cushion top or an edge: a rigid island with the rail-top features until the ball rolls back over the nose,
				// drops off, passes the outer edge or settles on the cap (collisions 6.2).
				IslandSeed Seed;
				Seed.BallA = Ball;
				Seed.Feature = Feature;
				Seed.RailTop = true;
				B.Context.Pocket = PocketId::None;
				StartIslandWithState(Ws, Seed, Time, After);
				return;
			}
			// Bounces on: an ordinary flight (inside a pocket's hole above WallTopZ it stays PocketFall until PocketExit).
			if (Before == MotionState::PocketFall && B.Context.Pocket != PocketId::None)
			{
				After.State = MotionState::PocketFall;
				ReplaceSegment(Ws, Ball, After, Time);
				return;
			}
			Fly(Ws, Ball, After, Time);
		}

		void SupportExit(Workspace& Ws, int Ball, const TableFeatureRef& Feature, double Time)
		{
			const TableGeometry& Table = TableOf(Ws);
			const RailTopPolygon& Poly = Table.RailTops[Feature.Index];
			BallState S = BallStateForEvent(Ws, Ball, Time);
			if (Feature.SubIndex == kCutRimEdge)
			{
				// Into the pocket cut: the ball drops into the hole (the edge pivot is not modelled, architecture 8.9).
				Fly(Ws, Ball, S, Time);
				return;
			}
			const int Edge = Feature.SubIndex;
			const RailEdgeKind Kind = Edge < Poly.VertexCount ? Poly.Edges[Edge] : RailEdgeKind::OuterEdge;
			switch (Kind)
			{
			case RailEdgeKind::Seam:
			{
				// The same surface continues on the neighbouring polygon: context update only (no ping-pong: a ball on the seam
				// moving into the new polygon has no exit event, rb/Physics/Detect.h PredictSupportExit).
				const Vec2 Ahead = Normalized(XY(S.Velocity)) * 1e-6;
				const int Next = FindRailTopPolygon(Table, XY(S.Position), false, RailTopKind::RailCap, Ahead, 1e-9);
				if (Next < 0 || Next == Feature.Index)
				{
					// No cap continues there (a seam onto a cushion top): the ball rolls onto the slope.
					IslandSeed Seed;
					Seed.BallA = Ball;
					Seed.Feature = Feature;
					Seed.RailTop = true;
					StartIslandWithState(Ws, Seed, Time, S);
					return;
				}
				ContinueOnCap(Ws, Ball, S, Next, Time);
				return;
			}
			case RailEdgeKind::CushionBack:
			{
				// Over the ridge onto the sloped cushion top: rigid island (Plane + EdgeLine features).
				IslandSeed Seed;
				Seed.BallA = Ball;
				Seed.Feature = Feature;
				Seed.RailTop = true;
				StartIslandWithState(Ws, Seed, Time, S);
				return;
			}
			case RailEdgeKind::OuterEdge:
			{
				// Rolls off the outer rail edge: the center is on the outer boundary moving out (6.3).
				ShotEvent E = MakeBallEvent(ShotEventType::BallOffTable, Time, Ball, S);
				E.Feature = static_cast<std::uint8_t>(OffTableReason::Floor);
				EmitEvent(Ws, E);
				MakeTerminal(Ws, Ball, MotionState::OffTable, Time, PocketId::None, OffTableReason::Floor);
				return;
			}
			case RailEdgeKind::Nose:
			case RailEdgeKind::Facing:
				// Off the cap into the pocket opening (or over a nose): the ball drops.
				Fly(Ws, Ball, S, Time);
				return;
			}
		}
	}

	void RestOnRail(Workspace& Ws, int Ball, const BallState& State, double Time)
	{
		BallState S = State;
		S.Velocity = Vec3{};
		S.Omega = Vec3{};
		S.State = MotionState::Stationary;
		ReplaceSegment(Ws, Ball, S, Time); // the final segment and Finals entry start from the resting state
		ShotEvent E = MakeBallEvent(ShotEventType::BallOffTable, Time, Ball, S);
		E.Feature = static_cast<std::uint8_t>(OffTableReason::RestsOnRailOrFrame);
		EmitEvent(Ws, E);
		MakeTerminal(Ws, Ball, MotionState::OffTable, Time, PocketId::None, OffTableReason::RestsOnRailOrFrame);
	}

	void ContinueOnCap(Workspace& Ws, int Ball, BallState State, int Polygon, double Time)
	{
		BallSlot& B = Ws.Balls[Ball];
		const RailTopPolygon& Poly = TableOf(Ws).RailTops[Polygon];
		const double R = SpecOf(Ws, Ball).Radius;
		const double SupportZ = Poly.PlanePoint.z;
		State.Position.z = SupportZ + R;
		State.Velocity.z = 0.0;
		State.State = MotionState::Sliding;
		ClassifyState(State, R, SupportZ, Ws.Params.Numerics);
		B.Context.Support = SupportKind::RailCap;
		B.Context.SupportPolygon = static_cast<std::uint8_t>(Polygon);
		B.Context.Pocket = PocketId::None;
		B.BounceIndex = 0;
		B.SequenceMaxZ = 0.0;
		if (State.State == MotionState::Stationary)
		{
			// At rest on the flat cap from the start: no transition will fire, so it is off the table now (WPA 2.6).
			RestOnRail(Ws, Ball, State, Time);
			return;
		}
		ReplaceSegment(Ws, Ball, State, Time);
	}

	bool ProcessRailTopEvent(Workspace& Ws, int Ball, const TableFeatureRef& Feature, double Time)
	{
		if (Ws.Input == nullptr || Ws.Input->Table == nullptr || Ball < 0 || Ball >= kMaxBalls)
		{
			return false;
		}
		const TableGeometry& Table = TableOf(Ws);
		if (Feature.Index >= Table.RailTops.Size())
		{
			return false;
		}
		const MotionState State = Ws.Balls[Ball].Seg.State;
		switch (Feature.Kind)
		{
		case TableFeatureKind::RailTop:
		case TableFeatureKind::RailTopEdge:
			if (IsTerminal(State) || State == MotionState::PocketPivot)
			{
				return false;
			}
			RailTopContact(Ws, Ball, Feature, Time);
			return true;
		case TableFeatureKind::SupportExit:
			if (!IsOnSurface(State) || Ws.Balls[Ball].Context.Support != SupportKind::RailCap)
			{
				return false;
			}
			SupportExit(Ws, Ball, Feature, Time);
			return true;
		default:
			return false;
		}
	}
}
