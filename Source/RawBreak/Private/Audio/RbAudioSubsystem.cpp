#include "Audio/RbAudioSubsystem.h"

#include "Audio/RbTableAudioComponent.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbLooseBallSubsystem.h"
#include "Camera/RbCameraRigComponent.h"
#include "Game/RbTableSubsystem.h"
#include "Settings/RbGameUserSettings.h"
#include "Table/RbTable.h"
#include "Venue/RbVenueInfo.h"

#include "Engine/World.h"

// Owner: M2-C. Stub of the M2 architect step: creates and binds one (silent) table-audio component per table and follows the
// settings; footsteps, loose balls, ambience, reverb and mixes are TODO(M2-C).

URbAudioSubsystem* URbAudioSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URbAudioSubsystem>() : nullptr;
}

bool URbAudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void URbAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Venue = ARbVenueInfo::GetVenue(&InWorld);
	if (URbTableSubsystem* Tables = URbTableSubsystem::Get(&InWorld))
	{
		for (ARbTable* Table : Tables->GetTables())
		{
			GetOrCreateTableAudio(Table);
		}
	}
	SettingsHandle = URbGameUserSettings::OnSettingsChanged().AddUObject(this, &URbAudioSubsystem::ApplyVolumes);
	ApplyVolumes();
	// TODO(M2-C): footsteps (URbCameraRigComponent::OnFootstep of the local pawn), URbLooseBallSubsystem::OnImpact / OnRolling,
	// venue room tone + reverb, tiers by listener distance.
}

URbTableAudioComponent* URbAudioSubsystem::GetOrCreateTableAudio(ARbTable* Table)
{
	if (!Table)
	{
		return nullptr;
	}
	if (URbTableAudioComponent* Existing = Table->FindComponentByClass<URbTableAudioComponent>())
	{
		return Existing;
	}
	URbTableAudioComponent* Audio = NewObject<URbTableAudioComponent>(Table, TEXT("RbTableAudio"));
	Audio->RegisterComponent();
	TableAudio.Add(Audio);
	// The ball set is spawned by the game mode in StartPlay; the binding is retried by M2-C when a session registers.
	if (URbTableSubsystem* Tables = URbTableSubsystem::Get(Table))
	{
		if (ARbBallSet* Balls = Tables->FindBallSet(Table))
		{
			Audio->BindPlayback(Balls->GetPlayback());
		}
	}
	return Audio;
}

void URbAudioSubsystem::ApplyVolumes()
{
	// TODO(M2-C): URbGameUserSettings::Get()->Volumes -> SUBM_RB_* output volumes (audio.md 7.1-7.2).
}

void URbAudioSubsystem::SetPausedMix(bool bPaused)
{
	bPausedMix = bPaused; // TODO(M2-C): World low-pass 800 Hz, -12 dB (audio.md 7.3)
}

void URbAudioSubsystem::HandleFootstep(const FRbFootstep& /*Step*/)
{
	// TODO(M2-C): synthesised footstep on the venue floor (AU-65).
}

void URbAudioSubsystem::HandleLooseBallImpact(const FRbLooseBallImpact& /*Impact*/)
{
	// TODO(M2-C): synthesised floor hit (AU-25).
}

void URbAudioSubsystem::Deinitialize()
{
	URbGameUserSettings::OnSettingsChanged().Remove(SettingsHandle);
	TableAudio.Reset();
	Super::Deinitialize();
}
