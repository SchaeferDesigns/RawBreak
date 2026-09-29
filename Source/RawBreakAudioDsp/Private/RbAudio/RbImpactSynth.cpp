#include "RbAudio/RbImpactSynth.h"

// Owner: M2-C. Stub of the M2 architect step.

namespace RbAudio
{
	FContactPulse HertzPulse(double NormalSpeed, double EffectiveMass, double Stiffness, double /*Restitution*/)
	{
		FContactPulse Pulse;
		if (NormalSpeed <= 0.0 || EffectiveMass <= 0.0 || Stiffness <= 0.0)
		{
			return Pulse;
		}
		// Elastic Hertz contact F = K delta^1.5: delta_max = (5 m* v^2 / (4 K))^(2/5), T = 2.9432 (m* / K)^(2/5) v^(-1/5).
		const double DeltaMax = FMath::Pow(5.0 * EffectiveMass * NormalSpeed * NormalSpeed / (4.0 * Stiffness), 0.4);
		Pulse.Duration = 2.9432 * FMath::Pow(EffectiveMass / Stiffness, 0.4) * FMath::Pow(NormalSpeed, -0.2);
		Pulse.PeakForce = Stiffness * FMath::Pow(DeltaMax, 1.5);
		Pulse.Impulse = 2.0 * EffectiveMass * NormalSpeed; // TODO(M2-C): (1 + e) m* v with the Tsuji damping
		return Pulse;
	}

	void FModalBank::Initialize(double InSampleRate, TArrayView<const FMode> InModes, double RadiationHighPassHz)
	{
		SampleRate = InSampleRate;
		Modes = TArray<FMode>(InModes.GetData(), InModes.Num());
		HighPassHz = RadiationHighPassHz;
	}

	void FModalBank::Excite(const FContactPulse& /*Pulse*/, double /*FractionalStart*/, TArrayView<float> /*Out*/) const
	{
		// TODO(M2-C): biquad resonators (impulse-invariant), radiation high-pass (audio.md 3.5).
	}

	void FImpactRenderer::Initialize(double InSampleRate)
	{
		SampleRate = InSampleRate;
		// TODO(M2-C): contact shape table, decimation FIR, per-order ball kernels (Lamb roots), cloth image IIR.
	}

	int32 FImpactRenderer::Render(const FImpactEvent& /*Event*/, double /*EventFrame*/, int64 /*BufferStartFrame*/, TArrayView<float> /*Out*/) const
	{
		return 0; // TODO(M2-C): audio.md 3.6 steps 0-5
	}
}
