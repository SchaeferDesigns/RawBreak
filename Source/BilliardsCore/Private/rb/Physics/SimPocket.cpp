#include "rb/Core/FpGuard.h"
// Owner: WP-6b (islands, pockets & rail-top routing). Spec: Docs/architecture.md 8.9; physics-collisions 5.3, 5.4, 6.1;
// physics-motion-and-cue C.2-C.4.
//
// Pocket state machine (collisions 5.4):
//   surface on cloth --DropEdge--> BallPocketEnter; v_perp >= sqrt(g rho) -> PocketFall, else PocketPivot
//   PocketPivot --pivot end (T_p) or truncation by a facing / jaw arc on the proxy--> PocketFall
//   PocketFall --liner / rim torus (GRI; wall-like contacts leave at least kPocketWallMinExitAngle into the hole)--> PocketFall;
//             --CaptureDepth--> Pocketed; --PocketExit on the front arc--> Airborne (BallPocketExit); --PocketExit behind the back
//             wall below its top (a missed wall contact, MissedEvents)--> Pocketed
// Landing routing (collisions 6.1): capture circle or the rounded annulus on the front arc -> PocketFall (no slate; the torus
// event follows from there, rb/Physics/Detect.h PredictRimTorus); playing surface / shelf -> ResolveSlateImpact; behind a nose
// line -> a rail-top / nose event must have fired (MissedEvents, recovered as a slate impact).
#include "SimIslandInternal.h"

#include "rb/Math/Scalar.h"
#include "rb/Physics/Cushion.h"
#include "rb/Physics/Slate.h"

namespace rb::sim
{
	namespace
	{
		const TableGeometry& TableOf(const Workspace& Ws) { return *Ws.Input->Table; }
		const BallSpec& SpecOf(const Workspace& Ws, int Ball) { return Ws.Input->Balls[Ball].Spec; }

		ShotEventType ContactEventType(TableFeatureKind Kind) { return Kind == TableFeatureKind::RimTorus ? ShotEventType::BallPocketRim : ShotEventType::BallLiner; }

		// BallJaw element code (ShotResult.h): 0 jaw arc, 1 facing face, 2 facing top edge.
		std::uint8_t JawElement(TableFeatureKind Kind)
		{
			return Kind == TableFeatureKind::JawArc ? 0 : (Kind == TableFeatureKind::FacingFace ? 1 : 2);
		}

		// Resolves a fixed contact (GRI for every non-surface state) of the ball in state S and fills the impulse fields of E.
		BallState ResolveContact(Workspace& Ws, int Ball, const TableFeatureRef& Feature, const BallState& S, ShotEvent& E)
		{
			const FixedContact Contact = MakeFixedContact(Feature, TableOf(Ws), S, SpecOf(Ws, Ball), Ws.Detection);
			const CushionImpactResult R =
				ResolveFixedContact(Contact, S, SpecOf(Ws, Ball), Ws.Params.Cushion, Ws.Params.PocketContacts, Ws.Params.Cloth, Ws.Params.Numerics);
			BallState Out = S;
			Out.Velocity = R.Velocity;
			Out.Omega = R.Omega;
			E.Normal = Contact.Normal;
			E.NormalSpeed = R.NormalSpeed;
			E.NormalImpulse = R.NormalImpulse;
			E.TangentImpulse = SpecOf(Ws, Ball).Mass * Length((R.Velocity - S.Velocity) - Contact.Normal * Dot(R.Velocity - S.Velocity, Contact.Normal));
			E.Flags = static_cast<std::uint8_t>(E.Flags | (Contact.BallOnCloth ? 0 : ShotEventFlags::Airborne) | (R.Stick ? ShotEventFlags::Stick : 0) |
				(R.Resting ? ShotEventFlags::Resting : 0));
			E.Post[0] = Out;
			return Out;
		}

		// Liner / back wall and rim torus from inside (collisions 5.3, GRI with e_l / rim values). Level A has no rolling on these
		// surfaces, so the resolution must leave the ball separating as the DETECTOR measures it, or the same contact is found
		// again at tau = 0 forever:
		//  * the liner is detected on the horizontal distance r_p - R (a vertical wall, PredictLinerWall) but resolved with the
		//    undercut normal (tilted down by beta_l, MakeFixedContact): a fast-falling ball can approach the wall horizontally while
		//    separating along the undercut normal. Then the wall is resolved with the detection's horizontal normal (DECISION);
		//  * a zero-speed contact (e.g. leaving the pivot exactly on the rounded edge, where the free flight has the rim's
		//    curvature) or a resting one (approach < v_rest, e = 0) leaves along the detection normal at v_rest (DECISION: the
		//    termination guarantee of a surface Level A cannot roll on; v_rest = 2 mm/s is below every visible scale);
		//  * a wall-like contact (the liner, the rim where it is steeper than 45 deg) leaves at least kPocketWallMinExitAngle into the
		//    hole (TurnOffPocketWall, SimIslandInternal.h): a ball running around the wall is a polygon of grazing contacts, not a
		//    chain of thousands of v_rest micro-contacts (review fix).
		BallState ResolvePocketInterior(Workspace& Ws, int Ball, const TableFeatureRef& Feature, const BallState& S, ShotEvent& E)
		{
			const TableGeometry& Table = TableOf(Ws);
			FixedContact Contact = MakeFixedContact(Feature, Table, S, SpecOf(Ws, Ball), Ws.Detection);
			Vec3 Detected = Contact.Normal;
			if (Feature.Kind == TableFeatureKind::LinerWall && Feature.Index < Table.Pockets.Size())
			{
				Detected = Normalized(ToVec3(Table.Pockets[Feature.Index].CaptureCenter) - Planar(S.Position)); // toward the axis
				if (!(-Dot(S.Velocity, Contact.Normal) > 0.0) && -Dot(S.Velocity, Detected) > 0.0)
				{
					Contact.Normal = Detected;
				}
			}
			const CushionImpactResult R =
				ResolveFixedContact(Contact, S, SpecOf(Ws, Ball), Ws.Params.Cushion, Ws.Params.PocketContacts, Ws.Params.Cloth, Ws.Params.Numerics);
			BallState Out = S;
			Out.Velocity = R.Velocity;
			Out.Omega = R.Omega;
			const bool WallLike = Feature.Kind == TableFeatureKind::LinerWall || Detected.x * Detected.x + Detected.y * Detected.y >= Detected.z * Detected.z;
			if (WallLike && Feature.Index < Table.Pockets.Size())
			{
				Out.Velocity = TurnOffPocketWall(Table.Pockets[Feature.Index], S.Position, Out.Velocity);
			}
			const double Floor = Ws.Params.Numerics.RestSpeed;
			const double Separation = Dot(Out.Velocity, Detected);
			if (Separation < Floor)
			{
				Out.Velocity += Detected * (Floor - Separation);
			}
			E.Normal = Contact.Normal;
			E.NormalSpeed = R.NormalSpeed;
			E.NormalImpulse = R.NormalImpulse;
			E.Flags = static_cast<std::uint8_t>(E.Flags | ShotEventFlags::Airborne | (R.Stick ? ShotEventFlags::Stick : 0) |
				(R.Resting || Separation < Floor ? ShotEventFlags::Resting : 0));
			E.Post[0] = Out;
			return Out;
		}

		// A ball inside the pocket cylinder below the cloth level stays PocketFall; above it, it is an ordinary flight.
		void ClassifyPocketState(Workspace& Ws, int Ball, BallState& S, int Pocket)
		{
			const TableGeometry& Table = TableOf(Ws);
			const double R = SpecOf(Ws, Ball).Radius;
			BallSlot& B = Ws.Balls[Ball];
			const bool InsideCylinder = Pocket >= 0 && Pocket < Table.Pockets.Size() &&
				LengthSquared(XY(S.Position) - Table.Pockets[Pocket].CaptureCenter) < Square(Table.Pockets[Pocket].DropEdgeRadius);
			if (InsideCylinder && S.Position.z <= R + Ws.Params.Numerics.EpsZ)
			{
				S.State = MotionState::PocketFall;
				B.Context.Pocket = static_cast<PocketId>(Pocket);
				return;
			}
			if (B.Seg.State == MotionState::PocketFall && InsideCylinder)
			{
				S.State = MotionState::PocketFall; // still inside the hole: PocketExit (center beyond a_d, z > R) hands it over
				return;
			}
			S.State = MotionState::Airborne;
			ClassifyState(S, R, 0.0, Ws.Params.Numerics);
			if (S.State != MotionState::Airborne)
			{
				// Back on a surface (a contact that stopped a ball exactly at the cloth level): the cloth.
				B.Context.Support = SupportKind::Cloth;
			}
			B.Context.Pocket = PocketId::None;
		}

		// Apex center height of the flight that starts in state S [m].
		double ApexHeight(const BallState& S, double Gravity)
		{
			return S.Velocity.z > 0.0 ? S.Position.z + S.Velocity.z * S.Velocity.z / (2.0 * Gravity) : S.Position.z;
		}

		// Liner / back wall or rim torus contact of a ball in state S (GRI, never the on-cloth models), then the pocket state.
		void PocketWallContact(Workspace& Ws, int Ball, const TableFeatureRef& Feature, BallState S, MotionState State, double Time)
		{
			S.State = State == MotionState::Airborne ? MotionState::Airborne : MotionState::PocketFall;
			ShotEvent E = MakeBallEvent(ContactEventType(Feature.Kind), Time, Ball, S);
			E.Feature = static_cast<std::uint8_t>(Feature.Index);
			BallState After = ResolvePocketInterior(Ws, Ball, Feature, S, E);
			ClassifyPocketState(Ws, Ball, After, Feature.Index);
			E.Post[0] = After;
			EmitEvent(Ws, E);
			NoteRailContact(Ws, Ball);
			ReplaceSegment(Ws, Ball, After, Time);
		}

		// Pivot end / truncation: the pivot's Sampled track, then free flight inside the pocket.
		void LeavePivot(Workspace& Ws, int Ball, const BallState& S, double Time)
		{
			SamplePivotTrack(Ws, Ball, Time);
			BallState Free = S;
			Free.State = MotionState::PocketFall;
			ReplaceSegment(Ws, Ball, Free, Time);
		}

		void SlateLanding(Workspace& Ws, int Ball, const BallState& S, double Time)
		{
			BallSlot& B = Ws.Balls[Ball];
			const PhysicsParams& P = Ws.Params;
			const BallSpec& Spec = SpecOf(Ws, Ball);
			B.SequenceMaxZ = Max(B.SequenceMaxZ, ApexHeight(EvaluateSegment(B.Seg, 0.0), P.Gravity));
			++B.BounceIndex;
			const SlateImpactResult Impact =
				ResolveSlateImpact(S.Velocity, S.Omega, Spec, P.Slate.Restitution, P.Cloth.SlidingFriction, P.Slate, B.BounceIndex, P.Gravity, P.Numerics);
			BallState After = S;
			After.Velocity = Impact.Velocity;
			After.Omega = Impact.Omega;
			ShotEvent E = MakeBallEvent(ShotEventType::BallSlate, Time, Ball, S);
			E.SubFeature = static_cast<std::uint8_t>(Min(static_cast<double>(B.BounceIndex), 255.0));
			E.Normal = Vec3::UnitZ();
			E.NormalSpeed = -S.Velocity.z;
			E.NormalImpulse = Spec.Mass * Impact.NormalImpulsePerMass;
			E.Flags = Impact.Stick ? ShotEventFlags::Stick : 0;
			After.State = MotionState::Airborne;
			ClassifyState(After, Spec.Radius, 0.0, P.Numerics);
			E.Post[0] = After;
			EmitEvent(Ws, E);
			B.Context.Support = SupportKind::Cloth;
			B.Context.Pocket = PocketId::None;
			if (After.State == MotionState::Airborne)
			{
				B.SequenceMaxZ = Max(B.SequenceMaxZ, ApexHeight(After, P.Gravity)); // next flight of the same sequence
			}
			else
			{
				ShotEvent Land = MakeBallEvent(ShotEventType::BallLand, Time, Ball, After);
				Land.Value = B.SequenceMaxZ;
				EmitEvent(Ws, Land);
				B.BounceIndex = 0;
				B.SequenceMaxZ = 0.0;
			}
			ReplaceSegment(Ws, Ball, After, Time);
		}
	}

	void EnterPocketOverDropEdge(Workspace& Ws, int Ball, const BallState& State, int Pocket, double Time)
	{
		const TableGeometry& Table = TableOf(Ws);
		BallSlot& B = Ws.Balls[Ball];
		const PocketGeometry& G = Table.Pockets[Pocket];
		const BallSpec& Spec = SpecOf(Ws, Ball);
		BallState S = State;
		S.Position.z = Spec.Radius;
		S.Velocity.z = 0.0;
		const Vec2 ToCenter = G.CaptureCenter - XY(S.Position);
		const double Distance = Length(ToCenter);
		const Vec2 Normal = Distance > 0.0 ? ToCenter / Distance : G.Axis;
		const double NormalSpeed = Dot(XY(S.Velocity), Normal);

		ShotEvent E = MakeBallEvent(ShotEventType::BallPocketEnter, Time, Ball, State);
		E.Feature = static_cast<std::uint8_t>(Pocket);
		E.Normal = ToVec3(Normal);
		E.NormalSpeed = NormalSpeed;
		E.Value = NormalSpeed;
		B.Context.Pocket = static_cast<PocketId>(Pocket);
		B.Context.Support = SupportKind::Cloth;

		const PivotPath Path = MakePivotPath(S, Time, G, Spec, Ws.Params.Gravity, Ws.Params.Numerics);
		if (Path.Result.Immediate)
		{
			// v_perp >= sqrt(g rho): the ball leaves the edge at once and flies across the hole (5.4).
			S.State = MotionState::PocketFall;
			E.Post[0] = S;
			EmitEvent(Ws, E);
			ReplaceSegment(Ws, Ball, S, Time);
			return;
		}
		B.Pivot = Path;
		BallState Pivoting = EvaluatePivot(Path, 0.0);
		E.Post[0] = Pivoting;
		EmitEvent(Ws, E);
		ReplaceSegment(Ws, Ball, Pivoting, Time); // Seg = PivotDetectionProxy(Pivot); end slot = pivot end
	}

	void CaptureInCircle(Workspace& Ws, int Ball, const BallState& State, int Pocket, double Time)
	{
		const PocketGeometry& G = TableOf(Ws).Pockets[Pocket];
		const Vec2 ToCenter = G.CaptureCenter - XY(State.Position);
		const double Distance = Length(ToCenter);
		const double NormalSpeed = Distance > 0.0 ? Dot(XY(State.Velocity), ToCenter / Distance) : 0.0;
		ShotEvent Enter = MakeBallEvent(ShotEventType::BallPocketEnter, Time, Ball, State);
		Enter.Feature = static_cast<std::uint8_t>(Pocket);
		Enter.NormalSpeed = NormalSpeed;
		Enter.Value = NormalSpeed;
		EmitEvent(Ws, Enter);
		ShotEvent Pocketed = MakeBallEvent(ShotEventType::BallPocketed, Time, Ball, State);
		Pocketed.Feature = static_cast<std::uint8_t>(Pocket);
		EmitEvent(Ws, Pocketed);
		Ws.Balls[Ball].Context.Pocket = static_cast<PocketId>(Pocket);
		MakeTerminal(Ws, Ball, MotionState::Pocketed, Time, static_cast<PocketId>(Pocket), OffTableReason::Floor);
	}

	bool ProcessPocketEvent(Workspace& Ws, int Ball, const TableFeatureRef& Feature, double Time)
	{
		if (Ws.Input == nullptr || Ws.Input->Table == nullptr || Ball < 0 || Ball >= kMaxBalls)
		{
			return false;
		}
		const TableGeometry& Table = TableOf(Ws);
		BallSlot& B = Ws.Balls[Ball];
		const MotionState State = B.Seg.State;
		const int Index = Feature.Index;
		switch (Feature.Kind)
		{
		case TableFeatureKind::DropEdge:
		{
			if (!IsOnSurface(State) || B.Context.Support != SupportKind::Cloth || Index >= Table.Pockets.Size())
			{
				return false;
			}
			EnterPocketOverDropEdge(Ws, Ball, BallStateForEvent(Ws, Ball, Time), Index, Time);
			return true;
		}
		case TableFeatureKind::CaptureCircle:
		{
			if (Index >= Table.Pockets.Size() || IsTerminal(State))
			{
				return false;
			}
			CaptureInCircle(Ws, Ball, BallStateForEvent(Ws, Ball, Time), Index, Time);
			return true;
		}
		case TableFeatureKind::CaptureDepth:
		{
			if (Index >= Table.Pockets.Size() || IsTerminal(State))
			{
				return false;
			}
			ShotEvent E = MakeBallEvent(ShotEventType::BallPocketed, Time, Ball, BallStateForEvent(Ws, Ball, Time));
			E.Feature = static_cast<std::uint8_t>(Index);
			EmitEvent(Ws, E);
			MakeTerminal(Ws, Ball, MotionState::Pocketed, Time, static_cast<PocketId>(Index), OffTableReason::Floor);
			return true;
		}
		case TableFeatureKind::PocketExit:
		{
			if (Index >= Table.Pockets.Size() || State != MotionState::PocketFall)
			{
				return false;
			}
			BallState S = BallStateForEvent(Ws, Ball, Time);
			const PocketGeometry& G = Table.Pockets[Index];
			if (!OnFrontArc(G, XY(S.Position)) && S.Position.z < G.WallTopZ)
			{
				// Behind the front arc the back wall stands up to WallTopZ (collisions 5.3): a center below its top cannot leave the hole
				// there. The wall contact was missed: PredictLinerWall only finds outward crossings of r_p - R, and a ball that bounces
				// off the rim near a front-arc end into the back sector already beyond r_p - R (past the end of a facing, where the back
				// wall starts) never crosses it (the WP-5 / WP-2 facing-to-back-wall junction). By now the ball is embedded in the wall and
				// the rail above it, so neither a flight (it went through the rail and off the table, VAL ROB-14) nor a late wall
				// contact (the rail cap's plane then lifts it onto the rail) is right. Count it like a landing behind a nose line
				// (collisions 6.1 step 3) and take the outcome of the missed contact: a ball thrown against the back wall below its
				// top with e_l = 0.3 falls into the hole, so it is pocketed here (review fix, DECISION).
				++Ws.Result->Diagnostics.MissedEvents;
				ShotEvent D = MakeBallEvent(ShotEventType::Diagnostic, Time, Ball, S);
				D.Feature = static_cast<std::uint8_t>(TableFeatureKind::LinerWall);
				D.Value = Length(XY(S.Position) - G.CaptureCenter) - (G.CaptureRadius - SpecOf(Ws, Ball).Radius); // how far behind the wall
				EmitEvent(Ws, D);
				ShotEvent E = MakeBallEvent(ShotEventType::BallPocketed, Time, Ball, S);
				E.Feature = static_cast<std::uint8_t>(Index);
				EmitEvent(Ws, E);
				MakeTerminal(Ws, Ball, MotionState::Pocketed, Time, static_cast<PocketId>(Index), OffTableReason::Floor);
				return true;
			}
			ShotEvent E = MakeBallEvent(ShotEventType::BallPocketExit, Time, Ball, S);
			E.Feature = static_cast<std::uint8_t>(Index);
			S.State = MotionState::Airborne;
			ClassifyState(S, SpecOf(Ws, Ball).Radius, 0.0, Ws.Params.Numerics);
			E.Post[0] = S;
			EmitEvent(Ws, E);
			B.Context.Pocket = PocketId::None;
			B.Context.Support = SupportKind::Cloth;
			ReplaceSegment(Ws, Ball, S, Time);
			return true;
		}
		case TableFeatureKind::LinerWall:
		case TableFeatureKind::RimTorus:
		{
			if (Index >= Table.Pockets.Size() || IsOnSurface(State) || IsTerminal(State))
			{
				return false;
			}
			BallState S = BallStateForEvent(Ws, Ball, Time);
			if (State == MotionState::PocketPivot)
			{
				S = EvaluatePivot(B.Pivot, Time - B.Pivot.T0);
				SamplePivotTrack(Ws, Ball, Time);
			}
			PocketWallContact(Ws, Ball, Feature, S, State, Time); // GRI: never the on-cloth models
			return true;
		}
		case TableFeatureKind::FacingFace:
		case TableFeatureKind::FacingTopEdge:
		case TableFeatureKind::JawArc:
		{
			// Truncation of the pivot by a facing / jaw arc found on the detection proxy (5.4): GRI with the ball free, then
			// free flight in the pocket. Contacts of every other state are the loop's cushion-like contacts.
			if (State != MotionState::PocketPivot)
			{
				return false;
			}
			BallState S = EvaluatePivot(B.Pivot, Time - B.Pivot.T0);
			S.State = MotionState::PocketFall;
			ShotEvent E = MakeBallEvent(ShotEventType::BallJaw, Time, Ball, S);
			E.Feature = static_cast<std::uint8_t>(Index / 2);
			E.SubFeature = static_cast<std::uint8_t>((Index & 1) | (JawElement(Feature.Kind) << 4));
			BallState After = S;
			if (Index < 2 * Table.Pockets.Size())
			{
				After = ResolveContact(Ws, Ball, Feature, S, E);
			}
			After.State = MotionState::PocketFall;
			E.Post[0] = After;
			EmitEvent(Ws, E);
			NoteRailContact(Ws, Ball);
			LeavePivot(Ws, Ball, After, Time);
			return true;
		}
		case TableFeatureKind::None:
		{
			// End slot of a pivot: the ball leaves the rounded edge at psi_leave and falls freely (5.4).
			if (State != MotionState::PocketPivot)
			{
				return false;
			}
			LeavePivot(Ws, Ball, PivotLeaveState(B.Pivot), Time);
			return true;
		}
		default:
			return false;
		}
	}

	void RouteLanding(Workspace& Ws, int Ball, double Time)
	{
		if (Ws.Input == nullptr || Ws.Input->Table == nullptr || Ball < 0 || Ball >= kMaxBalls)
		{
			return;
		}
		BallSlot& B = Ws.Balls[Ball];
		if (B.Seg.State != MotionState::Airborne)
		{
			return;
		}
		// The exact state just before the landing (z = R, the exact impact speed), motion C.2.
		BallState S = SegmentEndState(B.Seg, Ws.Params.Numerics);
		int Pocket = -1;
		switch (ClassifyLandingPoint(TableOf(Ws), Ws.Params.Pockets, XY(S.Position), Pocket))
		{
		case LandingSurface::PocketHole:
		{
			// No slate below: the ball keeps falling in the pocket (collisions 6.1 step 1); CaptureCircle pockets capture at once.
			if (Ws.Params.Pockets == PocketModel::CaptureCircle)
			{
				CaptureInCircle(Ws, Ball, S, Pocket, Time);
				return;
			}
			ShotEvent E = MakeBallEvent(ShotEventType::BallPocketEnter, Time, Ball, S);
			E.Feature = static_cast<std::uint8_t>(Pocket);
			E.NormalSpeed = -S.Velocity.z;
			E.Value = -S.Velocity.z;
			S.State = MotionState::PocketFall;
			E.Post[0] = S;
			EmitEvent(Ws, E);
			B.Context.Pocket = static_cast<PocketId>(Pocket);
			B.Context.Support = SupportKind::Cloth;
			B.BounceIndex = 0;
			B.SequenceMaxZ = 0.0;
			ReplaceSegment(Ws, Ball, S, Time);
			return;
		}
		case LandingSurface::PlayingSurface:
		case LandingSurface::Shelf:
			SlateLanding(Ws, Ball, S, Time);
			return;
		case LandingSurface::OverRail:
		{
			// Behind a nose line the cloth is not there: a rail-top or nose event must have fired first (collisions 6.1
			// step 3). Log it and recover with a slate impact so that the shot continues.
			++Ws.Result->Diagnostics.MissedEvents;
			ShotEvent D = MakeBallEvent(ShotEventType::Diagnostic, Time, Ball, S);
			D.Value = S.Position.z;
			EmitEvent(Ws, D);
			SlateLanding(Ws, Ball, S, Time);
			return;
		}
		}
	}
}
