cmake_minimum_required(VERSION 3.21)
project(xml2_fix VERSION 1.4.0 LANGUAGES CXX RC)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "Build with MSVC for Win32 (cmake -A Win32) - X-Men Legends II is a 32-bit game.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # no VC++ redistributable needed
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin") # the test runs next to the DLL
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_compile_options(/W4 /permissive- /utf-8)

# lcsave: the LC save format and the old-save converter (C++ translation of legends-classic tools/lcsave).
add_library(lcsave STATIC
	src/lcsave/keys.cpp
	src/lcsave/value.cpp
	src/lcsave/xmlb.cpp
	src/lcsave/oldsave.cpp
	src/lcsave/container.cpp
	src/lcsave/tables.cpp
	src/lcsave/convert.cpp
	src/lcsave/reverse.cpp
	src/lcsave/dump.cpp
	src/lcsave/api.cpp
	src/lcsave/api_engine.cpp)
target_include_directories(lcsave PUBLIC src/lcsave)

# The fix: a drop-in dinput.dll for the game folder.
add_library(xml2_fix SHARED
	src/geometry_sharing.cpp
	src/ladder_paths.cpp
	src/break_rule.cpp
	src/exports.cpp
	src/gamepad_fix.cpp
	src/pad_profile.cpp
	src/pad_bindings.cpp
	src/pad_prompts.cpp
	src/pad_input.cpp
	src/virtual_pad.cpp
	src/xinput_pad.cpp
	src/ini.cpp
	src/iat_hook.cpp
	src/openspy_redirect.cpp
	src/local_ip.cpp
	src/net_trace.cpp
	src/mod_loader.cpp
	src/display.cpp
	src/frame_rate.cpp
	src/options_menu.cpp
	src/new_game.cpp
	src/save_format.cpp
	src/save_content_guards.cpp
	src/save_trace.cpp
	src/postgame.cpp
	src/game_version.cpp
	src/window_title.cpp
	src/main_menu.cpp
	src/review_menu.cpp
	src/xtract.cpp
	src/forced_teams.cpp
	src/conversations.cpp
	src/objective_text.cpp
	src/xp_curve.cpp
	src/balance.cpp
	src/limits.cpp
	src/resolution_list.cpp
	src/frame_capture.cpp
	src/test_input.cpp
	src/test_state.cpp
	src/discord_ipc.cpp
	src/discord_presence.cpp
	src/log.cpp
	src/dinput.def
	src/version.rc)
target_compile_definitions(xml2_fix PRIVATE FIX_LOG_FILE=L"xml2-fix.log"
	FIX_VERSION="${PROJECT_VERSION}"
	FIX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} FIX_VERSION_MINOR=${PROJECT_VERSION_MINOR} FIX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
	FIX_VERSION_NUMBERS=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0)
target_link_libraries(xml2_fix PRIVATE lcsave dxguid ws2_32 winmm iphlpapi) # winmm: timeBeginPeriod for the frame limiter's fallback timer; iphlpapi: GetBestRoute2 for [Online] LocalIP
# Output is dinput.dll; its import library gets a different name so it can't shadow the Windows SDK's.
set_target_properties(xml2_fix PROPERTIES OUTPUT_NAME dinput ARCHIVE_OUTPUT_NAME xml2_fix)

# Checks the built DLL the way the game loads it (imported from next to the executable).
add_executable(xml2_test test/xml2_test.cpp src/frame_capture.cpp src/local_ip.cpp src/discord_ipc.cpp src/iat_hook.cpp src/pad_profile.cpp) # frame_capture: the back buffer copy, checked on a device of its own; local_ip: what LocalIP=auto asks Windows, to check the fix's answers against; discord_ipc: the Discord pipe, against a Discord of the test's own and for --discord-live; iat_hook: msvcr71.dll's ExitProcess hooked as the fix hooks it; pad_profile: the Dual Action's objects and state, for the virtual pads' data formats
target_compile_definitions(xml2_test PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS)
target_include_directories(xml2_test PRIVATE src) # ini_rules.hpp, display_rules.hpp, online_rules.hpp, local_ip_rules.hpp, frame_rate_rules.hpp, options_menu_rules.hpp, resolution_rules.hpp, limits_rules.hpp, forced_teams_rules.hpp, conversations_rules.hpp, postgame_rules.hpp, main_menu_rules.hpp, review_menu_rules.hpp, xp_curve_rules.hpp, balance_rules.hpp, break_rule_rules.hpp, window_title_rules.hpp, game_version_rules.hpp, pad_prompts_rules.hpp, test_input_rules.hpp, pad_input_rules.hpp, virtual_pad_rules.hpp, discord_rules.hpp, mod_order.hpp, image_file.hpp: the decisions, checked without the game
target_link_libraries(xml2_test PRIVATE xml2_fix dinput8.lib dxguid xinput ws2_32 winmm iphlpapi) # dinput8.lib: only for c_dfDIJoystick(2); winmm: timeBeginPeriod in the present timing

add_executable(xml2_ladder_paths_test test/ladder_paths_test.cpp)
target_include_directories(xml2_ladder_paths_test PRIVATE src)

# Pure rules; no game process or graphics.
add_executable(xml2_objective_text_test test/objective_text_test.cpp)
target_include_directories(xml2_objective_text_test PRIVATE src)

add_executable(xml2_stat_points_test test/stat_points_test.cpp)
target_include_directories(xml2_stat_points_test PRIVATE src)

# lcsave: the reference's test vectors, the real-save corpus against the Python outputs (folders given on the
# command line), and a fuzz of the readers.
add_executable(lcsave_test test/lcsave_test.cpp test/lcsave_vectors.cpp)
target_link_libraries(lcsave_test PRIVATE lcsave)

# Save protection and compatibility decisions on synthetic fixtures.
add_executable(xml2_save_guards_test test/save_guard_rules_test.cpp)
target_include_directories(xml2_save_guards_test PRIVATE src)

# Effective mod winners and unsupported keyrefs, using original synthetic files.
add_executable(xml2_save_content_guards_test test/save_content_guards_test.cpp
    src/save_content_guards.cpp src/mod_loader.cpp src/iat_hook.cpp src/ini.cpp src/log.cpp src/game_version.cpp)
target_include_directories(xml2_save_content_guards_test PRIVATE src)
target_link_libraries(xml2_save_content_guards_test PRIVATE lcsave ws2_32)
target_compile_definitions(xml2_save_content_guards_test PRIVATE FIX_LOG_FILE=L"save-guard-test.log")
