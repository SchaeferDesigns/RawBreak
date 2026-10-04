#pragma once

// Impact synthesis (Docs/specs/audio.md 3.2-3.6): the C++ port of Tools/audio/click_synth.py `runtime_render`.
//   FModalBankDesign   structural radiators (rails, bed, pockets, cue, cabinet) as biquad resonators driven by the contact force,
//                      radiating as small pistons with a radiation-efficiency high-pass (ESTIMATE banks of audio.md 3.5)
//   FImpactRenderer    renders one FImpactEvent into a buffer at a fractional start frame (overlap-add): force at the device rate
//                      (4x evaluation + decimation FIR, the fraction applied exactly), the four per-order ball kernels combined with
//                      the listener weights, the order-1 near-field term, the cloth / floor image, the structural bank
//                      (3.6 steps 0-5; golden vectors AU-T11). Zero latency: the output is aligned with the true onset; up to
//                      RuntimeLatency frames of band-limit pre-ringing are written before it.
// Owner: M2-C.

#include "CoreMinimal.h"

#include "RbAudio/RbAudioDspTypes.h"
#include "RbAudio/RbAudioMath.h"

namespace RbAudio
{
	struct FModalMode
	{
		double FrequencyHz = 100.0;
		double ModalMass = 1.0;   // [kg]
		double LossFactor = 0.1;  // eta
		double Area = 0.01;       // radiating area [m^2]
	};

	// Mode table of a bank (click_synth.py RAIL_BARBOX, RAIL_PRO, BED_MODES, POCKET_MODES, CUE_MODES).
	RAWBREAKAUDIODSP_API TConstArrayView<FModalMode> GetModalModes(EModalBank Bank, double& OutRadiationCornerHz, bool& bOutBaffled);

	// A bank's filters for one sample rate: per mode acc / F = -(1/M) s^2 / (s^2 + eta wn s + wn^2) (bilinear, prewarped),
	// p = sum rho0 S acc / (2 pi) (baffled) or / (4 pi) (free) at 1 m, radiation high-pass |H| = f^2 / (f^2 + fc^2).
	struct FModalBankDesign
	{
		EModalBank Bank = EModalBank::None;
		double SampleRate = 48000.0;
		TArray<FBiquad> Modes;      // gains folded in
		FBiquad HighPass;           // identity when fc = 0
		bool bHighPass = false;
		int32 TailSamples = 0;      // after the force pulse, until every mode decayed by 80 dB
	};
	using FModalBankDesignPtr = TSharedPtr<const FModalBankDesign, ESPMode::ThreadSafe>;
	RAWBREAKAUDIODSP_API FModalBankDesignPtr GetModalBankDesign(EModalBank Bank, double SampleRate);

	class RAWBREAKAUDIODSP_API FImpactRenderer
	{
	public:
		explicit FImpactRenderer(double InSampleRate = 48000.0) { Initialize(InSampleRate); }

		// Prepares the decimation FIR and the bank designs for the device rate (kernels come with the events).
		void Initialize(double InSampleRate);
		double GetSampleRate() const { return SampleRate; }

		// Adds Event into Out, whose first frame is device frame BufferStartFrame; EventFrame is the (fractional) device frame of
		// the contact start (before propagation). Frames outside Out are dropped. Returns the number of frames written.
		int32 Render(const FImpactEvent& Event, double EventFrame, int64 BufferStartFrame, TArrayView<float> Out) const;
		// The same into a double buffer (offline / golden tests).
		int32 Render(const FImpactEvent& Event, double EventFrame, int64 BufferStartFrame, TArrayView<double> Out) const;

		// Frames the event writes relative to EventFrame: [OutFirst, OutEnd) (pre-ringing, propagation, tails).
		void Extent(const FImpactEvent& Event, int32& OutFirst, int32& OutEnd) const;

	private:
		template <typename SampleType>
		int32 RenderImpl(const FImpactEvent& Event, double EventFrame, int64 BufferStartFrame, TArrayView<SampleType> Out) const;
		void ComputeForce(const FImpactEvent& Event, double Frac, TArray<double>& OutG) const;

		double SampleRate = 48000.0;
		FModalBankDesignPtr Banks[static_cast<int32>(EModalBank::Count)];
		mutable TArray<double> ScratchG;
		mutable TArray<double> ScratchKernel;
		mutable TArray<double> ScratchOut;
	};
}
