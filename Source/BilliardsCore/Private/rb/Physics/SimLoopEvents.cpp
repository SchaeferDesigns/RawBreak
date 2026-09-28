#include "rb/Core/FpGuard.h"
// Owner: WP-6a (simulator core loop). Spec: Docs/architecture.md 8.5 (processing per event kind), 8.6 (cue strikes and the
// follow-through tip), 8.11 (exact re-anchor, chalk-mark cling), 9 (Zeno guards); physics-collisions 2.4 (ball-ball steps 1-8), 3.6
// (pressing contacts), 3.9.2 (island test with delta_cl), 4.7 (fixed contacts), 6.3 (off table), 7.1-7.4; physics-motion-and-cue
// implementation notes 2, 6, 12.
#include "SimInternal.h"

#include "rb/Math/Scalar.h"
#include "rb/Physics/Playback.h"

namespace rb::sim
{
	namespace
	{
		const BallSpec& SpecOf(const Workspace& Ws, int Ball) { return Ws.Input->Balls[Ball].Spec; }

		ShotEvent MakeEvent(double Time, ShotEventType Type, int A, int B = kNoBall)
		{
			ShotEvent Event;
			Event.Time = Time;
			Event.Type = Type;
			Event.A = static_cast<BallId>(A);
			Event.B = static_cast<BallId>(B);
			return Event;
		}

		// Apex center height of a ballistic state [m].
		double Apex(const BallState& S, double Gravity)
		{
			const double Vz = Max(S.Velocity.z, 0.0);
			return S.Position.z + Vz * Vz / (2.0 * Gravity);
		}

		// A ball left its support: new airborne sequence (BallAirborne, Value = apex; bounce counter reset).
		void StartAirborne(Workspace& Ws, int Ball, const BallState& S, double Time)
		{
			BallSlot& Slot = Ws.Balls[Ball];
			Slot.BounceIndex = 0;
			Slot.SequenceMaxZ = Apex(S, Ws.Params.Gravity);
			ShotEvent Event = MakeEvent(Time, ShotEventType::BallAirborne, Ball);
			Event.Value = Slot.SequenceMaxZ;
			Event.Pre[0] = S;
			Event.Post[0] = S;
			EmitEvent(Ws, Event);
		}

		// Tip dome center of a follow-through path at absolute time Time (uniform deceleration, at rest after StopTime).
		Vec3 TipCenterAt(const CueTipPath& Path, double Time)
		{
			const double Duration = Path.StopTime > Path.StartTime ? Path.StopTime - Path.StartTime : 0.0;
			const double Tau = Clamp(Time - Path.StartTime, 0.0, Duration);
			return Path.Start + Path.Direction * (Path.Speed0 * Tau - 0.5 * Path.Deceleration * Tau * Tau);
		}

		double GapToFrozenTarget(const Workspace& Ws, int Strike, int Ball, double Time)
		{
			const int F = Ws.Tips[Strike].FrozenTarget;
			if (F == kNoBall || F == Ball)
			{
				return 0.0;
			}
			return Length(loop::CurrentPosition(Ws, F, Time) - loop::CurrentPosition(Ws, Ball, Time)) - (SpecOf(Ws, F).Radius + SpecOf(Ws, Ball).Radius);
		}

		// TipContactBegin of strike Strike on Ball; AtBall / AtTarget = the states of Ball and of the frozen target f at Time (the
		// record's positions, whatever EventStates).
		void EmitTipBegin(Workspace& Ws, int Strike, int Ball, double Time, double Gap, const BallState& AtBall, const BallState& AtTarget)
		{
			const TipSlot& Tip = Ws.Tips[Strike];
			ShotEvent Event = MakeEvent(Time, ShotEventType::TipContactBegin, Ball, Tip.FrozenTarget);
			Event.Feature = static_cast<std::uint8_t>(Strike);
			Event.SubFeature = static_cast<std::uint8_t>(Tip.StruckTouchedOther ? 1 : 0);
			Event.Value = Gap;
			Event.Pre[0] = AtBall;
			Event.Post[0] = AtBall;
			if (Tip.FrozenTarget != kNoBall)
			{
				Event.Pre[1] = AtTarget;
				Event.Post[1] = AtTarget;
			}
			EmitEvent(Ws, Event);
		}

		// Appends a new analytic cue-tip piece (closing the open one at its start). Recorded with Trajectories only.
		void OpenTipPiece(Workspace& Ws, int Strike)
		{
			TipSlot& Tip = Ws.Tips[Strike];
			if (!Ws.Input->Record.Trajectories)
			{
				return;
			}
			std::vector<CueTipSegment>& Pieces = Ws.Result->CueTips;
			if (Tip.OpenCueTipSegment >= 0 && Tip.OpenCueTipSegment < static_cast<int>(Pieces.size()))
			{
				CueTipSegment& Open = Pieces[static_cast<std::size_t>(Tip.OpenCueTipSegment)];
				Open.T1 = Min(Open.T1, Tip.Path.StartTime);
			}
			if (static_cast<int>(Pieces.size()) >= Ws.Caps.MaxCueTipSegments)
			{
				Ws.Result->Diagnostics.CueTipOverflow = true;
				Tip.OpenCueTipSegment = -1;
				return;
			}
			CueTipSegment Piece;
			Piece.Strike = Strike;
			Piece.Path = Tip.Path;
			Piece.T1 = Tip.Path.StopTime;
			Piece.Kind = SegmentKind::Analytic;
			Pieces.push_back(Piece);
			Tip.OpenCueTipSegment = static_cast<int>(Pieces.size()) - 1;
		}

		// ---------------------------------------------------------------------------------------------------------------
		// Zeno detector (guard 5): ZenoContactCount contacts of the same pair / ball-feature slot within ZenoWindow.
		// ---------------------------------------------------------------------------------------------------------------
		bool RecordZenoContact(Workspace& Ws, int A, int B, const TableFeatureRef& Feature, double Time)
		{
			const std::uint8_t KeyA = static_cast<std::uint8_t>(A);
			const std::uint8_t KeyB = B >= 0 ? static_cast<std::uint8_t>(B) : kNoBallSlot;
			const std::uint8_t Kind = static_cast<std::uint8_t>(Feature.Kind);
			ZenoEntry* Entry = nullptr;
			for (ZenoEntry& E : Ws.Zeno)
			{
				if (E.BallA == KeyA && E.BallB == KeyB && E.FeatureKind == Kind && E.FeatureIndex == Feature.Index && E.FeatureSub == Feature.SubIndex)
				{
					Entry = &E;
					break;
				}
			}
			if (Entry == nullptr)
			{
				ZenoEntry Fresh;
				Fresh.BallA = KeyA;
				Fresh.BallB = KeyB;
				Fresh.FeatureKind = Kind;
				Fresh.FeatureIndex = Feature.Index;
				Fresh.FeatureSub = Feature.SubIndex;
				if (!Ws.Zeno.PushBack(Fresh))
				{
					// Full: replace the entry whose latest contact is the oldest (first such entry on ties; deterministic).
					int Oldest = 0;
					double OldestTime = kInfinity;
					for (int i = 0; i < Ws.Zeno.Size(); ++i)
					{
						const ZenoEntry& E = Ws.Zeno[i];
						const double Last = E.Count > 0 ? E.Times[(E.Count - 1) % kZenoHistoryLength] : -kInfinity;
						if (Last < OldestTime)
						{
							OldestTime = Last;
							Oldest = i;
						}
					}
					Ws.Zeno[Oldest] = Fresh;
					Entry = &Ws.Zeno[Oldest];
				}
				else
				{
					Entry = &Ws.Zeno.Back();
				}
			}
			Entry->Times[Entry->Count % kZenoHistoryLength] = Time;
			++Entry->Count;
			const int Needed = Ws.Params.Numerics.ZenoContactCount;
			if (Needed > kZenoHistoryLength || Entry->Count < Needed)
			{
				return false; // a count beyond the history disables the detector
			}
			const double First = Entry->Times[(Entry->Count - Needed) % kZenoHistoryLength];
			if (Time - First <= Ws.Params.Numerics.ZenoWindow)
			{
				Entry->Count = 0; // cleared: the island takes over (histories are also cleared at island start and exit)
				return true;
			}
			return false;
		}

		void EmitZenoGuard(Workspace& Ws, int A, int B, const TableFeatureRef& Feature, double Time)
		{
			++Ws.Result->Diagnostics.ZenoTriggers;
			ShotEvent Event = MakeEvent(Time, ShotEventType::ZenoGuard, A, B);
			Event.Feature = static_cast<std::uint8_t>(Feature.Kind);
			Event.SubFeature = Feature.Index;
			EmitEvent(Ws, Event);
		}

		// ---------------------------------------------------------------------------------------------------------------
		// Island test of an event-mode ball-ball contact (architecture 8.5, collisions 3.9.2 steps 1-3): does the BFS from {I, J}
		// with delta_cl add anything - another ball, a table feature, or a moving cue tip within Join of I or J?
		// ---------------------------------------------------------------------------------------------------------------
		bool NeedsIsland(Workspace& Ws, int I, int J, const Vec3& PI, const Vec3& PJ, double Time, double Join)
		{
			const double RI = SpecOf(Ws, I).Radius;
			const double RJ = SpecOf(Ws, J).Radius;
			for (int k = 0; k < kMaxBalls; ++k)
			{
				if (k == I || k == J || !loop::IsLive(Ws, k))
				{
					continue;
				}
				const Vec3 PK = loop::CurrentPosition(Ws, k, Time);
				const double RK = SpecOf(Ws, k).Radius;
				if (Length(PK - PI) - (RK + RI) <= Join || Length(PK - PJ) - (RK + RJ) <= Join)
				{
					return true;
				}
			}
			const TableGeometry& Table = *Ws.Input->Table;
			const Vec3 Centers[2] = {PI, PJ};
			const double Radii[2] = {RI, RJ};
			for (int m = 0; m < 2; ++m)
			{
				const double Reach = Radii[m] + Join + Ws.Detection.NoseProfileRadius + 1e-6;
				const Aabb3 Region{Centers[m] - Vec3{Reach, Reach, Reach}, Centers[m] + Vec3{Reach, Reach, Reach}};
				TableFeatureRef Features[64];
				bool Overflow = false;
				const int Count = QueryTableFeatures(Region, Table, Ws.Detection, Features, 64, Overflow);
				for (int f = 0; f < Count; ++f)
				{
					if (loop::FeatureGap(Ws, Features[f], Centers[m], Radii[m]) <= Join)
					{
						return true;
					}
				}
			}
			for (const TipSlot& Tip : Ws.Tips)
			{
				if (!(Tip.Moving || Tip.InIsland) || !(Time < Tip.Path.StopTime))
				{
					continue;
				}
				const Vec3 C = TipCenterAt(Tip.Path, Time);
				if (Length(C - PI) - (Tip.Path.DomeRadius + RI) <= Join || Length(C - PJ) - (Tip.Path.DomeRadius + RJ) <= Join)
				{
					return true;
				}
			}
			return false;
		}

		// Orientation of a ball at Time for the chalk-mark cling (architecture 8.11): the orientation law from Orientation0.
		Quat OrientationForCling(const Workspace& Ws, int Ball, double Time)
		{
			const BallSlot& Slot = Ws.Balls[Ball];
			if (Slot.Seg.State == MotionState::PocketPivot)
			{
				return Slot.Orientation0;
			}
			TrajectorySegment Segment;
			Segment.Motion = Slot.Seg;
			Segment.T1 = kInfinity;
			Segment.Kind = SegmentKind::Analytic;
			return SegmentOrientationAt(Slot.Orientation0, Segment, Time - Slot.Seg.T0);
		}

		// A ball resolved while pivoting over the drop edge is free afterwards (collisions 5.4: contacts truncate the pivot).
		void FreeFromPivot(BallState& S)
		{
			if (S.State == MotionState::PocketPivot)
			{
				S.State = MotionState::PocketFall;
			}
		}

		// Post-impulse table step and classification (collisions 2.4 steps 6-7).
		void Settle(Workspace& Ws, int Ball, const BallState& Before, BallState& After)
		{
			const PhysicsParams& P = Ws.Params;
			ApplyTableReaction(After, IsOnSurface(Before.State), SpecOf(Ws, Ball), loop::SupportSurface(Ws, Ball), P.Slate, P.Gravity, P.Numerics);
			FreeFromPivot(After);
			ClassifyState(After, SpecOf(Ws, Ball).Radius, loop::SupportHeight(Ws, Ball), P.Numerics);
		}

		// ---------------------------------------------------------------------------------------------------------------
		// Event handlers (architecture 8.5)
		// ---------------------------------------------------------------------------------------------------------------

		// Snap diagnostics (prior-art 5.8): the residual a level transition snap discards must be <= SnapResidualRel of the
		// magnitude it acts on; a larger one is logged as a Diagnostic event (never corrected).
		void CheckSnap(Workspace& Ws, int Ball, const MotionSegment& Seg, double Time)
		{
			if (Seg.Tilt.Active || !(Seg.TauEnd > 0.0))
			{
				return;
			}
			const BallState Evolved = EvaluateSegment(Seg, Seg.TauEnd);
			double Residual = 0.0;
			double Scale = 0.0;
			if (Seg.State == MotionState::Sliding)
			{
				const Vec3 U = Planar(SlipVelocity(Evolved.Velocity, Evolved.Omega, Seg.Radius));
				Residual = Length(U);
				Scale = Max(Length(Planar(Seg.Vel0)), Length(Planar(SlipVelocity(Seg.Vel0, Seg.Omega0, Seg.Radius))));
			}
			else if (Seg.State == MotionState::Rolling)
			{
				Residual = Length(Planar(Evolved.Velocity));
				Scale = Length(Planar(Seg.Vel0));
			}
			else
			{
				return;
			}
			if (Residual > Ws.Params.Numerics.SnapResidualRel * Scale + Ws.Params.Numerics.EpsV)
			{
				ShotEvent Event = MakeEvent(Time, ShotEventType::Diagnostic, Ball);
				Event.Value = Residual;
				EmitEvent(Ws, Event);
			}
		}

		void ProcessEndSlot(Workspace& Ws, const QueuedEvent& E)
		{
			const int Ball = E.BallA;
			const double Time = E.Time;
			BallSlot& Slot = Ws.Balls[Ball];
			const MotionSegment Seg = Slot.Seg;
			if (Seg.State == MotionState::Airborne)
			{
				RouteLanding(Ws, Ball, Time); // collisions 6.1 (WP-6b)
				return;
			}
			if (Seg.State == MotionState::PocketPivot && !Slot.PivotPieceLast)
			{
				loop::AdvancePivotPiece(Ws, Ball, Time); // a node of the piecewise detection proxy (WP-10, SimInternal.h)
				return;
			}
			if (Seg.State == MotionState::PocketPivot)
			{
				const PocketId Pocket = Slot.Pivot.Pocket != PocketId::None ? Slot.Pivot.Pocket : Slot.Context.Pocket;
				ProcessPocketEvent(Ws, Ball, TableFeatureRef{TableFeatureKind::None, static_cast<std::uint8_t>(Pocket), 0}, Time); // pivot end
				return;
			}
			const BallState S = SegmentEndState(Seg, Ws.Params.Numerics);
			CheckSnap(Ws, Ball, Seg, Time);
			ShotEvent Event = MakeEvent(Time, ShotEventType::MotionTransition, Ball);
			Event.From = Seg.State;
			Event.To = S.State;
			Event.Pre[0] = EvaluateSegment(Seg, Seg.TauEnd); // the record's position, whatever EventStates (EmitEvent)
			Event.Post[0] = S;
			EmitEvent(Ws, Event);
			if (S.State == MotionState::Stationary && Slot.Context.Support == SupportKind::RailCap)
			{
				// Came to rest on the flat rail cap: off the table (WPA 2.6, collisions 6.2; architecture 8.5).
				ShotEvent Off = MakeEvent(Time, ShotEventType::BallOffTable, Ball);
				Off.Feature = static_cast<std::uint8_t>(OffTableReason::RestsOnRailOrFrame);
				Off.Pre[0] = S;
				Off.Post[0] = S;
				EmitEvent(Ws, Off);
				MakeTerminal(Ws, Ball, MotionState::OffTable, Time, PocketId::None, OffTableReason::RestsOnRailOrFrame);
				return;
			}
			loop::SetSegment(Ws, Ball, S, Time, false);
			loop::PredictBalls(Ws, 1u << Ball);
		}

		void ProcessTiltRefresh(Workspace& Ws, const QueuedEvent& E)
		{
			// Architecture 8.11: the exact node state starts the next chain piece (no snap, no impulse); version bump, observers on
			// [t, ...) with IncludeFrom, every slot re-predicted. Logged only with LogTransitions, never record-relevant.
			const int Ball = E.BallA;
			const MotionSegment Seg = Ws.Balls[Ball].Seg;
			const BallState S = SegmentEndState(Seg, Ws.Params.Numerics);
			++Ws.Result->Diagnostics.TiltRefreshes;
			ShotEvent Event = MakeEvent(E.Time, ShotEventType::TiltRefresh, Ball);
			Event.From = S.State;
			Event.To = S.State;
			Event.Pre[0] = S;
			Event.Post[0] = S;
			EmitEvent(Ws, Event);
			loop::SetSegment(Ws, Ball, S, E.Time, false);
			loop::PredictBalls(Ws, 1u << Ball);
		}

		bool IsCushionLike(TableFeatureKind Kind)
		{
			return Kind == TableFeatureKind::NoseSegment || Kind == TableFeatureKind::JawArc || Kind == TableFeatureKind::FacingFace ||
				Kind == TableFeatureKind::FacingTopEdge;
		}

		// Cushion-like fixed contact (nose, jaw arc, facing face / top edge; liner and rim when WP-6b does not consume them):
		// MakeFixedContact -> ResolveFixedContact (collisions 4.7), table step, classification, the rules event.
		void ResolveFixedFeature(Workspace& Ws, const QueuedEvent& E, const TableFeatureRef& Feature)
		{
			const int Ball = E.BallA;
			const double Time = E.Time;
			const PhysicsParams& P = Ws.Params;
			if ((E.Flags & ContactFlags::Pressing) != 0u)
			{
				IslandSeed Seed;
				Seed.BallA = Ball;
				Seed.Feature = Feature;
				Seed.Pressing = true;
				loop::HandOffToIsland(Ws, Seed, Time); // zero normal speed: never an impulse (collisions 3.6, pitfall 16)
				return;
			}
			if (RecordZenoContact(Ws, Ball, -1, Feature, Time))
			{
				EmitZenoGuard(Ws, Ball, kNoBall, Feature, Time);
				IslandSeed Seed;
				Seed.BallA = Ball;
				Seed.Feature = Feature;
				Seed.Zeno = true;
				loop::HandOffToIsland(Ws, Seed, Time);
				return;
			}
			const BallSpec& Spec = SpecOf(Ws, Ball);
			const BallState Before = BallStateForEvent(Ws, Ball, Time);
			const FixedContact Contact = MakeFixedContact(Feature, *Ws.Input->Table, Before, Spec, Ws.Detection);
			const bool OnClothModel = Contact.BallOnCloth &&
				(Contact.Kind == FixedContactKind::NoseEdge || Contact.Kind == FixedContactKind::JawArcEdge || Contact.Kind == FixedContactKind::FacingFace);
			const double Approach = OnClothModel ? Dot(Before.Velocity, Planar(Contact.IntoFeature)) : -Dot(Before.Velocity, Contact.Normal);
			if (!(Approach > P.Numerics.ApproachSpeedTol))
			{
				// Approaching by the segment (detection) but not by the exact state (tilt re-anchor, architecture 8.11): a pressing
				// contact for the island, never dropped.
				IslandSeed Seed;
				Seed.BallA = Ball;
				Seed.Feature = Feature;
				Seed.Pressing = true;
				loop::HandOffToIsland(Ws, Seed, Time);
				return;
			}
			const CushionImpactResult R = ResolveFixedContact(Contact, Before, Spec, P.Cushion, P.PocketContacts, loop::SupportSurface(Ws, Ball), P.Numerics);
			BallState After = Before;
			After.Velocity = R.Velocity;
			After.Omega = R.Omega;
			Settle(Ws, Ball, Before, After);

			ShotEvent Event = MakeEvent(Time, ShotEventType::BallCushion, Ball);
			int Rail = -1;
			const int Side = Feature.Index % 2;
			switch (Feature.Kind)
			{
			case TableFeatureKind::NoseSegment:
				Event.Type = ShotEventType::BallCushion;
				Event.Feature = Feature.Index;
				Rail = Feature.Index;
				break;
			case TableFeatureKind::JawArc:
			case TableFeatureKind::FacingFace:
			case TableFeatureKind::FacingTopEdge:
			{
				const int Element = Feature.Kind == TableFeatureKind::JawArc ? 0 : (Feature.Kind == TableFeatureKind::FacingFace ? 1 : 2);
				Event.Type = ShotEventType::BallJaw;
				Event.Feature = static_cast<std::uint8_t>(Feature.Index / 2);
				Event.SubFeature = static_cast<std::uint8_t>(Side | (Element << 4));
				Rail = kCushionCount + Feature.Index;
				break;
			}
			case TableFeatureKind::LinerWall:
				Event.Type = ShotEventType::BallLiner;
				Event.Feature = Feature.Index;
				break;
			case TableFeatureKind::RimTorus:
				Event.Type = ShotEventType::BallPocketRim;
				Event.Feature = Feature.Index;
				break;
			default:
				break;
			}
			const Vec3 Delta = (R.Velocity - Before.Velocity) * Spec.Mass;
			const double Normal = Dot(Delta, Contact.Normal);
			Event.Normal = Contact.Normal;
			Event.NormalSpeed = R.NormalSpeed;
			Event.NormalImpulse = R.NormalImpulse;
			Event.TangentImpulse = Length(Delta - Contact.Normal * Normal);
			std::uint8_t Flags = 0;
			if (R.Resting)
			{
				Flags |= ShotEventFlags::Resting; // e = 0 micro-impact (collisions 7.1, 7.3; ROB-05 "restingOnCushion")
			}
			if (R.Stick)
			{
				Flags |= ShotEventFlags::Stick;
			}
			if (!Contact.BallOnCloth)
			{
				Flags |= ShotEventFlags::Airborne;
			}
			if ((E.Flags & ContactFlags::AtStart) != 0u)
			{
				Flags |= ShotEventFlags::AtStart;
			}
			if (Rail >= 0 && (Ws.Balls[Ball].InitialFreezeRails & (1u << Rail)) != 0u)
			{
				Flags |= ShotEventFlags::ContinuesInitialFreeze; // rules.md 3.3
			}
			Event.Flags = Flags;
			Event.Pre[0] = Before;
			Event.Post[0] = After;
			EmitEvent(Ws, Event);
			if (IsOnSurface(Before.State) && After.State == MotionState::Airborne)
			{
				StartAirborne(Ws, Ball, After, Time);
			}
			loop::SetSegment(Ws, Ball, After, Time, false);
			loop::PredictBalls(Ws, 1u << Ball);
		}

		// Guards 2 and 5 (architecture 9) for the rail-top contacts of an event-mode ball, as ResolveFixedFeature applies them to the
		// cushion-like features: a Pressing contact (zero normal speed, curving into the feature), or the ZenoContactCount-th contact
		// of the ball-feature pair within ZenoWindow, starts the rigid rail-top island (StartIsland makes every RailTop / RailTopEdge
		// seed rigid, collisions 6.2), which resolves sustained contacts. (WP-10 fix, found by PERF-05, B1 seed 745323: a ball flying
		// along the inside of a side pocket's cut rim - a concave edge whose curvature (~70 m/s^2 at 1.6 m/s) pulls it back after each
		// bounce - bounced with e_rt = 0.5 at geometrically shorter intervals until the contact was pressing; ProcessRailTopEvent
		// resolved it by GRI without an impulse, the predictor found it again at the same instant, and the shot ran into the event
		// cap: SimStatus::Aborted.) The seed state is the ball's state at the event (StartIsland); nothing else changes, so every
		// shot without such a contact is bitwise unchanged.
		bool RailTopContactToIsland(Workspace& Ws, const QueuedEvent& E, const TableFeatureRef& Feature)
		{
			const int Ball = E.BallA;
			const MotionState State = Ws.Balls[Ball].Seg.State;
			if (IsTerminal(State) || State == MotionState::PocketPivot || Feature.Index >= Ws.Input->Table->RailTops.Size())
			{
				return false; // not a rail-top contact ProcessRailTopEvent would resolve
			}
			IslandSeed Seed;
			Seed.BallA = Ball;
			Seed.Feature = Feature;
			Seed.RailTop = true;
			if ((E.Flags & ContactFlags::Pressing) != 0u)
			{
				Seed.Pressing = true;
				loop::HandOffToIsland(Ws, Seed, E.Time);
				return true;
			}
			if (RecordZenoContact(Ws, Ball, -1, Feature, E.Time))
			{
				EmitZenoGuard(Ws, Ball, kNoBall, Feature, E.Time);
				Seed.Zeno = true;
				loop::HandOffToIsland(Ws, Seed, E.Time);
				return true;
			}
			return false;
		}

		void ProcessTableFeature(Workspace& Ws, const QueuedEvent& E)
		{
			const int Ball = E.BallA;
			const double Time = E.Time;
			const TableFeatureRef Feature{static_cast<TableFeatureKind>(E.FeatureKind), E.FeatureIndex, E.FeatureSub};
			switch (Feature.Kind)
			{
			case TableFeatureKind::NoseSegment:
			case TableFeatureKind::JawArc:
			case TableFeatureKind::FacingFace:
			case TableFeatureKind::FacingTopEdge:
				ResolveFixedFeature(Ws, E, Feature);
				return;
			case TableFeatureKind::LinerWall:
			case TableFeatureKind::RimTorus:
				if (!ProcessPocketEvent(Ws, Ball, Feature, Time))
				{
					ResolveFixedFeature(Ws, E, Feature); // GRI with the element's e / mu (architecture 8.5 cushion-like row)
				}
				return;
			case TableFeatureKind::DropEdge:
			case TableFeatureKind::CaptureDepth:
			case TableFeatureKind::PocketExit:
			case TableFeatureKind::CaptureCircle:
				ProcessPocketEvent(Ws, Ball, Feature, Time);
				return;
			case TableFeatureKind::RailTop:
			case TableFeatureKind::RailTopEdge:
				if (RailTopContactToIsland(Ws, E, Feature))
				{
					return;
				}
				ProcessRailTopEvent(Ws, Ball, Feature, Time);
				return;
			case TableFeatureKind::SupportExit:
				ProcessRailTopEvent(Ws, Ball, Feature, Time);
				return;
			case TableFeatureKind::OuterBoundary:
			{
				// Collisions 6.3: the center crossed the outer rail boundary -> BallOffTable(Floor); no floor physics.
				ShotEvent Event = MakeEvent(Time, ShotEventType::BallOffTable, Ball);
				Event.Feature = static_cast<std::uint8_t>(OffTableReason::Floor);
				Event.Pre[0] = BallStateAt(Ws, Ball, Time);
				Event.Post[0] = Event.Pre[0];
				EmitEvent(Ws, Event);
				MakeTerminal(Ws, Ball, MotionState::OffTable, Time, PocketId::None, OffTableReason::Floor);
				return;
			}
			case TableFeatureKind::LampApex:
			{
				// Collisions 6.3: the flight apex reached the lamp -> BallExternalContact(Lamp) + BallOffTable(ExternalObjectRebound).
				ShotEvent Contact = MakeEvent(Time, ShotEventType::BallExternalContact, Ball);
				Contact.Feature = static_cast<std::uint8_t>(ExternalObject::Lamp);
				Contact.Pre[0] = BallStateAt(Ws, Ball, Time);
				Contact.Post[0] = Contact.Pre[0];
				EmitEvent(Ws, Contact);
				ShotEvent Off = Contact;
				Off.Type = ShotEventType::BallOffTable;
				Off.Feature = static_cast<std::uint8_t>(OffTableReason::ExternalObjectRebound);
				EmitEvent(Ws, Off);
				MakeTerminal(Ws, Ball, MotionState::OffTable, Time, PocketId::None, OffTableReason::ExternalObjectRebound);
				return;
			}
			case TableFeatureKind::SlateLanding:
			case TableFeatureKind::None:
				return;
			}
		}

		void ProcessBallBall(Workspace& Ws, const QueuedEvent& E)
		{
			const int I = E.BallA;
			const int J = E.BallB;
			const double Time = E.Time;
			const PhysicsParams& P = Ws.Params;
			IslandSeed Seed;
			Seed.BallA = I;
			Seed.BallB = J;
			if ((E.Flags & ContactFlags::Pressing) != 0u)
			{
				Seed.Pressing = true;
				loop::HandOffToIsland(Ws, Seed, Time); // collisions 3.6 pressing rule (D-12)
				return;
			}
			if (RecordZenoContact(Ws, I, J, TableFeatureRef{}, Time))
			{
				EmitZenoGuard(Ws, I, J, TableFeatureRef{}, Time);
				Seed.Zeno = true;
				loop::HandOffToIsland(Ws, Seed, Time);
				return;
			}
			const BallSpec& SpecI = SpecOf(Ws, I);
			const BallSpec& SpecJ = SpecOf(Ws, J);
			const BallState BeforeI = BallStateForEvent(Ws, I, Time);
			const BallState BeforeJ = BallStateForEvent(Ws, J, Time);
			const Vec3 D = BeforeJ.Position - BeforeI.Position;
			const double Distance = Length(D);
			if (!(Distance > 0.0))
			{
				return; // coincident centers (corrupt state): nothing applied
			}
			const Vec3 N = D / Distance;
			const double Vn = Dot(BeforeI.Velocity - BeforeJ.Velocity, N);
			if (!(Vn > P.Numerics.ApproachSpeedTol))
			{
				Seed.Pressing = true; // exact re-anchor (8.11): approaching by the segments, not by the exact state
				loop::HandOffToIsland(Ws, Seed, Time);
				return;
			}
			const double ReducedMass = SpecI.Mass * SpecJ.Mass / (SpecI.Mass + SpecJ.Mass);
			const double Join = IslandJoinDistance(Vn, ReducedMass, P.Cli, P.Numerics.ContactTol);
			// An approaching pair with a ball in a pocket (pivoting or falling) is resolved pairwise (collisions 5.4: "PocketFall -- other
			// balls --> PocketFall", GRI): an island cannot hold it - pocket states never join islands and a member in a pocket's hole
			// leaves at the first step (MemberExit), so the island ended after one step with the pair still approaching, the contact
			// fired again at once, and the pair sank into each other by one step per round (WP-10 fix, a 9-ball break of a 20 000-break
			// scan: 592 one-step islands and 750 overlap diagnostics, two balls rattling on a side pocket's rim).
			const bool InPocket = IsInPocket(BeforeI.State) || IsInPocket(BeforeJ.State);
			if (!InPocket && NeedsIsland(Ws, I, J, BeforeI.Position, BeforeJ.Position, Time, Join))
			{
				loop::HandOffToIsland(Ws, Seed, Time); // cluster, frozen-to-rail kiss, frozen strike (3.9.2)
				return;
			}

			BallBallParams Contact = P.BallBall;
			const BallChalkMarks& MarksI = Ws.Input->Balls[I].ChalkMarks;
			const BallChalkMarks& MarksJ = Ws.Input->Balls[J].ChalkMarks;
			if (P.ChalkCling && (!MarksI.IsEmpty() || !MarksJ.IsEmpty()))
			{
				// Per-contact cling from the chalk marks (human-factors 4.3, architecture 8.11).
				const double ChiI = MarksI.IsEmpty() ? 0.0 : ChalkMarkWeight(MarksI, OrientationForCling(Ws, I, Time), SpecI.Radius, N);
				const double ChiJ = MarksJ.IsEmpty() ? 0.0 : ChalkMarkWeight(MarksJ, OrientationForCling(Ws, J, Time), SpecJ.Radius, -N);
				Contact.ClingFactor = ContactClingFactor(ChiI, ChiJ, P.BallBall);
			}
			const ImpactBody BodyI{BeforeI.Position, BeforeI.Velocity, BeforeI.Omega, SpecI.Radius, SpecI.Mass, SpecI.Inertia};
			const ImpactBody BodyJ{BeforeJ.Position, BeforeJ.Velocity, BeforeJ.Omega, SpecJ.Radius, SpecJ.Mass, SpecJ.Inertia};
			const BallBallImpulse R = ResolveBallBall(BodyI, BodyJ, Contact, P.Numerics.RestSpeed, P.Numerics.EpsV);
			if (!R.Approaching)
			{
				Seed.Pressing = true;
				loop::HandOffToIsland(Ws, Seed, Time);
				return;
			}
			BallState AfterI = BeforeI;
			AfterI.Velocity = R.Velocity1;
			AfterI.Omega = R.Omega1;
			BallState AfterJ = BeforeJ;
			AfterJ.Velocity = R.Velocity2;
			AfterJ.Omega = R.Omega2;
			Settle(Ws, I, BeforeI, AfterI);
			Settle(Ws, J, BeforeJ, AfterJ);

			ShotEvent Event = MakeEvent(Time, ShotEventType::BallBall, I, J);
			Event.Normal = R.Normal;
			Event.NormalSpeed = R.NormalSpeed;
			Event.NormalImpulse = R.NormalImpulse;
			Event.TangentImpulse = R.TangentImpulse;
			if (I == kCueBallId)
			{
				Event.CutAngle = CutAngle(BeforeI.Velocity, R.Normal);
			}
			std::uint8_t Flags = 0;
			if (R.RestitutionUsed == 0.0 && Vn < P.Numerics.RestSpeed)
			{
				Flags |= ShotEventFlags::Resting;
			}
			if (R.Stick)
			{
				Flags |= ShotEventFlags::Stick;
			}
			if ((E.Flags & ContactFlags::AtStart) != 0u)
			{
				Flags |= ShotEventFlags::AtStart;
			}
			if (BeforeI.State == MotionState::Airborne || BeforeJ.State == MotionState::Airborne)
			{
				Flags |= ShotEventFlags::Airborne;
			}
			Event.Flags = Flags;
			Event.Pre[0] = BeforeI;
			Event.Pre[1] = BeforeJ;
			Event.Post[0] = AfterI;
			Event.Post[1] = AfterJ;
			EmitEvent(Ws, Event);
			if (IsOnSurface(BeforeI.State) && AfterI.State == MotionState::Airborne)
			{
				StartAirborne(Ws, I, AfterI, Time);
			}
			if (IsOnSurface(BeforeJ.State) && AfterJ.State == MotionState::Airborne)
			{
				StartAirborne(Ws, J, AfterJ, Time);
			}
			loop::SetSegment(Ws, I, AfterI, Time, false);
			loop::SetSegment(Ws, J, AfterJ, Time, false);
			loop::PredictBalls(Ws, (1u << I) | (1u << J));
		}

		void ProcessTipContact(Workspace& Ws, const QueuedEvent& E)
		{
			const int Ball = E.BallA;
			const int Strike = E.FeatureIndex;
			const double Time = E.Time;
			const PhysicsParams& P = Ws.Params;
			TipSlot& Tip = Ws.Tips[Strike];
			if ((E.Flags & ContactFlags::Pressing) != 0u)
			{
				IslandSeed Seed;
				Seed.BallA = Ball;
				Seed.Pressing = true;
				loop::HandOffToIsland(Ws, Seed, Time); // tip and ball at zero relative speed, pressed together: the island's IslandTip
				return;
			}
			if (Ball == Tip.Path.StruckBall)
			{
				Tip.StrikeContactOpen = false; // predicted from the separation on: a genuine re-contact (double hit)
			}
			// Zeno detector (architecture 9 guard 5) for the tip-ball pair too (integration round 2): tip-ball pairs had no guard,
			// and a re-contact that left the pair approaching along the normal was found again at the same instant with ever smaller
			// impulses (4073 TipRecontacts at one time, the 20 000 event cap: VAL ROB-11 B1 shot 5476). The cause is fixed in
			// ResolveTipRecontact (non-penetration, CueStrike.h); this is the termination backstop every other contact pair has:
			// 8 contacts of the pair within ZenoWindow go to the island, whose IslandTip resolves a sustained tip contact with a force
			// model. Key: ball-feature slot with Kind None and Index 0xF0 + strike (no table feature uses Kind None).
			const TableFeatureRef TipKey{TableFeatureKind::None, static_cast<std::uint8_t>(0xF0 + Strike), 0};
			if (RecordZenoContact(Ws, Ball, -1, TipKey, Time))
			{
				EmitZenoGuard(Ws, Ball, -1, TipKey, Time);
				IslandSeed Seed;
				Seed.BallA = Ball;
				Seed.Zeno = true;
				loop::HandOffToIsland(Ws, Seed, Time);
				return;
			}
			const CueSpec& Cue = Ws.Input->Strikes[Strike].Input.Cue;
			const BallState Before = BallStateForEvent(Ws, Ball, Time);
			const TipRecontactResult R =
				ResolveTipRecontact(Tip.Path, Time, Before, SpecOf(Ws, Ball), Cue, loop::SupportSurface(Ws, Ball), P.Slate, P.Gravity, P.Numerics);
			if (!(R.Impulse > 0.0))
			{
				// No impulse (separating at the exact state, or WP-1's re-contact model gives none): no contact; the tip slot looks
				// further from Now (PredictTips never searches the past, so this contact is not found again later).
				loop::PredictTips(Ws, Ball, Time);
				return;
			}
			const Vec3 Center = TipCenterAt(Tip.Path, Time);
			const Vec3 N = Normalized(Before.Position - Center);
			const double CosPsi = Dot(Tip.Path.Direction, N);
			const double SinPsi = Length(Tip.Path.Direction - N * CosPsi);
			ShotEvent Event = MakeEvent(Time, ShotEventType::TipRecontact, Ball);
			Event.Feature = static_cast<std::uint8_t>(Strike);
			Event.Normal = N;
			Event.NormalSpeed = R.RelativeSpeed;
			Event.NormalImpulse = R.Impulse;
			Event.Value = R.Tip.Speed0;
			if (!(CosPsi > 0.0 && SinPsi <= MiscueLimit(Cue.TipFriction)))
			{
				Event.Flags = ShotEventFlags::Miscue;
			}
			BallState After = R.Ball;
			FreeFromPivot(After);
			Event.Pre[0] = Before;
			Event.Post[0] = After;

			// Tip contact interval (8.6): a re-contact on the ball of the open interval extends it (contacts closer than ContactTime
			// merge); otherwise the open interval ends now and a new one starts on this ball.
			if (Tip.ContactBall != kNoBall && Tip.ContactBall != Ball)
			{
				Tip.ContactEnd = Min(Tip.ContactEnd, Time);
				loop::EmitTipContactEnd(Ws, Strike);
			}
			EmitEvent(Ws, Event);
			if (Tip.ContactBall == Ball)
			{
				Tip.ContactEnd = Max(Tip.ContactEnd, Time + Cue.ContactTime);
			}
			else
			{
				Tip.ContactBall = static_cast<BallId>(Ball);
				Tip.ContactEnd = Time + Cue.ContactTime;
				const BallState AtTarget = Tip.FrozenTarget != kNoBall ? loop::CurrentState(Ws, Tip.FrozenTarget, Time) : BallState{};
				EmitTipBegin(Ws, Strike, Ball, Time, GapToFrozenTarget(Ws, Strike, Ball, Time), Before, AtTarget);
			}
			Tip.Path = R.Tip;
			Tip.Moving = Tip.Path.StopTime > Time;
			OpenTipPiece(Ws, Strike);
			if (IsOnSurface(Before.State) && After.State == MotionState::Airborne)
			{
				StartAirborne(Ws, Ball, After, Time);
			}
			loop::SetSegment(Ws, Ball, After, Time, false);
			loop::PredictBalls(Ws, 1u << Ball);
		}
	}

	namespace loop
	{
		bool IsImpulseContact(const QueuedEvent& Event)
		{
			switch (Event.Kind)
			{
			case QueuedEventKind::BallBall:
			case QueuedEventKind::TipContact:
				return true;
			case QueuedEventKind::TableFeature:
			{
				const TableFeatureKind Kind = static_cast<TableFeatureKind>(Event.FeatureKind);
				return IsCushionLike(Kind) || Kind == TableFeatureKind::LinerWall || Kind == TableFeatureKind::RimTorus || Kind == TableFeatureKind::RailTop ||
					Kind == TableFeatureKind::RailTopEdge;
			}
			case QueuedEventKind::CueStrike:
			case QueuedEventKind::Transition:
			case QueuedEventKind::TiltRefresh:
				break;
			}
			return false;
		}

		IslandSeed SeedOf(const QueuedEvent& Event)
		{
			IslandSeed Seed;
			Seed.BallA = Event.BallA;
			Seed.Pressing = (Event.Flags & ContactFlags::Pressing) != 0u;
			if (Event.Kind == QueuedEventKind::BallBall)
			{
				Seed.BallB = Event.BallB;
			}
			else if (Event.Kind == QueuedEventKind::TableFeature)
			{
				Seed.Feature = TableFeatureRef{static_cast<TableFeatureKind>(Event.FeatureKind), Event.FeatureIndex, Event.FeatureSub};
				Seed.RailTop = Seed.Feature.Kind == TableFeatureKind::RailTop || Seed.Feature.Kind == TableFeatureKind::RailTopEdge;
			}
			return Seed;
		}

		void HandOffToIsland(Workspace& Ws, const IslandSeed& Seed, double Time)
		{
			Ws.Now = Time;
			SimDiagnostics& Diag = Ws.Result->Diagnostics;
			++Diag.IslandHandOffs;
			if (Seed.Pressing)
			{
				++Diag.PressingContacts;
			}
			StartIsland(Ws, Seed, Time);
		}

		void EmitTipContactEnd(Workspace& Ws, int Strike)
		{
			TipSlot& Tip = Ws.Tips[Strike];
			const int Ball = Tip.ContactBall;
			if (Ball == kNoBall)
			{
				return;
			}
			ShotEvent Event = MakeEvent(Tip.ContactEnd, ShotEventType::TipContactEnd, Ball, Tip.FrozenTarget);
			Event.Feature = static_cast<std::uint8_t>(Strike);
			Event.SubFeature = static_cast<std::uint8_t>(Tip.StruckTouchedOther ? 1 : 0);
			Event.Value = GapToFrozenTarget(Ws, Strike, Ball, Tip.ContactEnd);
			Event.Pre[0] = CurrentState(Ws, Ball, Tip.ContactEnd); // the record's positions, whatever EventStates (EmitEvent)
			Event.Post[0] = Event.Pre[0];
			if (Tip.FrozenTarget != kNoBall)
			{
				Event.Pre[1] = CurrentState(Ws, Tip.FrozenTarget, Tip.ContactEnd);
				Event.Post[1] = Event.Pre[1];
			}
			Tip.ContactBall = kNoBall;
			EmitEvent(Ws, Event);
		}

		void ProcessStrikes(Workspace& Ws, BallState* States)
		{
			// Architecture 8.6: each strike at t = 0 (the strike includes its slate reaction, MOT impl. note 6), the frozen target f
			// (RUL F7: the ball frozen to the struck ball with the largest positive n_hat . d), CueStrike, TipContactBegin, and the
			// follow-through tip path.
			const SimInput& Input = *Ws.Input;
			const PhysicsParams& P = Ws.Params;
			for (int k = 0; k < Input.Strikes.Size(); ++k)
			{
				const StrikeRequest& Request = Input.Strikes[k];
				const int Ball = Request.Ball;
				const BallSpec& Spec = SpecOf(Ws, Ball);
				const BallState Before = States[Ball];
				const StrikeResult R = StrikeCueBall(Request.Input, Before, Spec, SupportSurface(Ws, Ball), P.Slate, P.Pinch, P.Gravity, P.Numerics);
				StrikeOutcome Outcome;
				Outcome.Ball = static_cast<BallId>(Ball);
				Outcome.Result = R;
				Ws.Result->Strikes.PushBack(Outcome);
				if (R.Error != ErrorCode::Ok)
				{
					continue; // validated before; unreachable
				}
				States[Ball] = R.State;

				// f: n_hat . d must be positive beyond rounding (1e-9): a frozen ball exactly beside the stroke line is not "the ball the
				// stroke goes into" (an azimuth of pi leaves 1e-16 of rounding in d).
				const Vec3 Axis = MakeCueFrame(Request.Input.Elevation, Request.Input.Azimuth).Axis;
				int Frozen = kNoBall;
				double BestDot = 1e-9;
				double FrozenGap = 0.0;
				for (int j = 0; j < kMaxBalls; ++j)
				{
					if (j == Ball || !Input.Balls[j].InPlay)
					{
						continue;
					}
					const Vec3 D = States[j].Position - Before.Position;
					const double Distance = Length(D);
					const double Gap = Distance - (Spec.Radius + SpecOf(Ws, j).Radius);
					if (Gap <= Input.Context.FrozenTolerance && Distance > 0.0)
					{
						const double Along = Dot(D / Distance, Axis);
						if (Along > BestDot)
						{
							BestDot = Along;
							Frozen = j;
							FrozenGap = Gap;
						}
					}
				}

				TipSlot& Tip = Ws.Tips[k];
				Tip = TipSlot{};
				Tip.Path = MakeCueTipPath(Request.Input, R, Before.Position, Spec.Radius);
				Tip.Path.Strike = k;
				Tip.Path.StruckBall = static_cast<BallId>(Ball);
				Tip.Moving = Tip.Path.StopTime > Tip.Path.StartTime;
				Tip.FrozenTarget = static_cast<BallId>(Frozen);
				Tip.ContactBall = static_cast<BallId>(Ball);
				Tip.ContactEnd = R.ContactDuration;
				Tip.StrikeContactOpen = true; // the strike resolved this contact incl. the slate reaction (SimInternal.h)

				ShotEvent Event = MakeEvent(0.0, ShotEventType::CueStrike, Ball);
				Event.Feature = static_cast<std::uint8_t>(k);
				Event.Normal = R.ImpulseDirection;
				Event.NormalSpeed = Request.Input.Speed;
				Event.NormalImpulse = R.Impulse;
				Event.Value = R.CueSpeedAfter;
				Event.Flags = R.Miscue ? ShotEventFlags::Miscue : std::uint8_t{0};
				Event.Pre[0] = Before;
				Event.Post[0] = R.State;
				EmitEvent(Ws, Event);
				// Segments start after the strikes: the gap and the record's positions from the t = 0 states.
				EmitTipBegin(Ws, k, Ball, 0.0, FrozenGap, Before, Frozen != kNoBall ? States[Frozen] : BallState{});
				OpenTipPiece(Ws, k);
				if (R.State.State == MotionState::Airborne)
				{
					StartAirborne(Ws, Ball, R.State, 0.0); // strike hop (jump shot)
				}
			}
		}

		void ProcessEvent(Workspace& Ws, const QueuedEvent& Event)
		{
			switch (Event.Kind)
			{
			case QueuedEventKind::Transition:
				ProcessEndSlot(Ws, Event);
				return;
			case QueuedEventKind::TiltRefresh:
				ProcessTiltRefresh(Ws, Event);
				return;
			case QueuedEventKind::TableFeature:
				ProcessTableFeature(Ws, Event);
				return;
			case QueuedEventKind::BallBall:
				ProcessBallBall(Ws, Event);
				return;
			case QueuedEventKind::TipContact:
				ProcessTipContact(Ws, Event);
				return;
			case QueuedEventKind::CueStrike:
				return; // strikes are processed at t = 0 (ProcessStrikes), never queued
			}
		}

		double FeatureGap(const Workspace& Ws, const TableFeatureRef& Feature, const Vec3& P, double R)
		{
			// Gaps beyond this are reported as kInfinity without the extent tests (every caller compares with at most a few mm).
			constexpr double kFarGap = 0.05;
			const TableGeometry& Table = *Ws.Input->Table;
			const NumericsConfig& N = Ws.Params.Numerics;
			const bool Airborne = P.z - R > N.EpsZ;
			const double NoseRadius = Ws.Detection.NoseProfileRadius;
			const Vec2 Q = XY(P);
			switch (Feature.Kind)
			{
			case TableFeatureKind::NoseSegment:
			{
				if (Feature.Index >= Table.Noses.Size() || !Table.Noses[Feature.Index].Present)
				{
					return kInfinity;
				}
				const NoseSegment& Nose = Table.Noses[Feature.Index];
				const double Along = Dot(Q - Nose.Start, Nose.Direction);
				if (Along < -N.SegmentParamSlack || Along > Nose.Length + N.SegmentParamSlack)
				{
					return kInfinity; // beyond the tangent points: the jaw arcs
				}
				if (Dot(Q - Nose.Start, Nose.InwardNormal) > R + NoseRadius + kFarGap)
				{
					return kInfinity;
				}
				if (!Airborne)
				{
					const double Offset = ComputeCushionContact(R, Nose.Height, NoseRadius, Ws.Detection.PooltoolCompat).HorizontalOffset;
					return Dot(Q - Nose.Start, Nose.InwardNormal) - Offset;
				}
				const Vec2 Edge = Nose.Start + Nose.Direction * Clamp(Along, 0.0, Nose.Length);
				return Length(P - ToVec3(Edge, Nose.Height)) - (R + NoseRadius);
			}
			case TableFeatureKind::JawArc:
			{
				if (Feature.Index >= Table.JawArcs.Size())
				{
					return kInfinity;
				}
				const JawArc& Arc = Table.JawArcs[Feature.Index];
				const Vec2 Rel = Q - Arc.Center;
				const double Distance = Length(Rel);
				if (!(Distance > 0.0) || Distance > Arc.Radius + R + NoseRadius + kFarGap)
				{
					return kInfinity; // far away (no angle test needed) or degenerate
				}
				double Delta = Atan2(Rel.y, Rel.x) - Arc.AngleFrom;
				while (Delta < 0.0)
				{
					Delta += kTwoPi;
				}
				while (Delta >= kTwoPi)
				{
					Delta -= kTwoPi;
				}
				if (Delta > Arc.AngleSweep)
				{
					return kInfinity; // not on the exposed arc
				}
				if (!Airborne)
				{
					const double Offset = ComputeCushionContact(R, Arc.Height, NoseRadius, Ws.Detection.PooltoolCompat).HorizontalOffset;
					return Distance - Arc.Radius - Offset;
				}
				const Vec2 Edge = Arc.Center + Rel * (Arc.Radius / Distance);
				return Length(P - ToVec3(Edge, Arc.Height)) - (R + NoseRadius);
			}
			case TableFeatureKind::FacingFace:
			{
				if (Feature.Index >= Table.Facings.Size() || Airborne)
				{
					return kInfinity;
				}
				const Facing& Face = Table.Facings[Feature.Index];
				const double Along = Dot(Q - Face.Start, Face.Direction);
				const double Across = Dot(Q - Face.Start, Face.PocketNormal);
				if (Along < -N.SegmentParamSlack || Along > Face.Length + N.SegmentParamSlack || Across > R + kFarGap)
				{
					return kInfinity;
				}
				return Across - FacingContactOffset(R, Face.TopHeight, Face.Backdraft);
			}
			default:
				break;
			}
			return kInfinity;
		}
	}
}
