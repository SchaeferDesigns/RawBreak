#pragma once

// rbsimInput schema v1 (Docs/architecture.md 5.3 and 14): the complete SimInput of one shot as JSON, written by
// rbsim --dump-input and read by rbsim --in, so a shot (a UE bug report, a calibration case) replays bit for bit.
// Not part of BilliardsCore (allocates, uses std::string). Owner: WP-7.
//
// Content: tableSpec (every TableSpec field; the geometry is rebuilt with BuildTableGeometry, which is deterministic),
// environment, params (every ParamTable key -> value; read back over MakePhysicsParams(tableSpec) with SetPhysicsParam),
// balls (id, radius, mass, inertia, state, q, chalkMarks), strikes (ball, V, theta, phi, a, b, lambdaOverride, squirt,
// tipTouchesCloth, the full cue), context (in hand, placed position, template, shot clock, foot on floor, frozen
// tolerance, non-tip contacts), record (every RecordOptions switch). Doubles are written with 17 significant digits
// (non-finite ones as "Infinity" / "-Infinity" / "NaN"), so parsing restores them bitwise.

#include "JsonReader.h"
#include "JsonWriter.h"

#include "rb/Core/Ids.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Physics/Simulator.h"

#include <string>

namespace rbsim
{
	inline constexpr int kSimInputSchemaVersion = 1;

	const char* MotionStateName(rb::MotionState State);
	bool ParseMotionState(const char* Name, rb::MotionState& Out);

	// Writes one rbsimInput document (Input.Table must be set).
	void WriteSimInput(const rb::SimInput& Input, JsonWriter& J);

	// A SimInput read back from the schema together with the storage it points to. Not copyable or movable
	// (Input.Table points at Geometry, Geometry.Spec.Name at TableName): allocate it once and keep it.
	struct LoadedSimInput
	{
		std::string TableName;
		rb::TableGeometry Geometry;
		rb::SimInput Input;
		int MissingParams = 0; // ParamTable keys absent from the dump (they keep MakePhysicsParams' value)

		LoadedSimInput() = default;
		LoadedSimInput(const LoadedSimInput&) = delete;
		LoadedSimInput& operator=(const LoadedSimInput&) = delete;
	};

	// Strict reader: a missing or malformed field (other than a ParamTable key), an unknown parameter key or a table the
	// geometry builder rejects is an error.
	bool ReadSimInput(const JsonValue& Root, LoadedSimInput& Out, std::string& Error);
	bool ParseSimInput(const std::string& Text, LoadedSimInput& Out, std::string& Error);
}
