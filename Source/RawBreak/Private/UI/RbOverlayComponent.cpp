#include "UI/RbOverlayComponent.h"

#include "Game/RbGameMode.h"
#include "Game/RbMatchDirector.h"
#include "UI/SRbInfoOverlay.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

// Owner: UE-7. TODO(UE-7): add / remove the widget on the game viewport (only for a local player with a viewport;
// nothing under -nullrhi), model from the director (score, shooter, fouls + three-foul warning, ball in hand, called
// ball, decision options, last-shot facts), glance fade, tests (model text for scripted states).

URbOverlayComponent::URbOverlayComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URbOverlayComponent::SetGlanceHeld(bool bHeld)
{
	bGlanceHeld = bHeld;
	UpdateVisibility();
}

void URbOverlayComponent::TogglePinned()
{
	Mode = Mode == ERbOverlayMode::Pinned ? ERbOverlayMode::Hidden : ERbOverlayMode::Pinned;
	UpdateVisibility();
}

void URbOverlayComponent::ToggleDebug()
{
	bDebug = !bDebug;
	Refresh();
}

void URbOverlayComponent::Refresh()
{
	// TODO(UE-7)
}

void URbOverlayComponent::BeginPlay()
{
	Super::BeginPlay();
	// TODO(UE-7): create Widget and subscribe to the director.
}

void URbOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// TODO(UE-7): remove the widget, unsubscribe.
	Super::EndPlay(EndPlayReason);
}

void URbOverlayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// TODO(UE-7): glance fade.
}

URbMatchDirector* URbOverlayComponent::FindDirector() const
{
	const ARbGameMode* GameMode = ARbGameMode::Get(this);
	return GameMode ? GameMode->GetDirector() : nullptr;
}

void URbOverlayComponent::UpdateVisibility()
{
	// TODO(UE-7)
}
