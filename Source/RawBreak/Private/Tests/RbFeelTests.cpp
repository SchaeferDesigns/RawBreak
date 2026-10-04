// M2-F feel acceptance, input traces (Docs/ue-architecture.md 18.3; playtest 2026-09-28 P1 / P3):
//   F1  P1: address, a 12 cm forward stroke in 0.15 s with 3 cm lateral drift, the Stroke button held 0.3 s past the contact, release
//       with 0.8 cm of residual motion, 1 s watching: no view change caused by the input after the contact (motion layer off: exactly
//       none; human layer on: < 0.3 deg in total), the player stays down.
//   F2  P1: then a deliberate 4 cm move within 0.3 s: look resumes after the dead zone, faded in, no step > 0.5 deg between frames.
//   F3  P3: 12.5 cm of mouse travel = 90 deg +- 0.5 at 800 / 400 / 1600 DPI (MouseDpi matched); the same counts in 30 / 60 / 144 fps
//       frame splits give BITWISE equal azimuths.
//   F4  P3: the F3 trace with Shift = 90 x 0.075 deg (13.3x slower); the acceleration curve is monotone, continuous, gain 1 at the
//       reference speed, and a steady move gives the same aim at any frame rate.
//   P3  the Look action carries raw counts (neutral mouse axes in DefaultInput.ini, re-asserted by ARbPlayerController).
//   Pause drops a held stroke (M2-D contract): no contact after the resume, the held button must be released first; Enhanced
//   Input's synthetic Completed on the paused frame (before the component's tick) processes nothing, and a button let go during
//   the pause does not swallow the next press (review).
// The traces run through the real pawn routing (ARbPlayerCharacter::HandleLook / HandleStroke -> look intent gate / stroke component
// -> camera rig) in a game world ticked by hand, on a test clock. The traces are written to Saved/RbFeel/*.csv for the plots of
// Tools/feel/plot_feel.py (Docs/images/dev/m2f/). Owner: M2-F.

#include "Camera/RbCameraRigComponent.h"
#include "Core/RbCoords.h"
#include "Input/RbAimResponse.h"
#include "Player/RbBallInHandComponent.h"
#include "Player/RbLookIntentGate.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbPlayerController.h"
#include "Player/RbStrokeComponent.h"
#include "Tests/RbTestFlags.h"

#include "CineCameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbFeelTests
{
	constexpr double kR = 0.028575;
	constexpr double kFps = 60.0;

	double MinimumJerk(double U)
	{
		U = FMath::Clamp(U, 0.0, 1.0);
		return U * U * U * (10.0 + U * (-15.0 + 6.0 * U));
	}

	// Writes a trace for Tools/feel/plot_feel.py (Saved/RbFeel/<Name>.csv).
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

	// Integer mouse counts per frame of a cumulative travel path [cm] (the mouse reports whole counts; the frame gets their sum).
	TArray<FVector2D> CountsPerFrame(const TArray<FVector2D>& CumulativeCm, double Dpi)
	{
		TArray<FVector2D> Out;
		FVector2D Last(0.0, 0.0);
		for (const FVector2D& Cm : CumulativeCm)
		{
			const FVector2D Counts(FMath::RoundToDouble(Cm.X * Dpi / 2.54), FMath::RoundToDouble(Cm.Y * Dpi / 2.54));
			Out.Add(Counts - Last);
			Last = Counts;
		}
		return Out;
	}

	// A game world ticked by hand with a RAW BREAK pawn that ignores the user's settings (the defaults of the settings structs).
	struct FFeelWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		ARbPlayerCharacter* Pawn = nullptr;
		URbStrokeComponent* Stroke = nullptr;
		URbCameraRigComponent* Rig = nullptr;
		double Clock = 1000.0;

		bool Create(FAutomationTestBase& Test, const FVector& Location = FVector(-100.0, 0.0, 88.0 + 5.0))
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
			{
				Wrapper.ForwardErrorMessages(&Test);
				return false;
			}
			World = Wrapper.GetTestWorld();
			const FTransform Spawn(FRotator::ZeroRotator, Location);
			Pawn = World->SpawnActorDeferred<ARbPlayerCharacter>(ARbPlayerCharacter::StaticClass(), Spawn, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Pawn)
			{
				Test.AddError(TEXT("could not spawn ARbPlayerCharacter"));
				return false;
			}
			Pawn->GetCameraRig()->bApplyUserSettings = false;
			Pawn->GetStroke()->bApplyUserSettings = false;
			Pawn->FinishSpawning(Spawn);
			Stroke = Pawn->GetStroke();
			Rig = Pawn->GetCameraRig();
			Rig->SetViewportAspectOverride(16.0 / 9.0);
			Stroke->MouseDpi = 800.0;
			Stroke->Controls = FRbControlSettings();
			Stroke->ClockOverride = [this]() { return Clock; };
			Pawn->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Pawn->GetCharacterMovement()->SetDefaultMovementMode();
			return true;
		}

		~FFeelWorld()
		{
			if (Stroke)
			{
				Stroke->ClockOverride = nullptr;
				Stroke->OnStrokeContact.Clear();
			}
		}

		// One frame of the game loop: input first (PlayerTick), then the stroke (pre-physics), the hand and the rig (post-physics).
		void Frame(double Dt, const FVector2D& LookCounts = FVector2D::ZeroVector)
		{
			Clock += Dt;
			if (!LookCounts.IsZero())
			{
				Pawn->HandleLook(LookCounts, Dt);
			}
			Stroke->TickStroke(Clock);
			Pawn->GetBallInHand()->TickCarry(Dt);
			Rig->TickRig(Dt);
		}

		void Run(double Seconds, double Fps = kFps)
		{
			const int32 N = FMath::RoundToInt32(Seconds * Fps);
			for (int32 I = 0; I < N; ++I)
			{
				Frame(1.0 / Fps);
			}
		}

		// Address the cue ball 40 cm ahead (aim along +x), get down, wait until the human get-down has settled.
		bool GetDown(FAutomationTestBase& Test, double Pressure = 0.0)
		{
			FRbStrokeContext Context;
			Context.Situation.Pressure = Pressure;
			Context.Key.MatchSeed = 11;
			Context.Key.ShooterId = 1;
			Stroke->SetStrokeContext(Context);
			Stroke->BeginAddress(rb::Vec3(-0.6, 0.0, kR), kR);
			Stroke->SetAim(0.0, 0.0, 0.0, 0.0);
			Stroke->RequestGetDownToggle();
			Run(1.6);
			return Test.TestEqual(TEXT("down on the shot"), static_cast<int32>(Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::Down)) &&
				Test.TestFalse(TEXT("the get-down has settled"), Rig->IsPostureChanging());
		}
	};

	// Angle between two view directions [deg].
	double AngleDeg(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0)));
	}

	struct FTraceFrame
	{
		double Time = 0.0;
		FVector2D MouseCm = FVector2D::ZeroVector; // cumulative
		FVector Forward = FVector::ForwardVector;
		double GazeTargetYaw = 0.0;
		double GazeViewYaw = 0.0;
		double GazeViewPitch = 0.0;
		bool bHeld = false;
		bool bOpen = false;
		bool bContact = false;
	};

	// F1 + F2 in one world. bHuman: the human motion layer on (else off: the camera = the base pose).
	struct FGateTrace
	{
		TArray<FTraceFrame> Frames;
		double ContactTime = -1.0;
		double ReleaseTime = -1.0;
		double MoveStart = -1.0;
		int32 ContactFrame = INDEX_NONE;
		bool bStayedDown = true;
	};

	bool RunGateTrace(FAutomationTestBase& Test, bool bHuman, FGateTrace& Out)
	{
		FFeelWorld W;
		if (!W.Create(Test))
		{
			return false;
		}
		W.Rig->SetComfort(bHuman ? 1.0 : 0.0, 1.0, 1.0, true);
		if (!W.GetDown(Test))
		{
			return false;
		}
		URbStrokeComponent* Stroke = W.Stroke;
		ARbPlayerCharacter* Pawn = W.Pawn;
		Stroke->OnStrokeContact.AddLambda([&Out](const FRbStrokeCommit& Commit) { Out.ContactTime = Commit.ContactTime; });

		// The stroke: the Stroke button and Commit held, the hand 12 cm forward in 0.15 s (minimum jerk) with 3 cm of drift to the right;
		// the stroke component gets the hand path as 1 kHz samples, the Look action the same motion as per-frame counts.
		const double T0 = W.Clock + 0.05;
		Stroke->SetCommitHeld(true);
		Pawn->HandleStroke(true);
		TArray<FRbStrokeSample> Samples;
		for (int32 K = 0; K <= 150; ++K)
		{
			FRbStrokeSample S;
			S.Time = T0 + 0.001 * K;
			S.Position = 0.12 * MinimumJerk(K / 150.0);
			S.Lateral = 0.03 * MinimumJerk(K / 150.0);
			Samples.Add(S);
		}
		Stroke->InjectStrokeSamples(Samples);
		const auto MouseAt = [&](double T) -> FVector2D {
			// Cumulative mouse travel [cm]: the stroke (forward = +Y, right = +X), then after the release 0.8 cm of residual motion
			// forward over 0.12 s, then still; the deliberate move (F2) is added by the caller.
			const double U = (T - T0) / 0.15;
			FVector2D Cm(3.0 * MinimumJerk(U), 12.0 * MinimumJerk(U));
			if (Out.ReleaseTime > 0.0)
			{
				Cm.Y += 0.8 * MinimumJerk((T - Out.ReleaseTime) / 0.12);
			}
			if (Out.MoveStart > 0.0)
			{
				Cm.X += 4.0 * MinimumJerk((T - Out.MoveStart) / 0.3);
			}
			return Cm;
		};

		const double Dt = 1.0 / kFps;
		const double Dpi = Stroke->MouseDpi;
		FVector2D LastCounts(0.0, 0.0);
		const double TraceStart = W.Clock;
		for (int32 Frame = 0; Frame < FMath::RoundToInt32(3.2 * kFps); ++Frame)
		{
			const double T = W.Clock + Dt;
			// Held 0.3 s past the contact, then released; F2 starts 1 s after the release window.
			if (Out.ContactTime > 0.0 && Out.ReleaseTime < 0.0 && T >= Out.ContactTime + 0.3)
			{
				Out.ReleaseTime = T;
				W.Clock += Dt; // HandleStroke stamps with the clock: the release happens at this frame's time
				Pawn->HandleStroke(false);
				W.Clock -= Dt;
			}
			if (Out.ReleaseTime > 0.0 && Out.MoveStart < 0.0 && T >= Out.ReleaseTime + 0.12 + 1.0)
			{
				Out.MoveStart = T;
			}
			const FVector2D Cm = MouseAt(T);
			const FVector2D Counts(FMath::RoundToDouble(Cm.X * Dpi / 2.54), FMath::RoundToDouble(Cm.Y * Dpi / 2.54));
			W.Frame(Dt, Counts - LastCounts);
			LastCounts = Counts;
			FTraceFrame F;
			F.Time = W.Clock - TraceStart;
			F.MouseCm = Cm;
			F.Forward = W.Pawn->GetCamera()->GetForwardVector();
			F.GazeTargetYaw = W.Rig->GetGazeYaw();
			F.GazeViewYaw = W.Rig->GetViewGazeYaw();
			F.GazeViewPitch = W.Rig->GetViewGazePitch();
			F.bHeld = Out.ReleaseTime < 0.0;
			F.bOpen = W.Pawn->GetLookGate().IsArmed() && W.Pawn->GetLookGate().IsOpen();
			F.bContact = Out.ContactTime > 0.0 && Out.ContactFrame == INDEX_NONE;
			if (F.bContact)
			{
				Out.ContactFrame = Out.Frames.Num();
			}
			Out.Frames.Add(F);
			if (Out.ContactTime > 0.0)
			{
				Out.bStayedDown &= W.Rig->GetMode() == ERbCameraRigMode::DownOnShot &&
					(Stroke->GetPhase() == ERbStrokePhase::Watching || Stroke->GetPhase() == ERbStrokePhase::Contact);
			}
		}
		Out.ContactTime -= TraceStart;
		Out.ReleaseTime -= TraceStart;
		Out.MoveStart -= TraceStart;
		return true;
	}
}

// --- P1: the look intent gate --------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbFeelGatePure, "RawBreak.Unit.Feel.LookGate_Rules", RB_UNIT_TEST_FLAGS)
bool FRbFeelGatePure::RunTest(const FString& Parameters)
{
	// The gate alone, in cm and seconds: held = stroke, quiet after the release, dead zone within the window, drop not accumulate,
	// fade-in, stays open, frame-rate independent decision.
	FRbLookIntentGate Gate;
	TestEqual(TEXT("disarmed: passes"), Gate.Filter(FVector2D(1.0, 2.0), 0.0), FVector2D(1.0, 2.0));
	Gate.Arm(10.0);
	Gate.SetStrokeHeld(true, 10.0);
	TestTrue(TEXT("held: the stroke"), Gate.Filter(FVector2D(5.0, 0.0), 10.1).IsZero());
	Gate.SetStrokeHeld(false, 10.3);
	TestTrue(TEXT("quiet after the release"), Gate.Filter(FVector2D(3.0, 0.0), 10.5).IsZero());
	// Small motion after the quiet period: dropped, not accumulated (1.4 cm in 0.40 s never opens, however long it goes on).
	double T = 10.6;
	for (int32 I = 0; I < 40; ++I, T += 0.05)
	{
		Gate.Filter(FVector2D(0.1, 0.0), T); // 2 cm per 1 s = 0.8 cm per 0.4 s window
	}
	TestFalse(TEXT("slow drift below the dead zone never opens"), Gate.IsOpen());
	// A deliberate move: 2 cm within 0.1 s opens, the opening motion is dropped, then a 0.2 s fade-in.
	TestTrue(TEXT("opening motion dropped"), Gate.Filter(FVector2D(2.0, 0.0), T).IsZero());
	TestTrue(TEXT("open"), Gate.IsOpen());
	TestEqual(TEXT("fade-in half way"), Gate.Filter(FVector2D(1.0, 0.0), T + 0.1).X, 0.5, 1e-9);
	TestEqual(TEXT("fully open"), Gate.Filter(FVector2D(1.0, 0.0), T + 0.25).X, 1.0);
	TestEqual(TEXT("stays open"), Gate.Filter(FVector2D(1.0, 0.0), T + 0.28).X, 1.0);
	// Review: a new Stroke press closes the open gate - mouse motion while the button is held is never look (a second "air stroke"
	// while watching); its release starts a new quiet period, then only a new deliberate move opens it again.
	Gate.SetStrokeHeld(true, T + 0.3);
	TestTrue(TEXT("a new press closes the open gate"), !Gate.IsOpen() && Gate.Filter(FVector2D(1.0, 0.0), T + 0.3).IsZero());
	Gate.SetStrokeHeld(true, T + 0.35); // Enhanced Input repeats Triggered while held
	TestTrue(TEXT("held: the stroke, however far the mouse goes"), Gate.Filter(FVector2D(6.0, 0.0), T + 0.4).IsZero() && !Gate.IsOpen());
	Gate.SetStrokeHeld(false, T + 0.5);
	TestTrue(TEXT("quiet after this release too"), Gate.Filter(FVector2D(3.0, 0.0), T + 0.6).IsZero() && !Gate.IsOpen());
	TestTrue(TEXT("a new deliberate move opens it again (dropped)"), Gate.Filter(FVector2D(2.0, 0.0), T + 0.8).IsZero() && Gate.IsOpen());
	TestEqual(TEXT("open again after the fade-in"), Gate.Filter(FVector2D(1.0, 0.0), T + 1.05).X, 1.0);
	Gate.Disarm();
	TestTrue(TEXT("disarmed"), !Gate.IsArmed() && Gate.IsOpen());

	// The decision depends on travel and time, not on the frame split: 1.6 cm in 0.3 s opens at 30, 60, 144, 240 and 500 fps (the
	// window holds every delta, however many frames), and 1.4 cm never does.
	for (const double Fps : {30.0, 60.0, 144.0, 240.0, 500.0})
	{
		FRbLookIntentGate G;
		G.Arm(0.0);
		double Opened = -1.0;
		const int32 N = FMath::RoundToInt32(0.3 * Fps);
		for (int32 I = 1; I <= N; ++I)
		{
			const double Time = 0.5 + I / Fps;
			G.Filter(FVector2D(1.6 / N, 0.0), Time);
			if (G.IsOpen() && Opened < 0.0)
			{
				Opened = Time;
			}
		}
		TestTrue(FString::Printf(TEXT("%.0f fps: 1.6 cm in 0.3 s opens (at %.3f s)"), Fps, Opened), Opened > 0.0 && Opened < 0.81);
		FRbLookIntentGate Small;
		Small.Arm(0.0);
		for (int32 I = 1; I <= N; ++I)
		{
			Small.Filter(FVector2D(1.4 / N, 0.0), 0.5 + I / Fps);
		}
		TestFalse(FString::Printf(TEXT("%.0f fps: 1.4 cm in 0.3 s stays closed"), Fps), Small.IsOpen());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbFeelF1, "RawBreak.Unit.Feel.F1_CalmAfterContact", RB_UNIT_TEST_FLAGS)
bool FRbFeelF1::RunTest(const FString& Parameters)
{
	using namespace RbFeelTests;
	for (const bool bHuman : {false, true})
	{
		const FString Layer = bHuman ? TEXT("human layer on") : TEXT("motion layer off");
		FGateTrace Trace;
		if (!RunGateTrace(*this, bHuman, Trace))
		{
			return false;
		}
		if (!TestTrue(Layer + TEXT(": contact"), Trace.ContactTime > 0.0 && Trace.ContactFrame != INDEX_NONE) ||
			!TestTrue(Layer + TEXT(": released 0.3 s after the contact"), Trace.ReleaseTime > Trace.ContactTime + 0.29))
		{
			return false;
		}
		TestTrue(Layer + TEXT(": the player stays down (Watching, rig DownOnShot)"), Trace.bStayedDown);
		// Everything after the contact up to the deliberate move: the follow-through with the button held, the release with 0.8 cm of
		// residual motion, 1 s of watching.
		const FVector AtContact = Trace.Frames[Trace.ContactFrame].Forward;
		double MaxInputGaze = 0.0;
		double MaxViewChange = 0.0;
		double MaxViewChangeAfterSettle = 0.0;
		const FVector AtRelease = [&] {
			for (const FTraceFrame& F : Trace.Frames)
			{
				if (F.Time >= Trace.ReleaseTime)
				{
					return F.Forward;
				}
			}
			return AtContact;
		}();
		for (int32 I = Trace.ContactFrame; I < Trace.Frames.Num() && Trace.Frames[I].Time < Trace.MoveStart; ++I)
		{
			const FTraceFrame& F = Trace.Frames[I];
			MaxInputGaze = FMath::Max3(MaxInputGaze, FMath::Abs(F.GazeTargetYaw), FMath::Max(FMath::Abs(F.GazeViewYaw), FMath::Abs(F.GazeViewPitch)));
			MaxViewChange = FMath::Max(MaxViewChange, AngleDeg(F.Forward, AtContact));
			if (F.Time >= Trace.ReleaseTime)
			{
				MaxViewChangeAfterSettle = FMath::Max(MaxViewChangeAfterSettle, AngleDeg(F.Forward, AtRelease));
			}
			TestFalse(Layer + TEXT(": the gate stays closed"), F.bOpen);
		}
		AddInfo(FString::Printf(TEXT("%s: contact at %.3f s, release at %.3f s; input gaze %.6f deg, view change %.4f deg (after the release %.4f deg)"),
			*Layer, Trace.ContactTime, Trace.ReleaseTime, MaxInputGaze, MaxViewChange, MaxViewChangeAfterSettle));
		TestEqual(Layer + TEXT(": no view change caused by the input after the contact"), MaxInputGaze, 0.0);
		if (bHuman)
		{
			TestTrue(FString::Printf(TEXT("human layer: total view change %.4f deg < 0.3 deg"), MaxViewChange), MaxViewChange < 0.3);
		}
		else
		{
			// Only the rig's cue-axis smoothing settles on the contact pose (not input): nothing after the release.
			TestTrue(FString::Printf(TEXT("motion off: view change after the release %.6f deg ~ 0"), MaxViewChangeAfterSettle), MaxViewChangeAfterSettle < 1e-3);
		}

		// F2: the deliberate 4 cm move within 0.3 s opens the gate after the dead zone; look resumes faded in, never with a jump.
		double TravelAtOpen = -1.0;
		double MaxStep = 0.0;
		double FirstStep = -1.0;
		double LastYaw = 0.0;
		bool bLookBeforeDeadZone = false;
		const double MoveStartCm = [&] {
			for (const FTraceFrame& F : Trace.Frames)
			{
				if (F.Time >= Trace.MoveStart)
				{
					return F.MouseCm.X;
				}
			}
			return 0.0;
		}();
		for (int32 I = 0; I < Trace.Frames.Num(); ++I)
		{
			const FTraceFrame& F = Trace.Frames[I];
			if (F.Time < Trace.MoveStart)
			{
				LastYaw = F.GazeViewYaw;
				continue;
			}
			const double Travel = F.MouseCm.X - MoveStartCm;
			if (F.bOpen && TravelAtOpen < 0.0)
			{
				TravelAtOpen = Travel;
			}
			if (Travel < 1.5 && FMath::Abs(F.GazeTargetYaw) > 0.0)
			{
				bLookBeforeDeadZone = true;
			}
			const double Step = FMath::Abs(F.GazeViewYaw - LastYaw);
			if (FirstStep < 0.0 && Step > 0.0)
			{
				FirstStep = Step;
			}
			MaxStep = FMath::Max(MaxStep, Step);
			LastYaw = F.GazeViewYaw;
		}
		const FTraceFrame& End = Trace.Frames.Last();
		AddInfo(FString::Printf(TEXT("%s F2: gate opened at %.2f cm of travel, first step %.3f deg, max step %.3f deg, head yaw %.2f deg (target %.2f)"),
			*Layer, TravelAtOpen, FirstStep, MaxStep, End.GazeViewYaw, End.GazeTargetYaw));
		TestTrue(Layer + TEXT(": F2 the gate opens after the dead zone (> 1.5 cm)"), TravelAtOpen > 1.5);
		TestFalse(Layer + TEXT(": F2 no look before the dead zone"), bLookBeforeDeadZone);
		TestTrue(Layer + TEXT(": F2 look resumes"), End.GazeTargetYaw > 1.0 && End.GazeViewYaw > 0.5 * End.GazeTargetYaw);
		TestTrue(FString::Printf(TEXT("%s: F2 no step > 0.5 deg between two frames (max %.3f)"), *Layer, MaxStep), MaxStep <= 0.5);

		if (bHuman)
		{
			TArray<FString> Rows;
			for (const FTraceFrame& F : Trace.Frames)
			{
				const FRotator R = F.Forward.Rotation();
				Rows.Add(FString::Printf(TEXT("%.5f,%.5f,%.5f,%.6f,%.6f,%.6f,%.6f,%d,%d"), F.Time, F.MouseCm.X, F.MouseCm.Y, R.Yaw, R.Pitch, F.GazeTargetYaw,
					F.GazeViewYaw, F.bHeld ? 1 : 0, F.bOpen ? 1 : 0));
			}
			Rows.Add(FString::Printf(TEXT("# contact %.5f release %.5f move %.5f"), Trace.ContactTime, Trace.ReleaseTime, Trace.MoveStart));
			WriteCsv(TEXT("look_gate"), TEXT("t,mouse_x_cm,mouse_y_cm,view_yaw_deg,view_pitch_deg,gaze_target_yaw_deg,gaze_view_yaw_deg,stroke_held,gate_open"), Rows);
		}
	}
	return true;
}

// --- P3: aim in centimetres ----------------------------------------------------------------------------------------------------------

namespace RbFeelTests
{
	// Aims with Cm of mouse travel to the right spread over Seconds at Fps through the pawn (down on the shot); returns the azimuth
	// change [deg] (+ = clockwise seen from above = mouse right) and the per-frame trace.
	double AimTrace(FFeelWorld& W, double Cm, double Seconds, double Fps, bool bFine, TArray<FVector2D>* OutTrace = nullptr)
	{
		W.Stroke->SetAim(0.0, 0.0, 0.0, 0.0);
		W.Pawn->HandleFineAim(bFine);
		const double Az0 = W.Stroke->GetAim().Azimuth;
		const int32 N = FMath::Max(1, FMath::RoundToInt32(Seconds * Fps));
		TArray<FVector2D> Path;
		for (int32 I = 1; I <= N; ++I)
		{
			Path.Add(FVector2D(Cm * MinimumJerk(static_cast<double>(I) / N), 0.0));
		}
		const TArray<FVector2D> Counts = CountsPerFrame(Path, W.Stroke->MouseDpi);
		for (int32 I = 0; I < Counts.Num(); ++I)
		{
			W.Pawn->HandleLook(Counts[I], 1.0 / Fps);
			if (OutTrace)
			{
				OutTrace->Add(FVector2D(Path[I].X, FMath::RadiansToDegrees(FMath::UnwindRadians(Az0 - W.Stroke->GetAim().Azimuth))));
			}
		}
		W.Pawn->HandleFineAim(false);
		return FMath::RadiansToDegrees(FMath::UnwindRadians(Az0 - W.Stroke->GetAim().Azimuth));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbFeelF3, "RawBreak.Unit.Feel.F3_AimPerCentimetre", RB_UNIT_TEST_FLAGS)
bool FRbFeelF3::RunTest(const FString& Parameters)
{
	using namespace RbFeelTests;
	FFeelWorld W;
	if (!W.Create(*this) || !W.GetDown(*this))
	{
		return false;
	}
	TArray<FString> Rows;
	for (const double Dpi : {800.0, 400.0, 1600.0})
	{
		W.Stroke->MouseDpi = Dpi; // the calibrated DPI matches the mouse
		double Reference = 0.0;
		for (const double Fps : {30.0, 60.0, 144.0})
		{
			TArray<FVector2D> Trace;
			const double Deg = AimTrace(W, 12.5, 0.5, Fps, false, &Trace);
			const FString Case = FString::Printf(TEXT("%.0f DPI, %.0f fps"), Dpi, Fps);
			TestTrue(FString::Printf(TEXT("%s: 12.5 cm = %.4f deg (90 +- 0.5)"), *Case, Deg), FMath::Abs(Deg - 90.0) <= 0.5);
			if (Fps == 30.0)
			{
				Reference = Deg;
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s: bitwise equal to 30 fps"), *Case), FMemory::Memcmp(&Deg, &Reference, sizeof(double)) == 0);
			}
			if (Dpi == 800.0 && Fps == 60.0)
			{
				for (const FVector2D& P : Trace)
				{
					Rows.Add(FString::Printf(TEXT("coarse,%.5f,%.6f"), P.X, P.Y));
				}
			}
		}
	}
	// The direction: mouse right turns the cue clockwise seen from above (the core azimuth decreases), mouse left back.
	W.Stroke->MouseDpi = 800.0;
	TestTrue(TEXT("mouse right = clockwise"), AimTrace(W, 2.0, 0.1, 60.0, false) > 0.0);
	TestTrue(TEXT("mouse left = counter-clockwise"), AimTrace(W, -2.0, 0.1, 60.0, false) < 0.0);
	// The settings row "90 deg per x cm" and the sensitivity multiplier.
	TestEqual(TEXT("90 deg per 12.5 cm (settings row)"), RbAimResponse::CmForAimDegrees(90.0, W.Stroke->Controls), 12.5, 1e-4);
	W.Stroke->Controls.AimSensitivity = 2.0f;
	TestEqual(TEXT("sensitivity 2: 90 deg in 6.25 cm"), AimTrace(W, 6.25, 0.3, 60.0, false), 90.0, 0.5);
	W.Stroke->Controls.AimSensitivity = 1.0f;

	// F4 fine (Shift): x 0.075 = 6.75 deg for the same 12.5 cm (13.3x slower), bitwise frame-split independent too.
	double FineRef = 0.0;
	for (const double Fps : {30.0, 60.0, 144.0})
	{
		TArray<FVector2D> Trace;
		const double Deg = AimTrace(W, 12.5, 0.5, Fps, true, &Trace);
		TestEqual(FString::Printf(TEXT("F4 fine at %.0f fps: 90 x 0.075 deg"), Fps), Deg, 90.0 * 0.075, 0.05);
		if (Fps == 30.0)
		{
			FineRef = Deg;
		}
		else
		{
			TestTrue(FString::Printf(TEXT("F4 fine at %.0f fps: bitwise equal"), Fps), FMemory::Memcmp(&Deg, &FineRef, sizeof(double)) == 0);
		}
		if (Fps == 60.0)
		{
			for (const FVector2D& P : Trace)
			{
				Rows.Add(FString::Printf(TEXT("fine,%.5f,%.6f"), P.X, P.Y));
			}
		}
	}
	TestTrue(TEXT("F4: fine is 10-20x slower (the owner's range)"), 1.0 / W.Stroke->Controls.FineAimFactor >= 10.0 && 1.0 / W.Stroke->Controls.FineAimFactor <= 20.0);

	// A mixed trace (coarse, then fine) is exact too: counts are summed per mode.
	const double Mixed = [&] {
		W.Stroke->SetAim(0.0, 0.0, 0.0, 0.0);
		const double Az0 = W.Stroke->GetAim().Azimuth;
		for (int32 I = 0; I < 10; ++I)
		{
			W.Pawn->HandleLook(FVector2D(100.0, 0.0), 1.0 / 60.0);
		}
		W.Pawn->HandleFineAim(true);
		for (int32 I = 0; I < 10; ++I)
		{
			W.Pawn->HandleLook(FVector2D(100.0, 0.0), 1.0 / 60.0);
		}
		W.Pawn->HandleFineAim(false);
		return FMath::RadiansToDegrees(FMath::UnwindRadians(Az0 - W.Stroke->GetAim().Azimuth));
	}();
	const double Cm = RbAimResponse::CountsToCm(1000.0, 800.0);
	TestEqual(TEXT("coarse + fine"), Mixed, Cm * 7.2 * (1.0 + 0.075), 1e-5);

	// Acceleration (optional): a steady move at 30 / 60 / 144 fps gives the same aim (the gain sees the hand speed, not the frame).
	W.Stroke->Controls.AimAcceleration = 1.0f;
	double AccelRef = 0.0;
	for (const double Fps : {30.0, 60.0, 144.0})
	{
		W.Stroke->SetAim(0.0, 0.0, 0.0, 0.0);
		const double Az0 = W.Stroke->GetAim().Azimuth;
		const int32 N = FMath::RoundToInt32(0.5 * Fps);
		TArray<FVector2D> Path;
		for (int32 I = 1; I <= N; ++I)
		{
			Path.Add(FVector2D(5.0 * I / N, 0.0)); // 10 cm/s: the reference speed
		}
		for (const FVector2D& C : CountsPerFrame(Path, 800.0))
		{
			W.Pawn->HandleLook(C, 1.0 / Fps);
		}
		const double Deg = FMath::RadiansToDegrees(FMath::UnwindRadians(Az0 - W.Stroke->GetAim().Azimuth));
		if (Fps == 30.0)
		{
			AccelRef = Deg;
		}
		TestEqual(FString::Printf(TEXT("acceleration, 10 cm/s at %.0f fps: %.4f deg (gain 1 at the reference speed = linear %.4f)"), Fps, Deg, 5.0 * 7.2),
			Deg, 5.0 * 7.2, 0.05 * 5.0 * 7.2);
		TestEqual(FString::Printf(TEXT("acceleration frame-rate independent (%.0f fps)"), Fps), Deg, AccelRef, 0.01 * AccelRef);
	}
	W.Stroke->Controls.AimAcceleration = 0.0f;
	WriteCsv(TEXT("aim_trace"), TEXT("mode,mouse_cm,azimuth_deg"), Rows);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbFeelF4Curve, "RawBreak.Unit.Feel.F4_AccelerationCurve", RB_UNIT_TEST_FLAGS)
bool FRbFeelF4Curve::RunTest(const FString& Parameters)
{
	using namespace RbFeelTests;
	TArray<FString> Rows;
	for (const double A : {0.0, 0.25, 0.5, 1.0})
	{
		TestEqual(FString::Printf(TEXT("a = %.2f: gain 1 at the reference speed"), A), RbAimResponse::AccelerationGain(RbAimResponse::ReferenceSpeedCmPerSecond, A), 1.0, 1e-12);
		double Last = RbAimResponse::AccelerationGain(0.0, A);
		double MaxJump = 0.0;
		bool bMonotone = true;
		for (int32 I = 1; I <= 20000; ++I)
		{
			const double V = 0.01 * I; // 0 .. 200 cm/s
			const double G = RbAimResponse::AccelerationGain(V, A);
			bMonotone &= G >= Last - 1e-15;
			MaxJump = FMath::Max(MaxJump, FMath::Abs(G - Last));
			Last = G;
			if (I % 100 == 0)
			{
				Rows.Add(FString::Printf(TEXT("%.2f,%.4f,%.6f"), A, V, G));
			}
		}
		TestTrue(FString::Printf(TEXT("a = %.2f: monotone"), A), bMonotone);
		TestTrue(FString::Printf(TEXT("a = %.2f: continuous (max jump %.5f per 0.01 cm/s)"), A, MaxJump), MaxJump < 0.02);
		// The output (degrees per cm x gain) never decreases with the speed either: a faster hand never turns less.
		TestTrue(FString::Printf(TEXT("a = %.2f: slow finer, fast coarser"), A),
			A == 0.0 || (RbAimResponse::AccelerationGain(2.0, A) < 1.0 && RbAimResponse::AccelerationGain(50.0, A) > 1.0));
	}
	TestEqual(TEXT("a = 0: linear everywhere"), RbAimResponse::AccelerationGain(123.0, 0.0), 1.0);
	WriteCsv(TEXT("aim_accel"), TEXT("acceleration,speed_cm_s,gain"), Rows);
	return true;
}

// --- P3: raw counts in the Look action -----------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbFeelRawCounts, "RawBreak.Unit.Feel.P3_RawCountsLookAction", RB_UNIT_TEST_FLAGS)
bool FRbFeelRawCounts::RunTest(const FString& Parameters)
{
	// DefaultInput.ini: no smoothing, neutral mouse axes (BaseInput.ini's 0.07 would become a hidden Scalar modifier on the Look
	// mapping, the M1 root cause of "1.9 m for 90 deg").
	const UInputSettings* Settings = GetDefault<UInputSettings>();
	TestFalse(TEXT("mouse smoothing off"), Settings->bEnableMouseSmoothing);
	for (const FKey& Key : {EKeys::MouseX, EKeys::MouseY, EKeys::Mouse2D})
	{
		int32 Entries = 0;
		for (const FInputAxisConfigEntry& Entry : Settings->AxisConfig)
		{
			if (Entry.AxisKeyName == Key.GetFName())
			{
				++Entries;
				TestEqual(FString::Printf(TEXT("%s sensitivity 1"), *Key.ToString()), Entry.AxisProperties.Sensitivity, 1.0f);
				TestEqual(FString::Printf(TEXT("%s no dead zone"), *Key.ToString()), Entry.AxisProperties.DeadZone, 0.0f);
				TestEqual(FString::Printf(TEXT("%s exponent 1"), *Key.ToString()), Entry.AxisProperties.Exponent, 1.0f);
			}
		}
		TestEqual(FString::Printf(TEXT("%s: one axis config entry (the engine's 0.07 removed)"), *Key.ToString()), Entries, 1);
	}
	// The controller re-asserts it on its player input before the mapping context is added (a user ini cannot bring 0.07 back).
	UPlayerInput* Input = NewObject<UPlayerInput>(GetTransientPackage());
	FInputAxisProperties Props;
	Props.Sensitivity = 0.07f;
	Props.DeadZone = 0.1f;
	Input->SetAxisProperties(EKeys::Mouse2D, Props);
	TestTrue(TEXT("NeutraliseMouseAxes changes a scaled axis"), ARbPlayerController::NeutraliseMouseAxes(Input));
	FInputAxisProperties After;
	TestTrue(TEXT("Mouse2D properties"), Input->GetAxisProperties(EKeys::Mouse2D, After));
	TestTrue(TEXT("Mouse2D neutral"), After.Sensitivity == 1.0f && After.DeadZone == 0.0f && After.Exponent == 1.0f && !After.bInvert);
	TestFalse(TEXT("idempotent"), ARbPlayerController::NeutraliseMouseAxes(Input));

	// The standing look in degrees per cm: 22 deg per cm of travel.
	TestEqual(TEXT("look 22 deg/cm"), RbAimResponse::LookDegrees(800.0 / 2.54, 800.0, FRbControlSettings()), 22.0, 1e-9);
	TestEqual(TEXT("DPI independent"), RbAimResponse::LookDegrees(1600.0 / 2.54, 1600.0, FRbControlSettings()), 22.0, 1e-9);
	return true;
}

// --- Pause drops a held stroke ---------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbFeelPause, "RawBreak.Unit.Feel.PauseDropsHeldStroke", RB_UNIT_TEST_FLAGS)
bool FRbFeelPause::RunTest(const FString& Parameters)
{
	using namespace RbFeelTests;
	FFeelWorld W;
	if (!W.Create(*this) || !W.GetDown(*this))
	{
		return false;
	}
	int32 Contacts = 0;
	int32 Aborts = 0;
	W.Stroke->OnStrokeContact.AddLambda([&Contacts](const FRbStrokeCommit&) { ++Contacts; });
	W.Stroke->OnStrokeAborted.AddLambda([&Aborts](bool) { ++Aborts; });
	// Stroke button and Commit held, a committed stroke on its way (its forward part starts 0.8 s from now).
	W.Stroke->SetCommitHeld(true);
	W.Pawn->HandleStroke(true);
	const TArray<FRbStrokeSample> Samples = W.Stroke->MakeScriptedStroke(3.0, W.Clock + 0.05);
	W.Stroke->InjectStrokeSamples(Samples);
	W.Run(0.3);
	TestTrue(TEXT("stroke running"), W.Stroke->IsStrokeActive());
	// The world pauses (menu): the held stroke is dropped; the samples that "happen" during the pause are gone.
	W.Stroke->NotifyWorldPaused(true);
	TestFalse(TEXT("paused: no stroke"), W.Stroke->IsStrokeActive());
	W.Clock += 3.0; // the real clock runs on during the pause
	W.Stroke->NotifyWorldPaused(false);
	W.Run(1.5);
	TestEqual(TEXT("no contact after the resume"), Contacts, 0);
	TestEqual(TEXT("the cue stayed down on the shot"), static_cast<int32>(W.Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::Down));
	// The button is still held (Enhanced Input repeats Triggered): nothing until it is released and pressed again.
	W.Pawn->HandleStroke(true);
	TestFalse(TEXT("a button held through the pause does not stroke"), W.Stroke->IsStrokeActive());
	W.Pawn->HandleStroke(false);
	W.Pawn->HandleStroke(true);
	TestTrue(TEXT("a fresh press strokes again"), W.Stroke->IsStrokeActive());
	W.Pawn->HandleStroke(false);
	AddInfo(FString::Printf(TEXT("aborts reported: %d"), Aborts));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbFeelPauseClock, "RawBreak.Unit.Feel.PauseFreezesTheAddressClock", RB_UNIT_TEST_FLAGS)
bool FRbFeelPauseClock::RunTest(const FString& Parameters)
{
	// Review (3rd pass): the player's body is frozen with the world (the rig does not tick while paused), so the address's human clock
	// skips the pause: a pause during the get-down still starts Down when the eye arrives; down on the shot the cue's drift and tremor
	// go on from where they stopped (no jump of the cue at the resume); the Settle does not run out behind the menu; the time in the
	// menu never counts as time down (IntendedStroke::TimeDown, the HF-07 envelope that grows after 10 s down).
	using namespace RbFeelTests;
	FFeelWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	FRbStrokeContext Context;
	Context.Key.MatchSeed = 11;
	Context.Key.ShooterId = 1;
	W.Stroke->SetStrokeContext(Context);
	W.Stroke->BeginAddress(rb::Vec3(-0.6, 0.0, kR), kR);
	W.Stroke->SetAim(0.0, 0.0, 0.0, 0.0);
	W.Stroke->RequestGetDownToggle();
	const double DownSince0 = W.Stroke->GetDownSince();
	W.Run(0.3);
	// 1. Paused for 20 s during the get-down: Down still comes when the eye arrives (0.3 s of the get-down had run).
	W.Stroke->NotifyWorldPaused(true);
	W.Clock += 20.0;
	W.Stroke->NotifyWorldPaused(false);
	TestEqual(TEXT("get-down: Down moves on by the paused 20 s"), W.Stroke->GetDownSince(), DownSince0 + 20.0, 1e-9);
	W.Stroke->TickStroke(W.Clock);
	TestEqual(TEXT("get-down: still getting down after the resume"), static_cast<int32>(W.Stroke->GetPhase()),
		static_cast<int32>(ERbStrokePhase::GettingDown));
	W.Run(1.6);
	if (!TestEqual(TEXT("down"), static_cast<int32>(W.Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::Down)))
	{
		return false;
	}
	// 2. Down, the Settle held, then paused for 60 s: the cue pose at the resume is the pose of the pause (the drift processes go on).
	const double SettleAt = W.Clock;
	W.Stroke->SetSettleHeld(true);
	W.Run(0.5);
	rb::Vec3 TipBefore;
	rb::Vec3 DirBefore;
	W.Stroke->GetCuePoseCore(TipBefore, DirBefore);
	W.Stroke->NotifyWorldPaused(true);
	W.Clock += 60.0;
	W.Stroke->NotifyWorldPaused(false);
	W.Stroke->TickStroke(W.Clock);
	rb::Vec3 TipAfter;
	rb::Vec3 DirAfter;
	W.Stroke->GetCuePoseCore(TipAfter, DirAfter);
	const double JumpMm = 1000.0 * rb::Length(TipAfter - TipBefore);
	AddInfo(FString::Printf(TEXT("cue tip moved %.6f mm across the 60 s pause"), JumpMm));
	TestTrue(FString::Printf(TEXT("no jump of the cue at the resume (%.6f mm)"), JumpMm), JumpMm < 1e-4);
	// 3. The shot after the pauses: 80 s of menu never count as time down; the Settle started where it was pressed.
	FRbStrokeCommit Commit;
	int32 Contacts = 0;
	W.Stroke->OnStrokeContact.AddLambda([&](const FRbStrokeCommit& C) { Commit = C; ++Contacts; });
	W.Stroke->SetCommitHeld(true);
	W.Pawn->HandleStroke(true);
	W.Stroke->InjectStrokeSamples(W.Stroke->MakeScriptedStroke(2.0, W.Clock + 0.05));
	for (int32 I = 0; I < 300 && Contacts == 0; ++I)
	{
		W.Frame(1.0 / kFps);
	}
	if (!TestEqual(TEXT("one contact"), Contacts, 1))
	{
		return false;
	}
	const double DownSeconds = Commit.ContactTime - DownSince0 - 80.0;
	AddInfo(FString::Printf(TEXT("contact: TimeDown %.4f s (%.4f s on the clock incl. the pauses), SettleStart %.4f s"), Commit.Intended.TimeDown,
		Commit.ContactTime - DownSince0, Commit.Intended.SettleStart));
	TestEqual(TEXT("TimeDown skips the 80 s of pause"), Commit.Intended.TimeDown, DownSeconds, 1e-6);
	TestTrue(TEXT("TimeDown is a few seconds"), Commit.Intended.TimeDown > 0.5 && Commit.Intended.TimeDown < 5.0);
	TestEqual(TEXT("SettleStart where it was pressed (the down time before it)"), Commit.Intended.SettleStart, SettleAt - DownSince0 - 20.0, 1e-6);
	W.Pawn->HandleStroke(false);
	W.Stroke->SetCommitHeld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbFeelPauseInputOrder, "RawBreak.Unit.Feel.PauseReleaseBeforeTick", RB_UNIT_TEST_FLAGS)
bool FRbFeelPauseInputOrder::RunTest(const FString& Parameters)
{
	// Review: Enhanced Input fires Completed for the held Stroke / Commit actions on the FIRST paused frame (their trigger state is
	// forced to None while paused) - possibly before the stroke component's own tick noticed the pause. That synthetic release must
	// not process the stroke's due samples (a contact while paused, the shot playing on resume) and must not count as the release
	// after the pause; on resume the physical button decides (held through: release first; let go during the pause: the next press
	// strokes). The world is really paused here (a pauser player state), the events come in the engine's order.
	using namespace RbFeelTests;
	FFeelWorld W;
	if (!W.Create(*this) || !W.GetDown(*this))
	{
		return false;
	}
	AWorldSettings* Settings = W.World->GetWorldSettings();
	APlayerState* Pauser = W.World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("world settings"), Settings) || !TestNotNull(TEXT("pauser"), Pauser))
	{
		return false;
	}
	bool bKeyDown = true; // the physical Stroke button, as the pawn's player input would report it
	W.Stroke->IsStrokeButtonDown = [&bKeyDown]() { return bKeyDown; };
	int32 Contacts = 0;
	W.Stroke->OnStrokeContact.AddLambda([&Contacts](const FRbStrokeCommit&) { ++Contacts; });

	// A committed 3 m/s stroke, run until its forward swing is a few centimetres from the ball.
	W.Stroke->SetCommitHeld(true);
	W.Pawn->HandleStroke(true);
	W.Stroke->InjectStrokeSamples(W.Stroke->MakeScriptedStroke(3.0, W.Clock + 0.05));
	double LastX = W.Stroke->GetCueDisplacement();
	bool bNear = false;
	for (int32 I = 0; I < 300 && Contacts == 0 && !bNear; ++I)
	{
		W.Frame(1.0 / kFps);
		const double X = W.Stroke->GetCueDisplacement();
		bNear = X > LastX && X > -0.08;
		LastX = X;
	}
	if (!TestTrue(TEXT("the forward swing is close to the ball, no contact yet"), bNear && Contacts == 0))
	{
		return false;
	}
	// Esc: the world pauses; the frame's input comes first - the Completed of Stroke and Commit, with the crossing samples due.
	Settings->SetPauserPlayerState(Pauser);
	TestTrue(TEXT("the world is paused"), W.World->IsPaused());
	W.Clock += 0.3;
	W.Pawn->HandleStroke(false);
	W.Stroke->SetCommitHeld(false);
	TestEqual(TEXT("no contact while paused (the synthetic release processed nothing)"), Contacts, 0);
	TestTrue(TEXT("the pause was noticed by the input"), W.Stroke->IsPausedByWorld());
	TestFalse(TEXT("the stroke was dropped"), W.Stroke->IsStrokeActive());
	TestTrue(TEXT("the button held at the pause must be released after it"), W.Stroke->IsStrokeReleaseRequired());
	W.Pawn->HandleStroke(true); // a press during the pause (the action does not trigger while paused; a direct call must not stroke)
	TestFalse(TEXT("no stroke starts while paused"), W.Stroke->IsStrokeActive());

	// Resume with the button still down: Enhanced Input sends Started again - ignored until the release.
	Settings->SetPauserPlayerState(nullptr);
	W.Pawn->HandleStroke(true);
	TestFalse(TEXT("resumed"), W.Stroke->IsPausedByWorld());
	TestFalse(TEXT("a button held through the pause does not stroke"), W.Stroke->IsStrokeActive());
	W.Run(1.5);
	TestEqual(TEXT("no contact after the resume"), Contacts, 0);
	TestEqual(TEXT("still down on the shot"), static_cast<int32>(W.Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::Down));
	bKeyDown = false;
	W.Pawn->HandleStroke(false);
	TestFalse(TEXT("released"), W.Stroke->IsStrokeReleaseRequired());

	// Let go DURING the pause: its release was the synthetic one (or never reached the game behind the menu) - the first press after
	// the resume strokes.
	bKeyDown = true;
	W.Pawn->HandleStroke(true);
	TestTrue(TEXT("a new stroke"), W.Stroke->IsStrokeActive());
	Settings->SetPauserPlayerState(Pauser);
	W.Pawn->HandleStroke(false); // the synthetic Completed (the button is still down)
	TestTrue(TEXT("held at the pause: a release is required"), W.Stroke->IsStrokeReleaseRequired());
	bKeyDown = false;            // the player lets go in the menu: no event reaches the game
	Settings->SetPauserPlayerState(nullptr);
	W.Stroke->NotifyWorldPaused(false); // the component's tick notices the resume and reads the button
	TestFalse(TEXT("let go during the pause: no release required"), W.Stroke->IsStrokeReleaseRequired());
	bKeyDown = true;
	W.Pawn->HandleStroke(true);
	TestTrue(TEXT("let go during the pause: the first press after the resume strokes"), W.Stroke->IsStrokeActive());
	W.Pawn->HandleStroke(false);
	TestEqual(TEXT("never a contact"), Contacts, 0);
	W.Stroke->IsStrokeButtonDown = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbFeelStrokeOwnsMouse, "RawBreak.Unit.Feel.StrokeButtonOwnsTheMouse", RB_UNIT_TEST_FLAGS)
bool FRbFeelStrokeOwnsMouse::RunTest(const FString& Parameters)
{
	// Review: whenever the Stroke button is held the mouse is the stroke - also when it was pressed during the get-down (the stroke
	// starts when Down begins) and when it was held through a pause (it must be released first): that mouse motion neither aims nor
	// pitches the eyes. Released, the same motion aims again.
	using namespace RbFeelTests;
	FFeelWorld W;
	if (!W.Create(*this))
	{
		return false;
	}
	FRbStrokeContext Context;
	Context.Key.MatchSeed = 11;
	Context.Key.ShooterId = 1;
	W.Stroke->SetStrokeContext(Context);
	W.Stroke->BeginAddress(rb::Vec3(-0.6, 0.0, kR), kR);
	W.Stroke->SetAim(0.0, 0.0, 0.0, 0.0);
	W.Stroke->RequestGetDownToggle();
	W.Run(0.2);
	if (!TestEqual(TEXT("getting down"), static_cast<int32>(W.Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::GettingDown)))
	{
		return false;
	}
	const double Az0 = W.Stroke->GetAim().Azimuth;
	// A stroke-like mouse motion (forward / back with a little drift) with the button pressed early, while still going down. Returns
	// the largest eye pitch target on the way (forward and back cancel at the end).
	const auto StrokeMotion = [&W](int32 Frames) {
		double MaxPitch = 0.0;
		for (int32 I = 0; I < Frames; ++I)
		{
			W.Frame(1.0 / kFps, FVector2D(15.0, (I % 10) < 5 ? 120.0 : -120.0));
			MaxPitch = FMath::Max(MaxPitch, FMath::Abs(W.Rig->GetGazePitch()));
		}
		return MaxPitch;
	};
	W.Pawn->HandleStroke(true);
	const double PitchDuring = StrokeMotion(30);
	const double AzDuring = W.Stroke->GetAim().Azimuth;
	TestEqual(TEXT("pressed during the get-down: no eye pitch from the stroke's motion"), PitchDuring, 0.0);
	TestTrue(TEXT("pressed during the get-down: the aim stays (bitwise)"), FMemory::Memcmp(&AzDuring, &Az0, sizeof(double)) == 0);
	W.Run(1.5);
	TestEqual(TEXT("down"), static_cast<int32>(W.Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::Down));
	TestTrue(TEXT("the button held through the get-down strokes from Down on"), W.Stroke->IsStrokeActive());

	// A pause with the button held drops the stroke; after the resume the button must be released first, and until then its motion is
	// still no aim and no eye pitch.
	W.Stroke->NotifyWorldPaused(true);
	W.Clock += 1.0;
	W.Stroke->NotifyWorldPaused(false);
	TestTrue(TEXT("held through the pause: a release is required"), W.Stroke->IsStrokeReleaseRequired());
	const double PitchHeld = StrokeMotion(30);
	const double AzHeld = W.Stroke->GetAim().Azimuth;
	TestEqual(TEXT("held through the pause: no eye pitch"), PitchHeld, 0.0);
	TestTrue(TEXT("held through the pause: the aim stays (bitwise)"), FMemory::Memcmp(&AzHeld, &Az0, sizeof(double)) == 0);

	// Released: the mouse aims (X) and runs the eyes along the line (Y) again.
	W.Pawn->HandleStroke(false);
	TestFalse(TEXT("released"), W.Stroke->IsStrokeReleaseRequired());
	for (int32 I = 0; I < 10; ++I)
	{
		W.Frame(1.0 / kFps, FVector2D(15.0, 30.0));
	}
	const double TurnedDeg = FMath::RadiansToDegrees(FMath::UnwindRadians(Az0 - W.Stroke->GetAim().Azimuth));
	AddInfo(FString::Printf(TEXT("after the release: aim %.3f deg, eye pitch %.3f deg"), TurnedDeg, W.Rig->GetGazePitch()));
	TestEqual(TEXT("released: 150 counts at 800 DPI aim 0.476 cm x 7.2 deg/cm"), TurnedDeg, RbAimResponse::CountsToCm(150.0, 800.0) * 7.2, 1e-6);
	TestTrue(TEXT("released: the eyes pitch again"), W.Rig->GetGazePitch() > 1.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
