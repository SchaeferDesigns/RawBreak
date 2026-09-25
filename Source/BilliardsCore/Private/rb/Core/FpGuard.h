#pragma once

// Floating-point semantics guard for bitwise determinism (Docs/architecture.md section 11; prior-art
// 5.9, OQ-3). MUST be the first include of every core .cpp (the root CMakeLists.txt checks this at
// configure time). It is the second safeguard after the build flags (CMake /fp:precise and
// -ffp-contract=off; Unreal BilliardsCore.Build.cs FPSemantics = Precise): whatever the including
// build uses, the rest of the translation unit is compiled with precise semantics and without FMA
// contraction. Owner: WP-0 (architecture). Private: never include it from a public header.

#if defined(_MSC_VER) && !defined(__clang__)
	#pragma float_control(precise, on)
	#pragma fp_contract(off)
#elif defined(__clang__)
	#pragma float_control(precise, on)
	#pragma clang fp contract(off)
#endif
// GCC has no per-file pragma that is warning-free under -Wall -Werror; it relies on -ffp-contract=off
// and the absence of -ffast-math (root CMakeLists.txt).
