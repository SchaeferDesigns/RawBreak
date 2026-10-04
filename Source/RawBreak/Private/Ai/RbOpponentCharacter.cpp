#include "Ai/RbOpponentCharacter.h"

#include "Ai/RbAiOpponentComponent.h"
#include "Body/RbBodyRigComponent.h"
#include "Core/RbAssetPaths.h"
#include "Cue/RbCue.h"
#include "Table/RbTable.h"

#include "Components/CapsuleComponent.h"

// Owner: M3-O (Docs/ue-architecture.md 19.5). Plan-step stub.

ARbOpponentCharacter::ARbOpponentCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(25.0f, 88.0f); // the player's capsule (6.1)
	Body = CreateDefaultSubobject<URbBodyRigComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Brain = CreateDefaultSubobject<URbAiOpponentComponent>(TEXT("Brain"));
	Tags.Add(RbAssetPaths::Tag::Opponent);
}

void ARbOpponentCharacter::InitForSession(URbMatchDirector* Director, ARbTable* InTable, int32 RulesPlayer, ERbAiProfile Profile)
{
	Table = InTable;
	if (Body)
	{
		Body->SetView(ERbBodyView::ThirdPerson);
		Body->SetTable(InTable);
	}
	if (Brain)
	{
		Brain->Bind(Director, RulesPlayer, Profile);
	}
	// TODO(M3-O): the roster's appearance and character seed, the opponent's own ARbCue (a house cue), the waiting spot.
}
