#pragma once

// Everything of audio v1 that is noise-like or not a table impact, all SYNTHESISED (M2 uses no library samples):
//   FShapedNoise      unit-RMS band noise per continuous kind: rolling on cloth 60-700 Hz (AU-22), sliding hiss 1.5-6 kHz (AU-23),
//                     gully run 120-900 Hz with seam bumps (AU-36), loose ball rolling on vinyl tile 200-3000 Hz (AU-25)
//   Levels            ESTIMATE levels of audio.md 2.2-2.3 (rolling 40 dB SPL rms at 1 m for 1 m/s, cloth presets, gully 55 dB SPL)
//   FFootstepSynth    heel + toe transients per floor surface (AU-65): a sin^1.5 contact pulse driving a small shoe / floor modal
//                     set plus a sole scuff; seeded variation, never two identical steps
//   FAmbienceSynth    room-tone layers per venue (audio.md 2.7, 12.1): HVAC bed (stereo), ceiling diffuser hiss, cooler compressor
//                     (60 Hz hum + motor, on / off cycles with start / stop clunks), neon transformer hum (120 Hz + harmonics,
//                     electrode sizzle); each calibrated to its A-weighted target level at 1 m
// Deterministic for a given seed; no allocation after Initialize. Owner: M2-C.

#include "CoreMinimal.h"

#include "RbAudio/RbAudioDspTypes.h"
#include "RbAudio/RbAudioMath.h"

namespace RbAudio
{
	// ESTIMATE levels (audio.md 2.2-2.3), Pa rms at 1 m.
	inline constexpr double RollingRmsPerMps = 2.0e-3;   // 40 dB SPL at 1 m for 1 m/s on cloth
	inline constexpr double SlidingHissFactor = 0.35;    // x the rolling amplitude
	inline constexpr double GullyRms = 0.01125;          // 55 dB SPL at 1 m
	inline constexpr double FloorRollingFactor = 3.98;   // +12 dB over cloth at equal speed (vinyl tile on concrete)
	inline constexpr double TileJointSpacing = 0.305;    // [m] 12 in VCT tiles (a tick per joint)

	class RAWBREAKAUDIODSP_API FShapedNoise
	{
	public:
		// Cheap once the (kind, rate) normalisation is cached (Prewarm: the plan builder's worker, never the audio thread).
		void Initialize(ENoiseKind Kind, double SampleRate, uint64 Seed);
		static void Prewarm(double SampleRate);
		// Next unit-RMS sample (white Gaussian-like noise through the kind's band filter).
		double Next()
		{
			return Norm * B.Process(A.Process(Rng.Gauss()));
		}
		ENoiseKind GetKind() const { return Kind; }

	private:
		ENoiseKind Kind = ENoiseKind::RollingCloth;
		FNoise Rng;
		FBiquad A;
		FBiquad B;
		double Norm = 1.0;
	};

	// Gully seam bumps (click_synth.py gully_noise): 1 + 0.6 max(0, sin(2 pi (0.8 / 0.06) t))^8, t since the run started.
	RAWBREAKAUDIODSP_API double GullyBumps(double SecondsSinceStart);
	// Gully envelope: 50 ms fade-in, 80 ms fade-out over a run of Duration seconds.
	RAWBREAKAUDIODSP_API double GullyEnvelope(double SecondsSinceStart, double Duration);

	enum class EFloorSurface : uint8
	{
		Concrete,   // test room
		Vct,        // vinyl composition tile on concrete (dive bar main floor)
		Rubber,     // anti-fatigue mats (bar aisle)
		Wood,       // ledges, later venues
	};
	RAWBREAKAUDIODSP_API const TCHAR* ToString(EFloorSurface Surface);

	struct FFootstepParams
	{
		EFloorSurface Surface = EFloorSurface::Vct;
		double SpeedMps = 1.4;     // walking speed
		bool bLeftFoot = false;
		bool bOwnSteps = true;     // the listener's own steps: body-conducted low shelf (+3 dB below 200 Hz)
		uint64 Seed = 1;
	};

	class RAWBREAKAUDIODSP_API FFootstepSynth
	{
	public:
		// Renders one step (heel, then toe) as pressure at 1 m [Pa] into Out (resized; about 0.35 s).
		static void Render(const FFootstepParams& Params, double SampleRate, TArray<float>& Out);
		// Nominal heel peak at 1 m for 1.4 m/s [Pa] per surface (ESTIMATE: ~69 dB SPL on vinyl tile).
		static double NominalHeelPeakPa(EFloorSurface Surface);
	};

	enum class EAmbienceLayer : uint8
	{
		HvacBed,       // stereo, non-spatialised room tone (air handler rumble + broadband air, slow drift)
		HvacDiffuser,  // mono, positional: air hiss at a ceiling diffuser
		Compressor,    // mono, positional: cooler compressor (60 Hz hum + motor), duty cycle with start / stop clunks
		NeonHum,       // mono, positional: neon transformer buzz (120 Hz family) + faint electrode sizzle
	};
	RAWBREAKAUDIODSP_API const TCHAR* ToString(EAmbienceLayer Layer);

	struct FAmbienceLayerDesc
	{
		EAmbienceLayer Layer = EAmbienceLayer::HvacBed;
		double LevelDbA = 40.0;        // LAeq target at 1 m (the bed: at the listener)
		double MainsHz = 60.0;         // US 60 Hz (neon 120 Hz), Germany 50 Hz later
		uint64 Seed = 1;
		double CycleOnSeconds[2] = {480.0, 900.0};  // compressor: on 8-15 min
		double CycleOffSeconds[2] = {300.0, 600.0}; // off 5-10 min
		bool bStartOn = true;          // compressor state at t = 0
		int32 NumChannels() const { return Layer == EAmbienceLayer::HvacBed ? 2 : 1; }
	};

	class RAWBREAKAUDIODSP_API FAmbienceSynth
	{
	public:
		// Prepares the layer and calibrates its level (renders a few seconds once, A-weighted). A known calibration (from an earlier
		// Initialize of the same desc and rate, GetCalibration) skips that pass (the audio thread never calibrates).
		void Initialize(const FAmbienceLayerDesc& Desc, double SampleRate, double KnownCalibration = 0.0);
		double GetCalibration() const { return Calibration; }
		// Renders Frames frames of Desc.NumChannels() interleaved channels [Pa at 1 m], adding nothing: Out is overwritten.
		void Render(float* Out, int32 Frames);
		const FAmbienceLayerDesc& GetDesc() const { return Desc; }
		// Compressor state (tests).
		bool IsCompressorOn() const { return bOn; }
		double GetSeconds() const { return static_cast<double>(FramesRendered) / SampleRate; }

	private:
		double RawSample(int32 Channel);   // uncalibrated sample of the layer
		void AdvanceFrame();

		FAmbienceLayerDesc Desc;
		double SampleRate = 48000.0;
		double Calibration = 1.0;
		int64 FramesRendered = 0;
		FNoise Rng;
		FNoise RngB;
		// Filters / states (meaning per layer).
		FBiquad Band[2][3];
		double Pink[2][7] = {};
		double Brown[2] = {};
		double Drift = 0.0;
		double DriftTarget = 0.0;
		double DriftRate = 0.0;
		int64 NextDriftFrame = 0;
		// Harmonic oscillators (phasor rotation, renormalised every block).
		static constexpr int32 MaxHarmonics = 32;
		int32 NumHarmonics = 0;
		double HRe[MaxHarmonics] = {};
		double HIm[MaxHarmonics] = {};
		double HCos[MaxHarmonics] = {};
		double HSin[MaxHarmonics] = {};
		double HGain[MaxHarmonics] = {};
		double MotorPhase = 0.0;
		// Compressor duty cycle.
		bool bOn = true;
		double StateEndSeconds = 0.0;
		double TransientSeconds = -1.0;   // time since the last start / stop transient (< 0: none)
		bool bTransientIsStart = true;
		double RunLevel = 1.0;            // hum level ramp (spin-up / run-down)
		// Neon sizzle.
		double SizzleEnv = 0.0;
	};
}
