#pragma once

// Game-side state that persists between shots (architecture.md 13 item 11, human-factors 4.7): the authoritative
// ball state of the table in CORE units, and per shooter the player-model state (attributes, tip, cue body, noise
// history). Plain data, owned by URbMatchDirector; replays copy it into FRbShotRequest. Owner: UE-6b.

#include "CoreMinimal.h"

#include "rb/Core/Constants.h"
#include "rb/Equipment/Cue.h"
#include "rb/Human/CueState.h"
#include "rb/Human/NoiseHash.h"
#include "rb/Human/Skill.h"
#include "rb/Human/TipState.h"
#include "rb/Physics/Simulator.h"

// The balls as they lie on the table between shots (index = ball id), the PHYSICS mirror of the rules' state: after
// ApplyShot the director rebuilds it with ONE function from MatchState.Game.Balls (which balls are OnTable and their plan
// position - spotted balls included, the rules' GameState is authoritative for status and position) and Result.Finals
// (orientation, ChalkMarks, z = R); balls not OnTable get InPlay = false; the cue ball in hand stays out until placed
// (review R-12). ChalkMarks carry over (HF 4.3).
// Every entry carries the table context's BallSpec of its id (also balls out of play), balls in play lie at rest on the
// cloth: State = {(x, y, R), 0, 0, Stationary}.
struct FRbTableState
{
	rb::SimBall Balls[rb::kMaxBalls];
};

struct FRbShooterState
{
	FString Name;                                 // "Player 1" / "Player 2" (hot-seat)
	rb::human::ShooterAttributes Attributes;      // hot-seat guests: HotSeatGuestAttributes (50 each, HF Q4)
	rb::human::ShooterHabits Habits;
	rb::human::TipState Tip;
	rb::human::ChalkCube Cube;                    // M1: auto-chalk between visits (Chores.h PerformChalking), HF-22
	rb::human::CueBodyState CueBody;
	rb::CueSpec Cue = rb::kCuePlaying19oz;
	rb::human::NoiseHistory History;              // cache of the streak history (rebuildable from the key)
	uint32 ShooterId = 0;                         // NoiseKey::ShooterId
	uint32 ShooterShotIndex = 0;                  // revealed per-shot draws so far (NoiseKey::ShooterShotIndex)
	uint32 CuePickupIndex = 0;
	int32 LastChalkTwists = 0;                    // twists of the last auto-chalk (F2 debug block)
};
