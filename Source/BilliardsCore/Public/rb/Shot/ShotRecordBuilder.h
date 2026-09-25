#pragma once

// Physics -> rules adapter: builds the ShotRecord (rules.md 3.1-3.4) from the simulator input and the
// ShotResult, computes frozen-to-rail / frozen-to-ball sets with eps_frozen, maps ShotEvents to
// RecordEvents, and builds the rules' table description from the geometry.
// Owner: WP-7 (output, playback & tools).
//
// Use by the simulator (so that the rules record stays complete even when the physics log
// ShotResult::Events overflows or is kept small for AI rollouts):
//   BeginShotRecord at t = 0  ->  AppendRecordEvent for every record-relevant ShotEvent (the simulator
//   produces observers and tip-contact events whenever RecordOptions::ShotRecord is set, independent of
//   the logging switches)  ->  FinishShotRecord.
// Standalone (replays, tools): BuildShotRecord = Begin + Append(all of Result.Events) + Finish; it is
// only complete if the log was complete (LogObservers and no EventLogOverflow).

#include "rb/Config.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Shot/ShotRecord.h"

#include <cstdint>

namespace rb
{
	// rules.md 3.1 snapshot at t = 0- (AllBallsAtRest from the ball states; per-ball radii; frozen sets with
	// Context.FrozenTolerance measured against nose lines (R_c contact distance), jaw arcs and balls;
	// PlacementOverPocket with IsOverPocketOpening).
	RB_API void BuildShotStartSnapshot(const SimInput& Input, ShotStartSnapshot& Out);

	// Whether a physics event is part of the rules record (rules.md 3.3), and its conversion.
	RB_API bool IsRecordRelevant(ShotEventType Type);
	RB_API bool ToRecordEvent(const ShotEvent& Event, std::uint32_t Sequence, RecordEvent& Out);

	// Clears Out.Events (keeps capacity), fills Out.Start (BuildShotStartSnapshot) and resets the rest.
	RB_API void BeginShotRecord(const SimInput& Input, ShotRecord& Out);

	// Converts and appends a relevant event with the next sequence number. Never grows the vector
	// beyond its reserved capacity: on overflow sets Out.Truncated and returns false.
	RB_API bool AppendRecordEvent(const ShotEvent& Event, ShotRecord& Out);

	// Stroke: one StrokeInfo per strike (Miscue, elevation, TipClothContact from the strike inputs /
	// results); TipContacts from the RECORD events TipBallBegin / TipBallEnd (per strike and ball,
	// intervals closer than CueSpec::ContactTime merged); tip contacts on a ball other than the strike's
	// struck ball become NonTipContact{Source = CueTip}; NonTipContacts from the context. End snapshot
	// (final status, pocket ids, frozen sets at rest). Truncated if the record or a stroke list overflowed
	// or the simulation was not Ok. BallJumpedOver comes from the simulator observer (record event), not
	// from trajectories.
	RB_API void FinishShotRecord(const SimInput& Input, const ShotResult& Result, ShotRecord& Out);

	// Standalone rebuild from a complete ShotResult::Events log.
	RB_API void BuildShotRecord(const SimInput& Input, const ShotResult& Result, ShotRecord& Out);

	// The rules' view of the table (rules.md 2.1-2.2) from the single geometry source: landmarks, pocket
	// openings (mouth lines = virtual jaw points, capture centers, drop-edge radii) and per-ball radii
	// (Radii[0..Count), the remaining ids get NominalRadius). NominalRadius = object-ball radius used for
	// rack lattices and the 14.1 outline.
	RB_API rules::RulesTable BuildRulesTable(const TableGeometry& Geometry, double NominalRadius, const double* Radii, int Count);
}
