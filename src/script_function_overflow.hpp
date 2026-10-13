#pragma once

// The script function overflow's runtime half (script_functions_rules.hpp has the decisions): the one
// table behind the game's 320-name tree and the two stubs the patched sites jump to. No logging and no
// ini here, so the native test links it as the DLL does and runs the game's own registration and
// lookup code through the same stubs.

#include "script_functions_rules.hpp"

namespace script_function_overflow
{
	script_functions_rules::overflow_table& table();

	// Told of every registration the overflow sees (on the game's thread, during its registration); the DLL
	// logs them. NULL: nothing told.
	using notify_t = void (*)(const script_functions_rules::func_entry* entry, script_functions_rules::added result);
	void set_notify(notify_t notify);

	// The patched sites' targets. register_stub: jumped to from 0x4d7637 inside 0x4d75a0's loop;
	// lookup_stub: jumped to from 0x4d696a, 0x4d6910's miss path.
	void register_stub();
	void lookup_stub();

	// What the stubs call (cdecl, the registers the game's code keeps are the compiler's callee-saved ones).
	extern "C" void __cdecl script_overflow_register(const script_functions_rules::func_entry* entry);
	extern "C" const script_functions_rules::func_entry* __cdecl script_overflow_find(const char* name);
}
