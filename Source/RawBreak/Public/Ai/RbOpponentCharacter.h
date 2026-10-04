#pragma once

// The AI opponent at the table (Docs/ue-architecture.md 19.5): a character (capsule r 25 cm, walking 1.4 m/s like the player) with the
// body rig in third person (URbBodyRigComponent, M3-H's API), its own cue (ARbCue, a house cue of the venue) and the brain
// (URbAiOpponentComponent). Spawned by ARbGameMode for a VsAi session at its waiting spot (the RbWaitSpot-tagged target point nearest to
// the table; fallback 1.2 m off the long rail opposite the player start); tagged RbAssetPaths::Tag::Opponent. Owner: M3-O.
// Plan-step stub: the components exist, InitForSession binds the brain (TODO(M3-O): body appearance, cue, navigation).

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "Core/RbTypes.h"

#include "RbOpponentCharacter.generated.h"

class ARbCue;
class ARbTable;
class URbAiOpponentComponent;
class URbBodyRigComponent;
class URbMatchDirector;

UCLASS()
class RAWBREAK_API ARbOpponentCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARbOpponentCharacter();

	// Wires the opponent to its table session: the director, the AI's rules player, the profile (roster: name, look, character seed).
	void InitForSession(URbMatchDirector* Director, ARbTable* Table, int32 RulesPlayer, ERbAiProfile Profile);

	URbBodyRigComponent* GetBody() const { return Body; }
	URbAiOpponentComponent* GetBrain() const { return Brain; }
	ARbCue* GetCue() const { return Cue; }
	ARbTable* GetTable() const { return Table.Get(); }

protected:
	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Ai")
	TObjectPtr<URbBodyRigComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "RawBreak|Ai")
	TObjectPtr<URbAiOpponentComponent> Brain;

	UPROPERTY(Transient)
	TObjectPtr<ARbCue> Cue;

	TWeakObjectPtr<ARbTable> Table;
};
