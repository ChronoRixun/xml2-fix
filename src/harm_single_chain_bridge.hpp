#pragma once
#include "harm_single_chain_rules.hpp"
namespace harm_single_chain_bridge
{
	inline std::uint32_t tail_resume=harm_single_chain_rules::tail_resume;
	inline std::uint32_t request_resume=harm_single_chain_rules::request_resume;
	// Explicit alternative policy, not a claim about XBE behavior. Only ordinary
	// boxed, touch-enabled, repeating start-on loops without loop toggling qualify.
	// ESI is the hazard; only AL and arithmetic flags are scratch in this helper.
	__declspec(naked) inline void eligible()
	{
		__asm {
			cmp dword ptr [esi], 0x684224
			jne no
			test byte ptr [esi+0x360], 8
			jnz no
			test byte ptr [esi+0x360], 2
			jz no
			test byte ptr [esi+0x360], 1
			jnz no
			test byte ptr [esi+6], 8
			jz no
			test byte ptr [esi+0xc5], 4
			jz no
			test byte ptr [esi+0xc5], 8
			jnz no
			test dword ptr [esi+0x364], 0x7fffffff
			jnz no
			test dword ptr [esi+0x368], 0x7fffffff
			jnz no
			cmp dword ptr [esi+0xa4], 0
			jle no
			cmp dword ptr [esi+0xa4], 0x7f800000
			jae no
			mov al, 1
			ret
		no:
			xor al, al
			ret
		}
	}
	__declspec(naked) inline void tail()
	{
		__asm {
			pushfd
			call eligible
			test al, al
			jz native_tail
			popfd
			mov dl, byte ptr [esi+0xc4]
			mov al, byte ptr [esi+0x360]
			jmp dword ptr [tail_resume]
		native_tail:
			popfd
			mov dl, byte ptr [esi+0xc4]
			mov al, byte ptr [esi+0x360]
			and dl, 0x7f
			jmp dword ptr [tail_resume]
		}
	}
	__declspec(naked) inline void request()
	{
		__asm {
			pushfd
			push eax
			call eligible
			test al, al
			pop eax
			jz native_request
			popfd
			jmp dword ptr [request_resume]
		native_request:
			popfd
			and byte ptr [esi+0xc4], 0x7f
			jmp dword ptr [request_resume]
		}
	}
}
