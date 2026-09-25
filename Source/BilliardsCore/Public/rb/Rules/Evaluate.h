#pragma once

// evaluateShot (rules.md 10): foul detection + enforcement, turn change, ball-in-hand zone, spotting
// list, win/loss, incoming player's options, push-out window, group assignment, 14.1 scoring and
// re-rack planning (calls rb/Rules/TableRules.h PlanRerack14 / PlanRerack15AfterFifteenthPocketed with
// the landmarks and radii of Table), free shots / visits, three-foul counters. Pure functions of
// (config, table, start state, declaration, facts).
// Owner: WP-8 (rules facts & evaluation).
//
// Declaration validation (call present/legal, push-out/safety allowed, placement region) happens
// BEFORE the stroke (rb/Rules/Match.h ValidateDeclaration); the evaluator assumes a valid declaration.

#include "rb/Config.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Rules/ShotFacts.h"

#include <cstdint>

namespace rb::rules
{
	// Dispatches on State.Game: Evaluate8Ball / 9Ball / 10Ball / StraightPool / Blackball (10.3-10.6,
	// 12.4) with the RulesConfig variant flags. Severity order for Enforced: rules.md 4.9.
	RB_API ShotOutcome EvaluateShot(const RulesConfig& Config, const RulesTable& Table, const GameState& State, const ShotDeclaration& Declaration,
		const ShotFacts& Facts);

	// Explicit call, or the ObviousAssist inference of 4.5 (first contact b pocketed in p touching no
	// other ball and no rail contact except p's jaws before it drops, per Facts.Balls[b].RailContacts /
	// BallContacts; the CB touched no rail before b). Needs no landmarks: pocket ids come with the jaw
	// contacts. CallMode::EightOnly infers nothing; None makes every ball count as called.
	RB_API Call ResolveCall(const RulesConfig& Config, const GameState& State, const ShotDeclaration& Declaration, const ShotFacts& Facts);

	// Helpers on the START state.
	RB_API int LowestObjectBallAtStart(const GameState& State);
	RB_API bool GroupCleared(const GameState& State, BallGroup Group);        // no ball of the group OnTable
	RB_API int CountObjectBallsOnTable(const GameState& State);
	RB_API std::uint32_t LegalFirstContactMask(const RulesConfig& Config, const GameState& State, const ShotDeclaration& Declaration);
}
