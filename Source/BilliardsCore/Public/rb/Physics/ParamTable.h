#pragma once

// Reflected access to every scalar field of PhysicsParams by a stable dotted key, e.g. "cloth.mu_s",
// "cushion.mathavan_steps", "numerics.rest_speed". Used by rbsim --param / --dump-input / --in, by
// the Unreal developer console and by calibration tooling (motion spec implementation note 14).
// Enumerations and booleans are exposed as numbers (enum = underlying value, bool = 0 / 1).
// Owner: WP-6a (PhysicsParams owner). Keys are part of the replay / tooling contract: never rename,
// only add (and extend the WP-6a round-trip test).

#include "rb/Config.h"
#include "rb/Physics/Simulator.h"

namespace rb
{
	enum class ParamType : unsigned char
	{
		Real,
		Integer, // value must be integral
		Boolean, // 0 or 1
		Enum,    // integral in [0, EnumMax]
	};

	struct PhysicsParamInfo
	{
		const char* Key = "";
		ParamType Type = ParamType::Real;
		int EnumMax = 0;          // Enum only
		const char* Unit = "";    // SI unit or "1"
	};

	RB_API int PhysicsParamCount();

	// Index in [0, PhysicsParamCount()); an out-of-range index returns an info with an empty key.
	RB_API PhysicsParamInfo PhysicsParamAt(int Index);

	// False for an unknown key.
	RB_API bool GetPhysicsParam(const PhysicsParams& Params, const char* Key, double& Out);

	// False for an unknown key or a value that the field cannot hold (non-integral for Integer /
	// Boolean / Enum, out of range for Boolean / Enum, non-finite). Params is unchanged then. Does not
	// validate physics ranges (ValidatePhysicsParams does).
	RB_API bool SetPhysicsParam(PhysicsParams& Params, const char* Key, double Value);
}
