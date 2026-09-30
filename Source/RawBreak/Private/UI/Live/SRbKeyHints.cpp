#include "UI/Live/SRbKeyHints.h"

#include "Input/RbInputSetup.h"
#include "Interaction/RbInteractionSubsystem.h"
#include "Player/RbPlayerCharacter.h"
#include "Player/RbPlayerController.h"
#include "Replay/RbReplaySubsystem.h"
#include "Settings/RbGameUserSettings.h"
#include "UI/Core/RbUiStyle.h"
#include "UI/Core/RbUiSubsystem.h"
#include "UI/Widgets/SRbMenuWidgets.h"

#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "InputMappingContext.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

// Owner: M2-D. Contextual key hints (header). Tests: RawBreak.Unit.UI.KeyHints* (RbUiTests.cpp).

#define LOCTEXT_NAMESPACE "RbKeyHints"

namespace RbKeyHintsPrivate
{
	// The M1 bindings (URbInputSetup) as the labels when no controller mapping is known.
	FText DefaultKey(FName Action)
	{
		static const TMap<FName, FText> Defaults = {
			{TEXT("GetDown"), LOCTEXT("DefGetDown", "RMB")},
			{TEXT("Stroke"), LOCTEXT("DefStroke", "LMB")},
			{TEXT("Commit"), LOCTEXT("DefCommit", "Space")},
			{TEXT("FineAim"), LOCTEXT("DefFine", "Shift")},
			{TEXT("Settle"), LOCTEXT("DefSettle", "Ctrl")},
			{TEXT("Elevation"), LOCTEXT("DefElevation", "Wheel")},
			{TEXT("TipOffset"), LOCTEXT("DefTip", "Arrows")},
			{TEXT("Confirm"), LOCTEXT("DefConfirm", "Enter / F")},
			{TEXT("CycleOption"), LOCTEXT("DefCycle", "Q / E")},
			{TEXT("Replay"), LOCTEXT("DefReplay", "R")},
		};
		const FText* Found = Defaults.Find(Action);
		return Found ? *Found : FText::FromName(Action);
	}

	// The confirm key alone ("F"): the interaction prompt names the key you press at the object.
	FText LastKey(const FText& Keys)
	{
		FString Left, Right;
		return Keys.ToString().Split(TEXT(" / "), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd) ? FText::FromString(Right) : Keys;
	}

	void Add(FRbKeyHintsModel& M, const FText& Key, const FText& Verb)
	{
		M.Hints.Add({Key, Verb});
	}
}

FText FRbKeyHintContext::KeyFor(FName Action) const
{
	const FText* Found = Keys.Find(Action);
	return Found && !Found->IsEmpty() ? *Found : RbKeyHintsPrivate::DefaultKey(Action);
}

FString FRbKeyHintsModel::ToDebugString() const
{
	FString Out;
	for (const FRbKeyHint& Hint : Hints)
	{
		if (!Out.IsEmpty())
		{
			Out += TEXT("  ");
		}
		Out += FString::Printf(TEXT("[%s] %s"), *Hint.Key.ToString(), *Hint.Verb.ToString());
	}
	if (!Note.IsEmpty())
	{
		Out += (Out.IsEmpty() ? TEXT("") : TEXT(" | ")) + Note.ToString();
	}
	return Out;
}

FText FRbKeyHintsModel::KeyLabel(const FKey& Key)
{
	if (Key == EKeys::LeftMouseButton) return LOCTEXT("LMB", "LMB");
	if (Key == EKeys::RightMouseButton) return LOCTEXT("RMB", "RMB");
	if (Key == EKeys::MiddleMouseButton) return LOCTEXT("MMB", "MMB");
	if (Key == EKeys::SpaceBar) return LOCTEXT("Space", "Space");
	if (Key == EKeys::LeftShift || Key == EKeys::RightShift) return LOCTEXT("Shift", "Shift");
	if (Key == EKeys::LeftControl || Key == EKeys::RightControl) return LOCTEXT("Ctrl", "Ctrl");
	if (Key == EKeys::LeftAlt || Key == EKeys::RightAlt) return LOCTEXT("Alt", "Alt");
	if (Key == EKeys::MouseWheelAxis || Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown) return LOCTEXT("Wheel", "Wheel");
	if (Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Left || Key == EKeys::Right) return LOCTEXT("Arrows", "Arrows");
	if (Key == EKeys::Enter) return LOCTEXT("Enter", "Enter");
	if (Key == EKeys::Escape) return LOCTEXT("Esc", "Esc");
	return Key.GetDisplayName(false);
}

FText FRbKeyHintsModel::KeysFor(const URbInputSetup* Setup, const UInputAction* Action)
{
	if (!Setup || !Setup->Context || !Action)
	{
		return FText::GetEmpty();
	}
	TArray<FString> Labels;
	for (const FEnhancedActionKeyMapping& Mapping : Setup->Context->GetMappings())
	{
		if (Mapping.Action == Action)
		{
			Labels.AddUnique(KeyLabel(Mapping.Key).ToString());
		}
	}
	return FText::FromString(FString::Join(Labels, TEXT(" / ")));
}

FRbKeyHintContext FRbKeyHintsModel::MakeContext(const APlayerController* Controller)
{
	FRbKeyHintContext C;
	const URbGameUserSettings* Settings = URbGameUserSettings::Get();
	C.bShowKeyHints = !Settings || Settings->bShowKeyHints;
	if (!Controller)
	{
		return C;
	}
	if (const URbUiSubsystem* Ui = URbUiSubsystem::Get(Controller))
	{
		C.bMenuOpen = Ui->IsMenuOpen();
	}
	const UWorld* World = Controller->GetWorld();
	const URbReplaySubsystem* Replay = World ? World->GetSubsystem<URbReplaySubsystem>() : nullptr;
	if (Replay)
	{
		C.bReplaying = Replay->IsReplaying();
	}
	if (const ARbPlayerController* PC = Cast<ARbPlayerController>(Controller))
	{
		if (const URbInputSetup* Setup = PC->GetInputSetup())
		{
			const TPair<FName, const UInputAction*> Actions[] = {
				{TEXT("GetDown"), Setup->GetDown}, {TEXT("Stroke"), Setup->Stroke}, {TEXT("Commit"), Setup->Commit}, {TEXT("FineAim"), Setup->FineAim},
				{TEXT("Settle"), Setup->Settle}, {TEXT("Elevation"), Setup->Elevation}, {TEXT("TipOffset"), Setup->TipOffset},
				{TEXT("Confirm"), Setup->Confirm}, {TEXT("CycleOption"), Setup->CycleOption}, {TEXT("Replay"), Setup->Replay}};
			for (const TPair<FName, const UInputAction*>& Action : Actions)
			{
				const FText Keys = KeysFor(Setup, Action.Value);
				if (!Keys.IsEmpty())
				{
					C.Keys.Add(Action.Key, Keys);
				}
			}
		}
	}
	if (const URbMatchDirector* Director = URbUiSubsystem::FindPlayerDirector(Controller))
	{
		C.bHasDirector = true;
		C.DirectorPhase = Director->GetPhase();
		C.bReplayAllowed = Director->IsReplayAllowed() && Replay && Replay->GetShotCount() > 0; // something to replay
	}
	APawn* Pawn = Controller->GetPawn();
	if (const ARbPlayerCharacter* Character = Cast<ARbPlayerCharacter>(Pawn))
	{
		if (const URbStrokeComponent* Stroke = Character->GetStroke())
		{
			C.bHasStroke = true;
			C.StrokePhase = Stroke->GetPhase();
		}
	}
	if (const URbBallInHandComponent* Hand = Pawn ? Pawn->FindComponentByClass<URbBallInHandComponent>() : nullptr)
	{
		C.bHasBallInHand = true;
		C.BallInHand = Hand->GetState();
	}
	if (Pawn && World)
	{
		if (const URbInteractionSubsystem* Interaction = World->GetSubsystem<URbInteractionSubsystem>())
		{
			FVector Eye;
			FRotator View;
			Controller->GetPlayerViewPoint(Eye, View);
			FRbInteractionQuery Query;
			Query.Pawn = Pawn;
			Query.Eye = Eye;
			Query.Direction = View.Vector();
			FText Verb;
			if (Interaction->FindInteraction(Query, Verb))
			{
				C.InteractionVerb = Verb;
			}
		}
	}
	return C;
}

FRbKeyHintsModel FRbKeyHintsModel::BuildFromContext(const FRbKeyHintContext& C)
{
	using namespace RbKeyHintsPrivate;
	FRbKeyHintsModel M;
	if (!C.bShowKeyHints || C.bMenuOpen || C.bReplaying)
	{
		return M;
	}
	const FText Confirm = C.KeyFor(TEXT("Confirm"));
	// Rules obligations of the player's match first (they wait for an input).
	if (C.bHasDirector)
	{
		switch (C.DirectorPhase)
		{
		case ERbDirectorPhase::AwaitDecision:
			Add(M, C.KeyFor(TEXT("CycleOption")), LOCTEXT("Choose", "Choose"));
			Add(M, Confirm, LOCTEXT("ConfirmChoice", "Confirm"));
			return M;
		case ERbDirectorPhase::RackOver:
			Add(M, Confirm, LOCTEXT("NextRack", "Next rack"));
			return M;
		case ERbDirectorPhase::MatchOver:
			Add(M, Confirm, LOCTEXT("NewMatch", "New match"));
			return M;
		default:
			break;
		}
	}
	if (!C.InteractionVerb.IsEmpty())
	{
		Add(M, LastKey(Confirm), C.InteractionVerb);
	}
	if (!C.bHasStroke)
	{
		return M;
	}
	switch (C.StrokePhase)
	{
	case ERbStrokePhase::Walking:
		if (!C.bHasDirector || C.DirectorPhase == ERbDirectorPhase::AwaitStroke || C.DirectorPhase == ERbDirectorPhase::Lag)
		{
			Add(M, C.KeyFor(TEXT("GetDown")), LOCTEXT("GetDown", "Get down"));
		}
		if (C.bReplayAllowed)
		{
			Add(M, C.KeyFor(TEXT("Replay")), LOCTEXT("Replay", "Replay"));
		}
		break;
	case ERbStrokePhase::GettingDown:
	case ERbStrokePhase::Down:
		Add(M, C.KeyFor(TEXT("Stroke")), LOCTEXT("Stroke", "Stroke"));
		Add(M, C.KeyFor(TEXT("Commit")), LOCTEXT("Commit", "Commit"));
		Add(M, C.KeyFor(TEXT("FineAim")), LOCTEXT("FineAim", "Fine aim"));
		Add(M, C.KeyFor(TEXT("Settle")), LOCTEXT("Settle", "Settle"));
		Add(M, C.KeyFor(TEXT("Elevation")), LOCTEXT("Elevation", "Elevation"));
		Add(M, C.KeyFor(TEXT("TipOffset")), LOCTEXT("English", "English"));
		Add(M, C.KeyFor(TEXT("GetDown")), LOCTEXT("StandUp", "Stand up"));
		break;
	case ERbStrokePhase::Watching:
		Add(M, C.KeyFor(TEXT("GetDown")), LOCTEXT("StandUp", "Stand up"));
		break;
	case ERbStrokePhase::PlacingCueBall:
	{
		// LMB is routed to Confirm while placing (R-17), so the stroke button places too.
		const FText PlaceKeys = FText::Format(LOCTEXT("PlaceKeys", "{0} / {1}"), C.KeyFor(TEXT("Stroke")), LastKey(Confirm));
		if (!C.bHasBallInHand || C.BallInHand == ERbBallInHandState::Carrying || C.BallInHand == ERbBallInHandState::Inactive)
		{
			Add(M, PlaceKeys, LOCTEXT("Place", "Place the cue ball"));
			if (C.bHasBallInHand)
			{
				Add(M, C.KeyFor(TEXT("FineAim")), LOCTEXT("Fine", "Fine"));
			}
		}
		else if (C.BallInHand == ERbBallInHandState::Refused)
		{
			M.Note = LOCTEXT("NotHere", "Not here – move the ball");
		}
		break;
	}
	case ERbStrokePhase::Locked:
	case ERbStrokePhase::Contact:
		break;
	}
	return M;
}

FRbKeyHintsModel FRbKeyHintsModel::Build(const APlayerController* Controller)
{
	return BuildFromContext(MakeContext(Controller));
}

void SRbKeyHints::Construct(const FArguments& /*InArgs*/)
{
	using namespace RbUi;
	SetVisibility(EVisibility::HitTestInvisible);
	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Bottom)
	.Padding(FMargin(LiveSafeX, 0.0f, LiveSafeX, LiveSafeY))
	[
		SAssignNew(Plate, SBorder)
		.BorderImage(RoundedBrush())
		.BorderBackgroundColor(Color(EColor::Ink900, ScrimCard))
		.Padding(FMargin(14.0f, 9.0f, 16.0f, 9.0f))
		[
			SAssignNew(Box, SWrapBox)
			.PreferredSize(760.0f)
			.InnerSlotPadding(FVector2D(22.0, 8.0))
		]
	];
	Plate->SetVisibility(EVisibility::Collapsed);
}

void SRbKeyHints::SetModel(const FRbKeyHintsModel& InModel)
{
	using namespace RbUi;
	FString Key = InModel.ToDebugString();
	Model = InModel;
	if (Key == ModelKey)
	{
		return;
	}
	ModelKey = MoveTemp(Key);
	Box->ClearChildren();
	for (const FRbKeyHint& Hint : Model.Hints)
	{
		Box->AddSlot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SRbKeycap).Key(Hint.Key)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(9.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(Hint.Verb)
				.Font(Font(EFont::Label))
				.ColorAndOpacity(Color(EColor::Chalk100))
			]
		];
	}
	if (!Model.Note.IsEmpty())
	{
		Box->AddSlot()
		[
			SNew(STextBlock)
			.Text(Model.Note)
			.Font(Font(EFont::Label))
			.ColorAndOpacity(Color(EColor::Chalk100))
		];
	}
	Plate->SetVisibility(Model.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}

#undef LOCTEXT_NAMESPACE
