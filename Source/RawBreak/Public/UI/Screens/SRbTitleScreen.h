#pragma once

// Minimal title / venue select on L_Title (M2; the 3D menu scene "Closing Time" of ui-ux 6 comes later): the RAW BREAK logotype
// over the live scene, a list in the left column (8 % from the left, vertically centred, t.menu; ui-ux 6.3) on a soft ink
// gradient, no panel.
//   Root     Play, Settings, Quit (confirm)
//   Play     The Low Bridge Tavern (dive bar), Test room, Back  - a venue whose level does not exist yet is disabled with the
//            reason ("Not built yet")
//   Venue    Practice, Hot-seat, Back -> the host travels to RbTypes::MapFor(venue) with ?Mode= (ARbTitleGameMode::MakeVenueUrl)
// Clean first seconds (ui-ux 6.3, UX-P1; on the launch's first title only - a return from a venue shows the list at once, the
// host's bSkipIntro): the scene runs alone for 2.5 s, then logotype and list fade in over 0.4 s; any key or click shows them at
// once. Esc: back one level; on the root it asks to quit. Owner: M2-D.

#include "CoreMinimal.h"

#include "UI/Core/SRbScreen.h"

class SVerticalBox;
class STextBlock;

class RAWBREAK_API SRbTitleScreen : public SRbScreen
{
public:
	enum class ELevel : uint8
	{
		Root,
		Play,
		Venue,
	};

	SLATE_BEGIN_ARGS(SRbTitleScreen) {}
		SLATE_ARGUMENT(TSharedPtr<FRbUiHost>, Host)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	ELevel GetLevel() const { return Level; }
	ERbVenue GetSelectedVenue() const { return Venue; }
	// Labels of the current list (tests).
	TArray<FString> GetItemLabels() const;
	bool IsIntroDone() const { return IntroTime >= RbUiIntroEnd(); }
	// Travel line while a venue loads ("Heading to ...").
	bool IsLoading() const { return bLoading; }

	virtual bool HandleBack() override;
	virtual bool HandleKey(const FKey& Key, const FModifierKeysState& Modifiers) override;
	virtual void ActivateFocused() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	// Venue names shown in the list.
	static FText VenueName(ERbVenue InVenue);
	static FText VenueDetail(ERbVenue InVenue);

private:
	struct FItem
	{
		FText Label;
		FText Detail;
		bool bEnabled = true;
		TFunction<void()> Action;
	};

	static float RbUiIntroEnd();
	void ShowLevel(ELevel InLevel, int32 FocusIndex = 0);
	void SkipIntro();
	void AskQuit();
	FText HeaderText() const;
	float ContentOpacity() const;
	void UpdateIntroOpacity();

	ELevel Level = ELevel::Root;
	ERbVenue Venue = ERbVenue::DiveBar;
	TArray<FItem> Items;
	TSharedPtr<SVerticalBox> List;
	TSharedPtr<SWidget> Content;
	TSharedPtr<SWidget> Gradient;
	float IntroTime = 0.0f;
	bool bLoading = false;
	FText LoadingText;
};
