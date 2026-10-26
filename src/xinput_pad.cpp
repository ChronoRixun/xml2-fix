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
		std::atomic<bool> cache_connections{true};
		std::atomic<unsigned> connected_mask{0};
		std::atomic<unsigned> failed_reads{0};
		HANDLE refresh_event = nullptr;

		void refresh_connections(get_state_t proc)
		{
			const unsigned before = failed_reads.load();
			unsigned old = connected_mask.load();
			unsigned mask = 0;
			for (DWORD index = 0; index < max_pads; ++index)
			{
				XINPUT_STATE state{};
				if (proc(index, &state) == ERROR_SUCCESS) mask |= 1u << index;
			}
			// A read failed during discovery: do not resurrect a disconnected slot
			// from an older observation. Its event triggers another pass immediately.
			if (failed_reads.load() != before) return;
			if (!connected_mask.compare_exchange_strong(old, mask)) return;
			for (int index = 0; index < max_pads; ++index)
				if ((old ^ mask) & (1u << index))
					logger::write("xinput: pad %d %s", index + 1, mask & (1u << index) ? "connected" : "disconnected");
		}

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
				if (cache_connections)
				{
					refresh_connections(proc);
					const auto mask = connected_mask.load();
					for (int i = 0; i < max_pads; ++i) pads += (mask >> i) & 1;
				}
				else
				{
					for (DWORD index = 0; index < max_pads; ++index)
					{
						XINPUT_STATE state{};
						pads += proc(index, &state) == ERROR_SUCCESS;
					}
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
			HMODULE pinned = nullptr;
			// This runs after DllMain, outside the loader lock. The connection
			// worker lives until process exit, like the input provider it owns.
			if (cache_connections && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
				reinterpret_cast<LPCWSTR>(&loader), &pinned))
			{
				cache_connections = false;
				logger::write("xinput: connection worker unavailable (module pin error %lu) - original discovery retained", GetLastError());
			}
			std::call_once(loaded, &load);
			const auto proc = get_state.load(std::memory_order_acquire);
			while (cache_connections && proc)
			{
				WaitForSingleObject(refresh_event, 1000);
				refresh_connections(proc);
			}
			return 0;
		}
	}

	void start(const bool cache)
	{
		cache_connections = cache;
		if (cache_connections)
		{
			refresh_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (!refresh_event)
			{
				cache_connections = false;
				logger::write("xinput: connection worker event unavailable (error %lu) - original discovery retained", GetLastError());
			}
		}
		loading_thread.store(true);
		const HANDLE thread = CreateThread(nullptr, 0, &loader, nullptr, 0, nullptr);
		if (!thread)
		{
			const auto error = GetLastError();
			cache_connections = false;
			if (refresh_event) { CloseHandle(refresh_event); refresh_event = nullptr; }
			loading_thread.store(false);
			logger::write("xinput: couldn't start the thread that loads XInput (error %lu) - the first pad read loads it", error);
			return;
		}
		CloseHandle(thread);
	}

	bool starting()
	{
		return loading_thread.load() && !finished.load(std::memory_order_acquire);
	}

	bool connections_cached() { return cache_connections.load(); }

	bool read_raw(const int index, raw_state& out)
	{
		if (!finished.load(std::memory_order_acquire))
		{
			if (loading_thread.load())
			{
				return false; // still loading on its own thread: no pad yet, and no waiting for it
			}
			cache_connections = false; // no connection worker: retain lazy discovery
			std::call_once(loaded, &load); // no thread (start() wasn't called or failed): load it here
		}
		const auto proc = get_state.load(std::memory_order_acquire);
		XINPUT_STATE raw{};
		if (!proc || index < 0 || index >= max_pads || (cache_connections && !(connected_mask.load() & (1u << index)))) return false;
		if (proc(static_cast<DWORD>(index), &raw) != ERROR_SUCCESS)
		{
			if (cache_connections)
			{
				connected_mask.fetch_and(~(1u << index));
				failed_reads.fetch_add(1);
				SetEvent(refresh_event);
			}
			return false;
		}

		const auto& pad = raw.Gamepad;
		out = {pad.wButtons, pad.bLeftTrigger, pad.bRightTrigger, pad.sThumbLX, pad.sThumbLY, pad.sThumbRX, pad.sThumbRY};
		return true;
	}

	std::vector<int> connected_indices()
	{
		if (!loading_thread.load() && !finished.load(std::memory_order_acquire)) cache_connections = false;
		if (cache_connections)
		{
			std::vector<int> result;
			const unsigned mask = connected_mask.load();
			for (int index = 0; index < max_pads; ++index)
				if (mask & (1u << index)) result.push_back(index);
			return result;
		}
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
