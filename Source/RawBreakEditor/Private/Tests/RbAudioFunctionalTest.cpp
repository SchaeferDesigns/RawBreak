// RawBreak.Functional.Audio.* (Docs/ue-architecture.md 18.5, Docs/specs/audio.md 5.1 / 8.8 / 14): engine-level checks of audio v1
// in PIE on L_M1_TestRoom with a real audio device. Run with `rbue.py test --sound --extra=-muteaudio` (the device renders in real
// time, the final output is muted; without --sound there is no audio device and the tests fail with that message).
//   AU0_Timing          the AU-0 spike: two table voices of one shot clock (the second started 3 game frames after the first),
//                       captured sample-exactly with their device frames (FRbSubmixCapture): the same impulse lands with 0
//                       samples skew (AU-T21), impulses 0.5104 ms apart land 24.50 +- 0.05 samples apart (AU-T08), and L_src (the
//                       delay between the frame a voice renders and the frame the mixer outputs it) is measured
//   RecordedBreak_*     a live break on a table of the venue (test room: the level's 9-ft table and break9; dive bar: a spawned 7-ft
//                       bar box with the dive-bar 8-ball break, the dive-bar ambience and reverb) heard at the breaker's ears:
//                       every sound class of the plan found in the recording at its scheduled frame (event log vs onsets), the
//                       gully runs, rolling, footsteps (OnFootstep path), a loose-ball floor hit, room tone at its level, the reverb
//                       tail, no clipping after the master limiter (AU-T16), the audio render time per block (AU-T19); the master
//                       mix is written to Saved/RbAudio/m2c/masters/<venue>_break_master.wav (Docs/audio/m2/ with
//                       -RbAudioWriteDocs), the stems to Saved/RbAudio/m2c/
//   MixReplayPauseVolumes  volume sliders (-12 dB at 0.5, the dry sound and its reverb return), the replay mix (ambience -10 dB) and film-style slow motion (x 0.25:
//                       impacts 4 x farther apart), a pause mid-shot holds the table without a click and resumes, the pause mix
//                       (World -12 dB and low-passed)
// Owner: M2-C.

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "AudioDevice.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Sound/SoundSubmix.h"

#include "Audio/RbAmbienceVoiceComponent.h"
#include "Audio/RbAudioCapture.h"
#include "Audio/RbAudioPlan.h"
#include "Audio/RbAudioScenarios.h"
#include "Audio/RbAudioSettings.h"
#include "Audio/RbAudioSubsystem.h"
#include "Audio/RbAudioTestKit.h"
#include "Audio/RbImpactVoiceComponent.h"
#include "Audio/RbTableAudioComponent.h"
#include "Balls/RbBallSet.h"
#include "Balls/RbLooseBallSubsystem.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Camera/RbCameraRigComponent.h"
#include "Core/RbAssetPaths.h"
#include "Game/RbGameMode.h"
#include "Settings/RbGameUserSettings.h"
#include "Simulation/RbSimScenarios.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbAudioFunctional
{
	using FCapturePtr = TSharedPtr<FRbSubmixCapture, ESPMode::ThreadSafe>;

	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	// The master renders go to Saved/RbAudio/m2c/masters/ by default (integration round, M2-0 request: a suite run must not rewrite
	// the committed LFS renders); `rbue.py test --sound --extra=-RbAudioWriteDocs` writes them into Docs/audio/m2/ on purpose.
	FString OutDir()
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("RbAudioWriteDocs")))
		{
			return FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/audio/m2"));
		}
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RbAudio/m2c/masters"));
	}

	FString StemDir()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RbAudio/m2c"));
	}

	// A script of steps run one after the other, one Update per editor frame; a step returns true when done. A step that runs
	// past its timeout fails the test and ends the script.
	struct FStep
	{
		FString Name;
		double Timeout = 10.0;
		TFunction<bool(double /*Seconds*/, int32 /*Frames*/)> Fn;
	};

	class FScriptCommand : public IAutomationLatentCommand
	{
	public:
		FScriptCommand(FAutomationTestBase* InTest, TArray<FStep>&& InSteps) : Test(InTest), Steps(MoveTemp(InSteps)) {}

		virtual bool Update() override
		{
			if (Index >= Steps.Num())
			{
				return true;
			}
			const double Now = FPlatformTime::Seconds();
			// The automation map open starts a PIE session that FStartPIECommand replaces: the script starts on a play world that
			// has existed for 3 s, and fails if the play world changes under it.
			UWorld* World = PlayWorld();
			if (!bStable)
			{
				if (FirstUpdate < 0.0)
				{
					FirstUpdate = Now;
				}
				if (World != StableWorld)
				{
					StableWorld = World;
					StableSince = Now;
				}
				if (!World || Now - StableSince < 3.0)
				{
					if (Now - FirstUpdate > 90.0)
					{
						Test->AddError(TEXT("no stable PIE world within 90 s"));
						Index = Steps.Num();
						return true;
					}
					return false;
				}
				bStable = true;
			}
			if (World != StableWorld)
			{
				Test->AddError(TEXT("the PIE world changed during the audio test"));
				Index = Steps.Num();
				return true;
			}
			if (StepStart < 0.0)
			{
				StepStart = Now;
				Frames = 0;
			}
			const double T = Now - StepStart;
			const bool bDone = Steps[Index].Fn(T, Frames++);
			if (bDone)
			{
				++Index;
				StepStart = -1.0;
			}
			else if (T > Steps[Index].Timeout)
			{
				Test->AddError(FString::Printf(TEXT("step '%s' timed out after %.1f s"), *Steps[Index].Name, T));
				Index = Steps.Num();
			}
			return Index >= Steps.Num();
		}

	private:
		FAutomationTestBase* Test;
		TArray<FStep> Steps;
		int32 Index = 0;
		double StepStart = -1.0;
		int32 Frames = 0;
		bool bStable = false;
		UWorld* StableWorld = nullptr;
		double StableSince = 0.0;
		double FirstUpdate = -1.0;
	};

	FStep Wait(double Seconds)
	{
		return {FString::Printf(TEXT("wait %.1f s"), Seconds), Seconds + 5.0, [Seconds](double T, int32) { return T >= Seconds; }};
	}

	// A headless editor never has the application focus, and an unfocused application plays at the "unfocused volume" (0): the
	// audio device then treats every sound as inaudible and starts no source. The tests switch the application volume off.
	void AllowUnfocusedAudio(bool bAllow)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("au.DisableAppVolume")))
		{
			Var->Set(bAllow ? 1 : 0, ECVF_SetByCode);
		}
	}

	// Without an audio device (-NoSound: the default of every headless run) there is nothing to measure: the tests pass with a
	// warning so the whole suite stays green; the audio acceptance runs with --sound.
	bool SkipWithoutSound(FAutomationTestBase* Test)
	{
		if (FApp::CanEverRenderAudio())
		{
			return false;
		}
		Test->AddWarning(TEXT("skipped: no audio device (-NoSound). Run `python Tools/unreal/rbue.py test --filter RawBreak.Functional.Audio --sound --extra=-muteaudio`"));
		return true;
	}

	// The listener of the play world's audio device. The engine moves it only in UGameViewportClient::Draw (from the player
	// controller's audio listener position); a -NullRHI PIE session draws no view, so the device's listener would stay where it was.
	// The tests set both: the controller's override (the plans and the tier distances read it) and the device's listener.
	void SetListener(const FVector& Location, const FRotator& Rotation)
	{
		UWorld* World = PlayWorld();
		if (!World)
		{
			return;
		}
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			PC->SetAudioListenerOverride(nullptr, Location, Rotation);
		}
		if (FAudioDevice* Device = World->GetAudioDeviceRaw())
		{
			Device->SetListener(World, 0, FTransform(Rotation, Location), 0.0f);
		}
	}

	void ClearListener()
	{
		if (UWorld* World = PlayWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->ClearAudioListenerOverride();
			}
		}
	}

	// The device's listener position (game-thread copy).
	FVector DeviceListener()
	{
		UWorld* World = PlayWorld();
		FAudioDevice* Device = World ? World->GetAudioDeviceRaw() : nullptr;
		FTransform T;
		return Device && Device->GetListenerTransform(0, T) ? T.GetLocation() : FVector(NAN, NAN, NAN);
	}

	// The audio subsystem of the play world with a device and the generated assets (rb_make_audio.py).
	URbAudioSubsystem* ReadyAudio(FAutomationTestBase* Test, double T, double Timeout)
	{
		AllowUnfocusedAudio(true);
		UWorld* World = PlayWorld();
		URbAudioSubsystem* Audio = World ? URbAudioSubsystem::Get(World) : nullptr;
		if (Audio && !Audio->HasAudio() && T > 3.0)
		{
			Test->AddError(TEXT("no audio device in the play world: run `rbue.py test --sound --extra=-muteaudio` (not -NoSound)"));
			return nullptr;
		}
		if (Audio && Audio->HasAudio() && !Audio->HasGeneratedAssets() && T > 3.0)
		{
			Test->AddError(TEXT("generated audio assets missing: run Tools/unreal/editor/rb_make_audio.py"));
			return nullptr;
		}
		return Audio && Audio->HasAudio() && Audio->HasGeneratedAssets() && World->GetFirstPlayerController() ? Audio : nullptr;
	}

	struct FCaptureData
	{
		TArray<float> Samples;
		int32 Channels = 0;
		int32 Rate = 0;
		int64 First = 0;
		TArray<double> Mono;
		int32 Frames() const { return Channels > 0 ? Samples.Num() / Channels : 0; }
	};

	FCaptureData Collect(const FCapturePtr& Capture)
	{
		FCaptureData D;
		if (Capture.IsValid())
		{
			Capture->Stop();
			Capture->GetSamples(D.Samples, D.Channels, D.Rate, D.First);
			RbAudioTestKit::ExtractChannel(D.Samples, D.Channels, -1, D.Mono);
		}
		return D;
	}

	TArray<double> Window(const TArray<double>& X, int64 Start, int32 N)
	{
		TArray<double> Out;
		Out.SetNumZeroed(N);
		for (int32 I = 0; I < N; ++I)
		{
			const int64 J = Start + I;
			if (J >= 0 && J < X.Num())
			{
				Out[I] = X[static_cast<int32>(J)];
			}
		}
		return Out;
	}

	double PeakAbs(const TArray<double>& X, int64 Begin, int64 End)
	{
		double P = 0.0;
		for (int64 I = FMath::Max<int64>(0, Begin); I < FMath::Min<int64>(End, X.Num()); ++I)
		{
			P = FMath::Max(P, FMath::Abs(X[static_cast<int32>(I)]));
		}
		return P;
	}

	int64 CurrentDeviceFrame(const URbImpactVoiceComponent* Voice)
	{
		return Voice ? Voice->GetShared()->LastBlockFrame.load() + Voice->GetShared()->BlockFrames.load() : 0;
	}


// ---------------------------------------------------------------------------------------------------------------------------
// AU-0: timing of the voices in the engine (AU-T08, AU-T21, L_src)
// ---------------------------------------------------------------------------------------------------------------------------

	struct FAu0State
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<URbImpactVoiceComponent> V1;
		TWeakObjectPtr<URbImpactVoiceComponent> V2;
		TWeakObjectPtr<URbImpactVoiceComponent> V3; // channel 0 with a reverb send 1.0 (the reverb chain's gain)
		// Gain probes: how the engine delivers a voice's output to the stereo submix (pan law, 1 / r, mono upmix). The voices'
		// compensation constants (URbAudioSettings::PanCompensation, NonSpatialCompensation) must match these measurements.
		struct FGainProbe
		{
			const TCHAR* Name = TEXT("");
			FVector OffsetCm = FVector::ZeroVector; // listener frame: X ahead, Y right
			int32 Channel = -1;                     // -1 positional mono, -2 non-spatialised mono, >= 0 stereo test channel
			double RefDistance = 1.0;               // the voice's reference distance [m]
			float ReverbSend = 0.0f;
			double OutputGain = 0.1;                // plan output gain
			double WaitSeconds = 0.7;               // after the click (reverb tails)
			bool bSendOnly = false;                 // no base submix output (the table's reverb feed)
			TWeakObjectPtr<URbImpactVoiceComponent> Voice;
			RbAudioTestKit::FClockState Shot;
			double GainL = 0.0;
			double GainR = 0.0;
		};
		TArray<FGainProbe> Probes;
		FRbShotAudioClockPtr Clock;
		FCapturePtr Capture;
		FCapturePtr ReverbCapture;
		RbAudioTestKit::FClockState ShotA;
		RbAudioTestKit::FClockState ShotB;
		RbAudioTestKit::FClockState ShotC;
		double Fs = 48000.0;
		int32 Block = 0;
		bool bDiagnosed = false;
	};

	constexpr int32 Au0NumProbes = 6;

	URbImpactVoiceComponent* MakeTestVoice(AActor* Owner, USoundSubmix* Submix, int32 Channel, const FRbShotAudioClockPtr& Clock, const TCHAR* Name)
	{
		return RbAudioTestKit::SpawnTestVoice(Owner, Submix, Channel, Clock, FName(Name));
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioAu0Test, "RawBreak.Functional.Audio.AU0_Timing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbAudioAu0Test::RunTest(const FString& Parameters)
{
	if (SkipWithoutSound(this))
	{
		return true;
	}
	if (!FPackageName::DoesPackageExist(RbAssetPaths::M1TestRoomMap))
	{
		AddError(TEXT("L_M1_TestRoom missing"));
		return false;
	}
	TSharedRef<FAu0State> S = MakeShared<FAu0State>();
	TArray<FStep> Steps;
	Steps.Add({TEXT("audio ready, first voice"), 30.0, [this, S](double T, int32)
	{
		URbAudioSubsystem* Audio = ReadyAudio(this, T, 30.0);
		if (!Audio)
		{
			return HasAnyErrors();
		}
		UWorld* World = PlayWorld();
		{
			FVector Location, Front, Right;
			World->GetFirstPlayerController()->GetAudioListenerPosition(Location, Front, Right);
			SetListener(Location, Front.Rotation());
		}
		FActorSpawnParameters P;
		P.ObjectFlags = RF_Transient;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"), RF_Transient);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		S->Actor = Actor;
		S->Clock = RbAudioTestKit::MakeClock();
		S->V1 = MakeTestVoice(Actor, Audio->GetSubmix(ERbAudioBus::Table), 0, S->Clock, TEXT("RbAu0VoiceA"));
		return true;
	}});
	Steps.Add({TEXT("second voice 3 game frames later"), 10.0, [this, S](double, int32 Frames)
	{
		if (Frames < 3)
		{
			return false;
		}
		URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
		S->V2 = MakeTestVoice(S->Actor.Get(), Audio->GetSubmix(ERbAudioBus::Table), 1, S->Clock, TEXT("RbAu0VoiceB"));
		S->V3 = RbAudioTestKit::SpawnTestVoice(S->Actor.Get(), Audio->GetSubmix(ERbAudioBus::Table), 0, S->Clock, FName(TEXT("RbAu0VoiceReverb")),
			Audio->GetReverbSubmix(), 1.0f, 1.0);
		FVector Location, Front, Right;
		PlayWorld()->GetFirstPlayerController()->GetAudioListenerPosition(Location, Front, Right);
		S->Probes.SetNum(Au0NumProbes);
		S->Probes[0].Name = TEXT("positional mono 1 m ahead");
		S->Probes[0].OffsetCm = FVector(100.0, 0.0, 0.0);
		S->Probes[1].Name = TEXT("positional mono 2 m ahead");
		S->Probes[1].OffsetCm = FVector(200.0, 0.0, 0.0);
		S->Probes[2].Name = TEXT("positional mono 1 m right");
		S->Probes[2].OffsetCm = FVector(0.0, 100.0, 0.0);
		S->Probes[3].Name = TEXT("non-spatialised mono");
		S->Probes[3].OffsetCm = FVector(100.0, 0.0, 0.0);
		S->Probes[3].Channel = -2;
		// A table voice as URbTableAudioComponent configures it (reference distance, reverb send, pan compensation) 2 m ahead: its
		// direct sound must arrive physically (p_ref x RefDistance / 2 m per ear) and its reverb at the room's diffuse-field level
		// (the energy of p at 1 m x the IR's energy per channel; Tools/audio/ir_synth.py normalisation).
		{
			const URbAudioSettings* Settings = URbAudioSettings::Get();
			FAu0State::FGainProbe& T = S->Probes[4];
			T.Name = TEXT("table voice 2 m ahead (calibration)");
			T.OffsetCm = FVector(200.0, 0.0, 0.0);
			T.RefDistance = Settings->RefDistanceMeters;
			T.ReverbSend = Settings->RefDistanceMeters * Settings->ReverbSendScale * Audio->GetVenueProfile().VoiceReverbSend;
			T.OutputGain = 0.1 * Settings->PanCompensation;
			T.WaitSeconds = 1.8;
			// The table's reverb feed as URbTableAudioComponent configures it: non-spatialised mono, send-only (no dry output), the
			// voices' send and compensation. Its reverb must reach the diffuse-field level of the same click at 1 m (review M2-C: the
			// room is excited by the radiated power through this voice, audio.md 3.6 / 6.4).
			FAu0State::FGainProbe& F = S->Probes[5];
			F = T;
			F.Name = TEXT("table reverb feed (calibration)");
			F.Channel = -2;
			F.bSendOnly = true;
		}
		for (int32 I = 0; I < S->Probes.Num(); ++I)
		{
			FAu0State::FGainProbe& P = S->Probes[I];
			URbImpactVoiceComponent* V = RbAudioTestKit::SpawnTestVoice(S->Actor.Get(), Audio->GetSubmix(ERbAudioBus::Table), P.Channel, S->Clock,
				FName(*FString::Printf(TEXT("RbAu0Probe%d"), I)), P.ReverbSend > 0.0f ? Audio->GetReverbSubmix() : nullptr, P.ReverbSend, P.RefDistance,
				P.bSendOnly);
			V->SetWorldLocation(Location + Front * P.OffsetCm.X + Right * P.OffsetCm.Y);
			P.Voice = V;
		}
		return true;
	}});
	Steps.Add({TEXT("both voices render"), 15.0, [this, S](double T, int32)
	{
		URbImpactVoiceComponent* A = S->V1.Get();
		URbImpactVoiceComponent* B = S->V2.Get();
		bool bOthers = S->V3.IsValid() && S->V3->IsRendering();
		for (const FAu0State::FGainProbe& P : S->Probes)
		{
			bOthers &= P.Voice.IsValid() && P.Voice->IsRendering();
		}
		if (!A || !B || !bOthers || !A->IsRendering() || !B->IsRendering() || A->GetShared()->BlocksRendered.load() < 20
			|| B->GetShared()->BlocksRendered.load() < 20)
		{
			if (T > 5.0 && !S->bDiagnosed)
			{
				S->bDiagnosed = true;
				AddInfo(RbAudioTestKit::DescribeVoice(A));
				AddInfo(RbAudioTestKit::DescribeVoice(B));
			}
			return false;
		}
		S->Fs = A->GetShared()->SampleRate.load();
		S->Block = A->GetShared()->BlockFrames.load();
		URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
		S->Capture = FRbSubmixCapture::Start(PlayWorld(), Audio->GetSubmix(ERbAudioBus::Table), 20.0, TEXT("RbAu0"));
		S->ReverbCapture = FRbSubmixCapture::Start(PlayWorld(), Audio->GetReverbSubmix(), 20.0, TEXT("RbAu0Reverb"));
		return S->Capture.IsValid() && S->ReverbCapture.IsValid();
	}});
	Steps.Add(Wait(0.3));
	Steps.Add({TEXT("shot A: the same impulse in both voices (AU-T21)"), 5.0, [S](double, int32)
	{
		const double Gain = 0.1;
		URbImpactVoiceComponent::PushPlan(S->V1->GetShared(), RbAudioTestKit::MakeClickPlan(101, {0.100}, S->Fs, Gain));
		URbImpactVoiceComponent::PushPlan(S->V2->GetShared(), RbAudioTestKit::MakeClickPlan(101, {0.100}, S->Fs, Gain));
		RbAudioTestKit::StartShot(S->Clock, 101, FPlatformTime::Seconds() + 0.05, 0.0, 1.0, 0.0);
		return true;
	}});
	Steps.Add({TEXT("shot A anchored"), 5.0, [S](double T, int32)
	{
		S->ShotA = RbAudioTestKit::ReadClock(S->Clock);
		return S->ShotA.bAnchored && S->ShotA.ShotId == 101 && T > 0.7;
	}});
	Steps.Add({TEXT("shot B: impulses 0.5104 ms apart in two voices (AU-T08)"), 5.0, [S](double, int32)
	{
		const double Gain = 0.1;
		URbImpactVoiceComponent::PushPlan(S->V1->GetShared(), RbAudioTestKit::MakeClickPlan(102, {0.100}, S->Fs, Gain));
		URbImpactVoiceComponent::PushPlan(S->V2->GetShared(), RbAudioTestKit::MakeClickPlan(102, {0.100 + 0.5104e-3}, S->Fs, Gain));
		RbAudioTestKit::StartShot(S->Clock, 102, FPlatformTime::Seconds() + 0.05, 0.0, 1.0, 0.0);
		return true;
	}});
	Steps.Add({TEXT("shot B anchored"), 5.0, [S](double T, int32)
	{
		S->ShotB = RbAudioTestKit::ReadClock(S->Clock);
		return S->ShotB.bAnchored && S->ShotB.ShotId == 102 && T > 0.7;
	}});
	Steps.Add({TEXT("shot C: one click with a reverb send of 1.0"), 5.0, [S](double, int32)
	{
		URbImpactVoiceComponent::PushPlan(S->V3->GetShared(), RbAudioTestKit::MakeClickPlan(103, {0.100}, S->Fs, 0.1));
		RbAudioTestKit::StartShot(S->Clock, 103, FPlatformTime::Seconds() + 0.05, 0.0, 1.0, 0.0);
		return true;
	}});
	Steps.Add({TEXT("shot C anchored, reverb tail"), 5.0, [S](double T, int32)
	{
		S->ShotC = RbAudioTestKit::ReadClock(S->Clock);
		return S->ShotC.bAnchored && S->ShotC.ShotId == 103 && T > 1.8;
	}});
	for (int32 I = 0; I < Au0NumProbes; ++I)
	{
		const uint64 Id = 104 + I;
		Steps.Add({FString::Printf(TEXT("gain probe %d: one click"), I), 5.0, [S, I, Id](double, int32)
		{
			URbImpactVoiceComponent::PushPlan(S->Probes[I].Voice->GetShared(), RbAudioTestKit::MakeClickPlan(Id, {0.100}, S->Fs, S->Probes[I].OutputGain));
			RbAudioTestKit::StartShot(S->Clock, Id, FPlatformTime::Seconds() + 0.05, 0.0, 1.0, 0.0);
			return true;
		}});
		Steps.Add({FString::Printf(TEXT("gain probe %d anchored"), I), 5.0, [S, I, Id](double T, int32)
		{
			S->Probes[I].Shot = RbAudioTestKit::ReadClock(S->Clock);
			return S->Probes[I].Shot.bAnchored && S->Probes[I].Shot.ShotId == Id && T > S->Probes[I].WaitSeconds;
		}});
	}
	Steps.Add({TEXT("analyse"), 30.0, [this, S](double, int32)
	{
		const FCaptureData Rev = Collect(S->ReverbCapture);
		const FCaptureData D = Collect(S->Capture);
		if (!TestTrue(TEXT("stereo capture of SUBM_RB_Table"), D.Channels == 2 && D.Frames() > 0))
		{
			return true;
		}
		TArray<double> L, R;
		RbAudioTestKit::ExtractChannel(D.Samples, D.Channels, 0, L);
		RbAudioTestKit::ExtractChannel(D.Samples, D.Channels, 1, R);
		const double Fs = D.Rate;
		AddInfo(FString::Printf(TEXT("device: %.0f Hz, %d-frame blocks (the generator's GetDesiredNumSamplesToRenderPerCallback); capture %d frames from device frame %lld, %d gaps; anchors %lld / %lld (lead %lld / %lld frames past LeadMin)"),
			Fs, S->Block, D.Frames(), D.First, S->Capture->GetGaps(), S->ShotA.AnchorFrame, S->ShotB.AnchorFrame, S->ShotA.LastLeadFrames, S->ShotB.LastLeadFrames));
		TestEqual(TEXT("the capture has no gaps"), S->Capture->GetGaps(), 0);
		TestTrue(TEXT("the voices render one device block per callback (512 frames, the audio block of DefaultEngine.ini)"), S->Block == 512);
		constexpr int32 N = 4096;
		constexpr int32 Pre = 256;
		// Shot A: 0 samples skew between the two voices (AU-T21), and L_src from the offline reference of the same click.
		const int64 FA = S->ShotA.AnchorFrame + std::llround((0.100 - S->ShotA.OriginShotTime) * Fs);
		const TArray<double> AL = Window(L, FA - D.First - Pre, N);
		const TArray<double> AR = Window(R, FA - D.First - Pre, N);
		TArray<double> Ref;
		RbAudioTestKit::RenderClick(static_cast<double>(Pre), N, Fs, Ref);
		const double SkewA = RbAudioTestKit::GroupDelaySamples(AL, AR, Fs);
		const double LSrc = RbAudioTestKit::GroupDelaySamples(Ref, AL, Fs);
		const double PeakL = PeakAbs(AL, 0, N);
		const double PeakRef = PeakAbs(Ref, 0, N) * 0.1;
		AddInfo(FString::Printf(TEXT("AU-T21: skew %.4f samples; L_src %.4f samples; click peak %.4f (offline x output gain %.4f, %+.2f dB)"), SkewA, LSrc, PeakL,
			PeakRef, 20.0 * std::log10(FMath::Max(PeakL, 1e-12) / FMath::Max(PeakRef, 1e-12))));
		TestTrue(TEXT("shot A: both voices sound"), PeakL > 0.01 && PeakAbs(AR, 0, N) > 0.01);
		TestTrue(FString::Printf(TEXT("AU-T21: the second voice (started 3 game frames later) is 0 samples skewed (%.4f)"), SkewA), FMath::Abs(SkewA) <= 0.05);
		TestTrue(FString::Printf(TEXT("AU-0: L_src = %.4f samples (the frame a voice renders is the frame the device outputs)"), LSrc), FMath::Abs(LSrc) <= 0.05);
		// Shot B: 0.5104 ms -> 24.4992 samples.
		const int64 FB = S->ShotB.AnchorFrame + std::llround((0.100 - S->ShotB.OriginShotTime) * Fs);
		const TArray<double> BL = Window(L, FB - D.First - Pre, N);
		const TArray<double> BR = Window(R, FB - D.First - Pre, N);
		const double SkewB = RbAudioTestKit::GroupDelaySamples(BL, BR, Fs);
		AddInfo(FString::Printf(TEXT("AU-T08: 0.5104 ms in two voices -> %.4f samples (exact 24.4992)"), SkewB));
		TestTrue(FString::Printf(TEXT("AU-T08: impulses 0.5104 ms apart in two voices land %.4f samples apart (want 24.50 +- 0.05)"), SkewB),
			FMath::Abs(SkewB - 24.4992) <= 0.05);
		// Shot C: the reverb chain's gain. Reverb energy of channel 0 over the click's energy == the IR's channel-0 energy when the
		// send (1.0), the convolution (normalisation 0 dB) and the submix path are unity.
		{
			const int64 FC = S->ShotC.AnchorFrame + std::llround((0.100 - S->ShotC.OriginShotTime) * Fs);
			const TArray<double> Click = Window(L, FC - D.First - Pre, N);
			TArray<double> RevL;
			RbAudioTestKit::ExtractChannel(Rev.Samples, Rev.Channels, 0, RevL);
			const TArray<double> Tail = Window(RevL, FC - Rev.First - Pre, static_cast<int32>(1.6 * Fs));
			double EClick = 0.0;
			double ETail = 0.0;
			for (double V : Click)
			{
				EClick += V * V;
			}
			for (double V : Tail)
			{
				ETail += V * V;
			}
			const double IrEnergy = RbAudioTestKit::WavChannelEnergy(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/audio/out/ref/IR_RB_TestRoom.wav")), 0);
			const double GainDb = 10.0 * std::log10(FMath::Max(ETail, 1e-30) / FMath::Max(EClick * IrEnergy, 1e-30));
			// A send into the left input only: how much of it the convolution puts into the right output (a 2-channel IR convolves
			// each input channel with its own IR channel; any right-channel energy is cross-feed).
			TArray<double> RevR;
			RbAudioTestKit::ExtractChannel(Rev.Samples, Rev.Channels, 1, RevR);
			double ETailR = 0.0;
			for (double V : Window(RevR, FC - Rev.First - Pre, static_cast<int32>(1.6 * Fs)))
			{
				ETailR += V * V;
			}
			AddInfo(FString::Printf(TEXT("reverb chain: tail / click energy %.3f, IR channel-0 energy %.3f: chain gain %+.2f dB (reverb capture %d ch); left-only send -> right output %+.1f dB re the left output"),
				ETail / FMath::Max(EClick, 1e-30), IrEnergy, GainDb, Rev.Channels, 10.0 * std::log10(FMath::Max(ETailR, 1e-30) / FMath::Max(ETail, 1e-30))));
			TestTrue(TEXT("the reverb submix renders the send"), ETail > 0.0);
		}
		// Gain probes (RefDistance 1 m): the gain from a voice's output to each channel of the stereo submix.
		const double RefPeak = PeakAbs(Ref, 0, N) * 0.1;
		for (int32 I = 0; I < FMath::Min(4, S->Probes.Num()); ++I)
		{
			FAu0State::FGainProbe& P = S->Probes[I];
			const int64 F = P.Shot.AnchorFrame + std::llround((0.100 - P.Shot.OriginShotTime) * Fs);
			P.GainL = 20.0 * std::log10(FMath::Max(PeakAbs(Window(L, F - D.First - Pre, N), 0, N), 1e-12) / RefPeak);
			P.GainR = 20.0 * std::log10(FMath::Max(PeakAbs(Window(R, F - D.First - Pre, N), 0, N), 1e-12) / RefPeak);
			AddInfo(FString::Printf(TEXT("gain probe '%s': %+.2f / %+.2f dB (L / R) of the voice's output"), P.Name, P.GainL, P.GainR));
		}
		// The calibration probes: direct sound per ear vs physical, reverb per ear vs the diffuse field. 4: a table voice (positional,
		// before the review its reverb send carried the room); 5: the table's reverb feed (send-only, non-spatialised).
		for (int32 ProbeIndex = 4; ProbeIndex < FMath::Min(S->Probes.Num(), Au0NumProbes); ++ProbeIndex)
		{
			const FAu0State::FGainProbe& P = S->Probes[ProbeIndex];
			const int64 F = P.Shot.AnchorFrame + std::llround((0.100 - P.Shot.OriginShotTime) * Fs);
			const double Pref = PeakAbs(Ref, 0, N) * 0.1; // the voice's pressure output (p at RefDistance, digital)
			const double DirectWant = Pref * P.RefDistance / 2.0;
			const double DirL = 20.0 * std::log10(FMath::Max(PeakAbs(Window(L, F - D.First - Pre, N), 0, N), 1e-12) / DirectWant);
			const double DirR = 20.0 * std::log10(FMath::Max(PeakAbs(Window(R, F - D.First - Pre, N), 0, N), 1e-12) / DirectWant);
			double ERef1m = 0.0;
			for (double V : Ref)
			{
				ERef1m += FMath::Square(V * 0.1 * P.RefDistance);
			}
			double RevDb[2] = {0.0, 0.0};
			for (int32 C = 0; C < 2; ++C)
			{
				TArray<double> RevC;
				RbAudioTestKit::ExtractChannel(Rev.Samples, Rev.Channels, C, RevC);
				const TArray<double> Tail = Window(RevC, F - Rev.First - Pre, static_cast<int32>(1.6 * Fs));
				double ETail = 0.0;
				for (double V : Tail)
				{
					ETail += V * V;
				}
				const double IrEnergy = RbAudioTestKit::WavChannelEnergy(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/audio/out/ref/IR_RB_TestRoom.wav")), C);
				RevDb[C] = 10.0 * std::log10(FMath::Max(ETail, 1e-30) / FMath::Max(ERef1m * IrEnergy, 1e-30));
			}
			AddInfo(FString::Printf(TEXT("%s: direct %+.2f / %+.2f dB vs physical, reverb %+.2f / %+.2f dB vs the diffuse field (L / R)"), P.Name,
				DirL, DirR, RevDb[0], RevDb[1]));
			if (P.bSendOnly)
			{
				TestTrue(FString::Printf(TEXT("%s: no dry output (%+.1f / %+.1f dB re the physical level, want <= -60)"), P.Name, DirL, DirR),
					DirL <= -60.0 && DirR <= -60.0);
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s: the direct sound arrives physically at each ear (%+.2f / %+.2f dB, +-0.5)"), P.Name, DirL, DirR),
					FMath::Abs(DirL) <= 0.5 && FMath::Abs(DirR) <= 0.5);
			}
			// The convolution feeds about -12 dB of each input channel into the other output (left-only probe above), so a centred source
			// comes out of the right channel ~1.8 dB hotter than the left; the send scale calibrates the mean of both ears.
			const double MeanDb = 10.0 * std::log10(0.5 * (FMath::Pow(10.0, RevDb[0] / 10.0) + FMath::Pow(10.0, RevDb[1] / 10.0)));
			TestTrue(FString::Printf(TEXT("%s: the reverb reaches the diffuse-field level (%+.2f dB mean of both ears, +-1.0; %+.2f / %+.2f dB, +-2.5 each)"),
				P.Name, MeanDb, RevDb[0], RevDb[1]), FMath::Abs(MeanDb) <= 1.0 && FMath::Abs(RevDb[0]) <= 2.5 && FMath::Abs(RevDb[1]) <= 2.5);
		}
		if (S->Probes.Num() >= 4)
		{
			const URbAudioSettings* Settings = URbAudioSettings::Get();
			const double Ahead = 0.5 * (S->Probes[0].GainL + S->Probes[0].GainR);
			const double Law = Ahead - 0.5 * (S->Probes[1].GainL + S->Probes[1].GainR);
			const double NonSpatial = 0.5 * (S->Probes[3].GainL + S->Probes[3].GainR);
			const double StereoChannel = 20.0 * std::log10(FMath::Max(PeakL, 1e-12) / FMath::Max(PeakRef, 1e-12));
			AddInfo(FString::Printf(TEXT("engine gain structure: positional mono at its reference distance straight ahead %+.2f dB per channel (compensated by %+.2f dB), 1 m -> 2 m %.2f dB, non-spatialised mono %+.2f dB, stereo test channel %+.2f dB (compensated by %+.2f dB)"),
				Ahead, 20.0 * std::log10(Settings->PanCompensation), Law, NonSpatial, StereoChannel, 20.0 * std::log10(Settings->NonSpatialCompensation)));
			TestTrue(FString::Printf(TEXT("1 / r: 1 m -> 2 m = %.2f dB (want 6.02 +- 0.3)"), Law), FMath::Abs(Law - 6.02) <= 0.3);
			TestTrue(FString::Printf(TEXT("PanCompensation (%+.2f dB) cancels the positional path (%+.2f dB) within 0.3 dB"), 20.0 * std::log10(Settings->PanCompensation), Ahead),
				FMath::Abs(Ahead + 20.0 * std::log10(Settings->PanCompensation)) <= 0.3);
			TestTrue(FString::Printf(TEXT("NonSpatialCompensation (%+.2f dB) cancels the stereo path (%+.2f dB) within 0.3 dB"), 20.0 * std::log10(Settings->NonSpatialCompensation), StereoChannel),
				FMath::Abs(StereoChannel + 20.0 * std::log10(Settings->NonSpatialCompensation)) <= 0.3);
			TestTrue(FString::Printf(TEXT("a source 1 m to the right is louder on the right (%+.2f / %+.2f dB)"), S->Probes[2].GainL, S->Probes[2].GainR),
				S->Probes[2].GainR > S->Probes[2].GainL + 3.0);
		}
		ClearListener();
		IFileManager::Get().MakeDirectory(*OutDir(), true);
		IFileManager::Get().MakeDirectory(*StemDir(), true);
		// Where each shot's click is scheduled in the capture (Tools/audio/m2_report.py plots the alignment).
		{
			FString Meta = FString::Printf(TEXT("fs=%.0f\nfirst=%lld\n"), Fs, static_cast<long long>(D.First));
			auto Line = [&](const TCHAR* Name, const RbAudioTestKit::FClockState& C, double ShotTime)
			{
				Meta += FString::Printf(TEXT("%s=%.3f\n"), Name, static_cast<double>(C.AnchorFrame - D.First) + (ShotTime - C.OriginShotTime) * Fs);
			};
			Line(TEXT("shot_a"), S->ShotA, 0.100);
			Line(TEXT("shot_b_left"), S->ShotB, 0.100);
			Line(TEXT("shot_b_right"), S->ShotB, 0.100 + 0.5104e-3);
			FFileHelper::SaveStringToFile(Meta, *FPaths::Combine(StemDir(), TEXT("au0_meta.txt")));
		}
		const FString Wav = FPaths::Combine(OutDir(), TEXT("au0_two_voices.wav"));
		TestTrue(TEXT("capture written"), RbAudioTestKit::WriteWav(Wav, D.Samples, D.Channels, D.Rate, false));
		AddInfo(FString::Printf(TEXT("AU-0 capture: %s"), *Wav));
		if (AActor* Actor = S->Actor.Get())
		{
			Actor->Destroy();
		}
		return true;
	}});
	AllowUnfocusedAudio(true);
	AutomationOpenMap(RbAssetPaths::M1TestRoomMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FScriptCommand(this, MoveTemp(Steps)));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------
// Recorded breaks
// ---------------------------------------------------------------------------------------------------------------------------

	struct FBreakState
	{
		bool bDiveBar = false;
		FString Name;
		TWeakObjectPtr<ARbTable> Table;
		TWeakObjectPtr<ARbBallSet> Balls;
		TWeakObjectPtr<URbTableAudioComponent> TableAudio;
		TArray<TWeakObjectPtr<AActor>> Spawned;
		TSharedPtr<FRbShot> Shot;
		rb::Vec3 Head;
		rb::Vec3 Left;
		FVector ListenerWorld = FVector::ZeroVector;
		TMap<FString, FCapturePtr> Captures;
		FRbAudioRenderProfilePtr Profile;
		RbAudioTestKit::FClockState Clock;
		TSharedPtr<const FRbShotAudioPlan, ESPMode::ThreadSafe> Plan;
		double StrikeSeconds = 0.0;
		bool bDiagnosed = false;
		TArray<int64> FootstepFrames;
		int64 FloorHitFrame = 0;
		int64 ProbeFrame = 0;
		int64 MuteFrame = 0;
		int64 RollFrame0 = 0;   // loose ball rolling on the floor (OnRolling path): first and last frame
		int64 RollFrame1 = 0;
		int32 LiveStep = 0;
	};

	// Mean square of X[Begin, End) (clamped; 0 for an empty window).
	double MeanSquare(const TArray<double>& X, int64 Begin, int64 End)
	{
		const int64 B = FMath::Max<int64>(0, Begin);
		const int64 E = FMath::Min<int64>(End, X.Num());
		double Sum = 0.0;
		for (int64 I = B; I < E; ++I)
		{
			Sum += X[static_cast<int32>(I)] * X[static_cast<int32>(I)];
		}
		return E > B ? Sum / static_cast<double>(E - B) : 0.0;
	}

	// The venue's room-tone layers on / off (the reverb probe needs a quiet reverb stem: the positional layers send to it).
	void MuteAmbience(URbAudioSubsystem& Audio, bool bMute)
	{
		for (URbAmbienceVoiceComponent* V : Audio.GetAmbienceVoices())
		{
			if (V)
			{
				V->SetOutputGain(bMute ? 0.0 : Audio.GetAmbienceVoiceGain(V->GetLayerDesc().NumChannels() == 1));
			}
		}
	}

	void StartCaptures(FBreakState& S, URbAudioSubsystem& Audio)
	{
		UWorld* World = PlayWorld();
		auto Add = [&](const TCHAR* Name, USoundSubmix* Submix)
		{
			if (Submix)
			{
				S.Captures.Add(Name, FRbSubmixCapture::Start(World, Submix, 30.0, FString::Printf(TEXT("RbBreak_%s"), Name)));
			}
		};
		Add(TEXT("master"), Audio.GetSubmix(ERbAudioBus::Master));
		Add(TEXT("world"), Audio.GetSubmix(ERbAudioBus::World));
		Add(TEXT("table"), Audio.GetSubmix(ERbAudioBus::Table));
		Add(TEXT("ambience"), Audio.GetSubmix(ERbAudioBus::Ambience));
		Add(TEXT("foley"), Audio.GetSubmix(ERbAudioBus::Foley));
		Add(TEXT("reverb"), Audio.GetReverbSubmix());
	}

	// Predicted A-weighted room tone at the listener from the venue's ambience voices (LAeq at 1 m, 1 / r beyond RefDistance).
	double PredictRoomToneDbA(const URbAudioSubsystem& Audio, const FVector& Listener)
	{
		const double Ref = URbAudioSettings::Get()->RefDistanceMeters;
		double Power = 0.0;
		for (const URbAmbienceVoiceComponent* V : Audio.GetAmbienceVoices())
		{
			if (!V)
			{
				continue;
			}
			const RbAudio::FAmbienceLayerDesc& Desc = V->GetLayerDesc();
			if (Desc.Layer == RbAudio::EAmbienceLayer::Compressor && !V->GetShared()->bCompressorOn.load())
			{
				continue; // an idle cooler (its duty cycle)
			}
			double Level = Desc.LevelDbA;
			if (Desc.NumChannels() == 1)
			{
				const double R = FMath::Max((V->GetComponentLocation() - Listener).Size() / 100.0, Ref);
				Level -= 20.0 * std::log10(R);
			}
			Power += FMath::Pow(10.0, Level / 10.0);
		}
		return Power > 0.0 ? 10.0 * std::log10(Power) : -300.0;
	}

	bool RunBreakAnalysis(FAutomationTestBase* Test, FBreakState& S)
	{
		URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
		TMap<FString, FCaptureData> D;
		for (TPair<FString, FCapturePtr>& C : S.Captures)
		{
			D.Add(C.Key, Collect(C.Value));
			Test->TestEqual(FString::Printf(TEXT("%s capture without gaps (a gap = a device underrun on an overloaded machine: re-run)"), *C.Key),
				C.Value.IsValid() ? C.Value->GetGaps() : -1, 0);
		}
		const FCaptureData* Table = D.Find(TEXT("table"));
		const FCaptureData* Master = D.Find(TEXT("master"));
		if (!Test->TestTrue(TEXT("table and master captured"), Table && Master && Table->Frames() > 0 && Master->Frames() > 0 && S.Plan.IsValid()))
		{
			return true;
		}
		const double Fs = Table->Rate;
		const double FsPa = RbAudioTestKit::FullScalePa();
		const FVector DevListener = DeviceListener();
		Test->TestTrue(FString::Printf(TEXT("%s: the device's listener is at the breaker's head (%.1f cm off)"), *S.Name, (DevListener - S.ListenerWorld).Size()),
			(DevListener - S.ListenerWorld).Size() < 1.0);
		const int32 Block = S.TableAudio.IsValid() && S.TableAudio->GetVoices().Num() > 0 ? S.TableAudio->GetVoices()[0]->GetShared()->BlockFrames.load() : 512;
		IFileManager::Get().MakeDirectory(*OutDir(), true);
		IFileManager::Get().MakeDirectory(*StemDir(), true);

		// 1. Every class of the plan at its scheduled frame (event log vs detected onsets). The capture index of shot time t heard at
		//    the listener: anchor + (arrival - origin) / rate x fs - first captured frame (L_src = 0, AU0_Timing).
		TArray<RbAudioTestKit::FOnset> Onsets;
		RbAudioTestKit::DetectOnsets(Table->Mono, Fs, Onsets, 0.0, 6.0, 2.0, 0.5, 1e-7);
		TArray<double> OnsetSamples;
		for (const RbAudioTestKit::FOnset& O : Onsets)
		{
			OnsetSamples.Add(O.Sample);
		}
		auto NearestOnset = [&OnsetSamples](double Want) -> double
		{
			double Best = 1e30;
			for (double O : OnsetSamples)
			{
				if (FMath::Abs(O - Want) < FMath::Abs(Best - Want))
				{
					Best = O;
				}
			}
			return Best;
		};
		TMap<uint8, TPair<int32, int32>> PerClass; // matched / total
		TArray<double> Errors;
		FString Csv = TEXT("impact,kind,shot_time_s,arrival_s,expected_sample,nearest_onset,error_ms,matched\n");
		const double Tolerance = 0.0015 * Fs;
		for (const FRbAudioPlanImpact& I : S.Plan->Impacts)
		{
			const double Expected = S.Clock.AnchorFrame + ((I.ShotTime - S.Clock.OriginShotTime) / S.Clock.Rate + (I.ArrivalTime - I.ShotTime)) * Fs - Table->First;
			const double Near = NearestOnset(Expected);
			const bool bMatch = FMath::Abs(Near - Expected) <= Tolerance;
			TPair<int32, int32>& C = PerClass.FindOrAdd(static_cast<uint8>(I.Kind));
			C.Key += bMatch ? 1 : 0;
			C.Value += 1;
			if (bMatch)
			{
				Errors.Add((Near - Expected) / Fs * 1e3);
			}
			Csv += FString::Printf(TEXT("%d,%s,%.6f,%.6f,%.1f,%.1f,%.3f,%d\n"), I.ImpactId, RbAudioTestKit::ImpactKindName(static_cast<uint8>(I.Kind)), I.ShotTime,
				I.ArrivalTime, Expected, Near, (Near - Expected) / Fs * 1e3, bMatch ? 1 : 0);
		}
		FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(StemDir(), S.Name + TEXT("_events.csv")));
		int32 Matched = 0;
		for (const TPair<uint8, TPair<int32, int32>>& C : PerClass)
		{
			Matched += C.Value.Key;
			Test->AddInfo(FString::Printf(TEXT("%s: class %s heard at %d of %d scheduled frames (+-1.5 ms)"), *S.Name, RbAudioTestKit::ImpactKindName(C.Key),
				C.Value.Key, C.Value.Value));
			Test->TestTrue(FString::Printf(TEXT("%s: every sound class is in the recording: %s (%d / %d)"), *S.Name, RbAudioTestKit::ImpactKindName(C.Key),
				C.Value.Key, C.Value.Value), C.Value.Key >= 1);
		}
		Errors.Sort();
		const double Median = Errors.Num() > 0 ? Errors[Errors.Num() / 2] : 0.0;
		Test->AddInfo(FString::Printf(TEXT("%s: %d of %d impacts matched an onset, median timing error %.3f ms, %d onsets detected"), *S.Name, Matched,
			S.Plan->Impacts.Num(), Median, Onsets.Num()));
		Test->TestTrue(FString::Printf(TEXT("%s: onsets on schedule (median error %.3f ms)"), *S.Name, Median), FMath::Abs(Median) <= 0.5);
		// Impacts closer than the detector's 0.5 ms merge gap (the rack cluster) share one onset: 95 % of all impacts are found.
		Test->TestTrue(FString::Printf(TEXT("%s: %d of %d impacts found at their scheduled frames (>= 95 %%)"), *S.Name, Matched, S.Plan->Impacts.Num()),
			Matched >= FMath::CeilToInt(0.95 * S.Plan->Impacts.Num()));
		// The plan's tail after the balls came to rest: every gully run ends in its trap click (the clock runs on after OnFinished).
		const TPair<int32, int32>* Traps = PerClass.Find(static_cast<uint8>(RbAudio::EImpactKind::TrapClick));
		Test->TestEqual(FString::Printf(TEXT("%s: every gully run ends in a trap click that is heard"), *S.Name), Traps ? Traps->Key : 0, S.Plan->NumGullyRuns);

		// 2. Continuous layers: rolling on cloth (60-700 Hz while balls roll, gone after the balls stopped); the coin-op gully runs.
		const double StopTime = S.Shot->Result.StopTime;
		auto ShotIndex = [&](double ShotTime) { return static_cast<int32>(S.Clock.AnchorFrame + (ShotTime - S.Clock.OriginShotTime) * Fs - Table->First); };
		const double RollingDb = RbAudioTestKit::BandRmsDb(Table->Mono, Fs, 60.0, 700.0, ShotIndex(1.0), ShotIndex(2.0));
		const double AfterDb = RbAudioTestKit::BandRmsDb(Table->Mono, Fs, 60.0, 700.0, ShotIndex(StopTime + 2.0), ShotIndex(StopTime + 2.5));
		Test->AddInfo(FString::Printf(TEXT("%s: rolling band 1-2 s %.1f dBFS, after the balls stopped %.1f dBFS"), *S.Name, RollingDb, AfterDb));
		Test->TestTrue(FString::Printf(TEXT("%s: rolling on the cloth is heard (%.1f dBFS, %.1f dB over the settled table)"), *S.Name, RollingDb, RollingDb - AfterDb),
			RollingDb > -110.0 && RollingDb > AfterDb + 20.0);
		int32 Gullies = 0;
		for (const RbAudio::FVoicePlan& V : S.Plan->Voices)
		{
			for (const RbAudio::FContinuousSegment& G : V.Continuous)
			{
				if (G.Kind != RbAudio::ENoiseKind::GullyRun)
				{
					continue;
				}
				++Gullies;
				// The gully run (120-900 Hz band noise with seam bumps, inside the cabinet) between its fade-in and fade-out.
				const int32 B0 = ShotIndex(G.StartTime + 0.15);
				const int32 B1 = ShotIndex(G.EndTime - 0.10);
				const double InRun = RbAudioTestKit::BandRmsDb(Table->Mono, Fs, 120.0, 900.0, B0, B1);
				const double Before = RbAudioTestKit::BandRmsDb(Table->Mono, Fs, 120.0, 900.0, ShotIndex(G.StartTime - 0.25), ShotIndex(G.StartTime - 0.05));
				Test->AddInfo(FString::Printf(TEXT("%s: gully run %.2f-%.2f s: 120-900 Hz band %.1f dBFS (before the drop %.1f)"), *S.Name, G.StartTime, G.EndTime,
					InRun, Before));
				Test->TestTrue(FString::Printf(TEXT("%s: the gully run is heard (%.1f dBFS)"), *S.Name, InRun), InRun > -100.0);
			}
		}
		Test->TestEqual(FString::Printf(TEXT("%s: gully runs of the plan"), *S.Name), Gullies, S.Plan->NumGullyRuns);

		// 3. Footsteps (OnFootstep path) in the foley stem, the loose-ball floor hit in the table stem.
		if (const FCaptureData* Foley = D.Find(TEXT("foley")))
		{
			TArray<RbAudioTestKit::FOnset> Steps;
			RbAudioTestKit::DetectOnsets(Foley->Mono, Fs, Steps, 0.0, 10.0, 5.0, 60.0, 1e-8);
			int32 Heard = 0;
			for (const int64 F : S.FootstepFrames)
			{
				const double Lo = static_cast<double>(F - Foley->First - Block);
				const double Hi = static_cast<double>(F - Foley->First + 4 * Block + 512);
				bool bFound = false;
				for (const RbAudioTestKit::FOnset& O : Steps)
				{
					bFound |= O.Sample >= Lo && O.Sample <= Hi;
				}
				Heard += bFound ? 1 : 0;
			}
			Test->TestEqual(FString::Printf(TEXT("%s: every footstep is heard in the foley stem at its time"), *S.Name), Heard, S.FootstepFrames.Num());
			RbAudioTestKit::WriteWav(FPaths::Combine(StemDir(), S.Name + TEXT("_foley.wav")), Foley->Samples, Foley->Channels, Foley->Rate, true);
		}
		{
			const double Lo = static_cast<double>(S.FloorHitFrame - Table->First - Block);
			const double Hi = static_cast<double>(S.FloorHitFrame - Table->First + 4 * Block + 512);
			bool bFound = false;
			for (const RbAudioTestKit::FOnset& O : Onsets)
			{
				bFound |= O.Sample >= Lo && O.Sample <= Hi;
			}
			Test->TestTrue(FString::Printf(TEXT("%s: the loose-ball floor hit (AU-25) is heard"), *S.Name), bFound);
		}

		// 3b. Loose-ball rolling on the floor (AU-25): 200-3000 Hz noise with tile-joint ticks while it rolls, gone after the pick-up.
		{
			const int64 R0 = S.RollFrame0 - Table->First;
			const int64 R1 = S.RollFrame1 - Table->First;
			const double Rolling = RbAudioTestKit::BandRmsDb(Table->Mono, Fs, 200.0, 3000.0, static_cast<int32>(R0 + 0.15 * Fs), static_cast<int32>(R1 - 0.05 * Fs));
			const double Before = RbAudioTestKit::BandRmsDb(Table->Mono, Fs, 200.0, 3000.0, static_cast<int32>(R0 - 0.6 * Fs), static_cast<int32>(R0 - 0.1 * Fs));
			const double After = RbAudioTestKit::BandRmsDb(Table->Mono, Fs, 200.0, 3000.0, static_cast<int32>(R1 + 0.15 * Fs), static_cast<int32>(R1 + 0.3 * Fs));
			Test->AddInfo(FString::Printf(TEXT("%s: loose ball rolling on the floor %.1f dBFS (200-3000 Hz; before %.1f, after the pick-up %.1f)"), *S.Name, Rolling, Before,
				After));
			Test->TestTrue(FString::Printf(TEXT("%s: the loose ball's rolling on the floor (AU-25) is heard and stops at the pick-up"), *S.Name),
				Rolling > -100.0 && Rolling > Before + 20.0 && Rolling > After + 20.0);
		}

		// 4. Room tone at its level (the ambience stem is after the Ambience slider).
		if (const FCaptureData* Amb = D.Find(TEXT("ambience")))
		{
			const float Slider = URbGameUserSettings::Get() ? URbGameUserSettings::Get()->Volumes.Ambience : 0.8f;
			const double SliderDb = 20.0 * std::log10(FMath::Max(URbAudioSettings::VolumeToGain(Slider, URbAudioSettings::Get()->VolumeTaperExponent), 1e-9f));
			double Power = 0.0;
			for (int32 C = 0; C < Amb->Channels; ++C)
			{
				TArray<double> Ch;
				RbAudioTestKit::ExtractChannel(Amb->Samples, Amb->Channels, C, Ch);
				const int32 N = FMath::Min(Ch.Num(), static_cast<int32>(0.9 * Fs));
				Power += FMath::Pow(10.0, RbAudioTestKit::LaeqDb(TArrayView<double>(Ch.GetData() + static_cast<int32>(0.1 * Fs), N - static_cast<int32>(0.1 * Fs)), Fs,
					FsPa) / 10.0);
			}
			const double Measured = 10.0 * std::log10(Power / FMath::Max(Amb->Channels, 1)) - SliderDb;
			const double Predicted = PredictRoomToneDbA(*Audio, S.ListenerWorld);
			Test->AddInfo(FString::Printf(TEXT("%s: room tone %.1f dB(A) at the listener (predicted %.1f; Ambience slider %.2f = %.1f dB removed)"), *S.Name, Measured,
				Predicted, Slider, SliderDb));
			Test->TestTrue(FString::Printf(TEXT("%s: room tone at its level (%.1f vs %.1f dB(A) +- 3)"), *S.Name, Measured, Predicted), FMath::Abs(Measured - Predicted) <= 3.0);
			RbAudioTestKit::WriteWav(FPaths::Combine(StemDir(), S.Name + TEXT("_ambience.wav")), Amb->Samples, Amb->Channels, Amb->Rate, true);
		}

		// 5. Reverb. (a) The break excites the room: the reverb stem's energy over the break minus its energy before the strike (the
		//    positional room-tone layers send to the reverb as well, e.g. the coolers' 60 Hz hum) against the dry table stem; for a
		//    listener around the critical distance (dive bar ~1.3 m) both are of the same order. (b) The tail after the probe (a loud
		//    floor hit after the break, the room-tone layers muted 1.5 s before it) decays with the venue's RT60.
		if (const FCaptureData* Rev = D.Find(TEXT("reverb")))
		{
			const int64 Shift = Table->First - Rev->First; // table capture index -> reverb capture index
			const double RevBreak = MeanSquare(Rev->Mono, ShotIndex(0.0) + Shift, ShotIndex(3.0) + Shift);
			const double RevBefore = MeanSquare(Rev->Mono, ShotIndex(-0.9) + Shift, ShotIndex(-0.1) + Shift);
			const double DryBreak = MeanSquare(Table->Mono, ShotIndex(0.0), ShotIndex(3.0));
			const double RevDb = 10.0 * std::log10(FMath::Max(RevBreak - RevBefore, 1e-30));
			const double DryDb = 10.0 * std::log10(FMath::Max(DryBreak, 1e-30));
			const int32 P0 = static_cast<int32>(S.ProbeFrame - Rev->First + static_cast<int64>(0.03 * Fs));
			const double Floor = MeanSquare(Rev->Mono, S.ProbeFrame - Rev->First - static_cast<int64>(0.5 * Fs), S.ProbeFrame - Rev->First);
			const double Rt = RbAudioTestKit::SchroederRt60(Rev->Mono, P0, P0 + static_cast<int32>(1.2 * Fs), Fs, 20.0);
			const double ProbeDb = 10.0 * std::log10(FMath::Max(MeanSquare(Rev->Mono, P0, P0 + static_cast<int32>(0.05 * Fs)), 1e-30) / FMath::Max(Floor, 1e-30));
			const FRbVenueAudioProfile& Profile = Audio->GetVenueProfile();
			Test->AddInfo(FString::Printf(TEXT("%s: reverb of the break %.1f dBFS (room tone removed: %.1f dBFS before the strike) vs the dry table stem %.1f dBFS; probe tail %.1f dB over the muted floor, RT60 %.2f s (venue %.2f / %.2f / %.2f s)"),
				*S.Name, RevDb, 10.0 * std::log10(FMath::Max(RevBefore, 1e-30)), DryDb, ProbeDb, Rt, Profile.Rt60[0], Profile.Rt60[1], Profile.Rt60[2]));
			Test->TestTrue(FString::Printf(TEXT("%s: the reverb carries the break (%.1f dB relative to the dry stem, want -20 ... +6)"), *S.Name, RevDb - DryDb),
				RevDb > DryDb - 20.0 && RevDb < DryDb + 6.0);
			Test->TestTrue(FString::Printf(TEXT("%s: the probe's tail stands %.1f dB over the muted floor (>= 35 dB for a T20 fit)"), *S.Name, ProbeDb), ProbeDb >= 35.0);
			Test->TestTrue(FString::Printf(TEXT("%s: reverb tail RT60 %.2f s (the venue's mid band %.2f s +- 0.2)"), *S.Name, Rt, Profile.Rt60[1]),
				FMath::Abs(Rt - Profile.Rt60[1]) <= 0.2);
			RbAudioTestKit::WriteWav(FPaths::Combine(StemDir(), S.Name + TEXT("_reverb.wav")), Rev->Samples, Rev->Channels, Rev->Rate, true);
		}

		// 6. AU-T16: no clipping. The master limiter holds -1 dBTP and acts by at most ~1 dB (the plan-time presentation keeps the
		//    stem below it).
		const double MasterTp = RbAudioTestKit::TruePeakDbtp(Master->Samples, Master->Channels);
		double WorldTp = -300.0;
		if (const FCaptureData* World = D.Find(TEXT("world")))
		{
			WorldTp = RbAudioTestKit::TruePeakDbtp(World->Samples, World->Channels);
		}
		Test->AddInfo(FString::Printf(TEXT("%s: true peak before the limiter (World) %.2f dBTP, after (Master) %.2f dBTP"), *S.Name, WorldTp, MasterTp));
		Test->TestTrue(FString::Printf(TEXT("AU-T16 %s: master true peak %.2f dBTP <= -1 (+0.3)"), *S.Name, MasterTp), MasterTp <= -0.7);
		Test->TestTrue(FString::Printf(TEXT("AU-T16 %s: limiter gain reduction %.2f dB <= 1 (+0.3)"), *S.Name, FMath::Max(0.0, WorldTp - MasterTp)),
			FMath::Max(0.0, WorldTp - MasterTp) <= 1.3);

		// 7. AU-T19: the summed render time of the table's 30 voices per device block (they run on the source workers in parallel).
		if (S.Profile.IsValid())
		{
			// CPU time of the voices' callbacks (thread cycle counters: other processes preempting the source workers on a busy
			// machine do not count); the wall time is reported next to it. The 30 voices run on the 4 source workers, so the sum
			// over all of them is an upper bound of the render thread's share.
			int64 AtBlock = 0;
			int64 AtBlockWall = 0;
			const double MaxMicros = S.Profile->MaxBlockMicros(&AtBlock);
			const double MaxWall = S.Profile->MaxBlockMicros(&AtBlockWall, true);
			const double BlockMicros = Block / Fs * 1e6;
			Test->AddInfo(FString::Printf(TEXT("AU-T19 %s: peak summed voice render CPU time %.0f us per %.0f us block (%.1f %%) at %.3f s into the shot; wall time %.0f us (%.1f %%) at %.3f s; %d samples"),
				*S.Name, MaxMicros, BlockMicros, 100.0 * MaxMicros / BlockMicros, (AtBlock - S.Clock.AnchorFrame) / Fs, MaxWall, 100.0 * MaxWall / BlockMicros,
				(AtBlockWall - S.Clock.AnchorFrame) / Fs, S.Profile->NumSamples()));
			Test->TestTrue(FString::Printf(TEXT("AU-T19 %s: %.1f %% of a block <= 60 %%"), *S.Name, 100.0 * MaxMicros / BlockMicros), MaxMicros <= 0.6 * BlockMicros);
		}

		// Recordings.
		const FString MasterWav = FPaths::Combine(OutDir(), S.Name + TEXT("_break_master.wav"));
		Test->TestTrue(TEXT("master mix written"), RbAudioTestKit::WriteWav(MasterWav, Master->Samples, Master->Channels, Master->Rate, false));
		RbAudioTestKit::WriteWav(FPaths::Combine(StemDir(), S.Name + TEXT("_table.wav")), Table->Samples, Table->Channels, Table->Rate, true);
		if (const FCaptureData* World = D.Find(TEXT("world")))
		{
			RbAudioTestKit::WriteWav(FPaths::Combine(StemDir(), S.Name + TEXT("_world.wav")), World->Samples, World->Channels, World->Rate, true);
		}
		FString Meta = FString::Printf(TEXT("name=%s\nfs=%.0f\nfull_scale_pa=%.6f\nanchor_frame=%lld\ntable_first=%lld\nmaster_first=%lld\nstop_time=%.6f\n"), *S.Name, Fs, FsPa,
			S.Clock.AnchorFrame, Table->First, Master->First, StopTime);
		// The live (non-plan) sounds and the continuous segments in capture frames of the table stem (Tools/audio/m2_report.py marks them).
		for (int32 I = 0; I < S.FootstepFrames.Num(); ++I)
		{
			Meta += FString::Printf(TEXT("footstep_%d=%lld\n"), I, static_cast<long long>(S.FootstepFrames[I] - Table->First));
		}
		Meta += FString::Printf(TEXT("floor_hit=%lld\nfloor_roll_0=%lld\nfloor_roll_1=%lld\n"), static_cast<long long>(S.FloorHitFrame - Table->First),
			static_cast<long long>(S.RollFrame0 - Table->First), static_cast<long long>(S.RollFrame1 - Table->First));
		{
			int32 G = 0;
			for (const RbAudio::FVoicePlan& V : S.Plan->Voices)
			{
				for (const RbAudio::FContinuousSegment& Seg : V.Continuous)
				{
					if (Seg.Kind == RbAudio::ENoiseKind::GullyRun)
					{
						const double F0 = S.Clock.AnchorFrame + (Seg.StartTime - S.Clock.OriginShotTime) / S.Clock.Rate * Fs - Table->First;
						const double F1 = S.Clock.AnchorFrame + (Seg.EndTime - S.Clock.OriginShotTime) / S.Clock.Rate * Fs - Table->First;
						Meta += FString::Printf(TEXT("gully_%d=%.0f,%.0f\n"), G++, F0, F1);
					}
				}
			}
		}
		FFileHelper::SaveStringToFile(Meta, *FPaths::Combine(StemDir(), S.Name + TEXT("_meta.txt")));
		Test->AddInfo(FString::Printf(TEXT("%s: master mix %s, stems %s"), *S.Name, *MasterWav, *StemDir()));
		return true;
	}

	TArray<FStep> BreakSteps(FAutomationTestBase* Test, const TSharedRef<FBreakState>& S)
	{
		TArray<FStep> Steps;
		Steps.Add({TEXT("audio ready, table and shot"), 40.0, [Test, S](double T, int32)
		{
			URbAudioSubsystem* Audio = ReadyAudio(Test, T, 40.0);
			UWorld* World = PlayWorld();
			ARbGameMode* Mode = World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
			if (!Audio || !Mode || !Mode->GetTable() || !Mode->GetBallSet())
			{
				return Test->HasAnyErrors();
			}
			FRbShotRequest Request;
			FString Error;
			if (S->bDiveBar)
			{
				Audio->SetVenue(ERbVenue::DiveBar);
				// The 7-ft bar box at its place in the venue frame (venue-dive-bar 2.1), the level's test-room table stays silent.
				const FTransform At(FRotator::ZeroRotator, FVector(1375.9, 542.7, 0.0));
				ARbTable* Table = World->SpawnActorDeferred<ARbTable>(ARbTable::StaticClass(), At);
				Table->Preset = ERbTablePreset::SevenFootBar;
				Table->BallSet = ERbBallSetPreset::OldBarOversizedCue;
				Table->BallSetSeed = 13;
				Table->TableIndex = 1;
				Table->LampUndersideHeight = 0.0;
				Table->FinishSpawning(At);
				ARbBallSet* Balls = World->SpawnActor<ARbBallSet>(ARbBallSet::StaticClass(), At);
				Balls->InitForTable(Table);
				S->Spawned.Add(Table);
				S->Spawned.Add(Balls);
				S->Table = Table;
				S->Balls = Balls;
				Test->TestTrue(TEXT("dive-bar scenario"), RbAudioScenarios::MakeDiveBarBreak8(Request, Error));
			}
			else
			{
				S->Table = Mode->GetTable();
				S->Balls = Mode->GetBallSet();
				Test->TestTrue(TEXT("break9 scenario"), RbAudioScenarios::MakeTestRoomBreak9(Request, Error));
			}
			RbAudioScenarios::BreakerHead(Request, S->Head, S->Left);
			S->Shot = RbAudioScenarios::Simulate(MoveTemp(Request), Error);
			if (!Test->TestTrue(FString::Printf(TEXT("break simulated (%s)"), *Error), S->Shot.IsValid() && S->Shot->Result.Status == rb::SimStatus::Ok))
			{
				return true;
			}
			URbTableAudioComponent* TableAudio = Audio->GetOrCreateTableAudio(S->Table.Get());
			if (TableAudio && !TableAudio->GetBoundPlayback())
			{
				TableAudio->BindPlayback(S->Balls->GetPlayback());
			}
			S->TableAudio = TableAudio;
			S->Profile = MakeShared<FRbAudioRenderProfile, ESPMode::ThreadSafe>();
			// The listener: the breaker's head (0.55 m behind the cue ball, eye 0.36 m above the cloth), facing the aim.
			ARbTable* Table = S->Table.Get();
			S->ListenerWorld = Table->CoreToWorld(S->Head);
			const rb::StrikeRequest& Strike = S->Shot->Request.Input.Strikes[0];
			const rb::Vec3 Aim(std::cos(Strike.Input.Azimuth), std::sin(Strike.Input.Azimuth), 0.0);
			const FVector Facing = Table->CoreToWorld(S->Head + Aim) - S->ListenerWorld;
			SetListener(S->ListenerWorld, Facing.Rotation());
			return true;
		}});
		Steps.Add({TEXT("table voices render (T0)"), 20.0, [Test, S](double T, int32)
		{
			URbTableAudioComponent* TableAudio = S->TableAudio.Get();
			URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
			if (!TableAudio || !Audio || TableAudio->GetTier() != ERbTableAudioTier::T0 || TableAudio->GetVoices().Num() != FRbAudioPlanBuilder::NumT0Voices)
			{
				return false;
			}
			bool bAll = true;
			for (URbImpactVoiceComponent* V : TableAudio->GetVoices())
			{
				bAll &= V && V->IsRendering();
			}
			// The table's reverb feed (the room hears the radiated power through it; review M2-C).
			if (Audio->GetReverbSubmix())
			{
				bAll &= TableAudio->GetReverbFeedVoice() && TableAudio->GetReverbFeedVoice()->IsRendering();
			}
			for (URbAmbienceVoiceComponent* V : Audio->GetAmbienceVoices())
			{
				bAll &= V && V->IsRendering();
			}
			if (!bAll)
			{
				if (T > 10.0 && !S->bDiagnosed)
				{
					S->bDiagnosed = true;
					for (URbTableAudioComponent* A : Audio->GetTableAudio())
					{
						for (int32 I = 0; A && I < A->GetVoices().Num(); I += 7)
						{
							Test->AddInfo(RbAudioTestKit::DescribeVoice(A->GetVoices()[I]));
						}
					}
					for (URbAmbienceVoiceComponent* V : Audio->GetAmbienceVoices())
					{
						Test->AddInfo(FString::Printf(TEXT("%s: rendering %d"), V ? *V->GetName() : TEXT("null"), V && V->IsRendering() ? 1 : 0));
					}
				}
				return false;
			}
			if (T < 1.0)
			{
				return false; // let the ambience settle (filters, drift)
			}
			TableAudio->SetProfile(S->Profile);
			StartCaptures(*S, *Audio);
			return true;
		}});
		Steps.Add(Wait(1.0)); // room tone before the break
		Steps.Add({TEXT("strike"), 5.0, [S](double, int32)
		{
			URbShotPlaybackComponent* Playback = S->Balls->GetPlayback();
			S->Shot->Request.ContactTime = Playback->ClockNow();
			Playback->Play(S->Shot.ToSharedRef(), true);
			S->StrikeSeconds = FPlatformTime::Seconds();
			return true;
		}});
		Steps.Add({TEXT("plan built and the clock anchored"), 5.0, [S](double, int32)
		{
			URbTableAudioComponent* TableAudio = S->TableAudio.Get();
			if (!TableAudio || TableAudio->IsPlanPending())
			{
				return false;
			}
			S->Plan = TableAudio->GetLastPlan();
			S->Clock = RbAudioTestKit::ReadClock(TableAudio->GetClock());
			return S->Plan.IsValid() && S->Clock.bAnchored && S->Clock.ShotId == TableAudio->GetPlaySerial();
		}});
		Steps.Add({TEXT("footsteps and a loose-ball floor hit during the break"), 10.0, [S](double, int32)
		{
			URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
			const double Since = FPlatformTime::Seconds() - S->StrikeSeconds;
			const URbImpactVoiceComponent* Ref = S->TableAudio->GetVoices()[0];
			const double StepTimes[3] = {2.0, 2.55, 3.1};
			if (S->LiveStep < 3 && Since >= StepTimes[S->LiveStep])
			{
				FRbFootstep Step;
				Step.WorldLocation = FVector(S->ListenerWorld.X - 30.0, S->ListenerWorld.Y + (S->LiveStep % 2 ? 12.0 : -12.0), S->Table->GetActorLocation().Z);
				Step.bLeftFoot = (S->LiveStep % 2) == 0;
				Step.SpeedMps = 1.2f;
				S->FootstepFrames.Add(CurrentDeviceFrame(Ref));
				Audio->PlayFootstep(Step, true);
				++S->LiveStep;
			}
			if (S->LiveStep == 3 && Since >= 3.6)
			{
				FRbLooseBallImpact Hit;
				Hit.TableIndex = S->Table->TableIndex;
				Hit.BallId = 3;
				Hit.WorldLocation = S->Table->GetActorLocation() + FVector(-60.0, 110.0, 0.0);
				Hit.Normal = FVector::UpVector;
				Hit.NormalSpeed = 1.5;
				Hit.MassKg = 0.163;
				Hit.Surface = RbAssetPaths::Surface::Vct;
				S->FloorHitFrame = CurrentDeviceFrame(Ref);
				Audio->HandleLooseBallImpact(Hit);
				Audio->HandleLooseBallReturned(Hit.TableIndex, Hit.BallId);
				return true;
			}
			return false;
		}});
		Steps.Add({TEXT("the shot plays out, tails and gully runs end"), 30.0, [S](double, int32)
		{
			const double Since = FPlatformTime::Seconds() - S->StrikeSeconds;
			return !S->Balls->GetPlayback()->IsPlaying() && Since > S->Shot->Result.StopTime + 3.0;
		}});
		// AU-25 rolling: a loose ball rolls across the floor for 0.8 s after the table came to rest (OnRolling samples every frame,
		// 1.0 -> 0.6 m/s), then it is picked up (OnReturned).
		Steps.Add({TEXT("a loose ball rolls on the floor"), 5.0, [S](double T, int32)
		{
			URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
			const URbImpactVoiceComponent* Ref = S->TableAudio->GetVoices()[0];
			if (S->RollFrame0 == 0)
			{
				S->RollFrame0 = CurrentDeviceFrame(Ref);
			}
			if (T < 0.8)
			{
				FRbLooseBallRolling Roll;
				Roll.TableIndex = S->Table->TableIndex;
				Roll.BallId = 5;
				Roll.WorldLocation = S->Table->GetActorLocation() + FVector(-120.0 + 100.0 * T, 130.0, 2.9);
				Roll.SpeedMps = 1.0 - 0.5 * T;
				Roll.Surface = S->bDiveBar ? RbAssetPaths::Surface::Vct : RbAssetPaths::Surface::Concrete;
				Audio->HandleLooseBallRolling(Roll);
				return false;
			}
			Audio->HandleLooseBallReturned(S->Table->TableIndex, 5);
			S->RollFrame1 = CurrentDeviceFrame(Ref);
			return true;
		}});
		Steps.Add(Wait(0.3));
		Steps.Add({TEXT("room tone off (a quiet reverb stem for the probe)"), 5.0, [S](double, int32)
		{
			URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
			MuteAmbience(*Audio, true);
			S->MuteFrame = CurrentDeviceFrame(S->TableAudio->GetVoices()[0]);
			return true;
		}});
		Steps.Add(Wait(1.5));
		Steps.Add({TEXT("reverb probe"), 5.0, [S](double, int32)
		{
			URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
			FRbLooseBallImpact Hit;
			Hit.TableIndex = S->Table->TableIndex;
			Hit.BallId = 4;
			Hit.WorldLocation = S->Table->GetActorLocation() + FVector(-90.0, -110.0, 0.0);
			Hit.Normal = FVector::UpVector;
			Hit.NormalSpeed = 3.0;
			Hit.MassKg = 0.17;
			Hit.Surface = RbAssetPaths::Surface::Vct;
			S->ProbeFrame = CurrentDeviceFrame(S->TableAudio->GetVoices()[0]);
			Audio->HandleLooseBallImpact(Hit);
			Audio->HandleLooseBallReturned(Hit.TableIndex, Hit.BallId);
			return true;
		}});
		Steps.Add(Wait(1.6));
		Steps.Add({TEXT("analyse"), 60.0, [Test, S](double, int32)
		{
			RunBreakAnalysis(Test, *S);
			if (URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld()))
			{
				MuteAmbience(*Audio, false);
			}
			ClearListener();
			if (S->TableAudio.IsValid())
			{
				S->TableAudio->SetProfile(nullptr);
			}
			for (TWeakObjectPtr<AActor>& A : S->Spawned)
			{
				if (A.IsValid())
				{
					A->Destroy();
				}
			}
			return true;
		}});
		return Steps;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioBreakDiveBarTest, "RawBreak.Functional.Audio.RecordedBreak_DiveBar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbAudioBreakDiveBarTest::RunTest(const FString& Parameters)
{
	if (SkipWithoutSound(this))
	{
		return true;
	}
	TSharedRef<FBreakState> S = MakeShared<FBreakState>();
	S->bDiveBar = true;
	S->Name = TEXT("divebar");
	AllowUnfocusedAudio(true);
	AutomationOpenMap(RbAssetPaths::M1TestRoomMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FScriptCommand(this, BreakSteps(this, S)));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioBreakTestRoomTest, "RawBreak.Functional.Audio.RecordedBreak_TestRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbAudioBreakTestRoomTest::RunTest(const FString& Parameters)
{
	if (SkipWithoutSound(this))
	{
		return true;
	}
	TSharedRef<FBreakState> S = MakeShared<FBreakState>();
	S->bDiveBar = false;
	S->Name = TEXT("testroom");
	AllowUnfocusedAudio(true);
	AutomationOpenMap(RbAssetPaths::M1TestRoomMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FScriptCommand(this, BreakSteps(this, S)));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------
// Volumes, replay mix and slow motion, pause without a click, pause mix
// ---------------------------------------------------------------------------------------------------------------------------

	struct FMixState
	{
		TWeakObjectPtr<ARbTable> Table;
		TWeakObjectPtr<ARbBallSet> Balls;
		TWeakObjectPtr<URbTableAudioComponent> TableAudio;
		TSharedPtr<FRbShot> TwoBall;
		TSharedPtr<FRbShot> Break;
		FVector Listener = FVector::ZeroVector;
		TMap<FString, FCapturePtr> Captures;
		FRbAudioVolumes SavedVolumes;
		// Clock states / windows (device frames) of the phases.
		RbAudioTestKit::FClockState LiveA;
		RbAudioTestKit::FClockState LiveB;
		RbAudioTestKit::FClockState Slow;
		int64 QuietA0 = 0, QuietA1 = 0, QuietB0 = 0, QuietB1 = 0, Replay0 = 0, Replay1 = 0;
		int64 Pause0 = 0, Resume0 = 0;
		int64 MixBefore0 = 0, MixOn0 = 0, MixOff0 = 0;
		RbAudioTestKit::FClockState BreakBefore;
		RbAudioTestKit::FClockState BreakAfter;
		double PhaseStart = 0.0;
		TSharedPtr<const FRbShotAudioPlan, ESPMode::ThreadSafe> PlanA; // the two-ball shot's plan (its impacts' arrival times)
		TArray<RbAudioTestKit::FClockState> SlowGenerations;          // every anchored mapping of the replay (diagnostics)
	};

	int64 Frame(const FMixState& S)
	{
		return S.TableAudio.IsValid() && S.TableAudio->GetVoices().Num() > 0 ? CurrentDeviceFrame(S.TableAudio->GetVoices()[0]) : 0;
	}

	void PlayLive(FMixState& S, const TSharedPtr<FRbShot>& Shot)
	{
		URbShotPlaybackComponent* Playback = S.Balls->GetPlayback();
		Shot->Request.ContactTime = Playback->ClockNow();
		Playback->Play(Shot.ToSharedRef(), true);
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioMixTest, "RawBreak.Functional.Audio.MixReplayPauseVolumes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbAudioMixTest::RunTest(const FString& Parameters)
{
	if (SkipWithoutSound(this))
	{
		return true;
	}
	TSharedRef<FMixState> S = MakeShared<FMixState>();
	TArray<FStep> Steps;
	Steps.Add({TEXT("audio ready"), 40.0, [this, S](double T, int32)
	{
		URbAudioSubsystem* Audio = ReadyAudio(this, T, 40.0);
		UWorld* World = PlayWorld();
		ARbGameMode* Mode = World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
		if (!Audio || !Mode || !Mode->GetTable() || !Mode->GetBallSet())
		{
			return HasAnyErrors();
		}
		S->Table = Mode->GetTable();
		S->Balls = Mode->GetBallSet();
		S->TableAudio = Audio->GetOrCreateTableAudio(S->Table.Get());
		if (S->TableAudio.IsValid() && !S->TableAudio->GetBoundPlayback())
		{
			S->TableAudio->BindPlayback(S->Balls->GetPlayback());
		}
		FRbShotRequest Two, Brk;
		FString Error;
		TestTrue(TEXT("two-ball shot"), RbSimScenarios::MakeTwoBall(Two, Error));
		TestTrue(TEXT("break9"), RbAudioScenarios::MakeTestRoomBreak9(Brk, Error));
		rb::Vec3 Head, Left;
		RbAudioScenarios::BreakerHead(Brk, Head, Left);
		S->TwoBall = RbAudioScenarios::Simulate(MoveTemp(Two), Error);
		S->Break = RbAudioScenarios::Simulate(MoveTemp(Brk), Error);
		S->Listener = S->Table->CoreToWorld(Head);
		SetListener(S->Listener, FRotator::ZeroRotator);
		if (URbGameUserSettings* User = URbGameUserSettings::Get())
		{
			S->SavedVolumes = User->Volumes;
		}
		return S->TwoBall.IsValid() && S->Break.IsValid();
	}});
	Steps.Add({TEXT("voices render, captures"), 20.0, [S](double T, int32)
	{
		URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
		URbTableAudioComponent* TableAudio = S->TableAudio.Get();
		if (!Audio || !TableAudio || TableAudio->GetVoices().Num() == 0 || !TableAudio->GetVoices()[0]->IsRendering() || T < 1.0
			|| (Audio->GetReverbSubmix() && !(TableAudio->GetReverbFeedVoice() && TableAudio->GetReverbFeedVoice()->IsRendering())))
		{
			return false;
		}
		for (const TPair<const TCHAR*, USoundSubmix*>& P : {TPair<const TCHAR*, USoundSubmix*>(TEXT("master"), Audio->GetSubmix(ERbAudioBus::Master)),
			TPair<const TCHAR*, USoundSubmix*>(TEXT("table"), Audio->GetSubmix(ERbAudioBus::Table)),
			TPair<const TCHAR*, USoundSubmix*>(TEXT("ambience"), Audio->GetSubmix(ERbAudioBus::Ambience)),
			TPair<const TCHAR*, USoundSubmix*>(TEXT("reverb"), Audio->GetReverbSubmix())})
		{
			S->Captures.Add(P.Key, FRbSubmixCapture::Start(PlayWorld(), P.Value, 60.0, FString::Printf(TEXT("RbMix_%s"), P.Key)));
		}
		return true;
	}});
	// A: the two-ball shot live at the default volumes.
	Steps.Add({TEXT("quiet A"), 5.0, [S](double T, int32) { if (T < 0.05) { S->QuietA0 = Frame(*S); } if (T >= 1.0) { S->QuietA1 = Frame(*S); return true; } return false; }});
	Steps.Add({TEXT("live A"), 10.0, [S](double T, int32)
	{
		if (T < 0.01)
		{
			PlayLive(*S, S->TwoBall);
			return false;
		}
		// The clock stops (and drops its anchor) when the playback ends: keep the anchored state of this shot.
		const RbAudioTestKit::FClockState Now = RbAudioTestKit::ReadClock(S->TableAudio->GetClock());
		if (Now.bAnchored)
		{
			S->LiveA = Now;
		}
		// The analysis needs the first second of the shot: stop it after 2 s (the two-ball shot rolls on for several seconds).
		if (T >= 2.0 && S->Balls->GetPlayback()->IsPlaying())
		{
			S->Balls->GetPlayback()->Stop(true);
		}
		if (S->LiveA.bAnchored && !S->PlanA.IsValid())
		{
			S->PlanA = S->TableAudio->GetLastPlan();
		}
		return !S->Balls->GetPlayback()->IsPlaying() && T > 1.0 && S->LiveA.bAnchored && S->PlanA.IsValid();
	}});
	// B: Table 0.5 and Ambience 0.5 (-12 dB on the table stem; ambience 0.8 -> 0.5: -8.2 dB).
	Steps.Add({TEXT("volumes 0.5"), 5.0, [S](double T, int32)
	{
		if (T < 0.01)
		{
			URbGameUserSettings* User = URbGameUserSettings::Get();
			User->Volumes.Table = 0.5f;
			User->Volumes.Ambience = 0.5f;
			URbGameUserSettings::OnSettingsChanged().Broadcast();
			return false;
		}
		if (T < 0.3)
		{
			return false;
		}
		if (S->QuietB0 == 0)
		{
			S->QuietB0 = Frame(*S);
		}
		if (T >= 1.3)
		{
			S->QuietB1 = Frame(*S);
			return true;
		}
		return false;
	}});
	Steps.Add({TEXT("live B"), 10.0, [S](double T, int32)
	{
		if (T < 0.01)
		{
			PlayLive(*S, S->TwoBall);
			return false;
		}
		const RbAudioTestKit::FClockState Now = RbAudioTestKit::ReadClock(S->TableAudio->GetClock());
		if (Now.bAnchored && Now.ShotId != S->LiveA.ShotId)
		{
			S->LiveB = Now;
		}
		if (T >= 2.0 && S->Balls->GetPlayback()->IsPlaying())
		{
			S->Balls->GetPlayback()->Stop(true);
		}
		if (!S->Balls->GetPlayback()->IsPlaying() && T > 1.0 && S->LiveB.bAnchored && S->LiveB.ShotId != S->LiveA.ShotId)
		{
			URbGameUserSettings* User = URbGameUserSettings::Get();
			User->Volumes = S->SavedVolumes;
			URbGameUserSettings::OnSettingsChanged().Broadcast();
			return true;
		}
		return false;
	}});
	Steps.Add(Wait(0.5));
	// C: a replay at x 0.25 (film style; the replay mix lowers the ambience by 10 dB).
	Steps.Add({TEXT("slow-motion replay"), 20.0, [this, S](double T, int32)
	{
		if (T < 0.01)
		{
			S->Balls->GetPlayback()->Play(S->TwoBall.ToSharedRef(), false, 0.0, 0.25f);
			return false;
		}
		URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
		if (T > 0.6 && S->Replay0 == 0)
		{
			S->Replay0 = Frame(*S);
			TestTrue(TEXT("the replay mix is active during a replay"), Audio->IsReplayMix());
		}
		if (T > 1.6 && S->Replay1 == 0)
		{
			S->Replay1 = Frame(*S);
		}
		const RbAudioTestKit::FClockState Now = RbAudioTestKit::ReadClock(S->TableAudio->GetClock());
		if (Now.bAnchored && Now.ShotId != S->LiveB.ShotId)
		{
			if (!S->Slow.bAnchored)
			{
				S->Slow = Now; // the first anchored mapping of the replay
			}
			if (S->SlowGenerations.Num() == 0 || S->SlowGenerations.Last().Generation != Now.Generation)
			{
				S->SlowGenerations.Add(Now);
			}
		}
		// x 0.25: the first second of the shot takes 4 s (the analysis looks at the first two impacts).
		if (T >= 4.5 && S->Balls->GetPlayback()->IsPlaying())
		{
			S->Balls->GetPlayback()->Stop(true);
		}
		return !S->Balls->GetPlayback()->IsPlaying() && T > 2.0 && S->Replay1 != 0 && S->Slow.bAnchored;
	}});
	Steps.Add(Wait(0.8));
	// D: pause mid-shot (break9): the table holds without a click, resumes later.
	Steps.Add({TEXT("pause mid-shot"), 20.0, [S](double T, int32)
	{
		URbShotPlaybackComponent* Playback = S->Balls->GetPlayback();
		if (T < 0.01)
		{
			PlayLive(*S, S->Break);
			return false;
		}
		if (T >= 0.8 && S->Pause0 == 0)
		{
			S->BreakBefore = RbAudioTestKit::ReadClock(S->TableAudio->GetClock());
			S->Pause0 = Frame(*S);
			Playback->SetPaused(true);
		}
		if (T >= 1.8 && S->Resume0 == 0)
		{
			S->Resume0 = Frame(*S);
			Playback->SetPaused(false);
		}
		if (T >= 2.3 && S->Resume0 != 0)
		{
			S->BreakAfter = RbAudioTestKit::ReadClock(S->TableAudio->GetClock());
			Playback->Stop(true);
			return true;
		}
		return false;
	}});
	Steps.Add(Wait(1.0));
	// E: the pause mix (the UI's SetPausedMix): World -12 dB and low-passed at 800 Hz, then released.
	Steps.Add({TEXT("pause mix"), 10.0, [S](double T, int32)
	{
		URbAudioSubsystem* Audio = URbAudioSubsystem::Get(PlayWorld());
		if (S->MixBefore0 == 0)
		{
			S->MixBefore0 = Frame(*S);
		}
		if (T >= 1.0 && S->MixOn0 == 0)
		{
			S->MixOn0 = Frame(*S);
			Audio->SetPausedMix(true);
		}
		if (T >= 2.5 && S->MixOff0 == 0)
		{
			S->MixOff0 = Frame(*S);
			Audio->SetPausedMix(false);
		}
		return T >= 4.0;
	}});
	Steps.Add({TEXT("analyse"), 60.0, [this, S](double, int32)
	{
		TMap<FString, FCaptureData> D;
		for (TPair<FString, FCapturePtr>& C : S->Captures)
		{
			D.Add(C.Key, Collect(C.Value));
			// A gap is a device underrun (the audio clock jumped over unrendered blocks: an overloaded machine); the level and
			// sub-sample checks below assume a continuous recording, so it fails here with its real cause.
			TestEqual(FString::Printf(TEXT("%s capture without gaps (a gap = a device underrun on an overloaded machine: re-run)"), *C.Key),
				C.Value.IsValid() ? C.Value->GetGaps() : -1, 0);
		}
		ClearListener();
		const FCaptureData* Table = D.Find(TEXT("table"));
		const FCaptureData* Master = D.Find(TEXT("master"));
		const FCaptureData* Amb = D.Find(TEXT("ambience"));
		if (!TestTrue(TEXT("captures"), Table && Master && Amb && Table->Frames() > 0))
		{
			return true;
		}
		const double Fs = Table->Rate;
		auto Idx = [](const FCaptureData& C, int64 Frame) { return static_cast<int32>(Frame - C.First); };
		// Volumes: the table stem after the Table slider 0.5 -> -12.04 dB; ambience 0.8 -> 0.5: -8.16 dB.
		const int64 A0 = S->LiveA.AnchorFrame;
		const int64 B0 = S->LiveB.AnchorFrame;
		// Energy, not the sample peak: the two live shots start at different fractional shot times (the playback clock's origin), so
		// their band-limited clicks sit at different sub-sample positions and their sample peaks differ by up to ~1 dB, while
		// the energy of a band-limited signal does not depend on the sub-sample position.
		const int32 Len = static_cast<int32>(1.2 * Fs);
		const double EnergyA = RbAudioTestKit::RmsDb(Table->Mono, Idx(*Table, A0) - 64, Idx(*Table, A0) + Len);
		const double EnergyB = RbAudioTestKit::RmsDb(Table->Mono, Idx(*Table, B0) - 64, Idx(*Table, B0) + Len);
		const double TableDb = EnergyB - EnergyA;
		TestTrue(FString::Printf(TEXT("Table slider 0.5: the shot is %.2f dB quieter (want -12.04 +- 0.5)"), TableDb), FMath::Abs(TableDb + 12.04) <= 0.5);
		const double AmbA = RbAudioTestKit::RmsDb(Amb->Mono, Idx(*Amb, S->QuietA0), Idx(*Amb, S->QuietA1));
		const double AmbB = RbAudioTestKit::RmsDb(Amb->Mono, Idx(*Amb, S->QuietB0), Idx(*Amb, S->QuietB1));
		TestTrue(FString::Printf(TEXT("Ambience slider 0.8 -> 0.5: room tone %.2f dB (want -8.16 +- 1.0)"), AmbB - AmbA), FMath::Abs(AmbB - AmbA + 8.16) <= 1.0);
		// Replay mix: ambience -10 dB during the replay.
		const double AmbReplay = RbAudioTestKit::RmsDb(Amb->Mono, Idx(*Amb, S->Replay0), Idx(*Amb, S->Replay1));
		AddInfo(FString::Printf(TEXT("volumes: Table slider 0.5 -> the shot %.2f dB (want -12.04); Ambience 0.8 -> 0.5 -> room tone %.2f dB (want -8.16); replay mix: ambience %.2f dB (want -10)"),
			TableDb, AmbB - AmbA, AmbReplay - AmbA));
		TestTrue(FString::Printf(TEXT("replay mix: ambience %.2f dB during the replay (want -10 +- 1.5)"), AmbReplay - AmbA), FMath::Abs(AmbReplay - AmbA + 10.0) <= 1.5);
		// The reverb submix is shared by every bus: the voices' sends carry their bus volume, so the sliders turn the reverb return
		// down with the dry sound (the shot through the Table slider, the room tone's reverb through the Ambience slider), and the
		// whole shot in the master drops by the slider's -12.04 dB.
		{
			const int32 RLen = static_cast<int32>(1.2 * Fs);
			const double MasA = RbAudioTestKit::RmsDb(Master->Mono, Idx(*Master, A0), Idx(*Master, A0) + RLen);
			const double MasB = RbAudioTestKit::RmsDb(Master->Mono, Idx(*Master, B0), Idx(*Master, B0) + RLen);
			TestTrue(FString::Printf(TEXT("Table slider 0.5: the shot in the master (dry + reverb) %.2f dB (want -12.04 +- 1.0)"), MasB - MasA),
				FMath::Abs(MasB - MasA + 12.04) <= 1.0);
			if (const FCaptureData* Rev = D.Find(TEXT("reverb")))
			{
				const double RevA = RbAudioTestKit::RmsDb(Rev->Mono, Idx(*Rev, A0), Idx(*Rev, A0) + RLen);
				const double RevB = RbAudioTestKit::RmsDb(Rev->Mono, Idx(*Rev, B0), Idx(*Rev, B0) + RLen);
				const double RevQA = RbAudioTestKit::RmsDb(Rev->Mono, Idx(*Rev, S->QuietA0), Idx(*Rev, S->QuietA1));
				const double RevQB = RbAudioTestKit::RmsDb(Rev->Mono, Idx(*Rev, S->QuietB0), Idx(*Rev, S->QuietB1));
				AddInfo(FString::Printf(TEXT("reverb return: the shot %.2f dB with the Table slider 0.5 (master %.2f dB); the room tone's %.2f dB with the Ambience slider 0.8 -> 0.5"),
					RevB - RevA, MasB - MasA, RevQB - RevQA));
				TestTrue(FString::Printf(TEXT("Table slider 0.5: the shot's reverb return %.2f dB (want -12.04 +- 1.0)"), RevB - RevA), FMath::Abs(RevB - RevA + 12.04) <= 1.0);
				TestTrue(FString::Printf(TEXT("Ambience slider 0.8 -> 0.5: the room tone's reverb return %.2f dB (want -8.16 +- 1.5)"), RevQB - RevQA),
					FMath::Abs(RevQB - RevQA + 8.16) <= 1.5);
			}
		}
		// Slow motion x 0.25 (film style): the tip strike and the first ball-ball click of the two-ball shot, found at their scheduled
		// frames (onsets of the click band, above 1 kHz: the rolling rumble stays below), 4 x farther apart than live.
		TArray<RbAudioTestKit::FOnset> Onsets;
		RbAudioTestKit::DetectOnsets(Table->Mono, Fs, Onsets, 1000.0, 10.0, 2.0, 0.5, 1e-6);
		auto Nearest = [&](double Want) -> double
		{
			double Best = -1e30;
			for (const RbAudioTestKit::FOnset& O : Onsets)
			{
				if (FMath::Abs(O.Sample - Want) < FMath::Abs(Best - Want))
				{
					Best = O.Sample;
				}
			}
			return FMath::Abs(Best - Want) <= 0.0015 * Fs ? Best : -1e30;
		};
		const FRbAudioPlanImpact* Tip = nullptr;
		const FRbAudioPlanImpact* Click = nullptr;
		if (S->PlanA.IsValid())
		{
			for (const FRbAudioPlanImpact& I : S->PlanA->Impacts)
			{
				Tip = !Tip && I.Kind == RbAudio::EImpactKind::TipStrike ? &I : Tip;
				Click = !Click && I.Kind == RbAudio::EImpactKind::BallBall ? &I : Click;
			}
		}
		if (TestTrue(TEXT("the two-ball plan has a tip strike and a ball-ball click"), Tip && Click))
		{
			// Scheduled capture index of an impact under a clock mapping. Film style: shot time runs at the rate, the propagation to
			// the listener does not.
			auto Scheduled = [&](const RbAudioTestKit::FClockState& C, const FRbAudioPlanImpact& I)
			{
				return static_cast<double>(C.AnchorFrame - Table->First) + ((I.ShotTime - C.OriginShotTime) / C.Rate + (I.ArrivalTime - I.ShotTime)) * Fs;
			};
			const double SchedTipLive = Scheduled(S->LiveA, *Tip);
			const double SchedClickLive = Scheduled(S->LiveA, *Click);
			const double SchedTipSlow = Scheduled(S->Slow, *Tip);
			const double SchedClickSlow = Scheduled(S->Slow, *Click);
			const bool bFound = Nearest(SchedTipLive) > -1e29 && Nearest(SchedClickLive) > -1e29 && Nearest(SchedTipSlow) > -1e29 && Nearest(SchedClickSlow) > -1e29;
			TestTrue(TEXT("slow motion x 0.25: the tip strike and the first click are heard at their scheduled frames (+-1.5 ms, live and replay)"), bFound);
			// Sub-sample: the same impact in the replay against the live one, each window cut at its own scheduled frame (the waveforms
			// are the same: natural pitch); a replay that follows the rate exactly puts both at 0 samples relative shift.
			// A short window (10.7 ms) keeps the click's energy and little of the rolling / sliding noise around it, which differs
			// between live and replay (time-stretched) and would bias the phase slope.
			constexpr int32 WinPre = 64;
			constexpr int32 WinN = 512;
			auto Shift = [&](double Live, double Slow)
			{
				const TArray<double> A = Window(Table->Mono, static_cast<int64>(std::llround(Live)) - WinPre, WinN);
				const TArray<double> B = Window(Table->Mono, static_cast<int64>(std::llround(Slow)) - WinPre, WinN);
				return RbAudioTestKit::GroupDelaySamples(A, B, Fs) - ((Slow - std::llround(Slow)) - (Live - std::llround(Live)));
			};
			// The tip strike sums the cue ball's and the cue's radiation from two moving voices (their mix, hence its group delay,
			// depends on where the voices stand): the click of the first ball-ball contact is the clean reference. Its shot time is
			// measured from the anchor (shot time 0 at the anchor frame of each mapping).
			const double ShiftTip = Shift(SchedTipLive, SchedTipSlow);
			const double ShiftClick = Shift(SchedClickLive, SchedClickSlow);
			const double Prop = (Click->ArrivalTime - Click->ShotTime) * Fs; // propagation: not stretched
			const double ClickLiveShot = SchedClickLive - static_cast<double>(S->LiveA.AnchorFrame - Table->First) - Prop;
			const double ClickSlowShot = SchedClickSlow + ShiftClick - static_cast<double>(S->Slow.AnchorFrame - Table->First) - Prop;
			const double Ratio = ClickSlowShot / FMath::Max(ClickLiveShot, 1.0);
			FString Gens;
			for (const RbAudioTestKit::FClockState& G : S->SlowGenerations)
			{
				Gens += FString::Printf(TEXT(" [gen %u anchor %lld origin %.4f rate %.3f]"), G.Generation, G.AnchorFrame, G.OriginShotTime, G.Rate);
			}
			AddInfo(FString::Printf(TEXT("two-ball shot x%.2f replay: first click %.1f samples of shot time after the anchor live, %.1f in the replay (ratio %.5f); the replay's click lands %+.3f samples from its film-style frame relative to live (tip strike %+.2f, two voices); replay mappings%s"),
				S->Slow.Rate, ClickLiveShot, ClickSlowShot, Ratio, ShiftClick, ShiftTip, *Gens));
			// The two windows are not the same signal: the click is the sum of TWO moving ball voices whose clicks are up to ~8 samples
			// apart (their own propagation delays), mixed with the engine's pan gains of wherever each voice stands when the click is
			// heard (live: ~2 blocks after the last game-tick position update at full ball speed, x0.25: a quarter of that motion); the
			// sum's group delay moves by a fraction of a sample with that mix (review M2-C measured +0.22 with the same code that gave
			// +0.12 before). Half a sample (10 us) bounds the comparison; AU-0 measures the scheduling itself to 0.05 samples with fixed
			// voices, and a wrong rate mapping would be off by thousands of samples (the ratio above).
			TestTrue(FString::Printf(TEXT("slow motion x 0.25: the click lands at its film-style frame (%+.3f samples, +-0.5)"), ShiftClick), FMath::Abs(ShiftClick) <= 0.5);
			TestTrue(FString::Printf(TEXT("slow motion x 0.25: impacts %.5f x farther apart (want 4 +- 0.0005)"), Ratio), FMath::Abs(Ratio - 4.0) <= 0.0005);
		}
		// Pause mid-shot: silent while held (after the 20 ms fade), no click at the hold / resume, resumes later.
		const int32 P0 = Idx(*Table, S->Pause0);
		const int32 R0 = Idx(*Table, S->Resume0);
		const double Running = RbAudioTestKit::RmsDb(Table->Mono, P0 - static_cast<int32>(0.3 * Fs), P0 - static_cast<int32>(0.05 * Fs));
		const double Held = RbAudioTestKit::RmsDb(Table->Mono, P0 + static_cast<int32>(0.4 * Fs), R0 - static_cast<int32>(0.05 * Fs));
		AddInfo(FString::Printf(TEXT("pause: table stem %.1f dBFS before, %.1f dBFS while held"), Running, Held));
		TestTrue(FString::Printf(TEXT("pause holds the table (%.1f dB below the running shot; tails ring out)"), Running - Held), Held < Running - 30.0);
		// No new impact while held: after the rendered tails (a rail bank rings < 0.3 s) the stem is silent until the resume.
		int32 OnsetsHeld = 0;
		FString HeldAt;
		for (const RbAudioTestKit::FOnset& O : Onsets)
		{
			if (O.Sample > P0 + 0.35 * Fs && O.Sample < R0)
			{
				++OnsetsHeld;
				HeldAt += FString::Printf(TEXT(" %.3f"), (O.Sample - P0) / Fs);
			}
		}
		TestEqual(FString::Printf(TEXT("no impact sounds while the shot is held (onsets at%s s after the hold)"), *HeldAt), OnsetsHeld, 0);
		// No click at the hold: the continuous layers fade over 20 ms (the largest sample step around the hold stays within the
		// running shot's own sample steps).
		double StepBefore = 0.0;
		double StepAtHold = 0.0;
		const int32 Split = P0 - static_cast<int32>(0.05 * Fs);
		for (int32 I = FMath::Max(1, P0 - static_cast<int32>(0.3 * Fs)); I < FMath::Min(Table->Mono.Num(), P0 + static_cast<int32>(0.1 * Fs)); ++I)
		{
			const double Step = FMath::Abs(Table->Mono[I] - Table->Mono[I - 1]);
			if (I < Split)
			{
				StepBefore = FMath::Max(StepBefore, Step);
			}
			else
			{
				StepAtHold = FMath::Max(StepAtHold, Step);
			}
		}
		AddInfo(FString::Printf(TEXT("pause: largest sample step %.2e while running, %.2e around the hold"), StepBefore, StepAtHold));
		TestTrue(TEXT("no click at the hold (no sample step larger than the running shot's)"), StepAtHold <= StepBefore * 1.05 + 1e-9);
		RbAudioTestKit::WriteWav(FPaths::Combine(StemDir(), TEXT("mix_table.wav")), Table->Samples, Table->Channels, Table->Rate, true);
		TestTrue(TEXT("resume re-anchors the clock"), S->BreakAfter.bAnchored && S->BreakAfter.Generation != S->BreakBefore.Generation);
		// Pause mix: master -12 dB and darker, released after.
		const int32 M0 = Idx(*Master, S->MixBefore0);
		const int32 M1 = Idx(*Master, S->MixOn0);
		const int32 M2 = Idx(*Master, S->MixOff0);
		const double Before = RbAudioTestKit::RmsDb(Master->Mono, M0, M1);
		const double During = RbAudioTestKit::RmsDb(Master->Mono, M1 + static_cast<int32>(0.5 * Fs), M2);
		const double After = RbAudioTestKit::RmsDb(Master->Mono, M2 + static_cast<int32>(0.6 * Fs), M2 + static_cast<int32>(1.4 * Fs));
		const double CBefore = RbAudioTestKit::SpectralCentroidHz(TArrayView<const double>(Master->Mono.GetData() + M0, M1 - M0), Fs);
		const double CDuring = RbAudioTestKit::SpectralCentroidHz(TArrayView<const double>(Master->Mono.GetData() + M1 + static_cast<int32>(0.5 * Fs),
			M2 - M1 - static_cast<int32>(0.5 * Fs)), Fs);
		AddInfo(FString::Printf(TEXT("pause mix: master %.1f -> %.1f -> %.1f dBFS, centroid %.0f -> %.0f Hz"), Before, During, After, CBefore, CDuring));
		TestTrue(FString::Printf(TEXT("pause mix: the world is %.1f dB quieter (-12 dB and the 800 Hz low-pass: <= -12)"), During - Before), During - Before <= -11.0);
		TestTrue(FString::Printf(TEXT("pause mix: low-passed (centroid %.0f -> %.0f Hz)"), CBefore, CDuring), CDuring < 0.7 * CBefore);
		TestTrue(FString::Printf(TEXT("pause mix released (%.1f dB from before)"), After - Before), FMath::Abs(After - Before) <= 1.0);
		RbAudioTestKit::WriteWav(FPaths::Combine(OutDir(), TEXT("mix_replay_pause_master.wav")), Master->Samples, Master->Channels, Master->Rate, false);
		RbAudioTestKit::WriteWav(FPaths::Combine(StemDir(), TEXT("mix_ambience.wav")), Amb->Samples, Amb->Channels, Amb->Rate, true);
		// The phases in capture frames (Tools/audio/m2_report.py annotates the plot).
		{
			auto M = [&](int64 Frame) { return static_cast<long long>(Frame - Master->First); };
			const FString Meta = FString::Printf(TEXT("fs=%.0f\nquiet_a=%lld\nlive_a=%lld\nquiet_b=%lld\nlive_b=%lld\nreplay=%lld\nreplay_end=%lld\npause=%lld\nresume=%lld\nmix_on=%lld\nmix_off=%lld\n"),
				Fs, M(S->QuietA0), M(S->LiveA.AnchorFrame), M(S->QuietB0), M(S->LiveB.AnchorFrame), M(S->Slow.AnchorFrame), M(S->Replay1), M(S->Pause0), M(S->Resume0),
				M(S->MixOn0), M(S->MixOff0));
			FFileHelper::SaveStringToFile(Meta, *FPaths::Combine(StemDir(), TEXT("mix_meta.txt")));
		}
		return true;
	}});
	AllowUnfocusedAudio(true);
	AutomationOpenMap(RbAssetPaths::M1TestRoomMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FScriptCommand(this, MoveTemp(Steps)));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

} // namespace RbAudioFunctional

#endif // WITH_DEV_AUTOMATION_TESTS
