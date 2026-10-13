#include "script_function_overflow.hpp"

#include <cstdint>

namespace script_function_overflow
{
	namespace
	{
		using namespace script_functions_rules;

		overflow_table overflow;
		notify_t told = nullptr;

		// The resume points, as memory operands for the stubs' indirect jumps.
		const std::uint32_t resume_insert = register_insert;
		const std::uint32_t resume_next = register_next;
	}

	overflow_table& table()
	{
		return overflow;
	}

	void set_notify(const notify_t notify)
	{
		told = notify;
	}

	extern "C" void __cdecl script_overflow_register(const func_entry* entry)
	{
		const auto result = overflow.add(entry);
		if (told) told(entry, result);
	}

	extern "C" const func_entry* __cdecl script_overflow_find(const char* name)
	{
		return overflow.find(name);
	}

	// From 0x4d7637 (EDI = the script system, EBP = &entry->name; the tree's search missed). The game's
	// own cap check first: below 320 its insert runs untouched; at 320 the entry goes to the overflow and
	// the loop moves on, as the game's skip did. EAX/ECX/EDX are free at both resume points (0x4d7643
	// loads ESI and EAX; 0x4d768f reloads EAX from the frame).
	__declspec(naked) void register_stub()
	{
		__asm {
			cmp dword ptr [edi + 0x1948], 0x140
			jne game_insert
			lea eax, [ebp - 4]
			push eax
			call script_overflow_register
			add esp, 4
			jmp dword ptr [resume_next]
		game_insert:
			jmp dword ptr [resume_insert]
		}
	}

	// From 0x4d696a, the lookup's miss (EBX = 0, ESI = the name's text, EDI/ESI/EBX still pushed; the
	// argument slot holds the interned handle by now, not the text): the game's epilogue, then the
	// overflow's answer (NULL when it has no such name) and ret 4.
	__declspec(naked) void lookup_stub()
	{
		__asm {
			mov eax, esi
			pop edi
			pop esi
			pop ebx
			push eax
			call script_overflow_find
			add esp, 4
			ret 4
		}
	}
}
