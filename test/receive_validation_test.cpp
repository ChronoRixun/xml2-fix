// [Online] ReceiveValidation: run the rule table against every opcode class, then execute the retail common
// event dispatch (0x60dbe0, bytes copied from the retail executable) under the production guard installed by
// the production contribute(). Handlers, dispatchers and registrations are synthetic; no game content is embedded.
#include "receive_validation.hpp"
#include "receive_validation_rules.hpp"
#include "entity_capacity.hpp"
#include "patch_transaction.hpp"
#include <Windows.h>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

extern "C" std::uint8_t fixture_space[0x700000];

// Test doubles for the module's environment (the production ini/logger/entity_capacity are not linked).
namespace ini
{
	bool enabled = true;
	std::optional<std::string> text(const wchar_t*, const wchar_t*) { return std::nullopt; }
	int number(const wchar_t*, const wchar_t*, int fallback) { return fallback; }
	bool flag(const wchar_t*, const wchar_t*, bool) { return enabled; }
}
namespace logger
{
	std::vector<std::string> logged;
	void write(const char* fmt, ...)
	{
		char line[512];
		va_list args;
		va_start(args, fmt);
		std::vsnprintf(line, sizeof(line), fmt, args);
		va_end(args);
		logged.push_back(line);
	}
	bool has(const char* part)
	{
		for (const auto& l : logged) if (l.find(part) != std::string::npos) return true;
		return false;
	}
}
namespace entity_capacity
{
	bool fast_transfer = false;
	bool fast_transfer_installed() { return fast_transfer; }
}

namespace
{
	using receive_validation_rules::check;
	using receive_validation_rules::context;
	using receive_validation_rules::drop_reason;
	using receive_validation_rules::valid;

	int failures = 0, checks = 0;
	void expect(bool ok, const std::string& what)
	{
		++checks;
		if (!ok) { ++failures; std::printf("FAIL %s\n", what.c_str()); }
	}
	std::uint8_t* at(std::uint32_t va) { return reinterpret_cast<std::uint8_t*>(va); }
	std::uint32_t va_of(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

	// ---- the retail executable, copied into the fixture space (read-only input) ----
	std::vector<std::uint8_t> binary;
	std::size_t file_offset(std::uint32_t va)
	{
		auto u16 = [](std::size_t o) { std::uint16_t n; std::memcpy(&n, binary.data() + o, 2); return n; };
		auto u32 = [](std::size_t o) { std::uint32_t n; std::memcpy(&n, binary.data() + o, 4); return n; };
		const auto pe = u32(0x3c), sections = std::size_t(pe) + 24 + u16(pe + 20);
		if (u16(0) != 0x5a4d || u32(pe) != 0x4550 || sections + std::size_t(u16(pe + 6)) * 40 > binary.size()) std::exit(2);
		for (unsigned i = 0; i < u16(pe + 6); ++i)
		{
			const auto row = sections + i * 40, rva = std::size_t(u32(row + 12));
			if (va - 0x400000 >= rva && va - 0x400000 < rva + u32(row + 16)) return u32(row + 20) + va - 0x400000 - rva;
		}
		return binary.size();
	}
	void copy(std::uint32_t va, std::size_t n)
	{
		const auto o = file_offset(va);
		if (o > binary.size() || n > binary.size() - o) std::exit(2);
		std::memcpy(at(va), binary.data() + o, n);
	}

	// ---- synthetic dispatcher registrations (native layouts, from the dispatch disassembly) ----
	// slot = dispatcher + 0x12b3c + opcode*0x10; slot+0 -> first node, +0xc is non-null while registered;
	// node +8 next, +0x10 entry;
	// entry +0 handler, +4 context (written to event+0xc before the call), +8 flags (called when (flags & event+0x18) == event+0x18).
	std::vector<std::uint8_t> dispatcher(0x12b3c + 256 * 0x10 + 0x10);
	struct entry { std::uint32_t fn = 0, context = 0, flags = 0; };
	struct node { std::uint32_t pad0[2]{}; std::uint32_t next = 0; std::uint32_t pad3 = 0; std::uint32_t handler = 0; };
	std::array<node, 8> nodes{};
	std::array<entry, 8> entries{};
	std::array<std::uint32_t, 8> cells{};
	node* heads[256]{};
	node* tails[256]{};
	unsigned registered = 0;
	void clear_registrations()
	{
		std::fill(dispatcher.begin(), dispatcher.end(), std::uint8_t{});
		for (auto& n : nodes) n = node{};
		for (auto& e : entries) e = entry{};
		cells.fill(0);
		std::memset(heads, 0, sizeof(heads));
		std::memset(tails, 0, sizeof(tails));
		registered = 0;
	}
	void enroll(std::uint8_t opcode, void* handler, std::uint32_t context_value, std::uint32_t flags)
	{
		const unsigned i = registered++;
		entries[i].fn = va_of(handler);
		entries[i].context = context_value;
		entries[i].flags = flags;
		nodes[i].handler = va_of(&entries[i]);
		auto* slot = dispatcher.data() + 0x12b3c + opcode * 0x10;
		if (!heads[opcode])
		{
			heads[opcode] = tails[opcode] = &nodes[i];
			cells[i] = va_of(&nodes[i]);
			*reinterpret_cast<std::uint32_t*>(slot) = va_of(&nodes[i]);
			*reinterpret_cast<std::uint32_t*>(slot + 0xc) = va_of(&cells[i]);
		}
		else
		{
			tails[opcode]->next = va_of(&nodes[i]);
			tails[opcode] = &nodes[i];
		}
	}

	// ---- synthetic handlers ----
	int calls[8]{}, last_context[8]{}, return_value[8]{};
	template <int N> int __cdecl handler(void* event)
	{
		++calls[N];
		last_context[N] = *reinterpret_cast<int*>(static_cast<std::uint8_t*>(event) + 0xc);
		return return_value[N];
	}

	using dispatch_t = int(__thiscall*)(void*, void*);
	int dispatch(std::uint8_t* event) { return reinterpret_cast<dispatch_t>(at(0x60dbe0))(dispatcher.data(), event); }

	std::array<std::uint8_t, 0x1c> event{};
	std::vector<std::uint8_t> body(1400);
	void make_event(std::uint8_t opcode, std::size_t length, std::uint32_t flags = 0)
	{
		event.fill(0);
		event[0] = opcode;
		*reinterpret_cast<std::uint8_t**>(event.data() + 4) = body.data();
		*reinterpret_cast<std::uint16_t*>(event.data() + 8) = static_cast<std::uint16_t>(length);
		*reinterpret_cast<std::uint32_t*>(event.data() + 0x18) = flags;
	}

	// Register/flag snapshot around a dispatch (the guard must leave every callee-saved register as stock does).
	std::uint32_t regs_before[4], regs_after[4];
	void* drive_dispatcher = nullptr;
	void* drive_event = nullptr;
	int drive_result = 0;
	__declspec(naked) void drive()
	{
		__asm
		{
			push ebp
			push ebx
			push esi
			push edi
			mov ebx, 0x11223344
			mov esi, 0x22334455
			mov edi, 0x33445566
			mov ebp, 0x44556677
			mov regs_before[0], ebx
			mov regs_before[4], esi
			mov regs_before[8], edi
			mov regs_before[12], ebp
			mov ecx, drive_dispatcher
			push drive_event
			mov eax, 0x60dbe0
			call eax
			mov drive_result, eax
			mov regs_after[0], ebx
			mov regs_after[4], esi
			mov regs_after[8], edi
			mov regs_after[12], ebp
			pop edi
			pop esi
			pop ebx
			pop ebp
			ret
		}
	}

	void rule_table_tests()
	{
		// Every accepted opcode: a zeroed body of the rule's minimum length is valid (index 0, NUL present,
		// roster count 0), except the 0x00 wrapper, which needs a valid inner message.
		for (const auto& r : receive_validation_rules::rules)
		{
			std::vector<std::uint8_t> b(r.min_len ? r.min_len : 1, 0);
			if (r.opcode == 0x00)
			{
				std::array<std::uint8_t, 7> w{0x02, 0, 0, 0, 0, 0, 0};
				expect(valid(0x00, w.data(), 4), "wrapper around a fixed 0x02 uses the native inner length");
				expect(check(0x00, w.data(), 3) == drop_reason::body_too_short, "wrapper with a short inner body is dropped");
				std::array<std::uint8_t, 11> admission{0x1e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
				expect(valid(0x00, admission.data(), 10), "wrapper around the stock eight-byte 0x1e admission body passes");
				static constexpr std::uint8_t eight_byte_session_opcodes[] = {0x14, 0x16, 0x2c, 0x26, 0x47};
				for (const auto op : eight_byte_session_opcodes)
				{
					admission[0] = op;
					expect(valid(0x00, admission.data(), 10), "wrapper around a stock eight-byte session body passes");
				}
				std::array<std::uint8_t, 3> empty{0x28, 0, 0};
				expect(valid(0x00, empty.data(), 2), "wrapper around an empty body passes");
				w[0] = 0x13;
				expect(check(0x00, w.data(), 6) == drop_reason::fragment_unsupported, "wrapper around a fragment is dropped");
				w[0] = 0x00;
				expect(check(0x00, w.data(), 6) == drop_reason::unknown_opcode, "a nested wrapper is dropped");
				w[0] = 0x99;
				expect(check(0x00, w.data(), 6) == drop_reason::unknown_opcode, "wrapper around an unknown opcode is dropped");
				continue;
			}
			expect(valid(r.opcode, b.data(), r.min_len), "opcode accepts its minimum body");
			if (r.min_len)
				expect(check(r.opcode, b.data(), r.min_len - 1) == drop_reason::body_too_short, "opcode drops a short body");
			if (r.max_len < receive_validation_rules::max_body)
			{
				b.resize(r.max_len + 1, 0);
				expect(check(r.opcode, b.data(), r.max_len + 1) == drop_reason::body_too_long, "opcode drops a long body");
			}
		}
		// r11 captured the first 16 bytes of each 35-byte reliable lockstep event. Fill the uncaptured
		// tail with synthetic bytes; the header, inner type and declared length reproduce the live failure.
		std::array<std::uint8_t, 36> lockstep{0x01,0x08,0,0,0,0,0,0,0,0,0,0x10,0x27,0,0,0};
		expect(valid(0x00, lockstep.data(), 35), "captured 35-byte wrapper admits its 33-byte lockstep body");
		expect(check(0x00, lockstep.data(), 34) == drop_reason::body_too_short, "truncated wrapped lockstep is rejected");
		expect(check(0x00, lockstep.data(), 36) == drop_reason::body_too_long, "oversized wrapped lockstep is rejected");
		const std::array<std::uint8_t, 5> idle{0x02,0x0b,0,1,0};
		expect(valid(0x00, idle.data(), 4), "captured four-byte wrapper admits its two-byte idle body");
		// Sender/decoder audit: compact and five-record movement shapes, independent of the rule table.
		const struct { std::uint8_t first; std::size_t second; } movement[] = {{0x01,33},{0x02,2},{0x05,8},
			{0x06,167},{0x68,6},{0x6a,142}};
		std::array<std::uint8_t, 170> movement_body{};
		for (const auto& shape : movement)
		{
			expect(valid(shape.first, movement_body.data(), shape.second), "native sender movement body passes");
			movement_body[0] = shape.first;
			expect(valid(0x00, movement_body.data(), shape.second + 2), "wrapped sender movement body passes");
			movement_body[0] = 0;
		}
		std::vector<std::uint8_t> b(1400, 0);
		expect(valid(0x2e, b.data(), 12), "stock twelve-byte synchronization record passes with native struct padding");
		b[0] = 0x2e;
		expect(valid(0x00, b.data(), 14), "reliable wrapper preserves the twelve-byte synchronization body");
		b[0] = 0;
		expect(valid(0x2b, b.data(), 12), "stock twelve-byte menu reply passes with native struct padding");
		b[0] = 0x2b;
		expect(valid(0x00, b.data(), 14), "reliable wrapper preserves the twelve-byte menu reply");
		b[0] = 0;
		expect(check(0x01, b.data(), 1286) == drop_reason::body_too_long, "any body past the stock cap is dropped");
		static constexpr std::uint8_t unknown_opcodes[] = {0x0b, 0x0c, 0x0d, 0x0e, 0x15, 0x20, 0x55, 0x73, 0x80, 0xff};
		for (const auto op : unknown_opcodes)
			expect(check(op, b.data(), 8) == drop_reason::unknown_opcode, "unregistered opcode is dropped");
		static constexpr std::uint8_t fragment_opcodes[] = {0x12, 0x13};
		for (const auto op : fragment_opcodes)
			expect(check(op, b.data(), 1) == drop_reason::fragment_unsupported, "fragment opcode is dropped");
		expect(valid(0x28, nullptr, 0), "an empty body may be null");
		expect(check(0x14, nullptr, 5) == drop_reason::body_too_short, "a nonzero body may not be null");

		// The index and string fields the handlers trust.
		auto put32 = [](std::uint8_t* p, std::uint32_t v) { std::memcpy(p, &v, 4); };
		put32(b.data(), 3); expect(valid(0x1b, b.data(), 8), "admission follow-up with slot 3 passes");
		put32(b.data(), 4); expect(check(0x1b, b.data(), 8) == drop_reason::bad_index, "admission follow-up with slot 4 is dropped");
		put32(b.data(), 0xffffffffu); expect(check(0x1b, b.data(), 8) == drop_reason::bad_index, "admission follow-up with slot -1 is dropped");
		std::memset(b.data(), 0, 100); put32(b.data() + 0x30, 3); expect(valid(0x1f, b.data(), 77), "participant reply with slot 3 passes");
		put32(b.data() + 0x30, 4); expect(check(0x1f, b.data(), 77) == drop_reason::bad_index, "participant reply with slot 4 is dropped");
		static constexpr std::uint8_t menu_index_opcodes[] = {0x29, 0x46};
		for (const auto op : menu_index_opcodes)
		{
			put32(b.data(), 5); expect(valid(op, b.data(), 4), "menu index 5 passes");
			put32(b.data(), 6); expect(check(op, b.data(), 4) == drop_reason::bad_index, "menu index 6 is dropped");
		}
		b[0] = 5; expect(valid(0x5d, b.data(), 40), "lockstep slot 5 passes");
		b[0] = 6; expect(check(0x5d, b.data(), 40) == drop_reason::bad_index, "lockstep slot 6 is dropped");
		b[0] = 0xff; expect(check(0x5d, b.data(), 40) == drop_reason::bad_index, "lockstep slot -1 is dropped");

		std::memset(b.data(), 'a', 224); b[0x3e] = 0;
		expect(valid(0x18, b.data(), 224), "game info with the agreement record and a terminated name passes");
		std::memset(b.data(), 'a', 224); expect(check(0x18, b.data(), 224) == drop_reason::unterminated_string, "game info without a terminator is dropped");
		expect(check(0x18, b.data(), 225) == drop_reason::body_too_long, "game info past the agreement size is dropped");
		std::memset(b.data(), 0, 238); b[0xaa] = 0; expect(valid(0x44, b.data(), 238), "roster with no entries passes");
		b[0xaa] = 0xff; expect(check(0x44, b.data(), 238) == drop_reason::roster_underflow, "roster with 255 entries in 238 bytes is dropped");
		b[0xaa] = 3; expect(valid(0x44, b.data(), 238), "roster with 3 entries in 238 bytes passes");
		std::memcpy(b.data(), "name", 5); expect(valid(0x72, b.data(), 5), "terminated 0x72 string passes");
		std::memcpy(b.data(), "name!", 5); expect(check(0x72, b.data(), 5) == drop_reason::unterminated_string, "unterminated 0x72 string is dropped");

		// 'H': stock chunks are exactly 200 bytes; FastTransfer records are the agreed shapes.
		expect(valid(0x48, b.data(), 200), "stock campaign chunk passes");
		expect(check(0x48, b.data(), 199) == drop_reason::body_too_short, "short stock chunk is dropped");
		expect(check(0x48, b.data(), 201) == drop_reason::body_too_long, "long stock chunk is dropped");
		context fast; fast.fast_transfer = true;
		std::memset(b.data(), 7, 400); b[0] = 'D';
		expect(valid(0x48, b.data(), 300, fast), "fast data record of 299 bytes passes");
		expect(valid(0x48, b.data(), 2, fast), "fast data record of 1 byte passes");
		expect(check(0x48, b.data(), 1, fast) == drop_reason::malformed_record, "tag-only record is dropped");
		expect(check(0x48, b.data(), 301, fast) == drop_reason::malformed_record, "data record over 299 bytes is dropped");
		b[0] = 'Z'; b[1] = 64; b[2] = b[3] = b[4] = 0;
		expect(valid(0x48, b.data(), 5, fast), "zero-run record passes");
		expect(check(0x48, b.data(), 6, fast) == drop_reason::malformed_record, "long zero-run record is dropped");
		b[1] = 0; expect(check(0x48, b.data(), 5, fast) == drop_reason::malformed_record, "empty zero-run record is dropped");
		b[0] = 'X'; expect(check(0x48, b.data(), 5, fast) == drop_reason::malformed_record, "unknown record tag is dropped");
	}

	void native_dispatch_tests()
	{
		copy(0x60dbe0, 0x70); // the retail common event dispatch
		for (const auto& h : {va_of(&handler<0>), va_of(&handler<1>), va_of(&handler<2>), va_of(&handler<3>),
		                      va_of(&handler<4>), va_of(&handler<5>), va_of(&handler<6>), va_of(&handler<7>)})
			if (h == 0x612f60) { std::puts("fixture clash: a synthetic handler landed on the dispatch's menu special case"); std::exit(2); }
		clear_registrations();

		// OFF: nothing is contributed, one log line, the dispatch stays stock.
		ini::enabled = false;
		{
			patch_transaction::transaction off;
			receive_validation::contribute(off, GetModuleHandleW(nullptr));
			expect(off.contributions() == 0 && logger::has("ReceiveValidation=0"), "OFF contributes no writes and says so");
			expect(!receive_validation::installed(), "OFF is not installed");
		}
		ini::enabled = true;

		// Guard mismatch: the entry differs, the module stays out and nothing is written.
		at(0x60dbe0)[0] ^= 1;
		{
			patch_transaction::transaction mismatch;
			receive_validation::contribute(mismatch, GetModuleHandleW(nullptr));
			expect(mismatch.contributions() == 0 && logger::has("not the retail one"), "a changed dispatch entry skips the hook with one log line");
		}
		at(0x60dbe0)[0] ^= 1;

		// Forced readback failure: everything rolls back to the retail bytes and nothing publishes.
		{
			patch_transaction::hooks fault;
			fault.after_apply = [] { at(0x60dbe1)[0] ^= 0xff; };
			patch_transaction::transaction t;
			receive_validation::contribute(t, GetModuleHandleW(nullptr));
			expect(t.commit(&fault) == patch_transaction::outcome::rolled_back, "a forced readback failure rolls the hook back");
			const auto expected = patch_transaction::bytes_of("5356578b7c2410");
			expect(std::memcmp(at(0x60dbe0), expected.data(), expected.size()) == 0 && !receive_validation::installed(),
			       "rollback leaves the retail dispatch byte-identical and unpublished");
		}

		// Production install through the transaction.
		{
			patch_transaction::transaction t;
			receive_validation::contribute(t, GetModuleHandleW(nullptr));
			expect(t.commit() == patch_transaction::outcome::committed, "the production guard installs with verified readback");
			expect(receive_validation::installed() && at(0x60dbe0)[0] == 0xe9, "the dispatch entry is the guard's jump");
			expect(logger::has("receive validation installed"), "the install logs its one line");
		}

		// Valid traffic still reaches its handler, with the registration context and result intact.
		enroll(0x01, &handler<0>, 0xabc, 0);
		make_event(0x01, 33);
		expect(dispatch(event.data()) == 0 && calls[0] == 1 && last_context[0] == (int)0xabc, "a valid lockstep record reaches its handler");
		return_value[0] = 5;
		expect(dispatch(event.data()) == 5 && calls[0] == 2, "the handler's result passes through the guard");
		return_value[0] = 0;

		// Registration order and the flags gate are the native ones, guard in place.
		enroll(0x02, &handler<1>, 0x100, 0);
		enroll(0x02, &handler<2>, 0x200, 0);
		return_value[2] = 3;
		make_event(0x02, 2);
		expect(dispatch(event.data()) == 3 && calls[1] == 1 && calls[2] == 1 && last_context[2] == 0x200,
		       "both registrations are tried in order until one consumes the message");
		return_value[2] = 0;
		enroll(0x03, &handler<3>, 0x300, 0);
		make_event(0x03, 6, 1); // the event asks for flag bit 0, which a flags-0 registration does not have
		expect(dispatch(event.data()) == 0 && calls[3] == 0 && receive_validation::dropped() == 0,
		       "the native flags gate still applies after the guard (and is not counted as a drop)");
		make_event(0x03, 6, 0);
		expect(dispatch(event.data()) == 0 && calls[3] == 1, "the flags-0 registration runs for a connected peer's event");

		// Invalid traffic is dropped before any handler, with the dispatch's no-handler return.
		make_event(0x01, 32);
		expect(dispatch(event.data()) == 0 && calls[0] == 2 && receive_validation::dropped() == 1, "a short body is dropped before the handler");
		expect(logger::has("dropped opcode 0x01 (32 bytes): body too short"), "the drop is logged with opcode, size and reason");
		enroll(0x13, &handler<4>, 0, 0);
		make_event(0x13, 20);
		expect(dispatch(event.data()) == 0 && calls[4] == 0 && receive_validation::dropped() == 2, "an orphan fragment chunk is dropped before the fragment handler");
		enroll(0x55, &handler<5>, 0, 0); // nobody registers 0x55 natively; a registration proves the drop precedes dispatch
		make_event(0x55, 8);
		expect(dispatch(event.data()) == 0 && calls[5] == 0 && receive_validation::dropped() == 3, "an unknown opcode is dropped even when registered");
		enroll(0x1b, &handler<6>, 0, 0);
		make_event(0x1b, 8);
		std::memset(body.data(), 0, 8);
		std::memcpy(body.data(), "\x63\0\0\0", 4); // slot 99
		expect(dispatch(event.data()) == 0 && calls[6] == 0 && receive_validation::dropped() == 4, "an admission follow-up with slot 99 is dropped");
		std::memset(body.data(), 0, 8);
		expect(dispatch(event.data()) == 0 && calls[6] == 1, "a valid admission follow-up passes");
		enroll(0x48, &handler<7>, 0, 0);
		make_event(0x48, 201);
		expect(dispatch(event.data()) == 0 && calls[7] == 0 && receive_validation::dropped() == 5, "an oversized campaign chunk is dropped");
		make_event(0x48, 200);
		expect(dispatch(event.data()) == 0 && calls[7] == 1, "a stock 200-byte chunk passes");
		entity_capacity::fast_transfer = true;
		std::memset(body.data(), 9, 300);
		body[0] = 'D';
		make_event(0x48, 300);
		expect(dispatch(event.data()) == 0 && calls[7] == 2, "a FastTransfer record passes when FastTransfer is installed");
		make_event(0x48, 201);
		expect(dispatch(event.data()) == 0 && calls[7] == 3 && receive_validation::dropped() == 5, "a 201-byte data record is a valid fast shape and passes");
		entity_capacity::fast_transfer = false;
		make_event(0x10, 1286);
		expect(dispatch(event.data()) == 0 && receive_validation::dropped() == 6, "a body past the stock cap is dropped");

		// Register preservation through both paths.
		drive_dispatcher = dispatcher.data();
		make_event(0x01, 32);
		drive_event = event.data();
		drive();
		expect(drive_result == 0 && std::memcmp(regs_before, regs_after, sizeof(regs_before)) == 0,
		       "the drop path preserves ebx/esi/edi/ebp and returns 0");
		make_event(0x01, 33);
		drive();
		expect(drive_result == 0 && std::memcmp(regs_before, regs_after, sizeof(regs_before)) == 0,
		       "the accept path preserves ebx/esi/edi/ebp through the resume");
		expect(calls[0] == 3, "the resumed dispatch still reached the handler");

		// The development audit must preserve handler delivery and distinguish would-drops from drops.
		copy(0x60dbe0, 0x70);
		SetEnvironmentVariableW(L"XML2FIX_RECEIVE_VALIDATION_LOG_ONLY", L"1");
		patch_transaction::transaction audit;
		receive_validation::contribute(audit, GetModuleHandleW(nullptr));
		expect(audit.commit() == patch_transaction::outcome::committed, "development audit installs the same guarded hook");
		SetEnvironmentVariableW(L"XML2FIX_RECEIVE_VALIDATION_LOG_ONLY", nullptr);
		const auto drops_before_audit = receive_validation::dropped();
		for (int i = 0; i < 10; ++i)
		{
			make_event(0x01, 32);
			dispatch(event.data());
		}
		expect(calls[0] == 13 && receive_validation::dropped() == drops_before_audit,
		       "log-only audit delivers every would-drop without incrementing actual drops");
		unsigned audit_lines = 0;
		for (const auto& line : logger::logged) if (line.find("would drop opcode") != std::string::npos) ++audit_lines;
		expect(audit_lines == 10, "log-only audit records every would-drop beyond the enforcement log limit");
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc != 2)
	{
		std::puts("Supply the locally owned retail executable.");
		return 2;
	}
	std::ifstream f(argv[1], std::ios::binary);
	binary.assign(std::istreambuf_iterator<char>(f), {});
	if (binary.size() < 4096) return 2;
	const auto start = reinterpret_cast<std::uintptr_t>(fixture_space);
	if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || start > 0x60d000 || start + sizeof fixture_space < 0x60dc60)
		return 2;
	DWORD old = 0;
	if (!VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old)) return 2;
	rule_table_tests();
	native_dispatch_tests();
	std::printf("Receive validation: %d check(s), %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
