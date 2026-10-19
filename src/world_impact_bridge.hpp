#pragma once
#include "world_impact_rules.hpp"

namespace world_impact_bridge
{
	inline std::uint32_t resume = world_impact_rules::resume;
	// Native frame: saved EDI, ESI, ECX, return address, speed, target, mode.
	// ST(0) already contains the speed factor. Preserve its depth in both paths.
	__declspec(naked) inline void bridge()
	{
		__asm {
			test esi, esi
			jz world_target
			movzx ax, byte ptr [esi + 0x31d]
			jmp dword ptr [resume]
		world_target:
			fstp st(0)
			fldz
			pop edi
			pop esi
			pop ecx
			ret 0x0c
		}
	}
}
