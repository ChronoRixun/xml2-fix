#pragma once
#include "box_harm_rules.hpp"

namespace box_harm_bridge
{
	inline float zero_origin[3]{};
	inline std::uint32_t resume = box_harm_rules::resume;
	// EDX is the source of the native vector copy and ECX its stack destination.
	// Neither displaced instruction changes flags, stack depth or the FPU stack.
	__declspec(naked) inline void bridge()
	{
		__asm {
			lea edx, zero_origin
			mov ecx, esp
			jmp dword ptr [resume]
		}
	}
}
