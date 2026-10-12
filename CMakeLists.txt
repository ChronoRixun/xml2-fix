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
	src/lcsave/conversations.cpp
	src/lcsave/dump.cpp
	src/lcsave/api.cpp
	src/lcsave/api_engine.cpp
	src/lcsave/typed_registry.cpp)
target_include_directories(lcsave PUBLIC src/lcsave)

# The fix: a drop-in dinput.dll for the game folder.
add_library(xml2_fix SHARED
	src/mold_beam.cpp
	src/mold_controller.cpp
	src/mold_query.cpp
	src/mold_effects.cpp
	src/stat_help.cpp
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
	src/classic_main_backdrop.cpp
	src/classic_main_panels.cpp
	src/review_menu.cpp
	src/cutscene_mouse.cpp
	src/personal_item_text.cpp
	src/xtract.cpp
	src/forced_teams.cpp
	src/conversations.cpp
	src/gameplay_agreement.cpp
	src/patch_transaction.cpp
	src/explosive_first_hit.cpp
	src/confirmation_release.cpp
	src/objective_text.cpp
	src/xp_curve.cpp
	src/balance.cpp
	src/limits.cpp
	src/entity_capacity.cpp
	src/entity_owners.cpp
	src/entity_transfer.cpp
	src/entity_world.cpp
	src/entity_grid.cpp
	src/entity_query.cpp
	src/entity_frames.cpp
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
target_include_directories(xml2_test PRIVATE src) # ini_rules.hpp, display_rules.hpp, online_rules.hpp, local_ip_rules.hpp, frame_rate_rules.hpp, options_menu_rules.hpp, resolution_rules.hpp, limits_rules.hpp, forced_teams_rules.hpp, conversations_rules.hpp, postgame_rules.hpp, main_menu_rules.hpp, review_menu_rules.hpp, cutscene_mouse_rules.hpp, xp_curve_rules.hpp, balance_rules.hpp, break_rule_rules.hpp, window_title_rules.hpp, game_version_rules.hpp, pad_prompts_rules.hpp, test_input_rules.hpp, pad_input_rules.hpp, virtual_pad_rules.hpp, discord_rules.hpp, mod_order.hpp, image_file.hpp: the decisions, checked without the game
target_link_libraries(xml2_test PRIVATE xml2_fix dinput8.lib dxguid xinput ws2_32 winmm iphlpapi) # dinput8.lib: only for c_dfDIJoystick(2); winmm: timeBeginPeriod in the present timing

add_executable(xml2_ladder_paths_test test/ladder_paths_test.cpp)
target_include_directories(xml2_ladder_paths_test PRIVATE src)

add_executable(xml2_xml1_controllers_test test/xml1_controllers_test.cpp src/game_version.cpp)
target_include_directories(xml2_xml1_controllers_test PRIVATE src)
target_link_libraries(xml2_xml1_controllers_test PRIVATE ws2_32)

# Pure rules; no game process or graphics.
add_executable(xml2_objective_text_test test/objective_text_test.cpp)
target_include_directories(xml2_objective_text_test PRIVATE src)

add_executable(xml2_classic_main_backdrop_test test/classic_main_backdrop_test.cpp)
target_include_directories(xml2_classic_main_backdrop_test PRIVATE src)
target_compile_options(xml2_classic_main_backdrop_test PRIVATE /UNDEBUG)

add_executable(xml2_classic_main_panels_test test/classic_main_panels_test.cpp)
target_include_directories(xml2_classic_main_panels_test PRIVATE src)
target_compile_options(xml2_classic_main_panels_test PRIVATE /UNDEBUG)

add_executable(xml2_stat_points_test test/stat_points_test.cpp)
target_include_directories(xml2_stat_points_test PRIVATE src)

add_executable(xml2_stat_help_test test/stat_help_test.cpp)
target_include_directories(xml2_stat_help_test PRIVATE src)

# lcsave: the reference's test vectors, the real-save corpus against the Python outputs (folders given on the
# command line), and a fuzz of the readers.
add_executable(lcsave_test test/lcsave_test.cpp test/lcsave_vectors.cpp)
target_link_libraries(lcsave_test PRIVATE lcsave)
add_executable(xml2_confirmation_release_test test/confirmation_release_test.cpp)
target_include_directories(xml2_confirmation_release_test PRIVATE src)

# Core save capture/lifetime rules, synthetic and independent of game/graphics.
add_executable(xml2_save_core_test test/save_core_test.cpp)
target_include_directories(xml2_save_core_test PRIVATE src)

# Exact bytes for Python-generated synthetic recording/retention vectors.
add_executable(lcsave_durable_parity test/lcsave_durable_parity.cpp)
target_link_libraries(lcsave_durable_parity PRIVATE lcsave)

# Save protection and compatibility decisions on synthetic fixtures.
add_executable(xml2_save_guards_test test/save_guard_rules_test.cpp)
target_include_directories(xml2_save_guards_test PRIVATE src)

# Effective mod winners and unsupported keyrefs, using original synthetic files.
add_executable(xml2_save_content_guards_test test/save_content_guards_test.cpp
    src/save_content_guards.cpp src/mod_loader.cpp src/iat_hook.cpp src/ini.cpp src/log.cpp src/game_version.cpp)
target_include_directories(xml2_save_content_guards_test PRIVATE src)
target_link_libraries(xml2_save_content_guards_test PRIVATE lcsave ws2_32)
target_compile_definitions(xml2_save_content_guards_test PRIVATE FIX_LOG_FILE=L"save-guard-test.log")

# Trivia cursors must never alias the native answer bitmap.
add_executable(xml2_trivia_test test/trivia_test.cpp)
target_include_directories(xml2_trivia_test PRIVATE src)

# Real DLL initialization against an owned synthetic Win32 image; no game/save inputs.
add_executable(xml2_save_startup_test test/save_startup_test.cpp)
target_include_directories(xml2_save_startup_test PRIVATE src)
target_link_libraries(xml2_save_startup_test PRIVATE ws2_32)
target_link_options(xml2_save_startup_test PRIVATE /DYNAMICBASE:NO /BASE:0x400000 /MERGE:.rdata=.fixture_data /MERGE:.text=.fixture_code)

# Opt-in fault DLL: same implementation, only save-installation Windows calls wrapped.
option(XML2_SAVE_STARTUP_FAULT_TEST "Build owned save-startup fault injection DLL" OFF)
if(XML2_SAVE_STARTUP_FAULT_TEST)
 get_target_property(fault_sources xml2_fix SOURCES)
 list(REMOVE_ITEM fault_sources src/dinput.def)
 add_library(xml2_save_startup_faults SHARED ${fault_sources})
 get_target_property(fault_definitions xml2_fix COMPILE_DEFINITIONS)
 target_compile_definitions(xml2_save_startup_faults PRIVATE ${fault_definitions} XML2_SAVE_STARTUP_FAULT_TEST)
 target_link_libraries(xml2_save_startup_faults PRIVATE lcsave dxguid ws2_32 winmm iphlpapi)
 set_target_properties(xml2_save_startup_faults PROPERTIES OUTPUT_NAME save-startup-faults)
endif()

# Conversation identity caching and invalidation, using only invented filesystem fixtures.
add_executable(lcsave_conversation_digest_test test/conversation_digest_test.cpp)
target_link_libraries(lcsave_conversation_digest_test PRIVATE lcsave)

# Effective per-zone conversation provenance: actual lookup, capture/restore, no game.
add_executable(lcsave_conversation_packages_test test/conversation_packages_test.cpp
    src/mod_loader.cpp src/iat_hook.cpp src/log.cpp)
target_include_directories(lcsave_conversation_packages_test PRIVATE src src/lcsave)
target_link_libraries(lcsave_conversation_packages_test PRIVATE lcsave)
target_compile_definitions(lcsave_conversation_packages_test PRIVATE FIX_LOG_FILE=L"conversation-pin-test.log")

add_executable(xml2_explosive_first_hit_test test/explosive_first_hit_test.cpp src/explosive_first_hit.cpp)
target_include_directories(xml2_explosive_first_hit_test PRIVATE src)
# This test reserves the retail image address for isolated native code.
target_link_options(xml2_explosive_first_hit_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_gameplay_agreement_test test/gameplay_agreement_test.cpp src/gameplay_agreement.cpp src/patch_transaction.cpp src/explosive_first_hit.cpp src/game_version.cpp)
target_include_directories(xml2_gameplay_agreement_test PRIVATE src)
target_link_libraries(xml2_gameplay_agreement_test PRIVATE ws2_32)

target_link_options(xml2_gameplay_agreement_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

# The protected code transaction (Phase A section 7) on blocks of the test's own: guards, conflicts, page ledger,
# injected failures and verified rollback.
add_executable(xml2_patch_transaction_test test/patch_transaction_test.cpp src/patch_transaction.cpp)
target_include_directories(xml2_patch_transaction_test PRIVATE src)

# Entity capacity, query owner: synthetic at 256/1024 and differential against the retail query code at 256
# (the exe path on the command line is a read-only test input; without it the differential part is SKIPPED).
# The test body is a DLL; the launcher is a CRT-free image based at 0x400000 whose fixture section starts right
# after one page of code, so it covers the retail helpers from 0x419c90 that a CRT-bearing image cannot.
add_library(xml2_entity_query_test_body SHARED test/entity_query_test.cpp src/entity_query.cpp)
target_include_directories(xml2_entity_query_test_body PRIVATE src)
target_link_libraries(xml2_entity_query_test_body PRIVATE shell32)
add_executable(xml2_entity_query_test test/entity_query_launch.cpp)
target_compile_options(xml2_entity_query_test PRIVATE /GS- /Zl)
target_link_options(xml2_entity_query_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO /NODEFAULTLIB /ENTRY:launch /SUBSYSTEM:CONSOLE)
target_link_libraries(xml2_entity_query_test PRIVATE kernel32)
add_dependencies(xml2_entity_query_test xml2_entity_query_test_body)

# CEntityGrid replacement owner: synthetic checks at both capacities and the retail grid code executed in-process
# against the port at 256 (the executable path is given on the command line; without it the native part is skipped).
add_executable(xml2_entity_grid_test test/entity_grid_test.cpp src/entity_grid.cpp)
target_include_directories(xml2_entity_grid_test PRIVATE src)
target_link_options(xml2_entity_grid_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

# Phase A entity capacity: the widened caller frames (physics/CCollide, navigation, named query), synthetic at
# 256 and 1024 against scripted World/Grid/Query fixtures, and differential against the retail frame code copied
# from a private executable given on the command line (skipped when absent). Reserves the retail image address.
# The test's own image must own the retail range 0x401000..0x801000: a 4 MB code-class block in .text$a (generated
# here, never a source file) sorts before the compiler's .text$mn, so the test's code starts above it.
enable_language(ASM_MASM)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/entity_frames_fixture.asm"
".386
.MODEL FLAT
fixture_seg SEGMENT PARA PUBLIC 'CODE' ALIAS('.text$a')
PUBLIC _fixture_space
_fixture_space DB 400000h DUP(0)
fixture_seg ENDS
END
")
add_executable(xml2_entity_frames_test test/entity_frames_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/entity_frames_fixture.asm" src/entity_frames.cpp)
target_include_directories(xml2_entity_frames_test PRIVATE src)
target_link_options(xml2_entity_frames_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

# Tests that execute retail code (or bridges that jump into it) at its own addresses own the image range: the same
# 4 MB code-class block in .text$a as the frames test, sorted before the compiler's .text$mn.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm"
".386
.MODEL FLAT
fixture_seg SEGMENT PARA PUBLIC 'CODE' ALIAS('.text$a')
PUBLIC _fixture_space
_fixture_space DB 400000h DUP(0)
fixture_seg ENDS
END
")
# The native campaign transfer receiver (0x608630) and its replacement executed side by side (Phase A, review P1).
add_executable(xml2_entity_transfer_test test/entity_transfer_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/entity_transfer.cpp)
target_include_directories(xml2_entity_transfer_test PRIVATE src)
target_link_options(xml2_entity_transfer_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# The save-core capture bridges executed as emitted under the retail and E10A2 profiles (Phase A, review P1).
add_executable(xml2_save_capture_bridge_test test/save_capture_bridge_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_save_capture_bridge_test PRIVATE src)
target_link_options(xml2_save_capture_bridge_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

# The entity-capacity install's game-free decisions: ledger shape and binding, allocator ring, save claim, transfer
# bounds, startup quiescence, layout contract.
add_executable(xml2_entity_capacity_test test/entity_capacity_test.cpp src/patch_transaction.cpp)
target_include_directories(xml2_entity_capacity_test PRIVATE src)

# CWorld replacement owner (Phase A entity capacity): synthetic at 256 and 1024 (lifecycle, names, hierarchy,
# relationships, properties, retirement, reset, reentrancy, invariants) and differential against the retail World code
# executed in-process at 256 (the executable path on the command line is a read-only test input; without it the
# differential part is SKIPPED). Reserves the retail image address.
add_executable(xml2_entity_world_test test/entity_world_test.cpp src/entity_world.cpp)
target_include_directories(xml2_entity_world_test PRIVATE src)
target_link_options(xml2_entity_world_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
