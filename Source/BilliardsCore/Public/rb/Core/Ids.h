#pragma once

// Shared identifiers and enums used by physics, geometry, output and rules alike.
// Owner: WP-0 (architecture, frozen).
//
// Table numbering (plan view, +x toward the foot rail, +y to the LEFT of a player at the head end):
//
//        P5 HeadLeft ---- C4 LeftHead ---- P4 SideLeft ---- C3 LeftFoot ---- P3 FootLeft
//          |                                                                      |
//     C5 Head                                                                  C2 Foot
//          |                                                                      |
//        P0 HeadRight --- C0 RightHead --- P1 SideRight --- C1 RightFoot --- P2 FootRight
//
// Pockets and cushions follow the rules.md 2.3 ids (P0..P5, C0..C5), which are counter-clockwise
// seen from above: cushion Ck runs from pocket Pk to pocket P(k+1 mod 6). equipment.md names map as
// POCKET_HEAD_RIGHT = P0, POCKET_SIDE_RIGHT = P1, POCKET_FOOT_RIGHT = P2, POCKET_FOOT_LEFT = P3,
// POCKET_SIDE_LEFT = P4, POCKET_HEAD_LEFT = P5; RAIL_RIGHT = C0 + C1, RAIL_FOOT = C2,
// RAIL_LEFT = C3 + C4, RAIL_HEAD = C5.

#include "rb/Config.h"

#include <cstdint>

namespace rb
{
	// Stable ball identifier 0..kMaxBalls-1 (0 = cue ball). -1 = none (compact records only).
	using BallId = std::int8_t;
	inline constexpr BallId kNoBall = -1;

	// ---------------------------------------------------------------------------------------------
	// Motion states (physics-motion-and-cue A.1, extended with the in-pocket states of
	// physics-collisions 5.4 and the terminal off-table state of 6.3).
	// ---------------------------------------------------------------------------------------------
	// The four "surface" states live on a HORIZONTAL support plane z = SupportZ: the cloth / slate /
	// shelf (SupportZ = 0) or the flat rail cap (SupportZ = TableSpec::RailTopZ, collisions 6.2); the
	// support height is stored in MotionSegment::SupportZ. Motion on the SLOPED cushion top is never a
	// closed-form segment: it is integrated by a rigid CLI island (Docs/architecture.md 8.8, 8.9).
	enum class MotionState : std::uint8_t
	{
		Stationary = 0, // v = 0, w = 0, resting on a support (z = SupportZ + R)
		Spinning,       // v = 0, w_h = 0, w_z != 0
		Sliding,        // on a support, slip u != 0
		Rolling,        // on a support, u = 0, v != 0
		Airborne,       // z > SupportZ + R or v_z > 0; ends with a landing or a table-feature contact
		PocketPivot,    // rolling over the rounded drop edge (collisions 5.4 PIVOT macro-step)
		PocketFall,     // ballistic inside a pocket (no slate below); ends by capture, liner, rim, exit
		Pocketed,       // captured (z <= -R); terminal
		OffTable,       // left the playing field (floor, at rest on the rail cap, lamp); terminal
	};

	// On a horizontal support (cloth, shelf or flat rail cap).
	constexpr bool IsOnSurface(MotionState S)
	{
		return S == MotionState::Stationary || S == MotionState::Spinning || S == MotionState::Sliding || S == MotionState::Rolling;
	}

	constexpr bool IsTerminal(MotionState S) { return S == MotionState::Pocketed || S == MotionState::OffTable; }

	// "Still moving" in the sense of rules.md 1 / R 2.19: spinning in place counts as moving.
	constexpr bool IsMoving(MotionState S) { return S != MotionState::Stationary && !IsTerminal(S); }

	constexpr bool IsInPocket(MotionState S) { return S == MotionState::PocketPivot || S == MotionState::PocketFall; }

	// ---------------------------------------------------------------------------------------------
	// Pockets, cushions, jaws
	// ---------------------------------------------------------------------------------------------
	enum class PocketId : std::uint8_t
	{
		HeadRight = 0, // P0 (-L/2, -W/2) corner
		SideRight = 1, // P1 (0, -W/2) side
		FootRight = 2, // P2 (+L/2, -W/2) corner
		FootLeft = 3,  // P3 (+L/2, +W/2) corner
		SideLeft = 4,  // P4 (0, +W/2) side
		HeadLeft = 5,  // P5 (-L/2, +W/2) corner
		None = 0xFF,
	};
	inline constexpr int kPocketCount = 6;

	enum class PocketKind : std::uint8_t
	{
		Corner,
		Side,
	};

	// Pocketless tables (TableSpec::HasPockets = false: carom, the 4.5 m square of prior-art ROB-03)
	// keep the same ids: C0 is the whole right rail (y = -W/2), C2 the foot rail, C3 the whole left rail
	// (y = +W/2), C5 the head rail; C1 and C4 are ABSENT (TableGeometry::Noses[1/4].Present == false).
	// Containers indexed by CushionId therefore always have kCushionCount entries.
	enum class CushionId : std::uint8_t
	{
		RightHead = 0, // C0: y = -W/2, P0 -> P1
		RightFoot = 1, // C1: y = -W/2, P1 -> P2
		Foot = 2,      // C2: x = +L/2, P2 -> P3
		LeftFoot = 3,  // C3: y = +W/2, P3 -> P4
		LeftHead = 4,  // C4: y = +W/2, P4 -> P5
		Head = 5,      // C5: x = -L/2, P5 -> P0
		None = 0xFF,
	};
	inline constexpr int kCushionCount = 6;

	// The two jaws of pocket Pk, in counter-clockwise order: Incoming terminates cushion C(k-1 mod 6),
	// Outgoing starts cushion Ck. Each jaw = rounded jaw arc + facing (collisions 5.3).
	enum class JawSide : std::uint8_t
	{
		Incoming = 0,
		Outgoing = 1,
	};

	// "Rail features" (rules.md 2.3: cushions and jaws are rails) numbered for 32-bit masks:
	// 0..5 = cushions C0..C5, 6 + 2 * pocket + side = jaws. Used by FrozenToRail masks and
	// continuesInitialFreeze tracking.
	inline constexpr int kRailFeatureCount = kCushionCount + 2 * kPocketCount;
	constexpr int RailFeatureOfCushion(CushionId C) { return static_cast<int>(C); }
	constexpr int RailFeatureOfJaw(PocketId P, JawSide S) { return kCushionCount + 2 * static_cast<int>(P) + static_cast<int>(S); }

	// ---------------------------------------------------------------------------------------------
	// Lines used by the rules (rules.md 2.2); crossings are reported by the simulator.
	// ---------------------------------------------------------------------------------------------
	enum class TableLine : std::uint8_t
	{
		HeadString = 0,   // x = -L/4
		FootString = 1,   // x = +L/4
		CenterString = 2, // x = 0
		LongString = 3,   // y = 0
		Baulk = 4,        // x = -L/2 + L/5 (Blackball)
	};
	inline constexpr int kTableLineCount = 5;

	// ---------------------------------------------------------------------------------------------
	// Presets
	// ---------------------------------------------------------------------------------------------
	enum class ClothPreset : std::uint8_t
	{
		Default,     // mu_s 0.20, mu_r 0.010, alpha_sp 10 (motion spec A.9)
		WorstedFast, // clean worsted (Simonis 860/760): 0.17 / 0.007 / 8
		NappedBar,   // napped woolen bar cloth: 0.26 / 0.014 / 13
	};

	enum class TablePreset : std::uint8_t
	{
		NineFootPro,   // TABLE_9FT_PRO (WPA 9-ft, pro-cut pockets)
		NineFootTight, // TABLE_9FT_TIGHT (4.25 in corners)
		EightFootPro,  // TABLE_8FT_PRO (WPA 8-ft)
		EightFootHome, // TABLE_8FT_HOME
		SevenFootBar,  // TABLE_7FT_BAR (80 x 40 in coin-op, dive-bar default)
		SevenFoot78,   // TABLE_7FT_78 (pooltool default, cross-validation only)
		SevenFootTrue, // TABLE_7FT_TRUE (76 x 38 in)
		Custom,
	};
}
