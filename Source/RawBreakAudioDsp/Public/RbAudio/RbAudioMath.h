#pragma once

// Numerical building blocks of the table-audio DSP (Docs/specs/audio.md 3): physical constants shared with
// Tools/audio/click_synth.py, spherical Bessel functions (scipy's algorithms), Legendre polynomials, Brent's root finder
// (scipy's brentq, line by line), a radix-2 FFT, biquads, the 4x decimation FIR (scipy.signal.firwin with a Kaiser window) and
// a deterministic noise generator. Plain C++ on UE Core: no UObjects, no audio device. Owner: M2-C.

#include "CoreMinimal.h"

#include <complex>

namespace RbAudio
{
	using FComplex = std::complex<double>;

	// --- constants (click_synth.py; DERIVED unless marked) ---------------------------------------------------------------------
	inline constexpr double Rho0 = 1.204;          // air density at 20 degC [kg/m^3]
	inline constexpr double C0 = 343.2;            // speed of sound at 20 degC [m/s]
	inline constexpr double PRef = 20e-6;          // [Pa]
	inline constexpr double Pi = 3.14159265358979323846;
	inline constexpr int32 DecimationFactor = 4;   // the force pulse is evaluated at 4 x fs and decimated
	inline constexpr int32 DecimationTaps = 129;   // linear phase, delay 64 at 4 x fs = 16 at fs
	inline constexpr double DecimationKaiserBeta = 8.0;
	inline constexpr int32 KernelTaps = 512;       // per-order ball radiation FIR
	inline constexpr int32 KernelPreDelay = 32;    // pre-delay of the kernels (zero-phase band limit) [samples]
	inline constexpr int32 RuntimeLatency = DecimationTaps / 2 / DecimationFactor + KernelPreDelay; // 16 + 32 = 48
	inline constexpr double NearFieldLeakHz = 2.0; // corner of the leaky integrator of the order-1 near-field term
	inline constexpr double ClothReflection = 0.9; // ESTIMATE: cloth-covered slate as a mirror with a one-pole loss
	inline constexpr double ClothCornerHz = 8000.0;

	// Core values (Docs/specs/equipment.md 6.1, physics-collisions.md 3.9.3).
	inline constexpr double StdBallRadius = 0.028575;
	inline constexpr double StdBallMass = 0.170097;
	inline constexpr double CoreHertzK = 8.0587e8; // ball-ball Hertz stiffness [N/m^1.5]
	inline constexpr double BallRestitution = 0.95;
	inline constexpr double SlateRestitution = 0.6;
	inline constexpr double Gravity = 9.81;

	inline double DbToGain(double Db) { return FMath::Pow(10.0, Db / 20.0); }
	inline double GainToDb(double Gain) { return 20.0 * FMath::LogX(10.0, FMath::Max(Gain, 1e-300)); }
	inline double PaToDbSpl(double Pa) { return GainToDb(Pa / PRef); }

	// Legendre polynomial P_n(x), n = 0..3.
	RAWBREAKAUDIODSP_API double Legendre(int32 N, double X);

	// Spherical Bessel functions of the first / second kind and their derivatives, n >= 0 (scipy.special.spherical_jn / yn:
	// upward recurrence from sin / cos; j_n for x < n by its power series instead of scipy's cylindrical Bessel call).
	RAWBREAKAUDIODSP_API double SphJ(int32 N, double X);
	RAWBREAKAUDIODSP_API double SphY(int32 N, double X);
	RAWBREAKAUDIODSP_API double SphJDeriv(int32 N, double X);
	RAWBREAKAUDIODSP_API double SphYDeriv(int32 N, double X);
	// Spherical Hankel function of the second kind h_n = j_n - i y_n and its derivative.
	RAWBREAKAUDIODSP_API FComplex SphH2(int32 N, double X);
	RAWBREAKAUDIODSP_API FComplex SphH2Deriv(int32 N, double X);

	// Modified Bessel function of the first kind, order 0 (Kaiser window).
	RAWBREAKAUDIODSP_API double BesselI0(double X);

	// Brent's method exactly as scipy.optimize.brentq (rtol = 4 eps, maxiter 100). A and B must bracket a root.
	RAWBREAKAUDIODSP_API double BrentQ(TFunctionRef<double(double)> F, double A, double B, double XTol, double RTol = 4.0 * 2.220446049250313e-16,
		int32 MaxIterations = 100);

	// In-place radix-2 complex FFT (forward: e^{-i}); Data.Num() must be a power of two. Inverse is unscaled.
	RAWBREAKAUDIODSP_API void Fft(TArrayView<FComplex> Data, bool bInverse);
	// numpy.fft.irfft(Half, N): Half has N / 2 + 1 bins; the imaginary parts of DC and Nyquist are ignored.
	RAWBREAKAUDIODSP_API void InverseRealFft(TConstArrayView<FComplex> Half, int32 N, TArray<double>& Out);

	// scipy.signal.firwin(Taps, Cutoff, window=('kaiser', Beta), fs=SampleRate): low-pass, DC gain 1.
	RAWBREAKAUDIODSP_API void KaiserLowPass(int32 Taps, double CutoffHz, double SampleRate, double Beta, TArray<double>& Out);

	// The 4x decimation FIR of the runtime pulse for a device rate (cutoff fs / 2 at 4 fs; 129 taps, Kaiser 8).
	RAWBREAKAUDIODSP_API const TArray<double>& DecimationFir(double SampleRate);

	// Biquad, transposed direct form II, double precision. Coefficients normalised (a0 = 1).
	struct FBiquad
	{
		double B0 = 1.0, B1 = 0.0, B2 = 0.0, A1 = 0.0, A2 = 0.0;
		double Z1 = 0.0, Z2 = 0.0;

		double Process(double X)
		{
			const double Y = B0 * X + Z1;
			Z1 = B1 * X - A1 * Y + Z2;
			Z2 = B2 * X - A2 * Y;
			return Y;
		}
		void Reset() { Z1 = Z2 = 0.0; }

		// Bilinear transform of H(s) = (b0 s^2 + b1 s + b2) / (a0 s^2 + a1 s + a2), prewarped at PrewarpHz (0 = none).
		RAWBREAKAUDIODSP_API static FBiquad FromAnalog(double Sb0, double Sb1, double Sb2, double Sa0, double Sa1, double Sa2, double SampleRate, double PrewarpHz);
		// 2nd-order Butterworth band-pass section pair as scipy.signal.butter(2, [Lo, Hi], 'band') (two biquads).
		RAWBREAKAUDIODSP_API static void ButterBandPass2(double LoHz, double HiHz, double SampleRate, FBiquad& OutA, FBiquad& OutB);
		RAWBREAKAUDIODSP_API static FBiquad ButterLowPass2(double CutoffHz, double SampleRate);
		RAWBREAKAUDIODSP_API static FBiquad ButterHighPass2(double CutoffHz, double SampleRate);
		RAWBREAKAUDIODSP_API static FBiquad OnePoleLowPass(double CutoffHz, double SampleRate); // bilinear, as a biquad with B2 = A2 = 0
	};

	// Deterministic noise (xorshift64*): the same seed gives the same sequence on every platform (replays, AU-T13).
	struct FNoise
	{
		uint64 State = 0x9E3779B97F4A7C15ull;

		explicit FNoise(uint64 Seed = 1) { Reseed(Seed); }
		RAWBREAKAUDIODSP_API void Reseed(uint64 Seed);
		uint64 NextU64()
		{
			State ^= State >> 12;
			State ^= State << 25;
			State ^= State >> 27;
			return State * 0x2545F4914F6CDD1Dull;
		}
		double Uniform() { return static_cast<double>(NextU64() >> 11) * (1.0 / 9007199254740992.0); } // [0, 1)
		double Uniform(double Lo, double Hi) { return Lo + (Hi - Lo) * Uniform(); }
		// Approximately Gaussian, unit variance (sum of 4 uniforms, cheap and deterministic).
		double Gauss()
		{
			const double S = Uniform() + Uniform() + Uniform() + Uniform() - 2.0;
			return S * 1.7320508075688772; // sqrt(12 / 4)
		}
	};

	// Stable 64-bit mix of several values (seeds of voices, events, steps; FNV-1a over the bytes).
	RAWBREAKAUDIODSP_API uint64 HashMix(uint64 A, uint64 B);
}
