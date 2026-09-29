#pragma once

// Minimal title / venue select on L_Title (M2; the 3D menu scene Closing Time of ui-ux 6 is later): RAW BREAK, Play - Test room / Dive bar (+ Practice / Hot-seat), Settings, Quit. Opens RbTypes::MapFor(venue) with the match options.
// Owner: M2-D (stub by the M2 architect step; TODO(M2-D)).

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SRbTitleScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRbTitleScreen) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
