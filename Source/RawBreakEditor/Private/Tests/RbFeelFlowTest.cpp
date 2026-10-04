// RawBreak.Functional.Feel.FeelFlow (Docs/ue-architecture.md 18.3 F8): PIE on the generated test room L_M1_TestRoom, the whole M2-F
// loop through the pawn's own input routing, frame by frame in the running game (real ticks, the stroke component's real clock):
//   ball in hand (the break): the carrying hand holds the cue ball over a legal kitchen spot, Confirm lowers it onto exactly the
//   previewed target -> the human get-down (0.8-1.5 s) -> aim with the F3 trace (12.5 cm at 800 DPI = 90 deg +- 0.5, back = bitwise
//   the start) -> a committed stroke with the Stroke button (left mouse, pressed through the player input, so Enhanced Input sends
//   its real events: the synthetic Completed on the paused frame, Started again on resume) held and the world paused: the stroke is
//   dropped, no contact while paused or after the resume, a button held through the pause does not stroke until it is released ->
//   the shot: the Stroke button held 0.3 s past the contact with the
//   mouse's follow-through, released with residual motion, 1 s of watching: the player stays down and no look input reaches the head
//   (P1); a deliberate move opens the look again (F2) -> stand up while watching (a human StandUp) -> after a scratch, ball in hand
//   anywhere: the hand refuses a spot on another ball (nothing placed) and sets the ball down on a free spot exactly.
// Needs the generated map (rb_make_test_room.py) and the player assets (rb_make_player.py: the hand is only a visual). Owner: M2-F.

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"

#include "Camera/RbCameraRigComponent.h"
#include "Core/RbAssetPaths.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Input/RbAimResponse.h"
#include "Player/RbBallInHandComponent.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbStrokeComponent.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbFeelFlow
{
	double MinimumJerk(double U)
	{
		U = FMath::Clamp(U, 0.0, 1.0);
		return U * U * U * (10.0 + U * (-15.0 + 6.0 * U));
	}

	double Dist(const rb::Vec2& A, const rb::Vec2& B)
	{
		return FMath::Sqrt(FMath::Square(A.x - B.x) + FMath::Square(A.y - B.y));
	}
}

class FRbWaitForFeelMatchCommand : public IAutomationLatentCommand
{
public:
	FRbWaitForFeelMatchCommand(FAutomationTestBase* InTest, double InTimeout) : Test(InTest), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		const UWorld* World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
		const ARbGameMode* Mode = World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
		const URbMatchDirector* Director = Mode ? Mode->GetDirector() : nullptr;
		if (Director && Director->GetPhase() != ERbDirectorPhase::Idle)
		{
			return true;
		}
		if (GetCurrentRunTime() > Timeout)
		{
			Test->AddError(TEXT("PIE on L_M1_TestRoom did not start an ARbGameMode match"));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	double Timeout;
};

// The flow, one step per group of frames (a step waits for its condition with a timeout; any failure ends the command).
class FRbFeelFlowCommand : public IAutomationLatentCommand
{
public:
	explicit FRbFeelFlowCommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual ~FRbFeelFlowCommand() override
	{
		if (Stroke.IsValid())
		{
			Stroke->OnStrokeContact.Remove(ContactHandle);
		}
		if (Hand.IsValid())
		{
			Hand->OnSetDown.Remove(SetDownHandle);
			Hand->OnRefused.Remove(RefusedHandle);
		}
	}

	virtual bool Update() override
	{
		if (!Resolve())
		{
			return true;
		}
		const double T = FPlatformTime::Seconds() - StepStart;
		++StepFrames;
		switch (Step)
		{
		case 0: return StartBallInHand(T);
		case 1: return WaitAndConfirmLegal(T);
		case 2: return WaitPlacedThenGetDown(T);
		case 3: return WaitDown(T);
		case 4: return AimTrace(false);
		case 5: return AimTrace(true);
		case 6: return PauseStart(T);
		case 7: return PauseHold();
		case 8: return PauseResumed(T);
		case 9: return StrokeStart();
		case 10: return StrokeAndWatch(T);
		case 11: return DeliberateLookThenStandUp(T);
		case 12: return WaitShotThenScratch(T);
		case 13: return WaitScratchBallInHand(T);
		case 14: return RefuseOnBall(T);
		case 15: return PlaceFree(T);
		default: return true;
		}
	}

private:
	// --- helpers ------------------------------------------------------------------------------------------------------------

	bool Resolve()
	{
		World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
		const ARbGameMode* Mode = World ? Cast<ARbGameMode>(World->GetAuthGameMode()) : nullptr;
		Director = Mode ? Mode->GetDirector() : nullptr;
		Table = Mode ? Mode->GetTable() : nullptr;
		PC = World ? World->GetFirstPlayerController() : nullptr;
		Pawn = PC ? Cast<ARbPlayerCharacter>(PC->GetPawn()) : nullptr;
		if (!Director || !Table || !Table->HasContext() || !Pawn)
		{
			Test->AddError(TEXT("the play world lost its director / table / pawn"));
			return false;
		}
		if (!Stroke.IsValid())
		{
			Stroke = Pawn->GetStroke();
			Rig = Pawn->GetCameraRig();
			Hand = Pawn->GetBallInHand();
			ContactHandle = Stroke->OnStrokeContact.AddLambda([this](const FRbStrokeCommit&) { ++Contacts; ContactAt = FPlatformTime::Seconds(); });
			SetDownHandle = Hand->OnSetDown.AddLambda([this](const rb::Vec2&) { ++SetDowns; });
			RefusedHandle = Hand->OnRefused.AddLambda([this](const rb::Vec2&) { ++Refusals; });
			// The shipped defaults of the settings structs (a user's ini must not change the F3 numbers).
			Stroke->MouseDpi = 800.0;
			Stroke->Controls = FRbControlSettings();
			Stroke->ApplyUserSettings(); // rebases the aim; re-reads the ini ...
			Stroke->MouseDpi = 800.0;    // ... so set the test values again
			Stroke->Controls = FRbControlSettings();
			Stroke->bHardcore = false;   // Commit decides practice vs shot (the pause step strokes with Commit held)
		}
		return Stroke.IsValid() && Rig.IsValid() && Hand.IsValid();
	}

	void Next()
	{
		++Step;
		StepStart = FPlatformTime::Seconds();
		StepFrames = 0;
	}

	bool Fail(const FString& Why)
	{
		Test->AddError(FString::Printf(TEXT("step %d: %s"), Step, *Why));
		if (bStrokeKeyDown)
		{
			PressStrokeKey(false);
		}
		Stroke->SetCommitHeld(false);
		Pawn->HandleStroke(false);
		if (PC && World && World->IsPaused())
		{
			PC->SetPause(false);
		}
		return true;
	}

	double Dt() const { return World ? World->GetDeltaSeconds() : 1.0 / 60.0; }

	// The standing / ball-in-hand view turned to a core point of the table frame (like rb.Player.LookAt).
	void LookAtCore(const rb::Vec2& P)
	{
		const FVector Point = Table->CoreToWorld(rb::Vec3(P.x, P.y, 0.0));
		for (int32 I = 0; I < 4; ++I)
		{
			const FVector Eye = Rig->GetBaseEyeTransform().GetLocation();
			const FRotator R = (Point - Eye).Rotation();
			PC->SetControlRotation(FRotator(R.Pitch, R.Yaw, 0.0));
			Rig->TickRig(0.0);
		}
	}

	rb::Vec3 CueBallCore() const { return Director->GetTableState().Balls[0].State.Position; }

	// The hand holds the ball over the look point (the target is set by the stroke component from the view, the hand follows with
	// human lag): at least 0.6 s and 30 frames into the step, within 3 mm.
	bool HandArrived(double T) const
	{
		return T >= 0.6 && StepFrames >= 30 && Hand->HasTarget() &&
			RbFeelFlow::Dist(Hand->GetHandPlanCore(), Hand->GetTargetCore()) <= 0.003;
	}

	FString HandState() const
	{
		const FRotator View = Rig->GetBaseEyeTransform().Rotator();
		const FRotator Control = PC->GetControlRotation();
		return FString::Printf(TEXT("hand state %d target %d (%.3f, %.3f) at (%.3f, %.3f); stroke %d rig %d view pitch %.1f yaw %.1f, control %.1f / %.1f, ")
			TEXT("world time %.2f s paused %d, %d frames"),
			static_cast<int32>(Hand->GetState()), Hand->HasTarget() ? 1 : 0, Hand->GetTargetCore().x, Hand->GetTargetCore().y,
			Hand->GetHandPlanCore().x, Hand->GetHandPlanCore().y, static_cast<int32>(Stroke->GetPhase()), static_cast<int32>(Rig->GetMode()),
			View.Pitch, View.Yaw, Control.Pitch, Control.Yaw, World->GetTimeSeconds(), World->IsPaused() ? 1 : 0, StepFrames);
	}

	// --- steps ----------------------------------------------------------------------------------------------------------------

	bool StartBallInHand(double T)
	{
		// The match starts before the game mode restarts (possesses) the player, which resets the control rotation: give the world
		// a second first.
		if (T < 1.0 || StepFrames < 30 || Director->GetPhase() != ERbDirectorPhase::AwaitPlacement ||
			Stroke->GetPhase() != ERbStrokePhase::PlacingCueBall || Rig->IsPostureChanging())
		{
			return T > 15.0 ? Fail(TEXT("no ball in hand at the break")) : false;
		}
		Test->TestEqual(TEXT("the hand carries the cue ball"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Carrying));
		Director->SetLivePlaybackRate(1.0f);
		LegalSpot = rb::Vec2(Director->GetMatchConfig().Table.HeadStringX - 0.30, 0.15);
		LookAtCore(LegalSpot);
		Next();
		return false;
	}

	bool WaitAndConfirmLegal(double T)
	{
		// The hand follows the look point with human lag: wait until it holds the ball over the target, then Confirm.
		if (!HandArrived(T))
		{
			return T > 5.0 ? Fail(TEXT("the hand never reached the look point: ") + HandState()) : false;
		}
		Test->TestEqual(TEXT("ball in hand: rig leans over the table"), static_cast<int32>(Rig->GetMode()), static_cast<int32>(ERbCameraRigMode::BallInHand));
		Test->TestTrue(TEXT("carried above the hover height"), Hand->GetBallBottomCm() >= Hand->HoverHeightCm - 1e-6);
		Target = Hand->GetTargetCore();
		Test->TestTrue(FString::Printf(TEXT("the target (%.3f, %.3f) is the look point"), Target.x, Target.y), RbFeelFlow::Dist(Target, LegalSpot) < 0.01);
		Test->TestTrue(TEXT("legal kitchen spot"), Hand->IsTargetLegal());
		Pawn->HandleConfirm();
		Test->TestEqual(TEXT("Confirm lowers the ball"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Lowering));
		Next();
		return false;
	}

	bool WaitPlacedThenGetDown(double T)
	{
		if (Director->GetPhase() != ERbDirectorPhase::AwaitStroke || Stroke->GetPhase() != ERbStrokePhase::Walking)
		{
			return T > 3.0 ? Fail(TEXT("the lowered ball was never placed")) : false;
		}
		const rb::Vec3 Cue = CueBallCore();
		const double ErrMm = 1000.0 * RbFeelFlow::Dist(rb::Vec2(Cue.x, Cue.y), Target);
		Test->TestTrue(FString::Printf(TEXT("placed exactly on the previewed target (%.4f mm)"), ErrMm), ErrMm < 0.1);
		Test->TestTrue(FString::Printf(TEXT("set down %.2f s after Confirm (~0.25 s + a frame)"), T), T >= 0.2 && T < 0.6);
		Test->TestEqual(TEXT("one set-down"), SetDowns, 1);
		// Aim at the apex ball and get down: a human get-down of 0.8-1.5 s.
		const rb::Vec3 Apex = Director->GetTableState().Balls[1].State.Position;
		Stroke->SetAim(FMath::Atan2(Apex.y - Cue.y, Apex.x - Cue.x), FMath::DegreesToRadians(3.0), 0.0, 0.0);
		Pawn->HandleGetDown();
		if (Stroke->GetPhase() != ERbStrokePhase::GettingDown)
		{
			return Fail(TEXT("the get-down did not start"));
		}
		PostureSeconds = Rig->GetPostureChangeSeconds();
		Test->TestTrue(FString::Printf(TEXT("human get-down %.3f s in 0.8-1.5 s"), PostureSeconds), PostureSeconds >= 0.8 && PostureSeconds <= 1.5);
		Next();
		return false;
	}

	bool WaitDown(double T)
	{
		if (Stroke->GetPhase() != ERbStrokePhase::Down || Rig->IsPostureChanging())
		{
			return T > 3.0 ? Fail(TEXT("never came down on the shot")) : false;
		}
		Test->TestTrue(FString::Printf(TEXT("down and settled after %.2f s (the posture %.2f s)"), T, PostureSeconds), T >= PostureSeconds - 0.05);
		Az0 = Stroke->GetAim().Azimuth;
		Sent = 0.0;
		Next();
		return false;
	}

	bool AimTrace(bool bBack)
	{
		// F3 in the running game: 12.5 cm of mouse travel over 20 frames (minimum jerk, integer counts at 800 DPI) = 90 deg; the
		// same counts back return the azimuth bitwise to the start.
		constexpr int32 N = 20;
		const double Sign = bBack ? -1.0 : 1.0;
		const double Cm = 12.5 * RbFeelFlow::MinimumJerk(static_cast<double>(StepFrames) / N);
		const double Counts = FMath::RoundToDouble(Cm * 800.0 / 2.54);
		const double Delta = Counts - Sent;
		Sent = Counts;
		Pawn->HandleLook(FVector2D(Sign * Delta, 0.0), Dt());
		if (StepFrames < N)
		{
			return false;
		}
		const double Deg = FMath::RadiansToDegrees(FMath::UnwindRadians(Az0 - Stroke->GetAim().Azimuth));
		if (!bBack)
		{
			Test->TestTrue(FString::Printf(TEXT("F3 in play: 12.5 cm = %.4f deg (90 +- 0.5)"), Deg), FMath::Abs(Deg - 90.0) <= 0.5);
		}
		else
		{
			const double Az = Stroke->GetAim().Azimuth;
			Test->TestTrue(FString::Printf(TEXT("the same counts back: azimuth bitwise the start (%.12f / %.12f)"), Az, Az0),
				FMemory::Memcmp(&Az, &Az0, sizeof(double)) == 0);
		}
		Sent = 0.0;
		Next();
		return false;
	}

	// The Stroke button (left mouse) through the player's own input: Enhanced Input then treats it like a real button (Triggered while
	// held, its synthetic Completed on the first paused frame, Started again on resume while still held).
	void PressStrokeKey(bool bDown)
	{
		if (!Pawn->InjectStrokeKey(bDown))
		{
			Test->AddError(TEXT("the Stroke key could not be sent through the player input"));
		}
		bStrokeKeyDown = bDown;
	}

	bool PauseStart(double T)
	{
		if (StepFrames == 1)
		{
			// A committed stroke with the Stroke button held (its forward part is ~1 s away).
			Stroke->SetCommitHeld(true);
			PressStrokeKey(true);
			Stroke->InjectStrokeSamples(Stroke->MakeScriptedStroke(5.0, Stroke->GetClockNow() + 0.05));
			return false;
		}
		if (T < 0.35)
		{
			return false;
		}
		Test->TestTrue(TEXT("the stroke runs before the pause"), Stroke->IsStrokeActive());
		Test->TestTrue(TEXT("the Stroke button reached the pawn through Enhanced Input"), Stroke->IsStrokeHeld());
		PC->SetPause(true);
		Next();
		return false;
	}

	bool PauseHold()
	{
		if (StepFrames < 4)
		{
			return false;
		}
		Test->TestTrue(TEXT("the world is paused"), World->IsPaused());
		Test->TestTrue(TEXT("pausing dropped the held stroke"), !Stroke->IsStrokeActive() && Stroke->IsPausedByWorld());
		// Enhanced Input released the action on the paused frame (Completed) - that release does not count: the button is still down.
		Test->TestFalse(TEXT("the Stroke action was released by the pause"), Stroke->IsStrokeHeld());
		Test->TestTrue(TEXT("the button held through the pause must be released first"), Stroke->IsStrokeReleaseRequired());
		Test->TestEqual(TEXT("no contact while paused"), Contacts, 0);
		PC->SetPause(false);
		Next();
		return false;
	}

	bool PauseResumed(double T)
	{
		if (Sub == 0)
		{
			if (T < 1.5)
			{
				return false;
			}
			// The button stayed down through the resume: Enhanced Input started the action again, the component ignored it.
			Test->TestEqual(TEXT("no contact out of the pause"), Contacts, 0);
			Test->TestEqual(TEXT("still down on the shot"), static_cast<int32>(Stroke->GetPhase()), static_cast<int32>(ERbStrokePhase::Down));
			Test->TestFalse(TEXT("a button held through the pause does not stroke"), Stroke->IsStrokeActive());
			Test->TestTrue(TEXT("... until it is released"), Stroke->IsStrokeReleaseRequired());
			PressStrokeKey(false);
			Sub = 1;
			SubStart = T;
			return false;
		}
		if (T - SubStart < 0.2 || Stroke->IsStrokeReleaseRequired())
		{
			return T - SubStart > 3.0 ? Fail(TEXT("the released Stroke button never reached the pawn")) : false;
		}
		Stroke->SetCommitHeld(false);
		Sub = 0;
		Next();
		return false;
	}

	bool StrokeStart()
	{
		Stroke->SetCommitHeld(true);
		Pawn->HandleStroke(true);
		Stroke->InjectStrokeSamples(Stroke->MakeScriptedStroke(6.0, Stroke->GetClockNow() + 0.05));
		ReleaseAt = -1.0;
		bStayedDown = true;
		MaxGaze = 0.0;
		Next();
		return false;
	}

	bool StrokeAndWatch(double T)
	{
		const double Now = FPlatformTime::Seconds();
		// The mouse: forward (the stroke) until the contact, the follow-through with drift for 0.3 s with the button held, the
		// release with 0.8 cm of residual motion over 0.12 s, then still for 1 s.
		FVector2D Counts(0.0, 60.0);
		if (Contacts > 0)
		{
			const double Since = Now - ContactAt;
			if (ReleaseAt < 0.0 && Since >= 0.3)
			{
				ReleaseAt = Now;
				Pawn->HandleStroke(false);
				Stroke->SetCommitHeld(false);
			}
			Counts = ReleaseAt < 0.0 ? FVector2D(25.0, 80.0) : (Now - ReleaseAt < 0.12 ? FVector2D(0.0, 0.8 * 800.0 / 2.54 * Dt() / 0.12) : FVector2D::ZeroVector);
			bStayedDown &= Rig->GetMode() == ERbCameraRigMode::DownOnShot &&
				(Stroke->GetPhase() == ERbStrokePhase::Watching || Stroke->GetPhase() == ERbStrokePhase::Contact);
			MaxGaze = FMath::Max3(MaxGaze, FMath::Abs(Rig->GetGazeYaw()), FMath::Abs(Rig->GetGazePitch()));
		}
		else if (T > 3.0)
		{
			return Fail(TEXT("no contact within 3 s of the stroke"));
		}
		Pawn->HandleLook(Counts, Dt());
		if (ReleaseAt < 0.0 || Now - ReleaseAt < 0.12 + 1.0)
		{
			return false;
		}
		Test->TestEqual(TEXT("one contact"), Contacts, 1);
		Test->TestTrue(TEXT("P1: the player stays down watching (rig DownOnShot, Watching)"), bStayedDown);
		Test->TestEqual(TEXT("P1: no look input reached the head after the contact (gaze targets 0)"), MaxGaze, 0.0);
		Test->TestTrue(TEXT("the shot is playing back live"), Director->GetPhase() == ERbDirectorPhase::PlayingBack ||
			Director->GetPhase() == ERbDirectorPhase::Simulating);
		Test->TestTrue(TEXT("the rig watches the shot"), Rig->IsWatching());
		Sent = 0.0;
		Next();
		return false;
	}

	bool DeliberateLookThenStandUp(double T)
	{
		// F2: a deliberate 4 cm move within 0.3 s (paced by the clock, whatever the frame rate) opens the look (after the dead zone,
		// faded in).
		if (T < 0.3 + 0.05)
		{
			const double Counts = FMath::RoundToDouble(4.0 * RbFeelFlow::MinimumJerk(T / 0.3) * 800.0 / 2.54);
			Pawn->HandleLook(FVector2D(Counts - Sent, 0.0), Dt());
			Sent = Counts;
			return false;
		}
		if (T < 0.8)
		{
			return false;
		}
		const bool bStillDown = Rig->GetMode() == ERbCameraRigMode::DownOnShot;
		if (bStillDown)
		{
			Test->TestTrue(FString::Printf(TEXT("F2: look resumed after the deliberate move (gaze %.2f deg)"), Rig->GetGazeYaw()), Rig->GetGazeYaw() > 1.0);
			// Stand up while the balls still run: a human StandUp.
			Pawn->HandleGetDown();
			Test->TestEqual(TEXT("standing up"), static_cast<int32>(Rig->GetMode()), static_cast<int32>(ERbCameraRigMode::Standing));
			Test->TestTrue(TEXT("the stand-up is a human posture change"), Rig->IsPostureChanging());
		}
		else
		{
			Test->AddWarning(TEXT("the shot ended before the stand-up check (the next address stood the player up)"));
		}
		Next();
		return false;
	}

	bool WaitShotThenScratch(double T)
	{
		const ERbDirectorPhase Phase = Director->GetPhase();
		if (Phase == ERbDirectorPhase::AwaitDecision || Phase == ERbDirectorPhase::RackOver)
		{
			Director->Confirm();
			return false;
		}
		if (Phase != ERbDirectorPhase::AwaitStroke && Phase != ERbDirectorPhase::AwaitPlacement)
		{
			return T > 40.0 ? Fail(TEXT("the break never finished")) : false;
		}
		Test->TestFalse(TEXT("stood up after the shot"), Rig->GetMode() == ERbCameraRigMode::DownOnShot);
		// A scratch: the cue ball straight into the head-left corner, committed at once -> ball in hand anywhere (9-ball).
		Director->SetLivePlaybackRate(0.0f);
		const FRbTableContext& Context = *Director->GetTableContext();
		const rb::PocketGeometry& Corner = Context.Geometry.Pockets[static_cast<int32>(rb::PocketId::HeadLeft)];
		if (Phase == ERbDirectorPhase::AwaitPlacement)
		{
			// The break left ball in hand (a foul / a new rack): place it first (behind the head string is legal in every case).
			Director->PlaceCueBall(Corner.MouthMid - Corner.Axis * 0.25);
		}
		FRbTableState State = Director->GetTableState();
		for (rb::SimBall& Ball : State.Balls)
		{
			Ball.InPlay = false;
		}
		const auto Put = [&State, &Context](int32 Id, const rb::Vec2& P) {
			State.Balls[Id].InPlay = true;
			State.Balls[Id].State.Position = rb::Vec3(P.x, P.y, Context.BallRadius(Id));
		};
		const rb::Vec2 Cue = Corner.MouthMid - Corner.Axis * 0.25;
		Put(0, Cue);
		Put(1, rb::Vec2(-0.62, 0.06));
		Put(2, rb::Vec2(-0.30, -0.22));
		Put(9, rb::Vec2(0.45, 0.18));
		OnBall = rb::Vec2(-0.62, 0.06);
		Director->SetTableStateForTest(State);
		if (!Director->SubmitScriptedStrike(1.6, FMath::Atan2(Corner.Axis.y, Corner.Axis.x), 0.0, 0.0, -0.2))
		{
			return Fail(TEXT("the scratch strike was refused"));
		}
		Next();
		return false;
	}

	bool WaitScratchBallInHand(double T)
	{
		if (Director->GetPhase() == ERbDirectorPhase::AwaitDecision)
		{
			Director->Confirm();
			return false;
		}
		// Ball in hand, and the lean over the table has settled (the look point is set from the final eye).
		if (Director->GetPhase() != ERbDirectorPhase::AwaitPlacement || Stroke->GetPhase() != ERbStrokePhase::PlacingCueBall ||
			Hand->GetState() != ERbBallInHandState::Carrying || Rig->IsPostureChanging())
		{
			return T > 10.0 ? Fail(FString::Printf(TEXT("no ball in hand after the scratch (director %d, stroke %d, hand %d)"),
				static_cast<int32>(Director->GetPhase()), static_cast<int32>(Stroke->GetPhase()), static_cast<int32>(Hand->GetState()))) : false;
		}
		Director->SetLivePlaybackRate(1.0f);
		SetDowns = 0;
		Refusals = 0;
		FString Balls;
		for (int32 Id = 0; Id < rb::kMaxBalls; ++Id)
		{
			const rb::SimBall& Ball = Director->GetTableState().Balls[Id];
			if (Ball.InPlay)
			{
				Balls += FString::Printf(TEXT(" %d(%.3f, %.3f)"), Id, Ball.State.Position.x, Ball.State.Position.y);
			}
		}
		Test->AddInfo(FString::Printf(TEXT("after the scratch: shot %u, fouls %08x, balls on the table:%s"), Director->GetMatchShotIndex(),
			Director->GetLastShot().Fouls.Bits, *Balls));
		LookAtCore(OnBall);
		Next();
		return false;
	}

	bool RefuseOnBall(double T)
	{
		if (Sub == 0)
		{
			if (!HandArrived(T))
			{
				return T > 5.0 ? Fail(TEXT("the hand never reached the ball it should refuse: ") + HandState()) : false;
			}
			Test->TestFalse(TEXT("the spot on the 1-ball is illegal"), Hand->IsTargetLegal());
			Test->TestTrue(FString::Printf(TEXT("carried clear over the 1-ball (bottom %.2f cm > 2R)"), Hand->GetBallBottomCm()),
				Hand->GetBallBottomCm() > 200.0 * Director->GetTableContext()->BallRadius(1));
			Pawn->HandleConfirm();
			Test->TestEqual(TEXT("refused"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Refused));
			Sub = 1;
			SubStart = T;
			return false;
		}
		if (T - SubStart < 0.6)
		{
			Test->TestTrue(TEXT("the refused hand never lowers"), Hand->GetState() == ERbBallInHandState::Refused);
			return false;
		}
		Test->TestEqual(TEXT("one refusal (soft knock hook)"), Refusals, 1);
		Test->TestEqual(TEXT("nothing placed"), SetDowns, 0);
		Test->TestEqual(TEXT("still ball in hand"), static_cast<int32>(Director->GetPhase()), static_cast<int32>(ERbDirectorPhase::AwaitPlacement));
		FreeSpot = rb::Vec2(-0.40, 0.30);
		LookAtCore(FreeSpot);
		Sub = 0;
		Next();
		return false;
	}

	bool PlaceFree(double T)
	{
		if (Sub == 0)
		{
			if (!HandArrived(T))
			{
				return T > 5.0 ? Fail(TEXT("the hand never reached the free spot: ") + HandState()) : false;
			}
			Test->TestEqual(TEXT("moving on ends the refusal"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Carrying));
			Target = Hand->GetTargetCore();
			Test->TestTrue(TEXT("free spot is legal"), Hand->IsTargetLegal());
			Pawn->HandleConfirm();
			Sub = 1;
			return false;
		}
		if (Director->GetPhase() != ERbDirectorPhase::AwaitStroke)
		{
			return T > 5.0 ? Fail(TEXT("the ball in hand after the scratch was never placed")) : false;
		}
		const rb::Vec3 Cue = CueBallCore();
		const double ErrMm = 1000.0 * RbFeelFlow::Dist(rb::Vec2(Cue.x, Cue.y), Target);
		Test->TestTrue(FString::Printf(TEXT("after the scratch: placed exactly on the previewed target (%.4f mm)"), ErrMm), ErrMm < 0.1);
		Test->TestEqual(TEXT("one set-down"), SetDowns, 1);
		Test->AddInfo(FString::Printf(TEXT("feel flow: get-down %.3f s, contacts %d, refusals %d"), PostureSeconds, Contacts, Refusals));
		return true; // done
	}

	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	URbMatchDirector* Director = nullptr;
	ARbTable* Table = nullptr;
	APlayerController* PC = nullptr;
	ARbPlayerCharacter* Pawn = nullptr;
	TWeakObjectPtr<URbStrokeComponent> Stroke;
	TWeakObjectPtr<URbCameraRigComponent> Rig;
	TWeakObjectPtr<URbBallInHandComponent> Hand;
	FDelegateHandle ContactHandle;
	FDelegateHandle SetDownHandle;
	FDelegateHandle RefusedHandle;

	int32 Step = 0;
	int32 Sub = 0;
	int32 StepFrames = 0;
	double StepStart = FPlatformTime::Seconds();
	double SubStart = 0.0;
	int32 Contacts = 0;
	int32 SetDowns = 0;
	int32 Refusals = 0;
	double ContactAt = 0.0;
	double ReleaseAt = -1.0;
	double PostureSeconds = 0.0;
	double Az0 = 0.0;
	double Sent = 0.0;
	double MaxGaze = 0.0;
	bool bStayedDown = true;
	bool bStrokeKeyDown = false;
	rb::Vec2 LegalSpot;
	rb::Vec2 Target;
	rb::Vec2 OnBall;
	rb::Vec2 FreeSpot;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbFeelFlowTest, "RawBreak.Functional.Feel.FeelFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRbFeelFlowTest::RunTest(const FString& Parameters)
{
	if (!FPackageName::DoesPackageExist(RbAssetPaths::M1TestRoomMap))
	{
		AddError(FString::Printf(TEXT("%s missing: run Tools/unreal/editor/rb_make_test_room.py"), RbAssetPaths::M1TestRoomMap));
		return false;
	}
	AutomationOpenMap(RbAssetPaths::M1TestRoomMap);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRbWaitForFeelMatchCommand(this, 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FRbFeelFlowCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
