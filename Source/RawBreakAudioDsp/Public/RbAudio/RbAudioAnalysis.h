#pragma once

// Offline reference and analysis of the table-audio DSP (Docs/specs/audio.md 3.7, 14; Docs/ue-architecture.md 18.5):
//   RenderExactImpact  the EXACT frequency-domain render of click_synth.py (render_impact_segment: the Hertz force integrated on
//                      the 768 kHz grid, its spectrum, the exact sphere radiation per Legendre order with Lamb's modes and the
//                      spherical Hankel functions, the cloth image, the structural pistons), the reference of AU-T09 / AU-T10;
//   GroupDelaySamples  sub-sample onset separation of two renders (phase slope over a band; click_synth.py onset_separation, AU-T08);
//   BandErrorDb        1/6-octave spectral error between two renders (click_synth.py band_error_db, AU-T10);
//   DetectOnsets       transient onsets of a recording (the event-log check of the recorded breaks);
//   Wav I/O            32-bit float / 24-bit PCM WAV files for the recorded checks (Docs/audio/m2).
// Offline only (allocates freely); no UObjects, no audio device. Owner: M2-C.

#include "CoreMinimal.h"

#include "RbAudio/RbAudioDspTypes.h"
#include "RbAudio/RbAudioMath.h"
#include "RbAudio/RbBallKernels.h"

namespace RbAudio
{
	struct FExactBody
	{
		FBallAcoustics Ball;
		double Center[3] = {0.0, 0.0, 0.0}; // [m]; the cloth plane is z = 0
		double Axis[3] = {1.0, 0.0, 0.0};   // unit, centre -> contact point
		bool bModes = true;                 // Lamb's elastic modes (false: rigid body only)
	};

	struct FExactPiston
	{
		EModalBank Bank = EModalBank::RailBarBox;
		double Position[3] = {0.0, 0.0, 0.0};
		double Gain = 1.0;
	};

	struct FExactImpact
	{
		TArray<double> Force;        // contact force on the HertzGridRate grid, Force[0] at the contact start [N]
		TArray<FExactBody> Bodies;
		TArray<FExactPiston> Pistons;
		double LowPassHz = 0.0;      // one-pole muffling (0 = off)
		bool bImage = true;          // cloth image of every ball above the cloth
	};

	// A head-on ball-ball impact of click_synth.py ball_ball_impact (B1 moving +x at V hits B2 at rest; both on the cloth).
	RAWBREAKAUDIODSP_API FExactImpact MakeBallBallExactImpact(double V, const FBallAcoustics& B1, const FBallAcoustics& B2, double Restitution = BallRestitution);
	// Its runtime-renderer events (one per ball; the same geometry and contact) for a listener (golden-case form, no directivity floor).
	RAWBREAKAUDIODSP_API void MakeBallBallRuntimeEvents(double V, const FBallAcoustics& B1, const FBallAcoustics& B2, const double Listener[3],
		double SampleRate, TArray<FImpactEvent>& OutEvents, double Restitution = BallRestitution);

	// Pressure [Pa] at Listener of Impact starting StartSeconds after Out[0] (Out.Num() = NumOut, a power of two).
	RAWBREAKAUDIODSP_API void RenderExactImpact(const FExactImpact& Impact, const double Listener[3], double StartSeconds, int32 NumOut, double SampleRate,
		TArray<double>& Out);

	// Group delay of B relative to A over [FLo, FHi] [samples] (phase slope of B conj(A), least squares).
	RAWBREAKAUDIODSP_API double GroupDelaySamples(TConstArrayView<double> A, TConstArrayView<double> B, double SampleRate, double FLo = 500.0, double FHi = 6000.0);

	// (max, energy-weighted rms) difference [dB] of the 1/6-octave power spectra over 100 Hz-16 kHz, in bands within WithinDb of the
	// reference's maximum.
	RAWBREAKAUDIODSP_API void BandErrorDb(TConstArrayView<double> Ref, TConstArrayView<double> Test, double SampleRate, double& OutMaxDb, double& OutRmsDb,
		double WithinDb = 20.0);

	struct FOnset
	{
		double Sample = 0.0;   // onset position [samples] (first frame of the rise, sub-frame interpolated)
		double LevelDb = 0.0;  // peak envelope level after the onset [dB re 1]
	};
	// Onsets of transients: a 0.25 ms RMS envelope of the signal (optionally high-passed at HighPassHz), a rise of RiseDb over the
	// envelope RiseWindowMs earlier and above AbsFloor; onsets closer than MinGapMs merge.
	RAWBREAKAUDIODSP_API void DetectOnsets(TConstArrayView<double> Mono, double SampleRate, TArray<FOnset>& Out, double HighPassHz = 0.0, double RiseDb = 6.0,
		double RiseWindowMs = 2.0, double MinGapMs = 1.0, double AbsFloor = 1e-7);

	// RMS level [dB re 1] of a window.
	RAWBREAKAUDIODSP_API double RmsDb(TConstArrayView<double> X, int32 Begin, int32 End);

	// Schroeder decay: RT60 [s] extrapolated from the -5 ... -5-DecayDb dB range of the backward-integrated energy of X[Begin..End).
	RAWBREAKAUDIODSP_API double SchroederRt60(TConstArrayView<double> X, int32 Begin, int32 End, double SampleRate, double DecayDb = 20.0);

	// WAV files (interleaved). bFloat32: IEEE float, else 24-bit PCM (clipped).
	RAWBREAKAUDIODSP_API bool WriteWavFile(const FString& Path, TConstArrayView<float> Interleaved, int32 NumChannels, int32 SampleRate, bool bFloat32 = true);
	RAWBREAKAUDIODSP_API bool ReadWavFile(const FString& Path, TArray<float>& OutInterleaved, int32& OutNumChannels, int32& OutSampleRate);

	// Channel Channel of an interleaved buffer (or the mean of all channels for Channel < 0) as doubles.
	RAWBREAKAUDIODSP_API void ExtractChannel(TConstArrayView<float> Interleaved, int32 NumChannels, int32 Channel, TArray<double>& Out);
}
