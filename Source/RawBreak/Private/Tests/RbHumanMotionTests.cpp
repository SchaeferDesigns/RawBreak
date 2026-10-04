// M2-F P5 acceptance, the camera IS the eyes (Docs/ue-architecture.md 18.3; ue5-realism-plan 4.8):
//   F6  posture: 20 get-downs with different seeds, 2 with the same seed: durations 0.8-1.5 s; overshoot 3-15 mm below the final eye
//       height, settled (within 1 mm) < 0.4 s after the overshoot peak; the head rotation reaches 50 % >= 80 ms before the translation;
//       different seeds differ (max eye-path difference > 5 mm), equal seeds bitwise equal; a stand-up reaches 50 % sooner than a
//       get-down; the rig's eye path at 30 / 60 / 144 fps within 0.1 mm.
//   F7  continuous: breathing peak in 0.2-0.33 Hz, rate and depth rise with Pressure 0 -> 1; Settle -> amplitude x 0.3 +- 0.1 within
//       2 s; Headcam standing sway RMS 3-6 mm; walking 1.4 m/s: bob 3-5 cm p-p (Headcam), footsteps at ~2 Hz alternating (and the rig
//       fires OnFootstep for a walking pawn); Eyes: no view rotation from bob (the gaze stays on the fixation point, < 0.05 deg);
//       Reduced motion: all zero. Plus the reactions (latency, no jump, the flinch).
// Traces for Tools/feel/plot_feel.py: Saved/RbFeel/getdown_seeds.csv. Owner: M2-F.

#include "Camera/RbCameraModel.h"
#include "Camera/RbCameraRigComponent.h"
#include "Camera/RbHumanMotion.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbStrokeComponent.h"
#include "Tests/RbTestFlags.h"

#include "CineCameraComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbHumanMotionTests
{
	// Standing eye -> down on the shot: 60 cm forward, 8 cm left, 70 cm down, the view pitching down 20 deg and turning 10 deg.
	const FTransform StandEye(FRotator(-5.0, 0.0, 0.0), FVector(0.0, 0.0, 165.0));
	const FTransform DownEye(FRotator(-25.0, 10.0, 0.0), FVector(60.0, -8.0, 95.0));

	struct FPath
	{
		TArray<double> Times;
		TArray<FTransform> Eyes;
		double Total = 0.0;
	};

	FPath Sample(ERbPostureChange Change, uint64 Seed, const FTransform& From, const FTransform& To, double Dt = 0.001)
	{
		FRbHumanMotion Motion;
		Motion.BeginPostureChange(Change, From, To, Seed);
		FPath Path;
		Path.Total = Motion.GetPostureChangeSeconds();
		for (double T = 0.0; T <= Path.Total + 0.1; T += Dt)
		{
			FTransform Eye;
			Motion.EvaluatePostureChange(T, To, Eye);
			Path.Times.Add(T);
			Path.Eyes.Add(Eye);
		}
		return Path;
	}

	// Time the translation (projection on the chord) / the rotation (angle from the start) first reaches Fraction.
	double TranslationTime(const FPath& P, const FTransform& From, const FTransform& To, double Fraction)
	{
		const FVector Chord = To.GetLocation() - From.GetLocation();
		for (int32 I = 0; I < P.Eyes.Num(); ++I)
		{
			if (FVector::DotProduct(P.Eyes[I].GetLocation() - From.GetLocation(), Chord) / Chord.SizeSquared() >= Fraction)
			{
				return P.Times[I];
			}
		}
		return -1.0;
	}

	double RotationTime(const FPath& P, const FTransform& From, const FTransform& To, double Fraction)
	{
		const double Full = From.GetRotation().AngularDistance(To.GetRotation());
		for (int32 I = 0; I < P.Eyes.Num(); ++I)
		{
			if (From.GetRotation().AngularDistance(P.Eyes[I].GetRotation()) >= Fraction * Full)
			{
				return P.Times[I];
			}
		}
		return -1.0;
	}

	void WriteCsv(const FString& Name, const FString& Header, const TArray<FString>& Rows)
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("RbFeel");
		IFileManager::Get().MakeDirectory(*Dir, true);
		FString Text = Header + TEXT("\n");
		for (const FString& Row : Rows)
		{
			Text += Row;
			Text += TEXT("\n");
		}
		FFileHelper::SaveStringToFile(Text, *(Dir / (Name + TEXT(".csv"))));
	}

	// Continuous layer at 60 Hz for Seconds; returns the samples.
	TArray<FRbHumanMotionSample> Run(const FRbCameraPresetParams& Params, FRbHumanMotionInputs In, double Seconds, double Fps = 60.0,
		TFunctionRef<void(int32, FRbHumanMotionInputs&)> Modify = [](int32, FRbHumanMotionInputs&) {})
	{
		FRbHumanMotion Motion;
		TArray<FRbHumanMotionSample> Out;
		In.DeltaSeconds = 1.0 / Fps;
		const int32 N = FMath::RoundToInt32(Seconds * Fps);
		for (int32 I = 0; I < N; ++I)
		{
			Modify(I, In);
			Out.Add(Motion.Step(In, Params));
		}
		return Out;
	}

	// Frequency [Hz] of the strongest component of X in [FLo, FHi] (DFT on a 0.002 Hz grid).
	double PeakFrequency(const TArray<double>& X, double Fs, double FLo, double FHi)
	{
		double Mean = 0.0;
		for (const double V : X)
		{
			Mean += V;
		}
		Mean /= FMath::Max(1, X.Num());
		double Best = 0.0;
		double BestF = 0.0;
		for (double F = FLo; F <= FHi; F += 0.002)
		{
			double Re = 0.0;
			double Im = 0.0;
			for (int32 I = 0; I < X.Num(); ++I)
			{
				const double Phase = UE_DOUBLE_TWO_PI * F * I / Fs;
				Re += (X[I] - Mean) * FMath::Cos(Phase);
				Im += (X[I] - Mean) * FMath::Sin(Phase);
			}
			const double P = Re * Re + Im * Im;
			if (P > Best)
			{
				Best = P;
				BestF = F;
			}
		}
		return BestF;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbHumanMotionF6, "RawBreak.Unit.HumanMotion.F6_PostureChanges", RB_UNIT_TEST_FLAGS)
bool FRbHumanMotionF6::RunTest(const FString& Parameters)
{
	using namespace RbHumanMotionTests;
	const double FinalZ = DownEye.GetLocation().Z;
	TArray<FPath> Downs;
	TArray<FString> Rows;
	double MinTotal = 1e9;
	double MaxTotal = 0.0;
	for (uint64 Seed = 1; Seed <= 20; ++Seed)
	{
		const FPath P = Sample(ERbPostureChange::GetDown, Seed, StandEye, DownEye);
		const FString Case = FString::Printf(TEXT("seed %llu"), Seed);
		MinTotal = FMath::Min(MinTotal, P.Total);
		MaxTotal = FMath::Max(MaxTotal, P.Total);
		TestTrue(FString::Printf(TEXT("%s: duration %.3f s in 0.8-1.5 s"), *Case, P.Total), P.Total >= 0.8 && P.Total <= 1.5);
		// Overshoot below the final eye height and the settle.
		double MinZ = 1e9;
		double PeakTime = 0.0;
		for (int32 I = 0; I < P.Eyes.Num(); ++I)
		{
			if (P.Eyes[I].GetLocation().Z < MinZ)
			{
				MinZ = P.Eyes[I].GetLocation().Z;
				PeakTime = P.Times[I];
			}
		}
		const double OvershootMm = 10.0 * (FinalZ - MinZ);
		double LastOut = PeakTime;
		for (int32 I = 0; I < P.Eyes.Num(); ++I)
		{
			if (P.Times[I] > PeakTime && FMath::Abs(P.Eyes[I].GetLocation().Z - FinalZ) > 0.1)
			{
				LastOut = P.Times[I];
			}
		}
		TestTrue(FString::Printf(TEXT("%s: overshoot %.2f mm in 3-15 mm"), *Case, OvershootMm), OvershootMm >= 3.0 && OvershootMm <= 15.0);
		TestTrue(FString::Printf(TEXT("%s: settled (1 mm) %.3f s after the peak (< 0.4 s)"), *Case, LastOut - PeakTime), LastOut - PeakTime < 0.4);
		// The head leads: the rotation reaches 50 % at least 80 ms before the translation.
		const double TRot = RotationTime(P, StandEye, DownEye, 0.5);
		const double TTrans = TranslationTime(P, StandEye, DownEye, 0.5);
		TestTrue(FString::Printf(TEXT("%s: rotation 50 %% at %.3f s, translation at %.3f s (lead >= 80 ms)"), *Case, TRot, TTrans),
			TRot >= 0.0 && TTrans >= 0.0 && TTrans - TRot >= 0.08);
		// It starts exactly at the start pose (the leading channels fade their lead in: no pop at the first frame) and ends exactly at
		// the target; the first millisecond moves the eye by far less than a millimetre and the view by far less than 0.01 deg.
		TestTrue(Case + TEXT(": starts at the start pose (no pop)"), P.Eyes[0].GetLocation().Equals(StandEye.GetLocation(), 1e-9) &&
			P.Eyes[0].GetRotation().Equals(StandEye.GetRotation(), 1e-9));
		TestTrue(Case + TEXT(": a smooth start"), FVector::Dist(P.Eyes[1].GetLocation(), P.Eyes[0].GetLocation()) < 0.001 &&
			FMath::RadiansToDegrees(P.Eyes[1].GetRotation().AngularDistance(P.Eyes[0].GetRotation())) < 0.01);
		TestTrue(Case + TEXT(": ends at the target"), P.Eyes.Last().GetLocation().Equals(DownEye.GetLocation(), 1e-9) &&
			P.Eyes.Last().GetRotation().Equals(DownEye.GetRotation(), 1e-9));
		// A stand-up with the same seed reaches 50 % sooner (fast start).
		const FPath Up = Sample(ERbPostureChange::StandUp, Seed, DownEye, StandEye);
		const double TUp = TranslationTime(Up, DownEye, StandEye, 0.5);
		TestTrue(FString::Printf(TEXT("%s: stand-up 50 %% at %.3f s < get-down %.3f s"), *Case, TUp, TTrans), TUp >= 0.0 && TUp < TTrans);
		// A human, not a jolt: the eye's peak speed stays below 2.5 m/s, the stand-up starts from rest too.
		for (const FPath* Path : {&P, &Up})
		{
			double PeakMps = 0.0;
			for (int32 I = 1; I < Path->Eyes.Num(); ++I)
			{
				PeakMps = FMath::Max(PeakMps, 0.01 * FVector::Dist(Path->Eyes[I].GetLocation(), Path->Eyes[I - 1].GetLocation()) / (Path->Times[I] - Path->Times[I - 1]));
			}
			TestTrue(FString::Printf(TEXT("%s: %s peak eye speed %.2f m/s < 2.5 m/s"), *Case, Path == &P ? TEXT("get-down") : TEXT("stand-up"), PeakMps),
				PeakMps < 2.5);
		}
		TestTrue(Case + TEXT(": the stand-up starts at rest"), FVector::Dist(Up.Eyes[1].GetLocation(), Up.Eyes[0].GetLocation()) < 0.001);
		Downs.Add(P);
		if (Seed <= 5)
		{
			for (int32 I = 0; I < P.Eyes.Num(); I += 5)
			{
				const FVector L = P.Eyes[I].GetLocation();
				Rows.Add(FString::Printf(TEXT("down,%llu,%.4f,%.4f,%.4f,%.4f,%.4f"), Seed, P.Times[I], L.X, L.Y, L.Z, P.Eyes[I].Rotator().Pitch));
			}
			for (int32 I = 0; I < Up.Eyes.Num(); I += 5)
			{
				const FVector L = Up.Eyes[I].GetLocation();
				Rows.Add(FString::Printf(TEXT("up,%llu,%.4f,%.4f,%.4f,%.4f,%.4f"), Seed, Up.Times[I], L.X, L.Y, L.Z, Up.Eyes[I].Rotator().Pitch));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("get-down durations %.3f-%.3f s"), MinTotal, MaxTotal));
	// Never two identical get-downs: consecutive seeds differ by more than 5 mm somewhere on the path.
	for (int32 S = 0; S + 1 < Downs.Num(); ++S)
	{
		double MaxDiff = 0.0;
		const int32 N = FMath::Min(Downs[S].Eyes.Num(), Downs[S + 1].Eyes.Num());
		for (int32 I = 0; I < N; ++I)
		{
			MaxDiff = FMath::Max(MaxDiff, FVector::Dist(Downs[S].Eyes[I].GetLocation(), Downs[S + 1].Eyes[I].GetLocation()));
		}
		TestTrue(FString::Printf(TEXT("seeds %d / %d differ by %.1f mm (> 5 mm)"), S + 1, S + 2, 10.0 * MaxDiff), 10.0 * MaxDiff > 5.0);
	}
	// The same seed twice: bitwise the same movement.
	const FPath A = Sample(ERbPostureChange::GetDown, 7, StandEye, DownEye);
	const FPath B = Sample(ERbPostureChange::GetDown, 7, StandEye, DownEye);
	bool bSame = A.Eyes.Num() == B.Eyes.Num();
	for (int32 I = 0; bSame && I < A.Eyes.Num(); ++I)
	{
		const FVector La = A.Eyes[I].GetLocation();
		const FVector Lb = B.Eyes[I].GetLocation();
		bSame = FMemory::Memcmp(&La, &Lb, sizeof(FVector)) == 0;
	}
	TestTrue(TEXT("equal seeds: bitwise equal paths"), bSame);
	// Quick = a plain 0.4 s ease without overshoot, Cut = instant.
	{
		FRbHumanMotion Motion;
		Motion.BeginPostureChange(ERbPostureChange::GetDown, StandEye, DownEye, 3, ERbPostureTransition::Quick);
		TestEqual(TEXT("Quick: 0.4 s"), Motion.GetPostureChangeSeconds(), 0.4, 1e-12);
		double MinZ = 1e9;
		for (double T = 0.0; T <= 0.5; T += 0.001)
		{
			FTransform Eye;
			Motion.EvaluatePostureChange(T, DownEye, Eye);
			MinZ = FMath::Min(MinZ, Eye.GetLocation().Z);
		}
		TestTrue(TEXT("Quick: no overshoot"), MinZ >= FinalZ - 1e-9);
		Motion.BeginPostureChange(ERbPostureChange::GetDown, StandEye, DownEye, 3, ERbPostureTransition::Cut);
		FTransform Eye;
		TestFalse(TEXT("Cut: done at once"), Motion.EvaluatePostureChange(0.0, DownEye, Eye));
		TestTrue(TEXT("Cut: at the target"), Eye.GetLocation().Equals(DownEye.GetLocation(), 1e-12));
	}
	WriteCsv(TEXT("getdown_seeds"), TEXT("change,seed,t,x_cm,y_cm,z_cm,pitch_deg"), Rows);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbHumanMotionRigFps, "RawBreak.Unit.HumanMotion.F6_RigPathFrameRate", RB_UNIT_TEST_FLAGS)
bool FRbHumanMotionRigFps::RunTest(const FString& Parameters)
{
	// The rig's get-down (human path, motion layer off) at 30 / 60 / 144 fps: the eye path within 0.1 mm at common times; the same
	// seed gives the same movement in every run.
	TMap<int32, TArray<FVector>> Paths;
	TMap<int32, TArray<double>> Times;
	for (const int32 Fps : {30, 60, 144})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
		{
			Wrapper.ForwardErrorMessages(this);
			return false;
		}
		UWorld* World = Wrapper.GetTestWorld();
		const FTransform Spawn(FRotator::ZeroRotator, FVector(-200.0, 30.0, 93.0));
		ARbPlayerCharacter* Pawn = World->SpawnActorDeferred<ARbPlayerCharacter>(ARbPlayerCharacter::StaticClass(), Spawn, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Pawn->GetCameraRig()->bApplyUserSettings = false;
		Pawn->GetStroke()->bApplyUserSettings = false;
		Pawn->FinishSpawning(Spawn);
		URbCameraRigComponent* Rig = Pawn->GetCameraRig();
		Rig->SetViewportAspectOverride(16.0 / 9.0);
		Rig->SetComfort(0.0, 1.0, 1.0, true);
		Rig->SetMotionSeed(42);
		Rig->TickRig(0.0);
		const FVector Contact(-100.0, 40.0, 79.4);
		const FVector U = FRotator(-4.0, 10.0, 0.0).Vector();
		Rig->SetCueAxisWorld(Contact, U);
		Rig->SetFocusTargetWorld(Contact + FVector(U.X, U.Y, 0.0).GetSafeNormal() * 150.0);
		Rig->SetMode(ERbCameraRigMode::DownOnShot);
		TArray<FVector>& Path = Paths.Add(Fps);
		TArray<double>& T = Times.Add(Fps);
		double Clock = 0.0;
		for (int32 I = 0; I < 2 * Fps; ++I)
		{
			Rig->TickRig(1.0 / Fps);
			Clock += 1.0 / Fps;
			Path.Add(Pawn->GetCamera()->GetComponentLocation());
			T.Add(Clock);
		}
	}
	// The 30 fps samples against the 60 / 144 fps samples at the same times (every 2nd / every 4.8th: compare where they coincide).
	double MaxDiff = 0.0;
	for (int32 I = 0; I < Paths[30].Num(); ++I)
	{
		const int32 I60 = 2 * I + 1;
		MaxDiff = FMath::Max(MaxDiff, FVector::Dist(Paths[30][I], Paths[60][I60]));
		if ((I + 1) % 5 == 0)
		{
			const int32 I144 = (I + 1) / 5 * 24 - 1; // 5 frames at 30 fps = 24 frames at 144 fps
			MaxDiff = FMath::Max(MaxDiff, FVector::Dist(Paths[30][I], Paths[144][I144]));
		}
	}
	AddInfo(FString::Printf(TEXT("30 / 60 / 144 fps eye paths: max difference %.6f mm"), 10.0 * MaxDiff));
	TestTrue(FString::Printf(TEXT("frame-rate independent within 0.1 mm (%.6f mm)"), 10.0 * MaxDiff), 10.0 * MaxDiff < 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbHumanMotionReactionEnd, "RawBreak.Unit.HumanMotion.ReactionEndsSmoothly", RB_UNIT_TEST_FLAGS)
bool FRbHumanMotionReactionEnd::RunTest(const FString& Parameters)
{
	// The rig down on the shot, watching a ball that runs 25 deg to the right: the head's reaction turns the view a few degrees. When
	// the watching ends (still down) the reaction relaxes back without a jump; watching again and standing up (a human StandUp)
	// never pops the view either.
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
	{
		Wrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();
	const FTransform Spawn(FRotator::ZeroRotator, FVector(-200.0, 30.0, 93.0));
	ARbPlayerCharacter* Pawn = World->SpawnActorDeferred<ARbPlayerCharacter>(ARbPlayerCharacter::StaticClass(), Spawn, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Pawn->GetCameraRig()->bApplyUserSettings = false;
	Pawn->GetStroke()->bApplyUserSettings = false;
	Pawn->FinishSpawning(Spawn);
	URbCameraRigComponent* Rig = Pawn->GetCameraRig();
	UCineCameraComponent* Cam = Pawn->GetCamera();
	Rig->SetViewportAspectOverride(16.0 / 9.0);
	Rig->SetMotionSeed(7);
	Rig->TickRig(0.0);
	const FVector Contact(-100.0, 30.0, 79.4);
	const FVector U = FRotator(-4.0, 0.0, 0.0).Vector();
	Rig->SetCueAxisWorld(Contact, U);
	Rig->SetFocusTargetWorld(Contact + FVector(150.0, 0.0, 0.0));
	Rig->SetMode(ERbCameraRigMode::DownOnShot);
	constexpr double Dt = 1.0 / 60.0;
	for (int32 I = 0; I < 100; ++I)
	{
		Rig->TickRig(Dt);
	}
	double Clock = 0.0;
	Rig->ReactionResolver = [&Clock, Contact](FVector& Out) {
		const double Deg = FMath::Min(25.0, 50.0 * Clock); // the ball runs to 25 deg right of the line within 0.5 s
		Out = Contact + FRotator(0.0, Deg, 0.0).Vector() * 150.0;
		return true;
	};
	const auto Run = [&](double Seconds, double& MaxStepDeg) {
		FVector Last = Cam->GetForwardVector();
		for (int32 I = 0; I < FMath::RoundToInt32(Seconds / Dt); ++I)
		{
			Clock += Dt;
			Rig->TickRig(Dt);
			const FVector Now = Cam->GetForwardVector();
			MaxStepDeg = FMath::Max(MaxStepDeg, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Now, Last), -1.0, 1.0))));
			Last = Now;
		}
	};
	Rig->SetWatching(true);
	double WatchStep = 0.0;
	Run(3.0, WatchStep);
	const double Turned = Rig->GetHumanMotionSample().Reaction.Yaw;
	TestTrue(FString::Printf(TEXT("the head followed the ball (reaction yaw %.2f deg)"), Turned), Turned > 3.0);
	// The watching ends while still down: the reaction relaxes, no jump.
	Rig->SetWatching(false);
	double EndStep = 0.0;
	Run(3.0, EndStep);
	TestTrue(FString::Printf(TEXT("the watch ends without a jump (max %.3f deg per frame)"), EndStep), EndStep < 0.25);
	TestTrue(FString::Printf(TEXT("the reaction relaxed back (%.3f deg)"), Rig->GetHumanMotionSample().Reaction.Yaw),
		FMath::Abs(Rig->GetHumanMotionSample().Reaction.Yaw) < 0.2);
	// Watching again, then standing up in the middle of it: the human StandUp, no pop at its first frame.
	Clock = 0.0;
	Rig->SetWatching(true);
	Run(2.0, WatchStep);
	FVector Before = Cam->GetForwardVector();
	Rig->SetMode(ERbCameraRigMode::Standing);
	Rig->TickRig(Dt);
	const double FirstStep = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Cam->GetForwardVector(), Before), -1.0, 1.0)));
	double StandStep = 0.0;
	Run(1.5, StandStep);
	AddInfo(FString::Printf(TEXT("watching max %.3f deg/frame, reaction %.2f deg; stand-up first frame %.3f deg, max %.3f deg/frame"), WatchStep, Turned,
		FirstStep, StandStep));
	TestTrue(FString::Printf(TEXT("standing up does not pop the view (first frame %.3f deg)"), FirstStep), FirstStep < 0.5);
	TestTrue(FString::Printf(TEXT("the stand-up turns the view smoothly (max %.3f deg per frame)"), StandStep), StandStep < 1.5);
	Rig->ReactionResolver = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbHumanMotionF7, "RawBreak.Unit.HumanMotion.F7_ContinuousLayer", RB_UNIT_TEST_FLAGS)
bool FRbHumanMotionF7::RunTest(const FString& Parameters)
{
	using namespace RbHumanMotionTests;
	const FRbCameraPresetParams Head = RbCameraModel::Defaults(ERbCameraPreset::Headcam);
	const FRbCameraPresetParams Eyes = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
	constexpr double Fs = 30.0;

	// Breathing: standing still, the Z spectrum peaks inside 0.2-0.33 Hz; faster and deeper under pressure.
	double LastRate = 0.0;
	double LastDepth = 0.0;
	for (const double Pressure : {0.0, 1.0})
	{
		FRbHumanMotionInputs In;
		In.Pressure = Pressure;
		In.BodySwayScale = 1.0;
		const TArray<FRbHumanMotionSample> S = Run(Head, In, 60.0, Fs);
		TArray<double> Z;
		double ZMin = 1e9;
		double ZMax = -1e9;
		for (const FRbHumanMotionSample& Sm : S)
		{
			Z.Add(Sm.Offset.Z);
			ZMin = FMath::Min(ZMin, Sm.Offset.Z);
			ZMax = FMath::Max(ZMax, Sm.Offset.Z);
		}
		const double Peak = PeakFrequency(Z, Fs, 0.05, 1.0);
		const double Depth = ZMax - ZMin;
		AddInfo(FString::Printf(TEXT("pressure %.1f: breathing peak %.3f Hz (rate %.3f Hz), p-p %.3f mm"), Pressure, Peak, S.Last().BreathRateHz, 10.0 * Depth));
		TestTrue(FString::Printf(TEXT("pressure %.1f: breathing peak %.3f Hz in 0.2-0.33 Hz"), Pressure, Peak), Peak >= 0.2 && Peak <= 0.33);
		if (Pressure > 0.0)
		{
			TestTrue(TEXT("pressure: faster breathing"), Peak > LastRate);
			TestTrue(TEXT("pressure: deeper breathing"), Depth > LastDepth * 1.3);
		}
		LastRate = Peak;
		LastDepth = Depth;
	}

	// Settle: breathing and sway x 0.3 +- 0.1 within 2 s (the rig ramps SettleAlpha over SettleSeconds = 1.5 s).
	{
		FRbHumanMotionInputs In;
		In.bDown = true;
		const double SettleSeconds = Head.HeadMotion.SettleSeconds;
		const TArray<FRbHumanMotionSample> S = Run(Head, In, 4.0, Fs, [&](int32 I, FRbHumanMotionInputs& X) {
			const double T = I / Fs;
			X.SettleAlpha = T < 1.0 ? 0.0 : FMath::Min(1.0, (T - 1.0) / SettleSeconds);
		});
		const double Before = S[FMath::RoundToInt32(0.9 * Fs)].BreathDepthScale;
		const double After = S[FMath::RoundToInt32(3.0 * Fs)].BreathDepthScale; // 2 s after the Settle began
		TestTrue(FString::Printf(TEXT("settle: amplitude x %.3f within 2 s (0.3 +- 0.1)"), After / Before), FMath::Abs(After / Before - 0.3) <= 0.1);
	}

	// Headcam standing sway: horizontal RMS 3-6 mm.
	{
		FRbHumanMotionInputs In;
		const TArray<FRbHumanMotionSample> S = Run(Head, In, 60.0, Fs);
		double Sum = 0.0;
		FVector2D Mean(0.0, 0.0);
		for (const FRbHumanMotionSample& Sm : S)
		{
			Mean += FVector2D(Sm.Offset.X, Sm.Offset.Y);
		}
		Mean /= S.Num();
		for (const FRbHumanMotionSample& Sm : S)
		{
			Sum += FVector2D::DistSquared(FVector2D(Sm.Offset.X, Sm.Offset.Y), Mean);
		}
		const double RmsMm = 10.0 * FMath::Sqrt(Sum / S.Num());
		TestTrue(FString::Printf(TEXT("Headcam standing sway RMS %.2f mm in 3-6 mm"), RmsMm), RmsMm >= 3.0 && RmsMm <= 6.0);
	}

	// Walking at 1.4 m/s: bob 3-5 cm p-p (Headcam), a footstep at every heel strike ~2 Hz, left / right alternating.
	{
		FRbHumanMotionInputs In;
		In.WalkSpeedMps = 1.4;
		In.BodySwayScale = 0.0; // the bob alone (breathing / sway are checked above)
		const TArray<FRbHumanMotionSample> S = Run(Head, In, 12.0, 120.0);
		double ZMin = 1e9;
		double ZMax = -1e9;
		int32 Steps = 0;
		int32 Alternations = 0;
		bool bLastLeft = false;
		for (int32 I = 240; I < S.Num(); ++I) // after the 0.25 s gait lag
		{
			ZMin = FMath::Min(ZMin, S[I].Offset.Z);
			ZMax = FMath::Max(ZMax, S[I].Offset.Z);
			if (S[I].bFootstep)
			{
				Alternations += (Steps > 0 && S[I].bLeftFoot != bLastLeft) ? 1 : 0;
				bLastLeft = S[I].bLeftFoot;
				++Steps;
			}
		}
		const double Rate = Steps / 10.0;
		TestTrue(FString::Printf(TEXT("walking bob p-p %.2f cm in 3-5 cm"), ZMax - ZMin), ZMax - ZMin >= 3.0 && ZMax - ZMin <= 5.0);
		TestTrue(FString::Printf(TEXT("footsteps at %.2f Hz (~2 Hz)"), Rate), Rate >= 1.8 && Rate <= 2.2);
		TestEqual(TEXT("left / right alternate"), Alternations, Steps - 1);
		// Standing still: no footsteps.
		FRbHumanMotionInputs Still;
		int32 StillSteps = 0;
		for (const FRbHumanMotionSample& Sm : Run(Head, Still, 5.0, 60.0))
		{
			StillSteps += Sm.bFootstep ? 1 : 0;
		}
		TestEqual(TEXT("no footsteps standing still"), StillSteps, 0);
	}

	// Eyes: the layer produces no view rotation from the bob (VOR), only translation x 0.3.
	{
		FRbHumanMotionInputs In;
		In.WalkSpeedMps = 1.4;
		double MaxRot = 0.0;
		for (const FRbHumanMotionSample& Sm : Run(Eyes, In, 5.0, 60.0))
		{
			MaxRot = FMath::Max3(MaxRot, FMath::Abs(Sm.Rotation.Pitch), FMath::Max(FMath::Abs(Sm.Rotation.Yaw), FMath::Abs(Sm.Rotation.Roll)));
		}
		TestTrue(FString::Printf(TEXT("Eyes: view rotation from bob %.6f deg < 0.05 deg"), MaxRot), MaxRot < 0.05);
	}

	// Reduced motion (MotionScale 0): everything zero.
	{
		FRbHumanMotionInputs In;
		In.WalkSpeedMps = 1.4;
		In.Pressure = 1.0;
		In.MotionScale = 0.0;
		In.bWatching = true;
		In.bHasReactionTarget = true;
		In.ReactionAnglesDeg = FVector2D(20.0, 5.0);
		bool bZero = true;
		FRbHumanMotion Motion;
		Motion.NotifyImpact(1.0);
		In.DeltaSeconds = 1.0 / 60.0;
		for (int32 I = 0; I < 300; ++I)
		{
			const FRbHumanMotionSample Sm = Motion.Step(In, Head);
			bZero &= Sm.Offset.IsZero() && Sm.Rotation.IsZero();
		}
		TestTrue(TEXT("reduced motion: all zero"), bZero);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbHumanMotionReactions, "RawBreak.Unit.HumanMotion.Reactions", RB_UNIT_TEST_FLAGS)
bool FRbHumanMotionReactions::RunTest(const FString& Parameters)
{
	using namespace RbHumanMotionTests;
	const FRbCameraPresetParams Eyes = RbCameraModel::Defaults(ERbCameraPreset::Eyes);
	// Watching: the cue ball runs 20 deg to the right; the head follows a fraction of it after the pursuit latency, without a jump.
	FRbHumanMotion Motion;
	FRbHumanMotionInputs In;
	In.bDown = true;
	In.bWatching = true;
	In.bHasReactionTarget = true;
	In.DeltaSeconds = 1.0 / 60.0;
	double FirstMove = -1.0;
	double MaxStep = 0.0;
	double Last = 0.0;
	for (int32 I = 0; I < 180; ++I)
	{
		const double T = I / 60.0;
		In.ReactionAnglesDeg = FVector2D(FMath::Min(20.0, 40.0 * T), 0.0); // 40 deg/s for 0.5 s
		const FRbHumanMotionSample S = Motion.Step(In, Eyes);
		if (FirstMove < 0.0 && FMath::Abs(S.Reaction.Yaw) > 1e-4)
		{
			FirstMove = T;
		}
		MaxStep = FMath::Max(MaxStep, FMath::Abs(S.Reaction.Yaw - Last));
		Last = S.Reaction.Yaw;
	}
	AddInfo(FString::Printf(TEXT("pursuit starts after %.3f s, head yaw %.2f deg after 3 s, max step %.3f deg"), FirstMove, Last, MaxStep));
	TestTrue(TEXT("pursuit latency 150-200 ms"), FirstMove >= 0.15 && FirstMove <= 0.25);
	TestEqual(TEXT("the head turns a fraction (0.3) of the way"), Last, 0.3 * 20.0, 0.5);
	TestTrue(TEXT("smooth pursuit: no jump"), MaxStep < 0.2);
	// The flinch: a loud impact moves the head back a few mm, peaking 50-80 ms later, and it returns.
	FRbHumanMotion Flinch;
	FRbHumanMotionInputs Quiet;
	Quiet.bDown = true;
	Quiet.DeltaSeconds = 1.0 / 240.0;
	Quiet.BodySwayScale = 1.0;
	FRbHumanMotionSample Base = Flinch.Step(Quiet, Eyes);
	Flinch.NotifyImpact(1.0);
	double PeakBack = 0.0;
	double PeakTime = 0.0;
	double EndBack = 0.0;
	for (int32 I = 1; I <= 240; ++I)
	{
		const FRbHumanMotionSample S = Flinch.Step(Quiet, Eyes);
		const double Back = -(S.Offset.X - S.StabilisedOffset.X);
		if (Back > PeakBack)
		{
			PeakBack = Back;
			PeakTime = I / 240.0;
		}
		EndBack = Back;
	}
	AddInfo(FString::Printf(TEXT("flinch: %.2f mm back after %.0f ms"), 10.0 * PeakBack, 1000.0 * PeakTime));
	TestTrue(TEXT("flinch: a few mm back"), 10.0 * PeakBack >= 2.0 && 10.0 * PeakBack <= 6.0);
	TestTrue(TEXT("flinch peaks after 50-80 ms"), PeakTime >= 0.05 && PeakTime <= 0.08);
	TestTrue(TEXT("flinch returns"), FMath::Abs(EndBack) < 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbHumanMotionRigWalk, "RawBreak.Unit.HumanMotion.F7_RigFootstepsAndGaze", RB_UNIT_TEST_FLAGS)
bool FRbHumanMotionRigWalk::RunTest(const FString& Parameters)
{
	// A walking pawn: the rig fires OnFootstep at ~2 Hz under the feet, and the Eyes keep the gaze on the fixation point (< 0.05 deg).
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
	{
		Wrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = Wrapper.GetTestWorld();
	AActor* Floor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	UBoxComponent* Box = NewObject<UBoxComponent>(Floor, TEXT("Floor"));
	Box->SetBoxExtent(FVector(5000.0, 5000.0, 10.0));
	Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Floor->SetRootComponent(Box);
	Box->RegisterComponent();
	Box->SetWorldLocation(FVector(0.0, 0.0, -10.0));
	const FTransform Spawn(FRotator::ZeroRotator, FVector(0.0, 0.0, 90.0));
	ARbPlayerCharacter* Pawn = World->SpawnActorDeferred<ARbPlayerCharacter>(ARbPlayerCharacter::StaticClass(), Spawn, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Pawn->GetCameraRig()->bApplyUserSettings = false;
	Pawn->GetStroke()->bApplyUserSettings = false;
	Pawn->FinishSpawning(Spawn);
	Pawn->GetCharacterMovement()->bRunPhysicsWithNoController = true;
	Pawn->GetCharacterMovement()->SetDefaultMovementMode();
	URbCameraRigComponent* Rig = Pawn->GetCameraRig();
	Rig->SetViewportAspectOverride(16.0 / 9.0);
	int32 Steps = 0;
	int32 Left = 0;
	double MaxFloorDev = 0.0;
	Rig->OnFootstep.AddLambda([&](const FRbFootstep& Step) {
		++Steps;
		Left += Step.bLeftFoot ? 1 : 0;
		MaxFloorDev = FMath::Max(MaxFloorDev, FMath::Abs(Step.WorldLocation.Z));
	});
	double MaxGazeError = 0.0;
	for (int32 I = 0; I < 5 * 60; ++I)
	{
		Pawn->AddMovementInput(FVector::ForwardVector, 1.0f);
		Wrapper.TickTestWorld(1.0f / 60.0f);
		if (I > 60)
		{
			const FVector Cam = Pawn->GetCamera()->GetComponentLocation();
			MaxGazeError = FMath::Max(MaxGazeError, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(Pawn->GetCamera()->GetForwardVector(), (Rig->GetFixationWorld() - Cam).GetSafeNormal()), -1.0, 1.0))));
		}
	}
	const double Speed = Pawn->GetVelocity().Size2D() / 100.0;
	AddInfo(FString::Printf(TEXT("walked at %.2f m/s: %d footsteps (%d left) in 5 s, gaze error %.5f deg"), Speed, Steps, Left, MaxGazeError));
	TestTrue(TEXT("walking at ~1.4 m/s"), Speed > 1.2 && Speed < 1.5);
	TestTrue(FString::Printf(TEXT("OnFootstep ~2 Hz (%d in 5 s incl. the start)"), Steps), Steps >= 8 && Steps <= 11);
	TestTrue(TEXT("both feet"), Left > 0 && Left < Steps);
	TestTrue(FString::Printf(TEXT("footsteps on the floor (%.2f cm: the capsule floats up to 2.4 cm)"), MaxFloorDev), MaxFloorDev < 3.0);
	TestTrue(FString::Printf(TEXT("Eyes: gaze on the fixation point while walking (%.5f deg < 0.05)"), MaxGazeError), MaxGazeError < 0.05);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
