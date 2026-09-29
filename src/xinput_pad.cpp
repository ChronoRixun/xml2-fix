#include "xinput_pad.hpp"
#include "log.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <atomic>
#include <mutex>

namespace xinput_pad
{
	namespace
	{
		using get_state_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

		std::once_flag loaded;
		std::atomic<get_state_t> get_state{nullptr}; // XInputGetState, once its first call has returned
		std::atomic<bool> finished{false};           // loading is over (XInput found or not)
		std::atomic<bool> loading_thread{false};     // start() made the thread that loads it

		get_state_t load_get_state()
		{
			for (const auto* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"})
			{
				if (const auto module = LoadLibraryW(dll))
				{
					if (const auto proc = GetProcAddress(module, "XInputGetState"))
					{
						logger::write("xinput: using %ls", dll);
						return reinterpret_cast<get_state_t>(proc);
					}
				}
			}

			logger::write("ERROR: no XInput DLL available - controllers will not work");
			return nullptr;
		}

		// Loads XInput and asks it once for every pad: XInput 1.4 finds the controllers on its first
		// call, through the device access broker, and that is what takes seconds. Reads go to it only
		// after that.
		void load()
		{
			const auto began = GetTickCount64();
			const auto proc = load_get_state();
			int pads = 0;
			if (proc)
			{
				for (DWORD index = 0; index < max_pads; ++index)
				{
					XINPUT_STATE state{};
					pads += proc(index, &state) == ERROR_SUCCESS;
				}
			}
			get_state.store(proc, std::memory_order_release);
			finished.store(true, std::memory_order_release);
			if (proc)
			{
				logger::write("xinput: ready after %llu ms on thread %lu%s - %d pad(s) connected", GetTickCount64() - began, GetCurrentThreadId(),
				              loading_thread.load() ? " (its own, not the game's; pads read as idle until now)" : "", pads);
			}
		}

		DWORD WINAPI loader(LPVOID)
		{
			std::call_once(loaded, &load);
			return 0;
		}
	}

	void start()
	{
		loading_thread.store(true);
		const HANDLE thread = CreateThread(nullptr, 0, &loader, nullptr, 0, nullptr);
		if (!thread)
		{
			loading_thread.store(false);
			logger::write("xinput: couldn't start the thread that loads XInput (error %lu) - the first pad read loads it", GetLastError());
			return;
		}
		CloseHandle(thread);
	}

	bool starting()
	{
		return loading_thread.load() && !finished.load(std::memory_order_acquire);
	}

	bool read_raw(const int index, raw_state& out)
	{
		if (!finished.load(std::memory_order_acquire))
		{
			if (loading_thread.load())
			{
				return false; // still loading on its own thread: no pad yet, and no waiting for it
			}
			std::call_once(loaded, &load); // no thread (start() wasn't called or failed): load it here
		}
		const auto proc = get_state.load(std::memory_order_acquire);
		XINPUT_STATE raw{};
		if (!proc || index < 0 || index >= max_pads || proc(static_cast<DWORD>(index), &raw) != ERROR_SUCCESS)
		{
			return false;
		}

		const auto& pad = raw.Gamepad;
		out = {pad.wButtons, pad.bLeftTrigger, pad.bRightTrigger, pad.sThumbLX, pad.sThumbLY, pad.sThumbRX, pad.sThumbRY};
		return true;
	}

	std::vector<int> connected_indices()
	{
		static bool was_connected[max_pads]{};

		std::vector<int> result;
		if (starting())
		{
			return result; // nothing to say about connections until XInput is ready
		}
		raw_state ignored{};
		for (int index = 0; index < max_pads; ++index)
		{
			const bool connected = read_raw(index, ignored);
			if (connected != was_connected[index])
			{
				logger::write("xinput: pad %d %s", index + 1, connected ? "connected" : "disconnected");
				was_connected[index] = connected;
			}
			if (connected)
			{
				result.push_back(index);
			}
		}
		return result;
	}
}
