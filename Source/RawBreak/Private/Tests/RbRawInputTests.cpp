// Raw mouse input (review R-01, Docs/ue-architecture.md 6.2.1) and the runtime Enhanced Input setup (6.5): SPSC ring order,
// fallback time reconstruction, input thread start / stop without leaks and its per-report timestamps (injected reports),
// headless inactivity, every action mapped and no key bound twice. Owner: UE-5a.

#include "Input/RbInputSetup.h"
#include "Input/RbRawMouseInput.h"
#include "Tests/RbTestFlags.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "UObject/Package.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRawInputRingOrder, "RawBreak.Unit.Input.RingOrder", RB_UNIT_TEST_FLAGS)
bool FRbRawInputRingOrder::RunTest(const FString& Parameters)
{
	// Full ring without a consumer: order kept, the NEWEST reports are dropped and counted.
	{
		FRbRawReportRing Ring(8);
		int32 Accepted = 0;
		for (int32 i = 0; i < 40; ++i)
		{
			FRbRawMouseReport R;
			R.Time = i;
			R.DeltaX = i;
			Accepted += Ring.Push(R) ? 1 : 0;
		}
		TestTrue(TEXT("holds at least its capacity"), Accepted >= 8);
		TestEqual(TEXT("dropped = rejected pushes"), static_cast<int32>(Ring.GetDropped()), 40 - Accepted);
		FRbRawMouseReport Out;
		int32 Expected = 0;
		while (Ring.Pop(Out))
		{
			TestEqual(TEXT("oldest first"), Out.DeltaX, Expected++);
		}
		TestEqual(TEXT("all accepted popped"), Expected, Accepted);
	}
	// Producer thread -> consumer (this thread): every report arrives once, in order.
	{
		FRbRawReportRing Ring(256);
		constexpr int32 Count = 200000;
		TFuture<void> Producer = Async(EAsyncExecution::Thread, [&Ring]()
		{
			for (int32 i = 0; i < Count; ++i)
			{
				FRbRawMouseReport R;
				R.Time = i;
				R.DeltaX = i;
				R.DeltaY = -i;
				R.bTrueTimestamp = true;
				while (!Ring.Push(R))
				{
					FPlatformProcess::YieldThread(); // the test producer retries; the real one drops (counted)
				}
			}
		});
		int32 Next = 0;
		bool bOrdered = true;
		const double Deadline = FPlatformTime::Seconds() + 30.0;
		FRbRawMouseReport Out;
		while (Next < Count && FPlatformTime::Seconds() < Deadline)
		{
			if (Ring.Pop(Out))
			{
				bOrdered &= Out.DeltaX == Next && Out.DeltaY == -Next && Out.Time == static_cast<double>(Next) && Out.bTrueTimestamp;
				++Next;
			}
		}
		Producer.Wait();
		TestEqual(TEXT("all reports received"), Next, Count);
		TestTrue(TEXT("SPSC order preserved"), bOrdered);
		TestFalse(TEXT("nothing left"), Ring.Pop(Out));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRawInputFallbackTimes, "RawBreak.Unit.Input.FallbackTimeReconstruction", RB_UNIT_TEST_FLAGS)
bool FRbRawInputFallbackTimes::RunTest(const FString& Parameters)
{
	double Times[32];
	// 5 reports in a 10 ms pump at a 1 ms interval: 1 ms apart, the last at the pump time.
	double Spacing = RbRawMouse::ReconstructPumpTimes(10.0, 9.99, 0.001, 5, Times);
	TestNearlyEqual(TEXT("spacing = interval"), Spacing, 0.001, 1e-15);
	for (int32 i = 0; i < 5; ++i)
	{
		TestNearlyEqual(*FString::Printf(TEXT("time %d"), i), Times[i], 10.0 - (4 - i) * 0.001, 1e-12);
	}
	// 20 reports in a 10 ms pump would reach back before the previous pump: the spacing shrinks to 0.5 ms.
	Spacing = RbRawMouse::ReconstructPumpTimes(10.0, 9.99, 0.001, 20, Times);
	TestNearlyEqual(TEXT("spacing shrinks to period / count"), Spacing, 0.0005, 1e-15);
	TestTrue(TEXT("first after the previous pump"), Times[0] > 9.99);
	bool bIncreasing = true;
	for (int32 i = 1; i < 20; ++i)
	{
		bIncreasing &= Times[i] > Times[i - 1];
	}
	TestTrue(TEXT("strictly increasing"), bIncreasing);
	TestEqual(TEXT("last at the pump time"), Times[19], 10.0);
	// No previous pump: the interval as measured.
	Spacing = RbRawMouse::ReconstructPumpTimes(3.0, 0.0, 0.002, 3, Times);
	TestNearlyEqual(TEXT("first pump"), Times[0], 2.996, 1e-12);

	// Interval estimate: 16 reports per 60 Hz frame of a moving 1 kHz-ish mouse -> 1/960 s; sparse batches do not count.
	double Estimate = 1.0 / 1000.0;
	double Pump = 100.0;
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		Estimate = RbRawMouse::UpdateIntervalEstimate(Estimate, Pump + 1.0 / 60.0, Pump, 16);
		Pump += 1.0 / 60.0;
	}
	TestNearlyEqual(TEXT("converges to the report interval"), Estimate, 1.0 / 960.0, 1e-7);
	const double Before = Estimate;
	Estimate = RbRawMouse::UpdateIntervalEstimate(Estimate, Pump + 0.5, Pump, 2); // start of a motion after a pause
	TestEqual(TEXT("sparse batch ignored"), Estimate, Before);
	TestNearlyEqual(TEXT("clamped to 125 Hz"), RbRawMouse::UpdateIntervalEstimate(0.1, 2.0, 1.0, 4), 1.0 / 125.0, 1e-15);
	TestNearlyEqual(TEXT("clamped to 8 kHz"), RbRawMouse::UpdateIntervalEstimate(1e-6, 1.0 + 1e-6, 1.0, 100), 1.0 / 8000.0, 1e-15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRawInputHeadless, "RawBreak.Unit.Input.HeadlessInactive", RB_UNIT_TEST_FLAGS)
bool FRbRawInputHeadless::RunTest(const FString& Parameters)
{
	// The automation run is headless (-NullRHI / -RenderOffscreen): the process instance never takes the mouse.
	TSharedRef<FRbRawMouseInput> A = FRbRawMouseInput::Create();
	TSharedRef<FRbRawMouseInput> B = FRbRawMouseInput::Create();
	TestTrue(TEXT("one process instance"), &A.Get() == &B.Get());
	TestFalse(TEXT("inactive headless"), A->IsActive());
	TestFalse(TEXT("no true timestamps headless"), A->HasTrueTimestamps());
	TArray<FRbRawMouseReport> Out;
	TestEqual(TEXT("drains nothing"), A->Drain(Out), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbRawInputThread, "RawBreak.Unit.Input.ThreadStartStop", RB_UNIT_TEST_FLAGS)
bool FRbRawInputThread::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
	FRbRawMouseInputOptions Options;
	Options.bForceActive = true;
	Options.bInstallHandler = false; // no game window: the thread runs, never registers the mouse
	Options.bForwardToSlate = false;

	DWORD HandlesBefore = 0;
	::GetProcessHandleCount(::GetCurrentProcess(), &HandlesBefore);
	const double Start = FPlatformTime::Seconds();
	constexpr int32 Cycles = 20;
	int32 Good = 0;
	for (int32 Cycle = 0; Cycle < Cycles; ++Cycle)
	{
		TSharedPtr<FRbRawMouseInput> Input = FRbRawMouseInput::CreateWithOptions(Options);
		if (!Input->IsActive() || !Input->IsThreadRunning() || !Input->HasTrueTimestamps())
		{
			AddError(FString::Printf(TEXT("cycle %d: thread not running"), Cycle));
			continue;
		}
		// Three reports ~2 ms apart through the thread: stamped on ARRIVAL (not one pump time), in order.
		const double PostTimes[3] = {FPlatformTime::Seconds(), 0.0, 0.0};
		Input->InjectTestReport(1, -1);
		FPlatformProcess::Sleep(0.002f);
		Input->InjectTestReport(2, -2);
		FPlatformProcess::Sleep(0.002f);
		Input->InjectTestReport(3, -3);
		TArray<FRbRawMouseReport> Reports;
		const double Deadline = FPlatformTime::Seconds() + 2.0;
		while (Reports.Num() < 3 && FPlatformTime::Seconds() < Deadline)
		{
			Input->Drain(Reports);
			FPlatformProcess::Sleep(0.0005f);
		}
		bool bOk = Reports.Num() == 3;
		for (int32 i = 0; bOk && i < 3; ++i)
		{
			bOk = Reports[i].DeltaX == i + 1 && Reports[i].DeltaY == -(i + 1) && Reports[i].bTrueTimestamp && Reports[i].Time >= PostTimes[0];
		}
		// The sleeps between the posts show up in the stamps (>= 1 ms apart): per-report times, not a burst.
		bOk = bOk && Reports[1].Time - Reports[0].Time >= 0.001 && Reports[2].Time - Reports[1].Time >= 0.001;
		if (!bOk)
		{
			AddError(FString::Printf(TEXT("cycle %d: %d reports, wrong order / stamps"), Cycle, Reports.Num()));
		}
		const FRbRawMouseInput::FStats Stats = Input->GetStats();
		bOk = bOk && Stats.ThreadReports == 3 && Stats.Registrations == 0 && Stats.Dropped == 0;
		Good += bOk ? 1 : 0;
		Input.Reset(); // joins the thread, destroys the window, unregisters the class
	}
	TestEqual(TEXT("every cycle delivered its reports"), Good, Cycles);
	DWORD HandlesAfter = 0;
	::GetProcessHandleCount(::GetCurrentProcess(), &HandlesAfter);
	AddInfo(FString::Printf(TEXT("%d thread cycles in %.2f s, process handles %u -> %u"), Cycles, FPlatformTime::Seconds() - Start,
		static_cast<uint32>(HandlesBefore), static_cast<uint32>(HandlesAfter)));
	TestTrue(TEXT("no handle leak (thread, event, window per cycle)"), static_cast<int64>(HandlesAfter) - static_cast<int64>(HandlesBefore) < 10);

	// Fallback options: no thread, no true timestamps.
	FRbRawMouseInputOptions NoThread = Options;
	NoThread.bAllowThread = false;
	TSharedRef<FRbRawMouseInput> Fallback = FRbRawMouseInput::CreateWithOptions(NoThread);
	TestFalse(TEXT("fallback: no thread"), Fallback->IsThreadRunning());
	TestFalse(TEXT("fallback: no true timestamps"), Fallback->HasTrueTimestamps());
	TestFalse(TEXT("fallback: injection needs the thread"), Fallback->InjectTestReport(1, 1));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbInputMapping, "RawBreak.Unit.Input.MappingContext", RB_UNIT_TEST_FLAGS)
bool FRbInputMapping::RunTest(const FString& Parameters)
{
	URbInputSetup* Setup = URbInputSetup::CreateDefault(GetTransientPackage());
	if (!TestNotNull(TEXT("setup"), Setup) || !TestNotNull(TEXT("context"), Setup->Context.Get()))
	{
		return false;
	}
	const TArray<FEnhancedActionKeyMapping>& Mappings = Setup->Context->GetMappings();

	// No key bound twice (review R-17), every action mapped.
	TMap<FKey, const UInputAction*> KeyOwner;
	for (const FEnhancedActionKeyMapping& M : Mappings)
	{
		if (const UInputAction** Other = KeyOwner.Find(M.Key))
		{
			AddError(FString::Printf(TEXT("key %s bound twice (%s, %s)"), *M.Key.ToString(), *GetNameSafe(*Other), *GetNameSafe(M.Action)));
		}
		KeyOwner.Add(M.Key, M.Action);
	}
	for (const UInputAction* Action : Setup->GetAllActions())
	{
		const bool bMapped = Mappings.ContainsByPredicate([Action](const FEnhancedActionKeyMapping& M) { return M.Action == Action; });
		TestTrue(*FString::Printf(TEXT("%s mapped"), *GetNameSafe(Action)), Action && bMapped);
	}
	TestEqual(TEXT("15 actions"), Setup->GetAllActions().Num(), 15);

	auto KeysOf = [&Mappings](const UInputAction* Action)
	{
		TArray<FKey> Keys;
		for (const FEnhancedActionKeyMapping& M : Mappings)
		{
			if (M.Action == Action)
			{
				Keys.Add(M.Key);
			}
		}
		return Keys;
	};
	auto HasTrigger = [&Mappings](const UInputAction* Action, UClass* TriggerClass)
	{
		bool bAll = true;
		for (const FEnhancedActionKeyMapping& M : Mappings)
		{
			if (M.Action == Action)
			{
				bAll &= M.Triggers.ContainsByPredicate([TriggerClass](const UInputTrigger* T) { return T && T->IsA(TriggerClass); });
			}
		}
		return bAll;
	};
	// The M1 defaults of Docs/ue-architecture.md 6.5.
	TestTrue(TEXT("Stroke = left mouse"), KeysOf(Setup->Stroke) == TArray<FKey>{EKeys::LeftMouseButton});
	TestTrue(TEXT("GetDown = right mouse"), KeysOf(Setup->GetDown) == TArray<FKey>{EKeys::RightMouseButton});
	TestTrue(TEXT("Commit = Space"), KeysOf(Setup->Commit) == TArray<FKey>{EKeys::SpaceBar});
	TestTrue(TEXT("Look = mouse XY"), KeysOf(Setup->Look) == TArray<FKey>{EKeys::Mouse2D});
	TestTrue(TEXT("Elevation = wheel"), KeysOf(Setup->Elevation) == TArray<FKey>{EKeys::MouseWheelAxis});
	TestTrue(TEXT("FineAim = Left Shift"), KeysOf(Setup->FineAim) == TArray<FKey>{EKeys::LeftShift});
	TestTrue(TEXT("Settle = Left Ctrl"), KeysOf(Setup->Settle) == TArray<FKey>{EKeys::LeftControl});
	TestTrue(TEXT("Glance = Tab"), KeysOf(Setup->Glance) == TArray<FKey>{EKeys::Tab});
	TestTrue(TEXT("ToggleOverlay = F1"), KeysOf(Setup->ToggleOverlay) == TArray<FKey>{EKeys::F1});
	TestTrue(TEXT("ToggleDebug = F2"), KeysOf(Setup->ToggleDebug) == TArray<FKey>{EKeys::F2});
	TestTrue(TEXT("Replay = R"), KeysOf(Setup->Replay) == TArray<FKey>{EKeys::R});
	const TArray<FKey> ConfirmKeys = KeysOf(Setup->Confirm);
	TestTrue(TEXT("Confirm = Enter + F, not the left mouse"), ConfirmKeys.Num() == 2 && ConfirmKeys.Contains(EKeys::Enter) &&
		ConfirmKeys.Contains(EKeys::F) && !ConfirmKeys.Contains(EKeys::LeftMouseButton));
	const TArray<FKey> MoveKeys = KeysOf(Setup->Move);
	TestTrue(TEXT("Move = WASD"), MoveKeys.Num() == 4 && MoveKeys.Contains(EKeys::W) && MoveKeys.Contains(EKeys::A) && MoveKeys.Contains(EKeys::S) &&
		MoveKeys.Contains(EKeys::D));
	const TArray<FKey> TipKeys = KeysOf(Setup->TipOffset);
	TestTrue(TEXT("TipOffset = arrows"), TipKeys.Num() == 4 && TipKeys.Contains(EKeys::Up) && TipKeys.Contains(EKeys::Down) &&
		TipKeys.Contains(EKeys::Left) && TipKeys.Contains(EKeys::Right));
	const TArray<FKey> CycleKeys = KeysOf(Setup->CycleOption);
	TestTrue(TEXT("CycleOption = Q / E"), CycleKeys.Num() == 2 && CycleKeys.Contains(EKeys::Q) && CycleKeys.Contains(EKeys::E));

	// Value types and triggers.
	TestTrue(TEXT("Move 2D"), Setup->Move->ValueType == EInputActionValueType::Axis2D);
	TestTrue(TEXT("Look 2D"), Setup->Look->ValueType == EInputActionValueType::Axis2D);
	TestTrue(TEXT("TipOffset 2D"), Setup->TipOffset->ValueType == EInputActionValueType::Axis2D);
	TestTrue(TEXT("Elevation 1D"), Setup->Elevation->ValueType == EInputActionValueType::Axis1D);
	TestTrue(TEXT("CycleOption 1D"), Setup->CycleOption->ValueType == EInputActionValueType::Axis1D);
	for (const UInputAction* Pressed : {Setup->GetDown.Get(), Setup->ToggleOverlay.Get(), Setup->ToggleDebug.Get(), Setup->Replay.Get(),
		Setup->Confirm.Get(), Setup->CycleOption.Get()})
	{
		TestTrue(*FString::Printf(TEXT("%s fires once per press"), *GetNameSafe(Pressed)), HasTrigger(Pressed, UInputTriggerPressed::StaticClass()));
	}
	TestTrue(TEXT("TipOffset pulses"), HasTrigger(Setup->TipOffset, UInputTriggerPulse::StaticClass()));
	for (const UInputAction* Held : {Setup->Stroke.Get(), Setup->Commit.Get(), Setup->FineAim.Get(), Setup->Settle.Get(), Setup->Glance.Get()})
	{
		const bool bNoTriggers = !Mappings.ContainsByPredicate([Held](const FEnhancedActionKeyMapping& M) { return M.Action == Held && M.Triggers.Num() > 0; });
		TestTrue(*FString::Printf(TEXT("%s held (implicit Down)"), *GetNameSafe(Held)), Held->ValueType == EInputActionValueType::Boolean && bNoTriggers);
	}

	// Direction modifiers: W / Up forward (swizzle to Y), S / Down swizzle + negate, A / Left negate, D / Right none; Q negated.
	auto Modifiers = [&Mappings](const FKey& Key)
	{
		TArray<UClass*> Classes;
		for (const FEnhancedActionKeyMapping& M : Mappings)
		{
			if (M.Key == Key)
			{
				for (const UInputModifier* Mod : M.Modifiers)
				{
					Classes.Add(Mod ? Mod->GetClass() : nullptr);
				}
			}
		}
		return Classes;
	};
	UClass* Swizzle = UInputModifierSwizzleAxis::StaticClass();
	UClass* Negate = UInputModifierNegate::StaticClass();
	TestTrue(TEXT("W: swizzle"), Modifiers(EKeys::W) == TArray<UClass*>{Swizzle});
	TestTrue(TEXT("S: swizzle + negate"), Modifiers(EKeys::S) == TArray<UClass*>({Swizzle, Negate}));
	TestTrue(TEXT("A: negate"), Modifiers(EKeys::A) == TArray<UClass*>{Negate});
	TestTrue(TEXT("D: none"), Modifiers(EKeys::D).Num() == 0);
	TestTrue(TEXT("Up: swizzle"), Modifiers(EKeys::Up) == TArray<UClass*>{Swizzle});
	TestTrue(TEXT("Down: swizzle + negate"), Modifiers(EKeys::Down) == TArray<UClass*>({Swizzle, Negate}));
	TestTrue(TEXT("Left: negate"), Modifiers(EKeys::Left) == TArray<UClass*>{Negate});
	TestTrue(TEXT("Q: negate (-1)"), Modifiers(EKeys::Q) == TArray<UClass*>{Negate});
	TestTrue(TEXT("E: +1"), Modifiers(EKeys::E).Num() == 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
