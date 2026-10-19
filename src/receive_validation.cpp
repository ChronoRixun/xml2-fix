#include "receive_validation.hpp"
#include "receive_validation_rules.hpp"
#include "entity_capacity.hpp"
#include "ini.hpp"
#include "limits_rules.hpp"
#include "log.hpp"
#include "patch_transaction.hpp"

#include <array>
#include <cstring>

namespace receive_validation
{
	namespace
	{
		using namespace receive_validation_rules;
		bool published = false;
		bool log_only = false;
		std::uint32_t dropped_count = 0, logged_drops = 0;

		// The dispatch (thiscall: ecx = dispatcher, one stack argument = the event) and its 7-byte entry, replayed
		// by the guard: push ebx/esi/edi; mov edi,[esp+0x10] (the event). Resume runs the opcode table lookup.
		constexpr std::uint32_t dispatch = 0x60dbe0;
		const std::uint32_t dispatch_resume = 0x60dbe7; // referenced from the guard's inline asm
		constexpr char dispatch_entry_hex[] = "5356578b7c2410";
		constexpr char dispatch_resume_hex[] = "0fb607c1e004";

		// Event: +0 opcode, +4 body, +8 u16 length (what the handler trusts), +0xc context, +0x10 sender, +0x18 flags.
		bool __cdecl dispatch_accept(const std::uint8_t* event)
		{
			__try
			{
				context ctx;
				ctx.fast_transfer = entity_capacity::fast_transfer_installed();
				const auto reason = check(event[0], *reinterpret_cast<const std::uint8_t* const*>(event + 4),
				                          *reinterpret_cast<const std::uint16_t*>(event + 8), ctx);
				if (reason == drop_reason::none) return true;
				if (!log_only) ++dropped_count;
				if (log_only || logged_drops < 8)
				{
					if (!log_only) ++logged_drops;
					char line[256];
					const auto* body = *reinterpret_cast<const std::uint8_t* const*>(event + 4);
					const auto length = *reinterpret_cast<const std::uint16_t*>(event + 8);
					format_drop(line, sizeof(line), log_only ? "would drop" : "dropped", event[0], body, length, reason, ctx);
					logger::write("%s%s", line, !log_only && logged_drops == 8 ? "; further drops are not logged" : "");
				}
				return log_only;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				// An event that cannot even be read is not handed to a handler.
				++dropped_count;
				return false;
			}
		}

		// Replays the hooked entry, validates the event, and either resumes the native dispatch or returns 0 as the
		// dispatch does for a message nobody consumed (the parser/drain then move on; the callers' cmp eax,2
		// deferral path is never triggered by a drop). pushad/pushfd keep every register for the native code.
		__declspec(naked) void dispatch_guard()
		{
			__asm
			{
				push ebx
				push esi
				push edi
				mov edi, dword ptr [esp + 0x10]
				pushfd
				pushad
				push edi
				call dispatch_accept
				add esp, 4
				mov dword ptr [esp + 0x1c], eax
				popad
				popfd
				test al, al
				jz refuse
				jmp dword ptr [dispatch_resume]
			refuse:
				xor eax, eax
				pop edi
				pop esi
				pop ebx
				ret 4
			}
		}

		std::uint32_t address_of(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

		bool retail_entry(HMODULE game)
		{
			if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base) return false;
			const auto entry = patch_transaction::bytes_of(dispatch_entry_hex);
			const auto resume = patch_transaction::bytes_of(dispatch_resume_hex);
			std::array<std::uint8_t, 7> entry_bytes{};
			std::array<std::uint8_t, 6> resume_bytes{};
			return patch_transaction::read_memory(dispatch, entry_bytes.data(), entry_bytes.size()) &&
			       patch_transaction::read_memory(dispatch_resume, resume_bytes.data(), resume_bytes.size()) &&
			       std::memcmp(entry_bytes.data(), entry.data(), entry.size()) == 0 &&
			       std::memcmp(resume_bytes.data(), resume.data(), resume.size()) == 0;
		}
	}

	void contribute(patch_transaction::transaction& transaction, HMODULE game)
	{
		if (!ini::flag(L"Online", L"ReceiveValidation", true))
		{
			logger::write("online: [Online] ReceiveValidation=0 - the stock receive handlers run unguarded");
			return;
		}
		if (!retail_entry(game))
		{
			logger::write("online: receive validation: the dispatch entry is not the retail one; not installed (handlers unguarded)");
			return;
		}
		// Development-only process opt-in; never emitted by the builder or exposed as a player setting.
		wchar_t audit_value[2]{};
		const bool audit_requested = GetEnvironmentVariableW(L"XML2FIX_RECEIVE_VALIDATION_LOG_ONLY", audit_value, 2) == 1 && audit_value[0] == L'1';
		patch_transaction::contribution c;
		c.owner = "receive_validation";
		c.guards.push_back({dispatch, patch_transaction::bytes_of(dispatch_entry_hex), "common event dispatch entry"});
		c.guards.push_back({dispatch_resume, patch_transaction::bytes_of(dispatch_resume_hex), "dispatch resume reads the event opcode"});
		c.writes.push_back(patch_transaction::relocation(dispatch, "e9", true, address_of(reinterpret_cast<const void*>(&dispatch_guard)), 2, "receive validation dispatch guard"));
		c.publish = [audit_requested]
		{
			published = true;
			log_only = audit_requested;
			logger::write("online: receive validation installed - every message is checked against the handler table before a stock handler runs");
			if (log_only) logger::write("online: receive validation DEVELOPMENT LOG ONLY - invalid messages are logged and delivered");
		};
		c.abandon = [] { logger::write("online: receive validation not installed; the stock receive handlers run unguarded"); };
		c.retain = c.abandon;
		if (!transaction.add(std::move(c)))
			logger::write("online: receive validation could not join the startup transaction; the stock receive handlers run unguarded");
	}

	bool installed() { return published; }
	std::uint32_t dropped() { return dropped_count; }
}
