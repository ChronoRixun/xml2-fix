// Runs next to the built dinput.dll and imports it, so Windows loads the fix exactly the way
// it does for X-Men Legends II, then reads the pad both ways the game does:
//   - the engine's DirectInput 7: DirectInputCreateEx, CreateDevice + QueryInterface to
//     IDirectInputDevice2A, ranges set by offset, c_dfDIJoystick;
//   - the game's own DirectInput 8: dinput8.dll loaded from the system folder by full path,
//     EnumObjects, ranges set by object id, c_dfDIJoystick2.
// With an Xbox-compatible pad connected, both must see a Logitech Dual Action. Also checks
// that GameSpy host lookups resolve through OpenSpy, and the display fix's decisions: the
// rules in display_rules.hpp against fixed inputs, and the Video options list the fix would
// build from this PC's Direct3D 8 modes. The test input pipe is checked on its rules, on a
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
#include <Xinput.h>

#include "display_rules.hpp"
#include "frame_capture.hpp"
#include "image_file.hpp"
#include "test_input_rules.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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
		std::string notes;

		auto pp = rewrite_present(stock, borderless, desktop_mode, msaa_ok, format_ok, notes);
		CHECK(pp.windowed == TRUE && pp.fullscreen_refresh_rate == 0 && pp.fullscreen_presentation_interval == 0);
		CHECK(pp.back_buffer_width == 1920 && pp.back_buffer_height == 1080 && pp.multi_sample_type == 4 && pp.swap_effect == d3d8::swap_discard);
		pp = rewrite_present(stock, windowed, desktop_mode, msaa_no, format_ok, notes);
		CHECK(pp.windowed == TRUE && pp.multi_sample_type == d3d8::multisample_none && !notes.empty());
		auto copy_vsync = stock;
		copy_vsync.swap_effect = d3d8::swap_copy_vsync;
		pp = rewrite_present(copy_vsync, borderless, desktop_mode, msaa_ok, format_ok, notes);
		CHECK(pp.multi_sample_type == d3d8::multisample_none && pp.swap_effect == d3d8::swap_copy_vsync);
		pp = rewrite_present(stock, borderless, desktop_mode, msaa_ok, [](DWORD) { return false; }, notes);
		CHECK(pp.back_buffer_format == d3d8::format_x8r8g8b8); // already the desktop's: not checked, not changed
		auto sixteen_bit = stock;
		sixteen_bit.back_buffer_format = 23;
		pp = rewrite_present(sixteen_bit, borderless, desktop_mode, msaa_ok, [](DWORD) { return false; }, notes);
		CHECK(pp.back_buffer_format == d3d8::format_x8r8g8b8);
		pp = rewrite_present(stock, fullscreen, desktop_mode, msaa_ok, format_ok, notes);
		CHECK(pp.windowed == FALSE && pp.fullscreen_refresh_rate == 0 && pp.multi_sample_type == 4); // not the desktop size: untouched
		auto native = stock;
		native.back_buffer_width = 2560;
		native.back_buffer_height = 1440;
		pp = rewrite_present(native, fullscreen, desktop_mode, msaa_ok, format_ok, notes);
		CHECK(pp.windowed == FALSE && pp.fullscreen_refresh_rate == 180);
		options stock_options;
		pp = rewrite_present(stock, stock_options, desktop_mode, msaa_ok, format_ok, notes);
		CHECK(std::memcmp(&pp, &stock, sizeof(pp)) == 0);

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
		std::printf("  info  the game gets %zu:", list.size());
		for (const auto& mode : list) std::printf(" %ux%u", mode.width, mode.height);
		std::printf("\n");
		CHECK(list.size() <= 20);
		CHECK(std::ranges::any_of(list, [&](const auto& m) { return m.width == desktop.width && m.height == desktop.height; }));
		check_d3d8_capture(d3d);
		static_cast<IUnknown*>(d3d)->Release();
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
		CHECK(refused(ask(pipe, "frob")));
		CHECK(refused(ask(pipe, "tap NOSUCHKEY")));
		CHECK(refused(ask(pipe, "screenshot")));
		CHECK(refused(ask(pipe, "tap ENTER 20"))); // nothing reads a keyboard yet
		const auto shot = ask(pipe, "screenshot " + (std::filesystem::temp_directory_path() / "xml2_test_pipe.png").string());
		std::printf("  info  %s\n", shot.c_str());
		CHECK(refused(shot)); // no Direct3D device in this process: times out

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
		CHECK(log.find("display: as the game has it") != std::string::npos && log.find("hooked for the test pipe") != std::string::npos);
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
			out << "[Test]\r\nInputPipe=1\r\n";
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
	check_test_input_rules();
	check_image_file();
	check_d3d8_modes();

	const auto log = read_file(module_dir() / "xml2-fix.log");
	CHECK(log.find("hooked a DirectInput 7 instance") != std::string::npos);
	CHECK(log.find("display: as the game has it") != std::string::npos); // no [Display] section next to the test
	CHECK(log.find("test:") == std::string::npos);                       // and no [Test] section: no pipe
	CHECK(log.find("GameSpy servers redirected to openspy.net") != std::string::npos);
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
