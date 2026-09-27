#include "rb/Core/FpGuard.h"
// Owner: WP-6b (islands, pockets & rail-top routing). Spec: Docs/architecture.md 8.7 (observers of balls leaving event mode),
// 8.8 (adaptive Sampled recording of island members, review item 35; re-entry guard), 8.10 (orientation law continuity).
// This file: event helpers, observer flushing, Sampled track pieces of islands and pivots, Zeno history clearing.
#include "SimIslandInternal.h"

#include "rb/Math/Scalar.h"
#include "rb/Physics/Playback.h"

namespace rb::sim
{
	namespace
	{
		bool Recording(const Workspace& Ws) { return Ws.Input != nullptr && Ws.Input->Record.Trajectories && Ws.Result != nullptr; }

		// The orientation law is maintained for recorded tracks and, whatever the RecordOptions, for balls with chalk marks when
		// PhysicsParams::ChalkCling (architecture 8.11).
		bool NeedsOrientation(const Workspace& Ws, int Ball)
		{
			return Recording(Ws) || (Ws.Params.ChalkCling && !Ws.Input->Balls[Ball].ChalkMarks.IsEmpty());
		}

		// Informative motion state of a Sampled piece (playback only): on a support or in the air.
		MotionState SampleStateOf(const Vec3& Position, const Vec3& Velocity, const Vec3& Omega, double Radius, const NumericsConfig& Numerics)
		{
			if (Position.z - Radius > Numerics.EpsZ || Abs(Velocity.z) > Numerics.EpsV)
			{
				return MotionState::Airborne;
			}
			const Vec3 U = SlipVelocity(Velocity, Omega, Radius);
			if (U.x * U.x + U.y * U.y > Numerics.EpsV * Numerics.EpsV)
			{
				return MotionState::Sliding;
			}
			return Velocity.x * Velocity.x + Velocity.y * Velocity.y > Numerics.EpsV * Numerics.EpsV ? MotionState::Rolling : MotionState::Stationary;
		}

		// Appends a Sampled segment (open: T1 = kInfinity) if the reserved capacity allows it (never grows the vector).
		void PushSampled(Workspace& Ws, int Ball, double T0, const Vec3& Position, const Vec3& Velocity, const Vec3& Omega, MotionState State,
			const Quat& Orientation)
		{
			std::vector<TrajectorySegment>& Segments = Ws.Result->Tracks[Ball].Segments;
			if (Segments.size() >= Segments.capacity())
			{
				Ws.Result->Diagnostics.TrajectoryOverflow = true;
				return;
			}
			TrajectorySegment Seg;
			Seg.Motion.State = State;
			Seg.Motion.T0 = T0;
			Seg.Motion.TauEnd = kInfinity;
			Seg.Motion.Radius = Ws.Input->Balls[Ball].Spec.Radius;
			Seg.Motion.Pos0 = Position;
			Seg.Motion.Vel0 = Velocity;
			Seg.Motion.Omega0 = Omega;
			Seg.T1 = kInfinity;
			Seg.Kind = SegmentKind::Sampled;
			Seg.EndPosition = Position;
			Seg.Orientation0 = Orientation;
			Segments.push_back(Seg);
		}

		// The open Sampled piece of the ball (nullptr if not recording or the track does not end in one).
		TrajectorySegment* OpenSampled(Workspace& Ws, int Ball)
		{
			if (!Recording(Ws))
			{
				return nullptr;
			}
			std::vector<TrajectorySegment>& Segments = Ws.Result->Tracks[Ball].Segments;
			if (Segments.empty())
			{
				return nullptr;
			}
			TrajectorySegment& Back = Segments.back();
			return Back.Kind == SegmentKind::Sampled && Back.T1 == kInfinity && Back.Motion.T0 == Ws.Balls[Ball].SampleStart ? &Back : nullptr;
		}

		// Orientation at the end of a Sampled piece [T0, T0 + Duration] with mean spin Omega, from Q0 (the orientation law).
		Quat SampledOrientationAt(const Quat& Q0, const Vec3& Position, const Vec3& Omega, double T0, double Duration)
		{
			TrajectorySegment Piece;
			Piece.Motion.State = MotionState::Airborne;
			Piece.Motion.T0 = T0;
			Piece.Motion.Pos0 = Position;
			Piece.Motion.Omega0 = Omega;
			Piece.T1 = T0 + Duration;
			Piece.Kind = SegmentKind::Sampled;
			Piece.Orientation0 = Q0;
			return SegmentOrientationAt(Q0, Piece, Duration);
		}

		void ApplyObserver(Workspace& Ws, int Ball, const Observer& O)
		{
			BallSlot& B = Ws.Balls[Ball];
			switch (O.Kind)
			{
			case ObserverKind::LineCross:
			{
				ShotEvent E = MakeBallEvent(ShotEventType::BallLineCross, O.Time, Ball, BallStateAt(Ws, Ball, O.Time));
				E.Feature = O.Index;
				E.SubFeature = O.Direction > 0 ? 0 : 1;
				EmitEvent(Ws, E);
				break;
			}
			case ObserverKind::FreezeLeave:
				if (O.Index < 32)
				{
					B.InitialFreezeRails &= ~(1u << O.Index);
				}
				else if (O.Index - 32 < kMaxBalls)
				{
					B.InitialFreezeBalls &= ~(1u << (O.Index - 32));
				}
				break;
			case ObserverKind::JumpEnter:
				if (O.Index < kMaxBalls)
				{
					B.JumpPending |= 1u << O.Index;
					B.ContactSinceJump &= ~(1u << O.Index);
				}
				break;
			case ObserverKind::JumpLeave:
				if (O.Index < kMaxBalls && (B.JumpPending & (1u << O.Index)) != 0)
				{
					if ((B.ContactSinceJump & (1u << O.Index)) == 0)
					{
						ShotEvent E = MakeBallEvent(ShotEventType::BallJumpedOver, O.Time, Ball, BallStateAt(Ws, Ball, O.Time));
						E.B = static_cast<BallId>(O.Index);
						EmitEvent(Ws, E);
					}
					B.JumpPending &= ~(1u << O.Index);
				}
				break;
			}
		}

		struct PivotPiece
		{
			double From = 0.0; // local pivot time [s]
			double To = 0.0;
		};
	}

	ShotEvent MakeBallEvent(ShotEventType Type, double Time, int Ball, const BallState& State)
	{
		ShotEvent E;
		E.Time = Time;
		E.Type = Type;
		E.A = static_cast<BallId>(Ball);
		E.Pre[0] = State;
		E.Post[0] = State;
		return E;
	}

	void NoteRailContact(Workspace& Ws, int Ball)
	{
		for (int s = 0; s < Ws.Input->Strikes.Size() && s < kMaxStrikes; ++s)
		{
			if (Ws.Tips[s].Path.StruckBall == Ball)
			{
				Ws.Tips[s].StruckTouchedOther = true;
			}
		}
	}

	void FlushObservers(Workspace& Ws, int Ball, double Before)
	{
		BallSlot& B = Ws.Balls[Ball];
		for (int k = 0; k < B.Observers.Size(); ++k)
		{
			const Observer O = B.Observers[k];
			if (!(O.Time < Before))
			{
				break; // time ordered
			}
			ApplyObserver(Ws, Ball, O);
		}
		B.Observers.Clear();
	}

	void BeginSampledTrack(Workspace& Ws, int Ball, const BallState& State, double T)
	{
		BallSlot& B = Ws.Balls[Ball];
		const bool Record = Recording(Ws);
		TrajectorySegment Previous;
		bool HavePrevious = false;
		if (Record)
		{
			std::vector<TrajectorySegment>& Segments = Ws.Result->Tracks[Ball].Segments;
			if (!Segments.empty())
			{
				TrajectorySegment& Back = Segments.back();
				if (Back.T1 > T)
				{
					Back.T1 = T; // close the open segment at the island time
				}
				if (Back.Kind == SegmentKind::Sampled)
				{
					Back.EndPosition = State.Position;
				}
				Previous = Back;
				HavePrevious = true;
			}
		}
		if (!HavePrevious)
		{
			Previous.Motion = B.Seg;
			Previous.T1 = T;
			Previous.Kind = SegmentKind::Analytic;
			Previous.Orientation0 = B.Orientation0;
		}
		Quat Q = B.Orientation0;
		if (NeedsOrientation(Ws, Ball))
		{
			Q = SegmentOrientationAt(Previous.Orientation0, Previous, Max(0.0, T - Previous.Motion.T0));
		}
		B.Orientation0 = Q;
		B.SampleStart = T;
		B.SamplePosition = State.Position;
		B.RotationAccumulator = Vec3{};
		if (Record)
		{
			const double R = Ws.Input->Balls[Ball].Spec.Radius;
			PushSampled(Ws, Ball, T, State.Position, State.Velocity, State.Omega, SampleStateOf(State.Position, State.Velocity, State.Omega, R, Ws.Params.Numerics),
				Q);
		}
	}

	void UpdateSampledTrack(Workspace& Ws, int Ball, const Vec3& Position, const Vec3& Velocity, const Vec3& Omega, double Dt, double T, bool Force)
	{
		BallSlot& B = Ws.Balls[Ball];
		B.RotationAccumulator += Omega * Dt;
		const bool Orientation = NeedsOrientation(Ws, Ball);
		if (!Orientation)
		{
			return;
		}
		const double Duration = T - B.SampleStart;
		if (!(Duration > 0.0))
		{
			return;
		}
		const double Inverse = 1.0 / Duration;
		const Vec3 Chord = (Position - B.SamplePosition) * Inverse;
		const Vec3 MeanSpin = B.RotationAccumulator * Inverse;
		TrajectorySegment* Open = OpenSampled(Ws, Ball);
		if (Open != nullptr)
		{
			Open->EndPosition = Position;
			Open->Motion.Vel0 = Chord;
			Open->Motion.Omega0 = MeanSpin;
		}
		// Chord deviation of a path with constant acceleration over the piece: |v(T) - chord| Duration / 4 (exact for that case;
		// contacts cut the piece anyway: Force).
		const NumericsConfig& N = Ws.Params.Numerics;
		// SampleMaxInterval is a hard upper bound: cut when the next step would exceed it.
		const bool Cut = Force || Duration + Dt > N.SampleMaxInterval || Length(Velocity - Chord) * Duration * 0.25 > N.SampleTolerance;
		if (!Cut)
		{
			return;
		}
		const Quat Next = SampledOrientationAt(B.Orientation0, B.SamplePosition, MeanSpin, B.SampleStart, Duration);
		if (Open != nullptr)
		{
			Open->T1 = T;
		}
		B.Orientation0 = Next;
		B.SampleStart = T;
		B.SamplePosition = Position;
		B.RotationAccumulator = Vec3{};
		if (Recording(Ws) && Open != nullptr)
		{
			const double R = Ws.Input->Balls[Ball].Spec.Radius;
			PushSampled(Ws, Ball, T, Position, Velocity, Omega, SampleStateOf(Position, Velocity, Omega, R, N), Next);
		}
	}

	void EndSampledTrack(Workspace& Ws, int Ball, const Vec3& Position, double T)
	{
		BallSlot& B = Ws.Balls[Ball];
		const double Duration = T - B.SampleStart;
		const Vec3 Chord = Duration > 0.0 ? (Position - B.SamplePosition) / Duration : Vec3{};
		const Vec3 MeanSpin = Duration > 0.0 ? B.RotationAccumulator / Duration : Vec3{};
		TrajectorySegment* Open = OpenSampled(Ws, Ball);
		if (Open != nullptr)
		{
			Open->EndPosition = Position;
			Open->Motion.Vel0 = Chord;
			Open->Motion.Omega0 = MeanSpin; // left open (T1 = kInfinity): ReplaceSegment / MakeTerminal close it at T
		}
		// The constant-spin equivalent of the open piece: continuing the orientation law from BallSlot::Seg gives the same
		// orientation at T as from the Sampled piece, and EvaluateSegment(Seg, T - T0) is the exit position.
		MotionSegment Equivalent;
		Equivalent.State = MotionState::Airborne;
		Equivalent.T0 = B.SampleStart;
		Equivalent.TauEnd = kInfinity;
		Equivalent.Radius = Ws.Input->Balls[Ball].Spec.Radius;
		Equivalent.Pos0 = B.SamplePosition;
		Equivalent.Vel0 = Chord;
		Equivalent.Omega0 = MeanSpin;
		B.Seg = Equivalent;
	}

	void SamplePivotTrack(Workspace& Ws, int Ball, double T)
	{
		BallSlot& B = Ws.Balls[Ball];
		const PivotPath& Path = B.Pivot;
		if (!Recording(Ws) || Path.Result.Immediate)
		{
			return;
		}
		std::vector<TrajectorySegment>& Segments = Ws.Result->Tracks[Ball].Segments;
		if (Segments.empty())
		{
			return;
		}
		const TrajectorySegment& Back = Segments.back();
		if (Back.Kind != SegmentKind::Analytic || Back.Motion.State != MotionState::PocketPivot || Back.Motion.T0 != Path.T0 || !(Back.T1 >= T))
		{
			return; // not the proxy segment of this pivot (recorded differently): keep it
		}
		const double Duration = Clamp(T - Path.T0, 0.0, Path.Result.Duration);
		const Quat Q0 = Back.Orientation0;
		Segments.pop_back();

		// Adaptive pieces: split while the true path deviates from the chord at the piece midpoint by more than SampleTolerance.
		constexpr int kMaxPieces = 64;
		PivotPiece Stack[kMaxPieces];
		int StackSize = 0;
		PivotPiece Done[kMaxPieces];
		int DoneCount = 0;
		Stack[StackSize++] = {0.0, Duration};
		const double Tolerance = Ws.Params.Numerics.SampleTolerance;
		while (StackSize > 0)
		{
			const PivotPiece Piece = Stack[--StackSize];
			const double Mid = 0.5 * (Piece.From + Piece.To);
			const Vec3 PA = EvaluatePivot(Path, Piece.From).Position;
			const Vec3 PB = EvaluatePivot(Path, Piece.To).Position;
			const Vec3 PM = EvaluatePivot(Path, Mid).Position;
			const bool Split = Length(PM - (PA + PB) * 0.5) > Tolerance && DoneCount + StackSize + 2 < kMaxPieces && Piece.To - Piece.From > 1e-6;
			if (Split)
			{
				Stack[StackSize++] = {Mid, Piece.To}; // processed after the first half: pieces come out in time order
				Stack[StackSize++] = {Piece.From, Mid};
			}
			else
			{
				Done[DoneCount++] = Piece;
			}
		}
		Quat Q = Q0;
		const double R = Ws.Input->Balls[Ball].Spec.Radius;
		for (int k = 0; k < DoneCount; ++k)
		{
			const BallState SA = EvaluatePivot(Path, Done[k].From);
			const BallState SB = EvaluatePivot(Path, Done[k].To);
			const double Span = Done[k].To - Done[k].From;
			const Vec3 Chord = Span > 0.0 ? (SB.Position - SA.Position) / Span : SA.Velocity;
			const Vec3 MeanSpin = (SA.Omega + SB.Omega) * 0.5; // trapezoid rule of the spin integral
			const bool Last = k + 1 == DoneCount;
			if (Segments.size() >= Segments.capacity())
			{
				Ws.Result->Diagnostics.TrajectoryOverflow = true;
				break;
			}
			TrajectorySegment Seg;
			Seg.Motion.State = MotionState::PocketPivot;
			Seg.Motion.T0 = Path.T0 + Done[k].From;
			Seg.Motion.TauEnd = kInfinity;
			Seg.Motion.Radius = R;
			Seg.Motion.Pos0 = SA.Position;
			Seg.Motion.Vel0 = Chord;
			Seg.Motion.Omega0 = MeanSpin;
			Seg.T1 = Last ? kInfinity : Path.T0 + Done[k].To;
			Seg.Kind = SegmentKind::Sampled;
			Seg.EndPosition = SB.Position;
			Seg.Orientation0 = Q;
			Segments.push_back(Seg);
			B.Orientation0 = Q;
			B.SampleStart = Seg.Motion.T0;
			B.SamplePosition = SA.Position;
			B.RotationAccumulator = MeanSpin * Span;
			if (!Last)
			{
				Q = SegmentOrientationAt(Q, Seg, Span);
			}
		}
		// ReplaceSegment continues from the last (open) piece; the same law from BallSlot::Seg.
		EndSampledTrack(Ws, Ball, EvaluatePivot(Path, Duration).Position, Path.T0 + Duration);
	}

	void ClearZenoHistories(Workspace& Ws, std::uint32_t Balls)
	{
		for (int i = Ws.Zeno.Size() - 1; i >= 0; --i)
		{
			const ZenoEntry& Z = Ws.Zeno[i];
			const bool A = Z.BallA < kMaxBalls && ((Balls >> Z.BallA) & 1u) != 0;
			const bool B = Z.BallB == kNoBallSlot || (Z.BallB < kMaxBalls && ((Balls >> Z.BallB) & 1u) != 0);
			if (A && B)
			{
				Ws.Zeno.RemoveAt(i);
			}
		}
	}
}
