// Camera rig, pawn and controller acceptance (Docs/ue-architecture.md 13, UE-5b): camera model defaults == plan 4.9; eye
// placement down on the shot (plan 4.2) vs a hand-computed eye; the ENGINE's projection (FMinimalViewInfo::
// CalculateProjectionMatrixGivenViewRectangle, MaintainYFOV) gives V = 50 deg at 16:9 and 21:9 with the filmback that
// ApplyPresetToCamera sets (also when the local player is configured for MaintainXFOV: the camera carries MaintainYFOV), f and N
// equal PupilToCineLens at the viewport aspect; exposure / shutter / sensor post-process of the presets; accommodation ease and the
// EV100 read-back of UE 5.8's eye adaptation; head motion amplitudes (plan 4.8); the rig's get-down / stand-up transition and gaze
// clamps in a game world; the focus following the gaze away from the line; the FOV slider range; the pawn's input routing through
// the stroke component; the controller's actions and input wiring (incl. the legacy Esc binding); and the functional check that the
// pawn cannot walk into the table (a ticked physics world with the UE-1 table, runtime and baked meshes).
// Owner: UE-5b.

#include "Camera/RbCameraModel.h"
#include "Camera/RbCameraRigComponent.h"
#include "Camera/RbHeadMotion.h"
#include "Core/RbCoords.h"
#include "Input/RbInputSetup.h"
#include "Math/RbCameraMath.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbPlayerController.h"
#include "Player/RbStrokeComponent.h"
#include "Table/RbTable.h"
#include "Tests/RbTestFlags.h"
#include "UI/RbOverlayComponent.h"

#include "CineCameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CheatManager.h"
#include "HAL/IConsoleManager.h"
#include "SceneView.h"
#include "Tests/AutomationCommon.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

// Everything lives in this namespace: unity builds merge test files, so no helper name may leak.
namespace RbCameraRigTests
{
	constexpr double kAspect169 = 16.0 / 9.0;
	constexpr double kAspect219 = 64.0 / 27.0;
	constexpr double kR = 0.028575;

	// The engine's own projection of a cine camera for a view rectangle (the path the game viewport takes). LocalPlayerConstraint is
	// the local player's config value the view falls back to when the camera does not carry its own constraint.
	void EngineFovs(UCineCameraComponent& Camera, int32 Width, int32 Height, double& OutVerticalDeg, double& OutHorizontalDeg,
		EAspectRatioAxisConstraint LocalPlayerConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV)
	{
		FMinimalViewInfo View;
		Camera.GetCameraView(0.0f, View);
		FSceneViewProjectionData Projection;
		const FIntRect Rect(0, 0, Width, Height);
		Projection.SetViewRectangle(Rect);
		FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, LocalPlayerConstraint, Rect, Projection);
		const FMatrix& M = Projection.ProjectionMatrix;
		OutVerticalDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan(1.0 / M.M[1][1]));
		OutHorizontalDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan(1.0 / M.M[0][0]));
	}

	UCineCameraComponent* NewCamera()
	{
		UCineCameraComponent* Camera = NewObject<UCineCameraComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Camera->AddToRoot();
		return Camera;
	}

	// A game world ticked by hand (FTestWorldWrapper) with a RAW BREAK pawn that ignores the user's settings.
	struct FPawnWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		ARbPlayerCharacter* Pawn = nullptr;

		bool Create(FAutomationTestBase& Test, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			const FTransform Spawn(Rotation, Location);
			Pawn = World->SpawnActorDeferred<ARbPlayerCharacter>(ARbPlayerCharacter::StaticClass(), Spawn, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Pawn)
			{
				Test.AddError(TEXT("could not spawn ARbPlayerCharacter"));
				return false;
			}
			Pawn->GetCameraRig()->bApplyUserSettings = false;
			Pawn->FinishSpawning(Spawn);
			Pawn->GetCameraRig()->SetViewportAspectOverride(kAspect169);
			// No controller possesses the pawn here: run the movement without one, in the default (walking / falling) mode that a
			// possession (ACharacter::Restart) would set.
			Pawn->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Pawn->GetCharacterMovement()->SetDefaultMovementMode();
			return true;
		}

		void Tick(double Dt = 1.0 / 60.0) { Wrapper.TickTestWorld(static_cast<float>(Dt)); }

		AActor* SpawnFloor()
		{
			AActor* Floor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
			UBoxComponent* Box = NewObject<UBoxComponent>(Floor, TEXT("Floor"));
			Box->SetBoxExtent(FVector(2000.0, 2000.0, 10.0));
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Floor->SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocation(FVector(0.0, 0.0, -10.0)); // top face at z = 0
			return Floor;
		}
	};

	// Table-local (actor frame) 2D footprint of every table part with collision.
	FBox TableFootprintLocal(const ARbTable& Table)
	{
		FBox Box(ForceInit);
		for (int32 Part = 0; Part < static_cast<int32>(ERbTablePart::Count); ++Part)
		{
			const UPrimitiveComponent* Component = Table.GetPartComponent(static_cast<ERbTablePart>(Part));
			if (Component && Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			{
				const FTransform ToActor = Component->GetComponentTransform().GetRelativeTransform(Table.GetActorTransform());
				Box += Component->CalcBounds(ToActor).GetBox();
			}
		}
		return Box;
	}

// --- camera model ------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraDefaults, "RawBreak.Unit.Camera.Defaults_Plan49", RB_UNIT_TEST_FLAGS)
bool FRbCameraDefaults::RunTest(const FString& Parameters)
{
	// Plan 4.9 parameter table: Eyes | Headcam.
	const FRbCameraPresetParams E = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
	const FRbCameraPresetParams H = RbCameraModel::Defaults(ERbCameraPreset::Headcam);
	TestEqual(TEXT("vertical FOV Eyes"), E.VerticalFovDeg, 50.0);
	TestTrue(TEXT("bloom: Eyes convolution, Headcam standard (plan 4.4)"), E.bConvolutionBloom && !H.bConvolutionBloom);
	TestTrue(TEXT("bloom: Eyes low, Headcam slightly higher"), E.BloomIntensity > 0.0 && E.BloomIntensity < H.BloomIntensity);
	TestEqual(TEXT("vertical FOV Headcam (H0 = 90 at 16:9)"), H.VerticalFovDeg, 58.7, 0.02);
	TestEqual(TEXT("Headcam base H0 = 90 at 16:9"), RbCameraMath::HorizontalFromVerticalFovDeg(H.VerticalFovDeg, kAspect169), 90.0, 1e-9);
	TestEqual(TEXT("k1 Eyes"), E.DistortionK1, 0.0);
	TestEqual(TEXT("k2 Eyes"), E.DistortionK2, 0.0);
	TestEqual(TEXT("k1 Headcam"), H.DistortionK1, 0.12);
	TestEqual(TEXT("k2 Headcam"), H.DistortionK2, 0.02);
	TestEqual(TEXT("overscan Eyes"), RbCameraModel::Overscan(E, kAspect169), 1.0, 1e-12);
	TestEqual(TEXT("overscan Headcam"), RbCameraModel::Overscan(H, kAspect169), 1.1926, 1e-4);
	TestEqual(TEXT("aperture Eyes [mm]"), E.ApertureDiameterMm, 4.0);
	TestEqual(TEXT("aperture Headcam [mm]"), H.ApertureDiameterMm, 1.1);
	TestEqual(TEXT("shutter Eyes"), E.ShutterAngleDeg, 108.0);
	TestEqual(TEXT("shutter Headcam"), H.ShutterAngleDeg, 180.0);
	TestEqual(TEXT("AE up Eyes"), E.AdaptSpeedUp, 1.5);
	TestEqual(TEXT("AE down Eyes"), E.AdaptSpeedDown, 0.7);
	TestEqual(TEXT("AE up Headcam"), H.AdaptSpeedUp, 3.0);
	TestEqual(TEXT("AE down Headcam"), H.AdaptSpeedDown, 2.0);
	TestEqual(TEXT("EV range Eyes min"), E.MinEv100, 2.0);
	TestEqual(TEXT("EV range Eyes max"), E.MaxEv100, 11.0);
	TestEqual(TEXT("EV range Headcam min"), H.MinEv100, 2.0);
	TestEqual(TEXT("EV range Headcam max"), H.MaxEv100, 11.0);
	TestTrue(TEXT("exposure compensation Eyes in [0, +0.5] (plan 4.4)"), E.ExposureCompensation >= 0.0 && E.ExposureCompensation <= 0.5);
	TestEqual(TEXT("exposure compensation Headcam (plan 4.4)"), H.ExposureCompensation, -0.3);
	TestEqual(TEXT("local exposure Eyes highlights"), E.LocalExposureHighlightContrast, 0.8);
	TestEqual(TEXT("local exposure Eyes shadows"), E.LocalExposureShadowContrast, 0.9);
	TestEqual(TEXT("local exposure Headcam"), H.LocalExposureHighlightContrast + H.LocalExposureShadowContrast, 2.0);
	TestEqual(TEXT("grain g0 Eyes"), E.GrainG0, 0.015);
	TestEqual(TEXT("grain max Eyes"), E.GrainMax, 0.06);
	TestEqual(TEXT("grain g0 Headcam"), H.GrainG0, 0.05);
	TestEqual(TEXT("grain max Headcam"), H.GrainMax, 0.35);
	TestEqual(TEXT("grain EV_ref (plan 4.7)"), H.GrainEvRef, 8.0);
	TestEqual(TEXT("near clip Eyes [cm]"), E.NearClipCm, 1.0);
	TestEqual(TEXT("near clip Headcam [cm]"), H.NearClipCm, 1.0);
	TestEqual(TEXT("head translation Eyes"), E.HeadTranslationScale, 0.3);
	TestEqual(TEXT("head translation Headcam"), H.HeadTranslationScale, 1.0);
	TestTrue(TEXT("Eyes gaze stabilised, Headcam not"), E.bStabiliseGaze && !H.bStabiliseGaze);
	TestEqual(TEXT("vision centre default (calibrated later)"), E.VisionCenterM, 0.0);
	TestEqual(TEXT("vignette Eyes (plan 4.7)"), E.Vignette, 0.1);
	TestTrue(TEXT("vignette Headcam 0.2-0.4"), H.Vignette >= 0.2 && H.Vignette <= 0.4);
	TestTrue(TEXT("no CA for the Eyes"), E.ChromaticAberration == 0.0 && H.ChromaticAberration >= 0.3 && H.ChromaticAberration <= 0.6);
	TestTrue(TEXT("histogram high percent < 100 (pitfall 8)"), E.HistogramHighPercent < 100.0 && H.HistogramHighPercent < 100.0);
	TestTrue(TEXT("Headcam meters more centre-weighted"), H.MeteringSigma < E.MeteringSigma);
	// Placement (plan 4.2 ranges) and the transition 0.8-1.5 s (4.8).
	TestTrue(TEXT("s_e in 0.35-0.55 m"), E.EyeBehindTipM >= 0.35 && E.EyeBehindTipM <= 0.55);
	TestTrue(TEXT("h_c in 0.06-0.18 m"), E.EyeAboveCueM >= 0.06 && E.EyeAboveCueM <= 0.18);
	TestEqual(TEXT("standing eye 1.65 m (architecture 6.1)"), E.StandingEyeHeightM, 1.65);
	TestTrue(TEXT("get-down 0.8-1.5 s"), E.GetDownSeconds >= 0.8 && E.GetDownSeconds <= 1.5);
	// The data asset starts from the same defaults and serves all three presets.
	const URbCameraModel* Model = NewObject<URbCameraModel>();
	TestEqual(TEXT("data asset Eyes"), Model->Get(ERbCameraPreset::Eyes).VerticalFovDeg, E.VerticalFovDeg);
	TestEqual(TEXT("data asset Headcam"), Model->Get(ERbCameraPreset::Headcam).ApertureDiameterMm, H.ApertureDiameterMm);
	TestEqual(TEXT("data asset Broadcast"), Model->Get(ERbCameraPreset::Broadcast).VerticalFovDeg,
		RbCameraModel::Defaults(ERbCameraPreset::Broadcast).VerticalFovDeg);
	TestTrue(TEXT("Broadcast = long lens (25-40 deg horizontal)"),
		FMath::IsWithinInclusive(RbCameraMath::HorizontalFromVerticalFovDeg(Model->Get(ERbCameraPreset::Broadcast).VerticalFovDeg, kAspect169), 25.0, 40.0));
	return true;
}

// --- placement ---------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraEyePlacement, "RawBreak.Unit.Camera.EyePlacement_Plan42", RB_UNIT_TEST_FLAGS)
bool FRbCameraEyePlacement::RunTest(const FString& Parameters)
{
	FRbCameraPresetParams P = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
	P.EyeBehindTipM = 0.45;
	P.EyeAboveCueM = 0.08;
	P.VisionCenterM = 0.02; // 2 cm right of the cue
	const FVector C(123.4, -56.7, 80.2);
	for (const double YawDeg : {0.0, 30.0, -135.0, 90.0})
	{
		for (const double ElevDeg : {0.0, 5.0, 12.0, 35.0})
		{
			// Hand computation: butt -> tip u = (cos th cos psi, cos th sin psi, -sin th) (butt raised = tip lower); the vertical-plane
			// normal n_up = (sin th cos psi, sin th sin psi, cos th); right of the cue n_side = (-sin psi, cos psi, 0) (UE: +Y = right).
			const double Psi = FMath::DegreesToRadians(YawDeg);
			const double Th = FMath::DegreesToRadians(ElevDeg);
			const FVector U(FMath::Cos(Th) * FMath::Cos(Psi), FMath::Cos(Th) * FMath::Sin(Psi), -FMath::Sin(Th));
			const FVector NUp(FMath::Sin(Th) * FMath::Cos(Psi), FMath::Sin(Th) * FMath::Sin(Psi), FMath::Cos(Th));
			const FVector NSide(-FMath::Sin(Psi), FMath::Cos(Psi), 0.0);
			const FVector Expected = C - U * 45.0 + NUp * 8.0 + NSide * 2.0;
			const FVector Eye = URbCameraRigComponent::ComputeDownOnShotEye(C, U, P);
			const FString Case = FString::Printf(TEXT("yaw %.0f elev %.0f"), YawDeg, ElevDeg);
			TestTrue(Case + TEXT(": e = P_axis(s_e) + h_c n_up + y_vc n_side"), Eye.Equals(Expected, 1e-9));
			// Geometry: s_e along the axis, h_c above it (perpendicular), y_vc to the side.
			const FVector D = Eye - C;
			TestEqual(Case + TEXT(": distance along the axis"), -FVector::DotProduct(D, U), 45.0, 1e-9);
			TestEqual(Case + TEXT(": height above the axis"), FVector::DotProduct(D, NUp), 8.0, 1e-9);
			TestEqual(Case + TEXT(": vision-centre offset"), FVector::DotProduct(D, NSide), 2.0, 1e-9);
			TestTrue(Case + TEXT(": the eye is above the cue"), Eye.Z > (C - U * 45.0).Z);
			// The view looks along the cue's yaw, level toward a point at eye height, down toward the contact point.
			const FRotator Level = URbCameraRigComponent::ComputeDownOnShotView(Eye, U, Eye + FVector(U.X, U.Y, 0.0).GetSafeNormal() * 100.0).Rotator();
			TestEqual(Case + TEXT(": view yaw = cue yaw"), FRotator::NormalizeAxis(Level.Yaw - YawDeg), 0.0, 1e-6);
			TestEqual(Case + TEXT(": level gaze = pitch 0"), Level.Pitch, 0.0, 1e-6);
			TestEqual(Case + TEXT(": no roll"), Level.Roll, 0.0, 1e-9);
			const FRotator ToContact = URbCameraRigComponent::ComputeDownOnShotView(Eye, U, C).Rotator();
			const double Along = FVector::DotProduct(C - Eye, FVector(U.X, U.Y, 0.0).GetSafeNormal());
			TestEqual(Case + TEXT(": pitch to the contact point"), ToContact.Pitch, FMath::RadiansToDegrees(FMath::Atan2(C.Z - Eye.Z, Along)), 1e-6);
		}
	}
	return true;
}

// --- projection and lens -----------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraEngineProjection, "RawBreak.Unit.Camera.EngineProjection_V50", RB_UNIT_TEST_FLAGS)
bool FRbCameraEngineProjection::RunTest(const FString& Parameters)
{
	UCineCameraComponent* Camera = NewCamera();
	struct FCase
	{
		int32 W, H;
		double HExpected; // T3
	};
	for (const FCase& Case : {FCase{1920, 1080, 79.317}, FCase{2560, 1080, 95.728}, FCase{3840, 2160, 79.317}})
	{
		const double Aspect = static_cast<double>(Case.W) / Case.H;
		const FRbCameraPresetParams Eyes = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
		URbCameraRigComponent::ApplyPresetToCamera(*Camera, Eyes, static_cast<float>(Aspect));
		const FString Name = FString::Printf(TEXT("%dx%d"), Case.W, Case.H);
		double V = 0.0;
		double H = 0.0;
		EngineFovs(*Camera, Case.W, Case.H, V, H);
		TestEqual(Name + TEXT(": engine vertical FOV"), V, 50.0, 0.01);
		TestEqual(Name + TEXT(": engine horizontal FOV (T3, Hor+)"), H, Case.HExpected, 0.01);
		TestEqual(Name + TEXT(": cine camera reports V"), static_cast<double>(Camera->GetVerticalFieldOfView()), 50.0, 0.01);
		// Filmback aspect = viewport aspect, fixed height (R-06).
		TestEqual(Name + TEXT(": filmback height"), static_cast<double>(Camera->Filmback.SensorHeight), RbCameraModel::SensorHeightMm, 1e-4);
		TestEqual(Name + TEXT(": filmback aspect"), static_cast<double>(Camera->Filmback.SensorWidth / Camera->Filmback.SensorHeight), Aspect, 1e-5);
		TestFalse(Name + TEXT(": no letterbox"), static_cast<bool>(Camera->bConstrainAspectRatio));
		// f and N equal PupilToCineLens at the viewport aspect.
		double F = 0.0;
		double N = 0.0;
		RbCameraMath::PupilToCineLens(RbCameraModel::SensorHeightMm * Aspect, RbCameraMath::HorizontalFromVerticalFovDeg(50.0, Aspect), 4.0, F, N);
		TestEqual(Name + TEXT(": focal length = PupilToCineLens"), static_cast<double>(Camera->CurrentFocalLength), F, 1e-3);
		TestEqual(Name + TEXT(": f-stop = PupilToCineLens"), static_cast<double>(Camera->CurrentAperture), N, 1e-3);
		TestEqual(Name + TEXT(": plan 4.5 focal length 21.71 mm"), static_cast<double>(Camera->CurrentFocalLength), 21.71, 0.01);
		TestEqual(Name + TEXT(": plan 4.5 f-stop 5.43"), static_cast<double>(Camera->CurrentAperture), 5.43, 0.01);
		// The DoF the engine gets: aperture and the sensor width that scales the circle of confusion.
		FMinimalViewInfo View;
		Camera->GetCameraView(0.0f, View);
		TestEqual(Name + TEXT(": DoF f-stop in the view"), static_cast<double>(View.PostProcessSettings.DepthOfFieldFstop), N, 1e-3);
		TestEqual(Name + TEXT(": DoF sensor width = filmback width"), static_cast<double>(View.PostProcessSettings.DepthOfFieldSensorWidth),
			RbCameraModel::SensorHeightMm * Aspect, 1e-3);
		TestEqual(Name + TEXT(": near clip 1 cm"), static_cast<double>(View.GetFinalPerspectiveNearClipPlane()), 1.0, 1e-6);
	}

	// A filmback that lags behind a resize (16:9 back on a 21:9 viewport) still gives V = 50: the MaintainYFOV path.
	URbCameraRigComponent::ApplyPresetToCamera(*Camera, RbCameraModel::Defaults(ERbCameraPreset::Eyes), static_cast<float>(kAspect169));
	double V = 0.0;
	double H = 0.0;
	EngineFovs(*Camera, 2560, 1080, V, H);
	TestEqual(TEXT("stale 16:9 filmback on 21:9: V"), V, 50.0, 0.01);

	// The camera carries MaintainYFOV itself: a local player configured for MaintainXFOV / MajorAxisFOV (engine default, user ini) still
	// gets the authored vertical FOV and Hor+ on 21:9.
	{
		FMinimalViewInfo View;
		Camera->GetCameraView(0.0f, View);
		TestTrue(TEXT("view carries MaintainYFOV"), View.AspectRatioAxisConstraint.IsSet() &&
			View.AspectRatioAxisConstraint.GetValue() == EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV);
	}
	for (const EAspectRatioAxisConstraint LocalPlayer : {EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV, EAspectRatioAxisConstraint::AspectRatio_MajorAxisFOV})
	{
		URbCameraRigComponent::ApplyPresetToCamera(*Camera, RbCameraModel::Defaults(ERbCameraPreset::Eyes), static_cast<float>(kAspect219));
		EngineFovs(*Camera, 2560, 1080, V, H, LocalPlayer);
		TestEqual(FString::Printf(TEXT("local player constraint %d: V at 21:9"), static_cast<int32>(LocalPlayer)), V, 50.0, 0.01);
		TestEqual(FString::Printf(TEXT("local player constraint %d: H at 21:9"), static_cast<int32>(LocalPlayer)), H, 95.728, 0.01);
		// Stale 16:9 filmback on a 21:9 viewport: without the camera's own constraint MaintainXFOV would crop V to 38.5 deg.
		URbCameraRigComponent::ApplyPresetToCamera(*Camera, RbCameraModel::Defaults(ERbCameraPreset::Eyes), static_cast<float>(kAspect169));
		EngineFovs(*Camera, 2560, 1080, V, H, LocalPlayer);
		TestEqual(FString::Printf(TEXT("local player constraint %d: stale filmback V"), static_cast<int32>(LocalPlayer)), V, 50.0, 0.01);
	}

	// Headcam: the base projection (distortion post-process is post-M1), V = 58.7 at every aspect.
	URbCameraRigComponent::ApplyPresetToCamera(*Camera, RbCameraModel::Defaults(ERbCameraPreset::Headcam), static_cast<float>(kAspect219));
	EngineFovs(*Camera, 2560, 1080, V, H);
	TestEqual(TEXT("Headcam V at 21:9"), V, 58.7156, 0.01);
	TestEqual(TEXT("Headcam aperture f / N = 1.1 mm"), static_cast<double>(Camera->CurrentFocalLength / Camera->CurrentAperture), 1.1, 1e-3);
	Camera->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraPostProcess, "RawBreak.Unit.Camera.ExposureShutterSensor", RB_UNIT_TEST_FLAGS)
bool FRbCameraPostProcess::RunTest(const FString& Parameters)
{
	UCineCameraComponent* Camera = NewCamera();
	for (const ERbCameraPreset Preset : {ERbCameraPreset::Eyes, ERbCameraPreset::Headcam})
	{
		const FRbCameraPresetParams P = RbCameraModel::Defaults(Preset);
		URbCameraRigComponent::ApplyPresetToCamera(*Camera, P, static_cast<float>(kAspect169));
		const FPostProcessSettings& PP = Camera->PostProcessSettings;
		const FString Name = Preset == ERbCameraPreset::Eyes ? TEXT("Eyes") : TEXT("Headcam");
		TestTrue(Name + TEXT(": histogram auto exposure"), PP.bOverride_AutoExposureMethod && PP.AutoExposureMethod == EAutoExposureMethod::AEM_Histogram);
		TestEqual(Name + TEXT(": min EV100"), static_cast<double>(PP.AutoExposureMinBrightness), P.MinEv100, 1e-6);
		TestEqual(Name + TEXT(": max EV100"), static_cast<double>(PP.AutoExposureMaxBrightness), P.MaxEv100, 1e-6);
		TestEqual(Name + TEXT(": speed up"), static_cast<double>(PP.AutoExposureSpeedUp), P.AdaptSpeedUp, 1e-6);
		TestEqual(Name + TEXT(": speed down"), static_cast<double>(PP.AutoExposureSpeedDown), P.AdaptSpeedDown, 1e-6);
		TestEqual(Name + TEXT(": compensation"), static_cast<double>(PP.AutoExposureBias), P.ExposureCompensation, 1e-6);
		TestTrue(Name + TEXT(": the pupil is no exposure control"), PP.bOverride_AutoExposureApplyPhysicalCameraExposure && !PP.AutoExposureApplyPhysicalCameraExposure);
		TestTrue(Name + TEXT(": centre-weighted metering mask"), PP.bOverride_AutoExposureMeterMask && PP.AutoExposureMeterMask != nullptr);
		TestEqual(Name + TEXT(": histogram high percent"), static_cast<double>(PP.AutoExposureHighPercent), P.HistogramHighPercent, 1e-6);
		TestEqual(Name + TEXT(": local exposure highlights"), static_cast<double>(PP.LocalExposureHighlightContrastScale), P.LocalExposureHighlightContrast, 1e-6);
		TestEqual(Name + TEXT(": motion blur = shutter / 360"), static_cast<double>(PP.MotionBlurAmount), P.ShutterAngleDeg / 360.0, 1e-6);
		TestEqual(Name + TEXT(": motion blur max 5 %"), static_cast<double>(PP.MotionBlurMax), 5.0, 1e-6);
		TestEqual(Name + TEXT(": base grain g0"), static_cast<double>(PP.FilmGrainIntensity), P.GrainG0, 1e-6);
		TestTrue(Name + TEXT(": more grain in the shadows"), PP.FilmGrainIntensityShadows > PP.FilmGrainIntensityMidtones && PP.FilmGrainIntensityMidtones > PP.FilmGrainIntensityHighlights);
		TestEqual(Name + TEXT(": vignette"), static_cast<double>(PP.VignetteIntensity), P.Vignette, 1e-6);
		TestEqual(Name + TEXT(": CA"), static_cast<double>(PP.SceneFringeIntensity), P.ChromaticAberration, 1e-6);
		// Plan 4.4 bloom row: Eyes convolution bloom at low intensity (lamp glare), Headcam standard and slightly higher.
		TestTrue(Name + TEXT(": bloom method"), PP.bOverride_BloomMethod &&
			PP.BloomMethod == (Preset == ERbCameraPreset::Eyes ? EBloomMethod::BM_FFT : EBloomMethod::BM_SOG));
		TestEqual(Name + TEXT(": bloom intensity"), static_cast<double>(PP.BloomIntensity), P.BloomIntensity, 1e-6);
		TestTrue(Name + TEXT(": manual focus, no engine smoothing"), Camera->FocusSettings.FocusMethod == ECameraFocusMethod::Manual && !Camera->FocusSettings.bSmoothFocusChanges);
	}
	Camera->RemoveFromRoot();

	// The metering mask: bright centre, dim periphery, symmetric.
	UTexture2D* Mask = URbCameraRigComponent::GetMeteringMask(0.35);
	if (TestNotNull(TEXT("mask texture"), Mask) && Mask->GetPlatformData() && Mask->GetPlatformData()->Mips.Num() > 0)
	{
		TestTrue(TEXT("mask is linear"), !Mask->SRGB);
		FTexture2DMipMap& Mip = Mask->GetPlatformData()->Mips[0];
		const FColor* Pixels = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
		if (TestNotNull(TEXT("mask pixels"), Pixels))
		{
			const int32 N = Mip.SizeX;
			const FColor Centre = Pixels[(N / 2) * N + N / 2];
			const FColor Corner = Pixels[0];
			TestTrue(TEXT("centre weight ~1"), Centre.R >= 250);
			TestTrue(TEXT("corner weight = the 5 % floor"), Corner.R >= 12 && Corner.R <= 15);
			TestEqual(TEXT("mirror symmetric"), Pixels[3 * N + 5].R, Pixels[3 * N + (N - 1 - 5)].R);
		}
		Mip.BulkData.Unlock();
	}
	TestTrue(TEXT("mask cached per sigma"), URbCameraRigComponent::GetMeteringMask(0.35) == Mask);
	return true;
}

// --- optics laws -------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraFocusEase, "RawBreak.Unit.Camera.FocusEase_Accommodation", RB_UNIT_TEST_FLAGS)
bool FRbCameraFocusEase::RunTest(const FString& Parameters)
{
	// Accommodation from 40 cm (the cue) to 2 m (the object ball), tau = 0.2 s: exponential in dioptres, independent of the frame
	// split for a constant target.
	const double Tau = 0.2;
	const double Target = 200.0;
	const auto Run = [&](double Fps, double Seconds) {
		double Focus = 40.0;
		const int32 Steps = FMath::RoundToInt32(Fps * Seconds);
		for (int32 I = 0; I < Steps; ++I)
		{
			Focus = URbCameraRigComponent::EaseFocusCm(Focus, Target, 1.0 / Fps, Tau);
		}
		return Focus;
	};
	const auto Exact = [&](double Seconds) { return 1.0 / (1.0 / Target + (1.0 / 40.0 - 1.0 / Target) * FMath::Exp(-Seconds / Tau)); };
	TestEqual(TEXT("30 fps after 0.5 s"), Run(30.0, 0.5), Exact(0.5), 1e-9);
	TestEqual(TEXT("60 fps after 0.5 s"), Run(60.0, 0.5), Exact(0.5), 1e-9);
	TestEqual(TEXT("144 fps after 0.5 s"), Run(144.0, 0.5), Exact(0.5), 1e-9);
	TestEqual(TEXT("after tau: 63 % of the dioptre step"), Run(60.0, 0.2), Exact(0.2), 1e-9);
	TestEqual(TEXT("converged after 2 s"), Run(60.0, 2.0), Target, 0.05);
	TestTrue(TEXT("dioptre ease: the far target is approached from below"), Run(60.0, 0.1) < Target && Run(60.0, 0.1) > 40.0);
	TestEqual(TEXT("first value snaps"), URbCameraRigComponent::EaseFocusCm(0.0, 150.0, 0.016, Tau), 150.0);
	TestEqual(TEXT("clamped to the minimum focus distance"), URbCameraRigComponent::EaseFocusCm(0.0, 1.0, 0.016, Tau), 5.0);

	// EV100 of UE 5.8's exposure multiplier (PostProcessEyeAdaptation.usf, extended luminance range): the white point is
	// L_white = LuminanceMax 2^EV100 with LuminanceMax = 0.78 / r.EyeAdaptation.LensAttenuation, exposure = 2^bias / L_white.
	// The project runs the extended range (the rig's Min / Max brightness are EV100 only then) at the 5.8 default attenuation 0.78:
	// LuminanceMax = 1 (review: the old 1.2 of the pre-5.x attenuation 0.65 read every EV 0.26 too low and skewed the grain law).
	const IConsoleVariable* Extended = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
	const IConsoleVariable* LensAttenuation = IConsoleManager::Get().FindConsoleVariable(TEXT("r.EyeAdaptation.LensAttenuation"));
	if (TestNotNull(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"), Extended) && TestNotNull(TEXT("r.EyeAdaptation.LensAttenuation"), LensAttenuation))
	{
		TestEqual(TEXT("extended luminance range on (DefaultEngine.ini): Min / Max brightness are EV100"), Extended->GetInt(), 1);
		TestEqual(TEXT("5.8 default lens attenuation"), static_cast<double>(LensAttenuation->GetFloat()), 0.78, 1e-6);
		TestEqual(TEXT("LuminanceMax = 0.78 / q"), URbCameraRigComponent::EyeAdaptationLuminanceMax(), 0.78 / static_cast<double>(LensAttenuation->GetFloat()), 1e-9);
	}
	const double LMax = URbCameraRigComponent::EyeAdaptationLuminanceMax();
	TestEqual(TEXT("LuminanceMax of the project config"), LMax, 1.0, 1e-6);
	for (const double Ev : {2.0, 7.634, 11.0})
	{
		for (const double Bias : {0.0, 0.25, -0.3})
		{
			const double Exposure = FMath::Pow(2.0, Bias) / (LMax * FMath::Pow(2.0, Ev));
			TestEqual(FString::Printf(TEXT("EV %.3f bias %.2f round trip"), Ev, Bias), URbCameraRigComponent::Ev100FromExposure(Exposure, Bias), Ev, 1e-6);
			// The pre-5.x calibration (q = 0.65 -> LuminanceMax 1.2) through the explicit overload.
			const double OldExposure = FMath::Pow(2.0, Bias) / (1.2 * FMath::Pow(2.0, Ev));
			TestEqual(FString::Printf(TEXT("EV %.3f bias %.2f, LuminanceMax 1.2"), Ev, Bias), URbCameraRigComponent::Ev100FromExposure(OldExposure, Bias, 1.2), Ev, 1e-9);
		}
	}
	// A frame exposed for the plan's lit cloth (EV 7.634, plan 4.4 / T6) with the Eyes bias reads back as EV 7.634, not 7.37.
	TestEqual(TEXT("cloth EV read back"), URbCameraRigComponent::Ev100FromExposure(FMath::Pow(2.0, 0.25) / FMath::Pow(2.0, 7.634), 0.25), 7.634, 1e-6);
	TestTrue(TEXT("no exposure = unknown"), URbCameraRigComponent::Ev100FromExposure(0.0, 0.0) < -50.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraHeadMotion, "RawBreak.Unit.Camera.HeadMotion_Plan48", RB_UNIT_TEST_FLAGS)
bool FRbCameraHeadMotion::RunTest(const FString& Parameters)
{
	const FRbCameraPresetParams Eyes = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
	const FRbCameraPresetParams Head = RbCameraModel::Defaults(ERbCameraPreset::Headcam);
	const FRbHeadMotionParams& HP = Eyes.HeadMotion;
	TestEqual(TEXT("bob 3 cm p-p at 0.8 m/s"), FRbHeadMotion::BobPeakToPeakCm(0.8, HP), 3.0, 1e-9);
	TestEqual(TEXT("bob 4.5 cm p-p at 1.4 m/s"), FRbHeadMotion::BobPeakToPeakCm(1.4, HP), 4.5, 1e-9);
	TestEqual(TEXT("no bob at rest"), FRbHeadMotion::BobPeakToPeakCm(0.0, HP), 0.0);

	// Peak-to-peak of each offset axis over Seconds at 120 Hz.
	struct FRange
	{
		FVector Min = FVector(UE_BIG_NUMBER);
		FVector Max = FVector(-UE_BIG_NUMBER);
		double MaxRot = 0.0;
		FVector PP() const { return Max - Min; }
	};
	const auto Sample = [](const FRbCameraPresetParams& P, double Speed, bool bDown, double Settle, double Scale, double Seconds) {
		FRbHeadMotion Motion;
		FRange R;
		const int32 Steps = FMath::RoundToInt32(Seconds * 120.0);
		for (int32 I = 0; I < Steps; ++I)
		{
			const FRbHeadMotionSample S = Motion.Step(1.0 / 120.0, Speed, bDown, Settle, P, Scale);
			R.Min = R.Min.ComponentMin(S.Offset);
			R.Max = R.Max.ComponentMax(S.Offset);
			R.MaxRot = FMath::Max(R.MaxRot, FMath::Max3(FMath::Abs(S.Rotation.Pitch), FMath::Abs(S.Rotation.Yaw), FMath::Abs(S.Rotation.Roll)));
		}
		return R;
	};

	// Headcam (full translation): walking at 1.4 m/s bobs ~4.5 cm p-p vertically (+ breathing), sways ~2.5 cm laterally.
	const FRange HeadWalk = Sample(Head, 1.4, false, 0.0, 1.0, 20.0);
	TestTrue(FString::Printf(TEXT("Headcam walking bob p-p %.2f cm in [4.3, 5.2]"), HeadWalk.PP().Z), HeadWalk.PP().Z >= 4.3 && HeadWalk.PP().Z <= 5.2);
	TestTrue(FString::Printf(TEXT("Headcam walking sway p-p %.2f cm in [2.3, 3.8]"), HeadWalk.PP().Y), HeadWalk.PP().Y >= 2.3 && HeadWalk.PP().Y <= 3.8);
	TestTrue(TEXT("Headcam nods / rolls (no VOR)"), HeadWalk.MaxRot > 0.1);
	// Eyes: translation x0.3, rotation stabilised (no rotation from the layer).
	const FRange EyesWalk = Sample(Eyes, 1.4, false, 0.0, 1.0, 20.0);
	TestEqual(TEXT("Eyes bob = 0.3 x Headcam"), EyesWalk.PP().Z, 0.3 * HeadWalk.PP().Z, 0.05);
	TestEqual(TEXT("Eyes: no layer rotation"), EyesWalk.MaxRot, 0.0);
	// Standing still: breathing + postural sway only, millimetres.
	const FRange HeadStill = Sample(Head, 0.0, false, 0.0, 1.0, 30.0);
	TestTrue(FString::Printf(TEXT("standing breathing p-p %.3f cm ~ 2 x 2.5 mm"), HeadStill.PP().Z), HeadStill.PP().Z >= 0.4 && HeadStill.PP().Z <= 0.55);
	TestTrue(FString::Printf(TEXT("standing sway p-p %.3f cm <= 2 x 4.5 mm"), HeadStill.PP().X), HeadStill.PP().X > 0.1 && HeadStill.PP().X <= 1.1);
	// Down on the shot: smaller breathing (1.5 mm) and sway (1 mm); no walking even with a speed.
	const FRange HeadDown = Sample(Head, 1.4, true, 0.0, 1.0, 30.0);
	TestTrue(FString::Printf(TEXT("down breathing p-p %.3f cm ~ 2 x 1.5 mm"), HeadDown.PP().Z), HeadDown.PP().Z >= 0.25 && HeadDown.PP().Z <= 0.33);
	TestTrue(TEXT("down sway smaller than standing"), HeadDown.PP().X < 0.5 * HeadStill.PP().X);
	// Settled: -70 %.
	const FRange HeadSettled = Sample(Head, 0.0, true, 1.0, 1.0, 30.0);
	TestEqual(TEXT("settle: breathing -70 %"), HeadSettled.PP().Z, 0.3 * HeadDown.PP().Z, 0.01);
	// Comfort off: nothing.
	const FRange Off = Sample(Head, 1.4, false, 0.0, 0.0, 5.0);
	TestTrue(TEXT("reduced motion: no offset, no rotation"), Off.PP().IsNearlyZero(1e-12) && Off.MaxRot == 0.0);
	// A speed change never makes the bob jump (integrated step phase).
	FRbHeadMotion Motion;
	double Last = 0.0;
	double MaxJump = 0.0;
	for (int32 I = 0; I < 600; ++I)
	{
		const double Speed = I < 300 ? 0.8 : 1.4;
		const double Z = Motion.Step(1.0 / 120.0, Speed, false, 0.0, Head, 1.0).Offset.Z;
		if (I > 0)
		{
			MaxJump = FMath::Max(MaxJump, FMath::Abs(Z - Last));
		}
		Last = Z;
	}
	TestTrue(FString::Printf(TEXT("continuous across a speed change (max step %.3f cm)"), MaxJump), MaxJump < 0.35);
	return true;
}

// --- rig in a game world -----------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraRigTransition, "RawBreak.Unit.Camera.RigTransition", RB_UNIT_TEST_FLAGS)
bool FRbCameraRigTransition::RunTest(const FString& Parameters)
{
	FPawnWorld W;
	if (!W.Create(*this, FVector(-200.0, 30.0, 88.0 + 5.0), FRotator(0.0, 20.0, 0.0)))
	{
		return false;
	}
	ARbPlayerCharacter* Pawn = W.Pawn;
	URbCameraRigComponent* Rig = Pawn->GetCameraRig();
	UCineCameraComponent* Cam = Pawn->GetCamera();
	Rig->SetComfort(0.0, 1.0, 1.0, true); // no head motion: the camera = the base pose
	Rig->TickRig(0.0);
	const FRbCameraPresetParams& P = Rig->GetParams();

	// Standing: the eye 1.65 m above the capsule bottom, looking along the pawn's view rotation.
	const double Floor = Pawn->GetActorLocation().Z - Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TestEqual(TEXT("standing eye height"), Cam->GetComponentLocation().Z - Floor, 165.0, 1e-6);
	TestTrue(TEXT("standing eye over the capsule"), FVector::Dist2D(Cam->GetComponentLocation(), Pawn->GetActorLocation()) < 1e-6);
	TestEqual(TEXT("standing view yaw"), Cam->GetComponentRotation().Yaw, 20.0, 1e-4);
	TestFalse(TEXT("the camera ignores the pawn control rotation"), static_cast<bool>(Cam->bUsePawnControlRotation));

	// Get down on a shot: cue axis 1 m ahead, azimuth 10 deg (UE yaw), 4 deg butt elevation.
	const FVector Contact(-100.0, 40.0, 76.5 + 2.8575);
	const double Th = FMath::DegreesToRadians(4.0);
	const double Psi = FMath::DegreesToRadians(10.0);
	const FVector U(FMath::Cos(Th) * FMath::Cos(Psi), FMath::Cos(Th) * FMath::Sin(Psi), -FMath::Sin(Th));
	const FVector StandEye = Cam->GetComponentLocation();
	Rig->SetCueAxisWorld(Contact, U);
	Rig->SetFocusTargetWorld(Contact + FVector(U.X, U.Y, 0.0).GetSafeNormal() * 150.0); // object ball 1.5 m down the line
	Rig->SetMode(ERbCameraRigMode::DownOnShot);
	TestEqual(TEXT("transition starts"), Rig->GetTransitionAlpha(), 0.0);
	const FVector DownEye = URbCameraRigComponent::ComputeDownOnShotEye(Contact, U, P);
	const double Half = 0.5 * P.GetDownSeconds;
	Rig->TickRig(Half);
	const FVector Mid = Cam->GetComponentLocation();
	TestEqual(TEXT("half-way (smoothstep 0.5)"), FVector::Dist(Mid, FMath::Lerp(StandEye, DownEye, 0.5)), 0.0, 1e-3);
	// Frame split: many small steps reach the same end pose.
	for (int32 I = 0; I < 30; ++I)
	{
		Rig->TickRig(Half / 30.0);
	}
	TestEqual(TEXT("transition done"), Rig->GetTransitionAlpha(), 1.0, 1e-9);
	TestTrue(TEXT("down eye = e(C, u)"), Cam->GetComponentLocation().Equals(DownEye, 1e-3));
	const FRotator DownView = Cam->GetComponentRotation();
	TestEqual(TEXT("down view yaw = cue yaw"), FRotator::NormalizeAxis(DownView.Yaw - 10.0), 0.0, 1e-3);
	const FVector Gaze = Contact + FVector(U.X, U.Y, 0.0).GetSafeNormal() * 150.0;
	TestEqual(TEXT("down view pitch toward the object ball"), DownView.Pitch,
		URbCameraRigComponent::ComputeDownOnShotView(DownEye, U, Gaze).Rotator().Pitch, 1e-3);
	// Focus eases toward the object ball (thin-lens plane distance).
	for (int32 I = 0; I < 120; ++I)
	{
		Rig->TickRig(1.0 / 60.0);
	}
	const double FocusExpected = FVector::DotProduct(Gaze - DownEye, Cam->GetForwardVector());
	TestEqual(TEXT("focus on the object ball after 2 s"), Rig->GetFocusDistanceCm(), FocusExpected, 0.5);
	TestEqual(TEXT("the lens gets the focus distance"), static_cast<double>(Cam->FocusSettings.ManualFocusDistance), Rig->GetFocusDistanceCm(), 1e-2);

	// Tremor on the axis (10 Hz, 0.2 mm) barely moves the eye: the head follows the aim, not the hand.
	double MaxEyeDev = 0.0;
	for (int32 I = 0; I < 120; ++I)
	{
		const double T = I / 120.0;
		const FVector Wobble = FVector(0.0, 0.02, 0.0) * FMath::Sin(UE_DOUBLE_TWO_PI * 10.0 * T);
		Rig->SetCueAxisWorld(Contact + Wobble, U);
		Rig->TickRig(1.0 / 120.0);
		MaxEyeDev = FMath::Max(MaxEyeDev, FVector::Dist(Cam->GetComponentLocation(), DownEye));
	}
	TestTrue(FString::Printf(TEXT("tremor attenuated at the eye (%.4f cm of 0.02 cm)"), MaxEyeDev), MaxEyeDev < 0.01);
	Rig->SetCueAxisWorld(Contact, U);

	// Gaze input: clamped, and the cue ball stays in the frame without it.
	Rig->AddGazeInput(0.0, 100.0);
	TestEqual(TEXT("gaze pitch clamp"), Rig->GetGazePitch(), 35.0);
	Rig->AddGazeInput(-500.0, -100.0);
	TestEqual(TEXT("gaze yaw clamp"), Rig->GetGazeYaw(), -70.0);

	// Stand up: back to the standing eye over the capsule.
	Rig->SetMode(ERbCameraRigMode::Standing);
	Rig->TickRig(P.GetDownSeconds + 0.01);
	TestTrue(TEXT("standing again"), FVector::Dist(Cam->GetComponentLocation(), StandEye) < 1e-3);

	// A new get-down resets the gaze offsets.
	Rig->SetMode(ERbCameraRigMode::DownOnShot);
	TestEqual(TEXT("gaze reset by a new address"), Rig->GetGazeYaw() + Rig->GetGazePitch(), 0.0);

	// The cue ball stays in the frame even with an object ball far up the line and a steep cue.
	const FVector USteep = FVector(FMath::Cos(FMath::DegreesToRadians(40.0)), 0.0, -FMath::Sin(FMath::DegreesToRadians(40.0)));
	Rig->SetCueAxisWorld(Contact, USteep);
	Rig->SetFocusTargetWorld(Contact + FVector(250.0, 0.0, 0.0));
	Rig->TickRig(P.GetDownSeconds + 0.01);
	const FVector SteepEye = Cam->GetComponentLocation();
	const double BallPitch = URbCameraRigComponent::ComputeDownOnShotView(SteepEye, USteep, Contact).Rotator().Pitch;
	TestTrue(TEXT("cue ball inside the vertical FOV"), BallPitch >= Cam->GetComponentRotation().Pitch - 0.5 * P.VerticalFovDeg);

	// Head motion on: the Eyes keep the fixation point (VOR) - the rotation compensates the translation.
	Rig->SetComfort(1.0, 1.0, 1.0, true);
	for (int32 I = 0; I < 90; ++I)
	{
		Rig->TickRig(1.0 / 60.0);
	}
	const FTransform Base = Rig->GetBaseEyeTransform();
	const FVector Fixation = Base.GetLocation() + Base.GetRotation().GetForwardVector() * FMath::Clamp(Rig->GetFocusDistanceCm(), 30.0, 2000.0);
	const FVector ToFix = (Fixation - Cam->GetComponentLocation()).GetSafeNormal();
	TestTrue(TEXT("gaze stabilised on the fixation point"), FVector::DotProduct(ToFix, Cam->GetForwardVector()) > 1.0 - 1e-7);
	TestTrue(TEXT("the head moves a little"), FVector::Dist(Cam->GetComponentLocation(), Base.GetLocation()) > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraFocusFollowsGaze, "RawBreak.Unit.Camera.FocusFollowsGaze", RB_UNIT_TEST_FLAGS)
bool FRbCameraFocusFollowsGaze::RunTest(const FString& Parameters)
{
	// Down on the shot the eyes rest on the object ball (focus plane through it); when the gaze input turns them away (watching the
	// balls after the shot, raising / lowering the eyes along the line) they focus on what they look at. Review finding: the focus
	// stayed on the fixation plane, whose distance along a head turned 60 deg is halved - the whole scene went soft while watching.
	FPawnWorld W;
	if (!W.Create(*this, FVector(-200.0, 30.0, 88.0 + 5.0)))
	{
		return false;
	}
	W.SpawnFloor(); // top face at z = 0
	// A wall parallel to X, 150 cm to the right (+Y) of the eye, long and tall enough for the turned view ray.
	const FVector Contact(-100.0, 40.0, 80.0);
	const double WallY = Contact.Y + 150.0;
	AActor* Wall = W.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall, TEXT("Wall"));
	WallBox->SetBoxExtent(FVector(600.0, 10.0, 300.0));
	WallBox->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Wall->SetRootComponent(WallBox);
	WallBox->RegisterComponent();
	WallBox->SetWorldLocation(FVector(Contact.X, WallY + 10.0, 0.0));
	for (int32 I = 0; I < 3; ++I)
	{
		W.Tick(); // physics scene up to date for the traces
	}

	URbCameraRigComponent* Rig = W.Pawn->GetCameraRig();
	UCineCameraComponent* Cam = W.Pawn->GetCamera();
	Rig->SetComfort(0.0, 1.0, 1.0, true); // no head motion: the camera = the base pose
	const FRbCameraPresetParams& P = Rig->GetParams();
	const double Th = FMath::DegreesToRadians(4.0);
	const FVector U(FMath::Cos(Th), 0.0, -FMath::Sin(Th)); // cue along +X, butt raised 4 deg
	const FVector ObjectBall = Contact + FVector(150.0, 0.0, 0.0);
	Rig->SetCueAxisWorld(Contact, U);
	Rig->SetFocusTargetWorld(ObjectBall);
	Rig->SetMode(ERbCameraRigMode::DownOnShot);
	Rig->TickRig(P.GetDownSeconds + 0.01);
	const auto Settle = [Rig]() {
		for (int32 I = 0; I < 120; ++I) // 2 s = 10 accommodation time constants
		{
			Rig->TickRig(1.0 / 60.0);
		}
	};
	Settle();

	// 1. On the line: the focus plane through the object ball.
	FVector Eye = Cam->GetComponentLocation();
	FVector Fwd = Cam->GetForwardVector();
	TestEqual(TEXT("on the line: focus on the object ball"), Rig->GetFocusDistanceCm(), FVector::DotProduct(ObjectBall - Eye, Fwd), 0.5);

	// 2. Head turned 60 deg to the right (after the shot): the focus is the wall the eyes look at, not the collapsed fixation plane.
	Rig->AddGazeInput(60.0, 0.0);
	Settle();
	Eye = Cam->GetComponentLocation();
	Fwd = Cam->GetForwardVector();
	if (TestTrue(TEXT("the view turned toward the wall"), Fwd.Y > 0.8))
	{
		const double ToWall = (WallY - Eye.Y) / Fwd.Y;
		const double OldPlane = FVector::DotProduct(ObjectBall - Eye, Fwd);
		AddInfo(FString::Printf(TEXT("turned 60 deg: focus %.1f cm, wall %.1f cm, fixation plane %.1f cm"), Rig->GetFocusDistanceCm(), ToWall, OldPlane));
		TestEqual(TEXT("turned: focus on the wall"), Rig->GetFocusDistanceCm(), ToWall, 0.5);
		TestTrue(TEXT("the fixation plane would have been far off"), FMath::Abs(OldPlane - ToWall) > 50.0);
		TestEqual(TEXT("the lens gets it"), static_cast<double>(Cam->FocusSettings.ManualFocusDistance), Rig->GetFocusDistanceCm(), 1e-2);
	}

	// 3. A gaze offset within 1.5 deg still rests on the object ball.
	Rig->AddGazeInput(-60.0, 1.0);
	Settle();
	Eye = Cam->GetComponentLocation();
	Fwd = Cam->GetForwardVector();
	TestEqual(TEXT("small gaze offset: focus on the object ball"), Rig->GetFocusDistanceCm(), FVector::DotProduct(ObjectBall - Eye, Fwd), 0.5);

	// 4. Eyes lowered along the line (look input while aiming): the focus follows the view ray down to the bed (here the floor).
	Rig->AddGazeInput(0.0, -9.0);
	Settle();
	Eye = Cam->GetComponentLocation();
	Fwd = Cam->GetForwardVector();
	if (TestTrue(TEXT("looking down"), Fwd.Z < -0.05))
	{
		TestEqual(TEXT("eyes lowered: focus where the view ray meets the floor"), Rig->GetFocusDistanceCm(), -Eye.Z / Fwd.Z, 0.5);
	}

	// 5. A new get-down resets the gaze: back on the object ball.
	Rig->SetMode(ERbCameraRigMode::Standing);
	Rig->TickRig(P.GetDownSeconds + 0.01);
	Rig->SetMode(ERbCameraRigMode::DownOnShot);
	Rig->TickRig(P.GetDownSeconds + 0.01);
	Settle();
	Eye = Cam->GetComponentLocation();
	Fwd = Cam->GetForwardVector();
	TestEqual(TEXT("new address: focus on the object ball"), Rig->GetFocusDistanceCm(), FVector::DotProduct(ObjectBall - Eye, Fwd), 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbCameraFovOverride, "RawBreak.Unit.Camera.FovOverride_Range", RB_UNIT_TEST_FLAGS)
bool FRbCameraFovOverride::RunTest(const FString& Parameters)
{
	// Settings slider (plan 4.9 range 40-75 deg), Eyes only; <= 0 = the preset's own.
	URbCameraRigComponent* Rig = NewObject<URbCameraRigComponent>();
	Rig->bApplyUserSettings = false;
	TestEqual(TEXT("default Eyes V"), Rig->GetEffectiveParams().VerticalFovDeg, 50.0);
	Rig->SetVerticalFovOverride(60.0);
	TestEqual(TEXT("60 deg"), Rig->GetEffectiveParams().VerticalFovDeg, 60.0);
	Rig->SetVerticalFovOverride(90.0);
	TestEqual(TEXT("clamped to 75 deg"), Rig->GetEffectiveParams().VerticalFovDeg, 75.0);
	Rig->SetVerticalFovOverride(20.0);
	TestEqual(TEXT("clamped to 40 deg"), Rig->GetEffectiveParams().VerticalFovDeg, 40.0);
	Rig->SetVerticalFovOverride(0.0);
	TestEqual(TEXT("0 = the preset's"), Rig->GetEffectiveParams().VerticalFovDeg, 50.0);
	Rig->SetVerticalFovOverride(60.0);
	Rig->SetPreset(ERbCameraPreset::Headcam);
	TestEqual(TEXT("the Headcam keeps its lens"), Rig->GetEffectiveParams().VerticalFovDeg, RbCameraModel::Defaults(ERbCameraPreset::Headcam).VerticalFovDeg);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlayerInputRouting, "RawBreak.Unit.Player.InputRouting", RB_UNIT_TEST_FLAGS)
bool FRbPlayerInputRouting::RunTest(const FString& Parameters)
{
	FPawnWorld W;
	if (!W.Create(*this, FVector(-100.0, 0.0, 88.0 + 5.0)))
	{
		return false;
	}
	ARbPlayerCharacter* Pawn = W.Pawn;
	URbStrokeComponent* Stroke = Pawn->GetStroke();
	URbCameraRigComponent* Rig = Pawn->GetCameraRig();
	double Clock = 1000.0;
	Stroke->ClockOverride = [&Clock]() { return Clock; };
	TestEqual(TEXT("the stroke's get-down lasts as long as the eye transition"), Stroke->GetDownSeconds, Rig->GetParams().GetDownSeconds);

	// The director's BeginAddress: the cue ball 40 cm ahead of the pawn (no table: FRbCoords at the origin).
	const rb::Vec3 CueBall(-0.6, 0.0, kR);
	Stroke->BeginAddress(CueBall, kR);
	TestEqual(TEXT("walking"), static_cast<int32>(Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::Walking));
	TestTrue(TEXT("can walk"), Pawn->CanWalk());

	// Get down: the stroke component switches the rig, the pawn stops walking.
	Stroke->RequestGetDownToggle();
	TestEqual(TEXT("getting down"), static_cast<int32>(Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::GettingDown));
	TestEqual(TEXT("rig down on the shot"), static_cast<int32>(Rig->GetMode()), static_cast<int32>(ERbCameraRigMode::DownOnShot));
	TestFalse(TEXT("no walking while getting down"), Pawn->CanWalk());
	Clock += Stroke->GetDownSeconds + 0.01;
	Stroke->TickStroke(Clock);
	TestEqual(TEXT("down"), static_cast<int32>(Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::Down));

	// The stroke component pushed the cue axis to the rig: the eye goes above the cue.
	Rig->SetComfort(0.0, 1.0, 1.0, true);
	Rig->TickRig(2.0);
	const FVector Eye = Pawn->GetCamera()->GetComponentLocation();
	const FVector BallUE = FRbCoords::PositionToUE(CueBall);
	TestTrue(FString::Printf(TEXT("eye above and behind the cue ball (%s)"), *Eye.ToString()), Eye.Z > BallUE.Z + 5.0 && FVector::Dist2D(Eye, BallUE) > 30.0);

	// Look while down: X aims (azimuth), Y moves the eyes along the line; the controller rotation is untouched.
	const double Az0 = Stroke->GetAim().Azimuth;
	Pawn->HandleLook(FVector2D(20.0, 4.0));
	TestTrue(TEXT("look X turns the aim"), !FMath::IsNearlyEqual(Stroke->GetAim().Azimuth, Az0));
	TestEqual(TEXT("look Y = eye pitch along the line"), Rig->GetGazePitch(), 0.5 * Pawn->GazeDegreesPerLookUnit * 4.0, 1e-9);
	TestEqual(TEXT("no gaze yaw while aiming"), Rig->GetGazeYaw(), 0.0);

	// Settle: stroke component and rig.
	Pawn->HandleSettle(true);
	Rig->TickRig(Rig->GetParams().HeadMotion.SettleSeconds + 0.01);
	TestEqual(TEXT("settled"), Rig->GetSettleAlpha(), 1.0);
	Pawn->HandleSettle(false);

	// Walking input is ignored down on the shot.
	const FVector Before = Pawn->GetActorLocation();
	Pawn->HandleMove(FVector2D(0.0, 1.0));
	TestTrue(TEXT("no movement input while down"), Pawn->GetPendingMovementInputVector().IsNearlyZero());

	// Stand up: walking and the controller look again.
	Stroke->RequestGetDownToggle();
	TestEqual(TEXT("walking again"), static_cast<int32>(Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::Walking));
	TestEqual(TEXT("rig standing"), static_cast<int32>(Rig->GetMode()), static_cast<int32>(ERbCameraRigMode::Standing));
	TestTrue(TEXT("can walk after standing up"), Pawn->CanWalk());
	Pawn->HandleMove(FVector2D(0.0, 1.0));
	TestFalse(TEXT("movement input while standing"), Pawn->GetPendingMovementInputVector().IsNearlyZero());
	TestTrue(TEXT("not moved yet"), Before.Equals(Pawn->GetActorLocation(), 1e-3));
	const double Gaze0 = Rig->GetGazePitch();
	Pawn->HandleLook(FVector2D(3.0, 3.0));
	TestEqual(TEXT("standing look is not gaze"), Rig->GetGazePitch(), Gaze0);

	// Ball in hand: the rig leans over the table; Confirm places (the component broadcasts the placement point).
	int32 Placed = 0;
	Stroke->OnCueBallPlaced.AddLambda([&Placed](const FVector&) { ++Placed; });
	Stroke->BeginCueBallPlacement();
	TestEqual(TEXT("rig ball in hand"), static_cast<int32>(Rig->GetMode()), static_cast<int32>(ERbCameraRigMode::BallInHand));
	TestTrue(TEXT("can walk with the ball in hand"), Pawn->CanWalk());
	Pawn->HandleConfirm();
	TestEqual(TEXT("confirm places the cue ball"), Placed, 1);
	Stroke->OnCueBallPlaced.Clear();

	// Locked while down (a scripted strike, a decision): GetDown still stands the player up.
	Stroke->BeginAddress(CueBall, kR);
	Pawn->HandleGetDown();
	Clock += Stroke->GetDownSeconds + 0.01;
	Stroke->TickStroke(Clock);
	TestEqual(TEXT("down again"), static_cast<int32>(Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::Down));
	Stroke->SetLocked(true);
	TestEqual(TEXT("locked while down: the eye stays down"), static_cast<int32>(Rig->GetMode()), static_cast<int32>(ERbCameraRigMode::DownOnShot));
	TestFalse(TEXT("locked down: no walking"), Pawn->CanWalk());
	Pawn->HandleGetDown();
	TestEqual(TEXT("locked: GetDown stands up"), static_cast<int32>(Rig->GetMode()), static_cast<int32>(ERbCameraRigMode::Standing));
	TestTrue(TEXT("locked and standing: walking around is allowed"), Pawn->CanWalk());
	Stroke->ClockOverride = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlayerControllerActions, "RawBreak.Unit.Player.ControllerActions", RB_UNIT_TEST_FLAGS)
bool FRbPlayerControllerActions::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
	{
		Wrapper.ForwardErrorMessages(this);
		return false;
	}
	ARbPlayerController* PC = Wrapper.GetTestWorld()->SpawnActor<ARbPlayerController>();
	if (!TestNotNull(TEXT("controller"), PC) || !TestNotNull(TEXT("overlay component"), PC->GetOverlay()))
	{
		return false;
	}
	URbOverlayComponent* Overlay = PC->GetOverlay();
	TestEqual(TEXT("overlay hidden by default (no HUD)"), static_cast<int32>(Overlay->GetMode()), static_cast<int32>(ERbOverlayMode::Hidden));
	PC->HandleToggleOverlay();
	TestEqual(TEXT("F1 pins"), static_cast<int32>(Overlay->GetMode()), static_cast<int32>(ERbOverlayMode::Pinned));
	PC->HandleToggleOverlay();
	TestEqual(TEXT("F1 again unpins"), static_cast<int32>(Overlay->GetMode()), static_cast<int32>(ERbOverlayMode::Hidden));
	PC->HandleToggleDebug();
	TestTrue(TEXT("F2 shows the debug block"), Overlay->IsDebugShown());
	PC->HandleToggleDebug();
	TestFalse(TEXT("F2 again hides it"), Overlay->IsDebugShown());
	PC->HandleGlance(true);
	PC->HandleGlance(false);
	// Without a match nothing replays and there is no option to cycle (never a crash).
	TestFalse(TEXT("no replay without a recorded shot"), PC->HandleReplay());
	PC->HandleReplayBack();
	TestFalse(TEXT("no option to cycle without a director"), PC->HandleCycleOption(1.0f));
	TestNotNull(TEXT("cheat manager class"), PC->CheatClass.Get());

	// The input wiring a local player gets (InitInputSystem = what ULocalPlayer / SetPlayer runs): the runtime input setup exists
	// before the pawn binds, the controller's non-pawn actions are bound on the Enhanced Input component, and Esc (a legacy key
	// binding outside the mapping context, evaluated by UEnhancedPlayerInput through UPlayerInput::EvaluateInputComponentDelegates)
	// reaches HandleReplayBack.
	PC->InitInputSystem();
	const URbInputSetup* Setup = PC->GetInputSetup();
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PC->InputComponent);
	if (!TestNotNull(TEXT("input setup created in SetupInputComponent"), Setup) || !TestNotNull(TEXT("Enhanced Input component (DefaultInput.ini)"), Input))
	{
		return false;
	}
	const auto Bound = [Input](const UInputAction* Action, ETriggerEvent Event) {
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
		{
			if (Binding && Action && Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
			{
				return true;
			}
		}
		return false;
	};
	TestTrue(TEXT("Glance held"), Bound(Setup->Glance, ETriggerEvent::Triggered));
	TestTrue(TEXT("Glance released"), Bound(Setup->Glance, ETriggerEvent::Completed));
	TestTrue(TEXT("ToggleOverlay"), Bound(Setup->ToggleOverlay, ETriggerEvent::Triggered));
	TestTrue(TEXT("ToggleDebug"), Bound(Setup->ToggleDebug, ETriggerEvent::Triggered));
	TestTrue(TEXT("Replay"), Bound(Setup->Replay, ETriggerEvent::Triggered));
	TestTrue(TEXT("CycleOption"), Bound(Setup->CycleOption, ETriggerEvent::Triggered));
	TestFalse(TEXT("pawn actions are not bound on the controller"), Bound(Setup->Move, ETriggerEvent::Triggered) || Bound(Setup->Look, ETriggerEvent::Triggered));
	int32 EscBindings = 0;
	for (const FInputKeyBinding& Key : Input->KeyBindings)
	{
		if (Key.Chord.Key == EKeys::Escape && Key.KeyEvent == IE_Pressed && Key.KeyDelegate.IsBoundToObject(PC))
		{
			++EscBindings;
			Key.KeyDelegate.Execute(EKeys::Escape); // HandleReplayBack without a replay: nothing to stop, never a crash
		}
	}
	TestEqual(TEXT("Esc pressed -> HandleReplayBack, bound once"), EscBindings, 1);
	return true;
}

// --- functional: the table blocks the pawn -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPawnBlockedByTable, "RawBreak.Functional.Player.PawnBlockedByTable", RB_UNIT_TEST_FLAGS)
bool FRbPawnBlockedByTable::RunTest(const FString& Parameters)
{
	// A yawed, translated 9-ft table on a floor; the pawn walks at it from the four sides and diagonally for 4 s each. It must reach
	// the table and stop at the apron: the capsule never overlaps the table footprint and never climbs onto it. Both table paths:
	// runtime meshes (complex-as-simple collision cooked when the mesh is set) and the baked static meshes the M1 level uses
	// (complex-as-simple bodies of URbAssetBakeLibrary; skipped with a note when the LFS assets are not checked out).
	FPawnWorld W;
	if (!W.Create(*this, FVector(0.0, 0.0, 5000.0)))
	{
		return false;
	}
	W.SpawnFloor();
	ARbPlayerCharacter* Pawn = W.Pawn;
	const double Radius = Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const double HalfHeight = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FTransform TableXf(FRotator(0.0, 17.0, 0.0), FVector(40.0, -30.0, 0.0));

	struct FApproach
	{
		FVector2D Dir; // table-local walking direction
		const TCHAR* Name;
	};
	for (const bool bBaked : {false, true})
	{
		const TCHAR* Path = bBaked ? TEXT("baked") : TEXT("runtime");
		ARbTable* Table = W.World->SpawnActorDeferred<ARbTable>(ARbTable::StaticClass(), TableXf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!TestNotNull(TEXT("table"), Table))
		{
			return false;
		}
		Table->bUseBakedMeshes = bBaked;
		Table->FinishSpawning(TableXf);
		if (!TestTrue(TEXT("table context"), Table->HasContext()))
		{
			return false;
		}
		if (bBaked && !Cast<UStaticMeshComponent>(Table->GetPartComponent(ERbTablePart::Bed)))
		{
			AddInfo(TEXT("baked table meshes not loaded (LFS assets not checked out?): only the runtime table was walked at"));
			Table->Destroy();
			continue;
		}
#if WITH_EDITOR
		// The editor loads and builds static meshes asynchronously (FStaticMeshCompilingManager): until the build finishes the
		// component has no physics state, and a test world's ticks never pump the compiling manager - the capsule would walk
		// through a table that in the game (cooked meshes, or the editor's own tick) blocks it. Finish the builds first.
		FAssetCompilingManager::Get().FinishAllCompilation();
		FAssetCompilingManager::Get().ProcessAsyncTasks();
#endif
		for (int32 I = 0; I < 3; ++I)
		{
			W.Tick(); // physics scene up to date
		}
		{
			// Precondition: the table's collision is live in the physics scene (a vertical trace at the table centre hits a part).
			const FVector Centre = Table->GetActorLocation();
			FHitResult Hit;
			const bool bHit = W.World->LineTraceSingleByChannel(Hit, Centre + FVector(0.0, 0.0, 300.0), Centre - FVector(0.0, 0.0, 1.0), ECC_Pawn);
			if (!TestTrue(FString::Printf(TEXT("%s: the table blocks a trace at its centre"), Path), bHit && Hit.GetActor() == Table))
			{
				return false;
			}
		}
		const FBox Local = TableFootprintLocal(*Table);
		if (!TestTrue(FString::Printf(TEXT("%s: table footprint"), Path), Local.IsValid && Local.GetSize().X > 250.0 && Local.GetSize().Y > 120.0))
		{
			return false;
		}
		const FTransform ActorXf = Table->GetActorTransform();
		for (const FApproach& A : {FApproach{FVector2D(1.0, 0.0), TEXT("head end")}, FApproach{FVector2D(-1.0, 0.0), TEXT("foot end")},
				 FApproach{FVector2D(0.0, 1.0), TEXT("side A")}, FApproach{FVector2D(0.0, -1.0), TEXT("side B")},
				 FApproach{FVector2D(1.0, 1.0).GetSafeNormal(), TEXT("diagonal at a corner")}})
		{
			const FString Name = FString::Printf(TEXT("%s table, %s"), Path, A.Name);
			// Start 1.5 m outside the footprint, walking at the table centre.
			const FVector Centre = Local.GetCenter();
			const double Reach = FMath::Max(Local.GetExtent().X, Local.GetExtent().Y) + 150.0 + Radius;
			const FVector StartLocal(Centre.X - A.Dir.X * Reach, Centre.Y - A.Dir.Y * Reach, 0.0);
			const FVector Start = ActorXf.TransformPosition(StartLocal) + FVector(0.0, 0.0, HalfHeight + 2.0);
			Pawn->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->GetCharacterMovement()->StopMovementImmediately();
			const FVector WorldDir = ActorXf.TransformVectorNoScale(FVector(A.Dir.X, A.Dir.Y, 0.0));
			for (int32 I = 0; I < 20; ++I)
			{
				W.Tick(); // settle on the floor
			}
			double MinClearance = UE_BIG_NUMBER;
			double MaxZDev = 0.0;
			for (int32 I = 0; I < 240; ++I)
			{
				Pawn->AddMovementInput(WorldDir, 1.0f);
				W.Tick();
				const FVector P = ActorXf.InverseTransformPosition(Pawn->GetActorLocation());
				// Distance of the capsule axis to the footprint rectangle (2D).
				const double Dx = FMath::Max3(Local.Min.X - P.X, 0.0, P.X - Local.Max.X);
				const double Dy = FMath::Max3(Local.Min.Y - P.Y, 0.0, P.Y - Local.Max.Y);
				MinClearance = FMath::Min(MinClearance, FMath::Sqrt(Dx * Dx + Dy * Dy) - Radius);
				MaxZDev = FMath::Max(MaxZDev, FMath::Abs(Pawn->GetActorLocation().Z - HalfHeight));
			}
			const FVector End = ActorXf.InverseTransformPosition(Pawn->GetActorLocation());
			const double Walked = FVector::Dist2D(End, StartLocal);
			AddInfo(FString::Printf(TEXT("%s: walked %.1f cm, min clearance to the footprint %.2f cm, max z deviation %.2f cm"), *Name, Walked,
				MinClearance, MaxZDev));
			TestTrue(FString::Printf(TEXT("%s: the pawn walked (%.0f cm)"), *Name, Walked), Walked > 100.0);
			// Rectangular footprint: at the rounded / cut corners the true boundary lies inside the box, so the diagonal only has to stay
			// out of the box minus the corner region; the sides must touch the apron.
			if (A.Dir.X == 0.0 || A.Dir.Y == 0.0)
			{
				TestTrue(FString::Printf(TEXT("%s: blocked at the table (clearance %.2f cm)"), *Name, MinClearance), MinClearance > -0.5 && MinClearance < 3.0);
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s: stays outside (clearance %.2f cm)"), *Name, MinClearance), MinClearance > -15.0);
				TestTrue(FString::Printf(TEXT("%s: reached the table (clearance %.2f cm)"), *Name, MinClearance), MinClearance < 20.0);
			}
			TestTrue(FString::Printf(TEXT("%s: feet on the floor (max z deviation %.2f cm)"), *Name, MaxZDev), MaxZDev < 4.0);
		}
		Table->Destroy();
		for (int32 I = 0; I < 3; ++I)
		{
			W.Tick();
		}
	}
	return true;
}

} // namespace RbCameraRigTests

#endif
