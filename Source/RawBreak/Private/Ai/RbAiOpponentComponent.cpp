#include "Ai/RbAiOpponentComponent.h"

#include "Game/RbMatchDirector.h"

// Owner: M3-O (Docs/ue-architecture.md 19.5). Plan-step stub: binds and idles.

URbAiOpponentComponent::URbAiOpponentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // TODO(M3-O): tick while bound
}

void URbAiOpponentComponent::Bind(URbMatchDirector* InDirector, int32 InRulesPlayer, ERbAiProfile InProfile)
{
	Director = InDirector;
	RulesPlayer = InRulesPlayer;
	Profile = InProfile;
	State = InDirector ? ERbAiOpponentState::Waiting : ERbAiOpponentState::Idle;
	// TODO(M3-O): listen to the director (turns, declarations), drive the planner service and the body.
}

void URbAiOpponentComponent::Unbind()
{
	Director.Reset();
	State = ERbAiOpponentState::Idle;
}

void URbAiOpponentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// TODO(M3-O): the opponent's state machine (19.5).
}

void URbAiOpponentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Unbind();
	Super::EndPlay(EndPlayReason);
}
