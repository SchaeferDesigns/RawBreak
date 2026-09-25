#pragma once

// Debug assertions for the core (physics-collisions pitfall 6: "assert in debug builds").
// Owner: WP-0 (architecture, frozen). Header-only.
//
// Never use Unreal's check()/ensure()/verify() in rb/ code (the core must compile without Unreal and
// those names are macros there). RB_ASSERT is active only when RB_DEBUG_ASSERTS is defined to 1
// (the CMake Debug configuration does this); otherwise it compiles to nothing and the expression is
// NOT evaluated, so it must never have side effects. A failed assertion stops the program at once
// (debug break / trap): the core has no I/O and no exceptions.

#include "rb/Config.h"

#if defined(RB_DEBUG_ASSERTS) && RB_DEBUG_ASSERTS
	#if defined(_MSC_VER)
		#define RB_DEBUG_BREAK() __debugbreak()
	#else
		#define RB_DEBUG_BREAK() __builtin_trap()
	#endif
	#define RB_ASSERT(Expr)                                                                       \
		do                                                                                        \
		{                                                                                         \
			if (!(Expr))                                                                          \
			{                                                                                     \
				RB_DEBUG_BREAK();                                                                 \
			}                                                                                     \
		} while (0)
#else
	#define RB_ASSERT(Expr) ((void)0)
#endif
