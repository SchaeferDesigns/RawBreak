#include "rb/Core/FpGuard.h"
// Owner: WP-6b (islands, pockets & rail-top routing). Spec: Docs/architecture.md 8.8, 8.11, 9 (guards 5-8); physics-collisions
// 3.6 (pressing islands), 3.9.2 (joining incl. features, member exits, exit), 3.9.6 (records), 6.2 (rigid rail-top islands), 7.3.
//
// Island life cycle (the solver is WP-3's CompliantIsland; this file decides who joins, who leaves and when it ends):
//  * StartIsland: BFS over balls (gap <= delta_cl of the seed contact) + table features in the members' reach (bounded
//    IslandFeatures) + moving cue tips that can reach a member; members leave event mode (InIsland, version bump, observers
//    flushed, Sampled track); Zeno histories of the members cleared; IslandBegin. Compliant, except rail-top islands (Rigid).
//    A seed during an active island merges into it at the island's time.
//  * AdvanceIsland: fixed steps up to (not beyond) the next event; per step: records -> events (BallBall / BallCushion / BallJaw
//    / BallRailTop / TipContactBegin/End, FromIsland), observers of members, adaptive Sampled recording, member exits (drop edge,
//    over a pocket hole, off the cloth region, rail-top exits), joining balls (gap <= delta_cl), release of members that are
//    free of every contact, the Compliant -> Rigid switch, exit (CanExit, or a rigid island at rest), the step budget; table
//    features join on a stateless schedule with a conservative reach bound (QueryWindowOf). Performance (A-ISL-2): the query
//    window adapts to the island's energy, the observer / exit tests of a member flat on the cloth are skipped while it stays
//    within its clearance (ExitClearance), and the joinable event-mode balls are collected once per AdvanceIsland call.
#include "SimIslandInternal.h"

#include "rb/Math/Scalar.h"
#include "rb/Physics/Slate.h"

#include <bit>

namespace rb::sim
{
	namespace
	{
		// Feature-query schedule (architecture 8.8 "every n steps with a conservative reach bound", see QueryWindowOf): n adapts to
		// the island's energy speed bound within [min, max] steps per mode, so that a window is crossed in at most kQueryTravel.
		constexpr int kQueryStepsCompliantMin = 64;
		constexpr int kQueryStepsCompliantMax = 512;
		constexpr int kQueryStepsRigidMin = 8;
		constexpr int kQueryStepsRigidMax = 256;
		constexpr double kQueryTravel = 5e-3;    // [m]
		constexpr double kQueryMargin = 1e-4;    // [m]
		// Release check of members free of every contact (not every step: O(members^2)).
		constexpr int kReleaseCheckStepsCompliant = 16;
		constexpr int kReleaseCheckStepsRigid = 2;
		// The schedules test StepCount & (n - 1): every period is a power of two (and the query minimum divides the maximum).
		constexpr bool IsPowerOfTwo(int N) { return N > 0 && (N & (N - 1)) == 0; }
		static_assert(IsPowerOfTwo(kQueryStepsCompliantMin) && IsPowerOfTwo(kQueryStepsCompliantMax) && IsPowerOfTwo(kQueryStepsRigidMin) &&
			IsPowerOfTwo(kQueryStepsRigidMax) && IsPowerOfTwo(kReleaseCheckStepsCompliant) && IsPowerOfTwo(kReleaseCheckStepsRigid) &&
			kQueryStepsCompliantMin <= kQueryStepsCompliantMax && kQueryStepsRigidMin <= kQueryStepsRigidMax);
		constexpr int kMaxFeatureRefs = 128;
		// Cap heights / rest tests of rail-top members [m], [m/s].
		constexpr double kSupportHeightTol = 1e-5;
		// A cloth member within this height of the cloth when it returns to event mode is ON the cloth (the ball-ball friction of
		// a contact lifts balls by fractions of a micrometre): it is snapped down and gets the table reaction of collisions 2.4
		// step 6, instead of a micro-flight with BallAirborne / BallSlate / BallLand events (DECISION; the jump is < 1 um, the
		// OverlapGuard scale).
		constexpr double kClothSnapHeight = 1e-6;

		const TableGeometry& TableOf(const Workspace& Ws) { return *Ws.Input->Table; }
		const BallSpec& SpecOf(const Workspace& Ws, int Ball) { return Ws.Input->Balls[Ball].Spec; }

		double StepSize(const Workspace& Ws)
		{
			return Ws.Island.Solver.Mode() == CliMode::Rigid ? Ws.Params.Cli.RigidTimeStep : Ws.Params.Cli.TimeStep;
		}

		int MinQuerySteps(const Workspace& Ws)
		{
			return Ws.Island.Solver.Mode() == CliMode::Rigid ? kQueryStepsRigidMin : kQueryStepsCompliantMin;
		}

		BallState BodyState(const IslandBody& Body)
		{
			BallState S;
			S.Position = Body.Position;
			S.Velocity = Body.Velocity;
			S.Omega = Body.Omega;
			S.State = MotionState::Sliding; // placeholder: classified by the caller
			return S;
		}

		double ReducedMass(double A, double B) { return A * B / (A + B); }

		std::uint32_t MemberMask(const Workspace& Ws)
		{
			std::uint32_t Mask = 0;
			const CompliantIsland& Solver = Ws.Island.Solver;
			for (int k = 0; k < Solver.BodyCount(); ++k)
			{
				Mask |= 1u << Solver.Body(k).Ball;
			}
			return Mask;
		}

		int LowestMember(const Workspace& Ws)
		{
			int Lowest = kMaxBalls;
			const CompliantIsland& Solver = Ws.Island.Solver;
			for (int k = 0; k < Solver.BodyCount(); ++k)
			{
				Lowest = Solver.Body(k).Ball < Lowest ? Solver.Body(k).Ball : Lowest;
			}
			return Lowest < kMaxBalls ? Lowest : -1;
		}

		// Event-mode balls that may join an island: in play, not in an island, not terminal and not inside a pocket (pocket
		// interiors are not island features, Level B).
		bool CanJoin(const Workspace& Ws, int Ball)
		{
			const BallSlot& B = Ws.Balls[Ball];
			return B.InPlay && !B.InIsland && !IsTerminal(B.Seg.State) && !IsInPocket(B.Seg.State);
		}

		// State of an event-mode ball at the island time T <= EventTime: its segment at T when the segment started by then, else
		// the event state moved back linearly (a segment replaced within the current step).
		BallState EventStateAt(const Workspace& Ws, int Ball, double T, double EventTime)
		{
			const BallSlot& B = Ws.Balls[Ball];
			if (B.Seg.State == MotionState::PocketPivot)
			{
				BallState S = EvaluatePivot(B.Pivot, Max(0.0, T - B.Pivot.T0));
				S.State = MotionState::PocketFall;
				return S;
			}
			if (B.Seg.T0 <= T)
			{
				return BallStateForEvent(Ws, Ball, T);
			}
			const double At = Max(EventTime, B.Seg.T0);
			BallState S = BallStateForEvent(Ws, Ball, At);
			S.Position -= S.Velocity * (At - T);
			return S;
		}

		// True if the body of a member rests on (or rolls on) the rail top rather than on the cloth.
		bool OverRailRegion(const TableGeometry& Table, const Vec3& P)
		{
			return !Table.PlayingArea.Contains(XY(P)) && !IsOverPocketOpening(Table, XY(P));
		}

		// Speed bound of any island body over the next steps: all the island's energy (kinetic incl. spin, potential above the
		// cloth, the tips' kinetic energy) in its lightest body, plus 10 % for the integrator.
		double SpeedBound(const Workspace& Ws)
		{
			const CompliantIsland& Solver = Ws.Island.Solver;
			double Energy = 0.0;
			double MinMass = kInfinity;
			for (int k = 0; k < Solver.BodyCount(); ++k)
			{
				const IslandBody& B = Solver.Body(k);
				Energy += 0.5 * B.Mass * LengthSquared(B.Velocity) + 0.5 * B.Inertia * LengthSquared(B.Omega) +
					B.Mass * Ws.Params.Gravity * Max(0.0, B.Position.z - B.Radius);
				MinMass = Min(MinMass, B.Mass);
			}
			for (int s = 0; s < Ws.Input->Strikes.Size() && s < kMaxStrikes; ++s)
			{
				if (Solver.HasTip(s))
				{
					const double Speed = Ws.Tips[s].Path.Speed0;
					Energy += 0.5 * Ws.Input->Strikes[s].Input.Cue.Mass * Speed * Speed;
				}
			}
			if (!(MinMass < kInfinity))
			{
				return 0.0;
			}
			const double Bound = Sqrt(2.0 * Energy / MinMass) * 1.1;
			return IsFinite(Bound) ? Bound : 0.0;
		}

		// Island bounds of the body centers and the largest radius.
		struct IslandBounds
		{
			Aabb3 Box{{kInfinity, kInfinity, kInfinity}, {-kInfinity, -kInfinity, -kInfinity}};
			double MaxRadius = 0.0;
			double MaxSpeed = 0.0;
			double MaxMass = 0.0;
			bool AnyRailTop = false; // a member not supported by the cloth (rail top)
		};

		IslandBounds Bounds(const Workspace& Ws)
		{
			IslandBounds Out;
			const CompliantIsland& Solver = Ws.Island.Solver;
			double MaxSpeedSquared = 0.0;
			for (int k = 0; k < Solver.BodyCount(); ++k)
			{
				const IslandBody& B = Solver.Body(k);
				Out.Box.Lo = Vec3{Min(Out.Box.Lo.x, B.Position.x), Min(Out.Box.Lo.y, B.Position.y), Min(Out.Box.Lo.z, B.Position.z)};
				Out.Box.Hi = Vec3{Max(Out.Box.Hi.x, B.Position.x), Max(Out.Box.Hi.y, B.Position.y), Max(Out.Box.Hi.z, B.Position.z)};
				Out.MaxRadius = Max(Out.MaxRadius, B.Radius);
				MaxSpeedSquared = Max(MaxSpeedSquared, LengthSquared(B.Velocity));
				Out.MaxMass = Max(Out.MaxMass, B.Mass);
				Out.AnyRailTop = Out.AnyRailTop || !B.ClothSupport;
			}
			// (WP-10 performance) One square root: sqrt is correctly rounded and monotone, so the root of the largest square is
			// bitwise the largest Length (= Sqrt(LengthSquared)).
			Out.MaxSpeed = Sqrt(MaxSpeedSquared);
			return Out;
		}

		bool HasFeature(const CompliantIsland& Solver, const TableFeatureRef& Ref)
		{
			for (int f = 0; f < Solver.FeatureCount(); ++f)
			{
				const IslandFeature& F = Solver.Feature(f);
				if (F.SourceKind == static_cast<std::uint8_t>(Ref.Kind) && F.SourceIndex == Ref.Index && F.SourceSub == Ref.SubIndex)
				{
					return true;
				}
			}
			return false;
		}

		void CapacityDiagnostic(Workspace& Ws, double Time, double Value)
		{
			if (!Ws.Result->Diagnostics.IslandCapacityExceeded)
			{
				Ws.Result->Diagnostics.IslandCapacityExceeded = true;
				ShotEvent D;
				D.Time = Time;
				D.Type = ShotEventType::Diagnostic;
				D.A = static_cast<BallId>(LowestMember(Ws));
				D.Value = Value;
				EmitEvent(Ws, D);
			}
		}

		// Adds every contact feature of Region to the solver (rail-top features only while a member is on the rail top: a cloth
		// member that could reach them flies over the rail and leaves the island first).
		void AddFeaturesIn(Workspace& Ws, const Aabb3& Region, bool Running)
		{
			IslandState& I = Ws.Island;
			const TableGeometry& Table = TableOf(Ws);
			// Every island contact feature (noses, jaw arcs, facings, rail tops and their edges) lies on or beyond the nose-line
			// rectangle; a region strictly inside it has none (drop edges reach inside but are no island contacts), so the ~2 us
			// query is skipped for islands in the open table (most of them).
			constexpr double kInsideMargin = 1e-3;
			if (Region.Lo.x > Table.PlayingArea.Lo.x + kInsideMargin && Region.Hi.x < Table.PlayingArea.Hi.x - kInsideMargin &&
				Region.Lo.y > Table.PlayingArea.Lo.y + kInsideMargin && Region.Hi.y < Table.PlayingArea.Hi.y - kInsideMargin)
			{
				return;
			}
			TableFeatureRef Refs[kMaxFeatureRefs];
			bool Overflow = false;
			const int Count = QueryTableFeatures(Region, Table, Ws.Detection, Refs, kMaxFeatureRefs, Overflow);
			const bool RailTop = Bounds(Ws).AnyRailTop;
			for (int r = 0; r < Count; ++r)
			{
				const TableFeatureRef& Ref = Refs[r];
				const bool IsRailTop = Ref.Kind == TableFeatureKind::RailTop || Ref.Kind == TableFeatureKind::RailTopEdge;
				if ((IsRailTop && !RailTop) || HasFeature(I.Solver, Ref))
				{
					continue;
				}
				IslandFeature Pieces[kMaxFeaturePieces];
				const int N = MakeIslandFeatures(Ref, Table, Ws.Params, Pieces);
				for (int k = 0; k < N; ++k)
				{
					if (!I.Solver.AddFeature(Pieces[k]))
					{
						CapacityDiagnostic(Ws, I.Solver.Time(), static_cast<double>(I.Solver.FeatureCount()));
						return;
					}
					if (Running)
					{
						++Ws.Result->Diagnostics.FeatureJoins;
					}
				}
			}
			if (Overflow)
			{
				CapacityDiagnostic(Ws, I.Solver.Time(), static_cast<double>(Count));
			}
		}

		// Feature-query window (architecture 8.8): queries run when StepCount % Steps == 0, and at once when the island starts,
		// switches mode or gains energy (a ball or tip joins). Steps is the largest power of two in [min, max] of the mode whose
		// window a body at the energy speed bound SB crosses within kQueryTravel. SB does not grow between those queries (the
		// island only loses energy), so Steps only grows between queries: each doubling can postpone the next query by one more
		// window of travel <= kQueryTravel. The region therefore covers SB Steps dt + log2(max / Steps) kQueryTravel.
		struct QueryWindow
		{
			int Steps = 1;
			double Speed = 0.0; // SB [m/s]
			double Reach = 0.0; // [m]
		};

		QueryWindow QueryWindowOf(const Workspace& Ws)
		{
			const bool Rigid = Ws.Island.Solver.Mode() == CliMode::Rigid;
			const int MaxSteps = Rigid ? kQueryStepsRigidMax : kQueryStepsCompliantMax;
			const double Dt = StepSize(Ws);
			QueryWindow W;
			W.Speed = SpeedBound(Ws);
			W.Steps = Rigid ? kQueryStepsRigidMin : kQueryStepsCompliantMin;
			while (W.Steps < MaxSteps && W.Speed * Dt * (2 * W.Steps) <= kQueryTravel)
			{
				W.Steps *= 2;
			}
			W.Reach = W.Speed * Dt * W.Steps + kQueryMargin;
			for (int Steps = W.Steps; Steps < MaxSteps; Steps *= 2)
			{
				W.Reach += kQueryTravel;
			}
			return W;
		}

		void AddFeaturesForWindow(Workspace& Ws, const QueryWindow& W, bool Running)
		{
			if (Ws.Island.Solver.BodyCount() == 0)
			{
				return;
			}
			const IslandBounds B = Bounds(Ws);
			AddFeaturesIn(Ws, B.Box.Inflated(B.MaxRadius + W.Reach), Running);
		}

		// Features in every body's reach until the next scheduled query.
		void AddFeaturesForReach(Workspace& Ws, bool Running)
		{
			AddFeaturesForWindow(Ws, QueryWindowOf(Ws), Running);
		}

		// Moving cue tips that can reach a member join as IslandTip participants (architecture 8.6, 8.8). Returns true if one
		// joined (the island's energy grew: the caller queries features again).
		bool JoinTips(Workspace& Ws, double T, double Margin)
		{
			IslandState& I = Ws.Island;
			bool Joined = false;
			for (int s = 0; s < Ws.Input->Strikes.Size() && s < kMaxStrikes; ++s)
			{
				TipSlot& Tip = Ws.Tips[s];
				if (!Tip.Moving || Tip.InIsland || I.Solver.HasTip(s))
				{
					continue;
				}
				const CueTipPath& Path = Tip.Path;
				if (!(T < Path.StopTime))
				{
					continue;
				}
				double Tau = Max(0.0, T - Path.StartTime);
				if (Path.Deceleration > 0.0)
				{
					Tau = Min(Tau, Max(0.0, Path.Speed0) / Path.Deceleration);
				}
				const double Speed = Path.Speed0 - Path.Deceleration * Tau;
				if (!(Speed > 0.0))
				{
					continue;
				}
				const Vec3 Center = Path.Start + Path.Direction * (Path.Speed0 * Tau - 0.5 * Path.Deceleration * Tau * Tau);
				const double Remaining = Path.Deceleration > 0.0 ? Speed * Speed / (2.0 * Path.Deceleration) : kInfinity;
				bool Reaches = false;
				for (int k = 0; k < I.Solver.BodyCount() && !Reaches; ++k)
				{
					const IslandBody& Body = I.Solver.Body(k);
					Reaches = Length(Body.Position - Center) - (Path.DomeRadius + Body.Radius) <= Remaining + Margin;
				}
				if (!Reaches)
				{
					continue;
				}
				const CueSpec& Cue = Ws.Input->Strikes[s].Input.Cue;
				const int Struck = Path.StruckBall >= 0 && Path.StruckBall < kMaxBalls ? Path.StruckBall : 0;
				IslandTip Participant;
				Participant.Path = Path;
				Participant.Mass = Cue.Mass;
				Participant.Stiffness = CueTipContactStiffness(Cue.ContactTime, Ws.Input->Balls[Struck].Spec.Mass, Cue.Mass);
				Participant.Restitution = Cue.TipRestitution;
				Participant.Friction = Cue.TipFriction;
				if (!I.Solver.SetTip(s, Participant))
				{
					continue;
				}
				Tip.InIsland = true;
				++Tip.Version;
				Joined = true;
				// Cue animation: the open tip piece ends here; the island piece is Sampled (along the cue axis).
				if (Ws.Input->Record.Trajectories && Tip.OpenCueTipSegment >= 0 && Tip.OpenCueTipSegment < static_cast<int>(Ws.Result->CueTips.size()))
				{
					Ws.Result->CueTips[static_cast<std::size_t>(Tip.OpenCueTipSegment)].T1 = T;
					if (Ws.Result->CueTips.size() < Ws.Result->CueTips.capacity())
					{
						CueTipSegment Piece;
						Piece.Strike = s;
						Piece.Path = Path;
						Piece.Path.Start = Center;
						Piece.Path.Speed0 = Speed;
						Piece.Path.StartTime = T;
						Piece.T1 = kInfinity;
						Piece.Kind = SegmentKind::Sampled;
						Piece.EndPosition = Center;
						Ws.Result->CueTips.push_back(Piece);
						Tip.OpenCueTipSegment = static_cast<int>(Ws.Result->CueTips.size()) - 1;
					}
					else
					{
						Ws.Result->Diagnostics.CueTipOverflow = true;
						Tip.OpenCueTipSegment = -1;
					}
				}
			}
			return Joined;
		}

		// A tip leaves the island (island end): new analytic path from its current state; an open contact interval closes.
		void ReleaseTip(Workspace& Ws, int Strike, double T)
		{
			IslandState& I = Ws.Island;
			TipSlot& Tip = Ws.Tips[Strike];
			CueTipPath Path;
			if (!I.Solver.RemoveTip(Strike, Path))
			{
				return;
			}
			if (Tip.ContactBall != kNoBall)
			{
				// The touched ball is (still) a member: its body state, not its pre-island segment; the envelope data like the island's
				// own TipEnd records (B = f, Value = the gap to f; rules F7).
				const int Ball = Tip.ContactBall;
				const auto CenterOf = [&](int Of) -> Vec3
				{
					const int Index = I.Solver.FindBody(Of);
					return Index >= 0 ? I.Solver.Body(Index).Position : BallStateAt(Ws, Of, T).Position;
				};
				const int Member = I.Solver.FindBody(Ball);
				ShotEvent E = MakeBallEvent(ShotEventType::TipContactEnd, T, Ball, Member >= 0 ? BodyState(I.Solver.Body(Member)) : BallStateAt(Ws, Ball, T));
				E.Feature = static_cast<std::uint8_t>(Strike);
				E.B = Tip.FrozenTarget;
				E.SubFeature = Tip.StruckTouchedOther ? 1 : 0;
				if (Tip.FrozenTarget != kNoBall && Tip.FrozenTarget < kMaxBalls)
				{
					const int F = Tip.FrozenTarget;
					E.Value = Length(CenterOf(F) - E.Pre[0].Position) - (SpecOf(Ws, F).Radius + SpecOf(Ws, Ball).Radius);
				}
				EmitEvent(Ws, E);
				Tip.ContactBall = kNoBall;
			}
			if (Ws.Input->Record.Trajectories && Tip.OpenCueTipSegment >= 0 && Tip.OpenCueTipSegment < static_cast<int>(Ws.Result->CueTips.size()))
			{
				CueTipSegment& Open = Ws.Result->CueTips[static_cast<std::size_t>(Tip.OpenCueTipSegment)];
				Open.T1 = T;
				Open.EndPosition = Path.Start;
				Tip.OpenCueTipSegment = -1;
				if (Path.StopTime > T)
				{
					if (Ws.Result->CueTips.size() < Ws.Result->CueTips.capacity())
					{
						CueTipSegment Piece;
						Piece.Strike = Strike;
						Piece.Path = Path;
						Piece.T1 = Path.StopTime;
						Piece.Kind = SegmentKind::Analytic;
						Ws.Result->CueTips.push_back(Piece);
						Tip.OpenCueTipSegment = static_cast<int>(Ws.Result->CueTips.size()) - 1;
					}
					else
					{
						Ws.Result->Diagnostics.CueTipOverflow = true;
					}
				}
			}
			Tip.Path = Path;
			Tip.InIsland = false;
			Tip.Moving = Path.StopTime > T && Path.Speed0 > 0.0;
			++Tip.Version;
		}

		// A ball joins the running island at island time T in state S (event mode -> island).
		bool AddMember(Workspace& Ws, int Ball, const BallState& S, double T, bool RailTop)
		{
			IslandState& I = Ws.Island;
			BallSlot& B = Ws.Balls[Ball];
			const BallSpec& Spec = SpecOf(Ws, Ball);
			IslandBody Body;
			Body.Ball = Ball;
			Body.Position = S.Position;
			Body.Velocity = S.Velocity;
			Body.Omega = S.Omega;
			Body.Radius = Spec.Radius;
			Body.Mass = Spec.Mass;
			Body.Inertia = Spec.Inertia;
			// On the cloth / shelf or above it (a hop lands on the cloth inside the island); rail-top members are supported by the
			// rail-top Plane features; balls inside a pocket fall freely until their exit.
			Body.ClothSupport = !RailTop && B.Context.Support == SupportKind::Cloth && !IsInPocket(S.State) && !OverRailRegion(TableOf(Ws), S.Position);
			if (!I.Solver.AddBody(Body))
			{
				CapacityDiagnostic(Ws, T, static_cast<double>(I.Solver.BodyCount()));
				return false;
			}
			BeginSampledTrack(Ws, Ball, S, T);
			B.InIsland = true;
			++B.Version;
			B.Observers.Clear();
			return true;
		}

		// A member returns to event mode at T with its body state: table reaction, classification, new segment (8.8 exit).
		void ReturnToEventMode(Workspace& Ws, const IslandBody& Body, double T)
		{
			const int Ball = Body.Ball;
			BallSlot& B = Ws.Balls[Ball];
			const TableGeometry& Table = TableOf(Ws);
			const PhysicsParams& P = Ws.Params;
			const BallSpec& Spec = SpecOf(Ws, Ball);
			BallState S = BodyState(Body);
			EndSampledTrack(Ws, Ball, S.Position, T);
			B.InIsland = false;
			if (!Body.ClothSupport)
			{
				// A rail-top member: resting on the flat cap, rolling on it, at rest elsewhere on the rail, or flying.
				const int Cap = FindRailTopPolygon(Table, XY(S.Position), false, RailTopKind::RailCap, Vec2{}, 0.0);
				const bool OnCap = Cap >= 0 && Abs(S.Position.z - (Table.RailTops[Cap].PlanePoint.z + Spec.Radius)) <= kSupportHeightTol &&
					Abs(S.Velocity.z) <= P.Numerics.RestSpeed;
				if (OnCap)
				{
					ContinueOnCap(Ws, Ball, S, Cap, T);
					return;
				}
				const double SpinLimit = P.Numerics.EpsWTimesRadius / Spec.Radius;
				if (OverRailRegion(Table, S.Position) && LengthSquared(S.Velocity) <= P.Numerics.EpsV * P.Numerics.EpsV &&
					LengthSquared(S.Omega) <= SpinLimit * SpinLimit)
				{
					RestOnRail(Ws, Ball, S, T);
					return;
				}
				S.State = MotionState::Airborne;
				B.Context.Support = SupportKind::Cloth;
				B.Context.Pocket = PocketId::None;
				B.BounceIndex = 0;
				B.SequenceMaxZ = S.Position.z;
				ReplaceSegment(Ws, Ball, S, T);
				return;
			}
			const bool WasOnSurface = S.Position.z - Spec.Radius <= kClothSnapHeight;
			if (WasOnSurface)
			{
				S.Position.z = Spec.Radius;
			}
			ApplyTableReaction(S, WasOnSurface, Spec, P.Cloth, P.Slate, P.Gravity, P.Numerics);
			ClassifyState(S, Spec.Radius, 0.0, P.Numerics);
			B.Context.Support = SupportKind::Cloth;
			B.Context.Pocket = PocketId::None;
			if (S.State == MotionState::Airborne)
			{
				const double Apex = S.Velocity.z > 0.0 ? S.Position.z + S.Velocity.z * S.Velocity.z / (2.0 * P.Gravity) : S.Position.z;
				if (B.SequenceMaxZ == 0.0)
				{
					// A new airborne sequence (a lift-off inside the island): BallAirborne once, BallLand closes it.
					ShotEvent E = MakeBallEvent(ShotEventType::BallAirborne, T, Ball, S);
					E.Value = Apex;
					EmitEvent(Ws, E);
				}
				B.BounceIndex = 0;
				B.SequenceMaxZ = Max(B.SequenceMaxZ, Apex);
			}
			ReplaceSegment(Ws, Ball, S, T);
		}

		// Ends the island at T: tips leave with new paths, every member returns to event mode (ascending ball id), IslandEnd.
		void EndIsland(Workspace& Ws, double T)
		{
			IslandState& I = Ws.Island;
			const std::uint32_t Members = MemberMask(Ws);
			const int Lowest = LowestMember(Ws);
			for (int s = 0; s < kMaxStrikes; ++s)
			{
				if (I.Solver.HasTip(s))
				{
					ReleaseTip(Ws, s, T);
				}
			}
			for (int Ball = 0; Ball < kMaxBalls; ++Ball)
			{
				if ((Members >> Ball) & 1u)
				{
					IslandBody Body;
					if (I.Solver.RemoveBody(Ball, Body))
					{
						ReturnToEventMode(Ws, Body, T);
					}
				}
			}
			ShotEvent E;
			E.Time = T;
			E.Type = ShotEventType::IslandEnd;
			E.A = static_cast<BallId>(Lowest);
			E.Value = T - I.StartTime;
			EmitEvent(Ws, E);
			ClearZenoHistories(Ws, Members);
			I.Active = false;
			I.RigidSince = -1.0;
		}

		// Step budget exhausted (guard 8): every member stops where it is and returns to event mode; the loop aborts the shot.
		void StopInPlace(Workspace& Ws, double T)
		{
			IslandState& I = Ws.Island;
			for (int s = 0; s < kMaxStrikes; ++s)
			{
				if (I.Solver.HasTip(s))
				{
					ReleaseTip(Ws, s, T);
				}
			}
			const std::uint32_t Members = MemberMask(Ws);
			for (int Ball = 0; Ball < kMaxBalls; ++Ball)
			{
				if ((Members >> Ball) & 1u)
				{
					IslandBody Body;
					if (I.Solver.RemoveBody(Ball, Body))
					{
						Body.Velocity = Vec3{};
						Body.Omega = Vec3{};
						BallState S = BodyState(Body);
						EndSampledTrack(Ws, Ball, S.Position, T);
						Ws.Balls[Ball].InIsland = false;
						S.State = MotionState::Stationary;
						if (Body.ClothSupport && S.Position.z - Body.Radius <= Ws.Params.Numerics.EpsZ)
						{
							S.Position.z = Body.Radius;
						}
						ReplaceSegment(Ws, Ball, S, T);
					}
				}
			}
			ShotEvent E;
			E.Time = T;
			E.Type = ShotEventType::IslandEnd;
			E.A = kNoBall;
			E.Value = T - I.StartTime;
			EmitEvent(Ws, E);
			ClearZenoHistories(Ws, Members);
			I.Active = false;
			I.RigidSince = -1.0;
		}

		// Seed approach speed and reduced mass -> delta_cl (collisions 3.9.2 step 1).
		double SeedJoinDistance(const Workspace& Ws, const IslandSeed& Seed, const BallState* A, const BallState* B)
		{
			const PhysicsParams& P = Ws.Params;
			double Approach = 0.0;
			double Mass = 0.0;
			if (A != nullptr && B != nullptr)
			{
				const Vec3 D = B->Position - A->Position;
				const double L = Length(D);
				Approach = L > 0.0 ? Dot(A->Velocity - B->Velocity, D / L) : 0.0;
				Mass = ReducedMass(SpecOf(Ws, Seed.BallA).Mass, SpecOf(Ws, Seed.BallB).Mass);
			}
			else if (A != nullptr && Seed.Feature.Kind != TableFeatureKind::None)
			{
				const FixedContact Contact = MakeFixedContact(Seed.Feature, TableOf(Ws), *A, SpecOf(Ws, Seed.BallA), Ws.Detection);
				Approach = -Dot(A->Velocity, Contact.Normal);
				Mass = SpecOf(Ws, Seed.BallA).Mass;
			}
			if (!(Mass > 0.0) || Seed.Pressing || Seed.RailTop)
			{
				return P.Numerics.ContactTol;
			}
			return IslandJoinDistance(Max(0.0, Approach), Mass, P.Cli, P.Numerics.ContactTol);
		}

		void StartIslandImpl(Workspace& Ws, const IslandSeed& InSeed, double Time, const BallState* SeedState)
		{
			if (Ws.Input == nullptr || Ws.Input->Table == nullptr || Ws.Result == nullptr || InSeed.BallA < 0 || InSeed.BallA >= kMaxBalls)
			{
				return;
			}
			IslandSeed Seed = InSeed;
			const TableFeatureKind SeedKind = Seed.Feature.Kind;
			const bool FeatureSeed = Seed.BallB < 0 || Seed.BallB >= kMaxBalls || Seed.BallB == Seed.BallA;
			if (FeatureSeed && (SeedKind == TableFeatureKind::LinerWall || SeedKind == TableFeatureKind::RimTorus))
			{
				// Pocket interiors are not island features (Level B): a pressing / Zeno contact with the liner or the rim is
				// resolved by the pocket state machine, which always leaves the ball separating.
				ProcessPocketEvent(Ws, Seed.BallA, Seed.Feature, Time);
				return;
			}
			if (FeatureSeed && (SeedKind == TableFeatureKind::RailTop || SeedKind == TableFeatureKind::RailTopEdge))
			{
				Seed.RailTop = true; // rail-top islands are rigid (collisions 6.2)
			}
			IslandState& I = Ws.Island;
			const PhysicsParams& P = Ws.Params;
			const bool Fresh = !I.Active;
			if (Fresh)
			{
				I.Solver.Reset(Time, Seed.RailTop ? CliMode::Rigid : CliMode::Compliant, P.Cli, P.BallBall, P.Cloth, P.Gravity, P.Numerics);
				// Tilted table (architecture 8.11): in-plane gravity on every body; (0, 0) (incl. -0) leaves the level island bitwise.
				I.Solver.SetInPlaneGravity(InPlaneGravity(P.Tilt, P.Gravity));
				I.Active = true;
				I.StartTime = Time;
				I.RigidSince = Seed.RailTop ? Time : -1.0;
				I.StepRecords.Clear();
				++Ws.Result->Diagnostics.Islands;
				if (Seed.RailTop)
				{
					++Ws.Result->Diagnostics.IslandRigidSwitches;
				}
			}
			const double T = I.Solver.Time();

			// Seed members (their states at the island time).
			BallState States[kMaxBalls];
			bool Known[kMaxBalls] = {};
			const auto StateOf = [&](int Ball) -> const BallState&
			{
				if (!Known[Ball])
				{
					States[Ball] = (SeedState != nullptr && Ball == Seed.BallA) ? *SeedState : EventStateAt(Ws, Ball, T, Time);
					Known[Ball] = true;
				}
				return States[Ball];
			};
			const bool PairSeed = Seed.BallB >= 0 && Seed.BallB < kMaxBalls && Seed.BallB != Seed.BallA;
			const auto PositionOf = [&](int Ball) -> BallState
			{
				const int Index = I.Solver.FindBody(Ball);
				if (Index >= 0)
				{
					return BodyState(I.Solver.Body(Index));
				}
				return StateOf(Ball);
			};
			const BallState SA = PositionOf(Seed.BallA);
			const BallState SB = PairSeed ? PositionOf(Seed.BallB) : BallState{};
			const double DeltaCl = SeedJoinDistance(Ws, Seed, &SA, PairSeed ? &SB : nullptr);

			// BFS over balls with gap <= delta_cl (collisions 3.9.2 step 1), starting from the seed balls.
			int Queue[kMaxBalls];
			int QueueSize = 0;
			bool Queued[kMaxBalls] = {};
			const auto Enqueue = [&](int Ball, bool IsSeed)
			{
				if (Queued[Ball])
				{
					return;
				}
				if (I.Solver.FindBody(Ball) < 0)
				{
					const BallSlot& B = Ws.Balls[Ball];
					if (!B.InPlay || B.InIsland || IsTerminal(B.Seg.State) || (!IsSeed && IsInPocket(B.Seg.State)))
					{
						return;
					}
					if (!AddMember(Ws, Ball, StateOf(Ball), T, Seed.RailTop && Ball == Seed.BallA))
					{
						return;
					}
				}
				Queued[Ball] = true;
				Queue[QueueSize++] = Ball;
			};
			Enqueue(Seed.BallA, true);
			if (PairSeed)
			{
				Enqueue(Seed.BallB, true);
			}
			for (int q = 0; q < QueueSize; ++q)
			{
				const int From = Queue[q];
				const int Index = I.Solver.FindBody(From);
				if (Index < 0)
				{
					continue;
				}
				const IslandBody& Body = I.Solver.Body(Index);
				for (int Ball = 0; Ball < kMaxBalls; ++Ball)
				{
					if (Queued[Ball] || !CanJoin(Ws, Ball))
					{
						continue;
					}
					const BallState& S = StateOf(Ball);
					if (Length(S.Position - Body.Position) - (Body.Radius + SpecOf(Ws, Ball).Radius) <= DeltaCl)
					{
						Enqueue(Ball, false);
					}
				}
			}
			if (I.Solver.BodyCount() == 0)
			{
				I.Active = false; // nothing could join (capacity): the island does not exist
				return;
			}

			// Tips first (their energy widens the query window), then the table features within delta_cl and the reach of the next
			// query window; the seed feature always.
			JoinTips(Ws, T, DeltaCl + Ws.Params.Numerics.LeaveDistance + QueryWindowOf(Ws).Speed * StepSize(Ws) * MinQuerySteps(Ws));
			AddFeaturesForReach(Ws, !Fresh);
			if (Seed.Feature.Kind != TableFeatureKind::None && !HasFeature(I.Solver, Seed.Feature))
			{
				IslandFeature Pieces[kMaxFeaturePieces];
				const int N = MakeIslandFeatures(Seed.Feature, TableOf(Ws), P, Pieces);
				for (int k = 0; k < N; ++k)
				{
					if (!I.Solver.AddFeature(Pieces[k]))
					{
						CapacityDiagnostic(Ws, T, static_cast<double>(I.Solver.FeatureCount()));
					}
				}
			}
			ClearZenoHistories(Ws, MemberMask(Ws));

			if (Fresh)
			{
				ShotEvent E;
				E.Time = T;
				E.Type = ShotEventType::IslandBegin;
				E.A = static_cast<BallId>(LowestMember(Ws));
				E.Value = static_cast<double>(I.Solver.BodyCount());
				E.Flags = Seed.Pressing ? ShotEventFlags::Pressing : 0;
				EmitEvent(Ws, E);
			}
			// A Zeno seed's ZenoGuard event is logged by the loop, which owns the Zeno detector (SimIslandInternal.h conventions; it was
			// logged twice per trigger before the review).
		}

		// ---------------------------------------------------------------------------------------------
		// Per-step work
		// ---------------------------------------------------------------------------------------------

		// Member states before a step. Plain storage without default initialisation (the Vec3 initialisers would zero 1.2 kB every
		// step); only the first Count entries are written and read.
		struct PreVec
		{
			double x;
			double y;
			double z;

			Vec3 Get() const { return Vec3{x, y, z}; }
			void Set(const Vec3& V)
			{
				x = V.x;
				y = V.y;
				z = V.z;
			}
		};

		struct PreStep
		{
			int Count = 0;
			int Ball[kMaxBalls];
			PreVec Position[kMaxBalls];
			PreVec Velocity[kMaxBalls];
			PreVec Omega[kMaxBalls];
		};

		void CapturePreStep(const Workspace& Ws, PreStep& Out)
		{
			const CompliantIsland& Solver = Ws.Island.Solver;
			Out.Count = Solver.BodyCount();
			for (int k = 0; k < Out.Count; ++k)
			{
				const IslandBody& B = Solver.Body(k);
				Out.Ball[k] = B.Ball;
				Out.Position[k].Set(B.Position);
				Out.Velocity[k].Set(B.Velocity);
				Out.Omega[k].Set(B.Omega);
			}
		}

		int PreIndex(const PreStep& Pre, int Ball)
		{
			for (int k = 0; k < Pre.Count; ++k)
			{
				if (Pre.Ball[k] == Ball)
				{
					return k;
				}
			}
			return -1;
		}

		BallState PreState(const PreStep& Pre, int Ball)
		{
			BallState S;
			const int k = PreIndex(Pre, Ball);
			if (k >= 0)
			{
				S.Position = Pre.Position[k].Get();
				S.Velocity = Pre.Velocity[k].Get();
				S.Omega = Pre.Omega[k].Get();
			}
			S.State = MotionState::Sliding;
			return S;
		}

		BallState PostState(const Workspace& Ws, int Ball)
		{
			const int Index = Ws.Island.Solver.FindBody(Ball);
			return Index >= 0 ? BodyState(Ws.Island.Solver.Body(Index)) : BallState{};
		}

		// Position of any in-play ball at island time T (members: their body; others: their segment).
		Vec3 AnyPosition(const Workspace& Ws, int Ball, double T)
		{
			const int Index = Ws.Island.Solver.FindBody(Ball);
			if (Index >= 0)
			{
				return Ws.Island.Solver.Body(Index).Position;
			}
			return BallStateAt(Ws, Ball, T).Position;
		}

		void NoteBallContact(Workspace& Ws, int A, int B)
		{
			Ws.Balls[A].ContactSinceJump |= 1u << B;
			Ws.Balls[B].ContactSinceJump |= 1u << A;
			for (int s = 0; s < Ws.Input->Strikes.Size() && s < kMaxStrikes; ++s)
			{
				TipSlot& Tip = Ws.Tips[s];
				if ((Tip.Path.StruckBall == A && Tip.FrozenTarget != B) || (Tip.Path.StruckBall == B && Tip.FrozenTarget != A))
				{
					Tip.StruckTouchedOther = true;
				}
			}
		}

		// Island records of one step -> events (collisions 3.9.6, architecture 8.6). Contact records set Touched[ball].
		void EmitRecords(Workspace& Ws, const PreStep& Pre, double T, bool* Touched)
		{
			IslandState& I = Ws.Island;
			const TableGeometry& Table = TableOf(Ws);
			const NumericsConfig& N = Ws.Params.Numerics;
			for (const IslandContactRecord& R : I.StepRecords)
			{
				switch (R.Kind)
				{
				case IslandRecordKind::BallBall:
				{
					if (R.BallA < 0 || R.BallB < 0 || R.BallA >= kMaxBalls || R.BallB >= kMaxBalls)
					{
						break;
					}
					ShotEvent E;
					E.Time = R.Time;
					E.Type = ShotEventType::BallBall;
					E.A = static_cast<BallId>(R.BallA);
					E.B = static_cast<BallId>(R.BallB);
					E.Normal = R.Normal;
					E.NormalSpeed = R.NormalSpeed;
					E.Flags = static_cast<std::uint8_t>(ShotEventFlags::FromIsland | (Abs(R.NormalSpeed) <= N.ApproachSpeedTol ? ShotEventFlags::Pressing : 0));
					E.Pre[0] = PreState(Pre, R.BallA);
					E.Pre[1] = PreState(Pre, R.BallB);
					E.Post[0] = PostState(Ws, R.BallA);
					E.Post[1] = PostState(Ws, R.BallB);
					if (R.BallA == kCueBallId)
					{
						E.CutAngle = CutAngle(E.Pre[0].Velocity, R.Normal);
					}
					EmitEvent(Ws, E);
					NoteBallContact(Ws, R.BallA, R.BallB);
					Touched[R.BallA] = true;
					Touched[R.BallB] = true;
					break;
				}
				case IslandRecordKind::BallFeature:
				{
					if (R.BallA < 0 || R.BallA >= kMaxBalls || R.Feature < 0 || R.Feature >= I.Solver.FeatureCount())
					{
						break;
					}
					const IslandFeature& F = I.Solver.Feature(R.Feature);
					const TableFeatureRef Ref = SourceOf(F);
					ShotEvent E;
					E.Time = R.Time;
					E.A = static_cast<BallId>(R.BallA);
					E.Normal = R.Normal;
					E.NormalSpeed = R.NormalSpeed;
					E.Flags = ShotEventFlags::FromIsland;
					E.Pre[0] = PreState(Pre, R.BallA);
					E.Post[0] = PostState(Ws, R.BallA);
					switch (Ref.Kind)
					{
					case TableFeatureKind::NoseSegment:
						E.Type = ShotEventType::BallCushion;
						E.Feature = Ref.Index;
						break;
					case TableFeatureKind::JawArc:
					case TableFeatureKind::FacingFace:
					case TableFeatureKind::FacingTopEdge:
					{
						E.Type = ShotEventType::BallJaw;
						E.Feature = static_cast<std::uint8_t>(Ref.Index / 2);
						const std::uint8_t Element = Ref.Kind == TableFeatureKind::JawArc ? 0 : (Ref.Kind == TableFeatureKind::FacingFace ? 1 : 2);
						E.SubFeature = static_cast<std::uint8_t>((Ref.Index & 1) | (Element << 4));
						break;
					}
					default:
					{
						E.Type = ShotEventType::BallRailTop;
						const int Poly = Ref.Index < Table.RailTops.Size() ? Ref.Index : 0;
						E.Feature = static_cast<std::uint8_t>(Table.RailTops.Size() > 0 ? Table.RailTops[Poly].Cushion : CushionId::None);
						E.SubFeature = static_cast<std::uint8_t>(Table.RailTops.Size() > 0 ? Table.RailTops[Poly].Kind : RailTopKind::CushionTop);
						E.Value = static_cast<double>(Ref.Index);
						break;
					}
					}
					if (F.RailFeature < 32 && ((Ws.Balls[R.BallA].InitialFreezeRails >> F.RailFeature) & 1u) != 0)
					{
						E.Flags = static_cast<std::uint8_t>(E.Flags | ShotEventFlags::ContinuesInitialFreeze);
					}
					EmitEvent(Ws, E);
					NoteRailContact(Ws, R.BallA);
					Touched[R.BallA] = true;
					break;
				}
				case IslandRecordKind::TipBegin:
				case IslandRecordKind::TipEnd:
				{
					if (R.BallA < 0 || R.BallA >= kMaxBalls || R.Strike < 0 || R.Strike >= kMaxStrikes)
					{
						break;
					}
					TipSlot& Tip = Ws.Tips[R.Strike];
					const bool Begin = R.Kind == IslandRecordKind::TipBegin;
					if (Begin && Tip.ContactBall == R.BallA)
					{
						break; // the open interval of this ball continues inside the island (e.g. the frozen cue ball at t = 0+)
					}
					if (!Begin && Tip.ContactBall != R.BallA)
					{
						break; // no open interval on this ball
					}
					if (Begin && Tip.ContactBall != kNoBall)
					{
						// One open interval per cue: the previous ball's ends here.
						ShotEvent End = MakeBallEvent(ShotEventType::TipContactEnd, R.Time, Tip.ContactBall, PreState(Pre, Tip.ContactBall));
						End.Feature = static_cast<std::uint8_t>(R.Strike);
						End.B = Tip.FrozenTarget;
						End.SubFeature = Tip.StruckTouchedOther ? 1 : 0;
						EmitEvent(Ws, End);
					}
					ShotEvent E = MakeBallEvent(Begin ? ShotEventType::TipContactBegin : ShotEventType::TipContactEnd, R.Time, R.BallA, PreState(Pre, R.BallA));
					E.Post[0] = PostState(Ws, R.BallA);
					E.Feature = static_cast<std::uint8_t>(R.Strike);
					E.B = Tip.FrozenTarget;
					E.Normal = R.Normal;
					E.NormalSpeed = R.NormalSpeed;
					E.Flags = ShotEventFlags::FromIsland;
					E.SubFeature = Tip.StruckTouchedOther ? 1 : 0;
					if (Tip.FrozenTarget != kNoBall)
					{
						const int F = Tip.FrozenTarget;
						E.Value = Length(AnyPosition(Ws, F, T) - E.Pre[0].Position) - (SpecOf(Ws, F).Radius + SpecOf(Ws, R.BallA).Radius);
					}
					EmitEvent(Ws, E);
					Tip.ContactBall = Begin ? static_cast<BallId>(R.BallA) : kNoBall;
					Touched[R.BallA] = true;
					break;
				}
				}
			}
		}

		// (WP-10 performance) Positions of event-mode balls at the ends of the island's steps, carried from one step to the next within
		// an AdvanceIsland call (their segments do not change during a call: every event, join, exit or release returns to the loop).
		struct EventPositions
		{
			double Time[2] = {-1.0, -1.0};
			std::uint32_t Have[2] = {};
			Vec3 At[2][kMaxBalls];
		};

		// BallStateAt(Ws, Ball, T).Position of an event-mode ball without the rest of the state, bitwise: EvaluateSegment's position
		// is PositionAt at the local time clamped to [0, TauEnd]; a pivot uses its true path.
		Vec3 EventPositionAt(const Workspace& Ws, int Ball, double T)
		{
			const MotionSegment& Seg = Ws.Balls[Ball].Seg;
			if (Seg.State == MotionState::PocketPivot)
			{
				return BallStateAt(Ws, Ball, T).Position;
			}
			const double Tau = T - Seg.T0;
			return PositionAt(Seg, !(Tau > 0.0) ? 0.0 : (Tau < Seg.TauEnd ? Tau : Seg.TauEnd));
		}

		// Observers of the members in Test over the step [T0, T1] (architecture 8.7: crossing time by linear interpolation).
		void MemberObservers(Workspace& Ws, const PreStep& Pre, double T0, double T1, std::uint32_t Test, EventPositions& Cache)
		{
			const TableGeometry& Table = TableOf(Ws);
			const CompliantIsland& Solver = Ws.Island.Solver;
			const double Eps = Ws.Params.Numerics.LineCrossEps;
			struct Crossing
			{
				double Time;
				int Ball;
				int Line;
				std::int8_t Direction;
			};
			Crossing Found[kTableLineCount * kMaxBalls]; // at most one crossing per line and member per step (Up > Down)
			int Count = 0;
			const TableLandmarks& L = Table.Landmarks;
			const double Lines[kTableLineCount] = {L.HeadStringX, L.FootStringX, L.CenterStringX, L.LongStringY, L.BaulkX};
			// (WP-10 performance, bitwise the same tests) Body index of every ball, and the positions at T1 of the balls the freeze
			// tests read, each evaluated once per step instead of once per (member, partner): a break tests most members every step
			// (their pending initial ball freezes give ExitClearance 0), which made this function the largest cost of the 15-ball
			// break (~1 us per step).
			int BodyOf[kMaxBalls];
			for (int b = 0; b < kMaxBalls; ++b)
			{
				BodyOf[b] = -1;
			}
			for (int i = 0; i < Solver.BodyCount(); ++i)
			{
				BodyOf[Solver.Body(i).Ball] = i;
			}
			// Event-mode balls: the step's two ends come from Cache, whose T1 buffer becomes the next step's T0 buffer.
			int Before = 0;
			if (Cache.Time[0] == T0)
			{
				Before = 0;
			}
			else if (Cache.Time[1] == T0)
			{
				Before = 1;
			}
			else
			{
				Cache.Time[0] = T0;
				Cache.Have[0] = 0;
			}
			const int After = 1 - Before;
			if (Cache.Time[After] != T1)
			{
				Cache.Time[After] = T1;
				Cache.Have[After] = 0;
			}
			const auto EventPosition = [&](int Buffer, int Other, double Time) -> const Vec3&
			{
				if (((Cache.Have[Buffer] >> Other) & 1u) == 0)
				{
					Cache.At[Buffer][Other] = EventPositionAt(Ws, Other, Time);
					Cache.Have[Buffer] |= 1u << Other;
				}
				return Cache.At[Buffer][Other];
			};
			const auto PositionOf = [&](int Other) -> Vec3
			{
				return BodyOf[Other] >= 0 ? Solver.Body(BodyOf[Other]).Position : EventPosition(After, Other, T1); // = AnyPosition
			};
			// Positions at T0 for the jump-over test: the pre-step state of a member, else the ball's segment at T0 (PreIndex order).
			int PreOf[kMaxBalls];
			for (int b = 0; b < kMaxBalls; ++b)
			{
				PreOf[b] = -1;
			}
			for (int k = Pre.Count - 1; k >= 0; --k)
			{
				PreOf[Pre.Ball[k]] = k; // the first index of the ball, as PreIndex returns
			}
			const auto PositionBefore = [&](int Other) -> Vec3
			{
				return PreOf[Other] >= 0 ? Pre.Position[PreOf[Other]].Get() : EventPosition(Before, Other, T0);
			};
			std::uint32_t LiveMask = 0;
			double RadiusOf[kMaxBalls];
			Vec2 PlanBefore[kMaxBalls];
			Vec2 PlanAfter[kMaxBalls];
			bool HaveLive = false;
			for (int k = 0; k < Pre.Count; ++k)
			{
				const int Ball = Pre.Ball[k];
				if (((Test >> Ball) & 1u) == 0)
				{
					continue;
				}
				const int Index = BodyOf[Ball];
				if (Index < 0)
				{
					continue;
				}
				const IslandBody& Body = Solver.Body(Index);
				const Vec3 P0 = Pre.Position[k].Get();
				const Vec3 P1 = Body.Position;
				for (int Line = 0; Line < kTableLineCount; ++Line)
				{
					const bool AlongY = Line == static_cast<int>(TableLine::LongString);
					const double C0 = AlongY ? P0.y : P0.x;
					const double C1 = AlongY ? P1.y : P1.x;
					const double Up = Lines[Line] + Eps;
					const double Down = Lines[Line] - Eps;
					if (C0 < Up && C1 >= Up)
					{
						Found[Count++] = {T0 + (T1 - T0) * (Up - C0) / (C1 - C0), Ball, Line, 1};
					}
					else if (C0 > Down && C1 <= Down)
					{
						Found[Count++] = {T0 + (T1 - T0) * (C0 - Down) / (C0 - C1), Ball, Line, -1};
					}
				}
				// Freeze-leave (rules continuesInitialFreeze): the member separated from its t = 0 rail features / balls.
				BallSlot& B = Ws.Balls[Ball];
				if (B.InitialFreezeRails != 0)
				{
					const double Rc = ComputeCushionContact(Body.Radius, Table.Spec.CushionNoseHeight, Ws.Params.Cushion.NoseProfileRadius,
						Ws.Params.Cushion.PooltoolCompat).HorizontalOffset;
					for (int Rail = 0; Rail < kRailFeatureCount; ++Rail)
					{
						if (((B.InitialFreezeRails >> Rail) & 1u) == 0)
						{
							continue;
						}
						double Gap = kInfinity;
						if (Rail < kCushionCount && Rail < Table.Noses.Size())
						{
							const NoseSegment& Nose = Table.Noses[Rail];
							Gap = Dot(XY(P1) - Nose.Start, Nose.InwardNormal) - Rc;
						}
						else if (Rail >= kCushionCount && Rail - kCushionCount < Table.JawArcs.Size())
						{
							const JawArc& Arc = Table.JawArcs[Rail - kCushionCount];
							Gap = Length(XY(P1) - Arc.Center) - (Arc.Radius + Rc);
						}
						if (Gap > Ws.Params.Numerics.LeaveDistance)
						{
							B.InitialFreezeRails &= ~(1u << Rail);
						}
					}
				}
				if (B.InitialFreezeBalls != 0)
				{
					// The set bits in ascending order (the order of the former 0..23 loop; each test reads only its own bit).
					for (std::uint32_t Pending = B.InitialFreezeBalls; Pending != 0; Pending &= Pending - 1)
					{
						const int Other = std::countr_zero(Pending);
						if (!Ws.Balls[Other].InPlay)
						{
							continue;
						}
						// Still within LeaveDistance with a relative margin (squared, no root): the exact test below is false as well.
						const double Sum = Body.Radius + SpecOf(Ws, Other).Radius;
						const double Inside = (Sum + Ws.Params.Numerics.LeaveDistance) * (1.0 - 1e-9);
						const Vec3 D = PositionOf(Other) - P1;
						if (LengthSquared(D) < Inside * Inside)
						{
							continue;
						}
						if (Length(D) - Sum > Ws.Params.Numerics.LeaveDistance)
						{
							B.InitialFreezeBalls &= ~(1u << Other);
						}
					}
				}
				// Jump-over (rules F9): plan overlaps entered / left while airborne.
				if (P0.z - Body.Radius > Ws.Params.Numerics.EpsZ)
				{
					if (!HaveLive)
					{
						// In play and not terminal, the radii and the plan positions at T0 / T1, once per call (members lifted by contact
						// friction are common in a break).
						for (int Other = 0; Other < kMaxBalls; ++Other)
						{
							const bool Live = Ws.Balls[Other].InPlay && !IsTerminal(Ws.Balls[Other].Seg.State);
							LiveMask |= Live ? 1u << Other : 0u;
							RadiusOf[Other] = Live ? SpecOf(Ws, Other).Radius : 0.0;
							if (Live)
							{
								PlanBefore[Other] = XY(PositionBefore(Other));
								PlanAfter[Other] = XY(PositionOf(Other));
							}
						}
						HaveLive = true;
					}
					const Vec2 Plan0 = XY(P0);
					const Vec2 Plan1 = XY(P1);
					for (std::uint32_t Others = LiveMask & ~(1u << Ball); Others != 0; Others &= Others - 1)
					{
						const int Other = std::countr_zero(Others); // ascending, as the former 0..23 loop
						const double Sum = Body.Radius + RadiusOf[Other];
						// (WP-10 performance, the same transitions) Both plan distances clearly on the same side of Sum (squared, with a
						// relative margin): neither an entry nor a leave. Members lifted by contact friction (z - R > EpsZ by micrometres)
						// ran this loop for every ball at every step of a break.
						const double Above = Sum * (1.0 + 1e-9);
						const double Below = Sum * (1.0 - 1e-9);
						const double S0 = LengthSquared(Plan0 - PlanBefore[Other]);
						const double S1 = LengthSquared(Plan1 - PlanAfter[Other]);
						if ((S0 > Above * Above && S1 > Above * Above) || (S0 < Below * Below && S1 < Below * Below))
						{
							continue;
						}
						const double D0 = Length(Plan0 - PlanBefore[Other]);
						const double D1 = Length(Plan1 - PlanAfter[Other]);
						const std::uint32_t Bit = 1u << Other;
						if (D0 > Sum && D1 <= Sum)
						{
							B.JumpPending |= Bit;
							B.ContactSinceJump &= ~Bit;
						}
						else if (D0 < Sum && D1 >= Sum && (B.JumpPending & Bit) != 0)
						{
							if ((B.ContactSinceJump & Bit) == 0)
							{
								ShotEvent E = MakeBallEvent(ShotEventType::BallJumpedOver, T1, Ball, BodyState(Body));
								E.B = static_cast<BallId>(Other);
								EmitEvent(Ws, E);
							}
							B.JumpPending &= ~Bit;
						}
					}
				}
			}
			// Line crossings in (Time, ball, line) order.
			for (int i = 1; i < Count; ++i)
			{
				const Crossing Key = Found[i];
				int j = i - 1;
				while (j >= 0 && (Found[j].Time > Key.Time || (Found[j].Time == Key.Time && (Found[j].Ball > Key.Ball || (Found[j].Ball == Key.Ball && Found[j].Line > Key.Line)))))
				{
					Found[j + 1] = Found[j];
					--j;
				}
				Found[j + 1] = Key;
			}
			for (int i = 0; i < Count; ++i)
			{
				const int Index = Solver.FindBody(Found[i].Ball);
				ShotEvent E = MakeBallEvent(ShotEventType::BallLineCross, Found[i].Time, Found[i].Ball, Index >= 0 ? BodyState(Solver.Body(Index)) : BallState{});
				E.Feature = static_cast<std::uint8_t>(Found[i].Line);
				E.SubFeature = Found[i].Direction > 0 ? 0 : 1;
				EmitEvent(Ws, E);
			}
		}

		enum class ExitKind : std::uint8_t
		{
			None,
			DropEdge,  // a cloth member reached a drop-edge circle: pocket state machine
			Capture,   // PocketModel::CaptureCircle: center inside the capture circle
			Fly,       // over a pocket hole, off the cloth region, off the rail top: event mode (Airborne)
			Cap,       // a rail-top member settled on the flat cap: analytic cap segment
		};

		// No island feature within LeaveDistance of the body (it is not rolling over an edge or pressed against a cushion).
		bool FreeOfFeatures(const Workspace& Ws, const IslandBody& Body)
		{
			const CompliantIsland& Solver = Ws.Island.Solver;
			for (int f = 0; f < Solver.FeatureCount(); ++f)
			{
				if (IslandFeatureGap(Solver.Feature(f), Body.Position, Body.Radius) <= Ws.Params.Numerics.LeaveDistance)
				{
					return false;
				}
			}
			return true;
		}

		ExitKind MemberExit(const Workspace& Ws, const IslandBody& Body, int& Where)
		{
			const TableGeometry& Table = TableOf(Ws);
			const Vec2 P = XY(Body.Position);
			const NumericsConfig& N = Ws.Params.Numerics;
			Where = -1;
			const int Pocket = PocketContaining(Table, Ws.Params.Pockets, P);
			if (Body.ClothSupport)
			{
				if (Pocket >= 0)
				{
					Where = Pocket;
					if (Ws.Params.Pockets == PocketModel::CaptureCircle)
					{
						return ExitKind::Capture;
					}
					return Body.Position.z - Body.Radius <= kClothSnapHeight && Abs(Body.Velocity.z) <= N.RestSpeed ? ExitKind::DropEdge : ExitKind::Fly;
				}
				// Over the rail (behind a nose line, outside every pocket opening): only an airborne member gets there; it leaves
				// once it is clear of the nose / jaw it may be rolling over (the rail top is event mode's).
				return OverRailRegion(Table, Body.Position) && FreeOfFeatures(Ws, Body) ? ExitKind::Fly : ExitKind::None;
			}
			// Rail-top member (collisions 6.2): beyond the outer boundary -> flight (the boundary event follows at once); off the
			// rail-top surfaces (back over the table, a pocket opening or a cut disc) -> flight once clear of the nose / edge /
			// rim it rolls over; settled on the flat cap -> cap segment.
			if (!Table.OuterBoundary.Contains(P))
			{
				return ExitKind::Fly;
			}
			// Whether a rail-top surface is below the member is FindRailTopPolygon's alone: the polygons cover the rail top incl. the pocket
			// surrounds and exclude the playing area, the pocket openings and the cut discs. (The drop-edge circle a_d and
			// IsOverPocketOpening are cloth-level regions: the first reaches r_d beyond the cut disc onto the cap, the second covers cap
			// surrounds behind a corner pocket. Testing them here kept a member rolling or spinning on the cap there in the rigid island
			// until it came to rest - 98 000 steps for a ball spinning out on a corner surround, up to the whole step budget; review fix.)
			const int Any = FindRailTopPolygon(Table, P, true, RailTopKind::CushionTop, Vec2{}, 0.0);
			if (Any < 0)
			{
				return FreeOfFeatures(Ws, Body) ? ExitKind::Fly : ExitKind::None;
			}
			const RailTopPolygon& Poly = Table.RailTops[Any];
			if (Poly.Kind == RailTopKind::RailCap && Abs(Body.Position.z - (Poly.PlanePoint.z + Body.Radius)) <= kSupportHeightTol &&
				Abs(Body.Velocity.z) <= N.RestSpeed)
			{
				Where = Any;
				return ExitKind::Cap;
			}
			return ExitKind::None;
		}

		// Plan distance [m] a cloth member can move from where it is before MemberObservers or MemberExit could report anything for
		// it: the nearest line-crossing threshold (line +- LineCrossEps), drop-edge (capture) circle and side of the nose-line
		// rectangle, and for pending initial rail freezes LeaveDistance minus the member's gap to each frozen rail feature (the plan
		// gaps of MemberObservers are 1-Lipschitz in the plan position; integration round 2: a ball pressed along the rail, frozen to
		// it at t = 0 like Z-3, ran both tests at every step - a fifth of the A-ISL-2 CPU). 0 where the tests depend on more than the
		// plan position: rail-top members, airborne members (jump-over) and pending initial ball freezes (the other balls' positions).
		double ExitClearance(const Workspace& Ws, const IslandBody& Body)
		{
			const TableGeometry& Table = TableOf(Ws);
			const BallSlot& B = Ws.Balls[Body.Ball];
			const NumericsConfig& N = Ws.Params.Numerics;
			if (!Body.ClothSupport || Body.Position.z - Body.Radius > N.EpsZ || B.InitialFreezeBalls != 0)
			{
				return 0.0;
			}
			const Vec2 P = XY(Body.Position);
			const Aabb2& Area = Table.PlayingArea;
			if (!Area.Contains(P))
			{
				return 0.0;
			}
			double Clear = Min(Min(P.x - Area.Lo.x, Area.Hi.x - P.x), Min(P.y - Area.Lo.y, Area.Hi.y - P.y));
			const bool Capture = Ws.Params.Pockets == PocketModel::CaptureCircle;
			for (const PocketGeometry& Pocket : Table.Pockets)
			{
				Clear = Min(Clear, Length(P - Pocket.CaptureCenter) - (Capture ? Pocket.CaptureRadius : Pocket.DropEdgeRadius));
			}
			const TableLandmarks& L = Table.Landmarks;
			const double Lines[kTableLineCount] = {L.HeadStringX, L.FootStringX, L.CenterStringX, L.LongStringY, L.BaulkX};
			for (int Line = 0; Line < kTableLineCount; ++Line)
			{
				const double C = Line == static_cast<int>(TableLine::LongString) ? P.y : P.x;
				Clear = Min(Clear, Abs(C - Lines[Line]) - N.LineCrossEps);
			}
			if (B.InitialFreezeRails != 0)
			{
				// The same gaps as MemberObservers' freeze-leave test.
				const double Rc = ComputeCushionContact(Body.Radius, Table.Spec.CushionNoseHeight, Ws.Params.Cushion.NoseProfileRadius,
					Ws.Params.Cushion.PooltoolCompat).HorizontalOffset;
				for (int Rail = 0; Rail < kRailFeatureCount; ++Rail)
				{
					if (((B.InitialFreezeRails >> Rail) & 1u) == 0)
					{
						continue;
					}
					double Gap = kInfinity;
					if (Rail < kCushionCount && Rail < Table.Noses.Size())
					{
						const NoseSegment& Nose = Table.Noses[Rail];
						Gap = Dot(P - Nose.Start, Nose.InwardNormal) - Rc;
					}
					else if (Rail >= kCushionCount && Rail - kCushionCount < Table.JawArcs.Size())
					{
						const JawArc& Arc = Table.JawArcs[Rail - kCushionCount];
						Gap = Length(P - Arc.Center) - (Arc.Radius + Rc);
					}
					Clear = Min(Clear, N.LeaveDistance - Gap);
				}
			}
			return Clear > 1e-9 ? Clear - 1e-9 : 0.0; // NaN (a missing line) -> 0
		}

		// Members (of those in Test) that must continue in event mode (architecture 8.8 member exits). Returns true if one left.
		bool MemberExits(Workspace& Ws, double T, std::uint32_t Test)
		{
			IslandState& I = Ws.Island;
			bool Changed = false;
			for (int k = I.Solver.BodyCount() - 1; k >= 0; --k)
			{
				const IslandBody& Body = I.Solver.Body(k);
				if (((Test >> Body.Ball) & 1u) == 0)
				{
					continue;
				}
				int Where = -1;
				const ExitKind Kind = MemberExit(Ws, Body, Where);
				if (Kind == ExitKind::None)
				{
					continue;
				}
				// A rail-top member settling on the cap while pressed by another member stays (a pile-up on the rail). So does a cloth
				// member reaching a drop-edge (capture) circle while another member still touches it (integration round 2): pocket
				// states never join islands and island members have no contacts with pocket-state balls, so a ball released into the
				// pocket at the speed it had one step into a collision (about zero) was passed through by the ball pushing it, and the
				// next island met that overlap (an explosion to 40 m/s). Held until the pair has separated by LeaveDistance, it
				// leaves with its post-collision velocity (over the flat cloth plane of the island for the fraction of a millisecond).
				double NearestGap = kInfinity;
				for (int j = 0; j < I.Solver.BodyCount(); ++j)
				{
					const IslandBody& Other = I.Solver.Body(j);
					if (j != k)
					{
						NearestGap = Min(NearestGap, Length(Other.Position - Body.Position) - (Other.Radius + Body.Radius));
					}
				}
				if ((Kind == ExitKind::Cap || Kind == ExitKind::DropEdge || Kind == ExitKind::Capture) && NearestGap <= Ws.Params.Numerics.LeaveDistance)
				{
					continue;
				}
				const int Ball = Body.Ball;
				IslandBody Out;
				if (!I.Solver.RemoveBody(Ball, Out))
				{
					continue;
				}
				Changed = true;
				BallState S = BodyState(Out);
				EndSampledTrack(Ws, Ball, S.Position, T);
				Ws.Balls[Ball].InIsland = false;
				switch (Kind)
				{
				case ExitKind::DropEdge:
				{
					// The center crossed the drop-edge circle within the step: the pivot starts on the circle (radial projection,
					// at most v dt). A member held by a touching neighbour (above) may be deeper inside: it starts where it is when the
					// projection would take it toward a remaining member (EnterPocketOverDropEdge accepts a center just inside).
					const PocketGeometry& G = TableOf(Ws).Pockets[Where];
					const Vec2 H = XY(S.Position) - G.CaptureCenter;
					const double Rho = Length(H);
					if (Rho > 0.0 && G.DropEdgeRadius - Rho < NearestGap - Ws.Params.Numerics.ContactTol)
					{
						const Vec2 OnCircle = G.CaptureCenter + H * (G.DropEdgeRadius / Rho);
						S.Position.x = OnCircle.x;
						S.Position.y = OnCircle.y;
					}
					EnterPocketOverDropEdge(Ws, Ball, S, Where, T);
					break;
				}
				case ExitKind::Capture:
					CaptureInCircle(Ws, Ball, S, Where, T);
					break;
				case ExitKind::Cap:
					ContinueOnCap(Ws, Ball, S, Where, T);
					break;
				case ExitKind::Fly:
				case ExitKind::None:
				{
					BallSlot& B = Ws.Balls[Ball];
					S.State = MotionState::Airborne;
					B.Context.Support = SupportKind::Cloth;
					B.Context.Pocket = PocketId::None;
					B.BounceIndex = 0;
					B.SequenceMaxZ = Max(B.SequenceMaxZ, S.Velocity.z > 0.0 ? S.Position.z + S.Velocity.z * S.Velocity.z / (2.0 * Ws.Params.Gravity) : S.Position.z);
					// Inside a pocket's cylinder at or below the cloth level (a pivoting seed ball): it falls in the pocket.
					const int Pocket = PocketContaining(TableOf(Ws), PocketModel::GeometricLevelA, XY(S.Position));
					if (Pocket >= 0 && S.Position.z <= Out.Radius + Ws.Params.Numerics.EpsZ)
					{
						S.State = MotionState::PocketFall;
						B.Context.Pocket = static_cast<PocketId>(Pocket);
					}
					ReplaceSegment(Ws, Ball, S, T);
					break;
				}
				}
			}
			return Changed;
		}

		// Event-mode balls within delta_cl of a member join (architecture 8.8; contacts between island and event-mode balls are
		// not predicted, so this per-step test is what catches them: delta_cl >> v dt for every speed).
		// Candidates: the balls that passed CanJoin when the current AdvanceIsland call began (the set changes only through events
		// and member exits / joins / releases, after each of which AdvanceIsland returns).
		// ReachAtOne (per AdvanceIsland call, < 0 = not yet computed): IslandJoinDistance at 1 m/s with MaxMassAtStart, the largest
		// member mass when the call began (members only leave during a call).
		bool JoinBalls(Workspace& Ws, double T, std::uint32_t Candidates, double* ReachAtOne, double MaxMassAtStart)
		{
			IslandState& I = Ws.Island;
			if (Candidates == 0 || I.Solver.BodyCount() == 0)
			{
				return false;
			}
			const IslandBounds Bb = Bounds(Ws);
			const PhysicsParams& P = Ws.Params;
			bool Joined = false;
			for (int Ball = 0; Ball < kMaxBalls; ++Ball)
			{
				if (((Candidates >> Ball) & 1u) == 0 || !CanJoin(Ws, Ball))
				{
					continue;
				}
				const BallSpec& Spec = SpecOf(Ws, Ball);
				// Cheap pre-filter (every step, every event-mode ball): the segment's position and speed against the island bounds
				// inflated by delta_cl's upper bound 0.01 v (valid above 1e-7 m/s; ContactTol below). (WP-10: from the ball's state at T,
				// which the exact test needs anyway: the same position and speed as the segment's at the clamped time, joining balls are
				// never pivoting.)
				const BallState S = BallStateAt(Ws, Ball, T);
				const double Speed = Length(S.Velocity);
				const double Delta = Max(P.Numerics.ContactTol, 0.01 * (Bb.MaxSpeed + Speed));
				const double BoxReach = Bb.MaxRadius + Spec.Radius + Delta;
				if (S.Position.x < Bb.Box.Lo.x - BoxReach || S.Position.x > Bb.Box.Hi.x + BoxReach || S.Position.y < Bb.Box.Lo.y - BoxReach ||
					S.Position.y > Bb.Box.Hi.y + BoxReach || S.Position.z < Bb.Box.Lo.z - BoxReach || S.Position.z > Bb.Box.Hi.z + BoxReach)
				{
					continue;
				}
				// The exact test (WP-10 performance, the same joins in the same order): the members are scanned first, each culled by
				// Reach (squared distances); the ball's own conditions (not over a pocket hole or the rail, inside the island's box
				// inflated by Reach) are pure predicates, evaluated only for a member that would join it. A ball released inside the
				// spread-out rack of a break passes the island's box every step (the pre-filter's 0.01 v is 10 cm at the break speed):
				// evaluating its conditions for every member was the largest per-step cost of the 15-ball break. Reach bounds every
				// member's delta_cl (IslandJoinDistance grows with the speed and the mass, the approach speed is at most MaxSpeed +
				// Speed and the reduced mass at most MaxMass); the margins cover rounding.
				// Reach bound without a power per step: IslandJoinDistance = max(eps_touch, c(m) v^0.8) <= max(eps_touch, c(m) max(1, v)),
				// c(m) = its value at 1 m/s, which grows with the mass (the call's largest member mass bounds every later one).
				if (!(ReachAtOne[Ball] >= 0.0))
				{
					ReachAtOne[Ball] = IslandJoinDistance(1.0, Max(MaxMassAtStart, Spec.Mass), P.Cli, P.Numerics.ContactTol);
				}
				const double Reach = Max(P.Numerics.ContactTol, ReachAtOne[Ball] * Max(1.0, Bb.MaxSpeed + Speed));
				for (int k = 0; k < I.Solver.BodyCount(); ++k)
				{
					const IslandBody& Body = I.Solver.Body(k);
					const Vec3 D = S.Position - Body.Position;
					const double Limit = Body.Radius + Spec.Radius + Reach * (1.0 + 1e-6) + P.Numerics.ContactTol + 1e-12;
					if (LengthSquared(D) > Limit * Limit)
					{
						continue; // beyond Reach: neither closing within delta_cl nor overlapping
					}
					const double Dist = Length(D);
					const double Gap = Dist - (Body.Radius + Spec.Radius);
					const double Approach = Dist > 0.0 ? Dot(Body.Velocity - S.Velocity, D / Dist) : 0.0;
					// Closing within delta_cl (a contact while the island's contacts are still active), or already overlapping. A
					// separating or resting touch is no contact (the event-mode approach test), so a member released beside it does not
					// come back at once.
					const bool Closing = Approach > P.Numerics.ApproachSpeedTol &&
						Gap <= IslandJoinDistance(Approach, ReducedMass(Body.Mass, Spec.Mass), P.Cli, P.Numerics.ContactTol);
					if (!(Closing || Gap < -P.Numerics.ContactTol))
					{
						continue;
					}
					// A ball over a pocket hole or over the rail would leave a cloth island at once (member exits): it stays in event
					// mode; a ball rolling on the flat cap joins a rail-top island only.
					const bool OnCap = Ws.Balls[Ball].Context.Support == SupportKind::RailCap && IsOnSurface(S.State);
					const bool Blocked = OnCap ? !Bb.AnyRailTop
											   : (PocketContaining(TableOf(Ws), P.Pockets, XY(S.Position)) >= 0 || OverRailRegion(TableOf(Ws), S.Position));
					const double ExactReach = IslandJoinDistance(Bb.MaxSpeed + Speed, Max(Bb.MaxMass, Spec.Mass), P.Cli, P.Numerics.ContactTol);
					const Aabb3 Box = Bb.Box.Inflated(Bb.MaxRadius + Spec.Radius + ExactReach);
					const bool Outside = S.Position.x < Box.Lo.x || S.Position.x > Box.Hi.x || S.Position.y < Box.Lo.y || S.Position.y > Box.Hi.y ||
						S.Position.z < Box.Lo.z || S.Position.z > Box.Hi.z;
					if (!Blocked && !Outside)
					{
						FlushObservers(Ws, Ball, T);
						if (AddMember(Ws, Ball, BallStateForEvent(Ws, Ball, T), T, false))
						{
							Joined = true;
						}
					}
					break;
				}
			}
			if (Joined)
			{
				const QueryWindow W = QueryWindowOf(Ws);
				JoinTips(Ws, T, P.Numerics.LeaveDistance + W.Speed * StepSize(Ws) * MinQuerySteps(Ws));
				AddFeaturesForReach(Ws, true);
			}
			return Joined;
		}

		// Acceleration a body will have in event mode (sliding / rolling on the cloth incl. the tilt drive, ballistic otherwise, none
		// at rest): the motion laws of rb/Physics/Motion.h, as the solver's exit test assumes them.
		Vec3 EventAcceleration(const IslandBody& Body, const PhysicsParams& P)
		{
			const NumericsConfig& N = P.Numerics;
			const Vec2 InPlane = InPlaneGravity(P.Tilt, P.Gravity);
			const bool OnCloth = Body.ClothSupport && Body.Position.z - Body.Radius <= kClothSnapHeight && Abs(Body.Velocity.z) <= N.EpsV;
			if (!OnCloth)
			{
				return Vec3{InPlane.x, InPlane.y, -P.Gravity};
			}
			const Vec3 U = SlipVelocity(Body.Velocity, Body.Omega, Body.Radius);
			const double Slip = Sqrt(U.x * U.x + U.y * U.y);
			if (Slip > N.EpsV)
			{
				const double A = P.Cloth.SlidingFriction * P.Gravity / Slip;
				return Vec3{InPlane.x - A * U.x, InPlane.y - A * U.y, 0.0};
			}
			const double Speed = Sqrt(Body.Velocity.x * Body.Velocity.x + Body.Velocity.y * Body.Velocity.y);
			if (Speed > N.EpsV)
			{
				const double K = Body.Inertia / (Body.Mass * Body.Radius * Body.Radius);
				const double A = P.Cloth.RollingResistance * P.Gravity / Speed;
				return Vec3{InPlane.x / (1.0 + K) - A * Body.Velocity.x, InPlane.y / (1.0 + K) - A * Body.Velocity.y, 0.0};
			}
			return Vec3{};
		}

		// How long EventAcceleration stays what it is [s]: the rest of the sliding phase, the rolling time to rest; unbounded in
		// flight and at rest (the tilt drive neglected).
		double EventAccelerationDuration(const IslandBody& Body, const PhysicsParams& P)
		{
			const NumericsConfig& N = P.Numerics;
			const bool OnCloth = Body.ClothSupport && Body.Position.z - Body.Radius <= kClothSnapHeight && Abs(Body.Velocity.z) <= N.EpsV;
			if (!OnCloth)
			{
				return kInfinity;
			}
			const Vec3 U = SlipVelocity(Body.Velocity, Body.Omega, Body.Radius);
			const double Slip = Sqrt(U.x * U.x + U.y * U.y);
			const double K = Body.Inertia / (Body.Mass * Body.Radius * Body.Radius);
			if (Slip > N.EpsV)
			{
				return SlideDuration(Slip, P.Cloth.SlidingFriction, P.Gravity, K);
			}
			const double Speed = Sqrt(Body.Velocity.x * Body.Velocity.x + Body.Velocity.y * Body.Velocity.y);
			return Speed > N.EpsV ? RollDuration(Speed, P.Cloth.RollingResistance, P.Gravity) : kInfinity;
		}

		// True if event mode can take over a contact of gap Gap closing at Approach (> 0 approaching) with gap acceleration GapAccel
		// (event-mode laws) without handing it straight back to an island (collisions 3.6, 3.9.2; architecture 8.8 re-entry guard):
		//  * never while compressed beyond the touching band;
		//  * closing: only farther than delta_cl, the distance within which a closing contact joins an island
		//    (delta_cl = 1.2 v T_H(v) < 0.01 v for every speed above 1e-7 m/s, so larger gaps skip the Hertz time);
		//  * not closing: beyond LeaveDistance; not accelerating together (GapAccel >= 0); separating at v_rest or faster (it comes
		//    back, if at all, as an ordinary collision with e > 0); or separating fast enough to pass LeaveDistance before it turns back
		//    (gap + v^2 / (2 |GapAccel|)). Otherwise the pair returns within the touching band or as a micro-impact below v_rest, i.e.
		//    as a pressing contact 10-100 us later (the pressing rule at the touching maximum): a pair separating at um/s under
		//    friction left a pressing island 8 steps after it started, thousands of times per shot. CanExit's own test
		//    (|rate| <= v_eps and gap'' < 0) sees only the instant; this is the predictive version (DECISION). The constant-
		//    acceleration turn-around counts only if it comes well before either side's acceleration changes (Hold,
		//    EventAccelerationDuration): a ball rolling off a rail at nm/s is decelerated along its velocity, so its normal speed
		//    reaches zero exactly when it stops, and it never comes back.
		bool EventModeCanTakeOver(double Gap, double Approach, double GapAccel, double Hold, double ReducedMassValue, const PhysicsParams& P)
		{
			const NumericsConfig& N = P.Numerics;
			if (Gap < -N.ContactTol)
			{
				return false;
			}
			if (Approach > N.ApproachSpeedTol)
			{
				// delta_cl >= ContactTol: a closing pair in the touching band is an AtStart contact for event mode.
				return Gap > N.ContactTol && (Gap > 0.01 * Approach || Gap > IslandJoinDistance(Approach, ReducedMassValue, P.Cli, N.ContactTol));
			}
			const double Separation = Max(0.0, -Approach);
			if (Gap > N.LeaveDistance || !(GapAccel < 0.0) || Separation >= N.RestSpeed || Separation >= 0.5 * Hold * -GapAccel)
			{
				return true;
			}
			return Gap + Separation * Separation / (2.0 * -GapAccel) > N.LeaveDistance;
		}

		// Event-mode acceleration of a member and how long it holds.
		struct EventMotion
		{
			Vec3 Accel;
			double Hold = kInfinity;
		};

		EventMotion MotionOf(const IslandBody& Body, const PhysicsParams& P) { return EventMotion{EventAcceleration(Body, P), EventAccelerationDuration(Body, P)}; }

		// EventModeCanTakeOver for the pair (member k, member j) / (member k, feature f).
		bool PairCanLeave(const IslandBody& A, const EventMotion& MA, const IslandBody& B, const EventMotion& MB, const PhysicsParams& P)
		{
			const Vec3 D = B.Position - A.Position;
			const double Dist = Length(D);
			if (!(Dist > 0.0))
			{
				return false;
			}
			const Vec3 N = D / Dist;
			const Vec3 Dv = B.Velocity - A.Velocity;
			const double Rate = Dot(Dv, N); // gap rate (> 0 separating)
			const double GapAccel = (LengthSquared(Dv) - Rate * Rate) / Dist + Dot(MB.Accel - MA.Accel, N);
			return EventModeCanTakeOver(Dist - (A.Radius + B.Radius), -Rate, GapAccel, Min(MA.Hold, MB.Hold), ReducedMass(A.Mass, B.Mass), P);
		}

		bool FeatureCanLeave(const IslandFeature& F, const IslandBody& A, const EventMotion& MA, const PhysicsParams& P)
		{
			Vec3 Normal;
			const double Gap = IslandFeatureGap(F, A.Position, A.Radius, &Normal);
			if (Gap == kInfinity)
			{
				return true;
			}
			return EventModeCanTakeOver(Gap, -Dot(A.Velocity, Normal), Dot(MA.Accel, Normal), MA.Hold, A.Mass, P);
		}

		// Every member pair and member-feature pair can be taken over by event mode (the island may end without a re-entry).
		bool NothingReturns(const Workspace& Ws)
		{
			const CompliantIsland& Solver = Ws.Island.Solver;
			const PhysicsParams& P = Ws.Params;
			EventMotion Motion[kMaxBalls];
			for (int k = 0; k < Solver.BodyCount(); ++k)
			{
				Motion[k] = MotionOf(Solver.Body(k), P);
			}
			for (int k = 0; k < Solver.BodyCount(); ++k)
			{
				for (int j = k + 1; j < Solver.BodyCount(); ++j)
				{
					if (!PairCanLeave(Solver.Body(k), Motion[k], Solver.Body(j), Motion[j], P))
					{
						return false;
					}
				}
				for (int f = 0; f < Solver.FeatureCount(); ++f)
				{
					if (!FeatureCanLeave(Solver.Feature(f), Solver.Body(k), Motion[k], P))
					{
						return false;
					}
				}
			}
			return true;
		}

		// Members that event mode can take over (every contact with another member and with every feature passes
		// EventModeCanTakeOver) return to event mode while the rest of the island continues: cheaper (a released ball moves
		// analytically) and the same physics (event mode islands a contact again exactly when this island would keep it; a released
		// ball closing in on a member re-joins through JoinBalls). When every member can go, the island ends here.
		bool ReleaseFreeMembers(Workspace& Ws, double T)
		{
			IslandState& I = Ws.Island;
			const int Count = I.Solver.BodyCount();
			if (Count < 2)
			{
				return false;
			}
			for (int s = 0; s < kMaxStrikes; ++s)
			{
				if (I.Solver.HasTip(s))
				{
					return false; // the tip's reach is not observable from outside the solver: keep the island whole
				}
			}
			const PhysicsParams& P = Ws.Params;
			bool Release[kMaxBalls] = {};
			EventMotion Motion[kMaxBalls];
			for (int k = 0; k < Count; ++k)
			{
				Motion[k] = MotionOf(I.Solver.Body(k), P);
			}
			int Released = 0;
			for (int k = 0; k < Count; ++k)
			{
				const IslandBody& Body = I.Solver.Body(k);
				if (!Body.ClothSupport)
				{
					continue; // rail-top members leave through their own exits
				}
				bool Free = true;
				for (int j = 0; j < Count && Free; ++j)
				{
					Free = j == k || PairCanLeave(Body, Motion[k], I.Solver.Body(j), Motion[j], P);
				}
				for (int f = 0; f < I.Solver.FeatureCount() && Free; ++f)
				{
					Free = FeatureCanLeave(I.Solver.Feature(f), Body, Motion[k], P);
				}
				if (Free)
				{
					Release[Body.Ball] = true;
					++Released;
				}
			}
			if (Released == 0)
			{
				return false;
			}
			if (Released == Count)
			{
				EndIsland(Ws, T);
				return true;
			}
			for (int Ball = 0; Ball < kMaxBalls; ++Ball)
			{
				if (!Release[Ball])
				{
					continue;
				}
				IslandBody Body;
				if (I.Solver.RemoveBody(Ball, Body))
				{
					ReturnToEventMode(Ws, Body, T);
				}
			}
			return true;
		}

		void SwitchToRigid(Workspace& Ws, double T)
		{
			IslandState& I = Ws.Island;
			I.Solver.SetMode(CliMode::Rigid);
			I.RigidSince = T;
			++Ws.Result->Diagnostics.IslandRigidSwitches;
			ShotEvent E;
			E.Time = T;
			E.Type = ShotEventType::IslandRigid;
			E.A = static_cast<BallId>(LowestMember(Ws));
			E.Value = T - I.StartTime;
			EmitEvent(Ws, E);
			AddFeaturesForReach(Ws, true); // the rigid step is 20x longer: a new reach window
		}

		bool AllAtRest(const Workspace& Ws)
		{
			const CompliantIsland& Solver = Ws.Island.Solver;
			for (int k = 0; k < Solver.BodyCount(); ++k)
			{
				if (!Solver.BodyAtRest(k))
				{
					return false;
				}
			}
			return true;
		}
	}

	void StartIsland(Workspace& Ws, const IslandSeed& Seed, double Time)
	{
		StartIslandImpl(Ws, Seed, Time, nullptr);
	}

	void StartIslandWithState(Workspace& Ws, const IslandSeed& Seed, double Time, const BallState& SeedState)
	{
		StartIslandImpl(Ws, Seed, Time, &SeedState);
	}

	bool AdvanceIsland(Workspace& Ws, double UntilTime)
	{
		IslandState& I = Ws.Island;
		const NumericsConfig& N = Ws.Params.Numerics;
		// Event-mode balls that may join, and the members' observer / exit guards (ExitClearance: the tests are skipped while a
		// member stays flat on the cloth within the clearance of the plan position of its last test). Both are valid for this call:
		// every join, exit, release and event returns to the loop (WP-6a), which calls again.
		std::uint32_t Candidates = 0;
		for (int Ball = 0; Ball < kMaxBalls; ++Ball)
		{
			Candidates |= CanJoin(Ws, Ball) ? 1u << Ball : 0u;
		}
		Vec2 GuardFrom[kMaxBalls];
		double GuardSquared[kMaxBalls] = {};
		double ReachAtOne[kMaxBalls];
		for (double& R : ReachAtOne)
		{
			R = -1.0;
		}
		const double MaxMassAtStart = Bounds(Ws).MaxMass;
		EventPositions Positions;
		while (I.Active)
		{
			const double Dt = StepSize(Ws);
			const double T0 = I.Solver.Time();
			const double T1 = T0 + Dt;
			if (!(T1 <= UntilTime))
			{
				return true;
			}
			if (!Ws.Queue.IsEmpty() && Ws.Queue.Top().Time <= T1)
			{
				return true; // an event-mode event is due first (new predictions of members that left, or a stale entry)
			}
			if (Ws.IslandStepsUsed >= N.MaxIslandSteps)
			{
				Ws.Result->Diagnostics.IslandBudgetExceeded = true;
				StopInPlace(Ws, T0);
				return false;
			}
			// Feature queries and tip joins (every MinQuerySteps: a divisor of every window length, all powers of two). The tip test's
			// margin covers the members' travel until the next one.
			const int MinSteps = MinQuerySteps(Ws);
			if ((I.Solver.StepCount() & (MinSteps - 1)) == 0)
			{
				const QueryWindow W = QueryWindowOf(Ws);
				if (JoinTips(Ws, T0, N.LeaveDistance + W.Speed * Dt * MinSteps))
				{
					AddFeaturesForReach(Ws, true);
				}
				else if ((I.Solver.StepCount() & (W.Steps - 1)) == 0)
				{
					AddFeaturesForWindow(Ws, W, true);
				}
			}

			PreStep Pre;
			CapturePreStep(Ws, Pre);
			I.StepRecords.Clear();
			I.Solver.Step(I.StepRecords);
			++Ws.IslandStepsUsed;
			++Ws.Result->Diagnostics.IslandSteps;
			const double T = I.Solver.Time();
			Ws.Now = T;

			bool Touched[kMaxBalls] = {};
			EmitRecords(Ws, Pre, T0, Touched);
			// Members due for the observer / exit tests: off the cloth before or after the step, or beyond their clearance.
			std::uint32_t Test = 0;
			for (int k = 0; k < I.Solver.BodyCount(); ++k)
			{
				const IslandBody& Body = I.Solver.Body(k);
				const int Ball = Body.Ball;
				const bool Flat = k < Pre.Count && Pre.Ball[k] == Ball && Pre.Position[k].z - Body.Radius <= N.EpsZ &&
					Body.Position.z - Body.Radius <= N.EpsZ;
				if (!Flat || !(LengthSquared(XY(Body.Position) - GuardFrom[Ball]) < GuardSquared[Ball]))
				{
					Test |= 1u << Ball;
				}
			}
			if (Test != 0)
			{
				MemberObservers(Ws, Pre, T0, T, Test, Positions);
			}
			for (int k = 0; k < I.Solver.BodyCount(); ++k)
			{
				const IslandBody& Body = I.Solver.Body(k);
				UpdateSampledTrack(Ws, Body.Ball, Body.Position, Body.Velocity, Body.Omega, Dt, T, Touched[Body.Ball]);
			}

			bool Changed = Test != 0 && MemberExits(Ws, T, Test);
			if (!Changed && Test != 0)
			{
				for (int k = 0; k < I.Solver.BodyCount(); ++k)
				{
					const IslandBody& Body = I.Solver.Body(k);
					if (((Test >> Body.Ball) & 1u) != 0)
					{
						const double Clear = ExitClearance(Ws, Body);
						GuardFrom[Body.Ball] = XY(Body.Position);
						GuardSquared[Body.Ball] = Clear * Clear;
					}
				}
			}
			Changed = JoinBalls(Ws, T, Candidates, ReachAtOne, MaxMassAtStart) || Changed;
			const int ReleaseSteps = I.Solver.Mode() == CliMode::Rigid ? kReleaseCheckStepsRigid : kReleaseCheckStepsCompliant;
			if ((I.Solver.StepCount() & (ReleaseSteps - 1)) == 0)
			{
				Changed = ReleaseFreeMembers(Ws, T) || Changed;
				if (!I.Active)
				{
					return true; // every member could go: the island ended
				}
			}
			if (I.Solver.Mode() == CliMode::Compliant && (I.Solver.SustainedContact() || T - I.StartTime > N.CompliantMaxDuration))
			{
				SwitchToRigid(Ws, T);
			}
			// Exit (architecture 8.8): CanExit (no force for ExitZeroForceSteps, separating, no overlap, no instant pressing) and no
			// pair that event mode would hand straight back (NothingReturns, the predictive re-entry guard); or a rigid island at rest.
			// A force-free, separating island kept only by such slowly returning pairs is past its Hertz transient: it continues in
			// Rigid mode (the sustained-contact mode) instead of 1 us compliant steps (DECISION, a third switch reason besides
			// SustainedContact and CompliantMaxDuration).
			const bool Quiet = I.Solver.BodyCount() == 0 || I.Solver.CanExit();
			const bool Returns = Quiet && I.Solver.BodyCount() > 0 && !NothingReturns(Ws);
			if ((Quiet && !Returns) || (I.Solver.Mode() == CliMode::Rigid && AllAtRest(Ws)))
			{
				EndIsland(Ws, T);
				return true;
			}
			if (Returns && I.Solver.Mode() == CliMode::Compliant)
			{
				SwitchToRigid(Ws, T);
			}
			if (Changed)
			{
				return true;
			}
		}
		return true;
	}

	double NextIslandStepTime(const Workspace& Ws)
	{
		return Ws.Island.Active ? Ws.Island.Solver.Time() + StepSize(Ws) : kInfinity;
	}
}
