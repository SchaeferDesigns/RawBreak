#pragma once

// The six AI opponents a match can be played against (Docs/ue-architecture.md 19.5; human-factors 5.5; venue-dive-bar 1.2 for the
// dive bar's regulars). Data only: M3-G's venue menu displays them, M3-O's game mode spawns the chosen one (the character seed fixes
// its constant aim bias, rb::human::CharacterAimBias; the appearance goes to the body rig). FROZEN API for M3-G.
// Owner: M3-O. Plan-step stub: the six profiles with their profile names as names (TODO(M3-O): names, descriptions, seeds, looks).

#include "CoreMinimal.h"

#include "Body/RbBodyTypes.h"
#include "Core/RbTypes.h"

struct FRbOpponentInfo
{
	ERbAiProfile Profile = ERbAiProfile::BarRegular;
	FText Name;               // display name ("Big Lou Pruitt")
	FText Description;        // one line for the menu ("Hits hard. Knows the table rolls toward the jukebox.")
	uint64 CharacterSeed = 0; // rb::human::AiCharacter::CharacterSeed (fixed per opponent)
	FRbBodyAppearance Appearance;
};

namespace RbOpponentRoster
{
	// All six, in ERbAiProfile order.
	RAWBREAK_API const TArray<FRbOpponentInfo>& GetAll();
	RAWBREAK_API const FRbOpponentInfo& Get(ERbAiProfile Profile);
}
