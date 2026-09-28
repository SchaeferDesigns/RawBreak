// Playback tests (UE-2, Docs/ue-architecture.md 5.5 / 13): a REAL simulated 2-ball shot (object ball pocketed, cue ball
// drawn back) played on a ball set of a translated + yawed table: transforms == FRbCoords(rb::StateAt) at 200 random times,
// cursor playback == random access bitwise, seek back, rate / pause without time jumps, anchored Play shows the state of
// now, Terminal hide, snap to the finals, every event once in order, ETeleportType::None (E3), jumps (seek, replay start,
// Stop(true)) drop the motion history while continuous frames keep it. The render-state checks need a scene, i.e. a real
// RHI: rbue.py test --filter RawBreak.Unit.Playback --render (under -NullRHI they are skipped). Owner: UE-2.

#include "Balls/RbBallSet.h"
#include "Balls/RbBallTestSupport.h"
#include "Balls/RbShotPlaybackComponent.h"
#include "Core/RbCoords.h"
#include "Cue/RbCue.h"
#include "Tests/RbTestFlags.h"

#include "Components/StaticMeshComponent.h"
#include "Math/RandomStream.h"

#include "rb/Equipment/Cue.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// World + table + ball set + the reference shot + a controllable clock.
	struct FPlaybackFixture
	{
		RbBallTest::FTestWorld TestWorld;
		RbBallTest::FTableWithBalls Scene;
		TSharedPtr<const FRbShot> Shot;
		URbShotPlaybackComponent* Playback = nullptr;
		double Now = 5000.0;

		bool Init(FAutomationTestBase& Test)
		{
			Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World);
			if (!Scene.Table || !Scene.Balls || !Scene.Table->HasContext())
			{
				Test.AddError(TEXT("could not spawn the table / ball set"));
				return false;
			}
			FString Error;
			Shot = RbBallTest::SimulateTwoBallShot(Scene.Table->GetContextPtr(), Error);
			if (!Shot.IsValid())
			{
				Test.AddError(FString::Printf(TEXT("simulation failed: %s"), *Error));
				return false;
			}
			const rb::ShotResult& R = Shot->Result;
			if (R.Finals[1].Status != rb::BallFinalStatus::Pocketed || R.Finals[0].Status != rb::BallFinalStatus::OnTable)
			{
				Test.AddError(FString::Printf(TEXT("reference shot: object ball status %d, cue ball status %d (expected pocketed / on table)"),
					static_cast<int32>(R.Finals[1].Status), static_cast<int32>(R.Finals[0].Status)));
				return false;
			}
			Playback = Scene.Balls->GetPlayback();
			Playback->SetClockSource([this]() { return Now; });
			return true;
		}

		// Replaces the reference shot with a full-rack break (islands -> Sampled segments, many events).
		bool UseBreak(FAutomationTestBase& Test)
		{
			rb::SimInput Input;
			FString Error;
			if (!ARbBallRackDemo::BuildRackInput(Scene.Table->GetContextPtr(), ERbDiscipline::EightBall, 3, true, FVector2D::ZeroVector, Input, Error))
			{
				Test.AddError(Error);
				return false;
			}
			rb::CueStrikeInput Strike;
			Strike.Cue = rb::kCueBreak21oz;
			Strike.Speed = 9.0;
			const rb::Vec2 Foot = Scene.Table->GetContext().RulesTable.FootSpot;
			Strike.Azimuth = FMath::Atan2(Foot.y - Input.Balls[0].State.Position.y, Foot.x - Input.Balls[0].State.Position.x) + 0.004;
			Strike.OffsetB = -0.1;
			Shot = ARbBallRackDemo::SimulateStrike(Scene.Table->GetContextPtr(), Input, Strike, Error);
			if (!Shot.IsValid())
			{
				Test.AddError(Error);
				return false;
			}
			return true;
		}

		const rb::ShotResult& Result() const { return Shot->Result; }
		TSharedRef<const FRbShot> ShotRef() const { return Shot.ToSharedRef(); }

		bool IsGone(int32 Ball, double T) const
		{
			const rb::BallFinal& F = Result().Finals[Ball];
			const bool bCaptured = F.Status == rb::BallFinalStatus::Pocketed || F.Status == rb::BallFinalStatus::OffTable;
			return bCaptured && T >= F.Time + static_cast<double>(Playback->DropHideDelay);
		}

		// Checks the shown state of the balls in play against random access at the playback's current time.
		// Returns the number of mismatches (bitwise core state, visibility, 1e-6 cm / exact orientation, world pose).
		int32 CheckShownState(FAutomationTestBase& Test, const TCHAR* Where) const
		{
			const double T = Playback->GetShotTime();
			int32 Errors = 0;
			for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
			{
				if (!((Result().BallsInPlay >> Ball) & 1u))
				{
					continue;
				}
				rb::BallState Expected;
				rb::Quat ExpectedQ;
				rb::BallState Shown;
				rb::Quat ShownQ;
				if (!rb::StateAt(Result(), Ball, T, Expected) || !rb::OrientationAt(Result(), Ball, T, ExpectedQ)
					|| !Playback->GetBallStateCore(Ball, Shown, ShownQ))
				{
					++Errors;
					continue;
				}
				if (!RbBallTest::SameBits(Shown, Expected) || !RbBallTest::SameBits(ShownQ, ExpectedQ))
				{
					++Errors;
					Test.AddError(FString::Printf(TEXT("%s: ball %d at t = %.9f: cursor state != random access (bitwise)"), Where, Ball, T));
				}
				const bool bGone = IsGone(Ball, T);
				if (Scene.Balls->IsBallVisible(Ball) == bGone)
				{
					++Errors;
					Test.AddError(FString::Printf(TEXT("%s: ball %d at t = %.9f: visibility %d, expected %d"), Where, Ball, T,
						Scene.Balls->IsBallVisible(Ball) ? 1 : 0, bGone ? 0 : 1));
				}
				if (bGone)
				{
					continue;
				}
				const UStaticMeshComponent* Component = Scene.Balls->GetBallComponent(Ball);
				const FVector Local = FRbCoords::PositionToUE(Expected.Position);
				const double PositionError = FVector::Dist(Component->GetRelativeLocation(), Local);
				// Component quaternion vs the adapter (q and -q are the same rotation); AngularDistance's acos cannot resolve < 3e-8.
				const FQuat ShownRotation = Component->GetRelativeTransform().GetRotation();
				const FQuat ExpectedRotation = FRbCoords::OrientationToUE(ExpectedQ).GetNormalized();
				const FQuat Delta = ExpectedRotation.Inverse() * ShownRotation;
				const double AngleError = 2.0 * FMath::Asin(FMath::Min(1.0, FVector(Delta.X, Delta.Y, Delta.Z).Size()));
				const double WorldError = FVector::Dist(Component->GetComponentLocation(), Scene.Table->CoreToWorld(Expected.Position));
				if (PositionError > 1e-6 || AngleError > 1e-9 || WorldError > 1e-6)
				{
					++Errors;
					Test.AddError(FString::Printf(TEXT("%s: ball %d at t = %.9f: position error %.3g cm, angle %.3g rad, world %.3g cm"), Where, Ball, T,
						PositionError, AngleError, WorldError));
				}
			}
			return Errors;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackRandomTimes, "RawBreak.Unit.Playback.StateAt_200RandomTimes", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackRandomTimes::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("reference shot: stop %.3f s, object ball pocketed at %.3f s, %d events, %d + %d segments"), F.Result().StopTime,
		F.Result().Finals[1].Time, static_cast<int32>(F.Result().Events.size()), static_cast<int32>(F.Result().Tracks[0].Segments.size()),
		static_cast<int32>(F.Result().Tracks[1].Segments.size())));
	F.Playback->Play(F.ShotRef(), false);
	FRandomStream Random(11);
	int32 Errors = 0;
	for (int32 I = 0; I < 200; ++I)
	{
		const double T = Random.FRandRange(0.0, static_cast<float>(F.Playback->GetFinishTime()));
		F.Playback->SeekTo(T);
		TestEqual(TEXT("seek sets the shot time"), F.Playback->GetShotTime(), FMath::Clamp(T, 0.0, F.Playback->GetFinishTime()));
		Errors += F.CheckShownState(*this, TEXT("random seek"));
	}
	TestEqual(TEXT("200 random times: shown state == rb::StateAt / OrientationAt"), Errors, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackFrames, "RawBreak.Unit.Playback.CursorFramesBitwise", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackFrames::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	int32 Finished = 0;
	F.Playback->OnFinished.AddLambda([&Finished](const TSharedRef<const FRbShot>&) { ++Finished; });
	const double Start = F.Now;
	F.Playback->Play(F.ShotRef(), false);
	FRandomStream Random(3);
	int32 Errors = F.CheckShownState(*this, TEXT("play"));
	int32 Frames = 0;
	while (F.Playback->IsPlaying() && Frames < 20000)
	{
		F.Now += Random.FRandRange(1.0f / 240.0f, 1.0f / 24.0f);
		F.Playback->Advance();
		++Frames;
		if (F.Playback->IsPlaying())
		{
			if (FMath::Abs(F.Playback->GetShotTime() - (F.Now - Start)) > 1e-9)
			{
				++Errors;
				AddError(TEXT("shot time != clock - start"));
			}
			Errors += F.CheckShownState(*this, TEXT("frame"));
		}
	}
	TestEqual(TEXT("monotone cursor == random access (bitwise) every frame"), Errors, 0);
	TestEqual(TEXT("finished once"), Finished, 1);
	TestTrue(TEXT("dozens of frames"), Frames > 30);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackBreak, "RawBreak.Unit.Playback.BreakFramesBitwise", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackBreak::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this) || !F.UseBreak(*this))
	{
		return false;
	}
	const rb::ShotResult& R = F.Result();
	int32 Sampled = 0;
	int32 Pocketed = 0;
	for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
	{
		for (const rb::TrajectorySegment& Segment : R.Tracks[Ball].Segments)
		{
			Sampled += Segment.Kind == rb::SegmentKind::Sampled ? 1 : 0;
		}
		Pocketed += R.Finals[Ball].Status == rb::BallFinalStatus::Pocketed ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("break: stop %.3f s, %d events, %d sampled segments, %d pocketed, sim %.2f ms"), R.StopTime,
		static_cast<int32>(R.Events.size()), Sampled, Pocketed, F.Shot->SimMilliseconds));
	int32 InPlay = 0;
	for (int32 Ball = 0; Ball < rb::kMaxBalls; ++Ball)
	{
		InPlay += static_cast<int32>((R.BallsInPlay >> Ball) & 1u);
	}
	TestEqual(TEXT("16 balls in the break"), InPlay, 16);
	int32 Fired = 0;
	F.Playback->OnShotEvent.AddLambda([&Fired](const TSharedRef<const FRbShot>&, int32) { ++Fired; });
	F.Playback->Play(F.ShotRef(), false);
	int32 Errors = F.CheckShownState(*this, TEXT("break play"));
	int32 Frames = 0;
	while (F.Playback->IsPlaying() && Frames++ < 100000)
	{
		F.Now += 1.0 / 60.0;
		F.Playback->Advance();
		if (F.Playback->IsPlaying())
		{
			Errors += F.CheckShownState(*this, TEXT("break frame"));
		}
	}
	TestEqual(TEXT("every ball, every 60 fps frame: shown == random access (bitwise)"), Errors, 0);
	TestEqual(TEXT("every event fired"), Fired, static_cast<int32>(R.Events.size()));
	for (int32 Ball = 0; Ball < 16; ++Ball)
	{
		TestEqual(FString::Printf(TEXT("ball %d shown iff on the table at the end"), Ball), F.Scene.Balls->IsBallVisible(Ball),
			R.Finals[Ball].Status == rb::BallFinalStatus::OnTable);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackSeekBack, "RawBreak.Unit.Playback.SeekBack", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackSeekBack::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	TArray<int32> Fired;
	F.Playback->OnShotEvent.AddLambda([&Fired](const TSharedRef<const FRbShot>&, int32 Index) { Fired.Add(Index); });
	F.Playback->Play(F.ShotRef(), false);
	for (int32 I = 0; I < 72; ++I)
	{
		F.Now += 1.0 / 60.0;
		F.Playback->Advance();
	}
	TestTrue(TEXT("played past 1 s"), F.Playback->GetShotTime() > 1.0);
	const int32 FiredBefore = Fired.Num();
	F.Playback->SeekTo(0.4);
	TestEqual(TEXT("seek back"), F.Playback->GetShotTime(), 0.4);
	TestEqual(TEXT("seek back shows t = 0.4 (bitwise)"), F.CheckShownState(*this, TEXT("seek back")), 0);
	TestEqual(TEXT("a seek fires nothing"), Fired.Num(), FiredBefore);
	const std::vector<rb::ShotEvent>& Events = F.Result().Events;
	int32 FirstAfter = 0;
	while (FirstAfter < static_cast<int32>(Events.size()) && Events[FirstAfter].Time < 0.4)
	{
		++FirstAfter;
	}
	TestEqual(TEXT("next event = first at / after the seek time"), F.Playback->GetNextEventIndex(), FirstAfter);
	F.Now += 0.25;
	F.Playback->Advance();
	TestNearlyEqual(TEXT("continues from the seek time"), F.Playback->GetShotTime(), 0.65, 1e-12);
	TestEqual(TEXT("after the seek"), F.CheckShownState(*this, TEXT("after seek")), 0);
	for (int32 I = FiredBefore; I < Fired.Num(); ++I)
	{
		TestTrue(TEXT("re-fired events lie in [0.4, 0.65]"), Events[Fired[I]].Time >= 0.4 && Events[Fired[I]].Time <= 0.65);
	}
	// Seeking to 0 shows the start state (the input) exactly.
	F.Playback->SeekTo(0.0);
	rb::BallState S;
	rb::Quat Q;
	F.Playback->GetBallStateCore(1, S, Q);
	TestTrue(TEXT("t = 0: object ball at its input position"), RbBallTest::SameBits(S.Position, F.Shot->Request.Input.Balls[1].State.Position));
	TestTrue(TEXT("t = 0: object ball input orientation"), RbBallTest::SameBits(Q, F.Shot->Request.Input.Balls[1].Orientation));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackRatePause, "RawBreak.Unit.Playback.RatePauseNoJump", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackRatePause::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	URbShotPlaybackComponent* P = F.Playback;
	UStaticMeshComponent* CueBall = F.Scene.Balls->GetBallComponent(0);
	P->Play(F.ShotRef(), false);
	F.Now += 0.2;
	P->Advance();
	TestNearlyEqual(TEXT("rate 1"), P->GetShotTime(), 0.2, 1e-12);

	// Slow motion: no jump at the switch, then a quarter of the clock.
	FVector Before = CueBall->GetRelativeLocation();
	P->SetRate(0.25f);
	P->Advance();
	TestNearlyEqual(TEXT("SetRate does not move the time"), P->GetShotTime(), 0.2, 1e-12);
	TestTrue(TEXT("SetRate does not move the ball"), CueBall->GetRelativeLocation().Equals(Before, 1e-9));
	F.Now += 0.4;
	P->Advance();
	TestNearlyEqual(TEXT("rate 0.25"), P->GetShotTime(), 0.3, 1e-12);

	// Pause: the clock runs on, the shot does not; resume continues without a jump.
	P->SetPaused(true);
	TestTrue(TEXT("paused"), P->IsPaused());
	Before = CueBall->GetRelativeLocation();
	F.Now += 5.0;
	P->Advance();
	P->TickComponent(10.0f, LEVELTICK_All, nullptr); // a (dilated) world delta time is ignored
	TestNearlyEqual(TEXT("paused time frozen"), P->GetShotTime(), 0.3, 1e-12);
	TestTrue(TEXT("paused ball frozen"), CueBall->GetRelativeLocation().Equals(Before, 0.0));
	P->SetPaused(false);
	P->Advance();
	TestNearlyEqual(TEXT("resume without a jump"), P->GetShotTime(), 0.3, 1e-12);
	F.Now += 0.2;
	P->Advance();
	TestNearlyEqual(TEXT("resumed at rate 0.25"), P->GetShotTime(), 0.35, 1e-12);

	// Fast forward, rate changes while paused, and the clock is the only time source (not the world's delta time).
	P->SetRate(2.0f);
	F.Now += 0.1;
	P->TickComponent(0.0f, LEVELTICK_All, nullptr);
	TestNearlyEqual(TEXT("rate 2"), P->GetShotTime(), 0.55, 1e-12);
	P->SetPaused(true);
	P->SetRate(1.0f);
	F.Now += 1.0;
	P->SetPaused(false);
	F.Now += 0.05;
	P->Advance();
	TestNearlyEqual(TEXT("rate set while paused applies after resume"), P->GetShotTime(), 0.6, 1e-12);
	P->SetRate(-3.0f);
	TestEqual(TEXT("negative rate clamps to 0"), P->GetRate(), 0.0f);
	F.Now += 1.0;
	P->Advance();
	TestNearlyEqual(TEXT("rate 0 holds"), P->GetShotTime(), 0.6, 1e-12);
	TestEqual(TEXT("state after all switches"), F.CheckShownState(*this, TEXT("rate/pause")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackAnchored, "RawBreak.Unit.Playback.AnchoredPlayShowsNow", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackAnchored::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	const TSharedRef<FRbShot> Live = MakeShared<FRbShot>(*F.Shot);
	Live->Request.ContactTime = 1000.0;
	TArray<int32> Fired;
	F.Playback->OnShotEvent.AddLambda([&Fired](const TSharedRef<const FRbShot>&, int32 Index) { Fired.Add(Index); });
	int32 Finished = 0;
	F.Playback->OnFinished.AddLambda([&Finished](const TSharedRef<const FRbShot>&) { ++Finished; });

	// The hand-off came a quarter second after the tip contact: Play already shows t = 0.25.
	F.Now = 1000.25;
	F.Playback->Play(Live, true);
	TestEqual(TEXT("anchored at the contact"), F.Playback->GetShotTime(), 0.25);
	TestEqual(TEXT("state of now"), F.CheckShownState(*this, TEXT("anchored play")), 0);
	const std::vector<rb::ShotEvent>& Events = Live->Result.Events;
	int32 Due = 0;
	while (Due < static_cast<int32>(Events.size()) && Events[Due].Time <= 0.25)
	{
		++Due;
	}
	TestEqual(TEXT("events up to now fired by Play"), Fired.Num(), Due);
	TestTrue(TEXT("the strike event is among them"), Due > 0 && Events[0].Type == rb::ShotEventType::CueStrike);
	F.Now += 0.1;
	F.Playback->Advance();
	TestNearlyEqual(TEXT("then runs with the clock"), F.Playback->GetShotTime(), 0.35, 1e-12);

	// Slow-motion live playback: the anchor scales with the rate.
	F.Now = 1000.25;
	F.Playback->Play(Live, true, 0.0, 0.5f);
	TestEqual(TEXT("anchored at rate 0.5"), F.Playback->GetShotTime(), 0.125);
	// A contact time slightly in the future (hand-off stamped before the frame clock) clamps to t = 0.
	F.Now = 999.99;
	F.Playback->Play(Live, true);
	TestEqual(TEXT("future contact clamps to 0"), F.Playback->GetShotTime(), 0.0);
	// A hand-off long after the end: Play shows the end but never finishes inside Play (the caller sets its state first).
	F.Now = 1100.0;
	F.Playback->Play(Live, true);
	TestEqual(TEXT("clamped to the finish time"), F.Playback->GetShotTime(), F.Playback->GetFinishTime());
	TestEqual(TEXT("no OnFinished from inside Play"), Finished, 0);
	TestTrue(TEXT("still playing"), F.Playback->IsPlaying());
	F.Playback->Advance();
	TestEqual(TEXT("finished on the next tick"), Finished, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackTerminal, "RawBreak.Unit.Playback.TerminalHide", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackTerminal::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	const rb::BallFinal& Final = F.Result().Finals[1];
	const double Capture = Final.Time;
	const double Delay = F.Playback->DropHideDelay;
	F.Playback->Play(F.ShotRef(), false);

	F.Playback->SeekTo(Capture - 0.02);
	TestTrue(TEXT("visible before the capture"), F.Scene.Balls->IsBallVisible(1));
	F.Playback->SeekTo(Capture + 0.5 * Delay);
	TestTrue(TEXT("visible during the drop delay"), F.Scene.Balls->IsBallVisible(1));
	rb::BallState S;
	rb::Quat Q;
	F.Playback->GetBallStateCore(1, S, Q);
	TestTrue(TEXT("frozen at the capture point (Terminal segment)"), RbBallTest::SameBits(S.Position, Final.State.Position));
	TestTrue(TEXT("component at the capture point"), F.Scene.Balls->GetBallComponent(1)->GetRelativeLocation() == FRbCoords::PositionToUE(Final.State.Position));
	TestTrue(TEXT("below the cloth (captured)"), Final.State.Position.z < 0.0);
	F.Playback->SeekTo(Capture + Delay + 1e-6);
	TestFalse(TEXT("hidden after the drop delay"), F.Scene.Balls->IsBallVisible(1));
	TestTrue(TEXT("the cue ball stays"), F.Scene.Balls->IsBallVisible(0));
	F.Playback->SeekTo(Capture - 0.02);
	TestTrue(TEXT("visible again after seeking back"), F.Scene.Balls->IsBallVisible(1));
	for (int32 Ball = 2; Ball < F.Scene.Balls->GetBallCount(); ++Ball)
	{
		TestFalse(TEXT("balls outside the shot hidden"), F.Scene.Balls->IsBallVisible(Ball));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackFinals, "RawBreak.Unit.Playback.FinishSnapsToFinals", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackFinals::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	const rb::ShotResult& R = F.Result();
	const double Delay = F.Playback->DropHideDelay;
	const double Finish = URbShotPlaybackComponent::ComputeFinishTime(*F.Shot, Delay);
	TestTrue(TEXT("finish >= stop time"), Finish >= R.StopTime);
	TestTrue(TEXT("finish >= capture + drop delay"), Finish >= R.Finals[1].Time + Delay);
	TestTrue(TEXT("finish = max(stop, capture + delay, tip end)"),
		Finish == R.StopTime || Finish == R.Finals[1].Time + Delay || (R.CueTips.size() > 0 && Finish == R.CueTips.back().T1));

	int32 Finished = 0;
	const FRbShot* FinishedShot = nullptr;
	bool bStillPlayingInCallback = true;
	F.Playback->OnFinished.AddLambda([&](const TSharedRef<const FRbShot>& Done)
	{
		++Finished;
		FinishedShot = &Done.Get();
		bStillPlayingInCallback = F.Playback->IsPlaying();
	});
	F.Playback->Play(F.ShotRef(), false);
	TestEqual(TEXT("finish time"), F.Playback->GetFinishTime(), Finish);
	int32 Frames = 0;
	while (F.Playback->IsPlaying() && Frames++ < 10000)
	{
		F.Now += 1.0 / 60.0;
		F.Playback->Advance();
	}
	TestEqual(TEXT("OnFinished once"), Finished, 1);
	TestTrue(TEXT("OnFinished carries the shot"), FinishedShot == F.Shot.Get());
	TestFalse(TEXT("not playing any more inside OnFinished (a listener may start the next playback)"), bStillPlayingInCallback);
	TestEqual(TEXT("shot time at the end"), F.Playback->GetShotTime(), Finish);

	auto CheckFinals = [&](const TCHAR* Where)
	{
		UStaticMeshComponent* CueBall = F.Scene.Balls->GetBallComponent(0);
		TestTrue(*FString::Printf(TEXT("%s: cue ball at its final position"), Where),
			CueBall->GetRelativeLocation() == FRbCoords::PositionToUE(R.Finals[0].State.Position));
		TestTrue(*FString::Printf(TEXT("%s: cue ball final orientation"), Where),
			CueBall->GetRelativeTransform().GetRotation().Equals(FRbCoords::OrientationToUE(R.Finals[0].Orientation).GetNormalized(), 1e-15));
		TestTrue(*FString::Printf(TEXT("%s: cue ball shown"), Where), F.Scene.Balls->IsBallVisible(0));
		TestFalse(*FString::Printf(TEXT("%s: pocketed ball hidden"), Where), F.Scene.Balls->IsBallVisible(1));
		rb::BallState S;
		rb::Quat Q;
		TestTrue(*FString::Printf(TEXT("%s: final core state"), Where),
			F.Playback->GetBallStateCore(0, S, Q) && RbBallTest::SameBits(S, R.Finals[0].State) && RbBallTest::SameBits(Q, R.Finals[0].Orientation));
	};
	CheckFinals(TEXT("finished"));

	// Stop(true) snaps too, and never broadcasts OnFinished.
	F.Playback->Play(F.ShotRef(), false);
	F.Playback->SeekTo(0.3);
	F.Playback->Stop(true);
	TestFalse(TEXT("stopped"), F.Playback->IsPlaying());
	TestEqual(TEXT("Stop does not broadcast"), Finished, 1);
	CheckFinals(TEXT("Stop(true)"));
	// Stop(false) leaves the current pose.
	F.Playback->Play(F.ShotRef(), false);
	F.Playback->SeekTo(0.3);
	const FVector At03 = F.Scene.Balls->GetBallComponent(0)->GetRelativeLocation();
	F.Playback->Stop(false);
	TestTrue(TEXT("Stop(false) keeps the pose"), F.Scene.Balls->GetBallComponent(0)->GetRelativeLocation() == At03);
	F.Playback->Advance();
	TestEqual(TEXT("a stopped playback never finishes"), Finished, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackEvents, "RawBreak.Unit.Playback.EventsOnceInOrder", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackEvents::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	const std::vector<rb::ShotEvent>& Events = F.Result().Events;
	TArray<int32> Fired;
	int32 Late = 0;
	F.Playback->OnShotEvent.AddLambda([&](const TSharedRef<const FRbShot>& Shot, int32 Index)
	{
		Fired.Add(Index);
		Late += (&Shot.Get() != F.Shot.Get() || Events[Index].Time > F.Playback->GetShotTime()) ? 1 : 0;
	});
	F.Playback->Play(F.ShotRef(), false);
	FRandomStream Random(5);
	int32 Frames = 0;
	while (F.Playback->IsPlaying() && Frames++ < 20000)
	{
		F.Now += Random.FRandRange(1.0f / 200.0f, 1.0f / 20.0f);
		if (Frames == 20)
		{
			F.Playback->SetPaused(true);
		}
		if (Frames == 30)
		{
			F.Playback->SetPaused(false);
		}
		if (Frames == 40)
		{
			F.Playback->SetRate(0.3f);
		}
		if (Frames == 90)
		{
			F.Playback->SetRate(3.0f);
		}
		F.Playback->Advance();
	}
	TestTrue(TEXT("the shot has events"), Events.size() > 5);
	bool bInOrder = Fired.Num() == static_cast<int32>(Events.size());
	for (int32 I = 0; bInOrder && I < Fired.Num(); ++I)
	{
		bInOrder = Fired[I] == I;
	}
	TestTrue(FString::Printf(TEXT("every event exactly once, in log order (%d of %d fired)"), Fired.Num(), static_cast<int32>(Events.size())), bInOrder);
	TestEqual(TEXT("no event fired before its time"), Late, 0);

	// A replay from t = 0.5 fires exactly the events at or after 0.5.
	Fired.Reset();
	F.Playback->Play(F.ShotRef(), false, 0.5, 4.0f);
	while (F.Playback->IsPlaying() && Frames++ < 40000)
	{
		F.Now += 1.0 / 60.0;
		F.Playback->Advance();
	}
	int32 Expected = 0;
	for (const rb::ShotEvent& E : Events)
	{
		Expected += E.Time >= 0.5 ? 1 : 0;
	}
	TestEqual(TEXT("replay from 0.5 fires the later events"), Fired.Num(), Expected);
	TestTrue(TEXT("first replayed event at or after 0.5"), Fired.Num() == 0 || Events[Fired[0]].Time >= 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackCue, "RawBreak.Unit.Playback.CueFollowsTipPath", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackCue::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	ARbCue* Cue = F.TestWorld.World->SpawnActor<ARbCue>();
	if (!TestNotNull(TEXT("cue"), Cue))
	{
		return false;
	}
	Cue->InitForTable(F.Scene.Table, rb::kCuePlaying19oz, rb::human::CueBodyState{});
	Cue->SetDrive(ERbCueDrive::Hidden);
	F.Playback->SetCue(Cue);
	F.Playback->Play(F.ShotRef(), false);
	TestTrue(TEXT("Play hands the cue to the tip path"), Cue->GetDrive() == ERbCueDrive::Playback);
	const rb::ShotResult& R = F.Result();
	TestTrue(TEXT("the shot recorded a tip path"), R.CueTips.size() > 0);
	int32 Errors = 0;
	for (int32 Frame = 0; Frame < 30 && F.Playback->IsPlaying(); ++Frame)
	{
		rb::Vec3 Tip;
		rb::Vec3 Direction;
		rb::CueTipAt(R, 0, F.Playback->GetShotTime(), Tip, Direction);
		// ARbCue (UE-4) uses the engine setters, which ignore moves below 1e-4 cm: compare to 1e-3 cm.
		Errors += Cue->GetRootComponent()->GetRelativeLocation().Equals(FRbCoords::PositionToUE(Tip), 1e-3) ? 0 : 1;
		Errors += Cue->GetActorForwardVector().Equals(F.Scene.Table->CoreDirectionToWorld(Direction), 1e-6) ? 0 : 1;
		F.Now += 1.0 / 120.0;
		F.Playback->Advance();
	}
	TestEqual(TEXT("cue pose == rb::CueTipAt (tip dome centre, direction)"), Errors, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackNoTeleport, "RawBreak.Unit.Playback.E3_NoTeleport", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackNoTeleport::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	int32 Updates = 0;
	int32 Teleports = 0;
	TArray<FDelegateHandle> Handles;
	for (int32 Ball = 0; Ball < F.Scene.Balls->GetBallCount(); ++Ball)
	{
		Handles.Add(F.Scene.Balls->GetBallComponent(Ball)->TransformUpdated.AddLambda(
			[&Updates, &Teleports](USceneComponent*, EUpdateTransformFlags, ETeleportType Teleport)
			{
				++Updates;
				Teleports += Teleport != ETeleportType::None ? 1 : 0;
			}));
	}
	// Largest speed of the shot [cm/s] bounds the per-frame displacement: no jumps between frames.
	double MaxSpeed = 0.0;
	for (int32 Ball = 0; Ball < 2; ++Ball)
	{
		for (double T = 0.0; T <= F.Result().StopTime; T += 1e-3)
		{
			rb::BallState S;
			rb::StateAt(F.Result(), Ball, T, S);
			MaxSpeed = FMath::Max(MaxSpeed, 100.0 * rb::Length(S.Velocity));
		}
	}
	// The live table shows the shot's start state (as the director's FRbTableState does), then the shot plays.
	UWorld* World = F.TestWorld.World;
	UStaticMeshComponent* CueBall = F.Scene.Balls->GetBallComponent(0);
	F.Scene.Balls->ShowSimBalls(F.Shot->Request.Input.Balls, rb::kMaxBalls);
	World->SendAllEndOfFrameUpdates();
	const bool bHasRenderState = CueBall->IsRenderStateCreated();
	F.Playback->Play(F.ShotRef(), false);
	FVector Previous[2] = {F.Scene.Balls->GetBallComponent(0)->GetRelativeLocation(), F.Scene.Balls->GetBallComponent(1)->GetRelativeLocation()};
	int32 Jumps = 0;
	int32 Moves = 0;
	int32 HistoryResets = CueBall->IsRenderStateDirty() ? 1 : 0;
	const double Dt = 1.0 / 60.0;
	while (F.Playback->IsPlaying())
	{
		World->SendAllEndOfFrameUpdates(); // end of the previous frame: render proxies updated, dirty flags cleared
		F.Now += Dt;
		F.Playback->Advance();
		HistoryResets += CueBall->IsRenderStateDirty() ? 1 : 0; // the cue ball stays on the table: never re-created
		for (int32 Ball = 0; Ball < 2; ++Ball)
		{
			const FVector Location = F.Scene.Balls->GetBallComponent(Ball)->GetRelativeLocation();
			const double Step = FVector::Dist(Location, Previous[Ball]);
			Moves += Step > 0.0 ? 1 : 0;
			Jumps += Step > 1.05 * MaxSpeed * Dt + 1e-6 ? 1 : 0;
			Previous[Ball] = Location;
		}
	}
	for (int32 Ball = 0; Ball < F.Scene.Balls->GetBallCount(); ++Ball)
	{
		F.Scene.Balls->GetBallComponent(Ball)->TransformUpdated.Remove(Handles[Ball]);
	}
	AddInfo(FString::Printf(TEXT("%d transform updates, max speed %.1f cm/s, render state %s"), Updates, MaxSpeed,
		bHasRenderState ? TEXT("created") : TEXT("not created (no scene)")));
	TestTrue(TEXT("the balls moved"), Moves > 20 && Updates > 20);
	TestEqual(TEXT("no teleporting transform update (ETeleportType::None)"), Teleports, 0);
	TestEqual(TEXT("no displacement larger than max speed x frame time"), Jumps, 0);
	TestEqual(TEXT("continuous playback keeps the motion history (no render-state re-creation)"), HistoryResets, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbPlaybackJumps, "RawBreak.Unit.Playback.JumpsDropMotionHistory", RB_UNIT_TEST_FLAGS)
bool FRbPlaybackJumps::RunTest(const FString& Parameters)
{
	FPlaybackFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	// Discontinuous re-placements (seek, replay start from another table state, Stop(true)) re-create the ball's render
	// proxy (no previous transform -> no velocity streak in motion blur / TSR); continuous ones never do.
	UWorld* World = F.TestWorld.World;
	URbShotPlaybackComponent* P = F.Playback;
	UStaticMeshComponent* CueBall = F.Scene.Balls->GetBallComponent(0);
	F.Scene.Balls->ShowSimBalls(F.Shot->Request.Input.Balls, rb::kMaxBalls);
	World->SendAllEndOfFrameUpdates();
	if (!CueBall->IsRenderStateCreated())
	{
		// -NullRHI (FApp::CanEverRender() false): worlds have no scene. Checked by: rbue.py test --filter RawBreak.Unit.Playback --render
		AddInfo(TEXT("no render scene under -NullRHI: render-state checks skipped (run with --render)"));
		return true;
	}
	TestFalse(TEXT("flushed"), CueBall->IsRenderStateDirty());

	P->Play(F.ShotRef(), false);
	TestFalse(TEXT("Play from the shown start state keeps the history"), CueBall->IsRenderStateDirty());
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		World->SendAllEndOfFrameUpdates();
		F.Now += 1.0 / 60.0;
		P->Advance();
		TestFalse(TEXT("frames keep the history"), CueBall->IsRenderStateDirty());
	}
	World->SendAllEndOfFrameUpdates();
	P->SeekTo(0.05);
	TestTrue(TEXT("a seek drops the history of a ball that jumps"), CueBall->IsRenderStateDirty());
	World->SendAllEndOfFrameUpdates();
	P->SeekTo(0.05);
	TestFalse(TEXT("a seek to the shown time moves nothing"), CueBall->IsRenderStateDirty());
	P->SetPaused(true);
	P->SetRate(0.25f);
	P->SetPaused(false);
	F.Now += 1.0 / 60.0;
	P->Advance();
	TestFalse(TEXT("pause / rate changes are continuous"), CueBall->IsRenderStateDirty());

	// Stop(true) from mid-shot snaps to the finals: a jump.
	World->SendAllEndOfFrameUpdates();
	P->Stop(true);
	TestTrue(TEXT("Stop(true) mid-shot drops the history"), CueBall->IsRenderStateDirty());

	// A replay started while the table shows the end state jumps back to the start.
	World->SendAllEndOfFrameUpdates();
	P->Play(F.ShotRef(), false);
	TestTrue(TEXT("a replay started from another table state drops the history"), CueBall->IsRenderStateDirty());

	// A live (anchored) shot one frame after the contact, from the shown start state: continuous.
	World->SendAllEndOfFrameUpdates();
	P->Stop(false);
	F.Scene.Balls->ShowSimBalls(F.Shot->Request.Input.Balls, rb::kMaxBalls);
	World->SendAllEndOfFrameUpdates();
	const TSharedRef<FRbShot> Live = MakeShared<FRbShot>(*F.Shot);
	Live->Request.ContactTime = F.Now - 1.0 / 60.0;
	const FVector Before = CueBall->GetRelativeLocation();
	P->Play(Live, true);
	TestFalse(TEXT("the live shot's first frame moved the cue ball"), CueBall->GetRelativeLocation().Equals(Before, 0.0));
	TestFalse(TEXT("a live shot keeps the history (motion vectors in its first frame)"), CueBall->IsRenderStateDirty());

	// ShowSimBalls: re-showing the same state is continuous, a moved ball jumps.
	P->Stop(false);
	World->SendAllEndOfFrameUpdates();
	F.Scene.Balls->ShowSimBalls(F.Shot->Request.Input.Balls, rb::kMaxBalls);
	World->SendAllEndOfFrameUpdates();
	F.Scene.Balls->ShowSimBalls(F.Shot->Request.Input.Balls, rb::kMaxBalls);
	TestFalse(TEXT("ShowSimBalls of the shown state keeps the history"), CueBall->IsRenderStateDirty());
	rb::SimInput Moved = F.Shot->Request.Input;
	Moved.Balls[0].State.Position.x += 0.2;
	F.Scene.Balls->ShowSimBalls(Moved.Balls, rb::kMaxBalls);
	TestTrue(TEXT("ShowSimBalls of a moved ball drops its history"), CueBall->IsRenderStateDirty());
	TestFalse(TEXT("... and only its own"), F.Scene.Balls->GetBallComponent(1)->IsRenderStateDirty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
