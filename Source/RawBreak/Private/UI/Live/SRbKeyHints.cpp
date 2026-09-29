#include "UI/Live/SRbKeyHints.h"

#include "Widgets/Text/STextBlock.h"

// Owner: M2-D. Stub of the M2 architect step: an empty model and a text block; TODO(M2-D) the sources, layout and fades.

FRbKeyHintsModel FRbKeyHintsModel::Build(const APlayerController* /*Controller*/)
{
	return FRbKeyHintsModel();
}

void SRbKeyHints::Construct(const FArguments& /*InArgs*/)
{
	ChildSlot
	[
		SNew(STextBlock).Text(FText::GetEmpty())
	];
}

void SRbKeyHints::SetModel(const FRbKeyHintsModel& InModel)
{
	Model = InModel;
}
