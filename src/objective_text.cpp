#include "objective_text.hpp"
#include "objective_text_rules.hpp"
#include "ini.hpp"
#include "limits_rules.hpp"
#include "log.hpp"

namespace objective_text
{
	namespace
	{
		using namespace objective_text_rules;
		texts table;
		using parser_t = void(__fastcall*)(void*, void*, const char*, const char*);
		DWORD allocate_return = allocate_continue;
		DWORD journal_return = journal_continue;
		DWORD primary_return = primary_continue;

		// The allocation callback runs before attributes, including on act reload
		// and save restoration. Clear just this slot: an act can load several files.
		void __cdecl reset_slot(unsigned index) noexcept { table.reset(static_cast<std::uint8_t>(index)); }

		std::string_view bounded(const char* text, std::size_t limit) noexcept
		{
			if (!text) return {};
			std::size_t n = 0;
			while (n < limit && text[n]) ++n;
			return {text, n};
		}

		void __fastcall attribute_hook(void* record, void*, const char* key, const char* value)
		{
			__try
			{
				if (same_field(bounded(key, 32), "updatedescription"))
				{
					const auto index = *(static_cast<std::uint8_t*>(record) + index_offset);
					table.attribute(index, "updatedescription", bounded(value, text_capacity - 1));
					return;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {}
			reinterpret_cast<parser_t>(attribute_parser)(record, nullptr, key, value);
		}

		const char* __cdecl choose_text(const std::uint8_t* record) noexcept
		{
			const auto* original = reinterpret_cast<const char*>(record + description_offset);
			__try
			{
				const auto index = record[index_offset];
				const auto state = reinterpret_cast<const std::uint8_t*>(state_bytes)[index];
				return table.select(index, state, original);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return original; }
		}

		__declspec(naked) void allocate_stub()
		{
			__asm
			{
				mov byte ptr [eax + 0x1a9], dl
				pushfd
				pushad
				movzx eax, dl
				push eax
				call reset_slot
				add esp, 4
				popad
				popfd
				jmp dword ptr [allocate_return]
			}
		}

		__declspec(naked) void journal_stub()
		{
			__asm
			{
				pushfd
				pushad
				push esi
				call choose_text
				add esp, 4
				mov dword ptr [esp + 20], eax // saved EDX; all other registers and flags retained
				popad
				popfd
				jmp dword ptr [journal_return]
			}
		}

		__declspec(naked) void primary_stub()
		{
			__asm
			{
				pushfd
				pushad
				push ebx
				call choose_text
				add esp, 4
				mov dword ptr [esp], eax // saved EDI: selected description
				mov al, byte ptr [eax]
				mov byte ptr [esp + 28], al // saved AL: native empty-description test
				popad
				popfd
				jmp dword ptr [primary_return]
			}
		}

		bool retail_bytes()
		{
			__try
			{
				for (const auto& g : guards)
				{
					if (!limits_rules::matches(reinterpret_cast<const std::uint8_t*>(g.va), g.hex)) return false;
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
	}

	void install(HMODULE game)
	{
		if (!ini::flag(L"Game", L"ObjectiveDescriptions", false)) return;
		if (reinterpret_cast<std::uintptr_t>(game) != 0x400000 || !retail_bytes())
		{
			logger::write("objective descriptions: unsupported executable; original descriptions retained");
			return;
		}
		const auto parser = branch<5>(attribute_call, reinterpret_cast<DWORD>(&attribute_hook), true);
		const auto allocation = branch<6>(allocate_site, reinterpret_cast<DWORD>(&allocate_stub));
		const auto journal = branch<6>(journal_site, reinterpret_cast<DWORD>(&journal_stub));
		const auto primary = branch<12>(primary_site, reinterpret_cast<DWORD>(&primary_stub));
		struct patch { DWORD site; const std::uint8_t* bytes; std::size_t size; };
		patch patches[] = {{attribute_call, parser.data(), parser.size()},
		                   {allocate_site, allocation.data(), allocation.size()},
		                   {journal_site, journal.data(), journal.size()},
		                   {primary_site, primary.data(), primary.size()}};
		// Acquire all pages before any hook is written. Allocation and parser
		// share one page; primary and secondary journal loops occupy two others.
		const DWORD pages[] = {attribute_call & ~DWORD(0xfff),
		                       journal_site & ~DWORD(0xfff), primary_site & ~DWORD(0xfff)};
		DWORD protections[3]{};
		DWORD ignored = 0;
		for (unsigned i = 0; i < 3; ++i)
		{
			if (VirtualProtect(reinterpret_cast<void*>(pages[i]), 0x1000,
			                   PAGE_EXECUTE_READWRITE, &protections[i])) continue;
			for (unsigned j = 0; j < i; ++j)
				VirtualProtect(reinterpret_cast<void*>(pages[j]), 0x1000, protections[j], &ignored);
			logger::write("objective descriptions: could not protect all hook pages; disabled");
			return;
		}
		for (const auto& p : patches)
		{
			std::memcpy(reinterpret_cast<void*>(p.site), p.bytes, p.size);
			FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(p.site), p.size);
		}
		for (unsigned i = 0; i < 3; ++i)
			VirtualProtect(reinterpret_cast<void*>(pages[i]), 0x1000, protections[i], &ignored);
		logger::write("objective descriptions: enabled; journal completion text follows the saved completion bit");
	}
}
