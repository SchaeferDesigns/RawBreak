#include "UI/SRbInfoOverlay.h"

#include "Widgets/Text/STextBlock.h"

// Owner: UE-7. TODO(UE-7): layout (corner panel, readable at 1080p-4K, subtle background), sections, colour for
// fouls / warnings, colour-blind safe (plan 12.22).

void SRbInfoOverlay::Construct(const FArguments& /*InArgs*/)
{
	ChildSlot
	[
		SNew(STextBlock).Text(this, &SRbInfoOverlay::GetText)
	];
}

void SRbInfoOverlay::SetModel(const FRbOverlayModel& InModel)
{
	Model = InModel;
}

FText SRbInfoOverlay::GetText() const
{
	return Model.Title; // TODO(UE-7)
}
