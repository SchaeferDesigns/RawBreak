#pragma once

// Radiation of a ball (Docs/specs/audio.md 3.3, 3.4): the exact sound field of a vibrating sphere per Legendre order n
// (rigid-body "acceleration noise" in order 1, Lamb's elastic spheroidal modes in orders 0..3), turned into four 512-tap
// far-field FIR kernels per ball and device rate (click_synth.py far_order_kernels): per unit contact force, pressure at 1 m,
// 32-sample pre-delay. The listener weights them with P_n(cos th) / r; order 1 adds the near-field term (c / r) int p1 dt.
// Computed at start-up / first use (no data files at runtime); Tools/audio/out/ref/ball_kernels_*_48k.json check the port.
// Owner: M2-C.

#include "CoreMinimal.h"

#include "RbAudio/RbAudioMath.h"

namespace RbAudio
{
	struct FBallMaterial
	{
		double YoungModulus = 8.87e9; // E [Pa]
		double Poisson = 0.35;        // nu
		double Loss = 0.015;          // structural loss factor eta (modal damping ratio = eta / 2)

		bool operator==(const FBallMaterial& Other) const
		{
			return YoungModulus == Other.YoungModulus && Poisson == Other.Poisson && Loss == Other.Loss;
		}
	};

	// Cast phenolic, E DERIVED from the core's Hertz K for two standard balls (8.87 GPa at nu 0.35), loss 0.015 (ESTIMATE).
	RAWBREAKAUDIODSP_API FBallMaterial PhenolicMaterial();
	// Polyester bar set (ESTIMATE): E 5 GPa, nu 0.36, loss 0.03.
	RAWBREAKAUDIODSP_API FBallMaterial PolyesterMaterial();
	// E of two identical spheres from their Hertz constant K = (4/3) E* sqrt(R/2), E* = E / (2 (1 - nu^2)).
	RAWBREAKAUDIODSP_API double YoungFromHertz(double K, double R, double Nu);

	struct FBallAcoustics
	{
		double Radius = StdBallRadius; // [m]
		double Mass = StdBallMass;     // [kg]
		FBallMaterial Material;

		double Density() const { return Mass / (4.0 / 3.0 * Pi * Radius * Radius * Radius); }
		bool operator==(const FBallAcoustics& Other) const
		{
			return Radius == Other.Radius && Mass == Other.Mass && Material == Other.Material;
		}
	};

	struct FSphereMode
	{
		int32 Order = 0;        // Legendre order n
		int32 Root = 1;         // 1 = fundamental of the order
		double X = 0.0;         // k_T a
		double FrequencyHz = 0.0;
		double ModalMass = 0.0; // for unit pole radial displacement [kg]
	};

	// Roots x = k_T a of the stress-free spheroidal frequency equation of order N (Lamb 1882; AU-T05).
	RAWBREAKAUDIODSP_API TArray<double> LambRoots(int32 N, const FBallMaterial& Material, double Radius, double Density, double XMax = 14.0, int32 Count = 2);
	RAWBREAKAUDIODSP_API FSphereMode LambMode(int32 N, double X, const FBallAcoustics& Ball, int32 RootIndex);
	// Modes of orders 0..3 up to FMax, sorted by frequency (click_synth.py sphere_modes).
	RAWBREAKAUDIODSP_API TArray<FSphereMode> SphereModes(const FBallAcoustics& Ball, double FMax = 40000.0);

	// Pressure at (r, th) per unit surface normal ACCELERATION amplitude of order n (times P_n(cos th)), e^{+iwt}:
	// p / A = -rho0 c h_n(kr) / (w h_n'(ka)) (AU-T06: order 1 at low frequency -> rho0 a^3 / (2 r^2)).
	RAWBREAKAUDIODSP_API FComplex RadiationAccel(int32 N, double FrequencyHz, double R, double A);

	// Band limit of the kernels: causal 2nd-order Butterworth high-pass at 12 Hz x raised-cosine taper 20-23.5 kHz.
	RAWBREAKAUDIODSP_API FComplex BandTaper(double FrequencyHz);

	struct FBallKernels
	{
		double SampleRate = 48000.0;
		FBallAcoustics Ball;
		bool bRigidOnly = false;     // order 1 rigid-body term only (sources the modes do not matter for: drops, tray)
		TArray<double> Orders[4];    // KernelTaps each [Pa at 1 m per N]
		TArray<double> LeakyOrder1;  // leaky running sum of Orders[1] (leak exp(-2 pi 2 Hz / fs)), KernelTaps + MaxPulseSamples
		uint32 OrderMask = 0;        // bit n: Orders[n] is not all zero
		static constexpr int32 MaxPulseSamples = 1024;
	};
	using FBallKernelsPtr = TSharedPtr<const FBallKernels, ESPMode::ThreadSafe>;

	// click_synth.py far_order_kernels (16384-point frequency design, first 512 taps of the impulse response).
	RAWBREAKAUDIODSP_API void ComputeFarOrderKernels(const FBallAcoustics& Ball, double SampleRate, bool bRigidOnly, TArray<double> OutOrders[4]);
	// Cached kernel set (thread-safe; the first call per ball / rate / variant computes it, a few ms).
	RAWBREAKAUDIODSP_API FBallKernelsPtr GetBallKernels(const FBallAcoustics& Ball, double SampleRate, bool bRigidOnly = false);
}
