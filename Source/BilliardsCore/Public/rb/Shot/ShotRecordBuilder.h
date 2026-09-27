#pragma once

// Physics -> rules adapter: builds the ShotRecord (rules.md 3.1-3.4) from the simulator input and the
// ShotResult, computes frozen-to-rail / frozen-to-ball sets with eps_frozen, maps ShotEvents to
// RecordEvents, and builds the rules' table description from the geometry.
// Owner: WP-7 (output, playback & tools).
//
// Use by the simulator (so that the rules record stays complete even when the physics log
// ShotResult::Events overflows or is kept small for AI rollouts):
//   BeginShotRecord at t = 0  ->  AppendRecordEvent for every record-relevant ShotEvent (the simulator
//   produces observers, motion transitions and tip-contact events whenever RecordOptions::ShotRecord is set,
//   independent of the logging switches)  ->  FinishShotRecord.
// Contract with the simulator: the event handed to AppendRecordEvent carries the states Pre[0] (ball A) and
// Pre[1] (ball B) whatever RecordOptions::EventStates (that switch only strips them from the copy kept in
// ShotResult::Events): the record's plan positions (PositionA / PositionB, rules F6 and F13) come from them.
// FinishShotRecord reads Result.Strikes, Result.Finals and Result.StopTime, and Result.Status only for the failure
// states InvalidInput / Aborted / HorizonReached (so it may run before the simulator sets Status = Ok).
// Standalone (replays, tools): BuildShotRecord = Begin + Append(all of Result.Events) + Finish; it is
// only complete if the log was complete (EventStates, LogObservers and no EventLogOverflow; otherwise Truncated).
//
// No allocation except BuildShotRecord (which reserves Out.Events for the whole log when needed): everything
// else works inside the reserved capacity and fixed-size lists.

#include "rb/Config.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Shot/ShotRecord.h"

#include <cstdint>

namespace rb
{
	// rules.md 3.1 snapshot at t = 0- and 4.11 (auto-declared frozen balls):
	//  * Presence: OnTable for every in-play ball, NotUsed otherwise (the game's GameState knows whether a missing ball
	//    was pocketed earlier; the rules only test OnTable). Position, State, Radius (in-play balls; 0 otherwise, so the
	//    rules fall back to RulesTable::BallRadius).
	//  * AllBallsAtRest: no in-play ball is moving (IsMoving: Stationary and terminal states are at rest, Spinning moves).
	//  * FrozenToRail (balls on the cloth, gap <= Context.FrozenTolerance): nose lines at the contact distance
	//    R_c = ComputeCushionContact(R, h, Params.Cushion.NoseProfileRadius, PooltoolCompat).HorizontalOffset within the
	//    segment, jaw arcs at r_j + R_c within the exposed angle range, facing plan lines at FacingContactOffset (ball on
	//    the shelf inside the opening) -> the jaw's rail feature. Needs Input.Table (no rail sets without a table).
	//  * FrozenToCueBall: object balls whose 3-D center distance to the cue ball is <= R_0 + R_b + FrozenTolerance.
	//  * InHand, PlacedPosition, TemplatePresent, ShotClockElapsed, FootOnFloor from Input.Context;
	//    PlacementOverPocket = IsOverPocketOpening(PlacedPosition) when the cue ball is in hand.
	RB_API void BuildShotStartSnapshot(const SimInput& Input, ShotStartSnapshot& Out);

	// The frozen-to-rail predicate of the snapshots (one definition for the record and the event loop's
	// BallSlot::InitialFreezeRails): bit k = rail feature k (Core/Ids.h) that a ball of radius Radius resting on the
	// cloth at plan position P touches within Tolerance (nose lines at R_c inside the segment, jaw arcs at r_j + R_c on
	// the exposed arc, facing plan lines at FacingContactOffset inside the opening). Cushion supplies the physics nose
	// profile radius and PooltoolCompat.
	RB_API std::uint32_t FrozenRailFeatures(const TableGeometry& Geometry, const CushionParams& Cushion, const Vec2& P, double Radius, double Tolerance);

	// Whether a physics event is part of the rules record (rules.md 3.3): BallBall, BallCushion, BallJaw, BallRailTop,
	// BallLiner, BallPocketRim (-> BallLiner: the pocket interior is a rail), BallPocketEnter, BallPocketExit,
	// BallPocketed, BallAirborne, BallLand, BallLineCross, BallExternalContact, BallOffTable, MotionTransition,
	// TipContactBegin / End (-> TipBallBegin / End), BallJumpedOver. Not: CueStrike (-> StrokeInfo), TipRecontact
	// (its interval is the TipContact pair), BallSlate, island bookkeeping, ZenoGuard, Diagnostic, TiltRefresh.
	RB_API bool IsRecordRelevant(ShotEventType Type);

	// Conversion of a relevant event (false otherwise): Time, A, B, Feature, Normal, CutAngle, Value copied;
	// PositionA / PositionB = plan positions of Pre[0] / Pre[1] (PositionB only for ball-ball, jumped-over and tip events
	// with a ball B); ContinuesInitialFreeze from the flag (cushion, jaw); BallJaw Side = SubFeature & 0x0F (JawSide);
	// BallLineCross Side = +1 / -1 for SubFeature 0 / 1; BallAirborne / BallLand ZMax = Value; tip events: Feature =
	// strike index, B = f, Value = gap (0 without f), OtherContactBefore = SubFeature == 1. BallRailTop keeps Value =
	// the rail-top polygon index; its Side (= PocketId of a pocket surround, Feature 0xFF) is filled from the geometry by
	// FinishShotRecord.
	RB_API bool ToRecordEvent(const ShotEvent& Event, std::uint32_t Sequence, RecordEvent& Out);

	// Clears Out.Events (keeps capacity), fills Out.Start (BuildShotStartSnapshot) and resets the rest.
	RB_API void BeginShotRecord(const SimInput& Input, ShotRecord& Out);

	// Converts and appends a relevant event with the next sequence number (= its index in Out.Events). Never grows the
	// vector beyond its reserved capacity: on overflow sets Out.Truncated and returns false. False (and no change) for
	// an event that is not record-relevant.
	RB_API bool AppendRecordEvent(const ShotEvent& Event, ShotRecord& Out);

	// Finishes the record after the last event:
	//  * Events stably sorted by (Time, Sequence) (the rules contract; appends are nearly sorted, so this is O(n)).
	//  * BallRailTop of a pocket surround (Feature 0xFF): Side = RailTops[Value].Pocket.
	//  * Stroke: one StrokeInfo per strike (Ball, Miscue of the strike at t = 0, CueElevation, TipClothContact from
	//    CueStrikeInput::TipTouchesCloth). TipContacts from the RECORD events TipBallBegin / TipBallEnd paired per
	//    (strike, ball); intervals of one (strike, ball) closer than that strike's CueSpec::ContactTime are merged (the
	//    merged interval keeps the exact begin / end event times, so the rules find the f-envelope data of both ends);
	//    sorted by (Start, Ball, Strike). A tip contact on a ball other than the strike's struck ball also becomes
	//    NonTipContact{Source = CueTip, Time = Start}, after the context's NonTipContacts.
	//  * End snapshot: StopTime, per in-play ball the final status from Result.Finals (OnTable position, frozen rails and
	//    balls at rest with the start snapshot's predicates; Pocketed pocket; OffTable).
	//  * Truncated if the record or a stroke list overflowed, a tip interval is unpaired, a ball has no final status, or
	//    the simulation failed (Status InvalidInput / Aborted / HorizonReached, IslandBudgetExceeded, RecordOverflow).
	// BallJumpedOver comes from the simulator observer (record event), not from trajectories.
	RB_API void FinishShotRecord(const SimInput& Input, const ShotResult& Result, ShotRecord& Out);

	// Standalone rebuild from a complete ShotResult::Events log (see above).
	RB_API void BuildShotRecord(const SimInput& Input, const ShotResult& Result, ShotRecord& Out);

	// The rules' view of the table (rules.md 2.1-2.2) from the single geometry source: Length, Width and landmarks
	// (Geometry.Landmarks; MakeRulesTable's expressions if the geometry was not built), pocket openings (index =
	// PocketId: virtual jaw points, axis, capture center, drop-edge radius a_d; PocketCount 0 on a pocketless table) and
	// per-ball radii (Radii[0..Count) where positive, the remaining ids get NominalRadius). NominalRadius = object-ball
	// radius used for rack lattices and the 14.1 outline. rules::OverPocketOpening on the result is the same predicate
	// as IsOverPocketOpening on the geometry.
	RB_API rules::RulesTable BuildRulesTable(const TableGeometry& Geometry, double NominalRadius, const double* Radii, int Count);
}
