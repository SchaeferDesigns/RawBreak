#include "Modes/RbMatchRules.h"

#include "Core/RbTypes.h"
#include "Game/RbMatchDirector.h"

#include "rb/Rules/RulesConfig.h"

// Owner: M3-G (Docs/ue-architecture.md 19.6). The plan step moved URbMatchDirector::StartMatch's M2 configuration here unchanged.

namespace RbMatchRules
{
	rb::rules::MatchConfig MakeMatchConfig(const FRbMatchSetup& Setup, uint64 MatchSeed, const rb::rules::RulesTable& Table)
	{
		const bool bPractice = Setup.Mode == ERbMatchMode::Practice;
		const int32 Race = FMath::Max(1, Setup.RaceTo);
		rb::rules::MatchConfig Config;
		Config.Game = RbTypes::ToCore(Setup.Discipline);
		Config.Rules = rb::rules::MakeRulesConfig(RbTypes::RulesPresetFor(Setup.Discipline));
		Config.Rules.Input = rb::rules::InputMode::Assisted; // M1: no body / bridge hand (rules.md 16 item 22, review R-11)
		// Practice and hot-seat are casual play: calls use the casual default ObviousAssist (rules.md 4.5) instead of the ranked
		// Explicit of the WPA presets (8-ball, 10-ball, 14.1). M1 has no call input, and Explicit would refuse every shot after the
		// break that carries no call (CallRequired); explicit calls made through SetCalledShot are still honoured.
		// TODO(M3-G): VsAi with ERbCallPolicy::EveryShot keeps Explicit (19.6, G1).
		if (Config.Rules.Calls == rb::rules::CallMode::Explicit)
		{
			Config.Rules.Calls = rb::rules::CallMode::ObviousAssist;
		}
		Config.RaceTo = bPractice ? TNumericLimits<int32>::Max() : Race; // practice: a won rack racks again
		if (Config.Game == rb::rules::Discipline::StraightPool)
		{
			Config.TargetPoints = bPractice ? TNumericLimits<int32>::Max() : Race; // ?Race= counts points in 14.1
			Config.Rules.TargetPoints = Config.TargetPoints;
		}
		Config.Seed = MatchSeed;
		Config.RackGaps = rb::kRackGapWoodenRack;
		Config.Table = Table;
		return Config;
	}
}
