// Owner: WP-0 (architecture). Macro-collision safety net (Docs/architecture.md section 2, rule 1).
//
// The standalone build never sees the macros that Unreal (Core: HAL/Platform.h, Math/UnrealMathUtility.h,
// Misc/AssertionMacros.h) and Windows (windef.h, winnt.h, wingdi.h, rpcndr.h, combaseapi.h) define. This
// translation unit defines look-alikes of all of them FIRST and then includes every public core header,
// so any identifier in rb/ that collides with one of those macros breaks the standalone build at once.
// Standard headers are included before the traps (as in a real Unreal translation unit, where the
// standard library is already guarded); only rb/ code is tested.
//
// Deliberately NOT trapped: the X11 macros None and Status. Unreal itself uses None / Status as
// enumerator and member names everywhere, so no Unreal translation unit can have them defined; the
// core uses them the same way (PocketId::None, ShotResult::Status). See architecture.md "Review resolution".

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
#define DOUBLE_PI (3.141592653589793238462643383279502884197169399)
#define UE_PI (3.1415926535897932f)
#define SMALL_NUMBER (1.e-8f)
#define KINDA_SMALL_NUMBER (1.e-4f)
#define BIG_NUMBER (3.4e+38f)
#define UE_SMALL_NUMBER (1.e-8f)
#define UE_KINDA_SMALL_NUMBER (1.e-4f)
#define UE_BIG_NUMBER (3.4e+38f)
#define EULERS_NUMBER (2.71828182845904523536f)
#define DELTA (0.00001f)
#define UE_DELTA (0.00001f)
#define THRESH_POINT_ON_PLANE (0.10f)
#define FLOAT_NORMAL_THRESH (0.0001f)
#define INDEX_NONE (-1)
#define TEXT(x) L##x
#define check(expr) ((void)(expr))
#define checkf(expr, ...) ((void)(expr))
#define checkSlow(expr) ((void)(expr))
#define checkNoEntry() ((void)0)
#define verify(expr) ((void)(expr))
#define verifyf(expr, ...) ((void)(expr))
#define ensure(expr) (!!(expr))
#define ensureMsgf(expr, ...) (!!(expr))
#define ensureAlways(expr) (!!(expr))
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#define FORCEINLINE inline
#define FORCENOINLINE
#define RESTRICT
#define ABSTRACT
#define CONSTEXPR constexpr
#define LINE_TERMINATOR TEXT("\n")
#define MAX_uint8 (0xff)
#define MAX_int32 (0x7fffffff)
#define MIN_int32 (-0x7fffffff - 1)
#define MAX_flt (3.402823466e+38F)
#define MAX_dbl (1.7976931348623158e+308)
#define DECLARE_LOG_CATEGORY_EXTERN(...)
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

#include "rb/Config.h"
#include "rb/Core/Assert.h"
#include "rb/Core/Constants.h"
#include "rb/Core/Error.h"
#include "rb/Core/FixedVector.h"
#include "rb/Core/Ids.h"
#include "rb/Core/Random.h"
#include "rb/Core/Tolerances.h"
#include "rb/Equipment/BallSets.h"
#include "rb/Equipment/Cue.h"
#include "rb/Equipment/EquipmentConstants.h"
#include "rb/Equipment/TableSpec.h"
#include "rb/Geometry/RackLayout.h"
#include "rb/Geometry/TableGeometry.h"
#include "rb/Math/Aabb.h"
#include "rb/Math/Polynomial.h"
#include "rb/Math/Quat.h"
#include "rb/Math/Scalar.h"
#include "rb/Math/Vec2.h"
#include "rb/Math/Vec3.h"
#include "rb/Physics/BallBall.h"
#include "rb/Physics/BallState.h"
#include "rb/Physics/Compliant.h"
#include "rb/Physics/CueStrike.h"
#include "rb/Physics/Cushion.h"
#include "rb/Physics/Detect.h"
#include "rb/Physics/EventQueue.h"
#include "rb/Physics/Motion.h"
#include "rb/Physics/ParamTable.h"
#include "rb/Physics/Playback.h"
#include "rb/Physics/PocketDrop.h"
#include "rb/Physics/ShotResult.h"
#include "rb/Physics/Simulator.h"
#include "rb/Physics/Slate.h"
#include "rb/Rules/Evaluate.h"
#include "rb/Rules/Lag.h"
#include "rb/Rules/Match.h"
#include "rb/Rules/RulesConfig.h"
#include "rb/Rules/RulesTypes.h"
#include "rb/Rules/ShotFacts.h"
#include "rb/Rules/TableRules.h"
#include "rb/Shot/ShotRecord.h"
#include "rb/Shot/ShotRecordBuilder.h"
#include "rb/Version.h"

RB_TEST(Arch_HeadersSurviveUnrealAndWindowsMacros)
{
	// Reaching this point means every public header compiled with the traps above in effect.
	const rb::rules::RulesTable Table = rb::rules::MakeRulesTable(2.54, 1.27, 0.028575);
	RB_CHECK(Table.HeadStringX == -0.635);
	RB_CHECK(rb::kPi > 3.14 && rb::Max(1.0, 2.0) == 2.0 && rb::Min(1.0, 2.0) == 1.0);
}
