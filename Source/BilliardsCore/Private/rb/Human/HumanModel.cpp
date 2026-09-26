#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 3.1, 3.4-3.7, 3.9, 4.1-4.3 (after the shot). Oracle:
// Tools/reference/human-factors/stroke.py (+ recompute_v12.py for the v1.2 streak-guarded draws).
//
// ExecuteStroke and SampleHand share one evaluation (EvaluateStroke) at a time t with a ramp r of the per-shot channels:
// ExecuteStroke is t = t_c, r = 1 (always the full draws), SampleHand any t with r = SmoothStep01((t - t_fwd) / 0.1 s). Every
// per-shot term enters as r (term), and r = 1 multiplies exactly, so SampleHand at t_c with the full ramp is bitwise the
// executed pose (A-HUM-2). The operation order mirrors the oracle (stroke.py), so the values agree to the last bits on one CRT.
#include "rb/Human/HumanModel.h"

#include "rb/Math/Scalar.h"

namespace rb::human
{
	namespace
	{
		double ChannelScale(const HumanParams& Params, NoiseChannel Channel)
		{
			return (Params.ChannelMask & ChannelBit(Channel)) != 0u ? 0.0 : Params.NoiseScale;
		}

		// V_i after the short-cue cap (HF-32).
		double EffectiveIntendedSpeed(const IntendedStroke& Intended, const StrokeSituation& Situation, const HumanParams& Params)
		{
			return Situation.ShortCue ? Min(Intended.Speed, Params.ShortCueSpeedCap * Params.MaxSpeed) : Intended.Speed;
		}

		double Hypot2(double X, double Y) { return Sqrt(X * X + Y * Y); }

		bool Finite(double X) { return IsFinite(X); }

		bool StrokeInputsValid(const IntendedStroke& Intended, const ShooterAttributes& Attributes, const StrokeSituation& Situation,
			const BallSpec& CueBall, const HumanParams& Params)
		{
			const bool IntendedOk = Finite(Intended.Azimuth) && Finite(Intended.Elevation) && Finite(Intended.AxisOffsetA) && Finite(Intended.AxisOffsetB) &&
				Finite(Intended.Speed) && Finite(Intended.TipVelocityRight) && Finite(Intended.TipVelocityUp) && Finite(Intended.TimeDown) &&
				Finite(Intended.ForwardStart) && Finite(Intended.SettleStart) && Finite(Intended.PauseDuration) && Finite(Intended.ContactAcceleration);
			const bool AttributesOk = Finite(Attributes.Steadiness) && Finite(Attributes.SpeedControl) && Finite(Attributes.SpinTouch) &&
				Finite(Attributes.BridgeStability) && Finite(Attributes.Stance) && Finite(Attributes.Nerve);
			const bool SituationOk = Finite(Situation.BridgeLength) && Situation.BridgeLength >= 0.0 && Finite(Situation.BridgeToGrip) &&
				Situation.BridgeToGrip > 0.0 && Finite(Situation.StanceDifficulty) && Finite(Situation.Pressure) && Finite(Situation.Fatigue) &&
				Finite(Situation.Sweat) && Finite(Situation.ElevationFloor) && Finite(Situation.Intoxication);
			const bool BallOk = Finite(CueBall.Radius) && CueBall.Radius > 0.0 && Finite(CueBall.Mass) && CueBall.Mass > 0.0;
			const bool ParamsOk = Finite(Params.NoiseScale) && Params.NoiseScale >= 0.0;
			return IntendedOk && AttributesOk && SituationOk && BallOk && ParamsOk;
		}

		// Everything of 3.5 at one time with a ramp of the per-shot channels.
		struct StrokeEvaluation
		{
			SituationFactors Factors;
			double IntendedSpeed = 0.0;   // V_i after the short-cue cap
			double Ns[11] = {};           // NS per channel 1..10 (index = channel id)
			WatchableProcess Drift[2];    // lateral, vertical
			WatchableProcess Tremor[2];
			double DriftValue[2] = {};    // D_lat(t), D_vert(t)
			double TremorValue[2] = {};   // T_lat(t), T_vert(t)
			GuardedDraw DrawTipA, DrawTipB, DrawElevation, DrawSpeed, DrawFlinch;
			WarpEffect Warp;
			double GripLateral = 0.0;     // y_g [m]
			double GripVertical = 0.0;    // z_g [m]
			double TremorRight = 0.0;     // tr_r [m]
			double TremorUp = 0.0;        // tr_u [m]
			double Yaw = 0.0;             // [rad]
			double Pitch = 0.0;           // [rad]
			double TipAOffset = 0.0;      // r NS eps_A sA [m]
			double TipBOffset = 0.0;      // r NS eps_B sB [m]
			double Bias = 0.0;            // r bias [m]
			double ElevationNoise = 0.0;  // r NS eps_El sTh [rad]
			double SpeedGain = 0.0;       // r NS eps_V sV [1]
			double FlinchLoss = 0.0;      // r fl [1]
			double Azimuth = 0.0;         // phi_x
			double ElevationRaw = 0.0;    // before the floor
			double Elevation = 0.0;       // theta_x
			double AxisA = 0.0;           // A_x
			double AxisB = 0.0;           // B_x
			double SpeedRaw = 0.0;        // before the clamp
			double Speed = 0.0;           // V_x
			bool ElevationClamped = false;
		};

		void EvaluateStroke(const IntendedStroke& Intended, const ShooterAttributes& Attributes, const StrokeSituation& Situation, const CueBodyState& CueBody,
			const CueSpec& Cue, const BallSpec& CueBall, const NoiseKey& Key, const NoiseHistory& History, const HumanParams& Params, double Time,
			double Ramp, bool WithDraws, StrokeEvaluation& Out)
		{
			const double R = CueBall.Radius;
			const double Lb = Situation.BridgeLength;
			const double Lbg = Situation.BridgeToGrip;
			const double Lbc = Lb + R; // bridge to cue-ball centre along the cue
			Out.Factors = ComputeSituationFactors(Intended, Attributes, Situation, Params, Time, R);
			Out.IntendedSpeed = EffectiveIntendedSpeed(Intended, Situation, Params);
			for (int c = 1; c <= 10; ++c)
			{
				Out.Ns[c] = ChannelScale(Params, static_cast<NoiseChannel>(c));
			}
			const SituationFactors& F = Out.Factors;

			// Watchable channels (drawn continuously, 3.7).
			Out.Drift[0] = MakeWatchableProcess(Key, NoiseChannel::DriftLat, kDriftBandLo, kDriftBandHi);
			Out.Drift[1] = MakeWatchableProcess(Key, NoiseChannel::DriftVert, kDriftBandLo, kDriftBandHi);
			Out.Tremor[0] = MakeWatchableProcess(Key, NoiseChannel::TremorLat, kTremorBandLo, kTremorBandHi);
			Out.Tremor[1] = MakeWatchableProcess(Key, NoiseChannel::TremorVert, kTremorBandLo, kTremorBandHi);
			for (int i = 0; i < 2; ++i)
			{
				Out.DriftValue[i] = ProcessValue(Out.Drift[i], Time);
				Out.TremorValue[i] = ProcessValue(Out.Tremor[i], Time);
			}
			Out.GripLateral = Out.Ns[1] * F.DriftSigma * Out.DriftValue[0];
			Out.GripVertical = Out.Ns[2] * Params.DriftVerticalRatio * F.DriftSigma * Out.DriftValue[1];
			Out.TremorRight = Out.Ns[3] * F.TremorSigma * Out.TremorValue[0];
			Out.TremorUp = Out.Ns[4] * F.TremorSigma * Out.TremorValue[1];

			// Per-shot channels (streak-guarded, truncated; 3.2).
			if (WithDraws)
			{
				Out.DrawTipA = DrawPerShot(Key, NoiseChannel::TipA, History, Params.StreakGuard);
				Out.DrawTipB = DrawPerShot(Key, NoiseChannel::TipB, History, Params.StreakGuard);
				Out.DrawElevation = DrawPerShot(Key, NoiseChannel::Elevation, History, Params.StreakGuard);
				Out.DrawSpeed = DrawPerShot(Key, NoiseChannel::Speed, History, Params.StreakGuard);
				Out.DrawFlinch = DrawPerShot(Key, NoiseChannel::Flinch, History, Params.StreakGuard);
				const double Fl = Out.Ns[9] * F.Pressure * Params.FlinchLoss * F.NerveScale * Out.DrawFlinch.U;
				const double Bias = -Out.Ns[9] * Params.GripDrop * F.Pressure * F.NerveScale;
				Out.TipAOffset = Ramp * (Out.Ns[5] * Out.DrawTipA.Eps * F.TipASigma);
				Out.TipBOffset = Ramp * (Out.Ns[6] * Out.DrawTipB.Eps * F.TipBSigma);
				Out.Bias = Ramp * Bias;
				Out.ElevationNoise = Ramp * (Out.Ns[7] * Out.DrawElevation.Eps * F.ElevationSigma);
				Out.SpeedGain = Ramp * (Out.Ns[8] * Out.DrawSpeed.Eps * F.SpeedSigma);
				Out.FlinchLoss = Ramp * Fl;
				Out.Factors.Flinch = Fl;
				Out.Factors.GripBias = Bias;
			}
			else
			{
				Out.Factors.Flinch = 0.0;
				Out.Factors.GripBias = 0.0;
			}

			// Warp (equipment: neither NoiseScale nor ChannelMask, 4.4).
			Out.Warp = ComputeWarp(CueBody, Cue, Key, Params);

			// Pivot geometry: grip right -> the cue rotates CCW about the bridge -> the tip moves left (UE 5.4, DERIVED).
			Out.Yaw = Out.GripLateral / Lbg;
			Out.Pitch = Out.GripVertical / Lbg;
			Out.Azimuth = Intended.Azimuth + Out.Yaw + Out.Warp.AzimuthError;
			Out.ElevationRaw = Intended.Elevation + Out.Pitch + Out.ElevationNoise + Out.Warp.ElevationError;
			Out.ElevationClamped = Out.ElevationRaw < Situation.ElevationFloor;
			Out.Elevation = Out.ElevationClamped ? Situation.ElevationFloor : Out.ElevationRaw;
			Out.AxisA = Intended.AxisOffsetA + (-Out.Yaw * Lbc + Out.TremorRight + Out.TipAOffset) / R;
			Out.AxisB = Intended.AxisOffsetB + (-Out.Pitch * Lbc + Out.TremorUp + Out.TipBOffset + Out.Bias) / R;
			Out.SpeedRaw = Out.IntendedSpeed * (1.0 + Out.SpeedGain) * (1.0 - Out.FlinchLoss);
			Out.Speed = Clamp(Out.SpeedRaw, 0.0, Params.MaxSpeed);
		}

		// Executed cue pose (3.6): dome centre, rim, cue frame. Axis offsets from the clamped contact offsets, so the dome touches
		// the ball exactly at the executed contact point.
		struct CuePose
		{
			CueFrame Frame;
			Vec3 RimCenter;      // centre of the tip rim (cap boundary), s = 0 of the body
			Vec3 DomeCenter;
		};

		CuePose MakeCuePose(double Elevation, double Azimuth, double OffsetA, double OffsetB, const Vec3& BallCenter, double R, const TipState& Tip)
		{
			CuePose Pose;
			Pose.Frame = MakeCueFrame(Elevation, Azimuth);
			const Vec3 ContactDir = CueContactPoint(Pose.Frame, OffsetA, OffsetB, 1.0); // unit: a e_r + b e_u - c d
			Pose.DomeCenter = BallCenter + ContactDir * (R + Tip.DomeRadius);
			const double HalfWidth = 0.5 * Tip.Width;
			const double RimDepth = Sqrt(Max(0.0, Tip.DomeRadius * Tip.DomeRadius - HalfWidth * HalfWidth));
			Pose.RimCenter = Pose.DomeCenter + Pose.Frame.Axis * RimDepth;
			return Pose;
		}

		// Lowest point of the tip rim, the dome (when its lowest point lies on the cap) and the shaft (tapered, linear in s, so
		// its ends) reaching below the cloth (3.6; UE 5.5 geometry, no margin).
		bool PoseTouchesCloth(const CuePose& Pose, double Elevation, const TipState& Tip, const CueBodyState& Body, const CueSpec& Cue)
		{
			const double CosTheta = Cos(Elevation);
			const double SinTheta = Sin(Elevation);
			const double HalfWidth = 0.5 * Tip.Width;
			double Lowest = Pose.RimCenter.z - HalfWidth * CosTheta;
			if (Tip.DomeRadius > 0.0)
			{
				const double CapHalfAngle = Asin(Min(1.0, HalfWidth / Tip.DomeRadius));
				if (Elevation >= 0.5 * kPi - CapHalfAngle)
				{
					Lowest = Min(Lowest, Pose.DomeCenter.z - Tip.DomeRadius);
				}
			}
			const double BodyLength = Cue.Length > 0.0 ? Cue.Length : 1.0;
			const double ShaftEnd = Min(Body.ShaftLength, BodyLength);
			const double RadiusAtEnd = Body.TipRadius + (Body.ButtRadius - Body.TipRadius) * ShaftEnd / BodyLength;
			Lowest = Min(Lowest, Pose.RimCenter.z - Body.TipRadius * CosTheta);
			Lowest = Min(Lowest, Pose.RimCenter.z + ShaftEnd * SinTheta - RadiusAtEnd * CosTheta);
			return Lowest < 0.0;
		}

		// Clearance of the tapered body P(s) = Rim - s d, r(s) = r_t + (r_b - r_t) s / L, s in [0, L], against one ball: the
		// minimum of |P(s) - P_j| - r(s) - R_j (convex in s) at s*; returns the clearance and s*.
		double BodyClearance(const CuePose& Pose, const CueBodyState& Body, double BodyLength, const BallObstacle& Ball, double& SStar)
		{
			const Vec3 W = Ball.Position - Pose.RimCenter;
			const double Along = Dot(Pose.Frame.Axis, W);
			const double S0 = -Along; // closest point of the axis line
			const double H = Sqrt(Max(0.0, LengthSquared(W) - Along * Along));
			const double Slope = (Body.ButtRadius - Body.TipRadius) / BodyLength;
			const double SlopeFactor = Sqrt(Max(1e-12, 1.0 - Slope * Slope));
			SStar = Clamp(S0 + Slope * H / SlopeFactor, 0.0, BodyLength);
			const double Ds = SStar - S0;
			return Sqrt(H * H + Ds * Ds) - (Body.TipRadius + Slope * SStar) - Ball.Radius;
		}

		NonTipSource SourceAt(double SStar, const CueBodyState& Body)
		{
			return SStar <= Body.FerruleLength ? NonTipSource::Ferrule : (SStar <= Body.ShaftLength ? NonTipSource::Shaft : NonTipSource::Butt);
		}

		bool HasCandidate(const FixedVector<NonTipContact, kMaxShaftContactCandidates>& List, BallId Ball)
		{
			for (const NonTipContact& Contact : List)
			{
				if (Contact.Ball == Ball)
				{
					return true;
				}
			}
			return false;
		}
	}

	SituationFactors ComputeSituationFactors(const IntendedStroke& Intended, const ShooterAttributes& Attributes, const StrokeSituation& Situation,
		const HumanParams& Params, double Time)
	{
		return ComputeSituationFactors(Intended, Attributes, Situation, Params, Time, kDefaultBallRadius);
	}

	SituationFactors ComputeSituationFactors(const IntendedStroke& Intended, const ShooterAttributes& Attributes, const StrokeSituation& Situation,
		const HumanParams& Params, double Time, double BallRadius)
	{
		SituationFactors F;
		const double Vi = EffectiveIntendedSpeed(Intended, Situation, Params);
		const double P = Clamp(Situation.Pressure, 0.0, 1.0);
		const double I = Clamp(Situation.Intoxication, 0.0, 1.0);
		const double Sweat = Clamp(Situation.Sweat, 0.0, 1.0);

		// Intoxication hook (Q2, 3.4): exactly neutral at I = 0.
		const double Ic = Params.IntoxicationCalmLevel;
		const double CalmShare = Ic > 0.0 ? Min(1.0, I / Ic) : (I > 0.0 ? 1.0 : 0.0);
		const double Excess = Ic < 1.0 ? Max(0.0, I - Ic) / (1.0 - Ic) : 0.0;
		F.Pressure = P * (1.0 - Params.IntoxicationCalm * CalmShare);
		F.IntoxicationDrift = 1.0 + Params.IntoxicationDriftGain * Excess;
		F.IntoxicationTremor = 1.0 + Params.IntoxicationTremorGain * Excess;

		F.NerveScale = SkillScale(Attributes.Nerve, Params.NerveRho);
		F.PressureGain = 1.0 + Params.PressureGainMax * F.Pressure * F.NerveScale;
		const BridgeSpec Bridge = BridgeSpecFor(Situation.Bridge);
		F.Bridge = 1.0 + (Bridge.BaseFactor - 1.0) * SkillScale(Attributes.BridgeStability, Params.BridgeRho);
		F.SlipSpeed = Bridge.SlipSpeed * (1.0 + Params.SlipSpeedSkillGain * (Clamp(Attributes.BridgeStability, 0.0, 100.0) - 25.0) / 75.0) *
			(Situation.Glove ? Params.GloveSlipSpeed : 1.0) * (1.0 - Params.SweatSlipSpeed * Sweat);
		F.Slip = F.SlipSpeed > 0.0 ? 1.0 + Params.SlipGain * Max(0.0, Vi - F.SlipSpeed) / F.SlipSpeed : 1.0;
		F.Stance = 1.0 + Clamp(Situation.StanceDifficulty, 0.0, 1.0) * SkillScale(Attributes.Stance, Params.StanceRho);
		F.Head = Intended.HeadMovedBeforeContact ? Params.HeadMoveFactor : 1.0;
		F.Stick = 1.0 + Params.SweatDrift * Sweat * (Situation.Glove ? Params.GloveSweat : 1.0);
		F.Fatigue = 1.0 + Params.FatigueGain * Clamp(Situation.Fatigue, 0.0, 1.0);
		F.OffHand = Situation.OffHand ? Params.OffHandFactor : 1.0;
		F.Rush = 1.0 + Params.RushGain * Clamp(1.0 - Intended.PauseDuration / Params.RushPause, 0.0, 1.0);
		F.Jab = 1.0 + Params.JabGain * Clamp(-Intended.ContactAcceleration / Params.JabAcceleration, 0.0, 1.0);
		F.SettleIn = SettleInEnvelope(Time, Params);
		F.Settle = SettleFactor(Time, Intended.SettleStart, Params);

		const double G = F.PressureGain;
		const double DriftGain = Pow(G, Params.DriftPressureExponent);
		const double SpeedGain = Params.SpeedPressureExponent == 0.5 ? Sqrt(G) : Pow(G, Params.SpeedPressureExponent);
		F.DriftSigma = Params.DriftSigma * SkillScale(Attributes.Steadiness, Params.DriftRho) * F.SettleIn * F.Settle * DriftGain * F.Bridge * F.Stance *
			F.Head * F.Stick * F.Slip * F.Fatigue * F.OffHand * F.IntoxicationDrift;
		F.TremorSigma = Params.TremorSigma * G * F.Settle * F.Fatigue * F.OffHand * F.IntoxicationTremor;
		const double SpinScaleA = SkillScale(Attributes.SpinTouch, Params.TipARho);
		const double SpinScaleB = SkillScale(Attributes.SpinTouch, Params.TipBRho);
		const double KappaScale = SkillScale(Attributes.SpinTouch, Params.OffsetKappaRho);
		F.TipASigma = Hypot2(Params.TipASigma * SpinScaleA, Params.OffsetKappa * KappaScale * Intended.AxisOffsetA * BallRadius) * F.Stance * F.Rush *
			F.Head * F.Fatigue * F.OffHand;
		F.TipBSigma = Hypot2(Params.TipBSigma * SpinScaleB, Params.OffsetKappa * KappaScale * Intended.AxisOffsetB * BallRadius) * F.Stance * F.Rush *
			F.Fatigue * F.OffHand;
		F.ElevationSigma = Params.ElevationSigma * SkillScale(Attributes.SpinTouch, Params.ElevationRho) * F.Bridge * F.Stance * F.Slip * F.Fatigue * F.OffHand;
		const double SoftShot = Params.SoftShotSpeed > 0.0 ? 1.0 + Params.SoftShotGain * Max(0.0, 1.0 - Vi / Params.SoftShotSpeed) : 1.0;
		F.SpeedSigma = Params.SpeedSigma * SkillScale(Attributes.SpeedControl, Params.SpeedRho) * SoftShot * F.Rush * F.Jab * SpeedGain * F.Fatigue * F.OffHand;
		F.Flinch = 0.0;
		F.GripBias = -Params.GripDrop * F.Pressure * F.NerveScale;
		return F;
	}

	double SettleInEnvelope(double Time, const HumanParams& Params)
	{
		const double T = Max(0.0, Time);
		return 1.0 + Exp(-T / Params.SettleInTime) + Min(Params.LongHoldMax, Params.LongHoldRate * Max(0.0, T - Params.LongHoldStart));
	}

	double SettleFactor(double Time, double SettleStart, const HumanParams& Params)
	{
		if (SettleStart < 0.0)
		{
			return 1.0;
		}
		const double Tau = Time - SettleStart;
		if (Tau < 0.0)
		{
			return 1.0;
		}
		if (Tau < Params.SettleRamp)
		{
			return 1.0 - (1.0 - Params.SettleFloor) * SmoothStep01(Tau / Params.SettleRamp);
		}
		const double HoldEnd = Params.SettleRamp + Params.SettleHold;
		if (Tau < HoldEnd)
		{
			return Params.SettleFloor;
		}
		return Params.SettleFloor + (Params.SettleAfter - Params.SettleFloor) * SmoothStep01((Tau - HoldEnd) / Params.SettleRelease);
	}

	ExecutedStroke ExecuteStroke(const IntendedStroke& Intended, const ShooterAttributes& Attributes, const StrokeSituation& Situation,
		const TipState& Tip, const CueBodyState& CueBody, const CueSpec& Cue, const BallSpec& CueBall, const Vec3& CueBallPosition,
		const BallObstacle* OtherBalls, int OtherBallCount, const NoiseKey& Key, const NoiseHistory& History, const HumanParams& Params,
		const TipParams& TipModel)
	{
		ExecutedStroke Result;
		Result.Strike.Cue = Cue;
		const bool BallsOk = OtherBallCount >= 0 && (OtherBallCount == 0 || OtherBalls != nullptr);
		const bool PositionOk = Finite(CueBallPosition.x) && Finite(CueBallPosition.y) && Finite(CueBallPosition.z);
		const bool TipOk = Finite(Tip.DomeRadius) && Tip.DomeRadius > 0.0 && Finite(Tip.Width) && Tip.Width > 0.0;
		if (!BallsOk || !PositionOk || !TipOk || !StrokeInputsValid(Intended, Attributes, Situation, CueBall, Params))
		{
			Result.Error = ErrorCode::InvalidArgument;
			return Result;
		}

		const double R = CueBall.Radius;
		StrokeEvaluation E;
		EvaluateStroke(Intended, Attributes, Situation, CueBody, Cue, CueBall, Key, History, Params, Intended.TimeDown, 1.0, true, E);

		// Tip velocity at contact (lever = bridge-to-tip L_b; output only, HF-11).
		const SituationFactors& F = E.Factors;
		const double Lb = Situation.BridgeLength;
		const double Lbg = Situation.BridgeToGrip;
		const double Lbc = Lb + R;
		const double T = Intended.TimeDown;
		const double DriftRateLat = ProcessRate(E.Drift[0], T);
		const double DriftRateVert = ProcessRate(E.Drift[1], T);
		const double TremorRateLat = ProcessRate(E.Tremor[0], T);
		const double TremorRateVert = ProcessRate(E.Tremor[1], T);
		Result.TipTransverseVelocity.x = Intended.TipVelocityRight - E.Ns[1] * F.DriftSigma * DriftRateLat * Lb / Lbg + E.Ns[3] * F.TremorSigma * TremorRateLat;
		Result.TipTransverseVelocity.y = Intended.TipVelocityUp - E.Ns[2] * Params.DriftVerticalRatio * F.DriftSigma * DriftRateVert * Lb / Lbg +
			E.Ns[4] * F.TremorSigma * TremorRateVert;
		Result.AxisOffset = {E.AxisA, E.AxisB};

		// Breakdown (sum = executed change before the floor and the clamps).
		StrokeBreakdown& B = Result.Channels;
		StrokeDelta& Drift = B.Sources[static_cast<int>(StrokeSource::Drift)];
		Drift.Azimuth = E.Yaw;
		Drift.Elevation = E.Pitch;
		Drift.AxisA = (-E.Yaw * Lbc) / R;
		Drift.AxisB = (-E.Pitch * Lbc) / R;
		StrokeDelta& Tremor = B.Sources[static_cast<int>(StrokeSource::Tremor)];
		Tremor.AxisA = E.TremorRight / R;
		Tremor.AxisB = E.TremorUp / R;
		StrokeDelta& Placement = B.Sources[static_cast<int>(StrokeSource::TipPlacement)];
		Placement.AxisA = E.TipAOffset / R;
		Placement.AxisB = E.TipBOffset / R;
		B.Sources[static_cast<int>(StrokeSource::Elevation)].Elevation = E.ElevationNoise;
		B.Sources[static_cast<int>(StrokeSource::Speed)].Speed = E.IntendedSpeed * E.SpeedGain;
		B.Sources[static_cast<int>(StrokeSource::Flinch)].Speed = -E.IntendedSpeed * (1.0 + E.SpeedGain) * E.FlinchLoss;
		B.Sources[static_cast<int>(StrokeSource::GripTension)].AxisB = E.Bias / R;
		StrokeDelta& Warp = B.Sources[static_cast<int>(StrokeSource::Warp)];
		Warp.Azimuth = E.Warp.AzimuthError;
		Warp.Elevation = E.Warp.ElevationError;
		B.DriftLat = E.DriftValue[0];
		B.DriftVert = E.DriftValue[1];
		B.TremorLat = E.TremorValue[0];
		B.TremorVert = E.TremorValue[1];
		B.TipA = E.DrawTipA;
		B.TipB = E.DrawTipB;
		B.Elevation = E.DrawElevation;
		B.Speed = E.DrawSpeed;
		B.Flinch = E.DrawFlinch;
		B.Warp = E.Warp;
		B.Factors = E.Factors;

		// 3.6: contact offsets with the CURRENT dome radius, the rho clamp, the chalk map.
		const Vec2 Contact = AimToContactOffset({E.AxisA, E.AxisB}, R, Tip.DomeRadius);
		double A = Contact.x;
		double Bo = Contact.y;
		double Rho = Hypot2(A, Bo);
		if (Rho > Params.OffsetClamp)
		{
			A = A * Params.OffsetClamp / Rho;
			Bo = Bo * Params.OffsetClamp / Rho;
			Rho = Params.OffsetClamp;
			Result.OffsetClamped = true;
		}
		Result.Contact = LookupTipContact(Tip, A, Bo, TipModel);
		const double Mu = Result.Contact.Friction;
		Result.Rho = Rho;
		Result.MiscueLimit = MiscueLimit(Mu);
		Result.PredictedMiscue = Rho > Result.MiscueLimit;
		Result.ElevationClamped = E.ElevationClamped;

		CueStrikeInput& Strike = Result.Strike;
		Strike.Speed = E.Speed;
		Strike.Elevation = E.Elevation;
		Strike.Azimuth = E.Azimuth;
		Strike.OffsetA = A;
		Strike.OffsetB = Bo;
		Strike.Cue.TipFriction = Mu;
		Strike.Cue.TipFrictionKinetic = Mu;
		Strike.Cue.TipDomeRadius = Tip.DomeRadius;
		Strike.Cue.TipDiameter = Tip.Width;
		Strike.Cue.TipRestitution = EffectiveTipRestitution(Tip, TipModel);

		// Executed pose: scoop flag and shaft clearance (UE 5.5 geometry).
		const CuePose Pose = MakeCuePose(E.Elevation, E.Azimuth, A, Bo, CueBallPosition, R, Tip);
		Strike.TipTouchesCloth = PoseTouchesCloth(Pose, E.Elevation, Tip, CueBody, Cue);
		const double BodyLength = Cue.Length > 0.0 ? Cue.Length : 1.0;
		for (int i = 0; i < OtherBallCount; ++i)
		{
			double SStar = 0.0;
			if (BodyClearance(Pose, CueBody, BodyLength, OtherBalls[i], SStar) < 0.0 && !HasCandidate(Result.ShaftContactCandidates, OtherBalls[i].Id))
			{
				Result.ShaftContactCandidates.PushBack({OtherBalls[i].Id, SourceAt(SStar, CueBody), 0.0});
			}
		}
		if (E.ElevationClamped && Situation.FloorBy == FloorSource::Ball && Situation.FloorBall != kNoBall &&
			!HasCandidate(Result.ShaftContactCandidates, Situation.FloorBall))
		{
			NonTipSource Source = NonTipSource::Shaft;
			for (int i = 0; i < OtherBallCount; ++i)
			{
				if (OtherBalls[i].Id == Situation.FloorBall)
				{
					double SStar = 0.0;
					BodyClearance(Pose, CueBody, BodyLength, OtherBalls[i], SStar);
					Source = SourceAt(SStar, CueBody);
					break;
				}
			}
			Result.ShaftContactCandidates.PushBack({Situation.FloorBall, Source, 0.0});
		}

		// Double hit / push risk from the first ball in the cue ball's corridor (RUL F7 / F8).
		const Vec2 Direction{Cos(E.Azimuth), Sin(E.Azimuth)};
		double BestTravel = kInfinity;
		double Gap = kInfinity;
		double CutAngle = 0.0;
		for (int i = 0; i < OtherBallCount; ++i)
		{
			const BallObstacle& Ball = OtherBalls[i];
			const Vec2 W = XY(Ball.Position) - XY(CueBallPosition);
			const double Along = Dot(W, Direction);
			const double Perp = Abs(Cross(Direction, W));
			const double RadiusSum = R + Ball.Radius;
			if (!(Along > 0.0) || !(Perp < RadiusSum))
			{
				continue;
			}
			const double Travel = Along - Sqrt(RadiusSum * RadiusSum - Perp * Perp);
			if (Travel < BestTravel)
			{
				BestTravel = Travel;
				Gap = Length(Ball.Position - CueBallPosition) - RadiusSum;
				CutAngle = Asin(Min(1.0, Perp / RadiusSum));
			}
		}
		const RulesTolerances& Rules = Params.Rules;
		const bool MarginNegative = SeparationMargin(Rho, Strike.Cue.TipRestitution, CueBall.Mass, Cue.Mass, InertiaFactor(CueBall)) < 0.0;
		Result.DoubleHitRisk = MarginNegative || (Gap > Rules.Frozen && Gap < Cue.FollowThroughDistance && CutAngle < Rules.GrazeAngle);
		Result.PushRisk = Gap > Rules.Frozen && Gap <= Rules.FrozenEnvelope;
		Result.Error = ErrorCode::Ok;
		return Result;
	}

	HandPose SampleHand(const IntendedStroke& Intended, const ShooterAttributes& Attributes, const StrokeSituation& Situation,
		const CueBodyState& CueBody, const CueSpec& Cue, const BallSpec& CueBall, const NoiseKey& Key, const NoiseHistory& History,
		const HumanParams& Params, double Time)
	{
		HandPose Pose;
		Pose.Time = Time;
		if (!Finite(Time) || !StrokeInputsValid(Intended, Attributes, Situation, CueBall, Params))
		{
			return Pose;
		}
		const double Ramp = Params.RampDuration > 0.0 ? SmoothStep01((Time - Intended.ForwardStart) / Params.RampDuration) : (Time >= Intended.ForwardStart ? 1.0 : 0.0);
		StrokeEvaluation E;
		EvaluateStroke(Intended, Attributes, Situation, CueBody, Cue, CueBall, Key, History, Params, Time, Ramp, Ramp > 0.0, E);
		const double R = CueBall.Radius;
		Pose.GripLateral = E.GripLateral;
		Pose.GripVertical = E.GripVertical;
		Pose.TremorRight = E.TremorRight;
		Pose.TremorUp = E.TremorUp;
		Pose.Ramp = Ramp;
		Pose.RampShown = Ramp > 0.0;
		Pose.PerShot.Elevation = E.ElevationNoise;
		Pose.PerShot.AxisA = E.TipAOffset / R;
		Pose.PerShot.AxisB = (E.TipBOffset + E.Bias) / R;
		Pose.PerShot.Speed = E.SpeedRaw - E.IntendedSpeed;
		Pose.Warp = E.Warp;
		Pose.Azimuth = E.Azimuth;
		Pose.Elevation = E.Elevation;
		Pose.AxisOffsetA = E.AxisA;
		Pose.AxisOffsetB = E.AxisB;
		return Pose;
	}

	DiagnosisStep DiagnosisStepAt(int Index)
	{
		DiagnosisStep Step;
		switch (Index)
		{
		case 0: Step.Cause = MissCause::Equipment; Step.FreshChalkNoWarp = true; break;
		case 1: Step.Cause = MissCause::Table; Step.LevelCleanTable = true; break;
		case 2: Step.Cause = MissCause::HandDrift; Step.ChannelMask = kWatchableChannelMask; break;
		case 3: Step.Cause = MissCause::TipPlacementSpeed; Step.ChannelMask = kPerShotChannelMask; break;
		case 4: Step.Cause = MissCause::HumanLayer; Step.ChannelMask = kWatchableChannelMask | kPerShotChannelMask; break;
		default: break; // Input: nothing to change
		}
		return Step;
	}

	void ApplyDiagnosisStep(const DiagnosisStep& Step, HumanParams& Human, TipState& Tip, CueBodyState& CueBody)
	{
		if (Step.FreshChalkNoWarp)
		{
			for (int z = 0; z < kTipZoneCount; ++z)
			{
				Tip.Coverage[z] = 1.0;
			}
			CueBody.BowSag = 0.0;
		}
		Human.ChannelMask |= Step.ChannelMask;
	}

	void ApplyDiagnosisStep(const DiagnosisStep& Step, PhysicsParams& Physics, SimBall* Balls)
	{
		if (!Step.LevelCleanTable)
		{
			return;
		}
		Physics.Tilt.Slope = {0.0, 0.0};
		Physics.Tilt.NapPseudoSlope = {0.0, 0.0};
		Physics.BallBall.ClingFactor = 1.0;
		Physics.ChalkCling = false;
		if (Balls != nullptr)
		{
			for (int i = 0; i < kMaxBalls; ++i)
			{
				Balls[i].ChalkMarks.Clear();
			}
		}
	}

	StrokeShares ComputeStrokeShares(const ExecutedStroke& Stroke, const StrokeDelta& InputError, const CueSpec& Cue, const BallSpec& CueBall)
	{
		// Budget model of 3.10, linearised at a = 0: CB direction error = d_phi + (d alpha_sq / da)|_0 d_a with
		// d alpha_sq / da|_0 = (1/k) / (1 + m / m_e + 1/k) (MOT B.7) and d_a = d_A R / (R + r_dome).
		StrokeShares Shares;
		const double K = InertiaFactor(CueBall);
		const double InverseK = K > 0.0 ? 1.0 / K : 0.0;
		const double MassRatio = Cue.EndMass > 0.0 ? CueBall.Mass / Cue.EndMass : 0.0;
		const double SquirtSlope = InverseK / (1.0 + MassRatio + InverseK);
		const double Dome = Stroke.Strike.Cue.TipDomeRadius > 0.0 ? Stroke.Strike.Cue.TipDomeRadius : Cue.TipDomeRadius;
		const double ContactScale = CueBall.Radius / (CueBall.Radius + Dome);
		const auto DirectionError = [&](const StrokeDelta& Delta) { return Delta.Azimuth + SquirtSlope * Delta.AxisA * ContactScale; };

		StrokeDelta Hand;
		for (int s = 0; s < kStrokeSourceCount; ++s)
		{
			if (s == static_cast<int>(StrokeSource::Warp))
			{
				continue;
			}
			Hand.Azimuth += Stroke.Channels.Sources[s].Azimuth;
			Hand.AxisA += Stroke.Channels.Sources[s].AxisA;
		}
		const double InputMagnitude = Abs(DirectionError(InputError));
		const double HandMagnitude = Abs(DirectionError(Hand));
		const double EquipmentMagnitude = Abs(DirectionError(Stroke.Channels.Sources[static_cast<int>(StrokeSource::Warp)]));
		const double Total = InputMagnitude + HandMagnitude + EquipmentMagnitude;
		if (!(Total > 0.0) || !IsFinite(Total))
		{
			return Shares;
		}
		Shares.Input = InputMagnitude / Total;
		Shares.Hand = HandMagnitude / Total;
		Shares.Equipment = EquipmentMagnitude / Total;
		return Shares;
	}

	void ApplyShotToEquipment(const ExecutedStroke& Stroke, const ShotResult& Result, int StrikeIndex, const Quat& StruckBallOrientation,
		TipState& Tip, BallChalkMarks* Marks, const TipParams& TipModel, const MarkParams& MarkModel)
	{
		const int StrikeCount = Result.Strikes.Size();
		if (StrikeIndex >= 0 && StrikeIndex < StrikeCount)
		{
			const StrikeOutcome& Outcome = Result.Strikes[StrikeIndex];
			const bool Miscue = Outcome.Result.Miscue; // the core's criterion, never the prediction (HF-B03)
			ApplyTipWear(Tip, Stroke.Contact, HitSeverity(Stroke.Strike.Speed, Stroke.Rho, Miscue), TipModel);
			if (Marks != nullptr && Outcome.Ball >= 0 && Outcome.Ball < kMaxBalls)
			{
				const CueFrame Frame = MakeCueFrame(Stroke.Strike.Elevation, Stroke.Strike.Azimuth);
				const Vec3 ContactDir = CueContactPoint(Frame, Stroke.Strike.OffsetA, Stroke.Strike.OffsetB, 1.0);
				DepositChalkMark(Marks[Outcome.Ball], StruckBallOrientation, ContactDir, Stroke.Contact.Coverage, Miscue, MarkModel);
			}
		}
		const bool LastStrike = StrikeCount == 0 || StrikeIndex == StrikeCount - 1;
		if (Marks != nullptr && LastStrike)
		{
			for (int Ball = 0; Ball < kMaxBalls; ++Ball)
			{
				if (((Result.BallsInPlay >> Ball) & 1u) == 0u || Marks[Ball].IsEmpty())
				{
					continue;
				}
				const TravelDistances Travel = ComputeTravelDistances(Result, Ball);
				FadeChalkMarks(Marks[Ball], Travel.Slide, Travel.Roll, MarkModel);
			}
		}
	}
}
