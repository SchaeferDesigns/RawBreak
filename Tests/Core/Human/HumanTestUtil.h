#pragma once

// Owner: WP-11 (player model). Shared setup of the human-factors tests: the common stroke setup S0 of human-factors 6 and
// the budget model of 3.10 (HF-S04..S06). Expected values come from Tools/reference/human-factors/recompute_v12.py
// (streak-guarded draws), noise.py, streak.py, chalk.py.

#include "rbtest.h"

#include "rb/Core/Constants.h"
#include "rb/Equipment/Cue.h"
#include "rb/Human/HumanModel.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Human/Skill.h"
#include "rb/Human/TipState.h"
#include "rb/Math/Scalar.h"
#include "rb/Physics/BallState.h"

#include <cmath>
#include <cstdint>
#include <cstring>

// Relative tolerance check (human-factors 6: process and stroke values 1e-9 relative).
#define RB_CHECK_REL(A, B, Rel) RB_CHECK_NEAR(A, B, (Rel) * std::fabs(static_cast<double>(B)))

namespace rb::human::testhelp
{
	inline constexpr double kR = 0.028575;

	// Common stroke setup S0 (human-factors 6).
	struct Setup
	{
		IntendedStroke Intended;
		ShooterAttributes Attributes;
		StrokeSituation Situation;
		TipState Tip;
		CueBodyState CueBody;
		CueSpec Cue = kCuePlaying19oz;
		BallSpec Ball;
		Vec3 BallPosition{0.0, 0.0, kR};
		NoiseKey Key;
		NoiseHistory History;
		HumanParams Params;
	};

	inline NoiseKey MakeKey(std::uint64_t Seed, std::uint32_t Rack, std::uint32_t Shot, std::uint32_t Shooter, std::uint32_t ShooterShot)
	{
		NoiseKey Key;
		Key.MatchSeed = Seed;
		Key.RackIndex = Rack;
		Key.ShotIndex = Shot;
		Key.ShooterId = Shooter;
		Key.ShooterShotIndex = ShooterShot;
		return Key;
	}

	inline Setup MakeS0()
	{
		Setup S;
		S.Intended.Azimuth = 0.0;
		S.Intended.Elevation = 3.0 * kDegToRad;
		S.Intended.AxisOffsetA = 0.0;
		S.Intended.AxisOffsetB = 0.0;
		S.Intended.Speed = 2.0;
		S.Intended.TimeDown = 2.5;
		S.Intended.ForwardStart = 2.2; // the per-shot ramp is complete at t_c (rendering only)
		S.Intended.SettleStart = -1.0;
		S.Intended.PauseDuration = 0.4;
		S.Intended.ContactAcceleration = 0.0;
		S.Intended.HeadMovedBeforeContact = false;
		S.Ball = MakeBallSpec(kR, kDefaultBallMass);
		S.Key = MakeKey(0x5EEDu, 0u, 0u, 1u, 0u);
		S.History = RebuildNoiseHistory(S.Key.MatchSeed, ShooterKey(S.Key), S.Key.ShooterShotIndex);
		return S;
	}

	inline ExecutedStroke Execute(const Setup& S, const BallObstacle* Others = nullptr, int OtherCount = 0)
	{
		return ExecuteStroke(S.Intended, S.Attributes, S.Situation, S.Tip, S.CueBody, S.Cue, S.Ball, S.BallPosition, Others, OtherCount, S.Key, S.History,
			S.Params);
	}

	inline HandPose Sample(const Setup& S, double Time)
	{
		return SampleHand(S.Intended, S.Attributes, S.Situation, S.CueBody, S.Cue, S.Ball, S.Key, S.History, S.Params, Time);
	}

	// Budget model of 3.10 (oracle budget.py): the net cue-ball direction error phi_x - phi_i + alpha_sq(a) with the TP A.31
	// squirt of a solid ball (1/k = 2.5) for m / m_e = MassRatio; contact offsets (a, b) = (A_x, B_x) R / (R + r_dome), rho
	// clamped at 0.9 like 3.6. Self-contained (the model of the spec, not the core's squirt), so the budget tests exercise
	// ExecuteStroke alone.
	inline double BudgetSquirt(double A, double MassRatio)
	{
		return std::atan2(2.5 * A * std::sqrt(1.0 - A * A), 1.0 + MassRatio + 2.5 * (1.0 - A * A));
	}

	struct ContactOffsets
	{
		double A = 0.0;
		double B = 0.0;
		double Rho = 0.0;
	};

	inline ContactOffsets BudgetContact(const ExecutedStroke& Stroke, double R, double DomeRadius)
	{
		ContactOffsets C;
		C.A = Stroke.AxisOffset.x * R / (R + DomeRadius);
		C.B = Stroke.AxisOffset.y * R / (R + DomeRadius);
		C.Rho = std::hypot(C.A, C.B);
		if (C.Rho > 0.9)
		{
			C.A = C.A * 0.9 / C.Rho;
			C.B = C.B * 0.9 / C.Rho;
			C.Rho = 0.9;
		}
		return C;
	}

	inline bool BudgetPotMissed(double Error, double R)
	{
		return std::fabs(Error) * 1.0 / (2.0 * R * std::cos(30.0 * kDegToRad)) > 2.0 * kDegToRad;
	}

	// Bit pattern of a double for bitwise comparisons and hashes.
	inline std::uint64_t Bits(double X)
	{
		std::uint64_t B = 0;
		std::memcpy(&B, &X, sizeof(B));
		return B;
	}

	inline bool SameBits(double A, double B) { return Bits(A) == Bits(B); }

	inline bool SameStrike(const CueStrikeInput& A, const CueStrikeInput& B)
	{
		return SameBits(A.Speed, B.Speed) && SameBits(A.Elevation, B.Elevation) && SameBits(A.Azimuth, B.Azimuth) && SameBits(A.OffsetA, B.OffsetA) &&
			SameBits(A.OffsetB, B.OffsetB) && SameBits(A.Cue.TipFriction, B.Cue.TipFriction) && SameBits(A.Cue.TipFrictionKinetic, B.Cue.TipFrictionKinetic) &&
			SameBits(A.Cue.TipDomeRadius, B.Cue.TipDomeRadius) && SameBits(A.Cue.TipRestitution, B.Cue.TipRestitution) &&
			SameBits(A.Cue.TipDiameter, B.Cue.TipDiameter) && SameBits(A.Cue.Mass, B.Cue.Mass) && SameBits(A.Cue.EndMass, B.Cue.EndMass) &&
			SameBits(A.LambdaOverride, B.LambdaOverride) && A.SquirtEnabled == B.SquirtEnabled && A.TipTouchesCloth == B.TipTouchesCloth;
	}

	// Hash of everything ExecuteStroke computes itself (the WP-1 contact mapping excluded), for bitwise replay checks.
	inline std::uint64_t StrokeHash(const ExecutedStroke& S)
	{
		std::uint64_t H = HashKeys(Bits(S.Strike.Speed), Bits(S.Strike.Elevation), Bits(S.Strike.Azimuth), Bits(S.AxisOffset.x), Bits(S.AxisOffset.y),
			Bits(S.TipTransverseVelocity.x), Bits(S.TipTransverseVelocity.y));
		for (int s = 0; s < kStrokeSourceCount; ++s)
		{
			const StrokeDelta& D = S.Channels.Sources[s];
			H = HashKeys(H, Bits(D.Azimuth), Bits(D.Elevation), Bits(D.AxisA), Bits(D.AxisB), Bits(D.Speed));
		}
		H = HashKeys(H, Bits(S.Channels.TipA.U), Bits(S.Channels.TipB.U), Bits(S.Channels.Elevation.U), Bits(S.Channels.Speed.U), Bits(S.Channels.Flinch.U));
		return H;
	}
}
