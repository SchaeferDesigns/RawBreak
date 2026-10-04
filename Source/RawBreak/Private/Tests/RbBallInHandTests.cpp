// M2-F P2 acceptance, the diegetic ball in hand (Docs/ue-architecture.md 18.3, F5): carry across the bed, fine adjust, confirm ->
// the placed position == the previewed target within 0.1 mm; the ball never below HoverHeight while carried; an illegal target ->
// Refused, no set-down; the hand never jumps (per-frame step bound); the target is clamped to the reachable bed; over another ball
// the carried one is lifted clear of it, eased; the table's ball is hidden while carried and shown again when the carry is
// cancelled; a Confirm while the lagging hand is still behind a ball next to the target lines the ball up over that ball (never
// through it) and still sets it down on exactly the confirmed target, which a look change during the lowering does not move
// (review); a Confirm while the hand is still far away lines the ball up at the hand's speed limit, never faster (review). On a
// translated + yawed 9-ft table. Owner: M2-F.

#include "Balls/RbBallTestSupport.h"
#include "Core/RbCoords.h"
#include "Player/RbBallInHandComponent.h"
#include "Simulation/RbTableContext.h"
#include "Table/RbTable.h"
#include "Tests/RbTestFlags.h"

#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RbBallInHandTests
{
	constexpr double kR = 0.028575;

	double Dist(const rb::Vec2& A, const rb::Vec2& B)
	{
		return FMath::Sqrt((A.x - B.x) * (A.x - B.x) + (A.y - B.y) * (A.y - B.y));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbBallInHandF5, "RawBreak.Unit.BallInHand.F5_CarryFineConfirm", RB_UNIT_TEST_FLAGS)
bool FRbBallInHandF5::RunTest(const FString& Parameters)
{
	using namespace RbBallInHandTests;
	RbBallTest::FTestWorld TestWorld;
	const RbBallTest::FTableWithBalls Scene = RbBallTest::SpawnTableWithBalls(TestWorld.World);
	if (!TestTrue(TEXT("table with balls"), Scene.Table && Scene.Table->HasContext() && Scene.Balls))
	{
		return false;
	}
	ARbTable* Table = Scene.Table;
	const rb::rules::RulesTable& Rules = Table->GetContext().RulesTable;
	// The director's rule for this test: behind the head string (the kitchen) and clear of a ball at (-0.9, 0.2).
	const rb::Vec2 Obstacle(-0.9, 0.2);
	const double HeadString = Rules.HeadStringX;
	const auto Legal = [HeadString, Obstacle](const rb::Vec2& P) { return P.x < HeadString && Dist(P, Obstacle) > 2.0 * kR; };

	URbBallInHandComponent* Hand = NewObject<URbBallInHandComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	Hand->AddToRoot();
	int32 SetDowns = 0;
	int32 Refusals = 0;
	rb::Vec2 SetDownAt;
	Hand->OnSetDown.AddLambda([&](const rb::Vec2& P) { ++SetDowns; SetDownAt = P; });
	Hand->OnRefused.AddLambda([&](const rb::Vec2&) { ++Refusals; });

	// The cue ball was on the table: picking it up hides the table's instance.
	Scene.Balls->SetBallVisible(0, true);
	Hand->BeginCarry(Table, 0, kR, Legal);
	TestEqual(TEXT("carrying"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Carrying));
	TestFalse(TEXT("the table's cue ball is in the hand"), Scene.Balls->IsBallVisible(0));

	// Carry across the kitchen for 1 s at 60 fps: the look point sweeps, the hand follows - never a jump, never below the hover height.
	const double Dt = 1.0 / 60.0;
	const double MaxStep = Hand->MaxHandSpeed * Dt + 3.0 * 0.001 * Hand->TremorMm; // speed limit + tremor
	rb::Vec2 Last;
	bool bHasLast = false;
	double WorstStep = 0.0;
	double LowestBottom = 1e9;
	for (int32 I = 0; I <= 120; ++I)
	{
		const double A = FMath::Min(1.0, I / 60.0);
		Hand->SetTargetCore(rb::Vec2(-1.05 + 0.25 * A, -0.40 + 0.35 * A));
		Hand->TickCarry(Dt);
		const rb::Vec2 Shown = Hand->GetHandPlanCore();
		if (bHasLast)
		{
			WorstStep = FMath::Max(WorstStep, Dist(Shown, Last));
		}
		Last = Shown;
		bHasLast = true;
		LowestBottom = FMath::Min(LowestBottom, Hand->GetBallBottomCm());
	}
	AddInfo(FString::Printf(TEXT("carry: worst per-frame step %.2f mm (bound %.2f mm), lowest ball bottom %.2f cm"), 1000.0 * WorstStep, 1000.0 * MaxStep, LowestBottom));
	TestTrue(TEXT("the hand never jumps (per-frame step bound)"), WorstStep <= MaxStep);
	TestTrue(TEXT("the ball never below the hover height while carried"), LowestBottom >= Hand->HoverHeightCm - 1e-9);
	TestTrue(TEXT("the hand followed the look point"), Dist(Hand->GetHandPlanCore(), Hand->GetTargetCore()) < 0.003);

	// Fine adjustment: 1 cm of mouse travel to the right nudges the target FineMetersPerCm (in the view frame; here world right).
	const rb::Vec2 BeforeFine = Hand->GetTargetCore();
	Hand->AddFineAdjustCm(FVector2D(1.0, 0.0));
	const rb::Vec2 AfterFine = Hand->GetTargetCore();
	TestEqual(TEXT("fine adjustment: FineMetersPerCm per cm"), Dist(AfterFine, BeforeFine), static_cast<double>(Hand->FineMetersPerCm), 1e-9);
	const FVector WorldMove = Table->CoreToWorld(rb::Vec3(AfterFine.x, AfterFine.y, kR)) - Table->CoreToWorld(rb::Vec3(BeforeFine.x, BeforeFine.y, kR));
	TestTrue(TEXT("fine adjustment: to the right"), FVector::DotProduct(WorldMove.GetSafeNormal(), FVector::RightVector) > 0.999);
	for (int32 I = 0; I < 60; ++I)
	{
		Hand->TickCarry(Dt);
		TestTrue(TEXT("hover while fine adjusting"), Hand->GetBallBottomCm() >= Hand->HoverHeightCm - 1e-9);
	}

	// Confirm at a legal spot: lowered in ~0.25 s onto EXACTLY the previewed target; set down only when it touches the cloth.
	const rb::Vec2 Previewed = Hand->GetTargetCore();
	TestTrue(TEXT("legal target"), Hand->IsTargetLegal());
	TestTrue(TEXT("confirm lowers"), Hand->RequestSetDown());
	TestEqual(TEXT("lowering"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Lowering));
	int32 Frames = 0;
	double LastBottom = Hand->GetBallBottomCm();
	bool bMonotoneDown = true;
	while (Hand->GetState() == ERbBallInHandState::Lowering && Frames < 120)
	{
		TestEqual(TEXT("no set-down before the touch"), SetDowns, 0);
		Hand->TickCarry(Dt);
		bMonotoneDown &= Hand->GetBallBottomCm() <= LastBottom + 1e-9;
		LastBottom = Hand->GetBallBottomCm();
		++Frames;
	}
	TestEqual(TEXT("one set-down"), SetDowns, 1);
	TestTrue(FString::Printf(TEXT("lowered in %.3f s (~0.25 s)"), Frames * Dt), Frames * Dt >= 0.2 && Frames * Dt <= 0.3);
	TestTrue(TEXT("the ball only goes down while lowering"), bMonotoneDown);
	TestTrue(TEXT("placed == previewed target (bitwise)"), FMemory::Memcmp(&SetDownAt, &Previewed, sizeof(rb::Vec2)) == 0);
	const FVector Shown = Hand->GetBallWorld();
	const FVector Expected = Table->CoreToWorld(rb::Vec3(Previewed.x, Previewed.y, kR));
	TestTrue(FString::Printf(TEXT("the shown ball touches the cloth on the target (%.4f mm)"), 10.0 * FVector::Dist(Shown, Expected)),
		FVector::Dist(Shown, Expected) < 0.01);
	TestEqual(TEXT("placed"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Placed));
	Hand->TickCarry(0.5);
	TestEqual(TEXT("the hand lets go"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Inactive));

	// Illegal spots: beyond the head string, on a ball: the hand hesitates and does not lower; nothing is placed.
	Scene.Balls->SetBallVisible(0, true);
	Hand->BeginCarry(Table, 0, kR, Legal);
	for (const rb::Vec2 Illegal : {rb::Vec2(HeadString + 0.10, 0.0), Obstacle})
	{
		Hand->SetTargetCore(Illegal);
		for (int32 I = 0; I < 90; ++I)
		{
			Hand->TickCarry(Dt);
		}
		TestFalse(TEXT("illegal target"), Hand->IsTargetLegal());
		const int32 Before = SetDowns;
		TestFalse(TEXT("confirm refused"), Hand->RequestSetDown());
		TestEqual(TEXT("refused"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Refused));
		double Lowest = 1e9;
		for (int32 I = 0; I < 60; ++I)
		{
			Hand->TickCarry(Dt);
			Lowest = FMath::Min(Lowest, Hand->GetBallBottomCm());
		}
		TestEqual(TEXT("no set-down at an illegal spot"), SetDowns, Before);
		TestTrue(TEXT("the hand hesitates above the hover height, never lowers"), Lowest >= Hand->HoverHeightCm - 1e-9);
		TestEqual(TEXT("still refused while the target stays"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Refused));
	}
	TestEqual(TEXT("two refusals reported (soft knock hook)"), Refusals, 2);
	Hand->SetTargetCore(rb::Vec2(Obstacle.x - 0.10, Obstacle.y));
	TestEqual(TEXT("moving the target carries on"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Carrying));

	// The target stays on the reachable bed: the nose rectangle less a radius.
	Hand->SetTargetCore(rb::Vec2(5.0, -5.0));
	const rb::Vec2 Clamped = Hand->GetTargetCore();
	TestTrue(TEXT("clamped to the bed"), Clamped.x <= 0.5 * Rules.Length - kR && Clamped.x > 0.5 * Rules.Length - kR - 0.002 &&
		Clamped.y >= -(0.5 * Rules.Width - kR) && Clamped.y < -(0.5 * Rules.Width - kR) + 0.002);

	// Over another ball the hand lifts the carried ball clear of it (never through it), eased (no vertical jump), and back down to
	// the hover height behind it.
	{
		const rb::Vec2 Other(-0.55, -0.15);
		Scene.Balls->SetBallCore(1, rb::Vec3(Other.x, Other.y, kR), rb::Quat());
		Scene.Balls->SetBallVisible(1, true);
		const FVector OtherWorld = Table->CoreToWorld(rb::Vec3(Other.x, Other.y, kR));
		Hand->SetTargetCore(rb::Vec2(-0.85, -0.15));
		for (int32 I = 0; I < 120; ++I)
		{
			Hand->TickCarry(Dt);
		}
		double MinGap = 1e9;
		double MaxBottom = 0.0;
		double WorstRise = 0.0;
		double PrevBottom = Hand->GetBallBottomCm();
		for (int32 I = 0; I <= 150; ++I)
		{
			const double A = FMath::Min(1.0, I / 90.0);
			Hand->SetTargetCore(rb::Vec2(-0.85 + 0.6 * A, -0.15)); // right across the other ball at 0.4 m/s
			Hand->TickCarry(Dt);
			MinGap = FMath::Min(MinGap, FVector::Dist(Hand->GetBallWorld(), OtherWorld) - 100.0 * 2.0 * kR);
			MaxBottom = FMath::Max(MaxBottom, Hand->GetBallBottomCm());
			WorstRise = FMath::Max(WorstRise, FMath::Abs(Hand->GetBallBottomCm() - PrevBottom));
			PrevBottom = Hand->GetBallBottomCm();
		}
		AddInfo(FString::Printf(TEXT("over a ball: lifted to %.2f cm, closest gap %.2f mm, worst vertical step %.2f mm per frame"), MaxBottom,
			10.0 * MinGap, 10.0 * WorstRise));
		TestTrue(TEXT("the carried ball never touches another ball"), MinGap > 0.0);
		TestTrue(TEXT("lifted over it (bottom > 2R)"), MaxBottom > 100.0 * 2.0 * kR);
		TestTrue(TEXT("the lift is eased (< 8 mm per frame at 60 fps)"), WorstRise < 0.8);
		TestTrue(TEXT("back to the hover height behind it"), FMath::Abs(Hand->GetBallBottomCm() - Hand->HoverHeightCm) < 0.2);
	}

	// Cancel: the ball goes back to the table.
	Hand->Cancel();
	TestEqual(TEXT("cancelled"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Inactive));
	TestTrue(TEXT("the table's cue ball is back"), Scene.Balls->IsBallVisible(0));

	// Review: Confirm while the lagging hand is still on the far side of a ball next to the target - lining up, the carried ball goes
	// over that ball (never through it) and still lands on exactly the target; the target is frozen during the lowering.
	{
		const rb::Vec2 Other(-1.00, -0.30);
		Scene.Balls->SetBallCore(1, rb::Vec3(Other.x, Other.y, kR), rb::Quat());
		Scene.Balls->SetBallVisible(1, true);
		const FVector OtherWorld = Table->CoreToWorld(rb::Vec3(Other.x, Other.y, kR));
		Scene.Balls->SetBallVisible(0, true);
		Hand->BeginCarry(Table, 0, kR, Legal);
		Hand->SetTargetCore(rb::Vec2(Other.x - 0.12, Other.y));
		for (int32 I = 0; I < 120; ++I)
		{
			Hand->TickCarry(Dt);
		}
		const rb::Vec2 Beyond(Other.x + 2.0 * kR + 0.004, Other.y); // clear of the ball, in the kitchen: legal
		Hand->SetTargetCore(Beyond);
		const rb::Vec2 Confirmed = Hand->GetTargetCore();
		TestTrue(TEXT("the target beyond the ball is legal"), Hand->IsTargetLegal());
		const int32 Before = SetDowns;
		TestTrue(TEXT("confirm at once (the hand still lags 18 cm behind)"), Hand->RequestSetDown());
		double MinGapMm = 1e9;
		int32 LowerFrames = 0;
		while (Hand->GetState() == ERbBallInHandState::Lowering && LowerFrames < 120)
		{
			if (LowerFrames == 3)
			{
				Hand->SetTargetCore(rb::Vec2(Other.x + 0.2, Other.y + 0.1)); // the look moves on: ignored while lowering
			}
			Hand->TickCarry(Dt);
			MinGapMm = FMath::Min(MinGapMm, 10.0 * (FVector::Dist(Hand->GetBallWorld(), OtherWorld) - 100.0 * 2.0 * kR));
			++LowerFrames;
		}
		AddInfo(FString::Printf(TEXT("lowering over a ball: closest gap %.3f mm, %d frames"), MinGapMm, LowerFrames));
		TestTrue(FString::Printf(TEXT("lining up never goes through the ball in between (gap %.3f mm)"), MinGapMm), MinGapMm > -1e-3);
		TestEqual(TEXT("set down once"), SetDowns, Before + 1);
		TestTrue(TEXT("still exactly the confirmed target (bitwise)"), FMemory::Memcmp(&SetDownAt, &Confirmed, sizeof(rb::Vec2)) == 0);
		TestEqual(TEXT("the ball touches the cloth"), Hand->GetBallBottomCm(), 0.0);
		Hand->TickCarry(0.5);
	}

	// Review: the refusal's hesitation (lift and shake) never ends or restarts with a jump - a repeated Confirm during it lets it run
	// on, moving the look on during it lets it fade out (it used to vanish in one frame: the ball dropped by up to 8 mm), and a set-down
	// confirmed while it still shakes starts where the ball is shown.
	{
		for (int32 Id = 1; Id < Scene.Balls->GetBallCount(); ++Id)
		{
			Scene.Balls->SetBallVisible(Id, false); // nothing to lift the carried ball over: only the hesitation moves it vertically
		}
		Scene.Balls->SetBallVisible(0, true);
		Hand->BeginCarry(Table, 0, kR, Legal);
		Hand->SetTargetCore(rb::Vec2(HeadString + 0.10, 0.25));
		for (int32 I = 0; I < 120; ++I)
		{
			Hand->TickCarry(Dt);
		}
		const int32 RefusalsBefore = Refusals;
		TestFalse(TEXT("refused beyond the head string"), Hand->RequestSetDown());
		double WorstVerticalMm = 0.0;
		double PeakLiftCm = 0.0;
		double Prev = Hand->GetBallBottomCm();
		for (int32 I = 0; I < 60; ++I)
		{
			if (I == 6)
			{
				TestFalse(TEXT("a repeated Confirm during the hesitation is refused too"), Hand->RequestSetDown());
			}
			if (I == 12)
			{
				Hand->SetTargetCore(rb::Vec2(HeadString - 0.25, 0.25)); // the look moves on with the lift near its peak
				TestEqual(TEXT("moving on carries on"), static_cast<int32>(Hand->GetState()), static_cast<int32>(ERbBallInHandState::Carrying));
			}
			Hand->TickCarry(Dt);
			WorstVerticalMm = FMath::Max(WorstVerticalMm, 10.0 * FMath::Abs(Hand->GetBallBottomCm() - Prev));
			PeakLiftCm = FMath::Max(PeakLiftCm, Hand->GetBallBottomCm() - Hand->HoverHeightCm);
			Prev = Hand->GetBallBottomCm();
		}
		AddInfo(FString::Printf(TEXT("hesitation: lift %.2f cm, worst vertical step %.3f mm per frame"), PeakLiftCm, WorstVerticalMm));
		TestEqual(TEXT("both refusals reported (the knock)"), Refusals, RefusalsBefore + 2);
		TestTrue(TEXT("the hesitation lifts the ball"), PeakLiftCm > 0.5);
		TestTrue(FString::Printf(TEXT("the hesitation fades out, never jumps (%.3f mm per frame < 1.5 mm)"), WorstVerticalMm), WorstVerticalMm < 1.5);
		TestTrue(TEXT("back at the hover height"), Hand->GetBallBottomCm() - Hand->HoverHeightCm < 0.1);

		// Refused just beyond the head string, the look 8 mm back into the kitchen and Confirm at once while the shake runs: the lowering
		// starts at the shown ball (it used to start at the hand without the shake and tremor: a jump of up to ~2.5 mm).
		Hand->SetTargetCore(rb::Vec2(HeadString + 0.004, -0.25));
		for (int32 I = 0; I < 120; ++I)
		{
			Hand->TickCarry(Dt);
		}
		TestFalse(TEXT("refused just beyond the head string"), Hand->RequestSetDown());
		Hand->TickCarry(Dt);
		Hand->TickCarry(Dt); // 33 ms into the hesitation: the shake is near its extreme
		Hand->SetTargetCore(rb::Vec2(HeadString - 0.004, -0.25));
		const rb::Vec2 Confirmed = Hand->GetTargetCore();
		const rb::Vec2 ShownBefore = Hand->GetHandPlanCore();
		TestTrue(TEXT("confirm at the legal spot lowers"), Hand->RequestSetDown());
		Hand->TickCarry(Dt);
		const double FirstStepMm = 1000.0 * Dist(Hand->GetHandPlanCore(), ShownBefore);
		AddInfo(FString::Printf(TEXT("lowering during the shake: first step %.3f mm"), FirstStepMm));
		TestTrue(FString::Printf(TEXT("the lowering starts where the ball is shown (first step %.3f mm < 1 mm)"), FirstStepMm), FirstStepMm < 1.0);
		const int32 Before = SetDowns;
		for (int32 I = 0; I < 60 && Hand->GetState() == ERbBallInHandState::Lowering; ++I)
		{
			Hand->TickCarry(Dt);
		}
		TestEqual(TEXT("set down"), SetDowns, Before + 1);
		TestTrue(TEXT("on exactly the confirmed target"), FMemory::Memcmp(&SetDownAt, &Confirmed, sizeof(rb::Vec2)) == 0);
		Hand->TickCarry(0.5);
	}

	// Review (3rd pass): Confirm while the lagging hand is still far from the target (a quick look across the kitchen and Confirm at
	// once, or a click right after the pick-up): lining the ball up keeps the hand's speed limit - it used to cover any distance in
	// 0.16 s (0.7 m at ~10 m/s: 11 cm per frame) - the set-down just takes longer, only goes down, and lands on exactly the target.
	{
		Scene.Balls->SetBallVisible(0, true);
		Hand->BeginCarry(Table, 0, kR, Legal);
		Hand->SetTargetCore(rb::Vec2(-1.05, -0.35));
		for (int32 I = 0; I < 120; ++I)
		{
			Hand->TickCarry(Dt);
		}
		Hand->SetTargetCore(rb::Vec2(HeadString - 0.12, 0.30));
		const rb::Vec2 Confirmed = Hand->GetTargetCore();
		const double Far = Dist(Hand->GetHandPlanCore(), Confirmed);
		TestTrue(TEXT("the far target is legal"), Hand->IsTargetLegal());
		const int32 Before = SetDowns;
		TestTrue(FString::Printf(TEXT("confirm at once with the hand %.2f m away"), Far), Far > 0.5 && Hand->RequestSetDown());
		rb::Vec2 Prev = Hand->GetHandPlanCore();
		double PrevBottom = Hand->GetBallBottomCm();
		double WorstStepMm = 0.0;
		bool bOnlyDown = true;
		int32 FarFrames = 0;
		while (Hand->GetState() == ERbBallInHandState::Lowering && FarFrames < 600)
		{
			Hand->TickCarry(Dt);
			WorstStepMm = FMath::Max(WorstStepMm, 1000.0 * Dist(Hand->GetHandPlanCore(), Prev));
			bOnlyDown &= Hand->GetBallBottomCm() <= PrevBottom + 1e-9;
			Prev = Hand->GetHandPlanCore();
			PrevBottom = Hand->GetBallBottomCm();
			++FarFrames;
		}
		const double Seconds = FarFrames * Dt;
		const double ExpectedSeconds = 1.6 * 1.5 * Far / Hand->MaxHandSpeed; // the line-up's smoothstep peaks at the speed limit
		AddInfo(FString::Printf(TEXT("far confirm: %.3f m in %.3f s, worst step %.2f mm per frame (bound %.2f mm)"), Far, Seconds, WorstStepMm,
			1000.0 * MaxStep));
		TestTrue(FString::Printf(TEXT("lining up keeps the hand's speed limit (%.2f mm per frame <= %.2f mm)"), WorstStepMm, 1000.0 * MaxStep),
			WorstStepMm <= 1000.0 * MaxStep);
		TestTrue(FString::Printf(TEXT("the set-down takes as long as the hand needs (%.3f s, expected %.3f s)"), Seconds, ExpectedSeconds),
			FMath::Abs(Seconds - ExpectedSeconds) <= Dt + 1e-9 && Seconds > Hand->LowerSeconds);
		TestTrue(TEXT("the ball only goes down while lowering"), bOnlyDown);
		TestEqual(TEXT("set down once"), SetDowns, Before + 1);
		TestTrue(TEXT("on exactly the confirmed target (bitwise)"), FMemory::Memcmp(&SetDownAt, &Confirmed, sizeof(rb::Vec2)) == 0);
		Hand->TickCarry(0.5);
	}
	Hand->OnSetDown.Clear();
	Hand->OnRefused.Clear();
	Hand->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
