#pragma once

// Generated audio assets of M2 (Docs/specs/audio.md 7.1, 8.5; Docs/ue-architecture.md 18.5) under RbAssetPaths::AudioDir, created
// by Tools/unreal/editor/rb_make_audio.py (names are a contract between that script and this module):
//   Mix/SUBM_RB_<Bus>              the submix graph of audio.md 7.1 (Master with the true-peak limiter, World and its children,
//                                  UI, MenuMusic); Reverb_<Venue> under World (the pause mix filters the reverb too)
//   Mix/DYN_RB_MasterLimiter       SubmixEffectDynamicsProcessorPreset: limiter at -1 dBFS, lookahead (the safety net of 4.2)
//   Mix/FLT_RB_PauseLowPass        SubmixEffectFilterPreset: low-pass 800 Hz (CBM_RB_Pause, applied as an effect-chain override)
//   IR/IR_RB_<Venue>               AudioImpulseResponse synthesised by Tools/audio/ir_synth.py (image sources + Sabine tail)
//   Mix/CRV_RB_<Venue>             SubmixEffectConvolutionReverbPreset with that IR (wet only)
// Every M2 sound is synthesised at runtime (RawBreakAudioDsp): no sound waves. The C++ side works without these assets (the
// voices then play straight to the engine's main submix, without reverb, limiter or pause filter). Owner: M2-C.

#include "CoreMinimal.h"

#include "Core/RbTypes.h"

enum class ERbAudioBus : uint8
{
	Master,
	World,
	Table,
	Foley,
	Ambience,
	Crowd,
	Voice,
	Jukebox,
	UI,
	MenuMusic,
	Count,
};

namespace RbAudioAssets
{
	inline const TCHAR* const MixDir = TEXT("/Game/Generated/Audio/Mix");
	inline const TCHAR* const IrDir = TEXT("/Game/Generated/Audio/IR");

	inline const TCHAR* BusName(ERbAudioBus Bus)
	{
		switch (Bus)
		{
		case ERbAudioBus::Master: return TEXT("Master");
		case ERbAudioBus::World: return TEXT("World");
		case ERbAudioBus::Table: return TEXT("Table");
		case ERbAudioBus::Foley: return TEXT("Foley");
		case ERbAudioBus::Ambience: return TEXT("Ambience");
		case ERbAudioBus::Crowd: return TEXT("Crowd");
		case ERbAudioBus::Voice: return TEXT("Voice");
		case ERbAudioBus::Jukebox: return TEXT("Jukebox");
		case ERbAudioBus::UI: return TEXT("UI");
		case ERbAudioBus::MenuMusic: return TEXT("MenuMusic");
		default: return TEXT("?");
		}
	}

	inline const TCHAR* VenueName(ERbVenue Venue)
	{
		return Venue == ERbVenue::DiveBar ? TEXT("DiveBar") : TEXT("TestRoom");
	}

	// Object paths ("/Game/.../Name.Name").
	inline FString SubmixPath(ERbAudioBus Bus)
	{
		return FString::Printf(TEXT("%s/SUBM_RB_%s.SUBM_RB_%s"), MixDir, BusName(Bus), BusName(Bus));
	}
	inline FString ReverbSubmixPath(ERbVenue Venue)
	{
		return FString::Printf(TEXT("%s/SUBM_RB_Reverb_%s.SUBM_RB_Reverb_%s"), MixDir, VenueName(Venue), VenueName(Venue));
	}
	inline FString ImpulseResponsePath(ERbVenue Venue)
	{
		return FString::Printf(TEXT("%s/IR_RB_%s.IR_RB_%s"), IrDir, VenueName(Venue), VenueName(Venue));
	}
	inline FString ConvolutionPresetPath(ERbVenue Venue)
	{
		return FString::Printf(TEXT("%s/CRV_RB_%s.CRV_RB_%s"), MixDir, VenueName(Venue), VenueName(Venue));
	}
	inline FString LimiterPresetPath() { return FString::Printf(TEXT("%s/DYN_RB_MasterLimiter.DYN_RB_MasterLimiter"), MixDir); }
	inline FString PauseLowPassPresetPath() { return FString::Printf(TEXT("%s/FLT_RB_PauseLowPass.FLT_RB_PauseLowPass"), MixDir); }

	// Audio anchor tags (RbAssetPaths::Tag::AudioAnchor) the ambience looks for in a venue level (M2-A places them; otherwise the
	// venue profile's default positions are used): RbAudio_Hvac (ceiling diffusers), RbAudio_Cooler (compressors), RbAudio_Neon.
	inline const TCHAR* const AnchorHvac = TEXT("Hvac");
	inline const TCHAR* const AnchorCooler = TEXT("Cooler");
	inline const TCHAR* const AnchorNeon = TEXT("Neon");
}
