#include "conversations.hpp"

#include "conversations_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace conversations
{
	namespace
	{
		using namespace conversations_rules;

		template <typename T>
		T at(const DWORD va)
		{
			return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));
		}

		// The virtual function at byte offset `slot` of `object`'s vtable. `this` travels in ecx
		// (__thiscall); __fastcall with an unused edx calls it the same way.
		template <typename T>
		T method(void* object, const DWORD slot)
		{
			return reinterpret_cast<T>((*static_cast<void***>(object))[slot / 4]);
		}

		using getter_t = void*(__cdecl*)();
		using accept_t = bool(__fastcall*)(void* menus, void* edx, int action);       // __thiscall, ret 4
		using menu_name_t = const char*(__fastcall*)(void* menus, void* edx);           // __thiscall
		using playing_t = bool(__fastcall*)(void* audio, void* edx, std::uint32_t handle); // __thiscall, ret 4
		using stop_t = void(__fastcall*)(void* audio, void* edx, std::uint32_t handle);    // __thiscall, ret 4
		using game_time_t = float(__fastcall*)(void* game, void* edx);                   // __thiscall, st(0)
		using node_lookup_t = std::uint8_t*(__fastcall*)(void* cs, void* edx, int id);   // __thiscall, ret 4
		using pick_t = void(__fastcall*)(void* cs, void* edx, int index);               // __thiscall, ret 4

		// ---- What the hooks keep. The game calls them on its one thread. ------------------------------

		line_state line;          // AutoAdvance: the line being watched
		bool pending_waiting = false;    // ReplyVoices: a reply's voice is being waited for
		std::uint32_t pending_handle = 0;
		DWORD pending_since = 0;         // GetTickCount at the wait's start
		float pending_since_game = 0;
		bool logged_menu_up = false;

		// ---- Reads of the game's objects, SEH-guarded (no C++ objects in these) --------------------------

		std::uint8_t* conversation_system()
		{
			__try
			{
				std::uint8_t* cs = *at<std::uint8_t**>(cs_pointer);
				return cs && *reinterpret_cast<const DWORD*>(cs) == cs_vtable ? cs : nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		void* object(const DWORD getter, const DWORD vtable)
		{
			__try
			{
				void* found = at<getter_t>(getter)();
				return found && *static_cast<const DWORD*>(found) == vtable ? found : nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		// The game's own answer to "accept pressed?" at the patched call: the vtable entry it would have called.
		bool call_accept(void* menus, const int action)
		{
			__try
			{
				return method<accept_t>(menus, menus_accept_slot)(menus, nullptr, action);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool menu_up()
		{
			__try
			{
				void* menus = object(menus_getter, menus_vtable);
				if (!menus)
				{
					return false;
				}
				const char* name = method<menu_name_t>(menus, menus_name_slot)(menus, nullptr);
				return name && name[0] != 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool voice_playing(const std::uint32_t handle)
		{
			__try
			{
				void* audio = object(audio_getter, audio_vtable);
				return audio && handle != none_handle && method<playing_t>(audio, audio_playing_slot)(audio, nullptr, handle);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool stop_voice(const std::uint32_t handle)
		{
			__try
			{
				void* audio = object(audio_getter, audio_vtable);
				if (!audio || handle == none_handle)
				{
					return false;
				}
				method<stop_t>(audio, audio_stop_slot)(audio, nullptr, handle);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool game_time(float& now)
		{
			__try
			{
				void* game = object(game_getter, game_vtable);
				if (!game)
				{
					return false;
				}
				now = method<game_time_t>(game, game_time_slot)(game, nullptr);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// The frame as the auto-advance sees it: false when any read fails.
		bool read_frame(std::uint8_t* cs, frame_view& v, std::uint32_t& handle)
		{
			__try
			{
				std::memcpy(&v.line_id, cs + cs_line_id, sizeof(v.line_id));
				const std::uint8_t* node = at<node_lookup_t>(node_lookup)(cs, nullptr, v.line_id);
				if (!node)
				{
					return false;
				}
				std::memcpy(&v.time_delay, node + node_time_delay, sizeof(v.time_delay));
				short visible = 0;
				std::memcpy(&visible, cs + cs_visible, sizeof(visible));
				v.visible = visible;
				std::memcpy(&v.accept_from, cs + cs_accept_from, sizeof(v.accept_from));
				std::memcpy(&handle, cs + cs_voice, sizeof(handle));
				v.voice = handle != none_handle;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// What accept does at 0x45d375-0x45d3bc, without the menu sound: the line's voice stopped, the
		// handle cleared, the one visible reply picked.
		bool advance(std::uint8_t* cs)
		{
			__try
			{
				std::uint32_t handle = none_handle;
				std::memcpy(&handle, cs + cs_voice, sizeof(handle));
				if (handle != none_handle)
				{
					stop_voice(handle);
					cs[cs_voice_flags] &= 0xfe;
					const std::uint32_t none = none_handle;
					std::memcpy(cs + cs_voice, &none, sizeof(none));
				}
				method<pick_t>(cs, cs_pick_slot)(cs, nullptr, 0);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// ---- AutoAdvance: in place of `call dword ptr [edx+0x138]` at 0x45d33e ------------------------------

		bool __fastcall accept_hook(void* menus, void* /*edx*/, const int action)
		{
			if (call_accept(menus, action))
			{
				return true;
			}
			std::uint8_t* cs = conversation_system();
			if (!cs)
			{
				return false;
			}
			frame_view v;
			std::uint32_t handle = none_handle;
			if (!read_frame(cs, v, handle) || !game_time(v.now))
			{
				return false;
			}
			v.playing = v.voice && voice_playing(handle);
			v.menu_up = menu_up();
			if (v.menu_up && v.time_delay < 0 && !logged_menu_up)
			{
				logged_menu_up = true;
				logger::write("conversations: a menu is up over line %d - auto-advance waits for it (logged once)", v.line_id);
			}
			if (auto_advance_step(line, v) != verdict::advance)
			{
				return false;
			}
			const bool ok = advance(cs);
			logger::write("conversations: line %d advanced by itself (timeDelay %.2f, %s, shown %.2f s, %d reply visible, voice handle 0x%08lX)%s", v.line_id, v.time_delay,
			              v.voice ? (line.seen_playing ? "its voice played and ended" : "its voice never played - the time") : "no voice - the time", v.now - line.shown_at, v.visible,
			              static_cast<unsigned long>(handle), ok ? "" : " - ERROR: the pick faulted");
			return false;
		}

		// ---- ReplyVoices: in place of `call 0x592480` at 0x45d242 --------------------------------------------

		// 1: keep drawing and wait (go on at step 3); 0: the game's own path (stop, advance).
		int __cdecl pending_decide()
		{
			std::uint8_t* cs = conversation_system();
			if (!cs)
			{
				return 0;
			}
			std::uint32_t handle = none_handle;
			std::memcpy(&handle, cs + cs_voice, sizeof(handle));
			const bool playing = voice_playing(handle);
			bool accept = false;
			if (void* menus = object(menus_getter, menus_vtable))
			{
				accept = call_accept(menus, accept_action);
			}
			const bool wait = pending_wait(handle != none_handle, playing, accept);
			float now = 0;
			game_time(now);
			if (wait && !pending_waiting)
			{
				pending_waiting = true;
				pending_handle = handle;
				pending_since = GetTickCount();
				pending_since_game = now;
				logger::write("conversations: a chosen reply's voice (handle 0x%08lX) is playing - the conversation waits for it", static_cast<unsigned long>(handle));
			}
			else if (!wait && pending_waiting)
			{
				pending_waiting = false;
				logger::write("conversations: the reply's voice (handle 0x%08lX) %s after %lu ms (%.2f s of game time) - on to its answer", static_cast<unsigned long>(pending_handle),
				              accept && playing ? "skipped by accept" : playing ? "let go" : "ended", static_cast<unsigned long>(GetTickCount() - pending_since), now - pending_since_game);
			}
			return wait ? 1 : 0;
		}

		int (__cdecl* pending_decide_fn)() = &pending_decide;
		DWORD pending_audio_getter = audio_getter;
		DWORD pending_continue_at = pending_continue;
		DWORD pending_wait_target = pending_wait_at;
		int pending_verdict = 0;

		// At 0x45d242: esi = CS, ebx = 0, edi free (pushed). The retail instruction was `call 0x592480`.
		__declspec(naked) void pending_stub()
		{
			__asm
			{
				pushad
				pushfd
				call dword ptr [pending_decide_fn]
				mov dword ptr [pending_verdict], eax
				popfd
				popad
				cmp dword ptr [pending_verdict], 0
				jne wait_here
				call dword ptr [pending_audio_getter]
				jmp dword ptr [pending_continue_at]
			wait_here:
				jmp dword ptr [pending_wait_target]
			}
		}

		// ---- ReplyCursor: in place of `mov word ptr [eax+0x21b26], di` at 0x45b5fc -----------------------------

		DWORD cursor_continue_at = cursor_continue;

		// At 0x45b5fc: eax = CS, edi = the visible count, ecx dead (0x45b603 pops and returns).
		__declspec(naked) void cursor_stub()
		{
			__asm
			{
				lea ecx, [edi - 1]
				test ecx, ecx
				jns store
				xor ecx, ecx
			store:
				mov word ptr [eax + 0x21b26], cx
				jmp dword ptr [cursor_continue_at]
			}
		}

		// ---- Patching ------------------------------------------------------------------------------------------

		std::uint8_t* game_image()
		{
			return at<std::uint8_t*>(image_base);
		}

		const guard* first_mismatch_guarded()
		{
			__try
			{
				return first_mismatch(game_image());
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}

		bool write_code(const DWORD va, const std::uint8_t* bytes, const std::size_t size)
		{
			void* target = at<void*>(va);
			DWORD protection = 0;
			if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &protection))
			{
				return false;
			}
			std::memcpy(target, bytes, size);
			FlushInstructionCache(GetCurrentProcess(), target, size);
			VirtualProtect(target, size, protection, &protection);
			return true;
		}

		template <std::size_t N>
		bool patch(const char* what, const DWORD va, const std::array<std::uint8_t, N>& bytes)
		{
			if (!write_code(va, bytes.data(), bytes.size()))
			{
				logger::write("conversations: %s: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - the game's own", what, va, GetLastError());
				return false;
			}
			return true;
		}
	}

	void install(const HMODULE game)
	{
		std::string error;
		const auto chosen = decide(ini::text(L"Game", L"AutoAdvance"), ini::text(L"Game", L"ReplyVoices"), ini::text(L"Game", L"ReplyCursor"), error);
		if (!error.empty())
		{
			logger::write("conversations: [Game] %s", error.c_str());
		}
		if (!chosen.auto_advance && !chosen.reply_voices && !chosen.reply_cursor)
		{
			logger::write("conversations: off ([Game] AutoAdvance=0, ReplyVoices=0, ReplyCursor=0) - the game's own");
			return;
		}
		constexpr const char* as_before = "conversations as the game has them (lines wait for accept, a reply's voice is cut, a menu's lost highlight)";
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("conversations: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("conversations: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}

		if (chosen.auto_advance)
		{
			const auto target = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&accept_hook));
			if (accept_site_is_retail(game_image()) && patch("AutoAdvance", accept_call, accept_call_bytes(target)))
			{
				logger::write("conversations: AutoAdvance on - a line whose timeDelay is negative goes on by itself once its voice has played (or after |timeDelay| s without one) "
				              "when exactly one reply is visible; the accept call at 0x%08lX now asks the fix (0x%08lX) first",
				              accept_call, target);
			}
		}
		else
		{
			logger::write("conversations: AutoAdvance off ([Game] AutoAdvance=0) - every line waits for accept");
		}

		if (chosen.reply_voices)
		{
			const auto target = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&pending_stub));
			if (pending_site_is_retail(game_image()) && patch("ReplyVoices", pending_call, pending_jump_bytes(target)))
			{
				logger::write("conversations: ReplyVoices on - a chosen reply's voice plays out (accept skips it) before the conversation goes on; the pending-reply step at 0x%08lX "
				              "jumps to the fix (0x%08lX), which goes on at 0x%08lX while it plays",
				              pending_call, target, pending_wait_at);
			}
		}
		else
		{
			logger::write("conversations: ReplyVoices off ([Game] ReplyVoices=0) - a chosen reply's voice is cut one frame in, as the game has it");
		}

		if (chosen.reply_cursor)
		{
			const auto target = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&cursor_stub));
			if (cursor_site_is_retail(game_image()) && patch("ReplyCursor", cursor_store, cursor_jump_bytes(target)))
			{
				logger::write("conversations: ReplyCursor on - a reply menu whose cursor is at or past its visible replies keeps the last one highlighted (the game stored the count, "
				              "which highlights nothing); the store at 0x%08lX jumps to the fix (0x%08lX)",
				              cursor_store, target);
			}
		}
		else
		{
			logger::write("conversations: ReplyCursor off ([Game] ReplyCursor=0) - the menu's clamp as the game has it");
		}
	}
}
