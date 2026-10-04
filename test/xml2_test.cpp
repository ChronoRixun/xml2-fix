// Runs next to the built dinput.dll and imports it, so Windows loads the fix exactly the way
// it does for X-Men Legends II, then reads the pad both ways the game does:
//   - the engine's DirectInput 7: DirectInputCreateEx, CreateDevice + QueryInterface to
//     IDirectInputDevice2A, ranges set by offset, c_dfDIJoystick;
//   - the game's own DirectInput 8: dinput8.dll loaded from the system folder by full path,
//     EnumObjects, ranges set by object id, c_dfDIJoystick2.
// With an Xbox-compatible pad connected, both must see a Logitech Dual Action. Also checks
// that GameSpy host lookups resolve through OpenSpy ([Online] Server's rules in online_rules.hpp,
// and its 127.0.0.1 in the pipe child), that this PC's own name resolves with the address Windows
// reaches the internet from first ([Online] LocalIP's rules in local_ip_rules.hpp, auto here and an
// address that isn't this PC's in the pipe child), and the display fix's decisions: the
// rules in display_rules.hpp, frame_rate_rules.hpp and options_menu_rules.hpp against fixed
// inputs (the frame cap's patch bytes and the in-game options' call sites against a copy of
// XMen2.exe when one is at hand), the Video options list the fix would build from this PC's
// Direct3D 8 modes, and whether a windowed Direct3D 8 present waits for the vertical blank
// here. The engine limit adjuster (limits_rules.hpp) is checked on its ini rules and layouts, and
// against a copy of XMen2.exe mapped in memory: every patch site's retail bytes, the patch applied
// to that copy, and the patched constructors and allocators run on blocks of the test's own. The
// forced parties' script functions (forced_teams_rules.hpp) are checked on their rules, run on a
// game of the test's own (arguments read by the game's own getter, costumes through the game's own
// registry accessors when a copy of XMen2.exe is at hand), and every byte they rely on, the table
// they register and its two operands against that copy. [Game] PostgameScript (postgame_rules.hpp)
// is checked on its name rules, every byte it relies on and its one operand write against that
// copy, and the game's own console word reader on the line. [Game] MainMenuItems (main_menu_rules.hpp) is checked on
// its list rules, every byte it relies on, the completeness of its push table in MAIN_MENU's code and exactly the
// operands it writes against that copy. [Game] XPCurve (xp_curve_rules.hpp) is checked on its value rules, XML1's
// tables against default.xbe's formulas worked out again, the level and kill lookups at their edges and the kill
// split, then every byte it relies on, exactly the bytes it writes and the patched lookup, cap and kill XP jump run
// on that copy. The test input pipe is checked on its rules, on a
// Direct3D 8 device of the test's own (the back buffer copy behind "screenshot"), and end to
// end in a child process started with an xml2-fix.ini that turns the pipe on under a name of its
// own ([Test] PipeName, so a running game's pipe is never touched): it creates the
// keyboard device the way XMen2.exe does and sees the pipe's keys in it.
//
//   xml2_test.exe          run the checks
//   xml2_test.exe --live   also show live pad input, as the game sees it, for 20 seconds
//   xml2_test.exe --discord-live   only a round trip with this PC's Discord: a test presence as X-Men
//                          Legends II for four seconds, then cleared (discord_rules.hpp's decisions,
//                          its reads of the game's memory over blocks of the test's own and its bytes
//                          against XMen2.exe are part of the normal run)

#define DIRECTINPUT_VERSION 0x0800
#include <WinSock2.h>
#include <Windows.h>
#include <dinput.h>
#include <timeapi.h>
#include <Xinput.h>

#include "discord_ipc.hpp"
#include "discord_rules.hpp"
#include "display_rules.hpp"
#include "conversations_rules.hpp"
#include "forced_teams_rules.hpp"
#include "frame_capture.hpp"
#include "frame_rate_rules.hpp"
#include "game_version_rules.hpp"
#include "iat_hook.hpp"
#include "image_file.hpp"
#include "ini_rules.hpp"
#include "limits_rules.hpp"
#include "geometry_sharing_rules.hpp"
#include "local_ip.hpp"
#include "local_ip_rules.hpp"
#include "main_menu_rules.hpp"
#include "mod_order.hpp"
#include "new_game.hpp"
#include "new_game_plus_rules.hpp"
#include "online_rules.hpp"
#include "options_menu_rules.hpp"
#include "pad_input_rules.hpp"
#include "pad_profile.hpp"
#include "pad_prompts_rules.hpp"
#include "postgame_rules.hpp"
#include "resolution_rules.hpp"
#include "review_menu_rules.hpp"
#include "test_input_rules.hpp"
#include "test_state_rules.hpp"
#include "virtual_pad_rules.hpp"
#include "xp_curve_rules.hpp"
#include "window_title_rules.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

extern "C" HRESULT WINAPI DirectInputCreateEx(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN); // imported from the fix

namespace
{
	int failures = 0;

#define CHECK(expr)                                                              \
	do                                                                           \
	{                                                                            \
		if (expr) std::printf("  ok    %s\n", #expr);                            \
		else { std::printf("  FAIL  %s  (line %d)\n", #expr, __LINE__); ++failures; } \
	} while (false)

	constexpr DWORD dual_action = 0xC216046D; // PID << 16 | VID, as in guidProduct.Data1

	// IDirectInput7A / IDirectInputDevice2A, by vtable slot (the SDK only declares them for DirectInput < 8).
	using create_device_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, void**, LPUNKNOWN);
	using enum_devices_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD);

	template <typename T>
	T slot(void* object, const int index)
	{
		return reinterpret_cast<T>((*static_cast<void***>(object))[index]);
	}

	std::string read_file(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	}

	std::filesystem::path module_dir()
	{
		wchar_t buffer[MAX_PATH]{};
		GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		return std::filesystem::path(buffer).parent_path();
	}

	int connected_xinput_pads()
	{
		int count = 0;
		for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index)
		{
			XINPUT_STATE state{};
			count += XInputGetState(index, &state) == ERROR_SUCCESS;
		}
		return count;
	}

	struct found_device
	{
		GUID instance;
		DWORD product;
		std::string name;
	};

	BOOL CALLBACK collect(LPCDIDEVICEINSTANCEA instance, LPVOID user)
	{
		static_cast<std::vector<found_device>*>(user)->push_back({instance->guidInstance, instance->guidProduct.Data1, instance->tszProductName});
		return DIENUM_CONTINUE;
	}

	void print_state(const DIJOYSTATE& state)
	{
		std::printf("X=%5ld Y=%5ld Z=%5ld Rz=%5ld POV=%5ld buttons=", state.lX, state.lY, state.lZ, state.lRz, static_cast<long>(state.rgdwPOV[0]));
		for (int b = 0; b < 12; ++b) std::printf("%d", state.rgbButtons[b] ? 1 : 0);
	}

	// The engine: libIGDisplay.dll's igWin32ControllerManager::createControllers.
	void check_engine_path()
	{
		std::printf("engine (DirectInput 7, what libIGDisplay.dll does)\n");
		void* input = nullptr;
		CHECK(SUCCEEDED(DirectInputCreateEx(GetModuleHandleW(nullptr), 0x0700, IID_IDirectInput7A, &input, nullptr)));
		if (!input) return;

		std::vector<found_device> devices;
		slot<enum_devices_t>(input, 4)(input, 4 /* DIDEVTYPE_JOYSTICK */, &collect, &devices, DIEDFL_ATTACHEDONLY);
		for (const auto& d : devices) std::printf("  info  %s enumerates as %04lX:%04lX\n", d.name.c_str(), d.product & 0xFFFF, d.product >> 16);
		const auto pad = std::ranges::find_if(devices, [](const auto& d) { return d.product == dual_action; });
		CHECK(pad != devices.end());
		if (pad == devices.end()) return;
		CHECK(pad->name == "Logitech Dual Action");

		IUnknown* created = nullptr;
		CHECK(SUCCEEDED(slot<create_device_t>(input, 3)(input, pad->instance, reinterpret_cast<void**>(&created), nullptr)));
		void* device = nullptr;
		CHECK(SUCCEEDED(created->QueryInterface(IID_IDirectInputDevice2A, &device)));
		created->Release();
		auto* dev = static_cast<IDirectInputDevice8A*>(device); // the first 16 slots match IDirectInputDevice2A

		CHECK(SUCCEEDED(dev->SetDataFormat(&c_dfDIJoystick)));
		CHECK(SUCCEEDED(dev->SetCooperativeLevel(GetConsoleWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)));
		bool all_ranges = true;
		for (DWORD offset = 0; offset <= DIJOFS_RZ; offset += sizeof(LONG))
		{
			DIPROPRANGE range{{sizeof(DIPROPRANGE), sizeof(DIPROPHEADER), offset, DIPH_BYOFFSET}, -1000, 1000};
			all_ranges &= SUCCEEDED(dev->SetProperty(DIPROP_RANGE, &range.diph));
		}
		CHECK(all_ranges);

		DIDEVCAPS caps{sizeof(caps)};
		CHECK(SUCCEEDED(dev->GetCapabilities(&caps)));
		CHECK(caps.dwAxes == 4 && caps.dwButtons == 12 && caps.dwPOVs == 1);

		DIDEVICEINSTANCEA info{sizeof(info)};
		CHECK(SUCCEEDED(dev->GetDeviceInfo(&info)) && info.guidProduct.Data1 == dual_action);

		CHECK(SUCCEEDED(dev->Acquire()));
		DIJOYSTATE state{};
		CHECK(SUCCEEDED(dev->GetDeviceState(sizeof(state), &state)));
		std::printf("  info  idle state: ");
		print_state(state);
		std::printf("\n");
		CHECK(std::abs(state.lX) < 300 && std::abs(state.lY) < 300 && std::abs(state.lZ) < 300 && std::abs(state.lRz) < 300);
		CHECK(state.lRx == 0 && state.lRy == 0);
		dev->Release();
		static_cast<IUnknown*>(input)->Release();
	}

	struct object_list
	{
		IDirectInputDevice8A* device;
		std::vector<std::string> names;
		bool ranges_ok = true;
	};

	// XMen2.exe's EnumObjects callback: every object gets a -1000..1000 range, by id.
	BOOL CALLBACK range_object(LPCDIDEVICEOBJECTINSTANCEA object, LPVOID user)
	{
		auto* list = static_cast<object_list*>(user);
		list->names.emplace_back(object->tszName);
		if (DIDFT_GETTYPE(object->dwType) & DIDFT_AXIS)
		{
			DIPROPRANGE range{{sizeof(DIPROPRANGE), sizeof(DIPROPHEADER), object->dwType, DIPH_BYID}, -1000, 1000};
			list->ranges_ok &= SUCCEEDED(list->device->SetProperty(DIPROP_RANGE, &range.diph));
		}
		return DIENUM_CONTINUE;
	}

	void live(IDirectInputDevice8A* device)
	{
		std::printf("\n  live input for 20 seconds, as a Dual Action\n");
		std::printf("  buttons: X A B Y LB RB LT RT Back Start LS RS  (Dual Action 1-12)\n");
		for (int i = 0; i < 200; ++i)
		{
			DIJOYSTATE2 state{};
			device->Poll();
			device->GetDeviceState(sizeof(state), &state);
			std::printf("\r  ");
			print_state(*reinterpret_cast<DIJOYSTATE*>(&state));
			std::fflush(stdout);
			Sleep(100);
		}
		std::printf("\n");
	}

	// The game: XMen2.exe loads the system's dinput8.dll itself and binds actions to objects.
	void check_game_path(const bool show_live)
	{
		std::printf("game (DirectInput 8, what XMen2.exe does)\n");
		wchar_t folder[MAX_PATH]{};
		GetSystemDirectoryW(folder, MAX_PATH);
		const auto dinput8 = LoadLibraryW((std::wstring(folder) + L"\\dinput8.dll").c_str());
		CHECK(dinput8 != nullptr);
		const auto create = reinterpret_cast<decltype(&DirectInput8Create)>(GetProcAddress(dinput8, "DirectInput8Create"));
		HMODULE owner = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(create), &owner);
		CHECK(create != nullptr && owner == GetModuleHandleW(L"dinput.dll")); // handed the fix's wrapper

		IDirectInput8A* input = nullptr;
		CHECK(SUCCEEDED(create(GetModuleHandleW(nullptr), 0x0800, IID_IDirectInput8A, reinterpret_cast<void**>(&input), nullptr)));
		if (!input) return;

		std::vector<found_device> devices;
		input->EnumDevices(DI8DEVCLASS_GAMECTRL, &collect, &devices, DIEDFL_ATTACHEDONLY);
		const auto pad = std::ranges::find_if(devices, [](const auto& d) { return d.product == dual_action; });
		CHECK(pad != devices.end());
		if (pad == devices.end()) return;
		CHECK(pad->name == "Logitech Dual Action");

		IDirectInputDevice8A* device = nullptr;
		CHECK(SUCCEEDED(input->CreateDevice(pad->instance, &device, nullptr)));
		CHECK(SUCCEEDED(device->SetDataFormat(&c_dfDIJoystick2)));
		CHECK(SUCCEEDED(device->SetCooperativeLevel(GetConsoleWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)));

		object_list objects{device};
		CHECK(SUCCEEDED(device->EnumObjects(&range_object, &objects, DIDFT_ALL)));
		std::printf("  info  objects:");
		for (const auto& name : objects.names) std::printf(" [%s]", name.c_str());
		std::printf("\n");
		CHECK(objects.names.size() == 17);
		CHECK(std::ranges::find(objects.names, "Z Rotation") != objects.names.end());
		CHECK(objects.ranges_ok);

		DIPROPRANGE rz{{sizeof(DIPROPRANGE), sizeof(DIPROPHEADER), DIJOFS_RZ, DIPH_BYOFFSET}};
		CHECK(SUCCEEDED(device->GetProperty(DIPROP_RANGE, &rz.diph)) && rz.lMin == -1000 && rz.lMax == 1000);

		CHECK(SUCCEEDED(device->Acquire()));
		DIJOYSTATE2 state{};
		CHECK(SUCCEEDED(device->GetDeviceState(sizeof(state), &state)));
		std::printf("  info  idle state: ");
		print_state(*reinterpret_cast<DIJOYSTATE*>(&state));
		std::printf("\n");
		CHECK(std::abs(state.lZ) < 300 && std::abs(state.lRz) < 300);
		CHECK(std::all_of(state.rgbButtons + 12, state.rgbButtons + 128, [](BYTE b) { return b == 0; }));

		if (show_live) live(device);
		device->Release();
		input->Release();
	}

	// The display fix's decisions, on fixed inputs (display_rules.hpp).
	void check_display_rules()
	{
		using namespace display_rules;
		std::printf("display rules ([Display] in xml2-fix.ini)\n");

		CHECK(parse_mode("Borderless") == mode::borderless);
		CHECK(parse_mode("windowed") == mode::windowed);
		CHECK(parse_mode("FULLSCREEN") == mode::fullscreen);
		CHECK(parse_mode("") == mode::stock && parse_mode("sideways") == mode::stock);
		// The mouse messages a window of ours drops while another window has the focus: the client-area
		// ones the game's menus act on, and nothing else (keys, activation, painting, non-client).
		CHECK(is_pointer_message(WM_MOUSEMOVE) && is_pointer_message(WM_LBUTTONDOWN) && is_pointer_message(WM_LBUTTONUP) && is_pointer_message(WM_RBUTTONDOWN));
		CHECK(is_pointer_message(WM_MOUSEWHEEL) && is_pointer_message(0x20e /* WM_MOUSEHWHEEL */) && is_pointer_message(WM_XBUTTONUP) && is_pointer_message(WM_MBUTTONDBLCLK));
		CHECK(!is_pointer_message(WM_KEYUP) && !is_pointer_message(WM_KEYDOWN) && !is_pointer_message(WM_ACTIVATE) && !is_pointer_message(WM_MOUSEACTIVATE));
		CHECK(!is_pointer_message(WM_NCMOUSEMOVE) && !is_pointer_message(WM_SETCURSOR) && !is_pointer_message(WM_PAINT) && !is_pointer_message(0x20f) && !is_pointer_message(0x1ff));
		CHECK(parse_resolution_list("") == resolution_list::stock && parse_resolution_list("ALL") == resolution_list::all && parse_resolution_list("game") == resolution_list::game);
		CHECK(!parse_resolution_list("off").has_value() && !parse_resolution_list("64").has_value());
		CHECK(std::string(name(resolution_list::game)) == "game" && std::string(name(resolution_list::all)) == "all" && std::string(name(resolution_list::stock)) == "stock");
		// Default-off: no keys = the game's own list in its own mode (no mode-list hooks, no relocation);
		// the fix's list in its own modes, as before; ResolutionList asks for it in the game's mode too.
		CHECK(options{}.resolutions == resolution_list::stock && !fix_builds_mode_list(options{}));
		{
			options with_mode;
			with_mode.window_mode = mode::fullscreen;
			options with_list;
			with_list.resolutions = resolution_list::game;
			CHECK(fix_builds_mode_list(with_mode) && fix_builds_mode_list(with_list));
		}

		const size desktop{2560, 1440};
		const d3d8::display_mode desktop_mode{2560, 1440, 180, d3d8::format_x8r8g8b8};
		options borderless;
		borderless.window_mode = mode::borderless;
		options windowed;
		windowed.window_mode = mode::windowed;
		options fullscreen;
		fullscreen.window_mode = mode::fullscreen;

		// What the game is told its resolution is.
		CHECK((resolution_override(borderless, desktop) == size{2560, 1440}));
		CHECK(!resolution_override(windowed, desktop).has_value());
		CHECK(!resolution_override(fullscreen, desktop).has_value());
		windowed.width = 1280;
		windowed.height = 720;
		CHECK((resolution_override(windowed, desktop) == size{1280, 720}));

		// The Video options list: one entry per size, nothing below 640x480, the desktop added,
		// ascending, and no more than the game's 20 slots (the smallest go).
		std::vector<d3d8::display_mode> adapter;
		for (const UINT width : {320u, 640u, 720u, 800u, 1024u, 1152u, 1176u, 1280u, 1360u, 1366u, 1440u, 1600u, 1680u, 1768u, 1920u, 2048u, 2560u})
		{
			for (const UINT height : {240u, 480u, 600u, 720u, 768u, 900u, 1080u})
			{
				adapter.push_back({width, height, 60, d3d8::format_x8r8g8b8});
				adapter.push_back({width, height, 144, d3d8::format_x8r8g8b8});
				adapter.push_back({width, height, 60, 23 /* R5G6B5 */});
			}
		}
		const auto list = curate_modes(adapter, desktop, 180, std::nullopt, 20);
		CHECK(list.size() == 20);
		CHECK(list.back().width == 2560 && list.back().height == 1440 && list.back().refresh_rate == 180);
		CHECK(std::ranges::is_sorted(list, [](const auto& a, const auto& b) { return a.width != b.width ? a.width < b.width : a.height < b.height; }));
		CHECK(std::ranges::none_of(list, [](const auto& m) { return m.width < 640 || m.height < 480; }));
		CHECK(std::ranges::count_if(list, [](const auto& m) { return m.width == 1920 && m.height == 1080; }) == 1);
		const auto few = curate_modes({{800, 600, 60, 22}, {1024, 768, 60, 22}}, desktop, 180, size{1280, 720}, 20);
		CHECK(few.size() == 4 && few[2].width == 1280 && few[3].width == 2560);

		// Present parameters: the engine's exclusive fullscreen ones at the registry resolution.
		const d3d8::present_parameters stock{1920, 1080, d3d8::format_x8r8g8b8, 1, 4, d3d8::swap_discard, nullptr, FALSE, TRUE, d3d8::format_d24s8, 1, 0, 0x80000000};
		const auto msaa_ok = [](DWORD, DWORD) { return true; };
		const auto msaa_no = [](DWORD, DWORD) { return false; };
		const auto format_ok = [](DWORD) { return true; };
		const auto interval_ok = [](UINT) { return true; };
		std::string notes;

		auto pp = rewrite_present(stock, borderless, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(pp.windowed == TRUE && pp.fullscreen_refresh_rate == 0 && pp.fullscreen_presentation_interval == 0);
		CHECK(pp.back_buffer_width == 1920 && pp.back_buffer_height == 1080 && pp.multi_sample_type == 4 && pp.swap_effect == d3d8::swap_discard);
		pp = rewrite_present(stock, windowed, desktop_mode, msaa_no, format_ok, interval_ok, notes);
		CHECK(pp.windowed == TRUE && pp.multi_sample_type == d3d8::multisample_none && !notes.empty());
		auto copy_vsync = stock;
		copy_vsync.swap_effect = d3d8::swap_copy_vsync;
		copy_vsync.back_buffer_count = 2;
		pp = rewrite_present(copy_vsync, borderless, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(pp.multi_sample_type == d3d8::multisample_none && pp.swap_effect == d3d8::swap_copy_vsync && pp.back_buffer_count == 1);
		pp = rewrite_present(stock, borderless, desktop_mode, msaa_ok, [](DWORD) { return false; }, interval_ok, notes);
		CHECK(pp.back_buffer_format == d3d8::format_x8r8g8b8); // already the desktop's: not checked, not changed
		auto sixteen_bit = stock;
		sixteen_bit.back_buffer_format = 23;
		pp = rewrite_present(sixteen_bit, borderless, desktop_mode, msaa_ok, [](DWORD) { return false; }, interval_ok, notes);
		CHECK(pp.back_buffer_format == d3d8::format_x8r8g8b8);
		pp = rewrite_present(stock, fullscreen, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(pp.windowed == FALSE && pp.fullscreen_refresh_rate == 0 && pp.multi_sample_type == 4); // not the desktop size: untouched
		auto native = stock;
		native.back_buffer_width = 2560;
		native.back_buffer_height = 1440;
		pp = rewrite_present(native, fullscreen, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(pp.windowed == FALSE && pp.fullscreen_refresh_rate == 180);
		options stock_options;
		pp = rewrite_present(stock, stock_options, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(std::memcmp(&pp, &stock, sizeof(pp)) == 0);

		// VSync. Fullscreen (the game's own mode and Mode=fullscreen): the presentation interval,
		// when the adapter offers it. Windowed: the COPY_VSYNC swap effect, one back buffer, no
		// multisampling; off = the discard swap effect.
		options vsync_on;
		vsync_on.vsync = true;
		pp = rewrite_present(stock, vsync_on, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(pp.windowed == FALSE && pp.fullscreen_presentation_interval == d3d8::present_interval_one && pp.multi_sample_type == 4);
		CHECK(notes.find("vsync: on") != std::string::npos);
		options vsync_off;
		vsync_off.vsync = false;
		auto synced = stock;
		synced.fullscreen_presentation_interval = d3d8::present_interval_one; // alchemy.ini presentationInterval=1
		pp = rewrite_present(synced, vsync_off, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(pp.fullscreen_presentation_interval == d3d8::present_interval_immediate);
		pp = rewrite_present(stock, vsync_off, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(std::memcmp(&pp, &stock, sizeof(pp)) == 0 && notes.empty()); // already immediate
		pp = rewrite_present(stock, vsync_on, desktop_mode, msaa_ok, format_ok, [](UINT) { return false; }, notes);
		CHECK(pp.fullscreen_presentation_interval == 0x80000000 && notes.find("doesn't offer") != std::string::npos);
		vsync_on.window_mode = mode::fullscreen;
		pp = rewrite_present(native, vsync_on, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(pp.fullscreen_refresh_rate == 180 && pp.fullscreen_presentation_interval == d3d8::present_interval_one);
		// In a window, on = paced at the refresh rate by the limiter (never copy_vsync), the device unchanged.
		vsync_on.window_mode = mode::borderless;
		pp = rewrite_present(stock, vsync_on, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(pp.windowed == TRUE && pp.swap_effect == d3d8::swap_discard && pp.multi_sample_type == 4 && pp.fullscreen_presentation_interval == d3d8::present_interval_default);
		CHECK(notes.find("paced at the desktop's refresh rate") != std::string::npos);
		vsync_off.window_mode = mode::windowed;
		pp = rewrite_present(copy_vsync, vsync_off, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(pp.swap_effect == d3d8::swap_discard && pp.multi_sample_type == 4); // the engine's own windowedVSync undone
		pp = rewrite_present(stock, vsync_off, desktop_mode, msaa_ok, format_ok, interval_ok, notes);
		CHECK(pp.swap_effect == d3d8::swap_discard && notes.empty());

		// Window placement: the engine asks for a maximised popup at 0,0 (style 0x85000000).
		const RECT monitor{0, 0, 2560, 1440};
		const RECT work{0, 0, 2560, 1392};
		auto place = place_window(borderless, monitor, work, {2560, 1440}, 0x85000000, WS_EX_TOPMOST);
		CHECK(place.style == (WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN) && place.ex_style == 0);
		CHECK(std::memcmp(&place.rect, &monitor, sizeof(RECT)) == 0);
		borderless.topmost = true;
		CHECK(place_window(borderless, monitor, work, {2560, 1440}, 0x85000000, 0).ex_style == WS_EX_TOPMOST);

		place = place_window(windowed, monitor, work, {1280, 720}, 0x85000000 | WS_VISIBLE, 0);
		CHECK((place.style & (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE)) == (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE));
		CHECK(!(place.style & (WS_THICKFRAME | WS_MAXIMIZEBOX | WS_POPUP)));
		RECT frame{0, 0, 0, 0};
		AdjustWindowRectEx(&frame, place.style, FALSE, place.ex_style);
		CHECK(place.rect.right - place.rect.left - (frame.right - frame.left) == 1280);
		CHECK(place.rect.bottom - place.rect.top - (frame.bottom - frame.top) == 720);
		CHECK(std::abs((place.rect.left + place.rect.right) / 2 - 1280) <= 1 && std::abs((place.rect.top + place.rect.bottom) / 2 - 696) <= 1);
		place = place_window(windowed, monitor, work, {2560, 1440}, 0x85000000, 0); // bigger than the work area: caption stays on screen
		CHECK(place.rect.left == 0 && place.rect.top == 0);
	}

	void check_d3d8_capture(void* d3d);
	void check_windowed_presents(void* d3d, const d3d8::display_mode& desktop);

	// A file offset for a virtual address of a PE image, or nothing.
	std::optional<size_t> file_offset(const std::string& image, const DWORD rva)
	{
		if (image.size() < 0x40) return std::nullopt;
		const auto u16 = [&](const size_t at) { return static_cast<DWORD>(static_cast<unsigned char>(image[at]) | (static_cast<unsigned char>(image[at + 1]) << 8)); };
		const auto u32 = [&](const size_t at) { return u16(at) | (u16(at + 2) << 16); };
		const size_t pe = u32(0x3c);
		if (pe + 24 > image.size() || image.compare(pe, 4, "PE\0\0", 4) != 0) return std::nullopt;
		const size_t sections = u16(pe + 6);
		const size_t section_table = pe + 24 + u16(pe + 20);
		for (size_t i = 0; i < sections; ++i)
		{
			const size_t header = section_table + i * 40;
			if (header + 40 > image.size()) return std::nullopt;
			const DWORD virtual_size = u32(header + 8), virtual_address = u32(header + 12), raw_size = u32(header + 16), raw_offset = u32(header + 20);
			if (rva >= virtual_address && rva < virtual_address + std::max(virtual_size, raw_size))
			{
				return raw_offset + (rva - virtual_address);
			}
		}
		return std::nullopt;
	}

	// A copy of XMen2.exe to check the patch sites against: the research copy in the repository
	// (docs/research, git-ignored), or the installed game, read only. Neither is on the CI runner.
	std::optional<std::string> game_executable()
	{
		std::vector<std::filesystem::path> candidates;
		for (auto dir = module_dir(); !dir.empty() && dir != dir.root_path(); dir = dir.parent_path())
		{
			candidates.push_back(dir / "docs" / "research" / "XMen2.exe");
		}
		candidates.emplace_back(L"D:\\Games\\X-Men Legends II\\XMen2.exe");
		for (const auto& path : candidates)
		{
			std::error_code ignored;
			if (std::filesystem::is_regular_file(path, ignored))
			{
				std::printf("  info  reading %s\n", path.string().c_str());
				return read_file(path);
			}
		}
		return std::nullopt;
	}

	// The frame limiter's decisions (frame_rate_rules.hpp): the ini keys, the target, the bytes of the
	// game's own 60 fps spin, the pacing and the fps count.
	void check_frame_rate_rules()
	{
		using namespace frame_rate_rules;
		std::printf("frame rate rules ([Display] FrameRate and VSync)\n");

		const cap stock_cap{};
		const cap unlimited{cap::kind::unlimited, 0};
		const cap refresh{cap::kind::refresh, 0};
		const cap fixed_144{cap::kind::fixed, 144};
		CHECK(parse_frame_rate("") == stock_cap);
		CHECK(parse_frame_rate("0") == unlimited && parse_frame_rate("Off") == unlimited && parse_frame_rate("unlimited") == unlimited);
		CHECK(parse_frame_rate("refresh") == refresh && parse_frame_rate("REFRESH") == refresh);
		CHECK(parse_frame_rate("144") == fixed_144 && parse_frame_rate("30")->fps == 30 && parse_frame_rate("1000")->fps == 1000);
		CHECK(!parse_frame_rate("5") && !parse_frame_rate("1001") && !parse_frame_rate("60fps") && !parse_frame_rate("-60") && !parse_frame_rate("6e1") && !parse_frame_rate("00060"));
		CHECK(parse_vsync("") == vsync::untouched && parse_vsync("1") == vsync::on && parse_vsync("On") == vsync::on && parse_vsync("true") == vsync::on);
		CHECK(parse_vsync("0") == vsync::off && parse_vsync("off") == vsync::off && parse_vsync("maybe") == vsync::invalid && parse_vsync("2") == vsync::invalid);

		CHECK(target_fps(stock_cap, 180) == 0 && target_fps(unlimited, 180) == 0);
		CHECK(target_fps(fixed_144, 180) == 144);
		CHECK(target_fps(refresh, 180) == 180 && target_fps(refresh, 0) == 60);
		CHECK(!disables_stock_cap(stock_cap) && disables_stock_cap(unlimited) && disables_stock_cap(fixed_144));
		// VSync=1 in a window: paced at the refresh rate, never above FrameRate, never without one.
		CHECK(effective_target(stock_cap, 180, true) == 0 && effective_target(stock_cap, 180, false) == 0);
		CHECK(effective_target(unlimited, 180, false) == 0 && effective_target(unlimited, 180, true) == 180 && effective_target(unlimited, 0, true) == 60);
		const cap fixed_240{cap::kind::fixed, 240};
		CHECK(effective_target(fixed_144, 180, true) == 144 && effective_target(fixed_240, 180, true) == 180 && effective_target(fixed_240, 180, false) == 240);
		CHECK(effective_target(refresh, 180, true) == 180 && effective_target(refresh, 180, false) == 180);
		CHECK(describe(refresh, 180) == "the desktop's refresh rate (180 fps)" && describe(stock_cap, 0) == "the game's own 60 fps cap");
		CHECK(describe(fixed_144, 0) == "144 fps" && describe(unlimited, 0) == "unlimited");

		// The patch: the checked bytes hold the two frame-time constants, the replacement zeroes the 1/60.
		const auto& patch = stock_cap_patch;
		CHECK(patch.offset + patch.replacement.size() <= patch.expected.size());
		std::array<std::uint8_t, 4> sixty{}, thirty{};
		std::copy_n(patch.expected.begin() + static_cast<std::ptrdiff_t>(patch.offset), 4, sixty.begin());
		std::copy_n(patch.expected.begin() + 3, 4, thirty.begin());
		CHECK(std::bit_cast<float>(sixty) == 1.0f / 60.0f && std::bit_cast<float>(thirty) == 1.0f / 30.0f);
		CHECK(std::bit_cast<float>(patch.replacement) == 0.0f);
		CHECK(patch.expected[0] == 0xC7 && patch.expected[9] == 0xC7 && patch.expected[7] == 0xEB && patch.expected[8] == 0x07); // mov [esi+18], imm32; jmp +7; mov
		CHECK(matches(patch, patch.expected.data()));
		auto other = patch.expected;
		other[15] ^= 1;
		CHECK(!matches(patch, other.data()));
		// Undoing it (the rows back to the game's own cap): only over exactly the patched bytes.
		auto patched = patch.expected;
		std::copy(patch.replacement.begin(), patch.replacement.end(), patched.begin() + static_cast<std::ptrdiff_t>(patch.offset));
		CHECK(matches_patched(patch, patched.data()) && !matches_patched(patch, patch.expected.data()) && !matches(patch, patched.data()));
		patched[3] ^= 1; // the 1/30 constant changed by someone else: not ours to touch
		CHECK(!matches_patched(patch, patched.data()));
		// Live changes: back to the game's cap = no pacing once its spin is back, 60 by the fix if it couldn't be.
		CHECK(live_target(stock_cap, 180, false, false) == 0 && live_target(stock_cap, 180, true, false) == 0);
		CHECK(live_target(stock_cap, 180, false, true) == 60 && live_target(stock_cap, 180, true, true) == 60);
		CHECK(live_target(fixed_144, 180, false, true) == 144 && live_target(unlimited, 180, false, true) == 0 && live_target(unlimited, 180, true, true) == 180);
		if (const auto exe = game_executable())
		{
			const auto at = file_offset(*exe, patch.rva);
			CHECK(at.has_value() && *at + patch.expected.size() <= exe->size());
			if (at && *at + patch.expected.size() <= exe->size())
			{
				CHECK(matches(patch, reinterpret_cast<const std::uint8_t*>(exe->data() + *at)));
				CHECK(*at == 0x1dab); // .text is mapped 1:1 in this build
			}
		}
		else
		{
			std::printf("  skip  no XMen2.exe to check the patch bytes against\n");
		}

		// Pacing, on a 1 kHz clock with a 100-tick interval: cadence kept when early, restarted when late.
		pacer pace;
		CHECK(pace.next(5) == 5); // nothing to pace
		pace.set_interval(100);
		CHECK(pace.next(1000) == 1000); // the first frame ends at once
		CHECK(pace.next(1010) == 1100);
		CHECK(pace.next(1100) == 1200);
		CHECK(pace.next(1190) == 1300);
		CHECK(pace.next(1500) == 1500); // late: no wait, no catch-up burst
		CHECK(pace.next(1550) == 1600);
		CHECK(ticks_for_fps(60, 10'000'000) == 166'666 && ticks_for_fps(0, 10'000'000) == 0);
		CHECK(ticks_for_ms(0.6, 10'000'000) == 6000);

		// The wait: a timer for all but the margin, then a spin; short waits spin only.
		constexpr LONGLONG mhz10 = 10'000'000, margin = 6000; // 0.6 ms
		auto plan = plan_wait(50'000, mhz10, margin);         // 5 ms
		CHECK(plan.spin && plan.timer_100ns == 44'000);       // 4.4 ms on the timer
		plan = plan_wait(8'000, mhz10, margin);               // 0.8 ms
		CHECK(plan.spin && plan.timer_100ns == 0);
		plan = plan_wait(12'001, mhz10, margin);
		CHECK(plan.spin && plan.timer_100ns == 6'001);
		CHECK(!plan_wait(0, mhz10, margin).spin && !plan_wait(-5, mhz10, margin).spin && !plan_wait(100, 0, margin).spin);

		// Counting: 100 frames in the second after the first one -> 100.0 fps; then 2 frames in a second.
		fps_counter counter;
		bool completed = false;
		CHECK(!counter.count(0, 1000) && counter.fps_x10() == 0);
		for (LONGLONG t = 10; t <= 1000; t += 10) completed = counter.count(t, 1000);
		CHECK(completed && counter.fps_x10() == 1000);
		CHECK(!counter.count(1500, 1000) && !counter.count(1999, 1000) && counter.count(2000, 1000) && counter.fps_x10() == 30);
		CHECK(fps_text(599) == "59.9" && fps_text(1800) == "180.0" && fps_text(0) == "0.0");

		// The wait against the real clock, the way frame_rate.cpp does it (a high-resolution waitable
		// timer for the bulk, a spin for the last 0.6 ms): 60 frames paced at 120 fps take 0.5 s.
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		const auto now = []
		{
			LARGE_INTEGER count{};
			QueryPerformanceCounter(&count);
			return count.QuadPart;
		};
		const HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
		std::printf("  info  high-resolution waitable timer: %s\n", timer ? "available" : "NOT available (the fix falls back to timeBeginPeriod(1))");
		CHECK(timer != nullptr);
		if (timer)
		{
			const LONGLONG real_margin = ticks_for_ms(0.6, frequency.QuadPart);
			const LONGLONG interval = ticks_for_fps(120, frequency.QuadPart);
			pacer real;
			real.set_interval(interval);
			LONGLONG worst_late = 0;
			int late_frames = 0; // over 0.5 ms past the deadline: a preempted wake-up
			const LONGLONG begin = now();
			for (int frame = 0; frame < 60; ++frame)
			{
				const LONGLONG deadline = real.next(now());
				for (;;)
				{
					const auto step = plan_wait(deadline - now(), frequency.QuadPart, real_margin);
					if (!step.spin) break;
					if (step.timer_100ns > 0)
					{
						LARGE_INTEGER due{};
						due.QuadPart = -step.timer_100ns;
						if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, 100);
						continue;
					}
					while (now() < deadline) YieldProcessor();
					break;
				}
				const LONGLONG late = now() - deadline;
				worst_late = std::max(worst_late, late);
				late_frames += late > ticks_for_ms(0.5, frequency.QuadPart);
			}
			const double total_ms = static_cast<double>(now() - begin) * 1000.0 / static_cast<double>(frequency.QuadPart);
			std::printf("  info  60 frames paced at 120 fps took %.1f ms (expected 491.7: 59 intervals); %d frame(s) over 0.5 ms late, the worst by %.3f ms\n",
			            total_ms, late_frames, static_cast<double>(worst_late) * 1000.0 / static_cast<double>(frequency.QuadPart));
			CHECK(total_ms >= 491.0 && total_ms < 530.0);
			CHECK(worst_late < interval); // a preempted wake-up is absorbed by the next frame; never a whole frame late
			CHECK(late_frames <= 6);      // the odd preemption on a busy desktop, not the rule
			CloseHandle(timer);
		}
	}

	// The in-game Advanced Options rows (options_menu_rules.hpp): the rows and their choices, what a
	// change writes to the ini (round trip through a temporary file), the status line, the navigation
	// records, the highlight's geometry, and the four call sites and nine game functions the menu
	// checks before patching, against the retail XMen2.exe when a copy is at hand.
	void check_options_menu_rules()
	{
		using namespace options_menu_rules;
		std::printf("in-game options rules (the Advanced Options rows)\n");

		// Rows and ids: four rows under FSAA, above Accept, with ids the game doesn't use.
		CHECK(row_count == 4 && item_id(row::mode) == 0x40 && item_id(row::background) == 0x43 && status_item_id == 0x4f);
		CHECK(row_for_item_id(0x41) == row::frame_rate && !row_for_item_id(6) && !row_for_item_id(0x44) && !row_for_item_id(0x4f));
		CHECK(row_y(0) == 170 && row_y(3) == 242 && row_y(0) > fsaa_y + row_h && row_y(3) + value_h < status_y && status_y + status_h <= accept_y);
		CHECK(highlight_y(0) == 149 && highlight_x == 30 && fsaa_y + highlight_dy == 125); // the game's own FSAA highlight sits at y 125
		CHECK(value_x + value_w == 177 + 40 && row_x + row_w == 229);                       // values end where FSAA's does; rows span the pane like FSAA
		CHECK(label_style == 3 && value_style == 5 && status_style == 0);
		CHECK(std::string(row_labels[3]) == "Run in background" && std::wstring(row_keys[1]) == L"FrameRate");

		// Display mode: no Mode in the ini shows as Fullscreen.
		auto mode = mode_choices(display_rules::mode::stock);
		CHECK(mode.texts.size() == 3 && mode.selected == 0 && mode.texts[0] == "Fullscreen");
		CHECK(mode_choices(display_rules::mode::borderless).selected == 1 && mode_choices(display_rules::mode::windowed).selected == 2);
		CHECK(mode_value(1) == "borderless" && mode_value(2) == "windowed" && mode_value(0) == "fullscreen" && mode_value(9) == "fullscreen");

		// Frame rate: the presets plus the desktop's rate and the ini's own value, ten at most.
		const frame_rate_rules::cap stock_cap{};
		auto frame = frame_rate_choices(stock_cap, 180);
		CHECK((frame.texts == std::vector<std::string>{"30", "60", "120", "144", "165", "180", "240", "Refresh", "Unlimited"}));
		CHECK(frame.selected == 1); // 60 when the ini has no FrameRate
		CHECK(frame_rate_choices({frame_rate_rules::cap::kind::fixed, 144}, 180).selected == 3);
		CHECK(frame_rate_choices({frame_rate_rules::cap::kind::refresh, 0}, 180).selected == 7);
		CHECK(frame_rate_choices({frame_rate_rules::cap::kind::unlimited, 0}, 180).selected == 8);
		frame = frame_rate_choices(stock_cap, 175); // an odd desktop rate is listed, in order
		CHECK(frame.texts.size() == 10 && frame.texts[5] == "175" && frame.texts[6] == "180" && frame.selected == 1);
		frame = frame_rate_choices({frame_rate_rules::cap::kind::fixed, 90}, 175); // both the desktop's rate and the ini's value: 165 makes room
		CHECK((frame.texts == std::vector<std::string>{"30", "60", "90", "120", "144", "175", "180", "240", "Refresh", "Unlimited"}));
		CHECK(frame.selected == 2);
		frame = frame_rate_choices({frame_rate_rules::cap::kind::fixed, 165}, 175); // a preset as the ini's value needs no room: nothing dropped
		CHECK(frame.texts.size() == 10 && frame.texts[4] == "165" && frame.texts[3] == "144" && frame.texts[5] == "175" && frame.selected == 4);
		CHECK(frame_rate_choices(stock_cap, 0).texts.size() == 9); // no desktop rate known
		CHECK(frame_rate_value("Refresh") == "refresh" && frame_rate_value("Unlimited") == "0" && frame_rate_value("144") == "144");

		// VSync and Run in background: Off / On, absent VSync shows Off, absent RunInBackground shows On.
		CHECK(vsync_choices(std::nullopt).selected == 0 && vsync_choices(true).selected == 1 && vsync_choices(false).texts[1] == "On");
		CHECK(background_choices(true).selected == 1 && background_choices(false).selected == 0);

		// The panel as a whole, from the ini, and its defaults.
		display_rules::options opts;
		auto shown = choices_for(opts, 180);
		CHECK((shown.selected() == std::array<int, 4>{0, 1, 0, 1}));
		CHECK((default_selection(shown) == std::array<int, 4>{0, 1, 0, 1}));
		opts.window_mode = display_rules::mode::windowed;
		opts.frame_rate = {frame_rate_rules::cap::kind::fixed, 120};
		opts.vsync = true;
		opts.run_in_background = false;
		shown = choices_for(opts, 180);
		CHECK((shown.selected() == std::array<int, 4>{2, 2, 1, 0}));
		CHECK((default_selection(shown) == std::array<int, 4>{0, 1, 0, 1}));

		// Accept writes only the rows that changed; a row put on its stock value removes its key
		// (absent = the game's own behaviour, which is what that value shows for).
		const std::array<bool, 4> all_keys{true, true, true, true};
		CHECK(ini_changes(shown, shown.selected(), all_keys).empty());
		auto changes = ini_changes(shown, {1, 2, 1, 0}, all_keys);
		CHECK(changes.size() == 1 && changes[0].which == row::mode && std::wstring(changes[0].key) == L"Mode" && changes[0].value == "borderless");
		changes = ini_changes(shown, {0, 2, 1, 0}, all_keys); // Fullscreen: no Mode (the game's own), not Mode=fullscreen (the fix's variant)
		CHECK(changes.size() == 1 && changes[0].which == row::mode && !changes[0].value.has_value());
		changes = ini_changes(shown, {2, 7, 0, 1}, all_keys);
		CHECK(changes.size() == 3);
		CHECK(changes[0].which == row::frame_rate && changes[0].value == "refresh");
		CHECK(changes[1].which == row::vsync && std::wstring(changes[1].key) == L"VSync" && !changes[1].value.has_value());
		CHECK(changes[2].which == row::background && std::wstring(changes[2].key) == L"RunInBackground" && !changes[2].value.has_value());
		changes = ini_changes(shown, {2, 8, 1, 0}, all_keys);
		CHECK(changes.size() == 1 && changes[0].value == "0" && std::wstring(changes[0].key) == L"FrameRate"); // Unlimited
		changes = ini_changes(shown, {2, 1, 1, 0}, all_keys); // 60: the game's own cap again, not FrameRate=60 (the fix's limiter)
		CHECK(changes.size() == 1 && changes[0].which == row::frame_rate && !changes[0].value.has_value());
		CHECK(ini_changes(shown, {2, 99, 1, 0}, all_keys).empty()); // out of range: ignored
		CHECK(changes_row(ini_changes(shown, {0, 2, 0, 0}, all_keys), row::mode) && changes_row(ini_changes(shown, {0, 2, 0, 0}, all_keys), row::vsync) &&
		      !changes_row(ini_changes(shown, {0, 2, 0, 0}, all_keys), row::frame_rate));

		// Revert to default, then Accept: every key the ini has goes, even one already on the stock
		// value (Mode=fullscreen, FrameRate=60, VSync=0 are the fix's, not the game's); a row moved
		// after the revert is written; no keys, nothing to do.
		display_rules::options explicit_defaults;
		explicit_defaults.window_mode = display_rules::mode::fullscreen;
		explicit_defaults.frame_rate = {frame_rate_rules::cap::kind::fixed, 60};
		explicit_defaults.vsync = false;
		const auto shown_defaults = choices_for(explicit_defaults, 180);
		const auto stock_rows = default_selection(shown_defaults);
		CHECK(shown_defaults.selected() == stock_rows);
		CHECK(ini_changes(shown_defaults, stock_rows, all_keys, false).empty()); // not reverted, not moved: left alone
		changes = ini_changes(shown_defaults, stock_rows, all_keys, true);
		CHECK(changes.size() == 4 && std::ranges::none_of(changes, [](const ini_change& c) { return c.value.has_value(); }));
		CHECK(ini_changes(shown_defaults, stock_rows, {true, false, false, false}, true).size() == 1);
		CHECK(ini_changes(shown_defaults, stock_rows, {}, true).empty());
		changes = ini_changes(shown_defaults, {1, stock_rows[1], 0, 1}, all_keys, true);
		CHECK(changes.size() == 4 && changes[0].value == "borderless" && !changes[1].value && !changes[2].value && !changes[3].value);

		// The status line.
		CHECK(status_text(false, false).empty());
		CHECK(status_text(true, false) == "Restart for the new mode");
		CHECK(status_text(false, true) == "Restart to apply VSync");
		CHECK(status_text(true, true) == "Restart to apply both");
		for (const bool mode_changed : {false, true})
		{
			for (const bool vsync : {false, true})
			{
				CHECK(status_text(mode_changed, vsync).size() <= status_max_chars); // the panel doesn't clip
			}
		}

		// Round trip through an ini the way the panel writes it: the changed key written, the one put
		// back on its stock value removed, the rest of the file kept.
		const auto ini = std::filesystem::temp_directory_path() / "xml2_test_options.ini";
		{
			std::ofstream out(ini, std::ios::binary);
			out << "; test\r\n[Online]\r\nDomain=off\r\n[Display]\r\nMode=windowed\r\n; keep me\r\nTopmost=1\r\nFrameRate=120\r\nVSync=1\r\nRunInBackground=0\r\n";
		}
		const auto wide = ini.wstring();
		for (const auto& change : ini_changes(shown, {2, 7, 0, 0}, all_keys))
		{
			const std::wstring value = change.value ? std::wstring(change.value->begin(), change.value->end()) : std::wstring();
			CHECK(WritePrivateProfileStringW(L"Display", change.key, change.value ? value.c_str() : nullptr, wide.c_str()) != 0);
		}
		wchar_t value[64]{};
		GetPrivateProfileStringW(L"Display", L"FrameRate", L"", value, 64, wide.c_str());
		CHECK(std::wstring(value) == L"refresh");
		CHECK(GetPrivateProfileStringW(L"Display", L"VSync", L"absent", value, 64, wide.c_str()) && std::wstring(value) == L"absent"); // stock value: key gone
		GetPrivateProfileStringW(L"Display", L"Mode", L"", value, 64, wide.c_str());
		CHECK(std::wstring(value) == L"windowed"); // unchanged row: untouched
		CHECK(GetPrivateProfileIntW(L"Display", L"Topmost", 0, wide.c_str()) == 1 && GetPrivateProfileIntW(L"Display", L"RunInBackground", 1, wide.c_str()) == 0);
		GetPrivateProfileStringW(L"Online", L"Domain", L"", value, 64, wide.c_str());
		CHECK(std::wstring(value) == L"off");
		const auto text = read_file(ini);
		CHECK(text.find("; keep me") != std::string::npos && text.find("; test") != std::string::npos && text.find("VSync") == std::string::npos);
		// What the panel then shows: parsed back the way the display fix reads it.
		CHECK(frame_rate_rules::parse_frame_rate("refresh")->what == frame_rate_rules::cap::kind::refresh && frame_rate_rules::parse_vsync("") == frame_rate_rules::vsync::untouched);
		std::error_code ignored;
		std::filesystem::remove(ini, ignored);

		// Navigation: our rows between FSAA and Accept, left/right kept on the row.
		int fsaa = 0, accept = 0, label = 0, rows[4]{};
		nav_record fsaa_record{&fsaa, &fsaa, &fsaa, &label, &accept, 1, 0, {}};
		nav_record accept_record{&accept, nullptr, nullptr, &fsaa, &label, 0, 0, {}};
		nav_record label_record{&label, nullptr, nullptr, &accept, &fsaa, 1, 0, {}};
		nav_record* records[] = {&label_record, &fsaa_record, &accept_record, nullptr};
		CHECK(relink(records, 4, &fsaa, &accept, &rows[0], &rows[3]) == 2);
		CHECK(fsaa_record.down == &rows[0] && accept_record.up == &rows[3] && fsaa_record.up == &label && label_record.down == &fsaa);
		const auto record = row_record(&rows[1], &rows[0], &rows[2]);
		CHECK(record.self == &rows[1] && record.left == &rows[1] && record.right == &rows[1] && record.up == &rows[0] && record.down == &rows[2]);
		CHECK(record.keep_left_right == 1 && record.keep_up_down == 0);
		CHECK(sizeof(nav_record) == 0x18 && sizeof(anim_slot) == 0x2c && highlight_slot == 5);
		CHECK(interpolate(500, 149, 1.0f) == 149 && interpolate(100, 200, 0.5f) == 150 && interpolate(200, 100, 0.25f) == 175);

		// The call sites: the expected bytes call where the table says, and rel32 lands on a replacement.
		for (const auto* site : call_sites)
		{
			CHECK(site->expected[0] == 0xE8 && decoded_target(*site) == site->target_rva);
			CHECK(matches(*site, site->expected.data()));
			auto other = site->expected;
			other[5] ^= 1;
			CHECK(!matches(*site, other.data()));
		}
		CHECK(rel32(0x61f356, 0x6222b0) == 0x2f55 && rel32(0x61f8a4, 0x619440) == -0x6469);
		CHECK(finish_panel_site.target_rva == game::set_all_anim && save_site.target_rva == game::save_settings && cancel_site.target_rva == game::load_settings &&
		      revert_site.target_rva == game::load_defaults);
		if (const auto exe = game_executable())
		{
			for (const auto* site : call_sites)
			{
				const auto at = file_offset(*exe, site->rva);
				CHECK(at.has_value() && *at + 16 <= exe->size() && matches(*site, reinterpret_cast<const std::uint8_t*>(exe->data() + *at)));
			}
			for (const auto& function : game_functions)
			{
				const auto at = file_offset(*exe, function.rva);
				CHECK(at.has_value() && *at + 16 <= exe->size() && matches(function, reinterpret_cast<const std::uint8_t*>(exe->data() + *at)));
			}
			const auto png = file_offset(*exe, game::toggle_png_string);
			CHECK(png.has_value() && exe->compare(*png, 16, std::string("texs\\toggle.png\0", 16)) == 0);
		}
		else
		{
			std::printf("  skip  no XMen2.exe to check the call sites against\n");
		}
	}

	// The Video options list's rules (resolution_rules.hpp): the seven places the game addresses its
	// resolution table, checked against the retail XMen2.exe when a copy is at hand, and what the
	// list offers in each mode.
	void check_resolution_rules()
	{
		using namespace resolution_rules;
		using display_rules::mode;
		using display_rules::size;
		std::printf("resolution list rules (the 64-slot table and what the Video options list offers)\n");

		CHECK(stock_slots == 20 && slots == 64 && slot_bytes == 12 && slots * slot_bytes == 768 && max_text == 9);
		CHECK(stock_table_va - stock_table_rva == 0x400000 && stock_table_va + stock_slots * slot_bytes == 0x6e98f0); // the default bindings start right after
		CHECK(table_sites.size() == 7);
		DWORD previous = 0;
		for (const auto& site : table_sites)
		{
			CHECK(site.rva > previous && site.rva > 0x218000 && site.rva < 0x220000); // the panel's code, in address order
			previous = site.rva;
			CHECK(decoded_address(site) == stock_table_va);
			CHECK(matches(site, site.expected.data()));
			// lea eax, [reg*4 + imm32] (8D 04 85/95) carries the address at +3, mov reg, imm32 (B8+reg) at +1.
			CHECK((site.expected[0] == 0x8D && site.expected[1] == 0x04 && site.imm_offset == 3) || ((site.expected[0] & 0xF8) == 0xB8 && site.imm_offset == 1));
			auto other = site.expected;
			other[site.imm_offset] ^= 1;
			CHECK(!matches(site, other.data()));
		}
		CHECK(table_sites[0].rva == 0x2181bf && table_sites[1].rva == 0x219b95 && table_sites[2].rva == 0x21d61f && table_sites[3].rva == 0x21e636);
		CHECK(table_sites[4].rva == 0x21f57b && table_sites[5].rva == 0x21f6a1 && table_sites[6].rva == 0x21f843);
		if (const auto exe = game_executable())
		{
			for (const auto& site : table_sites)
			{
				const auto at = file_offset(*exe, site.rva);
				CHECK(at.has_value() && *at + 16 <= exe->size() && matches(site, reinterpret_cast<const std::uint8_t*>(exe->data() + *at)));
			}
			// The table as shipped: "640x480" first, "1600x1200" last of seven, the rest empty.
			const auto table = file_offset(*exe, stock_table_rva);
			CHECK(table.has_value() && *table + stock_slots * slot_bytes <= exe->size());
			if (table)
			{
				CHECK(exe->compare(*table, 8, std::string("640x480\0", 8)) == 0 && exe->compare(*table + 6 * slot_bytes, 10, std::string("1600x1200\0", 10)) == 0);
				CHECK(std::ranges::all_of(exe->substr(*table + 7 * slot_bytes, 13 * slot_bytes), [](const char c) { return c == 0; }));
			}
		}
		else
		{
			std::printf("  skip  no XMen2.exe to check the table's references against\n");
		}

		// Texts: what the game sprintf's, within the nine characters its registry read allows.
		CHECK(text_of(2560, 1440) == "2560x1440" && text_of(640, 480) == "640x480");
		CHECK(fits(640, 480) && fits(3840, 2160) && fits(5120, 2880) && !fits(10240, 4320) && !fits(1920, 10800));
		for (const auto& common : common_sizes) CHECK(fits(common.width, common.height) && common.width >= 640 && common.height >= 480);

		// Aspect ratios: one per cent of slack takes 1366x768 as 16:9 and the two 21:9 sizes as one.
		CHECK(same_aspect(1366, 768, 2560, 1440) && same_aspect(1920, 1080, 3840, 2160) && same_aspect(3440, 1440, 2560, 1080) && same_aspect(3840, 1600, 3440, 1440));
		CHECK(!same_aspect(1280, 1024, 1280, 960) && !same_aspect(1920, 1200, 1920, 1080) && !same_aspect(0, 0, 1920, 1080));

		// Extra sizes: none for exclusive fullscreen; in a window the desktop's aspect ratio's common
		// sizes up to the desktop, plus half and three quarters of the desktop.
		CHECK(extra_sizes({2560, 1440}, mode::stock).empty() && extra_sizes({2560, 1440}, mode::fullscreen).empty() && extra_sizes({0, 0}, mode::borderless).empty());
		const auto has = [](const std::vector<size>& list, const UINT w, const UINT h) { return std::ranges::find(list, size{w, h}) != list.end(); };
		auto extras = extra_sizes({2560, 1440}, mode::borderless);
		CHECK(has(extras, 1280, 720) && has(extras, 1366, 768) && has(extras, 1600, 900) && has(extras, 1920, 1080) && has(extras, 2560, 1440));
		CHECK(!has(extras, 3840, 2160) && !has(extras, 1280, 800) && !has(extras, 1024, 768)); // above the desktop, other ratios
		extras = extra_sizes({3440, 1440}, mode::windowed);
		CHECK(has(extras, 2560, 1080) && has(extras, 3440, 1440) && has(extras, 1720, 720) && has(extras, 2580, 1080) && !has(extras, 1920, 1080));
		extras = extra_sizes({1920, 1200}, mode::borderless);
		CHECK(has(extras, 1280, 800) && has(extras, 1440, 900) && has(extras, 1680, 1050) && has(extras, 1920, 1200) && has(extras, 960, 600) && !has(extras, 1920, 1080));
		extras = extra_sizes({2560, 1080}, mode::borderless); // an odd desktop: half of it is 1280x540, rounded to even numbers
		CHECK(has(extras, 1280, 540) && has(extras, 1920, 810) && has(extras, 2560, 1080) && !has(extras, 3440, 1440));

		// The list itself. Exclusive fullscreen: the adapter's sizes and the desktop's, nothing the
		// adapter can't switch to.
		const std::vector<d3d8::display_mode> adapter{{640, 480, 60, 22}, {800, 600, 60, 22}, {1024, 768, 60, 22}, {1280, 1024, 60, 22},
		                                              {1920, 1080, 60, 22}, {1920, 1080, 144, 22}, {1920, 1080, 60, 23}, {320, 240, 60, 22}};
		const auto sorted = [](const std::vector<d3d8::display_mode>& list)
		{
			return std::ranges::is_sorted(list, [](const auto& a, const auto& b) { return a.width != b.width ? a.width < b.width : a.height < b.height; });
		};
		const auto lists = [](const std::vector<d3d8::display_mode>& list, const UINT w, const UINT h)
		{
			return std::ranges::count_if(list, [&](const auto& m) { return m.width == w && m.height == h; }) == 1;
		};
		auto list = build_list(adapter, {2560, 1440}, 180, std::nullopt, mode::stock, slots);
		CHECK(list.size() == 6 && sorted(list) && lists(list, 640, 480) && lists(list, 1920, 1080) && lists(list, 2560, 1440) && !lists(list, 1280, 720) && !lists(list, 320, 240));
		CHECK(list.back().refresh_rate == 180 && list.back().format == d3d8::format_x8r8g8b8);
		const auto again = build_list(adapter, {2560, 1440}, 180, std::nullopt, mode::fullscreen, slots); // Mode=fullscreen: the same
		CHECK(again.size() == list.size() && std::memcmp(again.data(), list.data(), list.size() * sizeof(d3d8::display_mode)) == 0);
		// A window: the aspect ratio's sizes and the render-scale presets join, the forced size too.
		list = build_list(adapter, {2560, 1440}, 180, size{1600, 1000}, mode::borderless, slots);
		CHECK(list.size() == 10 && sorted(list) && lists(list, 1280, 720) && lists(list, 1366, 768) && lists(list, 1600, 900) && lists(list, 1600, 1000) && lists(list, 2560, 1440));
		CHECK(std::ranges::all_of(list, [](const auto& m) { return fits(m.width, m.height) && m.width >= 640 && m.height >= 480; }));
		CHECK(build_list(adapter, {2560, 1440}, 180, std::nullopt, mode::windowed, slots).size() == 9);
		// Too many: the smallest go, the desktop stays; the game's own table takes 20.
		std::vector<d3d8::display_mode> many;
		for (UINT i = 0; i < 80; ++i) many.push_back({1000 + i * 10, 800, 60, 22});
		list = build_list(many, {2560, 1440}, 180, std::nullopt, mode::fullscreen, slots);
		CHECK(list.size() == slots && sorted(list) && lists(list, 2560, 1440) && list.front().width == 1000 + 17 * 10 && !lists(list, 1000, 800));
		list = build_list(many, {2560, 1440}, 180, std::nullopt, mode::fullscreen, stock_slots);
		CHECK(list.size() == stock_slots && lists(list, 2560, 1440) && list.front().width == 1000 + 61 * 10);
		// A size too long for the game's registry read is left out, even the desktop's.
		list = build_list({{10240, 4320, 60, 22}, {1920, 1080, 60, 22}}, {10240, 4320}, 60, size{12800, 7200}, mode::borderless, slots);
		CHECK(std::ranges::none_of(list, [](const auto& m) { return m.width >= 10000; }) && lists(list, 1920, 1080) && lists(list, 5120, 2160));

		// By [Display] ResolutionList. Absent: exactly the list the fix gave its own modes before the
		// 64-slot table (curate_modes into the game's 20 slots, no window extras), whatever table size
		// is passed; game: build_list within 20; all: within the table in use.
		using display_rules::resolution_list;
		const auto same = [](const std::vector<d3d8::display_mode>& a, const std::vector<d3d8::display_mode>& b)
		{
			return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(d3d8::display_mode)) == 0;
		};
		CHECK(same(video_list(many, {2560, 1440}, 180, std::nullopt, mode::borderless, resolution_list::stock, slots),
		           display_rules::curate_modes(many, {2560, 1440}, 180, std::nullopt, stock_slots)));
		CHECK(same(video_list(adapter, {2560, 1440}, 180, size{1600, 1000}, mode::borderless, resolution_list::stock, slots),
		           display_rules::curate_modes(adapter, {2560, 1440}, 180, size{1600, 1000}, stock_slots)));
		CHECK(video_list(many, {2560, 1440}, 180, std::nullopt, mode::fullscreen, resolution_list::game, slots).size() == stock_slots);
		CHECK(video_list(many, {2560, 1440}, 180, std::nullopt, mode::fullscreen, resolution_list::all, slots).size() == slots);
		CHECK(video_list(many, {2560, 1440}, 180, std::nullopt, mode::fullscreen, resolution_list::all, stock_slots).size() == stock_slots); // relocation refused
		CHECK(same(video_list(adapter, {2560, 1440}, 180, size{1600, 1000}, mode::borderless, resolution_list::all, slots),
		           build_list(adapter, {2560, 1440}, 180, size{1600, 1000}, mode::borderless, slots)));
	}

	// ---- The engine limit adjuster ------------------------------------------------------------------

	// A little-endian operand out of a hex string of retail bytes.
	std::uint32_t operand_in(const std::string_view hex, const std::size_t offset, const std::size_t size)
	{
		std::uint32_t value = 0;
		for (std::size_t i = size; i-- > 0;)
		{
			value = value << 8 | limits_rules::hex_byte(hex, offset + i);
		}
		return value;
	}

	std::uint32_t operand_at(const std::uint8_t* at, const std::size_t size)
	{
		std::uint32_t value = 0;
		std::memcpy(&value, at, size);
		return value;
	}

	// XMen2.exe as Windows maps it (the headers, then each section at its virtual address, the rest
	// zero), in memory of the test's own that may run code. Freed by the caller (VirtualFree).
	std::uint8_t* map_image(const std::string& file, DWORD& size_of_image)
	{
		const auto u16 = [&](const size_t at) { return static_cast<DWORD>(static_cast<unsigned char>(file[at]) | (static_cast<unsigned char>(file[at + 1]) << 8)); };
		const auto u32 = [&](const size_t at) { return u16(at) | (u16(at + 2) << 16); };
		if (file.size() < 0x40) return nullptr;
		const size_t pe = u32(0x3c);
		if (pe + 24 + 64 > file.size() || file.compare(pe, 4, "PE\0\0", 4) != 0) return nullptr;
		const size_t optional_header = pe + 24;
		size_of_image = u32(optional_header + 56);
		const DWORD size_of_headers = u32(optional_header + 60);
		auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, size_of_image, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		if (!image) return nullptr;
		std::memcpy(image, file.data(), std::min<size_t>(size_of_headers, file.size()));
		const size_t sections = u16(pe + 6);
		const size_t section_table = optional_header + u16(pe + 20);
		for (size_t i = 0; i < sections; ++i)
		{
			const size_t header = section_table + i * 40;
			const DWORD virtual_size = u32(header + 8), virtual_address = u32(header + 12), raw_size = u32(header + 16), raw_offset = u32(header + 20);
			const size_t mapped = std::min<size_t>({raw_size, (virtual_size + 0xfffu) & ~0xfffu, file.size() - std::min<size_t>(raw_offset, file.size()), size_of_image - virtual_address});
			std::memcpy(image + virtual_address, file.data() + raw_offset, mapped);
		}
		return image;
	}

	// The game's code, run on blocks of the test's own. No C++ objects here: an exception is a failure.
	using thiscall_t = void*(__fastcall*)(void* self, void* edx);
	using thiscall_int_t = int(__fastcall*)(void* self, void* edx, int argument);
	using find_next_t = int(__fastcall*)(void* bits, void* edx, int from, int want_set);

	bool run_thiscall(const std::uint8_t* function, void* self, void*& result)
	{
		__try
		{
			result = reinterpret_cast<thiscall_t>(const_cast<std::uint8_t*>(function))(self, nullptr);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool run_thiscall_int(const std::uint8_t* function, void* self, const int argument, int& result)
	{
		__try
		{
			result = reinterpret_cast<thiscall_int_t>(const_cast<std::uint8_t*>(function))(self, nullptr, argument);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	int run_find_next(const std::uint8_t* function, void* bits, const int from, const bool want_set)
	{
		__try
		{
			return reinterpret_cast<find_next_t>(const_cast<std::uint8_t*>(function))(bits, nullptr, from, want_set ? 1 : 0);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return -1;
		}
	}

	// A block for the game's code to build a structure in: `size` bytes of zeros, then a page of
	// 0xCD that must still be 0xCD afterwards (nothing written past the structure).
	struct test_block
	{
		std::vector<std::uint8_t> bytes;
		std::size_t size;
		explicit test_block(const std::size_t structure_size) : bytes(structure_size + 0x1000, std::uint8_t{0xCD}), size(structure_size)
		{
			std::fill_n(bytes.begin(), structure_size, std::uint8_t{0});
		}
		std::uint8_t* data() { return bytes.data(); }
		DWORD dword(const std::size_t offset) const { return operand_at(bytes.data() + offset, 4); }
		bool slack_untouched() const { return std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(size), bytes.end(), [](const std::uint8_t b) { return b == 0xCD; }); }
	};

	// The engine limit adjuster's rules (limits_rules.hpp): [Limits] in the ini, the layouts, the
	// patch tables on their own; then, when a copy of XMen2.exe is at hand, the tables against it.
	void check_limits_rules()
	{
		using namespace limits_rules;
		std::printf("engine limits rules ([Limits] ActorSlots, ResourceNames and ItemEnhancements)\n");

		// The values: digits, with spaces and an inline comment around them.
		CHECK(parse_count("127") == 127 && parse_count(" 1024\t") == 1024 && parse_count("127        ; 40 = the game's own") == 127 && parse_count("40;") == 40);
		CHECK(!parse_count("") && !parse_count("; 127") && !parse_count("abc") && !parse_count("12x") && !parse_count("-5") && !parse_count("+5"));
		CHECK(!parse_count("0x7f") && !parse_count("1e3") && !parse_count("1234567") && !parse_count("1 27") && !parse_count("127.0"));
		CHECK(names_needed(40) == 450 && names_needed(41) == 451 && names_needed(127) == 537);

		// Absent: the game's own, nothing said.
		auto chosen = decide(std::nullopt, std::nullopt);
		CHECK(chosen.actor_slots == 40 && chosen.resource_names == 450 && chosen.notes.empty());
		chosen = decide("40", std::nullopt);
		CHECK(chosen.actor_slots == 40 && chosen.resource_names == 450 && chosen.notes.empty());
		chosen = decide(std::nullopt, "1024");
		CHECK(chosen.actor_slots == 40 && chosen.resource_names == 1024 && chosen.notes.empty());
		chosen = decide("40", "450");
		CHECK(chosen.actor_slots == 40 && chosen.resource_names == 450 && chosen.notes.empty());
		// ActorSlots brings the name table along: 1024 without ResourceNames, at least 450 + 1 per slot with it.
		chosen = decide("127", std::nullopt);
		CHECK(chosen.actor_slots == 127 && chosen.resource_names == 1024 && chosen.notes.size() == 1 && chosen.notes[0].find("ResourceNames=1024") != std::string::npos);
		chosen = decide("127", "2048");
		CHECK(chosen.actor_slots == 127 && chosen.resource_names == 2048 && chosen.notes.empty());
		chosen = decide("127", "500");
		CHECK(chosen.actor_slots == 127 && chosen.resource_names == 537 && chosen.notes.size() == 1 && chosen.notes[0].find("using 537") != std::string::npos);
		chosen = decide("127", "450");
		CHECK(chosen.actor_slots == 127 && chosen.resource_names == 537);
		chosen = decide("41", "450");
		CHECK(chosen.actor_slots == 41 && chosen.resource_names == 451);
		chosen = decide("64", "4096");
		CHECK(chosen.actor_slots == 64 && chosen.resource_names == 4096 && chosen.notes.empty());
		// Out of range or not a number: logged, the game's own.
		for (const char* bad : {"128", "39", "0", "1000", "abc", "", "-127", "0x7f", "127.5"})
		{
			chosen = decide(bad, std::nullopt);
			CHECK(chosen.actor_slots == 40 && chosen.resource_names == 450 && chosen.notes.size() == 1 && chosen.notes[0].find("stays at 40 slots") != std::string::npos);
		}
		for (const char* bad : {"449", "4097", "0", "many", "1024x"})
		{
			chosen = decide(std::nullopt, bad);
			CHECK(chosen.actor_slots == 40 && chosen.resource_names == 450 && chosen.notes.size() == 1 && chosen.notes[0].find("ignored") != std::string::npos);
		}
		chosen = decide("64", "lots"); // a bad ResourceNames is as good as none: the actor table still gets its 1024
		CHECK(chosen.actor_slots == 64 && chosen.resource_names == 1024 && chosen.notes.size() == 2);
		chosen = decide("128", "1024");
		CHECK(chosen.actor_slots == 40 && chosen.resource_names == 1024 && chosen.notes.size() == 1);

		// The keys as a user writes them, read the way the DLL reads them: the profile API keeps the
		// inline comments, the parser drops them.
		const auto ini = std::filesystem::temp_directory_path() / "xml2_test_limits.ini";
		{
			std::ofstream out(ini, std::ios::binary);
			out << "[Limits]\r\nActorSlots = 127        ; 40 = the game's own; accept 41..127\r\nResourceNames = 1024    ; 450 = the game's own\r\n";
		}
		const auto ini_value = [&](const wchar_t* key)
		{
			wchar_t value[128]{};
			GetPrivateProfileStringW(L"Limits", key, L"", value, 128, ini.wstring().c_str());
			std::string narrow;
			for (const wchar_t* p = value; *p; ++p) narrow += static_cast<char>(*p);
			return narrow;
		};
		const auto actor_text = ini_value(L"ActorSlots");
		const auto names_text = ini_value(L"ResourceNames");
		CHECK(actor_text.find(';') != std::string::npos); // the comment comes along
		chosen = decide(actor_text, names_text);
		CHECK(chosen.actor_slots == 127 && chosen.resource_names == 1024 && chosen.notes.empty());
		std::error_code ignored;
		std::filesystem::remove(ini, ignored);

		// The actor table's layout: the retail one from N = 40, field for field (actor-table.md 2), and
		// N = 127 (7.1). The object ends where the init guard 0x7b7a58 starts.
		const auto retail_actors = actor_layout_for(stock_actor_slots);
		CHECK(retail_actors.bitmap_words == 2 && retail_actors.bitmap_a == 0x7300 && retail_actors.ring == 0x7308 && retail_actors.ring_write == 0x73ac && retail_actors.ring_read == 0x73b0);
		CHECK(retail_actors.ring_count == 0x73b4 && retail_actors.bitmap_b == 0x73b8 && retail_actors.live == 0x73c0 && retail_actors.ids == 0x73c4 && retail_actors.mask == 0x7464);
		CHECK(retail_actors.shift == 0x7468 && retail_actors.pool_size == 0x746c && retail_actors.object_size == 0x7470 && retail_actors.id_mask == 0x3f && retail_actors.id_shift == 6);
		CHECK(actor_object_retail + retail_actors.object_size == actor_init_guard && actor_live_retail == 0x7b79ac);
		const auto actors = actor_layout_for(max_actor_slots);
		CHECK(actors.bitmap_words == 4 && actors.bitmap_a == 0x16d20 && actors.ring == 0x16d30 && actors.ring_write == 0x16f30 && actors.ring_read == 0x16f34 && actors.ring_count == 0x16f38);
		CHECK(actors.bitmap_b == 0x16f3c && actors.live == 0x16f4c && actors.ids == 0x16f50 && actors.mask == 0x1714c && actors.shift == 0x17150 && actors.pool_size == 0x17154);
		CHECK(actors.object_size == 0x17158 && actors.id_mask == 0x7f && actors.id_shift == 7);
		CHECK(actor_layout_for(64).id_mask == 0x3f && actor_layout_for(65).id_mask == 0x7f && actor_layout_for(64).bitmap_words == 2 && actor_layout_for(65).bitmap_words == 3);
		bool actor_layouts_ok = true;
		for (int n = stock_actor_slots; n <= max_actor_slots; ++n)
		{
			const auto l = actor_layout_for(n);
			const auto slots = static_cast<DWORD>(n);
			actor_layouts_ok &= l.live + 4 == l.ids && l.mask + 4 == l.shift; // one displacement serves both bases
			actor_layouts_ok &= l.ring_read - l.ring == (slots + 1) * 4 + 4 && l.ids + slots * 4 == l.mask && l.object_size == l.pool_size + 4;
			actor_layouts_ok &= l.bitmap_words * 32 >= slots && (l.bitmap_words - 1) * 32 < slots;
			actor_layouts_ok &= l.id_mask == (1u << l.id_shift) - 1 && l.id_mask >= slots - 1 && l.id_mask / 2 < slots - 1; // the smallest mask that holds every index
		}
		CHECK(actor_layouts_ok);

		// The name table's layout: retail from M = 450 (igb-cache.md 8.1), and M = 1024 - with its count
		// right after the node bitmap, as retail (0x150a0: the node count seen map-relative, not 0x150a4),
		// and the entries at 8 mod 16.
		const auto retail_names = name_layout_for(stock_resource_names);
		CHECK(retail_names.bitmap_words == 15 && retail_names.node_ring == 0x8ca4 && retail_names.node_ring_write == 0x93b0 && retail_names.node_ring_read == 0x93b4);
		CHECK(retail_names.node_ring_count == 0x93b8 && retail_names.node_bitmap == 0x93bc && retail_names.node_live == 0x93f8 && retail_names.count == 0x9404);
		CHECK(retail_names.entries == 0x9408 && retail_names.entry_bitmap == 0xb028 && retail_names.size == 0xb064);
		CHECK(value_of(name_field::entry_owner_index, retail_names) == 0x941 && value_of(name_field::ring_write_via_ring, retail_names) == 0x70c &&
		      value_of(name_field::entry_bitmap_via_entries, retail_names) == 0x1c20);
		const auto names = name_layout_for(default_resource_names);
		CHECK(names.bitmap_words == 32 && names.node_ring == 0x14004 && names.node_ring_write == 0x15008 && names.node_ring_read == 0x1500c && names.node_ring_count == 0x15010);
		CHECK(names.node_bitmap == 0x15014 && names.node_live == 0x15094 && names.count == 0x150a0 && names.entries == 0x150a8 && names.entry_bitmap == 0x190a8 && names.size == 0x19128);
		CHECK(value_of(name_field::entry_owner_index, names) == 0x150b && value_of(name_field::ring_count_via_ring, names) == 0x100c && value_of(name_field::entry_bitmap_via_entries, names) == 0x4000);
		bool name_layouts_ok = true;
		for (int m = stock_resource_names; m <= max_resource_names; ++m)
		{
			const auto l = name_layout_for(m);
			const auto capacity = static_cast<DWORD>(m);
			name_layouts_ok &= l.entries % 16 == 8 && l.entries >= l.count + 4 && l.entries < l.count + 4 + 16 && l.count == l.node_live + name_pool_offset;
			name_layouts_ok &= l.node_ring_write - l.node_ring == (capacity + 1) * 4 && l.node_ring == capacity * name_node_size + 4;
			name_layouts_ok &= l.entry_bitmap == l.entries + capacity * 16 && l.size == l.entry_bitmap + l.bitmap_words * 4;
			name_layouts_ok &= l.bitmap_words * 32 >= capacity && (l.bitmap_words - 1) * 32 < capacity;
			name_layouts_ok &= value_of(name_field::entry_owner_index, l) * 16 == l.entries + 8;
		}
		CHECK(name_layouts_ok);

		// The tables on their own: well-formed bytes, the operand inside its instruction and holding its
		// retail value, in address order - and the retail layout giving back every retail value, so the
		// layout and each row's field agree. imm8 operands stay within 0x7f at N = 127.
		bool actor_rows_ok = true;
		DWORD previous = 0;
		for (const auto& s : actor_sites)
		{
			const bool row_ok = valid_hex(s.hex) && static_cast<std::size_t>(s.offset + s.size) <= hex_size(s.hex) && (s.size == 1 || s.size == 4) && operand_in(s.hex, s.offset, s.size) == s.retail &&
			                    value_of(s.field, retail_actors) == s.retail && s.va >= previous && (s.size == 4 || value_of(s.field, actors) <= 0x7f);
			if (!row_ok) std::printf("  info  actor table row 0x%08lX doesn't add up\n", s.va);
			actor_rows_ok &= row_ok;
			previous = s.va;
		}
		CHECK(actor_rows_ok);
		CHECK(actor_sites.size() == 74 && std::ranges::count_if(actor_sites, [](const actor_site& s) { return s.size == 1; }) == 13 &&
		      std::ranges::count_if(actor_sites, [](const actor_site& s) { return s.field == actor_field::last_slot; }) == 3); // + 3 imm8 and 2 imm32 in the clone: 16 imm8, 5 imm32, 58 disp32
		bool references_ok = true;
		for (const auto& s : actor_references)
		{
			references_ok &= valid_hex(s.hex) && hex_size(s.hex) == 5 && s.offset == 1 && s.size == 4 && operand_in(s.hex, 1, 4) == s.retail &&
			                 reference_value(s, actor_object_retail, find_next_40_va) == s.retail; // the retail addresses give back the retail operands
		}
		CHECK(references_ok && actor_references.size() == 5);
		CHECK(rel32(0x56b027, find_next_40_va) == 0xffeea4b4 && hex_byte(actor_references[0].hex, 0) == 0xE8 && hex_byte(actor_references[1].hex, 0) == 0xE8);

		bool name_rows_ok = true;
		previous = 0;
		for (const auto& s : name_sites)
		{
			const bool row_ok = valid_hex(s.hex) && static_cast<std::size_t>(s.offset + s.size) <= hex_size(s.hex) && s.size == 4 && operand_in(s.hex, s.offset, s.size) == s.retail &&
			                    value_of(s.field, retail_names) == s.retail && s.va >= previous;
			if (!row_ok) std::printf("  info  name table row 0x%08lX doesn't add up\n", s.va);
			name_rows_ok &= row_ok;
			previous = s.va;
		}
		CHECK(name_rows_ok && name_sites.size() == 71); // igb-cache.md 8.1's 67 and 0x55a6c0, 0x55a6c7, 0x55ae88, 0x55af40
		CHECK(std::ranges::count_if(name_sites, [](const name_site& s) { return s.va == 0x55a6c0 || s.va == 0x55a6c7 || s.va == 0x55ae88 || s.va == 0x55af40; }) == 4);

		// The clone's source: 0x8f bytes, its five N operands, and no rel32 anywhere (no E8/E9 byte, no 0F 8x pair).
		const auto& clone = find_next_40;
		bool clone_ok = valid_hex(clone.hex) && hex_size(clone.hex) == 0x8f && clone.rel32_count == 0;
		for (const auto& f : clone.fields)
		{
			clone_ok &= operand_in(clone.hex, f.offset, f.size) == f.retail && value_of(f.field, retail_actors) == f.retail;
		}
		for (std::size_t i = 0; i < hex_size(clone.hex); ++i)
		{
			const auto b = hex_byte(clone.hex, i);
			clone_ok &= b != 0xE8 && b != 0xE9 && !(b == 0x0F && i + 1 < hex_size(clone.hex) && (hex_byte(clone.hex, i + 1) & 0xF0) == 0x80);
		}
		CHECK(clone_ok);
		for (const auto& g : actor_guards) CHECK(valid_hex(g.hex));
		for (const auto& g : name_guards) CHECK(valid_hex(g.hex));
		CHECK(valid_hex(motion_counter_guards[0].hex) && valid_hex(motion_counter_guards[1].hex) && valid_hex(igb_counter_guards[0].hex) && valid_hex(igb_counter_guards[1].hex));
		CHECK(igb_live_retail == 0x7bf39c);

		// Moving code: a rel32 keeps its target. E8 FB 0F 00 00 at 0x1000 calls 0x2000; copied to 0x5000 it reads 0x2000 - 0x5005.
		std::uint8_t call[] = {0xE8, 0xFB, 0x0F, 0x00, 0x00, 0xC3};
		const std::uint8_t call_offsets[] = {1};
		relocate_rel32(call, call_offsets, 0x1000, 0x5000);
		CHECK(operand_at(call + 1, 4) == 0x2000u - 0x5005u && call[0] == 0xE8 && call[5] == 0xC3);

		// No two writes share a byte (0x56b2c3 and 0x55ae07 carry two operands each, apart).
		auto all_writes = actor_writes(actors, 0x12340000, 0x00a80000);
		const auto name_part = name_writes(names);
		all_writes.insert(all_writes.end(), name_part.begin(), name_part.end());
		std::ranges::sort(all_writes, [](const operand_write& a, const operand_write& b) { return a.va < b.va; });
		bool apart = true;
		for (std::size_t i = 1; i < all_writes.size(); ++i) apart &= all_writes[i - 1].va + all_writes[i - 1].size <= all_writes[i].va;
		CHECK(apart && all_writes.size() == 74 + 5 + 71);

		// ItemEnhancements: 376..1024 raises the pool, 375 is the game's own, anything else is a note; on its own.
		CHECK(decide(std::nullopt, std::nullopt).item_enhancements == 375 && decide(std::nullopt, std::nullopt, "512").item_enhancements == 512 &&
		      decide(std::nullopt, std::nullopt, "375").item_enhancements == 375 && decide(std::nullopt, std::nullopt, "1024 ; the most").item_enhancements == 1024);
		chosen = decide(std::nullopt, std::nullopt, "1025");
		CHECK(chosen.item_enhancements == 375 && chosen.notes.size() == 1 && chosen.notes[0].find("ItemEnhancements=1025 isn't a number from 376 to 1024") != std::string::npos);
		chosen = decide("127", "1024", "512");
		CHECK(chosen.actor_slots == 127 && chosen.resource_names == 1024 && chosen.item_enhancements == 512 && chosen.notes.empty());
		chosen = decide(std::nullopt, std::nullopt, "lots");
		CHECK(chosen.item_enhancements == 375 && chosen.actor_slots == 40 && chosen.notes.size() == 1);

		// The item layout: the retail one for 375; for N the records and the bitmap at the end of a bigger manager.
		const auto retail_items = item_layout_for(stock_item_enhancements);
		CHECK(retail_items.records_offset == 0x2594 && retail_items.bitmap_offset == 0x602c && retail_items.bitmap_dwords == 12 && retail_items.bitmap_in_pool == 0x3a98 &&
		      retail_items.manager_size == 0x7a00);
		const auto items_512 = item_layout_for(512);
		CHECK(items_512.records_offset == 0x7a00 && items_512.bitmap_in_pool == 512 * 0x28 && items_512.bitmap_offset == 0x7a00 + 512 * 0x28 && items_512.bitmap_dwords == 16 &&
		      items_512.manager_size == 0x7a00 + 512 * 0x28 + 64);
		CHECK(item_layout_for(376).bitmap_dwords == 12 && item_layout_for(1024).bitmap_dwords == 32 && item_layout_for(1024).manager_size == 0x7a00 + 1024 * 0x28 + 128);
		// The sites on their own: well-formed, the operand inside the instruction, the retail value in the bytes, 21 + 7 + 2 + 8 + 2 + 1.
		bool item_rows_ok = true;
		std::map<item_field, int> item_counts;
		for (const auto& st : item_sites)
		{
			item_rows_ok &= valid_hex(st.hex) && static_cast<std::size_t>(st.offset) + st.size <= hex_size(st.hex) && operand_in(st.hex, st.offset, st.size) == st.retail && st.size == 4;
			item_rows_ok &= value_of(st.field, retail_items) == st.retail;
			++item_counts[st.field];
		}
		CHECK(item_rows_ok && item_sites.size() == 41 && item_counts[item_field::records_offset] == 21 && item_counts[item_field::bitmap_offset] == 7 &&
		      item_counts[item_field::bitmap_dwords] == 2 && item_counts[item_field::capacity] == 8 && item_counts[item_field::bitmap_in_pool] == 2 && item_counts[item_field::manager_size] == 1);
		CHECK(valid_hex(item_clone_call.hex) && item_clone_call.va == 0x47dc69 && operand_in(item_clone_call.hex, 1, 4) == item_clone_call.retail && item_clone_call.retail == 0x45e5f0 - (0x47dc69 + 5));
		CHECK(valid_hex(item_find_next.hex) && hex_size(item_find_next.hex) == 0x99 && item_find_next.va == 0x45e5f0);
		bool clone_fields_ok = true;
		for (const auto& f : item_find_next.fields) clone_fields_ok &= operand_in(item_find_next.hex, f.offset, f.size) == 0x177 && f.size == 4;
		CHECK(clone_fields_ok);
		{
			// no E8/E9 and no 0F 8x in the clone: it moves without relocation
			bool pic = true;
			for (std::size_t i = 0; i < hex_size(item_find_next.hex); ++i)
			{
				const auto b = hex_byte(item_find_next.hex, i);
				pic &= b != 0xe8 && b != 0xe9 && !(b == 0x0f && i + 1 < hex_size(item_find_next.hex) && (hex_byte(item_find_next.hex, i + 1) & 0xf0) == 0x80);
			}
			CHECK(pic);
			std::vector<std::uint8_t> code(hex_size(item_find_next.hex));
			for (std::size_t i = 0; i < code.size(); ++i) code[i] = hex_byte(item_find_next.hex, i);
			finish_item_clone(code.data(), items_512);
			bool five = true;
			for (const auto& f : item_find_next.fields) five &= operand_at(code.data() + f.offset, 4) == 512;
			CHECK(five);
		}
		// The writes for N = 512 with a clone at 0x00b00000: every site, then the call.
		const auto item_w = item_writes(items_512, 0x00b00000);
		CHECK(item_w.size() == 42 && item_w.back().va == 0x47dc6a && item_w.back().value == 0x00b00000u - (0x47dc69 + 5) && item_w[0].va == 0x47bb04 && item_w[0].value == 0x7a00);
		const auto item_w2 = item_writes(items_512, 0x00b00000, 0x00b01000);
		CHECK(item_w2.size() == 43 && item_w2.back().va == 0x4806ae && item_w2.back().value == 0x00b01000u - (0x4806ad + 5));
		CHECK(affixes_fit(85, 79) && affixes_fit(375, 375) && !affixes_fit(376, 0) && !affixes_fit(0, 376) && item_affix_stack == 375);
		CHECK(std::ranges::count_if(item_w, [](const operand_write& w) { return w.value == 512; }) == 8 && std::ranges::count_if(item_w, [](const operand_write& w) { return w.value == 0x7a00; }) == 21 &&
		      std::ranges::count_if(item_w, [](const operand_write& w) { return w.value == 0x7a00 + 512 * 0x28; }) == 7 && std::ranges::count_if(item_w, [](const operand_write& w) { return w.value == 16; }) == 2 &&
		      std::ranges::count_if(item_w, [](const operand_write& w) { return w.value == 512 * 0x28; }) == 2 && std::ranges::count_if(item_w, [](const operand_write& w) { return w.value == 0x7a00 + 512 * 0x28 + 64; }) == 1);

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the patch sites against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr && image_size == 0x6744c6); // SizeOfImage, as the exe has it (not page-aligned)
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - image_base); };

		// Every site, reference, guard and the clone's source: the retail bytes.
		bool retail = true;
		const auto expect = [&](const DWORD va, const std::string_view hex)
		{
			if (!matches(at(va), hex))
			{
				std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says\n", va);
				retail = false;
			}
		};
		for (const auto& s : actor_sites) expect(s.va, s.hex);
		for (const auto& s : actor_references) expect(s.va, s.hex);
		for (const auto& g : actor_guards) expect(g.va, g.hex);
		expect(clone.va, clone.hex);
		for (const auto& s : name_sites) expect(s.va, s.hex);
		for (const auto& g : name_guards) expect(g.va, g.hex);
		for (const auto& g : motion_counter_guards) expect(g.va, g.hex);
		for (const auto& g : igb_counter_guards) expect(g.va, g.hex);
		for (const auto& st : item_sites) expect(st.va, st.hex);
		for (const auto& g : item_guards) expect(g.va, g.hex);
		expect(item_clone_call.va, item_clone_call.hex);
		expect(item_find_next.va, item_find_next.hex);
		CHECK(retail);
		const std::vector<std::uint8_t> before(image, image + image_size);

		// The stock caps write back exactly the retail bytes (with the retail object and findNext).
		for (const auto& w : actor_writes(retail_actors, actor_object_retail, find_next_40_va)) apply_write(image, w);
		for (const auto& w : name_writes(retail_names)) apply_write(image, w);
		for (const auto& w : item_writes(retail_items, item_find_next_va)) apply_write(image, w);
		CHECK(std::memcmp(image, before.data(), image_size) == 0);

		// N = 512 for the item pool: the 42 operands change, to their values, and nothing else does.
		{
			for (const auto& w : item_writes(items_512, 0x00b00000)) apply_write(image, w);
			std::size_t changed = 0, outside = 0;
			for (std::size_t i = 0; i < image_size; ++i)
			{
				if (image[i] == before[i]) continue;
				++changed;
				const DWORD va = image_base + static_cast<DWORD>(i);
				bool inside = false;
				for (const auto& w : item_writes(items_512, 0x00b00000)) inside |= va >= w.va && va < w.va + w.size;
				outside += !inside;
			}
			CHECK(outside == 0 && changed > 0 && changed <= 42 * 4);
			CHECK(operand_at(at(0x4806ae), 4) == 0x47ce80 - (0x4806ad + 5)); // the loader-end call untouched without a check
			CHECK(operand_at(at(0x4804a7), 4) == 512 && operand_at(at(0x480a26), 4) == items_512.manager_size && operand_at(at(0x47bb04), 4) == 0x7a00 && operand_at(at(0x480743), 4) == items_512.bitmap_offset &&
			      operand_at(at(0x47dc02), 4) == 512 * 0x28 && operand_at(at(0x47dc6a), 4) == 0x00b00000u - (0x47dc69 + 5) && operand_at(at(0x480748), 4) == 16);
			std::memcpy(image, before.data(), image_size);
		}

		// N = 127, M = 1024: the listed operand bytes change, to their new values, and nothing else does.
		constexpr DWORD object = 0x12340000, clone_va = 0x00a80000; // any addresses: only the arithmetic is looked at
		auto writes = actor_writes(actors, object, clone_va);
		const auto more = name_writes(names);
		writes.insert(writes.end(), more.begin(), more.end());
		for (const auto& w : writes) apply_write(image, w);
		std::vector<bool> listed(image_size, false);
		bool new_values = true;
		for (const auto& w : writes)
		{
			for (std::size_t i = 0; i < w.size; ++i) listed[w.va - image_base + i] = true;
			new_values &= operand_at(at(w.va), w.size) == w.value;
		}
		std::size_t changed = 0, outside = 0;
		for (std::size_t i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i])
			{
				++changed;
				outside += !listed[i];
			}
		}
		std::printf("  info  %zu operands written for 127 actor slots and 1024 names, %zu bytes changed\n", writes.size(), changed);
		CHECK(new_values && outside == 0 && changed > 0);
		// Spot checks against actor-table.md 5/5.1 and igb-cache.md 8.1.
		CHECK(operand_at(at(0x56b2c3 + 2), 4) == 0x16f50 && *at(0x56b2c3 + 6) == 0x7f && operand_at(at(0x455a50 + 2), 4) == 0x204 && operand_at(at(0x56abfd + 3), 4) == 0x16d30);
		CHECK(operand_at(at(0x56ac5d + 1), 4) == 0x7e && operand_at(at(0x56b16b + 2), 4) == 0x17150 && operand_at(at(0x56b848 + 3), 4) == 0x16d20 && *at(0x455a5d + 2) == 0x7f);
		CHECK(operand_at(at(0x56b90a + 1), 4) == object && operand_at(at(0x56b92c + 1), 4) == object && operand_at(at(0x67e160 + 1), 4) == object);
		CHECK(operand_at(at(0x56b027 + 1), 4) == clone_va - (0x56b027 + 5) && operand_at(at(0x56b086 + 1), 4) == clone_va - (0x56b086 + 5));
		CHECK(operand_at(at(0x55a6a0 + 2), 4) == 0x150a0 && operand_at(at(0x55ae07 + 2), 4) == 0x150a0 && operand_at(at(0x55ae07 + 6), 4) == 0x400 && operand_at(at(0x55af8d + 1), 4) == 0x19128);
		CHECK(operand_at(at(0x55aba8 + 2), 4) == 0x150b && operand_at(at(0x55acd5 + 1), 4) == 0x150b && operand_at(at(0x55ae88 + 3), 4) == 0x4000 && operand_at(at(0x55af40 + 2), 4) == 0x150a8);
		CHECK(operand_at(at(0x55a735 + 1), 4) == 0x20 && operand_at(at(0x55a8b1 + 1), 4) == 0x3ff && operand_at(at(0x55a880 + 2), 4) == 0x1004 && operand_at(at(0x55a818 + 3), 4) == 0x14004);
		CHECK(std::memcmp(at(clone.va), before.data() + (clone.va - image_base), hex_size(clone.hex)) == 0); // the original keeps its 40 bits for its other caller

		// The clone: the five operands and nothing else, all 127.
		std::vector<std::uint8_t> clone_code(at(clone.va), at(clone.va) + hex_size(clone.hex));
		finish_clone(clone_code.data(), clone, actors, clone_va);
		std::size_t clone_changed = 0;
		for (std::size_t i = 0; i < clone_code.size(); ++i) clone_changed += clone_code[i] != hex_byte(clone.hex, i);
		CHECK(clone_changed == 5 && clone_code[0x06] == 0x7f && clone_code[0x3b] == 0x7f && clone_code[0x82] == 0x7f && operand_at(&clone_code[0x0d], 4) == 0x7f && operand_at(&clone_code[0x86], 4) == 0x7f);

		// The patched code at work, on blocks of the test's own (those functions use no globals and call
		// only each other). The pool constructor builds 127 slots, alloc hands out all 127, and the clone
		// scans the 127-bit "live" bitmap.
		void* result = nullptr;
		test_block pool(actors.pool_size);
		CHECK(run_thiscall(at(0x56ac10), pool.data(), result) && result == pool.data() && pool.slack_untouched());
		bool ring_ok = true, ids_ok = true;
		for (DWORD i = 0; i < 127; ++i)
		{
			ring_ok &= pool.dword(actors.ring + i * 4) == i;
			ids_ok &= pool.dword(actors.ids + i * 4) == (0x80 | i);
		}
		CHECK(ring_ok && ids_ok && pool.dword(actors.ring_write) == 0 && pool.dword(actors.ring_read) == 0 && pool.dword(actors.ring_count) == 127 && pool.dword(actors.live) == 0);
		CHECK(pool.dword(actors.mask) == 0x7f && pool.dword(actors.shift) == 7);
		bool records_ok = true;
		for (DWORD i = 0; i < 127; ++i)
		{
			records_ok &= run_thiscall(at(0x56b5f0), pool.data(), result) && result == pool.data() + i * actor_record_size;
		}
		CHECK(records_ok && pool.dword(actors.live) == 127 && pool.dword(actors.ring_count) == 0 && pool.dword(actors.ring_read) == 0 && pool.slack_untouched());
		CHECK(pool.dword(actors.bitmap_a) == 0xffffffff && pool.dword(actors.bitmap_a + 12) == 0x7fffffff && pool.dword(actors.bitmap_b + 8) == 0xffffffff && pool.dword(actors.bitmap_b + 12) == 0x7fffffff);
		auto* clone_exec = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, clone_code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		CHECK(clone_exec != nullptr);
		if (clone_exec)
		{
			std::memcpy(clone_exec, clone_code.data(), clone_code.size());
			std::uint8_t* live_bits = pool.data() + actors.bitmap_b;
			CHECK(run_find_next(clone_exec, live_bits, 0, true) == 0 && run_find_next(clone_exec, live_bits, 126, true) == 126 && run_find_next(clone_exec, live_bits, 127, true) == 127);
			CHECK(run_find_next(clone_exec, live_bits, 0, false) == 127); // every slot live: no free one below 127
			std::uint32_t bits[4] = {1u << 3, 0, 1u << (70 - 64), 1u << (126 - 96)};
			CHECK(run_find_next(clone_exec, bits, 0, true) == 3 && run_find_next(clone_exec, bits, 4, true) == 70 && run_find_next(clone_exec, bits, 71, true) == 126);
			CHECK(run_find_next(clone_exec, bits, 127, true) == 127 && run_find_next(clone_exec, bits, 3, false) == 4);
			VirtualFree(clone_exec, 0, MEM_RELEASE);
		}

		// The name table's constructor builds 1024 free nodes; the node allocator (add's, 0x55aaa0) hands
		// out 1024 and isFull says so at the 1024th, not before; freeing one (vtable slot 4) makes room.
		test_block table(names.size);
		CHECK(run_thiscall(at(name_table_constructor), table.data(), result) && result == table.data() && table.slack_untouched());
		CHECK(table.dword(0) == name_table_vtable && table.dword(4) == 0x3fffffff && table.dword(8) == 0xffffffff);
		bool nodes_ok = true;
		for (DWORD i = 0; i < 1024; ++i) nodes_ok &= table.dword(name_pool_offset + names.node_ring + i * 4) == i;
		CHECK(nodes_ok && table.dword(name_pool_offset + names.node_ring_count) == 1024 && table.dword(name_pool_offset + names.node_ring_read) == 0 && table.dword(names.count) == 0);
		const auto is_full = [&](bool& full) // isFull (0x55a6a0): xor eax, eax; cmp; setge al
		{
			void* answer = nullptr;
			const bool ran = run_thiscall(at(0x55a6a0), table.data(), answer);
			full = answer != nullptr;
			return ran && reinterpret_cast<std::uintptr_t>(answer) <= 1;
		};
		bool full = true;
		CHECK(is_full(full) && !full);
		bool allocations_ok = true;
		for (DWORD i = 0; i < 1024; ++i)
		{
			allocations_ok &= run_thiscall(at(0x55aaa0), table.data(), result) && result == table.data() + name_pool_offset + i * name_node_size + 0xc && table.dword(8) == i;
			if (i == 1022) allocations_ok &= is_full(full) && !full;
		}
		CHECK(allocations_ok && table.dword(names.count) == 1024 && table.dword(name_pool_offset + names.node_live) == 1024 && table.slack_untouched());
		CHECK(is_full(full) && full);
		int ignored_result = 0;
		CHECK(run_thiscall_int(at(0x55ada0), table.data(), 700, ignored_result) && table.dword(names.count) == 1023); // free node 700 (ret 4)
		CHECK(is_full(full) && !full);
		CHECK(run_thiscall(at(0x55aaa0), table.data(), result) && table.dword(8) == 700 && table.dword(names.count) == 1024 && table.slack_untouched());

		VirtualFree(image, 0, MEM_RELEASE);
	}

	// Fighting style pool tests execute the retail node allocator/free/clear on synthetic blocks.
	int test_style_capacity = 19;
	void* __fastcall test_construct_style_ring(void* self, void*)
	{
		return limits_rules::style_ring_construct(self, test_style_capacity);
	}
	DWORD __fastcall test_push_style_ring(void* self, void*, const DWORD* value)
	{
		return limits_rules::style_ring_push(self, *value, test_style_capacity);
	}
	std::vector<DWORD> destroyed_style_slots;
	void __fastcall test_destroy_style(void* self, void*, int)
	{
		destroyed_style_slots.push_back(operand_at(static_cast<std::uint8_t*>(self) + 4, 4));
	}

	void check_fight_style_rules()
	{
		using namespace limits_rules;
		std::printf("fighting style registry ([Limits] FightStyles)\n");
		CHECK(decide(std::nullopt, std::nullopt).fight_styles == 19);
		for (const auto text : {"19", "20", "22", "31", "32 ; headroom"})
		{
			const auto c = decide(std::nullopt, std::nullopt, std::nullopt, text);
			CHECK(c.fight_styles == *parse_count(text) && c.notes.empty());
		}
		for (const auto bad : {"18", "33", "127", "0", "-1", "0x20", "32x", ""})
		{
			const auto c = decide(std::nullopt, std::nullopt, std::nullopt, bad);
			CHECK(c.fight_styles == 19 && c.notes.size() == 1 && c.notes[0].find("stays at 19") != std::string::npos);
		}
		const auto combined = decide("127", "1024", "512", "32");
		CHECK(combined.actor_slots == 127 && combined.resource_names == 1024 && combined.item_enhancements == 512 && combined.fight_styles == 32);
		const auto stock = style_layout_for(19), raised = style_layout_for(32);
		CHECK(stock.node_ring == 0x134 && stock.node_ring_write == 0x184 && stock.node_bitmap == 0x190 && stock.count == 0x1a4);
		CHECK(stock.styles == 0x1a8 && stock.bitmap == 0x11a80 && stock.auxiliary == 0x11a8c && stock.manager_size == 0x29a18);
		CHECK(raised.node_ring == 0x204 && raised.node_ring_write == 0x288 && raised.count == 0x2a8 && raised.styles == 0x2ac);
		CHECK(raised.bitmap == 0x1dbac && raised.auxiliary == 0x1dbb8 && raised.manager_size == 0x35b44);
		for (int n = 19; n <= 32; ++n)
		{
			const auto l = style_layout_for(n);
			CHECK(l.node_ring_write - l.node_ring == static_cast<DWORD>((n + 1) * 4) && l.count + 4 == l.styles && l.bitmap - l.styles == n * style_record_size);
			CHECK(l.auxiliary - l.bitmap == 12 && l.manager_size - l.auxiliary == 0x17f8c && dwords_for_bits(n) == 1);
		}
		bool rows_ok = true;
		for (const auto& s : style_sites)
		{
			rows_ok &= valid_hex(s.hex) && static_cast<std::size_t>(s.offset) + s.size <= hex_size(s.hex) && operand_in(s.hex, s.offset, s.size) == s.retail;
			rows_ok &= value_of(s.field, stock) == s.retail && (s.size == 4 || (s.size == 1 && value_of(s.field, raised) <= 127));
		}
		for (const auto& g : style_guards) rows_ok &= valid_hex(g.hex) && g.what && *g.what;
		CHECK(rows_ok);
		auto writes = style_writes(raised, 0x12340000, 0x12340100);
		std::ranges::sort(writes, {}, &operand_write::va);
		bool apart = true;
		for (std::size_t i = 1; i < writes.size(); ++i) apart &= writes[i - 1].va + writes[i - 1].size <= writes[i].va;
		CHECK(apart && writes.size() == 74);

		// The ring implementation independently preserves FIFO order across the signed-disp8 boundary.
		for (int n : {19, 20, 29, 30, 31, 32})
		{
			std::vector<DWORD> ring(n + 5, 0xcccccccc);
			CHECK(style_ring_construct(ring.data(), n) == ring.data());
			for (DWORD i = 0; i < static_cast<DWORD>(n); ++i) style_ring_push(ring.data(), 1000 + i, n);
			CHECK(ring[n + 1] == 0 && ring[n + 2] == 0 && ring[n + 3] == static_cast<DWORD>(n) && ring[n] == 0xcccccccc && ring[n + 4] == 0xcccccccc);
			for (DWORD i = 0; i < static_cast<DWORD>(n); ++i) CHECK(ring[i] == 1000 + i);
		}

		const auto exe = game_executable();
		if (!exe) { std::printf("  skip  no XMen2.exe for fighting style execution tests\n"); return; }
		DWORD image_size = 0;
		auto* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](DWORD va) { return image + (va - image_base); };
		const auto mismatch = [&] { return style_first_mismatch([&](DWORD va, std::string_view hex) { return matches(at(va), hex); }); };
		CHECK(mismatch() == 0);
		if (mismatch()) { VirtualFree(image, 0, MEM_RELEASE); return; }
		const std::vector<std::uint8_t> original(image, image + image_size);
		for (const auto& w : style_writes(stock)) apply_write(image, w);
		CHECK(std::memcmp(image, original.data(), image_size) == 0);
		// Every guarded byte, including opcodes and branch behavior, rejects a changed executable.
		bool rejects = true;
		const auto corrupt = [&](DWORD va, std::string_view hex)
		{
			for (std::size_t i = 0; i < hex_size(hex); ++i)
			{
				at(va)[i] ^= 1;
				rejects &= mismatch() != 0;
				at(va)[i] ^= 1;
			}
		};
		for (const auto& s : style_sites) corrupt(s.va, s.hex);
		for (const auto& g : style_guards) corrupt(g.va, g.hex);
		CHECK(rejects && mismatch() == 0);

		const auto hook_target = [&](const void* p)
		{
			return image_base + static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(image));
		};
		for (int n : {19, 20, 22, 30, 31, 32})
		{
			std::memcpy(image, original.data(), image_size);
			test_style_capacity = n;
			const auto l = style_layout_for(n);
			const auto patch = style_writes(l, hook_target(reinterpret_cast<const void*>(&test_construct_style_ring)), hook_target(reinterpret_cast<const void*>(&test_push_style_ring)));
			std::vector<bool> covered(image_size);
			for (const auto& w : patch)
			{
				apply_write(image, w);
				for (DWORD i = 0; i < w.size; ++i) covered[w.va - image_base + i] = true;
				CHECK(operand_at(at(w.va), w.size) == w.value);
			}
			bool only_listed = true;
			for (DWORD i = 0; i < image_size; ++i) only_listed &= image[i] == original[i] || covered[i];
			CHECK(only_listed);
			FlushInstructionCache(GetCurrentProcess(), image, image_size);
			test_block nodes(l.node_count + 4);
			void* result = nullptr;
			CHECK(run_thiscall(at(0x4ff7a0), nodes.data(), result) && result == nodes.data() && nodes.slack_untouched());
			CHECK(nodes.dword(l.node_ring_count) == static_cast<DWORD>(n) && nodes.dword(l.node_count) == 0 && nodes.dword(l.node_bitmap) == 0);
			bool allocations = true;
			for (DWORD i = 0; i < static_cast<DWORD>(n); ++i)
			{
				allocations &= nodes.dword(l.node_ring + i * 4) == i;
				allocations &= run_thiscall(at(0x4feb10), nodes.data(), result) && reinterpret_cast<std::uintptr_t>(result) == i;
			}
			CHECK(allocations && nodes.dword(l.node_count) == static_cast<DWORD>(n) && nodes.dword(l.node_ring_count) == 0 && nodes.slack_untouched());
			const DWORD all_bits = 0xffffffffu >> (32 - n);
			CHECK(nodes.dword(l.node_bitmap) == all_bits);
			// Exercise release/reuse on both sides of the old limit, including bit 31 and ring wrap.
			for (int cycle = 0; cycle < n + 2; ++cycle)
			{
				int answer = 0;
				const int index = cycle % 2 ? n - 1 : 18;
				const auto prior_head = nodes.dword(l.node_ring_write);
				CHECK(run_thiscall_int(at(0x4ffa30), nodes.data(), index, answer) && static_cast<DWORD>(answer) == prior_head);
				CHECK(nodes.dword(l.node_count) == static_cast<DWORD>(n - 1) && nodes.dword(l.node_ring_count) == 1 && !(nodes.dword(l.node_bitmap) & (1u << index)));
				CHECK(run_thiscall(at(0x4feb10), nodes.data(), result) && reinterpret_cast<std::uintptr_t>(result) == static_cast<std::uintptr_t>(index));
				CHECK(nodes.dword(l.node_count) == static_cast<DWORD>(n) && nodes.dword(l.node_bitmap) == all_bits && nodes.slack_untouched());
			}
			// The real style-pool destructor must visit high slots and leave the following fields intact.
			test_block objects(n * style_record_size + 4);
			DWORD vtable[] = {static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&test_destroy_style))};
			const DWORD vtable_address = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(vtable));
			std::vector<DWORD> expected{0, 18, static_cast<DWORD>(n - 1)};
			std::ranges::sort(expected);
			expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
			DWORD bits = 0;
			for (const DWORD i : expected)
			{
				std::memcpy(objects.data() + i * style_record_size, &vtable_address, 4);
				std::memcpy(objects.data() + i * style_record_size + 4, &i, 4);
				bits |= 1u << i;
			}
			std::memcpy(objects.data() + n * style_record_size, &bits, 4);
			destroyed_style_slots.clear();
			CHECK(run_thiscall(at(0x4fe9c0), objects.data(), result));
			CHECK(destroyed_style_slots == expected && objects.dword(n * style_record_size) == 0 && objects.slack_untouched());
			CHECK(run_thiscall(at(0x4fe9c0), objects.data(), result) && destroyed_style_slots == expected); // empty clear does not destroy twice
		}
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// ---- Forced parties (forced_teams_rules.hpp) ------------------------------------------------------

	// A script call's arguments as the game hands them to a handler: args->get(i) (0x4d5830) reads
	// [args + 4i] below the count at +0x1c, and a value's vt+0x10 / vt+0x14 is its number / text
	// (0x55d7e0 for both in the game: mov eax, [ecx+4]).
	struct fake_value
	{
		void* const* vtable;
		std::uintptr_t payload;
	};

	struct fake_args
	{
		void* values[7];
		int count;
	};
	static_assert(offsetof(fake_args, count) == 0x1c);

	std::uintptr_t __fastcall test_value_payload(void* value, void*)
	{
		return static_cast<const fake_value*>(value)->payload;
	}

	void* __fastcall test_get_argument(void* args, void*, const int index)
	{
		const auto* a = static_cast<const fake_args*>(args);
		return index >= 0 && index < a->count ? a->values[index] : nullptr;
	}

	// Value vtables: slot 4 the number, slot 5 the text.
	struct value_vtables
	{
		void* text[6]{};
		void* number[6]{};
		explicit value_vtables(void* accessor)
		{
			text[5] = accessor;
			number[4] = accessor;
		}
	};

	// One call's arguments; built in place (the values point into it).
	struct script_call
	{
		std::array<std::string, 7> texts{};
		std::array<fake_value, 7> values{};
		fake_args args{};
		const value_vtables& vtables;
		explicit script_call(const value_vtables& v) : vtables(v) {}
		script_call(const script_call&) = delete;
		script_call& operator=(const script_call&) = delete;
		script_call& text(const std::string& s)
		{
			const int i = args.count++;
			texts[i] = s;
			values[i] = {vtables.text, reinterpret_cast<std::uintptr_t>(texts[i].c_str())};
			args.values[i] = &values[i];
			return *this;
		}
		script_call& number(const int n)
		{
			const int i = args.count++;
			values[i] = {vtables.number, static_cast<std::uintptr_t>(static_cast<std::uint32_t>(n))};
			args.values[i] = &values[i];
			return *this;
		}
	};

	// The game as the functions see it, in plain C++: its party slots, the registry's names and
	// herostat heroes with their costumes, the side-mission stack, the console, the menus.
	struct fake_engine
	{
		forced_teams_rules::get_argument_t get = &test_get_argument;
		bool forced = true;
		bool add_on = false;
		bool join_on = true;
		bool add_claims_only = false; // 0x46c9f0 as seen in game: true, nobody seated
		bool name_fault = false;      // writing a record's name faults
		bool cancel_fault = false;    // cancelsidemission faults
		std::map<std::string, int> indices; // registry vt+0x3c: 0 = no such character
		std::set<int> herostats;            // registry vt+0x7c
		forced_teams_rules::party slots{};
		int seat_fault_at = -1;             // the slot setter faults at this slot
		struct hero
		{
			int index = 0;
			int costume = 0;
			std::array<std::uint8_t, 10> variants{};
			bool stats = true;
		};
		std::vector<hero> heroes; // the herostat hero list
		std::vector<forced_teams_rules::side_record> records;
		std::string zone = "mansion/man1b/subbasement2"; // the current zone; "": none, pushsidemission pushes nothing
		bool loading = false;  // the zone manager's load pending (vt+0x24)
		bool deferred = false; // a load the frame's run of the queue starts leaves pending (the zone being left runs on)
		forced_teams_rules::call_state kept;
		std::map<std::string, int> characters;           // entity name -> id; -1: not a character
		std::vector<std::string> ran, queued;
		std::size_t queue_room = 2;
		std::string menu;
		int hud_leaves = 0;
		std::vector<std::string> added;
		std::deque<int> ints;
		std::deque<std::string> strings;
		std::vector<std::string> lines;

		std::optional<std::string> text_argument(void* args, const int i)
		{
			char text[forced_teams_rules::argument_max + 1];
			if (!forced_teams_rules::read_text_argument(get, args, i, text)) return std::nullopt;
			return std::string(text);
		}
		std::optional<int> int_argument(void* args, const int i)
		{
			int value = 0;
			if (!forced_teams_rules::read_int_argument(get, args, i, value)) return std::nullopt;
			return value;
		}
		void* make_int(const int value) { ints.push_back(value); return &ints.back(); }
		void* make_string(const std::string& text) { strings.push_back(text); return &strings.back(); }
		bool forced_teams() { return forced; }
		bool add_hero_on() { return add_on; }
		bool join_hero_on() { return join_on; }
		std::optional<int> hero_index(const std::string& name) { const auto f = indices.find(name); return f == indices.end() ? 0 : f->second; }
		std::optional<bool> herostat(const int index) { return herostats.count(index) != 0; }
		std::optional<std::string> slot(const int i) { return slots[static_cast<std::size_t>(i)]; }
		bool seat(const int i, const std::string& name)
		{
			if (i == seat_fault_at) return false;
			slots[static_cast<std::size_t>(i)] = name;
			return true;
		}
		hero* find(const int index)
		{
			for (auto& h : heroes) if (h.index == index) return &h;
			return nullptr;
		}
		std::optional<int> hero_count() { return static_cast<int>(heroes.size()); }
		std::optional<int> hero_at(const int i) { return heroes[static_cast<std::size_t>(i)].index; }
		std::optional<bool> has_stats(const int index) { return find(index) && find(index)->stats; }
		std::optional<int> costume(const int index) { return find(index)->costume; }
		std::optional<bool> has_variant(const int index, const int costume) { return find(index)->variants[static_cast<std::size_t>(costume)] != 0; }
		bool set_costume(const int index, const int costume) { find(index)->costume = costume; return true; }
		std::optional<int> side_records() { return static_cast<int>(records.size()); }
		std::optional<forced_teams_rules::side_record> side_record_at(const int i)
		{
			if (i < 0 || i >= static_cast<int>(records.size())) return std::nullopt;
			return records[static_cast<std::size_t>(i)];
		}
		bool set_side_name(const int record, const int slot, const std::string& name) // as the fill: 31 characters at most
		{
			if (name_fault || record < 0 || record >= static_cast<int>(records.size()) || slot < 0 || slot > 3) return false;
			records[static_cast<std::size_t>(record)].names[static_cast<std::size_t>(slot)] = name.substr(0, forced_teams_rules::side_name_size - 1);
			return true;
		}
		forced_teams_rules::character_lookup find_character(const std::string& name, int& id)
		{
			const auto f = characters.find(name);
			if (f == characters.end()) return forced_teams_rules::character_lookup::missing;
			if (f->second < 0) return forced_teams_rules::character_lookup::not_character;
			id = f->second;
			return forced_teams_rules::character_lookup::found;
		}
		bool run_now(const std::string& line) // pushsidemission, as 0x5f3630: a record when there is a zone and room; cancelsidemission, as 0x5f2ce0
		{
			ran.push_back(line);
			if (line.rfind("pushsidemission ", 0) == 0 && !zone.empty() && records.size() < 2) records.push_back({zone, slots});
			if (line == "cancelsidemission")
			{
				if (cancel_fault) return false;
				if (!records.empty()) records.pop_back();
			}
			return true;
		}
		std::optional<bool> queue(const std::string& line)
		{
			if (queued.size() >= queue_room) return false;
			queued.push_back(line);
			return true;
		}
		std::optional<int> waiting() { return static_cast<int>(queued.size()); }
		std::optional<std::string> current_menu() { return menu; }
		void leave_hud() { ++hud_leaves; }
		// addSkillPoints: the entities a script name means (0x4a7e30), each with its stats name, its word at
		// block+0x14, whether it is a character with stats, whether the stats have talents (the setter writes
		// only then) and xpexempt.
		struct fake_actor
		{
			std::string stats_name;
			int points = 0;
			bool character = true;
			bool stats = true;
			bool talents = true;
			bool exempt = false;
		};
		std::map<std::string, std::vector<fake_actor>> actors;
		bool grant_fault = false;
		std::optional<std::vector<forced_teams_rules::grant>> grant_skill_points(const std::string& name, const int n)
		{
			if (grant_fault) return std::nullopt;
			std::vector<forced_teams_rules::grant> out;
			const auto f = actors.find(name);
			if (f == actors.end()) return out;
			for (auto& a : f->second)
			{
				forced_teams_rules::grant g{};
				std::strncpy(g.name, a.stats_name.c_str(), sizeof(g.name) - 1);
				g.character = a.character;
				g.stats = a.character && a.stats;
				g.exempt = a.exempt;
				if (g.stats)
				{
					g.before = a.points;
					if (a.talents) a.points += n;
					g.after = a.points;
				}
				out.push_back(g);
			}
			return out;
		}
		std::optional<std::string> current_zone() { return zone; }
		std::optional<bool> zone_loading() { return loading; }
		forced_teams_rules::call_state& state() { return kept; }
		// The frame's run of the console queue (0x55c230): every command waiting, in order. As the game's:
		// restorelastzone with no record runs "mainmenuexit 1" (0x5f46bf); with one it seats the record's
		// names and runs "loadmap <zone> 1", which names the zone, loads it and pops the record at once.
		void drain()
		{
			for (const auto& line : queued)
			{
				if (line != "restorelastzone 0")
				{
					ran.push_back(line); // "loadmap <zone> 0 1": the team menu, then the zone
					continue;
				}
				if (records.empty())
				{
					ran.push_back("mainmenuexit 1");
					continue;
				}
				slots = records.back().names;
				zone = records.back().zone;
				records.pop_back();
				loading = deferred;
				ran.push_back("loadmap " + zone + " 1");
			}
			queued.clear();
		}
		std::optional<bool> add_hero(const std::string& name) // 0x46c9f0: true if seated or seated already, slot = count
		{
			added.push_back(name);
			if (add_claims_only) return true;
			std::size_t count = 0;
			for (const auto& s : slots) count += !s.empty();
			if (std::find(slots.begin(), slots.end(), name) != slots.end()) return true;
			if (count >= 4) return false;
			slots[count] = name;
			return true;
		}
		void log(const std::string& line) { lines.push_back(line); }

		bool logged(const std::string_view text) const
		{
			return std::ranges::any_of(lines, [&](const std::string& l) { return l.find(text) != std::string::npos; });
		}
		std::string last() const { return lines.empty() ? std::string() : lines.back(); }
		static int as_int(void* value) { return *static_cast<const int*>(value); }
		static std::string as_text(void* value) { return *static_cast<const std::string*>(value); }
	};

	// A small cast: Iceman in civilian, Magma in default, Colossus in astonishing (a player's pick),
	// Cyclops in 60s and without a civilian costume, Wolverine without a stats object yet;
	// ProfXGladiator known to the registry but not a herostat hero.
	void cast(fake_engine& e)
	{
		e.indices = {{"magma", 5}, {"iceman", 9}, {"colossus", 12}, {"cyclops", 20}, {"wolverine", 21}, {"phoenix", 22}, {"profxgladiator", 40}};
		e.herostats = {5, 9, 12, 20, 21, 22};
		e.heroes.clear();
		fake_engine::hero magma{5, 0, {}, true}, iceman{9, 8, {}, true}, colossus{12, 1, {}, true}, cyclops{20, 3, {}, true}, wolverine{21, 0, {}, false}, phoenix{22, 0, {}, true};
		magma.variants = {1, 0, 0, 0, 0, 0, 0, 0, 3, 0};    // default, civilian (the port's magmacivilian)
		iceman.variants = {1, 2, 0, 0, 0, 0, 0, 0, 5, 0};   // default, astonishing, civilian
		colossus.variants = {1, 2, 0, 0, 0, 0, 0, 0, 4, 0}; // default, astonishing, civilian
		cyclops.variants = {1, 2, 3, 4, 5, 0, 0, 0, 0, 0};  // default .. 70s, no civilian
		phoenix.variants = {1, 0, 0, 4, 5, 0, 0, 0, 0, 0};
		e.heroes = {magma, iceman, colossus, cyclops, wolverine, phoenix};
		e.slots = {"wolverine", "", "", ""};
	}

	// The game's own registry accessors (vt+0x48, +0x4c, +0x74, +0x78, +0x7c), run on a registry of the
	// test's own. No C++ objects: an exception is a failure.
	using registry_count_t = int(__fastcall*)(void* registry, void* edx);
	using registry_short_t = short(__fastcall*)(void* registry, void* edx, int argument);
	using registry_flag_t = bool(__fastcall*)(void* registry, void* edx, int argument);
	using registry_stats_t = std::uint8_t*(__fastcall*)(void* registry, void* edx, int argument);

	bool run_registry_count(const std::uint8_t* code, void* registry, int& out)
	{
		__try { out = reinterpret_cast<registry_count_t>(const_cast<std::uint8_t*>(code))(registry, nullptr); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	bool run_registry_short(const std::uint8_t* code, void* registry, const int argument, int& out)
	{
		__try { out = reinterpret_cast<registry_short_t>(const_cast<std::uint8_t*>(code))(registry, nullptr, argument); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	bool run_registry_flag(const std::uint8_t* code, void* registry, const int argument, bool& out)
	{
		__try { out = reinterpret_cast<registry_flag_t>(const_cast<std::uint8_t*>(code))(registry, nullptr, argument); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	bool run_registry_stats(const std::uint8_t* code, void* registry, const int argument, std::uint8_t*& out)
	{
		__try { out = reinterpret_cast<registry_stats_t>(const_cast<std::uint8_t*>(code))(registry, nullptr, argument); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// The zone manager's vt+0x5c (a pointer) and vt+0x24 (0 or 1 in eax), run on a block of the test's own.
	using zone_accessor_t = std::uintptr_t(__fastcall*)(void* zones, void* edx);
	bool run_zone_accessor(const std::uint8_t* code, void* zones, std::uintptr_t& out)
	{
		__try { out = reinterpret_cast<zone_accessor_t>(const_cast<std::uint8_t*>(code))(zones, nullptr); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// The same cast in a registry laid out as the game's (the hero list's count at +0x12330 and
	// indices at +0x120dc, 0x1c-byte entries at +0x9b28 with the stats handle at +0x10 and the flags
	// at +0x18, the handle mask at +0x9b20, stats objects of 0x4f8 bytes from +4), read with the
	// game's own accessors: what the DLL calls through the registry's vtable. The current zone and its
	// load pending come the same way, from a zone manager block (the name at +0x1e0, the flags at +0x220)
	// read by the game's vt+0x5c (0x483f30) and vt+0x24 (0x483e90).
	struct retail_registry_engine : fake_engine
	{
		const std::uint8_t* image = nullptr;
		std::vector<std::uint8_t> block = std::vector<std::uint8_t>(0x12334 + 0x100, 0);
		std::array<std::uint8_t, 0x230> zones{};
		const std::uint8_t* code(const DWORD va) const { return image + (va - limits_rules::image_base); }
		void put32(const std::size_t offset, const std::uint32_t value) { std::memcpy(block.data() + offset, &value, 4); }
		void put16(const std::size_t offset, const std::uint16_t value) { std::memcpy(block.data() + offset, &value, 2); }

		explicit retail_registry_engine(const std::uint8_t* mapped) : image(mapped)
		{
			cast(*this);
			put32(0x9b20, 0x1f); // the handle mask
			std::uint32_t slot_number = 3;
			for (std::size_t i = 0; i < heroes.size(); ++i)
			{
				const auto& h = heroes[i];
				const std::size_t entry = static_cast<std::size_t>(h.index) * 0x1c;
				put16(0x120dc + 2 * i, static_cast<std::uint16_t>(h.index));
				block[entry + 0x9b40] = herostats.count(h.index) ? 1 : 0;
				if (h.stats)
				{
					put32(entry + 0x9b38, 0x2000 | slot_number); // a handle: generation bits above the mask
					std::uint8_t* stats = block.data() + 4 + slot_number * 0x4f8;
					stats[forced_teams_rules::stats_costume] = static_cast<std::uint8_t>(h.costume);
					std::memcpy(stats + forced_teams_rules::stats_variants, h.variants.data(), h.variants.size());
					slot_number += 4;
				}
			}
			put32(0x12330, static_cast<std::uint32_t>(heroes.size()));
			block[40 * 0x1c + 0x9b40] = 0; // profxgladiator: a character, not a herostat hero
		}
		std::uint8_t* stats(const int index)
		{
			std::uint8_t* s = nullptr;
			return run_registry_stats(code(0x44b890), block.data(), index, s) ? s : nullptr;
		}
		std::optional<int> hero_count()
		{
			int n = 0;
			return run_registry_count(code(0x44b6b0), block.data(), n) ? std::optional<int>(n) : std::nullopt;
		}
		std::optional<int> hero_at(const int i)
		{
			int index = 0;
			return run_registry_short(code(0x44b6e0), block.data(), i, index) ? std::optional<int>(index) : std::nullopt;
		}
		std::optional<bool> has_stats(const int index)
		{
			bool value = false;
			return run_registry_flag(code(0x44b7a0), block.data(), index, value) ? std::optional<bool>(value) : std::nullopt;
		}
		std::optional<bool> herostat(const int index)
		{
			bool value = false;
			return run_registry_flag(code(0x44b7c0), block.data(), index, value) ? std::optional<bool>(value) : std::nullopt;
		}
		std::optional<int> costume(const int index)
		{
			const auto* s = stats(index);
			return s ? std::optional<int>(s[forced_teams_rules::stats_costume]) : std::nullopt;
		}
		std::optional<bool> has_variant(const int index, const int costume)
		{
			const auto* s = stats(index);
			return s ? std::optional<bool>(s[forced_teams_rules::stats_variants + costume] != 0) : std::nullopt;
		}
		bool set_costume(const int index, const int costume)
		{
			auto* s = stats(index);
			if (s) s[forced_teams_rules::stats_costume] = static_cast<std::uint8_t>(costume);
			return s != nullptr;
		}
		std::optional<std::string> current_zone()
		{
			std::memset(zones.data() + 0x1e0, 0, forced_teams_rules::zone_name_size);
			std::memcpy(zones.data() + 0x1e0, zone.data(), std::min(zone.size(), forced_teams_rules::zone_name_size - 1));
			std::uintptr_t name = 0;
			if (!run_zone_accessor(code(0x483f30), zones.data(), name) || name != reinterpret_cast<std::uintptr_t>(zones.data() + 0x1e0)) return std::nullopt;
			return std::string(reinterpret_cast<const char*>(name));
		}
		std::optional<bool> zone_loading()
		{
			zones[0x220] = static_cast<std::uint8_t>(loading ? 0x02 | 0x04 : 0x04); // bit 1: the load asked for; bit 2 isn't a pending load
			std::uintptr_t pending = 0;
			if (!run_zone_accessor(code(0x483e90), zones.data(), pending) || pending > 1) return std::nullopt;
			return pending == 1;
		}
	};

	// The functions' work on a game of the test's own. `vtables` and the engine's `get` are the test's
	// own argument reader, or the game's (0x4d5830, 0x55d7e0 in a copy of XMen2.exe).
	template <typename Engine>
	void check_forced_team_functions(Engine& e, const value_vtables& vtables)
	{
		using namespace forced_teams_rules;
		const auto costume_of = [&](const int index) { return e.costume(index).value_or(-1); };

		// xml2fixFeature: the switches at the call.
		{
			script_call c(vtables);
			c.text("forcedteams");
			e.forced = true;
			e.add_on = false;
			CHECK(fake_engine::as_int(xml2fix_feature(e, &c.args)) == 1 && e.logged("xml2fixFeature(\"forcedteams\") -> 1"));
			script_call a(vtables);
			a.text(" AddHero ");
			CHECK(fake_engine::as_int(xml2fix_feature(e, &a.args)) == 0);
			e.add_on = true;
			CHECK(fake_engine::as_int(xml2fix_feature(e, &a.args)) == 1);
			e.forced = false; // AddHero counts only with ForcedTeams
			CHECK(fake_engine::as_int(xml2fix_feature(e, &a.args)) == 0 && fake_engine::as_int(xml2fix_feature(e, &c.args)) == 0);
			script_call u(vtables);
			u.text("xml2fixversion");
			e.forced = true;
			CHECK(fake_engine::as_int(xml2fix_feature(e, &u.args)) == 0 && e.last().find("not a feature") != std::string::npos);
			e.add_on = false;
		}

		// seatParty: exactly these heroes, compacted; all or nothing.
		cast(e);
		{
			script_call c(vtables);
			c.text("Magma").text("").text("").text("");
			CHECK(seat_party(e, &c.args) == nullptr);
			CHECK((e.slots == party{"magma", "", "", ""}));
			CHECK(e.last() == "forced teams: seatParty(\"Magma\", \"\", \"\", \"\") -> magma / - / - / - (was wolverine / - / - / -)");
		}
		{
			script_call c(vtables);
			c.text("").text("wolverine").text("").text("cyclops");
			seat_party(e, &c.args);
			CHECK((e.slots == party{"wolverine", "cyclops", "", ""}));
		}
		{
			script_call c(vtables);
			c.text("cyclops").text("phoenix").text("CYCLOPS").text("wolverine");
			seat_party(e, &c.args);
			CHECK((e.slots == party{"cyclops", "phoenix", "wolverine", ""}) && e.last().find("(cyclops named twice - seated once)") != std::string::npos);
		}
		{
			script_call c(vtables);
			c.text("profxgladiator").text("").text("").text("");
			seat_party(e, &c.args);
			CHECK((e.slots == party{"cyclops", "phoenix", "wolverine", ""}) && e.last().find("'profxgladiator' isn't a herostat hero - party left as it is") != std::string::npos);
			script_call n(vtables);
			n.text("magma").text("nobody").text("").text("");
			seat_party(e, &n.args);
			CHECK((e.slots == party{"cyclops", "phoenix", "wolverine", ""}) && e.last().find("'nobody' isn't a character the game knows") != std::string::npos);
			script_call z(vtables);
			z.text("").text(" ").text("").text("");
			seat_party(e, &z.args);
			CHECK((e.slots == party{"cyclops", "phoenix", "wolverine", ""}) && e.last().find("no hero named - party left as it is") != std::string::npos);
			script_call s(vtables); // three arguments: the game's compiler drops such a call, but the handler must cope
			s.text("magma").text("").text("");
			seat_party(e, &s.args);
			CHECK((e.slots == party{"cyclops", "phoenix", "wolverine", ""}) && e.last().find("couldn't read argument 4 - nothing done") != std::string::npos);
		}
		{
			e.seat_fault_at = 2;
			script_call c(vtables);
			c.text("magma").text("iceman").text("colossus").text("");
			seat_party(e, &c.args);
			CHECK(e.last().find("ERROR: the game's slot setter (game vt+0xf0) faulted at slot 2") != std::string::npos);
			e.seat_fault_at = -1;
		}

		// setSkinset: the listed heroes that have the costume, default for every other mission costume.
		cast(e);
		{
			script_call c(vtables);
			c.text("civilian").text("magma");
			CHECK(set_skinset(e, &c.args) == nullptr);
			CHECK(costume_of(5) == 8 && costume_of(9) == 0 && costume_of(12) == 1 && costume_of(20) == 0 && costume_of(22) == 0);
			CHECK(e.last() == "forced teams: setSkinset(\"civilian\", \"magma\") -> magma civilian; 2 other heroes back to default from a mission costume");
		}
		{
			script_call c(vtables); // XML1's civilian skinset: only Iceman and Colossus had skin_civilian
			c.text("civilian").text("iceman,colossus, CYCLOPS,beast");
			set_skinset(e, &c.args);
			CHECK(costume_of(5) == 0 && costume_of(9) == 8 && costume_of(12) == 1 && costume_of(20) == 0);
			CHECK(e.last().find("iceman civilian, colossus keeps astonishing (the player's pick), cyclops default (has no civilian costume); 1 other hero back to default") != std::string::npos &&
			      e.last().find("not herostat heroes, skipped: beast") != std::string::npos);
		}
		{
			script_call c(vtables);
			c.text("60s").text("beast,cyclops,iceman,phoenix,wolverine");
			set_skinset(e, &c.args);
			CHECK(costume_of(20) == 3 && costume_of(9) == 0 && costume_of(22) == 3 && costume_of(12) == 1 && costume_of(5) == 0);
			CHECK(e.last().find("cyclops 60s, iceman default (has no 60s costume), phoenix 60s, wolverine skipped (no stats object yet); 0 other heroes") != std::string::npos);
			script_call d(vtables);
			d.text("default").text("");
			set_skinset(e, &d.args);
			CHECK(costume_of(20) == 0 && costume_of(22) == 0 && costume_of(12) == 1 && e.last().find("2 other heroes back to default") != std::string::npos);
			script_call m(vtables);
			m.text("magmacivilian").text("magma");
			set_skinset(e, &m.args);
			CHECK(costume_of(5) == 0 && e.last().find("'magmacivilian' isn't one of the game's costumes") != std::string::npos);
		}

		// pushParty / popParty: XML1's side missions on the side-mission stack.
		cast(e);
		e.slots = {"magma", "", "", ""};
		e.characters = {{"_ACTIVE_HERO_", 77}, {"sp_crate01", -1}};
		{
			script_call c(vtables);
			c.text("_ACTIVE_HERO_");
			CHECK(push_party(e, &c.args) == nullptr);
			CHECK(e.records.size() == 1 && e.ran.back() == "pushsidemission 77");
			CHECK(e.last() == "forced teams: pushParty(\"_ACTIVE_HERO_\") -> pushsidemission 77: record 1 of 2, mansion/man1b/subbasement2 with magma / - / - / -");
			// the flashback party in its zone, then back
			script_call s(vtables);
			s.text("cyclops").text("colossus").text("iceman").text("phoenix");
			seat_party(e, &s.args);
			CHECK((e.slots == party{"cyclops", "colossus", "iceman", "phoenix"}));
			e.zone = "mansion/jugrnt/jugrnt01";
			script_call p(vtables);
			p.text("mansion/man1b/subbasement1b");
			CHECK(pop_party(e, &p.args) == nullptr);
			CHECK(e.queued.size() == 1 && e.queued[0] == "restorelastzone 0" && e.hud_leaves == 1);
			CHECK(e.last() == "forced teams: popParty(\"mansion/man1b/subbasement1b\") -> restorelastzone 0 queued, back to mansion/man1b/subbasement2 with magma / - / - / - (record 1)");
			// XML1's sent_fb end is every sentinel's death script: a second end before the frame runs the
			// queue queues nothing, so the stack is popped once and never found empty ("mainmenuexit 1")
			const auto lines_before = e.lines.size();
			CHECK(pop_party(e, &p.args) == nullptr);
			CHECK(e.queued.size() == 1 && e.hud_leaves == 1 && e.lines.size() == lines_before + 1 &&
			      e.last() == "forced teams: popParty(\"mansion/man1b/subbasement1b\"): 'restorelastzone 0' is queued already, from mansion/jugrnt/jugrnt01 (the game runs it at its next frame) - "
			                  "nothing done (one popParty per side mission's end)");
			e.drain();
			CHECK(e.records.empty() && (e.slots == party{"magma", "", "", ""}) && e.zone == "mansion/man1b/subbasement2" && e.ran.back() == "loadmap mansion/man1b/subbasement2 1" &&
			      std::ranges::count(e.ran, std::string("mainmenuexit 1")) == 0);
			// what the check keeps from happening: two restores in one run of the queue
			e.records = {{"mansion/man1b/subbasement2", {"magma", "", "", ""}}};
			e.queued = {"restorelastzone 0", "restorelastzone 0"};
			e.drain();
			CHECK(e.records.empty() && e.ran.back() == "mainmenuexit 1");
			e.ran.clear();
		}
		{
			// A load not in at once, the zone being left still running a script: the zone manager names the
			// new zone first and reports the load pending until it is in.
			e.records = {{"mansion/man2/mansion2_1", {"magma", "", "", ""}}};
			e.zone = "nyc/fb/nyc_fb4";
			e.deferred = true;
			script_call p(vtables);
			p.text("mansion/man2/mansion2_1");
			pop_party(e, &p.args);
			e.drain();
			CHECK(e.records.empty() && e.zone == "mansion/man2/mansion2_1" && e.loading && e.queued.empty());
			pop_party(e, &p.args);
			CHECK(e.queued.empty() &&
			      e.last().find("popParty(\"mansion/man2/mansion2_1\"): 'restorelastzone 0' has run and the load of mansion/man2/mansion2_1 is under way - nothing done") != std::string::npos);
			e.loading = false; // in: a popParty now is another end (with no record: the team menu)
			pop_party(e, &p.args);
			CHECK(e.queued.size() == 1 && e.queued[0] == "loadmap mansion/man2/mansion2_1 0 1");
			pop_party(e, &p.args); // the team menu, twice before the queue runs: once
			CHECK(e.queued.size() == 1 && e.last().find("'loadmap mansion/man2/mansion2_1 0 1' is queued already, from mansion/man2/mansion2_1") != std::string::npos);
			e.drain(); // the team menu is up; a call now asks for it again, as loadMapChooseTeam twice would
			pop_party(e, &p.args);
			CHECK(e.queued.size() == 1 && e.last().find("-> loadmap mansion/man2/mansion2_1 0 1 queued") != std::string::npos);
			e.drain();
			e.deferred = false;
		}
		{
			// A side mission inside one: each end goes back one record.
			e.records = {{"x1/a0", {"magma", "", "", ""}}, {"X1/A1", {"cyclops", "", "", ""}}};
			e.zone = "x1/b";
			script_call p(vtables);
			p.text("x1/fallback");
			pop_party(e, &p.args);
			e.drain();
			CHECK(e.records.size() == 1 && e.zone == "X1/A1" && (e.slots == party{"cyclops", "", "", ""}));
			pop_party(e, &p.args); // from X1/A1, its load in: the next end
			CHECK(e.queued.size() == 1 && e.queued[0] == "restorelastzone 0" && e.last().find("back to x1/a0 with magma") != std::string::npos);
			e.drain();
			CHECK(e.records.empty() && e.zone == "x1/a0" && (e.slots == party{"magma", "", "", ""}));
			e.ran.clear();
			e.zone = "mansion/man1b/subbasement2";
		}
		{
			e.queued.clear();
			e.records.clear();
			const auto leaves = e.hud_leaves;
			script_call p(vtables); // no record: the team menu at the fallback zone, as loadMapChooseTeam
			p.text(" mansion/man2/subbasement2 ");
			pop_party(e, &p.args);
			CHECK(e.queued.size() == 1 && e.queued[0] == "loadmap mansion/man2/subbasement2 0 1" && e.hud_leaves == leaves + 1 &&
			      e.last().find("no side-mission record: the team menu at") != std::string::npos);
			e.queued.clear();
			script_call bad(vtables);
			bad.text("two words");
			pop_party(e, &bad.args);
			CHECK(e.queued.empty() && e.last().find("can't go in a loadmap command - nothing done") != std::string::npos);
			e.queue_room = 1; // one command waiting already
			e.queued = {"loadmap nyc/fb/nyc_fb1 1"};
			pop_party(e, &p.args);
			CHECK(e.queued.size() == 1 && e.hud_leaves == leaves + 1 && e.last().find("ERROR: the console's queue refused 'loadmap mansion/man2/subbasement2 0 1'") != std::string::npos);
			e.queue_room = 2;
			e.queued.clear();
			e.records = {{"nyc/fb/nyc_fb1", {"magma", "", "", ""}}};
			e.menu = "Loading";
			pop_party(e, &p.args);
			CHECK(e.queued.empty() && e.last().find("the game is loading - nothing done") != std::string::npos);
			e.menu.clear();
		}
		{
			e.records = {{"a", {}}, {"b", {}}};
			e.ran.clear();
			script_call c(vtables);
			c.text("_ACTIVE_HERO_");
			push_party(e, &c.args);
			CHECK(e.ran.empty() && e.records.size() == 2 && e.last().find("the side-mission stack is full (2 records) - nothing pushed; popParty returns to b with - / - / - / -") != std::string::npos);
			e.records = {{"a", {}}};
			push_party(e, &c.args);
			CHECK(e.records.size() == 2 &&
			      e.last().find("record 2 of 2, mansion/man1b/subbasement2 with magma / - / - / - (below it a with - / - / - / -: a side mission inside a side mission") != std::string::npos);
			e.ran.clear();
			e.records.clear();
			script_call m(vtables);
			m.text("nobody");
			push_party(e, &m.args);
			script_call n(vtables);
			n.text("sp_crate01");
			const auto lines_before = e.lines.size();
			push_party(e, &n.args);
			CHECK(e.ran.empty() && e.records.empty() && e.lines.size() == lines_before + 1 && e.logged("pushParty(\"nobody\"): no entity by that name - nothing pushed") &&
			      e.last().find("that entity isn't a character - nothing pushed") != std::string::npos);
			e.zone.clear(); // no zone loaded: the game pushes nothing
			push_party(e, &c.args);
			CHECK(e.ran.size() == 1 && e.records.empty() && e.last().find("pushsidemission 77 pushed nothing (no zone loaded?)") != std::string::npos);
			e.zone = "mansion/man1b/subbasement2";
		}
		{
			// ForcedTeams off with a record on the stack (pushed with it on, a game saved in the flashback):
			// nothing will pop it - warned once for each stack seen.
			e.records = {{"mansion/man2/mansion2_1", {"magma", "", "", ""}}};
			e.forced = false;
			script_call c(vtables);
			c.text("forcedteams");
			const auto lines_before = e.lines.size();
			CHECK(fake_engine::as_int(xml2fix_feature(e, &c.args)) == 0 && e.lines.size() == lines_before + 2 &&
			      e.last().find("WARNING: ForcedTeams is off but the side-mission stack holds a record (the top: mansion/man2/mansion2_1 with magma / - / - / -)") != std::string::npos);
			xml2fix_feature(e, &c.args);
			CHECK(e.lines.size() == lines_before + 3);
			e.records.push_back({"nyc/fb/nyc_fb4", {"cyclops", "", "", ""}});
			xml2fix_feature(e, &c.args);
			CHECK(e.lines.size() == lines_before + 5 && e.last().find("holds 2 records (the top: nyc/fb/nyc_fb4 with cyclops") != std::string::npos);
			script_call a(vtables);
			a.text("addhero");
			xml2fix_feature(e, &a.args);
			e.forced = true;
			CHECK(fake_engine::as_int(xml2fix_feature(e, &c.args)) == 1 && e.lines.size() == lines_before + 7);
			e.forced = false;
			e.records.clear();
			xml2fix_feature(e, &c.args);
			e.records = {{"mansion/man2/mansion2_1", {"magma", "", "", ""}}};
			xml2fix_feature(e, &c.args);
			CHECK(e.lines.size() == lines_before + 10 && e.last().find("WARNING") != std::string::npos);
			e.forced = true;
			e.records.clear();
		}

		// addHero: off by default; on, the game's own routine seats at slot = count.
		cast(e);
		e.slots = {"magma", "", "", ""};
		{
			script_call c(vtables);
			c.text("cyclops");
			e.add_on = false;
			CHECK(fake_engine::as_int(add_hero(e, &c.args)) == 0 && e.added.empty() && e.last().find("-> 0: [Game] AddHero is off") != std::string::npos);
			e.add_on = true;
			CHECK(fake_engine::as_int(add_hero(e, &c.args)) == 1 && (e.slots == party{"magma", "cyclops", "", ""}) && e.added.size() == 1);
			CHECK(e.last() == "forced teams: addHero(\"cyclops\") -> 1: magma / cyclops / - / - (was magma / - / - / -)");
			script_call g(vtables);
			g.text("profxgladiator");
			CHECK(fake_engine::as_int(add_hero(e, &g.args)) == 0 && e.added.size() == 1);
			e.slots = {"magma", "iceman", "colossus", "phoenix"};
			CHECK(fake_engine::as_int(add_hero(e, &c.args)) == 0 && e.last().find("refused (party full?)") != std::string::npos);
			// In game the routine returned true and seated nobody: that is a 0, so the script's T7 fallback runs.
			e.slots = {"wolverine", "", "", ""};
			e.add_claims_only = true;
			CHECK(fake_engine::as_int(add_hero(e, &c.args)) == 0 && e.last().find("is in no party slot") != std::string::npos);
			e.add_claims_only = false;
			e.slots = {"magma", "iceman", "colossus", "phoenix"};
			e.add_on = false;
		}

		// getPartyMember.
		{
			script_call c(vtables);
			c.number(1);
			CHECK(fake_engine::as_text(get_party_member(e, &c.args)) == "iceman" && e.last() == "forced teams: getPartyMember(1) -> \"iceman\"");
			script_call o(vtables);
			o.number(4);
			CHECK(fake_engine::as_text(get_party_member(e, &o.args)).empty());
		}

		// joinHero: the spot saved with the hero in the record's party, then restorelastzone 0 - the zone
		// reloads with him and the record comes off. A refusal changes nothing.
		const auto join_setup = [&](const party& slots)
		{
			cast(e);
			e.slots = slots;
			e.records.clear();
			e.queued.clear();
			e.ran.clear();
			e.kept = {};
			e.queue_room = 2;
			e.menu.clear();
			e.loading = false;
			e.deferred = false;
			e.forced = true;
			e.join_on = true;
			e.name_fault = false;
			e.cancel_fault = false;
			e.zone = "nyc/alison/nyc1_1_3";
			e.characters = {{"_ACTIVE_HERO_", 77}};
		};
		join_setup({"wolverine", "", "", ""});
		{
			script_call c(vtables);
			c.text(" Cyclops ");
			const auto leaves = e.hud_leaves;
			CHECK(fake_engine::as_int(join_hero(e, &c.args)) == 1);
			CHECK(e.ran.size() == 1 && e.ran[0] == "pushsidemission 77" && e.records.size() == 1 && (e.records[0].names == party{"wolverine", "cyclops", "", ""}) &&
			      e.records[0].zone == "nyc/alison/nyc1_1_3" && (e.slots == party{"wolverine", "", "", ""}) && e.queued.size() == 1 && e.queued[0] == "restorelastzone 0" &&
			      e.hud_leaves == leaves + 1);
			CHECK(e.last() == "forced teams: joinHero(\" Cyclops \") -> 1: pushsidemission 77 (record 1 of 2), cyclops added to its party in slot 2, restorelastzone 0 queued: "
			                  "nyc/alison/nyc1_1_3 reloads at the saved spot with wolverine / cyclops / - / - (was wolverine / - / - / -) and the record comes off");
			// The trigger again before the frame runs the queue: that reload adds him already.
			const auto lines_before = e.lines.size();
			CHECK(fake_engine::as_int(join_hero(e, &c.args)) == 1 && e.records.size() == 1 && e.queued.size() == 1 && e.ran.size() == 1 && e.lines.size() == lines_before + 1 &&
			      e.last().find("'restorelastzone 0' is queued already, from nyc/alison/nyc1_1_3 (the game runs it at its next frame) - it adds cyclops already") != std::string::npos);
			// So does a popParty meanwhile: one restorelastzone, one record.
			script_call p(vtables);
			p.text("mansion/man2/subbasement2");
			pop_party(e, &p.args);
			CHECK(e.queued.size() == 1 && e.last().find("nothing done (one popParty per side mission's end)") != std::string::npos);
			// The frame: restorelastzone 0 seats the record's names, reloads the zone and pops the record.
			e.drain();
			CHECK(e.records.empty() && (e.slots == party{"wolverine", "cyclops", "", ""}) && e.zone == "nyc/alison/nyc1_1_3" && e.ran.back() == "loadmap nyc/alison/nyc1_1_3 1" &&
			      std::ranges::count(e.ran, std::string("mainmenuexit 1")) == 0);
			// In the party now: nothing to do, and not 0 (the script's team-menu fallback stays shut).
			CHECK(fake_engine::as_int(join_hero(e, &c.args)) == 2 && e.queued.empty() && e.records.empty() && e.ran.size() == 2 &&
			      e.last() == "forced teams: joinHero(\" Cyclops \") -> 2: cyclops is in the party already (wolverine / cyclops / - / -) - nothing to do");
		}
		{
			// Magma alone (dr_mag2's mag_nyc4), and a party with a gap: the first empty slot.
			join_setup({"magma", "", "", ""});
			script_call c(vtables);
			c.text("cyclops");
			CHECK(fake_engine::as_int(join_hero(e, &c.args)) == 1 && (e.records[0].names == party{"magma", "cyclops", "", ""}));
			join_setup({"wolverine", "", "phoenix", ""});
			CHECK(fake_engine::as_int(join_hero(e, &c.args)) == 1 && (e.records[0].names == party{"wolverine", "cyclops", "phoenix", ""}));
			e.drain();
			CHECK((e.slots == party{"wolverine", "cyclops", "phoenix", ""}) && e.records.empty());
			// A load the frame starts but doesn't finish: a call meanwhile finds him seated.
			join_setup({"wolverine", "", "", ""});
			e.deferred = true;
			join_hero(e, &c.args);
			e.drain();
			CHECK(e.loading && fake_engine::as_int(join_hero(e, &c.args)) == 2 && e.queued.empty());
		}
		{
			// Refusals: 0, nothing pushed, nothing queued, the party as it was.
			const auto refused = [&](const std::string& hero, const std::string& why)
			{
				script_call c(vtables);
				c.text(hero);
				const auto slots = e.slots;
				const auto records = e.records.size();
				const auto queued = e.queued.size();
				const bool zero = fake_engine::as_int(join_hero(e, &c.args)) == 0;
				const bool unchanged = e.slots == slots && e.records.size() == records && e.queued.size() == queued;
				const bool logged = e.last().find(" -> 0: " + why) != std::string::npos && e.last().find("the script's own fallback runs") != std::string::npos;
				if (!zero || !unchanged || !logged) std::printf("  info  joinHero(%s): %s\n", hero.c_str(), e.last().c_str());
				return zero && unchanged && logged;
			};
			join_setup({"wolverine", "", "", ""});
			e.join_on = false;
			CHECK(refused("cyclops", "[Game] JoinHero=0") && e.ran.empty());
			e.join_on = true;
			e.forced = false;
			CHECK(refused("cyclops", "[Game] ForcedTeams is off"));
			e.forced = true;
			CHECK(refused("", "no hero named") && refused("profxgladiator", "'profxgladiator' isn't a herostat hero") && refused("nobody", "'nobody' isn't a character the game knows"));
			CHECK(refused(std::string(32, 'x'), "'" + std::string(32, 'x') + "' is longer than a side-mission record's names (31 characters)"));
			e.slots = {"wolverine", "magma", "iceman", "colossus"};
			CHECK(refused("cyclops", "the party is full (wolverine / magma / iceman / colossus)"));
			e.slots = {"wolverine", "", "", ""};
			e.menu = "Loading";
			CHECK(refused("cyclops", "the game is loading"));
			e.menu.clear();
			e.loading = true;
			CHECK(refused("cyclops", "a zone load is under way"));
			e.loading = false;
			e.queued = {"loadmap nyc/alison/nyc1_1_4 0 0"};
			CHECK(refused("cyclops", "1 console command(s) waiting already (the reload must be the only one)"));
			e.queued.clear();
			e.records = {{"a", {}}, {"b", {}}};
			CHECK(refused("cyclops", "the side-mission stack is full (2 records; the top: b with - / - / - / -)"));
			e.records.clear();
			e.characters.clear();
			CHECK(refused("cyclops", "no _ACTIVE_HERO_ entity"));
			e.characters = {{"_ACTIVE_HERO_", -1}};
			CHECK(refused("cyclops", "_ACTIVE_HERO_ isn't a character"));
			e.characters = {{"_ACTIVE_HERO_", 77}};
			CHECK(e.ran.empty());
			e.zone.clear(); // no zone loaded: the game pushes nothing
			CHECK(refused("cyclops", "pushsidemission 77 pushed nothing (no zone loaded?)") && e.ran.size() == 1);
			e.zone = "nyc/alison/nyc1_1_3";
			// A popParty's reload on its way: not a join of this hero - refused (its record, its command).
			e.records = {{"mansion/man2/mansion2_1", {"magma", "", "", ""}}};
			script_call p(vtables);
			p.text("mansion/man2/mansion2_1");
			pop_party(e, &p.args);
			CHECK(e.queued.size() == 1 && refused("cyclops", "'restorelastzone 0' is queued already"));
			e.drain();
			CHECK(e.records.empty() && (e.slots == party{"magma", "", "", ""}));
			// After the push: the record comes off again through the game's cancelsidemission.
			join_setup({"wolverine", "", "", ""});
			e.name_fault = true;
			CHECK(refused("cyclops", "ERROR: writing the name into record 1 faulted") && e.ran.size() == 2 && e.ran[1] == "cancelsidemission" &&
			      e.logged("the record pushed for it came off again (cancelsidemission) - the stack is as it was, 0 record(s)"));
			e.name_fault = false;
			e.queue_room = 0; // the queue refuses (in the game only with two waiting, checked before the push)
			CHECK(refused("cyclops", "the console's queue refused 'restorelastzone 0'") && e.ran.back() == "cancelsidemission");
			e.queue_room = 2;
			e.records = {{"a", {"wolverine", "", "", ""}}};
			e.name_fault = true;
			e.cancel_fault = true;
			script_call c(vtables);
			c.text("cyclops");
			CHECK(fake_engine::as_int(join_hero(e, &c.args)) == 0 && e.records.size() == 2 &&
			      e.logged("ERROR: cancelsidemission faulted - a record may stay on the side-mission stack"));
			join_setup({"wolverine", "", "", ""});
		}

		// addSkillPoints: n to the word at block+0x14 of every character the name means, through the
		// game's own setter; a refusal changes nothing.
		{
			e.actors = {{"wolverine", {{"Wolverine", 2}}},
			            {"_ALL_HEROES_", {{"Wolverine", 2}, {"Magma", 0, true, true, true, true}, {"", 0, false}, {"Ghost", 0, true, false}}},
			            {"dummy", {{"Dummy", 5, true, true, false}}}};
			script_call c(vtables);
			c.text("wolverine").number(1);
			CHECK(add_skill_points(e, &c.args) == nullptr && e.actors["wolverine"][0].points == 3 &&
			      e.last() == "forced teams: addSkillPoints(\"wolverine\", 1) -> Wolverine: unspent skill points 2 -> 3");
			script_call all(vtables);
			all.text("_ALL_HEROES_").number(2);
			CHECK(add_skill_points(e, &all.args) == nullptr && e.actors["_ALL_HEROES_"][0].points == 4 && e.actors["_ALL_HEROES_"][1].points == 2 &&
			      e.last() == "forced teams: addSkillPoints(\"_ALL_HEROES_\", 2) -> Wolverine: unspent skill points 2 -> 4; Magma: unspent skill points 0 -> 2 (xpexempt: the game shows this "
			                  "hero no points); (unnamed): not a character, skipped; Ghost: no stats, skipped");
			script_call d(vtables);
			d.text("dummy").number(1);
			CHECK(add_skill_points(e, &d.args) == nullptr && e.actors["dummy"][0].points == 5 &&
			      e.last() == "forced teams: addSkillPoints(\"dummy\", 1) -> Dummy: unspent skill points 5 -> 5 (NOT 6: the game's setter writes only for stats with talents)");
			const auto refused = [&](const std::string& name, const int n, const std::string& why)
			{
				const int before = e.actors["wolverine"][0].points;
				script_call r(vtables);
				r.text(name).number(n);
				const bool none = add_skill_points(e, &r.args) == nullptr && e.actors["wolverine"][0].points == before && e.last().find(why) != std::string::npos;
				if (!none) std::printf("  info  addSkillPoints(%s, %d): %s\n", name.c_str(), n, e.last().c_str());
				return none;
			};
			CHECK(refused("wolverine", 0, "the count isn't 1 to 20 - nothing done") && refused("wolverine", 21, "the count isn't 1 to 20") && refused("wolverine", -1, "the count isn't 1 to 20"));
			CHECK(refused("nobody", 1, "no entity of that name - nothing done"));
			e.grant_fault = true;
			CHECK(refused("wolverine", 1, "ERROR: the game's name resolver (0x4a7e30) or the stats faulted - nothing done"));
			e.grant_fault = false;
			script_call one(vtables);
			one.text("wolverine"); // no count
			CHECK(add_skill_points(e, &one.args) == nullptr && e.actors["wolverine"][0].points == 3 && e.last() == "forced teams: addSkillPoints: ERROR: couldn't read the count (argument 2) - nothing done");
			script_call feature(vtables);
			feature.text("skillpoints");
			CHECK(fake_engine::as_int(xml2fix_feature(e, &feature.args)) == 1 && e.last() == "forced teams: xml2fixFeature(\"skillpoints\") -> 1");
			e.forced = false;
			CHECK(fake_engine::as_int(xml2fix_feature(e, &feature.args)) == 1); // registered with the rest, whatever the switches
			e.forced = true;
		}
	}

	void check_forced_teams_rules()
	{
		using namespace forced_teams_rules;
		std::printf("forced parties ([Game] ForcedTeams, AddHero and JoinHero: seatParty, setSkinset, pushParty, popParty, addHero, joinHero, addSkillPoints)\n");

		// The functions: ten, signatures the game's compiler knows, room in its tree.
		CHECK(functions.size() == 10 && table_count == 0x12b && builtin_count + table_count == 318 && table_count + builtin_count <= tree_capacity);
		bool signatures_ok = true;
		for (const auto& f : functions)
		{
			signatures_ok &= std::strlen(f.ret) == 1 && std::strchr("nis", f.ret[0]) && std::strlen(f.args) >= 1 && std::strlen(f.args) <= 7 &&
			                 std::strspn(f.args, "sia") == std::strlen(f.args);
		}
		CHECK(signatures_ok);
		CHECK(std::string_view(functions[1].name) == "seatParty" && std::string_view(functions[1].args) == "ssss" && std::string_view(functions[0].name) == "xml2fixFeature" &&
		      std::string_view(functions[static_cast<std::size_t>(function::get_party_member)].name) == "getPartyMember");
		CHECK(std::string_view(functions[static_cast<std::size_t>(function::join_hero)].name) == "joinHero" && std::string_view(functions[static_cast<std::size_t>(function::join_hero)].ret) == "i" &&
		      std::string_view(functions[static_cast<std::size_t>(function::join_hero)].args) == "s");
		const auto writes = registration_writes(0x12345678);
		CHECK(writes[0].va == 0x49fe31 && writes[0].value == 0x12345678 && writes[1].va == 0x49fe36 && writes[1].value == 0x12b && writes[0].size == 4 && writes[1].size == 4);
		CHECK(std::string_view(functions[static_cast<std::size_t>(function::skill_points)].name) == "addSkillPoints" && std::string_view(functions[static_cast<std::size_t>(function::skill_points)].ret) == "n" &&
		      std::string_view(functions[static_cast<std::size_t>(function::skill_points)].args) == "ai" && skill_points_max == 20 && grant_max >= 4);

		// The ini: absent = nothing; 0 = registered, off; 1 = on; AddHero only with ForcedTeams=1.
		auto chosen = decide(std::nullopt, std::nullopt);
		CHECK(!chosen.registered && !chosen.forced_teams && !chosen.add_hero && chosen.notes.empty());
		chosen = decide("1", std::nullopt);
		CHECK(chosen.registered && chosen.forced_teams && !chosen.add_hero && chosen.notes.empty());
		chosen = decide("0", "1");
		CHECK(chosen.registered && !chosen.forced_teams && !chosen.add_hero && chosen.notes.size() == 1 && chosen.notes[0].find("needs ForcedTeams=1") != std::string::npos);
		chosen = decide("1   ; the mod forces its parties", "1 ; experimental");
		CHECK(chosen.registered && chosen.forced_teams && chosen.add_hero && chosen.notes.empty());
		chosen = decide("1", "0");
		CHECK(chosen.forced_teams && !chosen.add_hero && chosen.notes.empty());
		chosen = decide("yes", "1");
		CHECK(chosen.registered && !chosen.forced_teams && !chosen.add_hero && chosen.notes.size() == 2 && chosen.notes[0] == "ForcedTeams=yes isn't 0 or 1 - taken as 0");
		chosen = decide(std::nullopt, "1");
		CHECK(!chosen.registered && chosen.notes.size() == 1 && chosen.notes[0].find("nothing registered") != std::string::npos);
		chosen = decide("1", "2");
		CHECK(chosen.forced_teams && !chosen.add_hero && chosen.notes.size() == 1);
		// JoinHero: on with ForcedTeams=1 unless 0.
		CHECK(decide("1", std::nullopt).join_hero && decide("1", "0", "1").join_hero && decide("1", "0", " 1 ; the join reloads").join_hero);
		chosen = decide("1", std::nullopt, "0 ; the team menu instead");
		CHECK(chosen.forced_teams && !chosen.join_hero && chosen.notes.empty());
		chosen = decide("0", std::nullopt, "1");
		CHECK(chosen.registered && !chosen.join_hero && chosen.notes.size() == 1 && chosen.notes[0] == "JoinHero=1 needs ForcedTeams=1 - joinHero reports off");
		CHECK(!decide("0", std::nullopt).join_hero && decide("0", std::nullopt).notes.empty());
		chosen = decide("1", std::nullopt, "yes");
		CHECK(chosen.join_hero && chosen.notes.size() == 1 && chosen.notes[0] == "JoinHero=yes isn't 0 or 1 - taken as 1 (the default)");
		chosen = decide(std::nullopt, std::nullopt, "1");
		CHECK(!chosen.registered && !chosen.join_hero && chosen.notes.size() == 1 && chosen.notes[0].find("JoinHero is set but ForcedTeams isn't") != std::string::npos);
		CHECK(feature_named("forcedteams") == feature::forced_teams && feature_named(" AddHero") == feature::add_hero && feature_named("JoinHero") == feature::join_hero &&
		      feature_named("SkillPoints") == feature::skill_points && !feature_named("forced_teams") && !feature_named(""));

		// Names: cleaned, repeats dropped, compacted.
		CHECK((split_heroes("magma,,x") == std::vector<std::string>{"magma", "x"}));
		CHECK((split_heroes(" Iceman , COLOSSUS,iceman,") == std::vector<std::string>{"iceman", "colossus"}) && split_heroes("").empty() && split_heroes(" , ,").empty());
		CHECK((split_heroes("beast,cyclops,iceman,phoenix,wolverine").size() == 5));
		auto order = seat_order({"", " Wolverine", "", "cyclops"});
		CHECK((order.slots == party{"wolverine", "cyclops", "", ""}) && order.dropped.empty());
		order = seat_order({"magma", "MAGMA", "magma ", "iceman"});
		CHECK((order.slots == party{"magma", "iceman", "", ""}) && (order.dropped == std::vector<std::string>{"magma", "magma"}));
		CHECK(seat_order({"", "", "", ""}).slots[0].empty());
		CHECK(describe({"magma", "", "", ""}) == "magma / - / - / -" && call_text("seatParty", {"magma", "", "", ""}) == "seatParty(\"magma\", \"\", \"\", \"\")");

		// Costumes: the game's names; magmacivilian is XML1's, the pipeline maps it to civilian + "magma".
		CHECK(costume_index("CIVILIAN") == 8 && costume_index(" 60s ") == 3 && costume_index("default") == 0 && costume_index("weaponx") == 5 && costume_index("70s") == 4);
		CHECK(!costume_index("magmacivilian") && !costume_index("") && !costume_index("civ"));
		CHECK(costume_name(8) == "civilian" && costume_name(9) == "costume 9");
		CHECK(skinset_costume(0, true, 8, true) == 8 && skinset_costume(0, true, 8, false) == 0 && skinset_costume(0, false, 8, true) == 0);
		CHECK(skinset_costume(8, false, 3, true) == 0 && skinset_costume(5, true, 5, true) == 5 && skinset_costume(4, true, 0, true) == 0);
		CHECK(skinset_costume(1, true, 8, true) == 1 && skinset_costume(2, false, 0, false) == 2 && skinset_costume(6, true, 3, true) == 6 && skinset_costume(7, false, 8, true) == 7 &&
		      skinset_costume(9, true, 8, true) == 9);
		CHECK(usable_zone("mansion/man2/subbasement2") && !usable_zone("") && !usable_zone("a b") && !usable_zone("a;b") && !usable_zone(std::string(116, 'z')) && usable_zone(std::string(115, 'z')));

		// The guards on their own: well-formed, one per address.
		bool guards_ok = true;
		std::set<DWORD> addresses;
		for (const auto& g : guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
		CHECK(guards_ok && guards.size() == 120);

		// Change Team in the Xtraction menus: greyed out only with ForcedTeams=1 and the flag read as set.
		CHECK(change_team_disabled(true, 1) && !change_team_disabled(true, 0) && !change_team_disabled(true, std::nullopt) && !change_team_disabled(false, 1) &&
		      change_team_disabled(true, -1));
		CHECK(std::string_view(team_lock_flag).size() < 12 && team_lock_bit >= 1 && team_lock_bit <= 32);
		CHECK(xpoint_menus[0].entry == retail_table + 175 * 16 && xpoint_menus[1].entry == retail_table + 176 * 16 && xpoint_menus[0].disabled == 0x4a6ccd + 1 &&
		      xpoint_menus[1].disabled == 0x4a6fb8 + 1);

		// The functions over a game of the test's own, arguments through the test's own reader.
		const value_vtables own(reinterpret_cast<void*>(&test_value_payload));
		fake_engine own_engine;
		check_forced_team_functions(own_engine, own);

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the forced parties' bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - limits_rules::image_base); };
		const auto text_at = [&](const DWORD va) { return std::string_view(reinterpret_cast<const char*>(at(va))); };
		const auto dword_at = [&](const DWORD va) { return operand_at(at(va), 4); };

		// Every guard: the retail bytes.
		bool retail = true;
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(at(g.va), g.hex))
			{
				std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", g.va, g.what);
				retail = false;
			}
		}
		CHECK(retail);

		// Run only the guarded native attribute accessor on a synthetic saved block.
		if (retail)
		{
			using point_get = short(__fastcall*)(void*, void*);
			using point_set = void(__fastcall*)(void*, void*, int);
			std::array<std::uint8_t, 0xc0> block{};
			block[0x14] = 9; // independent skill-point counter
			block[0x16] = 4;
			auto expected = block;
			expected[0x16] = 5;
			const auto get = reinterpret_cast<point_get>(at(stat_points_get));
			const auto set = reinterpret_cast<point_set>(at(stat_points_set));
			CHECK(get(block.data(), nullptr) == 4);
			set(block.data(), nullptr, 5);
			CHECK(get(block.data(), nullptr) == 5 && block == expected);
		}

		// The game's names: its 289 functions and 19 builtins; none of the ten is among them, nor
		// anywhere in the exe's bytes (any case).
		std::set<std::string> retail_names;
		for (DWORD i = 0; i < retail_count; ++i) retail_names.insert(lowercase(text_at(dword_at(retail_table + i * 16 + 4))));
		for (DWORD i = 0; i < builtin_count; ++i) retail_names.insert(lowercase(text_at(dword_at(builtin_table + i * 16 + 4))));
		CHECK(retail_names.size() == retail_count + builtin_count && retail_names.count("seatparty") == 0 && retail_names.count("restorelastzone") == 1 && retail_names.count("iadd") == 1);
		const auto lower_exe = lowercase(*exe);
		bool unique = true;
		for (const auto& f : functions)
		{
			unique &= retail_names.count(lowercase(f.name)) == 0 && lower_exe.find(lowercase(f.name)) == std::string::npos;
		}
		CHECK(unique);
		CHECK(text_at(dword_at(retail_table + 4)) == "setRotZ" && text_at(dword_at(retail_table + (retail_count - 1) * 16 + 4)) == "SetDontShowWarningOff" &&
		      text_at(dword_at(builtin_table + 4)) == "==");

		// The table the DLL builds: the game's 289 entries byte for byte, then the ten.
		std::vector<func_entry> built(table_count);
		std::array<const void*, functions.size()> handlers{};
		for (std::size_t i = 0; i < handlers.size(); ++i) handlers[i] = reinterpret_cast<const void*>(0x1000 + i);
		build_table(reinterpret_cast<const func_entry*>(at(retail_table)), handlers, built.data());
		CHECK(std::memcmp(built.data(), at(retail_table), retail_count * sizeof(func_entry)) == 0);
		bool extras_ok = true;
		for (std::size_t i = 0; i < functions.size(); ++i)
		{
			const auto& entry = built[retail_count + i];
			extras_ok &= entry.handler == handlers[i] && std::string_view(entry.name) == functions[i].name && std::string_view(entry.args) == functions[i].args &&
			             std::string_view(entry.ret) == functions[i].ret;
		}
		CHECK(extras_ok);

		// The Xtraction menus: their entries in the game's table, the Change Team option's `disabled` a push 0,
		// the game flag read through the script interface; the DLL's table with exactly those two handlers replaced.
		bool xpoint_ok = true;
		for (const auto& m : xpoint_menus)
		{
			xpoint_ok &= dword_at(m.entry) == m.handler && text_at(dword_at(m.entry + 4)) == m.name && at(m.disabled - 1)[0] == 0x6a && at(m.disabled)[0] == 0x00 &&
			             at(m.disabled + 1)[0] == 0x6a && at(m.disabled + 2)[0] == 0x01;
		}
		CHECK(xpoint_ok && text_at(0x68d4c4) == "extractionPointChange(%d,0)" && text_at(0x68d4f8) == "extractionPointChange(%d,%i)");
		CHECK(dword_at(game_flags_vtable + game_flags_get_slot) == 0x4a0190 && dword_at(0x4a1695) == game_flags_vtable && dword_at(0x6a332c + 0x1c) == 0x5e97d0);
		{
			std::vector<func_entry> wrapped_table(built);
			const std::array<const void*, xpoint_menus.size()> wrappers{reinterpret_cast<const void*>(0x2000), reinterpret_cast<const void*>(0x2001)};
			CHECK(wrap_xpoint_menus(wrapped_table.data(), wrappers) == 2);
			std::size_t differ = 0;
			for (std::size_t i = 0; i < wrapped_table.size(); ++i) differ += std::memcmp(&wrapped_table[i], &built[i], sizeof(func_entry)) != 0;
			// (the copied entries' names point into the game's image: read them through it)
			CHECK(differ == 2 && wrapped_table[175].handler == wrappers[0] && wrapped_table[176].handler == wrappers[1] &&
			      text_at(static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(wrapped_table[175].name))) == "extractionPoint" &&
			      text_at(static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(wrapped_table[176].args))) == "asss");
			CHECK(wrap_xpoint_menus(wrapped_table.data(), wrappers) == 0); // wrapped already: the game's handler isn't there any more
		}

		// The costume table: the game's names and indices, then {"", -1}.
		bool costumes_ok = true;
		for (std::size_t i = 0; i < costumes.size(); ++i)
		{
			costumes_ok &= text_at(dword_at(costume_table + static_cast<DWORD>(i) * 8)) == costumes[i].name && static_cast<int>(dword_at(costume_table + static_cast<DWORD>(i) * 8 + 4)) == costumes[i].index;
		}
		CHECK(costumes_ok && text_at(dword_at(costume_table + 9 * 8)).empty() && dword_at(costume_table + 9 * 8 + 4) == 0xffffffff);

		// The registration patched in the copy: the two operands and nothing else.
		const std::vector<std::uint8_t> before(image, image + image_size);
		for (const auto& w : registration_writes(0x12345678)) limits_rules::apply_write(image, w);
		std::size_t changed = 0, outside = 0;
		for (std::size_t i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i])
			{
				++changed;
				const DWORD va = limits_rules::image_base + static_cast<DWORD>(i);
				outside += !(va >= table_operand && va < table_operand + 4) && !(va >= count_operand && va < count_operand + 4);
			}
		}
		CHECK(changed == 5 && outside == 0); // 0x0068a908 -> 0x12345678, 0x121 -> 0x12a
		CHECK(limits_rules::matches(at(registration), "6878563412682b010000e8318903008bc8e85a770300c3"));
		std::memcpy(image, before.data(), image_size);

		// The strings the handlers send are the game's own.
		CHECK(text_at(0x68d584) == "pushsidemission %d" && text_at(0x68d284) == "restorelastzone %s" && text_at(0x68d31c) == "loadmap %s 0 1" && text_at(0x688874) == loading_menu);
		CHECK(text_at(0x68d1cc) == "mainmenuexit 1" && text_at(0x6a3744) == "loadmap %s %d");

		// The zone manager's vtable slots the DLL calls, and the two accessors run on a block of the test's own.
		CHECK(dword_at(zones_vtable + zones_loading_slot) == 0x483e90 && dword_at(zones_vtable + zones_current_slot) == 0x483f30 && dword_at(0x4849dd) == 0x72a578);
		{
			std::array<std::uint8_t, 0x230> zones{};
			std::memcpy(zones.data() + 0x1e0, "nyc/fb/nyc_fb4", 15);
			std::uintptr_t name = 0, pending = 9;
			CHECK(run_zone_accessor(at(0x483f30), zones.data(), name) && name == reinterpret_cast<std::uintptr_t>(zones.data() + 0x1e0));
			bool flags_ok = true;
			const std::uint8_t flags[] = {0x00, 0x01, 0x02, 0x03, 0xfc}; // bits 0 and 1: a load pending
			const std::uintptr_t wanted[] = {0, 1, 1, 1, 0};
			for (std::size_t i = 0; i < std::size(flags); ++i)
			{
				zones[0x220] = flags[i];
				flags_ok &= run_zone_accessor(at(0x483e90), zones.data(), pending) && pending == wanted[i];
			}
			CHECK(flags_ok);
		}

		// The functions again, their arguments read by the game's own getter (0x4d5830) and value
		// accessor (0x55d7e0), and setSkinset on a registry read by the game's own accessors.
		const value_vtables game_values(at(0x55d7e0));
		fake_engine game_reader;
		game_reader.get = reinterpret_cast<get_argument_t>(at(argument_getter));
		check_forced_team_functions(game_reader, game_values);
		retail_registry_engine registry(image);
		registry.get = game_reader.get;
		CHECK(registry.hero_count() == 6 && registry.hero_at(3) == 20 && registry.has_stats(21) == false && registry.has_stats(9) == true && registry.herostat(9) == true &&
		      registry.herostat(40) == false && registry.costume(9) == 8 && registry.has_variant(20, 8) == false && registry.has_variant(20, 3) == true);
		check_forced_team_functions(registry, game_values);

		VirtualFree(image, 0, MEM_RELEASE);
	}

	// The Video options list the fix builds from this PC's Direct3D 8 modes (the game's list comes
	// from the same IDirect3D8::EnumAdapterModes).
	void check_d3d8_modes()
	{
		std::printf("Direct3D 8 modes (the game's Video options list)\n");
		wchar_t folder[MAX_PATH]{};
		GetSystemDirectoryW(folder, MAX_PATH);
		const auto d3d8_dll = LoadLibraryW((std::wstring(folder) + L"\\d3d8.dll").c_str());
		const auto create = d3d8_dll ? reinterpret_cast<d3d8::direct3d_create8_t>(GetProcAddress(d3d8_dll, "Direct3DCreate8")) : nullptr;
		void* d3d = create ? create(d3d8::sdk_version) : nullptr;
		if (!d3d)
		{
			std::printf("  skip  Direct3D 8 isn't available here\n");
			return;
		}

		d3d8::display_mode desktop{};
		CHECK(SUCCEEDED(d3d8::method<d3d8::get_adapter_display_mode_t>(d3d, d3d8::d3d_slot::get_adapter_display_mode)(d3d, 0, &desktop)));
		std::printf("  info  desktop %ux%u @ %u Hz, format %lu\n", desktop.width, desktop.height, desktop.refresh_rate, desktop.format);

		const UINT count = d3d8::method<d3d8::get_adapter_mode_count_t>(d3d, d3d8::d3d_slot::get_adapter_mode_count)(d3d, 0);
		std::vector<d3d8::display_mode> adapter;
		for (UINT i = 0; i < count; ++i)
		{
			d3d8::display_mode mode{};
			if (SUCCEEDED(d3d8::method<d3d8::enum_adapter_modes_t>(d3d, d3d8::d3d_slot::enum_adapter_modes)(d3d, 0, i, &mode)))
			{
				adapter.push_back(mode);
			}
		}
		const auto everything = display_rules::curate_modes(adapter, {}, 0, std::nullopt, 1000);
		std::printf("  info  %u adapter modes, %zu sizes:", count, everything.size());
		for (const auto& mode : everything) std::printf(" %ux%u", mode.width, mode.height);
		std::printf("\n");
		const bool desktop_listed = std::ranges::any_of(everything, [&](const auto& m) { return m.width == desktop.width && m.height == desktop.height; });
		std::printf("  info  Direct3D 8 %s the desktop size itself\n", desktop_listed ? "lists" : "does NOT list");

		const auto list = display_rules::curate_modes(adapter, {desktop.width, desktop.height}, desktop.refresh_rate, std::nullopt, 20);
		std::printf("  info  the game's own 20-slot table gets %zu:", list.size());
		for (const auto& mode : list) std::printf(" %ux%u", mode.width, mode.height);
		std::printf("\n");
		CHECK(list.size() <= 20);
		CHECK(std::ranges::any_of(list, [&](const auto& m) { return m.width == desktop.width && m.height == desktop.height; }));
		// The 64-slot table, in the game's own fullscreen and in a borderless window.
		const auto fullscreen = resolution_rules::build_list(adapter, {desktop.width, desktop.height}, desktop.refresh_rate, std::nullopt, display_rules::mode::stock, resolution_rules::slots);
		const auto borderless = resolution_rules::build_list(adapter, {desktop.width, desktop.height}, desktop.refresh_rate, std::nullopt, display_rules::mode::borderless, resolution_rules::slots);
		std::printf("  info  the 64-slot table gets %zu fullscreen, %zu borderless:", fullscreen.size(), borderless.size());
		for (const auto& mode : borderless) std::printf(" %ux%u", mode.width, mode.height);
		std::printf("\n");
		CHECK(fullscreen.size() <= resolution_rules::slots && fullscreen.size() >= list.size());
		CHECK(fullscreen.size() == everything.size() + (desktop_listed ? 0 : 1)); // every adapter size, none dropped, the desktop's added if it wasn't there
		CHECK(borderless.size() <= resolution_rules::slots && borderless.size() >= fullscreen.size());
		CHECK(std::ranges::any_of(fullscreen, [&](const auto& m) { return m.width == desktop.width && m.height == desktop.height; }));
		CHECK(std::ranges::all_of(borderless, [](const auto& m) { return resolution_rules::fits(m.width, m.height); }));
		check_d3d8_capture(d3d);
		check_windowed_presents(d3d, desktop);
		static_cast<IUnknown*>(d3d)->Release();
	}

	// The game's console queue (vt+0x1c, 0x55c410), called as the fix calls it. No C++ objects here.
	using console_queue_t = bool(__fastcall*)(void* self, void* edx, const char* line);

	bool run_console_queue(const std::uint8_t* function, void* console, const char* line, bool& queued)
	{
		__try
		{
			queued = reinterpret_cast<console_queue_t>(const_cast<std::uint8_t*>(function))(console, nullptr, line);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// strncpy for the game's code in a mapped copy, where its import (0x672170: jmp [0x67f148]) isn't bound.
	char* __cdecl strncpy_for_the_game(char* to, const char* from, const std::size_t count)
	{
		return std::strncpy(to, from, count);
	}

	// The test pipe's rules (test_input_rules.hpp): key names, commands, and the keys it holds.

	void check_test_state_rules()
	{
		using namespace test_state_rules;
		struct memory
		{
			std::vector<unsigned char> bytes = std::vector<unsigned char>(0x700000);
			bool read(address a, void* dst, std::size_t n)
			{
				if (a < 0x400000 || a - 0x400000 + n > bytes.size())
				{
					return false;
				}
				std::memcpy(dst, bytes.data() + a - 0x400000, n);
				return true;
			}
			void put(address a, std::uint32_t v)
			{
				std::memcpy(bytes.data() + a - 0x400000, &v, 4);
			}
		} m;
		reader<memory> r(m);
		// The same stats name on an NPC must never replace the seated hero, in either order.
		snapshot unread;
        bind_party(unread);
        CHECK(unread.party[0].binding == "unresolved");
        snapshot doubles;
		doubles.party[0].name = "hero_test";
		actor hero;
		hero.name = "Hero_Test";
		hero.id = 17;
		hero.health = 80.f;
		hero.party_member = true;
		actor npc = hero;
		npc.id = 29;
		npc.health = 20.f;
		npc.party_member = false;
		doubles.actors = {hero, npc};
		bind_party(doubles);
		CHECK(doubles.party[0].id == 17);
		doubles.party[0] = actor{};
		doubles.party[0].name = "hero_test";
		doubles.actors = {npc, hero};
		bind_party(doubles);
		CHECK(doubles.party[0].id == 17);
		npc.party_member = true; // a reused snapshot must also clear its previously bound body
		doubles.actors = {hero, npc};
		bind_party(doubles);
		CHECK(doubles.party[0].id == 0 && !doubles.party[0].health && doubles.party[0].binding == "ambiguous");
		tracker menu_events;
		snapshot menu_state;
		menu_state.menu = "main";
		menu_events.observe(menu_state);
		menu_events.drain();
		menu_state.menu = "team";
		menu_events.observe(menu_state);
		CHECK(menu_events.drain().find("team_menu_open") != std::string::npos);
		CHECK(!r.read(1).mode);
		CHECK(!r.get<float>(0xffffffff));
		m.put(0x729960, 0x686e1c);
		m.put(0x72a578, 0x68878c);
		m.put(0x729f40, 3);
		m.put(0x72b108, 0x900000);
		m.put(0x900000, 0x6892d4);
		m.put(0x907d78, 1);
		m.put(0x900008, 0x006b7361); // invented identifier "ask"
		m.put(0x900008 + 0x1a9, 0x00000402);
		m.put(0x72b118 + 2, 0x000000c3);
		m.put(0x729d54, 0xbf800000);
		CHECK(r.read(98).script_controls_locked == true);
		m.put(0x729d54, 0);
		CHECK(r.read(99).script_controls_locked == false);
		auto s = r.read(100);
		CHECK(s.act == 3);
		CHECK(s.objectives && s.objectives->size() == 1);
		CHECK((*s.objectives)[0].name == "ask" && (*s.objectives)[0].count == 3 && (*s.objectives)[0].goal == 4);
		CHECK((*s.objectives)[0].complete && !(*s.objectives)[0].shown);
		m.put(0x907d78, 76);
		CHECK(!r.read(101).objectives);
		// A live synthetic character; stale generations and non-finite values must fail closed.
		m.put(0x778b70 + 0x818, 1);
		m.put(0x778b74, 0x910000);
		m.put(0x778b70 + 0x83c, 17);
		m.put(0x910004, 1u << 20);
		m.put(0x9103d4, 0);
		m.put(0x91001c, 17);
		m.put(0x910000, 0x680100);
		m.put(0x680100, 0x401100);
		m.put(0x401100, 0x920000b8);
		m.put(0x401104, 0x0000c300); // mov eax,0x920000; ret
		m.put(0x70b840, 0);
		m.put(0x920000 + 0x18, 16);
		m.put(0x91035c, 0x930000);
		m.put(0x930150, 0x006b7341);
		m.put(0x91027c, 0x41200000);
		m.put(0x910284, 0x41a00000);
		m.put(0x910768, 0xbf800000);
		m.put(0x729974, 1);
		m.put(0xa0a828, 0);
		m.put(0xa12828, 0x006b7361);
		auto live = r.read(102);
		CHECK(live.party[0].id == 17 && live.party[0].health == 10.f && live.party[0].ai == true);
		m.put(0x910020, 0x7f800000);
		CHECK(!r.read(103).party[0].pos[0]);
		m.put(0x91001c, 18);
		CHECK(r.read(104).actors.empty() && !r.read(104).party[0].health);
		m.put(0x9404b0, 0x941000);
		m.put(0x941000, 0x942000);
		m.put(0x942000, 'f');
		m.put(0x940008, 0);
		m.put(0x94001c, 0x942000);
		m.put(0x94032c, 0x970000);
		m.put(0x97059c, 63);
		m.put(0x9704fc + 3 * 4, 3);
		m.put(0x9704f0, 8);
		m.put(0x97039c + 3 * 4, 0x980000);
		m.put(0x980064, 0x990000);
		m.put(0x990000, 0x006b7341);
		CHECK(r.speaker(0x940000, 3) == "Ask");
		m.put(0x717aac, 0x940000);
		m.put(0x940000, 0x685e04);
		m.put(0x940000 + 0x21b26, 0x00030002);
		auto cursor = r.read(106);
		CHECK(cursor.selected == 2 && cursor.responses == 3);
		CHECK(state_json(cursor).find("\"selected\":2") != std::string::npos);

		m.put(0x9704fc + 3 * 4, 67);
		CHECK(!r.speaker(0x940000, 3));
		m.put(0x94001c, 0x942008);
		m.put(0x942008, 'a');
		m.put(0x940018, 0);
		CHECK(!r.speaker(0x940000, 3)); // corrupt tree cycles, bounded traversal
		CHECK(json(std::optional<float>(INFINITY)) == "null");
		CHECK(json(std::optional<float>(1.5f)) == "1.5");
		CHECK(quote("a\"\\\n") == "\"a\\\"\\\\\\u000a\"");
		CHECK(state_json(snapshot{}).find("\"health\":null") != std::string::npos);
		CHECK(objectives_json(snapshot{}).find("\"objectives\":null") != std::string::npos);
		using K = test_input_rules::command::kind;
		CHECK(test_input_rules::parse_command(" STATE ").what == K::state);
		CHECK(test_input_rules::parse_command("objectives").what == K::objectives);
		CHECK(test_input_rules::parse_command("events").what == K::events);
		CHECK(test_input_rules::parse_command("state extra").what == K::unknown);
		tracker tr;
		s = {};
		s.ms = 100;
		s.zone = "synthetic/room";
		s.loading = false;
		s.conversation = false;
		s.party[0].id = 17;
		s.party[0].name = "hero_test";
		s.party[0].health = 8.f;
		s.party[0].pos[2] = 10.f;
		tr.observe(s);
		tr.drain();
		s.ms = 200;
		s.party[0].health = 0.f;
		s.party[0].pos[2] = 8.f;
		s.conversation = true;
		tr.observe(s);
		CHECK(s.party[0].vz && std::abs(*s.party[0].vz + 20) < .001f);
		auto ev = tr.drain();
		CHECK(ev.find("hero_death") != std::string::npos && ev.find("conversation_start") != std::string::npos);
		CHECK(tr.drain().find("\"events\":[]") != std::string::npos);
		s.ms = 300;
		s.zone = "synthetic/other";
		s.party[0].vz.reset();
		s.party[0].pos[2] = -200.f;
		tr.observe(s);
		CHECK(!s.party[0].vz);
		tr.drain();
		for (unsigned i = 0; i < 520; ++i)
		{
			tr.emit(s, "synthetic_event");
		}
		CHECK(tr.drain().find("\"dropped\":8") != std::string::npos);

		// events carries the observer error like state/objectives, instead of an empty queue
		tracker quiet;
		quiet.emit(s, "synthetic_event");
		CHECK(events_reply("no game-thread sample", quiet) == "{\"schema\":1,\"error\":\"no game-thread sample\"}");
		CHECK(events_reply("unsupported executable layout", quiet).find("\"error\":\"unsupported executable layout\"") != std::string::npos);
		CHECK(events_reply("", quiet).find("synthetic_event") != std::string::npos);
		CHECK(events_reply("", quiet).find("\"events\":[]") != std::string::npos);
	}

	void check_test_input_rules()
	{
		using namespace test_input_rules;
		std::printf("test input rules ([Test] InputPipe commands, PipeName)\n");

		// [Test] PipeName: letters, digits, '-', '_', at most 64; unset or refused -> xml2-fix-input.
		auto pipe = choose_pipe("");
		CHECK(pipe.name == "xml2-fix-input" && pipe.path == "\\\\.\\pipe\\xml2-fix-input" && pipe.error.empty());
		pipe = choose_pipe("xml2-host_1");
		CHECK(pipe.name == "xml2-host_1" && pipe.path == "\\\\.\\pipe\\xml2-host_1" && pipe.error.empty());
		pipe = choose_pipe("  xml2-join   ; the second window");
		CHECK(pipe.name == "xml2-join" && pipe.error.empty());
		CHECK(choose_pipe("; commented out").name == "xml2-fix-input" && choose_pipe("; commented out").error.empty());
		CHECK(choose_pipe(std::string(64, 'a')).name == std::string(64, 'a'));
		for (const char* bad : {"has space", "back\\slash", "sl/ash", "dot.name", "colon:", "star*", "quote\"", "tést"})
		{
			pipe = choose_pipe(bad);
			if (pipe.name != "xml2-fix-input" || pipe.error.empty())
			{
				std::printf("  FAIL  PipeName=%s accepted\n", bad);
				++failures;
			}
		}
		pipe = choose_pipe(std::string(65, 'a'));
		CHECK(pipe.name == "xml2-fix-input" && pipe.error.find("at most 64") != std::string::npos);

		CHECK(parse_key("ENTER") == 0x1C && parse_key("return") == 0x1C && parse_key("DIK_RETURN") == 0x1C);
		CHECK(parse_key("esc") == 0x01 && parse_key("w") == 0x11 && parse_key("UP") == 0xC8 && parse_key("numpad4") == 0x4B);
		CHECK(parse_key("1") == 0x02 && parse_key("0") == 0x0B);      // single digits are the digit keys
		CHECK(parse_key("28") == 0x1C && parse_key("0x1C") == 0x1C); // longer numbers are scancodes
		CHECK(!parse_key("bogus") && !parse_key("") && !parse_key("0x100") && !parse_key("00"));
		CHECK(name_of(0x1C) == "RETURN" && name_of(0x54) == "0x54");

		auto cmd = parse_command("tap ENTER 120\r");
		CHECK(cmd.what == command::kind::tap && (cmd.keys == std::vector<unsigned char>{0x1C}) && cmd.ms == 120);
		cmd = parse_command("hold w+d 1500");
		CHECK(cmd.what == command::kind::hold && (cmd.keys == std::vector<unsigned char>{0x11, 0x20}) && cmd.ms == 1500);
		CHECK(parse_command("hold w").what == command::kind::unknown); // needs a duration
		cmd = parse_command("down LSHIFT");
		CHECK(cmd.what == command::kind::down && cmd.keys.size() == 1 && cmd.ms == 0);
		CHECK(parse_command("tap W 99999").ms == max_hold_ms); // clamped
		CHECK(parse_command("up all").what == command::kind::release);
		cmd = parse_command("screenshot \"C:\\shots\\frame 1.png\"");
		CHECK(cmd.what == command::kind::screenshot && cmd.path == "C:\\shots\\frame 1.png");
		CHECK(parse_command("screenshot").what == command::kind::unknown);
		CHECK(parse_command("  ").what == command::kind::empty);
		CHECK(parse_command("frob").what == command::kind::unknown && !parse_command("frob").error.empty());
		CHECK(parse_command("tap NOSUCH").what == command::kind::unknown && parse_command("tap W x").what == command::kind::unknown);
		CHECK(parse_command("PING").what == command::kind::ping && parse_command("status").what == command::kind::status);

		// "wm" (posted key messages) is gone: the Advanced Options panel reads the DirectInput
		// keyboard, so a stale client is told to use tap.
		cmd = parse_command("wm DOWN");
		CHECK(cmd.what == command::kind::unknown && cmd.error.find("use tap") != std::string::npos);
		CHECK(parse_command("frob").error.find("wm") == std::string::npos); // not offered in the list of commands
		CHECK(parse_command("frob").error.find("script, console") != std::string::npos);

		// script: "runscript STATEMENT" for the game's console, which hands runscript one word - the
		// spaces outside quotes go (the script tokenizer ends an argument at a space or a ',' alike),
		// quotes stay as they are.
		cmd = parse_command("script unlockCharacter(\"storm\", \"\")");
		CHECK(cmd.what == command::kind::script && cmd.text == "runscript unlockCharacter(\"storm\",\"\")");
		cmd = parse_command("SCRIPT   setGameFlag(\"x1join\", 1, 0 )  ");
		CHECK(cmd.what == command::kind::script && cmd.text == "runscript setGameFlag(\"x1join\",1,0)");
		CHECK(script_line("unlockCharacter('', 'astonishing')").text == "runscript unlockCharacter('','astonishing')"); // resetgame's own line (0x685538)
		CHECK(script_line("say(\"it's\",'\"x\"')").text == "runscript say(\"it's\",'\"x\"')");                        // a quote inside the other kind
		CHECK(script_line("\tf( 1,\t2 )").text == "runscript f(1,2)");
		CHECK(script_line("f(1)\\n\\rg(2)").text == "runscript f(1)\\n\\rg(2)"); // two statements: the four characters \n\r
		CHECK(script_line("menus/new_game").text == "runscript menus/new_game"); // no '(': scripts/menus/new_game.py
		for (const char* bad : {"script", "script   ", "script hudMessage(1, 2.0, \"hello there\")", "script f(1);g(2)", "script f(\"caf\xe9\")", "script f(1)\rg(2)"})
		{
			cmd = parse_command(bad);
			CHECK(cmd.what == command::kind::unknown && !cmd.error.empty() && cmd.text.empty());
		}
		CHECK(parse_command("script").error.find("needs a statement") != std::string::npos);
		CHECK(script_line("hudMessage(1, 2.0, \"hello there\")").error.find("inside a quoted string") != std::string::npos);
		CHECK(script_line("f(1);g(2)").error.find("';'") != std::string::npos);
		CHECK(script_line("f(1)\ng(2)").error.find("0x0A") != std::string::npos && script_line("f(1)\rg(2)").error.find("no embedded newlines") != std::string::npos);
		// 127 characters at most, "runscript " included, counted after the spaces go: f("...") is 5 + n.
		CHECK(script_line("f(\"" + std::string(112, 'a') + "\")").text.size() == console_max);
		CHECK(script_line("f(\"" + std::string(112, 'a') + "\" )").text.size() == console_max);
		CHECK(script_line("f(\"" + std::string(113, 'a') + "\")").error.find("is 128 characters; the game's console keeps 127") != std::string::npos);

		// console: as it is.
		cmd = parse_command("console loadmap nyc/alison/nyc1_1_3 1");
		CHECK(cmd.what == command::kind::console && cmd.text == "loadmap nyc/alison/nyc1_1_3 1");
		CHECK(parse_command("CONSOLE runscript say('a b')").text == "runscript say('a b')");
		CHECK(console_line(std::string(127, 'x')).text.size() == 127);
		CHECK(console_line(std::string(128, 'x')).error.find("is 128 characters") != std::string::npos);
		CHECK(parse_command("console").what == command::kind::unknown && parse_command("console").error.find("needs a command") != std::string::npos);
		CHECK(!console_line("a\rb").error.empty() && console_line("a\rb").text.empty());
		for (const auto& g : console_guards) CHECK(limits_rules::valid_hex(g.hex));
		for (const auto& g : runscript_guards) CHECK(limits_rules::valid_hex(g.hex));
		// Why, in the retail XMen2.exe: the window's message filter (0x6223d0) passes WM_KEYDOWN only
		// (lea edx,[eax-2]; cmp edx,0xfe; ja drop: 0x101 - 2 is out, and its index table sends 0x100
		// alone to handleMessage, which acts on WM_KEYUP only); the panel's per-frame input function
		// makes its WM_KEYUP from DirectInput key releases (mov esi,0x101 ... call 0x621ea0); and the
		// keyboard state it reads comes from GetDeviceState(256), vtable +0x24 = slot 9, the one the
		// pipe hooks.
		if (const auto exe = game_executable())
		{
			struct fact
			{
				const char* what;
				DWORD rva;
				std::vector<std::uint8_t> bytes;
			};
			const fact facts[] = {
				{"message filter", 0x2223fb, {0x3D, 0x02, 0x01, 0x00, 0x00, 0x77, 0x1B, 0x74, 0x32, 0x8D, 0x50, 0xFE, 0x81, 0xFA, 0xFE, 0x00, 0x00, 0x00, 0x77, 0xE6}},
				{"filter index for 0x100", 0x22245c + 0xfe, {0x01}},
				{"per-frame WM_KEYUP", 0x219272, {0xBE, 0x01, 0x01, 0x00, 0x00}},
				{"per-frame handleMessage call", 0x2192c4, {0x52, 0x50, 0x56, 0xE8, 0xD4, 0x8B, 0x00, 0x00}},
				{"keyboard GetDeviceState(256)", 0x228614, {0x68, 0x00, 0x01, 0x00, 0x00, 0xF3, 0xA5, 0x8B, 0x08, 0x50, 0xFF, 0x51, 0x24}},
			};
			for (const auto& f : facts)
			{
				const auto at = file_offset(*exe, f.rva);
				const bool same = at && *at + f.bytes.size() <= exe->size() && std::memcmp(exe->data() + *at, f.bytes.data(), f.bytes.size()) == 0;
				if (!same) std::printf("  info  %s differs\n", f.what);
				CHECK(same);
			}

			// script and console: the console code's retail bytes, then its queue (vt+0x1c) run on a
			// console of the test's own, called as the fix calls it - two commands at most, 127
			// characters kept, the count at +0x630.
			DWORD image_size = 0;
			std::uint8_t* image = map_image(*exe, image_size);
			CHECK(image != nullptr);
			if (image)
			{
				const auto at = [&](const DWORD va) { return image + (va - limits_rules::image_base); };
				for (const auto& group : {std::span<const guard>(console_guards), std::span<const guard>(runscript_guards)})
				{
					for (const auto& g : group)
					{
						const bool same = limits_rules::matches(at(g.va), g.hex);
						if (!same) std::printf("  info  %s differs\n", g.what);
						CHECK(same);
					}
				}
				CHECK(std::string_view(reinterpret_cast<const char*>(at(0x685538))) == "runscript unlockCharacter('','astonishing')"); // the game's own: no spaces

				std::uint8_t* thunk = at(0x672170);
				CHECK(thunk[0] == 0xFF && thunk[1] == 0x25); // jmp [strncpy's import]
				const auto rel = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&strncpy_for_the_game) - reinterpret_cast<std::uintptr_t>(thunk + 5));
				thunk[0] = 0xE9;
				std::memcpy(thunk + 1, &rel, sizeof(rel));

				auto* console = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
				void* built = nullptr;
				CHECK(console && run_thiscall(at(0x55bdd0), console + 0x500, built) && built == console + 0x500); // the queue's own constructor
				if (console && built)
				{
					const auto waiting = [&] { DWORD n = 0; std::memcpy(&n, console + console_waiting, sizeof(n)); return n; };
					const auto queued_text = [&](const int slot) { return std::string(reinterpret_cast<const char*>(console + 0x500 + 8 + slot * 0x88)); };
					const auto queue = at(console_queue);
					bool queued = true;
					CHECK(run_console_queue(queue, console, "", queued) && !queued && waiting() == 0);
					CHECK(run_console_queue(queue, console, "loadmap nyc/alison/nyc1_1_3 1", queued) && queued && waiting() == 1);
					const std::string long_line(200, 'x');
					CHECK(run_console_queue(queue, console, long_line.c_str(), queued) && queued && waiting() == console_slots);
					CHECK(queued_text(0) == "loadmap nyc/alison/nyc1_1_3 1" || queued_text(1) == "loadmap nyc/alison/nyc1_1_3 1");
					CHECK(queued_text(0) == std::string(console_max, 'x') || queued_text(1) == std::string(console_max, 'x'));
					CHECK(run_console_queue(queue, console, "runscript f()", queued) && !queued && waiting() == console_slots); // full
				}
				if (console) VirtualFree(console, 0, MEM_RELEASE);
				VirtualFree(image, 0, MEM_RELEASE);
			}
		}

		synthetic_keys keys;
		unsigned char state[256]{};
		state[0x11] = key_down; // the real keyboard holds W
		std::vector<unsigned char> expired;
		keys.press(0x1C, 1000);
		keys.merge(state, 500, expired);
		CHECK(state[0x1C] == key_down && state[0x11] == key_down && expired.empty() && keys.held() == 1);
		std::memset(state, 0, sizeof(state));
		keys.merge(state, 1000, expired); // time's up
		CHECK(state[0x1C] == 0 && (expired == std::vector<unsigned char>{0x1C}) && keys.held() == 0 && !keys.is_down(0x1C));
		keys.press(0x20, 5000);
		keys.press(0x21, 5000);
		keys.release(0x20);
		CHECK(keys.held() == 1 && keys.is_down(0x21));
		keys.release_all();
		CHECK(keys.held() == 0);
	}

	// The test pipe's pad commands (pad_input_rules.hpp): names, commands, and what a pad holds.
	void check_pad_input_rules()
	{
		using namespace pad_input_rules;
		using kind = pad_command::kind;
		std::printf("pad input rules (the test pipe's pad commands)\n");
		const auto button = [](const char* name) { return static_cast<int>(parse_button(name).value_or(0)); };

		CHECK(button("a") == XINPUT_GAMEPAD_A && button("START") == XINPUT_GAMEPAD_START && button("lb") == XINPUT_GAMEPAD_LEFT_SHOULDER);
		CHECK(button("LT") == static_cast<int>(left_trigger_bit) && button("rt") == static_cast<int>(right_trigger_bit) && button("Up") == XINPUT_GAMEPAD_DPAD_UP);
		CHECK(button("select") == XINPUT_GAMEPAD_BACK && button("L3") == XINPUT_GAMEPAD_LEFT_THUMB && button("dpadright") == XINPUT_GAMEPAD_DPAD_RIGHT);
		CHECK(!parse_button("") && !parse_button("Z") && !parse_button("A1"));
		CHECK(buttons_text(XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_A) == "A+START" && buttons_text(XINPUT_GAMEPAD_BACK) == "BACK" && buttons_text(0) == "nothing");
		for (const auto& entry : button_names) CHECK(parse_button(entry.name) == entry.mask); // every name parses back

		auto cmd = parse_pad_command("pad", "1 A");
		CHECK(cmd.what == kind::tap && cmd.pad == 1 && cmd.buttons == XINPUT_GAMEPAD_A && cmd.ms == 0 && cmd.error.empty());
		cmd = parse_pad_command("PAD", "2 a+start 120");
		CHECK(cmd.what == kind::tap && cmd.pad == 2 && cmd.buttons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START) && cmd.ms == 120);
		CHECK(parse_pad_command("pad", "1 A 99999").ms == max_hold_ms); // clamped
		cmd = parse_pad_command("padhold", "4 LB 500");
		CHECK(cmd.what == kind::hold && cmd.pad == 4 && cmd.buttons == XINPUT_GAMEPAD_LEFT_SHOULDER && cmd.ms == 500);
		CHECK(parse_pad_command("padhold", "1 LB").error.find("needs a duration") != std::string::npos);
		cmd = parse_pad_command("paddown", "3 RT");
		CHECK(cmd.what == kind::down && cmd.pad == 3 && cmd.buttons == right_trigger_bit && cmd.ms == 0);
		cmd = parse_pad_command("padup", "1 A");
		CHECK(cmd.what == kind::up && cmd.buttons == XINPUT_GAMEPAD_A);
		cmd = parse_pad_command("padup", "4 all");
		CHECK(cmd.what == kind::release && cmd.pad == 4);
		CHECK(parse_pad_command("padup", "1 A 100").what == kind::up && !parse_pad_command("padup", "1 A 100").error.empty());
		cmd = parse_pad_command("padrelease", "");
		CHECK(cmd.what == kind::release && cmd.pad == 0 && cmd.error.empty());
		CHECK(parse_pad_command("padrelease", "2").pad == 2 && !parse_pad_command("padrelease", "5").error.empty());
		CHECK(parse_pad_command("tap", "1 A").what == kind::none); // the keyboard's verb: someone else's line
		for (const char* bad : {"5 A", "0 A", "A", "", "12 A", "1", "1 FOO", "1 A+", "1 +A", "1 A x", "1 A 10 20"})
		{
			cmd = parse_pad_command("pad", bad);
			if (cmd.what != kind::tap || cmd.error.empty())
			{
				std::printf("  FAIL  pad %s accepted\n", bad);
				++failures;
			}
		}
		CHECK(parse_pad_command("pad", "5 A").error.find("1 to 4") != std::string::npos);
		CHECK(parse_pad_command("pad", "1 FOO").error.find("unknown pad button 'FOO'") != std::string::npos);

		cmd = parse_pad_command("stick", "1 L 0 1");
		CHECK(cmd.what == kind::stick && cmd.pad == 1 && cmd.side == 0 && cmd.x == 0.0f && cmd.y == 1.0f && cmd.ms == 0);
		cmd = parse_pad_command("STICK", "2 r -0.5 .25 300");
		CHECK(cmd.what == kind::stick && cmd.side == 1 && cmd.x == -0.5f && cmd.y == 0.25f && cmd.ms == 300);
		CHECK(parse_pad_command("stick", "1 LS 1.0 -1.0").side == 0 && parse_pad_command("stick", "1 RS 1 0").side == 1);
		cmd = parse_pad_command("trigger", "1 R 1");
		CHECK(cmd.what == kind::trigger && cmd.side == 1 && cmd.value == 1.0f && cmd.ms == 0);
		cmd = parse_pad_command("trigger", "3 lt 0.5 200");
		CHECK(cmd.what == kind::trigger && cmd.pad == 3 && cmd.side == 0 && cmd.value == 0.5f && cmd.ms == 200);
		for (const auto& [verb, bad] : std::initializer_list<std::pair<const char*, const char*>>{
		         {"stick", "1 L 2 0"}, {"stick", "1 L 0 -1.5"}, {"stick", "1 M 0 0"}, {"stick", "1 L 0"}, {"stick", "1 L 0 0 1 2"}, {"stick", "1 L x 0"},
		         {"stick", "1 L nan 0"}, {"stick", "1 L inf 0"}, {"stick", "1 L 0 0 -5"}, {"trigger", "1 R 1.5"}, {"trigger", "1 R -0.1"}, {"trigger", "1 RS 1"},
		         {"trigger", "1 R"}, {"stick", "9 L 0 0"}})
		{
			cmd = parse_pad_command(verb, bad);
			if (cmd.error.empty())
			{
				std::printf("  FAIL  %s %s accepted\n", verb, bad);
				++failures;
			}
		}
		CHECK(parse_pad_command("stick", "1 L 2 0").error.find("from -1 to 1") != std::string::npos);
		CHECK(parse_pad_command("trigger", "1 R 1.5").error.find("from 0 to 1") != std::string::npos);

		// Through the pipe's own parser: the pad verbs are one kind, their errors its errors.
		{
			const auto line = test_input_rules::parse_command("pad 1 a 50\r");
			CHECK(line.what == test_input_rules::command::kind::pad && line.pad.what == kind::tap && line.pad.buttons == XINPUT_GAMEPAD_A && line.pad.ms == 50);
			const auto refused_line = test_input_rules::parse_command("stick 9 L 0 0");
			CHECK(refused_line.what == test_input_rules::command::kind::unknown && refused_line.error.find("1 to 4") != std::string::npos);
			CHECK(test_input_rules::parse_command("  PADRELEASE  ").what == test_input_rules::command::kind::pad);
			CHECK(test_input_rules::parse_command("frob").error.find("pad, padhold, paddown, padup, padrelease, stick, trigger") != std::string::npos);
		}

		// What a pad holds, merged into a read: buttons added, triggers raised, sticks replaced - until their time.
		synthetic_pad pad;
		xinput_pad::raw_state state{XINPUT_GAMEPAD_X, 50, 200, 1000, -2000, 0, 0}; // the real pad: X, triggers, left stick
		std::vector<std::string> expired;
		pad.press(XINPUT_GAMEPAD_A | left_trigger_bit, 1000);
		pad.set_stick(0, 0.0f, 1.0f, 1000);
		pad.set_trigger(1, 0.5f, 1000);
		CHECK(pad.held() == 4 && pad.buttons_down() == (XINPUT_GAMEPAD_A | left_trigger_bit) && pad.stick_held(0) && pad.trigger_held(1));
		pad.merge(state, 500, expired);
		CHECK(state.buttons == (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_A) && state.left_trigger == 255 && state.right_trigger == 200); // 0.5 = 128 < the real 200
		CHECK(state.left_x == 0 && state.left_y == 32767 && state.right_x == 0 && expired.empty());
		state = {0, 0, 50, 1000, -2000, 0, 0};
		pad.merge(state, 999, expired);
		CHECK(state.right_trigger == 128 && state.left_trigger == 255);
		state = {0, 0, 0, 1000, -2000, 0, 0};
		pad.merge(state, 1000, expired); // time's up: everything let go, the real state stands
		CHECK(state.buttons == 0 && state.left_trigger == 0 && state.right_trigger == 0 && state.left_x == 1000 && state.left_y == -2000);
		CHECK(pad.held() == 0 && expired.size() == 4);
		CHECK(std::ranges::find(expired, "A") != expired.end() && std::ranges::find(expired, "LT") != expired.end());
		CHECK(std::ranges::find(expired, "left stick") != expired.end() && std::ranges::find(expired, "right trigger") != expired.end());
		pad.set_stick(1, -1.0f, 0.0f, 5000);
		CHECK(pad.stick_held(1));
		pad.set_stick(1, 0.0f, 0.0f, 5000); // 0 0 lets go
		CHECK(!pad.stick_held(1));
		pad.set_trigger(0, 0.0f, 5000);
		CHECK(!pad.trigger_held(0));
		pad.press(XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_Y, 5000);
		pad.release(XINPUT_GAMEPAD_B);
		CHECK(pad.buttons_down() == XINPUT_GAMEPAD_Y);
		pad.release_all();
		CHECK(pad.held() == 0);
		CHECK(stick_value(1.0f) == 32767 && stick_value(-1.0f) == -32767 && stick_value(0.5f) == 16384 && trigger_value(1.0f) == 255 && trigger_value(0.5f) == 128);

		// Through the Dual Action profile, as the game reads a pad: A is button 2, START 10, LT 7, the d-pad the hat,
		// the left stick up the top of Y's range.
		pad.press(XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START | left_trigger_bit | XINPUT_GAMEPAD_DPAD_UP, 1000);
		pad.set_stick(0, 0.0f, 1.0f, 1000);
		pad.set_stick(1, 1.0f, 0.0f, 1000);
		state = {};
		pad.merge(state, 0, expired);
		pad_profile::axis_ranges axes{};
		for (auto& axis : axes) axis = {-1000, 1000, 0};
		DIJOYSTATE2 joy{};
		pad_profile::logitech_dual_action.fill_state(*reinterpret_cast<DIJOYSTATE*>(&joy), state, axes);
		CHECK(joy.rgbButtons[1] == 0x80 && joy.rgbButtons[9] == 0x80 && joy.rgbButtons[6] == 0x80);
		CHECK(joy.rgbButtons[0] == 0 && joy.rgbButtons[2] == 0 && joy.rgbButtons[7] == 0 && joy.rgbButtons[8] == 0);
		CHECK(joy.rgdwPOV[0] == 0 && joy.lY == -1000 && joy.lX == 0 && joy.lZ == 1000 && joy.lRz == 0);
	}

	// [Test] VirtualPads (virtual_pad_rules.hpp): the ini value, the pads' identity, the lists they're in, and how
	// their state reaches a caller's data format.
	void check_virtual_pad_rules()
	{
		using namespace virtual_pad_rules;
		std::printf("virtual pad rules ([Test] VirtualPads)\n");

		CHECK(choose_count("").count == 0 && choose_count("").error.empty());
		CHECK(choose_count("2").count == 2 && choose_count("2").error.empty());
		CHECK(choose_count(" 3 ; players 1-3").count == 3 && choose_count("0").count == 0);
		CHECK(choose_count("9").count == 4 && choose_count("9").error.find("4 virtual pads") != std::string::npos);
		CHECK(choose_count("two").count == 0 && choose_count("two").error.find("isn't a number") != std::string::npos);
		CHECK(choose_count("-1").count == 0 && !choose_count("-1").error.empty());

		CHECK(!IsEqualGUID(instance_guid(0), instance_guid(1)) && pad_of(instance_guid(1), 2) == 1 && pad_of(instance_guid(0), 2) == 0);
		CHECK(!pad_of(instance_guid(2), 2) && !pad_of(instance_guid(0), 0) && !pad_of(GUID_SysKeyboard, 4));
		// The product GUID the game picks the Dual Action's console map by (its string at 0x551b90's table).
		wchar_t text[64]{};
		StringFromGUID2(product_guid(0x046D, 0xC216), text, 64);
		CHECK(std::wstring(text) == L"{C216046D-0000-0000-0000-504944564944}");
		CHECK(device_type(true) == 0x00010215 && device_type(false) == 0x00010404);

		CHECK(lists_pads(true, DI8DEVCLASS_GAMECTRL, DIEDFL_ATTACHEDONLY) && lists_pads(true, DI8DEVCLASS_ALL, 0) && lists_pads(true, DI8DEVTYPE_GAMEPAD, 0));
		CHECK(!lists_pads(true, DI8DEVCLASS_KEYBOARD, 0) && !lists_pads(true, DI8DEVCLASS_POINTER, 0) && !lists_pads(true, DI8DEVTYPE_JOYSTICK, 0));
		CHECK(!lists_pads(true, DI8DEVCLASS_GAMECTRL, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK));
		CHECK(lists_pads(false, 4, DIEDFL_ATTACHEDONLY) && lists_pads(false, 0, 0) && lists_pads(false, 4 | (4 << 8), 0));
		CHECK(!lists_pads(false, 2, 0) && !lists_pads(false, 3, 0) && !lists_pads(false, 4 | (2 << 8), 0));
		CHECK(is_game_controller(true, 0x00010215) && is_game_controller(true, 0x00010114) && is_game_controller(true, DI8DEVTYPE_DRIVING));
		CHECK(!is_game_controller(true, DI8DEVTYPE_KEYBOARD) && !is_game_controller(true, DI8DEVTYPE_MOUSE) && !is_game_controller(true, DI8DEVTYPE_SUPPLEMENTAL));
		CHECK(is_game_controller(false, 0x00010404) && !is_game_controller(false, 3) && !is_game_controller(false, 2));

		// The standard formats, as the game and the engine set them: every Dual Action object where DIJOYSTATE(2) has it.
		const auto objects = pad_profile::logitech_dual_action.objects;
		CHECK(objects.size() == 17);
		CHECK((c_dfDIJoystick2.rgodf[0].dwType & optional_object) && c_dfDIJoystick2.dwDataSize == sizeof(DIJOYSTATE2)); // the SDK's formats mark every object optional
		for (const auto* format : {&c_dfDIJoystick, &c_dfDIJoystick2})
		{
			const auto layout = map_format(*format, objects);
			CHECK(layout.error.empty() && layout.size == format->dwDataSize && layout.fields.size() == 17);
			CHECK(std::ranges::all_of(layout.fields, [](const field& f) { return f.from == f.to; }));
			CHECK((layout.empty_povs == std::vector<DWORD>{DIJOFS_POV(1), DIJOFS_POV(2), DIJOFS_POV(3)}));
		}

		// A format of the caller's own: button instance 1 (A), then Rz and the hat, packed.
		DIOBJECTDATAFORMAT own[] = {
			{nullptr, 0, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(1), 0},
			{&GUID_RzAxis, 4, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
			{&GUID_POV, 8, DIDFT_POV | DIDFT_ANYINSTANCE, 0},
			{&GUID_POV, 12, DIDFT_POV | DIDFT_ANYINSTANCE | optional_object, 0},
		};
		DIDATAFORMAT format{sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS, 16, static_cast<DWORD>(std::size(own)), own};
		const auto layout = map_format(format, objects);
		CHECK(layout.error.empty() && layout.fields.size() == 3 && (layout.empty_povs == std::vector<DWORD>{12}));
		DIJOYSTATE2 joy{};
		joy.rgbButtons[1] = 0x80;
		joy.lRz = 777;
		joy.rgdwPOV[0] = 9000;
		BYTE out[16];
		std::memset(out, 0x55, sizeof(out));
		translate(layout, reinterpret_cast<const BYTE*>(&joy), out);
		LONG rz = 0;
		DWORD pov = 0, empty_pov = 0;
		std::memcpy(&rz, out + 4, 4);
		std::memcpy(&pov, out + 8, 4);
		std::memcpy(&empty_pov, out + 12, 4);
		CHECK(out[0] == 0x80 && out[1] == 0 && out[2] == 0 && out[3] == 0 && rz == 777 && pov == 9000 && empty_pov == 0xFFFFFFFF);
		// By offset, the caller's; by id, by usage; an axis' DIJOYSTATE index.
		const int rz_object = find_object(objects, &layout, 4, DIPH_BYOFFSET);
		CHECK(rz_object >= 0 && std::string(objects[rz_object].name) == "Z Rotation" && axis_of(objects[rz_object]) == 5);
		CHECK(find_object(objects, &layout, 16, DIPH_BYOFFSET) == -1);
		CHECK(find_object(objects, nullptr, DIJOFS_Y, DIPH_BYOFFSET) >= 0 && axis_of(objects[find_object(objects, nullptr, DIJOFS_Y, DIPH_BYOFFSET)]) == 1);
		CHECK(find_object(objects, nullptr, DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(5), DIPH_BYID) == rz_object);
		CHECK(find_object(objects, nullptr, DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(3), DIPH_BYID) == -1); // Rx: not on a Dual Action
		CHECK(find_object(objects, nullptr, MAKELONG(0x35, 0x01), DIPH_BYUSAGE) == rz_object);
		const int button_2 = find_object(objects, nullptr, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(1), DIPH_BYID);
		CHECK(button_2 >= 0 && std::string(objects[button_2].name) == "Button 2" && axis_of(objects[button_2]) == -1);
		// Ranges and deadzones: the pad's axes where the caller's format has them, and all six standard ones answer
		// (Rx, Ry too), as for a real pad presented as a Dual Action; a button or a stranger offset doesn't.
		CHECK(addressed_axis(objects, &layout, 4, DIPH_BYOFFSET) == 5 && addressed_axis(objects, nullptr, DIJOFS_RZ, DIPH_BYOFFSET) == 5);
		CHECK(addressed_axis(objects, nullptr, DIJOFS_RX, DIPH_BYOFFSET) == 3 && addressed_axis(objects, nullptr, DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(4), DIPH_BYID) == 4);
		CHECK(addressed_axis(objects, nullptr, DIJOFS_BUTTON(0), DIPH_BYOFFSET) == -1 && addressed_axis(objects, nullptr, DIJOFS_X + 2, DIPH_BYOFFSET) == -1);
		CHECK(addressed_axis(objects, nullptr, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(1), DIPH_BYID) == -1 && addressed_axis(objects, nullptr, DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(6), DIPH_BYID) == -1);

		// Refused: an object the pad hasn't that the format requires, sizes that aren't a DIDATAFORMAT's, data out of bounds.
		DIOBJECTDATAFORMAT slider[] = {{&GUID_Slider, 0, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0}};
		DIDATAFORMAT needs_slider{sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS, 4, 1, slider};
		CHECK(map_format(needs_slider, objects).error.find("required") != std::string::npos);
		DIDATAFORMAT wrong = format;
		wrong.dwObjSize = 12;
		CHECK(!map_format(wrong, objects).error.empty());
		DIOBJECTDATAFORMAT outside[] = {{&GUID_XAxis, 4, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0}};
		DIDATAFORMAT past_end{sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS, 4, 1, outside};
		CHECK(map_format(past_end, objects).error.find("outside") != std::string::npos);
	}

	// The screenshot files (image_file.hpp): checksums against known values, and the layouts.
	void check_image_file()
	{
		std::printf("image files (the test pipe's screenshots)\n");
		const std::uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
		CHECK(image_file::crc32(digits) == 0xCBF43926);
		CHECK(image_file::crc32(std::span(digits).subspan(4), image_file::crc32(std::span(digits).first(4))) == 0xCBF43926);
		const std::uint8_t wiki[] = {'W', 'i', 'k', 'i', 'p', 'e', 'd', 'i', 'a'};
		CHECK(image_file::adler32(wiki) == 0x11E60398);

		// 2x2: red, green / blue, white, as BGRX.
		const std::uint8_t pixels[] = {0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0};
		const auto bmp = image_file::encode_bmp(2, 2, pixels);
		CHECK(bmp.size() == 54 + 2 * 8); // 6-byte rows padded to 8
		CHECK(bmp[0] == 'B' && bmp[1] == 'M' && bmp[28] == 24);
		CHECK(bmp[54] == 255 && bmp[55] == 0 && bmp[56] == 0); // bottom row first: blue
		CHECK(bmp[62] == 0 && bmp[63] == 0 && bmp[64] == 255); // top row: red

		const auto png = image_file::encode_png(2, 2, pixels);
		CHECK(png.size() == 8 + 25 + (12 + 2 + 5 + 14 + 4) + 12);
		CHECK(png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
		const std::uint32_t ihdr_crc = (static_cast<std::uint32_t>(png[29]) << 24) | (png[30] << 16) | (png[31] << 8) | png[32];
		CHECK(ihdr_crc == 0xFDD49A73); // zlib.crc32(b"IHDR" + 2x2, 8-bit RGB)
		CHECK(png[41] == 0x78 && png[42] == 0x01);                                             // zlib header
		CHECK(png[43] == 1 && png[44] == 14 && png[45] == 0 && png[46] == 0xF1 && png[47] == 0xFF); // one final stored block of 14 bytes
		CHECK(png[48] == 0 && png[49] == 255 && png[50] == 0 && png[51] == 0);                // filter 0, then red as RGB
		CHECK(png[png.size() - 8] == 'I' && png[png.size() - 7] == 'E' && png[png.size() - 6] == 'N' && png[png.size() - 5] == 'D');
	}

	// The back buffer copy behind "screenshot", on a windowed device of our own (never shown).
	void check_d3d8_capture(void* d3d)
	{
		std::printf("Direct3D 8 back buffer capture (the test pipe's screenshot)\n");
		WNDCLASSW wc{};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"xml2_test_capture";
		RegisterClassW(&wc);
		const HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"xml2_test", WS_OVERLAPPEDWINDOW, 0, 0, 64, 48, nullptr, nullptr, wc.hInstance, nullptr);
		CHECK(hwnd != nullptr);

		d3d8::present_parameters pp{64, 48, d3d8::format_x8r8g8b8, 1, d3d8::multisample_none, d3d8::swap_discard, hwnd, TRUE, FALSE, 0, 0, 0, d3d8::present_interval_default};
		void* device = nullptr;
		const HRESULT created = d3d8::method<d3d8::create_device_t>(d3d, d3d8::d3d_slot::create_device)(d3d, 0, d3d8::device_type_hal, hwnd, 0x20 /* software vertex processing */, &pp, &device);
		if (FAILED(created) || !device)
		{
			std::printf("  skip  no Direct3D 8 device here (%08lX)\n", created);
			DestroyWindow(hwnd);
			return;
		}

		CHECK(SUCCEEDED(d3d8::method<d3d8::clear_t>(device, d3d8::device_slot::clear)(device, 0, nullptr, d3d8::clear_target, 0xFF3366CC, 1.0f, 0)));
		frame_capture::frame picture;
		const auto error = frame_capture::read_back_buffer(device, picture);
		if (!error.empty()) std::printf("  info  %s\n", error.c_str());
		CHECK(error.empty());
		CHECK(picture.width == 64 && picture.height == 48 && picture.bgrx.size() == 64 * 48 * 4);
		if (picture.bgrx.size() == 64 * 48 * 4)
		{
			const auto* pixel = picture.bgrx.data() + (10 * 64 + 10) * 4;
			CHECK(pixel[0] == 0xCC && pixel[1] == 0x66 && pixel[2] == 0x33); // the cleared colour, as BGR
		}

		const auto png_path = std::filesystem::temp_directory_path() / "xml2_test_capture.png";
		CHECK(frame_capture::save(picture, png_path).empty());
		const auto png = read_file(png_path);
		CHECK(png.size() == 8 + 25 + (12 + 2 + 5 + 48 * (1 + 64 * 3) + 4) + 12 && png.compare(1, 3, "PNG") == 0);
		const auto bmp_path = std::filesystem::temp_directory_path() / "xml2_test_capture.bmp";
		CHECK(frame_capture::save(picture, bmp_path).empty() && std::filesystem::file_size(bmp_path) == 54 + 64 * 3 * 48);
		CHECK(!frame_capture::save({}, png_path).empty()); // nothing captured
		std::printf("  info  wrote %s and .bmp\n", png_path.string().c_str());

		static_cast<IUnknown*>(device)->Release();
		DestroyWindow(hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
	}

	// Whether a windowed Direct3D 8 present waits for the vertical blank on this desktop, which
	// decides what VSync can mean in the borderless and windowed modes (docs/in-game-options-plan.md,
	// owner decision 4). Times Presents on a small window of our own, shown without taking the focus,
	// with each swap effect, and confirms which presentation intervals a windowed device refuses.
	void check_windowed_presents(void* d3d, const d3d8::display_mode& desktop)
	{
		std::printf("Direct3D 8 windowed presentation (does a window sync to the vertical blank?)\n");
		if (!desktop.refresh_rate)
		{
			std::printf("  skip  the desktop's refresh rate is unknown\n");
			return;
		}
		WNDCLASSW wc{};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"xml2_test_present";
		RegisterClassW(&wc);
		// Bottom right of the primary monitor, a tool window that never activates.
		const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"xml2_test", WS_POPUP,
		                                  static_cast<int>(desktop.width) - 80, static_cast<int>(desktop.height) - 120, 64, 48, nullptr, nullptr, wc.hInstance, nullptr);
		CHECK(hwnd != nullptr);

		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		const double refresh_ms = 1000.0 / desktop.refresh_rate;

		// Average milliseconds per Present over 90 frames, after 10 to settle.
		const auto measure = [&](void* device, const char* what) -> double
		{
			const auto clear = d3d8::method<d3d8::clear_t>(device, d3d8::device_slot::clear);
			const auto present = d3d8::method<d3d8::present_t>(device, d3d8::device_slot::present);
			for (int i = 0; i < 10; ++i)
			{
				clear(device, 0, nullptr, d3d8::clear_target, 0xFF204060, 1.0f, 0);
				present(device, nullptr, nullptr, nullptr, nullptr);
			}
			LARGE_INTEGER start{}, end{};
			QueryPerformanceCounter(&start);
			constexpr int frames = 90;
			for (int i = 0; i < frames; ++i)
			{
				clear(device, 0, nullptr, d3d8::clear_target, 0xFF204060, 1.0f, 0);
				present(device, nullptr, nullptr, nullptr, nullptr);
			}
			QueryPerformanceCounter(&end);
			const double ms = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart) / frames;
			std::printf("  info  %-46s %8.3f ms/frame = %7.1f fps  (%s the %u Hz refresh)\n", what, ms, 1000.0 / ms,
			            ms >= refresh_ms * 0.85 ? "waits for" : "ignores", desktop.refresh_rate);
			return ms;
		};
		const auto synced = [&](const double ms) { return ms >= refresh_ms * 0.85; };

		d3d8::present_parameters pp{64, 48, d3d8::format_x8r8g8b8, 1, d3d8::multisample_none, d3d8::swap_discard, hwnd, TRUE, FALSE, 0, 0, 0, d3d8::present_interval_default};
		const auto create = d3d8::method<d3d8::create_device_t>(d3d, d3d8::d3d_slot::create_device);
		void* device = nullptr;
		HRESULT result = create(d3d, 0, d3d8::device_type_hal, hwnd, 0x20 /* software vertex processing */, &pp, &device);
		if (FAILED(result) || !device)
		{
			std::printf("  skip  no Direct3D 8 device here (%08lX)\n", result);
			DestroyWindow(hwnd);
			UnregisterClassW(wc.lpszClassName, wc.hInstance);
			return;
		}
		const auto reset = d3d8::method<d3d8::reset_t>(device, d3d8::device_slot::reset);

		const double hidden_default = measure(device, "discard, default interval, window hidden");
		ShowWindow(hwnd, SW_SHOWNOACTIVATE);
		Sleep(200); // the compositor picks the window up
		const double shown_default = measure(device, "discard, default interval, window shown");
		pp.swap_effect = d3d8::swap_copy_vsync;
		result = reset(device, &pp);
		CHECK(SUCCEEDED(result));
		const double shown_copy_vsync = SUCCEEDED(result) ? measure(device, "copy_vsync, default interval, window shown") : 0.0;
		timeBeginPeriod(1); // does the runtime's wait poll on the scheduler's tick?
		const double shown_copy_vsync_1ms = SUCCEEDED(result) ? measure(device, "copy_vsync, window shown, timeBeginPeriod(1)") : 0.0;
		timeEndPeriod(1);
		pp.swap_effect = d3d8::swap_copy;
		result = reset(device, &pp);
		CHECK(SUCCEEDED(result));
		const double shown_copy = SUCCEEDED(result) ? measure(device, "copy, default interval, window shown") : 0.0;
		pp.swap_effect = d3d8::swap_discard;
		reset(device, &pp);
		ShowWindow(hwnd, SW_HIDE);
		static_cast<IUnknown*>(device)->Release();
		device = nullptr;

		// The intervals: Direct3D 8 only allows DEFAULT windowed.
		for (const auto [interval, name] : {std::pair{d3d8::present_interval_immediate, "immediate"}, std::pair{d3d8::present_interval_one, "one"}})
		{
			pp.fullscreen_presentation_interval = interval;
			result = create(d3d, 0, d3d8::device_type_hal, hwnd, 0x20, &pp, &device);
			std::printf("  info  windowed device with presentation interval %-9s -> %08lX%s\n", name, result, result == d3d8::err_invalid_call ? " (D3DERR_INVALIDCALL)" : "");
			CHECK(result == d3d8::err_invalid_call && device == nullptr);
			if (device)
			{
				static_cast<IUnknown*>(device)->Release();
				device = nullptr;
			}
		}

		std::printf("  info  conclusion: a windowed present with the default interval %s for the vertical blank on this desktop; copy_vsync %s (%.1f fps; %.1f with a 1 ms tick)%s\n",
		            synced(shown_default) ? "WAITS" : "does NOT wait", synced(shown_copy_vsync) ? "waits" : "does not wait",
		            shown_copy_vsync > 0 ? 1000.0 / shown_copy_vsync : 0.0, shown_copy_vsync_1ms > 0 ? 1000.0 / shown_copy_vsync_1ms : 0.0,
		            synced(hidden_default) ? "; a hidden window waits too" : "; a hidden window doesn't wait");
		(void)shown_copy;
		DestroyWindow(hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
	}

	// ---- The pipe, end to end -----------------------------------------------------------------------

	// The child's pipe has a name of its own ([Test] PipeName), so a game serving the default
	// \\.\pipe\xml2-fix-input (a harness run, say) is never touched by these checks.
	std::string child_pipe_name()
	{
		return "xml2-fix-test-" + std::to_string(GetCurrentProcessId());
	}

	// One request, one reply line.
	std::string ask(const HANDLE pipe, const std::string& line)
	{
		const auto request = line + "\n";
		DWORD written = 0;
		if (!WriteFile(pipe, request.c_str(), static_cast<DWORD>(request.size()), &written, nullptr)) return "error write failed";
		std::string reply;
		char buffer[256];
		DWORD got = 0;
		while (reply.find('\n') == std::string::npos)
		{
			if (!ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) || !got) return "error pipe closed";
			reply.append(buffer, got);
		}
		return reply.substr(0, reply.find('\n'));
	}

	bool ok(const std::string& reply) { return reply.rfind("ok", 0) == 0; }
	bool refused(const std::string& reply) { return reply.rfind("error", 0) == 0; }

	// In the pipe child, whose xml2-fix.ini says [Test] VirtualPads=2: both of the game's DirectInput paths list
	// exactly two Dual Actions, the virtual pads (any real controller hidden), and the pipe's pad commands reach
	// them - a round trip of every pad verb, read the way the game and the engine read their pads.
	void check_virtual_pads(const HANDLE pipe)
	{
		std::printf("virtual pads ([Test] VirtualPads=2, this is the child process)\n");
		using get_device_status_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID);
		const auto is_virtual = [](const found_device& d) { return virtual_pad_rules::pad_of(d.instance, 2).has_value(); };

		const auto status = ask(pipe, "status");
		CHECK(status.find("; virtual pads 2; pad reads 0/0/0/0; pad inputs held 0; ") != std::string::npos);

		// The engine: libIGDisplay.dll's DirectInput 7.
		void* input7 = nullptr;
		CHECK(SUCCEEDED(DirectInputCreateEx(GetModuleHandleW(nullptr), 0x0700, IID_IDirectInput7A, &input7, nullptr)) && input7);
		if (!input7) return;
		std::vector<found_device> engine_pads;
		CHECK(SUCCEEDED(slot<enum_devices_t>(input7, 4)(input7, 4 /* DIDEVTYPE_JOYSTICK */, &collect, &engine_pads, DIEDFL_ATTACHEDONLY)));
		CHECK(engine_pads.size() == 2 && std::ranges::all_of(engine_pads, is_virtual)); // a real pad, if one is on, is hidden
		CHECK(engine_pads.size() == 2 && virtual_pad_rules::pad_of(engine_pads[0].instance, 2) == 0 && virtual_pad_rules::pad_of(engine_pads[1].instance, 2) == 1);
		CHECK(std::ranges::all_of(engine_pads, [](const found_device& d) { return d.product == dual_action && d.name == "Logitech Dual Action"; }));
		std::vector<found_device> mice;
		slot<enum_devices_t>(input7, 4)(input7, 2 /* DIDEVTYPE_MOUSE */, &collect, &mice, DIEDFL_ATTACHEDONLY);
		CHECK(std::ranges::none_of(mice, is_virtual));
		CHECK(slot<get_device_status_t>(input7, 5)(input7, virtual_pad_rules::instance_guid(1)) == DI_OK);
		if (engine_pads.size() != 2)
		{
			static_cast<IUnknown*>(input7)->Release();
			return;
		}

		IUnknown* created = nullptr;
		CHECK(SUCCEEDED(slot<create_device_t>(input7, 3)(input7, engine_pads[0].instance, reinterpret_cast<void**>(&created), nullptr)) && created);
		void* engine_view = nullptr;
		CHECK(created && SUCCEEDED(created->QueryInterface(IID_IDirectInputDevice2A, &engine_view)));
		if (created) created->Release();
		auto* engine = static_cast<IDirectInputDevice8A*>(engine_view); // the first 26 slots match IDirectInputDevice2A
		if (!engine)
		{
			static_cast<IUnknown*>(input7)->Release();
			return;
		}
		CHECK(SUCCEEDED(engine->SetDataFormat(&c_dfDIJoystick)));
		CHECK(SUCCEEDED(engine->SetCooperativeLevel(GetConsoleWindow(), DISCL_EXCLUSIVE | DISCL_FOREGROUND)));
		bool engine_ranges = true;
		for (DWORD offset = 0; offset <= DIJOFS_RZ; offset += sizeof(LONG))
		{
			DIPROPRANGE range{{sizeof(DIPROPRANGE), sizeof(DIPROPHEADER), offset, DIPH_BYOFFSET}, -1000, 1000};
			engine_ranges &= SUCCEEDED(engine->SetProperty(DIPROP_RANGE, &range.diph)); // Rx and Ry too, as for a real pad presented as one
		}
		CHECK(engine_ranges);
		DIDEVCAPS caps{sizeof(caps)};
		CHECK(SUCCEEDED(engine->GetCapabilities(&caps)) && caps.dwAxes == 4 && caps.dwButtons == 12 && caps.dwPOVs == 1 && (caps.dwFlags & DIDC_ATTACHED));
		CHECK(caps.dwDevType == virtual_pad_rules::device_type(false));
		DIDEVICEINSTANCEA info{sizeof(info)};
		CHECK(SUCCEEDED(engine->GetDeviceInfo(&info)) && info.guidProduct.Data1 == dual_action && IsEqualGUID(info.guidInstance, engine_pads[0].instance));
		CHECK(SUCCEEDED(engine->Acquire()) && SUCCEEDED(engine->Poll()));
		DIJOYSTATE engine_state{};
		CHECK(SUCCEEDED(engine->GetDeviceState(sizeof(engine_state), &engine_state)));
		CHECK(engine_state.lX == 0 && engine_state.lY == 0 && engine_state.lZ == 0 && engine_state.lRz == 0 && engine_state.rgdwPOV[0] == 0xFFFFFFFF);
		CHECK(engine->GetDeviceState(sizeof(DIJOYSTATE2), &engine_state) == DIERR_INVALIDPARAM); // not the format it set

		// The game: XMen2.exe's own DirectInput 8, as 0x628e20 / 0x628b40 / 0x6285c0 use it.
		wchar_t folder[MAX_PATH]{};
		GetSystemDirectoryW(folder, MAX_PATH);
		const auto dinput8 = LoadLibraryW((std::wstring(folder) + L"\\dinput8.dll").c_str());
		const auto create = dinput8 ? reinterpret_cast<decltype(&DirectInput8Create)>(GetProcAddress(dinput8, "DirectInput8Create")) : nullptr;
		IDirectInput8A* input8 = nullptr;
		if (create) create(GetModuleHandleW(nullptr), 0x0800, IID_IDirectInput8A, reinterpret_cast<void**>(&input8), nullptr);
		CHECK(input8 != nullptr);
		if (!input8)
		{
			engine->Release();
			static_cast<IUnknown*>(input7)->Release();
			return;
		}
		std::vector<found_device> game_pads;
		CHECK(SUCCEEDED(input8->EnumDevices(DI8DEVCLASS_GAMECTRL, &collect, &game_pads, DIEDFL_ATTACHEDONLY)));
		CHECK(game_pads.size() == 2 && std::ranges::all_of(game_pads, is_virtual));
		std::vector<found_device> keyboards;
		input8->EnumDevices(DI8DEVCLASS_KEYBOARD, &collect, &keyboards, DIEDFL_ATTACHEDONLY);
		CHECK(!keyboards.empty() && std::ranges::none_of(keyboards, is_virtual));
		CHECK(input8->GetDeviceStatus(virtual_pad_rules::instance_guid(0)) == DI_OK);
		if (game_pads.size() != 2)
		{
			input8->Release();
			engine->Release();
			static_cast<IUnknown*>(input7)->Release();
			return;
		}

		std::array<IDirectInputDevice8A*, 2> pads{};
		for (int i = 0; i < 2; ++i)
		{
			CHECK(SUCCEEDED(input8->CreateDevice(game_pads[i].instance, &pads[i], nullptr)) && pads[i]);
			if (!pads[i]) continue;
			CHECK(SUCCEEDED(pads[i]->SetDataFormat(&c_dfDIJoystick2)));
			CHECK(SUCCEEDED(pads[i]->SetCooperativeLevel(GetConsoleWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)));
			object_list objects{pads[i]};
			CHECK(SUCCEEDED(pads[i]->EnumObjects(&range_object, &objects, DIDFT_AXIS)) && objects.names.size() == 4 && objects.ranges_ok); // the game's filter, 3
			CHECK(SUCCEEDED(pads[i]->Acquire()));
		}
		if (!pads[0] || !pads[1])
		{
			for (auto* p : pads) if (p) p->Release();
			input8->Release();
			engine->Release();
			static_cast<IUnknown*>(input7)->Release();
			return;
		}
		object_list all{pads[0]};
		CHECK(SUCCEEDED(pads[0]->EnumObjects(&range_object, &all, DIDFT_ALL)) && all.names.size() == 17);
		DIPROPDWORD vidpid{{sizeof(DIPROPDWORD), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}};
		CHECK(SUCCEEDED(pads[1]->GetProperty(DIPROP_VIDPID, &vidpid.diph)) && vidpid.dwData == dual_action);
		DIPROPRANGE rz{{sizeof(DIPROPRANGE), sizeof(DIPROPHEADER), DIJOFS_RZ, DIPH_BYOFFSET}};
		CHECK(SUCCEEDED(pads[1]->GetProperty(DIPROP_RANGE, &rz.diph)) && rz.lMin == -1000 && rz.lMax == 1000);
		DIPROPSTRING name{{sizeof(DIPROPSTRING), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}};
		CHECK(SUCCEEDED(pads[1]->GetProperty(DIPROP_PRODUCTNAME, &name.diph)) && std::wstring(name.wsz) == L"Logitech Dual Action");
		DIDEVICEOBJECTINSTANCEA object{sizeof(object)};
		CHECK(SUCCEEDED(pads[0]->GetObjectInfo(&object, DIJOFS_BUTTON(9), DIPH_BYOFFSET)) && std::string(object.tszName) == "Button 10");
		CHECK(pads[0]->GetObjectInfo(&object, DIJOFS_BUTTON(12), DIPH_BYOFFSET) == DIERR_OBJECTNOTFOUND);
		IUnknown* effect = reinterpret_cast<IUnknown*>(1);
		CHECK(FAILED(pads[0]->CreateEffect(GUID_ConstantForce, nullptr, reinterpret_cast<IDirectInputEffect**>(&effect), nullptr)) && effect == nullptr);

		const auto read_game = [&](const int i)
		{
			DIJOYSTATE2 state{};
			if (FAILED(pads[i]->Poll()) || FAILED(pads[i]->GetDeviceState(sizeof(state), &state))) state.lX = 12345; // no read: fails the checks
			return state;
		};
		const auto read_engine = [&]
		{
			DIJOYSTATE state{};
			engine->Poll();
			engine->GetDeviceState(sizeof(state), &state);
			return state;
		};
		// Polls both pads (and the engine's pad 1) like the game's frames, 5 ms apart, until `seen` or `times` reads.
		const auto poll = [&](const int times, auto seen)
		{
			for (int n = 0; n < times; ++n)
			{
				const auto one = read_game(0), two = read_game(1);
				const auto engine_one = read_engine();
				if (seen(one, two, engine_one)) return true;
				Sleep(5);
			}
			return false;
		};
		const auto idle = [](const DIJOYSTATE2& s)
		{
			return s.lX == 0 && s.lY == 0 && s.lZ == 0 && s.lRz == 0 && s.rgdwPOV[0] == 0xFFFFFFFF &&
			       std::all_of(s.rgbButtons, s.rgbButtons + 128, [](const BYTE b) { return b == 0; });
		};
		CHECK(idle(read_game(0)) && idle(read_game(1)));

		// pad 1 A: the Dual Action's button 2 on pad 1, in both paths, and nothing on pad 2 meanwhile.
		std::string reply;
		bool pad_two_touched = false;
		std::thread tapper([&] { reply = ask(pipe, "pad 1 A 300"); });
		CHECK(poll(300, [&](const DIJOYSTATE2& one, const DIJOYSTATE2& two, const DIJOYSTATE& engine_one)
		           {
			           pad_two_touched = pad_two_touched || !idle(two);
			           return one.rgbButtons[1] == 0x80 && engine_one.rgbButtons[1] == 0x80 && one.rgbButtons[0] == 0 && one.rgbButtons[2] == 0;
		           }));
		tapper.join();
		CHECK(ok(reply) && !pad_two_touched);
		CHECK(idle(read_game(0)) && read_engine().rgbButtons[1] == 0); // let go again

		// paddown / padup on pad 2: START, the Dual Action's button 10 - player 2's join and pause.
		CHECK(ok(ask(pipe, "paddown 2 START")));
		CHECK(poll(4, [](const auto& one, const auto& two, const auto&) { return two.rgbButtons[9] == 0x80 && one.rgbButtons[9] == 0; }));
		const auto held_status = ask(pipe, "status");
		CHECK(held_status.find("pad inputs held 1") != std::string::npos && held_status.find("pad reads 0/") == std::string::npos);
		CHECK(ok(ask(pipe, "padup 2 START")));
		CHECK(idle(read_game(1)));

		// Sticks: X, Y up; the right stick on Z / Rz, as the Dual Action has it. Without ms they stay until changed.
		CHECK(ok(ask(pipe, "stick 1 L 0 1")));
		CHECK(poll(4, [](const auto& one, const auto&, const auto& engine_one) { return one.lY == -1000 && one.lX == 0 && engine_one.lY == -1000; }));
		CHECK(ok(ask(pipe, "stick 1 R -1 0")));
		CHECK(poll(4, [](const auto& one, const auto&, const auto&) { return one.lZ == -1000 && one.lY == -1000; }));
		CHECK(ok(ask(pipe, "stick 1 L 0 0")));
		CHECK(poll(4, [](const auto& one, const auto&, const auto&) { return one.lY == 0 && one.lZ == -1000; }));
		CHECK(ok(ask(pipe, "padup 1 ALL")));
		CHECK(idle(read_game(0)));
		// With ms: held that long, then centred, the reply once it's over.
		std::thread sticker([&] { reply = ask(pipe, "stick 2 L 0.5 -0.5 200"); });
		CHECK(poll(200, [](const auto&, const auto& two, const auto&) { return two.lX == 500 && two.lY == 500; }));
		sticker.join();
		CHECK(ok(reply) && idle(read_game(1)));

		// Triggers: the Dual Action's buttons 7 (LT) and 8 (RT), past XInput's threshold.
		CHECK(ok(ask(pipe, "trigger 2 R 1")));
		CHECK(poll(4, [](const auto&, const auto& two, const auto&) { return two.rgbButtons[7] == 0x80 && two.rgbButtons[6] == 0; }));
		CHECK(ok(ask(pipe, "trigger 2 R 0")));
		CHECK(ok(ask(pipe, "paddown 2 LT")));
		CHECK(poll(4, [](const auto&, const auto& two, const auto&) { return two.rgbButtons[6] == 0x80 && two.rgbButtons[7] == 0; }));
		CHECK(ok(ask(pipe, "padrelease 2")));
		CHECK(idle(read_game(1)));

		// The d-pad is the hat; padhold blocks for its duration.
		CHECK(ok(ask(pipe, "paddown 1 UP+RIGHT")));
		CHECK(poll(4, [](const auto& one, const auto&, const auto& engine_one) { return one.rgdwPOV[0] == 4500 && engine_one.rgdwPOV[0] == 4500; }));
		CHECK(ok(ask(pipe, "release"))); // keys and pads
		CHECK(idle(read_game(0)));
		std::thread holder([&] { reply = ask(pipe, "padhold 2 LB+Y 200"); });
		CHECK(poll(200, [](const auto&, const auto& two, const auto&) { return two.rgbButtons[4] == 0x80 && two.rgbButtons[3] == 0x80; }));
		holder.join();
		CHECK(ok(reply) && idle(read_game(1)));

		// Refused: a pad the game hasn't (after waiting for a read of it), and lines that aren't pad commands.
		const auto missing = ask(pipe, "pad 3 A");
		std::printf("  info  %s\n", missing.c_str());
		CHECK(refused(missing) && missing.find("VirtualPads=2: the game has pads 1 to 2") != std::string::npos);
		CHECK(refused(ask(pipe, "pad 5 A")) && refused(ask(pipe, "stick 1 L 3 0")) && refused(ask(pipe, "padhold 1 A")));
		const auto after = ask(pipe, "status");
		std::printf("  info  %s\n", after.c_str());
		CHECK(after.find("pad inputs held 0") != std::string::npos);

		for (auto* p : pads) p->Release();
		input8->Release();
		engine->Unacquire();
		engine->Release();
		static_cast<IUnknown*>(input7)->Release();

		const auto log = read_file(module_dir() / "xml2-fix.log");
		CHECK(log.find("virtual pads: the game sees 2 Logitech Dual Action pads ([Test] VirtualPads) in place of any real controller") != std::string::npos);
		CHECK(log.find("virtual pads: DirectInput 7 lists the game 2 virtual pads") != std::string::npos && log.find("virtual pads: DirectInput 8 lists the game 2 virtual pads") != std::string::npos);
		CHECK(log.find("virtual pads: DirectInput 7 creates virtual pad 1") != std::string::npos && log.find("virtual pads: DirectInput 8 creates virtual pad 2") != std::string::npos);
		CHECK(log.find("virtual pads: the game reads virtual pad 1 through DirectInput 7") != std::string::npos);
		CHECK(log.find("virtual pads: the game reads virtual pad 2 through DirectInput 8") != std::string::npos);
		CHECK(log.find("test: the game reads pad 1 - pipe pad commands reach it") != std::string::npos);
		CHECK(log.find("test: pad 1 tap A 300 ms") != std::string::npos && log.find("test: pad 2 hold Y+LB 200 ms") != std::string::npos);
		CHECK(log.find("test: pad 3 tap A 80 ms - the game didn't read the pad meanwhile") != std::string::npos);
		if (connected_xinput_pads() > 0)
		{
			CHECK(log.find("virtual pads: the game doesn't see the real controller") != std::string::npos);
		}
	}

	// Runs in the child process (run_pipe_child), whose dinput.dll saw [Test] InputPipe=1 with
	// PipeName=`name`, VirtualPads=2, and [Online] Server=127.0.0.1.
	void check_pipe(const std::string& name)
	{
		const auto pipe_path = "\\\\.\\pipe\\" + name;
		std::printf("test input pipe ([Test] InputPipe=1, this is the child process)\n");
		// First, as it needs no pipe: [Game] PostgameScript set, but this isn't XMen2.exe - the
		// credits' push is left alone (the DLL logged it while this process loaded it).
		const auto start_log = read_file(module_dir() / "xml2-fix.log");
		CHECK(start_log.find("postgame: XMen2.exe isn't loaded at 0x400000 (not the game?) - the end credits load XML2's act5/egypt/egypt6 as before") != std::string::npos ||
		      start_log.find("isn't the retail code (CREDITS_MENU") != std::string::npos);
		CHECK(start_log.find("now points at") == std::string::npos);
		// [Game] MainMenuItems likewise: parsed, then refused as this isn't the game.
		CHECK(start_log.find("main menu: XMen2.exe isn't loaded at 0x400000 (not the game?) - the main menu keeps XML2's item names") != std::string::npos ||
		      start_log.find("main menu: 0x005B855D isn't the retail code") != std::string::npos);
		CHECK(start_log.find("name pushes re-pointed") == std::string::npos);
		// [Game] XPCurve=xml1 likewise.
		CHECK(start_log.find("xp curve: XMen2.exe isn't loaded at 0x400000 (not the game?) - XML2's own levels and kill XP stay") != std::string::npos ||
		      start_log.find("xp curve: 0x00448A90 isn't the retail code") != std::string::npos);
		CHECK(start_log.find("xp curve: X-Men Legends 1's") == std::string::npos);

		// [Online] Server=127.0.0.1: every GameSpy and OpenSpy name resolves to it, each logged once.
		WSADATA wsa{};
		WSAStartup(MAKEWORD(2, 2), &wsa);
		const auto resolved = [](const char* host) -> std::string
		{
			const hostent* found = gethostbyname(host);
			return found && found->h_addr_list[0] ? inet_ntoa(*reinterpret_cast<in_addr*>(found->h_addr_list[0])) : "";
		};
		CHECK(resolved("xmenlegpc.master.gamespy.com") == "127.0.0.1");
		CHECK(resolved("xmenlegpc.master.gamespy.com") == "127.0.0.1"); // again: logged once
		CHECK(resolved("xmenlegpc.available.gamespy.com") == "127.0.0.1" && resolved("xmenlegpc.ms7.gamespy.com") == "127.0.0.1");
		CHECK(resolved("natneg1.gamespy.com") == "127.0.0.1" && resolved("natneg2.gamespy.com") == "127.0.0.1" && resolved("peerchat.gamespy.com") == "127.0.0.1");
		CHECK(resolved("xmenlegpc.ms7.openspy.net") == "127.0.0.1");
		CHECK(resolved("xmenlegpc.gamespy.com.invalid") != "127.0.0.1"); // not GameSpy's: a real lookup (.invalid never resolves)
		WSACleanup();
		const auto online_log = read_file(module_dir() / "xml2-fix.log");
		CHECK(online_log.find("online: every GameSpy and OpenSpy host resolves to 127.0.0.1 ([Online] Server)") != std::string::npos);
		const std::string mapped = "online: xmenlegpc.master.gamespy.com -> 127.0.0.1 ([Online] Server)";
		CHECK(online_log.find(mapped) != std::string::npos && online_log.find(mapped) == online_log.rfind(mapped));
		CHECK(online_log.find("online: natneg1.gamespy.com -> 127.0.0.1 ([Online] Server)") != std::string::npos);
		CHECK(online_log.find("GameSpy servers redirected to") == std::string::npos); // Server wins over the default Domain
		CHECK(online_log.find(".invalid ->") == std::string::npos);

		// [Online] LocalIP=203.0.113.9, which isn't this PC's: this PC's name resolves as with auto, and the log says so.
		CHECK(online_log.find("online: LocalIP=203.0.113.9 - that address first when the game looks up this PC's name") != std::string::npos);
		{
			WSAStartup(MAKEWORD(2, 2), &wsa);
			char own[256]{};
			CHECK(gethostname(own, sizeof(own)) == 0);
			const hostent* mine = gethostbyname(own);
			CHECK(mine != nullptr && mine->h_addr_list[0] != nullptr);
			const auto route = local_ip::default_route_address();
			if (mine && mine->h_addr_list[0] && route)
			{
				bool listed = false;
				for (auto** entry = mine->h_addr_list; *entry; ++entry) listed = listed || reinterpret_cast<const in_addr*>(*entry)->s_addr == *route;
				CHECK(!listed || reinterpret_cast<const in_addr*>(mine->h_addr_list[0])->s_addr == *route);
			}
			WSACleanup();
			const auto local_log = read_file(module_dir() / "xml2-fix.log");
			CHECK(local_log.find("online: LocalIP: [Online] LocalIP=203.0.113.9 isn't one of this PC's addresses - auto: ") != std::string::npos);
		}

		HANDLE pipe = INVALID_HANDLE_VALUE;
		for (int attempt = 0; attempt < 50 && pipe == INVALID_HANDLE_VALUE; ++attempt)
		{
			pipe = CreateFileA(pipe_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			if (pipe == INVALID_HANDLE_VALUE) Sleep(100);
		}
		CHECK(pipe != INVALID_HANDLE_VALUE);
		if (pipe == INVALID_HANDLE_VALUE) return;

		CHECK(ok(ask(pipe, "ping")));
		const auto status = ask(pipe, "status");
		std::printf("  info  %s\n", status.c_str());
		CHECK(ok(status));
		CHECK(status.find("; fps 0.0; frame rate the game's own 60 fps cap") != std::string::npos); // no frames drawn here, no [Display] FrameRate
		CHECK(status.find("; actors -; names -; motions -; igb -") != std::string::npos);            // not XMen2.exe: no engine tables to count
		CHECK(refused(ask(pipe, "frob")));
		CHECK(refused(ask(pipe, "tap NOSUCHKEY")));
		CHECK(refused(ask(pipe, "screenshot")));
		CHECK(refused(ask(pipe, "tap ENTER 20"))); // nothing reads a keyboard yet
		const auto wm = ask(pipe, "wm ENTER 20");
		CHECK(refused(wm) && wm.find("use tap") != std::string::npos); // the panel reads DirectInput: tap drives it
		const auto shot = ask(pipe, "screenshot " + (std::filesystem::temp_directory_path() / "xml2_test_pipe.png").string());
		std::printf("  info  %s\n", shot.c_str());
		CHECK(refused(shot)); // no Direct3D device in this process: times out

		// script and console: this isn't XMen2.exe, so its console code isn't here - refused at once,
		// before anything waits for the game's thread.
		const ULONGLONG asked = GetTickCount64();
		const auto script = ask(pipe, "script unlockCharacter(\"storm\", \"\")");
		const auto console = ask(pipe, "console loadmap nyc/alison/nyc1_1_3 1");
		std::printf("  info  %s\n", script.c_str());
		CHECK(refused(script) && script.find("doesn't have the expected code at 0x0055C890") != std::string::npos);
		CHECK(refused(console) && console.find("doesn't have the expected code at 0x0055C890") != std::string::npos);
		CHECK(GetTickCount64() - asked < 1000);
		CHECK(refused(ask(pipe, "script")) && refused(ask(pipe, "console " + std::string(128, 'x'))));
		CHECK(ok(ask(pipe, "ping"))); // the pipe carries on

		check_virtual_pads(pipe);

		// The keyboard as XMen2.exe creates it: its own DirectInput 8, through the fix's wrapper.
		wchar_t folder[MAX_PATH]{};
		GetSystemDirectoryW(folder, MAX_PATH);
		const auto dinput8 = LoadLibraryW((std::wstring(folder) + L"\\dinput8.dll").c_str());
		const auto create = dinput8 ? reinterpret_cast<decltype(&DirectInput8Create)>(GetProcAddress(dinput8, "DirectInput8Create")) : nullptr;
		IDirectInput8A* input = nullptr;
		IDirectInputDevice8A* keyboard = nullptr;
		if (create) create(GetModuleHandleW(nullptr), 0x0800, IID_IDirectInput8A, reinterpret_cast<void**>(&input), nullptr);
		if (input) input->CreateDevice(GUID_SysKeyboard, &keyboard, nullptr);
		if (!keyboard)
		{
			std::printf("  skip  no DirectInput keyboard device here\n");
			CloseHandle(pipe);
			return;
		}
		CHECK(SUCCEEDED(keyboard->SetDataFormat(&c_dfDIKeyboard)));
		CHECK(SUCCEEDED(keyboard->SetCooperativeLevel(GetConsoleWindow(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY))); // the game's flags
		CHECK(SUCCEEDED(keyboard->Acquire()));
		BYTE state[256]{};
		CHECK(SUCCEEDED(keyboard->GetDeviceState(sizeof(state), state)));

		// A tap over the pipe while this thread polls the device like the game does.
		const auto poll = [&](const int times, const BYTE code)
		{
			bool seen = false;
			for (int i = 0; i < times; ++i)
			{
				if (SUCCEEDED(keyboard->GetDeviceState(sizeof(state), state)) && (state[code] & 0x80)) seen = true;
				Sleep(5);
			}
			return seen;
		};
		std::string reply;
		std::thread tapper([&] { reply = ask(pipe, "tap RETURN 300"); });
		const bool seen_down = poll(300, DIK_RETURN);
		tapper.join();
		CHECK(seen_down);
		CHECK(ok(reply));
		CHECK(!poll(10, DIK_RETURN)); // released again

		CHECK(ok(ask(pipe, "down W+D")));
		CHECK(poll(4, DIK_W) && (state[DIK_D] & 0x80));
		CHECK(ok(ask(pipe, "up W")));
		CHECK(!poll(4, DIK_W) && (state[DIK_D] & 0x80));
		CHECK(ok(ask(pipe, "release")));
		CHECK(!poll(4, DIK_D));

		std::thread tapper2([&] { reply = ask(pipe, "tap 0x1C"); }); // by scancode, the default 80 ms
		CHECK(poll(200, DIK_RETURN));
		tapper2.join();
		CHECK(ok(reply));

		keyboard->Unacquire();
		keyboard->Release();
		input->Release();
		CloseHandle(pipe);

		const auto log = read_file(module_dir() / "xml2-fix.log");
		CHECK(log.find("test: input pipe " + pipe_path + " ([Test] InputPipe=1, PipeName " + name + ")") != std::string::npos);
		CHECK(log.find("keyboard cooperative level 16 -> A (background, non-exclusive): ok") != std::string::npos); // FOREGROUND|NONEXCLUSIVE|NOWINKEY -> BACKGROUND|NONEXCLUSIVE
		CHECK(log.find("the game reads its DirectInput keyboard") != std::string::npos);
		const std::string guard_line = "test: XMen2.exe doesn't have the expected code at 0x0055C890";
		CHECK(log.find(guard_line) != std::string::npos && log.find(guard_line) == log.rfind(guard_line)); // logged once
		CHECK(log.find("test: script runscript unlockCharacter(\"storm\",\"\") - not queued") != std::string::npos);
		CHECK(log.find("test: console loadmap nyc/alison/nyc1_1_3 1 - not queued") != std::string::npos);
		CHECK(log.find("display: as the game has it") != std::string::npos && log.find("hooked for the test pipe") != std::string::npos);
		CHECK(log.find("options: XMen2.exe doesn't have the expected code") != std::string::npos); // this isn't the game: the panel is left alone
		// ResolutionList=all, but no libIGGfx.dll here, so no mode-list hooks: the table must not be
		// relocated at all (the game's writer has no bounds check; only the hooks keep it within 64).
		CHECK(log.find("hooked for the test pipe's screenshots, the Video options list (ResolutionList)") != std::string::npos);
		CHECK(log.find("the Video options list and its table stay the game's own") != std::string::npos);
		CHECK(log.find("resolution list:") == std::string::npos && log.find("references patched") == std::string::npos);
		// [Limits] ActorSlots=127, but this isn't XMen2.exe: the name table it needs isn't raised, so neither is it.
		CHECK(log.find("ResourceNames=1024, as no ResourceNames says otherwise") != std::string::npos);
		CHECK(log.find("the resource name table stays at 450 names") != std::string::npos && log.find("limits: the actor table stays at 40 slots") != std::string::npos);
		CHECK(log.find("raised from") == std::string::npos);
		// [Game] ForcedTeams=1, but this isn't XMen2.exe: nothing registered, the mod's scripts open the team menu.
		CHECK(log.find("- no script functions registered; the mod's scripts open the team menu") != std::string::npos && log.find("script functions added") == std::string::npos);
	}

	// Starts this program again with an xml2-fix.ini next to it that turns the pipe on, so its
	// dinput.dll runs check_pipe's side. Last, because that DLL starts xml2-fix.log over.
	int run_pipe_child()
	{
		// The child serves a pipe named after this process ([Test] PipeName), never the default one: with the
		// default name and a game already serving it, the checks connected to that game and drove it (2026-09-28:
		// taps, a queued unlockCharacter and a loadmap went into a running harness game).
		const auto pipe_name = child_pipe_name();
		std::printf("test input pipe: starting a child with [Test] InputPipe=1, PipeName=%s\n", pipe_name.c_str());
		const auto ini = module_dir() / "xml2-fix.ini";
		if (std::filesystem::exists(ini))
		{
			std::printf("  skip  an xml2-fix.ini already sits next to the test\n");
			return 0;
		}
		{
			std::ofstream out(ini, std::ios::binary);
			out << "[Test]\r\nInputPipe=1\r\nPipeName=" << pipe_name << " ; this test's own\r\nVirtualPads=2 ; two pads with nothing plugged in\r\n[Display]\r\nResolutionList=all\r\n[Limits]\r\nActorSlots=127\r\n[Game]\r\nForcedTeams=1\r\nAddHero=1\r\nPostgameScript=x1/menus/postgame ; XML1's r505\r\nMainMenuItems=button1,button2,button3,button4,button5,button6,button7 ; XML1's buttons\r\n"
			       "XPCurve=xml1 ; XML1's levels and kill XP\r\n[Online]\r\nServer=127.0.0.1 ; a private OpenSpy stack\r\nLocalIP=203.0.113.9 ; not this PC's\r\n";
		}

		wchar_t exe[MAX_PATH]{};
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		std::wstring command = std::wstring(L"\"") + exe + L"\" --pipe-child " + std::wstring(pipe_name.begin(), pipe_name.end());
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		int result = 1;
		if (CreateProcessW(exe, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process))
		{
			if (WaitForSingleObject(process.hProcess, 60000) == WAIT_OBJECT_0)
			{
				DWORD code = 1;
				GetExitCodeProcess(process.hProcess, &code);
				result = static_cast<int>(code);
			}
			else
			{
				TerminateProcess(process.hProcess, 1);
				std::printf("  FAIL  the child didn't finish in 60 s\n");
			}
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
		}
		else
		{
			std::printf("  FAIL  couldn't start the child (error %lu)\n", GetLastError());
		}
		std::error_code ignored;
		std::filesystem::remove(ini, ignored);
		return result;
	}

	void check_online()
	{
		std::printf("online (GameSpy -> OpenSpy)\n");
		WSADATA wsa{};
		WSAStartup(MAKEWORD(2, 2), &wsa);
		const hostent* gamespy = gethostbyname("xmenlegpc.master.gamespy.com");
		CHECK(gamespy != nullptr);
		in_addr redirected{};
		if (gamespy)
		{
			redirected = *reinterpret_cast<in_addr*>(gamespy->h_addr_list[0]); // hostent is reused by the next lookup
			std::printf("  info  xmenlegpc.master.gamespy.com resolves to %s\n", inet_ntoa(redirected));
		}
		const hostent* openspy = gethostbyname("xmenlegpc.master.openspy.net");
		CHECK(openspy != nullptr && reinterpret_cast<in_addr*>(openspy->h_addr_list[0])->s_addr == redirected.s_addr);
		const hostent* other = gethostbyname("localhost");
		CHECK(other != nullptr); // non-GameSpy names are untouched
		WSACleanup();
	}

	void check_online_rules()
	{
		using namespace online_rules;
		using mode = plan::mode;
		std::printf("online rules ([Online] Domain, Server)\n");

		// Which names are GameSpy's: a host under gamespy.com, any case, not the bare domain or a look-alike.
		CHECK(is_gamespy_host("xmenlegpc.master.gamespy.com") && is_gamespy_host("natneg1.gamespy.com") && is_gamespy_host("XMENLEGPC.MS7.GameSpy.Com"));
		CHECK(is_gamespy_host("peerchat.gamespy.com") && is_gamespy_host("gpcm.gamespy.com") && is_gamespy_host("xmenlegpc.available.gamespy.com"));
		CHECK(!is_gamespy_host("gamespy.com") && !is_gamespy_host(".gamespy.com") && !is_gamespy_host("notgamespy.com") && !is_gamespy_host("xgamespy.com"));
		CHECK(!is_gamespy_host("gamespy.com.example") && !is_gamespy_host("master.gamespy.co") && !is_gamespy_host("") && !is_gamespy_host("localhost"));
		// Server also catches OpenSpy's names (already renamed ones land on the same server).
		CHECK(is_server_host("xmenlegpc.ms7.openspy.net") && is_server_host("natneg2.OPENSPY.NET") && is_server_host("natneg2.gamespy.com"));
		CHECK(!is_server_host("openspy.net") && !is_server_host("myopenspy.net") && !is_server_host("openspy.net.invalid") && !is_server_host("example.com"));

		// Addresses: dotted quads only, leading zeros dropped.
		CHECK(parse_ipv4("127.0.0.1") == "127.0.0.1" && parse_ipv4("192.168.1.20") == "192.168.1.20" && parse_ipv4("0.0.0.0") == "0.0.0.0");
		CHECK(parse_ipv4("255.255.255.255") == "255.255.255.255" && parse_ipv4("127.000.0.01") == "127.0.0.1");
		CHECK(!parse_ipv4("256.0.0.1") && !parse_ipv4("1.2.3") && !parse_ipv4("1.2.3.4.5") && !parse_ipv4("1.2.3.") && !parse_ipv4(".1.2.3"));
		CHECK(!parse_ipv4("1..2.3") && !parse_ipv4("1.2.3.1234") && !parse_ipv4("-1.2.3.4") && !parse_ipv4("localhost") && !parse_ipv4("") && !parse_ipv4("1.2.3.4 "));
		CHECK(!parse_ipv4("0x7f.0.0.1") && !parse_ipv4("::1") && !parse_ipv4("1.2.3.4:6667"));

		// The plan: Domain as before (openspy.net when the key isn't set - absent or empty, the fix's one ini rule -
		// and off = no redirect)...
		auto chosen = choose("openspy.net", "");
		CHECK(chosen.how == mode::domain && chosen.target == "openspy.net" && chosen.problem.empty());
		CHECK(choose("off", "").how == mode::off && choose("OFF ; no online", "").how == mode::off);
		chosen = choose("", "");
		CHECK(chosen.how == mode::domain && chosen.target == "openspy.net" && choose("   ; later", "").target == "openspy.net");
		chosen = choose("example.org   ; my own server", "");
		CHECK(chosen.how == mode::domain && chosen.target == "example.org"); // the README's inline comment isn't part of the domain
		// ...and a Server address wins over it, with or without a comment.
		chosen = choose("openspy.net", "127.0.0.1");
		CHECK(chosen.how == mode::server && chosen.target == "127.0.0.1" && chosen.problem.empty());
		chosen = choose("off", " 192.168.1.20 ; the stack on the LAN box");
		CHECK(chosen.how == mode::server && chosen.target == "192.168.1.20");
		CHECK(choose("openspy.net", "; 127.0.0.1 commented out").how == mode::domain);
		// A Server that isn't an address is ignored, and says so; Domain stays in charge.
		chosen = choose("openspy.net", "my.server.example");
		CHECK(chosen.how == mode::domain && chosen.target == "openspy.net" && chosen.problem.find("Server=my.server.example isn't an IPv4 address") != std::string::npos);
		chosen = choose("off", "127.0.0.256");
		CHECK(chosen.how == mode::off && !chosen.problem.empty());

		// Lookups under each plan.
		const auto domain_plan = choose("openspy.net", "");
		CHECK(redirect("xmenlegpc.master.gamespy.com", domain_plan) == "xmenlegpc.master.openspy.net");
		CHECK(redirect("xmenlegpc.ms7.gamespy.com", domain_plan) == "xmenlegpc.ms7.openspy.net");
		CHECK(redirect("NatNeg1.GameSpy.com", domain_plan) == "NatNeg1.openspy.net"); // the host's own part kept as it is
		CHECK(!redirect("xmenlegpc.master.openspy.net", domain_plan) && !redirect("localhost", domain_plan) && !redirect("gamespy.com", domain_plan));
		const auto server_plan = choose("openspy.net", "127.0.0.1");
		for (const char* host : {"xmenlegpc.available.gamespy.com", "xmenlegpc.master.gamespy.com", "xmenlegpc.ms7.gamespy.com", "natneg1.gamespy.com",
		                         "natneg2.gamespy.com", "peerchat.gamespy.com", "gpcm.gamespy.com", "gpsp.gamespy.com", "xmenlegpc.ms7.openspy.net"})
		{
			if (redirect(host, server_plan) != "127.0.0.1")
			{
				std::printf("  FAIL  %s isn't sent to [Online] Server\n", host);
				++failures;
			}
		}
		CHECK(!redirect("localhost", server_plan) && !redirect("www.example.com", server_plan) && !redirect("gamespy.com", server_plan));
		CHECK(!redirect("xmenlegpc.master.gamespy.com", choose("off", "")));
	}

	void check_local_ip_rules()
	{
		using namespace local_ip_rules;
		using mode = choice::mode;
		constexpr auto npos = std::string::npos;
		std::printf("local IP rules ([Online] LocalIP)\n");

		// Addresses as in_addr holds them.
		CHECK(parse_address("192.168.1.20") == 0x1401a8c0u && parse_address("127.0.0.1") == 0x0100007fu);
		CHECK(parse_address("172.18.0.1") == inet_addr("172.18.0.1") && parse_address("100.64.0.10") == inet_addr("100.64.0.10"));
		CHECK(!parse_address("192.168.1") && !parse_address("host") && !parse_address("") && !parse_address("1.2.3.256"));
		CHECK(text(0x1401a8c0u) == "192.168.1.20" && text(inet_addr("100.64.0.10")) == "100.64.0.10" && text(0) == "0.0.0.0");

		// The setting: auto by default, first, or an address; anything else is auto and says why.
		CHECK(choose("").how == mode::automatic && choose("auto").how == mode::automatic && choose(" AUTO ; the default").how == mode::automatic);
		CHECK(choose("").problem.empty() && choose("auto").problem.empty() && choose("; first, commented out").how == mode::automatic);
		CHECK(choose("first").how == mode::first && choose("First ; Windows' order").how == mode::first && choose("first").problem.empty());
		auto chosen = choose(" 192.168.1.20 ; the LAN card");
		CHECK(chosen.how == mode::address && chosen.wanted == inet_addr("192.168.1.20") && chosen.problem.empty());
		chosen = choose("ethernet");
		CHECK(chosen.how == mode::automatic && chosen.problem.find("[Online] LocalIP=ethernet isn't auto, first or an IPv4 address") == 0);
		chosen = choose("192.168.1.256");
		CHECK(chosen.how == mode::automatic && !chosen.problem.empty());
		CHECK(setting_text(choose("")) == "auto" && setting_text(choose("FIRST")) == "first" && setting_text(choose("010.0.0.2")) == "10.0.0.2");

		// This PC's name: any of its spellings, any case, a trailing root dot allowed; nothing else.
		const std::vector<std::string> own = {"MYPC", "MYPC", "MYPC.lan"};
		CHECK(is_own_host("MYPC", own) && is_own_host("mypc", own) && is_own_host("MyPc.", own) && is_own_host("mypc.LAN", own));
		CHECK(!is_own_host("localhost", own) && !is_own_host("", own) && !is_own_host(".", own) && !is_own_host("MYPC2", own) && !is_own_host("mypc.lan.example", own));
		CHECK(!is_own_host("xmenlegpc.master.gamespy.com", own) && !is_own_host("MYPC", std::vector<std::string>{}) && !is_own_host("x", std::vector<std::string>{""}));

		// A development PC as Windows lists it: WSL's vEthernet, Docker, Tailscale, then the LAN card with the default route.
		const auto at = [](const char* dotted) { return *parse_address(dotted); };
		const std::vector<address> windows = {at("172.18.0.1"), at("172.17.0.1"), at("100.64.0.10"), at("192.168.1.20")};
		const auto arranged = [&](const decision& made)
		{
			auto list = windows;
			if (made.front) move_to_front(list.data(), *made.front);
			return list_text(list);
		};
		CHECK(list_text(windows) == "172.18.0.1, 172.17.0.1, 100.64.0.10, 192.168.1.20" && list_text(std::vector<address>{}) == "(none)");
		auto made = decide(choose("auto"), windows, at("192.168.1.20"));
		CHECK(made.front == 3u && arranged(made) == "192.168.1.20, 172.18.0.1, 172.17.0.1, 100.64.0.10");
		CHECK(made.why == "192.168.1.20 first (the address Windows reaches the internet from, [Online] LocalIP=auto)");
		made = decide(choose("first"), windows, at("192.168.1.20"));
		CHECK(!made.front && arranged(made) == list_text(windows) && made.why == "Windows' order ([Online] LocalIP=first)");
		made = decide(choose("100.64.0.10"), windows, at("192.168.1.20"));
		CHECK(made.front == 2u && arranged(made) == "100.64.0.10, 172.18.0.1, 172.17.0.1, 192.168.1.20");
		made = decide(choose("172.18.0.1"), windows, at("192.168.1.20")); // already first
		CHECK(made.front == 0u && arranged(made) == list_text(windows));
		// An address that isn't this PC's: auto, and the log line says so.
		made = decide(choose("10.9.9.9"), windows, at("192.168.1.20"));
		CHECK(made.front == 3u && made.why == "[Online] LocalIP=10.9.9.9 isn't one of this PC's addresses - auto: 192.168.1.20 first (the address Windows reaches the internet from)");
		made = decide(choose("10.9.9.9"), windows, std::nullopt);
		CHECK(!made.front && made.why.find("isn't one of this PC's addresses") != npos && made.why.find("no route to the internet") != npos);
		// No route to the internet, or one from an address the lookup doesn't list: Windows' order.
		made = decide(choose("auto"), windows, std::nullopt);
		CHECK(!made.front && made.why == "no route to the internet - Windows' order kept");
		made = decide(choose("auto"), windows, at("10.8.0.2"));
		CHECK(!made.front && made.why == "the internet route's address 10.8.0.2 isn't in the list - Windows' order kept");
		made = decide(choose("auto"), windows, at("172.18.0.1"));
		CHECK(made.front == 0u && arranged(made) == list_text(windows));
		// One address, none, the same one twice (the first one moves).
		CHECK(decide(choose("auto"), std::vector<address>{at("192.168.1.20")}, at("192.168.1.20")).front == 0u);
		CHECK(!decide(choose("auto"), std::vector<address>{}, at("192.168.1.20")).front && !decide(choose("10.0.0.1"), std::vector<address>{}, std::nullopt).front);
		CHECK(decide(choose("auto"), std::vector<address>{at("10.0.0.1"), at("192.168.1.20"), at("10.0.0.1"), at("192.168.1.20")}, at("192.168.1.20")).front == 1u);

		// On a hostent's list: the pointers move, the addresses stay where they are, the terminator stays last.
		in_addr stored[4]{};
		char* list[5] = {reinterpret_cast<char*>(&stored[0]), reinterpret_cast<char*>(&stored[1]), reinterpret_cast<char*>(&stored[2]), reinterpret_cast<char*>(&stored[3]), nullptr};
		move_to_front(list, 3);
		CHECK(list[0] == reinterpret_cast<char*>(&stored[3]) && list[1] == reinterpret_cast<char*>(&stored[0]) && list[2] == reinterpret_cast<char*>(&stored[1]) &&
		      list[3] == reinterpret_cast<char*>(&stored[2]) && list[4] == nullptr);
		move_to_front(list, 0);
		CHECK(list[0] == reinterpret_cast<char*>(&stored[3]) && list[3] == reinterpret_cast<char*>(&stored[2]));
	}

	// This PC's own name through the fix (xml2_test's gethostbyname is hooked like the game's; with no
	// xml2-fix.ini next to the test, LocalIP=auto), against Winsock's own answer and the route Windows reports.
	void check_local_ip()
	{
		using local_ip_rules::address;
		std::printf("local IP ([Online] LocalIP=auto, this PC's own name)\n");
		WSADATA wsa{};
		WSAStartup(MAKEWORD(2, 2), &wsa);
		using gethostbyname_t = hostent*(WSAAPI*)(const char*);
		const auto winsock = reinterpret_cast<gethostbyname_t>(GetProcAddress(GetModuleHandleW(L"ws2_32.dll"), "gethostbyname")); // not the hooked import
		const auto addresses = [](const hostent* found)
		{
			std::vector<address> list;
			if (found && found->h_addrtype == AF_INET)
			{
				for (auto** entry = found->h_addr_list; *entry; ++entry) list.push_back(reinterpret_cast<const in_addr*>(*entry)->s_addr);
			}
			return list;
		};
		char name[256]{};
		CHECK(gethostname(name, sizeof(name)) == 0);
		CHECK(winsock != nullptr);
		const auto windows = winsock ? addresses(winsock(name)) : std::vector<address>{};
		const auto fixed = addresses(gethostbyname(name)); // GameSpy's way (getlocalhost, 0x641fe0)
		const auto route = local_ip::default_route_address();
		std::printf("  info  %s: Windows lists %s; the fix answers %s; the internet route leaves from %s\n", name, local_ip_rules::list_text(windows).c_str(),
		            local_ip_rules::list_text(fixed).c_str(), route ? local_ip_rules::text(*route).c_str() : "(no route)");
		CHECK(!windows.empty());
		CHECK(std::is_permutation(fixed.begin(), fixed.end(), windows.begin(), windows.end()));
		auto expected = windows;
		if (route)
		{
			if (const auto found = std::ranges::find(expected, *route); found != expected.end()) std::rotate(expected.begin(), found, found + 1);
		}
		CHECK(fixed == expected);
		// The game's own way (0x615d30): "localhost", then the name that answer carries - Winsock's own buffer, passed back in.
		const hostent* localhost = gethostbyname("localhost");
		CHECK(localhost != nullptr && localhost->h_name != nullptr);
		if (localhost && localhost->h_name)
		{
			std::printf("  info  \"localhost\" answers with the name %s\n", localhost->h_name);
			CHECK(addresses(gethostbyname(localhost->h_name)) == expected);
		}
		CHECK(addresses(gethostbyname("localhost")) == std::vector<address>{inet_addr("127.0.0.1")}); // not this PC's name: untouched
		WSACleanup();

		const auto log = read_file(module_dir() / "xml2-fix.log");
		CHECK(log.find("online: LocalIP=auto - the address Windows reaches the internet from goes first when the game looks up this PC's name") != std::string::npos);
		const auto before = "online: this PC's addresses (" + std::string(name) + "): " + local_ip_rules::list_text(windows);
		const auto after = "online: LocalIP: " + local_ip_rules::decide(local_ip_rules::choice{}, windows, route).why + ": " + local_ip_rules::list_text(expected);
		std::printf("  info  %s\n", after.c_str());
		CHECK(log.find(before) != std::string::npos && log.find(before) == log.rfind(before)); // once, however often the game asks
		CHECK(log.find(after) != std::string::npos && log.find(after) == log.rfind(after));
	}

	void check_save_folder()
	{
		std::printf("[Game] SaveFolder names\n");
		using new_game::valid_save_folder;
		CHECK(valid_save_folder("X-Men Legends"));
		CHECK(valid_save_folder("X-Men Legends (port tests)"));
		CHECK(valid_save_folder(std::string(new_game::save_folder_max, 'a')));
		CHECK(!valid_save_folder(std::string(new_game::save_folder_max + 1, 'a')));
		CHECK(!valid_save_folder(""));
		CHECK(!valid_save_folder(".") && !valid_save_folder(".."));
		CHECK(!valid_save_folder(" lead") && !valid_save_folder("trail ") && !valid_save_folder("dot."));
		for (const char* bad : {"a\\b", "a/b", "c:", "a*", "a?", "a\"b", "a<b", "a>b", "a|b", "100%s", "tab\there", "caf\xe9", "del\x7f"})
		{
			CHECK(!valid_save_folder(bad));
		}
	}

	// The game's console word reader (0x55b670, __thiscall char* (const char** cursor), ret 4) on a
	// console block of the test's own: the next word, and the cursor moved past it.
	std::string run_word_reader(const std::uint8_t* function, test_block& console, const char*& cursor)
	{
		int word = 0;
		if (!run_thiscall_int(function, console.data(), static_cast<int>(reinterpret_cast<std::uintptr_t>(&cursor)), word) || !word)
		{
			return "(faulted)";
		}
		return std::string(reinterpret_cast<const char*>(static_cast<std::uintptr_t>(static_cast<unsigned>(word))));
	}

	// [Game] PostgameScript (postgame_rules.hpp): the value's rules, the guards on their own, then,
	// when a copy of XMen2.exe is at hand, every guard against it, the change applied to that copy
	// (exactly the four bytes of the push operand) and the game's own word reader on the line.
	// The conversation hooks' decisions (conversations_rules.hpp): the ini keys, the auto-advance over a
	// line's frames, the pending reply's wait, the cursor clamp, the patch bytes and the guards.
	void check_conversations_rules()
	{
		using namespace conversations_rules;
		std::printf("conversations ([Game] AutoAdvance, ReplyVoices, ReplyCursor)\n");

		// The ini: on unless 0; anything else is on with a note.
		std::string error;
		auto chosen = decide(std::nullopt, std::nullopt, std::nullopt, error);
		CHECK(chosen.auto_advance && chosen.reply_voices && chosen.reply_cursor && error.empty());
		chosen = decide("0", "0 ; the game's own", " 0", error);
		CHECK(!chosen.auto_advance && !chosen.reply_voices && !chosen.reply_cursor && error.empty());
		chosen = decide("1", "off", "yes", error);
		CHECK(chosen.auto_advance && !chosen.reply_voices && chosen.reply_cursor && error.empty());
		chosen = decide("maybe", "", "2", error);
		CHECK(chosen.auto_advance && chosen.reply_voices && chosen.reply_cursor && error == "AutoAdvance=maybe isn't 1 or 0 - taken as 1; ReplyCursor=2 isn't 1 or 0 - taken as 1");

		// The auto-advance over a line: unmarked lines and menus are never the fix's; a marked line with a
		// voice goes once the voice was heard playing and then not (plus a breath); without one, after
		// |timeDelay|; never inside the game's first-second lock-out; a pick is retried after a while.
		line_state s;
		frame_view v;
		v.line_id = 7;
		v.time_delay = 0.5f; // unmarked
		v.visible = 1;
		v.now = 10;
		v.accept_from = 1;
		CHECK(auto_advance_step(s, v) == verdict::none && s.line_id == 7 && s.shown_at == 10);
		v.time_delay = -3;
		v.visible = 2; // a menu
		CHECK(auto_advance_step(s, v) == verdict::none);
		v.visible = 1;
		v.menu_up = true;
		CHECK(auto_advance_step(s, v) == verdict::none);
		v.menu_up = false;
		v.now = 11;
		CHECK(auto_advance_step(s, v) == verdict::wait); // no voice: 1 s of 3 shown
		v.now = 12.9f;
		CHECK(auto_advance_step(s, v) == verdict::wait);
		v.now = 13;
		CHECK(auto_advance_step(s, v) == verdict::advance && s.advanced && s.advanced_at == 13);
		v.now = 13.5f;
		CHECK(auto_advance_step(s, v) == verdict::wait); // picked already: the next line comes, or a retry later
		v.now = 15.1f;
		CHECK(auto_advance_step(s, v) == verdict::advance);
		// A new line with a voice.
		v.line_id = 8;
		v.now = 20;
		v.voice = true;
		v.playing = true;
		CHECK(auto_advance_step(s, v) == verdict::wait && s.line_id == 8 && s.shown_at == 20 && s.seen_playing);
		v.now = 40; // long past |timeDelay|: the voice decides
		CHECK(auto_advance_step(s, v) == verdict::wait);
		v.playing = false;
		CHECK(auto_advance_step(s, v) == verdict::wait && s.ended_at == 40); // the breath
		v.now = 40.1f;
		CHECK(auto_advance_step(s, v) == verdict::wait);
		v.now = 40.3f;
		CHECK(auto_advance_step(s, v) == verdict::advance);
		// A voice that never plays (the sound system couldn't start it): the time.
		v.line_id = 9;
		v.now = 50;
		v.playing = false;
		CHECK(auto_advance_step(s, v) == verdict::wait && !s.seen_playing);
		v.now = 52.9f;
		CHECK(auto_advance_step(s, v) == verdict::wait);
		v.now = 53;
		CHECK(auto_advance_step(s, v) == verdict::advance);
		// The lock-out: the conversation's first second, whatever the line says.
		v.line_id = 10;
		v.time_delay = -0.1f;
		v.now = 60;
		v.accept_from = 61;
		CHECK(auto_advance_step(s, v) == verdict::wait);
		v.now = 61.01f;
		CHECK(auto_advance_step(s, v) == verdict::advance);
		// A line id seen again later is a new line (the state resets on any change).
		v.line_id = 7;
		v.now = 70;
		v.time_delay = -1;
		CHECK(auto_advance_step(s, v) == verdict::wait && s.shown_at == 70 && !s.advanced);
		// The same conversation run again: its first line has the same id (ids are per file), but the accept
		// lock-out time is new - the state resets, so the line waits its |timeDelay| again instead of going at once.
		v.now = 71;
		CHECK(auto_advance_step(s, v) == verdict::advance && s.advanced);
		v.now = 80;
		v.accept_from = 81; // a new start at 80
		CHECK(auto_advance_step(s, v) == verdict::wait && s.shown_at == 80 && !s.advanced && s.accept_from == 81);
		v.now = 81.5f;
		CHECK(auto_advance_step(s, v) == verdict::advance); // 1.5 s shown, |timeDelay| = 1
		// ... and a voiced line heard playing in the first run isn't taken as ended in the second.
		v.line_id = 8;
		v.now = 92;
		v.accept_from = 91;
		v.time_delay = -5;
		v.voice = true;
		v.playing = true;
		CHECK(auto_advance_step(s, v) == verdict::wait && s.seen_playing);
		v.now = 100;
		v.accept_from = 101; // run again: the voice not yet started (the handle set, not playing)
		v.playing = false;
		CHECK(auto_advance_step(s, v) == verdict::wait && !s.seen_playing && s.shown_at == 100);
		v.now = 104.9f;
		CHECK(auto_advance_step(s, v) == verdict::wait);
		v.now = 105;
		CHECK(auto_advance_step(s, v) == verdict::advance);

		// The pending reply: wait while its voice plays and accept isn't pressed.
		CHECK(pending_wait(true, true, false) && !pending_wait(true, true, true) && !pending_wait(true, false, false) && !pending_wait(false, false, false) && !pending_wait(false, true, false));
		CHECK(clamped_cursor(3) == 2 && clamped_cursor(1) == 0 && clamped_cursor(0) == 0);

		// The patch bytes: a call rel32 and a nop over the 6-byte call, jumps over the 5- and 7-byte sites.
		const auto call = accept_call_bytes(0x10001000);
		CHECK(call[0] == 0xe8 && operand_at(call.data() + 1, 4) == 0x10001000u - (accept_call + 5) && call[5] == 0x90);
		const auto jump = pending_jump_bytes(0x10002000);
		CHECK(jump[0] == 0xe9 && operand_at(jump.data() + 1, 4) == 0x10002000u - (pending_call + 5));
		const auto cursor = cursor_jump_bytes(0x10003000);
		CHECK(cursor[0] == 0xe9 && operand_at(cursor.data() + 1, 4) == 0x10003000u - (cursor_store + 5) && cursor[5] == 0x90 && cursor[6] == 0x90);
		CHECK(pending_continue == pending_call + 5 && cursor_continue == cursor_store + 7 && accept_call + 6 == 0x45d344);

		// The guards on their own: well-formed, one per address; each site inside one.
		bool guards_ok = true;
		std::set<DWORD> addresses;
		for (const auto& g : guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
		for (const auto& g : menu_guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
		CHECK(guards_ok);
		const auto covered = [&](const DWORD from, const DWORD size)
		{
			for (const auto& g : guards)
			{
				if (g.va <= from && from + size <= g.va + limits_rules::hex_size(g.hex)) return true;
			}
			return false;
		};
		CHECK(covered(accept_call, 6) && covered(pending_call, 5) && covered(cursor_store, 7) && covered(pending_continue, 1) && covered(pending_wait_at, 1) && covered(cursor_continue, 1));

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the conversation hooks' bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const guard* mismatch = first_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr);
		CHECK(accept_site_is_retail(image) && pending_site_is_retail(image) && cursor_site_is_retail(image));
		// The sites' own bytes: the call through vt+0x138, the call to the audio getter, the store of di.
		CHECK(operand_at(image + (pending_call + 1 - limits_rules::image_base), 4) == audio_getter - (pending_call + 5));
		CHECK(operand_at(image + (0x45d33e + 2 - limits_rules::image_base), 4) == menus_accept_slot);
		CHECK(operand_at(image + (cursor_store + 3 - limits_rules::image_base), 4) == cs_cursor);
		// What the hooks read: the vtable slots the guards name.
		CHECK(operand_at(image + (audio_vtable + audio_playing_slot - limits_rules::image_base), 4) == 0x5909a0 && operand_at(image + (audio_vtable + audio_stop_slot - limits_rules::image_base), 4) == 0x590780);
		CHECK(operand_at(image + (cs_vtable + cs_pick_slot - limits_rules::image_base), 4) == 0x45d5d0 && operand_at(image + (cs_vtable + 0xc - limits_rules::image_base), 4) == 0x45d1a0);
		CHECK(operand_at(image + (game_vtable + game_time_slot - limits_rules::image_base), 4) == 0x469740 && operand_at(image + (menus_vtable + menus_accept_slot - limits_rules::image_base), 4) == 0x5d4950 &&
		      operand_at(image + (menus_vtable + menus_name_slot - limits_rules::image_base), 4) == 0x5d8640);
		CHECK(operand_at(image + (none_handle_va - limits_rules::image_base), 4) == none_handle);
		// The patches applied to the copy: the written bytes, nothing else on the page changed.
		std::vector<std::uint8_t> copy(image, image + image_size);
		const auto fixed = accept_call_bytes(0x10001000);
		std::memcpy(copy.data() + (accept_call - limits_rules::image_base), fixed.data(), fixed.size());
		CHECK(!accept_site_is_retail(copy.data()) && std::memcmp(copy.data() + (accept_call + 6 - limits_rules::image_base), image + (accept_call + 6 - limits_rules::image_base), 64) == 0);
		UnmapViewOfFile(image);
	}

	void check_postgame_rules()
	{
		using namespace postgame_rules;
		std::printf("[Game] PostgameScript (after the end credits)\n");

		CHECK(parse_script("x1/menus/postgame").name == "x1/menus/postgame" && parse_script("x1/menus/postgame").error.empty());
		CHECK(parse_script("  x1/menus/postgame \t; XML1's r505, then the main menu").name == "x1/menus/postgame");
		CHECK(parse_script("Menus/Post_Game2").name == "Menus/Post_Game2" && parse_script("postgame").name == "postgame");
		const auto unset = parse_script("   ; nothing");
		CHECK(unset.name.empty() && unset.error.empty() && parse_script("").name.empty() && parse_script("").error.empty());
		CHECK(parse_script(std::string(name_max, 'a')).name.size() == name_max && name_max == 117);
		CHECK(command_line(std::string(name_max, 'a')).size() == console_max);
		CHECK(parse_script(std::string(name_max + 1, 'a')).error == "is 118 characters; the game's console keeps 127, 117 after \"runscript \"");
		for (const char* bad : {"x1/menus/post game", "x1/menus/postgame.py", "x1\\menus\\postgame", "x1/menus/post-game", "loadZone('a','')", "a(b)", "\"x1/postgame\"",
		                        "100%s", "a\tb", "caf\xe9", "del\x7f", "/x1/postgame", "x1/postgame/", "x1//postgame", "/", "scripts/x1/postgame", "Scripts/x1/postgame",
		                        "x1/myscripts/postgame"})
		{
			const auto refused = parse_script(bad);
			if (refused.error.empty()) std::printf("  info  \"%s\" was taken\n", bad);
			CHECK(refused.name.empty() && !refused.error.empty());
		}
		CHECK(parse_script("x1/postgame.py").error.find("without its .py") != std::string::npos);
		CHECK(parse_script("x1\\postgame").error.find("separate folders with /") != std::string::npos);
		CHECK(parse_script("x1/post game").error.find("space or byte 0x20") != std::string::npos);
		CHECK(parse_script("Scripts/x1/postgame").error.find("under the Scripts folder") != std::string::npos);
		CHECK(command_line("x1/menus/postgame") == "runscript x1/menus/postgame");
		CHECK(script_file("x1/menus/postgame") == "Scripts\\x1\\menus\\postgame.py");

		// The guards on their own: well-formed, one per address, none overlapping; the operand inside one.
		bool guards_ok = true;
		std::set<DWORD> addresses;
		for (const auto& g : guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
		CHECK(guards_ok);
		bool apart = true, covered = false;
		for (std::size_t i = 0; i < guards.size(); ++i)
		{
			const DWORD end = guards[i].va + static_cast<DWORD>(limits_rules::hex_size(guards[i].hex));
			covered |= guards[i].va <= load_push && load_operand + 4 <= end;
			for (std::size_t j = 0; j < guards.size(); ++j)
			{
				if (i != j && guards[j].va >= guards[i].va && guards[j].va < end) apart = false;
			}
		}
		CHECK(apart && covered);

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the end credits' bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - image_base); };
		const auto text_at = [&](const DWORD va) { return std::string_view(reinterpret_cast<const char*>(at(va))); };

		// Every guard: the retail bytes; the two pushes and their lines.
		const guard* mismatch = first_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr && operand_is_retail(image));
		CHECK(at(save_push)[0] == 0x68 && operand_at(at(save_push + 1), 4) == retail_save_line && operand_at(at(load_operand), 4) == retail_load_line);
		CHECK(text_at(retail_load_line) == retail_load_text && text_at(retail_save_line) == retail_save_text);

		// The change on a copy: the four bytes of the operand, nothing else.
		std::vector<std::uint8_t> before(image, image + image_size);
		constexpr std::uint32_t dll_line = 0x10203040;
		apply(image, dll_line);
		std::vector<DWORD> changed;
		for (DWORD i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i]) changed.push_back(image_base + i);
		}
		CHECK((changed == std::vector<DWORD>{load_operand, load_operand + 1, load_operand + 2, load_operand + 3}));
		CHECK(operand_at(at(load_operand), 4) == dll_line && at(load_push)[0] == 0x68 && operand_at(at(save_push + 1), 4) == retail_save_line);
		// Patched (or any other build): the state 2 guard no longer matches, so nothing would be written.
		mismatch = first_mismatch(image);
		CHECK(mismatch && mismatch->va == 0x5b1c81 && !operand_is_retail(image));
		std::memcpy(image, before.data(), image_size);

		// What the console makes of the line: the game's own word reader gives runscript the whole
		// name as one word (a space would have cut it: why the rules refuse one).
		test_block console(0x63c + 0x201);
		const auto reader = at(0x55b670);
		const std::string line = command_line("x1/menus/postgame");
		const char* cursor = line.c_str();
		CHECK(run_word_reader(reader, console, cursor) == "runscript");
		CHECK(run_word_reader(reader, console, cursor) == "x1/menus/postgame" && *cursor == '\0');
		const std::string longest = command_line(std::string(name_max, 'z'));
		cursor = longest.c_str();
		run_word_reader(reader, console, cursor);
		CHECK(run_word_reader(reader, console, cursor) == std::string(name_max, 'z'));
		const std::string spaced = "runscript x1/menus/post game";
		cursor = spaced.c_str();
		run_word_reader(reader, console, cursor);
		CHECK(run_word_reader(reader, console, cursor) == "x1/menus/post");
		CHECK(console.slack_untouched());
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// [Game] NewGamePlus=0 (new_game_plus_rules.hpp): the guards on their own, then, when a copy of
	// XMen2.exe is at hand, every guard against it, the retail branch and where it goes, and the change
	// applied to that copy (exactly the two opcode bytes of the je; the jump lands where the je did).
	void check_new_game_plus_rules()
	{
		using namespace new_game_plus_rules;
		std::printf("[Game] NewGamePlus (no saved-statistics choice at New Game)\n");

		bool guards_ok = true;
		std::set<DWORD> addresses;
		for (const auto& g : guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
		CHECK(guards_ok);
		bool apart = true, covered = false, path_covered = false;
		for (std::size_t i = 0; i < guards.size(); ++i)
		{
			const DWORD end = guards[i].va + static_cast<DWORD>(limits_rules::hex_size(guards[i].hex));
			covered |= guards[i].va <= offer_branch && offer_branch + retail_branch.size() <= end;
			path_covered |= guards[i].va == start_path;
			for (std::size_t j = 0; j < guards.size(); ++j)
			{
				if (i != j && guards[j].va >= guards[i].va && guards[j].va < end) apart = false;
			}
		}
		CHECK(apart && covered && path_covered);
		// The two encodings land in the same place: je rel32 from 0x4a099e, nop + jmp rel32 from 0x4a099f.
		const std::array<std::uint8_t, 6> je = retail_branch, jmp = patched_branch;
		CHECK(branch_target(je.data(), offer_branch) == start_path);
		CHECK(branch_target(jmp.data(), offer_branch) == start_path);

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check setDifficultyLevel's bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - image_base); };

		const guard* mismatch = first_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr);
		CHECK(std::memcmp(at(offer_branch), retail_branch.data(), retail_branch.size()) == 0 && branch_target(at(offer_branch), offer_branch) == start_path);
		// The not-unlocked path queues the retail line: push 0x6872a0 "runscript startFirstMission()".
		CHECK(at(start_path + 0x15)[0] == 0x68 && operand_at(at(start_path + 0x16), 4) == 0x6872a0);
		CHECK(std::string_view(reinterpret_cast<const char*>(at(0x6872a0))) == "runscript startFirstMission()");

		std::vector<std::uint8_t> before(image, image + image_size);
		apply(image);
		std::vector<DWORD> changed;
		for (DWORD i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i]) changed.push_back(image_base + i);
		}
		CHECK((changed == std::vector<DWORD>{offer_branch, offer_branch + 1}));
		CHECK(at(offer_branch)[0] == 0x90 && at(offer_branch + 1)[0] == 0xe9 && branch_target(at(offer_branch), offer_branch) == start_path);
		// Patched (or any other build): setDifficultyLevel's guard no longer matches, so nothing would be written again.
		mismatch = first_mismatch(image);
		CHECK(mismatch && mismatch->va == 0x4a0930);
		std::memcpy(image, before.data(), image_size);
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// [Game] ReviewStats=0 (review_menu_rules.hpp): the tables on their own (every site inside a guard, with
	// its instruction and retail byte there, inside the write span), the tab change as the exe does it with
	// either set of bytes, then, when a copy of XMen2.exe is at hand, every guard against it, the tab table's
	// names, and the change applied to that copy (exactly the five bytes).
	void check_review_menu_rules()
	{
		using namespace review_menu_rules;
		std::printf("[Game] ReviewStats (the Review menu without its Stats tab)\n");

		bool guards_ok = true;
		std::set<DWORD> addresses;
		for (const auto& g : guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
		CHECK(guards_ok);
		bool apart = true;
		for (std::size_t i = 0; i < guards.size(); ++i)
		{
			const DWORD end = guards[i].va + static_cast<DWORD>(limits_rules::hex_size(guards[i].hex));
			for (std::size_t j = 0; j < guards.size(); ++j)
			{
				if (i != j && guards[j].va >= guards[i].va && guards[j].va < end) apart = false;
			}
		}
		CHECK(apart);
		// Each site: its instruction (cmp r32, imm8 = 83 /7 ib; mov esi, imm32 = be id) and retail byte in a guard.
		bool sites_ok = true;
		std::set<DWORD> written;
		for (const auto& s : sites)
		{
			sites_ok &= s.va >= span_begin && s.va < span_end && written.insert(s.va).second && s.retail != s.without_stats && s.what && *s.what;
			bool covered = false;
			for (const auto& g : guards)
			{
				const DWORD end = g.va + static_cast<DWORD>(limits_rules::hex_size(g.hex));
				if (g.va <= s.instruction && s.va < end)
				{
					const auto at = [&](const DWORD va) { return limits_rules::hex_byte(g.hex, va - g.va); };
					const bool compare = at(s.instruction) == 0x83 && (at(s.instruction + 1) & 0xf8) == 0xf8 && s.va == s.instruction + 2;
					const bool move = at(s.instruction) == 0xbe && s.va == s.instruction + 1 && s.va + 4 <= end && at(s.va + 1) == 0 && at(s.va + 2) == 0 && at(s.va + 3) == 0;
					covered = (compare || move) && at(s.va) == s.retail;
				}
			}
			if (!covered) std::printf("  info  site 0x%08lX isn't covered by a guard with its retail instruction\n", s.va);
			sites_ok &= covered;
		}
		CHECK(sites_ok && written.size() == 5);
		CHECK(writes().size() == sites.size() && writes().front().size == 1 && writes().front().value == 4);

		// The tab change (0x5d17d0) as the exe does it: tab + step; >= wrap -> 0; <= -1 -> last.
		const auto change = [](const int tab, const int step, const int wrap, const int last)
		{
			const int next = tab + step;
			return next >= wrap ? 0 : next <= -1 ? last : next;
		};
		const auto go_round = [&](const int step, const int wrap, const int last)
		{
			std::vector<int> seen{0};
			for (int tab = change(0, step, wrap, last); tab != 0 && seen.size() < 10; tab = change(tab, step, wrap, last)) seen.push_back(tab);
			return seen;
		};
		CHECK((go_round(1, 5, 4) == std::vector<int>{0, 1, 2, 3, 4} && go_round(-1, 5, 4) == std::vector<int>{0, 4, 3, 2, 1}));
		CHECK((go_round(1, 4, 3) == std::vector<int>{0, 1, 2, 3} && go_round(-1, 4, 3) == std::vector<int>{0, 3, 2, 1}));
		CHECK(retail_tabs == 5 && tabs_without_stats == 4 && sites[0].without_stats == tabs_without_stats && sites[1].without_stats == tabs_without_stats - 1);

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check REVIEW_PATHS_MENU's bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - image_base); };

		const guard* mismatch = first_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr);
		// The tab table: option01_text .. option05_text, the fifth being Stats; the pushes of the mouse the same five.
		bool tabs_ok = true;
		for (int i = 0; i < retail_tabs; ++i)
		{
			const DWORD name = operand_at(at(tab_names + 4 * i), 4);
			tabs_ok &= std::string_view(reinterpret_cast<const char*>(at(name))) == "option0" + std::to_string(i + 1) + "_text";
			tabs_ok &= at(0x5d04ef + 0x10 * i + (i ? 2 : 0))[0] == 0x68 && operand_at(at(0x5d04ef + 0x10 * i + (i ? 2 : 0) + 1), 4) == name;
		}
		CHECK(tabs_ok);
		// The list: Stats (0x5d0c20) when the tab is 4.
		CHECK(at(0x5d1780)[0] == 0x83 && at(0x5d1780)[6] == 4 && operand_at(at(0x5d178d), 4) == static_cast<std::uint32_t>(0x5d0c20 - 0x5d1791));
		for (const auto& s : sites)
		{
			CHECK(*at(s.va) == s.retail);
		}

		std::vector<std::uint8_t> before(image, image + image_size);
		apply(image);
		std::vector<DWORD> changed;
		for (DWORD i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i]) changed.push_back(image_base + i);
		}
		CHECK((changed == std::vector<DWORD>{0x5d05b3, 0x5d17f8, 0x5d180a, 0x5d1817, 0x5d1c89}));
		// The tab change's own bytes, read back: four tabs each way.
		CHECK((go_round(1, *at(0x5d180a), *at(0x5d1817)) == std::vector<int>{0, 1, 2, 3} && go_round(-1, *at(0x5d180a), *at(0x5d1817)) == std::vector<int>{0, 3, 2, 1}));
		// Patched (or any other build): the mouse handler's guard no longer matches, so nothing would be written again.
		mismatch = first_mismatch(image);
		CHECK(mismatch && mismatch->va == 0x5d04d0);
		std::memcpy(image, before.data(), image_size);
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// Menus, popups and conversations at 60 fps (frame_rate_rules.hpp): the rate rules on their own, then,
	// when a copy of XMen2.exe is at hand, every guard against it and the game's own four functions (menu up
	// 0x5d8870, movie 0x5d8420, popup up 0x5e9e30, conversation 0x458010) run on blocks of the test's in
	// every state that matters, each answer compared with the fix's reading of the same block.
	void check_menu_screens()
	{
		using namespace frame_rate_rules;
		std::printf("frame rate: menus, popups and conversations at 60 fps\n");

		// The rate: 60 on such a screen when FrameRate is above it or unlimited, FrameRate when lower.
		CHECK(paced_fps(180, true) == 60 && paced_fps(180, false) == 180 && paced_fps(0, true) == 60 && paced_fps(0, false) == 0);
		CHECK(paced_fps(144, true) == 60 && paced_fps(61, true) == 60 && paced_fps(60, true) == 60 && paced_fps(30, true) == 30 && paced_fps(30, false) == 30);
		CHECK(menus_differ(180) && menus_differ(0) && menus_differ(61) && !menus_differ(60) && !menus_differ(30));
		// Which screens: a menu, a popup or a conversation - not a movie, not the loading screen.
		CHECK(!at_menu_rate({}) && at_menu_rate({true}) && at_menu_rate({false, true}) && at_menu_rate({false, false, true}));
		CHECK(!at_menu_rate({true, false, false, true}) && !at_menu_rate({true, false, false, false, true}) && !at_menu_rate({false, true, false, false, true}));
		CHECK(at_menu_rate({true, true, true}) && !at_menu_rate({false, false, false, true}));
		CHECK(describe_screen({}) == "play" && describe_screen({true}) == "a menu" && describe_screen({true, true}) == "a popup" && describe_screen({false, false, true}) == "a conversation");
		CHECK(describe_screen({true, false, false, true}) == "a movie" && describe_screen({true, false, false, false, true}) == "the loading screen");
		std::set<DWORD> addresses;
		bool guards_ok = true;
		for (const auto& g : screen_guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
		CHECK(guards_ok);

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the menu state's bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - image_base); };

		const guard* mismatch = first_screen_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr);
		// The layout the fix reads is the one the functions' own bytes name (their displacements).
		CHECK(operand_at(at(0x5d8870 + 2), 4) == menu_stack_count && operand_at(at(0x5d887a + 2), 4) == menu_stack_first);
		CHECK(operand_at(at(0x5d8884 + 2), 4) == menu_current && operand_at(at(0x5d888e + 2), 4) == menu_pending && operand_at(at(0x5d8420 + 2), 4) == menu_flags);
		CHECK(operand_at(at(0x5e9e30 + 2), 4) == popup_current && operand_at(at(0x5e9e3f + 2), 4) == popup_stride && operand_at(at(0x5e9e49 + 2), 4) == popup_active);
		CHECK(operand_at(at(0x458010 + 2), 4) == conversation_flags && operand_at(at(0x5d8920 + 1), 4) == menu_manager_cell);
		CHECK(operand_at(at(0x5eb300 + 1), 4) == popups_cell && operand_at(at(0x4583f0 + 1), 4) == conversations_cell && operand_at(at(0x5b81e3 + 2), 4) == loading_menu_vtable);
		CHECK(operand_at(at(0x5d88c6 + 1), 4) == menu_manager_size && operand_at(at(0x5eb2ac + 1), 4) == popups_size && operand_at(at(0x45839c + 1), 4) == conversations_size);

		// The game's answer (al) for a block, and the fix's for the same block.
		const auto game_says = [&](const DWORD function, std::uint8_t* object)
		{
			void* result = nullptr;
			const bool ran = run_thiscall(at(function), object, result);
			return ran ? static_cast<int>(reinterpret_cast<std::uintptr_t>(result) & 0xff) : -1;
		};

		// The menu manager: stack count, its first entry, the current menu, a pending name, the movie bit.
		test_block manager(menu_manager_size);
		std::uint8_t* m = manager.data();
		const auto put = [](std::uint8_t* object, const DWORD offset, const std::uint32_t value) { std::memcpy(object + offset, &value, sizeof(value)); };
		struct menu_case
		{
			std::int32_t count;
			std::uint32_t first, current;
			const char* pending;
			std::uint8_t flags;
		};
		const std::array<menu_case, 12> menu_cases{{
			{0, 0, 0, "", 0},                    // at the start: nothing
			{2, 0x1234, 0, "", 0},               // in play: the manager's two processes, no menu
			{2, 0x1234, 0x5678, "", 0},          // a menu
			{2, 0x1234, 0, "pause", 0},          // one about to open
			{0, 0x1234, 0x5678, "main", 0},      // no stack: never
			{-3, 0x1234, 0x5678, "", 0},         // a negative count
			{2, 0, 0x5678, "main", 0},           // no first entry
			{1, 0x1234, 0x5678, "", 0x20},       // a movie
			{2, 0x1234, 0, "", 0x20},            // the movie bit alone
			{2, 0x1234, 0x5678, "", 0xdf},       // every other flag
			{2, 0x1234, 0x5678, "", 0xff},
			{7, 0xffffffff, 0, "x", 0x40},
		}};
		bool menus_ok = true;
		for (const auto& c : menu_cases)
		{
			std::fill_n(m, menu_manager_size, std::uint8_t{0});
			put(m, menu_stack_count, static_cast<std::uint32_t>(c.count));
			put(m, menu_stack_first, c.first);
			put(m, menu_current, c.current);
			std::memcpy(m + menu_pending, c.pending, std::strlen(c.pending) + 1);
			m[menu_flags] = c.flags;
			const int up = game_says(0x5d8870, m), movie = game_says(0x5d8420, m);
			menus_ok &= up == (menu_up(m) ? 1 : 0) && movie == (movie_playing(m) ? 1 : 0);
		}
		CHECK(menus_ok && manager.slack_untouched());
		// The loading screen: the current menu's vtable.
		std::array<std::uint32_t, 4> loading_menu{loading_menu_vtable, 0, 0, 0}, other_menu{0x69f134, 0, 0, 0}; // CMenuLoading, CMenuMain
		const auto read_vtable = [](const std::uint32_t object) { return *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(object)); };
		std::fill_n(m, menu_manager_size, std::uint8_t{0});
		put(m, menu_stack_count, 2);
		put(m, menu_stack_first, 0x1234);
		CHECK(!loading_screen(m, read_vtable));
		put(m, menu_current, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(loading_menu.data())));
		CHECK(loading_screen(m, read_vtable) && menu_up(m));
		put(m, menu_current, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(other_menu.data())));
		CHECK(!loading_screen(m, read_vtable) && menu_up(m));

		// The popups: the index (in range and out) and each popup's shown bit.
		test_block popups(popups_size);
		std::uint8_t* p = popups.data();
		bool popups_ok = true;
		int popups_up = 0;
		for (int index = -2; index <= 4; ++index)
		{
			for (int shown = 0; shown < 8; ++shown)
			{
				std::fill_n(p, popups_size, std::uint8_t{0});
				put(p, popup_current, static_cast<std::uint32_t>(index));
				for (int i = 0; i < popup_count; ++i)
				{
					p[popup_first + static_cast<DWORD>(i) * popup_stride + popup_active] = static_cast<std::uint8_t>((shown >> i) & 1 ? 0xf1 : 0xf0);
				}
				const int up = game_says(0x5e9e30, p);
				popups_ok &= up == (popup_up(p) ? 1 : 0);
				popups_up += up == 1;
			}
		}
		CHECK(popups_ok && popups_up == 28 && popups.slack_untouched()); // shown in half the patterns: the index's popup, popup 0's when out of range

		// The conversations: every value of the flags byte.
		test_block conversations(conversations_size);
		std::uint8_t* c = conversations.data();
		bool conversations_ok = true;
		for (int flags = 0; flags < 256; ++flags)
		{
			c[conversation_flags] = static_cast<std::uint8_t>(flags);
			conversations_ok &= game_says(0x458010, c) == (conversation_up(c) ? 1 : 0);
		}
		CHECK(conversations_ok && conversations.slack_untouched());
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// [Game] MainMenuItems (main_menu_rules.hpp): the list's rules, the tables and guards on their own, then,
	// when a copy of XMen2.exe is at hand, every guard against it, the push table complete for MAIN_MENU's code
	// (every reference to the nine names in 0x5c9260..0x5c99ee is a site, and nothing else there), and the
	// change applied to that copy: exactly the operands of the slots that change, nothing else - not the cells
	// the Danger Room and Play Online compare with, not the item parser's or the other menu's pushes.
	void check_main_menu_rules()
	{
		using namespace main_menu_rules;
		std::printf("[Game] MainMenuItems (the main menu's item names)\n");

		const std::string xml1 = "button1,button2,button3,button4,button5,button6,button7";
		const auto port = parse_items(xml1);
		CHECK(port.set && port.error.empty());
		bool names_ok = true;
		for (std::size_t i = 0; i < slot_count; ++i)
		{
			names_ok &= i < 7 ? port.names[i] == "button" + std::to_string(i + 1) && changes(port, i) : port.names[i].empty() && !changes(port, i);
		}
		CHECK(names_ok && effective(port, quit_slot) == "button7" && effective(port, 7) == "debug" && effective(port, 8) == "debug_focus" && !eight_items(port));
		// The port's menu with Play Online: an eighth name is a seventh mouse slot, the ninth stays debug_focus.
		const auto port8 = parse_items(xml1 + ",button8   ; Play Online");
		CHECK(port8.set && port8.error.empty() && port8.names[7] == "button8" && effective(port8, 7) == "button8" && effective(port8, 8) == "debug_focus" && eight_items(port8));
		CHECK(!eight_items(parse_items("a,b,c,d,e,f,g,debug")) && !eight_items(parse_items("a,b,c,d,e,f,g,,i")) && eight_items(parse_items(",,,,,,,button8")));
		// The port's own list (tools/harness.py): Play Online is its button7, Quit its button8 - Quit in the seventh
		// slot, Play Online in the eighth.
		const auto port_online = parse_items("button1,button2,button3,button4,button5,button6,button8,button7");
		CHECK(port_online.set && port_online.error.empty() && effective(port_online, quit_slot) == "button8" && effective(port_online, 7) == "button7" &&
		      effective(port_online, 8) == "debug_focus" && eight_items(port_online));
		const auto commented = parse_items("  button1 , button2,button3 ;  XML1's buttons, not a name");
		CHECK(commented.set && commented.error.empty() && commented.names[1] == "button2" && commented.names[2] == "button3" && commented.names[3].empty());
		const auto unset = parse_items("   ; nothing");
		CHECK(!unset.set && unset.error.empty() && !parse_items("").set && parse_items("").error.empty());
		const auto gaps = parse_items(",,button3,,,,quit_item");
		CHECK(gaps.set && gaps.error.empty() && gaps.names[0].empty() && gaps.names[2] == "button3" && gaps.names[6] == "quit_item" && !changes(gaps, 0) && changes(gaps, 6));
		const auto own = parse_items("label_option04,label_option05");
		CHECK(own.set && own.error.empty() && !changes(own, 0) && !changes(own, 1));
		CHECK(parse_items("a,b,c,d,e,f,g,h,i").error.empty() && parse_items(std::string(name_max, 'a')).error.empty());
		for (const char* bad : {"a,b,c,d,e,f,g,h,i,j", "button 1", "button-1", "button1.igb", "\"button1\"", "caf\xe9", "del\x7f", "button1\tbutton2", "a/b",
		                        "button1,button1", "Button1,button1", "a,b,c,d,e,f,debug", "debug_text", ",,,,,,,debug_text", "label_option05,label_option05"})
		{
			const auto refused = parse_items(bad);
			if (refused.error.empty()) std::printf("  info  \"%s\" was taken\n", bad);
			CHECK(!refused.set && !refused.error.empty());
		}
		CHECK(!parse_items(std::string(name_max + 1, 'a')).error.empty());
		CHECK(parse_items("a,b,c,d,e,f,g,h,i,j").error.find("more than 9 names") != std::string::npos);
		CHECK(parse_items("button1,Button1").error.find("two slots (label_option04 and label_option05)") != std::string::npos);
		CHECK(parse_items("a,b,c,d,e,f,debug").error.find("\"debug\" to two slots (debug_text and debug)") != std::string::npos);

		// The tables on their own: every site's slot exists, its push inside MAIN_MENU's code and the write span,
		// inside a guard, with its slot's retail string as the guard's operand; the guards well-formed, apart.
		bool sites_ok = true;
		std::set<DWORD> pushes;
		std::array<int, slot_count> per_slot{};
		for (const auto& s : sites)
		{
			sites_ok &= s.slot < slot_count && s.push >= code_begin && s.push + 5 <= code_end && s.push + 1 >= span_begin && s.push + 5 <= span_end && pushes.insert(s.push).second;
			++per_slot[s.slot];
			bool covered = false;
			for (const auto& g : guards)
			{
				const DWORD end = g.va + static_cast<DWORD>(limits_rules::hex_size(g.hex));
				if (g.va <= s.push && s.push + 5 <= end)
				{
					const std::size_t offset = s.push - g.va;
					covered = limits_rules::hex_byte(g.hex, offset) == push_imm32 && operand_in(g.hex, offset + 1, 4) == slots[s.slot].retail_va;
				}
			}
			if (!covered) std::printf("  info  site 0x%08lX isn't covered by a guard with its retail push\n", s.push);
			sites_ok &= covered;
		}
		CHECK(sites_ok && (per_slot == std::array<int, slot_count>{1, 1, 2, 2, 1, 2, 8, 1, 1}));
		CHECK((span_end - 1) / 0x1000 == span_begin / 0x1000); // one page
		bool guards_ok = true;
		std::set<DWORD> addresses;
		for (const auto& g : guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
		bool apart = true;
		for (std::size_t i = 0; i < guards.size(); ++i)
		{
			const DWORD end = guards[i].va + static_cast<DWORD>(limits_rules::hex_size(guards[i].hex));
			for (std::size_t j = 0; j < guards.size(); ++j)
			{
				if (i != j && guards[j].va >= guards[i].va && guards[j].va < end) apart = false;
			}
		}
		CHECK(guards_ok && apart);
		// The port's list: 17 writes, one per site of slots 0..6; the slots 7 and 8 left as they are.
		std::array<std::uint32_t, slot_count> pointers{};
		for (std::size_t i = 0; i < slot_count; ++i) pointers[i] = 0x10203000 + static_cast<std::uint32_t>(i) * 0x40;
		const auto writes = writes_for(port, pointers);
		bool writes_ok = writes.size() == 17;
		for (const auto& w : writes)
		{
			const auto site = std::find_if(sites.begin(), sites.end(), [&](const auto& s) { return s.push + 1 == w.va; });
			writes_ok &= site != sites.end() && w.size == 4 && site->slot < 7 && w.value == pointers[site->slot];
		}
		CHECK(writes_ok && writes_for(own, pointers).empty() && writes_for(parse_items("a,b,c,d,e,f,g,h,i"), pointers).size() == sites.size() + 1);
		// The eight-item list: the 17, slot 7's one site, and the clamp's imm8 (6 -> 8) at 0x5c944d.
		const auto writes8 = writes_for(port8, pointers);
		bool writes8_ok = writes8.size() == 19 && clamp_operand == 0x5c944d && clamp_operand >= span_begin && clamp_operand < span_end;
		std::size_t clamp_writes = 0;
		for (const auto& w : writes8)
		{
			if (w.va == clamp_operand)
			{
				++clamp_writes;
				writes8_ok &= w.size == 1 && w.value == eight_item_clamp && eight_item_clamp == 8;
				continue;
			}
			const auto site = std::find_if(sites.begin(), sites.end(), [&](const auto& s) { return s.push + 1 == w.va; });
			writes8_ok &= site != sites.end() && w.size == 4 && site->slot < 8 && w.value == pointers[site->slot];
		}
		CHECK(writes8_ok && clamp_writes == 1);
		// The clamp's instruction inside the mouse handler's guard, with its retail imm8.
		bool clamp_covered = false;
		for (const auto& g : guards)
		{
			const DWORD end = g.va + static_cast<DWORD>(limits_rules::hex_size(g.hex));
			if (g.va <= clamp_compare && clamp_compare + 8 <= end)
			{
				const std::size_t offset = clamp_compare - g.va;
				clamp_covered = limits_rules::hex_byte(g.hex, offset) == 0x83 && limits_rules::hex_byte(g.hex, offset + 1) == 0xff &&
				                limits_rules::hex_byte(g.hex, offset + 2) == retail_clamp && limits_rules::hex_byte(g.hex, offset + 3) == 0xbb &&
				                operand_in(g.hex, offset + 4, 4) == retail_clamp;
			}
		}
		CHECK(clamp_covered);

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the main menu's bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - image_base); };
		const auto text_at = [&](const DWORD va) { return std::string_view(reinterpret_cast<const char*>(at(va))); };

		// Every guard: the retail bytes; every push its slot's string; the strings the names.
		const guard* mismatch = first_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr && sites_are_retail(image));
		bool strings_ok = true;
		for (const auto& s : slots) strings_ok &= text_at(s.retail_va) == s.retail;
		CHECK(strings_ok && text_at(0x68d184) == danger_room_line);
		CHECK(operand_at(at(0x6e6628), 4) == slots[2].retail_va && operand_at(at(0x6e662c), 4) == slots[5].retail_va);

		// Complete: in MAIN_MENU's code every dword naming one of the nine strings is a site's operand, and every
		// site is found. Outside it, the pushes of the same strings the change must leave: the item parser's and
		// another menu class's.
		std::set<DWORD> found;
		for (DWORD va = code_begin; va + 4 <= code_end; ++va)
		{
			const auto value = operand_at(at(va), 4);
			for (const auto& s : slots)
			{
				if (value == s.retail_va) found.insert(va - 1);
			}
		}
		CHECK(found == pushes);
		const std::array<std::pair<DWORD, std::size_t>, 10> elsewhere{{
			{0x5bc9c8, 7}, {0x5bca21, 6}, {0x5bca57, 8}, {0x5bca69, 7},
			{0x5cc426, 0}, {0x5cc436, 1}, {0x5cc446, 2}, {0x5cc456, 3}, {0x5cc466, 4}, {0x5cc476, 5},
		}};
		bool elsewhere_ok = true;
		for (const auto& [push, slot] : elsewhere) elsewhere_ok &= at(push)[0] == push_imm32 && operand_at(at(push + 1), 4) == slots[slot].retail_va;
		CHECK(elsewhere_ok);

		// The change on a copy: exactly the operands of the 17 sites, each its slot's pointer.
		std::vector<std::uint8_t> before(image, image + image_size);
		apply(image, writes);
		std::set<DWORD> changed;
		for (DWORD i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i]) changed.insert(image_base + i);
		}
		std::set<DWORD> expected;
		for (const auto& w : writes)
		{
			for (DWORD b = 0; b < 4; ++b) expected.insert(w.va + b);
		}
		CHECK(changed == expected);
		bool applied = true;
		for (const auto& s : sites)
		{
			applied &= at(s.push)[0] == push_imm32 && operand_at(at(s.push + 1), 4) == (s.slot < 7 ? pointers[s.slot] : slots[s.slot].retail_va);
		}
		CHECK(applied);
		elsewhere_ok = true;
		for (const auto& [push, slot] : elsewhere) elsewhere_ok &= operand_at(at(push + 1), 4) == slots[slot].retail_va;
		CHECK(elsewhere_ok && operand_at(at(0x6e6628), 4) == slots[2].retail_va && operand_at(at(0x6e662c), 4) == slots[5].retail_va);
		// Patched (or any other build): the guards no longer match, so nothing would be written twice.
		mismatch = first_mismatch(image);
		CHECK(mismatch && mismatch->va == 0x5c92a5 && !sites_are_retail(image));
		std::memcpy(image, before.data(), image_size);

		// The eight-item list on a copy: the operands of slots 0..7 and the one clamp byte, nothing else. The
		// clamp: cmp edi, 8 / mov ebx, 6 / jge / mov ebx, edi - slot 7 keeps its own index, slot 8 is Quit's.
		CHECK(at(clamp_compare)[0] == 0x83 && at(clamp_compare)[1] == 0xff && at(clamp_operand)[0] == retail_clamp && at(clamp_compare + 3)[0] == 0xbb &&
		      operand_at(at(clamp_compare + 4), 4) == retail_clamp && at(clamp_compare + 8)[0] == 0x7d && at(clamp_compare + 10)[0] == 0x8b && at(clamp_compare + 11)[0] == 0xdf);
		apply(image, writes8);
		changed.clear();
		for (DWORD i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i]) changed.insert(image_base + i);
		}
		expected.clear();
		for (const auto& w : writes8)
		{
			for (DWORD b = 0; b < w.size; ++b) expected.insert(w.va + b);
		}
		CHECK(changed == expected && at(clamp_operand)[0] == eight_item_clamp && operand_at(at(clamp_compare + 4), 4) == retail_clamp);
		applied = true;
		for (const auto& s : sites)
		{
			applied &= operand_at(at(s.push + 1), 4) == (s.slot < 8 ? pointers[s.slot] : slots[s.slot].retail_va);
		}
		CHECK(applied);
		std::memcpy(image, before.data(), image_size);
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// The DLL's kill XP function, as xp_curve.cpp has it: what the patched vt+0xcc jumps to.
	int __fastcall test_kill_xp(void* /*registry*/, void* /*edx*/, const int level)
	{
		return xp_curve_rules::kill_xp(level);
	}

	// The game's code on the patched copy. No C++ objects here: an exception is a failure.
	using xp_lookup_t = std::uint32_t(__stdcall*)(int level);
	using level_cap_t = int(__stdcall*)();
	using kill_xp_t = int(__fastcall*)(void* registry, void* edx, int level);

	bool run_xp_lookup(const std::uint8_t* function, const int level, std::uint32_t& xp)
	{
		__try
		{
			xp = reinterpret_cast<xp_lookup_t>(const_cast<std::uint8_t*>(function))(level);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool run_level_cap(const std::uint8_t* function, int& cap)
	{
		__try
		{
			cap = reinterpret_cast<level_cap_t>(const_cast<std::uint8_t*>(function))();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool run_kill_xp(const std::uint8_t* function, const int level, int& xp)
	{
		__try
		{
			xp = reinterpret_cast<kill_xp_t>(const_cast<std::uint8_t*>(function))(nullptr, nullptr, level);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// [Game] XPCurve (xp_curve_rules.hpp): the value's rules; XML1's tables against default.xbe's formulas worked out
	// again here, the lookups at the table's edges, the kill split; the sites and guards on their own; then, when a
	// copy of XMen2.exe is at hand, every guard against it, the change applied to that copy (exactly the sites'
	// bytes) and the patched code run there: the table lookup, the cap and the kill XP jump.
	void check_xp_curve_rules()
	{
		using namespace xp_curve_rules;
		std::printf("[Game] XPCurve (X-Men Legends 1's levels and kill XP)\n");

		const auto xml1 = parse_curve("xml1");
		CHECK(xml1.set && xml1.value == curve::xml1 && xml1.error.empty());
		CHECK(parse_curve("  XML1 \t; X-Men Legends 1's levels").set && parse_curve("  XML1 \t; X-Men Legends 1's levels").value == curve::xml1);
		CHECK(parse_curve("xml2").set && parse_curve("Xml2").value == curve::xml2);
		const auto unset = parse_curve("   ; nothing");
		CHECK(!unset.set && unset.error.empty() && !parse_curve("").set && parse_curve("").error.empty());
		for (const char* bad : {"xml3", "1", "on", "xml", "xml1 xml2", "xml1,xml2", "x-men legends", "caf\xe9"})
		{
			const auto refused = parse_curve(bad);
			if (refused.error.empty()) std::printf("  info  \"%s\" was taken\n", bad);
			CHECK(!refused.set && !refused.error.empty());
		}

		// f(L) = trunc(2.5 x (4/3)^(L-1)) (default.xbe 0x54800), in double precision here: none of them is within a
		// thousandth of a whole number, so XML1's own double pow gave the same (the closest, f(40) = 186444.998, is
		// the one a 24-bit multiply would round up).
		bool kills_ok = true;
		double closest = 1.0;
		double power = 0.75; // (4/3)^(0 - 1)
		for (int level = 0; level <= xml1_max_level; ++level, power *= 4.0 / 3.0)
		{
			const double value = 2.5 * power;
			const double fraction = value - std::floor(value);
			closest = std::min({closest, fraction, 1.0 - fraction});
			kills_ok &= xml1_kill_xp_table[static_cast<std::size_t>(level)] == static_cast<std::int32_t>(std::floor(value));
		}
		std::printf("  info  closest f(L) to a whole number: %.4f away\n", closest);
		CHECK(kills_ok && closest > 1e-3);
		// T1 (default.xbe 0x541e0): -1, 0, then T1(n-1) + 5 (n + 8) f(n-1) up to 45, then 0x7fffffff.
		bool table_ok = xml1_level_xp[0] == 0xffffffff && xml1_level_xp[1] == 0 && xml1_level_xp[46] == 0x7fffffff;
		for (int n = 2; n <= xml1_max_level; ++n)
		{
			table_ok &= xml1_level_xp[static_cast<std::size_t>(n)] ==
			            xml1_level_xp[static_cast<std::size_t>(n - 1)] + 5u * static_cast<std::uint32_t>(n + 8) * static_cast<std::uint32_t>(xml1_kill_xp_table[static_cast<std::size_t>(n - 1)]);
		}
		CHECK(table_ok);
		// research/heroes/levels.md section 4's rows.
		CHECK(xp_for_level(5) == 830 && xp_for_level(10) == 6880 && xp_for_level(20) == 220650 && xp_for_level(26) == 1543300 && xp_for_level(30) == 5510355 &&
		      xp_for_level(35) == 26544400 && xp_for_level(40) == 125847705);
		CHECK(kill_xp(5) == 7 && kill_xp(10) == 33 && kill_xp(20) == 591 && kill_xp(26) == 3322 && kill_xp(30) == 10499 && kill_xp(35) == 44244 && kill_xp(40) == 186444);

		// The lookups at the table's edges: the patched 0x448a90 reads 1..46; the cap; the XP a hero can have.
		CHECK(xp_for_level(1) == 0 && xp_for_level(2) == 100 && xp_for_level(45) == 589254820);
		CHECK(xp_for_level(0) == 0x7fffffff && xp_for_level(-1) == 0x7fffffff && xp_for_level(46) == 0x7fffffff && xp_for_level(47) == 0x7fffffff &&
		      xp_for_level(99) == 0x7fffffff && xp_for_level(100) == 0x7fffffff && xp_for_level(101) == 0x7fffffff);
		CHECK(max_xp() == 589254821);
		CHECK(level_for_xp(0) == 1 && level_for_xp(99) == 1 && level_for_xp(100) == 2);
		CHECK(level_for_xp(1543299) == 25 && level_for_xp(1543300) == 26 && level_for_xp(2124649) == 26 && level_for_xp(2124650) == 27);
		CHECK(level_for_xp(589254819) == 44 && level_for_xp(589254820) == 45 && level_for_xp(max_xp()) == 45 && level_for_xp(0xffffffff) == 45);
		// The port's cases: awardXPToPlayable(2000000) -> 26 (XML2's table: 40); act 9's 500000 alone -> 22;
		// asteroid_m's setXP(..., 1125000) -> 25 (a top-up; XML2's table: level 32).
		CHECK(level_for_xp(2000000) == 26 && xml2_level_for_xp(2000000) == 40 && xml2_xp_for_level(40) == 1988935 && xml2_xp_for_level(41) == 2124300);
		CHECK(level_for_xp(500000) == 22 && level_for_xp(1125000) == 25 && xml2_level_for_xp(1125000) == 32);
		CHECK(xml2_xp_for_level(1) == 0 && xml2_xp_for_level(5) == 17910 && xml2_xp_for_level(15) == 172935 && xml2_xp_for_level(100) == 0x7fffffff);

		// Kill XP: f(L), the enemy level capped at 45, XML1's formula below 0.
		CHECK(kill_xp(0) == 1 && kill_xp(1) == 2 && kill_xp(45) == 785677);
		CHECK(kill_xp(-1) == 1 && kill_xp(-2) == 1 && kill_xp(-3) == 0 && kill_xp(-100) == 0 && kill_xp(std::numeric_limits<int>::min()) == 0);
		CHECK(kill_xp(46) == 785677 && kill_xp(99) == 785677 && kill_xp(255) == 785677 && kill_xp(std::numeric_limits<int>::max()) == 785677);
		// The split: every hero (the bench included) half, each party hero 3 (half + 1) more; XML2's bench 1 at most.
		const auto level20 = xml1_shares(591);
		CHECK(level20.every_hero == 295 && level20.party == 888 && xml2_bench_share(591) == 1);
		CHECK(xml1_shares(1).every_hero == 0 && xml1_shares(1).party == 3 && xml1_shares(2).every_hero == 1 && xml1_shares(2).party == 6);
		CHECK(xml2_bench_share(1) == 0 && xml2_bench_share(2) == 0 && xml2_bench_share(3) == 1 && xml2_bench_share(1000000) == 1);
		const auto biggest = xml1_shares(3u * 785677u); // a level-45 kill with XML2's x3 flag: no overflow into the loop's signed int
		CHECK(biggest.every_hero == 1178515 && biggest.party == 3535548 && biggest.party < 0x7fffffffu);
		// An AI teammate: all of the base beside the kill, a third just inside 300 units, nothing beyond; XML1's
		// (1 - x)(2 (half + 1)) + (half + 1) within a unit of the game's float.
		CHECK(std::fabs(teammate_factor(0.0f) - 1.0f) < 1e-6f && std::fabs(teammate_factor(89999.9f) - 1.0f / 3.0f) < 1e-5f && teammate_factor(90000.0f) == 0.0f &&
		      teammate_factor(1e6f) == 0.0f);
		bool teammates_ok = true;
		for (const float d2 : {0.0f, 900.0f, 22500.0f, 45000.0f, 80000.0f})
		{
			const std::uint32_t half = level20.every_hero;
			const auto patched = static_cast<long long>(static_cast<float>(level20.party) * teammate_factor(d2));
			const auto xml1_share = static_cast<long long>((1.0f - d2 / 90000.0f) * static_cast<float>(2 * (half + 1)) + static_cast<float>(half + 1));
			teammates_ok &= patched - xml1_share <= 1 && xml1_share - patched <= 1;
		}
		CHECK(teammates_ok);

		// The sites and guards on their own: well-formed, apart; every site inside one guard with its retail bytes;
		// an address site 4 bytes (the jump 5), a fixed one as long as what it replaces.
		bool guards_ok = true;
		std::set<DWORD> addresses_seen;
		for (const auto& g : guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses_seen.insert(g.va).second && g.what && *g.what;
		bool apart = true;
		for (std::size_t i = 0; i < guards.size(); ++i)
		{
			const DWORD end = guards[i].va + static_cast<DWORD>(limits_rules::hex_size(guards[i].hex));
			for (std::size_t j = 0; j < guards.size(); ++j)
			{
				if (i != j && guards[j].va >= guards[i].va && guards[j].va < end) apart = false;
			}
		}
		CHECK(guards_ok && apart);
		bool sites_ok = true;
		for (std::size_t i = 0; i < sites.size(); ++i)
		{
			const auto& s = sites[i];
			const std::size_t size = limits_rules::hex_size(s.retail);
			sites_ok &= limits_rules::valid_hex(s.retail) && s.what && *s.what;
			if (s.kind == value_kind::fixed) sites_ok &= limits_rules::valid_hex(s.patched) && limits_rules::hex_size(s.patched) == size && s.patched != s.retail;
			else sites_ok &= s.patched.empty() && size == (s.kind == value_kind::kill_xp_jump ? 5u : 4u);
			int covering = 0;
			for (const auto& g : guards)
			{
				const DWORD end = g.va + static_cast<DWORD>(limits_rules::hex_size(g.hex));
				if (g.va <= s.va && s.va + size <= end)
				{
					++covering;
					for (std::size_t b = 0; b < size; ++b) sites_ok &= limits_rules::hex_byte(g.hex, s.va - g.va + b) == limits_rules::hex_byte(s.retail, b);
				}
			}
			if (covering != 1) std::printf("  info  site 0x%08lX is inside %d guards\n", s.va, covering);
			sites_ok &= covering == 1;
			for (std::size_t j = i + 1; j < sites.size(); ++j)
			{
				const auto& t = sites[j];
				sites_ok &= t.va >= s.va + size || s.va >= t.va + limits_rules::hex_size(t.retail);
			}
		}
		CHECK(sites_ok);
		const addresses fake{0x10001000, 0x10002000, 0x10003000, 0x10003004, 0x10003008};
		const auto fake_writes = writes_for(fake);
		bool sizes_ok = fake_writes.size() == sites.size();
		for (std::size_t i = 0; i < fake_writes.size() && sizes_ok; ++i) sizes_ok &= fake_writes[i].va == sites[i].va && fake_writes[i].bytes.size() == limits_rules::hex_size(sites[i].retail);
		CHECK(sizes_ok);
		CHECK((bytes_for(sites[12], fake) == std::vector<std::uint8_t>{0xe9, 0x0b, 0x76, 0xbb, 0x0f})); // jmp 0x10002000 from 0x44a9f0
		CHECK((bytes_for(sites[1], fake) == std::vector<std::uint8_t>{0x00, 0x10, 0x00, 0x10}));

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the XP curve's bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - image_base); };

		// Every guard: the retail bytes; the cap runs as the retail 99 first.
		const guard* mismatch = first_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr);
		int cap = 0;
		CHECK(run_level_cap(at(0x44b690), cap) && cap == xml2_max_level);

		// The change on a copy: exactly the bytes of the sites that differ, each site its new bytes; the DLL's
		// addresses here are the test's own, the jump's aimed so that it lands on test_kill_xp in this copy.
		std::vector<std::uint8_t> before(image, image + image_size);
		addresses mine;
		mine.level_table = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(xml1_level_xp.data()));
		mine.kill_xp_function = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&test_kill_xp)) - (static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(image)) - image_base);
		mine.far_teammate = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&xp_curve_rules::far_teammate));
		mine.teammate_floor = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&xp_curve_rules::teammate_floor));
		mine.teammate_falloff = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&xp_curve_rules::teammate_falloff));
		const auto writes = writes_for(mine);
		apply(image, writes);
		std::set<DWORD> changed;
		for (DWORD i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i]) changed.insert(image_base + i);
		}
		std::set<DWORD> expected;
		bool written_ok = true;
		for (const auto& w : writes)
		{
			for (std::size_t b = 0; b < w.bytes.size(); ++b)
			{
				if (w.bytes[b] != before[w.va - image_base + b]) expected.insert(w.va + static_cast<DWORD>(b));
				written_ok &= at(w.va)[b] == w.bytes[b];
			}
		}
		std::printf("  info  %zu bytes changed at %zu sites\n", changed.size(), writes.size());
		CHECK(changed == expected && written_ok);
		// The 23 bytes of the shares, exactly: fstp st(0); mov edi, ebx; shr edi, 1; lea eax, [edi+edi*2+3];
		// mov [esp+0x1c], eax; a 7- and a 2-byte nop - and the push 1 and the fild after them.
		CHECK(std::memcmp(at(0x437199), "\xdd\xd8\x8b\xfb\xd1\xef\x8d\x44\x7f\x03\x89\x44\x24\x1c\x0f\x1f\x80\x00\x00\x00\x00\x66\x90", 23) == 0);
		CHECK(std::memcmp(at(0x4371b7), "\x6a\x01\x57", 3) == 0 && std::memcmp(at(0x4373d1), "\xdb\x44\x24\x1c", 4) == 0);
		CHECK(operand_at(at(0x448afd), 3) == 0x85048b && operand_at(at(0x448b00), 4) == mine.level_table && at(0x448afa)[0] == 0x2f); // mov eax, [eax*4 + table]; cmp eax, 47

		// The patched code, run: the lookup's tail (0x448af0: [esp+4] in 1..46 -> the table, else 0x7fffffff) for
		// every level around the table; the cap; the kill XP through the jump.
		bool lookups_ok = true;
		for (int level = -2; level <= 102; ++level)
		{
			std::uint32_t xp = 0;
			lookups_ok &= run_xp_lookup(at(0x448af0), level, xp) && xp == xp_for_level(level);
		}
		CHECK(lookups_ok);
		CHECK(run_level_cap(at(0x44b690), cap) && cap == xml1_max_level);
		bool kills_run_ok = true;
		for (const int level : {-5, -2, 0, 1, 20, 40, 45, 46, 99, 255})
		{
			int xp = -1;
			kills_run_ok &= run_kill_xp(at(0x44a9f0), level, xp) && xp == kill_xp(level);
		}
		CHECK(kills_run_ok);
		// Patched (or any other build): the guards no longer match, so nothing would be written twice.
		mismatch = first_mismatch(image);
		CHECK(mismatch && mismatch->va == 0x448a90);
		std::memcpy(image, before.data(), image_size);
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// [Input] Prompts (pad_prompts_rules.hpp): the ini values, the UI codes, the pad's names, the colour table; the
	// slot shown on binding maps of the test's own, the device chosen on an input object as the game's poll leaves
	// it; the writes, and the colour stub run the way the renderer runs it; then, when a copy of XMen2.exe is at
	// hand, every guard against it, the action table against 0x619c40's, maps built by the game's own setter and
	// the change applied to that copy.
	struct fake_map
	{
		std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x980, 0);
		std::uint8_t* data() { return bytes.data(); }
		void set(const int action, const int slot, const std::uint32_t device, const std::uint32_t control)
		{
			using namespace pad_prompts_rules;
			auto* record = bytes.data() + map_records + (static_cast<std::size_t>(action) * slot_count + slot) * record_size;
			std::memcpy(record, &device, 4);
			std::memcpy(record + 4, &control, 4);
			std::uint32_t count = 0;
			std::memcpy(&count, bytes.data(), 4);
			count = std::max<std::uint32_t>(count, static_cast<std::uint32_t>(action) + 1);
			std::memcpy(bytes.data(), &count, 4);
			if (pad_device(device)) std::memcpy(bytes.data() + map_pad_device, &device, 4);
		}
	};

	using set_binding_t = void(__fastcall*)(void* map, void* edx, int action, int slot, int device, int control);
	using get_binding_t = void(__fastcall*)(void* map, void* edx, int action, int slot, int* device, int* control);
	using no_argument_t = void(__fastcall*)(void* self, void* edx);

	bool run_set_binding(const std::uint8_t* function, void* map, const int action, const int slot, const int device, const int control)
	{
		__try
		{
			reinterpret_cast<set_binding_t>(const_cast<std::uint8_t*>(function))(map, nullptr, action, slot, device, control);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool run_get_binding(const std::uint8_t* function, void* map, const int action, const int slot, int& device, int& control)
	{
		__try
		{
			reinterpret_cast<get_binding_t>(const_cast<std::uint8_t*>(function))(map, nullptr, action, slot, &device, &control);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool run_no_argument(const std::uint8_t* function, void* self)
	{
		__try
		{
			reinterpret_cast<no_argument_t>(const_cast<std::uint8_t*>(function))(self, nullptr);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// The colour stub run the way the renderer runs it (thunk in `memory`): bl = the label's character, esi = the
	// slot, the word read back from [esp + esi*2 + 0x80]; -1 when edx didn't come back as it went in.
	int run_colour_stub(std::uint8_t* memory, const int character, const int slot)
	{
		using thunk_t = int(__cdecl*)(int character, int slot);
		__try
		{
			return reinterpret_cast<thunk_t>(memory)(character, slot);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return -2;
		}
	}

	void check_pad_prompts_rules()
	{
		using namespace pad_prompts_rules;
		std::printf("[Input] Prompts (the button prompts for the pad)\n");

		// The ini.
		CHECK(!parse_mode("").set && parse_mode("").value == mode::automatic && parse_mode("  ; nothing").error.empty());
		CHECK(parse_mode("auto").value == mode::automatic && parse_mode(" PAD ").value == mode::pad && parse_mode("Keyboard ; comment").value == mode::keyboard);
		CHECK(parse_mode("off").set && parse_mode("off").value == mode::off);
		bool refused_ok = true;
		for (const char* bad : {"1", "on", "xbox", "pad keyboard", "automatic", "gamepad"}) refused_ok &= !parse_mode(bad).error.empty();
		CHECK(refused_ok);
		CHECK(parse_switch("").value && !parse_switch("").set && parse_switch("0").set && !parse_switch("0").value && !parse_switch("Off").value && parse_switch("yes").value);
		CHECK(!parse_switch("2").error.empty() && !parse_switch("colour").error.empty());

		// UI codes: the wheel's tokens, the menus' and the per-player ones.
		CHECK(action_of(4) == 4 && action_of(5) == 5 && action_of(6) == 6 && action_of(8) == 7); // ATTACK LowAttack, SMASH HighAttack, MOVE Jump, GUARD Guard
		CHECK(action_of(0x15) == 5 && action_of(0x14) == 0x11 && action_of(0x18) == 6 && action_of(0xd) == 0xa && action_of(9) == 9); // MENU_BACK, MENU_OK, MENU_OTHER, MENU_DROP, MENU_DETAILS
		CHECK(action_of(0xe) == -1 && action_of(0x34) == -1 && action_of(-1) == -1 && action_of(0x33) == 0x29);
		CHECK(stock_target(0x36).code == 8 && stock_target(0x36).player == 2 && !stock_target(0x36).play_row);  // GUAR3
		CHECK(stock_target(0x4f).code == 10 && stock_target(0x4f).player == 3 && !stock_target(0x4f).play_row); // SOL4
		CHECK(stock_target(0x51).code == 4 && stock_target(0x52).code == 5 && stock_target(0x50).code == 8 && stock_target(0x53).code == 10);
		CHECK(stock_target(0x51).play_row && stock_target(0x52).play_row && stock_target(0x50).play_row && stock_target(0x53).play_row);
		CHECK(!stock_target(5).play_row && !stock_target(6).play_row); // the game reads SMASH and MOVE in the current row...
		CHECK(prompt_target(5).play_row && prompt_target(6).play_row); // ... the fix in play's
		CHECK(!prompt_target(4).play_row && !prompt_target(8).play_row && !prompt_target(0x15).play_row && !prompt_target(0x18).play_row); // ATTACK = MENU_ACCEPT, GUARD = MENU_SUBTRACT: as the game

		// The pad's names: the console layout pad_bindings.cpp gives.
		CHECK(pad_label(0x16, false) == "[A]" && pad_label(0x17, false) == "[B]" && pad_label(0x18, false) == "[Y]" && pad_label(0x15, false) == "[X]");
		CHECK(pad_label(0x16, true) == "A" && pad_label(0x17, true) == "B" && pad_label(0x18, true) == "Y" && pad_label(0x15, true) == "X");
		CHECK(pad_label(0x1c, true) == "[RT]" && pad_label(0x1b, true) == "[LT]" && pad_label(0x1a, true) == "[RB]" && pad_label(0x19, true) == "[LB]");
		CHECK(pad_label(0x1d, true) == "[Back]" && pad_label(0x1e, true) == "[Start]" && pad_label(0x1f, true) == "[LS]" && pad_label(0x20, true) == "[RS]");
		CHECK(pad_label(20, true) == "[D-pad Up]" && pad_label(19, true) == "[D-pad Down]" && pad_label(18, true) == "[D-pad Left]" && pad_label(17, true) == "[D-pad Right]");
		CHECK(pad_label(4, true) == "[LS Up]" && pad_label(3, true) == "[LS Down]" && pad_label(2, true) == "[LS Left]" && pad_label(1, true) == "[LS Right]"); // Forward: Y below centre
		CHECK(pad_label(12, true) == "[RS Up]" && pad_label(11, true) == "[RS Down]" && pad_label(6, true) == "[RS Left]" && pad_label(5, true) == "[RS Right]");
		CHECK(pad_label(0x21, true) == "[Button 13]" && pad_label(7, true) == "[Axis 4+]" && pad_label(16, true) == "[Axis 8-]" && pad_label(0, true) == "[???]");
		const auto table = label_colours();
		CHECK(table['A'] == 1014 && table['B'] == 1016 && table['X'] == 1015 && table['Y'] == 1017);
		CHECK(table['E'] == 1041 && table[0xa4] == 1041 && table[0] == 1041 && std::count(table.begin(), table.end(), std::uint16_t{1041}) == 252);

		// The slot shown. Player 1 in the team menu's row as 0x61b030 leaves it: HighAttack KP6 (slot 0), the pad's
		// B (slot 1), the menus' Esc (slot 2); LowAttack the pad's A and the menus' J; Pause Enter, KP Enter in slot 3.
		fake_map team;
		team.set(5, 0, 1, 0x4d);
		team.set(5, 1, 3, 0x17);
		team.set(5, 2, 1, 0x01);
		team.set(4, 1, 3, 0x16);
		team.set(4, 2, 1, 0x24);
		team.set(0x11, 2, 1, 0x1c);
		team.set(0x11, 3, 1, 0x9c);
		team.set(41, 0, 1, 0x0c); // QuickPower11, the last action
		const auto smash = read_bindings(team.data(), 5);
		CHECK(smash[0].device == 1 && smash[0].control == 0x4d && smash[1].device == 3 && smash[1].control == 0x17 && smash[2].control == 1 && smash[3].device == 0);
		CHECK(stock_slot(smash) == 2);    // the game: [Esc]
		CHECK(keyboard_slot(smash) == 2); // the keyboard in a menu row: the menu's key, as the game
		CHECK(pad_slot(smash) == 1);      // the pad: B
		CHECK(pad_slot(read_bindings(team.data(), 4)) == 1 && keyboard_slot(read_bindings(team.data(), 4)) == 2);
		CHECK(stock_slot(read_bindings(team.data(), 0x11)) == 2 && pad_slot(read_bindings(team.data(), 0x11)) == 2); // no pad binding: the keyboard's
		CHECK(stock_slot(read_bindings(team.data(), 7)) == -1 && pad_slot(read_bindings(team.data(), 7)) == -1 && keyboard_slot(read_bindings(team.data(), 7)) == -1);
		CHECK(read_bindings(team.data(), 42)[0].device == 0 && read_bindings(team.data(), -1)[0].device == 0 && read_bindings(team.data(), 41)[0].control == 0x0c);
		CHECK(has_keyboard(team.data()) && pad_of(team.data()) == 0);
		// Row 0, the bindings of play: KP6 first for the keyboard.
		fake_map play;
		play.set(5, 0, 1, 0x4d);
		play.set(5, 1, 3, 0x17);
		CHECK(keyboard_slot(read_bindings(play.data(), 5)) == 0 && pad_slot(read_bindings(play.data(), 5)) == 1);
		// Player 2: the second pad only; the game's own order shows it too.
		fake_map second;
		second.set(6, 0, 4, 0x18);
		CHECK(keyboard_slot(read_bindings(second.data(), 6)) == 0 && stock_slot(read_bindings(second.data(), 6)) == 0 && !has_keyboard(second.data()) && pad_of(second.data()) == 1);
		// A keyboard binding after a pad one: the keyboard shows it.
		fake_map mixed;
		mixed.set(6, 0, 3, 0x18);
		mixed.set(6, 1, 2, 0x01);
		CHECK(keyboard_slot(read_bindings(mixed.data(), 6)) == 1 && stock_slot(read_bindings(mixed.data(), 6)) == 0 && pad_slot(read_bindings(mixed.data(), 6)) == 0);
		fake_map none;
		CHECK(!has_keyboard(none.data()) && pad_of(none.data()) == -1);

		// Which device: an input object as the poll leaves it.
		std::vector<std::uint8_t> input(input_size, 0);
		const auto put = [&](const std::size_t offset, const std::uint32_t value) { std::memcpy(input.data() + offset, &value, 4); };
		const auto pad_at = [](const int pad, const bool now) { return (now ? pad_now : pad_before) + pad_stride * static_cast<std::size_t>(pad); };
		const auto settle = [&] {
			std::memcpy(input.data() + keyboard_before, input.data() + keyboard_now, 256);
			std::memcpy(input.data() + mouse_buttons_before, input.data() + mouse_buttons_now, 4);
			for (int p = 0; p < pad_count; ++p) std::memcpy(input.data() + pad_at(p, false), input.data() + pad_at(p, true), pad_stride);
		};
		for (int p = 0; p < pad_count; ++p) put(pad_at(p, true) + pad_pov, 0xffffffff);
		settle();
		activity seen;
		put(pads_read, 1); // pad 0 read
		seen.sample(input.data());
		CHECK(seen.frame == 1 && seen.keyboard == 0 && seen.pads[0] == 0 && seen.pads_present == 1);
		CHECK(choose(mode::automatic, true, 0, seen) == shown::pad);       // nothing pressed yet, the pad connected
		CHECK(choose(mode::automatic, true, 1, seen) == shown::keyboard);  // its pad not read
		CHECK(choose(mode::automatic, true, -1, seen) == shown::keyboard); // no pad bindings
		CHECK(choose(mode::automatic, false, 0, seen) == shown::pad);      // no keyboard bindings
		CHECK(choose(mode::pad, true, -1, seen) == shown::pad && choose(mode::keyboard, true, 0, seen) == shown::keyboard);
		input[keyboard_now + 0x12] = 0x80; // E
		seen.sample(input.data());
		CHECK(seen.keyboard == 2 && choose(mode::automatic, true, 0, seen) == shown::keyboard);
		settle();
		seen.sample(input.data()); // E held: nothing new
		CHECK(seen.keyboard == 2 && seen.frame == 3);
		input[pad_at(0, true) + pad_buttons + 1] = 0x80; // A
		seen.sample(input.data());
		CHECK(seen.pads[0] == 4 && choose(mode::automatic, true, 0, seen) == shown::pad);
		settle();
		input[mouse_buttons_now] = 0x80; // a click
		seen.sample(input.data());
		CHECK(seen.keyboard == 5 && choose(mode::automatic, true, 0, seen) == shown::keyboard);
		settle();
		put(pad_at(0, true) + pad_pov, 0); // the D-pad up
		seen.sample(input.data());
		CHECK(seen.pads[0] == 6 && choose(mode::automatic, true, 0, seen) == shown::pad);
		settle();
		input[keyboard_now + 0x1c] = 0x80; // Enter
		seen.sample(input.data());
		settle();
		put(pad_at(0, true) + pad_axes + 4, static_cast<std::uint32_t>(-400)); // the left stick up, not half way
		seen.sample(input.data());
		CHECK(seen.pads[0] == 6 && choose(mode::automatic, true, 0, seen) == shown::keyboard);
		settle();
		put(pad_at(0, true) + pad_axes + 4, static_cast<std::uint32_t>(-900)); // past half way
		seen.sample(input.data());
		CHECK(seen.pads[0] == 9 && choose(mode::automatic, true, 0, seen) == shown::pad);
		settle();
		input[pad_at(3, true) + pad_buttons] = 0x80; // pad 3 pressed but not read this frame: not counted
		seen.sample(input.data());
		CHECK(seen.pads[3] == 0);
		put(pads_read, 0); // pad 0 unplugged
		seen.sample(input.data());
		CHECK(seen.pads_present == 0 && choose(mode::automatic, true, 0, seen) == shown::keyboard);

		// The patch on its own: the writes, and the colour stub run.
		addresses fake;
		fake.label = 0x10001000;
		fake.poll = 0x10002000;
		fake.colour_stub = 0x10003000;
		fake.wheel = {0x10004000, 0x10004008, 0x10004010};
		const auto writes = writes_for(fake);
		CHECK(writes.size() == 6 && writes[0].va == label_call && writes[1].va == poll_call && writes[5].va == colour_site && writes[5].bytes.size() == colour_site_size);
		CHECK((writes[0].bytes == std::vector<std::uint8_t>{0xe8, 0xc2, 0x38, 0xb4, 0x0f})); // call 0x10001000 from 0x4bd739
		CHECK((writes[2].bytes == std::vector<std::uint8_t>{0x00, 0x40, 0x00, 0x10}) && writes[3].va == wheel_tokens + 4 && writes[4].va == wheel_tokens + 8);
		fake.colour_stub = 0;
		CHECK(writes_for(fake).size() == 5);
		auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		CHECK(code != nullptr);
		if (code)
		{
			const auto stub = colour_stub(static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(table.data())));
			std::memcpy(code + 0x80, stub.data(), stub.size());
			// push ebx; push esi; mov bl, [esp+0xc]; mov esi, [esp+0x10]; sub esp, 0x100; mov edx, 0x12345678; call stub;
			// movzx eax, word [esp+esi*2+0x80]; cmp edx, 0x12345678; je +5; mov eax, -1; add esp, 0x100; pop esi; pop ebx; ret
			std::vector<std::uint8_t> thunk{0x53, 0x56, 0x8a, 0x5c, 0x24, 0x0c, 0x8b, 0x74, 0x24, 0x10, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00, 0xba, 0x78, 0x56, 0x34, 0x12, 0xe8, 0, 0, 0, 0};
			const std::uint32_t rel = 0x80 - static_cast<std::uint32_t>(thunk.size());
			std::memcpy(thunk.data() + thunk.size() - 4, &rel, 4);
			thunk.insert(thunk.end(), {0x0f, 0xb7, 0x84, 0x74, 0x80, 0x00, 0x00, 0x00, 0x81, 0xfa, 0x78, 0x56, 0x34, 0x12, 0x74, 0x05, 0xb8, 0xff, 0xff, 0xff, 0xff,
			                           0x81, 0xc4, 0x00, 0x01, 0x00, 0x00, 0x5e, 0x5b, 0xc3});
			std::memcpy(code, thunk.data(), thunk.size());
			FlushInstructionCache(GetCurrentProcess(), code, 0x1000);
			CHECK(run_colour_stub(code, 'A', 0) == 1014 && run_colour_stub(code, 'B', 3) == 1016 && run_colour_stub(code, 'X', 17) == 1015 && run_colour_stub(code, 'Y', 63) == 1017);
			CHECK(run_colour_stub(code, 'E', 5) == 1041 && run_colour_stub(code, 0xa4, 1) == 1041);
			VirtualFree(code, 0, MEM_RELEASE);
		}

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the prompts' bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - image_base); };

		const guard* mismatch = first_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr);

		// The action table against 0x619c40's jump table: mov eax, imm32 / xor eax, eax / or eax, -1.
		bool actions_ok = true;
		for (int c = 0; c < static_cast<int>(actions.size()); ++c)
		{
			const auto* target = at(operand_at(at(0x619d54 + 4 * static_cast<DWORD>(c)), 4));
			const int value = target[0] == 0xb8 ? static_cast<int>(operand_at(target + 1, 4)) : target[0] == 0x33 ? 0 : target[0] == 0x83 && target[2] == 0xff ? -1 : -99;
			actions_ok &= value == action_of(c);
		}
		CHECK(actions_ok && at(0x619d4d)[0] == 0x83); // anything above 0x33: -1
		// 0x619e30's own pre-map: the byte table of its switch on code - 0x34 (4 GUAR, 20 others, 4 SOL, GUAR9 ATTAC9 SMAS9 SOL9).
		CHECK(std::memcmp(at(0x619fb8), "\x00\x00\x00\x00\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x06\x01\x01\x01\x01\x02\x03\x04\x05", 32) == 0);

		// Maps built by the game's own setter: the layout the rules read, the pad the game keeps at +0x964.
		test_block map(0x980);
		CHECK(run_set_binding(at(0x6297a0), map.data(), 5, 0, 1, 0x4d) && run_set_binding(at(0x6297a0), map.data(), 5, 1, 3, 0x17) &&
		      run_set_binding(at(0x6297a0), map.data(), 5, 2, 1, 0x01) && run_set_binding(at(0x6297a0), map.data(), 41, 3, 1, 0x9c));
		const auto game_smash = read_bindings(map.data(), 5);
		CHECK(game_smash[0].device == 1 && game_smash[0].control == 0x4d && game_smash[1].device == 3 && game_smash[1].control == 0x17 && game_smash[2].control == 1);
		CHECK(read_bindings(map.data(), 41)[3].control == 0x9c && map.dword(0) == 42 && pad_of(map.data()) == 0 && has_keyboard(map.data()) && map.slack_untouched());
		int device = -1, control = -1;
		CHECK(run_get_binding(at(0x6294b0), map.data(), 5, 1, device, control) && device == 3 && control == 0x17);
		CHECK(stock_slot(game_smash) == 2 && pad_slot(game_smash) == 1 && pad_label(game_smash[1].control, true) == "B");
		CHECK(run_set_binding(at(0x6297a0), map.data(), 5, 1, 0, 0) && run_no_argument(at(0x6295a0), map.data()) && pad_of(map.data()) == -1); // the pad unbound: +0x964 back to 0

		// The change on a copy: exactly the writes' bytes, the calls landing where they should.
		std::vector<std::uint8_t> before(image, image + image_size);
		addresses mine;
		mine.label = 0x10001000;
		mine.poll = 0x10002000;
		mine.colour_stub = 0x10003000;
		mine.wheel = {0x10004000, 0x10004008, 0x10004010};
		const auto patch = writes_for(mine);
		apply(image, patch);
		std::set<DWORD> changed, expected;
		for (DWORD i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i]) changed.insert(image_base + i);
		}
		for (const auto& w : patch)
		{
			for (std::size_t b = 0; b < w.bytes.size(); ++b)
			{
				if (w.bytes[b] != before[w.va - image_base + b]) expected.insert(w.va + static_cast<DWORD>(b));
			}
		}
		CHECK(changed == expected);
		const auto call_target = [&](const DWORD va) { return at(va)[0] == 0xe8 ? va + 5 + operand_at(at(va) + 1, 4) : 0; };
		CHECK(call_target(label_call) == mine.label && call_target(poll_call) == mine.poll && call_target(colour_site) == mine.colour_stub);
		CHECK(std::memcmp(at(colour_site + 5), "\x0f\x1f\x44\x00\x00", 5) == 0 && std::memcmp(at(colour_site + 10), "\x66\xc7\x84\x74\x82", 5) == 0); // then the game's own "end colour" word
		CHECK(operand_at(at(wheel_tokens), 4) == mine.wheel[0] && operand_at(at(wheel_tokens + 12), 4) == 0x6a275c); // $MOVE stays
		mismatch = first_mismatch(image);
		CHECK(mismatch && mismatch->va == 0x4bd720); // patched: the guards no longer match
		std::memcpy(image, before.data(), image_size);
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// ---- Discord Rich Presence (discord_rules.hpp) --------------------------------------------------

	// Blocks of memory at the game's addresses, for discord_rules::game_reader: what isn't in a block
	// can't be read.
	struct fake_memory
	{
		std::map<DWORD, std::vector<std::uint8_t>> blocks;

		std::uint8_t* at(const DWORD va, const std::size_t size = 1)
		{
			auto it = blocks.upper_bound(va);
			if (it == blocks.begin()) return nullptr;
			--it;
			if (va - it->first + size > it->second.size()) return nullptr;
			return it->second.data() + (va - it->first);
		}
		bool read(const DWORD va, void* out, const std::size_t size)
		{
			const auto* p = at(va, size);
			if (!p) return false;
			std::memcpy(out, p, size);
			return true;
		}
		void block(const DWORD va, const std::size_t size)
		{
			blocks[va].assign(size, 0);
		}
		void u32(const DWORD va, const std::uint32_t value)
		{
			std::memcpy(at(va, 4), &value, 4);
		}
		void u16(const DWORD va, const std::uint16_t value)
		{
			std::memcpy(at(va, 2), &value, 2);
		}
		void u8(const DWORD va, const std::uint8_t value)
		{
			*at(va) = value;
		}
		void text(const DWORD va, const std::string_view value)
		{
			std::memcpy(at(va, value.size() + 1), value.data(), value.size());
			*at(va + static_cast<DWORD>(value.size())) = 0;
		}
	};

	// The game's state as XMen2.exe keeps it: the zone manager, the game object with its party and act,
	// the string pool, the stats registry with three herostat entries (Wolverine with his stats loaded,
	// Cyclops without, the port's hidden Magneto placeholder), the menu manager, the Danger Room, the
	// network manager and the session.
	struct fake_game
	{
		fake_memory memory;
		std::uint32_t next_string = 0;
		static constexpr DWORD registry = 0x10000000;
		static constexpr DWORD manager = 0x20000000;

		std::uint32_t intern(const std::string_view text)
		{
			using namespace discord_rules;
			const std::uint32_t index = ++next_string;
			const std::uint32_t offset = index * 64;
			memory.u32(pool + pool_offsets + index * 4, offset);
			memory.text(pool + pool_text + offset, text);
			return 0x01000000 | index; // the handle's high byte isn't the index
		}

		fake_game()
		{
			using namespace discord_rules;
			memory.block(zones, 0x230);
			memory.u32(zones, zones_vtable);
			memory.block(game_object, 0x600);
			memory.u32(game_object, game_vtable);
			memory.block(pool, pool_text + 0x2000);
			memory.block(registry_cell, 4);
			memory.u32(registry_cell, registry);
			memory.block(registry, 0x12400);
			memory.u32(registry, registry_vtable);
			memory.block(frame_rate_rules::menu_manager_cell, 4);
			memory.u32(frame_rate_rules::menu_manager_cell, manager);
			memory.block(manager, frame_rate_rules::menu_current + 4);
			memory.u32(manager, menu_manager_vtable);
			memory.block(danger_room_state, 4);
			memory.u8(danger_room_state, danger_room_off);
			memory.block(courses, course_size * 4);
			memory.block(network, 0x200);
			memory.block(session, 0x430);

			zone("nyc/alison/nyc1_1_3", "East Manhattan");
			memory.u8(game_object + game_act, 1);
			memory.u32(registry + registry_stats_mask, 0x1f);
			const auto entry = [&](const std::uint16_t k, const std::string_view name, const std::string_view shown, const std::uint32_t stats, const std::uint8_t level,
			                       const std::uint8_t team)
			{
				const DWORD at = registry + registry_entries + k * entry_size;
				memory.u32(at + entry_name, intern(name));
				memory.u32(at + entry_character, intern(shown));
				memory.u32(at + entry_stats, stats);
				memory.u8(at + entry_level, level);
				memory.u8(at + entry_team, team);
				memory.u8(at + entry_flags, 1);
			};
			entry(3, "Wolverine", "Wolverine", 0x00010002, 1, team_hero); // stats slot 2
			entry(5, "Cyclops", "Cyclops", 0, 1, team_hero);                // no stats yet: the entry's level
			entry(7, "Magneto", "defaultman", 0, 1, 0);                     // the port's placeholder: no team
			entry(9, "Phoenix", "Jean Grey", 0x00020004, 1, team_hero);
			const std::uint16_t heroes[] = {3, 5, 7, 9};
			for (std::uint16_t i = 0; i < 4; ++i) memory.u16(registry + registry_heroes + i * 2, heroes[i]);
			memory.u32(registry + registry_hero_count, 4);
			memory.u8(registry + registry_stats + 2 * stats_size + stats_level, 3); // Wolverine: level 3
			memory.u8(registry + registry_stats + 4 * stats_size + stats_level, 9); // Phoenix: level 9
			party({"wolverine", "cyclops"});
		}

		void zone(const std::string_view path, const std::string_view title)
		{
			using namespace discord_rules;
			memory.text(zones + zone_path, path);
			memory.text(zones + zone_savename, title);
		}

		void party(const std::vector<std::string_view>& names)
		{
			using namespace discord_rules;
			for (DWORD slot = 0; slot < 4; ++slot)
			{
				memory.u32(game_object + game_party + slot * 4, slot < names.size() && !names[slot].empty() ? intern(names[slot]) : 0);
			}
		}

		std::optional<discord_rules::snapshot> read()
		{
			discord_rules::game_reader<fake_memory> reader(memory);
			return reader.read();
		}
	};

	void check_discord_rules()
	{
		using namespace discord_rules;
		std::printf("discord: Rich Presence\n");

		// [Discord]'s switches: 1/0, true/false, yes/no, on/off in any case; absent, empty or anything else: on.
		CHECK(parse_switch(std::nullopt, true) && parse_switch("", true) && parse_switch("  ", true) && parse_switch("maybe", true) && !parse_switch("maybe", false));
		CHECK(parse_switch("1", false) && parse_switch("true", false) && parse_switch("TRUE", false) && parse_switch("Yes", false) && parse_switch("on", false));
		CHECK(!parse_switch("0", true) && !parse_switch("false", true) && !parse_switch("False", true) && !parse_switch("NO", true) && !parse_switch("off", true));
		CHECK(!parse_switch(" 0 ", true) && !parse_switch("0   ; the launcher's switch", true) && parse_switch("on;", false));
		// An inline comment and the spaces or tabs around the value, as the launcher reads them too; nothing
		// left once the comment is cut: the default, as for an absent key.
		CHECK(!parse_switch("0\t; tab", true) && !parse_switch("\toff  \t;private", true) && parse_switch("1 ; x ; y", false) && !parse_switch("No;", true));
		CHECK(parse_switch("; only a comment", true) && !parse_switch("; only a comment", false) && parse_switch("  ;", true) && parse_switch("2 ; odd", true));
		{
			// What GetPrivateProfileString hands over for the README's lines: the value with its comment.
			const auto ini = std::filesystem::temp_directory_path() / ("xml2_test-discord-" + std::to_string(GetCurrentProcessId()) + ".ini");
			{
				std::ofstream out(ini, std::ios::binary);
				out << "[Discord]\r\nEnabled=0        ; no presence at all (0, false, no or off; anything else, or no key: on)\r\nShowZone = off\t; private\r\n"
				       "ShowParty= ; nothing\r\nLargeImage=none   ; no art\r\n";
			}
			const auto raw = [&](const char* key)
			{
				char value[512]{};
				GetPrivateProfileStringA("Discord", key, "\x7f", value, static_cast<DWORD>(std::size(value)), ini.string().c_str());
				return std::string(value);
			};
			CHECK(raw("Enabled").find("; no presence") != std::string::npos && !parse_switch(raw("Enabled"), true));
			CHECK(!parse_switch(raw("ShowZone"), true) && raw("ShowParty") == "; nothing" && parse_switch(raw("ShowParty"), true));
			CHECK(choose_images(raw("LargeImage"), std::nullopt).large.empty());
			std::error_code ignored;
			std::filesystem::remove(ini, ignored);
		}

		// Which game: [Discord] Game first, then the port's own clues.
		CHECK(choose_game({"xml1"}).which == game::xml1 && choose_game({"XML2 ; forced", true}).which == game::xml2 && choose_game({"xml2", true}).why == "[Discord] Game=xml2");
		const auto odd = choose_game({"xml3", true});
		CHECK(odd.which == game::xml1 && odd.why == "Scripts\\x1 is here" && odd.note == "[Discord] Game=xml3 isn't xml1 or xml2 - ignored");
		CHECK(choose_game({"", false, "x1/menus/postgame   ; after the credits"}).which == game::xml1);
		CHECK(choose_game({"", false, "", "X-Men Legends"}).which == game::xml1 && choose_game({"", false, "", "X-Men Legends 2"}).which == game::xml2);
		const auto plain = choose_game({});
		CHECK(plain.which == game::xml2 && plain.note.empty() && plain.why == "no X-Men Legends I port here");
		CHECK(client_id_for(game::xml1) == "1554317674606235738" && client_id_for(game::xml2) == "1554317812661493791");
		CHECK(title_of(game::xml1) == "X-Men Legends" && title_of(game::xml2) == "X-Men Legends II");
		CHECK(valid_client_id("1554317812661493791") && !valid_client_id("15543178126614937x1") && !valid_client_id("12345") && !valid_client_id(""));
		CHECK(valid_asset("xml2_logo") && valid_asset("https://example.org/a.png") && !valid_asset("two words") && !valid_asset("a\"b") && !valid_asset(""));

		// The art: the logo and each mode's badge unless LargeImage / SmallImage say otherwise.
		const auto art = choose_images(std::nullopt, std::nullopt);
		CHECK(art.large == "logo" && !art.badge && art.note.empty());
		// An empty value is the same as no key (the fix's one ini rule): the logo, the mode's badges.
		CHECK(choose_images("", std::nullopt).large == "logo" && choose_images("   ; later", std::nullopt).large == "logo" && !choose_images(std::nullopt, "").badge);
		CHECK(choose_images("  none ; no art", std::nullopt).large.empty() && choose_images("NONE", "online").badge == "");
		CHECK(choose_images("my_logo ; mine", std::nullopt).large == "my_logo" && choose_images(std::nullopt, "badge").badge == "badge");
		CHECK(choose_images(std::nullopt, "none").badge == "" && choose_images("logo", "None").large == "logo" && choose_images("logo", "None").badge == "");
		const auto odd_art = choose_images("two words", "a\"b");
		CHECK(odd_art.large == "logo" && !odd_art.badge && odd_art.note.find("LargeImage isn't an asset key") != std::string::npos && odd_art.note.find("SmallImage") != std::string::npos);

		// Text: the game's Windows-1252 as UTF-8, JSON escapes, Discord's 128 characters.
		CHECK(utf8_from_game("Queen's Lair") == "Queen's Lair" && utf8_from_game("Caf\xe9") == "Caf\xC3\xA9" && utf8_from_game("It\x92s") == "It\xE2\x80\x99s");
		CHECK(utf8_from_game("a\x01\nb\x7f") == "ab" && utf8_from_game("\x81") == "?");
		// Every byte of 0x80 and up: two or three bytes of UTF-8, never lost, never invalid.
		std::string high_bytes;
		for (int b = 0x80; b <= 0xff; ++b) high_bytes += static_cast<char>(b);
		const auto high_utf8 = utf8_from_game(high_bytes);
		CHECK(characters(high_utf8) == 128 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, high_utf8.data(), static_cast<int>(high_utf8.size()), nullptr, 0) == 128);
		CHECK(utf8_from_game("Monta\xF1" "a \xC0 \xFF \x80") == "Monta\xC3\xB1" "a \xC3\x80 \xC3\xBF \xE2\x82\xAC");
		CHECK(lower_game('A') == 'a' && lower_game('\xC9') == '\xE9' && lower_game('\xD7') == '\xD7' && lower_game('\x8A') == '\x9A' && lower_game('\x9F') == '\xFF' && lower_game('1') == '1');
		CHECK(upper_game('a') == 'A' && upper_game('\xE9') == '\xC9' && upper_game('\xF7') == '\xF7' && upper_game('\x9C') == '\x8C' && upper_game('\xFF') == '\x9F');
		CHECK(json_escape("a\"b\\c\n\x01") == "a\\\"b\\\\c\\n\\u0001" && json_escape("Act 1 \xC2\xB7 X") == "Act 1 \xC2\xB7 X");
		CHECK(clip("short") == "short" && characters(clip(std::string(130, 'a'))) == 128 && clip(std::string(130, 'a')).ends_with("a\xE2\x80\xA6"));
		std::string accents;
		for (int i = 0; i < 130; ++i) accents += "\xC3\xA9";
		CHECK(characters(clip(accents)) == 128 && clip(accents).substr(0, 4) == "\xC3\xA9\xC3\xA9" && clip(accents, 3) == "\xC3\xA9\xC3\xA9\xE2\x80\xA6");

		// Where: the savename, else the path's words; the act when there is one.
		CHECK(pretty_zone("mansion/man1a/mansion1a_1") == "Mansion" && pretty_zone("mansion/man3/danger_room") == "Danger Room");
		CHECK(pretty_zone("mansion/man7/status_meeting") == "Status Meeting" && pretty_zone("act1/sanctuary/sanctuary1") == "Sanctuary" && pretty_zone("xjet/123") == "Xjet");
		CHECK(pretty_zone("") == "" && pretty_zone("1/2") == "");
		CHECK(pretty_zone("mods/monta\xF1" "a1") == "Monta\xC3\xB1" "a" && pretty_zone("maps/\xE9" "cole_\xC9T\xC9") == "\xC3\x89" "cole \xC3\x89t\xC3\xA9");
		// A zone path's torn read has a control character; the game's code page doesn't make one.
		CHECK(game_reader<fake_memory>::valid_path("mods/monta\xF1" "a1") && game_reader<fake_memory>::valid_path("\xE9t\xE9") && !game_reader<fake_memory>::valid_path("nyc/al\x01son") &&
		      !game_reader<fake_memory>::valid_path("a\x7f"));
		CHECK(zone_line(1, "East Manhattan", "nyc/alison/nyc1_1_3") == "Act 1 \xC2\xB7 East Manhattan" && zone_line(0, "", "mansion/man1a/mansion1a_1") == "Mansion");
		CHECK(zone_line(4, "", "") == "Act 4" && zone_line(0, "Prison Outpost", "act0/tutorial/tutorial1") == "Prison Outpost");
		CHECK(is_menu_zone("") && is_menu_zone("menu/main_back") && is_menu_zone("Menu/Main_Back") && !is_menu_zone("act1/sanctuary/sanctuary1"));

		// Who: one or two heroes with their levels, three or four shortened.
		CHECK(party_text({}) == "" && party_text({{"Wolverine", 3}}) == "Wolverine Lv 3" && party_text({{"Wolverine", 0}}) == "Wolverine");
		CHECK(party_text({{"Wolverine", 3}, {"Cyclops", 1}}) == "Wolverine Lv 3 \xC2\xB7 Cyclops Lv 1");
		CHECK(party_text({{"Wolverine", 12}, {"Storm", 11}, {"Cyclops", 11}, {"Iceman", 10}}) == "Wolverine, Storm +2 \xC2\xB7 Lv 10-12");
		CHECK(party_text({{"Wolverine", 7}, {"Storm", 7}, {"Rogue", 7}}) == "Wolverine, Storm +1 \xC2\xB7 Lv 7" && party_text({{"A", 0}, {"B", 0}, {"C", 0}}) == "A, B +1");

		// The activities.
		const display all;
		snapshot menu;
		menu.zone = "menu/main_back";
		menu.party = {{"Wolverine", 3}};
		CHECK((build(menu, all) == activity{"In the menus", "", 0, 0, "menu", "In the menus"} && build({}, all) == in_the_menus()));
		snapshot port;
		port.zone = "nyc/alison/nyc1_1_3";
		port.zone_title = "East Manhattan";
		port.act = 1;
		port.party = {{"Wolverine", 3}, {"Cyclops", 1}};
		const auto port_activity = build(port, all);
		CHECK(port_activity && port_activity->details == "Act 1 \xC2\xB7 East Manhattan" && port_activity->state == "Wolverine Lv 3 \xC2\xB7 Cyclops Lv 1" &&
		      port_activity->small_image.empty() && port_activity->small_text.empty() && port_activity->party_max == 0); // plain play: no badge
		snapshot hub;
		hub.zone = "act1/sanctuary/sanctuary1";
		hub.zone_title = "Sanctuary";
		hub.act = 1;
		hub.party = {{"Wolverine", 12}, {"Storm", 11}, {"Cyclops", 11}, {"Iceman", 10}};
		CHECK((build(hub, all) == activity{"Act 1 \xC2\xB7 Sanctuary", "Wolverine, Storm +2 \xC2\xB7 Lv 10-12", 0, 0, "", ""}));
		snapshot online = hub;
		online.session = true;
		online.hosting = true;
		online.players = 2;
		online.max_players = 4;
		CHECK((build(online, all) == activity{"Act 1 \xC2\xB7 Sanctuary", "Online co-op \xC2\xB7 hosting", 2, 4, "online", "Online co-op"}));
		online.hosting = false;
		CHECK(build(online, all)->state == "Online co-op \xC2\xB7 joined");
		snapshot lobby;
		lobby.zone = "menu/main_back";
		lobby.session = true;
		lobby.players = 3;
		lobby.max_players = 4;
		CHECK((build(lobby, all) == activity{"Online lobby", "Joined", 3, 4, "online", "Online co-op"}));
		snapshot browsing = menu;
		browsing.online_menus = true;
		CHECK((build(browsing, all) == activity{"In the menus", "Play Online", 0, 0, "menu", "In the menus"}));
		snapshot danger = port;
		danger.zone = "arena/arena_dr";
		danger.danger_room = true;
		danger.course = "Setting 101 - Hidden Goods";
		CHECK(build(danger, all)->details == "Danger Room \xC2\xB7 Setting 101 - Hidden Goods" && build(danger, all)->state == port_activity->state);
		CHECK(build(danger, all)->small_image == "dangerroom" && build(danger, all)->small_text == "Danger Room");
		danger.course.clear();
		CHECK(build(danger, all)->details == "Danger Room");
		snapshot movie = port;
		movie.movie = true;
		CHECK((build(movie, all) == activity{"Watching a cutscene", "Act 1 \xC2\xB7 East Manhattan", 0, 0, "cutscene", "Watching a cutscene"}));
		snapshot intro = menu;
		intro.movie = true;
		CHECK(build(intro, all)->details == "Watching a cutscene" && build(intro, all)->state.empty() && build(intro, all)->small_image == "cutscene");
		snapshot loading = port;
		loading.loading = true;
		CHECK(!build(loading, all) && build(snapshot{"menu/main_back", "", 0, true}, all) == in_the_menus()); // a load holds the last activity, but the menus are the menus
		// ShowZone=0 and ShowParty=0.
		CHECK(build(port, {false, true})->details == "Playing" && build(port, {true, false})->state.empty() && build(port, {true, false})->small_text.empty());
		danger.course = "Setting 101";
		CHECK(build(danger, {false, true})->details == "Danger Room" && build(movie, {false, true})->state.empty());
		CHECK((build(online, {false, false}) == activity{"Playing", "Online co-op \xC2\xB7 joined", 2, 4, "online", "Online co-op"}));
		snapshot unnamed = port;
		unnamed.zone = "mansion/man1a/mansion1a_1";
		unnamed.zone_title.clear();
		CHECK(build(unnamed, all)->details == "Act 1 \xC2\xB7 Mansion");
		snapshot long_title = port;
		long_title.zone_title = std::string(200, 'x');
		CHECK(characters(build(long_title, all)->details) == 128);
		CHECK(describe(*build(online, all)) == "Act 1 \xC2\xB7 Sanctuary | Online co-op \xC2\xB7 joined (2 of 4)" && describe(activity{}) == "(no details)");
		// The team menu: a hero it seats shows once the menu closes (Accept), not while it's up.
		party_hold hold;
		snapshot team = port;
		hold.apply(team);
		CHECK((team.party == std::vector<hero>{{"Wolverine", 3}, {"Cyclops", 1}}));
		team.menu = true;
		team.party = {{"Wolverine", 3}, {"Cyclops", 1}, {"Jean Grey", 9}};
		hold.apply(team);
		CHECK((team.party == std::vector<hero>{{"Wolverine", 3}, {"Cyclops", 1}} && build(team, all) == port_activity));
		team.party = {{"Wolverine", 3}}; // another pick, or Back: still the party from before the menu
		hold.apply(team);
		CHECK(team.party.size() == 2);
		team.menu = false;
		team.party = {{"Wolverine", 3}, {"Jean Grey", 9}};
		hold.apply(team);
		CHECK((team.party == std::vector<hero>{{"Wolverine", 3}, {"Jean Grey", 9}})); // accepted: it shows
		snapshot elsewhere = team; // a menu up in a zone whose party wasn't read yet: as read
		elsewhere.zone = "act1/sanctuary/sanctuary1";
		elsewhere.menu = true;
		elsewhere.party = {{"Storm", 11}};
		hold.apply(elsewhere);
		CHECK((elsewhere.party == std::vector<hero>{{"Storm", 11}}));
		snapshot main_menu = menu; // the main menu forgets it
		hold.apply(main_menu);
		team.menu = true;
		team.party = {{"Rogue", 7}};
		hold.apply(team);
		CHECK((team.party == std::vector<hero>{{"Rogue", 7}}));
		party_hold loads;
		snapshot during = port;
		loads.apply(during);
		during.loading = true; // a load reseats the party as it goes: not what stands for a menu after it
		during.party = {{"Storm", 11}};
		loads.apply(during);
		during.loading = false;
		during.menu = true;
		during.party = {{"Rogue", 7}};
		loads.apply(during);
		CHECK((during.party == std::vector<hero>{{"Wolverine", 3}, {"Cyclops", 1}}));

		// Pacing: one update per 5 s, and an activity counts once read twice in a row.
		gate pacing;
		CHECK(pacing.may_send(0));
		pacing.sent(1000);
		CHECK(!pacing.may_send(1000) && !pacing.may_send(5999) && pacing.may_send(6000));
		pacing.reset();
		CHECK(pacing.may_send(1001));
		settle settled;
		const activity a{"A place", "Somebody", 0, 0, ""};
		const activity b{"Another place", "Somebody", 0, 0, ""};
		CHECK(!settled.seen(a) && settled.seen(a) == a && settled.seen(a) == a);
		CHECK(!settled.seen(b) && !settled.seen(std::nullopt) && !settled.seen(b) && settled.seen(b) == b);

		// Frames.
		CHECK(encode(op_frame, "{}") == std::string("\x01\0\0\0\x02\0\0\0{}", 10) && encode(op_handshake, "").size() == 8);
		const auto two = encode(op_frame, "{\"evt\":\"READY\"}") + encode(op_ping, "{\"n\":1}");
		frame_reader frames;
		frames.feed(two.substr(0, 5));
		CHECK(!frames.next());
		frames.feed(two.substr(5, 20));
		const auto first = frames.next();
		CHECK(first && first->op == op_frame && first->json == "{\"evt\":\"READY\"}" && !frames.next());
		frames.feed(two.substr(25));
		const auto second = frames.next();
		CHECK(second && second->op == op_ping && second->json == "{\"n\":1}" && !frames.next() && !frames.broken());
		frames.feed(std::string("\x01\0\0\0\xff\xff\xff\x7f", 8));
		CHECK(!frames.next() && frames.broken());
		// Many frames in one read come out whole and in order, however the read was cut.
		std::string many;
		for (int i = 0; i < 100; ++i) many += encode(op_frame, std::to_string(i));
		frame_reader lots;
		int in_order = 0;
		lots.feed(many.substr(0, many.size() - 3));
		while (const auto m = lots.next()) in_order += m->json == std::to_string(in_order);
		lots.feed(many.substr(many.size() - 3));
		while (const auto m = lots.next()) in_order += m->json == std::to_string(in_order);
		CHECK(in_order == 100 && !lots.broken());

		// The inbox holds the pipe to what Discord sends: 64 frames waiting at once are fine, the 65th is a
		// flood - everything is dropped, and nothing more is taken or handed out until clear() (the next
		// connection). The connection reads at most 64 KiB a poll, so a flood is caught in the first one.
		CHECK(max_read_per_poll == 64 * 1024 && max_queued == 64 && max_frame == 64 * 1024);
		inbox box;
		std::string sixty_four;
		for (std::size_t i = 0; i < max_queued; ++i) sixty_four += encode(op_frame, "");
		CHECK(box.take(sixty_four) && box.waiting() == 64 && !box.problem());
		CHECK(!box.take(encode(op_ping, "{}")) && box.waiting() == 0 && box.problem() && std::string(box.problem()) == "more than 64 frames at once" && !box.pop());
		CHECK(!box.take(encode(op_frame, "{}")) && !box.pop());
		box.clear();
		CHECK(!box.problem() && box.take(encode(op_frame, "{\"evt\":\"READY\"}") + encode(op_close, "{}")) && box.pop()->op == op_frame && box.pop()->op == op_close && !box.pop());
		std::string flood;
		for (std::size_t i = 0; i < max_read_per_poll / 8; ++i) flood += encode(op_frame, ""); // one poll's worth of empty frames
		CHECK(!box.take(flood) && box.waiting() == 0);
		box.clear();
		bool steady = true; // popped as they come, a steady stream never fills it
		for (int i = 0; i < 1000 && steady; ++i) steady = box.take(encode(op_ping, "{}")) && box.pop().has_value();
		CHECK(steady && !box.problem());
		CHECK(!box.take(std::string("\x01\0\0\0\x01\0\x01\0", 8)) && std::string(box.problem()) == "a frame longer than 64 KiB");
		box.clear();
		CHECK(box.take(encode(op_frame, std::string(max_frame, 'x'))) && box.pop()->json.size() == max_frame); // the longest there may be

		// JSON.
		CHECK(handshake_json("123") == "{\"v\":1,\"client_id\":\"123\"}");
		extras x;
		x.start = 1790000000;
		CHECK(set_activity_json(1234, *port_activity, x, 7) ==
		      "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":1234,\"activity\":{\"details\":\"Act 1 \xC2\xB7 East Manhattan\",\"state\":\"Wolverine Lv 3 \xC2\xB7 Cyclops Lv 1\","
		      "\"timestamps\":{\"start\":1790000000}}},\"nonce\":\"7\"}");
		// The art: the logo with the game's name, and each mode's badge with the mode.
		x.large_image = "logo";
		x.large_text = "X-Men Legends II";
		x.party_id = "xml2fix-0123456789abcdef";
		const auto assets_of = [&](const activity& a)
		{
			const auto json = set_activity_json(1234, a, x, 8);
			const auto at = json.find("\"assets\":{");
			return at == std::string::npos ? std::string("(none)") : json.substr(at + 10, json.find('}', at) - at - 10);
		};
		const std::string logo = "\"large_image\":\"logo\",\"large_text\":\"X-Men Legends II\"";
		CHECK(assets_of(in_the_menus()) == logo + ",\"small_image\":\"menu\",\"small_text\":\"In the menus\"");
		CHECK(assets_of(*build(browsing, all)) == logo + ",\"small_image\":\"menu\",\"small_text\":\"In the menus\"");
		CHECK(assets_of(*build(movie, all)) == logo + ",\"small_image\":\"cutscene\",\"small_text\":\"Watching a cutscene\"");
		CHECK(assets_of(*build(intro, all)) == logo + ",\"small_image\":\"cutscene\",\"small_text\":\"Watching a cutscene\"");
		CHECK(assets_of(*build(danger, all)) == logo + ",\"small_image\":\"dangerroom\",\"small_text\":\"Danger Room\"");
		CHECK(assets_of(*build(lobby, all)) == logo + ",\"small_image\":\"online\",\"small_text\":\"Online co-op\"");
		CHECK(assets_of(*build(online, all)) == logo + ",\"small_image\":\"online\",\"small_text\":\"Online co-op\"");
		CHECK(assets_of(*port_activity) == logo && assets_of(*build(hub, all)) == logo && assets_of(activity{}) == logo); // plain play, or a build it can't read: the logo alone
		const auto full = set_activity_json(1234, *build(online, all), x, 8);
		CHECK(full == "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":1234,\"activity\":{\"details\":\"Act 1 \xC2\xB7 Sanctuary\",\"state\":\"Online co-op \xC2\xB7 joined\","
		              "\"timestamps\":{\"start\":1790000000},\"assets\":{" + logo + ",\"small_image\":\"online\",\"small_text\":\"Online co-op\"},"
		              "\"party\":{\"id\":\"xml2fix-0123456789abcdef\",\"size\":[2,4]}}},\"nonce\":\"8\"}");
		// SmallImage=key: that key for every badge (still none in plain play); SmallImage=none: no badges;
		// LargeImage=none: no images at all.
		x.small_image = "coop";
		CHECK(assets_of(*build(online, all)) == logo + ",\"small_image\":\"coop\",\"small_text\":\"Online co-op\"" && assets_of(*port_activity) == logo);
		x.small_image = "";
		CHECK(assets_of(in_the_menus()) == logo && assets_of(*build(danger, all)) == logo);
		x.large_image.clear();
		x.small_image.reset();
		CHECK(assets_of(in_the_menus()) == "(none)" && set_activity_json(1234, *build(online, all), x, 8).find("\"party\"") != std::string::npos);

		// The online party's id: random, nothing about this PC (no process id, no uptime).
		CHECK(party_id_of(0x1234, 0xabcdef) == "xml2fix-0000123400abcdef" && party_id_of(0xffffffff, 0) == "xml2fix-ffffffff00000000");
		const auto id_shape = [](const std::string& id)
		{
			return id.size() == 24 && id.starts_with("xml2fix-") && std::all_of(id.begin() + 8, id.end(), [](const char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
		};
		const auto party_a = random_party_id();
		const auto party_b = random_party_id();
		char pid_hex[16]{};
		std::snprintf(pid_hex, sizeof(pid_hex), "%08lx", GetCurrentProcessId());
		CHECK(id_shape(party_a) && id_shape(party_b) && party_a != party_b && party_a.find(pid_hex) == std::string::npos);
		CHECK(set_activity_json(1, activity{"A", "", 0, 0, ""}, extras{}, 1) == "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":1,\"activity\":{}},\"nonce\":\"1\"}"); // under two characters: left out
		CHECK(set_activity_json(1, activity{"Quote \"here\"", "", 0, 0, ""}, extras{}, 2).find("\"details\":\"Quote \\\"here\\\"\"") != std::string::npos);
		CHECK(clear_activity_json(1234, 9) == "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":1234},\"nonce\":\"9\"}");
		const std::string ready = "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1,\"config\":{\"api_endpoint\":\"//discord.com/api\"}},\"evt\":\"READY\",\"nonce\":null}";
		CHECK(json_value(ready, "evt") == "READY" && json_value(ready, "nonce") == "null" && json_value(ready, "v") == "1" && !json_value(ready, "code"));
		const std::string refused = "{\"code\":4000,\"message\":\"Invalid Client ID\"}";
		CHECK(json_value(refused, "code") == "4000" && json_value(refused, "message") == "Invalid Client ID");
		CHECK(json_value("{\"message\":\"a \\\"b\\\" \\u00e9\\/\"}", "message") == "a \"b\" \xC3\xA9/" && json_value("{\"x\":\"evt\",\"evt\":\"ERROR\"}", "evt") == "ERROR");
		CHECK(!json_value("{\"message\":\"unterminated", "message") && !json_value("{}", "evt"));

		// Reading the game: a party, its levels and names as the game shows them, the act and the savename.
		fake_game fake;
		auto state = fake.read();
		CHECK(state && state->zone == "nyc/alison/nyc1_1_3" && state->zone_title == "East Manhattan" && state->act == 1 && !state->loading && !state->movie);
		CHECK((state && state->party == std::vector<hero>{{"Wolverine", 3}, {"Cyclops", 1}} && !state->danger_room && !state->session && !state->online_menus));
		CHECK(state && build(*state, all) == port_activity);
		fake.party({"phoenix", "", "magneto", "wolverine"}); // the charactername; the placeholder isn't a hero
		state = fake.read();
		CHECK((state && state->party == std::vector<hero>{{"Jean Grey", 9}, {"Wolverine", 3}}));
		fake.party({"nobody"});
		state = fake.read();
		CHECK(state && state->party.empty() && build(*state, all)->state.empty());
		fake.party({"wolverine", "cyclops"});
		// The Danger Room: a course and its title; free play; off.
		fake.memory.u32(courses + 2 * course_size + course_title, fake.intern("Setting 101 - Hidden Goods"));
		fake.memory.u8(danger_room_state, 2);
		state = fake.read();
		CHECK(state && state->danger_room && state->course == "Setting 101 - Hidden Goods");
		fake.memory.u8(danger_room_state, 0xfe);
		state = fake.read();
		CHECK(state && state->danger_room && state->course.empty());
		fake.memory.u8(danger_room_state, danger_room_off);
		// Online: Play Online, then a hosted game with two players of four.
		fake.memory.u32(network + network_mode, 1);
		state = fake.read();
		CHECK(state && state->online_menus && !state->session);
		fake.memory.u32(session, session_vtable);
		state = fake.read();
		CHECK(state && !state->session); // the session is built, but no game is set up
		fake.memory.u8(session + session_active, 1);
		fake.memory.u8(session + session_hosting, 1);
		fake.memory.u8(session + session_players, 2);
		fake.memory.u8(session + session_max, 4);
		state = fake.read();
		CHECK(state && state->session && state->hosting && state->players == 2 && state->max_players == 4);
		fake.memory.u8(session + session_active, 0);
		fake.memory.u32(network + network_mode, 0);
		// A load, a movie, the loading screen.
		fake.memory.u32(zones + zone_loading, 2);
		state = fake.read();
		CHECK(state && state->loading && !build(*state, all));
		fake.memory.u32(zones + zone_loading, 0);
		fake.memory.u8(fake_game::manager + frame_rate_rules::menu_flags, frame_rate_rules::menu_movie_bit);
		state = fake.read();
		CHECK(state && state->movie && !state->loading);
		fake.memory.u8(fake_game::manager + frame_rate_rules::menu_flags, 0);
		fake.memory.block(0x30000000, 4);
		fake.memory.u32(0x30000000, frame_rate_rules::loading_menu_vtable);
		fake.memory.u32(fake_game::manager + frame_rate_rules::menu_current, 0x30000000);
		state = fake.read();
		CHECK(state && state->loading);
		fake.memory.u32(fake_game::manager + frame_rate_rules::menu_current, 0);
		// A menu up, as the game tests it (a stack, its first entry, a current menu or one about to open).
		fake.memory.u32(fake_game::manager + frame_rate_rules::menu_stack_count, 1);
		fake.memory.u32(fake_game::manager + frame_rate_rules::menu_stack_first, 0x30000000);
		fake.memory.u32(fake_game::manager + frame_rate_rules::menu_current, 0x30000010); // not the loading screen
		state = fake.read();
		CHECK(state && state->menu && !state->loading);
		fake.memory.u32(fake_game::manager + frame_rate_rules::menu_current, 0);
		fake.memory.u8(fake_game::manager + frame_rate_rules::menu_pending, 't');
		state = fake.read();
		CHECK(state && state->menu);
		fake.memory.u8(fake_game::manager + frame_rate_rules::menu_pending, 0);
		state = fake.read();
		CHECK(state && !state->menu);
		{
			// The team menu seats Jean Grey before Accept: the presence keeps Cyclops until it closes.
			party_hold team_menu;
			state = fake.read();
			team_menu.apply(*state);
			fake.memory.u32(fake_game::manager + frame_rate_rules::menu_current, 0x30000010);
			fake.party({"wolverine", "phoenix"});
			state = fake.read();
			if (state) team_menu.apply(*state);
			CHECK((state && state->menu && state->party == std::vector<hero>{{"Wolverine", 3}, {"Cyclops", 1}}));
			fake.memory.u32(fake_game::manager + frame_rate_rules::menu_current, 0);
			state = fake.read();
			if (state) team_menu.apply(*state);
			CHECK((state && !state->menu && state->party == std::vector<hero>{{"Wolverine", 3}, {"Jean Grey", 9}}));
		}
		fake.memory.u32(fake_game::manager + frame_rate_rules::menu_stack_count, 0);
		fake.party({"wolverine", "cyclops"});
		// The game's code page (Windows-1252) in a mod's zone path, a savename and a hero's name: read, never
		// a failed read, and shown as UTF-8.
		fake.zone("mods/monta\xF1" "a1", "");
		state = fake.read();
		CHECK(state && build(*state, all) && build(*state, all)->details == "Act 1 \xC2\xB7 Monta\xC3\xB1" "a");
		fake.zone("mods/monta\xF1" "a1", "La Monta\xF1" "a");
		fake.memory.u32(fake_game::registry + registry_entries + 9 * entry_size + entry_character, fake.intern("F\xE9nix"));
		fake.party({"phoenix"});
		state = fake.read();
		CHECK((state && state->zone_title == "La Monta\xC3\xB1" "a" && state->party == std::vector<hero>{{"F\xC3\xA9nix", 9}}));
		const auto spanish = state ? build(*state, all) : std::nullopt;
		CHECK(spanish && spanish->details == "Act 1 \xC2\xB7 La Monta\xC3\xB1" "a" && spanish->state == "F\xC3\xA9nix Lv 9");
		CHECK(spanish && set_activity_json(1, *spanish, extras{}, 1).find("\"details\":\"Act 1 \xC2\xB7 La Monta\xC3\xB1" "a\",\"state\":\"F\xC3\xA9nix Lv 9\"") != std::string::npos);
		fake.party({"wolverine", "cyclops"});
		// The zone's text torn mid-copy, an unknown zone manager, none yet, and the registry unreadable.
		fake.zone("nyc/al\x01son", "East Manhattan");
		CHECK(!fake.read());
		fake.zone("menu/main_back", "");
		state = fake.read();
		CHECK(state && build(*state, all) == in_the_menus());
		fake.memory.u32(registry_cell, 0x40000000);
		fake.zone("nyc/alison/nyc1_1_3", "East Manhattan");
		state = fake.read();
		CHECK(state && state->party.empty() && state->act == 1);
		fake.memory.u32(zones, 0x12345678);
		CHECK(!fake.read());
		fake.memory.u32(zones, 0);
		state = fake.read();
		CHECK(state && state->zone.empty() && build(*state, all) == in_the_menus());

		// The guards: well formed, one per address; against XMen2.exe, every byte, and the displacements
		// the reads use are the ones in the game's own code - each of them inside a guard, so the game
		// checks it too (the +0x421 host flag's getter and setter among them).
		std::set<DWORD> addresses;
		bool guards_ok = true;
		for (const auto& g : guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
		CHECK(guards_ok);
		struct operand
		{
			DWORD va;
			std::size_t size;
			std::uint32_t value;
		};
		const operand operands[] = {
			{0x4849ba + 1, 4, zones}, {0x483c1f + 2, 4, zones_vtable}, {0x483f30 + 2, 4, zone_path}, {0x483e90 + 2, 4, zone_loading},
			{0x484f60 + 2, 4, zone_savename}, {0x4850d5 + 1, 1, zone_text_size}, {0x46dd0a + 1, 4, game_object}, {0x468e6d + 2, 4, game_vtable},
			{0x469c40 + 3, 4, game_act}, {0x46c883 + 3, 1, game_party}, {0x60218e + 1, 4, pool}, {0x425bdd + 3, 4, pool_text},
			{0x44b8fe + 1, 4, registry_cell}, {0x44b541 + 2, 4, registry_vtable}, {0x44b6b0 + 2, 4, registry_hero_count}, {0x44b6e4 + 4, 4, registry_heroes},
			{0x44b708 + 3, 4, registry_entries}, {0x44b728 + 3, 4, registry_entries + entry_character}, {0x44b788 + 4, 4, registry_entries + entry_team},
			{0x449d45 + 2, 4, registry_stats_mask - 4}, {0x449d4d + 2, 4, stats_size}, {0x4b87c5 + 4, 1, stats_level}, {0x4c87a0 + 2, 4, danger_room_state},
			{0x4c9c18 + 1, 4, courses}, {0x4c9c15 + 2, 1, course_size}, {0x4d450c + 2, 1, course_title}, {0x60b23a + 1, 4, network},
			{0x60a770 + 6, 4, network_mode}, {0x612c0a + 1, 4, session}, {0x612136 + 2, 4, session_vtable}, {0x610d20 + 2, 4, session_active},
			{0x610d10 + 2, 4, session_hosting}, {0x611059 + 2, 4, session_hosting}, {0x615916 + 2, 4, session_max}, {0x61591c + 2, 4, session_players},
		};
		const auto guarded = [&](const operand& o)
		{
			const auto inside = [&](const guard& g) { return o.va >= g.va && o.va + o.size <= g.va + g.hex.size() / 2; };
			return std::any_of(guards.begin(), guards.end(), inside) || std::any_of(frame_rate_rules::screen_guards.begin(), frame_rate_rules::screen_guards.end(), inside);
		};
		std::string unguarded;
		for (const auto& o : operands)
		{
			if (!guarded(o))
			{
				char text[16]{};
				std::snprintf(text, sizeof(text), " 0x%06lx", o.va);
				unguarded += text;
			}
		}
		if (!unguarded.empty()) std::printf("  info  operands outside every guard:%s\n", unguarded.c_str());
		CHECK(unguarded.empty());
		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the presence's bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - limits_rules::image_base); };
		const guard* mismatch = discord_rules::first_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr);
		std::string wrong;
		for (const auto& o : operands)
		{
			if (operand_at(at(o.va), o.size) != o.value)
			{
				char text[16]{};
				std::snprintf(text, sizeof(text), " 0x%06lx", o.va);
				wrong += text;
			}
		}
		if (!wrong.empty()) std::printf("  info  operands that aren't the table's:%s\n", wrong.c_str());
		CHECK(wrong.empty());
		// The Danger Room's state and the session are in the exe's zero-filled data: 0 until the game sets them.
		CHECK(operand_at(at(danger_room_state), 4) == 0 && operand_at(at(session), 4) == 0 && operand_at(at(network + network_mode), 4) == 0);
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// A Discord of the test's own: a pipe server at \\.\pipe\xml2_test-discord-<pid>-<tag>-0 that takes
	// the handshake and then runs `script` on a thread of its own, for discord_ipc::connection to talk to
	// as to Discord - without the Discord on this PC ever seeing it.
	class fake_discord
	{
	public:
		std::wstring pipes;
		std::string handshake;                   // the handshake's JSON, as it came
		std::vector<discord_rules::message> got; // what the script read after it

		template <typename Script>
		fake_discord(const std::wstring& tag, Script script)
		{
			pipes = L"\\\\.\\pipe\\xml2_test-discord-" + std::to_wstring(GetCurrentProcessId()) + L"-" + tag + L"-";
			server_ = CreateNamedPipeW((pipes + L"0").c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
			                           64 * 1024, 64 * 1024, 0, nullptr);
			go_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			thread_ = std::thread([this, script]
			{
				if (!ConnectNamedPipe(server_, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) return;
				const auto first = read();
				if (!first) return;
				handshake = first->json;
				script(*this);
			});
		}
		~fake_discord()
		{
			finish();
			CloseHandle(go_);
		}
		fake_discord(const fake_discord&) = delete;
		fake_discord& operator=(const fake_discord&) = delete;

		// Ends the script (cutting short a read or write it's stuck in) and hangs up.
		void finish()
		{
			if (thread_.joinable())
			{
				stopping_ = true;
				while (WaitForSingleObject(thread_.native_handle(), 50) == WAIT_TIMEOUT)
				{
					SetEvent(go_);
					CancelSynchronousIo(thread_.native_handle());
				}
				thread_.join();
			}
			hang_up();
		}
		// Waits up to `timeout_ms` for the script to end by itself.
		bool done(const DWORD timeout_ms)
		{
			return WaitForSingleObject(thread_.native_handle(), timeout_ms) == WAIT_OBJECT_0;
		}
		void go()
		{
			SetEvent(go_);
		}

		// For the script.
		bool stopping() const
		{
			return stopping_;
		}
		void wait_for_go()
		{
			WaitForSingleObject(go_, INFINITE);
		}
		bool write(const std::string& bytes)
		{
			std::size_t total = 0;
			while (total < bytes.size())
			{
				DWORD done = 0;
				if (!WriteFile(server_, bytes.data() + total, static_cast<DWORD>(bytes.size() - total), &done, nullptr)) return false;
				total += done;
			}
			return true;
		}
		bool send(const std::uint32_t op, const std::string_view json)
		{
			return write(discord_rules::encode(op, json));
		}
		std::optional<discord_rules::message> read()
		{
			char header[8]{};
			if (!read_exactly(header, 8)) return std::nullopt;
			std::uint32_t op = 0;
			std::uint32_t length = 0;
			std::memcpy(&op, header, 4);
			std::memcpy(&length, header + 4, 4);
			if (length > discord_rules::max_frame) return std::nullopt;
			std::string json(length, '\0');
			if (length && !read_exactly(json.data(), length)) return std::nullopt;
			return discord_rules::message{op, json};
		}
		// Discord's end closes - its handle, as Discord does: what it wrote can still be read.
		void hang_up()
		{
			if (server_ != INVALID_HANDLE_VALUE)
			{
				CloseHandle(server_);
				server_ = INVALID_HANDLE_VALUE;
			}
		}

	private:
		HANDLE server_ = INVALID_HANDLE_VALUE;
		HANDLE go_ = nullptr;
		std::atomic<bool> stopping_{false};
		std::thread thread_;

		bool read_exactly(char* out, const DWORD size)
		{
			DWORD total = 0;
			while (total < size)
			{
				DWORD done = 0;
				if (!ReadFile(server_, out + total, size - total, &done, nullptr) || done == 0) return false;
				total += done;
			}
			return true;
		}
	};

	// The connection (discord_ipc.cpp) against the test's own Discord: READY, PING/PONG, Discord's CLOSE
	// and its reason, the handshake's deadline, a peer that floods the pipe, and quitting mid-handshake.
	void check_discord_pipe()
	{
		using namespace discord_rules;
		std::printf("discord: the pipe, against a Discord of the test's own\n");
		const std::string ready = "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\",\"nonce\":null}";
		const std::string refusal = "{\"code\":4000,\"message\":\"Invalid Client ID\"}";
		std::string empty_frames;
		for (int i = 0; i < 8192; ++i) empty_frames += encode(op_frame, ""); // 64 KiB of the smallest frames there are
		std::string name;
		std::string error;

		{
			discord_ipc::connection pipe;
			CHECK(!pipe.open("123", name, error, 100, L"\\\\.\\pipe\\xml2_test-nobody-") && error.starts_with("Discord isn't running"));
		}
		{
			// READY after a PING (answered with a PONG of its body); later Discord's CLOSE, and its end closes
			// right after, as Discord's does: the CLOSE and its reason are still handed out.
			fake_discord discord(L"ready", [&](fake_discord& d)
			{
				d.send(op_ping, "{\"n\":7}");
				d.send(op_frame, ready);
				if (const auto pong = d.read()) d.got.push_back(*pong);
				d.wait_for_go();
				if (d.stopping()) return;
				d.send(op_close, refusal);
				d.hang_up();
			});
			discord_ipc::connection pipe;
			const bool opened = pipe.open("1554317812661493791", name, error, 2000, discord.pipes);
			CHECK(opened && pipe.is_open() && name.starts_with("xml2_test-discord-") && name.ends_with("-ready-0"));
			discord.go();
			CHECK(discord.done(3000)); // it wrote its CLOSE and closed its end before the connection looked
			std::vector<message> frames;
			const bool received = pipe.receive(frames);
			CHECK(received && !pipe.is_open() && frames.size() == 1 && frames[0].op == op_close && json_value(frames[0].json, "code") == "4000" && pipe.problem().empty());
			std::vector<message> more;
			CHECK(!pipe.receive(more) && more.empty());
			discord.finish();
			CHECK(discord.handshake == "{\"v\":1,\"client_id\":\"1554317812661493791\"}");
			CHECK(discord.got.size() == 1 && discord.got[0].op == op_pong && discord.got[0].json == "{\"n\":7}");
		}
		{
			// Refused during the handshake (an unknown application): the log gets Discord's reason.
			fake_discord discord(L"refused", [&](fake_discord& d)
			{
				d.send(op_close, refusal);
				d.hang_up();
			});
			discord_ipc::connection pipe;
			CHECK(!pipe.open("123", name, error, 2000, discord.pipes) && error == "Discord refused the connection (4000: Invalid Client ID)" && !pipe.is_open());
		}
		{
			// Frames that keep coming but never READY: the deadline holds.
			fake_discord discord(L"deadline", [&](fake_discord& d)
			{
				while (!d.stopping() && d.send(op_frame, "{\"cmd\":\"DISPATCH\",\"evt\":\"SOMETHING_ELSE\"}")) Sleep(5);
			});
			discord_ipc::connection pipe;
			const DWORD start = GetTickCount();
			const bool opened = pipe.open("123", name, error, 300, discord.pipes);
			const DWORD took = GetTickCount() - start;
			std::printf("  info  a handshake that never ends gave up after %lu ms (deadline 300)\n", took);
			CHECK(!opened && error.starts_with("Discord didn't answer the handshake on ") && took >= 250 && took < 1500 && !pipe.is_open());
		}
		{
			// A peer that floods the pipe with valid frames from the start: dropped at once.
			fake_discord discord(L"flood", [&](fake_discord& d)
			{
				while (!d.stopping() && d.write(empty_frames)) {}
			});
			discord_ipc::connection pipe;
			const DWORD start = GetTickCount();
			const bool opened = pipe.open("123", name, error, 3000, discord.pipes);
			const DWORD took = GetTickCount() - start;
			std::printf("  info  a flood during the handshake: \"%s\" after %lu ms\n", error.c_str(), took);
			CHECK(!opened && error.find("sent more than 64 frames at once - that isn't Discord") != std::string::npos && took < 1500 && !pipe.is_open());
		}
		{
			// And one that floods it later: the poll after it reads at most 64 KiB and drops the connection.
			fake_discord discord(L"flood-later", [&](fake_discord& d)
			{
				d.send(op_frame, ready);
				d.wait_for_go();
				while (!d.stopping() && d.write(empty_frames)) {}
			});
			discord_ipc::connection pipe;
			CHECK(pipe.open("123", name, error, 2000, discord.pipes));
			discord.go();
			Sleep(100);
			std::vector<message> frames;
			bool still = true;
			const DWORD start = GetTickCount();
			while (still && GetTickCount() - start < 2000)
			{
				still = pipe.receive(frames);
				Sleep(10);
			}
			CHECK(!still && !pipe.is_open() && pipe.problem() == "sent more than 64 frames at once - that isn't Discord" && frames.size() <= max_queued);
		}
		{
			// The game quitting mid-handshake: open() gives up at once (and doesn't start one after).
			fake_discord discord(L"quiet", [&](fake_discord& d) { d.wait_for_go(); });
			const HANDLE quitting = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			discord_ipc::connection pipe;
			pipe.cancel_on(quitting);
			std::thread quit([&] { Sleep(100); SetEvent(quitting); });
			const DWORD start = GetTickCount();
			const bool opened = pipe.open("123", name, error, 5000, discord.pipes);
			const DWORD took = GetTickCount() - start;
			quit.join();
			CHECK(!opened && error == "the game is quitting" && took < 600 && !pipe.is_open());
			CHECK(!pipe.open("123", name, error, 5000, discord.pipes) && error == "the game is quitting");
			CloseHandle(quitting);
		}
	}

	// mods\load-order.txt, as the mod loader and Discord's X-Men Legends I port detection read it: a mod it
	// switches off, or a leftover .staging folder, doesn't count.
	void check_mod_order()
	{
		namespace fs = std::filesystem;
		std::printf("mods: the load order (mod_order.hpp)\n");
		const auto root = fs::temp_directory_path() / ("xml2_test-mods-" + std::to_string(GetCurrentProcessId()));
		std::error_code ignored;
		fs::remove_all(root, ignored);
		const auto file = [&](const fs::path& relative, const std::string& text)
		{
			fs::create_directories((root / relative).parent_path(), ignored);
			std::ofstream(root / relative, std::ios::binary) << text;
		};
		const fs::path x1 = fs::path(L"Scripts") / L"x1";
		CHECK(!mod_order::enabled_mod_has_files(root, x1) && mod_order::load_order(root).empty()); // no mods folder at all
		file(L"A/Scripts/x1/a.py", "x");
		file(L".staging-00000001/Scripts/x1/b.py", "x"); // a launcher import cut short
		file(L"B/Scripts/x1/c.py", "x");
		fs::create_directories(root / L"C" / x1, ignored); // an empty Scripts\x1
		file(L"load-order.txt", "\xEF\xBB\xBF# the launcher's list\r\n-A\r\n+C\r\n\r\nnot an entry\r\n");
		const auto order = mod_order::load_order(root);
		CHECK(order.size() == 3 && order[0].name == L"A" && !order[0].enabled && order[1].name == L"C" && order[1].enabled && order[2].name == L"B" && order[2].enabled);
		const auto mods = mod_order::enabled_mods(root);
		CHECK(mods.size() == 2 && mods[0].first == L"C" && mods[1].first == L"B" && mods[1].second == root / L"B");
		CHECK(mod_order::enabled_mod_has_files(root, x1)); // B isn't listed: it loads
		file(L"load-order.txt", "-A\r\n+C\r\n-B\r\n");
		CHECK(!mod_order::enabled_mod_has_files(root, x1)); // A and B off, C's folder empty, the staging folder never counts
		file(L"load-order.txt", "+A\n-B\n");
		CHECK(mod_order::enabled_mod_has_files(root, x1) && mod_order::enabled_mods(root).size() == 2); // A on; C unlisted, after it
		file(L"load-order.txt", "+a\n-b\n"); // names in any case
		CHECK(mod_order::enabled_mod_has_files(root, x1) && mod_order::load_order(root).size() == 3);
		fs::remove_all(root, ignored);
	}

	// How the game quits normally: WinMain returns into msvcr71.dll's exit(), and exit() calls ExitProcess
	// through msvcr71.dll's own import - not through XMen2.exe's, whose only caller is the runtime's abort
	// path (0x67215a). The fix hooks both. xml2_test --crt-exit-child <msvcr71.dll> does what the fix does
	// to the game's copy of the runtime, then quits the game's way, exit(7): the hook turns it into 107.
	using exit_process_t = void(WINAPI*)(UINT);
	exit_process_t crt_real_exit = nullptr;

	void WINAPI crt_exit_hook(const UINT code)
	{
		crt_real_exit(code + 100);
	}

	int run_crt_exit_child(const char* path)
	{
		const HMODULE crt = LoadLibraryA(path);
		if (!crt) return 2;
		crt_real_exit = reinterpret_cast<exit_process_t>(iat_hook::hook(crt, "KERNEL32.dll", "ExitProcess", 0, reinterpret_cast<void*>(&crt_exit_hook)));
		if (!crt_real_exit) return 3;
		const auto crt_exit = reinterpret_cast<void(__cdecl*)(int)>(GetProcAddress(crt, "exit"));
		if (!crt_exit) return 4;
		crt_exit(7);
		return 5; // exit() doesn't return
	}

	void check_crt_exit()
	{
		std::printf("discord: quitting the game's way (msvcr71.dll's exit()) reaches the hooked ExitProcess\n");
		std::vector<std::filesystem::path> candidates;
		for (auto dir = module_dir(); !dir.empty() && dir != dir.root_path(); dir = dir.parent_path())
		{
			candidates.push_back(dir / "docs" / "research" / "msvcr71.dll");
		}
		candidates.emplace_back(L"D:\\Games\\X-Men Legends II\\msvcr71.dll");
		std::error_code ignored;
		const auto found = std::find_if(candidates.begin(), candidates.end(), [&](const auto& path) { return std::filesystem::is_regular_file(path, ignored); });
		if (found == candidates.end())
		{
			std::printf("  skip  no msvcr71.dll (the game's C runtime) to quit through\n");
			return;
		}
		std::printf("  info  %s\n", found->string().c_str());
		wchar_t exe[MAX_PATH]{};
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		std::wstring command = std::wstring(L"\"") + exe + L"\" --crt-exit-child \"" + found->wstring() + L"\"";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		DWORD code = 0;
		if (CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
		{
			if (WaitForSingleObject(process.hProcess, 20000) != WAIT_OBJECT_0)
			{
				TerminateProcess(process.hProcess, 1);
			}
			GetExitCodeProcess(process.hProcess, &code);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
		}
		CHECK(code == 107);
	}

	// xml2_test --discord-live: a real round trip with the Discord client on this PC, as X-Men Legends II -
	// a test activity for a few seconds, then cleared. Not part of the normal run: it changes the
	// profile's presence while it runs.
	int run_discord_live()
	{
		using namespace discord_rules;
		std::printf("discord: live round trip with this PC's Discord (as X-Men Legends II)\n");
		discord_ipc::connection pipe;
		std::string name;
		std::string error;
		if (!pipe.open(xml2_client_id, name, error))
		{
			std::printf("  FAIL  %s\n", error.c_str());
			return 1;
		}
		std::printf("  ok    connected (%s), READY\n", name.c_str());
		int failed = 0;
		const auto expect = [&](const char* what, const std::string& nonce)
		{
			for (int i = 0; i < 20; ++i)
			{
				const auto reply = pipe.wait(250);
				if (!reply) continue;
				if (reply->op != op_frame || json_value(reply->json, "nonce") != nonce) continue;
				const bool ok = json_value(reply->json, "cmd") == "SET_ACTIVITY" && json_value(reply->json, "evt") != "ERROR";
				std::printf("  %s  %s: %s\n", ok ? "ok  " : "FAIL", what, reply->json.substr(0, 400).c_str());
				failed += !ok;
				return;
			}
			std::printf("  FAIL  %s: no answer\n", what);
			++failed;
		};
		FILETIME now{};
		GetSystemTimeAsFileTime(&now);
		extras x;
		x.start = static_cast<std::int64_t>(((static_cast<ULONGLONG>(now.dwHighDateTime) << 32 | now.dwLowDateTime) - 116444736000000000ULL) / 10000000ULL);
		x.large_image = std::string(large_image_key);
		x.large_text = "X-Men Legends II";
		x.party_id = random_party_id();
		const activity test{"xml2_test: Discord pipe check", "Online co-op \xC2\xB7 hosting", 1, 4, std::string(badge_online), "Online co-op"};
		CHECK(pipe.send(op_frame, set_activity_json(GetCurrentProcessId(), test, x, 1)));
		expect("set", "1");
		Sleep(4000);
		CHECK(pipe.send(op_frame, clear_activity_json(GetCurrentProcessId(), 2)));
		expect("cleared", "2");
		pipe.close();
		return failed;
	}

	// Another file of the game, next to the copy of XMen2.exe the checks use (read only), or nothing.
	std::optional<std::string> game_file(const wchar_t* name)
	{
		std::vector<std::filesystem::path> candidates;
		for (auto dir = module_dir(); !dir.empty() && dir != dir.root_path(); dir = dir.parent_path())
		{
			candidates.push_back(dir / "docs" / "research" / name);
		}
		candidates.push_back(std::filesystem::path(L"D:\\Games\\X-Men Legends II") / name);
		for (const auto& path : candidates)
		{
			std::error_code ignored;
			if (std::filesystem::is_regular_file(path, ignored))
			{
				std::printf("  info  reading %s\n", path.string().c_str());
				return read_file(path);
			}
		}
		return std::nullopt;
	}

	// The one ini rule (ini_rules.hpp): the helpers on their own, then a file of the test's own with the
	// README's lines (inline comments and all) read through ini_rules::read, as the fix reads xml2-fix.ini,
	// into every section's parser.
	void check_ini_rules()
	{
		std::printf("xml2-fix.ini: one rule for every key (a value ends at ';', empty = not set)\n");
		using ini_rules::value_text;
		CHECK(value_text("borderless   ; the desktop's size") == "borderless" && value_text("\t 0\t;x") == "0" && value_text("1 ; x ; y") == "1");
		CHECK(value_text("; only a comment").empty() && value_text("   ").empty() && value_text("").empty() && value_text("a;b") == "a");
		CHECK(value_text("X-Men #1   ; a hash is part of the value") == "X-Men #1" && value_text("on#x") == "on#x" && value_text(" two words ") == "two words");
		CHECK(!ini_rules::value(std::nullopt) && !ini_rules::value("") && !ini_rules::value("   ; later") && ini_rules::value(" 0 ; off") == "0");
		CHECK(ini_rules::number(std::nullopt, 7) == 7 && ini_rules::number("", 7) == 7 && ini_rules::number("  ; later", 1) == 1 && ini_rules::number("0 ; off", 1) == 0);
		CHECK(ini_rules::number("1440", 0) == 1440 && ini_rules::number("144fps", 0) == 144 && ini_rules::number("-5", 0) == -5 && ini_rules::number("+3", 0) == 3);
		CHECK(ini_rules::number("on", 1) == 0 && ini_rules::number("0x10", 0) == 16 && ini_rules::number("0x1F ; hex", 0) == 31 && ini_rules::number("1 # not a comment", 0) == 1);
		// Switches: the launcher's words, anything else (a '#' "comment" included) the key's default.
		CHECK(ini_rules::flag("1") == true && ini_rules::flag("On ; x") == true && ini_rules::flag("YES") == true && ini_rules::flag("true") == true);
		CHECK(ini_rules::flag("0") == false && ini_rules::flag(" off\t; later") == false && ini_rules::flag("No") == false && ini_rules::flag("FALSE") == false);
		CHECK(!ini_rules::flag(std::nullopt) && !ini_rules::flag("") && !ini_rules::flag("  ; later") && !ini_rules::flag("2") && !ini_rules::flag("off # pause behind other windows"));
		CHECK(ini_rules::narrow(L"abc") == "abc" && ini_rules::narrow(L"caf\u00e9") == "caf\x7f" && ini_rules::narrow(L"\u65e5") == "\x7f");

		const auto file = std::filesystem::temp_directory_path() / ("xml2_test-ini-" + std::to_string(GetCurrentProcessId()) + ".ini");
		{
			std::ofstream out(file, std::ios::binary);
			out << "; xml2-fix.ini as the README and the port's builder write it\r\n"
			       "[Display]\r\n"
			       "Mode=borderless      ; fullscreen, borderless or windowed; leave out for the game's own behaviour\r\n"
			       "Width=0              ; force a resolution; 0 = your desktop's size\r\n"
			       "Height=   ; later\r\n"
			       "Topmost=1\t; keep it on top\r\n"
			       "RunInBackground=     ; not decided\r\n"
			       "FrameRate=144 ; x\r\n"
			       "VSync=1 ; 0\r\n"
			       "ResolutionList=all   ; all: up to 64 entries\r\n"
			       "[Online]\r\n"
			       "Domain=   ; later\r\n"
			       "Server=127.0.0.1     ; every *.gamespy.com lookup -> this address\r\n"
			       "LocalIP=auto # a hash doesn't start a comment\r\n"
			       "GameVersion=X1.0     ; the X-Men Legends 1 port\r\n"
			       "[Game]\r\n"
			       "NewGameTeam=wolverine   ; Wolverine alone\r\n"
			       "SaveFolder=X-Men Legends   ; its own saves\r\n"
			       "WindowTitle=X-Men Legends   ; the window's and the taskbar's name\r\n"
			       "EndHeroUnlock=0   ; no Deadpool after the credits\r\n"
			       "MainMenuItems=button1,button2,button3,button4,button5,button6,button8,button7   ; XML1's menu: Quit button8, Play Online button7\r\n"
			       "PostgameScript=x1/menus/postgame   ; r505, then the main menu\r\n"
			       "NewGamePlus=0   ; no saved statistics\r\n"
			       "ResetUnlocks=  ; later\r\n"
			       "XPCurve=xml1    ; XML1's levels\r\n"
			       "GeometrySharingBlendIndices=1   ; the port's skinned models\r\n"
			       "[Discord]\r\n"
			       "Enabled=0        ; no presence at all (0, false, no or off; anything else, or no key: on)\r\n"
			       "ShowParty=OFF # later\r\n"
			       "LargeImage=     ; none: no images; another asset's key instead of the logo (no key: the logo)\r\n"
			       "[Test]\r\n"
			       "InputPipe=1 ; on\r\n"
			       "PipeName=my-pipe   ; mine\r\n"
			       "[Limits]\r\n"
			       "ActorSlots=127   ; headroom\r\n";
		}
		const auto read = [&](const wchar_t* section, const wchar_t* key) { return ini_rules::read(file, section, key); };
		const auto number = [&](const wchar_t* section, const wchar_t* key, const int fallback) { return ini_rules::read_number(file, section, key, fallback); };
		const auto flag = [&](const wchar_t* section, const wchar_t* key, const bool fallback) { return ini_rules::read_flag(file, section, key, fallback); };
		const auto view = [](const std::optional<std::string>& text) { return text ? std::optional<std::string_view>(*text) : std::nullopt; };

		// [Display]: the text keys without their comments (display.cpp's read_text), the numbers as before but
		// an empty one is its default.
		CHECK(read(L"Display", L"Mode") == "borderless" && display_rules::parse_mode(read(L"Display", L"Mode").value_or("")) == display_rules::mode::borderless);
		CHECK(number(L"Display", L"Width", 0) == 0 && number(L"Display", L"Height", 5) == 5 && flag(L"Display", L"Topmost", false) && flag(L"Display", L"RunInBackground", true));
		CHECK(frame_rate_rules::parse_frame_rate(read(L"Display", L"FrameRate").value_or("")) == (frame_rate_rules::cap{frame_rate_rules::cap::kind::fixed, 144}));
		CHECK(frame_rate_rules::parse_vsync(read(L"Display", L"VSync").value_or("")) == frame_rate_rules::vsync::on);
		CHECK(display_rules::parse_resolution_list(read(L"Display", L"ResolutionList").value_or("")) == display_rules::resolution_list::all);
		CHECK(!read(L"Display", L"Height") && !read(L"Display", L"RunInBackground") && !read(L"Display", L"Missing") && number(L"Display", L"Missing", 42) == 42);
		// [Online]: an empty Domain is openspy.net; Server wins; a '#' is part of LocalIP's value (refused, logged).
		const auto plan = online_rules::choose(read(L"Online", L"Domain").value_or(""), read(L"Online", L"Server").value_or(""));
		CHECK(plan.how == online_rules::plan::mode::server && plan.target == "127.0.0.1");
		CHECK(online_rules::choose(read(L"Online", L"Domain").value_or(""), "").target == "openspy.net");
		CHECK(read(L"Online", L"LocalIP") == "auto # a hash doesn't start a comment" && !local_ip_rules::choose(read(L"Online", L"LocalIP").value_or("")).problem.empty());
		CHECK(game_version_rules::parse_version(view(read(L"Online", L"GameVersion"))).version == "X1.0");
		// [Game]: every key takes an inline comment now, NewGameTeam and SaveFolder included.
		CHECK(read(L"Game", L"NewGameTeam") == "wolverine" && read(L"Game", L"SaveFolder") == "X-Men Legends" && new_game::valid_save_folder(*read(L"Game", L"SaveFolder")));
		CHECK(window_title_rules::parse_title(view(read(L"Game", L"WindowTitle"))).title == "X-Men Legends");
		CHECK(postgame_rules::parse_end_unlock(view(read(L"Game", L"EndHeroUnlock"))).skip);
		const auto items = main_menu_rules::parse_items(read(L"Game", L"MainMenuItems").value_or(""));
		CHECK(items.set && items.error.empty() && items.names[6] == "button8" && items.names[7] == "button7" && main_menu_rules::eight_items(items));
		CHECK(postgame_rules::parse_script(read(L"Game", L"PostgameScript").value_or("")).name == "x1/menus/postgame");
		CHECK(!flag(L"Game", L"NewGamePlus", true) && flag(L"Game", L"ResetUnlocks", true));
		CHECK(xp_curve_rules::parse_curve(read(L"Game", L"XPCurve").value_or("")).value == xp_curve_rules::curve::xml1);
		CHECK(flag(L"Game", L"GeometrySharingBlendIndices", geometry_sharing_rules::enabled_by_default) && !flag(L"Display", L"GeometrySharingBlendIndices", geometry_sharing_rules::enabled_by_default));
		// [Discord]: the README's line is off; "OFF # later" isn't a switch value (the default, on); the old
		// template's empty LargeImage is the logo, not "no art".
		CHECK(!discord_rules::parse_switch(view(read(L"Discord", L"Enabled")), true) && discord_rules::parse_switch(view(read(L"Discord", L"ShowParty")), true));
		CHECK(!read(L"Discord", L"LargeImage") && discord_rules::choose_images(view(read(L"Discord", L"LargeImage")), std::nullopt).large == "logo");
		// [Test], [Limits].
		CHECK(flag(L"Test", L"InputPipe", false) && test_input_rules::choose_pipe(read(L"Test", L"PipeName").value_or("")).name == "my-pipe");
		CHECK(limits_rules::decide(view(read(L"Limits", L"ActorSlots")), std::nullopt).actor_slots == 127);
		std::error_code ignored;
		std::filesystem::remove(file, ignored);
		CHECK(!read(L"Display", L"Mode") && number(L"Display", L"Width", 3) == 3); // no file: nothing set
	}

	// [Game] WindowTitle (window_title_rules.hpp): the value's rules, then, when the game's files are at hand,
	// what the hooks rely on: libIGDisplay.dll creates its window of class igWin32WindowClass with
	// CreateWindowExA and retitles it with SetWindowTextA, and XMen2.exe's title is also its registry root.
	void check_window_title_rules()
	{
		using namespace window_title_rules;
		std::printf("[Game] WindowTitle (the game window's title)\n");
		CHECK(parse_title("X-Men Legends").title == "X-Men Legends" && parse_title("  X-Men Legends   ; the port").title == "X-Men Legends");
		CHECK(parse_title(std::nullopt).title.empty() && parse_title(std::nullopt).error.empty() && parse_title("   ; later").title.empty() && parse_title("").error.empty());
		CHECK(parse_title("X-Men Legends #1: Rise!").title == "X-Men Legends #1: Rise!" && parse_title(std::string(title_max, 'x')).title.size() == title_max);
		CHECK(!parse_title(std::string(title_max + 1, 'x')).error.empty() && !parse_title("caf\x7f").error.empty() && !parse_title("a\tb").error.empty());
		CHECK(parse_title("caf\x7f").title.empty() && is_engine_class("igWin32WindowClass") && !is_engine_class("igwin32windowclass") && !is_engine_class("ConsoleWindowClass"));

		if (const auto exe = game_executable())
		{
			DWORD size = 0;
			std::uint8_t* image = map_image(*exe, size);
			CHECK(image != nullptr);
			if (image)
			{
				const auto at = [&](const DWORD va) { return image + (va - 0x400000); };
				// The window's title is the registry root's string too: why the string stays and the calls are hooked.
				CHECK(std::string_view(reinterpret_cast<const char*>(at(0x6a3a70))) == "X-Men Legends 2" && at(0x5faf43)[0] == 0x68 && operand_at(at(0x5faf44), 4) == 0x6a3a70);
				CHECK(at(0x5f5215)[0] == 0x68 && operand_at(at(0x5f5216), 4) == 0x6a3a70 && at(0x5f521a)[0] == 0x68 && operand_at(at(0x5f521b), 4) == 0x6a3a64);
				VirtualFree(image, 0, MEM_RELEASE);
			}
		}
		const auto display = game_file(L"libIGDisplay.dll");
		if (!display)
		{
			std::printf("  skip  no libIGDisplay.dll to check the window calls against\n");
			return;
		}
		DWORD size = 0;
		std::uint8_t* image = map_image(*display, size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto module = reinterpret_cast<HMODULE>(image);
		const auto at = [&](const DWORD va) { return image + (va - 0x10000000); };
		// igWin32Window: CreateWindowExA(0, "igWin32WindowClass", title, ...) at 0x10005894, SetWindowTextA(hwnd, title)
		// at 0x10005a62 - both through the import table the hooks change.
		CHECK(std::string_view(reinterpret_cast<const char*>(at(0x1000ae0c))) == engine_class);
		CHECK(at(0x1000588d)[0] == 0x68 && operand_at(at(0x1000588e), 4) == 0x1000ae0c && at(0x10005894)[0] == 0xff && at(0x10005894)[1] == 0x15 &&
		      operand_at(at(0x10005896), 4) == 0x100090b4);
		CHECK(at(0x10005a62)[0] == 0xff && at(0x10005a62)[1] == 0x15 && operand_at(at(0x10005a64), 4) == 0x100090ac);
		int dummy = 0;
		CHECK(iat_hook::hook(module, "USER32.dll", "CreateWindowExA", 0, &dummy) != nullptr && operand_at(at(0x100090b4), 4) == reinterpret_cast<std::uintptr_t>(&dummy));
		CHECK(iat_hook::hook(module, "USER32.dll", "SetWindowTextA", 0, &dummy) != nullptr && operand_at(at(0x100090ac), 4) == reinterpret_cast<std::uintptr_t>(&dummy));
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// [Online] GameVersion (game_version_rules.hpp): the value's rules, the guards on their own, then against a
	// copy of XMen2.exe every place that reads the version (each reads at most five bytes) and the change
	// applied to that copy.
	void check_game_version_rules()
	{
		using namespace game_version_rules;
		std::printf("[Online] GameVersion (the version GameSpy and other players see)\n");
		CHECK(parse_version("X1.0").version == "X1.0" && parse_version(" X1.0   ; the port").version == "X1.0" && parse_version("2").version == "2" && parse_version("a_b-").version == "a_b-");
		CHECK(parse_version(std::nullopt).version.empty() && parse_version(std::nullopt).error.empty() && parse_version("  ; later").version.empty() && parse_version("  ; later").error.empty());
		for (const char* bad : {"X1.01", "1.30a", "X 10", "a\\b", "1/2", "caf\x7f", "\"1\"", "1,2"})
		{
			const auto refused = parse_version(bad);
			if (refused.error.empty()) std::printf("  info  \"%s\" was taken\n", bad);
			CHECK(refused.version.empty() && !refused.error.empty());
		}
		CHECK(parse_version("X1.01").error.find("keeps 4") != std::string::npos);
		const auto bytes = bytes_for("X1.0");
		CHECK((bytes == std::array<std::uint8_t, 5>{'X', '1', '.', '0', 0}) && (bytes_for("2") == std::array<std::uint8_t, 5>{'2', 0, 0, 0, 0}));

		bool guards_ok = true;
		std::set<DWORD> addresses;
		for (const auto& g : guards) guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
		CHECK(guards_ok && guards[0].va == version_va && limits_rules::hex_size(guards[0].hex) == 8);

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the version's bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - image_base); };
		const guard* mismatch = first_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr && std::string_view(reinterpret_cast<const char*>(at(version_va))) == retail_version);
		CHECK(std::string_view(reinterpret_cast<const char*>(at(version_va + 8))) == "NoName");
		// Complete: every dword in the exe naming a byte of the version (0x6a49b0..0x6a49b7) lies in a guard.
		std::vector<DWORD> references;
		for (DWORD i = 0; i + 4 <= image_size; ++i)
		{
			const auto value = operand_at(image + i, 4);
			if (value >= version_va && value < version_va + 8) references.push_back(image_base + i);
		}
		bool covered = references.size() == 12;
		for (const DWORD r : references)
		{
			bool inside = false;
			for (const auto& g : guards) inside |= g.va != version_va && g.va <= r && r + 4 <= g.va + limits_rules::hex_size(g.hex);
			if (!inside) std::printf("  info  0x%08lX reads the version outside the guards\n", r);
			covered &= inside;
		}
		CHECK(covered);

		// The change on a copy: the version's bytes, nothing else; then the guard no longer matches.
		std::vector<std::uint8_t> before(image, image + image_size);
		apply(image, "X1.0");
		std::vector<DWORD> changed;
		for (DWORD i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i]) changed.push_back(image_base + i);
		}
		CHECK((changed == std::vector<DWORD>{version_va, version_va + 1, version_va + 2}) && // "1.30" -> "X1.0": the 0 stays
		      std::string_view(reinterpret_cast<const char*>(at(version_va))) == "X1.0");
		mismatch = first_mismatch(image);
		CHECK(mismatch && mismatch->va == version_va);
		std::memcpy(image, before.data(), image_size);
		apply(image, "2");
		CHECK(std::string_view(reinterpret_cast<const char*>(at(version_va))) == "2" && at(version_va + 4)[0] == 0 && std::string_view(reinterpret_cast<const char*>(at(version_va + 8))) == "NoName");
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// [Game] EndHeroUnlock (postgame_rules.hpp): the value's rules, the guards on their own, then against a copy
	// of XMen2.exe what state 3 does (the calls, the name, the popup's string) and the jump applied to it.
	void check_end_unlock_rules()
	{
		using namespace postgame_rules;
		std::printf("[Game] EndHeroUnlock (the ending's Deadpool unlock and popup)\n");
		CHECK(parse_end_unlock("0").skip && parse_end_unlock(" 0 ; the port").skip && parse_end_unlock("0").error.empty());
		CHECK(!parse_end_unlock(std::nullopt).skip && !parse_end_unlock("1").skip && parse_end_unlock("1").error.empty() && !parse_end_unlock("  ; later").skip);
		CHECK(!parse_end_unlock("no").skip && !parse_end_unlock("no").error.empty() && !parse_end_unlock("2").error.empty());
		CHECK(skip_unlock[0] == 0xe9 && operand_at(skip_unlock.data() + 1, 4) == unlock_skip_to - (unlock_call + 5) && unlock_skip_to - (unlock_call + 5) == 0x58);

		bool guards_ok = true, covered = false;
		std::set<DWORD> addresses;
		for (const auto& g : unlock_guards)
		{
			guards_ok &= limits_rules::valid_hex(g.hex) && addresses.insert(g.va).second && g.what && *g.what;
			const DWORD end = g.va + static_cast<DWORD>(limits_rules::hex_size(g.hex));
			covered |= g.va <= unlock_call && unlock_skip_to + 2 <= end;
		}
		CHECK(guards_ok && covered);

		const auto exe = game_executable();
		if (!exe)
		{
			std::printf("  skip  no XMen2.exe to check the ending's bytes against\n");
			return;
		}
		DWORD image_size = 0;
		std::uint8_t* image = map_image(*exe, image_size);
		CHECK(image != nullptr);
		if (!image) return;
		const auto at = [&](const DWORD va) { return image + (va - image_base); };
		const guard* mismatch = first_unlock_mismatch(image);
		if (mismatch) std::printf("  info  XMen2.exe at 0x%08lX isn't what the table says (%s)\n", mismatch->va, mismatch->what);
		CHECK(mismatch == nullptr && std::equal(retail_unlock_call.begin(), retail_unlock_call.end(), at(unlock_call)));
		// What state 3 does: the win (profile vt+0xa4) and Hard (vt+0xac) before the jump, the registry's lookup of
		// "deadpool", its unlock and the popup's string 1194 inside what it skips, and the step's epilogue after.
		const auto call_target = [&](const DWORD va) { return va + 5 + operand_at(at(va + 1), 4); };
		CHECK(call_target(0x5b1d17) == 0x48fed0 && at(0x5b1d21)[0] == 0xff && operand_at(at(0x5b1d23), 4) == 0xa4);
		CHECK(call_target(0x5b1d3a) == 0x48fed0 && at(0x5b1d44)[0] == 0xff && operand_at(at(0x5b1d46), 4) == 0xac && at(0x5b1d38)[0] == 0x75 && 0x5b1d3a + at(0x5b1d39)[0] == unlock_call);
		CHECK(call_target(unlock_call) == 0x44b8f0 && at(0x5b1d51)[0] == 0x68 && operand_at(at(0x5b1d52), 4) == 0x68253c && call_target(0x5b1d5d) == 0x44b8f0);
		CHECK(at(0x5b1d97)[0] == 0x68 && operand_at(at(0x5b1d98), 4) == 1194 && std::string_view(reinterpret_cast<const char*>(at(0x68253c))) == "deadpool");
		CHECK(at(unlock_skip_to)[0] == 0x5d && at(unlock_skip_to + 1)[0] == 0x5f && at(unlock_skip_to + 2)[0] == 0xc7 && operand_at(at(unlock_skip_to + 4), 4) == 0x1958 &&
		      operand_at(at(unlock_skip_to + 8), 4) == 4);
		// The one branch in state 3 that isn't inside the skipped block lands on the jump itself: jne 0x5b1d4a.
		CHECK(at(0x5b1d7c)[0] == 0x7d && 0x5b1d7e + at(0x5b1d7d)[0] == unlock_skip_to); // (the popup's jge, inside it, lands after it)

		// The change on a copy: the five bytes of the call, nothing else; then the guard no longer matches.
		std::vector<std::uint8_t> before(image, image + image_size);
		apply_skip_unlock(image);
		std::vector<DWORD> changed;
		for (DWORD i = 0; i < image_size; ++i)
		{
			if (image[i] != before[i]) changed.push_back(image_base + i);
		}
		CHECK((changed == std::vector<DWORD>{unlock_call, unlock_call + 1, unlock_call + 2, unlock_call + 3, unlock_call + 4}));
		CHECK(at(unlock_call)[0] == 0xe9 && unlock_call + 5 + operand_at(at(unlock_call + 1), 4) == unlock_skip_to);
		mismatch = first_unlock_mismatch(image);
		CHECK(mismatch && mismatch->va == 0x5b1ce3 && postgame_rules::first_mismatch(image) == nullptr); // PostgameScript's guards don't see it
		std::memcpy(image, before.data(), image_size);
		VirtualFree(image, 0, MEM_RELEASE);
	}

	// XInput on a thread of its own (xinput_pad.hpp): the fix's log says it was ready after some time on
	// another thread than this program's main one, which never waited for it.
	void check_xinput_thread(const DWORD main_thread)
	{
		std::printf("xinput: loaded off the game's thread\n");
		std::string line;
		for (int i = 0; i < 150 && line.empty(); ++i)
		{
			const auto log = read_file(module_dir() / "xml2-fix.log");
			if (const auto at = log.find("xinput: ready after "); at != std::string::npos)
			{
				line = log.substr(at, log.find('\n', at) - at);
			}
			else
			{
				Sleep(100);
			}
		}
		std::printf("  info  %s\n", line.empty() ? "(no xinput line in the log)" : line.c_str());
		const auto thread_at = line.find(" on thread ");
		const auto thread = thread_at == std::string::npos ? 0ul : std::strtoul(line.c_str() + thread_at + 11, nullptr, 10);
		CHECK(!line.empty() && thread != 0 && thread != main_thread && line.find("(its own, not the game's") != std::string::npos);
	}
}

namespace
{
	// ---- Geometry sharing: the bridge run for real ------------------------------------------------------

	std::uint32_t geometry_bridge_choice = 1;
	std::uint32_t geometry_x87_bits = 0;

	// The callback the bridge calls: it wipes xmm0 and the x87 stack, which the bridge must put back, and
	// answers geometry_bridge_choice for the incoming and candidate geometries invoke_geometry_bridge
	// sets up (9 for any other pair).
	std::uint32_t __cdecl geometry_bridge_callback(std::uint32_t a, std::uint32_t b)
	{
		__asm pxor xmm0, xmm0
		__asm fninit
		return a == 0x123000 && b == 0x456000 ? geometry_bridge_choice : 9;
	}

	// Where a kept candidate resumes: the displaced instructions' results (eax from [esp+20], esi 0, so
	// the zero flag set and the carry clear), every other register, xmm0 and the x87 stack as
	// invoke_geometry_bridge left them. 1 when they all are, 0 when not; then back to its caller.
	__declspec(naked) void geometry_accept_landing()
	{
		__asm fstp dword ptr [geometry_x87_bits]
		__asm jc failed
		__asm jnz failed
		__asm cmp ecx, [esp + 52]
		__asm jne failed
		__asm cmp edx, 0x13579bdf
		__asm jne failed
		__asm cmp edi, 0x123000
		__asm jne failed
		__asm cmp eax, 0x12345678
		__asm jne failed
		__asm test esi, esi
		__asm jnz failed
		__asm cmp ebx, 0x11223344
		__asm jne failed
		__asm cmp ebp, 0x55667788
		__asm jne failed
		__asm movd ecx, xmm0
		__asm cmp ecx, 0xaabbccdd
		__asm jne failed
		__asm mov eax, 1
		__asm jmp done
	failed:
		__asm xor eax, eax
	done:
		__asm add esp, 32
		__asm pop edi
		__asm pop esi
		__asm pop ebx
		__asm pop ebp
		__asm ret
	}

	// Where a rejected candidate goes: every register, the flags (the carry set), xmm0 and the x87 stack
	// as invoke_geometry_bridge left them. 2 when they all are, 0 when not.
	__declspec(naked) void geometry_reject_landing()
	{
		__asm fstp dword ptr [geometry_x87_bits]
		__asm jnc failed
		__asm cmp ecx, [esp + 52]
		__asm jne failed
		__asm cmp edx, 0x13579bdf
		__asm jne failed
		__asm cmp eax, 0xaabbccdd
		__asm jne failed
		__asm cmp esi, 0x456000
		__asm jne failed
		__asm cmp edi, 0x123000
		__asm jne failed
		__asm cmp ebx, 0x11223344
		__asm jne failed
		__asm cmp ebp, 0x55667788
		__asm jne failed
		__asm movd ecx, xmm0
		__asm cmp ecx, 0xaabbccdd
		__asm jne failed
		__asm mov eax, 2
		__asm jmp done
	failed:
		__asm xor eax, eax
	done:
		__asm add esp, 32
		__asm pop edi
		__asm pop esi
		__asm pop ebx
		__asm pop ebp
		__asm ret
	}

	// Jumps to the bridge at `stub` as the hook does: edi the incoming geometry, esi the candidate, a
	// known value in every other register, xmm0, [esp+20], the x87 stack (1.0) and the carry flag.
	__declspec(naked) std::uint32_t __cdecl invoke_geometry_bridge(void*)
	{
		__asm push ebp
		__asm push ebx
		__asm push esi
		__asm push edi
		__asm sub esp, 32
		__asm mov edi, 0x123000
		__asm mov esi, 0x456000
		__asm mov dword ptr [esp + 20], 0x12345678
		__asm mov ebx, 0x11223344
		__asm mov ebp, 0x55667788
		__asm mov eax, 0xaabbccdd
		__asm movd xmm0, eax
		__asm mov ecx, [esp + 52]
		__asm mov edx, 0x13579bdf
		__asm fld1
		__asm stc
		__asm jmp ecx
	}

	// [Game] GeometrySharingBlendIndices (geometry_sharing_rules.hpp): the comparison on memory of the
	// test's own, the code guards against the game's files when they are at hand, the bridge run for
	// real and the hook's jump.
	void check_geometry_sharing_rules()
	{
		using namespace geometry_sharing_rules;
		std::printf("geometry sharing blend-index rules\n");
		// Off unless [Game] GeometrySharingBlendIndices says so: no key, an empty one or 0 is the game's own
		// comparison (the X-Men Legends I port sets it; XML2 as it ships leaves it out).
		CHECK(!enabled_by_default);
		CHECK(!ini_rules::flag(std::nullopt).value_or(enabled_by_default) && !ini_rules::flag("  ; later").value_or(enabled_by_default));
		CHECK(!ini_rules::flag("0").value_or(enabled_by_default) && ini_rules::flag("1").value_or(enabled_by_default));
		struct memory
		{
			std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x8000);

			bool read(address a, void* out, std::size_t n)
			{
				if (a < 0x10000 || n > bytes.size() || a - 0x10000 > bytes.size() - n)
				{
					return false;
				}
				std::memcpy(out, bytes.data() + a - 0x10000, n);
				return true;
			}

			void word(address a, address v)
			{
				std::memcpy(bytes.data() + a - 0x10000, &v, 4);
			}

			void byte(address a, std::uint8_t v)
			{
				bytes[a - 0x10000] = v;
			}
		} mem;
		constexpr layout types{0x90000, 0xa0000};

		// A geometry, its vertex array and the array's memory: five vertices of 32 bytes, three weights
		// and three indices, the indices packed at +28.
		const auto make = [&](address g, address v, address m, address data)
		{
			mem.word(g, types.geometry_vtable);
			mem.word(g + 12, v);
			mem.word(v, types.vertex_vtable);
			mem.word(v + 8, m);
			mem.word(v + 12, 5);
			mem.word(v + 28, 1 | (3 << 4) | (3 << 8));
			mem.byte(v + 56, 32);
			mem.byte(v + 61, 28);
			mem.word(m + 80, data);
			for (address i = 0; i < 5; ++i)
			{
				mem.word(data + i * 32 + 28, 0x00030201);
			}
		};
		make(0x10100, 0x10200, 0x10300, 0x11000);
		make(0x14100, 0x14200, 0x14300, 0x15000);
		CHECK(compare(mem, 0x10100, 0x14100, types) == comparison::equal);
		mem.byte(0x15000 + 4 * 32 + 29, 7);
		CHECK(compare(mem, 0x10100, 0x14100, types) == comparison::different);
		mem.byte(0x15000 + 4 * 32 + 29, 2);
		// A non-index field is still the retail comparator's decision.
		mem.byte(0x15000 + 9, 19);
		CHECK(compare(mem, 0x10100, 0x14100, types) == comparison::equal);
		// Identical packed data across every basic supported count/format combination.
		for (address indices = 1; indices <= 4; ++indices)
		{
			for (address weights = 0; weights <= 4; ++weights)
			{
				for (address uv = 0; uv <= 8; ++uv)
				{
					const address format = 7 | (weights << 4) | (indices << 8) | (uv << 16);
					mem.word(0x10200 + 28, format);
					mem.word(0x14200 + 28, format);
					CHECK(compare(mem, 0x10100, 0x14100, types) == comparison::equal);
				}
			}
		}

		// Anything the rules don't know is the game's own comparison: another vtable, extended formats,
		// more than four indices or weights, more than eight UVs, a count mismatch, an index field past
		// the address space or past the end of readable memory, an offset or stride that can't hold it.
		mem.word(0x10200 + 28, 0x331);
		mem.word(0x14200 + 28, 0x331);
		const auto saved = mem.bytes;
		const auto unsupported = [&](address at, address value)
		{
			mem.bytes = saved;
			mem.word(at, value);
			CHECK(compare(mem, 0x10100, 0x14100, types) == comparison::unsupported);
		};
		unsupported(0x14100, 0x90004);
		unsupported(0x14200, 0xa0004);
		unsupported(0x14200 + 28, 0x00400331);
		unsupported(0x14200 + 28, 0x00000531);
		unsupported(0x14200 + 28, 0x00000351);
		unsupported(0x14200 + 28, 0x00090331);
		unsupported(0x14200 + 12, 4);
		unsupported(0x14300 + 80, 0xfffffff0);
		unsupported(0x14300 + 80, 0x17fff);
		mem.bytes = saved;
		mem.byte(0x14200 + 61, 255);
		CHECK(compare(mem, 0x10100, 0x14100, types) == comparison::unsupported);
		mem.bytes = saved;
		mem.byte(0x14200 + 56, 2);
		CHECK(compare(mem, 0x10100, 0x14100, types) == comparison::unsupported);
		// No indices at all: nothing to compare, the game decides.
		mem.bytes = saved;
		mem.word(0x10200 + 28, 1);
		mem.word(0x14200 + 28, 1);
		CHECK(compare(mem, 0x10100, 0x14100, types) == comparison::equal);
		// Rejection must reach another candidate, not terminate the original search.
		mem.bytes = saved;
		mem.byte(0x15000 + 28, 8);
		make(0x15100, 0x15200, 0x15300, 0x16000);
		CHECK(compare(mem, 0x10100, 0x14100, types) == comparison::different);
		CHECK(compare(mem, 0x10100, 0x15100, types) == comparison::equal);

		// The guards: each matches its own bytes and refuses a change to any one of them.
		const auto guard_tests = [&](const auto& guards)
		{
			for (const auto& g : guards)
			{
				std::vector<std::uint8_t> bytes(g.hex.size() / 2);
				for (std::size_t i = 0; i < bytes.size(); ++i)
				{
					bytes[i] = static_cast<std::uint8_t>(std::stoi(std::string(g.hex.substr(2 * i, 2)), nullptr, 16));
				}
				CHECK(limits_rules::matches(bytes.data(), g.hex));
				for (std::size_t i = 0; i < bytes.size(); ++i)
				{
					bytes[i] ^= 1;
					CHECK(!limits_rules::matches(bytes.data(), g.hex));
					bytes[i] ^= 1;
				}
			}
		};
		guard_tests(exe_guards);
		guard_tests(gfx_guards);
		guard_tests(attrs_guards);
		CHECK(!first_mismatch(exe_guards, [](address, std::string_view) { return true; }));
		for (const auto& g : exe_guards)
		{
			CHECK(first_mismatch(exe_guards, [&](address a, std::string_view) { return a != g.va; }) == &g);
		}

		// Against the game's files, when they are at hand: every guard, and the vtable slots install()
		// checks (libIGGfx.dll and libIGAttrs.dll prefer 0x10000000).
		if (const auto exe = game_executable())
		{
			for (const auto& g : exe_guards)
			{
				const auto off = file_offset(*exe, g.va - 0x400000);
				CHECK(off && *off + g.hex.size() / 2 <= exe->size());
				if (off && *off + g.hex.size() / 2 <= exe->size())
				{
					CHECK(limits_rules::matches(reinterpret_cast<const std::uint8_t*>(exe->data() + *off), g.hex));
				}
			}
			for (const auto& spec : std::array<std::pair<const wchar_t*, bool>, 2>{{{L"libIGGfx.dll", true}, {L"libIGAttrs.dll", false}}})
			{
				if (const auto file = game_file(spec.first))
				{
					const auto& guards = spec.second ? std::vector<guard>(gfx_guards.begin(), gfx_guards.end()) : std::vector<guard>(attrs_guards.begin(), attrs_guards.end());
					for (const auto& g : guards)
					{
						const auto off = file_offset(*file, g.va);
						CHECK(off && *off + g.hex.size() / 2 <= file->size());
						if (off && *off + g.hex.size() / 2 <= file->size())
						{
							CHECK(limits_rules::matches(reinterpret_cast<const std::uint8_t*>(file->data() + *off), g.hex));
						}
					}
					const auto check_slot = [&](address rva, address target)
					{
						const auto off = file_offset(*file, rva);
						CHECK(off && *off + 4 <= file->size());
						if (off && *off + 4 <= file->size())
						{
							address ptr = 0;
							std::memcpy(&ptr, file->data() + *off, 4);
							CHECK(ptr == 0x10000000 + target);
						}
					};
					if (spec.second)
					{
						check_slot(vertex_vtable_rva + 0x5c, 0x4010);
						check_slot(vertex_vtable_rva + 0x70, 0x46040);
					}
					else
					{
						check_slot(geometry_vtable_rva + 0x54, 0x17000);
					}
				}
			}
		}
		else
		{
			std::printf("  skip  no local executable for geometry sharing guards\n");
		}

		// The bridge, run: a kept candidate lands on the accept side, a rejected one on the reject side,
		// both with the x87 stack's 1.0 still there.
		const auto code = bridge(static_cast<address>(reinterpret_cast<std::uintptr_t>(&geometry_bridge_callback)),
		                         static_cast<address>(reinterpret_cast<std::uintptr_t>(&geometry_accept_landing)),
		                         static_cast<address>(reinterpret_cast<std::uintptr_t>(&geometry_reject_landing)));
		auto* stub = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		CHECK(stub != nullptr);
		if (stub)
		{
			std::memcpy(stub, code.data(), code.size());
			FlushInstructionCache(GetCurrentProcess(), stub, code.size());
			geometry_bridge_choice = 1;
			CHECK(invoke_geometry_bridge(stub) == 1);
			CHECK(geometry_x87_bits == 0x3f800000);
			geometry_bridge_choice = 0;
			CHECK(invoke_geometry_bridge(stub) == 2);
			CHECK(geometry_x87_bits == 0x3f800000);
			VirtualFree(stub, 0, MEM_RELEASE);
		}

		// The hook's jump: e9 and the distance to the target from the end of it, then a nop.
		const auto patch = jump(hook_va, 0x12345678);
		address rel = 0;
		std::memcpy(&rel, patch.data() + 1, 4);
		CHECK(patch[0] == 0xe9 && patch[5] == 0x90 && hook_va + 5 + rel == 0x12345678);
	}
}

int main(const int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "--script-rules") == 0)
	{
		check_forced_teams_rules();
		std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
		return failures ? 1 : 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "--geometry-sharing-rules") == 0)
	{
		check_geometry_sharing_rules();
		return failures ? 1 : 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "--fight-style-rules") == 0)
	{
		check_fight_style_rules();
		std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
		return failures ? 1 : 0;
	}
	if (argc > 1 && std::strcmp(argv[1], "--state-rules") == 0)
	{
		check_test_input_rules();
		check_test_state_rules();
		std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
		return failures ? 1 : 0;
	}
	const bool show_live = argc > 1 && std::strcmp(argv[1], "--live") == 0;
	std::setvbuf(stdout, nullptr, _IONBF, 0); // keep output up to a crash

	if (argc > 2 && std::strcmp(argv[1], "--pipe-child") == 0)
	{
		check_pipe(argv[2]);
		return failures; // added to the parent's
	}
	if (argc > 2 && std::strcmp(argv[1], "--crt-exit-child") == 0)
	{
		return run_crt_exit_child(argv[2]);
	}
	if (argc > 1 && std::strcmp(argv[1], "--discord-live") == 0)
	{
		failures += run_discord_live();
		std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
		return failures ? 1 : 0;
	}

	const DWORD main_thread = GetCurrentThreadId();
	std::printf("the fix is the dinput.dll this program loaded\n");
	const auto fix = GetModuleHandleW(L"dinput.dll");
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(fix, path, MAX_PATH);
	CHECK(fix != nullptr && std::filesystem::path(path).parent_path() == module_dir());

	std::printf("forwarded exports behave like Windows' own dinput.dll\n");
	void* direct_input = nullptr;
	CHECK(SUCCEEDED(DirectInputCreateEx(GetModuleHandleW(nullptr), 0x0700, IID_IDirectInput7A, &direct_input, nullptr)) && direct_input);
	if (direct_input)
	{
		std::vector<found_device> devices;
		CHECK(SUCCEEDED(slot<enum_devices_t>(direct_input, 4)(direct_input, 4 /* DIDEVTYPE_JOYSTICK; Windows' DirectInput 7 can crash listing all types */, &collect, &devices, DIEDFL_ATTACHEDONLY)));
		std::printf("  info  %zu device(s) attached\n", devices.size());
		CHECK(std::ranges::none_of(devices, [](const found_device& d) { return virtual_pad_rules::pad_of(d.instance, virtual_pad_rules::max_pads).has_value(); })); // no [Test] VirtualPads here
		static_cast<IUnknown*>(direct_input)->Release();
	}

	const int pads = connected_xinput_pads();
	std::printf("  info  %d XInput pad(s) connected\n", pads);
	if (pads > 0)
	{
		check_engine_path();
		check_game_path(show_live);
	}
	else
	{
		std::printf("  skip  pad checks (connect an Xbox-compatible pad)\n");
	}
	check_online();
	check_online_rules();
	check_local_ip_rules();
	check_local_ip();
	check_display_rules();
	check_frame_rate_rules();
	check_menu_screens();
	check_options_menu_rules();
	check_resolution_rules();
	check_geometry_sharing_rules();
	check_limits_rules();
	check_fight_style_rules();
	check_forced_teams_rules();
	check_conversations_rules();
	check_test_input_rules();
	check_test_state_rules();
	check_pad_input_rules();
	check_virtual_pad_rules();
	check_image_file();
	check_save_folder();
	check_postgame_rules();
	check_end_unlock_rules();
	check_new_game_plus_rules();
	check_review_menu_rules();
	check_main_menu_rules();
	check_window_title_rules();
	check_game_version_rules();
	check_ini_rules();
	check_xp_curve_rules();
	check_pad_prompts_rules();
	check_discord_rules();
	check_discord_pipe();
	check_mod_order();
	check_d3d8_modes();

	check_xinput_thread(main_thread);

	const auto log = read_file(module_dir() / "xml2-fix.log");
	CHECK(log.find("hooked a DirectInput 7 instance") != std::string::npos);
	CHECK(log.find("display: as the game has it") != std::string::npos); // no [Display] section next to the test
	CHECK(log.find("test:") == std::string::npos);                       // and no [Test] section: no pipe
	CHECK(log.find("virtual pads:") == std::string::npos);               // nor virtual pads
	CHECK(log.find("options: XMen2.exe doesn't have the expected code") != std::string::npos && log.find("call sites patched") == std::string::npos); // not the game
	// Default-off: no keys and no rows (not the game) -> nothing hooked, the resolution table never looked at.
	CHECK(log.find("nothing hooked") != std::string::npos && log.find("resolution list:") == std::string::npos && log.find("references patched") == std::string::npos);
	CHECK(log.find("GameSpy servers redirected to openspy.net") != std::string::npos);
	// No [Limits]: the engine's caps untouched (and this isn't the game anyway).
	CHECK(log.find("limits: the game's own caps - 40 actor slots, 450 resource names, 375 item enhancements (no [Limits] in xml2-fix.ini)") != std::string::npos && log.find("raised from") == std::string::npos);
	// No [Game] ForcedTeams: no script functions.
	CHECK(log.find("forced teams: off (no [Game] ForcedTeams in xml2-fix.ini)") != std::string::npos && log.find("script functions added") == std::string::npos);
	CHECK(log.find("postgame:") == std::string::npos); // no [Game] PostgameScript: not a word, nothing patched
	// Conversations: on by default, but only ever in XMen2.exe - nothing patched here.
	CHECK(log.find("conversations: XMen2.exe isn't loaded at 0x400000 (not the game?)") != std::string::npos && log.find("conversations: AutoAdvance on") == std::string::npos);
	CHECK(log.find("main menu:") == std::string::npos); // no [Game] MainMenuItems: not a word, nothing patched
	CHECK(log.find("xp curve:") == std::string::npos);  // no [Game] XPCurve: not a word, nothing patched
	CHECK(log.find("title:") == std::string::npos);     // no [Game] WindowTitle: not a word, nothing hooked
	CHECK(log.find("online: GameSpy game version") == std::string::npos && log.find("GameVersion") == std::string::npos); // no [Online] GameVersion
	// No [Input]: Prompts is auto, but this isn't XMen2.exe - nothing patched.
	CHECK(log.find("prompts: XMen2.exe isn't loaded at 0x400000 (not the game?) - the game's own prompts") != std::string::npos ||
	      log.find("prompts: 0x004BD720 isn't the retail code") != std::string::npos);
	CHECK(log.find("prompts: button prompts show") == std::string::npos);
	// Discord Rich Presence is on by default, but only ever in XMen2.exe: never this test's own presence.
	CHECK(log.find("discord: XMen2.exe isn't loaded at 0x400000 (not the game?) - no presence") != std::string::npos && log.find("discord: presence on") == std::string::npos);
	CHECK(log.find("xmenlegpc.master.gamespy.com -> xmenlegpc.master.openspy.net (resolved)") != std::string::npos);
	if (pads > 0)
	{
		CHECK(log.find("hooked a DirectInput 8 instance") != std::string::npos);
		CHECK(log.find("as Logitech Dual Action #2") == std::string::npos); // one pad seen by both paths
	}

	failures += run_pipe_child();
	check_crt_exit(); // after the log checks: the child writes a log of its own

	std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
