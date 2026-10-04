#pragma once

// The contact force of every synthesised impact (Docs/specs/audio.md 3.2): the core's Hertz + Tsuji law
//   m* d'' = -F,  F = max(0, K d^1.5 + eta d^0.25 d'),  eta = alpha_T(e) sqrt(m* K)
// is self-similar: with d0 = (m* v^2 / K)^0.4 and t0 = (m*^2 / (K^2 v))^0.2 every pulse is ONE curve per restitution e,
//   T = tau_e t0,  F_max = phi_e K d0^1.5,  F(t) = F_max shape(t / T),  J = (1 + e) m* v.
// The runtime evaluates the shape table at 4 x fs from the fractional start and decimates with the 129-tap Kaiser FIR
// (audio.md 3.6 step 1): the sub-sample start time is applied exactly here. C++ port of click_synth.py (_integrate_hertz,
// alpha_for_restitution, hertz_shape, runtime_force). Owner: M2-C.

#include "CoreMinimal.h"

namespace RbAudio
{
	// Force grid of the exact integration (click_synth.py FS_HI = 48 kHz x 16), independent of the device rate.
	inline constexpr double HertzGridRate = 48000.0 * 16.0;

	struct FHertzIntegration
	{
		double Duration = 0.0;   // contact time [s] (a multiple of the RK4 step)
		double ExitSpeed = 0.0;  // separation speed [m/s] (restitution = ExitSpeed / v)
		double PeakForce = 0.0;  // [N]
		double Impulse = 0.0;    // integral F dt on the HertzGridRate grid [N s]
		TArray<double> Forces;   // F on the HertzGridRate grid (only with bKeepForces)
	};

	// RK4 of the core law at Sub x HertzGridRate (click_synth.py _integrate_hertz).
	RAWBREAKAUDIODSP_API FHertzIntegration IntegrateHertz(double V, double MStar, double K, double Alpha, int32 Sub = 4, double MaxTime = 0.05,
		bool bKeepForces = false);

	// Tsuji alpha_T for restitution e (speed independent for the d^1.5 / d^0.25 pair); 0 for e >= 0.9999 (AU-T01).
	RAWBREAKAUDIODSP_API double AlphaForRestitution(double E);

	struct FContactShape
	{
		double Restitution = 0.95;
		double Alpha = 0.0;
		double Tau = 0.0;       // dimensionless contact time
		double Phi = 0.0;       // dimensionless peak force
		double Area = 0.0;      // integral of the normalised shape over u in [0, 1]
		TArray<double> Shape;   // shape(u) on a uniform grid over [0, 1], peak 1

		// Linear interpolation on the uniform grid (numpy.interp); 0 outside [0, 1].
		RAWBREAKAUDIODSP_API double Eval(double U) const;
	};

	// Integrates the dimensionless contact x'' = -(x^1.5 + alpha x^0.25 x') (click_synth.py hertz_shape; step 2e-5 as the
	// golden data, AU-T04).
	RAWBREAKAUDIODSP_API FContactShape ComputeContactShape(double E, int32 Points = 129, double Step = 2e-5);

	using FContactShapePtr = TSharedPtr<const FContactShape, ESPMode::ThreadSafe>;
	// Cached shape (thread-safe). e = 0.95 (ball-ball) is computed exactly like the golden data (step 2e-5); other restitutions
	// are quantised to 0.01 and integrated with step 1e-4 (ESTIMATE contacts: cushions, slate, liner, floor).
	RAWBREAKAUDIODSP_API FContactShapePtr GetContactShape(double E);
	// Restitution the cache uses for E.
	RAWBREAKAUDIODSP_API double QuantizeRestitution(double E);

	struct FContactPulse
	{
		double Duration = 0.0;  // T [s]
		double PeakForce = 0.0; // F_max [N]
		double Impulse = 0.0;   // J [N s]
	};

	// Runtime pulse constants of a Hertz + Tsuji contact (the shape table's tau / phi; J = (1 + e) m* v).
	RAWBREAKAUDIODSP_API FContactPulse HertzPulse(double NormalSpeed, double EffectiveMass, double Stiffness, double Restitution);
	RAWBREAKAUDIODSP_API FContactPulse HertzPulse(const FContactShape& Shape, double NormalSpeed, double EffectiveMass, double Stiffness);

	// K such that the undamped Hertz contact time at V is T1 (T = 3.2181 (m*^2 / (K^2 v))^(1/5), physics 3.9.3).
	RAWBREAKAUDIODSP_API double StiffnessForContactTime(double T1, double MStar, double V = 1.0);
	// Core e_c(v_perp) law of the cushion (Cushion.h): 0.97 up to 1 m/s, 0.90 at 3, 0.655 at 10, >= 0.60.
	RAWBREAKAUDIODSP_API double CushionRestitution(double V);
	// Hertz stiffness of two spheres from radii and materials: (4/3) sqrt(R*) / ((1 - nu1^2) / E1 + (1 - nu2^2) / E2).
	RAWBREAKAUDIODSP_API double HertzStiffness(double R1, double E1, double Nu1, double R2, double E2, double Nu2);
	// Integral of sin^1.5(pi u) over [0, 1].
	RAWBREAKAUDIODSP_API double Sin15Area();

	// Device-rate contact force for a pulse starting Frac (0 <= Frac < 1) samples after sample 0: G[k] = F_bl((k - 16 - Frac) /
	// fs), band-limited by DecimationFir (click_synth.py runtime_force). OutG has ceil(T fs + Frac) + 1 + 32 samples.
	RAWBREAKAUDIODSP_API void RuntimeForceHertz(const FContactShape& Shape, double T, double FMax, double Frac, double SampleRate, TArray<double>& OutG);
	// The same for a sin^1.5 pulse of duration T and impulse J (tip strike, soft drops).
	RAWBREAKAUDIODSP_API void RuntimeForceSine(double Impulse, double T, double Frac, double SampleRate, TArray<double>& OutG);
}
