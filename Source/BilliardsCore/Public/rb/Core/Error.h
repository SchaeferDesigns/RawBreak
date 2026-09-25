#pragma once

// Error codes (the core never throws). Owner: WP-0 (architecture, frozen).

#include "rb/Config.h"

#include <cstdint>

namespace rb
{
	enum class ErrorCode : std::uint8_t
	{
		Ok = 0,
		NotImplemented,         // stub not yet implemented by its work package
		InvalidArgument,        // generic bad input (NaN, null pointer, wrong id, ...)
		InvalidParameter,       // physics/rules parameter outside its physical range (prior-art 5.11, ROB-06)
		BallNotAtRest,          // cue strike requires the struck ball at rest (motion spec B.1)
		CueOffsetTooLarge,      // rho = |(a, b)| >= rho_valid (motion spec B.1, T-B18)
		CueElevationOutOfRange, // theta outside [0, pi/2) (motion spec B.1)
		CueSpeedOutOfRange,     // V < 0, non-finite, or above kMaxCueSpeed
		CapacityExceeded,       // a fixed-capacity container is full
		InvalidTable,           // TableSpec cannot produce a consistent geometry
		InvalidState,           // e.g. overlapping balls in the input, ball outside the table
		InvalidDeclaration,     // rules: call / push-out / safety not allowed (rules.md 16.16)
		InvalidOption,          // rules: option not offered to this player
	};

	constexpr bool Succeeded(ErrorCode Code) { return Code == ErrorCode::Ok; }

	constexpr const char* ToString(ErrorCode Code)
	{
		switch (Code)
		{
		case ErrorCode::Ok: return "Ok";
		case ErrorCode::NotImplemented: return "NotImplemented";
		case ErrorCode::InvalidArgument: return "InvalidArgument";
		case ErrorCode::InvalidParameter: return "InvalidParameter";
		case ErrorCode::BallNotAtRest: return "BallNotAtRest";
		case ErrorCode::CueOffsetTooLarge: return "CueOffsetTooLarge";
		case ErrorCode::CueElevationOutOfRange: return "CueElevationOutOfRange";
		case ErrorCode::CueSpeedOutOfRange: return "CueSpeedOutOfRange";
		case ErrorCode::CapacityExceeded: return "CapacityExceeded";
		case ErrorCode::InvalidTable: return "InvalidTable";
		case ErrorCode::InvalidState: return "InvalidState";
		case ErrorCode::InvalidDeclaration: return "InvalidDeclaration";
		case ErrorCode::InvalidOption: return "InvalidOption";
		}
		return "Unknown";
	}
}
