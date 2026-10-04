#pragma once

// Helpers of the engine-level audio tests RawBreak.Functional.Audio.* (Source/RawBreakEditor/Private/Tests/RbAudioFunctionalTest.cpp;
// Docs/ue-architecture.md 18.5) and of dev recordings: the editor module links the RawBreak module only, so the DSP analysis of
// RawBreakAudioDsp (onsets, group delay, true peak, loudness, WAV files) and the table clock are reached through these exported
// wrappers. Offline, allocation-heavy; never on the audio thread. Owner: M2-C.

#include "CoreMinimal.h"

#include "Audio/RbImpactVoiceComponent.h"

namespace RbAudioTestKit
{
	struct FOnset
	{
		double Sample = 0.0;
		double LevelDb = 0.0;
	};

	RAWBREAK_API void DetectOnsets(TConstArrayView<double> Mono, double SampleRate, TArray<FOnset>& Out, double HighPassHz = 0.0, double RiseDb = 6.0,
		double RiseWindowMs = 2.0, double MinGapMs = 1.0, double AbsFloor = 1e-7);
	RAWBREAK_API double GroupDelaySamples(TConstArrayView<double> A, TConstArrayView<double> B, double SampleRate, double FLo = 500.0, double FHi = 6000.0);
	RAWBREAK_API double TruePeakDbtp(TConstArrayView<float> Interleaved, int32 NumChannels);
	RAWBREAK_API double RmsDb(TConstArrayView<double> X, int32 Begin, int32 End);
	RAWBREAK_API double SchroederRt60(TConstArrayView<double> X, int32 Begin, int32 End, double SampleRate, double DecayDb = 20.0);
	// Equivalent A-weighted level of a digital signal converted to pressure with FullScalePa [dB(A)].
	RAWBREAK_API double LaeqDb(TConstArrayView<double> Digital, double SampleRate, double FullScalePa);
	RAWBREAK_API double SpectralCentroidHz(TConstArrayView<double> X, double SampleRate);
	// RMS of a band [dB re 1] (2nd-order Butterworth high-pass Lo / low-pass Hi, twice each).
	RAWBREAK_API double BandRmsDb(TConstArrayView<double> X, double SampleRate, double LoHz, double HiHz, int32 Begin, int32 End);
	RAWBREAK_API bool WriteWav(const FString& Path, TConstArrayView<float> Interleaved, int32 NumChannels, int32 SampleRate, bool bFloat32);
	RAWBREAK_API void ExtractChannel(TConstArrayView<float> Interleaved, int32 NumChannels, int32 Channel, TArray<double>& Out);
	RAWBREAK_API const TCHAR* ImpactKindName(uint8 Kind);

	// The table clock (tests drive a clock of their own voices like URbTableAudioComponent does).
	RAWBREAK_API FRbShotAudioClockPtr MakeClock();
	RAWBREAK_API void StartShot(const FRbShotAudioClockPtr& Clock, uint64 ShotId, double OriginClock, double OriginShotTime, double Rate, double VisualLatency);
	struct FClockState
	{
		uint64 ShotId = 0;
		uint32 Generation = 0;
		bool bAnchored = false;
		bool bRunning = false;
		bool bHeld = false;
		int64 AnchorFrame = 0;
		double OriginShotTime = 0.0;
		double Rate = 1.0;
		int64 LastLeadFrames = 0;
	};
	RAWBREAK_API FClockState ReadClock(const FRbShotAudioClockPtr& Clock);

	// A voice plan of single standard ball-ball clicks (1 m/s, on-axis at 1 m, the golden case std_1ms_shooter) at the given shot
	// times, without propagation delay, output gain OutputGain (Pa -> digital).
	RAWBREAK_API FRbVoicePlanPtr MakeClickPlan(uint64 ShotId, TConstArrayView<double> ShotTimes, double SampleRate, double OutputGain);
	// The same click rendered offline at a fractional frame (the reference waveform of the capture checks) [Pa].
	RAWBREAK_API void RenderClick(double EventFrame, int32 NumOut, double SampleRate, TArray<double>& Out);
	// A test voice (AU-0), registered and started (the editor module does not link the AudioMixer module that USynthComponent::Start
	// lives in): Channel >= 0 = a non-spatialised stereo voice writing only to that output channel; Channel == -1 = a positional mono
	// voice (1 / r beyond RefDistance) like the table voices; Channel == -2 = a non-spatialised mono voice (the engine's mono upmix).
	// Optional reverb send (pre distance attenuation); bSendOnly: no base submix output (the table's reverb-feed configuration).
	RAWBREAK_API URbImpactVoiceComponent* SpawnTestVoice(AActor* Owner, USoundSubmix* Submix, int32 Channel, const FRbShotAudioClockPtr& Clock, FName Name,
		USoundSubmix* ReverbSubmix = nullptr, float ReverbSend = 0.0f, double RefDistance = 1.0, bool bSendOnly = false);

	// Diagnostics of a voice: component active / playing, generator alive, blocks rendered.
	RAWBREAK_API FString DescribeVoice(const URbImpactVoiceComponent* Voice);

	// Energy (sum of squares) of one channel of a WAV file (the venue IRs of Tools/audio/out/ref).
	RAWBREAK_API double WavChannelEnergy(const FString& Path, int32 Channel);

	// P_fs of the current dynamic-range mode [Pa at 0 dBFS].
	RAWBREAK_API double FullScalePa();
}
