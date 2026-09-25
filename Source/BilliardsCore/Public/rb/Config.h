#pragma once

// Build configuration shared by the Unreal module and the standalone CMake build.
//
// Rules for all code under rb/ (enforced by the CMake flags /W4 /WX /GR- and no exceptions):
//  - no Unreal headers, no exceptions, no RTTI, double precision everywhere;
//  - everything lives in namespace rb;
//  - never use identifiers that Unreal defines as macros (PI, check, verify, ensure,
//    SMALL_NUMBER, KINDA_SMALL_NUMBER, BIG_NUMBER, DELTA, INDEX_NONE, TEXT, ...).

#if defined(BILLIARDSCORE_API)
	// Compiled as part of the Unreal module: BILLIARDSCORE_API expands to DLLEXPORT/DLLIMPORT,
	// which are defined by the platform header.
	#include "HAL/Platform.h"
	#define RB_API BILLIARDSCORE_API
#else
	#define RB_API
#endif
