// Owner: WP-12 (AI opponent). A-AI-11: the rb/Ai headers survive the Unreal and Windows macros (Docs/architecture.md section 2,
// rule 1): the same look-alike macros as Tests/Core/Architecture/TestMacroTraps.cpp are defined FIRST, then every rb/Ai header is
// included (they pull in rb/Human, rb/Rules and rb/Physics, which survive the same traps there).

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "rbtest.h"

// --- Unreal (Core) ---------------------------------------------------------------------------------
#define PI (3.1415926535897932f)
#define HALF_PI (1.57079632679f)
#define TWO_PI (6.28318530717f)
#define INV_PI (0.31830988618f)
#define UE_PI (3.1415926535897932f)
#define SMALL_NUMBER (1.e-8f)
#define KINDA_SMALL_NUMBER (1.e-4f)
#define BIG_NUMBER (3.4e+38f)
#define DELTA (0.00001f)
#define INDEX_NONE (-1)
#define TEXT(x) L##x
#define check(expr) ((void)(expr))
#define checkf(expr, ...) ((void)(expr))
#define verify(expr) ((void)(expr))
#define ensure(expr) (!!(expr))
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#define FORCEINLINE inline
#define FORCENOINLINE
#define RESTRICT
#define ABSTRACT
#define CONSTEXPR constexpr
#define UE_LOG(...)

// --- Windows ---------------------------------------------------------------------------------------
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define near
#define far
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define VOID void
#define ERROR 0
#define DELETE (0x00010000L)
#define IGNORE 0
#define ABSOLUTE 1
#define RELATIVE 2
#define TRANSPARENT 1
#define OPAQUE 2
#define TRUE 1
#define FALSE 0
#define interface struct
#define small char
#define hyper long long
#define CALLBACK
#define WINAPI
#define PASCAL
#define CDECL

#include "rb/Ai/Planner.h"
#include "rb/Ai/PlannerProfile.h"
#include "rb/Ai/PositionEval.h"

RB_TEST(ARCH_AI11_AiHeadersSurviveUnrealAndWindowsMacros)
{
	// Reaching this point means every rb/Ai header compiled with the traps above in effect.
	const rb::ai::PlannerProfile P = rb::ai::GetPlannerProfile(rb::human::AiProfileId::TouringPro);
	RB_CHECK(P.NoisySamples == 16);
	RB_CHECK(std::fabs(rb::ai::Erf(0.0)) < 1e-8);
}
