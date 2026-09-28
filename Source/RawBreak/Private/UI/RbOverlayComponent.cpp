#include "UI/RbOverlayComponent.h"

#include "RawBreak.h"
#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "Player/RbStrokeComponent.h"
#include "Replay/RbReplaySubsystem.h"
#include "UI/SRbInfoOverlay.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "UnrealClient.h"

#include "rb/Rules/TableRules.h"

// Owner: UE-7. Model, modes and fades of Docs/ue-architecture.md 6.6 (the header lists the rules); tests
// RawBreak.Unit.Overlay.* (RbOverlayTests.cpp).
//
// Text rules of BuildModel (balls by NUMBER, never by colour - plan 12.22; names only in hot-seat, practice is one human):
//   mandatory  replay tag | lag turn | pending decision with every option (the highlighted one in brackets) | ball in hand
//              for the incoming shooter with its region | the shooter's consecutive fouls while > 0 ("TWO FOULS" = the
//              mandatory warning, Reg 8) | rack over / match over with the Confirm prompt
//   title      discipline + mode; subtitle: score and race (hot-seat) or rack number (practice)
//   match      shooter / phase, both foul counters (always displayed, rules.md 16 item 12), push-out window, call
//   last shot  pocketed balls, first contact, the ENFORCED foul with its rule reference (rules.md 16 item 2) and every other
//              detected foul, what follows (continue / turn / decision / rack)
//   debug (F2) shot index, sim time, events, hand-off frames, strike, human layer (intended vs executed, miscue prediction vs
//              physics, shaft-contact candidates of the Assisted mode, R-11), tip chalk, raw-input timestamp mode, hashes

#define LOCTEXT_NAMESPACE "RbOverlay"

namespace
{
	TAutoConsoleVariable<int32> CVarRbOverlayInScreenshots(TEXT("rb.Overlay.InScreenshots"), 0,
		TEXT("1 = screenshots requested without UI (the headless capture, rbue.py capture) include the info overlay (Slate window capture)."),
		ECVF_Cheat);
}

namespace RbOverlayText
{
	using rb::rules::Foul;
	using rb::rules::Option;

	bool IsHotSeat(const URbMatchDirector& D)
	{
		return D.GetSetup().Mode == ERbMatchMode::HotSeat;
	}

	FText Name(const URbMatchDirector& D, int32 Player)
	{
		if (Player < 0 || Player > 1)
		{
			return LOCTEXT("NobodyName", "-");
		}
		return FText::FromString(D.GetShooter(Player).Name);
	}

	FText DisciplineName(ERbDiscipline Discipline)
	{
		switch (Discipline)
		{
		case ERbDiscipline::NineBall: return LOCTEXT("NineBall", "9-BALL");
		case ERbDiscipline::EightBall: return LOCTEXT("EightBall", "8-BALL");
		case ERbDiscipline::TenBall: return LOCTEXT("TenBall", "10-BALL");
		case ERbDiscipline::StraightPool: return LOCTEXT("StraightPool", "14.1 STRAIGHT POOL");
		}
		return FText::GetEmpty();
	}

	FText BallName(int32 Ball)
	{
		return Ball == 0 ? LOCTEXT("CueBall", "cue ball") : FText::AsNumber(Ball);
	}

	FText BallList(const TArray<int32>& Balls)
	{
		TArray<FText> Parts;
		for (const int32 Ball : Balls)
		{
			Parts.Add(BallName(Ball));
		}
		return FText::Join(LOCTEXT("ListSeparator", ", "), Parts);
	}

	FText PocketName(rb::PocketId Pocket)
	{
		switch (Pocket)
		{
		case rb::PocketId::HeadRight: return LOCTEXT("PocketHeadRight", "head right corner");
		case rb::PocketId::SideRight: return LOCTEXT("PocketSideRight", "right side pocket");
		case rb::PocketId::FootRight: return LOCTEXT("PocketFootRight", "foot right corner");
		case rb::PocketId::FootLeft: return LOCTEXT("PocketFootLeft", "foot left corner");
		case rb::PocketId::SideLeft: return LOCTEXT("PocketSideLeft", "left side pocket");
		case rb::PocketId::HeadLeft: return LOCTEXT("PocketHeadLeft", "head left corner");
		case rb::PocketId::None: break;
		}
		return LOCTEXT("PocketAny", "any pocket");
	}

	FText FoulName(Foul F)
	{
		switch (F)
		{
		case Foul::CueBallScratch: return LOCTEXT("FoulScratch", "cue ball scratched");
		case Foul::CueBallOffTable: return LOCTEXT("FoulCueOffTable", "cue ball off the table");
		case Foul::WrongBallFirst: return LOCTEXT("FoulWrongBall", "wrong ball first");
		case Foul::NoRailAfterContact: return LOCTEXT("FoulNoRail", "no rail after contact");
		case Foul::BreakTooFewRails: return LOCTEXT("FoulBreakRails", "illegal break (too few balls to a rail)");
		case Foul::NoFootOnFloor: return LOCTEXT("FoulFoot", "no foot on the floor");
		case Foul::ObjectBallOffTable: return LOCTEXT("FoulBallOffTable", "object ball off the table");
		case Foul::TouchedBall: return LOCTEXT("FoulTouched", "ball touched");
		case Foul::DoubleHit: return LOCTEXT("FoulDoubleHit", "double hit");
		case Foul::PushShot: return LOCTEXT("FoulPush", "push shot");
		case Foul::BallsStillMoving: return LOCTEXT("FoulMoving", "balls still moving");
		case Foul::BadCueBallPlacement: return LOCTEXT("FoulPlacement", "bad cue-ball placement");
		case Foul::BadPlayAboveHeadStringP1:
		case Foul::BadPlayAboveHeadStringP2: return LOCTEXT("FoulKitchen", "illegal play from the kitchen");
		case Foul::SlowPlay: return LOCTEXT("FoulSlow", "slow play");
		case Foul::TemplateFoul: return LOCTEXT("FoulTemplate", "template foul");
		case Foul::IllegalScoop: return LOCTEXT("FoulScoop", "illegal scoop");
		case Foul::BreakingFoul141: return LOCTEXT("FoulBreaking141", "breaking foul");
		case Foul::ThreeConsecutiveFouls: return LOCTEXT("FoulThree", "third consecutive foul");
		case Foul::BlackballBreakFoul: return LOCTEXT("FoulBlackballBreak", "illegal break");
		case Foul::PottedOpponentBallOnly: return LOCTEXT("FoulOpponentBall", "potted an opponent ball only");
		case Foul::JumpedOverBall: return LOCTEXT("FoulJumped", "jumped over a ball");
		case Foul::Count: break;
		}
		return LOCTEXT("FoulUnknown", "foul");
	}

	FText OptionName(Option O)
	{
		switch (O)
		{
		case Option::AcceptTable: return LOCTEXT("OptAccept", "accept the table");
		case Option::BallInHandAboveHeadString: return LOCTEXT("OptKitchen", "ball in hand behind the head string");
		case Option::RerackDeciderBreaks: return LOCTEXT("OptRerackMe", "re-rack, you break");
		case Option::RerackOffenderBreaks: return LOCTEXT("OptRerackThem", "re-rack, opponent breaks");
		case Option::Spot8ContinueFromPosition: return LOCTEXT("OptSpot8", "spot the 8, shoot from here");
		case Option::Spot8BallInHandAboveHeadString: return LOCTEXT("OptSpot8Kitchen", "spot the 8, ball in hand in the kitchen");
		case Option::AcceptTableNoPushOut: return LOCTEXT("OptAcceptNoPush", "accept the table");
		case Option::HandBackPushOutAllowed: return LOCTEXT("OptHandBack", "hand it back (push-out allowed)");
		case Option::ShootFromPosition: return LOCTEXT("OptShoot", "shoot from here");
		case Option::PassBack: return LOCTEXT("OptPassBack", "pass it back");
		case Option::RequireRebreak: return LOCTEXT("OptRebreak", "opponent breaks again");
		}
		return FText::GetEmpty();
	}

	FText RegionName(rb::rules::CueBallNext Region)
	{
		switch (Region)
		{
		case rb::rules::CueBallNext::InHandAnywhere: return LOCTEXT("RegionAnywhere", "anywhere");
		case rb::rules::CueBallNext::InHandAboveHeadString: return LOCTEXT("RegionKitchen", "behind the head string");
		case rb::rules::CueBallNext::InHandBaulk: return LOCTEXT("RegionBaulk", "in baulk");
		case rb::rules::CueBallNext::InPosition: break;
		}
		return FText::GetEmpty();
	}

	FText ViewName(ERbReplayView View)
	{
		switch (View)
		{
		case ERbReplayView::Shooter: return LOCTEXT("ViewShooter", "Shooter");
		case ERbReplayView::Overhead: return LOCTEXT("ViewOverhead", "Overhead");
		case ERbReplayView::Rail: return LOCTEXT("ViewRail", "Rail");
		case ERbReplayView::Follow: return LOCTEXT("ViewFollow", "Follow");
		}
		return FText::GetEmpty();
	}

	// "Player 1: ball in hand" in hot-seat, "Ball in hand" in practice (one human).
	FText WithName(const URbMatchDirector& D, int32 Player, const FText& Text)
	{
		if (!IsHotSeat(D))
		{
			FString Solo = Text.ToString();
			if (Solo.Len() > 0)
			{
				Solo[0] = FChar::ToUpper(Solo[0]);
			}
			return FText::FromString(Solo);
		}
		return FText::Format(LOCTEXT("NamedLine", "{0}: {1}"), Name(D, Player), Text);
	}

	// Winner of the rack that the last shot ended: the tracked value when known, else the rules' convention (a foul that ends
	// the rack - third foul, 8 on a foul - gives it to the opponent).
	int32 RackWinner(const URbMatchDirector& D, const FRbOverlayExtras& Extras)
	{
		if (Extras.LastRackWinner == 0 || Extras.LastRackWinner == 1)
		{
			return Extras.LastRackWinner;
		}
		const FRbLastShotSummary& Last = D.GetLastShot();
		if (Last.Shooter < 0 || Last.Shooter > 1)
		{
			return -1;
		}
		return Last.Enforced != Foul::Count ? 1 - Last.Shooter : Last.Shooter;
	}

	FText ThirdFoulConsequence(const URbMatchDirector& D)
	{
		return D.GetSetup().Discipline == ERbDiscipline::StraightPool ? LOCTEXT("ThirdFoul141", "a third foul costs 15 points")
			: LOCTEXT("ThirdFoulRack", "a third foul loses the rack");
	}

	bool IsShooterAtTable(ERbDirectorPhase Phase)
	{
		return Phase == ERbDirectorPhase::AwaitStroke || Phase == ERbDirectorPhase::AwaitPlacement || Phase == ERbDirectorPhase::Simulating ||
			Phase == ERbDirectorPhase::PlayingBack;
	}

	void Add(TArray<FText>& Lines, TArray<ERbOverlayTone>& Tones, const FText& Text, ERbOverlayTone Tone)
	{
		Lines.Add(Text);
		Tones.Add(Tone);
	}

	FText Debug(const FString& Line)
	{
		return FText::FromString(Line);
	}

	const TCHAR* PhaseName(ERbDirectorPhase Phase)
	{
		switch (Phase)
		{
		case ERbDirectorPhase::Idle: return TEXT("Idle");
		case ERbDirectorPhase::Lag: return TEXT("Lag");
		case ERbDirectorPhase::AwaitStroke: return TEXT("AwaitStroke");
		case ERbDirectorPhase::AwaitPlacement: return TEXT("AwaitPlacement");
		case ERbDirectorPhase::Simulating: return TEXT("Simulating");
		case ERbDirectorPhase::PlayingBack: return TEXT("PlayingBack");
		case ERbDirectorPhase::AwaitDecision: return TEXT("AwaitDecision");
		case ERbDirectorPhase::RackOver: return TEXT("RackOver");
		case ERbDirectorPhase::MatchOver: return TEXT("MatchOver");
		}
		return TEXT("?");
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------------------------------------------------

FRbOverlayModel URbOverlayComponent::BuildModel(const URbMatchDirector* Director, const FRbOverlayExtras& Extras)
{
	using namespace RbOverlayText;
	FRbOverlayModel M;
	M.bShowDebug = Extras.bDebug;

	// Replay tag first: it explains why the table does not answer.
	if (Extras.bReplaying)
	{
		const FText Rate = FMath::IsNearlyEqual(Extras.ReplayRate, 1.0f) ? FText::GetEmpty()
			: (Extras.ReplayRate <= 0.0f ? LOCTEXT("ReplayPaused", "  \u00B7  paused")
				: FText::Format(LOCTEXT("ReplayRate", "  \u00B7  {0}x"), FText::AsNumber(Extras.ReplayRate, &FNumberFormattingOptions::DefaultNoGrouping())));
		Add(M.MandatoryLines, M.MandatoryTones, FText::Format(LOCTEXT("ReplayTag", "REPLAY  \u00B7  {0}{1}  \u00B7  R: next view"), ViewName(Extras.ReplayView), Rate),
			ERbOverlayTone::Info);
	}

	if (!Director || Director->GetPhase() == ERbDirectorPhase::Idle)
	{
		M.Title = LOCTEXT("NoMatchTitle", "RAW BREAK");
		Add(M.MatchLines, M.MatchTones, LOCTEXT("NoMatch", "No match running"), ERbOverlayTone::Dim);
		return M;
	}
	const URbMatchDirector& D = *Director;
	const rb::rules::MatchState& S = D.GetMatchState();
	const rb::rules::GameState& G = S.Game;
	const FRbMatchSetup& Setup = D.GetSetup();
	const ERbDirectorPhase Phase = D.GetPhase();
	const bool bHotSeat = IsHotSeat(D);
	const bool bStraight = Setup.Discipline == ERbDiscipline::StraightPool;
	const int32 Shooter = D.GetActivePlayer();
	const rb::rules::ShotConstraints Constraints = D.GetConstraints();

	// --- mandatory lines (every mode) -----------------------------------------------------------------------------------
	if (Phase == ERbDirectorPhase::Lag)
	{
		Add(M.MandatoryLines, M.MandatoryTones, FText::Format(LOCTEXT("LagTurn", "LAG  \u00B7  {0} to lag"), Name(D, D.GetLagStroker())), ERbOverlayTone::Info);
	}
	if (Phase == ERbDirectorPhase::AwaitDecision)
	{
		TArray<FText> Options;
		const int32 Count = S.PendingOutcome.Options.Size();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FText Option = OptionName(S.PendingOutcome.Options[Index]);
			Options.Add(Index == D.GetSelectedOption() ? FText::Format(LOCTEXT("SelectedOption", "[{0}]"), Option) : Option);
		}
		const FText Choice = FText::Join(LOCTEXT("OptionSeparator", "  \u00B7  "), Options);
		const FText Line = bHotSeat ? FText::Format(LOCTEXT("DecisionNamed", "{0} decides:  {1}   (Q/E, Enter)"), Name(D, S.Decider), Choice)
			: FText::Format(LOCTEXT("Decision", "Decide:  {0}   (Q/E, Enter)"), Choice);
		Add(M.MandatoryLines, M.MandatoryTones, Line, ERbOverlayTone::Info);
	}
	if (Phase == ERbDirectorPhase::AwaitPlacement)
	{
		Add(M.MandatoryLines, M.MandatoryTones,
			WithName(D, Shooter, FText::Format(LOCTEXT("BallInHand", "ball in hand {0}"), RegionName(Constraints.PlacementRegion))),
			ERbOverlayTone::Info);
	}
	if (IsShooterAtTable(Phase) && Shooter >= 0 && Shooter <= 1)
	{
		const int32 Fouls = G.Players[Shooter].ConsecutiveFouls;
		if (Constraints.ThreeFoulWarning || Fouls >= 2)
		{
			Add(M.MandatoryLines, M.MandatoryTones,
				WithName(D, Shooter, FText::Format(LOCTEXT("TwoFouls", "ON TWO FOULS  \u2013  {0}"), ThirdFoulConsequence(D))), ERbOverlayTone::Warning);
		}
		else if (Fouls == 1)
		{
			Add(M.MandatoryLines, M.MandatoryTones, WithName(D, Shooter, LOCTEXT("OneFoul", "on 1 foul")), ERbOverlayTone::Warning);
		}
	}
	if (Phase == ERbDirectorPhase::RackOver)
	{
		const int32 Winner = RackWinner(D, Extras);
		const FText Line = bHotSeat && Winner >= 0
			? FText::Format(LOCTEXT("RackOverNamed", "Rack over  \u2013  {0} wins the rack  \u00B7  Enter: next rack"), Name(D, Winner))
			: LOCTEXT("RackOver", "Rack over  \u00B7  Enter: next rack");
		Add(M.MandatoryLines, M.MandatoryTones, Line, ERbOverlayTone::Info);
	}
	if (Phase == ERbDirectorPhase::MatchOver)
	{
		const int32 Winner = S.Winner;
		const FText Line = Winner >= 0 && Winner <= 1
			? FText::Format(LOCTEXT("MatchOverNamed", "Match over  \u2013  {0} wins {1} : {2}  \u00B7  Enter: new match"), Name(D, Winner),
				FText::AsNumber(bStraight ? G.Players[Winner].Score : S.RackWins[Winner]),
				FText::AsNumber(bStraight ? G.Players[1 - Winner].Score : S.RackWins[1 - Winner]))
			: LOCTEXT("MatchOver", "Match over  \u00B7  Enter: new match");
		Add(M.MandatoryLines, M.MandatoryTones, Line, ERbOverlayTone::Info);
	}

	// --- title / subtitle ----------------------------------------------------------------------------------------------
	M.Title = FText::Format(LOCTEXT("Title", "{0}  \u00B7  {1}"), DisciplineName(Setup.Discipline),
		bHotSeat ? LOCTEXT("HotSeat", "HOT-SEAT") : LOCTEXT("Practice", "PRACTICE"));
	const FText Rack = FText::Format(LOCTEXT("RackNumber", "rack {0}"), FText::AsNumber(FMath::Max(1, S.RackNumber)));
	if (bHotSeat)
	{
		const int32 A = bStraight ? G.Players[0].Score : S.RackWins[0];
		const int32 B = bStraight ? G.Players[1].Score : S.RackWins[1];
		const FText Race = bStraight ? FText::Format(LOCTEXT("RacePoints", "to {0} points"), FText::AsNumber(D.GetMatchConfig().TargetPoints))
			: FText::Format(LOCTEXT("RaceTo", "race to {0}"), FText::AsNumber(D.GetMatchConfig().RaceTo));
		M.Subtitle = FText::Format(LOCTEXT("Score", "{0}   {1} : {2}   {3}     {4}  \u00B7  {5}"), Name(D, 0), FText::AsNumber(A), FText::AsNumber(B),
			Name(D, 1), Race, Rack);
	}
	else
	{
		M.Subtitle = bStraight ? FText::Format(LOCTEXT("PracticeScore141", "{0}  \u00B7  {1} points"), Rack,
			FText::AsNumber(G.Players[0].Score + G.Players[1].Score))
			: Rack;
	}

	// --- match lines ---------------------------------------------------------------------------------------------------
	switch (Phase)
	{
	case ERbDirectorPhase::Lag:
		Add(M.MatchLines, M.MatchTones, LOCTEXT("LagLine", "Lag for the break: closest to the head rail wins"), ERbOverlayTone::Normal);
		break;
	case ERbDirectorPhase::AwaitPlacement:
	case ERbDirectorPhase::AwaitStroke:
	{
		const FText Action = G.IsBreakShot ? LOCTEXT("ToBreak", "to break") : LOCTEXT("ToShoot", "to shoot");
		const FText Line = bHotSeat ? FText::Format(LOCTEXT("ShooterNamed", "{0} {1}"), Name(D, Shooter), Action)
			: (G.IsBreakShot ? LOCTEXT("YourBreak", "Your break") : LOCTEXT("YourShot", "Your shot"));
		Add(M.MatchLines, M.MatchTones, Line, ERbOverlayTone::Normal);
		break;
	}
	case ERbDirectorPhase::Simulating:
	case ERbDirectorPhase::PlayingBack:
		Add(M.MatchLines, M.MatchTones, LOCTEXT("Rolling", "Balls rolling"), ERbOverlayTone::Dim);
		break;
	case ERbDirectorPhase::AwaitDecision:
		Add(M.MatchLines, M.MatchTones, bHotSeat ? FText::Format(LOCTEXT("WaitDecisionNamed", "Waiting for {0}'s decision"), Name(D, S.Decider))
			: LOCTEXT("WaitDecision", "Waiting for your decision"), ERbOverlayTone::Normal);
		break;
	default:
		break;
	}
	// The foul counters are ALWAYS on display in the full card (rules.md 16 item 12).
	if (bHotSeat)
	{
		Add(M.MatchLines, M.MatchTones, FText::Format(LOCTEXT("FoulsBoth", "Fouls in a row:  {0} {1}  \u00B7  {2} {3}"), Name(D, 0),
			FText::AsNumber(G.Players[0].ConsecutiveFouls), Name(D, 1), FText::AsNumber(G.Players[1].ConsecutiveFouls)), ERbOverlayTone::Normal);
	}
	else
	{
		const int32 Player = FMath::Clamp(Shooter, 0, 1);
		Add(M.MatchLines, M.MatchTones, FText::Format(LOCTEXT("FoulsOne", "Fouls in a row:  {0}"), FText::AsNumber(G.Players[Player].ConsecutiveFouls)),
			ERbOverlayTone::Normal);
	}
	if (Phase == ERbDirectorPhase::AwaitStroke || Phase == ERbDirectorPhase::AwaitPlacement)
	{
		const rb::rules::ShotDeclaration& Declaration = D.GetDeclaration();
		if (Constraints.PushOutAllowed)
		{
			Add(M.MatchLines, M.MatchTones, Declaration.Kind == rb::rules::ShotKind::PushOut ? LOCTEXT("PushOutDeclared", "Push-out declared")
				: LOCTEXT("PushOutAvailable", "Push-out available"), ERbOverlayTone::Info);
		}
		else if (Declaration.Kind == rb::rules::ShotKind::Safety)
		{
			Add(M.MatchLines, M.MatchTones, LOCTEXT("SafetyDeclared", "Safety declared"), ERbOverlayTone::Info);
		}
		if (Declaration.Called.Ball != rb::kNoBall)
		{
			Add(M.MatchLines, M.MatchTones, FText::Format(LOCTEXT("Called", "Called: {0} in the {1}"), BallName(Declaration.Called.Ball),
				PocketName(Declaration.Called.Pocket)), ERbOverlayTone::Info);
		}
		else if (Constraints.CallRequired)
		{
			Add(M.MatchLines, M.MatchTones, LOCTEXT("CallRequired", "Call your shot"), ERbOverlayTone::Info);
		}
	}

	// --- last shot -----------------------------------------------------------------------------------------------------
	const FRbLastShotSummary& Last = D.GetLastShot();
	if (Last.bValid)
	{
		const FText Header = bHotSeat && !Last.bLag && Last.Shooter >= 0
			? FText::Format(LOCTEXT("LastShotNamed", "LAST SHOT  \u00B7  {0}"), Name(D, Last.Shooter)) : LOCTEXT("LastShot", "LAST SHOT");
		Add(M.LastShotLines, M.LastShotTones, Header, ERbOverlayTone::Dim);
		if (Last.bLag)
		{
			if (Last.LagWinner == 0 || Last.LagWinner == 1)
			{
				Add(M.LastShotLines, M.LastShotTones, FText::Format(LOCTEXT("LagWon", "Lag won by {0}  ({1})"), Name(D, Last.LagWinner),
					FText::FromString(Last.RuleRef)), ERbOverlayTone::Good);
			}
			else
			{
				Add(M.LastShotLines, M.LastShotTones, LOCTEXT("Relag", "Lag undecided: both lag again"), ERbOverlayTone::Info);
			}
		}
		else
		{
			Add(M.LastShotLines, M.LastShotTones, Last.Pocketed.Num() > 0 ? FText::Format(LOCTEXT("Pocketed", "Pocketed: {0}"), BallList(Last.Pocketed))
				: LOCTEXT("NothingPocketed", "Nothing pocketed"), Last.Pocketed.Num() > 0 ? ERbOverlayTone::Good : ERbOverlayTone::Normal);
			const bool bContact = Last.FirstContactBall >= 1 && Last.FirstContactBall < rb::rules::kRulesBallCount;
			Add(M.LastShotLines, M.LastShotTones, bContact ? FText::Format(LOCTEXT("FirstContact", "First contact: {0}"), BallName(Last.FirstContactBall))
				: LOCTEXT("NoContact", "No ball contacted"), ERbOverlayTone::Normal);
			if (Last.Enforced != rb::rules::Foul::Count)
			{
				const FText Enforced = FoulName(Last.Enforced);
				Add(M.LastShotLines, M.LastShotTones, Last.RuleRef.IsEmpty() ? FText::Format(LOCTEXT("FoulLine", "FOUL: {0}"), Enforced)
					: FText::Format(LOCTEXT("FoulLineRef", "FOUL: {0}  ({1})"), Enforced, FText::FromString(Last.RuleRef)), ERbOverlayTone::Warning);
				TArray<FText> Others;
				for (int32 F = 0; F < static_cast<int32>(rb::rules::Foul::Count); ++F)
				{
					const rb::rules::Foul Detected = static_cast<rb::rules::Foul>(F);
					if (Detected != Last.Enforced && Last.Fouls.Has(Detected))
					{
						Others.Add(FoulName(Detected));
					}
				}
				if (Others.Num() > 0)
				{
					Add(M.LastShotLines, M.LastShotTones, FText::Format(LOCTEXT("AlsoFouls", "also detected: {0}"),
						FText::Join(LOCTEXT("FoulSeparator", ", "), Others)), ERbOverlayTone::Dim);
				}
			}
			FText Next;
			ERbOverlayTone NextTone = ERbOverlayTone::Normal;
			switch (Last.Next)
			{
			case rb::rules::NextAction::Continue:
				Next = bHotSeat ? FText::Format(LOCTEXT("Continues", "{0} continues"), Name(D, Last.Shooter)) : LOCTEXT("KeepShooting", "Keep shooting");
				break;
			case rb::rules::NextAction::Pass:
				Next = bHotSeat ? FText::Format(LOCTEXT("TurnTo", "Turn to {0}"), Name(D, 1 - FMath::Clamp(Last.Shooter, 0, 1)))
					: LOCTEXT("TurnOver", "Turn over");
				break;
			case rb::rules::NextAction::AwaitDecision:
				Next = LOCTEXT("DecisionFollows", "A decision is pending");
				break;
			case rb::rules::NextAction::RackWon:
			{
				const int32 Winner = RackWinner(D, Extras);
				Next = bHotSeat && Winner >= 0 ? FText::Format(LOCTEXT("RackTo", "Rack to {0}"), Name(D, Winner)) : LOCTEXT("RackWon", "Rack won");
				NextTone = ERbOverlayTone::Good;
				break;
			}
			case rb::rules::NextAction::MatchWon:
				Next = S.Winner >= 0 ? FText::Format(LOCTEXT("MatchTo", "Match to {0}"), Name(D, S.Winner)) : LOCTEXT("MatchWon", "Match won");
				NextTone = ERbOverlayTone::Good;
				break;
			case rb::rules::NextAction::RerackAndBreak:
				Next = LOCTEXT("Rerack", "Re-rack and break");
				break;
			}
			Add(M.LastShotLines, M.LastShotTones, Next, NextTone);
		}
	}

	// --- debug block (F2) ----------------------------------------------------------------------------------------------
	if (Extras.bDebug)
	{
		TArray<FText>& Dbg = M.DebugLines;
		Dbg.Add(Debug(FString::Printf(TEXT("shot %u  phase %s  seed %llu"), D.GetMatchShotIndex(), PhaseName(Phase), D.GetMatchSeed())));
		const TSharedPtr<const FRbShot> Shot = D.GetLastCommittedShot();
		if (Shot.IsValid())
		{
			const rb::ShotResult& Result = Shot->Result;
			const int64 HandOff = static_cast<int64>(Shot->HandOffFrame) - static_cast<int64>(Shot->SubmitFrame);
			Dbg.Add(Debug(FString::Printf(TEXT("sim %.2f ms  %d events  stop %.2f s  hand-off +%lld fr%s"), Shot->SimMilliseconds,
				static_cast<int32>(Result.Events.size()), Result.StopTime, HandOff, Shot->bSimulatedOnWorker ? TEXT(" (worker)") : TEXT(""))));
			if (Shot->Request.Input.Strikes.Size() > 0)
			{
				const rb::CueStrikeInput& Strike = Shot->Request.Input.Strikes[0].Input;
				Dbg.Add(Debug(FString::Printf(TEXT("strike V %.3f m/s  phi %.2f  theta %.2f deg  a %+.3f  b %+.3f"), Strike.Speed,
					FMath::RadiansToDegrees(Strike.Azimuth), FMath::RadiansToDegrees(Strike.Elevation), Strike.OffsetA, Strike.OffsetB)));
			}
			const FRbStrokeRecord& Stroke = Shot->Request.Stroke;
			if (Stroke.bHuman)
			{
				const rb::human::ExecutedStroke& X = Stroke.Executed;
				Dbg.Add(Debug(FString::Printf(TEXT("intended V %.3f  exec V %.3f  d-phi %+.3f deg  address %u"), Stroke.Intended.Speed, X.Strike.Speed,
					FMath::RadiansToDegrees(X.Strike.Azimuth - Stroke.Intended.Azimuth), Stroke.Key.AddressIndex)));
				Dbg.Add(Debug(FString::Printf(TEXT("miscue predicted %s  physics %s  rho %.3f / %.3f  shaft contacts %d"),
					Last.bPredictedMiscue ? TEXT("yes") : TEXT("no"), Last.bMiscue ? TEXT("yes") : TEXT("no"), X.Rho, X.MiscueLimit,
					static_cast<int32>(X.ShaftContactCandidates.Size()))));
			}
			else
			{
				Dbg.Add(Debug(FString::Printf(TEXT("scripted strike (no human layer)  miscue physics %s"), Last.bMiscue ? TEXT("yes") : TEXT("no"))));
			}
			Dbg.Add(Debug(FString::Printf(TEXT("hash in %016llx  out %016llx"), Shot->InputHash, Shot->ResultHash)));
		}
		if (Shooter >= 0 && Shooter <= 1)
		{
			const FRbShooterState& State = D.GetShooter(Shooter);
			double MinCoverage = 1.0;
			for (const double C : State.Tip.Coverage)
			{
				MinCoverage = FMath::Min(MinCoverage, C);
			}
			Dbg.Add(Debug(FString::Printf(TEXT("tip chalk min %.0f %%  twists %d  shooter shot %u"), 100.0 * MinCoverage, State.LastChalkTwists,
				State.ShooterShotIndex)));
		}
		Dbg.Add(Debug(Extras.RawInputMode == 1 ? FString(TEXT("raw input: true per-report timestamps"))
			: (Extras.RawInputMode == 0 ? FString(TEXT("raw input: reconstructed report times")) : FString(TEXT("raw input: n/a")))));
		if (!D.GetLastError().IsEmpty())
		{
			Dbg.Add(Debug(FString::Printf(TEXT("last refusal: %s"), *D.GetLastError())));
		}
	}
	return M;
}

// ---------------------------------------------------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------------------------------------------------

URbOverlayComponent::URbOverlayComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true; // the glance works in a paused world too
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void URbOverlayComponent::SetGlanceHeld(bool bHeld)
{
	bGlanceHeld = bHeld;
	UpdateVisibility();
}

void URbOverlayComponent::TogglePinned()
{
	SetPinned(!bPinned);
}

void URbOverlayComponent::SetPinned(bool bPin)
{
	bPinned = bPin;
	UpdateVisibility();
}

void URbOverlayComponent::ToggleDebug()
{
	SetDebugShown(!bDebug);
}

void URbOverlayComponent::SetDebugShown(bool bShow)
{
	bDebug = bShow;
	Refresh();
}

void URbOverlayComponent::StartAutoGlance()
{
	AutoGlanceRemaining = FMath::Max(0.0f, AutoGlanceSeconds);
	UpdateVisibility();
}

void URbOverlayComponent::UpdateVisibility()
{
	Mode = bPinned ? ERbOverlayMode::Pinned : ((bGlanceHeld || AutoGlanceRemaining > 0.0f) ? ERbOverlayMode::Glance : ERbOverlayMode::Hidden);
}

void URbOverlayComponent::AdvanceOverlay(float DeltaSeconds)
{
	const float Dt = FMath::Max(0.0f, DeltaSeconds);
	if (AutoGlanceRemaining > 0.0f)
	{
		AutoGlanceRemaining = FMath::Max(0.0f, AutoGlanceRemaining - Dt);
	}
	UpdateVisibility();
	const float Target = GetTargetOpacity();
	if (Opacity < Target)
	{
		Opacity = FadeInSeconds > 0.0f ? FMath::Min(Target, Opacity + Dt / FadeInSeconds) : Target;
	}
	else if (Opacity > Target)
	{
		Opacity = FadeOutSeconds > 0.0f ? FMath::Max(Target, Opacity - Dt / FadeOutSeconds) : Target;
	}
	if (Widget.IsValid())
	{
		Widget->SetFullOpacity(Opacity);
	}
}

FRbOverlayExtras URbOverlayComponent::MakeExtras() const
{
	FRbOverlayExtras Extras;
	Extras.bDebug = bDebug;
	Extras.LastRackWinner = LastRackWinner;
	if (const URbReplaySubsystem* Replay = ReplaySubsystem.Get())
	{
		Extras.bReplaying = Replay->IsReplaying();
		Extras.ReplayView = Replay->GetView();
		Extras.ReplayRate = Replay->GetRate();
		Extras.ReplayIndexFromLast = Replay->GetReplayIndex();
	}
	if (const URbMatchDirector* D = Director.Get())
	{
		if (const URbStrokeComponent* Stroke = D->GetStrokeComponent())
		{
			Extras.RawInputMode = Stroke->HasTrueTimestamps() ? 1 : 0;
		}
	}
	return Extras;
}

void URbOverlayComponent::Refresh()
{
	if (!bDirectorOverride)
	{
		EnsureDirectorBound();
	}
	const URbMatchDirector* D = Director.Get();
	if (D)
	{
		// A newly committed shot: the 4 s auto-glance of its result (6.6).
		const TSharedPtr<const FRbShot> Last = D->GetLastCommittedShot();
		if (Last.IsValid() && Last != LastSeenShot.Pin())
		{
			StartAutoGlance();
		}
		LastSeenShot = Last;
		// Rack winner: the rules player whose rack count grew (a new match resets both counts).
		const rb::rules::MatchState& S = D->GetMatchState();
		if (S.RackWins[0] < SeenRackWins[0] || S.RackWins[1] < SeenRackWins[1])
		{
			LastRackWinner = -1;
		}
		else if (S.RackWins[0] > SeenRackWins[0])
		{
			LastRackWinner = 0;
		}
		else if (S.RackWins[1] > SeenRackWins[1])
		{
			LastRackWinner = 1;
		}
		SeenRackWins[0] = S.RackWins[0];
		SeenRackWins[1] = S.RackWins[1];
	}
	Model = BuildModel(D, MakeExtras());
	PushToWidget();
}

void URbOverlayComponent::PushToWidget()
{
	if (Widget.IsValid())
	{
		Widget->SetModel(Model);
		Widget->SetFullOpacity(Opacity);
	}
}

void URbOverlayComponent::SetDirector(URbMatchDirector* InDirector)
{
	bDirectorOverride = InDirector != nullptr;
	BindDirector(InDirector);
	Refresh();
}

URbMatchDirector* URbOverlayComponent::FindDirector() const
{
	const ARbGameMode* GameMode = ARbGameMode::Get(this);
	return GameMode ? GameMode->GetDirector() : nullptr;
}

void URbOverlayComponent::EnsureDirectorBound()
{
	if (bDirectorOverride)
	{
		return;
	}
	URbMatchDirector* Found = FindDirector();
	if (Found != Director.Get())
	{
		BindDirector(Found);
	}
	if (!ReplaySubsystem.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (URbReplaySubsystem* Replay = World->GetSubsystem<URbReplaySubsystem>())
			{
				ReplaySubsystem = Replay;
				ReplayChangedHandle = Replay->OnReplayChanged.AddUObject(this, &URbOverlayComponent::OnReplayChanged);
			}
		}
	}
}

void URbOverlayComponent::BindDirector(URbMatchDirector* InDirector)
{
	UnbindDirector();
	Director = InDirector;
	if (InDirector)
	{
		MatchChangedHandle = InDirector->OnMatchChanged.AddUObject(this, &URbOverlayComponent::OnMatchChanged);
		// What is already committed is not "new": no auto-glance for it.
		LastSeenShot = InDirector->GetLastCommittedShot();
		SeenRackWins[0] = InDirector->GetMatchState().RackWins[0];
		SeenRackWins[1] = InDirector->GetMatchState().RackWins[1];
		LastRackWinner = -1;
	}
}

void URbOverlayComponent::UnbindDirector()
{
	if (URbMatchDirector* Old = Director.Get())
	{
		Old->OnMatchChanged.Remove(MatchChangedHandle);
	}
	MatchChangedHandle.Reset();
	Director.Reset();
}

void URbOverlayComponent::OnMatchChanged()
{
	Refresh();
}

void URbOverlayComponent::OnReplayChanged()
{
	Refresh();
}

void URbOverlayComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureDirectorBound();
	CreateWidget();
	Refresh();
}

void URbOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindDirector();
	if (URbReplaySubsystem* Replay = ReplaySubsystem.Get())
	{
		Replay->OnReplayChanged.Remove(ReplayChangedHandle);
	}
	ReplayChangedHandle.Reset();
	ReplaySubsystem.Reset();
	RemoveWidget();
	Super::EndPlay(EndPlayReason);
}

void URbOverlayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Real time (not dilated): the UI fades the same in slow motion or a paused world.
	const float RealDelta = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.0f, 0.1f);
	if (!Widget.IsValid())
	{
		CreateWidget();
	}
	RefreshCountdown -= RealDelta;
	if (RefreshCountdown <= 0.0f || (!bDirectorOverride && FindDirector() != Director.Get()))
	{
		// Periodic refresh for what has no event (raw-input mode, replay rate); OnMatchChanged covers the match itself.
		RefreshCountdown = 0.25f;
		Refresh();
	}
	AdvanceOverlay(RealDelta);
}

void URbOverlayComponent::CreateWidget()
{
	if (Widget.IsValid() || !FApp::CanEverRender() || !GEngine)
	{
		return;
	}
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	ULocalPlayer* Player = PC && PC->IsLocalController() ? PC->GetLocalPlayer() : nullptr;
	UGameViewportClient* Viewport = Player ? Player->ViewportClient.Get() : nullptr;
	if (!Viewport)
	{
		return;
	}
	Widget = SNew(SRbInfoOverlay);
	Widget->SetModel(Model);
	Widget->SetFullOpacity(Opacity);
	Viewport->AddViewportWidgetForPlayer(Player, Widget.ToSharedRef(), 10);
	WidgetViewport = Viewport;
	BeginDrawHandle = Viewport->OnBeginDraw().AddUObject(this, &URbOverlayComponent::OnViewportBeginDraw);
}

void URbOverlayComponent::RemoveWidget()
{
	if (!Widget.IsValid())
	{
		return;
	}
	UGameViewportClient* Viewport = WidgetViewport.Get();
	if (Viewport)
	{
		Viewport->OnBeginDraw().Remove(BeginDrawHandle);
	}
	BeginDrawHandle.Reset();
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	if (Viewport && Player)
	{
		Viewport->RemoveViewportWidgetForPlayer(Player, Widget.ToSharedRef());
	}
	else if (Viewport)
	{
		Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	}
	Widget.Reset();
	WidgetViewport.Reset();
}

void URbOverlayComponent::OnViewportBeginDraw()
{
	// Dev: the headless capture requests its screenshot without UI during the engine tick; when the viewport starts drawing
	// (before it processes the request), turn it into one with UI so the dev screenshots show the overlay
	// (rb.Overlay.InScreenshots 1).
	if (!Widget.IsValid() || CVarRbOverlayInScreenshots.GetValueOnGameThread() <= 0)
	{
		return;
	}
	if (FScreenshotRequest::IsScreenshotRequested() && !FScreenshotRequest::ShouldShowUI())
	{
		FScreenshotRequest::RequestScreenshot(true);
	}
}

#undef LOCTEXT_NAMESPACE
