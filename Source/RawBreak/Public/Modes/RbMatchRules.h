#pragma once

// The rules configuration of a match setup (Docs/ue-architecture.md 19.6). URbMatchDirector::StartMatch calls it since the M3 plan
// step (the M2 logic moved here unchanged: practice / hot-seat configurations stay bitwise the M2 ones). M3-G adds the VsAi
// configuration: race-to-N, the call policy (ERbCallPolicy -> rb::rules::CallMode), the preset's break order and spotting. Every rules
// option stays a RulesConfig value (no rules code in the game). Owner: M3-G. Tests: RawBreak.Unit.Modes.* (G1).

#include "CoreMinimal.h"

#include "rb/Rules/Match.h"

struct FRbMatchSetup;

namespace RbMatchRules
{
	// MatchConfig of a setup for a match seed and the table's rules table (Config.Table).
	RAWBREAK_API rb::rules::MatchConfig MakeMatchConfig(const FRbMatchSetup& Setup, uint64 MatchSeed, const rb::rules::RulesTable& Table);
}
