// UE-7 unit tests (Docs/ue-architecture.md 13): overlay model text for scripted match states, the overlay modes and fades,
// mandatory lines in Hidden mode (review R-05), the post-shot auto-glance, and the replay history cap. World-free: the match
// director simulates on the game thread at live playback rate 0 (as in RbMatchTests.cpp), the overlay component gets the
// director injected (SetDirector), the Slate widget is built only when Slate runs. Owner: UE-7.
//
//   Unit.Overlay.BallInHand        break ball in hand behind the head string: mandatory Info line (named in hot-seat), title,
//                                  score line
//   Unit.Overlay.TwoFoulWarning    "on 1 foul", then "ON TWO FOULS - a third foul loses the rack" (Reg 8) for the shooter, the
//                                  enforced foul with its rule reference, the foul counters of both players always listed
//   Unit.Overlay.DecisionOptions   push-out -> the decider's options, the highlighted one in brackets, follows CycleOption
//   Unit.Overlay.RackAndMatchOver  9 pocketed -> "Rack over ... Enter: next rack" / "Match over ... wins 1 : 0 ... new match"
//   Unit.Overlay.MandatoryInHidden Hidden mode: full card at opacity 0, mandatory lines still shown (component + widget)
//   Unit.Overlay.Modes             Hidden / Glance (held) / Pinned, fade in / out timing, debug block (F2)
//   Unit.Overlay.AutoGlance        a committed shot glances for AutoGlanceSeconds, other match changes do not restart it
//   Unit.Replay.HistoryCap         the newest MaxShots shots are kept, oldest dropped, index 0 = the last shot; no world = no replay

#include "Game/RbMatchDirector.h"
#include "Replay/RbReplaySubsystem.h"
#include "Simulation/RbTableContext.h"
#include "Tests/RbTestFlags.h"
#include "UI/RbOverlayComponent.h"
#include "UI/SRbInfoOverlay.h"

#include "Framework/Application/SlateApplication.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "rb/Rules/TableRules.h"

#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS

namespace RbOverlayTest
{
	TStrongObjectPtr<URbMatchDirector> MakeOverlayDirector(FAutomationTestBase& Test, ERbMatchMode Mode, int64 Seed, int32 RaceTo = 5,
		ERbDiscipline Discipline = ERbDiscipline::NineBall)
	{
		FString Error;
		const TSharedPtr<const FRbTableContext> Table = FRbTableContext::Create(FRbTableSetup{}, Error);
		if (!Table.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("table context: %s"), *Error));
			return nullptr;
		}
		TStrongObjectPtr<URbMatchDirector> Director(NewObject<URbMatchDirector>(GetTransientPackage()));
		Director->Initialize(nullptr, nullptr, nullptr, nullptr);
		Director->SetTableContext(Table);
		Director->SetLivePlaybackRate(0.0f);
		FRbMatchSetup Setup;
		Setup.Mode = Mode;
		Setup.Seed = Seed;
		Setup.RaceTo = RaceTo;
		Setup.Discipline = Discipline;
		Setup.NoiseScale = 0.0;
		if (!Director->StartMatch(Setup))
		{
			Test.AddError(FString::Printf(TEXT("StartMatch failed: %s"), *Director->GetLastError()));
			return nullptr;
		}
		return Director;
	}

	TStrongObjectPtr<URbOverlayComponent> MakeOverlay(URbMatchDirector* Director)
	{
		TStrongObjectPtr<URbOverlayComponent> Overlay(NewObject<URbOverlayComponent>(GetTransientPackage()));
		Overlay->SetDirector(Director);
		return Overlay;
	}

	void SetBalls(URbMatchDirector& D, std::initializer_list<TPair<int32, rb::Vec2>> Layout)
	{
		FRbTableState S = D.GetTableState();
		for (rb::SimBall& B : S.Balls)
		{
			B.InPlay = false;
		}
		for (const TPair<int32, rb::Vec2>& At : Layout)
		{
			S.Balls[At.Key].InPlay = true;
			S.Balls[At.Key].State.Position = rb::Vec3(At.Value.x, At.Value.y, D.GetTableContext()->BallRadius(At.Key));
		}
		D.SetTableStateForTest(S);
	}

	bool HasLine(const TArray<FText>& Lines, const FString& Part, int32* OutIndex = nullptr)
	{
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			if (Lines[Index].ToString().Contains(Part, ESearchCase::CaseSensitive))
			{
				if (OutIndex)
				{
					*OutIndex = Index;
				}
				return true;
			}
		}
		return false;
	}

	FRbOverlayModel Model(const URbMatchDirector& D, bool bDebug = false)
	{
		FRbOverlayExtras Extras;
		Extras.bDebug = bDebug;
		return URbOverlayComponent::BuildModel(&D, Extras);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbOverlayBallInHand, "RawBreak.Unit.Overlay.BallInHand", RB_UNIT_TEST_FLAGS)
bool FRbOverlayBallInHand::RunTest(const FString& Parameters)
{
	namespace T = RbOverlayTest;
	TStrongObjectPtr<URbMatchDirector> D = T::MakeOverlayDirector(*this, ERbMatchMode::HotSeat, 11, 5);
	if (!D.IsValid())
	{
		return false;
	}
	const FRbOverlayModel M = T::Model(*D);
	AddInfo(M.ToDebugString());
	int32 Index = -1;
	TestTrue(TEXT("hot-seat break: ball in hand behind the head string, named"), T::HasLine(M.MandatoryLines, TEXT("Player 1: ball in hand behind the head string"), &Index));
	TestEqual(TEXT("ball in hand is a prompt (Info tone)"), M.MandatoryTone(Index), ERbOverlayTone::Info);
	TestEqual(TEXT("no foul line at the break"), M.MandatoryLines.Num(), 1);
	TestTrue(TEXT("title: discipline and mode"), M.Title.ToString().Contains(TEXT("9-BALL")) && M.Title.ToString().Contains(TEXT("HOT-SEAT")));
	TestTrue(TEXT("subtitle: score and race"), M.Subtitle.ToString().Contains(TEXT("Player 1   0 : 0   Player 2")) && M.Subtitle.ToString().Contains(TEXT("race to 5")));
	TestTrue(TEXT("shooter line"), T::HasLine(M.MatchLines, TEXT("Player 1 to break")));
	TestTrue(TEXT("foul counters always on display"), T::HasLine(M.MatchLines, TEXT("Fouls in a row:  Player 1 0  \u00B7  Player 2 0")));
	TestTrue(TEXT("no last shot yet"), M.LastShotLines.Num() == 0);

	TStrongObjectPtr<URbMatchDirector> P = T::MakeOverlayDirector(*this, ERbMatchMode::Practice, 12);
	if (!P.IsValid())
	{
		return false;
	}
	const FRbOverlayModel PM = T::Model(*P);
	TestTrue(TEXT("practice: no names, capitalised"), T::HasLine(PM.MandatoryLines, TEXT("Ball in hand behind the head string")) &&
		!PM.ToDebugString().Contains(TEXT("Player 1")));
	TestTrue(TEXT("practice title"), PM.Title.ToString().Contains(TEXT("PRACTICE")) && PM.Subtitle.ToString().Contains(TEXT("rack 1")));

	// After the placement the prompt is gone (placed, AwaitStroke).
	TestTrue(TEXT("place"), D->PlaceCueBall(rb::Vec2(D->GetMatchConfig().Table.HeadStringX - 0.12, 0.08)));
	TestEqual(TEXT("no mandatory line once placed"), T::Model(*D).MandatoryLines.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbOverlayTwoFoulWarning, "RawBreak.Unit.Overlay.TwoFoulWarning", RB_UNIT_TEST_FLAGS)
bool FRbOverlayTwoFoulWarning::RunTest(const FString& Parameters)
{
	namespace T = RbOverlayTest;
	TStrongObjectPtr<URbMatchDirector> D = T::MakeOverlayDirector(*this, ERbMatchMode::HotSeat, 51, 3);
	if (!D.IsValid())
	{
		return false;
	}
	T::SetBalls(*D, {{0, rb::Vec2(-0.8, -0.3)}, {1, rb::Vec2(0.6, 0.3)}, {9, rb::Vec2(0.7, -0.3)}});
	// A soft stroke into the head cushion that touches no ball: a foul (R 3.3); both players foul in turn.
	const auto Foul = [&](int32 Shooter, bool bPlace) {
		if (bPlace)
		{
			TestTrue(TEXT("ball in hand placement"), D->PlaceCueBall(rb::Vec2(-0.8, Shooter == 0 ? -0.3 : 0.3)));
		}
		TestTrue(TEXT("foul stroke"), D->SubmitScriptedStrike(0.4, PI, 0.0, 0.0, 0.0));
	};
	Foul(0, false);
	{
		// Player 2 to shoot with ball in hand; player 1 has one foul (not the shooter: only in the full card).
		const FRbOverlayModel M = T::Model(*D);
		AddInfo(M.ToDebugString());
		TestTrue(TEXT("ball in hand anywhere for the incoming shooter"), T::HasLine(M.MandatoryLines, TEXT("Player 2: ball in hand anywhere")));
		TestFalse(TEXT("no foul line for a shooter without fouls"), T::HasLine(M.MandatoryLines, TEXT("foul")));
		TestTrue(TEXT("counters of both players"), T::HasLine(M.MatchLines, TEXT("Fouls in a row:  Player 1 1  \u00B7  Player 2 0")));
		int32 FoulIndex = -1;
		const FRbLastShotSummary& Last = D->GetLastShot();
		TestTrue(TEXT("a foul was enforced with a rule reference"), Last.Enforced != rb::rules::Foul::Count && !Last.RuleRef.IsEmpty());
		TestTrue(TEXT("enforced foul with its rule reference"), T::HasLine(M.LastShotLines, TEXT("FOUL: "), &FoulIndex) &&
			M.LastShotLines[FoulIndex].ToString().Contains(FString::Printf(TEXT("(%s)"), *Last.RuleRef)));
		TestEqual(TEXT("the foul is a warning"), M.LastShotTone(FoulIndex), ERbOverlayTone::Warning);
		TestTrue(TEXT("what follows"), T::HasLine(M.LastShotLines, TEXT("Turn to Player 2")));
		TestTrue(TEXT("last shot names its shooter"), T::HasLine(M.LastShotLines, TEXT("LAST SHOT  \u00B7  Player 1")));
	}
	Foul(1, true);
	{
		const FRbOverlayModel M = T::Model(*D);
		int32 Index = -1;
		TestTrue(TEXT("player 1 back at the table on 1 foul"), T::HasLine(M.MandatoryLines, TEXT("Player 1: on 1 foul"), &Index));
		TestEqual(TEXT("foul line is a warning"), M.MandatoryTone(Index), ERbOverlayTone::Warning);
		TestTrue(TEXT("with ball in hand"), T::HasLine(M.MandatoryLines, TEXT("Player 1: ball in hand anywhere")));
	}
	Foul(0, true);
	Foul(1, true);
	{
		// Player 1 on two fouls and to shoot: the mandatory two-foul warning (Reg 8), shown in every mode.
		TestTrue(TEXT("rules warning flag"), D->GetConstraints().ThreeFoulWarning);
		const FRbOverlayModel M = T::Model(*D);
		AddInfo(M.ToDebugString());
		int32 Index = -1;
		TestTrue(TEXT("two-foul warning"), T::HasLine(M.MandatoryLines, TEXT("Player 1: ON TWO FOULS  \u2013  a third foul loses the rack"), &Index));
		TestEqual(TEXT("warning tone"), M.MandatoryTone(Index), ERbOverlayTone::Warning);
		TestTrue(TEXT("counters"), T::HasLine(M.MatchLines, TEXT("Fouls in a row:  Player 1 2  \u00B7  Player 2 2")));
	}
	Foul(0, true);
	{
		// The third foul loses the rack: RackOver with the winner named.
		const FRbOverlayModel M = T::Model(*D);
		AddInfo(M.ToDebugString());
		TestEqual(TEXT("rack over"), D->GetPhase(), ERbDirectorPhase::RackOver);
		TestTrue(TEXT("third foul enforced"), T::HasLine(M.LastShotLines, TEXT("FOUL: third consecutive foul")));
		TestTrue(TEXT("rack over prompt, rack to player 2"), T::HasLine(M.MandatoryLines, TEXT("Rack over  \u2013  Player 2 wins the rack  \u00B7  Enter: next rack")));
		TestFalse(TEXT("no foul warning once the rack is over"), T::HasLine(M.MandatoryLines, TEXT("FOULS")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbOverlayDecisionOptions, "RawBreak.Unit.Overlay.DecisionOptions", RB_UNIT_TEST_FLAGS)
bool FRbOverlayDecisionOptions::RunTest(const FString& Parameters)
{
	namespace T = RbOverlayTest;
	// The first seed whose scripted break is legal (push-out window open) - deterministic.
	TStrongObjectPtr<URbMatchDirector> D;
	for (int64 Seed = 1; Seed <= 30 && !D.IsValid(); ++Seed)
	{
		TStrongObjectPtr<URbMatchDirector> Candidate = T::MakeOverlayDirector(*this, ERbMatchMode::HotSeat, Seed);
		if (!Candidate.IsValid())
		{
			return false;
		}
		const rb::rules::RulesTable& Rules = Candidate->GetMatchConfig().Table;
		if (!Candidate->PlaceCueBall(rb::Vec2(Rules.HeadStringX - 0.12, 0.08)))
		{
			continue;
		}
		const int32 Apex = rb::rules::LowestObjectBallAtStart(Candidate->GetMatchState().Game);
		const rb::Vec3 Cue = Candidate->GetTableState().Balls[0].State.Position;
		const rb::Vec3 Target = Candidate->GetTableState().Balls[Apex].State.Position;
		Candidate->SubmitScriptedStrike(9.0, FMath::Atan2(Target.y - Cue.y, Target.x - Cue.x), 0.0, 0.0, -0.1);
		if (Candidate->GetPhase() == ERbDirectorPhase::AwaitStroke && Candidate->GetConstraints().PushOutAllowed)
		{
			D = Candidate;
		}
	}
	if (!TestTrue(TEXT("a legal break among seeds 1..30"), D.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("push-out available is listed"), T::HasLine(T::Model(*D).MatchLines, TEXT("Push-out available")));
	D->SetShotKind(rb::rules::ShotKind::PushOut);
	TestTrue(TEXT("push-out declared is listed"), T::HasLine(T::Model(*D).MatchLines, TEXT("Push-out declared")));
	const int32 Pusher = D->GetMatchState().Game.Shooter;
	const rb::Vec3 Cue = D->GetTableState().Balls[0].State.Position;
	D->SubmitScriptedStrike(0.35, FMath::Atan2(-Cue.y, -Cue.x), 0.0, 0.0, 0.0);
	if (!TestEqual(TEXT("push-out -> decision"), D->GetPhase(), ERbDirectorPhase::AwaitDecision))
	{
		return false;
	}
	const FString Decider = D->GetShooter(1 - Pusher).Name;
	{
		const FRbOverlayModel M = T::Model(*D);
		AddInfo(M.ToDebugString());
		int32 Index = -1;
		TestTrue(TEXT("decision with both options, the first highlighted"),
			T::HasLine(M.MandatoryLines, Decider + TEXT(" decides:  [shoot from here]  \u00B7  pass it back   (Q/E, Enter)"), &Index));
		TestEqual(TEXT("decision is a prompt"), M.MandatoryTone(Index), ERbOverlayTone::Info);
		TestTrue(TEXT("the push-out result"), T::HasLine(M.LastShotLines, TEXT("A decision is pending")));
	}
	TestTrue(TEXT("cycle"), D->CycleOption(1));
	TestTrue(TEXT("the highlight follows CycleOption"),
		T::HasLine(T::Model(*D).MandatoryLines, TEXT("decides:  shoot from here  \u00B7  [pass it back]")));
	TestTrue(TEXT("Confirm = pass back"), D->Confirm());
	TestFalse(TEXT("no decision line afterwards"), T::HasLine(T::Model(*D).MandatoryLines, TEXT("decides")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbOverlayDeciderFouls, "RawBreak.Unit.Overlay.DeciderFouls", RB_UNIT_TEST_FLAGS)
bool FRbOverlayDeciderFouls::RunTest(const FString& Parameters)
{
	// UE-7 review: the player who comes to the table to DECIDE sees their own foul counter (Reg 8). 10-ball: player 1 fouls,
	// player 2 pockets the 1 in a pocket other than the called one (R 6.6, not a foul) -> player 1, on one foul, decides.
	namespace T = RbOverlayTest;
	TStrongObjectPtr<URbMatchDirector> D = T::MakeOverlayDirector(*this, ERbMatchMode::HotSeat, 51, 3, ERbDiscipline::TenBall);
	if (!D.IsValid())
	{
		return false;
	}
	T::SetBalls(*D, {{0, rb::Vec2(-0.8, -0.3)}, {1, rb::Vec2(0.6, 0.3)}, {10, rb::Vec2(0.7, -0.3)}});
	TestTrue(TEXT("foul stroke (touches nothing)"), D->SubmitScriptedStrike(0.4, PI, 0.0, 0.0, 0.0));
	if (!TestEqual(TEXT("player 1 on one foul"), D->GetMatchState().Game.Players[0].ConsecutiveFouls, 1))
	{
		AddInfo(T::Model(*D).ToDebugString());
		return false;
	}
	if (D->GetPhase() == ERbDirectorPhase::AwaitPlacement)
	{
		TestTrue(TEXT("ball in hand placement"), D->PlaceCueBall(rb::Vec2(-0.8, 0.3)));
	}
	const rb::PocketGeometry& Corner = D->GetTableContext()->Geometry.Pockets[static_cast<int32>(rb::PocketId::FootRight)];
	const rb::Vec2 One = Corner.MouthMid - Corner.Axis * 0.3;
	T::SetBalls(*D, {{0, One - Corner.Axis * 0.25}, {1, One}, {10, rb::Vec2(-0.9, 0.4)}});
	D->SetCalledShot(1, static_cast<int32>(rb::PocketId::HeadLeft)); // called elsewhere: a wrongly pocketed ball
	TestTrue(TEXT("player 2 shoots"), D->SubmitScriptedStrike(2.0, FMath::Atan2(Corner.Axis.y, Corner.Axis.x), 0.0, 0.0, -0.35));
	const FRbOverlayModel M = T::Model(*D);
	AddInfo(M.ToDebugString());
	if (!TestEqual(TEXT("wrongly pocketed -> decision"), D->GetPhase(), ERbDirectorPhase::AwaitDecision))
	{
		return false;
	}
	TestEqual(TEXT("player 1 decides"), D->GetMatchState().Decider, 0);
	TestTrue(TEXT("decision line"), T::HasLine(M.MandatoryLines, TEXT("Player 1 decides:")));
	int32 Index = -1;
	TestTrue(TEXT("the decider's foul counter is on screen"), T::HasLine(M.MandatoryLines, TEXT("Player 1: on 1 foul"), &Index));
	TestEqual(TEXT("as a warning"), M.MandatoryTone(Index), ERbOverlayTone::Warning);
	TestFalse(TEXT("not the counter of the player who just shot"), T::HasLine(M.MandatoryLines, TEXT("Player 2: on")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbOverlayRackAndMatchOver,"RawBreak.Unit.Overlay.RackAndMatchOver", RB_UNIT_TEST_FLAGS)
bool FRbOverlayRackAndMatchOver::RunTest(const FString& Parameters)
{
	namespace T = RbOverlayTest;
	for (const int32 Race : {2, 1})
	{
		TStrongObjectPtr<URbMatchDirector> D = T::MakeOverlayDirector(*this, ERbMatchMode::HotSeat, 41, Race);
		if (!D.IsValid())
		{
			return false;
		}
		TStrongObjectPtr<URbOverlayComponent> Overlay = T::MakeOverlay(D.Get());
		const rb::PocketGeometry& Corner = D->GetTableContext()->Geometry.Pockets[static_cast<int32>(rb::PocketId::FootRight)];
		const rb::Vec2 Nine = Corner.MouthMid - Corner.Axis * 0.3;
		T::SetBalls(*D, {{0, Nine - Corner.Axis * 0.25}, {9, Nine}});
		TestTrue(TEXT("9 shot"), D->SubmitScriptedStrike(2.0, FMath::Atan2(Corner.Axis.y, Corner.Axis.x), 0.0, 0.0, -0.35));
		const FRbOverlayModel& M = Overlay->GetModel();
		AddInfo(M.ToDebugString());
		TestTrue(TEXT("9 pocketed shown"), T::HasLine(M.LastShotLines, TEXT("Pocketed: 9")));
		if (Race == 2)
		{
			TestEqual(TEXT("race 2: rack over"), D->GetPhase(), ERbDirectorPhase::RackOver);
			TestTrue(TEXT("rack over prompt names the winner"), T::HasLine(M.MandatoryLines, TEXT("Rack over  \u2013  Player 1 wins the rack  \u00B7  Enter: next rack")));
			TestTrue(TEXT("rack to player 1"), T::HasLine(M.LastShotLines, TEXT("Rack to Player 1")));
			TestTrue(TEXT("score 1 : 0"), M.Subtitle.ToString().Contains(TEXT("Player 1   1 : 0   Player 2")));
		}
		else
		{
			TestEqual(TEXT("race 1: match over"), D->GetPhase(), ERbDirectorPhase::MatchOver);
			TestTrue(TEXT("match over prompt"), T::HasLine(M.MandatoryLines, TEXT("Match over  \u2013  Player 1 wins 1 : 0  \u00B7  Enter: new match")));
			TestTrue(TEXT("Confirm starts a new match and the prompt goes"), D->Confirm() &&
				!T::HasLine(Overlay->GetModel().MandatoryLines, TEXT("Match over")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbOverlayMandatoryInHidden, "RawBreak.Unit.Overlay.MandatoryInHidden", RB_UNIT_TEST_FLAGS)
bool FRbOverlayMandatoryInHidden::RunTest(const FString& Parameters)
{
	namespace T = RbOverlayTest;
	TStrongObjectPtr<URbMatchDirector> D = T::MakeOverlayDirector(*this, ERbMatchMode::HotSeat, 61, 3);
	if (!D.IsValid())
	{
		return false;
	}
	TStrongObjectPtr<URbOverlayComponent> Overlay = T::MakeOverlay(D.Get());
	// Two fouls for player 1 and player 1 to shoot with ball in hand.
	T::SetBalls(*D, {{0, rb::Vec2(-0.8, -0.3)}, {1, rb::Vec2(0.6, 0.3)}, {9, rb::Vec2(0.7, -0.3)}});
	for (int32 Shot = 0; Shot < 4; ++Shot)
	{
		if (Shot > 0)
		{
			D->PlaceCueBall(rb::Vec2(-0.8, (Shot % 2) == 0 ? -0.3 : 0.3));
		}
		D->SubmitScriptedStrike(0.4, PI, 0.0, 0.0, 0.0);
	}
	Overlay->AdvanceOverlay(10.0f); // any auto-glance is over
	TestEqual(TEXT("Hidden mode"), Overlay->GetMode(), ERbOverlayMode::Hidden);
	TestEqual(TEXT("full card invisible"), Overlay->GetFullOpacity(), 0.0f);
	TestTrue(TEXT("mandatory lines shown in Hidden mode"), Overlay->AreMandatoryLinesShown());
	const FRbOverlayModel& M = Overlay->GetModel();
	AddInfo(M.ToDebugString());
	TestTrue(TEXT("the two-foul warning is among them"), T::HasLine(M.MandatoryLines, TEXT("ON TWO FOULS")));
	TestTrue(TEXT("and ball in hand"), T::HasLine(M.MandatoryLines, TEXT("ball in hand anywhere")));

	if (FSlateApplication::IsInitialized())
	{
		TSharedRef<SRbInfoOverlay> Widget = SNew(SRbInfoOverlay);
		Widget->SetModel(M);
		Widget->SetFullOpacity(Overlay->GetFullOpacity());
		TestTrue(TEXT("widget: mandatory card visible in Hidden mode"), Widget->IsMandatoryCardVisible());
		TestFalse(TEXT("widget: full card collapsed"), Widget->IsFullCardVisible());
		Widget->SetFullOpacity(1.0f);
		TestTrue(TEXT("widget: full card at opacity 1"), Widget->IsFullCardVisible());
		FRbOverlayModel Empty = M;
		Empty.MandatoryLines.Reset();
		Empty.MandatoryTones.Reset();
		Widget->SetModel(Empty);
		TestFalse(TEXT("widget: no mandatory card without lines"), Widget->IsMandatoryCardVisible());
	}
	else
	{
		AddInfo(TEXT("Slate not initialised: widget part skipped"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbOverlayModes, "RawBreak.Unit.Overlay.Modes", RB_UNIT_TEST_FLAGS)
bool FRbOverlayModes::RunTest(const FString& Parameters)
{
	namespace T = RbOverlayTest;
	TStrongObjectPtr<URbMatchDirector> D = T::MakeOverlayDirector(*this, ERbMatchMode::Practice, 71);
	if (!D.IsValid())
	{
		return false;
	}
	TStrongObjectPtr<URbOverlayComponent> Overlay = T::MakeOverlay(D.Get());
	TestEqual(TEXT("Hidden by default (no HUD, decisions)"), Overlay->GetMode(), ERbOverlayMode::Hidden);
	TestEqual(TEXT("target 0"), Overlay->GetTargetOpacity(), 0.0f);

	Overlay->SetGlanceHeld(true);
	TestEqual(TEXT("glance key held -> Glance"), Overlay->GetMode(), ERbOverlayMode::Glance);
	Overlay->AdvanceOverlay(0.5f * Overlay->FadeInSeconds);
	TestTrue(TEXT("fading in"), FMath::IsNearlyEqual(Overlay->GetFullOpacity(), 0.5f, 1.0e-4f));
	Overlay->AdvanceOverlay(Overlay->FadeInSeconds);
	TestEqual(TEXT("fully in"), Overlay->GetFullOpacity(), 1.0f);
	Overlay->SetGlanceHeld(false);
	TestEqual(TEXT("released -> Hidden"), Overlay->GetMode(), ERbOverlayMode::Hidden);
	Overlay->AdvanceOverlay(0.5f * Overlay->FadeOutSeconds);
	TestTrue(TEXT("fading out"), FMath::IsNearlyEqual(Overlay->GetFullOpacity(), 0.5f, 1.0e-4f));
	Overlay->AdvanceOverlay(Overlay->FadeOutSeconds);
	TestEqual(TEXT("fully out"), Overlay->GetFullOpacity(), 0.0f);

	Overlay->TogglePinned();
	TestEqual(TEXT("F1 -> Pinned"), Overlay->GetMode(), ERbOverlayMode::Pinned);
	Overlay->SetGlanceHeld(true);
	Overlay->SetGlanceHeld(false);
	TestEqual(TEXT("a glance does not unpin"), Overlay->GetMode(), ERbOverlayMode::Pinned);
	Overlay->AdvanceOverlay(1.0f);
	TestEqual(TEXT("pinned = fully visible"), Overlay->GetFullOpacity(), 1.0f);
	Overlay->TogglePinned();
	TestEqual(TEXT("F1 again -> Hidden"), Overlay->GetMode(), ERbOverlayMode::Hidden);

	// Debug block (F2): off by default, physics facts of the last shot when on.
	TestTrue(TEXT("place"), D->PlaceCueBall(rb::Vec2(D->GetMatchConfig().Table.HeadStringX - 0.12, 0.08)));
	const int32 Apex = rb::rules::LowestObjectBallAtStart(D->GetMatchState().Game);
	const rb::Vec3 Cue = D->GetTableState().Balls[0].State.Position;
	const rb::Vec3 Target = D->GetTableState().Balls[Apex].State.Position;
	TestTrue(TEXT("break"), D->SubmitScriptedStrike(8.0, FMath::Atan2(Target.y - Cue.y, Target.x - Cue.x), 0.0, 0.0, -0.1));
	TestFalse(TEXT("no debug block by default"), Overlay->IsDebugShown() || Overlay->GetModel().bShowDebug);
	Overlay->ToggleDebug();
	const FRbOverlayModel& M = Overlay->GetModel();
	AddInfo(M.ToDebugString());
	TestTrue(TEXT("F2 -> debug block"), Overlay->IsDebugShown() && M.bShowDebug);
	TestTrue(TEXT("sim time and events"), T::HasLine(M.DebugLines, TEXT("sim ")) && T::HasLine(M.DebugLines, TEXT(" events")));
	TestTrue(TEXT("the strike"), T::HasLine(M.DebugLines, TEXT("strike V 8.000 m/s")));
	TestTrue(TEXT("scripted strike marked"), T::HasLine(M.DebugLines, TEXT("scripted strike (no human layer)")));
	TestTrue(TEXT("hashes"), T::HasLine(M.DebugLines, TEXT("hash in ")));
	TestTrue(TEXT("tip chalk"), T::HasLine(M.DebugLines, TEXT("tip chalk min")));
	TestTrue(TEXT("raw-input timestamp mode"), T::HasLine(M.DebugLines, TEXT("raw input: n/a")));
	TestTrue(TEXT("debug lines are in the text only with F2"), M.ToDebugString().Contains(TEXT("[D] ")));
	Overlay->ToggleDebug();
	TestFalse(TEXT("F2 again -> no debug block"), Overlay->GetModel().ToDebugString().Contains(TEXT("[D] ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbOverlayAutoGlance, "RawBreak.Unit.Overlay.AutoGlance", RB_UNIT_TEST_FLAGS)
bool FRbOverlayAutoGlance::RunTest(const FString& Parameters)
{
	namespace T = RbOverlayTest;
	TStrongObjectPtr<URbMatchDirector> D = T::MakeOverlayDirector(*this, ERbMatchMode::HotSeat, 81);
	if (!D.IsValid())
	{
		return false;
	}
	TStrongObjectPtr<URbOverlayComponent> Overlay = T::MakeOverlay(D.Get());
	TestTrue(TEXT("place"), D->PlaceCueBall(rb::Vec2(D->GetMatchConfig().Table.HeadStringX - 0.12, 0.08)));
	TestEqual(TEXT("no glance before a shot"), Overlay->GetMode(), ERbOverlayMode::Hidden);
	const int32 Apex = rb::rules::LowestObjectBallAtStart(D->GetMatchState().Game);
	const rb::Vec3 Cue = D->GetTableState().Balls[0].State.Position;
	const rb::Vec3 Target = D->GetTableState().Balls[Apex].State.Position;
	TestTrue(TEXT("break"), D->SubmitScriptedStrike(8.0, FMath::Atan2(Target.y - Cue.y, Target.x - Cue.x), 0.0, 0.0, -0.1));
	TestTrue(TEXT("committed"), D->GetLastCommittedShot().IsValid());
	TestEqual(TEXT("the committed shot glances"), Overlay->GetMode(), ERbOverlayMode::Glance);
	TestEqual(TEXT("for AutoGlanceSeconds"), Overlay->GetAutoGlanceRemaining(), Overlay->AutoGlanceSeconds);
	TestTrue(TEXT("the result is in the model"), Overlay->GetModel().LastShotLines.Num() > 0);
	Overlay->AdvanceOverlay(Overlay->AutoGlanceSeconds - 0.1f);
	TestEqual(TEXT("still glancing"), Overlay->GetMode(), ERbOverlayMode::Glance);
	TestEqual(TEXT("fully visible while glancing"), Overlay->GetFullOpacity(), 1.0f);
	// A match change that commits nothing (a cycled option / refused call) does not restart the glance.
	D->SetCalledShot(-1, -1);
	Overlay->Refresh();
	TestTrue(TEXT("no restart without a new shot"), Overlay->GetAutoGlanceRemaining() < 0.2f);
	Overlay->AdvanceOverlay(0.2f);
	TestEqual(TEXT("glance over -> Hidden"), Overlay->GetMode(), ERbOverlayMode::Hidden);
	Overlay->AdvanceOverlay(Overlay->FadeOutSeconds);
	TestEqual(TEXT("faded out"), Overlay->GetFullOpacity(), 0.0f);
	// Pinned stays pinned through a glance.
	Overlay->SetPinned(true);
	Overlay->StartAutoGlance();
	Overlay->AdvanceOverlay(Overlay->AutoGlanceSeconds + 1.0f);
	TestEqual(TEXT("pinned after the glance"), Overlay->GetMode(), ERbOverlayMode::Pinned);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRbReplayHistoryCap, "RawBreak.Unit.Replay.HistoryCap", RB_UNIT_TEST_FLAGS)
bool FRbReplayHistoryCap::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<URbReplaySubsystem> Replay(NewObject<URbReplaySubsystem>(GetTransientPackage()));
	TestEqual(TEXT("32 shots by default (ue-architecture 6.7)"), Replay->MaxShots, 32);
	TestFalse(TEXT("nothing to replay"), Replay->PlayReplay(0, ERbReplayView::Shooter));
	for (uint32 Id = 1; Id <= 40; ++Id)
	{
		const TSharedRef<FRbShot> Shot = MakeShared<FRbShot>();
		Shot->Id = Id;
		Replay->RecordShot(Shot);
	}
	TestEqual(TEXT("capped at MaxShots"), Replay->GetShotCount(), 32);
	TestTrue(TEXT("index 0 = the last shot"), Replay->GetShot(0).IsValid() && Replay->GetShot(0)->Id == 40u);
	TestTrue(TEXT("index 31 = the oldest kept (9)"), Replay->GetShot(31).IsValid() && Replay->GetShot(31)->Id == 9u);
	TestFalse(TEXT("index 32 is gone"), Replay->GetShot(32).IsValid());
	TestFalse(TEXT("negative index"), Replay->GetShot(-1).IsValid());
	Replay->MaxShots = 5;
	const TSharedRef<FRbShot> Next = MakeShared<FRbShot>();
	Next->Id = 41;
	Replay->RecordShot(Next);
	TestEqual(TEXT("a smaller cap trims on the next record"), Replay->GetShotCount(), 5);
	TestTrue(TEXT("newest kept"), Replay->GetShot(0)->Id == 41u && Replay->GetShot(4)->Id == 37u);
	// No world: no ball set, no director - a replay cannot start and nothing changes.
	TestFalse(TEXT("no replay without a world"), Replay->PlayReplay(0, ERbReplayView::Overhead, 1.0f));
	TestFalse(TEXT("not replaying"), Replay->IsReplaying());
	Replay->StopReplay();
	Replay->CycleView();
	TestEqual(TEXT("CycleView cycles the view"), Replay->GetView(), ERbReplayView::Overhead);
	TestEqual(TEXT("names"), FString(URbReplaySubsystem::ViewName(ERbReplayView::Follow)), FString(TEXT("Follow")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
