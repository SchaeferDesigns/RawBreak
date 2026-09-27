#include "rb/Core/FpGuard.h"
// Owner: WP-7 (output, playback & tools). Spec: ue5-realism-plan 5.7; prior-art 7.4 (P6); Docs/architecture.md 8.10.
#include "rb/Physics/Playback.h"

#include "rb/Math/Scalar.h"
#include "rb/Physics/CueStrike.h"

#include <vector>

namespace rb
{
	namespace
	{
		constexpr int kMaxGridIndex = 1 << 30;

		double EffectiveSubstep(double Substep)
		{
			return (Substep > 0.0 && Substep < kInfinity) ? Substep : kOrientationSubstep;
		}

		// Local time of absolute time T in the segment, T clamped to [T0, T1].
		double LocalTau(const TrajectorySegment& S, double T)
		{
			const double T0 = S.Motion.T0;
			const double Tc = T < T0 ? T0 : (S.T1 < T ? S.T1 : T);
			return Tc - T0;
		}

		// Local time clamped to where the segment's laws are valid: EvaluateSegment's [0, TauEnd] (Analytic), the recorded
		// interval (Sampled); a Terminal segment does not move.
		double ClampLocalTau(const TrajectorySegment& S, double Tau)
		{
			if (!(Tau > 0.0))
			{
				return 0.0;
			}
			switch (S.Kind)
			{
			case SegmentKind::Analytic:
			{
				const double End = S.Motion.TauEnd > 0.0 ? S.Motion.TauEnd : 0.0;
				return Tau < End ? Tau : End;
			}
			case SegmentKind::Sampled:
			{
				const double Duration = S.T1 - S.Motion.T0;
				if (!(Duration > 0.0))
				{
					return 0.0;
				}
				return Tau < Duration ? Tau : Duration;
			}
			case SegmentKind::Terminal: return 0.0;
			}
			return 0.0;
		}

		// Exact integral of w_z = OmegaZAt(M, tau) over [A, B] (linear decay clamped at OmegaZStopTau, A.7).
		double SpinIntegral(const MotionSegment& M, double A, double B)
		{
			const double Stop = M.OmegaZStopTau;
			const double Lo = A < Stop ? A : Stop;
			const double Hi = B < Stop ? B : Stop;
			if (!(Hi > Lo))
			{
				return 0.0;
			}
			return (Hi - Lo) * (M.Omega0.z + M.OmegaZRate * (0.5 * (Lo + Hi)));
		}

		// Exact integral of the segment's angular velocity law over [A, B] (0 <= A <= B inside the clamped range) [rad].
		Vec3 RotationIntegral(const TrajectorySegment& S, double A, double B)
		{
			if (!(B > A))
			{
				return Vec3::Zero();
			}
			const double D = B - A;
			switch (S.Kind)
			{
			case SegmentKind::Terminal: return Vec3::Zero();
			case SegmentKind::Sampled: return S.Motion.Omega0 * D;
			case SegmentKind::Analytic: break;
			}

			const MotionSegment& M = S.Motion;
			switch (M.State)
			{
			case MotionState::Sliding:
			{
				const double Mid = 0.5 * (A + B);
				return {D * (M.Omega0.x + M.OmegaDotH.x * Mid), D * (M.Omega0.y + M.OmegaDotH.y * Mid), SpinIntegral(M, A, B)};
			}
			case MotionState::Rolling:
			{
				// w_h = z_hat x v / R  ->  integral = z_hat x (r(B) - r(A)) / R, r(B) - r(A) = D (Vel0 + Accel2 (A + B)).
				if (!(M.Radius > 0.0))
				{
					return {0.0, 0.0, SpinIntegral(M, A, B)};
				}
				const double Sum = A + B;
				const double Dx = D * (M.Vel0.x + M.Accel2.x * Sum);
				const double Dy = D * (M.Vel0.y + M.Accel2.y * Sum);
				return {-Dy / M.Radius, Dx / M.Radius, SpinIntegral(M, A, B)};
			}
			case MotionState::Spinning: return {0.0, 0.0, SpinIntegral(M, A, B)};
			case MotionState::Airborne:
			case MotionState::PocketPivot:
			case MotionState::PocketFall: return M.Omega0 * D;
			case MotionState::Stationary:
			case MotionState::Pocketed:
			case MotionState::OffTable: return Vec3::Zero();
			}
			return Vec3::Zero();
		}

		// Exp(0.5 Theta) as a unit quaternion. Rotations below kSeriesAngle (every grid step up to 250 rad/s) use the
		// Taylor series of cos(a/2) and sin(a/2)/a in a^2 (no Sqrt / Sin / Cos; the omitted terms are below 1e-18, i.e.
		// the result equals the transcendental form to rounding); larger ones FromRotationVector (rb/Math/Quat.h).
		constexpr double kSeriesAngle = 0.25; // [rad]

		Quat ExpHalf(const Vec3& Theta)
		{
			const double A2 = LengthSquared(Theta);
			if (!(A2 < kSeriesAngle * kSeriesAngle))
			{
				return FromRotationVector(Theta);
			}
			const double X2 = 0.25 * A2; // (a/2)^2
			const double C = 1.0 - X2 * (1.0 / 2.0 - X2 * (1.0 / 24.0 - X2 * (1.0 / 720.0 - X2 * (1.0 / 40320.0 - X2 * (1.0 / 3628800.0)))));
			const double S = 0.5 * (1.0 - X2 * (1.0 / 6.0 - X2 * (1.0 / 120.0 - X2 * (1.0 / 5040.0 - X2 * (1.0 / 362880.0 - X2 * (1.0 / 39916800.0))))));
			return {C, Theta.x * S, Theta.y * S, Theta.z * S};
		}

		// q' = Exp(0.5 Theta) (x) q, renormalised; an exactly zero rotation vector leaves q unchanged bitwise.
		Quat ApplyRotation(const Quat& Q, const Vec3& Theta)
		{
			if (Theta.x == 0.0 && Theta.y == 0.0 && Theta.z == 0.0)
			{
				return Q;
			}
			return Normalized(ExpHalf(Theta) * Q);
		}

		// K = floor(Tau / Substep), lowered by one if rounding would put tau_K after Tau.
		int GridIndexOf(double Tau, double Substep)
		{
			const double Ratio = Tau / Substep;
			if (!(Ratio >= 1.0))
			{
				return 0;
			}
			if (!(Ratio < static_cast<double>(kMaxGridIndex)))
			{
				return kMaxGridIndex;
			}
			int K = static_cast<int>(Ratio);
			if (K > 0 && static_cast<double>(K) * Substep > Tau)
			{
				--K;
			}
			return K;
		}

		// Last grid point the law steps to: a level Rolling segment whose spin stops inside it has a constant axis from the
		// first grid point at or after OmegaZStopTau on (closed form from there, Playback.h).
		int GridLimit(const TrajectorySegment& S, double Substep)
		{
			const MotionSegment& M = S.Motion;
			if (S.Kind != SegmentKind::Analytic || M.State != MotionState::Rolling || M.Tilt.Active || !(M.OmegaZStopTau >= 0.0)
				|| !(M.OmegaZStopTau < kInfinity))
			{
				return kMaxGridIndex;
			}
			const double Stop = M.OmegaZStopTau;
			const double Ratio = Stop / Substep;
			if (!(Ratio < static_cast<double>(kMaxGridIndex - 2)))
			{
				return kMaxGridIndex;
			}
			int K = static_cast<int>(Ratio);
			while (static_cast<double>(K) * Substep < Stop)
			{
				++K;
			}
			while (K > 0 && static_cast<double>(K - 1) * Substep >= Stop)
			{
				--K;
			}
			return K;
		}

		// One grid step q_k -> q_{k+1}.
		Quat GridStep(const Quat& Qk, const TrajectorySegment& S, int K, double Substep)
		{
			const double A = static_cast<double>(K) * Substep;
			const double B = static_cast<double>(K + 1) * Substep;
			return ApplyRotation(Qk, RotationIntegral(S, A, B));
		}

		// Final partial step from grid point K to TauC.
		Quat PartialStep(const Quat& QK, const TrajectorySegment& S, int K, double TauC, double Substep)
		{
			const double A = static_cast<double>(K) * Substep;
			return TauC > A ? ApplyRotation(QK, RotationIntegral(S, A, TauC)) : QK;
		}

		int TargetGridIndex(const TrajectorySegment& S, double TauC, double Substep)
		{
			const int K = GridIndexOf(TauC, Substep);
			const int Limit = GridLimit(S, Substep);
			return K < Limit ? K : Limit;
		}

		// Index of the last segment with Motion.T0 <= T (0 if none): the segment that shows time T.
		int FindSegment(const std::vector<TrajectorySegment>& Segments, double T)
		{
			int Lo = 0;
			int Hi = static_cast<int>(Segments.size()); // predicate T0 <= T is true on [0, Lo) and false on [Hi, n)
			while (Lo < Hi)
			{
				const int Mid = Lo + (Hi - Lo) / 2;
				if (Segments[static_cast<std::size_t>(Mid)].Motion.T0 <= T)
				{
					Lo = Mid + 1;
				}
				else
				{
					Hi = Mid;
				}
			}
			return Lo > 0 ? Lo - 1 : 0;
		}

		const std::vector<TrajectorySegment>* TrackOf(const ShotResult& Result, int Ball)
		{
			if (Ball < 0 || Ball >= kMaxBalls)
			{
				return nullptr;
			}
			const std::vector<TrajectorySegment>& Segments = Result.Tracks[Ball].Segments;
			return Segments.empty() ? nullptr : &Segments;
		}
	}

	BallState EvaluateTrajectorySegment(const TrajectorySegment& Segment, double T)
	{
		const MotionSegment& M = Segment.Motion;
		switch (Segment.Kind)
		{
		case SegmentKind::Analytic:
			// A segment at rest (Stationary, Pocketed, OffTable: zero velocity and acceleration) is evaluated at tau = 0:
			// bitwise the same state for every finite tau, and still finite for T = +inf on the open last segment (T1 = +inf),
			// where Pos0 + 0 * tau + 0 * tau^2 would be NaN.
			return EvaluateSegment(M, IsMoving(M.State) ? LocalTau(Segment, T) : 0.0);
		case SegmentKind::Sampled:
		{
			BallState S;
			S.State = M.State;
			S.Omega = M.Omega0;
			const double Duration = Segment.T1 - M.T0;
			const bool Finite = Duration > 0.0 && Duration < kInfinity;
			const Vec3 Chord = Segment.EndPosition - M.Pos0;
			S.Velocity = Finite ? Chord / Duration : Vec3::Zero();
			if (!Finite || !(T > M.T0))
			{
				S.Position = M.Pos0;
			}
			else if (!(T < Segment.T1))
			{
				S.Position = Segment.EndPosition;
			}
			else
			{
				S.Position = M.Pos0 + Chord * ((T - M.T0) / Duration);
			}
			return S;
		}
		case SegmentKind::Terminal:
		{
			BallState S;
			S.State = M.State;
			S.Position = M.Pos0;
			return S;
		}
		}
		return BallState{};
	}

	bool HasConstantRotationAxis(const TrajectorySegment& Segment)
	{
		if (Segment.Kind != SegmentKind::Analytic)
		{
			return true; // Terminal: no rotation; Sampled: w = Motion.Omega0
		}
		const MotionSegment& M = Segment.Motion;
		switch (M.State)
		{
		case MotionState::Sliding: return false;
		case MotionState::Rolling:
			// Structural test (never a floating-point parallelism test): level piece, no spin on the whole segment.
			return !M.Tilt.Active && M.Omega0.z == 0.0 && (M.OmegaZRate == 0.0 || !(M.OmegaZStopTau > 0.0));
		case MotionState::Stationary:
		case MotionState::Spinning:
		case MotionState::Airborne:
		case MotionState::PocketPivot:
		case MotionState::PocketFall:
		case MotionState::Pocketed:
		case MotionState::OffTable: return true;
		}
		return true;
	}

	Quat SegmentOrientationAt(const Quat& Q0, const TrajectorySegment& Segment, double Tau, double Substep)
	{
		const double TauC = ClampLocalTau(Segment, Tau);
		if (HasConstantRotationAxis(Segment))
		{
			return ApplyRotation(Q0, RotationIntegral(Segment, 0.0, TauC));
		}
		const double Sub = EffectiveSubstep(Substep);
		const int K = TargetGridIndex(Segment, TauC, Sub);
		Quat Q = Q0;
		for (int k = 0; k < K; ++k)
		{
			Q = GridStep(Q, Segment, k, Sub);
		}
		return PartialStep(Q, Segment, K, TauC, Sub);
	}

	Quat IntegrateOrientationSteps(const Quat& Q0, const Vec3& Omega, double Dt, int Steps)
	{
		Quat Q = Q0;
		for (int i = 0; i < Steps; ++i)
		{
			Q = IntegrateConstantOmega(Q, Omega, Dt);
		}
		return Q;
	}

	bool StateAt(const ShotResult& Result, int Ball, double T, BallState& Out)
	{
		const std::vector<TrajectorySegment>* Segments = TrackOf(Result, Ball);
		if (Segments == nullptr)
		{
			return false;
		}
		Out = EvaluateTrajectorySegment((*Segments)[static_cast<std::size_t>(FindSegment(*Segments, T))], T);
		return true;
	}

	bool OrientationAt(const ShotResult& Result, int Ball, double T, Quat& Out)
	{
		const std::vector<TrajectorySegment>* Segments = TrackOf(Result, Ball);
		if (Segments == nullptr)
		{
			return false;
		}
		const TrajectorySegment& S = (*Segments)[static_cast<std::size_t>(FindSegment(*Segments, T))];
		Out = SegmentOrientationAt(S.Orientation0, S, LocalTau(S, T), kOrientationSubstep);
		return true;
	}

	void ResetCursor(PlaybackCursor& Cursor)
	{
		Cursor = PlaybackCursor{};
	}

	bool StateAtCursor(const ShotResult& Result, PlaybackCursor& Cursor, int Ball, double T, BallState& OutState, Quat& OutOrientation)
	{
		const std::vector<TrajectorySegment>* Track = TrackOf(Result, Ball);
		if (Track == nullptr)
		{
			return false;
		}
		const std::vector<TrajectorySegment>& Segments = *Track;
		const int Count = static_cast<int>(Segments.size());

		int K = Cursor.Segment[Ball];
		bool Reseed = false;
		// Not "T < LastTime": a NaN frame time (or a NaN LastTime left by one) is no forward step either, and would otherwise
		// keep the cached segment for every later (backward) time.
		if (!Cursor.Valid[Ball] || K < 0 || K >= Count || !(T >= Cursor.LastTime[Ball]))
		{
			K = FindSegment(Segments, T); // first use, a backward jump or a non-finite time: random access
			Reseed = true;
		}
		else
		{
			const int Previous = K;
			while (K + 1 < Count && Segments[static_cast<std::size_t>(K + 1)].Motion.T0 <= T)
			{
				++K;
			}
			Reseed = K != Previous;
		}

		const TrajectorySegment& S = Segments[static_cast<std::size_t>(K)];
		if (Reseed)
		{
			Cursor.GridIndex[Ball] = 0;
			Cursor.GridOrientation[Ball] = S.Orientation0;
		}

		OutState = EvaluateTrajectorySegment(S, T);
		const double TauC = ClampLocalTau(S, LocalTau(S, T));
		if (HasConstantRotationAxis(S))
		{
			OutOrientation = ApplyRotation(S.Orientation0, RotationIntegral(S, 0.0, TauC));
		}
		else
		{
			// The same grid steps as SegmentOrientationAt, continued from the cached grid point (bitwise equal).
			const double Sub = kOrientationSubstep;
			const int Target = TargetGridIndex(S, TauC, Sub);
			if (Target < Cursor.GridIndex[Ball])
			{
				Cursor.GridIndex[Ball] = 0;
				Cursor.GridOrientation[Ball] = S.Orientation0;
			}
			while (Cursor.GridIndex[Ball] < Target)
			{
				Cursor.GridOrientation[Ball] = GridStep(Cursor.GridOrientation[Ball], S, Cursor.GridIndex[Ball], Sub);
				++Cursor.GridIndex[Ball];
			}
			OutOrientation = PartialStep(Cursor.GridOrientation[Ball], S, Target, TauC, Sub);
		}

		Cursor.Segment[Ball] = K;
		Cursor.LastTime[Ball] = T;
		Cursor.Valid[Ball] = true;
		return true;
	}

	bool CueTipAt(const ShotResult& Result, int Strike, double T, Vec3& TipCenter, Vec3& Direction)
	{
		const CueTipSegment* First = nullptr;
		const CueTipSegment* Piece = nullptr;
		for (const CueTipSegment& C : Result.CueTips)
		{
			if (C.Strike != Strike)
			{
				continue;
			}
			if (First == nullptr)
			{
				First = &C;
			}
			if (C.Path.StartTime <= T)
			{
				Piece = &C; // pieces of one strike are time ordered: the last one that has started
			}
		}
		if (First == nullptr)
		{
			return false;
		}
		if (Piece == nullptr)
		{
			Piece = First;
		}

		const CueTipPath& Path = Piece->Path;
		Direction = Path.Direction;
		if (Piece->Kind == SegmentKind::Sampled)
		{
			const double Duration = Piece->T1 - Path.StartTime;
			if (!(Duration > 0.0) || !(T > Path.StartTime))
			{
				TipCenter = Path.Start;
			}
			else if (!(T < Piece->T1))
			{
				TipCenter = Piece->EndPosition;
			}
			else
			{
				TipCenter = Path.Start + (Piece->EndPosition - Path.Start) * ((T - Path.StartTime) / Duration);
			}
			return true;
		}

		// Analytic: the tip dome center as detection sees it (CueTipAsSegment), evaluated up to the piece's end.
		const MotionSegment Seg = CueTipAsSegment(Path);
		const double Tc = Piece->T1 < T ? Piece->T1 : T;
		double Tau = Tc - Path.StartTime;
		Tau = Tau > 0.0 ? (Tau < Seg.TauEnd ? Tau : Seg.TauEnd) : 0.0;
		TipCenter = PositionAt(Seg, Tau);
		return true;
	}

	int SampleTrajectory(const ShotResult& Result, int Ball, double Dt, TrajectorySample* Out, int Capacity)
	{
		const std::vector<TrajectorySegment>* Track = TrackOf(Result, Ball);
		if (Track == nullptr)
		{
			return 0;
		}
		const std::vector<TrajectorySegment>& Segments = *Track;
		const int SegmentCount = static_cast<int>(Segments.size());
		const double Stop = Result.StopTime > 0.0 && Result.StopTime < kInfinity ? Result.StopTime : 0.0;
		const bool Regular = Dt > 0.0 && Dt < kInfinity;

		PlaybackCursor Cursor;
		int Written = 0;
		double LastTime = 0.0;
		const auto Emit = [&](double T) -> bool {
			if (Written > 0 && !(T > LastTime))
			{
				return true; // duplicate time
			}
			if (Out == nullptr || Written >= Capacity)
			{
				return false;
			}
			TrajectorySample& S = Out[Written];
			BallState State;
			Quat Q;
			StateAtCursor(Result, Cursor, Ball, T, State, Q);
			S.Time = T;
			S.Position = State.Position;
			S.Velocity = State.Velocity;
			S.Omega = State.Omega;
			S.Orientation = Q;
			S.State = State.State;
			++Written;
			LastTime = T;
			return true;
		};

		if (!Emit(0.0))
		{
			return -1;
		}
		long long K = 1;
		int J = 1; // next segment boundary (Segments[J].Motion.T0)
		for (;;)
		{
			double TRegular = kInfinity;
			if (Regular)
			{
				const double T = static_cast<double>(K) * Dt;
				TRegular = T <= Stop ? T : kInfinity;
			}
			while (J < SegmentCount && !(Segments[static_cast<std::size_t>(J)].Motion.T0 > 0.0))
			{
				++J;
			}
			double TBoundary = kInfinity;
			if (J < SegmentCount && Segments[static_cast<std::size_t>(J)].Motion.T0 <= Stop)
			{
				TBoundary = Segments[static_cast<std::size_t>(J)].Motion.T0;
			}
			const double T = TRegular < TBoundary ? TRegular : TBoundary;
			if (!(T < kInfinity))
			{
				break;
			}
			if (!Emit(T))
			{
				return -1;
			}
			if (T == TRegular)
			{
				++K;
			}
			if (T == TBoundary)
			{
				++J;
			}
		}
		if (!Emit(Stop))
		{
			return -1;
		}
		return Written;
	}
}
