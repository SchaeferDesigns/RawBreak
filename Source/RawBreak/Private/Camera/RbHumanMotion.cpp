#include "Camera/RbHumanMotion.h"

#include "rb/Human/NoiseHash.h"

// Owner: M2-F. The human head / body motion of the header (P5). Tests: RawBreak.Unit.HumanMotion.* (Private/Tests/RbHumanMotionTests.cpp).

namespace
{
	constexpr double kSlowSpeed = 0.8;          // [m/s] of the 3 cm bob (Hirasaki et al. 1999)
	constexpr double kFastSpeed = 1.4;          // [m/s] of the 4.5 cm bob and the 2.5 cm sway
	constexpr double kGaitLagSeconds = 0.25;
	constexpr double kWalkingSpeed = 0.2;       // [m/s] a heel strike below this is no footstep
	constexpr double kQuickSeconds = 0.4;       // PostureTransition Quick
	constexpr double kSettleEnvelopeCm = 0.05;  // the posture change ends when the settle envelope is below 0.5 mm
	constexpr double kDownBlendSeconds = 0.4;   // standing <-> down amplitude blend (never a jump of the sway / breathing)
	constexpr double kPressureSeconds = 1.0;    // breathing follows a pressure change
	constexpr double kInternalStep = 1.0 / 480.0; // reaction spring sub-step
	constexpr double kReactionHistorySeconds = 1.0;
	constexpr double kFlinchPeakSeconds = 0.065;  // 50-80 ms
	constexpr double kFlinchSeconds = 0.6;
	constexpr double kTremorHzA = 8.7;
	constexpr double kTremorHzB = 9.3;

	// Postural sway: a band-limited sum of sines per axis (0.1-0.5 Hz), amplitudes normalised so the 2D RMS is the parameter.
	constexpr double kSwayHzX[3] = {0.137, 0.231, 0.389};
	constexpr double kSwayHzY[3] = {0.113, 0.197, 0.347};
	constexpr double kSwayAmp[3] = {0.6, 0.35, 0.2};
	constexpr double kSwayPhaseX[3] = {0.7, 2.9, 5.1};
	constexpr double kSwayPhaseY[3] = {1.9, 4.2, 0.3};
	// Headcam mount jitter (1-4 Hz).
	constexpr double kJitterHz[3] = {1.3, 2.1, 3.4};

	double MinimumJerk(double U)
	{
		U = FMath::Clamp(U, 0.0, 1.0);
		return U * U * U * (10.0 + U * (-15.0 + 6.0 * U));
	}

	double SmoothStep(double U)
	{
		U = FMath::Clamp(U, 0.0, 1.0);
		return U * U * (3.0 - 2.0 * U);
	}

	// Seeded uniform in [A, B] (draw K of Seed).
	double Draw(uint64 Seed, uint64 K, double A, double B)
	{
		return A + (B - A) * rb::human::U01(rb::human::HashKeys(Seed, K, 0x4D324650ull /* "M2FP" */));
	}

	bool IsDownward(ERbPostureChange Change)
	{
		return Change == ERbPostureChange::GetDown || Change == ERbPostureChange::LeanOver;
	}

	// Duration of each channel of a natural posture change: the view rotation starts at once, the leading translation HeadLead later,
	// the lagging one ArcLead after that, all with this duration, so the movement ends at MainSeconds and every channel starts and
	// ends with zero velocity (no pop at the first frame, no kink in between).
	double ChannelSeconds(const FRbPostureParams& P)
	{
		return FMath::Max(0.05, P.MainSeconds - P.HeadLeadSeconds - P.ArcLeadSeconds);
	}

	// Progress of the channel that starts at Start (time-warped minimum jerk: WarpExponent < 1 = a faster start).
	double ChannelProgress(const FRbPostureParams& P, double Seconds, double Start)
	{
		const double U = FMath::Clamp((Seconds - Start) / ChannelSeconds(P), 0.0, 1.0);
		const double W = P.WarpExponent == 1.0 ? U : FMath::Pow(U, P.WarpExponent);
		return MinimumJerk(W);
	}

	// Free response of the damped settle, starting at 1 with zero velocity.
	double SettleResponse(const FRbPostureParams& P, double Tau)
	{
		const double Omega = UE_DOUBLE_TWO_PI * P.SettleHz;
		const double Zeta = FMath::Clamp(P.SettleZeta, 0.05, 0.95);
		const double OmegaD = Omega * FMath::Sqrt(1.0 - Zeta * Zeta);
		return FMath::Exp(-Zeta * Omega * Tau) * (FMath::Cos(OmegaD * Tau) + (Zeta * Omega / OmegaD) * FMath::Sin(OmegaD * Tau));
	}

	// Time the vertical part of the main movement ends (the overshoot peak).
	double VerticalEnd(const FRbPostureParams& P)
	{
		return IsDownward(P.Change) ? P.MainSeconds : FMath::Max(0.0, P.MainSeconds - P.ArcLeadSeconds);
	}

	double SwayAxis(double Time, const double* Hz, const double* Phase)
	{
		double S = 0.0;
		for (int32 I = 0; I < 3; ++I)
		{
			S += kSwayAmp[I] * FMath::Sin(UE_DOUBLE_TWO_PI * Hz[I] * Time + Phase[I]);
		}
		// Per-axis RMS of the sum = sqrt(sum a^2 / 2); normalise it to 1 / sqrt(2) so the 2D RMS is 1.
		static const double Norm = FMath::Sqrt(0.5 * (kSwayAmp[0] * kSwayAmp[0] + kSwayAmp[1] * kSwayAmp[1] + kSwayAmp[2] * kSwayAmp[2]));
		return S / (Norm * UE_DOUBLE_SQRT_2);
	}

	double Jitter(double Time, double Phase)
	{
		double S = 0.0;
		for (int32 I = 0; I < 3; ++I)
		{
			S += FMath::Sin(UE_DOUBLE_TWO_PI * kJitterHz[I] * Time + Phase * (I + 1));
		}
		return S / 3.0;
	}
}

// ---------------------------------------------------------------------------------------------------------
// Posture changes
// ---------------------------------------------------------------------------------------------------------

FRbPostureParams FRbHumanMotion::MakePostureParams(ERbPostureChange Change, uint64 Seed, ERbPostureTransition Style)
{
	FRbPostureParams P;
	P.Change = Change;
	P.Style = Style;
	if (Style == ERbPostureTransition::Cut)
	{
		P.MainSeconds = 0.0;
		P.TotalSeconds = 0.0;
		return P;
	}
	if (Style == ERbPostureTransition::Quick)
	{
		P.MainSeconds = kQuickSeconds;
		P.TotalSeconds = kQuickSeconds;
		return P;
	}
	// Seeded ranges per change (the header: slower down with a long settle, faster up at the start). Main = the whole movement
	// (rotation, leading and lagging translation each run Main - Head - Arc, staggered); the settle comes on top.
	struct FRange
	{
		double Main[2], Warp[2], Arc[2], Head[2], Over[2], Hz[2], Zeta[2], Shift[2], Wobble[2];
	};
	static const FRange Ranges[4] = {
		/* GetDown      */ {{0.90, 1.05}, {1.00, 1.00}, {0.06, 0.12}, {0.09, 0.14}, {0.40, 1.20}, {2.2, 3.0}, {0.55, 0.70}, {1.5, 4.0}, {0.2, 0.8}},
		/* StandUp      */ {{0.85, 1.05}, {0.76, 0.86}, {0.05, 0.10}, {0.08, 0.12}, {0.10, 0.40}, {2.5, 3.2}, {0.60, 0.75}, {0.5, 1.5}, {0.1, 0.4}},
		/* LeanOver     */ {{0.55, 0.75}, {1.00, 1.00}, {0.04, 0.08}, {0.08, 0.12}, {0.20, 0.60}, {2.2, 3.0}, {0.55, 0.70}, {0.5, 1.5}, {0.1, 0.4}},
		/* StraightenUp */ {{0.50, 0.65}, {0.70, 0.80}, {0.03, 0.06}, {0.08, 0.11}, {0.10, 0.30}, {2.5, 3.2}, {0.60, 0.75}, {0.3, 1.0}, {0.1, 0.3}},
	};
	const FRange& R = Ranges[static_cast<int32>(Change)];
	P.MainSeconds = Draw(Seed, 1, R.Main[0], R.Main[1]);
	P.WarpExponent = Draw(Seed, 2, R.Warp[0], R.Warp[1]);
	P.ArcLeadSeconds = Draw(Seed, 3, R.Arc[0], R.Arc[1]);
	P.HeadLeadSeconds = Draw(Seed, 4, R.Head[0], R.Head[1]);
	P.OvershootCm = Draw(Seed, 5, R.Over[0], R.Over[1]);
	P.SettleHz = Draw(Seed, 6, R.Hz[0], R.Hz[1]);
	P.SettleZeta = Draw(Seed, 7, R.Zeta[0], R.Zeta[1]);
	P.WeightShiftCm = Draw(Seed, 8, R.Shift[0], R.Shift[1]);
	P.WobbleCm = Draw(Seed, 9, R.Wobble[0], R.Wobble[1]);
	P.WobblePhase = Draw(Seed, 10, 0.0, UE_DOUBLE_TWO_PI);
	// The change ends when the settle envelope (amplitude factor 1 / sqrt(1 - zeta^2)) falls below 0.5 mm.
	const double Omega = UE_DOUBLE_TWO_PI * P.SettleHz;
	const double Envelope = P.OvershootCm / FMath::Sqrt(1.0 - P.SettleZeta * P.SettleZeta);
	const double SettleSeconds = Envelope > kSettleEnvelopeCm ? FMath::Loge(Envelope / kSettleEnvelopeCm) / (P.SettleZeta * Omega) : 0.0;
	P.TotalSeconds = FMath::Max(P.MainSeconds, VerticalEnd(P) + SettleSeconds);
	return P;
}

void FRbHumanMotion::EvaluateProgress(const FRbPostureParams& P, double Seconds, double& OutHorizontal, double& OutVertical, double& OutRotation,
	double& OutOvershootFraction)
{
	OutOvershootFraction = 0.0;
	if (P.Style == ERbPostureTransition::Cut || P.TotalSeconds <= 0.0)
	{
		OutHorizontal = OutVertical = OutRotation = 1.0;
		return;
	}
	if (P.Style == ERbPostureTransition::Quick)
	{
		OutHorizontal = OutVertical = OutRotation = SmoothStep(Seconds / P.MainSeconds);
		return;
	}
	const bool bDown = IsDownward(P.Change);
	const double Lead = ChannelProgress(P, Seconds, P.HeadLeadSeconds);
	const double Lag = ChannelProgress(P, Seconds, P.HeadLeadSeconds + P.ArcLeadSeconds);
	// Getting down the head goes forward first (hip hinge), standing up it rises first; the view rotation leads both.
	OutHorizontal = bDown ? Lead : Lag;
	OutVertical = bDown ? Lag : Lead;
	OutRotation = ChannelProgress(P, Seconds, 0.0);
	const double VEnd = VerticalEnd(P);
	OutOvershootFraction = Seconds <= VEnd ? OutVertical : SettleResponse(P, Seconds - VEnd);
}

void FRbHumanMotion::BeginPostureChange(ERbPostureChange InChange, const FTransform& FromEye, const FTransform& /*ToEye*/, uint64 Seed,
	ERbPostureTransition Style)
{
	Change = InChange;
	ChangeFrom = FromEye;
	ChangeFrom.SetScale3D(FVector::OneVector);
	ChangeSeed = Seed;
	Posture = MakePostureParams(InChange, Seed, Style);
	ChangeSeconds = Posture.TotalSeconds;
}

bool FRbHumanMotion::EvaluatePostureChange(double Seconds, const FTransform& ToEye, FTransform& OutEye) const
{
	if (Seconds >= ChangeSeconds)
	{
		OutEye = FTransform(ToEye.GetRotation(), ToEye.GetLocation());
		return false;
	}
	double H = 1.0;
	double V = 1.0;
	double R = 1.0;
	double B = 0.0;
	EvaluateProgress(Posture, FMath::Max(0.0, Seconds), H, V, R, B);

	const FVector From = ChangeFrom.GetLocation();
	const FVector Delta = ToEye.GetLocation() - From;
	const FVector Horizontal(Delta.X, Delta.Y, 0.0);
	double Sign = IsDownward(Change) ? -1.0 : 1.0;
	if (FMath::Abs(Delta.Z) > 1.0)
	{
		Sign = Delta.Z < 0.0 ? -1.0 : 1.0;
	}
	FVector Eye = From + Horizontal * H + FVector(0.0, 0.0, Delta.Z * V + Sign * Posture.OvershootCm * B);

	// Weight shift toward the bridge-hand side (left of where the player ends up facing) plus the seeded wobble; zero at both ends.
	const double M = FMath::Clamp(0.5 * (H + V), 0.0, 1.0);
	const double Bump = FMath::Sin(UE_DOUBLE_PI * M);
	const double Lateral = Posture.WeightShiftCm * Bump + Posture.WobbleCm * Bump * FMath::Sin(UE_DOUBLE_TWO_PI * M + Posture.WobblePhase);
	const FVector Left = FRotator(0.0, ToEye.Rotator().Yaw, 0.0).RotateVector(FVector(0.0, -1.0, 0.0));
	Eye += Left * Lateral;

	OutEye = FTransform(FQuat::Slerp(ChangeFrom.GetRotation(), ToEye.GetRotation(), FMath::Clamp(R, 0.0, 1.0)).GetNormalized(), Eye);
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// Continuous layer
// ---------------------------------------------------------------------------------------------------------

double FRbHumanMotion::BobPeakToPeakCm(double SpeedMps, const FRbHeadMotionParams& P)
{
	if (SpeedMps <= 0.0)
	{
		return 0.0;
	}
	if (SpeedMps < kSlowSpeed)
	{
		return P.BobPeakToPeakSlowCm * SpeedMps / kSlowSpeed;
	}
	const double PP = P.BobPeakToPeakSlowCm + (P.BobPeakToPeakCm - P.BobPeakToPeakSlowCm) * (SpeedMps - kSlowSpeed) / (kFastSpeed - kSlowSpeed);
	return FMath::Min(PP, 1.5 * P.BobPeakToPeakCm);
}

double FRbHumanMotion::BreathRateHz(double Pressure, const FRbHeadMotionParams& P)
{
	return P.BreathRateHz * (P.BreathRateCalmFactor + (P.BreathRatePressureFactor - P.BreathRateCalmFactor) * FMath::Clamp(Pressure, 0.0, 1.0));
}

double FRbHumanMotion::BreathDepthFactor(double Pressure)
{
	return 1.0 + 0.6 * FMath::Clamp(Pressure, 0.0, 1.0);
}

void FRbHumanMotion::NotifyImpact(double Loudness)
{
	PendingFlinch = FMath::Max(PendingFlinch, FMath::Clamp(Loudness, 0.0, 1.0));
}

void FRbHumanMotion::ResetReaction()
{
	ReactionHistory.Reset();
	ReactionAngle = FVector2D::ZeroVector;
	ReactionVelocity = FVector2D::ZeroVector;
	ReactionStepRemainder = 0.0;
}

void FRbHumanMotion::Reset()
{
	*this = FRbHumanMotion();
}

void FRbHumanMotion::StepReaction(double Dt, const FRbHumanMotionInputs& Inputs, const FRbHeadMotionParams& P)
{
	const bool bReact = Inputs.bWatching && Inputs.bHasReactionTarget;
	if (Inputs.bWatching && !bWasWatching)
	{
		ReactionHistory.Reset(); // a new watch: the pursuit starts from the current view
	}
	bWasWatching = Inputs.bWatching;
	if (bReact)
	{
		ReactionHistory.Add({Time, Inputs.ReactionAnglesDeg});
		int32 Old = 0;
		while (Old + 1 < ReactionHistory.Num() && ReactionHistory[Old + 1].Time < Time - kReactionHistorySeconds)
		{
			++Old;
		}
		if (Old > 0)
		{
			ReactionHistory.RemoveAt(0, Old, EAllowShrinking::No);
		}
	}
	else
	{
		ReactionHistory.Reset();
	}
	// The pursuit target: the target angles as they were Latency ago (the eyes need ~150-200 ms to start a smooth pursuit), a
	// fraction of the way (the eyes do the rest), clamped.
	FVector2D Target = FVector2D::ZeroVector;
	if (bReact && ReactionHistory.Num() > 0)
	{
		const double At = Time - P.ReactionLatencySeconds;
		if (At >= ReactionHistory[0].Time)
		{
			int32 I = 0;
			while (I + 1 < ReactionHistory.Num() && ReactionHistory[I + 1].Time <= At)
			{
				++I;
			}
			FVector2D Angles = ReactionHistory[I].Angles;
			if (I + 1 < ReactionHistory.Num())
			{
				const double Span = ReactionHistory[I + 1].Time - ReactionHistory[I].Time;
				const double A = Span > 0.0 ? (At - ReactionHistory[I].Time) / Span : 0.0;
				Angles = FMath::Lerp(ReactionHistory[I].Angles, ReactionHistory[I + 1].Angles, A);
			}
			Target.X = FMath::Clamp(Angles.X * P.ReactionFollowFraction, -P.ReactionMaxYawDeg, P.ReactionMaxYawDeg);
			Target.Y = FMath::Clamp(Angles.Y * P.ReactionFollowFraction, -P.ReactionMaxPitchDeg, P.ReactionMaxPitchDeg);
		}
	}
	// Critically damped pursuit, sub-stepped at a fixed rate (frame-rate independent).
	const double Omega = UE_DOUBLE_TWO_PI * P.ReactionPursuitHz;
	ReactionStepRemainder += Dt;
	while (ReactionStepRemainder >= kInternalStep)
	{
		ReactionStepRemainder -= kInternalStep;
		const FVector2D Accel = (Target - ReactionAngle) * (Omega * Omega) - ReactionVelocity * (2.0 * Omega);
		ReactionVelocity += Accel * kInternalStep;
		ReactionAngle += ReactionVelocity * kInternalStep;
	}
}

FRbHumanMotionSample FRbHumanMotion::Step(const FRbHumanMotionInputs& Inputs, const FRbCameraPresetParams& Params)
{
	const FRbHeadMotionParams& P = Params.HeadMotion;
	const double Dt = FMath::Max(0.0, Inputs.DeltaSeconds);
	const double TimeBefore = Time;
	Time += Dt;

	// Gait: the speed with a short lag; the step phase advances only while walking (heel strike at every 2 pi).
	const double TargetSpeed = Inputs.bDown ? 0.0 : FMath::Max(0.0, Inputs.WalkSpeedMps);
	SmoothedSpeed = TargetSpeed + (SmoothedSpeed - TargetSpeed) * FMath::Exp(-Dt / kGaitLagSeconds);
	const double Speed = SmoothedSpeed;
	FRbHumanMotionSample Out;
	if (Speed > 0.01)
	{
		StepPhase += UE_DOUBLE_TWO_PI * (P.StepRateAtRestHz + P.StepRatePerMps * Speed) * Dt;
		while (StepPhase >= UE_DOUBLE_TWO_PI)
		{
			StepPhase -= UE_DOUBLE_TWO_PI;
			++StepCount;
			if (Speed >= kWalkingSpeed)
			{
				Out.bFootstep = true;
				Out.bLeftFoot = (StepCount & 1) != 0;
			}
		}
	}
	// Smoothed context: standing <-> down and the pressure never make the amplitudes jump.
	const double DownTarget = Inputs.bDown ? 1.0 : 0.0;
	const double PressureTarget = FMath::Clamp(Inputs.Pressure, 0.0, 1.0);
	if (!bStarted)
	{
		DownBlend = DownTarget; // the first step starts in the current posture / pressure
		SmoothedPressure = PressureTarget;
		bStarted = true;
	}
	DownBlend = DownTarget + (DownBlend - DownTarget) * FMath::Exp(-Dt / kDownBlendSeconds);
	SmoothedPressure = PressureTarget + (SmoothedPressure - PressureTarget) * FMath::Exp(-Dt / kPressureSeconds);
	const double BreathHz = BreathRateHz(SmoothedPressure, P);
	BreathPhase = FMath::Fmod(BreathPhase + UE_DOUBLE_TWO_PI * BreathHz * Dt, 1000.0 * UE_DOUBLE_TWO_PI);
	StepReaction(Dt, Inputs, P);
	if (PendingFlinch > 0.0)
	{
		// One flinch per burst of impacts (the break is a clatter of clicks within a few ms): a new one only when none runs, the
		// running one is fading, or the new impact is clearly louder.
		if (FlinchStart < 0.0 || TimeBefore - FlinchStart > 0.3 || PendingFlinch > 1.3 * FlinchAmplitude)
		{
			FlinchStart = TimeBefore;
			FlinchAmplitude = PendingFlinch;
		}
		PendingFlinch = 0.0;
	}
	Out.BreathRateHz = BreathHz;

	const double Master = FMath::Max(0.0, Inputs.MotionScale);
	if (Master <= 0.0)
	{
		return Out; // Reduced motion / comfort off: nothing moves (footsteps are still reported: the audio hears the walk)
	}
	const double Bob = Master * FMath::Max(0.0, Inputs.HeadBobScale);
	const double Body = Master * FMath::Max(0.0, Inputs.BodySwayScale);
	const double Settle = 1.0 - P.SettleReduction * FMath::Clamp(Inputs.SettleAlpha, 0.0, 1.0);

	// Walking: vertical bob at the step rate (lowest at the heel strike), lateral sway toward the stance foot at the stride rate.
	const double BobCm = -0.5 * BobPeakToPeakCm(Speed, P) * FMath::Cos(StepPhase);
	const double StanceSide = (StepCount & 1) != 0 ? -1.0 : 1.0; // left stance after a left heel strike
	const double SwayCm = StanceSide * 0.5 * P.SwayPeakToPeakCm * FMath::Min(Speed / kFastSpeed, 1.5) * FMath::Sin(0.5 * StepPhase);

	// Breathing (rate and depth with the pressure), postural sway, tremor; all calmed by the Settle.
	const double BreathMm = FMath::Lerp(P.BreathStandingMm, P.BreathDownMm, DownBlend);
	const double BreathDepth = BreathDepthFactor(SmoothedPressure) * Settle;
	const double BreathCm = 0.1 * BreathMm * BreathDepth;
	Out.BreathDepthScale = BreathDepth * Body;
	const double SwayMm = FMath::Lerp(P.PosturalSwayStandingMm, P.PosturalSwayDownMm, DownBlend);
	const double PosturalCm = 0.1 * SwayMm * Settle;
	const double TremorCm = 0.1 * P.PressureTremorMm * SmoothedPressure * Settle;

	FVector Stabilised;
	Stabilised.X = Body * (0.3 * BreathCm * FMath::Sin(BreathPhase + 0.5) + PosturalCm * SwayAxis(Time, kSwayHzX, kSwayPhaseX));
	Stabilised.Y = Bob * SwayCm + Body * (PosturalCm * SwayAxis(Time, kSwayHzY, kSwayPhaseY) +
		TremorCm * FMath::Sin(UE_DOUBLE_TWO_PI * kTremorHzB * Time + 1.0));
	Stabilised.Z = Bob * BobCm + Body * (BreathCm * FMath::Sin(BreathPhase) + TremorCm * FMath::Sin(UE_DOUBLE_TWO_PI * kTremorHzA * Time));
	Out.StabilisedOffset = Stabilised * Params.HeadTranslationScale;
	Out.Offset = Out.StabilisedOffset;

	if (!Params.bStabiliseGaze)
	{
		// A head-mounted camera nods with the bob and rolls with the stride (no VOR); the mount adds a little 1-4 Hz jitter.
		const double JitterDeg = Params.MountJitterDeg * Master * FMath::Max(0.0, Inputs.MountShakeScale);
		Out.Rotation.Pitch = -P.HeadPitchPerCmBob * BobCm * Bob + JitterDeg * Jitter(Time, 7.9);
		Out.Rotation.Yaw = JitterDeg * Jitter(Time, 13.1);
		Out.Rotation.Roll = P.HeadRollPerCmSway * SwayCm * Bob + JitterDeg * Jitter(Time, 29.5);
	}

	// Reactions: the pursuit turn (and a little lean toward it) and the flinch - deliberate, never stabilised.
	FRotator Reaction(ReactionAngle.Y * Body, ReactionAngle.X * Body, 0.0);
	Out.Offset.Y += P.ReactionLeanCmPerDeg * ReactionAngle.X * Body;
	if (FlinchStart >= 0.0)
	{
		const double Tau = Time - FlinchStart;
		if (Tau > kFlinchSeconds)
		{
			FlinchStart = -1.0;
		}
		else
		{
			const double X = Tau / kFlinchPeakSeconds;
			const double Alpha = X * FMath::Exp(1.0 - X) * FlinchAmplitude * Body;
			Out.Offset.X -= 0.1 * P.FlinchBackMm * Alpha;
			Reaction.Pitch += P.FlinchPitchDeg * Alpha;
		}
	}
	Out.Reaction = Reaction;
	Out.Rotation += Reaction;
	return Out;
}
