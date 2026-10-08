#include "confirmation_release.hpp"
#include "confirmation_release_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstring>

namespace confirmation_release
{
	namespace
	{
		using namespace confirmation_release_rules;
		latch held;
		physical_state physical;
		bool raw_valid = false;
		bool keyboard_read = false, mouse_read = false;
		std::array<source, 16> pending{};
		std::size_t pending_count = 0;
		void* pending_popup = nullptr;
		bool warned = false, logged_block = false;

		template <typename T> T at(const std::uint32_t va) { return reinterpret_cast<T>(static_cast<std::uintptr_t>(va)); }
		template <typename T> T method(void* object, const unsigned slot) { return reinterpret_cast<T>((*static_cast<void***>(object))[slot / 4]); }
		using int_t = int(__fastcall*)(void*, void*);
		using indexed_bool_t = bool(__fastcall*)(void*, void*, int);
		using controller_t = void*(__fastcall*)(void*, void*, int);
		using mask_t = unsigned(__fastcall*)(void*, void*);

		void __cdecl note_read(const HRESULT result, const unsigned device)
		{
			if (device == 1) keyboard_read = SUCCEEDED(result);
			else mouse_read = SUCCEEDED(result);
		}

		// Before the poll's modal input suppression: +0x25e4 is still the physical keyboard,
		// +0x4c8 the mouse buttons, +0x4f0 the ten freshly polled pads. No async OS/XInput reads.
		bool read_raw(const std::uint8_t* input)
		{
			__try
			{
				if (!input || input != *at<const std::uint8_t**>(0xa6abfc)) return false;
				std::memcpy(physical.keys.data(), input + 0x25e4, physical.keys.size());
				std::memcpy(physical.mouse.data(), input + 0x4c8, physical.mouse.size());
				const bool keyboard_present = *reinterpret_cast<const void* const*>(input + 4) != nullptr;
				const bool mouse_present = *reinterpret_cast<const void* const*>(input + 8) != nullptr;
				physical.keyboard_known = !keyboard_present || keyboard_read;
				physical.mouse_known = !mouse_present || mouse_read;
				if (!keyboard_present) physical.keys.fill(0);
				if (!mouse_present) physical.mouse.fill(0);
				const unsigned pads_read = *reinterpret_cast<const unsigned*>(input + 0x129cc);
				for (std::size_t i = 0; i < physical.pads.size(); ++i)
				{
					const auto* state = input + 0x4f0 + i * 0x110;
					std::memcpy(physical.pads[i].axes.data(), state, 32);
					std::memcpy(&physical.pads[i].pov, state + 0x20, 4);
					std::memcpy(physical.pads[i].buttons.data(), state + 0x30, physical.pads[i].buttons.size());
					const bool present = *reinterpret_cast<const void* const*>(input + 0xc + i * 4) != nullptr;
					physical.pad_known[i] = !present || (pads_read & (1u << i)) != 0;
					if (!present) physical.pads[i] = {};
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		void __cdecl sample_raw(const std::uint8_t* input)
		{
			pending_count = 0;
			pending_popup = nullptr;
			raw_valid = read_raw(input);
			if (raw_valid) held.sample(physical);
			keyboard_read = mouse_read = false; // next poll must establish success again
		}

		// Follow exactly the native popup's logical player gates, then its controller remapping.
		// The mask checks still belong to the game; this only identifies sources of an already
		// requested acceptance. In particular player 1's held key does not consume player 2's pad.
		bool candidates(std::uint8_t* popup, void* manager, const unsigned primary, const unsigned fallback,
		                std::array<source, 16>& found, std::size_t& count)
		{
			__try
			{
				void* players = at<void*>(0x79ebc0);
				void* network = at<void*>(0x72c4f0);
				if (manager != *at<void**>(0x8b13ec) || *static_cast<const unsigned*>(players) != 0x69a134 || *static_cast<const unsigned*>(network) != 0x6898c4 ||
				    *static_cast<const unsigned*>(manager) != 0x6a332c) return false;
				const unsigned row = *at<const unsigned*>(0xa6ac04);
				if (row >= 5) return false;
				const int owner = static_cast<signed char>(popup[0x155c]);
				const int active = method<int_t>(players, 0x60)(players, nullptr);
				const int player_count = method<int_t>(players, 0x70)(players, nullptr);
				if (player_count < 1 || player_count > 4) return false;
				const bool aggregate = (static_cast<const std::uint8_t*>(manager)[0x4056] & 2) != 0;
				const bool active_remote = method<indexed_bool_t>(network, 0x10)(network, nullptr, active);
				for (int player = 0; player < player_count; ++player)
				{
					const bool participant = method<indexed_bool_t>(players, 0x54)(players, nullptr, player);
					const bool aggregate_allowed = method<indexed_bool_t>(network, 0x24)(network, nullptr, player);
					const bool eligible = eligible_player(owner, player, primary, fallback, aggregate, active, active_remote, participant, aggregate_allowed);
					if (!eligible) continue;
					void* controller = method<controller_t>(players, 0x4c)(players, nullptr, player);
					if (!controller || *static_cast<const unsigned*>(controller) != 0x69a0f4) return false;
					if (!(method<mask_t>(controller, 0x18)(controller, nullptr) & 16)) continue;
					int column = static_cast<const int*>(players)[player + 1];
					if (column < 0 || column >= 4) column = player;
					const auto* config = at<const std::uint8_t* const*>(0xa68f40)[row * 4 + column];
					if (!config || *reinterpret_cast<const unsigned*>(config + 0x18) <= 4) return false;
					for (unsigned slot = 0; slot < 4; ++slot)
					{
						const auto* record = config + 0x18 + 4 + (4 * 4 + slot) * 12;
						const source s{*reinterpret_cast<const unsigned*>(record), *reinterpret_cast<const unsigned*>(record + 4)};
						const bool contributes = *reinterpret_cast<const float*>(record + 8) >= 0.5f;
						if (!supported(s)) { if (contributes) return false; continue; }
						if (!known(physical, s))
						{
							// A failed read isn't evidence of release, even if the game zeroed its
							// buffer and binding value. Another known fresh source may still answer.
							if (!held.available(s)) found[count++] = s;
							else if (contributes) return false;
						}
						else if (contributes && down(physical, s)) found[count++] = s;
					}
				}
				return count != 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		int __cdecl allow_popup(std::uint8_t* popup, void* manager, const unsigned primary, const unsigned fallback)
		{
			pending_count = 0;
			pending_popup = nullptr;
			std::array<source, 16> found{};
			std::size_t count = 0;
			if (!raw_valid || !candidates(popup, manager, primary, fallback, found, count))
			{
				if (!warned)
				{
					warned = true;
					logger::write("confirmation release: can't identify a physical accept binding - this acceptance stays native (logged once)");
				}
				return 1;
			}
			const std::span<const source> sources(found.data(), count);
			if (!held.allows(sources))
			{
				if (!logged_block)
				{
					logged_block = true;
					logger::write("confirmation release: held accept blocked before popup acceptance; release its binding for another answer (logged once)");
				}
				return 0;
			}
			// Only newly available sources contributed this answer. Existing consumed sources stay
			// latched, and simultaneous fresh sources cannot answer a second popup in this poll.
			for (const auto s : sources) if (held.available(s)) pending[pending_count++] = s;
			pending_popup = popup;
			return 1;
		}

		void __cdecl consume_popup(void* popup, const unsigned visible)
		{
			if ((visible & 0xff) && popup == pending_popup) held.consume({pending.data(), pending_count});
			pending_count = 0;
			pending_popup = nullptr;
		}

		DWORD raw_next = raw_continue, accept_next = accept_continue, rejected_next = cancel_check, close_next = close_continue;
		DWORD keyboard_next = keyboard_continue, keyboard_retry_next = keyboard_retry_continue, mouse_next = mouse_continue;
		int verdict = 1;
		__declspec(naked) void keyboard_stub()
		{
			__asm {
				pushfd
				pushad
				push 1
				push eax
				call note_read
				add esp, 8
				popad
				popfd
				cmp eax, 0x8007001e
				jmp dword ptr [keyboard_next]
			}
		}
		__declspec(naked) void keyboard_retry_stub()
		{
			__asm {
				push 0x100
				push eax
				call dword ptr [ecx + 0x24]
				pushfd
				pushad
				push 1
				push eax
				call note_read
				add esp, 8
				popad
				popfd
				jmp dword ptr [keyboard_retry_next]
			}
		}
		__declspec(naked) void mouse_stub()
		{
			__asm {
				mov edx, [eax]
				push 0x14
				push eax
				call dword ptr [edx + 0x24]
				pushfd
				pushad
				push 2
				push eax
				call note_read
				add esp, 8
				popad
				popfd
				jmp dword ptr [mouse_next]
			}
		}
		__declspec(naked) void raw_stub()
		{
			__asm {
				mov edi, [esp + 0x10]
				pushfd
				pushad
				push edi
				call sample_raw
				add esp, 4
				popad
				popfd
				mov al, [edi + 1]
				jmp dword ptr [raw_next]
			}
		}
		__declspec(naked) void accept_stub()
		{
			__asm {
				pushfd
				pushad
				mov eax, [esp + 0x34] // original esp+0x10 (pushfd + pushad = 0x24)
				push ebx
				push eax
				push edi
				push esi
				call allow_popup
				add esp, 16
				mov dword ptr [verdict], eax
				popad
				popfd
				cmp dword ptr [verdict], 0
				je rejected
				mov al, [esi + 0x1558]
				jmp dword ptr [accept_next]
			rejected:
				jmp dword ptr [rejected_next]
			}
		}
		__declspec(naked) void close_stub()
		{
			__asm {
				mov eax, [edi]
				mov ecx, edi
				call dword ptr [eax + 0x78]
				pushfd
				pushad
				push eax
				push esi
				call consume_popup
				add esp, 8
				popad
				popfd
				jmp dword ptr [close_next]
			}
		}

		const guard* mismatch()
		{
			__try { return first_mismatch(at<const std::uint8_t*>(image_base)); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &guards[0]; }
		}
		bool patch()
		{
			const std::array sites{raw_site, accept_site, close_site, keyboard_site, keyboard_retry_site, mouse_site};
			const std::array targets{&raw_stub, &accept_stub, &close_stub, &keyboard_stub, &keyboard_retry_stub, &mouse_stub};
			const std::array<std::size_t, 6> sizes{7, 6, 7, 5, 9, 8};
			std::array<DWORD, 6> protections{};
			// Unprotect all sites before any write; failures leave every byte native.
			for (std::size_t i = 0; i < sites.size(); ++i)
			{
				if (!VirtualProtect(at<void*>(sites[i]), sizes[i], PAGE_EXECUTE_READWRITE, &protections[i]))
				{
					const auto error = GetLastError();
					for (std::size_t j = i; j-- > 0;) { DWORD unused; VirtualProtect(at<void*>(sites[j]), sizes[j], protections[j], &unused); }
					logger::write("confirmation release: can't unprotect 0x%08lX (error %lu) - nothing patched", sites[i], error);
					return false;
				}
			}
			for (std::size_t i = 0; i < sites.size(); ++i)
			{
				const auto bytes = jump(sites[i], static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(targets[i])));
				std::memcpy(at<void*>(sites[i]), bytes.data(), sizes[i]);
			}
			for (std::size_t i = sites.size(); i-- > 0;)
			{
				FlushInstructionCache(GetCurrentProcess(), at<void*>(sites[i]), sizes[i]);
				DWORD unused;
				VirtualProtect(at<void*>(sites[i]), sizes[i], protections[i], &unused);
			}
			return true;
		}
	}

	void install(const HMODULE game)
	{
		const auto value = ini::text(L"Game", L"ConfirmationRelease");
		if (!enabled(value))
		{
			logger::write("confirmation release: off ([Game] ConfirmationRelease=1 to enable) - the game's own popup input");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("confirmation release: image base isn't 0x400000 - nothing patched");
			return;
		}
		if (const auto* g = mismatch())
		{
			logger::write("confirmation release: 0x%08lX isn't retail (%s) - nothing patched", static_cast<DWORD>(g->va), g->what);
			return;
		}
		if (patch()) logger::write("confirmation release: on - raw poll 0x62887D, acceptance gate 0x5EB7C6, close 0x5EB8AB; read results at 0x628621, 0x628642, 0x6286B7; a binding answers once until a successful read proves release");
	}
}
