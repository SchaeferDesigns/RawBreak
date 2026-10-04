// Offline tests of the audio v1 plan builder and live synthesis (Docs/specs/audio.md 14, Docs/ue-architecture.md 18.5):
//   AU-T12   event coverage of the dive-bar 8-ball break: the C++ scenario reproduces the prototype's rbsim event log and the plan
//            holds its 79 audible impacts + 3 gully runs, pressing / too-slow contacts silent
//   AU-T13   replay determinism: the same shot planned and rendered twice (and with other block sizes) is bit-identical
//   AU-T16   presentation of the dive-bar break at the breaker's ears per dynamic-range mode (true peak before the limiter)
//   AU-T19   offline render cost of the densest device block of the break (all 30 voices of the table)
//   plans    the test-room break9, the audio tiers T1 / T2 (audio.md 6.6), emitter positions
//   live     loose-ball floor hits and footsteps (AU-25, AU-65), venue profiles, tier hysteresis, the volume taper
//   anchors  the room-tone layers at the dive-bar level's RbAudio_* anchors (M2-A's layout.json names).
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

#include "Algo/Reverse.h"
#include "Audio/RbAudioAssets.h"
#include "Audio/RbImpactVoiceComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

#include "rb/Physics/Playback.h"

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

	// Renders every voice of a plan offline through FVoiceRenderer (anchored at frame 0), Frames frames, BlockFrames per block; with
	// bWithFeed the table's reverb feed is rendered as the last entry of Out (it runs on the audio thread like the voices).
	// OutBlockMicros: the CPU time of the calling thread per block (FRbAudioRenderProfile::ThreadCpuMicros, as the engine T19 check):
	// wall time on a shared, busy machine measured preemption (a quiet block 0.6 s into the shot at 79 %, review M2-C).
	void RenderVoices(const FRbShotAudioPlan& Plan, double Fs, int32 BlockFrames, int32 Frames, TArray<TArray<float>>& Out, TArray<double>* OutBlockMicros = nullptr,
		bool bWithFeed = true)
	{
		FShotAudioClock Clock;
		Clock.StartShotAnchored(1, 0, 0.0, 1.0, Fs);
		const int32 NumRenderers = Plan.Voices.Num() + (bWithFeed ? 1 : 0);
		TArray<FVoiceRenderer> Renderers;
		Renderers.SetNum(NumRenderers);
		Out.SetNum(NumRenderers);
		for (int32 V = 0; V < NumRenderers; ++V)
		{
			Renderers[V].Initialize(Fs, BlockFrames);
			Renderers[V].SetPlan(MakeShared<const FVoicePlan, ESPMode::ThreadSafe>(V < Plan.Voices.Num() ? Plan.Voices[V] : Plan.ReverbFeed));
			Out[V].Reset();
		}
		const int32 TotalFrames = (Frames + BlockFrames - 1) / BlockFrames * BlockFrames;
		for (int32 V = 0; V < NumRenderers; ++V)
		{
			Out[V].SetNumZeroed(TotalFrames); // no growth inside the timed blocks (31 arrays reallocating at once measured as render time)
		}
		for (int64 F = 0; F < Frames; F += BlockFrames)
		{
			const double Cpu0 = OutBlockMicros ? FRbAudioRenderProfile::ThreadCpuMicros() : 0.0;
			for (int32 V = 0; V < NumRenderers; ++V)
			{
				Renderers[V].RenderBlock(&Clock, F, TArrayView<float>(Out[V].GetData() + F, BlockFrames));
			}
			if (OutBlockMicros)
			{
				OutBlockMicros->Add(FRbAudioRenderProfile::ThreadCpuMicros() - Cpu0);
			}
		}
		for (int32 V = 0; V < NumRenderers; ++V)
		{
			Out[V].SetNum(Frames); // the requested length
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
	AddInfo(FString::Printf(TEXT("densest block %d (%.1f ms into the shot): all 30 voices + the reverb feed rendered in %.0f us CPU time on one thread = %.1f %% of a %.2f ms block"),
		At, At * 512.0 / Fs * 1e3, Max, 100.0 * Max / BlockMicros, BlockMicros / 1000.0));
	TestTrue(FString::Printf(TEXT("AU-T19 (offline, one thread): %.0f us CPU <= 60 %% of a block (%.0f us)"), Max, 0.6 * BlockMicros), Max <= 0.6 * BlockMicros);
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

	// Renders one voice plan offline (anchored at frame 0) and returns its energy.
	double RenderEnergy(const FVoicePlan& Plan, double Fs, int32 Frames)
	{
		FShotAudioClock Clock;
		Clock.StartShotAnchored(Plan.ShotId, 0, 0.0, 1.0, Fs);
		FVoiceRenderer R;
		R.Initialize(Fs, 512);
		R.SetPlan(MakeShared<const FVoicePlan, ESPMode::ThreadSafe>(Plan));
		TArray<float> Block;
		Block.SetNumZeroed(512);
		double Energy = 0.0;
		for (int64 F = 0; F < Frames; F += 512)
		{
			R.RenderBlock(&Clock, F, Block);
			for (const float S : Block)
			{
				Energy += static_cast<double>(S) * S;
			}
		}
		return Energy;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioPlanReverbFeed, "RawBreak.Unit.Audio.Plan_ReverbFeed_RadiatedPower", RB_UNIT_TEST_FLAGS)
bool FRbAudioPlanReverbFeed::RunTest(const FString& Parameters)
{
	// audio.md 3.6 / 6.4: the room is excited by the radiated power of a sound, independent of where the listener hears it from. The
	// table voices carry the listener's directional signal (a click heard in its dipole's null is ~20 dB down); the reverb feed must
	// not: the same break planned for a listener on the first click's axis (the breaker) and for one beside it (in its null) feeds
	// the reverb with the same energy.
	const FBreak B = MakeBreak(true);
	if (!TestTrue(TEXT("break simulated"), B.Shot.IsValid()))
	{
		return false;
	}
	const rb::ShotResult& R = B.Shot->Result;
	const rb::ShotEvent* First = nullptr;
	for (const rb::ShotEvent& E : R.Events)
	{
		if (E.Type == rb::ShotEventType::BallBall)
		{
			First = &E;
			break;
		}
	}
	if (!TestNotNull(TEXT("the break has a ball-ball click"), First))
	{
		return false;
	}
	rb::BallState Cue;
	rb::StateAt(R, First->A, First->Time, Cue);
	const double Nl = rb::Length(rb::Planar(First->Normal));
	const rb::Vec3 Axis = Nl > 1e-9 ? rb::Planar(First->Normal) / Nl : rb::Vec3(1.0, 0.0, 0.0);
	const rb::Vec3 Side = Cue.Position + rb::Vec3(-Axis.y, Axis.x, 0.0) * 1.2 + rb::Vec3(0.0, 0.0, 0.36);
	FRbAudioPlanOptions O = Options(ERbTableAudioTier::T0, EDynamicRangeMode::Wide, B.Shot->ResultHash);
	O.bPresentation = false; // the envelope depends on the listener; the feed's directivity is what is tested here
	FRbShotAudioPlan OnAxis, SideOn;
	FRbAudioPlanBuilder::Build(R, *B.Shot->Request.Table, B.Head, O, OnAxis);
	FRbAudioPlanBuilder::Build(R, *B.Shot->Request.Table, Side, O, SideOn);

	// The feed holds every voice's events and continuous layers.
	int32 VoiceEvents = 0;
	int32 VoiceSegments = 0;
	for (const FVoicePlan& V : OnAxis.Voices)
	{
		VoiceEvents += V.Impacts.Num();
		VoiceSegments += V.Continuous.Num();
	}
	TestEqual(TEXT("the feed holds every voice event"), OnAxis.ReverbFeed.Impacts.Num(), VoiceEvents);
	TestEqual(TEXT("the feed holds every continuous layer"), OnAxis.ReverbFeed.Continuous.Num(), VoiceSegments);
	TestTrue(TEXT("the feed plays under the voices' shot id"), OnAxis.ReverbFeed.ShotId == OnAxis.Voices[0].ShotId);

	// The voices are directional: the first click on the cue ball's voice is near its axis for the breaker, near the null beside it.
	auto FirstClickWeight = [First](const FRbShotAudioPlan& P)
	{
		for (const FImpactEvent& E : P.Voices[First->A].Impacts)
		{
			if (E.Kind == EImpactKind::BallBall)
			{
				return FMath::Abs(E.Paths[0].Weights[1]);
			}
		}
		return 0.0;
	};
	const double WOn = FirstClickWeight(OnAxis);
	const double WSide = FirstClickWeight(SideOn);
	TestTrue(FString::Printf(TEXT("the voices are directional: first click |P_1| %.3f for the breaker, %.3f beside it (>= 10 dB apart)"), WOn, WSide),
		WOn >= 3.16 * WSide);

	// The feed is not: power weights 1 / sqrt(2n + 1), no near field, no image, identical for both listeners.
	bool bPower = OnAxis.ReverbFeed.Impacts.Num() == SideOn.ReverbFeed.Impacts.Num();
	int32 BallEvents = 0;
	for (int32 I = 0; bPower && I < OnAxis.ReverbFeed.Impacts.Num(); ++I)
	{
		const FImpactEvent& A = OnAxis.ReverbFeed.Impacts[I];
		const FImpactEvent& S = SideOn.ReverbFeed.Impacts[I];
		bPower &= A.ImpactId == S.ImpactId && A.Kind == S.Kind && A.Bank == S.Bank && A.Kernels == S.Kernels;
		if (A.Kernels.IsValid())
		{
			++BallEvents;
			bPower &= A.NumPaths == 1 && S.NumPaths == 1 && A.Paths[0].NearField == 0.0 && A.Paths[0].Gain == S.Paths[0].Gain;
			for (int32 N = 0; N <= 3; ++N)
			{
				bPower &= A.Paths[0].Weights[N] == FRbAudioPlanBuilder::PowerWeight(N) && S.Paths[0].Weights[N] == A.Paths[0].Weights[N];
			}
		}
	}
	TestTrue(FString::Printf(TEXT("feed: %d ball events at their radiated power (weights 1 / sqrt(2n + 1)), the same for both listeners"), BallEvents),
		bPower && BallEvents > 0);
	const double Fs = 48000.0;
	const int32 Frames = static_cast<int32>(8.0 * Fs);
	const double EOn = RenderEnergy(OnAxis.ReverbFeed, Fs, Frames);
	const double ESide = RenderEnergy(SideOn.ReverbFeed, Fs, Frames);
	const double FeedDb = 10.0 * std::log10(FMath::Max(EOn, 1e-30) / FMath::Max(ESide, 1e-30));
	AddInfo(FString::Printf(TEXT("reverb feed energy: breaker vs beside the first click %+.3f dB (direction independent)"), FeedDb));
	// Only the propagation delays differ between the two feeds: overlapping clicks of the break cluster interfere a little differently.
	TestTrue(FString::Printf(TEXT("the reverb feed energy is listener independent (%+.3f dB, want +-1.0)"), FeedDb), EOn > 0.0 && FMath::Abs(FeedDb) <= 1.0);

	// Reduced tiers: order 1 only (at its power weight).
	FRbShotAudioPlan Far;
	FRbAudioPlanBuilder::Build(R, *B.Shot->Request.Table, Side, Options(ERbTableAudioTier::T1, EDynamicRangeMode::Wide, 1), Far);
	bool bOrder1 = Far.ReverbFeed.Impacts.Num() > 0;
	for (const FImpactEvent& E : Far.ReverbFeed.Impacts)
	{
		if (E.Kernels.IsValid())
		{
			bOrder1 &= E.Paths[0].Weights[0] == 0.0 && E.Paths[0].Weights[2] == 0.0 && E.Paths[0].Weights[3] == 0.0
				&& E.Paths[0].Weights[1] == FRbAudioPlanBuilder::PowerWeight(1);
		}
	}
	TestTrue(TEXT("T1 feed: order 1 only, at 1 / sqrt 3"), bOrder1);
	TestTrue(TEXT("the feed carries the presentation envelope of the stem"), Far.ReverbFeed.Presentation.IsValid() && Far.ReverbFeed.Presentation == Far.Presentation);
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

	// A loose ball sounds with the table's acoustic ball, so its floor hit reuses the kernels the table prewarmed (never a fresh
	// kernel design on the game thread for the physics body's mass, e.g. 0.17 or a float-rounded 0.170097).
	FRbShotRequest Request;
	FString Error;
	if (TestTrue(FString::Printf(TEXT("dive-bar table (%s)"), *Error), RbAudioScenarios::MakeDiveBarBreak8(Request, Error) && Request.Table.IsValid()))
	{
		const FRbTableContext& Context = *Request.Table;
		const FBallAcoustics Table3 = FRbAudioPlanBuilder::BallAcoustics(Context, 3);
		const FBallAcoustics Loose3 = RbAudioLive::LooseBallAcoustics(&Context, 3);
		TestTrue(TEXT("a loose ball is the table's ball (radius, mass, material)"), Loose3 == Table3);
		RbAudioLive::Prewarm(Fs);
		const FBallKernelsPtr Prewarmed = GetBallKernels(Table3, Fs, false);
		FRbFloorHitParams Hit;
		Hit.NormalSpeed = 2.0;
		Hit.BallRadius = Loose3.Radius;
		Hit.BallMass = Loose3.Mass;
		const FImpactEvent Ev = RbAudioLive::MakeFloorHitEvent(Hit, Fs);
		TestTrue(TEXT("the floor hit reuses the table ball's cached kernels"), Ev.Kernels.IsValid() && Ev.Kernels == Prewarmed);
		const FBallAcoustics Unknown = RbAudioLive::LooseBallAcoustics(nullptr, 3);
		TestTrue(TEXT("unknown table: the standard ball"), Unknown.Radius == StdBallRadius && Unknown.Mass == StdBallMass && Unknown.Material == PhenolicMaterial());
		TestTrue(TEXT("a ball id outside the set: the standard ball"), RbAudioLive::LooseBallAcoustics(&Context, 99) == Unknown);
		const uint64 T0 = FPlatformTime::Cycles64();
		TArray<float> Pcm;
		RbAudioLive::RenderFloorHit(Hit, Fs, Pcm);
		const double Ms = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0);
		AddInfo(FString::Printf(TEXT("a loose ball's floor hit with warm caches renders in %.2f ms on the calling (game) thread"), Ms));
	}
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

	// Mix-state ramps (audio.md 7.3 CBM_RB_Pause: -12 dB, attack 0.2 s / release 0.3 s), 10 ms frames.
	auto Run = [](float Gain, bool bActive, double Seconds)
	{
		for (int32 I = 0; I < FMath::RoundToInt(Seconds / 0.01); ++I)
		{
			Gain = URbAudioSubsystem::StepMixGain(Gain, bActive, -12.0, 0.2, 0.3, 0.01);
		}
		return 20.0 * std::log10(FMath::Max(static_cast<double>(Gain), 1e-9));
	};
	TestTrue(FString::Printf(TEXT("pause mix: half the depth after half the attack (%.2f dB)"), Run(1.0f, true, 0.1)), FMath::IsNearlyEqual(Run(1.0f, true, 0.1), -6.0, 0.05));
	TestTrue(TEXT("pause mix: -12 dB after the attack"), FMath::IsNearlyEqual(Run(1.0f, true, 0.2), -12.0, 0.01));
	const float Down = static_cast<float>(RbAudio::DbToGain(-12.0));
	TestTrue(FString::Printf(TEXT("pause mix: release takes its 0.3 s (half way after 0.15 s: %.2f dB)"), Run(Down, false, 0.15)),
		FMath::IsNearlyEqual(Run(Down, false, 0.15), -6.0, 0.05));
	TestTrue(TEXT("pause mix: released to exactly unity after 0.3 s"), URbAudioSubsystem::StepMixGain(static_cast<float>(RbAudio::DbToGain(-0.005)), false, -12.0, 0.2, 0.3, 0.01) == 1.0f
		&& FMath::IsNearlyEqual(Run(Down, false, 0.3), 0.0, 1e-6));
	return true;
}

// The room-tone layers at a level's audio anchors (review M2-C: the subsystem looked for RbAudio_Hvac / _Cooler / _Neon, the dive-bar
// generator of M2-A tags RbAudio_RoomTone, _CoolerCompressor1, _NeonN3 ... from layout.json, so no layer ever reached its anchor).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbAudioAnchorTest, "RawBreak.Unit.Audio.Venue_AmbienceAnchors", RB_UNIT_TEST_FLAGS)
bool FRbAudioAnchorTest::RunTest(const FString& Parameters)
{
	const FRbVenueAudioProfile Bar = RbGetVenueAudioProfile(ERbVenue::DiveBar);
	auto M = [](double X, double Y, double Z) { return FVector(X, Y, Z) * 100.0; };
	// Art/DiveBar/layout.json "audio_anchors" (M2-A, venue frame V in metres): every entry, the ones of later layers included.
	const TArray<FRbAudioAnchor> Layout = {
		{TEXT("RoomTone"), M(8.845, 2.745, 2.72)}, {TEXT("RoomTone2"), M(12.505, 1.525, 2.72)}, {TEXT("CoolerCompressor1"), M(3.11, 0.30, 0.20)},
		{TEXT("CoolerCompressor2"), M(7.90, 0.30, 0.20)}, {TEXT("IceMachine"), M(17.50, 5.00, 0.90)}, {TEXT("Jukebox"), M(11.30, 6.90, 0.95)},
		{TEXT("CeilingSpeaker1"), M(5.49, 5.18, 2.72)}, {TEXT("CeilingSpeaker2"), M(12.20, 2.44, 2.72)}, {TEXT("TV1"), M(6.80, 0.40, 2.30)},
		{TEXT("TV2"), M(16.36, 6.375, 2.22)}, {TEXT("DartMachine"), M(8.90, 6.90, 1.73)}, {TEXT("StreetDoor"), M(0.05, 6.10, 1.10)},
		{TEXT("StreetWindow"), M(0.05, 2.64, 1.50)}, {TEXT("Restrooms"), M(18.30, 1.46, 1.00)}, {TEXT("NeonN1"), M(0.06, 2.05, 1.32)},
		{TEXT("NeonN2"), M(0.06, 3.45, 1.32)}, {TEXT("NeonN3"), M(8.50, 0.05, 2.41)}, {TEXT("NeonN4"), M(13.80, 7.28, 2.17)},
		{TEXT("NeonN5"), M(16.44, 2.90, 1.67)}, {TEXT("PopcornMachine"), M(2.15, 1.87, 1.30)}, {TEXT("CeilingFan"), M(3.0, 5.2, 2.45)}};

	// Kinds by prefix.
	TestTrue(TEXT("RoomTone2 is an HVAC diffuser"), RbAmbienceAnchorKind(TEXT("RoomTone2")) == RbAudioAssets::AnchorHvac);
	TestTrue(TEXT("Hvac (the profile's own name) is an HVAC diffuser"), RbAmbienceAnchorKind(TEXT("Hvac")) == RbAudioAssets::AnchorHvac);
	TestTrue(TEXT("CoolerCompressor1 is a cooler"), RbAmbienceAnchorKind(TEXT("CoolerCompressor1")) == RbAudioAssets::AnchorCooler);
	TestTrue(TEXT("NeonN4 is a neon sign"), RbAmbienceAnchorKind(TEXT("NeonN4")) == RbAudioAssets::AnchorNeon);
	TestTrue(TEXT("Jukebox / IceMachine / CeilingSpeaker1 are not room-tone layers of M2"),
		!RbAmbienceAnchorKind(TEXT("Jukebox")) && !RbAmbienceAnchorKind(TEXT("IceMachine")) && !RbAmbienceAnchorKind(TEXT("CeilingSpeaker1")));

	auto Find = [](const TArray<FRbAmbiencePlacement>& P, const TCHAR* Name) -> const FRbAmbiencePlacement*
	{
		return P.FindByPredicate([Name](const FRbAmbiencePlacement& X) { return X.Name == Name; });
	};
	auto Near = [](const FRbAmbiencePlacement* P, const FVector& Cm) { return P && P->bAtAnchor && FVector::Dist(P->LocationCm, Cm) < 0.5; };

	// The dive-bar level: every designed layer at its own anchor, the further neon signs as layers of their own.
	TArray<FRbAmbiencePlacement> Placed;
	RbPlaceAmbience(Bar, Layout, Placed);
	int32 Diffusers = 0, Coolers = 0, Neons = 0, Beds = 0;
	TSet<uint64> Seeds;
	for (const FRbAmbiencePlacement& P : Placed)
	{
		Beds += P.Layer.Layer == EAmbienceLayer::HvacBed ? 1 : 0;
		Diffusers += P.Layer.Layer == EAmbienceLayer::HvacDiffuser ? 1 : 0;
		Coolers += P.Layer.Layer == EAmbienceLayer::Compressor ? 1 : 0;
		Neons += P.Layer.Layer == EAmbienceLayer::NeonHum ? 1 : 0;
		TestTrue(FString::Printf(TEXT("%s: positional layers sit at a level anchor"), *P.Name), P.Layer.Layer == EAmbienceLayer::HvacBed || P.bAtAnchor);
		Seeds.Add(P.Layer.Seed);
	}
	TestEqual(TEXT("dive-bar level: one bed"), Beds, 1);
	TestEqual(TEXT("dive-bar level: two diffusers (RoomTone, RoomTone2)"), Diffusers, 2);
	TestEqual(TEXT("dive-bar level: two coolers"), Coolers, 2);
	TestEqual(TEXT("dive-bar level: five neon signs N1..N5 (audio.md 12.1)"), Neons, 5);
	TestEqual(TEXT("every layer has its own seed (no two identical hums)"), Seeds.Num(), Placed.Num());
	TestTrue(TEXT("DiffuserPool at RoomTone2 (its nearest)"), Near(Find(Placed, TEXT("DiffuserPool")), M(12.505, 1.525, 2.72)));
	TestTrue(TEXT("DiffuserBar at RoomTone"), Near(Find(Placed, TEXT("DiffuserBar")), M(8.845, 2.745, 2.72)));
	TestTrue(TEXT("Cooler3Door at CoolerCompressor1"), Near(Find(Placed, TEXT("Cooler3Door")), M(3.11, 0.30, 0.20)));
	TestTrue(TEXT("Cooler2Door at CoolerCompressor2"), Near(Find(Placed, TEXT("Cooler2Door")), M(7.90, 0.30, 0.20)));
	const FRbAmbiencePlacement* N3 = Find(Placed, TEXT("NeonHollenbeck"));
	const FRbAmbiencePlacement* N4 = Find(Placed, TEXT("NeonPool"));
	const FRbAmbiencePlacement* N5 = Find(Placed, TEXT("NeonLanternFlats"));
	TestTrue(TEXT("the designed neon layers at N3 / N4 / N5"), Near(N3, M(8.50, 0.05, 2.41)) && Near(N4, M(13.80, 7.28, 2.17)) && Near(N5, M(16.44, 2.90, 1.67)));
	for (const FRbAmbienceEmitter& E : Bar.Emitters)
	{
		if (const FRbAmbiencePlacement* P = Find(Placed, *E.Name))
		{
			TestTrue(FString::Printf(TEXT("%s keeps its designed layer (level, seed, duty-cycle start)"), *E.Name), P->Layer.LevelDbA == E.Layer.LevelDbA
				&& P->Layer.Seed == E.Layer.Seed && P->Layer.bStartOn == E.Layer.bStartOn && P->Layer.Layer == E.Layer.Layer);
		}
		else
		{
			AddError(FString::Printf(TEXT("%s is missing in the dive-bar level"), *E.Name));
		}
	}
	TestTrue(TEXT("the window signs N1 / N2 hum too (extra layers at their anchors)"),
		Placed.ContainsByPredicate([&](const FRbAmbiencePlacement& P) { return P.Layer.Layer == EAmbienceLayer::NeonHum && FVector::Dist(P.LocationCm, M(0.06, 2.05, 1.32)) < 0.5; })
		&& Placed.ContainsByPredicate([&](const FRbAmbiencePlacement& P) { return P.Layer.Layer == EAmbienceLayer::NeonHum && FVector::Dist(P.LocationCm, M(0.06, 3.45, 1.32)) < 0.5; }));

	// Deterministic and independent of the actor order of the level.
	TArray<FRbAudioAnchor> Reversed = Layout;
	Algo::Reverse(Reversed);
	TArray<FRbAmbiencePlacement> Again;
	RbPlaceAmbience(Bar, Reversed, Again);
	bool bSame = Again.Num() == Placed.Num();
	for (int32 I = 0; bSame && I < Placed.Num(); ++I)
	{
		bSame = Again[I].Name == Placed[I].Name && Again[I].Layer.Seed == Placed[I].Layer.Seed && Again[I].LocationCm.Equals(Placed[I].LocationCm, 0.0);
	}
	TestTrue(TEXT("the placement does not depend on the actor order"), bSame);

	// A level without anchors (the test room, a scene in another frame): the profile at its default positions.
	TArray<FRbAmbiencePlacement> Defaults;
	RbPlaceAmbience(Bar, TArray<FRbAudioAnchor>(), Defaults);
	TestEqual(TEXT("no anchors: every profile layer"), Defaults.Num(), Bar.Emitters.Num());
	bool bAtDefaults = true;
	for (int32 I = 0; I < Defaults.Num(); ++I)
	{
		bAtDefaults &= !Defaults[I].bAtAnchor && Defaults[I].LocationCm.Equals(Bar.Emitters[Defaults[I].Emitter].DefaultLocationCm, 0.0);
	}
	TestTrue(TEXT("no anchors: the default positions"), bAtDefaults);

	// Fewer anchors than designed layers: the level decides (one neon sign -> one neon layer, the nearest design).
	TArray<FRbAmbiencePlacement> One;
	TArray<FRbAudioAnchor> OneSign;
	OneSign.Add({TEXT("Neon"), M(13.0, 7.0, 2.2)});
	RbPlaceAmbience(Bar, OneSign, One);
	int32 OneNeons = 0;
	for (const FRbAmbiencePlacement& P : One)
	{
		OneNeons += P.Layer.Layer == EAmbienceLayer::NeonHum ? 1 : 0;
	}
	TestEqual(TEXT("one neon anchor -> one neon layer"), OneNeons, 1);
	TestTrue(TEXT("... the design nearest to it (NeonPool)"), Near(Find(One, TEXT("NeonPool")), M(13.0, 7.0, 2.2)));
	TestEqual(TEXT("kinds without anchors keep their defaults (2 diffusers, 2 coolers, the bed)"), One.Num(), 6);
	return true;
}

} // namespace RbAudioTests

#endif // WITH_DEV_AUTOMATION_TESTS
