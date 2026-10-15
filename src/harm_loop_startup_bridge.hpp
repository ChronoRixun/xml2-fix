#pragma once
#include "harm_loop_startup_rules.hpp"

namespace harm_loop_startup_bridge
{
	inline std::uint32_t resume = harm_loop_startup_rules::resume;
	inline std::uint32_t skip = harm_loop_startup_rules::skip;
	__declspec(naked) inline void bridge()
	{
		__asm {
			pushfd
			test byte ptr [esi+0x360], 8
			jnz native_path
			test byte ptr [esi+0xc5], 4
			jz native_path
			test dword ptr [esi+0x364], 0x7fffffff
			jne native_path
			test dword ptr [esi+0x368], 0x7fffffff
			jne native_path
			popfd
			jmp dword ptr [skip]
		native_path:
			popfd
			mov eax, dword ptr [esi]
			mov ecx, esi
			call dword ptr [eax+0xe8]
			jmp dword ptr [resume]
		}
	}
}
