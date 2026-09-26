#include "rb/Core/FpGuard.h"
// Owner: WP-1 (motion, slate & cue strike). Spec: physics-motion-and-cue Part A, C.1.
#include "rb/Physics/Motion.h"

namespace rb
{
	MotionState ClassifyState(BallState& S, double /*Radius*/, double /*SupportZ*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-1): A.8 classifier relative to the support plane, with exact snaps (implementation note 2, prior-art 5.8).
		return S.State;
	}

	MotionSegment MakeSegment(const BallState& S, double T0, const BallSpec& Spec, const ClothParams& /*Surface*/, double SupportZ, double /*Gravity*/)
	{
		// TODO(WP-1): per-state coefficients (A.3-A.7, C.1) with k = InertiaFactor(Spec) and TauEnd (A.8 table);
		// PocketFall: TauEnd = +inf.
		MotionSegment Seg;
		Seg.State = S.State;
		Seg.T0 = T0;
		Seg.Radius = Spec.Radius;
		Seg.SupportZ = SupportZ;
		Seg.Pos0 = S.Position;
		Seg.Vel0 = S.Velocity;
		Seg.Omega0 = S.Omega;
		return Seg;
	}

	BallState EvaluateSegment(const MotionSegment& Seg, double /*Tau*/)
	{
		// TODO(WP-1): closed-form evaluation in local time with the w_z clamp (A.7).
		BallState S;
		S.Position = Seg.Pos0;
		S.Velocity = Seg.Vel0;
		S.Omega = Seg.Omega0;
		S.State = Seg.State;
		return S;
	}

	BallState SegmentEndState(const MotionSegment& Seg, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-1): exact snapped end state + next classification (A.5 end, A.6, A.4; prior-art 5.8); tilt chain pieces:
		// exact node state (EndsInRefresh) or the human-factors 4.5.3 end snaps.
		BallState S;
		S.Position = Seg.Pos0;
		S.State = Seg.State;
		return S;
	}

	double PursuitStopTime(const Vec2& /*X0*/, const Vec2& /*G*/, double /*K*/)
	{
		// TODO(WP-1): human-factors 4.5.2 T_stop (collinear quadratics included).
		return 0.0;
	}

	PursuitState EvaluatePursuit(const Vec2& X0, const Vec2& /*G*/, double /*K*/, double /*Tau*/)
	{
		// TODO(WP-1): human-factors 4.5.2 closed form (E_n = -Expm1(-n lam)) + safeguarded Newton on t(lam) = Tau.
		PursuitState State;
		State.X = X0;
		return State;
	}

	double TiltPieceDuration(double /*SpeedX*/, double /*K*/, double /*GNorm*/, double /*Cs*/, double /*SinBound*/, double /*Tolerance*/, double /*MaxInterval*/,
		double Remaining)
	{
		// TODO(WP-1): human-factors 4.5.3 refresh rule (collinear / tail -> Remaining, else the cubic root, Newton).
		return Remaining;
	}

	MotionSegment MakeSegment(const BallState& S, double T0, const BallSpec& Spec, const ClothParams& Surface, double SupportZ, double Gravity,
		const TiltParams& Tilt)
	{
		// TODO(WP-1): for !IsLevel(Tilt): human-factors 4.5.3 MakeTiltSegment (chain piece with exact node, Airborne in-plane
		// g_t / 2, nap). A level Tilt must keep returning the level segment bitwise (A-MOT-3).
		static_cast<void>(Tilt);
		return MakeSegment(S, T0, Spec, Surface, SupportZ, Gravity);
	}

	BallState EvaluateSegmentForEvent(const MotionSegment& Seg, double Tau)
	{
		// TODO(WP-1): position from the segment, velocity / spin from EvaluatePursuit for tilt chain pieces (human-factors 4.5.3).
		return EvaluateSegment(Seg, Tau);
	}
}
