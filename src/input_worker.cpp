#include "input_worker.hpp"
#include "input_worker_rules.hpp"
#include "game_version.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

namespace input_worker
{
	namespace
	{
		SRWLOCK lifetime_lock = SRWLOCK_INIT;
		unsigned active = 0;
		bool installed = false;

		void __cdecl worker_enter()
		{
			AcquireSRWLockExclusive(&lifetime_lock);
			++active;
			ReleaseSRWLockExclusive(&lifetime_lock);
		}
		void __cdecl worker_leave()
		{
			AcquireSRWLockExclusive(&lifetime_lock);
			// A worker that is still enumerating may reload the manager several
			// times. Keep both the pointer and the shutdown acknowledgement alive
			// until the last worker finishes. Entry shares this lock so a late
			// worker cannot enter between the zero count and the pointer clear.
			if (--active == 0)
			{
				*reinterpret_cast<void* volatile*>(input_worker_rules::manager) = nullptr;
				*reinterpret_cast<volatile unsigned char*>(input_worker_rules::stopped) = 1;
			}
			ReleaseSRWLockExclusive(&lifetime_lock);
		}

		DWORD entry_next = input_worker_rules::entry_next;
		DWORD finish_next = input_worker_rules::finish_next;
		__declspec(naked) void entry_bridge()
		{
			__asm {
				pushfd
				pushad
				call worker_enter
				popad
				popfd
				push ebp
				mov ebp, esp
				and esp, -8
				jmp dword ptr [entry_next]
			}
		}
		__declspec(naked) void finish_bridge()
		{
			__asm {
				pushfd
				pushad
				call worker_leave
				popad
				popfd
				jmp dword ptr [finish_next]
			}
		}
		const input_worker_rules::guard* mismatch()
		{
			__try { return input_worker_rules::first_mismatch(reinterpret_cast<const std::uint8_t*>(input_worker_rules::image_base)); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &input_worker_rules::guards[0]; }
		}
		xml1_hook_transaction::patch jump(const input_worker_rules::guard& g, void* target)
		{
			xml1_hook_transaction::patch p{g.va, {}, std::vector<unsigned char>(g.hex.size()/2, 0x90)};
			for (std::size_t i = 0; i < g.hex.size(); i += 2)
				p.before.push_back(static_cast<unsigned char>((input_worker_rules::nibble(g.hex[i]) << 4) | input_worker_rules::nibble(g.hex[i+1])));
			p.after[0] = 0xe9;
			const auto relative = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) - (g.va + 5);
			std::memcpy(p.after.data()+1, &relative, 4);
			return p;
		}
	}

	void install(HMODULE game)
	{
		if (installed) return;
		// This is an engine lifetime repair for both games; no gameplay/online rule.
		if (reinterpret_cast<std::uintptr_t>(game) != input_worker_rules::image_base) return;
		if (const auto* g = mismatch())
		{
			logger::write("input worker: retail guard refused at 0x%08x - stock shutdown retained", g->va);
			return;
		}
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, {
			jump(input_worker_rules::guards[0], reinterpret_cast<void*>(&entry_bridge)),
			jump(input_worker_rules::guards[1], reinterpret_cast<void*>(&finish_bridge))}, &safe))
		{
			installed = true;
			logger::write("input worker: last-worker shutdown guard installed");
		}
		else if (safe)
			logger::write("input worker: transaction refused - stock shutdown retained");
		else
		{
			game_version::refuse_startup();
			logger::write("input worker: rollback/protection verification failed; startup refused");
		}
	}
}
