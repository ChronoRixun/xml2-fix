cmake_minimum_required(VERSION 3.21)
project(xml2_fix VERSION 1.9.0 LANGUAGES CXX RC)

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
	src/lcsave/fresh_tail.cpp
	src/lcsave/api_engine.cpp
	src/lcsave/typed_registry.cpp)
target_include_directories(lcsave PUBLIC src/lcsave)

# The fix: a drop-in dinput.dll for the game folder.
add_library(xml2_fix SHARED
	src/mold_beam.cpp
	src/mold_controller.cpp
	src/xml1_bosses.cpp
	src/boss_dopples.cpp
	src/boss_avalanche.cpp
	src/boss_pyro.cpp
	src/boss_mystique.cpp
	src/boss_shadowking.cpp
	src/boss_magneto.cpp
	src/boss_blob.cpp
	src/multipart.cpp
	src/mold_query.cpp
	src/mold_effects.cpp
	src/lockon_effects.cpp
	src/xml1_turrets.cpp
	src/stat_help.cpp
	src/geometry_sharing.cpp
	src/ladder_paths.cpp
	src/break_rule.cpp
	src/placement_crush.cpp
	src/world_impact.cpp
	src/box_harm.cpp
	src/harm_loop_startup.cpp
	src/harm_single_chain.cpp
	src/double_heal.cpp
	src/world_backfaces.cpp
	src/balance_parity.cpp
	src/xml1_cling.cpp
	src/xml1_rogue_decide.cpp
	src/xml1_grab_carry.cpp
	src/xml1_air_carry.cpp
	src/lift_rule.cpp
	src/combo_heads.cpp
	src/model_pools.cpp
	src/pool_counters.cpp
	src/online_tick.cpp
	src/npc_power.cpp
	src/npc_semi_auto.cpp
	src/stun_rule.cpp
	src/shared_handlers.cpp
	src/shared_handlers_c.cpp
	src/danger_room_party.cpp
	src/danger_room_healthbars.cpp
	src/resistance_display.cpp
	src/classic_hud.cpp
	src/codex_menu.cpp
	src/classic_objectives.cpp
	src/speaker_label.cpp
	src/pack_amount.cpp
	src/exports.cpp
	src/gamepad_fix.cpp
	src/input_worker.cpp
	src/pad_profile.cpp
	src/pad_bindings.cpp
	src/pad_prompts.cpp
	src/pad_input.cpp
	src/virtual_pad.cpp
	src/xinput_pad.cpp
	src/x1_leader.cpp
	src/ini.cpp
	src/iat_hook.cpp
	src/openspy_redirect.cpp
	src/local_ip.cpp
	src/net_trace.cpp
	src/load_trace.cpp
	src/plugin_scan.cpp
	src/mod_loader.cpp
	src/display.cpp
	src/display_settings.cpp
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
	src/danger_room_gate.cpp
	src/revival_cost.cpp
	src/review_menu.cpp
	src/cutscene_mouse.cpp
	src/personal_item_text.cpp
	src/xtract.cpp
	src/forced_teams.cpp
	src/script_functions.cpp
	src/script_function_overflow.cpp
	src/solo_mode.cpp
	src/conversations.cpp
	src/gameplay_agreement.cpp
	src/resident_stats.cpp
	src/motion_pool.cpp
	src/char_sounds.cpp
	src/anim_enums.cpp
	src/anim_enum_registry.cpp
	src/patch_transaction.cpp
	src/receive_validation.cpp
	src/explosive_first_hit.cpp
	src/rank_costs.cpp
	src/death_threshold.cpp
	src/death_switch.cpp
	src/confirmation_release.cpp
	src/objective_text.cpp
	src/xp_curve.cpp
	src/balance.cpp
	src/limits.cpp
	src/address_space.cpp
	src/entity_capacity.cpp
	src/actor_pools.cpp
	src/entity_owners.cpp
	src/entity_transfer.cpp
	src/entity_world.cpp
	src/entity_grid.cpp
	src/entity_query.cpp
	src/entity_frames.cpp
	src/resolution_list.cpp
	src/frame_capture.cpp
	src/test_input.cpp
	src/test_mode.cpp
	src/test_state.cpp
	src/discord_ipc.cpp
	src/discord_presence.cpp
	src/log.cpp
	src/gns_transport.cpp
	src/online_room.cpp
	src/online_status.cpp
	src/join_in_progress.cpp
	src/net_impair.cpp
	src/dinput.def
	src/version.rc)
target_compile_definitions(xml2_fix PRIVATE FIX_LOG_FILE=L"xml2-fix.log"
	FIX_VERSION="${PROJECT_VERSION}"
	FIX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} FIX_VERSION_MINOR=${PROJECT_VERSION_MINOR} FIX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
	FIX_VERSION_NUMBERS=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0)
target_link_libraries(xml2_fix PRIVATE lcsave dxguid ws2_32 winmm iphlpapi) # winmm: timeBeginPeriod for the frame limiter's fallback timer; iphlpapi: GetBestRoute2 for [Online] LocalIP
# [Online] Transport=gns (docs/gns-transport.md): GameNetworkingSockets v1.6.1 linked statically (BSD-3-Clause; with
# protobuf and abseil, /MT, built with this same toolset). XML2_FIX_GNS_ROOT is a folder with include/steam and lib/
# (GameNetworkingSockets_s.lib, libprotobuf.lib, absl_*.lib, utf8_*.lib). Without it the DLL is built without GNS and
# Transport=gns logs that the stock transport stays in use.
set(XML2_FIX_GNS_ROOT "" CACHE PATH "GameNetworkingSockets static build (include/, lib/)")
if(XML2_FIX_GNS_ROOT)
	target_sources(xml2_fix PRIVATE src/broker_tls.cpp)
	file(GLOB XML2_FIX_GNS_ABSL "${XML2_FIX_GNS_ROOT}/lib/absl_*.lib")
	file(GLOB XML2_FIX_GNS_UTF8 "${XML2_FIX_GNS_ROOT}/lib/utf8_*.lib") # utf8_range (+ utf8_validity in older packages)
	target_include_directories(xml2_fix PRIVATE "${XML2_FIX_GNS_ROOT}/include")
	target_compile_definitions(xml2_fix PRIVATE XML2_FIX_GNS=1 STEAMNETWORKINGSOCKETS_OPENSOURCE STEAMNETWORKINGSOCKETS_STATIC_LINK)
	set(XML2_FIX_GNS_LIBS "${XML2_FIX_GNS_ROOT}/lib/GameNetworkingSockets_s.lib" "${XML2_FIX_GNS_ROOT}/lib/libprotobuf.lib"
		${XML2_FIX_GNS_ABSL} ${XML2_FIX_GNS_UTF8} bcrypt crypt32 secur32 psapi shell32 ole32)
	target_link_libraries(xml2_fix PRIVATE ${XML2_FIX_GNS_LIBS})
	# The project CA pin the GNS build carries (net030-gnslib ca-pin.txt): the broker's auth_ok names its CA, checked against it.
	if(EXISTS "${XML2_FIX_GNS_ROOT}/ca-pin.txt")
		file(STRINGS "${XML2_FIX_GNS_ROOT}/ca-pin.txt" XML2_FIX_GNS_CA_PIN LIMIT_COUNT 1)
		target_compile_definitions(xml2_fix PRIVATE "XML2_FIX_GNS_CA_PIN=\"${XML2_FIX_GNS_CA_PIN}\"")
	endif()
else()
	target_compile_definitions(xml2_fix PRIVATE XML2_FIX_GNS=0)
endif()
# Output is dinput.dll; its import library gets a different name so it can't shadow the Windows SDK's.
set_target_properties(xml2_fix PROPERTIES OUTPUT_NAME dinput ARCHIVE_OUTPUT_NAME xml2_fix)

# Checks the built DLL the way the game loads it (imported from next to the executable).
add_executable(xml2_test test/xml2_test.cpp src/frame_capture.cpp src/local_ip.cpp src/discord_ipc.cpp src/iat_hook.cpp src/pad_profile.cpp) # frame_capture: the back buffer copy, checked on a device of its own; local_ip: what LocalIP=auto asks Windows, to check the fix's answers against; discord_ipc: the Discord pipe, against a Discord of the test's own and for --discord-live; iat_hook: msvcr71.dll's ExitProcess hooked as the fix hooks it; pad_profile: the Dual Action's objects and state, for the virtual pads' data formats
target_compile_definitions(xml2_test PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS)
target_include_directories(xml2_test PRIVATE src) # ini_rules.hpp, display_rules.hpp, online_rules.hpp, local_ip_rules.hpp, frame_rate_rules.hpp, options_menu_rules.hpp, resolution_rules.hpp, limits_rules.hpp, forced_teams_rules.hpp, conversations_rules.hpp, postgame_rules.hpp, main_menu_rules.hpp, review_menu_rules.hpp, cutscene_mouse_rules.hpp, xp_curve_rules.hpp, balance_rules.hpp, break_rule_rules.hpp, x1_leader_rules.hpp, window_title_rules.hpp, game_version_rules.hpp, pad_prompts_rules.hpp, test_input_rules.hpp, pad_input_rules.hpp, virtual_pad_rules.hpp, discord_rules.hpp, mod_order.hpp, image_file.hpp: the decisions, checked without the game
target_link_libraries(xml2_test PRIVATE xml2_fix dinput8.lib dxguid xinput ws2_32 winmm iphlpapi) # dinput8.lib: only for c_dfDIJoystick(2); winmm: timeBeginPeriod in the present timing

add_executable(xml2_multipart_test test/multipart_test.cpp)
target_include_directories(xml2_multipart_test PRIVATE src)

add_executable(xml2_ladder_paths_test test/ladder_paths_test.cpp)
target_include_directories(xml2_ladder_paths_test PRIVATE src)

add_executable(xml2_world_impact_test test/world_impact_test.cpp)
target_include_directories(xml2_world_impact_test PRIVATE src)

add_executable(xml2_danger_room_party_test test/danger_room_party_test.cpp)
target_include_directories(xml2_danger_room_party_test PRIVATE src)
target_compile_options(xml2_danger_room_party_test PRIVATE /UNDEBUG)

add_executable(xml2_danger_room_healthbars_test test/danger_room_healthbars_test.cpp)
target_include_directories(xml2_danger_room_healthbars_test PRIVATE src)
target_compile_options(xml2_danger_room_healthbars_test PRIVATE /UNDEBUG)

add_executable(xml2_xml1_controllers_test test/xml1_controllers_test.cpp src/game_version.cpp)
add_executable(xml2_boss_mystique_bridge_test test/boss_mystique_bridge_test.cpp)
target_include_directories(xml2_boss_mystique_bridge_test PRIVATE src)
target_include_directories(xml2_xml1_controllers_test PRIVATE src)
target_link_libraries(xml2_xml1_controllers_test PRIVATE ws2_32)

# Pure rules; no game process or graphics.
add_executable(xml2_classic_objectives_test test/classic_objectives_test.cpp)
target_include_directories(xml2_classic_objectives_test PRIVATE src)
target_compile_options(xml2_classic_objectives_test PRIVATE /UNDEBUG)

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

add_executable(xml2_solo_mode_test test/solo_mode_test.cpp)
target_include_directories(xml2_solo_mode_test PRIVATE src)

# E12: the address space words, the 2 GB guard's straddle test, the high-address test's reservations, the pipe verb
add_executable(xml2_address_space_test test/address_space_test.cpp)
target_include_directories(xml2_address_space_test PRIVATE src)
target_compile_options(xml2_address_space_test PRIVATE /UNDEBUG)
# the module itself in a large-address-aware host: the guard, the status, the high-address test, the crash line
add_executable(xml2_address_space_host test/address_space_host.cpp src/address_space.cpp src/log.cpp)
target_include_directories(xml2_address_space_host PRIVATE src)
target_compile_definitions(xml2_address_space_host PRIVATE FIX_LOG_FILE=L"address-space-test.log")
target_link_options(xml2_address_space_host PRIVATE /LARGEADDRESSAWARE)

add_executable(xml2_stat_help_test test/stat_help_test.cpp)
target_include_directories(xml2_stat_help_test PRIVATE src)

# lcsave: the reference's test vectors, the real-save corpus against the Python outputs (folders given on the
# command line), and a fuzz of the readers.
add_executable(lcsave_test test/lcsave_test.cpp test/lcsave_vectors.cpp)
target_link_libraries(lcsave_test PRIVATE lcsave)
# The per-zone save-compatibility release gate (scripts/zone_records_gate.py): applied zone records of real saves on
# a real build's tables (no engine, no game). Takes arguments; run_native.py does not run it.
add_executable(lcsave_zone_records test/lcsave_zone_records.cpp)
target_link_libraries(lcsave_zone_records PRIVATE lcsave)
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
 if(XML2_FIX_GNS_ROOT) # the same GameNetworkingSockets build as dinput.dll (its sources and definitions are xml2_fix's)
  target_include_directories(xml2_save_startup_faults PRIVATE "${XML2_FIX_GNS_ROOT}/include")
  target_link_libraries(xml2_save_startup_faults PRIVATE ${XML2_FIX_GNS_LIBS})
 endif()
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

add_executable(xml2_x1_leader_test test/x1_leader_test.cpp src/x1_leader.cpp)
target_include_directories(xml2_x1_leader_test PRIVATE src)
# This test reserves the retail image address for isolated native code.
target_link_options(xml2_x1_leader_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

# [Game] DeathSwitchClear: the production installer and hook on the retail addresses, in a fixture section of the
# test's own (its image is based at 0x300000 so that section spans them): rules, every guard refusal, the one rel32
# write, the hook's clear and what it leaves alone. Optional argument: a retail XMen2.exe, read only, for the guards.
add_executable(xml2_death_switch_test test/death_switch_test.cpp src/death_switch.cpp)
target_include_directories(xml2_death_switch_test PRIVATE src)
target_link_options(xml2_death_switch_test PRIVATE /BASE:0x300000 /DYNAMICBASE:NO)

add_executable(xml2_gameplay_agreement_test test/gameplay_agreement_test.cpp src/gameplay_agreement.cpp src/patch_transaction.cpp src/explosive_first_hit.cpp src/world_impact.cpp src/game_version.cpp)
add_executable(xml2_npc_combo_timing_test test/npc_combo_timing_test.cpp)
target_include_directories(xml2_npc_combo_timing_test PRIVATE src)
add_executable(xml2_npc_semi_auto_test test/npc_semi_auto_test.cpp)
target_include_directories(xml2_npc_semi_auto_test PRIVATE src)
# The shared AI query scheduler of both games executed side by side (XML1 0xfc990 from the owner's default.xbe,
# XMen2.exe 0x502730 retail and with rule 16): budget, reservation, the stale-reservation throttle, random schedules.
add_executable(xml2_npc_query_scheduler_test test/npc_query_scheduler_test.cpp)
add_executable(xml2_dopple_xbe_test test/dopple_xbe_test.cpp)
target_include_directories(xml2_dopple_xbe_test PRIVATE src)
target_include_directories(xml2_npc_query_scheduler_test PRIVATE src)
# [Balance] ReceiveRounding / DamageCap executed side by side (XML1 0x45517 / 0x5ce0d from the owner's default.xbe,
# XMen2.exe 0x437a54 / 0x453bfb retail and with rule 31's stubs), in 24- and 53-bit x87 precision.
add_executable(xml2_balance_parity_test test/balance_parity_test.cpp)
target_include_directories(xml2_balance_parity_test PRIVATE src)
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
add_executable(xml2_danger_room_party_install_test test/danger_room_party_install_test.cpp
    src/danger_room_party.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_danger_room_party_install_test PRIVATE src)
target_compile_options(xml2_danger_room_party_install_test PRIVATE /UNDEBUG)
target_link_options(xml2_danger_room_party_install_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_danger_room_healthbars_install_test test/danger_room_healthbars_install_test.cpp
    src/danger_room_healthbars.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_danger_room_healthbars_install_test PRIVATE src)
target_compile_options(xml2_danger_room_healthbars_install_test PRIVATE /UNDEBUG)
target_link_options(xml2_danger_room_healthbars_install_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# TestMode also executes the normal-play reader: reserve its higher menu/name/player globals.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test_mode_fixture.asm"
".386
.MODEL FLAT
fixture_seg SEGMENT PARA PUBLIC 'CODE' ALIAS('.text$a')
PUBLIC _fixture_space
_fixture_space DB 700000h DUP(0)
fixture_seg ENDS
END
")
add_executable(xml2_test_mode_test test/test_mode_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/test_mode_fixture.asm")
target_include_directories(xml2_test_mode_test PRIVATE src)
target_link_options(xml2_test_mode_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# The native campaign transfer receiver (0x608630) and its replacement executed side by side (Phase A, review P1).
add_executable(xml2_entity_transfer_test test/entity_transfer_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/entity_transfer.cpp)
target_include_directories(xml2_entity_transfer_test PRIVATE src)
target_link_options(xml2_entity_transfer_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/reliable_receive_fixture.asm"
".386
.MODEL FLAT
fixture_seg SEGMENT PARA PUBLIC 'CODE' ALIAS('.text$a')
PUBLIC _fixture_space
_fixture_space DB 700000h DUP(0)
fixture_seg ENDS
END
")
add_executable(xml2_reliable_receive_test test/reliable_receive_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/reliable_receive_fixture.asm" src/entity_transfer.cpp src/patch_transaction.cpp)
target_include_directories(xml2_reliable_receive_test PRIVATE src)
target_link_options(xml2_reliable_receive_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# [Online] FastTransfer: the retail campaign sender with the record bridge, through the production receiver.
add_executable(xml2_campaign_sender_test test/campaign_sender_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/reliable_receive_fixture.asm" src/entity_transfer.cpp src/patch_transaction.cpp)
target_include_directories(xml2_campaign_sender_test PRIVATE src)
target_link_options(xml2_campaign_sender_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# [Online] ReceiveValidation: the retail event dispatch under the production guard, plus the whole rule table.
add_executable(xml2_receive_validation_test test/receive_validation_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/reliable_receive_fixture.asm" src/receive_validation.cpp src/patch_transaction.cpp)
target_include_directories(xml2_receive_validation_test PRIVATE src)
target_link_options(xml2_receive_validation_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

# Alchemy plugin enumeration of the drive root: the rule, and the guard hooked into this test's own FindFirstFileA import.
add_executable(xml2_plugin_scan_test test/plugin_scan_test.cpp src/plugin_scan.cpp src/iat_hook.cpp)
target_include_directories(xml2_plugin_scan_test PRIVATE src)

add_executable(xml2_stun_rule_test test/stun_rule_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/stun_rule.cpp)
add_executable(xml2_shared_handlers_test test/shared_handlers_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/shared_handlers.cpp)
target_include_directories(xml2_shared_handlers_test PRIVATE src)
target_link_options(xml2_shared_handlers_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
target_include_directories(xml2_stun_rule_test PRIVATE src)
target_link_options(xml2_stun_rule_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
add_executable(xml2_box_harm_test test/box_harm_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/box_harm.cpp)
target_include_directories(xml2_box_harm_test PRIVATE src)
target_link_options(xml2_box_harm_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_harm_loop_startup_test test/harm_loop_startup_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/harm_loop_startup.cpp)
target_include_directories(xml2_harm_loop_startup_test PRIVATE src)
target_link_options(xml2_harm_loop_startup_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

# The worker bridges read and write the retail globals at 0xa6a7e1/0xa6a7e4, so the fixture image spans them: the loader maps the
# whole image at its fixed base before any other allocation, instead of the test reserving 0xa60000 itself (which fails whenever
# Windows has already placed something there). Same 7 MB zero block as the reliable-receive test.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/input_worker_fixture.asm"
".386
.MODEL FLAT
fixture_seg SEGMENT PARA PUBLIC 'CODE' ALIAS('.text$a')
PUBLIC _fixture_space
_fixture_space DB 700000h DUP(0)
fixture_seg ENDS
END
")
add_executable(xml2_input_worker_test test/input_worker_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/input_worker_fixture.asm" src/input_worker.cpp)
target_include_directories(xml2_input_worker_test PRIVATE src)
target_link_options(xml2_input_worker_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

# The GNS adapter's buffers and framing (src/gns_transport_core.hpp): ordering, coalesce, deferral, backlog, teardown.
add_executable(xml2_gns_transport_test test/gns_transport_test.cpp)
target_include_directories(xml2_gns_transport_test PRIVATE src)
# Rooms (src/online_room_core.hpp): broker lines, identities, join codes, virtual addresses, certificate blob, tunnel
# framing; with GNS, the library's Ed25519 against RFC 8032.
# [Online] JoinInProgress (src/join_in_progress_core.hpp): the host's bring-in state machine, rule 26, retail guards.
add_executable(xml2_join_in_progress_test test/join_in_progress_test.cpp)
target_include_directories(xml2_join_in_progress_test PRIVATE src)
add_executable(xml2_online_room_test test/online_room_test.cpp)
target_include_directories(xml2_online_room_test PRIVATE src)
target_compile_options(xml2_online_room_test PRIVATE /utf-8)
# The public game browser (contract v1.3/v1.4): room name, metadata from the game's state, presence and its pacing.
add_executable(xml2_online_browser_test test/online_browser_test.cpp)
target_include_directories(xml2_online_browser_test PRIVATE src)
target_compile_options(xml2_online_browser_test PRIVATE /utf-8)
# online-status.json (the launcher contract): JSON text, every state and error id, the atomic writer.
add_executable(xml2_online_status_test test/online_status_test.cpp src/online_status.cpp src/log.cpp)
target_include_directories(xml2_online_status_test PRIVATE src)
target_compile_definitions(xml2_online_status_test PRIVATE FIX_LOG_FILE=L"online-status-test.log")
target_compile_options(xml2_online_status_test PRIVATE /utf-8)
add_executable(xml2_online_security_test test/online_security_test.cpp)
target_include_directories(xml2_online_security_test PRIVATE src)
add_executable(xml2_log_test test/log_test.cpp src/log.cpp)
target_include_directories(xml2_log_test PRIVATE src)
target_compile_definitions(xml2_log_test PRIVATE FIX_LOG_FILE=L"log-test.log")
if(XML2_FIX_GNS_ROOT)
	target_compile_definitions(xml2_online_room_test PRIVATE XML2_FIX_GNS=1)
	target_link_libraries(xml2_online_room_test PRIVATE ${XML2_FIX_GNS_LIBS})
endif()
add_executable(xml2_broker_tls_test test/broker_tls_test.cpp src/broker_tls.cpp)
target_include_directories(xml2_broker_tls_test PRIVATE src)
target_compile_definitions(xml2_broker_tls_test PRIVATE BROKER_TLS_TEST_READ_GRAIN=1)
target_link_libraries(xml2_broker_tls_test PRIVATE secur32 crypt32 bcrypt ws2_32)
add_executable(xml2_harm_single_chain_test test/harm_single_chain_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/harm_single_chain.cpp)
target_include_directories(xml2_harm_single_chain_test PRIVATE src)
target_link_options(xml2_harm_single_chain_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# [Game] WorldBackfaces=xml1 (rule 30): the installer against guards seeded from the retail executable given on the
# command line, the retail trace helper 0x4a9650 run before and after the patch, the face filter and the agreement entry.
add_executable(xml2_world_backfaces_test test/world_backfaces_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/world_backfaces.cpp)
target_include_directories(xml2_world_backfaces_test PRIVATE src)
target_link_options(xml2_world_backfaces_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# [Game] Xml1WallCling=xml1 (rule 31): the wall-cling rules and every guard against the retail executable
# (XML2_FIX_TEST_EXE or the first argument); nothing of the game runs.
add_executable(xml2_xml1_cling_test test/xml1_cling_test.cpp)
target_include_directories(xml2_xml1_cling_test PRIVATE src)
# [Game] LiftRule=xml1 (rule 38): XML1 tables, retail guards, OFF bytes and emitted revision 2 bridges.
add_executable(xml2_lift_rule_test test/lift_rule_test.cpp src/lift_rule.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_lift_rule_test PRIVATE src)
target_link_options(xml2_lift_rule_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# multipart::install against the retail image (review cases: invalid table operand, foreign setSegmentVisible handler,
# relocated table, changed guard) and the loop length's generator draw. SKIPPED without XML2_FIX_TEST_EXE.
add_executable(xml2_multipart_install_test test/multipart_install_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_multipart_install_test PRIVATE src)
target_link_options(xml2_multipart_install_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# The save-core capture bridges executed as emitted under the retail and E10A2 profiles (Phase A, review P1).
add_executable(xml2_save_capture_bridge_test test/save_capture_bridge_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_save_capture_bridge_test PRIVATE src)
target_link_options(xml2_save_capture_bridge_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

# [Game] RankCosts: the cost rules, and the production install + stubs executed at the retail addresses against
# invented talents; an optional executable on the command line checks every guard and site against retail bytes.
add_executable(xml2_rank_costs_test test/rank_costs_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/rank_costs.cpp)
target_include_directories(xml2_rank_costs_test PRIVATE src)
target_link_options(xml2_rank_costs_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

# [Limits] ScriptFunctions: the decisions and the overflow table, then the retail registration (0x4d75a0) and lookup
# (0x4d6910) copied from an executable given on the command line and run at their own addresses, stock and patched with
# the DLL's own stubs (without the executable the native part is SKIPPED). Reserves the retail image address.
add_executable(xml2_script_functions_test test/script_functions_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/script_function_overflow.cpp)
target_include_directories(xml2_script_functions_test PRIVATE src)
target_link_options(xml2_script_functions_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

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

add_executable(xml2_lockon_effects_test test/lockon_effects_test.cpp)
target_include_directories(xml2_lockon_effects_test PRIVATE src)

add_executable(xml2_lockon_effects_install_test test/lockon_effects_install_test.cpp
    src/lockon_effects.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_lockon_effects_install_test PRIVATE src)
target_compile_options(xml2_lockon_effects_install_test PRIVATE /UNDEBUG)
target_link_options(xml2_lockon_effects_install_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_model_pools_test test/model_pools_test.cpp)
target_include_directories(xml2_model_pools_test PRIVATE src)
target_compile_options(xml2_model_pools_test PRIVATE /UNDEBUG)
# The pools' lifecycle executed: the retail code at its own addresses with the x2/x4 plans applied, on fake items.
add_executable(xml2_model_pools_lifecycle_test test/model_pools_lifecycle_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_model_pools_lifecycle_test PRIVATE src)
target_compile_options(xml2_model_pools_lifecycle_test PRIVATE /UNDEBUG /EHa)
target_link_options(xml2_model_pools_lifecycle_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_combo_heads_test test/combo_heads_test.cpp src/combo_heads.cpp)
target_include_directories(xml2_combo_heads_test PRIVATE src)
target_compile_options(xml2_combo_heads_test PRIVATE /UNDEBUG)
add_executable(xml2_resistance_display_test test/resistance_display_test.cpp)
target_include_directories(xml2_resistance_display_test PRIVATE src)
target_compile_options(xml2_resistance_display_test PRIVATE /UNDEBUG)
add_executable(xml2_resistance_display_install_test test/resistance_display_install_test.cpp
    src/resistance_display.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_resistance_display_install_test PRIVATE src)
target_compile_options(xml2_resistance_display_install_test PRIVATE /UNDEBUG)
target_link_options(xml2_resistance_display_install_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_classic_hud_install_test test/classic_hud_install_test.cpp
    src/classic_hud.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_classic_hud_install_test PRIVATE src)
target_compile_options(xml2_classic_hud_install_test PRIVATE /UNDEBUG)
target_link_options(xml2_classic_hud_install_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_codex_back_install_test test/codex_back_install_test.cpp
    src/codex_menu.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_codex_back_install_test PRIVATE src)
target_compile_options(xml2_codex_back_install_test PRIVATE /UNDEBUG)
target_link_options(xml2_codex_back_install_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_speaker_label_install_test test/speaker_label_install_test.cpp
    src/speaker_label.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_speaker_label_install_test PRIVATE src)
target_compile_options(xml2_speaker_label_install_test PRIVATE /UNDEBUG)
target_link_options(xml2_speaker_label_install_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# Release-gate tool (scripts/prove_off_wire.py, prove_combined_install.py): the retail image laid out at its own
# addresses inside this executable, the production DLL's whole DllMain run against it (tools/union_host).
add_executable(xml2_union_host tools/union_host/union_host.cpp tools/union_host/union_space.asm)
# Its own folder: no dinput.dll may sit next to it, or the engine DLLs' dinput import would load that one first.
set_target_properties(xml2_union_host PROPERTIES OUTPUT_NAME union_host RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/union")
target_link_options(xml2_union_host PRIVATE /BASE:0x400000 /DYNAMICBASE:NO /FIXED /SAFESEH:NO)

add_executable(xml2_xml1_rogue_decide_test test/xml1_rogue_decide_test.cpp)
target_include_directories(xml2_xml1_rogue_decide_test PRIVATE src)

add_executable(xml2_xml1_rogue_decide_install_test test/xml1_rogue_decide_install_test.cpp
    src/xml1_rogue_decide.cpp src/xml1_cling.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_xml1_rogue_decide_install_test PRIVATE src)
target_link_options(xml2_xml1_rogue_decide_install_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
add_executable(xml2_boss_wave3_test test/boss_wave3_test.cpp)
target_include_directories(xml2_boss_wave3_test PRIVATE src)
target_compile_options(xml2_boss_wave3_test PRIVATE /UNDEBUG)

add_executable(xml2_boss_wave3_bridge_test test/boss_wave3_bridge_test.cpp)
target_include_directories(xml2_boss_wave3_bridge_test PRIVATE src)
target_compile_options(xml2_boss_wave3_bridge_test PRIVATE /UNDEBUG)
add_executable(xml2_xml1_grab_carry_test test/xml1_grab_carry_test.cpp)
target_include_directories(xml2_xml1_grab_carry_test PRIVATE src)
add_executable(xml2_xml1_grab_carry_install_test test/xml1_grab_carry_install_test.cpp
    src/xml1_grab_carry.cpp src/xml1_air_carry.cpp src/xml1_cling.cpp src/xml1_rogue_decide.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_xml1_grab_carry_install_test PRIVATE src)
target_link_options(xml2_xml1_grab_carry_install_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
add_executable(xml2_shared_handlers_c_test test/shared_handlers_c_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm" src/shared_handlers_c.cpp)
target_include_directories(xml2_shared_handlers_c_test PRIVATE src)
target_link_options(xml2_shared_handlers_c_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
add_executable(xml2_tk_guard_test test/tk_guard_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_tk_guard_test PRIVATE src)
target_link_options(xml2_tk_guard_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# E4 stage 1: the production transaction plus retail pool/guard instructions inside
# (the descriptor test compares against 9677892 = release-1.9.0-beta1 before E4: NpcPowerRule revision 14)
# a union image. These are ordinary native tests; neither game nor engine starts.
add_executable(xml2_actor_pools_host test/actor_pools_host.cpp src/actor_pools.cpp src/patch_transaction.cpp tools/union_host/union_space.asm)
target_include_directories(xml2_actor_pools_host PRIVATE src)
target_compile_definitions(xml2_actor_pools_host PRIVATE XML2_ACTOR_POOLS_TEST)
target_compile_options(xml2_actor_pools_host PRIVATE /UNDEBUG)
target_link_options(xml2_actor_pools_host PRIVATE /BASE:0x400000 /DYNAMICBASE:NO /FIXED /SAFESEH:NO)
set_target_properties(xml2_actor_pools_host PROPERTIES OUTPUT_NAME actor_pools_host)

execute_process(COMMAND git show 9677892:src/npc_power_rules.hpp WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE ACTOR_POOLS_BETA7_POWER RESULT_VARIABLE ACTOR_POOLS_BASE_RESULT)
if(ACTOR_POOLS_BASE_RESULT EQUAL 0)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/npc_power_rules_beta7.hpp" "${ACTOR_POOLS_BETA7_POWER}")
    execute_process(COMMAND git show 9677892:src/gameplay_capabilities.hpp WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE ACTOR_POOLS_BETA7_AGREEMENT COMMAND_ERROR_IS_FATAL ANY)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gameplay_capabilities_beta7.hpp" "${ACTOR_POOLS_BETA7_AGREEMENT}")
    add_executable(xml2_actor_ai_descriptor_test test/actor_ai_descriptor_test.cpp)
    target_include_directories(xml2_actor_ai_descriptor_test PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_options(xml2_actor_ai_descriptor_test PRIVATE /UNDEBUG)
else()
    message(STATUS "Actor descriptor byte differential unavailable: fetch 9677892 (release-1.9.0-beta1 before E4) to run this required E4 test")
endif()

add_executable(xml2_anim_enum_registry_test test/anim_enum_registry_test.cpp src/anim_enum_registry.cpp)
target_include_directories(xml2_anim_enum_registry_test PRIVATE src)
target_compile_options(xml2_anim_enum_registry_test PRIVATE /UNDEBUG)

add_executable(xml2_anim_enums_test test/anim_enums_test.cpp src/anim_enums.cpp src/anim_enum_registry.cpp src/patch_transaction.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_anim_enums_test PRIVATE src)
target_compile_options(xml2_anim_enums_test PRIVATE /UNDEBUG)
target_link_options(xml2_anim_enums_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

execute_process(COMMAND git show 69f5115:src/gameplay_capabilities.hpp WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE CHAR_SOUNDS_BETA8_AGREEMENT RESULT_VARIABLE CHAR_SOUNDS_BASE_RESULT)
if(CHAR_SOUNDS_BASE_RESULT EQUAL 0)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gameplay_capabilities_charsounds_base.hpp" "${CHAR_SOUNDS_BETA8_AGREEMENT}")
    add_executable(xml2_char_sounds_test test/char_sounds_test.cpp src/char_sounds.cpp src/patch_transaction.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
    target_include_directories(xml2_char_sounds_test PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_options(xml2_char_sounds_test PRIVATE /UNDEBUG)
    target_link_options(xml2_char_sounds_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
else()
    message(STATUS "CharSounds TS2 baseline unavailable: fetch 69f5115 to run the required CharSounds fixture")
endif()

add_executable(xml2_xml1_air_carry_test test/xml1_air_carry_test.cpp
    src/xml1_grab_carry.cpp src/xml1_air_carry.cpp src/xml1_cling.cpp src/xml1_rogue_decide.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_xml1_air_carry_test PRIVATE src)
target_link_options(xml2_xml1_air_carry_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_motion_pool_test test/motion_pool_test.cpp src/motion_pool.cpp src/patch_transaction.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_motion_pool_test PRIVATE src)
target_compile_options(xml2_motion_pool_test PRIVATE /UNDEBUG)
target_link_options(xml2_motion_pool_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_resident_payload_test test/resident_payload_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_resident_payload_test PRIVATE src)
target_compile_options(xml2_resident_payload_test PRIVATE /UNDEBUG)
target_link_options(xml2_resident_payload_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_stats_layout_test test/stats_layout_test.cpp)
target_include_directories(xml2_stats_layout_test PRIVATE src)
target_compile_options(xml2_stats_layout_test PRIVATE /UNDEBUG)

add_executable(xml2_resident_stats_test test/resident_stats_test.cpp src/resident_stats.cpp src/patch_transaction.cpp "${CMAKE_CURRENT_BINARY_DIR}/image_fixture.asm")
target_include_directories(xml2_resident_stats_test PRIVATE src)
target_compile_options(xml2_resident_stats_test PRIVATE /UNDEBUG)
target_link_options(xml2_resident_stats_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
# Diagnostics: fixed-storage sampler rules and native observer semantics (no game launches).
add_executable(xml2_pool_counters_test test/pool_counters_test.cpp)
target_include_directories(xml2_pool_counters_test PRIVATE src)
target_compile_options(xml2_pool_counters_test PRIVATE /UNDEBUG)

add_executable(xml2_pool_counters_runtime_test test/pool_counters_runtime_test.cpp src/patch_transaction.cpp "${CMAKE_CURRENT_BINARY_DIR}/test_mode_fixture.asm")
target_include_directories(xml2_pool_counters_runtime_test PRIVATE src)
target_link_options(xml2_pool_counters_runtime_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

add_executable(xml2_pool_counter_observers_test test/pool_counter_observers_test.cpp src/patch_transaction.cpp "${CMAKE_CURRENT_BINARY_DIR}/test_mode_fixture.asm")
target_include_directories(xml2_pool_counter_observers_test PRIVATE src)
target_link_options(xml2_pool_counter_observers_test PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)
