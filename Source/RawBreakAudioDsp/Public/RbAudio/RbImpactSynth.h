#pragma once

// Impact synthesis (Docs/specs/audio.md 3.2-3.6): the C++ port of Tools/audio/click_synth.py `runtime_render`.
//   FContactPulse / HertzPulse   self-similar Hertz + Tsuji contact force pulse (AU-T01..T04)
//   FModalBank                   biquad resonator bank with a radiation high-pass (rails, bed, pockets, cue body)
//   FImpactRenderer              renders one FImpactEvent into a voice buffer at a fractional start frame (overlap-add):
//                                force at the device rate (4x evaluation + decimation FIR), per-order ball radiation kernels,
//                                cloth image, structural bank (3.6 steps 0-5); golden vectors AU-T11
//   FNoiseSource                 rolling / sliding / gully / floor noise driven by FContinuousSegment speeds
// Owner: M2-C (stub by the M2 architect step: the API; TODO(M2-C) the port).

#include "CoreMinimal.h"

#include "RbAudio/RbAudioDspTypes.h"

namespace RbAudio
{
	struct FContactPulse
	{
		double Duration = 0.0;  // T [s]
		double PeakForce = 0.0; // F_max [N]
		double Impulse = 0.0;   // J [N s]
	};

	// Undamped Hertz estimate for an impact at NormalSpeed between masses with the effective mass m* and stiffness K; the
	// restitution-dependent (Tsuji) correction and the self-similar shape table are TODO(M2-C) (AU-T01..T04).
	RAWBREAKAUDIODSP_API FContactPulse HertzPulse(double NormalSpeed, double EffectiveMass, double Stiffness, double Restitution);

	class RAWBREAKAUDIODSP_API FModalBank
	{
	public:
		struct FMode
		{
			double FrequencyHz = 1000.0;
			double Decay60Seconds = 0.05; // T60 of the mode
			double Gain = 1.0;
		};

		void Initialize(double SampleRate, TArrayView<const FMode> Modes, double RadiationHighPassHz);
		// Adds the bank's response to a force pulse starting at FractionalStart (frames) into Out (TODO(M2-C)).
		void Excite(const FContactPulse& Pulse, double FractionalStart, TArrayView<float> Out) const;

	private:
		double SampleRate = 48000.0;
		TArray<FMode> Modes;
		double HighPassHz = 0.0;
	};

	class RAWBREAKAUDIODSP_API FImpactRenderer
	{
	public:
		// Builds the kernels for the device rate (per ball radius / mass of the active ball set).
		void Initialize(double SampleRate);
		double GetSampleRate() const { return SampleRate; }

		// Renders Event into Out, whose first frame is device frame BufferStartFrame; EventFrame is the event's (fractional)
		// device frame from the clock. Returns the number of frames written (0 when the event is outside the buffer).
		int32 Render(const FImpactEvent& Event, double EventFrame, int64 BufferStartFrame, TArrayView<float> Out) const;

	private:
		double SampleRate = 48000.0;
	};
}
