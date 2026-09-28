// Stroke component acceptance (Docs/ue-architecture.md 13, UE-5a): scripted stroke of known speed -> Intended.Speed within
// 1e-3 m/s and BITWISE identical for 30 / 60 / 144 fps frame splits; practice stop-short; commit; AddressIndex; cue pose =
// SampleHand and, at t_c with the full ramp, = ExecuteStroke (what you see is what hits, R-04); abort after / before the
// ramp; Settle; a Stroke press in PlacingCueBall confirms; steering, pause, contact acceleration, head movement; the
// director's context hooks (auto-chalked tip, context pushed after an abort).
// The component runs without a world (NewObject, driven through TickStroke with a test clock). Owner: UE-5a.

#include "Core/RbCoords.h"
#include "Input/RbRawMouseInput.h"
#include "Player/RbStrokeComponent.h"
#include "Tests/RbTestFlags.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UObject/Package.h"

#include "rb/Human/Chores.h"
#include "rb/Human/HumanModel.h"
#include "rb/Physics/CueStrike.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double kR = 0.028575;

	// A context with visible noise: pressure, a hidden bow, a matching streak history, other balls.
	FRbStrokeContext MakeTestContext()
	{
		FRbStrokeContext C;
		C.Attributes = rb::human::UniformAttributes(25.0);
		C.Situation.Pressure = 0.4;
		C.CueBody.BowSag = 0.0006;
		C.CueBody.WarpKnown = false;
		C.Key.MatchSeed = 11;
		C.Key.RackIndex = 1;
		C.Key.ShotIndex = 7;
		C.Key.ShooterId = 1;
		C.Key.ShooterShotIndex = 4;
		C.Key.CuePickupIndex = 1;
		C.History = rb::human::RebuildNoiseHistory(C.Key.MatchSeed, rb::human::ShooterKey(C.Key), C.Key.ShooterShotIndex);
		rb::human::BallObstacle One;
		One.Id = 1;
		One.Position = rb::Vec3(0.4, 0.05, kR);
		C.OtherBalls.Add(One);
		rb::human::BallObstacle Nine;
		Nine.Id = 9;
		Nine.Position = rb::Vec3(0.9, -0.3, kR);
		C.OtherBalls.Add(Nine);
		return C;
	}

	const rb::Vec3 kCueBall(-0.6, 0.1, kR);

	// A stroke component on a test clock with recorded delegate calls.
	struct FStrokeRig
	{
		double Clock = 5000.0;
		URbStrokeComponent* Stroke = nullptr;
		TArray<FRbStrokeCommit> Contacts;
		TArray<bool> Aborts;
		TArray<FVector> Placed;

		explicit FStrokeRig(const FRbStrokeContext& Context)
		{
			Stroke = NewObject<URbStrokeComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			Stroke->AddToRoot();
			Stroke->ClockOverride = [this]() { return Clock; };
			Stroke->OnStrokeContact.AddLambda([this](const FRbStrokeCommit& C) { Contacts.Add(C); });
			Stroke->OnStrokeAborted.AddLambda([this](bool bShown) { Aborts.Add(bShown); });
			Stroke->OnCueBallPlaced.AddLambda([this](const FVector& W) { Placed.Add(W); });
			Stroke->SetStrokeContext(Context);
			Stroke->BeginAddress(kCueBall, kR);
			Stroke->SetAim(FMath::DegreesToRadians(8.0), FMath::DegreesToRadians(3.0), 0.1, -0.2);
		}
		~FStrokeRig()
		{
			Stroke->ClockOverride = nullptr;
			Stroke->OnStrokeContact.Clear();
			Stroke->OnStrokeAborted.Clear();
			Stroke->OnCueBallPlaced.Clear();
			Stroke->RemoveFromRoot();
			Stroke->MarkAsGarbage();
		}
		FStrokeRig(const FStrokeRig&) = delete;
		FStrokeRig& operator=(const FStrokeRig&) = delete;

		void GetDown()
		{
			Stroke->RequestGetDownToggle();
			Clock = Stroke->GetDownSince();
			Stroke->TickStroke(Clock);
		}

		// Ticks at a fixed frame rate until Until or contact.
		void RunFrames(double Fps, double Until)
		{
			const double Dt = 1.0 / Fps;
			while (Clock < Until && Stroke->GetPhase() == ERbStrokePhase::Down)
			{
				Clock += Dt;
				Stroke->TickStroke(Clock);
			}
		}

		void RunUntil(double Until, double Fps = 60.0)
		{
			const double Dt = 1.0 / Fps;
			while (Clock + Dt <= Until)
			{
				Clock += Dt;
				Stroke->TickStroke(Clock);
			}
		}
	};

	bool SameBits(double A, double B)
	{
		return FMemory::Memcmp(&A, &B, sizeof(double)) == 0;
	}

	bool SameIntended(const rb::human::IntendedStroke& A, const rb::human::IntendedStroke& B)
	{
		return SameBits(A.Azimuth, B.Azimuth) && SameBits(A.Elevation, B.Elevation) && SameBits(A.AxisOffsetA, B.AxisOffsetA) &&
			SameBits(A.AxisOffsetB, B.AxisOffsetB) && SameBits(A.Speed, B.Speed) && SameBits(A.TipVelocityRight, B.TipVelocityRight) &&
			SameBits(A.TipVelocityUp, B.TipVelocityUp) && SameBits(A.TimeDown, B.TimeDown) && SameBits(A.ForwardStart, B.ForwardStart) &&
			SameBits(A.SettleStart, B.SettleStart) && SameBits(A.PauseDuration, B.PauseDuration) &&
			SameBits(A.ContactAcceleration, B.ContactAcceleration) && A.HeadMovedBeforeContact == B.HeadMovedBeforeContact;
	}

	bool SamePose(const rb::human::HandPose& A, const rb::human::HandPose& B)
	{
		return SameBits(A.Time, B.Time) && SameBits(A.GripLateral, B.GripLateral) && SameBits(A.GripVertical, B.GripVertical) &&
			SameBits(A.TremorRight, B.TremorRight) && SameBits(A.TremorUp, B.TremorUp) && SameBits(A.Ramp, B.Ramp) &&
			SameBits(A.Azimuth, B.Azimuth) && SameBits(A.Elevation, B.Elevation) && SameBits(A.AxisOffsetA, B.AxisOffsetA) &&
			SameBits(A.AxisOffsetB, B.AxisOffsetB) && A.RampShown == B.RampShown;
	}

	rb::human::HandPose SampleWithContext(const rb::human::IntendedStroke& I, const FRbStrokeContext& C, double T)
	{
		return rb::human::SampleHand(I, C.Attributes, C.Situation, C.CueBody, C.Cue, C.CueBall, C.Key, C.History, C.Params, T);
	}

	// The pose the component must render while down without a stroke: SampleHand of the aim (no ramp) at Now - DownSince.
	rb::human::HandPose ExpectedIdlePose(const URbStrokeComponent& Stroke, double Now)
	{
		const FRbAimState& Aim = Stroke.GetAim();
		const double T = FMath::Max(0.0, Now - Stroke.GetDownSince());
		rb::human::IntendedStroke I;
		I.Azimuth = Aim.Azimuth;
		I.Elevation = FMath::Max(Aim.Elevation, Aim.ElevationFloor);
		I.AxisOffsetA = Aim.AxisOffsetA;
		I.AxisOffsetB = Aim.AxisOffsetB;
		I.TimeDown = (Stroke.GetDownSince() + T) - Stroke.GetDownSince();
		I.ForwardStart = I.TimeDown + 1.0e6;
		return SampleWithContext(I, Stroke.GetContext(), T);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeKnownSpeed, "RawBreak.Unit.Stroke.KnownSpeed", RB_UNIT_TEST_FLAGS)
bool FRbStrokeKnownSpeed::RunTest(const FString& Parameters)
{
	for (const double V : {0.5, 1.0, 2.0, 4.0, 8.0, 12.0})
	{
		FStrokeRig Rig(MakeTestContext());
		Rig.GetDown();
		Rig.Stroke->SetCommitHeld(true);
		const TArray<FRbStrokeSample> Samples = Rig.Stroke->MakeScriptedStroke(V, Rig.Clock + 0.1);
		if (!TestTrue(*FString::Printf(TEXT("scripted %.1f m/s"), V), Samples.Num() > 0))
		{
			continue;
		}
		Rig.Stroke->InjectStrokeSamples(Samples);
		Rig.RunFrames(60.0, Samples.Last().Time + 0.1);
		if (!TestEqual(*FString::Printf(TEXT("one contact at %.1f m/s"), V), Rig.Contacts.Num(), 1))
		{
			continue;
		}
		const FRbStrokeCommit& C = Rig.Contacts[0];
		TestNearlyEqual(*FString::Printf(TEXT("Intended.Speed at %.1f m/s"), V), C.Intended.Speed, V, 1e-3);
		TestNearlyEqual(TEXT("TimeDown = contact - down"), C.Intended.TimeDown, C.ContactTime - Rig.Stroke->GetDownSince(), 1e-12);
		TestTrue(TEXT("scripted strokes carry no true timestamps"), !C.bTrueTimestamps);
		TestTrue(TEXT("phase Watching after contact"), Rig.Stroke->GetPhase() == ERbStrokePhase::Watching);
		TestEqual(TEXT("cue displacement 0 at contact"), Rig.Stroke->GetCueDisplacement(), 0.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeFrameSplit, "RawBreak.Unit.Stroke.FrameSplitBitwise", RB_UNIT_TEST_FLAGS)
bool FRbStrokeFrameSplit::RunTest(const FString& Parameters)
{
	// The same timestamped samples arriving in 30 / 60 / 144 fps frames (and an irregular split) must give a bitwise identical
	// IntendedStroke, contact time, input log and contact pose (pitfall 19: never per-frame deltas).
	TArray<FRbStrokeSample> Samples;
	FRbStrokeCommit Reference;
	bool bHaveReference = false;
	const double Rates[] = {30.0, 60.0, 144.0, 0.0}; // 0 = irregular frame times
	for (const double Fps : Rates)
	{
		FStrokeRig Rig(MakeTestContext());
		Rig.GetDown();
		Rig.Stroke->SetCommitHeld(true);
		if (Samples.Num() == 0)
		{
			FRbScriptedStroke P;
			P.TipSpeed = 3.3;
			P.StartTime = Rig.Clock + 0.137;
			P.StartCueDisplacement = Rig.Stroke->GetCueDisplacement();
			P.LateralDrift = 0.004; // steering too
			TestTrue(TEXT("stroke built"), RbStrokeMath::MakeScriptedStroke(P, Rig.Stroke->Gain, Samples));
		}
		Rig.Stroke->InjectStrokeSamples(Samples);
		if (Fps > 0.0)
		{
			Rig.RunFrames(Fps, Samples.Last().Time + 0.2);
		}
		else
		{
			uint32 State = 12345u;
			while (Rig.Clock < Samples.Last().Time + 0.2 && Rig.Stroke->GetPhase() == ERbStrokePhase::Down)
			{
				State = State * 1664525u + 1013904223u;
				Rig.Clock += 0.003 + 0.04 * (State >> 8) / 16777216.0; // 3..43 ms frames
				Rig.Stroke->TickStroke(Rig.Clock);
			}
		}
		if (!TestEqual(*FString::Printf(TEXT("one contact (%s)"), Fps > 0.0 ? *FString::Printf(TEXT("%.0f fps"), Fps) : TEXT("irregular")),
			Rig.Contacts.Num(), 1))
		{
			continue;
		}
		const FRbStrokeCommit& C = Rig.Contacts[0];
		if (!bHaveReference)
		{
			Reference = C;
			bHaveReference = true;
			TestNearlyEqual(TEXT("speed 3.3 m/s"), C.Intended.Speed, 3.3, 1e-3);
			continue;
		}
		const FString Label = Fps > 0.0 ? FString::Printf(TEXT("%.0f fps"), Fps) : FString(TEXT("irregular frames"));
		TestTrue(*(TEXT("IntendedStroke bitwise identical: ") + Label), SameIntended(C.Intended, Reference.Intended));
		TestTrue(*(TEXT("contact time bitwise identical: ") + Label), SameBits(C.ContactTime, Reference.ContactTime));
		TestTrue(*(TEXT("contact pose bitwise identical: ") + Label), SamePose(C.ContactPose, Reference.ContactPose));
		bool bLogSame = C.InputLog.Num() == Reference.InputLog.Num();
		for (int32 i = 0; bLogSame && i < C.InputLog.Num(); ++i)
		{
			bLogSame = SameBits(C.InputLog[i].Time, Reference.InputLog[i].Time) && SameBits(C.InputLog[i].Position, Reference.InputLog[i].Position) &&
				SameBits(C.InputLog[i].Lateral, Reference.InputLog[i].Lateral);
		}
		TestTrue(*(TEXT("input log identical: ") + Label), bLogSame);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokePracticeStopShort, "RawBreak.Unit.Stroke.PracticeStopShort", RB_UNIT_TEST_FLAGS)
bool FRbStrokePracticeStopShort::RunTest(const FString& Parameters)
{
	FStrokeRig Rig(MakeTestContext());
	Rig.GetDown();
	TestTrue(TEXT("down after the transition"), Rig.Stroke->GetPhase() == ERbStrokePhase::Down);
	TestNearlyEqual(TEXT("address position"), Rig.Stroke->GetCueDisplacement(), -Rig.Stroke->AddressDistance, 1e-15);
	const TArray<FRbStrokeSample> Samples = Rig.Stroke->MakeScriptedStroke(4.0, Rig.Clock + 0.1);
	Rig.Stroke->InjectStrokeSamples(Samples); // Commit NOT held: a practice stroke
	Rig.RunFrames(60.0, Samples.Last().Time + 0.1);
	TestEqual(TEXT("no contact"), Rig.Contacts.Num(), 0);
	TestTrue(TEXT("still down"), Rig.Stroke->GetPhase() == ERbStrokePhase::Down);
	TestEqual(TEXT("stops exactly PracticeStopShort before the ball"), Rig.Stroke->GetCueDisplacement(), -Rig.Stroke->PracticeStopShort);
	TestEqual(TEXT("an uncommitted stroke never aborts"), Rig.Aborts.Num(), 0);

	// The rendered tip dome centre is 4 mm behind its contact position.
	rb::Vec3 Tip;
	rb::Vec3 Dir;
	Rig.Stroke->GetCuePoseCore(Tip, Dir);
	rb::Vec3 TipAtContact;
	rb::Vec3 Dir0;
	URbStrokeComponent::ComputeCuePoseCore(Rig.Stroke->GetHandPose(), kCueBall, kR, Rig.Stroke->GetContext().Tip.DomeRadius,
		Rig.Stroke->GetContext().Params.OffsetClamp, 0.0, TipAtContact, Dir0);
	TestNearlyEqual(TEXT("gap along the axis"), rb::Dot(TipAtContact - Tip, Dir), Rig.Stroke->PracticeStopShort, 1e-12);

	// Hardcore: any tip contact is a shot.
	FStrokeRig Hard(MakeTestContext());
	Hard.Stroke->bHardcore = true;
	Hard.GetDown();
	const TArray<FRbStrokeSample> HardSamples = Hard.Stroke->MakeScriptedStroke(2.0, Hard.Clock + 0.1);
	Hard.Stroke->InjectStrokeSamples(HardSamples);
	Hard.RunFrames(60.0, HardSamples.Last().Time + 0.1);
	TestEqual(TEXT("hardcore: contact without Commit"), Hard.Contacts.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeCommitOnce, "RawBreak.Unit.Stroke.CommitContactOnce", RB_UNIT_TEST_FLAGS)
bool FRbStrokeCommitOnce::RunTest(const FString& Parameters)
{
	FStrokeRig Rig(MakeTestContext());
	Rig.GetDown();
	Rig.Stroke->SetCommitHeld(true);
	const TArray<FRbStrokeSample> Samples = Rig.Stroke->MakeScriptedStroke(2.5, Rig.Clock + 0.1);
	Rig.Stroke->InjectStrokeSamples(Samples);
	Rig.RunFrames(60.0, Samples.Last().Time + 0.1);
	if (!TestEqual(TEXT("exactly one OnStrokeContact"), Rig.Contacts.Num(), 1))
	{
		return false;
	}
	// More samples and frames after contact change nothing.
	Rig.Stroke->InjectStrokeSamples(Rig.Stroke->MakeScriptedStroke(2.5, Rig.Clock + 0.05));
	Rig.RunUntil(Rig.Clock + 2.0);
	TestEqual(TEXT("still one contact"), Rig.Contacts.Num(), 1);
	TestTrue(TEXT("Watching"), Rig.Stroke->GetPhase() == ERbStrokePhase::Watching);
	const FRbStrokeCommit& C = Rig.Contacts[0];
	TestTrue(TEXT("input log up to the crossing sample"), C.InputLog.Num() > 10 && C.InputLog.Last().Time >= C.ContactTime);
	TestTrue(TEXT("contact inside the last sample step"), C.InputLog[C.InputLog.Num() - 2].Time <= C.ContactTime);
	TestEqual(TEXT("AddressIndex 0 on the first get-down"), static_cast<int32>(C.AddressIndex), 0);
	TestEqual(TEXT("context key carries the AddressIndex"), static_cast<int32>(C.Context.Key.AddressIndex), 0);
	TestTrue(TEXT("committed forward stroke recorded"), C.Intended.ForwardStart >= 0.0 && C.Intended.ForwardStart < C.Intended.TimeDown);
	TestEqual(TEXT("no Settle"), C.Intended.SettleStart, -1.0);
	TestFalse(TEXT("no head movement"), C.Intended.HeadMovedBeforeContact);
	TestEqual(TEXT("no abort"), Rig.Aborts.Num(), 0);

	// Locked by the director, unlocked for the next shot.
	Rig.Stroke->SetLocked(true);
	TestTrue(TEXT("Locked"), Rig.Stroke->GetPhase() == ERbStrokePhase::Locked);
	Rig.Stroke->BeginAddress(kCueBall, kR);
	TestTrue(TEXT("next shot: Walking"), Rig.Stroke->GetPhase() == ERbStrokePhase::Walking);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeAddressIndex, "RawBreak.Unit.Stroke.AddressIndex", RB_UNIT_TEST_FLAGS)
bool FRbStrokeAddressIndex::RunTest(const FString& Parameters)
{
	FStrokeRig Rig(MakeTestContext());
	Rig.GetDown();
	Rig.RunUntil(Rig.Clock + 0.5);
	Rig.Stroke->RequestGetDownToggle(); // stand up
	TestTrue(TEXT("Walking after standing up"), Rig.Stroke->GetPhase() == ERbStrokePhase::Walking);
	TestEqual(TEXT("AddressIndex 1"), static_cast<int32>(Rig.Stroke->GetAddressIndex()), 1);
	Rig.Stroke->RequestGetDownToggle(); // down again (still GettingDown until the transition ends)
	Rig.Stroke->RequestGetDownToggle(); // and up during the transition: counts too
	TestEqual(TEXT("AddressIndex 2"), static_cast<int32>(Rig.Stroke->GetAddressIndex()), 2);
	Rig.GetDown();
	Rig.Stroke->SetCommitHeld(true);
	const TArray<FRbStrokeSample> Samples = Rig.Stroke->MakeScriptedStroke(2.0, Rig.Clock + 0.1);
	Rig.Stroke->InjectStrokeSamples(Samples);
	Rig.RunFrames(60.0, Samples.Last().Time + 0.1);
	if (!TestEqual(TEXT("contact"), Rig.Contacts.Num(), 1))
	{
		return false;
	}
	const FRbStrokeCommit& C = Rig.Contacts[0];
	TestEqual(TEXT("commit AddressIndex"), static_cast<int32>(C.AddressIndex), 2);
	TestEqual(TEXT("NoiseKey::AddressIndex"), static_cast<int32>(C.Context.Key.AddressIndex), 2);
	// New drift / tremor processes: the same stroke on address 0 would show another grip offset.
	FRbStrokeContext First = C.Context;
	First.Key.AddressIndex = 0;
	const rb::human::HandPose P0 = SampleWithContext(C.Intended, First, C.Intended.TimeDown);
	TestTrue(TEXT("another address -> another drift"), P0.GripLateral != C.ContactPose.GripLateral);
	Rig.Stroke->BeginAddress(kCueBall, kR);
	TestEqual(TEXT("BeginAddress resets it"), static_cast<int32>(Rig.Stroke->GetAddressIndex()), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeWhatYouSee, "RawBreak.Unit.Stroke.SampleHandEqualsExecuteStroke", RB_UNIT_TEST_FLAGS)
bool FRbStrokeWhatYouSee::RunTest(const FString& Parameters)
{
	// Review R-04 / A-HUM-2: while down the rendered pose is SampleHand(provisional stroke, context, t); at t_c with the full
	// ramp it is exactly the executed stroke.
	const FRbStrokeContext Context = MakeTestContext();
	FStrokeRig Rig(Context);
	Rig.GetDown();
	Rig.RunUntil(Rig.Clock + 0.73);

	// Before any stroke: the provisional stroke is the aim with no ramp.
	{
		const FRbAimState& Aim = Rig.Stroke->GetAim();
		const rb::human::HandPose Expected = ExpectedIdlePose(*Rig.Stroke, Rig.Clock);
		TestTrue(TEXT("rendered pose = SampleHand while down"), SamePose(Rig.Stroke->GetHandPose(), Expected));
		TestFalse(TEXT("no ramp before a committed stroke"), Rig.Stroke->GetHandPose().RampShown);
		TestTrue(TEXT("drift visible (pose != aim)"), Rig.Stroke->GetHandPose().Azimuth != Aim.Azimuth);
		rb::Vec3 Tip;
		rb::Vec3 Dir;
		Rig.Stroke->GetCuePoseCore(Tip, Dir);
		rb::Vec3 ExpectedTip;
		rb::Vec3 ExpectedDir;
		URbStrokeComponent::ComputeCuePoseCore(Expected, kCueBall, kR, Context.Tip.DomeRadius, Context.Params.OffsetClamp,
			Rig.Stroke->GetCueDisplacement(), ExpectedTip, ExpectedDir);
		TestTrue(TEXT("cue pose from the hand pose"), Tip == ExpectedTip && Dir == ExpectedDir);
	}

	Rig.Stroke->SetCommitHeld(true);
	const TArray<FRbStrokeSample> Samples = Rig.Stroke->MakeScriptedStroke(2.0, Rig.Clock + 0.05);
	Rig.Stroke->InjectStrokeSamples(Samples);
	Rig.RunFrames(60.0, Samples.Last().Time + 0.1);
	if (!TestEqual(TEXT("contact"), Rig.Contacts.Num(), 1))
	{
		return false;
	}
	const FRbStrokeCommit& C = Rig.Contacts[0];
	TestTrue(TEXT("full ramp at contact (forward stroke >= 0.1 s)"), C.Intended.TimeDown - C.Intended.ForwardStart >= Context.Params.RampDuration);
	const rb::human::HandPose& Pose = Rig.Stroke->GetHandPose();
	TestTrue(TEXT("pose on screen = commit contact pose"), SamePose(Pose, C.ContactPose));
	TestTrue(TEXT("contact pose = SampleHand(final stroke, t_c)"), SamePose(Pose, SampleWithContext(C.Intended, C.Context, C.Intended.TimeDown)));
	TestEqual(TEXT("ramp complete"), Pose.Ramp, 1.0);

	TArray<rb::human::BallObstacle> Others = C.Context.OtherBalls;
	const rb::human::ExecutedStroke E = rb::human::ExecuteStroke(C.Intended, C.Context.Attributes, C.Context.Situation, C.Context.Tip,
		C.Context.CueBody, C.Context.Cue, C.Context.CueBall, kCueBall, Others.GetData(), Others.Num(), C.Context.Key, C.Context.History,
		C.Context.Params);
	if (!TestTrue(TEXT("ExecuteStroke Ok"), E.Error == rb::ErrorCode::Ok))
	{
		return false;
	}
	TestTrue(TEXT("azimuth bitwise"), SameBits(Pose.Azimuth, E.Strike.Azimuth));
	TestTrue(TEXT("elevation bitwise"), SameBits(Pose.Elevation, E.Strike.Elevation));
	TestTrue(TEXT("axis offset A bitwise"), SameBits(Pose.AxisOffsetA, E.AxisOffset.x));
	TestTrue(TEXT("axis offset B bitwise"), SameBits(Pose.AxisOffsetB, E.AxisOffset.y));
	TestTrue(TEXT("noise is on (executed != intended)"), E.Strike.Azimuth != C.Intended.Azimuth);

	// The rendered tip dome centre is the executed one (contact point of the strike, current dome radius).
	rb::Vec3 Tip;
	rb::Vec3 Dir;
	Rig.Stroke->GetCuePoseCore(Tip, Dir);
	const rb::CueFrame Frame = rb::MakeCueFrame(E.Strike.Elevation, E.Strike.Azimuth);
	const rb::Vec3 Dome = kCueBall + rb::CueContactPoint(Frame, E.Strike.OffsetA, E.Strike.OffsetB, 1.0) * (kR + C.Context.Tip.DomeRadius);
	TestNearlyEqual(TEXT("dome centre x"), Tip.x, Dome.x, 1e-12);
	TestNearlyEqual(TEXT("dome centre y"), Tip.y, Dome.y, 1e-12);
	TestNearlyEqual(TEXT("dome centre z"), Tip.z, Dome.z, 1e-12);
	TestNearlyEqual(TEXT("axis"), rb::Dot(Dir, Frame.Axis), 1.0, 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeAbort, "RawBreak.Unit.Stroke.AbortRamp", RB_UNIT_TEST_FLAGS)
bool FRbStrokeAbort::RunTest(const FString& Parameters)
{
	// Forward part of a committed stroke (up to 80 ms into the forward motion), no contact.
	auto ForwardPart = [](FStrokeRig& Rig, double& OutForwardStart)
	{
		FRbScriptedStroke P;
		P.TipSpeed = 2.0;
		P.StartTime = Rig.Clock + 0.1;
		P.StartCueDisplacement = Rig.Stroke->GetCueDisplacement();
		TArray<FRbStrokeSample> All;
		double Tc = 0.0;
		RbStrokeMath::MakeScriptedStroke(P, Rig.Stroke->Gain, All, &Tc, &OutForwardStart);
		TArray<FRbStrokeSample> Part;
		for (const FRbStrokeSample& S : All)
		{
			if (S.Time <= OutForwardStart + 0.08)
			{
				Part.Add(S);
			}
		}
		return Part;
	};

	// 1. The ramp was presented, then the stroke turns back: OnStrokeAborted(true).
	{
		FStrokeRig Rig(MakeTestContext());
		Rig.GetDown();
		Rig.Stroke->SetCommitHeld(true);
		double ForwardStart = 0.0;
		TArray<FRbStrokeSample> Samples = ForwardPart(Rig, ForwardStart);
		const FRbStrokeSample Last = Samples.Last();
		for (int32 k = 1; k <= 60; ++k)
		{
			FRbStrokeSample Back = Last;
			Back.Time = Last.Time + k * 0.001;
			Back.Position = Last.Position - 0.0005 * k; // pull back 3 cm of hand travel
			Samples.Add(Back);
		}
		Rig.Stroke->InjectStrokeSamples(Samples);
		Rig.RunUntil(Last.Time - 0.001);
		TestTrue(TEXT("committed stroke in progress"), Rig.Stroke->IsCommittedStrokeInProgress());
		TestTrue(TEXT("the ramp is shown"), Rig.Stroke->GetHandPose().RampShown);
		Rig.RunUntil(Samples.Last().Time + 0.05);
		TestEqual(TEXT("one abort"), Rig.Aborts.Num(), 1);
		TestTrue(TEXT("abort after the ramp -> true"), Rig.Aborts.Num() == 1 && Rig.Aborts[0]);
		TestEqual(TEXT("no contact"), Rig.Contacts.Num(), 0);
		TestFalse(TEXT("no committed stroke after the abort"), Rig.Stroke->IsCommittedStrokeInProgress());
		TestFalse(TEXT("the next frame shows no ramp"), Rig.Stroke->GetHandPose().RampShown);
	}
	// 2. Commit released after the ramp was presented: true.
	{
		FStrokeRig Rig(MakeTestContext());
		Rig.GetDown();
		Rig.Stroke->SetCommitHeld(true);
		double ForwardStart = 0.0;
		const TArray<FRbStrokeSample> Samples = ForwardPart(Rig, ForwardStart);
		Rig.Stroke->InjectStrokeSamples(Samples);
		Rig.RunUntil(Samples.Last().Time);
		Rig.Stroke->SetCommitHeld(false);
		TestTrue(TEXT("commit released after the ramp -> true"), Rig.Aborts.Num() == 1 && Rig.Aborts[0]);
	}
	// 3. The forward stroke is detected and aborted before any frame presented the ramp: false.
	{
		FStrokeRig Rig(MakeTestContext());
		Rig.GetDown();
		Rig.Stroke->SetCommitHeld(true);
		double ForwardStart = 0.0;
		const TArray<FRbStrokeSample> Samples = ForwardPart(Rig, ForwardStart);
		Rig.Stroke->InjectStrokeSamples(Samples);
		Rig.RunUntil(ForwardStart); // frames of the backswing and the pause only
		TestFalse(TEXT("not committed yet"), Rig.Stroke->IsCommittedStrokeInProgress());
		Rig.Clock = Samples.Last().Time;
		Rig.Stroke->SetCommitHeld(false); // processes the forward samples (commit starts), then releases before a frame
		TestTrue(TEXT("abort before the ramp -> false"), Rig.Aborts.Num() == 1 && !Rig.Aborts[0]);
	}
	// 4. Standing up during a committed stroke after the ramp: true; the stroke is gone.
	{
		FStrokeRig Rig(MakeTestContext());
		Rig.GetDown();
		Rig.Stroke->SetCommitHeld(true);
		double ForwardStart = 0.0;
		const TArray<FRbStrokeSample> Samples = ForwardPart(Rig, ForwardStart);
		Rig.Stroke->InjectStrokeSamples(Samples);
		Rig.RunUntil(Samples.Last().Time);
		Rig.Stroke->RequestGetDownToggle();
		TestTrue(TEXT("stand up after the ramp -> true"), Rig.Aborts.Num() == 1 && Rig.Aborts[0]);
		TestTrue(TEXT("Walking"), Rig.Stroke->GetPhase() == ERbStrokePhase::Walking);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeSettle, "RawBreak.Unit.Stroke.SettleAndHead", RB_UNIT_TEST_FLAGS)
bool FRbStrokeSettle::RunTest(const FString& Parameters)
{
	FStrokeRig Rig(MakeTestContext());
	Rig.GetDown();
	Rig.RunUntil(Rig.Clock + 0.4);
	Rig.Clock += 0.0123;
	const double SettleAt = Rig.Clock;
	Rig.Stroke->SetSettleHeld(true);
	Rig.Stroke->SetSettleHeld(true); // repeated Triggered keeps the first press
	Rig.Stroke->SetCommitHeld(true);
	double ForwardStart = 0.0;
	FRbScriptedStroke P;
	P.TipSpeed = 1.5;
	P.StartTime = Rig.Clock + 0.1;
	P.StartCueDisplacement = Rig.Stroke->GetCueDisplacement();
	TArray<FRbStrokeSample> Samples;
	double Tc = 0.0;
	RbStrokeMath::MakeScriptedStroke(P, Rig.Stroke->Gain, Samples, &Tc, &ForwardStart);
	Rig.Stroke->InjectStrokeSamples(Samples);
	// A head movement (look input the scripted stroke does not explain) during the committed forward stroke.
	Rig.RunUntil(ForwardStart + 0.05);
	const double AzimuthBefore = Rig.Stroke->GetAim().Azimuth;
	Rig.Stroke->AddAimInput(FVector2D(40.0, 0.0), false);
	TestEqual(TEXT("aim frozen during the stroke"), Rig.Stroke->GetAim().Azimuth, AzimuthBefore);
	Rig.RunFrames(60.0, Samples.Last().Time + 0.1);
	if (!TestEqual(TEXT("contact"), Rig.Contacts.Num(), 1))
	{
		return false;
	}
	const rb::human::IntendedStroke& I = Rig.Contacts[0].Intended;
	TestEqual(TEXT("SettleStart = press - down"), I.SettleStart, SettleAt - Rig.Stroke->GetDownSince());
	TestTrue(TEXT("HeadMovedBeforeContact"), I.HeadMovedBeforeContact);
	// Pause at the back (HF-08): the scripted still pause of 0.3 s plus the dwell inside the 1 mm band.
	TestTrue(TEXT("PauseDuration ~ scripted pause"), I.PauseDuration >= P.Pause && I.PauseDuration < P.Pause + 0.06);
	TestTrue(TEXT("ForwardStart = end of the pause"), I.ForwardStart >= ForwardStart - Rig.Stroke->GetDownSince() &&
		I.ForwardStart < ForwardStart - Rig.Stroke->GetDownSince() + 0.04);
	// Contact acceleration: uniformly accelerated hand a_m, through the gain curve's slope.
	const double Hand = RbStrokeMath::HandSpeedForCueSpeed(P.TipSpeed, Rig.Stroke->Gain);
	const double HandAccel = RbStrokeMath::CueTravelIntegral(Hand, Rig.Stroke->Gain) / P.ForwardTravel;
	TestNearlyEqual(TEXT("ContactAcceleration"), I.ContactAcceleration, RbStrokeMath::CueSpeedDerivative(Hand, Rig.Stroke->Gain) * HandAccel, 1e-6);

	// Releasing Settle clears it; no head movement without look input.
	FStrokeRig Calm(MakeTestContext());
	Calm.GetDown();
	Calm.Stroke->SetSettleHeld(true);
	Calm.Stroke->SetSettleHeld(false);
	Calm.Stroke->SetCommitHeld(true);
	const TArray<FRbStrokeSample> CalmSamples = Calm.Stroke->MakeScriptedStroke(1.5, Calm.Clock + 0.1);
	Calm.Stroke->InjectStrokeSamples(CalmSamples);
	Calm.RunFrames(60.0, CalmSamples.Last().Time + 0.1);
	if (TestEqual(TEXT("calm contact"), Calm.Contacts.Num(), 1))
	{
		TestEqual(TEXT("released Settle -> -1"), Calm.Contacts[0].Intended.SettleStart, -1.0);
		TestFalse(TEXT("no head movement"), Calm.Contacts[0].Intended.HeadMovedBeforeContact);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeSteering, "RawBreak.Unit.Stroke.Steering", RB_UNIT_TEST_FLAGS)
bool FRbStrokeSteering::RunTest(const FString& Parameters)
{
	// Lateral hand motion during the committed forward stroke: grip right -> azimuth + y_g / L_bg, axis offset
	// - y_g (L_b + R) / (L_bg R) (T11 geometry through the bridge pivot), swoop to the left.
	const FRbStrokeContext Context = MakeTestContext();
	FStrokeRig Rig(Context);
	Rig.GetDown();
	Rig.Stroke->SetCommitHeld(true);
	FRbScriptedStroke P;
	P.TipSpeed = 2.0;
	P.StartTime = Rig.Clock + 0.1;
	P.StartCueDisplacement = Rig.Stroke->GetCueDisplacement();
	P.LateralDrift = 0.02;
	TArray<FRbStrokeSample> Samples;
	RbStrokeMath::MakeScriptedStroke(P, Rig.Stroke->Gain, Samples);
	Rig.Stroke->InjectStrokeSamples(Samples);
	Rig.RunFrames(60.0, Samples.Last().Time + 0.1);
	if (!TestEqual(TEXT("contact"), Rig.Contacts.Num(), 1))
	{
		return false;
	}
	const rb::human::IntendedStroke& I = Rig.Contacts[0].Intended;
	const FRbAimState& Aim = Rig.Stroke->GetAim();
	const double Yaw = I.Azimuth - Aim.Azimuth;
	const double ShiftR = (I.AxisOffsetA - Aim.AxisOffsetA) * kR;
	const double Lb = Context.Situation.BridgeLength;
	const double Lbg = Context.Situation.BridgeToGrip;
	const double GripLateral = Yaw * Lbg;
	TestTrue(TEXT("grip right: azimuth increases (tip left)"), Yaw > 0.0);
	TestTrue(TEXT("y_g = G_lat x lateral travel since ForwardStart"), GripLateral > 0.25 * 0.02 * 0.6 && GripLateral <= 0.25 * 0.02 + 1e-12);
	TestNearlyEqual(TEXT("axis shift = -yaw (L_b + R)"), ShiftR, -Yaw * (Lb + kR), 1e-12);
	TestTrue(TEXT("swoop to the left"), I.TipVelocityRight < 0.0);
	TestEqual(TEXT("elevation / B unchanged"), I.AxisOffsetB, Aim.AxisOffsetB);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeContextHook, "RawBreak.Unit.Stroke.ContextAndAutoChalk", RB_UNIT_TEST_FLAGS)
bool FRbStrokeContextHook::RunTest(const FString& Parameters)
{
	// Hooks of the director (UE-6b): auto-chalk at the start of a visit (PerformChalking, architecture 7.1) and the context
	// pushed after an abort that spent the draws (ShooterShotIndex++, HF-B13) both arrive through SetStrokeContext, possibly
	// while down. The component keeps its own fields (AddressIndex, elevation floor), renders with the new key at once, and
	// the commit carries exactly the pushed tip / key / history (ExecuteStroke must use Commit.Context).
	FRbStrokeContext Worn = MakeTestContext();
	for (double& Coverage : Worn.Tip.Coverage)
	{
		Coverage = 0.15;
	}
	FStrokeRig Rig(Worn);
	Rig.GetDown();
	Rig.RunUntil(Rig.Clock + 0.3);
	Rig.Stroke->RequestGetDownToggle(); // stand up: AddressIndex 1
	TestEqual(TEXT("AddressIndex 1"), static_cast<int32>(Rig.Stroke->GetAddressIndex()), 1);

	FRbStrokeContext Chalked = Worn;
	double Duration = 0.0;
	const int Twists = rb::human::PerformChalking(Chalked.Tip, rb::human::ChalkCube{}, rb::human::ChoreMode::Automatic, 0.6, 0.0, -1, Duration);
	TestTrue(TEXT("auto-chalk twisted"), Twists > 0 && Chalked.Tip.Coverage[0] > Worn.Tip.Coverage[0]);
	Rig.Stroke->SetStrokeContext(Chalked);
	TestEqual(TEXT("pushed context keeps the AddressIndex"), static_cast<int32>(Rig.Stroke->GetContext().Key.AddressIndex), 1);

	// Down again; an abort spends the draws, the director pushes the next shooter shot index while the player stays down.
	Rig.GetDown();
	const double Floor = Rig.Stroke->GetContext().Situation.ElevationFloor;
	const rb::human::HandPose Before = Rig.Stroke->GetHandPose();
	FRbStrokeContext Next = Chalked;
	Next.Key.ShooterShotIndex += 1;
	Next.History = rb::human::RebuildNoiseHistory(Next.Key.MatchSeed, rb::human::ShooterKey(Next.Key), Next.Key.ShooterShotIndex);
	Next.Situation.ElevationFloor = 1.0; // the director's value is ignored: the floor belongs to the component (RbCueClearance)
	Rig.Stroke->SetStrokeContext(Next);
	TestEqual(TEXT("floor kept"), Rig.Stroke->GetContext().Situation.ElevationFloor, Floor);
	TestEqual(TEXT("AddressIndex kept while down"), static_cast<int32>(Rig.Stroke->GetContext().Key.AddressIndex), 1);
	Rig.RunUntil(Rig.Clock + 0.25);
	TestTrue(TEXT("the pushed context renders at once"), SamePose(Rig.Stroke->GetHandPose(), ExpectedIdlePose(*Rig.Stroke, Rig.Clock)));
	TestTrue(TEXT("the pose time advanced"), Rig.Stroke->GetHandPose().Time > Before.Time);

	Rig.Stroke->SetCommitHeld(true);
	const TArray<FRbStrokeSample> Samples = Rig.Stroke->MakeScriptedStroke(2.2, Rig.Clock + 0.1);
	Rig.Stroke->InjectStrokeSamples(Samples);
	Rig.RunFrames(60.0, Samples.Last().Time + 0.1);
	if (!TestEqual(TEXT("contact"), Rig.Contacts.Num(), 1))
	{
		return false;
	}
	const FRbStrokeCommit& C = Rig.Contacts[0];
	bool bSameTip = true;
	for (int32 z = 0; z < rb::human::kTipZoneCount; ++z)
	{
		bSameTip &= SameBits(C.Context.Tip.Coverage[z], Chalked.Tip.Coverage[z]);
	}
	TestTrue(TEXT("commit carries the chalked tip"), bSameTip);
	TestEqual(TEXT("commit carries the pushed ShooterShotIndex"), static_cast<int32>(C.Context.Key.ShooterShotIndex), static_cast<int32>(Next.Key.ShooterShotIndex));
	TestEqual(TEXT("commit AddressIndex"), static_cast<int32>(C.Context.Key.AddressIndex), 1);
	TestTrue(TEXT("contact pose = SampleHand with the pushed context"), SamePose(C.ContactPose, SampleWithContext(C.Intended, C.Context, C.Intended.TimeDown)));
	return true;
}

#if PLATFORM_WINDOWS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokeRawThread, "RawBreak.Unit.Stroke.RawThreadStroke", RB_UNIT_TEST_FLAGS)
bool FRbStrokeRawThread::RunTest(const FString& Parameters)
{
	// End to end through the raw-input thread (review R-01): reports stamped on ARRIVAL by the thread -> SPSC ring -> the
	// component's hand samples -> contact carrying true timestamps. Mouse forward (away from the user, raw DeltaY < 0) moves the
	// cue toward the ball. The component runs on the real clock here, the domain the thread stamps in.
	FRbRawMouseInputOptions Options;
	Options.bForceActive = true;     // headless: the thread runs but never registers the mouse (no game window)
	Options.bInstallHandler = false;
	Options.bForwardToSlate = false;
	TSharedPtr<FRbRawMouseInput> Raw = FRbRawMouseInput::CreateWithOptions(Options);
	if (!TestTrue(TEXT("input thread running"), Raw->IsThreadRunning()))
	{
		return false;
	}
	FStrokeRig Rig(MakeTestContext());
	Rig.Stroke->ClockOverride = []() { return FPlatformTime::Seconds(); };
	Rig.Stroke->SetRawMouseInput(Raw);
	Rig.Stroke->GetDownSeconds = 0.0;
	Rig.Stroke->RequestGetDownToggle();
	Rig.Stroke->TickStroke(FPlatformTime::Seconds());
	if (!TestTrue(TEXT("down"), Rig.Stroke->GetPhase() == ERbStrokePhase::Down))
	{
		return false;
	}
	TestTrue(TEXT("true timestamps"), Rig.Stroke->HasTrueTimestamps());
	Rig.Stroke->SetCommitHeld(true);
	Rig.Stroke->SetStrokeHeld(true);
	TestTrue(TEXT("stroke source active"), Rig.Stroke->IsStrokeActive());

	// Reports of 30 counts (0.95 mm of hand at 800 dpi) about 1 ms apart, a tick every 4 reports: the 30 mm address gap is
	// crossed after roughly a dozen reports.
	constexpr int32 CountsPerReport = 30;
	for (int32 k = 0; k < 200 && Rig.Contacts.Num() == 0; ++k)
	{
		Raw->InjectTestReport(0, -CountsPerReport);
		FPlatformProcess::Sleep(0.001f);
		if (k % 4 == 3)
		{
			Rig.Stroke->TickStroke(FPlatformTime::Seconds());
		}
	}
	const double Deadline = FPlatformTime::Seconds() + 2.0;
	while (Rig.Contacts.Num() == 0 && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.001f);
		Rig.Stroke->TickStroke(FPlatformTime::Seconds());
	}
	Rig.Stroke->SetRawMouseInput(nullptr);
	if (!TestEqual(TEXT("one contact from raw reports"), Rig.Contacts.Num(), 1))
	{
		return false;
	}
	const FRbStrokeCommit& C = Rig.Contacts[0];
	TestTrue(TEXT("commit carries true timestamps"), C.bTrueTimestamps);
	TestTrue(TEXT("speed in range"), C.Intended.Speed > 0.0 && C.Intended.Speed <= Rig.Stroke->Gain.VTipMax);
	const double MetersPerCount = RbStrokeMath::CountsToMeters(1.0, Rig.Stroke->MouseDpi);
	bool bPositions = C.InputLog.Num() >= 3 && C.InputLog[0].Position == 0.0;
	bool bOrdered = true;
	for (int32 i = 1; i < C.InputLog.Num(); ++i)
	{
		// The reference sample at the press, then one sample per report: hand position = accumulated counts.
		bPositions &= FMath::IsNearlyEqual(C.InputLog[i].Position, i * CountsPerReport * MetersPerCount, 1e-12);
		bOrdered &= C.InputLog[i].Time >= C.InputLog[i - 1].Time;
	}
	TestTrue(TEXT("hand positions from the counts (forward = +)"), bPositions);
	TestTrue(TEXT("report times ordered"), bOrdered);
	const double Span = C.InputLog.Last().Time - C.InputLog[1].Time;
	AddInfo(FString::Printf(TEXT("%d raw reports over %.2f ms, V = %.3f m/s"), C.InputLog.Num() - 1, Span * 1000.0, C.Intended.Speed));
	TestTrue(TEXT("per-report stamps, not one burst (reports posted >= 1 ms apart)"), Span >= 0.3e-3 * (C.InputLog.Num() - 2));
	TestTrue(TEXT("contact inside the last report step"), C.ContactTime >= C.InputLog[C.InputLog.Num() - 2].Time && C.ContactTime <= C.InputLog.Last().Time);
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbStrokePlacement, "RawBreak.Unit.Stroke.PlacementConfirm", RB_UNIT_TEST_FLAGS)
bool FRbStrokePlacement::RunTest(const FString& Parameters)
{
	FStrokeRig Rig(MakeTestContext());
	Rig.Stroke->BeginCueBallPlacement();
	TestTrue(TEXT("PlacingCueBall"), Rig.Stroke->GetPhase() == ERbStrokePhase::PlacingCueBall);
	Rig.Stroke->SetStrokeHeld(true); // left mouse in hand = Confirm (R-17)
	TestEqual(TEXT("a Stroke press confirms"), Rig.Placed.Num(), 1);
	Rig.Stroke->SetStrokeHeld(true); // Triggered repeats while held
	Rig.Stroke->TickStroke(Rig.Clock += 0.1);
	TestEqual(TEXT("once per press"), Rig.Placed.Num(), 1);
	TestTrue(TEXT("placement at the cue ball (no camera / table)"), Rig.Placed.Num() == 1 && Rig.Placed[0].Equals(FRbCoords::PositionToUE(kCueBall), 1e-9));
	Rig.Stroke->SetStrokeHeld(false);
	Rig.Stroke->ConfirmPressed(); // Enter / F
	TestEqual(TEXT("Confirm key"), Rig.Placed.Num(), 2);
	// The director accepted: next shot; the button that confirmed must be released before it strokes.
	Rig.Stroke->SetStrokeHeld(true);
	TestEqual(TEXT("second press"), Rig.Placed.Num(), 3);
	Rig.Stroke->BeginAddress(kCueBall, kR);
	Rig.GetDown();
	Rig.Stroke->SetStrokeHeld(true);
	TestFalse(TEXT("held Confirm press does not start a stroke"), Rig.Stroke->IsStrokeActive());
	TestEqual(TEXT("no confirm while down"), Rig.Placed.Num(), 3);
	Rig.Stroke->SetStrokeHeld(false);
	Rig.Stroke->SetStrokeHeld(true);
	TestTrue(TEXT("a fresh press strokes"), Rig.Stroke->IsStrokeActive());
	Rig.Stroke->SetStrokeHeld(false);
	TestFalse(TEXT("release ends the stroke"), Rig.Stroke->IsStrokeActive());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
