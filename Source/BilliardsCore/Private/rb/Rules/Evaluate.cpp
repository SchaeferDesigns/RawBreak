#include "rb/Core/FpGuard.h"
// Owner: WP-8 (rules facts & evaluation). Spec: rules.md 4.5, 4.9, 6-10, 12.
#include "rb/Rules/Evaluate.h"

namespace rb::rules
{
	ShotOutcome EvaluateShot(const RulesConfig& /*Config*/, const RulesTable& /*Table*/, const GameState& State, const ShotDeclaration& /*Declaration*/,
		const ShotFacts& /*Facts*/)
	{
		// TODO(WP-8): Evaluate8Ball / 9Ball / 10Ball / StraightPool (re-rack plans via TableRules.h with Table) / Blackball
		// (10.2-10.6, 12.4) with variant flags, free shots and visits.
		ShotOutcome Outcome;
		Outcome.Next = NextAction::Pass;
		Outcome.NextShooter = 1 - State.Shooter;
		Outcome.FoulsAfter[0] = State.Players[0].ConsecutiveFouls;
		Outcome.FoulsAfter[1] = State.Players[1].ConsecutiveFouls;
		Outcome.RuleRef = "TODO(WP-8)";
		return Outcome;
	}

	Call ResolveCall(const RulesConfig& /*Config*/, const GameState& /*State*/, const ShotDeclaration& Declaration, const ShotFacts& /*Facts*/)
	{
		// TODO(WP-8): Explicit / ObviousAssist inference (4.5) from BallContacts and RailContacts.
		return Declaration.Called;
	}

	int LowestObjectBallAtStart(const GameState& /*State*/)
	{
		// TODO(WP-8): lowest OnTable object ball at shot start.
		return -1;
	}

	bool GroupCleared(const GameState& /*State*/, BallGroup /*Group*/)
	{
		// TODO(WP-8): no ball of the group OnTable at shot start.
		return false;
	}

	int CountObjectBallsOnTable(const GameState& /*State*/)
	{
		// TODO(WP-8): object balls OnTable at shot start.
		return 0;
	}

	std::uint32_t LegalFirstContactMask(const RulesConfig& /*Config*/, const GameState& /*State*/, const ShotDeclaration& /*Declaration*/)
	{
		// TODO(WP-8): per-discipline legal-first-contact predicate as a bit mask (10.3-10.5).
		return 0u;
	}
}
