# rbsim: command-line shot runner writing JSON (event log + sampled trajectories), the rbsimInput replay
# format (--dump-input / --in) and the table-geometry export; viewer/index.html plays its output top-down.
# Owner: WP-7 (output, playback & tools). Interface: Docs/architecture.md, section "rbsim".

add_executable(rbsim Main.cpp JsonWriter.cpp JsonWriter.h JsonReader.cpp JsonReader.h SimInputJson.cpp SimInputJson.h)
target_link_libraries(rbsim PRIVATE BilliardsCore)

if(MSVC)
	# fopen is fine for a developer tool; BilliardsCore itself never touches files.
	target_compile_definitions(rbsim PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
