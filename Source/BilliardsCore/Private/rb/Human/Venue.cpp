#include "rb/Core/FpGuard.h"
// Owner: WP-11 (player model). Spec: human-factors 4.4, 4.5.5, HF-41, HF-B11.
#include "rb/Human/Venue.h"

namespace rb::human
{
	Vec2 SeedTableSlope(std::uint64_t /*VenueSeed*/, int /*TableIndex*/, VenueKind /*Kind*/, bool /*FirstCareerTable*/)
	{
		// TODO(WP-11): magnitude per venue kind, uniform direction, first career table <= 1 mm/m (4.5.5).
		return {};
	}

	double VenueBallCling(VenueKind /*Kind*/)
	{
		// TODO(WP-11): 1.3 dive bar, 1.0 otherwise (HF-41).
		return 1.0;
	}

	TableCondition MakeVenueTableCondition(std::uint64_t /*VenueSeed*/, int /*TableIndex*/, VenueKind /*Kind*/, bool /*FirstCareerTable*/, bool ChalkCling)
	{
		// TODO(WP-11): SeedTableSlope + VenueBallCling.
		TableCondition Condition;
		Condition.ChalkCling = ChalkCling;
		return Condition;
	}

	std::uint64_t VenueBallSetSeed(std::uint64_t VenueSeed, int TableIndex)
	{
		return HashKeys(VenueSeed, kVenueBallSetPurpose, static_cast<std::uint64_t>(static_cast<std::uint32_t>(TableIndex)));
	}

	HouseCue SeedHouseCue(std::uint64_t /*VenueSeed*/, int /*RackSlot*/, std::uint32_t /*Generation*/)
	{
		// TODO(WP-11): mass, length, bow, tip from HashKeys(VenueSeed, kVenueHouseCuePurpose, RackSlot, Generation, field) (4.4).
		HouseCue Cue;
		Cue.Spec = kCueHouse19oz;
		return Cue;
	}
}
