#pragma once

// Level and dynamics (Docs/specs/audio.md 4.2): the dynamic-range modes (Wide / Normal / Night: which sound pressure maps to
// 0 dBFS, knee and ratio of the table stem), the plan-time presentation envelope of a table's summed impact stem (peak
// envelope with 3 ms lookahead and 60 ms release -> static curve -> 1 ms one-pole on the dB gain), and the meters the tests
// use: ITU-R BS.1770-4 integrated loudness, 4x-oversampled true peak, IEC 61672 A-weighting. Port of click_synth.py
// (presentation_gain_envelope, loudness_lufs, true_peak_dbtp, a_weighting). Owner: M2-C.

#include "CoreMinimal.h"

#include "RbAudio/RbAudioDspTypes.h"
#include "RbAudio/RbAudioMath.h"

namespace RbAudio
{
	enum class EDynamicRangeMode : uint8
	{
		Wide,   // headphones default: 0 dBFS = 114 dB SPL, knee 105 dB, 3:1
		Normal, // speakers default: 106 dB SPL, knee 97 dB, 4:1
		Night,  // 100 dB SPL, knee 95 dB, 10:1
	};

	struct FPresentationMode
	{
		double FullScaleSpl = 114.0;     // L_fs [dB SPL at 0 dBFS]
		double KneeSpl = 105.0;          // [dB SPL] physical peak envelope above which the table stem is compressed
		double Ratio = 3.0;              // presented dB per ratio dB above the knee
		double AmbienceOffsetDb = 0.0;   // g_bus of ambience / crowd / music
		double VoiceAddressedDb = 3.0;   // voice lines addressed to the player

		double FullScalePa() const { return PRef * FMath::Pow(10.0, FullScaleSpl / 20.0); }
		// Static curve (click_synth.py presentation_gain_db): 0 below the knee, -(over) (1 - 1 / ratio) above.
		double GainDb(double PeakSpl) const
		{
			const double Over = PeakSpl - KneeSpl;
			return Over <= 0.0 ? 0.0 : -Over * (1.0 - 1.0 / Ratio);
		}
	};
	RAWBREAKAUDIODSP_API FPresentationMode GetPresentationMode(EDynamicRangeMode Mode);
	RAWBREAKAUDIODSP_API const TCHAR* ToString(EDynamicRangeMode Mode);

	inline constexpr double PresentationLookaheadSeconds = 0.003;
	inline constexpr double PresentationReleaseSeconds = 0.060;
	inline constexpr double PresentationSmoothSeconds = 0.001;

	// Per-sample linear gain of a physical stem [Pa] (mono, or the max over channels given as one array) for a mode.
	RAWBREAKAUDIODSP_API void PresentationGainPerSample(TConstArrayView<double> PeakPa, double SampleRate, const FPresentationMode& Mode, TArray<double>& OutGain);
	// The same reduced to the plan's grid (minimum gain per Step, linear interpolation at run time). StartShotTime = shot time of
	// sample 0 of the stem.
	RAWBREAKAUDIODSP_API FPresentationGainPtr ComputePresentationEnvelope(TConstArrayView<double> PeakPa, double SampleRate, double StartShotTime,
		const FPresentationMode& Mode, double Step = 0.001);

	// IEC 61672 A-weighting as three cascaded biquads (bilinear transform, 0 dB at 1 kHz).
	struct RAWBREAKAUDIODSP_API FAWeighting
	{
		FBiquad Sections[3];
		double Gain = 1.0;
		explicit FAWeighting(double SampleRate = 48000.0);
		double Process(double X)
		{
			return Gain * Sections[2].Process(Sections[1].Process(Sections[0].Process(X)));
		}
	};
	// Equivalent A-weighted level [dB(A)] of a pressure signal [Pa].
	RAWBREAKAUDIODSP_API double LaeqDb(TConstArrayView<double> Pa, double SampleRate);

	// BS.1770-4 integrated loudness [LUFS] of an interleaved digital signal (full scale 1), channel weights 1.
	RAWBREAKAUDIODSP_API double LoudnessLufs(TConstArrayView<float> Interleaved, int32 NumChannels, double SampleRate);
	// True peak [dBTP], 4x oversampling (windowed-sinc polyphase interpolator).
	RAWBREAKAUDIODSP_API double TruePeakDbtp(TConstArrayView<float> Interleaved, int32 NumChannels);
	// Sample peak [dBFS].
	RAWBREAKAUDIODSP_API double SamplePeakDbfs(TConstArrayView<float> Interleaved);
}
