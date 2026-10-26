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
		void __cdecl reset_slot(unsigned index) noexcept { table.reset(index); }

		// the global index of a loaded objective record and the state array, from the installed layout (W2-L-10)
		std::uint32_t record_index(const std::uint8_t* record)
		{
			const auto& layout = save_layout_view::objectives();
			const auto* pool = layout.pool_address ? reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(layout.pool_address)) : nullptr;
			if (layout.wide_index && !pool) return 0xffff;
			return save_layout_view::objective_index(layout, record, pool);
		}

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
					const auto index = record_index(static_cast<const std::uint8_t*>(record));
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
				const auto& layout = save_layout_view::objectives();
				const auto index = record_index(record);
				if (index >= layout.objectives) return original;
				const auto state = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(layout.state_bytes))[index];
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

	namespace
	{
		bool build(patch_transaction::contribution& c, HMODULE game, bool w2)
		{
			if (!ini::flag(L"Game", L"ObjectiveDescriptions", false)) return false;
			if (reinterpret_cast<std::uintptr_t>(game) != 0x400000 || !retail_bytes())
			{
				logger::write("objective descriptions: unsupported executable; original descriptions retained");
				return false;
			}
			const auto parser = branch<5>(attribute_call, reinterpret_cast<DWORD>(&attribute_hook), true);
			const auto allocation = branch<6>(allocate_site, reinterpret_cast<DWORD>(&allocate_stub));
			const auto journal = branch<6>(journal_site, reinterpret_cast<DWORD>(&journal_stub));
			const auto primary = branch<12>(primary_site, reinterpret_cast<DWORD>(&primary_stub));
			c.owner = "objective_text";
			// every guard of the module; the four hooked sites are guarded on exactly the bytes they overwrite, so a
			// contributor that also expects the allocation site 0x489a01 (the W2 pool relocation) unions with this one
			for (const auto& g : guards) c.guards.push_back({g.va, patch_transaction::bytes_of(g.hex), "objective descriptions"});
			c.writes.push_back({attribute_call, {parser.begin(), parser.end()}, "objective descriptions: attribute parser"});
			if (!w2) c.writes.push_back({allocate_site, {allocation.begin(), allocation.end()}, "objective descriptions: allocation"});
			c.writes.push_back({journal_site, {journal.begin(), journal.end()}, "objective descriptions: journal"});
			c.writes.push_back({primary_site, {primary.begin(), primary.end()}, "objective descriptions: primary"});
			c.publish = [] { logger::write("objective descriptions: enabled; journal completion text follows the saved completion bit"); };
			c.abandon = [] { logger::write("objective descriptions: the transaction did not commit; original descriptions retained"); };
			return true;
		}
	}

	bool contribute(patch_transaction::transaction& t, HMODULE game)
	{ return contribute(t, game, false); }
	void reset(unsigned index) noexcept { reset_slot(index); }
	bool contribute(patch_transaction::transaction& t, HMODULE game, bool w2)
	{
		patch_transaction::contribution c;
		return build(c, game, w2) && t.add(std::move(c));
	}

	void install(HMODULE game)
	{
		patch_transaction::transaction own;
		if (!contribute(own, game)) return;
		const auto outcome = own.commit();
		if (outcome != patch_transaction::outcome::committed)
			logger::write("objective descriptions: %s (%s); original descriptions retained", patch_transaction::name(outcome), own.report().c_str());
	}
}
