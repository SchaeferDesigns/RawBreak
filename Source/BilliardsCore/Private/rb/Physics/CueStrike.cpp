#include "rb/Core/FpGuard.h"
// Owner: WP-1 (motion, slate & cue strike). Spec: physics-motion-and-cue Part B.
#include "rb/Physics/CueStrike.h"

namespace rb
{
	ErrorCode ValidateCueStrike(const CueStrikeInput& /*Input*/)
	{
		// TODO(WP-1): B.1 preconditions (V in [0, kMaxCueSpeed], theta in [0, pi/2), rho < kCueOffsetValidLimit, finite).
		return ErrorCode::NotImplemented;
	}

	CueFrame MakeCueFrame(double /*Elevation*/, double /*Azimuth*/)
	{
		// TODO(WP-1): B.2 right-handed triad (d, e_r, e_u).
		return {};
	}

	Vec3 CueContactPoint(const CueFrame& /*Frame*/, double /*OffsetA*/, double /*OffsetB*/, double /*Radius*/)
	{
		// TODO(WP-1): B.3 Q = R (a e_r + b e_u - c d).
		return {};
	}

	Vec2 AimToContactOffset(const Vec2& /*AxisOffset*/, double /*Radius*/, double /*TipDomeRadius*/)
	{
		// TODO(WP-1): B.3 (a, b) = (A, B) R / (R + r_tip).
		return {};
	}

	double MiscueLimit(double /*TipFriction*/)
	{
		// TODO(WP-1): B.4 rho_max = mu / sqrt(1 + mu^2).
		return 0.0;
	}

	double SquirtAngle(double /*OffsetA*/, double /*BallToEndMassRatio*/, double /*InertiaK*/)
	{
		// TODO(WP-1): B.7 TP A.31 end-mass model.
		return 0.0;
	}

	double PinchLambda(double /*Elevation*/, const CueSpec& /*Cue*/, const PinchParams& /*Pinch*/)
	{
		// TODO(WP-1): B.8.2 smoothstep schedule (playing vs jump cue).
		return 0.0;
	}

	double SeparationMargin(double /*Rho*/, double /*TipRestitution*/, double /*BallMass*/, double /*CueMass*/, double /*InertiaK*/)
	{
		// TODO(WP-1): B.6 k e_tip (1 + m/M) - rho^2.
		return 0.0;
	}

	StrikeResult StrikeCueBall(const CueStrikeInput& /*Input*/, const BallState& Ball, const BallSpec& /*Spec*/, const ClothParams& /*Cloth*/,
		const SlateParams& /*Slate*/, const PinchParams& /*Pinch*/, double /*Gravity*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-1): B.8.5 algorithm (grip / miscue branch, squirt once, slate reaction at t = 0+, classify).
		StrikeResult Result;
		Result.Error = ErrorCode::NotImplemented;
		Result.State = Ball;
		return Result;
	}

	CueTipPath MakeCueTipPath(const CueStrikeInput& /*Input*/, const StrikeResult& /*Strike*/, const Vec3& /*BallCenter*/, double /*BallRadius*/)
	{
		// TODO(WP-1): follow-through path (architecture decision): dome center, V', uniform deceleration.
		return {};
	}

	MotionSegment CueTipAsSegment(const CueTipPath& /*Path*/)
	{
		// TODO(WP-1): quadratic tip trajectory as a MotionSegment for PredictBallBall.
		return {};
	}

	TipRecontactResult ResolveTipRecontact(const CueTipPath& Tip, double /*Time*/, const BallState& Ball, const BallSpec& /*Spec*/, const CueSpec& /*Cue*/,
		const ClothParams& /*Surface*/, const SlateParams& /*Slate*/, double /*Gravity*/, const NumericsConfig& /*Numerics*/)
	{
		// TODO(WP-1): tip-ball re-contact impulse on any ball (B.5 with relative velocity), new tip speed.
		TipRecontactResult Result;
		Result.Ball = Ball;
		Result.Tip = Tip;
		return Result;
	}
}
