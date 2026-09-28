// Runs next to the built dinput.dll and imports it, so Windows loads the fix exactly the way
// it does for X-Men Legends II, then reads the pad both ways the game does:
//   - the engine's DirectInput 7: DirectInputCreateEx, CreateDevice + QueryInterface to
//     IDirectInputDevice2A, ranges set by offset, c_dfDIJoystick;
//   - the game's own DirectInput 8: dinput8.dll loaded from the system folder by full path,
//     EnumObjects, ranges set by object id, c_dfDIJoystick2.
// With an Xbox-compatible pad connected, both must see a Logitech Dual Action. Also checks
// that GameSpy host lookups resolve through OpenSpy, and the display fix's decisions: the
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
// operands it writes against that copy. The test input pipe is checked on its rules, on a
// Direct3D 8 device of the test's own (the back buffer copy behind "screenshot"), and end to
// end in a child process started with an xml2-fix.ini that turns the pipe on: it creates the
// keyboard device the way XMen2.exe does and sees the pipe's keys in it.
//
//   xml2_test.exe          run the checks
//   xml2_test.exe --live   also show live pad input, as the game sees it, for 20 seconds

#define DIRECTINPUT_VERSION 0x0800
#include <WinSock2.h>
#include <Windows.h>
#include <dinput.h>
#include <timeapi.h>
#include <Xinput.h>

#include "display_rules.hpp"
#include "forced_teams_rules.hpp"
#include "frame_capture.hpp"
#include "frame_rate_rules.hpp"
#include "image_file.hpp"
#include "limits_rules.hpp"
#include "main_menu_rules.hpp"
#include "new_game.hpp"
#include "options_menu_rules.hpp"
#include "postgame_rules.hpp"
#include "resolution_rules.hpp"
#include "test_input_rules.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
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
		std::printf("engine limits rules ([Limits] ActorSlots and ResourceNames)\n");

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
		CHECK(retail);
		const std::vector<std::uint8_t> before(image, image + image_size);

		// The stock caps write back exactly the retail bytes (with the retail object and findNext).
		for (const auto& w : actor_writes(retail_actors, actor_object_retail, find_next_40_va)) apply_write(image, w);
		for (const auto& w : name_writes(retail_names)) apply_write(image, w);
		CHECK(std::memcmp(image, before.data(), image_size) == 0);

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
		bool add_claims_only = false; // 0x46c9f0 as seen in game: true, nobody seated
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
		forced_teams_rules::character_lookup find_character(const std::string& name, int& id)
		{
			const auto f = characters.find(name);
			if (f == characters.end()) return forced_teams_rules::character_lookup::missing;
			if (f->second < 0) return forced_teams_rules::character_lookup::not_character;
			id = f->second;
			return forced_teams_rules::character_lookup::found;
		}
		bool run_now(const std::string& line) // pushsidemission, as 0x5f3630: a record when there is a zone and room
		{
			ran.push_back(line);
			if (line.rfind("pushsidemission ", 0) == 0 && !zone.empty() && records.size() < 2) records.push_back({zone, slots});
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
	}

	void check_forced_teams_rules()
	{
		using namespace forced_teams_rules;
		std::printf("forced parties ([Game] ForcedTeams and AddHero: seatParty, setSkinset, pushParty, popParty, addHero)\n");

		// The functions: seven, signatures the game's compiler knows, room in its tree.
		CHECK(functions.size() == 7 && table_count == 0x128 && builtin_count + table_count == 315 && table_count + builtin_count <= tree_capacity);
		bool signatures_ok = true;
		for (const auto& f : functions)
		{
			signatures_ok &= std::strlen(f.ret) == 1 && std::strchr("nis", f.ret[0]) && std::strlen(f.args) >= 1 && std::strlen(f.args) <= 7 &&
			                 std::strspn(f.args, "sia") == std::strlen(f.args);
		}
		CHECK(signatures_ok);
		CHECK(std::string_view(functions[1].name) == "seatParty" && std::string_view(functions[1].args) == "ssss" && std::string_view(functions[0].name) == "xml2fixFeature" &&
		      std::string_view(functions[static_cast<std::size_t>(function::get_party_member)].name) == "getPartyMember");
		const auto writes = registration_writes(0x12345678);
		CHECK(writes[0].va == 0x49fe31 && writes[0].value == 0x12345678 && writes[1].va == 0x49fe36 && writes[1].value == 0x128 && writes[0].size == 4 && writes[1].size == 4);

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
		CHECK(feature_named("forcedteams") == feature::forced_teams && feature_named(" AddHero") == feature::add_hero && !feature_named("forced_teams") && !feature_named(""));

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
		CHECK(guards_ok && guards.size() == 94);

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

		// The game's names: its 289 functions and 19 builtins; none of the seven is among them, nor
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

		// The table the DLL builds: the game's 289 entries byte for byte, then the seven.
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
		CHECK(changed == 5 && outside == 0); // 0x0068a908 -> 0x12345678, 0x121 -> 0x128
		CHECK(limits_rules::matches(at(registration), "68785634126828010000e8318903008bc8e85a770300c3"));
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
	void check_test_input_rules()
	{
		using namespace test_input_rules;
		std::printf("test input rules ([Test] InputPipe commands)\n");

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

	constexpr const char* pipe_name = "\\\\.\\pipe\\xml2-fix-input";

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

	// Runs in the child process (run_pipe_child), whose dinput.dll saw [Test] InputPipe=1.
	void check_pipe()
	{
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

		HANDLE pipe = INVALID_HANDLE_VALUE;
		for (int attempt = 0; attempt < 50 && pipe == INVALID_HANDLE_VALUE; ++attempt)
		{
			pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
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
		CHECK(log.find("test: input pipe") != std::string::npos);
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
		std::printf("test input pipe: starting a child with [Test] InputPipe=1\n");
		const auto ini = module_dir() / "xml2-fix.ini";
		if (std::filesystem::exists(ini))
		{
			std::printf("  skip  an xml2-fix.ini already sits next to the test\n");
			return 0;
		}
		{
			std::ofstream out(ini, std::ios::binary);
			out << "[Test]\r\nInputPipe=1\r\n[Display]\r\nResolutionList=all\r\n[Limits]\r\nActorSlots=127\r\n[Game]\r\nForcedTeams=1\r\nAddHero=1\r\nPostgameScript=x1/menus/postgame ; XML1's r505\r\nMainMenuItems=button1,button2,button3,button4,button5,button6,button7 ; XML1's buttons\r\n";
		}

		wchar_t exe[MAX_PATH]{};
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		std::wstring command = std::wstring(L"\"") + exe + L"\" --pipe-child";
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
		CHECK(names_ok && effective(port, quit_slot) == "button7" && effective(port, 7) == "debug" && effective(port, 8) == "debug_focus");
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
		CHECK(writes_ok && writes_for(own, pointers).empty() && writes_for(parse_items("a,b,c,d,e,f,g,h,i"), pointers).size() == sites.size());

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
		VirtualFree(image, 0, MEM_RELEASE);
	}
}

int main(const int argc, char** argv)
{
	const bool show_live = argc > 1 && std::strcmp(argv[1], "--live") == 0;
	std::setvbuf(stdout, nullptr, _IONBF, 0); // keep output up to a crash

	if (argc > 1 && std::strcmp(argv[1], "--pipe-child") == 0)
	{
		check_pipe();
		return failures; // added to the parent's
	}

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
	check_display_rules();
	check_frame_rate_rules();
	check_options_menu_rules();
	check_resolution_rules();
	check_limits_rules();
	check_forced_teams_rules();
	check_test_input_rules();
	check_image_file();
	check_save_folder();
	check_postgame_rules();
	check_main_menu_rules();
	check_d3d8_modes();

	const auto log = read_file(module_dir() / "xml2-fix.log");
	CHECK(log.find("hooked a DirectInput 7 instance") != std::string::npos);
	CHECK(log.find("display: as the game has it") != std::string::npos); // no [Display] section next to the test
	CHECK(log.find("test:") == std::string::npos);                       // and no [Test] section: no pipe
	CHECK(log.find("options: XMen2.exe doesn't have the expected code") != std::string::npos && log.find("call sites patched") == std::string::npos); // not the game
	// Default-off: no keys and no rows (not the game) -> nothing hooked, the resolution table never looked at.
	CHECK(log.find("nothing hooked") != std::string::npos && log.find("resolution list:") == std::string::npos && log.find("references patched") == std::string::npos);
	CHECK(log.find("GameSpy servers redirected to openspy.net") != std::string::npos);
	// No [Limits]: the engine's caps untouched (and this isn't the game anyway).
	CHECK(log.find("limits: the game's own caps - 40 actor slots, 450 resource names (no [Limits] in xml2-fix.ini)") != std::string::npos && log.find("raised from") == std::string::npos);
	// No [Game] ForcedTeams: no script functions.
	CHECK(log.find("forced teams: off (no [Game] ForcedTeams in xml2-fix.ini)") != std::string::npos && log.find("script functions added") == std::string::npos);
	CHECK(log.find("postgame:") == std::string::npos); // no [Game] PostgameScript: not a word, nothing patched
	CHECK(log.find("main menu:") == std::string::npos); // no [Game] MainMenuItems: not a word, nothing patched
	CHECK(log.find("xmenlegpc.master.gamespy.com -> xmenlegpc.master.openspy.net (resolved)") != std::string::npos);
	if (pads > 0)
	{
		CHECK(log.find("hooked a DirectInput 8 instance") != std::string::npos);
		CHECK(log.find("as Logitech Dual Action #2") == std::string::npos); // one pad seen by both paths
	}

	failures += run_pipe_child();

	std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
