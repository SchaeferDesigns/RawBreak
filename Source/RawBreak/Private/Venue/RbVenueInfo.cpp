#include "Venue/RbVenueInfo.h"

#include "RawBreak.h"
#include "Camera/RbCameraModel.h"
#include "Core/RbAssetPaths.h"
#include "Game/RbTableSubsystem.h"
#include "Table/RbTable.h"

#include "Camera/CameraActor.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/LightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "SceneViewExtension.h"

#include <atomic>

// Owner: M2-A.

// rb.Venue.GroupScale <group|all> <factor>: look-dev switch of a light group (captures: -ExecCmds="rb.Venue.GroupScale sconces 0").
static FAutoConsoleCommandWithWorldAndArgs GRbVenueGroupScale(TEXT("rb.Venue.GroupScale"),
	TEXT("rb.Venue.GroupScale <group|all> <factor>: scales every venue light of the group (look-dev; 1 = as generated)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2 || !World)
		{
			return;
		}
		for (TActorIterator<ARbVenueInfo> It(World); It; ++It)
		{
			It->SetGroupScale(FName(*Args[0]), FCString::Atof(*Args[1]));
			UE_LOG(LogRawBreak, Display, TEXT("RbVenue: group %s x %s"), *Args[0], *Args[1]);
		}
	}));

// rb.Venue.LuxProbe <x> <y> <z>: the VDB-T1 rendered white card on a core point [m] (capture_divebar.py --lux).
static FAutoConsoleCommandWithWorldAndArgs GRbVenueLuxProbe(TEXT("rb.Venue.LuxProbe"),
	TEXT("rb.Venue.LuxProbe <x> <y> <z>: shows the hidden lux rig with its white card on the core point [m] and leaves only the table lamp on"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 3 || !World)
		{
			return;
		}
		const FVector3d Core(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		for (TActorIterator<ARbVenueInfo> It(World); It; ++It)
		{
			const bool bOk = It->ShowLuxProbe(Core);
			UE_LOG(LogRawBreak, Display, TEXT("RbVenue LuxProbe: core (%.4f, %.4f, %.4f) m -> %s"), Core.X, Core.Y, Core.Z, bOk ? TEXT("shown") : TEXT("FAILED (no rig / table)"));
			// the analytic / UE-direct half of VDB-T1 into the same log (capture_divebar.py --lux merges both into lux_report.txt)
			bool bReportOk = false;
			TArray<FString> Lines;
			ARbVenueInfo::ComputeLuxReport(World, bReportOk).ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				UE_LOG(LogRawBreak, Display, TEXT("RbVenue Lux: %s"), *Line);
			}
		}
	}));

const FName ARbVenueInfo::CeilingTag(TEXT("RbDB_Ceiling"));
const FName ARbVenueInfo::FanTag(TEXT("RbDB_Fan"));
const FName ARbVenueInfo::GeometryTag(TEXT("RbDB_Geo"));
const FName ARbVenueInfo::LuxRigTag(TEXT("RbDB_LuxRig"));
const FName ARbVenueInfo::LuxCardTag(TEXT("RbDB_LuxCard"));

// Reads the adapted exposure of the main view (VDB-T2 EV report). SetupView runs on the game thread for every view.
class FRbVenueViewExtension : public FSceneViewExtensionBase
{
public:
	explicit FRbVenueViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{
		if (InView.State && !InView.bIsSceneCapture && !InView.bIsReflectionCapture && !InView.bIsPlanarReflection)
		{
			Exposure.store(InView.State->GetLastEyeAdaptationExposure());
			AverageLuminance.store(InView.State->GetLastAverageSceneLuminance());
			bCurve.store(InView.FinalPostProcessSettings.AutoExposureBiasCurve != nullptr);
		}
	}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	std::atomic<float> Exposure{0.0f};
	std::atomic<float> AverageLuminance{0.0f};
	std::atomic<bool> bCurve{false};
};

namespace RbVenueInfoPrivate
{
	// Centre-weighted metering mask (plan 4.4: the eye meters what it looks at).
	UTexture2D* MakeMeterMask(double Sigma)
	{
		constexpr int32 N = 64;
		UTexture2D* Texture = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}
		Texture->SRGB = false;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const double U = (X + 0.5) / N - 0.5, V = (Y + 0.5) / N - 0.5;
				const double W = FMath::Exp(-(U * U + V * V) / (2.0 * Sigma * Sigma));
				const uint8 C = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(255.0 * (0.15 + 0.85 * W)), 0, 255));
				Pixels[Y * N + X] = FColor(C, C, C, 255);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	bool ParseState(const FString& Text, ERbLightingState& Out)
	{
		if (Text.Equals(TEXT("Open"), ESearchCase::IgnoreCase)) { Out = ERbLightingState::Open; return true; }
		if (Text.Equals(TEXT("LightsUp"), ESearchCase::IgnoreCase)) { Out = ERbLightingState::LightsUp; return true; }
		if (Text.Equals(TEXT("AfterHours"), ESearchCase::IgnoreCase)) { Out = ERbLightingState::AfterHours; return true; }
		return false;
	}

	const TCHAR* StateName(ERbLightingState State)
	{
		switch (State)
		{
		case ERbLightingState::LightsUp: return TEXT("LightsUp");
		case ERbLightingState::AfterHours: return TEXT("AfterHours");
		default: return TEXT("Open");
		}
	}
}

ARbVenueInfo::ARbVenueInfo()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->bUnbound = true;
	RootComponent = PostProcess;
	// AInfo hides itself, and a post-process component only contributes while it "renders" (UPostProcessComponent::GetProperties:
	// bEnabled && ShouldRender(), which is false for a hidden owner in game): without this the venue grade never reached a view.
	SetHidden(false);
	Tags.Add(RbAssetPaths::Tag::VenueInfo);
}

ARbVenueInfo* ARbVenueInfo::Find(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ARbVenueInfo> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

ERbVenue ARbVenueInfo::GetVenue(const UObject* WorldContext)
{
	const ARbVenueInfo* Info = Find(WorldContext);
	return Info ? Info->Venue : ERbVenue::TestRoom;
}

void ARbVenueInfo::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyPostProcess();
}

void ARbVenueInfo::ApplyPostProcess()
{
	if (!PostProcess)
	{
		return;
	}
	// Baseline for views without a RAW BREAK camera (the cameras apply the same Eyes law themselves, plan 4.4) + the venue grade.
	const FRbCameraPresetParams Eyes = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
	FPostProcessSettings& S = PostProcess->Settings;
	S = FPostProcessSettings();
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = static_cast<float>(Eyes.MinEv100);
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = static_cast<float>(Eyes.MaxEv100);
	S.bOverride_AutoExposureSpeedUp = true;
	S.AutoExposureSpeedUp = static_cast<float>(Eyes.AdaptSpeedUp);
	S.bOverride_AutoExposureSpeedDown = true;
	S.AutoExposureSpeedDown = static_cast<float>(Eyes.AdaptSpeedDown);
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = static_cast<float>(Eyes.ExposureCompensation);
	S.bOverride_AutoExposureLowPercent = true;
	S.AutoExposureLowPercent = static_cast<float>(Eyes.HistogramLowPercent);
	S.bOverride_AutoExposureHighPercent = true;
	S.AutoExposureHighPercent = static_cast<float>(Eyes.HistogramHighPercent);
	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		if (!MeterMask)
		{
			MeterMask = RbVenueInfoPrivate::MakeMeterMask(0.30);
		}
		S.bOverride_AutoExposureMeterMask = MeterMask != nullptr;
		S.AutoExposureMeterMask = MeterMask;
	}
	// Night look: incomplete mesopic adaptation (RbVenueLighting::NightCompensationKeys); x = metered scene EV100.
	if (bNightLook)
	{
		if (!NightCurve)
		{
			NightCurve = NewObject<UCurveFloat>(this, TEXT("RbVenueNightCurve"), RF_Transient);
		}
		FRichCurve& Curve = NightCurve->FloatCurve;
		Curve.Reset();
		for (const FVector2D& Key : RbVenueLighting::NightCompensationKeys())
		{
			Curve.AddKey(static_cast<float>(RbVenueLighting::UeCurveX(Key.X)), static_cast<float>(Key.Y));
		}
		for (auto It = Curve.GetKeyHandleIterator(); It; ++It)
		{
			Curve.SetKeyInterpMode(*It, RCIM_Linear);
		}
		S.bOverride_AutoExposureBiasCurve = true;
		S.AutoExposureBiasCurve = NightCurve;
	}
	// White balance: the eye adapts toward the key light's 2700 K but a warm residue remains (a dive bar reads warm).
	S.bOverride_WhiteTemp = true;
	S.WhiteTemp = WhiteTemp;
	S.bOverride_WhiteTint = true;
	S.WhiteTint = WhiteTint;
	PostProcess->bUnbound = true;
	PostProcess->Priority = 0.0f;
	PostProcess->BlendWeight = 1.0f;
}

int32 ARbVenueInfo::BindLights()
{
	Bound.Reset();
	Bound.SetNum(Lights.Num());
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}
	TMap<FName, AActor*> ByTag;
	TMultiMap<FName, AActor*> EmissiveByTag;
	TSet<FName> EmissiveTags;
	for (const FRbVenueLight& Light : Lights)
	{
		if (!Light.EmissiveActorTag.IsNone())
		{
			EmissiveTags.Add(Light.EmissiveActorTag);
		}
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			if (Tag.ToString().StartsWith(TEXT("LT_DB_")))
			{
				ByTag.Add(Tag, *It);
			}
			if (EmissiveTags.Contains(Tag))
			{
				EmissiveByTag.Add(Tag, *It);
			}
		}
	}
	int32 Count = 0;
	for (int32 I = 0; I < Lights.Num(); ++I)
	{
		const FRbVenueLight& Light = Lights[I];
		FBoundLight& B = Bound[I];
		AActor* const* Actor = ByTag.Find(RbVenueLighting::LightTag(Light.Id));
		ULightComponent* Component = Actor ? (*Actor)->FindComponentByClass<ULightComponent>() : nullptr;
		if (!Component)
		{
			continue;
		}
		B.Component = Component;
		B.BaseColor = Component->GetLightColor();
		B.BaseRotation = Component->GetComponentRotation();
		B.Ramp.Snap(Light.FactorFor(static_cast<uint8>(LightingState)));
		if (!Light.EmissiveActorTag.IsNone())
		{
			TArray<AActor*> Emitters;
			EmissiveByTag.MultiFind(Light.EmissiveActorTag, Emitters);
			for (AActor* Emitter : Emitters)
			{
				TInlineComponentArray<UStaticMeshComponent*> Meshes(Emitter);
				for (UStaticMeshComponent* Mesh : Meshes)
				{
					for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
					{
						if (UMaterialInstanceDynamic* Mid = Mesh->CreateAndSetMaterialInstanceDynamic(Slot))
						{
							B.EmissiveMids.Add(Mid);
						}
					}
				}
			}
		}
		++Count;
	}
	return Count;
}

void ARbVenueInfo::BeginPlay()
{
	Super::BeginPlay();
	LightingState = InitialLightingState;
	FString StateArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("RbLightingState="), StateArg))
	{
		ERbLightingState Parsed;
		if (RbVenueInfoPrivate::ParseState(StateArg, Parsed))
		{
			LightingState = Parsed;
		}
	}
	ApplyPostProcess();
	const int32 Count = BindLights();
	FanActors.Reset();
	CeilingActors.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(FanTag))
		{
			FanActors.Add(*It);
		}
		if (It->ActorHasTag(CeilingTag))
		{
			CeilingActors.Add(*It);
		}
	}
	LightTime = 0.0;
	UpdateLighting(0.0f);
	FString CaptureArg;
	bEvLog = FParse::Param(FCommandLine::Get(), TEXT("RbEvLog")) || FParse::Value(FCommandLine::Get(), TEXT("RBCapture="), CaptureArg);
	if (bEvLog)
	{
		ViewExtension = FSceneViewExtensions::NewExtension<FRbVenueViewExtension>();
	}
	UE_LOG(LogRawBreak, Display, TEXT("RbVenue: %d / %d lights bound, lighting state %s"), Count, Lights.Num(), RbVenueInfoPrivate::StateName(LightingState));
}

void ARbVenueInfo::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ViewExtension.Reset();
	Super::EndPlay(EndPlayReason);
}

void ARbVenueInfo::SetLightingState(ERbLightingState NewState, float RampSeconds)
{
	if (NewState == LightingState)
	{
		return;
	}
	LightingState = NewState;
	for (int32 I = 0; I < Lights.Num() && I < Bound.Num(); ++I)
	{
		const float Seconds = FMath::Max3(RampSeconds, Lights[I].RampSeconds, RbVenueLighting::MinRampSeconds);
		Bound[I].Ramp.Start(Lights[I].FactorFor(static_cast<uint8>(NewState)), Seconds);
	}
	OnLightingStateChanged.Broadcast(NewState);
}

float ARbVenueInfo::GetLightStateFactor(FName Id) const
{
	for (int32 I = 0; I < Lights.Num() && I < Bound.Num(); ++I)
	{
		if (Lights[I].Id == Id)
		{
			return Bound[I].Ramp.Current;
		}
	}
	return -1.0f;
}

bool ARbVenueInfo::IsRamping() const
{
	for (const FBoundLight& B : Bound)
	{
		if (B.Ramp.IsActive())
		{
			return true;
		}
	}
	return false;
}

double ARbVenueInfo::FocalLengthFor(const FRbVenueCameraOptics& Optics, double SensorWidthMm, double SensorHeightMm)
{
	auto FromHorizontal = [SensorWidthMm](double HDeg) { return SensorWidthMm / (2.0 * FMath::Tan(FMath::DegreesToRadians(0.5 * HDeg))); };
	if (Optics.VerticalFovDeg > 0.0f)
	{
		return SensorHeightMm / (2.0 * FMath::Tan(FMath::DegreesToRadians(0.5 * Optics.VerticalFovDeg)));
	}
	if (Optics.HorizontalFovDeg > 0.0f)
	{
		return FromHorizontal(Optics.HorizontalFovDeg);
	}
	if (Optics.FocalLength35mm > 0.0f)
	{
		// the full-frame equivalent: the horizontal field of a 36 mm wide frame
		return FromHorizontal(FMath::RadiansToDegrees(2.0 * FMath::Atan(36.0 / (2.0 * Optics.FocalLength35mm))));
	}
	return 0.0;
}

void ARbVenueInfo::ApplyCameraOptics()
{
	if (CameraOptics.Num() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	ACineCameraActor* Camera = PC ? Cast<ACineCameraActor>(PC->GetViewTarget()) : nullptr;
	UCineCameraComponent* Component = Camera ? Camera->GetCineCameraComponent() : nullptr;
	if (!Component)
	{
		return;
	}
	for (const FRbVenueCameraOptics& Optics : CameraOptics)
	{
		if (!Camera->ActorHasTag(Optics.CameraTag))
		{
			continue;
		}
		const double Focal = FocalLengthFor(Optics, Component->Filmback.SensorWidth, Component->Filmback.SensorHeight);
		if (Focal > 0.0 && !FMath::IsNearlyEqual(Component->CurrentFocalLength, static_cast<float>(Focal), 1.0e-3f))
		{
			Component->SetCurrentFocalLength(static_cast<float>(Focal));
		}
		if (Optics.FStop > 0.0f && !FMath::IsNearlyEqual(Component->CurrentAperture, Optics.FStop, 1.0e-3f))
		{
			Component->SetCurrentAperture(Optics.FStop);
		}
		return;
	}
}

bool ARbVenueInfo::ShowLuxProbe(const FVector3d& CorePoint)
{
	UWorld* World = GetWorld();
	URbTableSubsystem* Tables = World ? URbTableSubsystem::Get(World) : nullptr;
	ARbTable* Table = Tables ? Tables->GetPlayerTable() : nullptr;
	AActor* Card = nullptr;
	TArray<AActor*> Rig;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(LuxRigTag))
			{
				Rig.Add(*It);
			}
			if (It->ActorHasTag(LuxCardTag))
			{
				Card = *It;
			}
		}
	}
	if (!Table || !Card)
	{
		return false;
	}
	// the 2 mm card lies on the surface: its centre 1 mm above the core point (the rig's children follow the root)
	const FVector Surface = Table->CoreToWorld(rb::Vec3(CorePoint.X, CorePoint.Y, CorePoint.Z));
	Card->SetActorLocation(Surface + FVector(0.0, 0.0, 0.1));
	for (AActor* A : Rig)
	{
		A->SetActorHiddenInGame(false);
	}
	// lamp only: every other group off (the ramps of 4.6 still apply; the capture warms up for seconds)
	TSet<FName> Groups;
	for (const FRbVenueLight& Light : Lights)
	{
		Groups.Add(Light.Group);
	}
	for (const FName& Group : Groups)
	{
		if (Group != LampGroup && Group != LampReflectorGroup)
		{
			SetGroupScale(Group, 0.0f);
		}
	}
	return true;
}

void ARbVenueInfo::SetGroupScale(FName Group, float Scale)
{
	GroupScales.Add(Group, FMath::Max(0.0f, Scale));
	UpdateLighting(0.0f);
}

float ARbVenueInfo::GetGroupScale(FName Group) const
{
	const float* All = GroupScales.Find(TEXT("all"));
	const float* One = GroupScales.Find(Group);
	return (All ? *All : 1.0f) * (One ? *One : 1.0f);
}

void ARbVenueInfo::ApplyLight(int32 Index, double Time)
{
	const FRbVenueLight& Light = Lights[Index];
	FBoundLight& B = Bound[Index];
	ULightComponent* Component = B.Component.Get();
	const double Factor = B.Ramp.Current * RbVenueLighting::AnimationFactor(Light, Time) * GetGroupScale(Light.Group);
	if (Component)
	{
		const bool bVisible = Factor > 1e-4;
		if (bVisible != B.bVisible || Component->IsVisible() != bVisible)
		{
			Component->SetVisibility(bVisible);
			B.bVisible = bVisible;
		}
		Component->SetIntensity(static_cast<float>(Light.Intensity * Factor));
		if (Light.Animation == ERbVenueLightAnimation::Cycle)
		{
			Component->SetLightColor(RbVenueLighting::AnimationColor(Light, Time));
		}
		else if (Light.Animation == ERbVenueLightAnimation::Headlights)
		{
			const double Yaw = RbVenueLighting::HeadlightYaw(Light, Time);
			Component->SetWorldRotation(B.BaseRotation + FRotator(0.0, Yaw, 0.0));
		}
	}
	for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Mid : B.EmissiveMids)
	{
		if (UMaterialInstanceDynamic* M = Mid.Get())
		{
			M->SetScalarParameterValue(TEXT("Emissive"), static_cast<float>(Light.EmissiveNits * B.Ramp.Current));
		}
	}
}

void ARbVenueInfo::UpdateLighting(float DeltaSeconds)
{
	LightTime += FMath::Max(0.0f, DeltaSeconds);
	for (int32 I = 0; I < Lights.Num() && I < Bound.Num(); ++I)
	{
		Bound[I].Ramp.Advance(DeltaSeconds);
		ApplyLight(I, LightTime);
	}
}

void ARbVenueInfo::UpdateCeilingVisibility()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AActor* Target = PC ? PC->GetViewTarget() : nullptr;
	const bool bHide = Target && Target->ActorHasTag(FName(*RbAssetPaths::CaptureCamera::DiveBarView(10)));
	if (bHide == bCeilingHidden)
	{
		return;
	}
	bCeilingHidden = bHide;
	for (const TWeakObjectPtr<AActor>& Actor : CeilingActors)
	{
		if (AActor* A = Actor.Get())
		{
			A->SetActorHiddenInGame(bHide);
		}
	}
}

void ARbVenueInfo::LogExposure(double Now)
{
	if (!bEvLog || !ViewExtension.IsValid() || Now - LastEvLog < 0.5)
	{
		return;
	}
	LastEvLog = Now;
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AActor* Target = PC ? PC->GetViewTarget() : nullptr;
	FString View = Target ? Target->GetName() : FString(TEXT("none"));
	if (Target)
	{
		for (const FName& Tag : Target->Tags)
		{
			if (Tag.ToString().StartsWith(TEXT("RbCam_")))
			{
				View = Tag.ToString();
			}
		}
	}
	const float Exposure = ViewExtension->Exposure.load();
	const float Luminance = ViewExtension->AverageLuminance.load();
	UE_LOG(LogRawBreak, Display, TEXT("RbVenue EV: view=%s state=%s t=%.2f exposure=%.6g ev100_camera=%.3f avg_luminance=%.5f ev100_scene=%.3f night_curve=%d"), *View,
		RbVenueInfoPrivate::StateName(LightingState), Now, Exposure, RbVenueLighting::CameraEv100(Exposure), Luminance, RbVenueLighting::SceneEv100(Luminance),
		ViewExtension->bCurve.load() ? 1 : 0);
}

void ARbVenueInfo::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLighting(DeltaSeconds);
	if (UWorld* World = GetWorld())
	{
		if (!FMath::IsNearlyZero(FanRpm))
		{
			const double DegreesPerSecond = FanRpm * 6.0;
			for (const TWeakObjectPtr<AActor>& Actor : FanActors)
			{
				if (AActor* A = Actor.Get())
				{
					A->AddActorLocalRotation(FRotator(0.0, DegreesPerSecond * DeltaSeconds, 0.0));
				}
			}
		}
		UpdateCeilingVisibility();
		ApplyCameraOptics();
		LogExposure(World->GetRealTimeSeconds());
	}
}
