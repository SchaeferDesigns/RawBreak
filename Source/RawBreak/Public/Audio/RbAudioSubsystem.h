#pragma once

// Audio of a game world (Docs/specs/audio.md 6-8; Docs/ue-architecture.md 18.5). M2 = audio v1, fully SYNTHESISED (no library
// samples yet: the only external downloads allowed in M2 are CC0 textures): the physics sounds of every table through its
// URbTableAudioComponent, footsteps, loose-ball floor hits, the room tone and reverb of the venue, the volumes of the settings
// and the pause / replay mixes.
//
//   Tables      one URbTableAudioComponent per ARbTable (URbTableSubsystem::GetTables), created at OnWorldBeginPlay and on every
//               later tick for tables / ball sets that appear later (the game mode spawns the ball sets in StartPlay, after the
//               subsystems' OnWorldBeginPlay); bound to the table's ball-set playback; tier by listener distance to the nearest
//               rail with 1 m hysteresis (audio.md 6.6: the player's table and tables < 3 m T0, 3-10 m T1, beyond T2).
//   Footsteps   URbCameraRigComponent::OnFootstep of the local pawn (M2-F fires it from the human-motion step phase) ->
//               synthesised heel / toe on the venue's floor surface (VCT on concrete in the dive bar; audio.md 2.6 AU-65) through
//               a small pool of positional foley voices (bus Foley).
//   Loose balls URbLooseBallSubsystem::OnImpact / OnRolling / OnReturned (M2-E) -> synthesised floor hits (Hertz contact on the
//               floor surface + the ball's radiation) and rolling on the floor per loose ball (AU-25; bus Table).
//   Venue       ARbVenueInfo::GetVenue (M2-A; TestRoom without one): room tone bed + HVAC diffusers / cooler compressors / neon
//               transformers as synthesised layers at the level's audio anchors (actors tagged RbAudio_<Name>: RoomTone*, Cooler*,
//               Neon* of the dive-bar layout; RbPlaceAmbience), else at the profile's default positions, the venue reverb
//               (SUBM_RB_Reverb_<Venue>: convolution with the IR of Tools/audio/ir_synth.py; dive bar RT60 0.8 / 0.6 / 0.5 s).
//   Settings    FRbAudioVolumes (URbGameUserSettings::Volumes) -> submix output volumes, re-applied on OnSettingsChanged (M2-D owns
//               the storage and the sliders): Master, Table (table voices, loose balls), Ambience (room tone, crowd, footsteps /
//               foley of the world, audio.md 7.2), Voices, Music (jukebox, menu music), Interface.
//   Mix states  pause (the UI calls SetPausedMix, and a paused world switches it on by itself): World low-pass 800 Hz and -12 dB,
//               attack 0.2 s / release 0.3 s (audio.md 7.3 CBM_RB_Pause); replay (a replay plays on any table): ambience -10 dB.
// Routing uses the generated assets of Tools/unreal/editor/rb_make_audio.py (RbAudioAssets); without them every voice plays to
// the engine's main submix (no reverb / limiter / pause filter, volumes as a gain on the voices). Game and PIE worlds only; with
// -NoSound (headless tests) there is no audio device and nothing is created. Owner: M2-C.

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Audio/RbAudioAssets.h"
#include "Audio/RbAudioSettings.h"
#include "Audio/RbAudioVenue.h"
#include "Core/RbTypes.h"

#include "RbAudioSubsystem.generated.h"

class AActor;
class ARbTable;
class URbAmbienceVoiceComponent;
class URbCameraRigComponent;
class URbImpactVoiceComponent;
class URbTableAudioComponent;
class USoundEffectSubmixPreset;
class USoundSubmix;
struct FRbFootstep;
struct FRbLooseBallImpact;
struct FRbLooseBallRolling;

UCLASS()
class RAWBREAK_API URbAudioSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static URbAudioSubsystem* Get(const UObject* WorldContext);

	// True when the world has an audio device (not -NoSound, not a dedicated server).
	bool HasAudio() const;
	double GetSampleRate() const;

	// The table's audio component (created on first use; one per table).
	URbTableAudioComponent* GetOrCreateTableAudio(ARbTable* Table);
	const TArray<TObjectPtr<URbTableAudioComponent>>& GetTableAudio() const { return TableAudio; }

	// The venue whose ambience / reverb plays (ARbVenueInfo, else TestRoom). SetVenue rebuilds the ambience and the reverb routing
	// (venue changes, tests).
	ERbVenue GetVenue() const { return Venue; }
	void SetVenue(ERbVenue NewVenue);
	const FRbVenueAudioProfile& GetVenueProfile() const { return Profile; }
	const TArray<TObjectPtr<URbAmbienceVoiceComponent>>& GetAmbienceVoices() const { return AmbienceVoices; }

	// Applies URbGameUserSettings::Volumes and the mix states to the submixes (also bound to OnSettingsChanged).
	void ApplyVolumes();

	// Pause-menu mix state (the UI calls it when it pauses / resumes the game; audio.md 7.3 CBM_RB_Pause). A paused world turns
	// it on as well.
	void SetPausedMix(bool bPaused);
	bool IsPausedMix() const { return bPausedMix || bWorldPausedMix; }
	// Current linear gains of the mix states (ramped; 1 = inactive).
	float GetPauseMixGain() const { return PauseGain; }
	float GetReplayMixGain() const { return ReplayGain; }
	bool IsReplayMix() const { return bReplayMix; }
	// One step of a mix-state level ramp (pure): linear in dB, the mix's whole depth DepthDb in AttackSeconds when it engages and in
	// ReleaseSeconds when it lets go (audio.md 7.3); returns the new linear gain (exactly 1 when released).
	static float StepMixGain(float Gain, bool bActive, double DepthDb, double AttackSeconds, double ReleaseSeconds, double DeltaSeconds);

	// Routing (generated assets; null when rb_make_audio.py has not run).
	USoundSubmix* GetSubmix(ERbAudioBus Bus) const;
	USoundSubmix* GetReverbSubmix() const { return ReverbSubmix; }
	bool HasGeneratedAssets() const { return GetSubmix(ERbAudioBus::Master) != nullptr; }

	// Digital output gain of the physical sources: 1 / P_fs of the dynamic-range mode (audio.md 4.2), x the ambience bus offset.
	double GetPhysicalOutputGain() const;
	double GetAmbienceOutputGain() const;
	// Output gain of one room-tone voice: the ambience output gain x the engine path's compensation (positional mono vs the
	// non-spatialised stereo bed; URbAudioSettings, measured by AU-0).
	double GetAmbienceVoiceGain(bool bPositional) const;

	// Live sounds (public for the functional tests: they fire the same paths as the delegates).
	void PlayFootstep(const FRbFootstep& Step, bool bOwnSteps = true);
	void HandleLooseBallImpact(const FRbLooseBallImpact& Impact);
	void HandleLooseBallRolling(const FRbLooseBallRolling& Rolling);
	void HandleLooseBallReturned(int32 TableIndex, int32 BallId);
	int32 GetFootstepsPlayed() const { return FootstepsPlayed; }
	int32 GetFloorHitsPlayed() const { return FloorHitsPlayed; }

	// Automatic tiers by listener distance (default on); off: tests set tiers themselves.
	void SetAutoTiers(bool bEnable) { bAutoTiers = bEnable; }
	// The tier a table would get for a listener (pure distance rule with hysteresis from the current tier; audio.md 6.6).
	static ERbTableAudioTier TierForDistance(double DistanceMeters, bool bPlayerTable, ERbTableAudioTier Current, double T1Meters, double T2Meters);
	// Plan distance [m] from a world point to the table's nose rectangle (0 inside).
	static double DistanceToTable(const ARbTable& Table, const FVector& WorldPoint);

	// The local listener (the player's camera / audio listener), world.
	FVector GetListenerLocation() const;

	// UWorldSubsystem / FTickableGameObject
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

protected:
	void HandleFootstep(const FRbFootstep& Step);
	void LoadRouting();
	void RefreshTables();
	void UpdateTiers();
	void UpdateMixStates(double RealDeltaSeconds);
	void BindFootsteps();
	void BindLooseBalls();
	void CreateAmbience();
	void DestroyAmbience();
	AActor* GetEmitterActor();
	URbImpactVoiceComponent* CreateLiveVoice(const FName& Name, USoundSubmix* Submix, float ReverbSend, float ReverbSendGain = 1.0f);
	// The bus volumes on the reverb sends of every voice (the reverb submix is shared: without this a slider would leave its bus's
	// reverb return at full level).
	void ApplyReverbSendGains();
	// The live voices (footsteps, loose balls) exist from the start: a mixer source takes ~0.1 s to start, so a voice created at its
	// first sound would play that sound late. Re-created with the venue's reverb routing on SetVenue.
	void CreateLiveVoices();
	void DestroyLiveVoices();
	// The pool voice of a loose ball (assigned on its first sound; the least recently used voice is reassigned when all are busy).
	URbImpactVoiceComponent* LooseBallVoice(int32 TableIndex, int32 BallId, bool bAssign);
	void ApplyLiveOutputGains();
	// Voices whose sound plays but whose generator never started (a sound started while the application was unfocused, whose
	// volume is then 0, gets no mixer source): restarted after a grace period. Returns the number restarted.
	int32 RestartStalledVoices(double Now);

	UPROPERTY(Transient)
	TArray<TObjectPtr<URbTableAudioComponent>> TableAudio;

	UPROPERTY(Transient)
	TArray<TObjectPtr<URbAmbienceVoiceComponent>> AmbienceVoices;

	UPROPERTY(Transient)
	TArray<TObjectPtr<URbImpactVoiceComponent>> FootstepVoices;

	UPROPERTY(Transient)
	TArray<TObjectPtr<URbImpactVoiceComponent>> LooseBallVoices; // pool
	TArray<int64> LooseBallVoiceKeys;                             // ball of each pool voice (INDEX_NONE: free)
	TArray<double> LooseBallVoiceLastUse;                         // real seconds of each pool voice's last sound

	UPROPERTY(Transient)
	TObjectPtr<AActor> EmitterActor;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundSubmix>> Submixes; // index ERbAudioBus

	UPROPERTY(Transient)
	TObjectPtr<USoundSubmix> ReverbSubmix;

	UPROPERTY(Transient)
	TObjectPtr<USoundEffectSubmixPreset> PauseLowPass;

	TWeakObjectPtr<URbCameraRigComponent> BoundRig;
	FDelegateHandle FootstepHandle;
	FDelegateHandle LooseImpactHandle;
	FDelegateHandle LooseRollingHandle;
	FDelegateHandle LooseReturnedHandle;
	FDelegateHandle SettingsHandle;

	ERbVenue Venue = ERbVenue::TestRoom;
	FRbVenueAudioProfile Profile;
	bool bBegunPlay = false;
	bool bPausedMix = false;
	bool bWorldPausedMix = false;
	bool bPauseFilterOn = false;
	bool bReplayMix = false;
	bool bAutoTiers = true;
	float PauseGain = 1.0f;
	float ReplayGain = 1.0f;
	float TableSendGain = 1.0f;    // Table slider (table and loose-ball voices)
	float FoleySendGain = 1.0f;    // Ambience slider (footsteps)
	float AmbienceSendGain = 1.0f; // Ambience slider x the replay dip (room-tone layers)
	double LastRealSeconds = 0.0;
	double NextTableRefresh = 0.0;
	int32 NextFootstepVoice = 0;
	int32 FootstepsPlayed = 0;
	int32 FloorHitsPlayed = 0;
	uint64 LiveSeed = 1;
	TMap<int64, double> LooseBallLastRolling; // real seconds of the last rolling update per loose ball
	TMap<TWeakObjectPtr<UObject>, double> StalledSince;
	int32 VoicesRestarted = 0;

public:
	int32 GetVoicesRestarted() const { return VoicesRestarted; }
};
