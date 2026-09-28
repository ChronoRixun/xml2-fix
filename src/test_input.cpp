#include "test_input.hpp"
#include "display.hpp"
#include "frame_capture.hpp"
#include "frame_rate.hpp"
#include "log.hpp"
#include "test_input_rules.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace test_input
{
	namespace
	{
		using namespace test_input_rules;

		constexpr const char* pipe_name = "\\\\.\\pipe\\xml2-fix-input";
		constexpr DWORD read_wait_ms = 500;     // for the game to poll the keyboard once
		constexpr DWORD capture_wait_ms = 3000; // for the game to draw a frame

		bool enabled = false;

		// The game's DirectInput 8 keyboard device(s). All devices of the class share one vtable, so
		// the hooks check the object.
		std::mutex devices_mutex;
		std::set<void*> keyboards;

		constexpr int slot_create_device = 3;
		constexpr int slot_get_device_state = 9;
		constexpr int slot_set_cooperative_level = 13;

		using create_device_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, void**, LPUNKNOWN);
		using get_device_state_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPVOID);
		using set_cooperative_level_t = HRESULT(STDMETHODCALLTYPE*)(void*, HWND, DWORD);

		create_device_t real_create_device = nullptr;
		get_device_state_t real_get_device_state = nullptr;
		set_cooperative_level_t real_set_cooperative_level = nullptr;

		// What the pipe holds down, and how often the game has read the keyboard.
		std::mutex keys_mutex;
		synthetic_keys keys;
		std::atomic<unsigned> reads{0};
		HANDLE read_event = nullptr; // auto-reset; set on every keyboard read

		// A pending screenshot: the pipe thread asks, the render thread copies the back buffer.
		std::mutex capture_mutex;
		std::atomic<bool> capture_wanted{false};
		frame_capture::frame captured;
		std::string capture_error;
		HANDLE capture_done = nullptr; // auto-reset

		// Replaces one vtable slot in place, once, keeping the original. Refuses a second, different
		// vtable (another class) so there is one chain of originals.
		template <typename T>
		bool hook_slot(void* object, const int slot, T replacement, T& original)
		{
			auto** vtable = *static_cast<void***>(object);
			if (vtable[slot] == reinterpret_cast<void*>(replacement))
			{
				return true;
			}
			if (original)
			{
				return false;
			}
			DWORD old_protect = 0;
			VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &old_protect);
			original = reinterpret_cast<T>(vtable[slot]);
			vtable[slot] = reinterpret_cast<void*>(replacement);
			VirtualProtect(&vtable[slot], sizeof(void*), old_protect, &old_protect);
			return true;
		}

		bool is_keyboard(void* device)
		{
			std::lock_guard lock(devices_mutex);
			return keyboards.contains(device);
		}

		bool game_in_foreground()
		{
			DWORD process = 0;
			if (const HWND foreground = GetForegroundWindow())
			{
				GetWindowThreadProcessId(foreground, &process);
			}
			return process == GetCurrentProcessId();
		}

		std::string keys_text(const std::vector<unsigned char>& codes)
		{
			std::string text;
			for (const auto code : codes)
			{
				text += (text.empty() ? "" : "+") + name_of(code);
			}
			return text;
		}

		// ---- The game's keyboard -----------------------------------------------------------------

		HRESULT STDMETHODCALLTYPE hooked_get_device_state(void* self, const DWORD size, LPVOID data)
		{
			const HRESULT result = real_get_device_state(self, size, data);
			if (size != 256 || !data || !is_keyboard(self))
			{
				return result;
			}
			if (FAILED(result))
			{
				logger::write_once("test:read-failed", "test: a keyboard read failed (%08lX) - the game re-acquires; pipe keys wait for that", result);
				return result;
			}

			auto* state = static_cast<unsigned char*>(data);
			if (!game_in_foreground())
			{
				std::memset(state, 0, 256); // typing in another window stays there, as with the game's own foreground-only device
			}

			std::vector<unsigned char> expired;
			{
				std::lock_guard lock(keys_mutex);
				keys.merge(state, GetTickCount64(), expired);
			}
			for (const auto code : expired)
			{
				logger::write("test: %s released - held for %lu ms without the client letting go", name_of(code).c_str(), max_hold_ms);
			}
			if (reads.fetch_add(1) == 0)
			{
				logger::write("test: the game reads its DirectInput keyboard - pipe keys reach it");
			}
			SetEvent(read_event);
			return result;
		}

		HRESULT STDMETHODCALLTYPE hooked_set_cooperative_level(void* self, const HWND window, const DWORD flags)
		{
			if (!is_keyboard(self))
			{
				return real_set_cooperative_level(self, window, flags);
			}

			constexpr DWORD ours = DISCL_BACKGROUND | DISCL_NONEXCLUSIVE;
			const HRESULT result = real_set_cooperative_level(self, window, ours);
			logger::write("test: keyboard cooperative level %lX -> %lX (background, non-exclusive): %s", flags, ours, SUCCEEDED(result) ? "ok" : "refused");
			if (FAILED(result))
			{
				return real_set_cooperative_level(self, window, flags); // the game's own, as if we weren't here
			}
			return result;
		}

		HRESULT STDMETHODCALLTYPE hooked_create_device(void* self, REFGUID instance, void** device, LPUNKNOWN outer)
		{
			const HRESULT result = real_create_device(self, instance, device, outer);
			if (FAILED(result) || !device || !*device || !IsEqualGUID(instance, GUID_SysKeyboard))
			{
				return result;
			}

			{
				std::lock_guard lock(devices_mutex);
				keyboards.insert(*device);
			}
			// After the pad fix's own hooks on this vtable (it hooked them inside the call above), so both chains stand.
			const bool state_hooked = hook_slot(*device, slot_get_device_state, &hooked_get_device_state, real_get_device_state);
			const bool level_hooked = hook_slot(*device, slot_set_cooperative_level, &hooked_set_cooperative_level, real_set_cooperative_level);
			if (state_hooked && level_hooked)
			{
				logger::write("test: the game created its DirectInput keyboard - the pipe's keys go into it");
			}
			else
			{
				logger::write("test: ERROR: the game's keyboard uses a second DirectInput device class - pipe keys can't reach it");
			}
			return result;
		}

		// ---- Screenshots ----------------------------------------------------------------------------

		// On the game's render thread, just before Present.
		void on_frame(void* device)
		{
			if (!capture_wanted.exchange(false))
			{
				return;
			}
			frame_capture::frame picture;
			std::string error = frame_capture::read_back_buffer(device, picture);
			{
				std::lock_guard lock(capture_mutex);
				captured = std::move(picture);
				capture_error = std::move(error);
			}
			SetEvent(capture_done);
		}

		std::string screenshot(const std::string& utf8_path)
		{
			const int length = MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(), -1, nullptr, 0);
			std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
			if (length > 1)
			{
				MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(), -1, wide.data(), length);
			}
			const std::filesystem::path path(wide);

			ResetEvent(capture_done);
			capture_wanted = true;
			if (WaitForSingleObject(capture_done, capture_wait_ms) != WAIT_OBJECT_0)
			{
				capture_wanted = false;
				logger::write("test: screenshot: no frame within %lu ms", capture_wait_ms);
				return "error no frame within " + std::to_string(capture_wait_ms) + " ms - is the game drawing? (the fix hooks Direct3D only when the pipe was on at start)";
			}

			frame_capture::frame picture;
			std::string error;
			{
				std::lock_guard lock(capture_mutex);
				picture = std::move(captured);
				error = capture_error;
				captured = {};
			}
			if (error.empty())
			{
				error = frame_capture::save(picture, path);
			}
			if (!error.empty())
			{
				logger::write("test: screenshot %ls failed: %s", path.c_str(), error.c_str());
				return "error " + error;
			}
			logger::write("test: screenshot %ux%u -> %ls", picture.width, picture.height, path.c_str());
			return "ok " + std::to_string(picture.width) + "x" + std::to_string(picture.height) + " " + utf8_path;
		}

		// ---- Commands ---------------------------------------------------------------------------------

		void press(const std::vector<unsigned char>& codes, const DWORD ms)
		{
			const ULONGLONG now = GetTickCount64();
			std::lock_guard lock(keys_mutex);
			for (const auto code : codes)
			{
				keys.press(code, now + ms);
			}
		}

		void release(const std::vector<unsigned char>& codes)
		{
			std::lock_guard lock(keys_mutex);
			for (const auto code : codes)
			{
				keys.release(code);
			}
		}

		// Waits until the game has read the keyboard once more (or `timeout` passes).
		bool wait_for_read(const unsigned reads_before, const DWORD timeout)
		{
			if (reads.load() != reads_before)
			{
				return true;
			}
			return WaitForSingleObject(read_event, timeout) == WAIT_OBJECT_0 || reads.load() != reads_before;
		}

		// Down for `ms` and at least one read, then up and read once more, so back-to-back taps stay apart.
		std::string press_and_release(const std::vector<unsigned char>& codes, const DWORD ms, const char* what)
		{
			const unsigned before = reads.load();
			ResetEvent(read_event);
			press(codes, ms + 2 * read_wait_ms); // expiry is a safety net; we release explicitly
			Sleep(ms);
			const bool seen = wait_for_read(before, read_wait_ms);

			const unsigned at_release = reads.load();
			ResetEvent(read_event);
			release(codes);
			if (seen)
			{
				wait_for_read(at_release, read_wait_ms);
			}

			logger::write("test: %s %s %lu ms%s", what, keys_text(codes).c_str(), ms, seen ? "" : " - the game didn't read the keyboard meanwhile");
			return seen ? "ok" : "error the game didn't read the keyboard while the key was down (no DirectInput keyboard yet, or it isn't polling)";
		}

		// Win32 key messages to the game window, for the Advanced Options panel (it reads WM_KEYUP,
		// never DirectInput). Posted, so the game's own thread handles them when it pumps messages;
		// the window needn't have the focus.
		std::string post_keys(const std::vector<unsigned char>& codes, const DWORD ms)
		{
			const HWND target = display::game_window();
			if (!target)
			{
				return "error no game window yet (the engine hasn't created it)";
			}
			std::vector<win32_key> messages;
			std::string virtual_keys;
			for (const auto code : codes)
			{
				const auto key = win32_key_for(code);
				if (!key)
				{
					return "error no Windows virtual key for " + name_of(code);
				}
				messages.push_back(*key);
				char text[8];
				std::snprintf(text, sizeof(text), "0x%02X", key->virtual_key);
				virtual_keys += (virtual_keys.empty() ? "" : "+") + std::string(text);
			}
			for (const auto& key : messages)
			{
				PostMessageA(target, WM_KEYDOWN, key.virtual_key, static_cast<LPARAM>(key_lparam(key.scancode, key.extended, false)));
			}
			Sleep(ms);
			for (auto it = messages.rbegin(); it != messages.rend(); ++it)
			{
				PostMessageA(target, WM_KEYUP, it->virtual_key, static_cast<LPARAM>(key_lparam(it->scancode, it->extended, true)));
			}
			logger::write("test: wm %s %lu ms -> window %p (virtual key%s %s)", keys_text(codes).c_str(), ms, static_cast<void*>(target), messages.size() == 1 ? "" : "s",
			              virtual_keys.c_str());
			return "ok";
		}

		std::string status()
		{
			int held = 0;
			{
				std::lock_guard lock(keys_mutex);
				held = keys.held();
			}
			size_t devices = 0;
			{
				std::lock_guard lock(devices_mutex);
				devices = keyboards.size();
			}
			// fps: Presents counted over the last full second (0 until then), so a frame cap can be
			// checked from a script.
			return "ok XML2 Fix " FIX_VERSION "; keyboard devices " + std::to_string(devices) + "; reads " + std::to_string(reads.load()) + "; keys held " +
			       std::to_string(held) + "; game " + (game_in_foreground() ? "has" : "doesn't have") + " the focus; fps " +
			       frame_rate_rules::fps_text(frame_rate::measured_fps_x10()) + "; frame rate " + frame_rate::describe();
		}

		std::string handle(const std::string& line)
		{
			const command cmd = parse_command(line);
			switch (cmd.what)
			{
			case command::kind::empty:
				return "ok";
			case command::kind::ping:
				return "ok pong";
			case command::kind::status:
				return status();
			case command::kind::down:
				press(cmd.keys, cmd.ms ? cmd.ms : max_hold_ms);
				logger::write("test: down %s (%lu ms at most)", keys_text(cmd.keys).c_str(), cmd.ms ? cmd.ms : max_hold_ms);
				return "ok";
			case command::kind::up:
				release(cmd.keys);
				logger::write("test: up %s", keys_text(cmd.keys).c_str());
				return "ok";
			case command::kind::release:
			{
				std::lock_guard lock(keys_mutex);
				keys.release_all();
				logger::write("test: release all");
				return "ok";
			}
			case command::kind::tap:
				return press_and_release(cmd.keys, cmd.ms ? cmd.ms : default_tap_ms, "tap");
			case command::kind::hold:
				return press_and_release(cmd.keys, cmd.ms, "hold");
			case command::kind::wm:
				return post_keys(cmd.keys, cmd.ms ? cmd.ms : default_tap_ms);
			case command::kind::screenshot:
				return screenshot(cmd.path);
			default:
				logger::write("test: rejected '%s': %s", line.c_str(), cmd.error.c_str());
				return "error " + cmd.error;
			}
		}

		// ---- The pipe -----------------------------------------------------------------------------------

		DWORD WINAPI serve(LPVOID)
		{
			for (;;)
			{
				const HANDLE pipe = CreateNamedPipeA(pipe_name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
				if (pipe == INVALID_HANDLE_VALUE)
				{
					logger::write_once("test:pipe-create", "test: ERROR: CreateNamedPipe %s failed (error %lu) - another game with the pipe on?", pipe_name, GetLastError());
					Sleep(2000);
					continue;
				}
				if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
				{
					CloseHandle(pipe);
					Sleep(100);
					continue;
				}

				logger::write("test: pipe client connected");
				std::string pending;
				char buffer[512];
				DWORD got = 0;
				while (ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) && got)
				{
					pending.append(buffer, got);
					size_t newline;
					while ((newline = pending.find('\n')) != std::string::npos)
					{
						std::string line = pending.substr(0, newline);
						pending.erase(0, newline + 1);
						if (!line.empty() && line.back() == '\r')
						{
							line.pop_back();
						}
						const std::string reply = handle(line) + "\n";
						DWORD written = 0;
						WriteFile(pipe, reply.c_str(), static_cast<DWORD>(reply.size()), &written, nullptr);
					}
					if (pending.size() > 4096)
					{
						pending.clear(); // not a line
					}
				}
				logger::write("test: pipe client disconnected");
				FlushFileBuffers(pipe);
				DisconnectNamedPipe(pipe);
				CloseHandle(pipe);
			}
		}
	}

	void install()
	{
		const auto ini = (logger::module_dir() / L"xml2-fix.ini").wstring();
		enabled = GetPrivateProfileIntW(L"Test", L"InputPipe", 0, ini.c_str()) != 0;
		if (!enabled)
		{
			return;
		}

		read_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		capture_done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		const HANDLE thread = read_event && capture_done ? CreateThread(nullptr, 0, &serve, nullptr, 0, nullptr) : nullptr;
		if (!thread)
		{
			logger::write("test: ERROR: couldn't start the input pipe thread (error %lu)", GetLastError());
			enabled = false;
			return;
		}
		CloseHandle(thread);

		display::set_frame_hook(&on_frame);
		display::disable_multisampling("the test pipe copies the back buffer");
		display::show_without_focus("the test pipe drives the game in the background");
		logger::write("test: input pipe %s ([Test] InputPipe=1) - keys from it reach the game without the focus; the keyboard only with it", pipe_name);
	}

	void hook_direct_input8(void* direct_input)
	{
		if (!enabled || !direct_input)
		{
			return;
		}
		if (hook_slot(direct_input, slot_create_device, &hooked_create_device, real_create_device))
		{
			logger::write_once("test:di8", "test: watching the game's DirectInput 8 for its keyboard");
		}
	}
}
