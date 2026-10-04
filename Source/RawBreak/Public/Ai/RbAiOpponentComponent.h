#pragma once

// The AI opponent's brain (Docs/ue-architecture.md 19.5): on ARbOpponentCharacter, bound to one table session's director and rules
// player. It builds the rb::ai::PlannerInput from the director, asks URbAiPlannerService, and acts through the director's public API
// exactly like the human (SubmitStroke with a commit from rb::human::SyntheticHand + SampleHand, PlaceCueBall, ChooseOption,
// SetCalledShot / SetShotKind, RequestSpot, ChooseBreaker, ChalkTip), while the body (H's rig) walks, studies the table from the
// planner's Progress(), gets down, strokes, chalks and waits at its spot. Owner: M3-O. Plan-step stub: binds, never acts (TODO(M3-O)).

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Core/RbTypes.h"

#include "RbAiOpponentComponent.generated.h"

class URbMatchDirector;

UENUM(BlueprintType)
enum class ERbAiOpponentState : uint8
{
	Idle,           // not bound / no match
	Waiting,        // at its waiting spot while the player shoots
	WalkingToTable,
	Studying,       // the planner runs; the body walks along the table toward Progress().BestCueBall and looks along the best shot
	Placing,        // ball in hand: carries the cue ball to the planned placement
	GettingDown,
	Stroking,       // the synthetic stroke timeline (practice strokes, pause, the shot at t_c)
	Watching,
	Chalking,
	Deciding,       // an option / the breaker / a spot request
	WalkingBack,
};

UCLASS(ClassGroup = (RawBreak), meta = (BlueprintSpawnableComponent))
class RAWBREAK_API URbAiOpponentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URbAiOpponentComponent();

	// The session's director, the AI's rules player and its profile.
	void Bind(URbMatchDirector* InDirector, int32 InRulesPlayer, ERbAiProfile InProfile);
	void Unbind();

	ERbAiOpponentState GetState() const { return State; }
	ERbAiProfile GetProfile() const { return Profile; }
	int32 GetRulesPlayer() const { return RulesPlayer; }
	URbMatchDirector* GetDirector() const { return Director.Get(); }

	// Think-time scale (?AiThink=): 1 = the profile's human study time, 0 = act as soon as the decision is ready (tests).
	double ThinkTimeScale = 1.0;
	// Wall-clock escape of the planner service [s] (?AiDeadline=, cvar rb.Ai.MaxComputeSeconds); 0 = off (deterministic).
	double MaxComputeSeconds = 5.0;

	// UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	ERbAiOpponentState State = ERbAiOpponentState::Idle;
	ERbAiProfile Profile = ERbAiProfile::BarRegular;
	int32 RulesPlayer = 1;
	TWeakObjectPtr<URbMatchDirector> Director;
};
