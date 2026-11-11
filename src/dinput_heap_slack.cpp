#include "dinput_heap_slack.hpp"

#include "iat_hook.hpp"

#include <cstring>
#include <limits>

namespace dinput_heap_slack
{
	namespace
	{
		using local_alloc_t = HLOCAL(WINAPI*)(UINT, SIZE_T);
		using local_realloc_t = HLOCAL(WINAPI*)(HLOCAL, SIZE_T, UINT);

		local_alloc_t previous_alloc = &LocalAlloc;
		local_realloc_t previous_realloc = &LocalReAlloc;

		// The DLL names a 32-bit Windows module can import the Local* functions from.
		constexpr const char* heap_dlls[] = {
			"KERNEL32.dll",
			"api-ms-win-core-heap-obsolete-l1-1-0.dll",
			"api-ms-win-core-heap-l2-1-0.dll",
			"KERNELBASE.dll",
		};

		// Zeroes [bytes, bytes + slack) of a block; keeps the caller's last-error value (LocalUnlock of a fixed
		// block sets ERROR_NOT_LOCKED).
		void zero_tail(const HLOCAL block, const SIZE_T bytes)
		{
			const DWORD error = GetLastError();
			if (auto* data = static_cast<unsigned char*>(LocalLock(block)))
			{
				std::memset(data + bytes, 0, slack);
				LocalUnlock(block);
			}
			SetLastError(error);
		}

		template <typename T>
		bool redirect(const HMODULE module, const char* function, void* replacement, T& previous)
		{
			for (const char* dll : heap_dlls)
			{
				if (void* const before = iat_hook::hook(module, dll, function, 0, replacement))
				{
					if (before != replacement)
					{
						previous = reinterpret_cast<T>(before);
					}
					return true;
				}
			}
			return false;
		}
	}

	HLOCAL WINAPI padded_local_alloc(const UINT flags, const SIZE_T bytes)
	{
		if (bytes > std::numeric_limits<SIZE_T>::max() - slack)
		{
			return previous_alloc(flags, bytes);
		}
		const HLOCAL block = previous_alloc(flags, bytes + slack);
		if (block && !(flags & LMEM_ZEROINIT))
		{
			zero_tail(block, bytes);
		}
		return block;
	}

	HLOCAL WINAPI padded_local_realloc(const HLOCAL memory, const SIZE_T bytes, const UINT flags)
	{
		if ((flags & LMEM_MODIFY) || bytes > std::numeric_limits<SIZE_T>::max() - slack)
		{
			return previous_realloc(memory, bytes, flags); // a flags change, not a size
		}
		const HLOCAL block = previous_realloc(memory, bytes + slack, flags);
		if (block)
		{
			zero_tail(block, bytes); // a shrink leaves old data there; LMEM_ZEROINIT only covers growth
		}
		return block;
	}

	hid_list_state hid_list(const HMODULE system_dinput)
	{
		// mov ecx,[list]; xor eax,eax; add esp,0Ch; mov [ebp-50h],eax; xor edi,edi; lea esi,[ecx+0Ch];
		// test ecx,ecx; jz rel32; test esi,esi; jz rel8; cmp dword ptr [esi+44h],0 - the field before the count.
		static const unsigned char shape[] = {0x33, 0xC0, 0x83, 0xC4, 0x0C, 0x89, 0x45, 0xB0, 0x33, 0xFF, 0x8D, 0x71, 0x0C, 0x85, 0xC9, 0x0F, 0x84};
		static const unsigned char test_esi[] = {0x85, 0xF6, 0x74};
		static const unsigned char compare[] = {0x83, 0x7E, 0x44, 0x00};
		hid_list_state state;
		if (!system_dinput)
		{
			return state;
		}
		const auto* base = reinterpret_cast<const unsigned char*>(system_dinput);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
		const ULONG_PTR image_begin = reinterpret_cast<ULONG_PTR>(base);
		const ULONG_PTR image_end = image_begin + nt->OptionalHeader.SizeOfImage;
		const auto* section = IMAGE_FIRST_SECTION(nt);
		constexpr DWORD before = 6;                                                          // mov ecx,[imm32]
		constexpr DWORD length = sizeof(shape) + 4 + sizeof(test_esi) + 1 + sizeof(compare); // to the compare's end
		for (WORD s = 0; s < nt->FileHeader.NumberOfSections && !state.shape; ++s, ++section)
		{
			if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || section->Misc.VirtualSize < before + length)
			{
				continue;
			}
			const unsigned char* text = base + section->VirtualAddress;
			for (DWORD i = before; i + length <= section->Misc.VirtualSize; ++i)
			{
				const unsigned char* at = text + i;
				if (at[-6] != 0x8B || at[-5] != 0x0D || std::memcmp(at, shape, sizeof(shape)) != 0 ||
					std::memcmp(at + sizeof(shape) + 4, test_esi, sizeof(test_esi)) != 0 ||
					std::memcmp(at + sizeof(shape) + 4 + sizeof(test_esi) + 1, compare, sizeof(compare)) != 0)
				{
					continue;
				}
				DWORD global = 0;
				std::memcpy(&global, at - 4, sizeof(global));
				if (global < image_begin || global + sizeof(void*) > image_end)
				{
					continue; // not a global of this module
				}
				state.shape = true;
				state.compare_rva = static_cast<DWORD>(at + sizeof(shape) + 4 + sizeof(test_esi) + 1 - base);
				const auto* list = *reinterpret_cast<const int* const*>(static_cast<ULONG_PTR>(global));
				MEMORY_BASIC_INFORMATION memory{};
				if (list && VirtualQuery(list, &memory, sizeof(memory)) == sizeof(memory) && memory.State == MEM_COMMIT &&
					!(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
					reinterpret_cast<ULONG_PTR>(list) + 2 * sizeof(int) <= reinterpret_cast<ULONG_PTR>(memory.BaseAddress) + memory.RegionSize)
				{
					state.count = list[0];
					state.capacity = list[1];
				}
				break;
			}
		}
		return state;
	}

	result install(const HMODULE system_dinput)
	{
		result installed;
		if (!system_dinput)
		{
			return installed;
		}
		installed.alloc = redirect(system_dinput, "LocalAlloc", reinterpret_cast<void*>(&padded_local_alloc), previous_alloc);
		installed.realloc = redirect(system_dinput, "LocalReAlloc", reinterpret_cast<void*>(&padded_local_realloc), previous_realloc);
		return installed;
	}
}
