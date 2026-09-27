#include "rb/Core/FpGuard.h"
// Owner: WP-2 (equipment & table geometry). Spec: equipment 6 (6.3 DIVE_BAR sampling, 6.4 other standards),
// rules.md 12.4 (Blackball set).
#include "rb/Equipment/BallSets.h"

#include "rb/Core/Random.h"
#include "rb/Equipment/EquipmentConstants.h"
#include "rb/Math/Scalar.h"

namespace rb
{
	namespace
	{
		void Fill(BallSet& Out, int First, int Last, const BallSpec& Spec)
		{
			for (int i = First; i <= Last; ++i)
			{
				Out.Balls[i] = Spec;
			}
		}

		// Worn bar object balls 1..15 (equipment 6.3): per ball, in id order, first the mass
		// m = clamp(0.163 + 0.003 N(0, 1), 0.155, 0.167) kg (rb::Rng::NextNormal), then the diameter
		// D ~ U[57.00, 57.15) mm. The stream is the same for DiveBar and OldBarOversizedCue, so both presets
		// give identical object balls for the same seed.
		void FillBarObjectBalls(BallSet& Out, std::uint64_t Seed)
		{
			Rng Stream(Seed);
			for (int i = 1; i < kPoolBallCount; ++i)
			{
				const double Mass = Clamp(kBarObjectBallMassMean + kBarObjectBallMassSigma * Stream.NextNormal(), kBarObjectBallMassMin, kBarObjectBallMassMax);
				const double Diameter = Stream.NextUniform(kBarObjectBallDiameterMin, kBallDiameter);
				Out.Balls[i] = MakeBallSpec(0.5 * Diameter, Mass);
			}
		}
	}

	ErrorCode BuildBallSet(BallSetPreset Preset, std::uint64_t Seed, BallSet& Out)
	{
		Out = BallSet{};
		switch (Preset)
		{
		case BallSetPreset::StandardPool:
			Out.Count = kPoolBallCount;
			Fill(Out, 0, kPoolBallCount - 1, kStandardPoolBall);
			return ErrorCode::Ok;
		case BallSetPreset::DiveBar:
			Out.Count = kPoolBallCount;
			Out.Balls[kCueBallId] = kMagneticCueBall;
			FillBarObjectBalls(Out, Seed);
			return ErrorCode::Ok;
		case BallSetPreset::OldBarOversizedCue:
			Out.Count = kPoolBallCount;
			Out.Balls[kCueBallId] = kOversizedCueBall;
			FillBarObjectBalls(Out, Seed);
			return ErrorCode::Ok;
		case BallSetPreset::Snooker:
			Out.Count = 22; // 15 reds + 6 colours + the cue ball
			Fill(Out, 0, 21, MakeBallSpec(0.5 * kSnookerBallDiameter, kSnookerBallMass));
			return ErrorCode::Ok;
		case BallSetPreset::Blackball:
			Out.Count = kPoolBallCount;
			Out.Balls[kCueBallId] = MakeBallSpec(0.5 * kBlackballCueBallDiameter, kBlackballCueBallMass);
			Fill(Out, 1, kPoolBallCount - 1, MakeBallSpec(0.5 * kBlackballObjectBallDiameter, kBlackballObjectBallMass));
			return ErrorCode::Ok;
		}
		return ErrorCode::InvalidArgument;
	}
}
