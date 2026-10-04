#pragma once

// Venue audio profiles of M2 (Docs/specs/audio.md 6.4, 12.1; venue-dive-bar 2.2, 10): the synthesised room-tone layers with their
// default emitter positions (world cm; the dive bar's world frame is the venue frame V of venue-dive-bar 2.1), the floor surface of
// the footsteps, the reverb (RT60 of the IR that Tools/audio/ir_synth.py builds) and the reverb sends. A later
// URbVenueAudioProfile data asset replaces this table (audio.md 8.2). Owner: M2-C.

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"

#include "Core/RbTypes.h"
#include "RbAudio/RbNoiseSynth.h"

struct FRbAmbienceEmitter
{
	RbAudio::FAmbienceLayerDesc Layer;
	FVector DefaultLocationCm = FVector::ZeroVector; // positional layers only
	const TCHAR* Anchor = nullptr;                   // anchor kind (RbAudioAssets::Anchor*): the level's anchors of it place the layer
	FString Name;
};

struct FRbVenueAudioProfile
{
	ERbVenue Venue = ERbVenue::TestRoom;
	TArray<FRbAmbienceEmitter> Emitters;
	RbAudio::EFloorSurface Floor = RbAudio::EFloorSurface::Concrete;
	double Rt60[3] = {0.6, 0.45, 0.4};   // low / mid / high [s] of the venue's IR (information; the IR carries it)
	// Reverb send multipliers. The send level of a physical voice is RefDistance x URbAudioSettings::ReverbSendScale x this: the
	// voices output pressure referred to RefDistance, the send refers it back to 1 m, and the venue IR (Tools/audio/ir_synth.py)
	// is scaled so that a unit direct sound at 1 m produces the room's diffuse field (energy (1 / r_c)^2, r_c = critical distance).
	float VoiceReverbSend = 1.0f;        // physical voices (table, loose balls, footsteps)
	float AmbienceReverbSend = 1.0f;     // positional room-tone layers
};

RAWBREAK_API FRbVenueAudioProfile RbGetVenueAudioProfile(ERbVenue Venue);

// An audio anchor of a level: an actor tagged RbAudio_<Name> (RbAssetPaths::Tag::AudioAnchor; M2-A's generator places one per entry
// of Art/DiveBar/layout.json "audio_anchors": RoomTone, RoomTone2, CoolerCompressor1, NeonN3, Jukebox, ...).
struct FRbAudioAnchor
{
	FString Name;                              // <Name> of the tag
	FVector LocationCm = FVector::ZeroVector;  // world
};

// One room-tone layer as it plays in a level.
struct FRbAmbiencePlacement
{
	RbAudio::FAmbienceLayerDesc Layer;
	FVector LocationCm = FVector::ZeroVector;  // positional layers only
	FString Name;
	int32 Emitter = INDEX_NONE;                // the profile emitter it comes from
	bool bAtAnchor = false;                    // placed at a level anchor (else the profile's default position)
};

// The profile anchor kind (RbAudioAssets::AnchorHvac / AnchorCooler / AnchorNeon) of a level anchor name, by prefix, case-insensitive:
// Hvac* and RoomTone* (the HVAC diffusers M16 of the dive-bar layout), Cooler* (CoolerCompressor1 / 2), Neon* (NeonN1..N5); nullptr
// for anchors of later layers (jukebox, TVs, street, ...).
RAWBREAK_API const TCHAR* RbAmbienceAnchorKind(const FString& AnchorName);

// Places the profile's layers in a level (pure). Layers without an anchor kind (the stereo bed) play at their default positions. For
// every anchor kind the level's anchors of that kind are the instances (at most RbMaxAmbienceAnchorsPerKind, by name): (emitter,
// anchor) pairs are matched closest first, each designed layer keeps its own sound (level, seed, duty-cycle start) at its anchor,
// every further anchor plays the layer of its nearest emitter with a derived seed (compressors start in the other state); profile
// emitters left without an anchor are dropped (the level has fewer of them). A kind without anchors in the level keeps the profile's
// default positions (a level in the venue frame V, or the test room). Independent of the order of Anchors.
inline constexpr int32 RbMaxAmbienceAnchorsPerKind = 6;
RAWBREAK_API void RbPlaceAmbience(const FRbVenueAudioProfile& Profile, TConstArrayView<FRbAudioAnchor> Anchors, TArray<FRbAmbiencePlacement>& Out);

// Floor surface of a physical surface type (RbAssetPaths::Surface), with the venue's floor as the fallback.
RAWBREAK_API RbAudio::EFloorSurface RbFloorSurfaceFor(EPhysicalSurface Surface, RbAudio::EFloorSurface VenueDefault);
