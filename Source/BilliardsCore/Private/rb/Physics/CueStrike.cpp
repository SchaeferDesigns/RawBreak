#include "rb/Core/FpGuard.h"
// Owner: WP-1 (motion, slate & cue strike). Spec: physics-motion-and-cue Part B; architecture 8.6 (follow-through).
#include "rb/Physics/CueStrike.h"

namespace rb
{
	namespace
	{
		// Q / R for contact-point offsets (a, b): a e_r + b e_u - c d, c = sqrt(1 - a^2 - b^2) (B.3).
		Vec3 ContactDirection(const CueFrame& Frame, double OffsetA, double OffsetB)
		{
			const double C2 = 1.0 - OffsetA * OffsetA - OffsetB * OffsetB;
			const double C = Sqrt(C2 > 0.0 ? C2 : 0.0);
			return Frame.Right * OffsetA + Frame.Up * OffsetB - Frame.Axis * C;
		}

		bool ValidCue(const CueSpec& Cue, bool NeedEndMass)
		{
			const bool Finite = IsFinite(Cue.Mass) && IsFinite(Cue.EndMass) && IsFinite(Cue.TipRestitution) && IsFinite(Cue.TipFriction) &&
				IsFinite(Cue.TipFrictionKinetic) && IsFinite(Cue.TipDomeRadius) && IsFinite(Cue.ContactTime) && IsFinite(Cue.FollowThroughDistance);
			return Finite && Cue.Mass > 0.0 && (!NeedEndMass || Cue.EndMass > 0.0) && Cue.TipRestitution >= 0.0 && Cue.TipRestitution <= 1.0 &&
				Cue.TipFriction >= 0.0 && Cue.TipFrictionKinetic >= 0.0 && Cue.TipDomeRadius >= 0.0 && Cue.ContactTime >= 0.0 &&
				Cue.FollowThroughDistance >= 0.0;
		}

		bool ValidBall(const BallSpec& Spec)
		{
			return IsFinite(Spec.Radius) && IsFinite(Spec.Mass) && IsFinite(Spec.Inertia) && Spec.Radius > 0.0 && Spec.Mass > 0.0 && Spec.Inertia > 0.0;
		}

		// Friction-cone edge direction of a slipping tip (B.5 miscue branch): p_hat = (n + mu_k t_hat) / sqrt(1 + mu_k^2),
		// t_hat = the stroke's tangential direction at the contact (0 if the stroke is along the normal).
		Vec3 MiscueDirection(const Vec3& Axis, const Vec3& Normal, double KineticFriction)
		{
			const Vec3 Tangential = Axis - Normal * Dot(Axis, Normal);
			const Vec3 THat = Normalized(Tangential);
			return (Normal + THat * KineticFriction) / Sqrt(1.0 + KineticFriction * KineticFriction);
		}

		// Stop time and deceleration of a follow-through path with speed Speed0 at StartTime.
		void FinishTipPath(CueTipPath& Path, double Deceleration, double FollowThroughDistance)
		{
			if (!(Path.Speed0 > 0.0))
			{
				Path.Speed0 = 0.0;
				Path.Deceleration = Deceleration > 0.0 ? Deceleration : 0.0;
				Path.StopTime = Path.StartTime;
				return;
			}
			if (!(Deceleration > 0.0))
			{
				if (!(FollowThroughDistance > 0.0))
				{
					Path.Speed0 = 0.0;
					Path.Deceleration = 0.0;
					Path.StopTime = Path.StartTime;
					return;
				}
				Deceleration = Path.Speed0 * Path.Speed0 / (2.0 * FollowThroughDistance);
			}
			Path.Deceleration = Deceleration;
			Path.StopTime = Path.StartTime + Path.Speed0 / Deceleration;
		}
	}

	ErrorCode ValidateCueStrike(const CueStrikeInput& Input)
	{
		if (!IsFinite(Input.Speed) || Input.Speed < 0.0 || Input.Speed > kMaxCueSpeed)
		{
			return ErrorCode::CueSpeedOutOfRange;
		}
		if (!IsFinite(Input.Elevation) || Input.Elevation < 0.0 || !(Input.Elevation < 0.5 * kPi))
		{
			return ErrorCode::CueElevationOutOfRange;
		}
		if (!IsFinite(Input.Azimuth) || !IsFinite(Input.OffsetA) || !IsFinite(Input.OffsetB))
		{
			return ErrorCode::InvalidArgument;
		}
		if (!(Sqrt(Input.OffsetA * Input.OffsetA + Input.OffsetB * Input.OffsetB) < kCueOffsetValidLimit))
		{
			return ErrorCode::CueOffsetTooLarge;
		}
		if (!ValidCue(Input.Cue, Input.SquirtEnabled) || !IsFinite(Input.LambdaOverride) || Input.LambdaOverride > 1.0)
		{
			return ErrorCode::InvalidParameter;
		}
		return ErrorCode::Ok;
	}

	CueFrame MakeCueFrame(double Elevation, double Azimuth)
	{
		const double CosT = Cos(Elevation);
		const double SinT = Sin(Elevation);
		const double CosP = Cos(Azimuth);
		const double SinP = Sin(Azimuth);
		CueFrame Frame;
		Frame.Axis = {CosT * CosP, CosT * SinP, -SinT};
		Frame.Right = {SinP, -CosP, 0.0};
		Frame.Up = {SinT * CosP, SinT * SinP, CosT};
		return Frame;
	}

	Vec3 CueContactPoint(const CueFrame& Frame, double OffsetA, double OffsetB, double Radius)
	{
		return ContactDirection(Frame, OffsetA, OffsetB) * Radius;
	}

	Vec2 AimToContactOffset(const Vec2& AxisOffset, double Radius, double TipDomeRadius)
	{
		return AxisOffset * (Radius / (Radius + TipDomeRadius));
	}

	double MiscueLimit(double TipFriction)
	{
		return TipFriction / Sqrt(1.0 + TipFriction * TipFriction);
	}

	double SquirtAngle(double OffsetA, double BallToEndMassRatio, double InertiaK)
	{
		const double InvK = 1.0 / InertiaK; // 5/2 for a solid ball
		const double C2 = 1.0 - OffsetA * OffsetA;
		const double C = Sqrt(C2 > 0.0 ? C2 : 0.0);
		return Atan2(InvK * OffsetA * C, 1.0 + BallToEndMassRatio + InvK * C2);
	}

	double PinchLambda(double Elevation, const CueSpec& Cue, const PinchParams& Pinch)
	{
		const bool Jump = Cue.JumpCue || Cue.Mass <= Pinch.JumpCueMaxMass;
		const double Theta0 = Jump ? Pinch.Theta0Jump : Pinch.Theta0Cue;
		const double Theta1 = Jump ? Pinch.Theta1Jump : Pinch.Theta1Cue;
		if (!(Theta1 > Theta0))
		{
			return Elevation >= Theta0 ? 1.0 : 0.0;
		}
		return SmoothStep01((Elevation - Theta0) / (Theta1 - Theta0));
	}

	double SeparationMargin(double Rho, double TipRestitution, double BallMass, double CueMass, double InertiaK)
	{
		return InertiaK * TipRestitution * (1.0 + BallMass / CueMass) - Rho * Rho;
	}

	StrikeResult StrikeCueBall(const CueStrikeInput& Input, const BallState& Ball, const BallSpec& Spec, const ClothParams& Cloth,
		const SlateParams& Slate, const PinchParams& Pinch, double Gravity, const NumericsConfig& Numerics)
	{
		StrikeResult Result;
		Result.State = Ball;
		Result.Error = ValidateCueStrike(Input);
		if (Result.Error != ErrorCode::Ok)
		{
			return Result;
		}
		if (!ValidBall(Spec))
		{
			Result.Error = ErrorCode::InvalidArgument;
			return Result;
		}
		if (Ball.State != MotionState::Stationary)
		{
			Result.Error = ErrorCode::BallNotAtRest;
			return Result;
		}

		const CueSpec& Cue = Input.Cue;
		const CueFrame Frame = MakeCueFrame(Input.Elevation, Input.Azimuth);
		const double A = Input.OffsetA;
		const double B = Input.OffsetB;
		const double Rho = Sqrt(A * A + B * B);
		const double Radius = Spec.Radius;
		const double BallMass = Spec.Mass;
		const double InertiaK = InertiaFactor(Spec);
		const double CueMass = Cue.Mass;
		const double TipE = Cue.TipRestitution;
		const double V = Input.Speed;
		const Vec3 QUnit = ContactDirection(Frame, A, B); // Q / R
		const double RhoMax = MiscueLimit(Cue.TipFriction);

		Result.Rho = Rho;
		Result.MiscueLimit = RhoMax;
		Result.SeparationMargin = SeparationMargin(Rho, TipE, BallMass, CueMass, InertiaK);
		Result.ContactDuration = Cue.ContactTime;

		Vec3 PHat;
		Vec3 OmegaGain; // w = (J / I) (Q x p_hat) = (J R / I) (Q/R x p_hat)
		double J = 0.0;
		double Lambda = 0.0;
		if (Rho <= RhoMax)
		{
			// Grip (B.5 normal case, B.8.2 pinch): J = (1 + e) V / (1/M + (1/m)(1 - lambda sin^2 theta + rho^2 / k)).
			Lambda = Input.LambdaOverride >= 0.0 ? Input.LambdaOverride : PinchLambda(Input.Elevation, Cue, Pinch);
			const double SinT = Sin(Input.Elevation);
			J = (1.0 + TipE) * V / (1.0 / CueMass + (1.0 - Lambda * SinT * SinT + Rho * Rho / InertiaK) / BallMass);
			OmegaGain = Frame.Up * A - Frame.Right * B; // Q/R x d = a e_u - b e_r
			// Squirt (B.7), once, grip branch only: p_hat = cos(alpha) d - sin(alpha) e_r.
			const double Alpha = Input.SquirtEnabled ? SquirtAngle(A, BallMass / Cue.EndMass, InertiaK) : 0.0;
			PHat = Alpha != 0.0 ? Frame.Axis * Cos(Alpha) - Frame.Right * Sin(Alpha) : Frame.Axis;
			Result.SquirtAngle = Alpha;
		}
		else
		{
			// Miscue (B.5): impulse on the friction-cone edge; lambda and squirt are not applied.
			const Vec3 Normal = -QUnit;
			PHat = MiscueDirection(Frame.Axis, Normal, Cue.TipFrictionKinetic);
			OmegaGain = Cross(QUnit, PHat);
			const double Dp = Dot(Frame.Axis, PHat);
			J = (1.0 + TipE) * V * Dp / (Dp * Dp / CueMass + (1.0 + LengthSquared(OmegaGain) / InertiaK) / BallMass);
			Result.Miscue = true;
		}

		const Vec3 VelocityGain = PHat * (J / BallMass);
		const Vec3 SpinGain = OmegaGain * (J * Radius / Spec.Inertia);
		Result.Impulse = J;
		Result.Lambda = Lambda;
		Result.ImpulseDirection = PHat;
		Result.VelocityAfterTip = Ball.Velocity + VelocityGain;
		Result.OmegaAfterTip = Ball.Omega + SpinGain;
		Result.CueSpeedAfter = V - J * Dot(Frame.Axis, PHat) / CueMass;

		// Slate reaction at t = 0+ (B.8.3), never a separate event (implementation note 6).
		BallState After = Ball;
		After.Velocity = Result.VelocityAfterTip;
		After.Omega = Result.OmegaAfterTip;
		const double Wn = -After.Velocity.z; // "virtual" speed into the slate
		if (Wn > 0.0)
		{
			const double EEff = Lambda * Pinch.PinchRestitution + (1.0 - Lambda) * Slate.Restitution;
			const SlateImpactResult Impact = ResolveSlateImpact(After.Velocity, After.Omega, Spec, EEff, Cloth.SlidingFriction, Slate, 1, Gravity, Numerics);
			After.Velocity = Impact.Velocity;
			After.Omega = Impact.Omega;
			Result.SlateContact = true;
			Result.SlateStick = Impact.Stick;
		}
		else if (After.Velocity.z < MinBounceSpeed(Slate, Gravity))
		{
			After.Velocity.z = 0.0; // upward kick of a draw miscue below v_z_min: no micro-hop (B.8.3)
		}
		ClassifyState(After, Radius, Ball.Position.z - Radius, Numerics);
		Result.State = After;
		return Result;
	}

	CueTipPath MakeCueTipPath(const CueStrikeInput& Input, const StrikeResult& Strike, const Vec3& BallCenter, double BallRadius)
	{
		const CueFrame Frame = MakeCueFrame(Input.Elevation, Input.Azimuth);
		const double DomeRadius = Input.Cue.TipDomeRadius;
		CueTipPath Path;
		Path.Start = BallCenter + ContactDirection(Frame, Input.OffsetA, Input.OffsetB) * (BallRadius + DomeRadius);
		Path.Direction = Frame.Axis;
		Path.DomeRadius = DomeRadius;
		Path.StartTime = 0.0;
		Path.Speed0 = Strike.Error == ErrorCode::Ok ? Strike.CueSpeedAfter : 0.0;
		FinishTipPath(Path, 0.0, Input.Cue.FollowThroughDistance);
		return Path;
	}

	MotionSegment CueTipAsSegment(const CueTipPath& Path)
	{
		MotionSegment Seg;
		Seg.State = MotionState::Airborne; // free 3-D quadratic, no gravity, no support
		Seg.T0 = Path.StartTime;
		Seg.TauEnd = Path.StopTime > Path.StartTime ? Path.StopTime - Path.StartTime : 0.0;
		Seg.Radius = Path.DomeRadius;
		Seg.SupportZ = 0.0;
		Seg.Pos0 = Path.Start;
		Seg.Vel0 = Path.Direction * Path.Speed0;
		Seg.Accel2 = Path.Direction * (-0.5 * Path.Deceleration);
		return Seg;
	}

	TipRecontactResult ResolveTipRecontact(const CueTipPath& Tip, double Time, const BallState& Ball, const BallSpec& Spec, const CueSpec& Cue,
		const ClothParams& Surface, const SlateParams& Slate, double Gravity, const NumericsConfig& Numerics)
	{
		TipRecontactResult Result;
		Result.Ball = Ball;
		Result.Tip = Tip;
		if (!ValidBall(Spec) || !ValidCue(Cue, false))
		{
			return Result;
		}

		// Tip state at Time (uniform deceleration, at rest after StopTime).
		const double Duration = Tip.StopTime > Tip.StartTime ? Tip.StopTime - Tip.StartTime : 0.0;
		const double Tau = Clamp(Time - Tip.StartTime, 0.0, Duration);
		const double Travel = Tip.Speed0 * Tau - 0.5 * Tip.Deceleration * Tau * Tau;
		const double TipSpeed = Max(0.0, Tip.Speed0 - Tip.Deceleration * Tau);
		const Vec3 TipCenter = Tip.Start + Tip.Direction * Travel;

		// Contact normal n from the tip dome center into the ball; contact point Q = -R n.
		const Vec3 Separation = Ball.Position - TipCenter;
		const double Distance = Length(Separation);
		if (!(Distance > 0.0))
		{
			return Result;
		}
		const Vec3 Normal = Separation / Distance;
		const Vec3 QUnit = -Normal;
		const double Radius = Spec.Radius;
		const double BallMass = Spec.Mass;
		const double InertiaK = InertiaFactor(Spec);
		const Vec3 ContactVelocity = Ball.Velocity + Cross(Ball.Omega, QUnit * Radius);
		const Vec3 Relative = Tip.Direction * TipSpeed - ContactVelocity; // tip relative to the ball's contact point
		Result.RelativeSpeed = Dot(Relative, Normal);
		if (!(Result.RelativeSpeed > 0.0))
		{
			return Result; // separating or grazing: no impulse
		}

		// B.4 / B.5 with the relative velocity: grip (impulse along the cue axis) inside the friction cone, else the
		// cone edge. No squirt on re-contacts (applied once, at the strike, B.7).
		const double CosPsi = Dot(Tip.Direction, Normal);
		const double SinPsi = Length(Tip.Direction - Normal * CosPsi);
		const bool Grip = CosPsi > 0.0 && SinPsi <= MiscueLimit(Cue.TipFriction);
		const Vec3 PHat = Grip ? Tip.Direction : MiscueDirection(Tip.Direction, Normal, Cue.TipFrictionKinetic);
		const double Approach = Dot(Relative, PHat);
		if (!(Approach > 0.0))
		{
			return Result;
		}
		const double Dp = Dot(Tip.Direction, PHat);
		const Vec3 QxP = Cross(QUnit, PHat);
		const double InverseMass = Dp * Dp / Cue.Mass + (1.0 + LengthSquared(QxP) / InertiaK) / BallMass;
		const double J = (1.0 + Cue.TipRestitution) * Approach / InverseMass;

		BallState After = Ball;
		After.Velocity += PHat * (J / BallMass);
		After.Omega += QxP * (J * Radius / Spec.Inertia);
		const bool OnSurface = IsOnSurface(Ball.State);
		ApplyTableReaction(After, OnSurface, Spec, Surface, Slate, Gravity, Numerics);
		ClassifyState(After, Radius, OnSurface ? Ball.Position.z - Radius : 0.0, Numerics);

		Result.Ball = After;
		Result.Impulse = J;
		Result.Tip.Start = TipCenter;
		Result.Tip.StartTime = Time > Tip.StartTime ? Time : Tip.StartTime;
		Result.Tip.Speed0 = TipSpeed - J * Dp / Cue.Mass;
		FinishTipPath(Result.Tip, Tip.Deceleration, Cue.FollowThroughDistance); // the arm keeps braking at the same rate
		return Result;
	}
}
