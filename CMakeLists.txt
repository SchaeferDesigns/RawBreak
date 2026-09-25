cmake_minimum_required(VERSION 3.25)
project(RawBreakCore LANGUAGES CXX)

# Standalone build of the engine-agnostic BilliardsCore (physics + rules) for unit tests and tools.
# The same sources are compiled by Unreal as the BilliardsCore module (Source/BilliardsCore).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RB_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Source/BilliardsCore)

if(MSVC)
	# CMake injects /EHsc by default; we build without exceptions like Unreal does.
	string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

file(GLOB_RECURSE RB_CORE_SOURCES CONFIGURE_DEPENDS ${RB_CORE_DIR}/Private/rb/*.cpp)
file(GLOB_RECURSE RB_CORE_HEADERS CONFIGURE_DEPENDS ${RB_CORE_DIR}/Public/rb/*.h ${RB_CORE_DIR}/Private/rb/*.h)

# Determinism guard (Docs/architecture.md section 11): every core translation unit must include
# rb/Core/FpGuard.h FIRST, so precise floating point without FMA contraction holds even inside a
# build that uses fast floating-point flags (Unreal).
foreach(RB_SOURCE ${RB_CORE_SOURCES})
	file(STRINGS ${RB_SOURCE} RB_FIRST_LINE LIMIT_COUNT 1)
	if(NOT RB_FIRST_LINE STREQUAL "#include \"rb/Core/FpGuard.h\"")
		message(FATAL_ERROR "${RB_SOURCE}: the first line must be #include \"rb/Core/FpGuard.h\" (determinism guard)")
	endif()
endforeach()

add_library(BilliardsCore STATIC ${RB_CORE_SOURCES} ${RB_CORE_HEADERS})
target_include_directories(BilliardsCore
	PUBLIC ${RB_CORE_DIR}/Public
	PRIVATE ${RB_CORE_DIR}/Private)

# Mirror Unreal's compile environment: strict warnings (incl. shadowing), no exceptions, no RTTI.
# No _USE_MATH_DEFINES: M_PI etc. would compile here and fail in Unreal (use rb::kPi).
if(MSVC)
	target_compile_options(BilliardsCore PUBLIC /W4 /WX /permissive- /GR- /EHs-c- /utf-8 /Zc:__cplusplus /fp:precise)
	target_compile_definitions(BilliardsCore PUBLIC _HAS_EXCEPTIONS=0)
else()
	target_compile_options(BilliardsCore PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Werror -fno-rtti -fno-exceptions -ffp-contract=off)
endif()
# rb/Core/Assert.h: RB_ASSERT is active in Debug builds only.
target_compile_definitions(BilliardsCore PUBLIC $<$<CONFIG:Debug>:RB_DEBUG_ASSERTS=1>)

enable_testing()
add_subdirectory(Tests/Core)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Tools/rbsim/CMakeLists.txt)
	add_subdirectory(Tools/rbsim)
endif()
