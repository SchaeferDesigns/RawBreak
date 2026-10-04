#include "Audio/RbAudioSubsystem.h"

#include "Audio/RbAmbienceVoiceComponent.h"
#include "Audio/RbAudioLive.h"
#include "Audio/RbAudioPlan.h"
#include "Audio/RbAudioLog.h"
#include "Audio/RbAudioSettings.h"
#include "Audio/RbImpactVoiceComponent.h"
#include "Audio/RbTableAudioComponent.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbLooseBallSubsystem.h"
#include "Camera/RbCameraRigComponent.h"
#include "Core/RbAssetPaths.h"
#include "Game/RbTableSubsystem.h"
#include "Settings/RbGameUserSettings.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"
#include "Venue/RbVenueInfo.h"

#include "RbAudio/RbAudioMath.h"
#include "RbAudio/RbNoiseSynth.h"
#include "RbAudio/RbPresentation.h"

#include "AudioDevice.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundEffectSubmix.h"
#include "Sound/SoundSubmix.h"

// Owner: M2-C.

DEFINE_LOG_CATEGORY(LogRbAudio);

namespace RbAudioSubsystemPrivate
{
	constexpr int32 NumFootstepVoices = 4;
	constexpr int32 NumLooseBallVoices = 4;
	constexpr double TableRefreshSeconds = 0.5;
	constexpr double LooseRollingTimeout = 0.25; // [s] without an OnRolling sample: the ball stopped

	int64 LooseKey(int32 TableIndex, int32 BallId)
	{
		return (static_cast<int64>(TableIndex) << 32) | static_cast<uint32>(BallId);
	}

	template <typename T>
	T* LoadQuiet(const FString& Path)
	{
		return LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

}

URbAudioSubsystem* URbAudioSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URbAudioSubsystem>() : nullptr;
}

bool URbAudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId URbAudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URbAudioSubsystem, STATGROUP_Tickables);
}

bool URbAudioSubsystem::HasAudio() const
{
	const UWorld* World = GetWorld();
	return World && World->GetAudioDeviceRaw() != nullptr;
}

double URbAudioSubsystem::GetSampleRate() const
{
	const UWorld* World = GetWorld();
	const FAudioDevice* Device = World ? World->GetAudioDeviceRaw() : nullptr;
	return Device ? Device->GetSampleRate() : 48000.0;
}

USoundSubmix* URbAudioSubsystem::GetSubmix(ERbAudioBus Bus) const
{
	const int32 Index = static_cast<int32>(Bus);
	return Submixes.IsValidIndex(Index) ? Submixes[Index].Get() : nullptr;
}

double URbAudioSubsystem::GetPhysicalOutputGain() const
{
	const RbAudio::FPresentationMode Mode = RbAudio::GetPresentationMode(static_cast<RbAudio::EDynamicRangeMode>(URbAudioSettings::Get()->DynamicRange));
	return 1.0 / Mode.FullScalePa();
}

double URbAudioSubsystem::GetAmbienceVoiceGain(bool bPositional) const
{
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	return GetAmbienceOutputGain() * (bPositional ? Settings->PanCompensation : Settings->NonSpatialCompensation);
}

double URbAudioSubsystem::GetAmbienceOutputGain() const
{
	const RbAudio::FPresentationMode Mode = RbAudio::GetPresentationMode(static_cast<RbAudio::EDynamicRangeMode>(URbAudioSettings::Get()->DynamicRange));
	return RbAudio::DbToGain(Mode.AmbienceOffsetDb) / Mode.FullScalePa();
}

void URbAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bBegunPlay = true;
	LastRealSeconds = FPlatformTime::Seconds();
	if (!HasAudio())
	{
		return; // -NoSound: no device, nothing to create
	}
	LoadRouting();
	RbAudio::FShapedNoise::Prewarm(GetSampleRate()); // live rolling (loose balls) never normalises its noise on the audio thread
	Venue = ARbVenueInfo::GetVenue(&InWorld);
	Profile = RbGetVenueAudioProfile(Venue);
	ReverbSubmix = RbAudioSubsystemPrivate::LoadQuiet<USoundSubmix>(RbAudioAssets::ReverbSubmixPath(Venue));
	CreateAmbience();
	CreateLiveVoices();
	RefreshTables();
	BindFootsteps();
	BindLooseBalls();
	SettingsHandle = URbGameUserSettings::OnSettingsChanged().AddUObject(this, &URbAudioSubsystem::ApplyVolumes);
	ApplyVolumes();
	UE_LOG(LogRbAudio, Log, TEXT("Audio v1: venue %s, %d ambience layers, %d tables, generated assets %s, %.0f Hz"), RbAudioAssets::VenueName(Venue),
		AmbienceVoices.Num(), TableAudio.Num(), HasGeneratedAssets() ? TEXT("yes") : TEXT("NO (run rb_make_audio.py)"), GetSampleRate());
}

void URbAudioSubsystem::LoadRouting()
{
	Submixes.SetNum(static_cast<int32>(ERbAudioBus::Count));
	for (int32 B = 0; B < static_cast<int32>(ERbAudioBus::Count); ++B)
	{
		Submixes[B] = RbAudioSubsystemPrivate::LoadQuiet<USoundSubmix>(RbAudioAssets::SubmixPath(static_cast<ERbAudioBus>(B)));
	}
	PauseLowPass = RbAudioSubsystemPrivate::LoadQuiet<USoundEffectSubmixPreset>(RbAudioAssets::PauseLowPassPresetPath());
}

AActor* URbAudioSubsystem::GetEmitterActor()
{
	if (EmitterActor)
	{
		return EmitterActor;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Name = MakeUniqueObjectName(World->PersistentLevel, AActor::StaticClass(), TEXT("RbAudioEmitters"));
	Params.ObjectFlags = RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!Actor)
	{
		return nullptr;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"), RF_Transient);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	EmitterActor = Actor;
	return Actor;
}

void URbAudioSubsystem::SetVenue(ERbVenue NewVenue)
{
	Venue = NewVenue;
	Profile = RbGetVenueAudioProfile(Venue);
	if (!HasAudio())
	{
		return;
	}
	ReverbSubmix = RbAudioSubsystemPrivate::LoadQuiet<USoundSubmix>(RbAudioAssets::ReverbSubmixPath(Venue));
	DestroyAmbience();
	CreateAmbience();
	// The table voices take the new reverb routing when they are created again.
	for (URbTableAudioComponent* Audio : TableAudio)
	{
		if (Audio)
		{
			FRbTableAudioRouting Routing;
			Routing.TableSubmix = GetSubmix(ERbAudioBus::Table);
			Routing.ReverbSubmix = ReverbSubmix;
			Routing.ReverbSend = URbAudioSettings::Get()->RefDistanceMeters * URbAudioSettings::Get()->ReverbSendScale * Profile.VoiceReverbSend;
			Routing.ReverbSendGain = TableSendGain;
			Audio->SetRouting(Routing);
			const ERbTableAudioTier Tier = Audio->GetTier();
			Audio->SetTier(ERbTableAudioTier::Off);
			Audio->SetTier(Tier);
		}
	}
	DestroyLiveVoices();
	CreateLiveVoices();
}

void URbAudioSubsystem::CreateLiveVoices()
{
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	const float Send = static_cast<float>(Settings->RefDistanceMeters * Settings->ReverbSendScale * Profile.VoiceReverbSend);
	for (int32 I = FootstepVoices.Num(); I < RbAudioSubsystemPrivate::NumFootstepVoices; ++I)
	{
		if (URbImpactVoiceComponent* Voice = CreateLiveVoice(FName(*FString::Printf(TEXT("RbFootstep%d"), I)), GetSubmix(ERbAudioBus::Foley), Send, FoleySendGain))
		{
			FootstepVoices.Add(Voice);
		}
	}
	for (int32 I = LooseBallVoices.Num(); I < RbAudioSubsystemPrivate::NumLooseBallVoices; ++I)
	{
		if (URbImpactVoiceComponent* Voice = CreateLiveVoice(FName(*FString::Printf(TEXT("RbLooseBall%d"), I)), GetSubmix(ERbAudioBus::Table), Send, TableSendGain))
		{
			LooseBallVoices.Add(Voice);
			LooseBallVoiceKeys.Add(INDEX_NONE);
			LooseBallVoiceLastUse.Add(0.0);
		}
	}
}

void URbAudioSubsystem::DestroyLiveVoices()
{
	for (URbImpactVoiceComponent* Voice : FootstepVoices)
	{
		if (Voice)
		{
			Voice->Stop();
			Voice->DestroyComponent();
		}
	}
	FootstepVoices.Reset();
	for (URbImpactVoiceComponent* Voice : LooseBallVoices)
	{
		if (Voice)
		{
			Voice->Stop();
			Voice->DestroyComponent();
		}
	}
	LooseBallVoices.Reset();
	LooseBallVoiceKeys.Reset();
	LooseBallVoiceLastUse.Reset();
	LooseBallLastRolling.Reset();
}

void URbAudioSubsystem::CreateAmbience()
{
	AActor* Owner = GetEmitterActor();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	// The level's audio anchors (actors tagged RbAudio_<Name>: M2-A's layout.json "audio_anchors" in the dive bar).
	TArray<FRbAudioAnchor> Anchors;
	const FString TagPrefix = RbAssetPaths::Tag::AudioAnchor(TEXT("")).ToString();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			const FString TagName = Tag.ToString();
			if (TagName.StartsWith(TagPrefix, ESearchCase::IgnoreCase) && TagName.Len() > TagPrefix.Len())
			{
				Anchors.Add({TagName.RightChop(TagPrefix.Len()), It->GetActorLocation()});
			}
		}
	}
	TArray<FRbAmbiencePlacement> Placements;
	RbPlaceAmbience(Profile, Anchors, Placements);
	int32 AtAnchors = 0;
	for (const FRbAmbiencePlacement& Placement : Placements)
	{
		const RbAudio::FAmbienceLayerDesc& Desc = Placement.Layer;
		URbAmbienceVoiceComponent* Voice = NewObject<URbAmbienceVoiceComponent>(Owner, MakeUniqueObjectName(Owner, URbAmbienceVoiceComponent::StaticClass(),
			FName(*FString::Printf(TEXT("RbAmb_%s"), *Placement.Name))), RF_Transient);
		const bool bPositional = Desc.NumChannels() == 1;
		Voice->SetupAttachment(Owner->GetRootComponent());
		Voice->ConfigureLayer(Desc, GetSubmix(ERbAudioBus::Ambience), bPositional ? ReverbSubmix.Get() : nullptr,
			static_cast<float>(Settings->RefDistanceMeters * Settings->ReverbSendScale * Profile.AmbienceReverbSend), Settings->RefDistanceMeters,
			Settings->AttenuationRangeMeters);
		Voice->SetOutputGain(GetAmbienceVoiceGain(bPositional));
		Voice->SetReverbSendGain(AmbienceSendGain);
		Voice->RegisterComponent();
		Voice->SetWorldLocation(Placement.LocationCm);
		Voice->StartVoice();
		AmbienceVoices.Add(Voice);
		AtAnchors += Placement.bAtAnchor ? 1 : 0;
	}
	UE_LOG(LogRbAudio, Log, TEXT("Audio v1: %d room-tone layers, %d of them at the level's RbAudio_* anchors (%d anchors in the level)"), Placements.Num(),
		AtAnchors, Anchors.Num());
}

void URbAudioSubsystem::DestroyAmbience()
{
	for (URbAmbienceVoiceComponent* Voice : AmbienceVoices)
	{
		if (Voice)
		{
			Voice->Stop();
			Voice->DestroyComponent();
		}
	}
	AmbienceVoices.Reset();
}

URbTableAudioComponent* URbAudioSubsystem::GetOrCreateTableAudio(ARbTable* Table)
{
	if (!Table)
	{
		return nullptr;
	}
	URbTableAudioComponent* Audio = Table->FindComponentByClass<URbTableAudioComponent>();
	if (!Audio)
	{
		Audio = NewObject<URbTableAudioComponent>(Table, TEXT("RbTableAudio"), RF_Transient);
		FRbTableAudioRouting Routing;
		Routing.TableSubmix = GetSubmix(ERbAudioBus::Table);
		Routing.ReverbSubmix = ReverbSubmix;
		Routing.ReverbSend = URbAudioSettings::Get()->RefDistanceMeters * URbAudioSettings::Get()->ReverbSendScale * Profile.VoiceReverbSend;
		Routing.ReverbSendGain = TableSendGain;
		Audio->SetRouting(Routing);
		Audio->RegisterComponent();
		TableAudio.Add(Audio);
		if (HasAudio())
		{
			URbTableSubsystem* Tables = URbTableSubsystem::Get(Table);
			const bool bPlayer = Tables && Tables->GetPlayerTable() == Table;
			const URbAudioSettings* Settings = URbAudioSettings::Get();
			Audio->SetTier(TierForDistance(DistanceToTable(*Table, GetListenerLocation()), bPlayer, ERbTableAudioTier::T2, Settings->TierT1Meters,
				Settings->TierT2Meters));
		}
	}
	if (!Audio->GetBoundPlayback())
	{
		if (URbTableSubsystem* Tables = URbTableSubsystem::Get(Table))
		{
			if (ARbBallSet* Balls = Tables->FindBallSet(Table))
			{
				Audio->BindPlayback(Balls->GetPlayback());
			}
		}
	}
	return Audio;
}

void URbAudioSubsystem::RefreshTables()
{
	if (URbTableSubsystem* Tables = URbTableSubsystem::Get(this))
	{
		for (ARbTable* Table : Tables->GetTables())
		{
			GetOrCreateTableAudio(Table);
		}
	}
	TableAudio.RemoveAll([](const TObjectPtr<URbTableAudioComponent>& A) { return !IsValid(A); });
}

ERbTableAudioTier URbAudioSubsystem::TierForDistance(double Distance, bool bPlayerTable, ERbTableAudioTier Current, double T1Meters, double T2Meters)
{
	if (bPlayerTable)
	{
		return ERbTableAudioTier::T0;
	}
	constexpr double Half = 0.5; // 1 m hysteresis band around each boundary
	const int32 Cur = Current == ERbTableAudioTier::Off ? 2 : static_cast<int32>(Current);
	const double B1 = T1Meters + (Cur <= 0 ? Half : -Half); // T0 | T1
	const double B2 = T2Meters + (Cur <= 1 ? Half : -Half); // T1 | T2
	if (Distance < B1)
	{
		return ERbTableAudioTier::T0;
	}
	return Distance < B2 ? ERbTableAudioTier::T1 : ERbTableAudioTier::T2;
}

double URbAudioSubsystem::DistanceToTable(const ARbTable& Table, const FVector& WorldPoint)
{
	if (!Table.HasContext())
	{
		return (WorldPoint - Table.GetActorLocation()).Size() / 100.0;
	}
	const rb::Vec3 P = Table.WorldToCore(WorldPoint);
	const rb::TableSpec& Spec = Table.GetContext().Spec;
	const double Dx = FMath::Max(0.0, FMath::Abs(P.x) - 0.5 * Spec.Length);
	const double Dy = FMath::Max(0.0, FMath::Abs(P.y) - 0.5 * Spec.Width);
	return FMath::Sqrt(Dx * Dx + Dy * Dy);
}

FVector URbAudioSubsystem::GetListenerLocation() const
{
	if (const UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector Location, Front, Right;
			PC->GetAudioListenerPosition(Location, Front, Right);
			return Location;
		}
	}
	return FVector::ZeroVector;
}

void URbAudioSubsystem::UpdateTiers()
{
	if (!bAutoTiers)
	{
		return;
	}
	URbTableSubsystem* Tables = URbTableSubsystem::Get(this);
	const ARbTable* PlayerTable = Tables ? Tables->GetPlayerTable() : nullptr;
	const FVector Listener = GetListenerLocation();
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	for (URbTableAudioComponent* Audio : TableAudio)
	{
		ARbTable* Table = Audio ? Audio->GetTable() : nullptr;
		if (!Table)
		{
			continue;
		}
		const ERbTableAudioTier Tier = TierForDistance(DistanceToTable(*Table, Listener), Table == PlayerTable, Audio->GetTier(), Settings->TierT1Meters,
			Settings->TierT2Meters);
		if (Tier != Audio->GetTier())
		{
			Audio->SetTier(Tier);
		}
	}
}

void URbAudioSubsystem::BindFootsteps()
{
	const UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	URbCameraRigComponent* Rig = Pawn ? Pawn->FindComponentByClass<URbCameraRigComponent>() : nullptr;
	if (Rig == BoundRig.Get())
	{
		return;
	}
	if (URbCameraRigComponent* Old = BoundRig.Get())
	{
		Old->OnFootstep.Remove(FootstepHandle);
	}
	BoundRig = Rig;
	if (Rig)
	{
		FootstepHandle = Rig->OnFootstep.AddUObject(this, &URbAudioSubsystem::HandleFootstep);
	}
}

void URbAudioSubsystem::BindLooseBalls()
{
	if (LooseImpactHandle.IsValid())
	{
		return;
	}
	if (URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(this))
	{
		LooseImpactHandle = Loose->OnImpact.AddUObject(this, &URbAudioSubsystem::HandleLooseBallImpact);
		LooseRollingHandle = Loose->OnRolling.AddUObject(this, &URbAudioSubsystem::HandleLooseBallRolling);
		LooseReturnedHandle = Loose->OnReturned.AddUObject(this, &URbAudioSubsystem::HandleLooseBallReturned);
	}
}

URbImpactVoiceComponent* URbAudioSubsystem::CreateLiveVoice(const FName& Name, USoundSubmix* Submix, float ReverbSend, float ReverbSendGain)
{
	AActor* Owner = GetEmitterActor();
	if (!Owner)
	{
		return nullptr;
	}
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	URbImpactVoiceComponent* Voice = NewObject<URbImpactVoiceComponent>(Owner, MakeUniqueObjectName(Owner, URbImpactVoiceComponent::StaticClass(), Name),
		RF_Transient);
	Voice->SetupAttachment(Owner->GetRootComponent());
	Voice->ConfigureVoice(true, Submix, ReverbSubmix, ReverbSend, Settings->RefDistanceMeters, Settings->AttenuationRangeMeters);
	Voice->SetReverbSendGain(ReverbSendGain);
	Voice->RegisterComponent();
	Voice->SetLiveOutputGain(GetPhysicalOutputGain() * Settings->PanCompensation);
	Voice->StartVoice();
	return Voice;
}

void URbAudioSubsystem::ApplyLiveOutputGains()
{
	const double Gain = GetPhysicalOutputGain() * URbAudioSettings::Get()->PanCompensation;
	for (URbImpactVoiceComponent* Voice : FootstepVoices)
	{
		if (Voice)
		{
			Voice->SetLiveOutputGain(Gain);
		}
	}
	for (URbImpactVoiceComponent* Voice : LooseBallVoices)
	{
		if (Voice)
		{
			Voice->SetLiveOutputGain(Gain);
		}
	}
}

void URbAudioSubsystem::HandleFootstep(const FRbFootstep& Step)
{
	PlayFootstep(Step, true);
}

void URbAudioSubsystem::PlayFootstep(const FRbFootstep& Step, bool bOwnSteps)
{
	if (!HasAudio())
	{
		return;
	}
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	CreateLiveVoices(); // normally created at begin play; only a failed creation retries here
	if (FootstepVoices.Num() == 0)
	{
		return;
	}
	URbImpactVoiceComponent* Voice = FootstepVoices[NextFootstepVoice % FootstepVoices.Num()];
	NextFootstepVoice = (NextFootstepVoice + 1) % FootstepVoices.Num();
	if (!Voice)
	{
		return;
	}
	// Surface under the foot: the floor's physical material when M2-E / M2-A give it (a short trace), else the venue's floor.
	RbAudio::EFloorSurface Surface = Profile.Floor;
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(RbFootstepSurface), false);
		Query.bReturnPhysicalMaterial = true;
		if (const URbCameraRigComponent* Rig = BoundRig.Get())
		{
			Query.AddIgnoredActor(Rig->GetOwner()); // the walker's own capsule / body must not be the "floor" under its foot
		}
		if (World->LineTraceSingleByChannel(Hit, Step.WorldLocation + FVector(0.0, 0.0, 20.0), Step.WorldLocation - FVector(0.0, 0.0, 30.0), ECC_Visibility, Query)
			&& Hit.PhysMaterial.IsValid())
		{
			Surface = RbFloorSurfaceFor(Hit.PhysMaterial->SurfaceType, Profile.Floor);
		}
	}
	RbAudio::FFootstepParams Params;
	Params.Surface = Surface;
	Params.SpeedMps = Step.SpeedMps > 0.0f ? Step.SpeedMps : 1.4;
	Params.bLeftFoot = Step.bLeftFoot;
	Params.bOwnSteps = bOwnSteps;
	Params.Seed = RbAudio::HashMix(0xF007'57E9ull, ++LiveSeed);
	TArray<float> Pcm;
	RbAudioLive::RenderFootstep(Params, Settings->RefDistanceMeters, GetSampleRate(), Pcm);
	Voice->SetWorldLocation(Step.WorldLocation);
	Voice->AddLivePcm(MoveTemp(Pcm));
	++FootstepsPlayed;
}

URbImpactVoiceComponent* URbAudioSubsystem::LooseBallVoice(int32 TableIndex, int32 BallId, bool bAssign)
{
	const int64 Key = RbAudioSubsystemPrivate::LooseKey(TableIndex, BallId);
	const double Now = FPlatformTime::Seconds();
	int32 Slot = LooseBallVoiceKeys.Find(Key);
	if (Slot == INDEX_NONE)
	{
		if (!bAssign)
		{
			return nullptr;
		}
		CreateLiveVoices(); // normally created at begin play; only a failed creation retries here
		// A free voice, else the least recently used one (its ball's rolling stops).
		for (int32 I = 0; I < LooseBallVoices.Num(); ++I)
		{
			if (LooseBallVoiceKeys[I] == INDEX_NONE)
			{
				Slot = I;
				break;
			}
			if (Slot == INDEX_NONE || LooseBallVoiceLastUse[I] < LooseBallVoiceLastUse[Slot])
			{
				Slot = I;
			}
		}
		if (Slot == INDEX_NONE)
		{
			return nullptr;
		}
		if (LooseBallVoiceKeys[Slot] != INDEX_NONE)
		{
			LooseBallLastRolling.Remove(LooseBallVoiceKeys[Slot]);
			if (LooseBallVoices[Slot])
			{
				LooseBallVoices[Slot]->SetLiveContinuous(RbAudio::ENoiseKind::RollingFloor, 0.0, 0.0);
			}
		}
		LooseBallVoiceKeys[Slot] = Key;
	}
	LooseBallVoiceLastUse[Slot] = Now;
	return IsValid(LooseBallVoices[Slot]) ? LooseBallVoices[Slot].Get() : nullptr;
}

void URbAudioSubsystem::HandleLooseBallImpact(const FRbLooseBallImpact& Impact)
{
	if (!HasAudio())
	{
		return;
	}
	URbImpactVoiceComponent* Voice = LooseBallVoice(Impact.TableIndex, Impact.BallId, true);
	if (!Voice)
	{
		return;
	}
	FRbFloorHitParams Params;
	Params.NormalSpeed = Impact.NormalSpeed > 0.0 ? Impact.NormalSpeed : Impact.NormalImpulse / FMath::Max(Impact.MassKg, 1e-3);
	// The acoustic ball is the table's ball (its kernels are prewarmed with the table's voices), never the physics body's mass: an
	// exact-key cache miss would design a new kernel set on the game thread (~30 ms hitch) at every new mass value (review M2-C).
	const FRbTableContext* Context = nullptr;
	if (URbTableSubsystem* Tables = URbTableSubsystem::Get(this))
	{
		if (const ARbTable* Table = Tables->FindTable(Impact.TableIndex))
		{
			Context = Table->HasContext() ? &Table->GetContext() : nullptr;
		}
	}
	const RbAudio::FBallAcoustics Ball = RbAudioLive::LooseBallAcoustics(Context, Impact.BallId);
	Params.BallMass = Ball.Mass;
	Params.BallRadius = Ball.Radius;
	Params.Surface = RbFloorSurfaceFor(Impact.Surface, Profile.Floor);
	Params.PlaneNormal = Impact.Normal;
	Params.ContactPointCm = Impact.WorldLocation;
	Params.ListenerCm = GetListenerLocation();
	Params.RefDistance = URbAudioSettings::Get()->RefDistanceMeters;
	Params.Seed = RbAudio::HashMix(0xF100'4817ull, ++LiveSeed);
	TArray<float> Pcm;
	if (RbAudioLive::RenderFloorHit(Params, GetSampleRate(), Pcm))
	{
		// Loose balls belong to the table stem: the static curve of the presentation mode (audio.md 4.2) for this one impact, from
		// its peak at the listener (a ball dropping off the rail hits the floor at ~4 m/s: ~120 dB SPL at 1 m, far above the knee).
		const RbAudio::FPresentationMode Mode = RbAudio::GetPresentationMode(static_cast<RbAudio::EDynamicRangeMode>(URbAudioSettings::Get()->DynamicRange));
		float PeakPa = 0.0f;
		for (const float S : Pcm)
		{
			PeakPa = FMath::Max(PeakPa, FMath::Abs(S));
		}
		const double Distance = FMath::Max((Impact.WorldLocation - Params.ListenerCm).Size() / 100.0, Params.RefDistance);
		double LouderEar = 1.0;
		if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			FVector Location, Front, Right;
			PC->GetAudioListenerPosition(Location, Front, Right);
			LouderEar = RbLouderEarPanGain(rb::Vec3(Impact.WorldLocation.X, Impact.WorldLocation.Y, Impact.WorldLocation.Z),
				rb::Vec3(Location.X, Location.Y, Location.Z), rb::Vec3(Right.X, Right.Y, Right.Z));
		}
		const double AtListenerPa = static_cast<double>(PeakPa) * Params.RefDistance / Distance * LouderEar;
		const float Gain = static_cast<float>(RbAudio::DbToGain(Mode.GainDb(RbAudio::PaToDbSpl(FMath::Max(AtListenerPa, 1e-9)))));
		if (Gain < 1.0f)
		{
			for (float& S : Pcm)
			{
				S *= Gain;
			}
		}
		Voice->SetWorldLocation(Impact.WorldLocation + Impact.Normal * 100.0 * Params.BallRadius);
		Voice->AddLivePcm(MoveTemp(Pcm));
		++FloorHitsPlayed;
	}
}

void URbAudioSubsystem::HandleLooseBallRolling(const FRbLooseBallRolling& Rolling)
{
	if (!HasAudio())
	{
		return;
	}
	URbImpactVoiceComponent* Voice = LooseBallVoice(Rolling.TableIndex, Rolling.BallId, Rolling.SpeedMps > 0.01);
	if (!Voice)
	{
		return;
	}
	const RbAudio::EFloorSurface Surface = RbFloorSurfaceFor(Rolling.Surface, Profile.Floor);
	// Rolling on the floor (AU-25): VCT / concrete +12 dB over cloth at equal speed, rubber mats about cloth level.
	const double SurfaceFactor = Surface == RbAudio::EFloorSurface::Rubber ? 1.0 : RbAudio::FloorRollingFactor;
	Voice->SetWorldLocation(Rolling.WorldLocation);
	Voice->SetLiveContinuous(RbAudio::ENoiseKind::RollingFloor, Rolling.SpeedMps,
		RbAudio::RollingRmsPerMps * SurfaceFactor / URbAudioSettings::Get()->RefDistanceMeters);
	LooseBallLastRolling.Add(RbAudioSubsystemPrivate::LooseKey(Rolling.TableIndex, Rolling.BallId), FPlatformTime::Seconds());
}

void URbAudioSubsystem::HandleLooseBallReturned(int32 TableIndex, int32 BallId)
{
	const int64 Key = RbAudioSubsystemPrivate::LooseKey(TableIndex, BallId);
	if (URbImpactVoiceComponent* Voice = LooseBallVoice(TableIndex, BallId, false))
	{
		Voice->SetLiveContinuous(RbAudio::ENoiseKind::RollingFloor, 0.0, 0.0);
	}
	LooseBallLastRolling.Remove(Key);
	const int32 Slot = LooseBallVoiceKeys.Find(Key);
	if (Slot != INDEX_NONE)
	{
		LooseBallVoiceKeys[Slot] = INDEX_NONE; // the ball is back on the table: its voice is free (its last sound rings out)
	}
}

void URbAudioSubsystem::ApplyVolumes()
{
	UWorld* World = GetWorld();
	if (!World || !HasAudio())
	{
		return;
	}
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	FRbAudioVolumes Volumes;
	if (const URbGameUserSettings* User = URbGameUserSettings::Get())
	{
		Volumes = User->Volumes;
	}
	const float Exp = Settings->VolumeTaperExponent;
	auto G = [Exp](float Slider) { return URbAudioSettings::VolumeToGain(Slider, Exp); };
	auto Set = [&](ERbAudioBus Bus, float Gain)
	{
		if (USoundSubmix* Submix = GetSubmix(Bus))
		{
			Submix->SetSubmixOutputVolume(World, Gain);
		}
	};
	Set(ERbAudioBus::Master, G(Volumes.Master));
	Set(ERbAudioBus::World, PauseGain);
	Set(ERbAudioBus::Table, G(Volumes.Table));
	Set(ERbAudioBus::Foley, G(Volumes.Ambience));      // footsteps and the world's foley follow the Ambience slider (audio.md 7.2)
	Set(ERbAudioBus::Ambience, G(Volumes.Ambience) * ReplayGain);
	Set(ERbAudioBus::Crowd, G(Volumes.Ambience) * ReplayGain);
	Set(ERbAudioBus::Voice, G(Volumes.Voices));
	Set(ERbAudioBus::Jukebox, G(Volumes.Music));
	Set(ERbAudioBus::MenuMusic, G(Volumes.Music));
	Set(ERbAudioBus::UI, G(Volumes.Interface));
	TableSendGain = G(Volumes.Table);
	FoleySendGain = G(Volumes.Ambience);
	AmbienceSendGain = G(Volumes.Ambience) * ReplayGain;
	ApplyReverbSendGains();
	// The dynamic-range mode may have changed with the settings: the live voices and the ambience follow at once (the table
	// voices take it with their next plan).
	ApplyLiveOutputGains();
	for (URbAmbienceVoiceComponent* Voice : AmbienceVoices)
	{
		if (Voice)
		{
			Voice->SetOutputGain(GetAmbienceVoiceGain(Voice->GetLayerDesc().NumChannels() == 1));
		}
	}
}

void URbAudioSubsystem::ApplyReverbSendGains()
{
	for (URbTableAudioComponent* Audio : TableAudio)
	{
		if (Audio)
		{
			Audio->SetReverbSendGain(TableSendGain);
		}
	}
	for (URbImpactVoiceComponent* Voice : FootstepVoices)
	{
		if (Voice)
		{
			Voice->SetReverbSendGain(FoleySendGain);
		}
	}
	for (URbImpactVoiceComponent* Voice : LooseBallVoices)
	{
		if (Voice)
		{
			Voice->SetReverbSendGain(TableSendGain);
		}
	}
	for (URbAmbienceVoiceComponent* Voice : AmbienceVoices)
	{
		if (Voice)
		{
			Voice->SetReverbSendGain(AmbienceSendGain);
		}
	}
}

void URbAudioSubsystem::SetPausedMix(bool bPaused)
{
	bPausedMix = bPaused;
}

bool URbAudioSubsystem::IsPausedMix() const
{
	const UWorld* World = GetWorld();
	return bPausedMix || (World && World->IsPaused());
}

float URbAudioSubsystem::StepMixGain(float Gain, bool bActive, double DepthDb, double AttackSeconds, double ReleaseSeconds, double Dt)
{
	// Linear in dB: the mix's whole depth in its stage time, both ways (a release of a -12 dB mix over 0.3 s is 40 dB/s; review M2-C:
	// the release used a 60 dB span and was 5 x faster than audio.md 7.3).
	const double TargetDb = bActive ? DepthDb : 0.0;
	const double CurrentDb = RbAudio::GainToDb(FMath::Max(Gain, 1e-6f));
	const double Seconds = FMath::Max(bActive ? AttackSeconds : ReleaseSeconds, 1e-3);
	const double Rate = FMath::Max(FMath::Abs(DepthDb), 1.0) / Seconds;
	double NewDb = CurrentDb + FMath::Clamp(TargetDb - CurrentDb, -Rate * Dt, Rate * Dt);
	if (FMath::Abs(NewDb - TargetDb) < 0.01)
	{
		NewDb = TargetDb;
	}
	return NewDb >= -0.001 ? 1.0f : static_cast<float>(RbAudio::DbToGain(NewDb));
}

void URbAudioSubsystem::UpdateMixStates(double Dt)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const URbAudioSettings* Settings = URbAudioSettings::Get();
	bWorldPausedMix = World->IsPaused();
	const bool bPause = IsPausedMix();
	bool bReplay = false;
	for (const URbTableAudioComponent* Audio : TableAudio)
	{
		bReplay |= Audio && Audio->IsPlayingReplay();
	}
	bReplayMix = bReplay;

	// Level ramps in dB (attack / release times of audio.md 7.3).
	auto Ramp = [Dt](float& Gain, bool bActive, double DepthDb, double AttackSeconds, double ReleaseSeconds) -> bool
	{
		const float NewGain = StepMixGain(Gain, bActive, DepthDb, AttackSeconds, ReleaseSeconds, Dt);
		const bool bChanged = NewGain != Gain;
		Gain = NewGain;
		return bChanged;
	};
	bool bChanged = Ramp(PauseGain, bPause, Settings->PauseLevelDb, Settings->PauseAttackSeconds, Settings->PauseReleaseSeconds);
	bChanged |= Ramp(ReplayGain, bReplay, Settings->ReplayAmbienceDb, 0.3, 0.3);
	if (bChanged)
	{
		ApplyVolumes();
	}
	// World low-pass of the pause mix (a crossfaded effect-chain override on SUBM_RB_World).
	if (bPause != bPauseFilterOn)
	{
		bPauseFilterOn = bPause;
		if (USoundSubmix* WorldSubmix = GetSubmix(ERbAudioBus::World))
		{
			if (bPause && PauseLowPass)
			{
				TArray<USoundEffectSubmixPreset*> Chain;
				Chain.Add(PauseLowPass);
				UAudioMixerBlueprintLibrary::SetSubmixEffectChainOverride(World, WorldSubmix, Chain, Settings->PauseAttackSeconds);
			}
			else if (!bPause)
			{
				UAudioMixerBlueprintLibrary::ClearSubmixEffectChainOverride(World, WorldSubmix, Settings->PauseReleaseSeconds);
			}
		}
	}
}

void URbAudioSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bBegunPlay || !HasAudio())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const double Dt = FMath::Clamp(Now - LastRealSeconds, 0.0, 0.25);
	LastRealSeconds = Now;
	if (Now >= NextTableRefresh)
	{
		NextTableRefresh = Now + RbAudioSubsystemPrivate::TableRefreshSeconds;
		RefreshTables();
		BindFootsteps();
		BindLooseBalls();
		RestartStalledVoices(Now);
	}
	UpdateTiers();
	UpdateMixStates(Dt);
	// Loose balls that stopped sending rolling samples came to rest (or were picked up): fade their rolling out.
	for (auto It = LooseBallLastRolling.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() > RbAudioSubsystemPrivate::LooseRollingTimeout)
		{
			const int32 Slot = LooseBallVoiceKeys.Find(It.Key());
			if (Slot != INDEX_NONE && IsValid(LooseBallVoices[Slot]))
			{
				LooseBallVoices[Slot]->SetLiveContinuous(RbAudio::ENoiseKind::RollingFloor, 0.0, 0.0);
			}
			It.RemoveCurrent();
		}
	}
}

int32 URbAudioSubsystem::RestartStalledVoices(double Now)
{
	constexpr double Grace = 1.5; // [s] a new source takes a few blocks; a stalled one never starts
	int32 Restarted = 0;
	auto Check = [&](USynthComponent* Voice, bool bRendering, TFunctionRef<void()> Restart)
	{
		if (!Voice || !Voice->IsActive())
		{
			return;
		}
		const TWeakObjectPtr<UObject> Key(Voice);
		if (bRendering)
		{
			StalledSince.Remove(Key);
			return;
		}
		const double* Since = StalledSince.Find(Key);
		if (!Since)
		{
			StalledSince.Add(Key, Now);
		}
		else if (Now - *Since > Grace)
		{
			Voice->Stop();
			Restart();
			StalledSince.Remove(Key);
			++Restarted;
		}
	};
	for (URbTableAudioComponent* Audio : TableAudio)
	{
		if (!Audio)
		{
			continue;
		}
		for (URbImpactVoiceComponent* Voice : Audio->GetVoices())
		{
			Check(Voice, Voice && Voice->IsRendering(), [Voice]() { Voice->StartVoice(); });
		}
		if (URbImpactVoiceComponent* Feed = Audio->GetReverbFeedVoice())
		{
			Check(Feed, Feed->IsRendering(), [Feed]() { Feed->StartVoice(); });
		}
	}
	for (URbAmbienceVoiceComponent* Voice : AmbienceVoices)
	{
		Check(Voice, Voice && Voice->IsRendering(), [Voice]() { Voice->StartVoice(); });
	}
	for (URbImpactVoiceComponent* Voice : FootstepVoices)
	{
		Check(Voice, Voice && Voice->IsRendering(), [Voice]() { Voice->StartVoice(); });
	}
	for (URbImpactVoiceComponent* Voice : LooseBallVoices)
	{
		Check(Voice, Voice && Voice->IsRendering(), [Voice]() { Voice->StartVoice(); });
	}
	for (auto It = StalledSince.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	if (Restarted > 0)
	{
		VoicesRestarted += Restarted;
		UE_LOG(LogRbAudio, Log, TEXT("Audio v1: restarted %d stalled voices (sources that never started, e.g. while the window was unfocused)"), Restarted);
	}
	return Restarted;
}

void URbAudioSubsystem::Deinitialize()
{
	URbGameUserSettings::OnSettingsChanged().Remove(SettingsHandle);
	if (URbCameraRigComponent* Rig = BoundRig.Get())
	{
		Rig->OnFootstep.Remove(FootstepHandle);
	}
	if (URbLooseBallSubsystem* Loose = URbLooseBallSubsystem::Get(this))
	{
		Loose->OnImpact.Remove(LooseImpactHandle);
		Loose->OnRolling.Remove(LooseRollingHandle);
		Loose->OnReturned.Remove(LooseReturnedHandle);
	}
	TableAudio.Reset();
	AmbienceVoices.Reset();
	FootstepVoices.Reset();
	LooseBallVoices.Reset();
	LooseBallVoiceKeys.Reset();
	LooseBallVoiceLastUse.Reset();
	EmitterActor = nullptr;
	Super::Deinitialize();
}
