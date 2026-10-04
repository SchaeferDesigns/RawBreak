// Offline tests of the audio v1 plan builder and live synthesis (Docs/specs/audio.md 14, Docs/ue-architecture.md 18.5):
//   AU-T12   event coverage of the dive-bar 8-ball break: the C++ scenario reproduces the prototype's rbsim event log and the plan
//            holds its 79 audible impacts + 3 gully runs, pressing / too-slow contacts silent
//   AU-T13   replay determinism: the same shot planned and rendered twice (and with other block sizes) is bit-identical
//   AU-T16   presentation of the dive-bar break at the breaker's ears per dynamic-range mode (true peak before the limiter)
//   AU-T19   offline render cost of the densest device block of the break (all 30 voices of the table)
//   plans    the test-room break9, the audio tiers T1 / T2 (audio.md 6.6), emitter positions
//   live     loose-ball floor hits and footsteps (AU-25, AU-65), venue profiles, tier hysteresis, the volume taper.
// No audio device, no world. Owner: M2-C.

#include "Tests/RbTestFlags.h"

#include "Audio/RbAudioLive.h"
#include "Audio/RbAudioPlan.h"
#include "Audio/RbAudioScenarios.h"
#include "Audio/RbAudioSettings.h"
#include "Audio/RbAudioSubsystem.h"
#include "Audio/RbAudioVenue.h"
#include "Core/RbAssetPaths.h"
#include "Simulation/RbShot.h"
#include "Simulation/RbTableContext.h"

#include "RbAudio/RbAudioAnalysis.h"
#include "RbAudio/RbJsonLite.h"
#include "RbAudio/RbPresentation.h"
#include "RbAudio/RbShotAudioClock.h"
#include "RbAudio/RbVoiceRenderer.h"

#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbAudioTests
{
	using namespace RbAudio;

	const TCHAR* EventTypeName(rb::ShotEventType Type)
	{
		static const TCHAR* Names[] = {TEXT("CueStrike"), TEXT("TipRecontact"), TEXT("TipContactBegin"), TEXT("TipContactEnd"), TEXT("BallBall"),
			TEXT("BallCushion"), TEXT("BallJaw"), TEXT("BallRailTop"), TEXT("BallSlate"), TEXT("BallAirborne"), TEXT("BallLand"), TEXT("BallPocketEnter"),
			TEXT("BallPocketRim"), TEXT("BallLiner"), TEXT("BallPocketExit"), TEXT("BallPocketed"), TEXT("BallOffTable"), TEXT("BallExternalContact"),
			TEXT("MotionTransition"), TEXT("BallLineCross"), TEXT("BallJumpedOver"), TEXT("IslandBegin"), TEXT("IslandRigid"), TEXT("IslandEnd"),
			TEXT("ZenoGuard"), TEXT("Diagnostic"), TEXT("TiltRefresh")};
		const int32 I = static_cast<int32>(Type);
		return I >= 0 && I < static_cast<int32>(UE_ARRAY_COUNT(Names)) ? Names[I] : TEXT("?");
	}

	// The dive-bar break (simulated once per test run) and the breaker's head.
	struct FBreak
	{
		TSharedPtr<FRbShot> Shot;
		rb::Vec3 Head;
		rb::Vec3 Left;
		FString Error;
	};

	FBreak MakeBreak(bool bDiveBar)
	{
		FBreak B;
		FRbShotRequest Request;
		const bool bOk = bDiveBar ? RbAudioScenarios::MakeDiveBarBreak8(Request, B.Error) : RbAudioScenarios::MakeTestRoomBreak9(Request, B.Error);
		if (!bOk)
		{
			return B;
		}
		RbAudioScenarios::BreakerHead(Request, B.Head, B.Left);
		B.Shot = RbAudioScenarios::Simulate(MoveTemp(Request), B.Error);
		return B;
	}

	FRbAudioPlanOptions Options(ERbTableAudioTier Tier, EDynamicRangeMode Mode, uint64 Seed)
	{
		FRbAudioPlanOptions O;
		O.Tier = Tier;
		O.Mode = Mode;
		O.RefDistance = 0.25;
		O.DirectivityFloorDb = -20.0;
		O.PanCompensation = 1.0;
		O.ShotId = 1;
		O.Seed = Seed;
		return O;
	}

	// Renders every voice of a plan offline through FVoiceRenderer (anchored at frame 0), Frames frames, BlockFrames per block.
	void RenderVoices(const FRbShotAudioPlan& Plan, double Fs, int32 BlockFrames, int32 Frames, TArray<TArray<float>>& Out, TArray<double>* OutBlockMicros = nullptr)
	{
		FShotAudioClock Clock;
		Clock.StartShotAnchored(1, 0, 0.0, 1.0, Fs);
		TArray<FVoiceRenderer> Renderers;
		Renderers.SetNum(Plan.Voices.Num());
		Out.SetNum(Plan.Voices.Num());
		for (int32 V = 0; V < Plan.Voices.Num(); ++V)
		{
			Renderers[V].Initialize(Fs, BlockFrames);
			Renderers[V].SetPlan(MakeShared<const FVoicePlan, ESPMode::ThreadSafe>(Plan.Voices[V]));
			Out[V].Reset();
		}
		TArray<float> Block;
		for (int64 F = 0; F < Frames; F += BlockFrames)
		{
			const uint64 T0 = FPlatformTime::Cycles64();
			for (int32 V = 0; V < Plan.Voices.Num(); ++V)
			{
				Block.SetNumZeroed(BlockFrames);
				Renderers[V].RenderBlock(&Clock, F, Block);
				Out[V].Append(Block);
			}
			if (OutBlockMicros)
			{
				OutBlockMicros->Add(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1000.0);
			}
		}
	}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioT12, "RawBreak.Unit.Audio.AU_T12_EventCoverage_DiveBarBreak8", RB_UNIT_TEST_FLAGS)
bool FRbAudioT12::RunTest(const FString& Parameters)
{
	const FBreak B = MakeBreak(true);
	if (!TestTrue(FString::Printf(TEXT("dive-bar break simulated (%s)"), *B.Error), B.Shot.IsValid() && B.Shot->Result.Status == rb::SimStatus::Ok))
	{
		return false;
	}
	const rb::ShotResult& R = B.Shot->Result;
	// 1. The scenario is the prototype's shot: the event log of Tools/audio/out/ref/divebar_break8_events.json.
	FJsonLite Ref;
	if (TestTrue(TEXT("divebar_break8_events.json readable"),
		FJsonLite::ParseFile(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/audio/out/ref/divebar_break8_events.json")), Ref)))
	{
		const FJsonLite& Events = Ref[TEXT("events")];
		const int32 N = static_cast<int32>(R.Events.size());
		TestEqual(TEXT("event count == rbsim"), N, Events.Num());
		int32 FirstMismatch = INDEX_NONE;
		double MaxDt = 0.0;
		for (int32 I = 0; I < FMath::Min(N, Events.Num()); ++I)
		{
			const FJsonLite& E = Events.At(I);
			const rb::ShotEvent& S = R.Events[I];
			const bool bSame = E[TEXT("type")].String == EventTypeName(S.Type) && static_cast<int32>(E[TEXT("a")].AsNumber()) == (S.A == rb::kNoBall ? -1 : S.A)
				&& static_cast<int32>(E[TEXT("b")].AsNumber()) == (S.B == rb::kNoBall ? -1 : S.B);
			MaxDt = FMath::Max(MaxDt, FMath::Abs(E[TEXT("t")].AsNumber() - S.Time));
			if (!bSame && FirstMismatch == INDEX_NONE)
			{
				FirstMismatch = I;
			}
		}
		TestTrue(FString::Printf(TEXT("event types and balls == rbsim (first mismatch %d)"), FirstMismatch), FirstMismatch == INDEX_NONE);
		TestTrue(FString::Printf(TEXT("event times == rbsim (max |dt| %.3g s, rbsim JSON has 17 significant digits)"), MaxDt), MaxDt <= 1e-9);
	}
	// 2. The plan: 79 audible impacts + 3 gully runs (click_synth.py events_to_impacts), pressing / slow contacts silent.
	FRbShotAudioPlan Plan;
	FRbAudioPlanBuilder::Build(R, *B.Shot->Request.Table, B.Head, Options(ERbTableAudioTier::T0, EDynamicRangeMode::Wide, B.Shot->ResultHash), Plan);
	TestEqual(TEXT("AU-T12: 79 audible impacts"), Plan.Impacts.Num(), 79);
	TestEqual(TEXT("AU-T12: 3 gully runs (coin-op, one per pocketed ball)"), Plan.NumGullyRuns, 3);
	int32 Silent = 0;
	for (const rb::ShotEvent& E : R.Events)
	{
		const bool bContact = E.Type == rb::ShotEventType::BallBall || E.Type == rb::ShotEventType::BallCushion || E.Type == rb::ShotEventType::BallJaw
			|| E.Type == rb::ShotEventType::BallRailTop;
		if ((bContact && ((E.Flags & rb::ShotEventFlags::Pressing) != 0 || FMath::Abs(E.NormalSpeed) < 0.003))
			|| (E.Type == rb::ShotEventType::BallSlate && FMath::Abs(E.NormalSpeed) < 0.01))
		{
			++Silent;
		}
	}
	TestEqual(TEXT("pressing / too slow contacts are silent (skipped, never rendered)"), Plan.NumSkippedEvents, Silent);
	// Every plan impact reaches at least one voice; every voice list is sorted; the classes of the break are all there.
	TSet<int32> Ids;
	int32 VoiceEvents = 0;
	for (const FVoicePlan& V : Plan.Voices)
	{
		for (int32 I = 0; I < V.Impacts.Num(); ++I)
		{
			Ids.Add(V.Impacts[I].ImpactId);
			++VoiceEvents;
			TestTrue(TEXT("voice events sorted"), I == 0 || V.Impacts[I - 1].ShotTime <= V.Impacts[I].ShotTime);
		}
	}
	TestEqual(TEXT("every impact has a voice event"), Ids.Num(), Plan.Impacts.Num());
	TMap<EImpactKind, int32> Classes;
	for (const FRbAudioPlanImpact& I : Plan.Impacts)
	{
		Classes.FindOrAdd(I.Kind)++;
	}
	for (const EImpactKind Kind : {EImpactKind::TipStrike, EImpactKind::BallBall, EImpactKind::BallCushion, EImpactKind::BallJaw, EImpactKind::BallSlate,
		EImpactKind::BallLiner, EImpactKind::PocketDrop, EImpactKind::TrapClick})
	{
		TestTrue(FString::Printf(TEXT("class %s present (%d)"), RbAudio::ToString(Kind), Classes.FindRef(Kind)), Classes.FindRef(Kind) > 0);
	}
	int32 Rolling = 0;
	for (const FVoicePlan& V : Plan.Voices)
	{
		for (const FContinuousSegment& S : V.Continuous)
		{
			Rolling += S.Kind == ENoiseKind::RollingCloth ? 1 : 0;
		}
	}
	TestTrue(FString::Printf(TEXT("rolling layers from the ball tracks (%d segments)"), Rolling), Rolling > 10);
	AddInfo(FString::Printf(TEXT("dive-bar break plan: %d impacts on %d voice events, %d gully runs, stem peak %.1f dB SPL at the breaker's ears, built in %.1f ms"),
		Plan.Impacts.Num(), VoiceEvents, Plan.NumGullyRuns, Plan.StemPeakSpl, Plan.BuildMilliseconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioT13, "RawBreak.Unit.Audio.AU_T13_ReplayDeterminism", RB_UNIT_TEST_FLAGS)
bool FRbAudioT13::RunTest(const FString& Parameters)
{
	const FBreak B = MakeBreak(true);
	if (!TestTrue(TEXT("break simulated"), B.Shot.IsValid()))
	{
		return false;
	}
	const double Fs = 48000.0;
	FRbShotAudioPlan P1, P2;
	FRbAudioPlanBuilder::Build(B.Shot->Result, *B.Shot->Request.Table, B.Head, Options(ERbTableAudioTier::T0, EDynamicRangeMode::Wide, B.Shot->ResultHash), P1);
	FRbAudioPlanBuilder::Build(B.Shot->Result, *B.Shot->Request.Table, B.Head, Options(ERbTableAudioTier::T0, EDynamicRangeMode::Wide, B.Shot->ResultHash), P2);
	AddInfo(FString::Printf(TEXT("plan build times: %.1f ms (cold caches: kernels of every ball) / %.1f ms (warm), of which the presentation render %.1f / %.1f ms"),
		P1.BuildMilliseconds, P2.BuildMilliseconds, P1.PresentationMilliseconds, P2.PresentationMilliseconds));
	TestTrue(FString::Printf(TEXT("a break's plan is ready in < 20 ms once the table's caches are warm (%.1f ms; audio.md 8.3)"), P2.BuildMilliseconds),
		P2.BuildMilliseconds < 20.0);
	TestTrue(TEXT("presentation envelopes identical"), P1.Presentation.IsValid() && P2.Presentation.IsValid() && P1.Presentation->Gains == P2.Presentation->Gains);
	const int32 Frames = static_cast<int32>(4.0 * Fs);
	TArray<TArray<float>> A, C, D;
	RenderVoices(P1, Fs, 512, Frames, A);
	RenderVoices(P2, Fs, 512, Frames, C);
	RenderVoices(P2, Fs, 480, Frames, D);
	int32 Diff = 0;
	int32 DiffBlocks = 0;
	double Energy = 0.0;
	for (int32 V = 0; V < A.Num(); ++V)
	{
		for (int32 I = 0; I < Frames; ++I)
		{
			Diff += A[V][I] != C[V][I] ? 1 : 0;
			DiffBlocks += A[V][I] != D[V][I] ? 1 : 0;
			Energy += static_cast<double>(A[V][I]) * A[V][I];
		}
	}
	TestTrue(TEXT("the break sounds"), Energy > 0.0);
	TestEqual(TEXT("AU-T13: the same shot rendered twice is bit-identical"), Diff, 0);
	TestEqual(TEXT("AU-T13: and with 480-frame device blocks too"), DiffBlocks, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioT16, "RawBreak.Unit.Audio.AU_T16_PresentationDiveBarBreak", RB_UNIT_TEST_FLAGS)
bool FRbAudioT16::RunTest(const FString& Parameters)
{
	const FBreak B = MakeBreak(true);
	if (!TestTrue(TEXT("break simulated"), B.Shot.IsValid()))
	{
		return false;
	}
	const double Fs = 48000.0;
	const double Want[3] = {-1.5, -1.2, -1.2}; // prototype, audio.md 3.7 / 14 AU-T16 (stereo stem incl. its room tail)
	for (const EDynamicRangeMode Mode : {EDynamicRangeMode::Wide, EDynamicRangeMode::Normal, EDynamicRangeMode::Night})
	{
		const FPresentationMode M = GetPresentationMode(Mode);
		// Both ears (+-8.75 cm), the envelope from the plan's centre listener (the engine plans at the listener, audio.md 4.2).
		FRbShotAudioPlan Centre;
		FRbAudioPlanBuilder::Build(B.Shot->Result, *B.Shot->Request.Table, B.Head, Options(ERbTableAudioTier::T0, Mode, B.Shot->ResultHash), Centre, true);
		double TruePeak = -300.0;
		for (int32 Ear = 0; Ear < 2; ++Ear)
		{
			const rb::Vec3 At = B.Head + B.Left * (Ear == 0 ? 0.0875 : -0.0875);
			FRbShotAudioPlan EarPlan;
			FRbAudioPlanOptions O = Options(ERbTableAudioTier::T0, Mode, B.Shot->ResultHash);
			O.bPresentation = false;
			FRbAudioPlanBuilder::Build(B.Shot->Result, *B.Shot->Request.Table, At, O, EarPlan);
			TArray<double> Stem;
			FRbAudioPlanBuilder::RenderImpactsAtListener(EarPlan, Fs, O.RefDistance, Centre.MonoStemStartTime, 8.0, Stem);
			TArray<float> Presented;
			Presented.SetNumUninitialized(Stem.Num());
			for (int32 I = 0; I < Stem.Num(); ++I)
			{
				const double T = Centre.MonoStemStartTime + I / Fs;
				Presented[I] = static_cast<float>(Stem[I] * Centre.Presentation->Evaluate(T) / M.FullScalePa());
			}
			TruePeak = FMath::Max(TruePeak, TruePeakDbtp(Presented, 1));
		}
		const int32 Index = static_cast<int32>(Mode);
		AddInfo(FString::Printf(TEXT("%s: dive-bar break at the breaker's ears %.2f dBTP before the limiter (prototype %.1f)"), ToString(Mode), TruePeak,
			Want[Index]));
		TestTrue(FString::Printf(TEXT("AU-T16 %s: %.2f dBTP <= -0.9 (the master limiter then acts by <= 1 dB)"), ToString(Mode), TruePeak), TruePeak <= -0.9);
		TestTrue(FString::Printf(TEXT("AU-T16 %s: %.2f dBTP within the prototype's %.1f +- 0.6 dB (no room tail in this render)"), ToString(Mode), TruePeak,
			Want[Index]), FMath::Abs(TruePeak - Want[Index]) <= 0.6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioT19, "RawBreak.Unit.Audio.AU_T19_OfflineRenderCost", RB_UNIT_TEST_FLAGS)
bool FRbAudioT19::RunTest(const FString& Parameters)
{
	const FBreak B = MakeBreak(true);
	if (!TestTrue(TEXT("break simulated"), B.Shot.IsValid()))
	{
		return false;
	}
	const double Fs = 48000.0;
	FRbShotAudioPlan Plan;
	FRbAudioPlanBuilder::Build(B.Shot->Result, *B.Shot->Request.Table, B.Head, Options(ERbTableAudioTier::T0, EDynamicRangeMode::Wide, B.Shot->ResultHash), Plan);
	TArray<TArray<float>> Out;
	TArray<double> Micros;
	RenderVoices(Plan, Fs, 512, static_cast<int32>(1.0 * Fs), Out, &Micros);
	double Max = 0.0;
	int32 At = 0;
	for (int32 I = 0; I < Micros.Num(); ++I)
	{
		if (Micros[I] > Max)
		{
			Max = Micros[I];
			At = I;
		}
	}
	const double BlockMicros = 512.0 / Fs * 1e6;
	AddInfo(FString::Printf(TEXT("densest block %d (%.1f ms into the shot): all 30 voices rendered in %.0f us on one thread = %.1f %% of a %.2f ms block"),
		At, At * 512.0 / Fs * 1e3, Max, 100.0 * Max / BlockMicros, BlockMicros / 1000.0));
	TestTrue(FString::Printf(TEXT("AU-T19 (offline, one thread): %.0f us <= 60 %% of a block (%.0f us)"), Max, 0.6 * BlockMicros), Max <= 0.6 * BlockMicros);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioPlanBreak9, "RawBreak.Unit.Audio.Plan_TestRoomBreak9", RB_UNIT_TEST_FLAGS)
bool FRbAudioPlanBreak9::RunTest(const FString& Parameters)
{
	const FBreak B = MakeBreak(false);
	if (!TestTrue(FString::Printf(TEXT("break9 simulated (%s)"), *B.Error), B.Shot.IsValid()))
	{
		return false;
	}
	FRbShotAudioPlan Plan;
	FRbAudioPlanBuilder::Build(B.Shot->Result, *B.Shot->Request.Table, B.Head, Options(ERbTableAudioTier::T0, EDynamicRangeMode::Wide, 5), Plan);
	TestFalse(TEXT("the pro 9-ft table is no coin-op"), FRbAudioPlanBuilder::IsCoinOp(*B.Shot->Request.Table));
	TestEqual(TEXT("no gully runs on a pro table"), Plan.NumGullyRuns, 0);
	TestEqual(TEXT("30 voices (T0)"), Plan.Voices.Num(), FRbAudioPlanBuilder::NumT0Voices);
	TestTrue(TEXT("the cue voice carries the tip strike"), Plan.Voices[FRbAudioPlanBuilder::CueVoice].Impacts.Num() >= 1
		&& Plan.Voices[FRbAudioPlanBuilder::CueVoice].Impacts[0].Bank == EModalBank::Cue);
	int32 RailEvents = 0;
	for (int32 C = 0; C < 6; ++C)
	{
		for (const FImpactEvent& E : Plan.Voices[FRbAudioPlanBuilder::FirstRailVoice + C].Impacts)
		{
			RailEvents += E.Bank == EModalBank::RailPro ? 1 : 0;
		}
	}
	TestTrue(FString::Printf(TEXT("cushion hits drive the pro rail bank (%d)"), RailEvents), RailEvents > 0);
	TestTrue(FString::Printf(TEXT("break9: %d impacts, stem peak %.1f dB SPL"), Plan.Impacts.Num(), Plan.StemPeakSpl), Plan.Impacts.Num() > 30
		&& Plan.StemPeakSpl > 110.0 && Plan.StemPeakSpl < 135.0);
	TestTrue(TEXT("the plan was built in < 100 ms"), Plan.BuildMilliseconds < 100.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioPlanTiers, "RawBreak.Unit.Audio.Plan_Tiers", RB_UNIT_TEST_FLAGS)
bool FRbAudioPlanTiers::RunTest(const FString& Parameters)
{
	const FBreak B = MakeBreak(true);
	if (!TestTrue(TEXT("break simulated"), B.Shot.IsValid()))
	{
		return false;
	}
	FRbShotAudioPlan T0, T1, T2;
	const rb::Vec3 Far = B.Head + rb::Vec3(-6.0, 3.0, 1.2);
	FRbAudioPlanBuilder::Build(B.Shot->Result, *B.Shot->Request.Table, Far, Options(ERbTableAudioTier::T0, EDynamicRangeMode::Wide, 1), T0);
	FRbAudioPlanBuilder::Build(B.Shot->Result, *B.Shot->Request.Table, Far, Options(ERbTableAudioTier::T1, EDynamicRangeMode::Wide, 1), T1);
	FRbAudioPlanBuilder::Build(B.Shot->Result, *B.Shot->Request.Table, Far, Options(ERbTableAudioTier::T2, EDynamicRangeMode::Wide, 1), T2);
	auto Count = [](const FRbShotAudioPlan& P)
	{
		int32 N = 0;
		for (const FVoicePlan& V : P.Voices)
		{
			N += V.Impacts.Num();
		}
		return N;
	};
	TestEqual(TEXT("T1: 4 quadrant voices"), T1.Voices.Num(), 4);
	TestEqual(TEXT("T2: 1 voice"), T2.Voices.Num(), 1);
	TestEqual(TEXT("T1 keeps every voice event"), Count(T1), Count(T0));
	TestEqual(TEXT("T2 keeps every voice event"), Count(T2), Count(T0));
	bool bReduced = true;
	for (const FVoicePlan& V : T1.Voices)
	{
		for (const FImpactEvent& E : V.Impacts)
		{
			bReduced &= E.NumPaths <= 1 && (E.Kernels.IsValid() ? (E.Paths[0].Weights[0] == 0.0 && E.Paths[0].Weights[2] == 0.0 && E.Paths[0].Weights[3] == 0.0) : true);
		}
	}
	TestTrue(TEXT("T1: order-1 radiation only, no cloth image"), bReduced);
	bool bLowPass = true;
	for (const FImpactEvent& E : T2.Voices[0].Impacts)
	{
		bLowPass &= E.LowPassHz > 0.0 && E.LowPassHz <= 6000.0;
	}
	TestTrue(TEXT("T2: every event low-passed at 6 kHz or below"), bLowPass);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioLiveTest, "RawBreak.Unit.Audio.Live_FloorHitFootstep", RB_UNIT_TEST_FLAGS)
bool FRbAudioLiveTest::RunTest(const FString& Parameters)
{
	const double Fs = 48000.0;
	FRbFloorHitParams P;
	P.NormalSpeed = 1.0;
	P.ContactPointCm = FVector(0.0, 0.0, 0.0);
	P.ListenerCm = FVector(-100.0, 0.0, 0.0) + FVector(0.0, 0.0, 2.8575); // 1 m to the side, at the ball's height
	P.RefDistance = 1.0;
	TArray<float> A, B2;
	TestTrue(TEXT("a 1 m/s floor hit renders"), RbAudioLive::RenderFloorHit(P, Fs, A, 64));
	TestTrue(TEXT("deterministic"), RbAudioLive::RenderFloorHit(P, Fs, B2, 64) && A == B2);
	double Peak = 0.0;
	int32 PeakAt = 0;
	for (int32 I = 0; I < A.Num(); ++I)
	{
		if (FMath::Abs(A[I]) > Peak)
		{
			Peak = FMath::Abs(A[I]);
			PeakAt = I;
		}
	}
	const double Spl = PaToDbSpl(Peak);
	AddInfo(FString::Printf(TEXT("1 m/s ball on VCT, 1 m side-on at floor level: peak %.1f dB SPL at sample %d"), Spl, PeakAt));
	TestTrue(FString::Printf(TEXT("floor hit level plausible (%.1f dB SPL: between a soft click and a 1 m/s ball-ball click at 1 m, 80-110)"), Spl),
		Spl > 80.0 && Spl < 110.0);
	TestTrue(TEXT("the onset sits after the pre-frames"), PeakAt >= 64);
	P.NormalSpeed = 2.0;
	TArray<float> Loud;
	RbAudioLive::RenderFloorHit(P, Fs, Loud, 64);
	double LoudPeak = 0.0;
	for (float S : Loud)
	{
		LoudPeak = FMath::Max(LoudPeak, static_cast<double>(FMath::Abs(S)));
	}
	TestTrue(FString::Printf(TEXT("2 m/s is louder than 1 m/s by %.1f dB (Hertz level law ~+7)"), GainToDb(LoudPeak / Peak)),
		GainToDb(LoudPeak / Peak) > 5.0 && GainToDb(LoudPeak / Peak) < 10.0);
	P.NormalSpeed = 0.001;
	TArray<float> Silent;
	TestFalse(TEXT("below 3 mm/s nothing sounds"), RbAudioLive::RenderFloorHit(P, Fs, Silent));
	// Footstep referred to 0.25 m: 4 x the 1 m pressure.
	FFootstepParams Step;
	Step.Seed = 5;
	Step.bOwnSteps = false;
	TArray<float> AtOne, AtRef;
	FFootstepSynth::Render(Step, Fs, AtOne);
	RbAudioLive::RenderFootstep(Step, 0.25, Fs, AtRef);
	TestTrue(TEXT("footstep referred to 0.25 m = 4 x the 1 m pressure"), AtOne.Num() == AtRef.Num() && FMath::IsNearlyEqual(AtRef[1000], 4.0f * AtOne[1000], 1e-6f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioVenueTest, "RawBreak.Unit.Audio.VenueProfilesTiersVolumes", RB_UNIT_TEST_FLAGS)
bool FRbAudioVenueTest::RunTest(const FString& Parameters)
{
	const FRbVenueAudioProfile Bar = RbGetVenueAudioProfile(ERbVenue::DiveBar);
	const FRbVenueAudioProfile Room = RbGetVenueAudioProfile(ERbVenue::TestRoom);
	auto CountLayer = [](const FRbVenueAudioProfile& P, EAmbienceLayer L)
	{
		int32 N = 0;
		for (const FRbAmbienceEmitter& E : P.Emitters)
		{
			N += E.Layer.Layer == L ? 1 : 0;
		}
		return N;
	};
	TestEqual(TEXT("dive bar: one room-tone bed"), CountLayer(Bar, EAmbienceLayer::HvacBed), 1);
	TestTrue(TEXT("dive bar: HVAC diffusers"), CountLayer(Bar, EAmbienceLayer::HvacDiffuser) >= 1);
	TestEqual(TEXT("dive bar: the two back-bar coolers"), CountLayer(Bar, EAmbienceLayer::Compressor), 2);
	TestEqual(TEXT("dive bar: three neon transformers"), CountLayer(Bar, EAmbienceLayer::NeonHum), 3);
	TestTrue(TEXT("dive bar: VCT floor, RT60 0.8 / 0.6 / 0.5"), Bar.Floor == EFloorSurface::Vct && Bar.Rt60[0] == 0.8 && Bar.Rt60[1] == 0.6 && Bar.Rt60[2] == 0.5);
	TestTrue(TEXT("test room: concrete, a bed and a diffuser"), Room.Floor == EFloorSurface::Concrete && Room.Emitters.Num() == 2);
	for (const FRbAmbienceEmitter& E : Bar.Emitters)
	{
		const bool bInside = E.Layer.Layer == EAmbienceLayer::HvacBed
			|| (E.DefaultLocationCm.X >= 0.0 && E.DefaultLocationCm.X <= 1646.0 && E.DefaultLocationCm.Y >= 0.0 && E.DefaultLocationCm.Y <= 732.0
				&& E.DefaultLocationCm.Z >= 0.0 && E.DefaultLocationCm.Z <= 274.0);
		TestTrue(FString::Printf(TEXT("%s inside the main room (venue frame V)"), *E.Name), bInside);
	}
	TestTrue(TEXT("surface RbVct -> VCT"), RbFloorSurfaceFor(RbAssetPaths::Surface::Vct, EFloorSurface::Concrete) == EFloorSurface::Vct);
	TestTrue(TEXT("surface RbRubber -> rubber"), RbFloorSurfaceFor(RbAssetPaths::Surface::Rubber, EFloorSurface::Vct) == EFloorSurface::Rubber);
	TestTrue(TEXT("unknown surface -> the venue floor"), RbFloorSurfaceFor(SurfaceType_Default, EFloorSurface::Vct) == EFloorSurface::Vct);

	// Tiers (audio.md 6.6): the player's table always T0; 3 / 10 m with a 1 m hysteresis band.
	using T = ERbTableAudioTier;
	TestTrue(TEXT("player table T0 at any distance"), URbAudioSubsystem::TierForDistance(30.0, true, T::T2, 3.0, 10.0) == T::T0);
	TestTrue(TEXT("2 m -> T0"), URbAudioSubsystem::TierForDistance(2.0, false, T::T2, 3.0, 10.0) == T::T0);
	TestTrue(TEXT("5 m -> T1"), URbAudioSubsystem::TierForDistance(5.0, false, T::T0, 3.0, 10.0) == T::T1);
	TestTrue(TEXT("12 m -> T2"), URbAudioSubsystem::TierForDistance(12.0, false, T::T0, 3.0, 10.0) == T::T2);
	TestTrue(TEXT("T0 stays T0 up to 3.5 m"), URbAudioSubsystem::TierForDistance(3.4, false, T::T0, 3.0, 10.0) == T::T0);
	TestTrue(TEXT("T1 stays T1 down to 2.5 m"), URbAudioSubsystem::TierForDistance(2.6, false, T::T1, 3.0, 10.0) == T::T1);
	TestTrue(TEXT("T1 stays T1 up to 10.5 m"), URbAudioSubsystem::TierForDistance(10.4, false, T::T1, 3.0, 10.0) == T::T1);
	TestTrue(TEXT("T2 stays T2 down to 9.5 m"), URbAudioSubsystem::TierForDistance(9.6, false, T::T2, 3.0, 10.0) == T::T2);

	// Volume taper.
	TestEqual(TEXT("slider 0 -> silence"), URbAudioSettings::VolumeToGain(0.0f), 0.0f);
	TestEqual(TEXT("slider 1 -> unity"), URbAudioSettings::VolumeToGain(1.0f), 1.0f);
	TestTrue(TEXT("slider 0.5 -> -12 dB (square law)"), FMath::IsNearlyEqual(URbAudioSettings::VolumeToGain(0.5f), 0.25f));
	return true;
}

} // namespace RbAudioTests

#endif // WITH_DEV_AUTOMATION_TESTS
