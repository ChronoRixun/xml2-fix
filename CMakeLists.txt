cmake_minimum_required(VERSION 3.21)
project(xml2_fix VERSION 1.2.0 LANGUAGES CXX RC)

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

# The fix: a drop-in dinput.dll for the game folder.
add_library(xml2_fix SHARED
	src/exports.cpp
	src/gamepad_fix.cpp
	src/pad_profile.cpp
	src/pad_bindings.cpp
	src/xinput_pad.cpp
	src/iat_hook.cpp
	src/openspy_redirect.cpp
	src/net_trace.cpp
	src/mod_loader.cpp
	src/display.cpp
	src/frame_rate.cpp
	src/options_menu.cpp
	src/new_game.cpp
	src/limits.cpp
	src/resolution_list.cpp
	src/frame_capture.cpp
	src/test_input.cpp
	src/log.cpp
	src/dinput.def
	src/version.rc)
target_compile_definitions(xml2_fix PRIVATE FIX_LOG_FILE=L"xml2-fix.log"
	FIX_VERSION="${PROJECT_VERSION}"
	FIX_VERSION_NUMBERS=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0)
target_link_libraries(xml2_fix PRIVATE dxguid ws2_32 winmm) # winmm: timeBeginPeriod for the frame limiter's fallback timer
# Output is dinput.dll; its import library gets a different name so it can't shadow the Windows SDK's.
set_target_properties(xml2_fix PROPERTIES OUTPUT_NAME dinput ARCHIVE_OUTPUT_NAME xml2_fix)

# Checks the built DLL the way the game loads it (imported from next to the executable).
add_executable(xml2_test test/xml2_test.cpp src/frame_capture.cpp) # frame_capture: the back buffer copy, checked on a device of its own
target_compile_definitions(xml2_test PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS)
target_include_directories(xml2_test PRIVATE src) # display_rules.hpp, frame_rate_rules.hpp, options_menu_rules.hpp, resolution_rules.hpp, limits_rules.hpp, test_input_rules.hpp, image_file.hpp: the decisions, checked without the game
target_link_libraries(xml2_test PRIVATE xml2_fix dinput8.lib dxguid xinput ws2_32 winmm) # dinput8.lib: only for c_dfDIJoystick(2); winmm: timeBeginPeriod in the present timing
