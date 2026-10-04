#include "Ai/RbOpponentRoster.h"

// Owner: M3-O (Docs/ue-architecture.md 19.5). Plan-step stub: profile names only.

namespace RbOpponentRoster
{
	const TArray<FRbOpponentInfo>& GetAll()
	{
		static const TArray<FRbOpponentInfo> Roster = []()
		{
			TArray<FRbOpponentInfo> Out;
			for (int32 Index = 0; Index < static_cast<int32>(ERbAiProfile::Count); ++Index)
			{
				FRbOpponentInfo Info;
				Info.Profile = static_cast<ERbAiProfile>(Index);
				Info.Name = FText::FromString(RbTypes::ToString(Info.Profile)); // TODO(M3-O): the characters' names and descriptions
				Info.CharacterSeed = 0x0BB0000ull + static_cast<uint64>(Index);  // TODO(M3-O): fixed per opponent
				Out.Add(Info);
			}
			return Out;
		}();
		return Roster;
	}

	const FRbOpponentInfo& Get(ERbAiProfile Profile)
	{
		const TArray<FRbOpponentInfo>& Roster = GetAll();
		const int32 Index = FMath::Clamp(static_cast<int32>(Profile), 0, Roster.Num() - 1);
		return Roster[Index];
	}
}
