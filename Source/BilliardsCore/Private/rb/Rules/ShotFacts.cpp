#include "rb/Core/FpGuard.h"
// Owner: WP-8 (rules facts & evaluation). Spec: rules.md 3.5, 3.6.
#include "rb/Rules/ShotFacts.h"

namespace rb::rules
{
	void DeriveShotFacts(const ShotRecord& Record, const RulesTable& /*Table*/, const RulesTolerances& /*Tolerances*/, double /*ShotClockLimit*/,
		ShotFacts& Out)
	{
		// TODO(WP-8): one pass over Record.Events computing F1-F13 with the tie windows of 3.6.
		Out = ShotFacts{};
		Out.RecordTruncated = Record.Truncated;
	}

	int ResolveFirstContact(const ShotFacts& Facts, std::uint32_t /*LegalMask*/)
	{
		// TODO(WP-8): F1 tie rule (legal ball in the tie set wins, lowest id).
		return Facts.EarliestContact;
	}
}
