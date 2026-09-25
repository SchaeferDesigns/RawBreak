#pragma once

// The physics -> rules contract (rules.md 3.1-3.4): one immutable ShotRecord per shot. The rules
// module derives everything (ShotFacts) from this record and NEVER reads physics internals; the
// record contains only plain data (ids, times, plan positions, enums).
// Producer: the simulator (via rb/Shot/ShotRecordBuilder.h, WP-7). Consumer: rb::rules (WP-8/WP-9).
// Owner: WP-8 (rules facts & evaluation). Changes need sign-off from WP-6a/WP-7 and the architect.

#include "rb/Config.h"
#include "rb/Core/Constants.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Ids.h"
#include "rb/Math/Vec2.h"
#include "rb/Math/Vec3.h"

#include <cstdint>
#include <vector>

namespace rb
{
	enum class BallPresence : std::uint8_t
	{
		NotUsed,   // id not part of this game
		OnTable,
		Pocketed,  // pocketed on an earlier shot and not spotted
		OutOfPlay, // driven off earlier and not spotted (8-ball)
	};

	enum class CueBallInHand : std::uint8_t
	{
		No,
		Anywhere,
		AboveHeadString, // strictly above (kitchen)
		Baulk,           // Blackball
	};

	enum class ExternalObject : std::uint8_t
	{
		Lamp,
		Player,
		Floor,
		Furniture,
		Chalk,
		Template,
	};

	enum class OffTableReason : std::uint8_t
	{
		Floor,                // center crossed the outer rail boundary
		RestsOnRailOrFrame,   // came to rest on the rail cap / frame
		ExternalObjectRebound,// hit an outside object (lamp, ...) - off even if it came back (R 2.6)
	};

	enum class RecordEventType : std::uint8_t
	{
		BallBall,               // A < B; CutAngle when one ball is the cue ball; Normal from A to B
		BallCushion,            // A; Feature = CushionId; ContinuesInitialFreeze
		BallJaw,                // A; Feature = PocketId; Side = JawSide; ContinuesInitialFreeze
		BallRailTop,            // A; Feature = CushionId of the rail (0xFF for a pocket surround; Side = PocketId then)
		BallLiner,              // A; Feature = PocketId (liner / back wall / rim-from-inside contact: pocket interior counts as a rail contact)
		BallPocketEnter,        // A; Feature = PocketId (drop edge crossed)
		BallPocketExit,         // A; Feature = PocketId (rattled back onto the playing surface)
		BallPocketed,           // A; Feature = PocketId (final)
		BallTouchesPocketedBall,// A touches pocketed ball B (Level B / pocket fill only)
		SupportedOverPocket,    // A; Feature = PocketId; Value = supporter bit mask (as double) - Level B only
		BallAirborne,           // A left the cloth; ZMax = apex center height of the flight [m]
		BallLand,               // A back on the cloth; ZMax = max center height of the whole airborne sequence [m]
		BallLineCross,          // A; Feature = TableLine; Side = direction (+1 / -1)
		BallExternalContact,    // A; Feature = ExternalObject
		BallOffTable,           // A; Feature = OffTableReason
		MotionTransition,       // A; From -> To
		TipBallBegin,           // tip contact starts on A (the struck ball, or another ball touched by the follow-through);
		                        //   Feature = strike index; B = f: the ball frozen to A at shot start that the stroke goes into
		                        //   (largest positive n_hat . d among A's frozen balls; kNoBall if none); Value = gap A-f [m]
		                        //   (0 if B = kNoBall); OtherContactBefore = A already touched a ball other than f or a rail
		TipBallEnd,             // tip contact ends on A; same fields as TipBallBegin, evaluated at the end (F7/F8 envelope)
		BallJumpedOver,         // A passed over B while airborne without touching it (F9 JumpedOver, simulator observer)
	};

	struct RecordEvent
	{
		double Time = 0.0;              // [s] since the first tip contact
		std::uint32_t Sequence = 0;     // stable tie order (sort key (Time, Sequence))
		RecordEventType Type = RecordEventType::BallBall;
		BallId A = kNoBall;
		BallId B = kNoBall;
		std::uint8_t Feature = 0xFF;    // see RecordEventType
		std::int8_t Side = 0;           // BallJaw: JawSide; BallLineCross: direction
		bool ContinuesInitialFreeze = false; // rules.md 3.3
		bool OtherContactBefore = false;// TipBallBegin / TipBallEnd only
		MotionState From = MotionState::Stationary;
		MotionState To = MotionState::Stationary;
		Vec2 PositionA;                 // plan center of A at the event [m]
		Vec2 PositionB;                 // plan center of B at the event [m] (ball-ball)
		Vec3 Normal;                    // contact normal (ball-ball: A -> B)
		double CutAngle = 0.0;          // [rad], BallBall with the cue ball; 0 = full hit
		double ZMax = 0.0;              // [m] BallAirborne / BallLand
		double Value = 0.0;             // event-specific scalar (see RecordEventType)
	};

	// rules.md 3.1 (taken at t = 0-).
	struct ShotStartSnapshot
	{
		BallPresence Presence[kMaxBalls] = {};
		Vec2 Position[kMaxBalls];              // plan centers [m]
		double Radius[kMaxBalls] = {};         // per-ball radius R_b [m] (spotting, placement, lag metric, rack interference)
		MotionState State[kMaxBalls] = {};
		bool AllBallsAtRest = true;            // false => foul R 3.9
		CueBallInHand InHand = CueBallInHand::No;
		Vec2 PlacedPosition;                   // where the in-hand cue ball was placed
		bool PlacementOverPocket = false;      // placed center over a pocket opening (F11)
		std::uint32_t FrozenToRail[kMaxBalls] = {}; // bit k = rail feature k (Core/Ids.h), gap <= eps_frozen
		std::uint32_t FrozenToCueBall = 0;     // bit b = object ball b frozen to the cue ball
		bool TemplatePresent = false;
		double ShotClockElapsed = 0.0;         // [s] from "all balls at rest" to tip contact
		bool FootOnFloor = true;
	};

	struct TipContact
	{
		BallId Ball = kNoBall; // ball the tip touched (struck ball: double hit / push; any other ball -> NonTipContact CueTip)
		int Strike = 0;        // index into the strikes of this simulation (the lag has two cues)
		double Start = 0.0;    // [s]
		double End = 0.0;      // [s]
	};

	enum class NonTipSource : std::uint8_t
	{
		Shaft,
		Ferrule,
		Butt,
		BridgeHand,
		MechanicalBridge,
		Body,
		Clothing,
		Hair,
		Chalk,
		PlacingHand,
		CueBallInHandTouch,
		CueTip,             // the follow-through tip touched a ball other than the one it struck (from the simulation)
		Other,
	};

	// Contact of player-controlled equipment with a ball other than the tip->cue ball stroke (R 3.6).
	struct NonTipContact
	{
		BallId Ball = kNoBall;
		NonTipSource Source = NonTipSource::Other;
		double Time = 0.0; // [s], may be negative (before the stroke)
	};

	inline constexpr int kMaxTipContacts = 16;
	inline constexpr int kMaxNonTipContacts = 16;

	// One cue stroke (normal shot: exactly one, Ball = the cue ball; lag: two, one per lag ball).
	struct StrokeInfo
	{
		BallId Ball = kNoBall;        // struck ball
		bool TipClothContact = false; // scoop candidate (from the UE cue-body model, CueStrikeInput::TipTouchesCloth)
		bool Miscue = false;
		double CueElevation = 0.0;    // [rad]
	};

	// rules.md 3.2. Built from the record events TipBallBegin / TipBallEnd (never from the physics log,
	// which may overflow or be switched off for AI rollouts).
	struct StrokeRecord
	{
		FixedVector<StrokeInfo, kMaxStrikes> Strokes;
		FixedVector<TipContact, kMaxTipContacts> TipContacts; // merged intervals per (strike, ball), sorted by (Start, Ball)
		FixedVector<NonTipContact, kMaxNonTipContacts> NonTipContacts; // game-layer contacts + CueTip contacts from the simulation
		bool Overflow = false;        // a list was full (the shot record is then Truncated)
	};

	// Game-layer facts the physics cannot know; passed into the simulator with the shot.
	struct ShotContext
	{
		CueBallInHand InHand = CueBallInHand::No;
		Vec2 PlacedPosition;
		bool TemplatePresent = false;
		double ShotClockElapsed = 0.0;
		bool FootOnFloor = true;
		FixedVector<NonTipContact, kMaxNonTipContacts> NonTipContacts;
		double FrozenTolerance = 1.0e-4; // eps_frozen [m] used for the start snapshot (RulesTolerances::Frozen)
	};

	enum class BallEndStatus : std::uint8_t
	{
		NotUsed,
		OnTable,
		Pocketed,
		OffTable,
	};

	struct BallEnd
	{
		BallEndStatus Status = BallEndStatus::NotUsed;
		Vec2 Position;                  // final plan center (OnTable) [m]
		PocketId Pocket = PocketId::None;
		std::uint32_t FrozenToRail = 0; // rail features touched at rest (gap <= eps_frozen)
		std::uint32_t FrozenToBalls = 0;// balls touched at rest
	};

	struct SupportedBall
	{
		BallId Ball = kNoBall;
		PocketId Pocket = PocketId::None;
		std::uint32_t Supporters = 0;
	};

	// rules.md 3.4.
	struct ShotEndSnapshot
	{
		double StopTime = 0.0;          // tStop: last ball stationary (spin included) or terminal [s]
		BallEnd Balls[kMaxBalls];
		FixedVector<SupportedBall, 4> Supported;
	};

	struct ShotRecord
	{
		ShotStartSnapshot Start;
		StrokeRecord Stroke;
		std::vector<RecordEvent> Events; // sorted by (Time, Sequence); capacity reserved outside the event loop
		ShotEndSnapshot End;
		bool Truncated = false;          // simulation aborted or the record overflowed: rules should replay the shot
	};
}
