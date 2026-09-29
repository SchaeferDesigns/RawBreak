#pragma once

// Audio of a game world (Docs/specs/audio.md 6-8; Docs/ue-architecture.md 18.5). M2 = audio v1, fully SYNTHESISED (no library
// samples yet: the only external downloads allowed in M2 are CC0 textures): the physics sounds of every table through its
// URbTableAudioComponent, footsteps, loose-ball floor hits, the room tone and reverb of the venue, the volumes of the settings
// and the pause mix.
//
//   Tables      one URbTableAudioComponent per ARbTable (URbTableSubsystem::GetTables), created at OnWorldBeginPlay and bound
//               to the table's playback; tier by listener distance (audio.md 6.6).
//   Footsteps   URbCameraRigComponent::OnFootstep of the local pawn (M2-F fires it from the human-motion step phase) ->
//               synthesised step on the venue's floor surface (VCT on concrete in the dive bar; audio.md 2.6 AU-65).
//   Loose balls URbLooseBallSubsystem::OnImpact / OnRolling (M2-E) -> floor hits and floor rolling (AU-25).
//   Venue       ARbVenueInfo::GetVenue (M2-A; TestRoom without one): room tone bed(s) + HVAC / cooler / neon hum layers
//               (synthesised in M2), the venue reverb (convolution, IR from Tools/audio/ir_synth.py; dive bar RT60 0.8 / 0.6 /
//               0.5 s), acoustic zones later.
//   Settings    FRbAudioVolumes (URbGameUserSettings::Volumes) -> submix volumes, re-applied on OnSettingsChanged (M2-D owns
//               the storage and the sliders).
//   Mix states  pause menu (world paused): World low-pass 800 Hz, -12 dB (audio.md 7.3); replay: ambience -10 dB.
// Game and PIE worlds only; with -NoSound (headless tests) there is no audio device and everything stays silent.
// Owner: M2-C (stub by the M2 architect step; TODO(M2-C)).

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Core/RbTypes.h"

#include "RbAudioSubsystem.generated.h"

class ARbTable;
class URbTableAudioComponent;
struct FRbFootstep;
struct FRbLooseBallImpact;

UCLASS()
class RAWBREAK_API URbAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static URbAudioSubsystem* Get(const UObject* WorldContext);

	// The table's audio component (created on first use; one per table).
	URbTableAudioComponent* GetOrCreateTableAudio(ARbTable* Table);

	// The venue whose ambience / reverb plays (ARbVenueInfo, else TestRoom).
	ERbVenue GetVenue() const { return Venue; }

	// Applies URbGameUserSettings::Volumes to the submixes (also bound to OnSettingsChanged).
	void ApplyVolumes();

	// Pause-menu mix state (the UI calls it when it pauses / resumes the game; audio.md 7.3 CBM_RB_Pause).
	void SetPausedMix(bool bPaused);
	bool IsPausedMix() const { return bPausedMix; }

	// UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:
	void HandleFootstep(const FRbFootstep& Step);
	void HandleLooseBallImpact(const FRbLooseBallImpact& Impact);

	UPROPERTY(Transient)
	TArray<TObjectPtr<URbTableAudioComponent>> TableAudio;

	ERbVenue Venue = ERbVenue::TestRoom;
	bool bPausedMix = false;
	FDelegateHandle SettingsHandle;
};
