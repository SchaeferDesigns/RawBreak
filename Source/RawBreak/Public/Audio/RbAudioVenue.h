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
	const TCHAR* Anchor = nullptr;                   // RbAudio_<Anchor> actors replace the default positions of this layer
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

// Floor surface of a physical surface type (RbAssetPaths::Surface), with the venue's floor as the fallback.
RAWBREAK_API RbAudio::EFloorSurface RbFloorSurfaceFor(EPhysicalSurface Surface, RbAudio::EFloorSurface VenueDefault);
