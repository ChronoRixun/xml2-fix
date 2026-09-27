// Runs next to the built dinput.dll and imports it, so Windows loads the fix exactly the way
// it does for X-Men Legends II, then reads the pad both ways the game does:
//   - the engine's DirectInput 7: DirectInputCreateEx, CreateDevice + QueryInterface to
//     IDirectInputDevice2A, ranges set by offset, c_dfDIJoystick;
//   - the game's own DirectInput 8: dinput8.dll loaded from the system folder by full path,
//     EnumObjects, ranges set by object id, c_dfDIJoystick2.
// With an Xbox-compatible pad connected, both must see a Logitech Dual Action. Also checks
// that GameSpy host lookups resolve through OpenSpy.
//
//   xml2_test.exe          run the checks
//   xml2_test.exe --live   also show live pad input, as the game sees it, for 20 seconds

#define DIRECTINPUT_VERSION 0x0800
#include <WinSock2.h>
#include <Windows.h>
#include <dinput.h>
#include <Xinput.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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

	const auto log = read_file(module_dir() / "xml2-fix.log");
	CHECK(log.find("hooked a DirectInput 7 instance") != std::string::npos);
	CHECK(log.find("GameSpy servers redirected to openspy.net") != std::string::npos);
	CHECK(log.find("xmenlegpc.master.gamespy.com -> xmenlegpc.master.openspy.net (resolved)") != std::string::npos);
	if (pads > 0)
	{
		CHECK(log.find("hooked a DirectInput 8 instance") != std::string::npos);
		CHECK(log.find("as Logitech Dual Action #2") == std::string::npos); // one pad seen by both paths
	}

	std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
