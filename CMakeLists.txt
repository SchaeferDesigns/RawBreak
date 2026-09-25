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

add_library(BilliardsCore STATIC ${RB_CORE_SOURCES} ${RB_CORE_HEADERS})
target_include_directories(BilliardsCore
	PUBLIC ${RB_CORE_DIR}/Public
	PRIVATE ${RB_CORE_DIR}/Private)

# Mirror Unreal's compile environment: strict warnings (incl. shadowing), no exceptions, no RTTI.
if(MSVC)
	target_compile_options(BilliardsCore PUBLIC /W4 /WX /permissive- /GR- /EHs-c- /utf-8 /Zc:__cplusplus /fp:precise)
	target_compile_definitions(BilliardsCore PUBLIC _HAS_EXCEPTIONS=0 _USE_MATH_DEFINES)
else()
	target_compile_options(BilliardsCore PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Werror -fno-rtti -fno-exceptions)
endif()

enable_testing()
add_subdirectory(Tests/Core)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Tools/rbsim/CMakeLists.txt)
	add_subdirectory(Tools/rbsim)
endif()
