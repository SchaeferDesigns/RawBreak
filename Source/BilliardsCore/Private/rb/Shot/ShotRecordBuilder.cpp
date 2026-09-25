#include "rb/Core/FpGuard.h"
// Owner: WP-7 (output, playback & tools). Spec: rules.md 3.1-3.4; physics-collisions 8.13.
#include "rb/Shot/ShotRecordBuilder.h"

namespace rb
{
	void BuildShotStartSnapshot(const SimInput& /*Input*/, ShotStartSnapshot& Out)
	{
		// TODO(WP-7): presence/positions/states/radii, AllBallsAtRest, in-hand info, frozen sets with eps_frozen,
		// PlacementOverPocket.
		Out = ShotStartSnapshot{};
	}

	bool IsRecordRelevant(ShotEventType /*Type*/)
	{
		// TODO(WP-7): rules.md 3.3 event subset.
		return false;
	}

	bool ToRecordEvent(const ShotEvent& /*Event*/, std::uint32_t /*Sequence*/, RecordEvent& /*Out*/)
	{
		// TODO(WP-7): ShotEvent -> RecordEvent mapping.
		return false;
	}

	void BeginShotRecord(const SimInput& Input, ShotRecord& Out)
	{
		// TODO(WP-7): verify the snapshot fields once BuildShotStartSnapshot exists.
		Out.Events.clear();
		BuildShotStartSnapshot(Input, Out.Start);
		Out.Stroke = StrokeRecord{};
		Out.End = ShotEndSnapshot{};
		Out.Truncated = false;
	}

	bool AppendRecordEvent(const ShotEvent& /*Event*/, ShotRecord& /*Out*/)
	{
		// TODO(WP-7): IsRecordRelevant + ToRecordEvent + capacity check (no reallocation).
		return false;
	}

	void FinishShotRecord(const SimInput& /*Input*/, const ShotResult& /*Result*/, ShotRecord& /*Out*/)
	{
		// TODO(WP-7): StrokeInfo per strike, TipContacts from TipBallBegin/End record events (merged per strike and ball),
		// CueTip NonTipContacts, end snapshot, Truncated.
	}

	void BuildShotRecord(const SimInput& Input, const ShotResult& Result, ShotRecord& Out)
	{
		BeginShotRecord(Input, Out);
		for (const ShotEvent& Event : Result.Events)
		{
			AppendRecordEvent(Event, Out);
		}
		FinishShotRecord(Input, Result, Out);
	}

	rules::RulesTable BuildRulesTable(const TableGeometry& Geometry, double NominalRadius, const double* /*Radii*/, int /*Count*/)
	{
		// TODO(WP-7): landmarks from Geometry.Landmarks, pocket openings from Geometry.Pockets, per-ball radii.
		return rules::MakeRulesTable(Geometry.Spec.Length, Geometry.Spec.Width, NominalRadius);
	}
}
