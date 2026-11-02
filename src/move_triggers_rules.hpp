#pragma once

#include "limits_rules.hpp"
#include "fight_moves_layout.hpp"

// CFightMove's layout stays native. Its old inline handle array becomes a pointer to one
// preallocated side array for each published pool slot (332 at stock). Count, tags, event
// ownership, copies and destruction remain the game's own. These are not save records.
namespace move_triggers_rules
{
	using namespace limits_rules;
	constexpr int stock_capacity = 19;
	constexpr int default_capacity = 64;
	constexpr int max_capacity = 127; // parser's signed imm8 comparison
	constexpr DWORD pool_pointer = 0x789e1c;
	constexpr DWORD record_size = 0x148;
	constexpr DWORD record_count = fight_moves_layout::stock_count; // legacy callers/tests; production uses descriptor
	constexpr DWORD array_pointer = 0xcc;
	constexpr DWORD count_offset = 0x11c;
	constexpr DWORD constructor = 0x500540;
	constexpr DWORD constructor_call = 0x500afd;

	inline int capacity(const std::optional<std::string>& text)
	{
		const auto n = text ? parse_count(*text) : std::nullopt;
		return n && *n >= stock_capacity && *n <= max_capacity ? *n : stock_capacity;
	}

	constexpr std::optional<DWORD> slot(const DWORD pool, const DWORD move)
	{
		return fight_moves_layout::slot({pool, record_count}, move);
	}
	constexpr std::optional<DWORD> slot(fight_moves_layout::pool_descriptor pool, DWORD move)
	{
		return fight_moves_layout::slot(pool, move);
	}
	// W3-3 owns these relocated operands once its descriptor is published. Keep
	// every original stock guard when absent; retain the non-operand prefix when on.
	inline guard descriptor_guard(guard g, bool raised)
	{
		if (raised && g.va == 0x4f7a9d) g.hex = g.hex.substr(0, 2);
		if (raised && g.va == 0x4f6260) g.hex = g.hex.substr(0, 12);
		return g;
	}

	constexpr DWORD timer_slots = 1024; // cleared after native actor-arena teardown
	struct actor_times
	{
		DWORD actor = 0, packet = 0, move = 0;
		std::array<DWORD, max_capacity> scheduled{}, source{}; // float bits, no rounding in these helpers
	};
	inline actor_times* times_for(std::span<actor_times> slots, DWORD actor)
	{
		if (!actor || slots.size() != timer_slots) return nullptr;
		const DWORD first = ((actor >> 3) * 2654435761u) & (timer_slots - 1);
		for (DWORD i = 0; i < timer_slots; ++i)
		{
			auto& slot = slots[(first + i) & (timer_slots - 1)];
			if (slot.actor == actor) return &slot;
			if (!slot.actor)
			{
				slot.scheduled.fill(0xbf800000); slot.source.fill(0xbf800000);
				slot.actor = actor;
				return &slot;
			}
		}
		return nullptr;
	}
	inline void retire_times(std::span<actor_times> slots) { std::fill(slots.begin(), slots.end(), actor_times{}); }
	struct source_scope
	{
		DWORD packet, move;
		std::array<DWORD, max_capacity> source;
		explicit source_scope(const actor_times& times) : packet(times.packet), move(times.move), source(times.source) {}
		void finish(actor_times& times) const
		{
			times.packet = packet; times.move = move; times.source = source;
		}
	};
	struct scheduler_hooks
	{
		DWORD timers, source, collect, clear, initialize, tick, start = 0, retire = 0;
	};

	struct access
	{
		DWORD va;
		std::string_view retail;
		// LEA -> MOV needs just an opcode change. Indexed loads/stores use a thunk
		// preserving scratch registers and flags. No stack-relative operand is relocated.
		std::string_view thunk;
	};
	inline constexpr std::array<access, 10> accesses{{
		{0x4f6420, "8bbc86c8000000", "528b96cc0000008b7c82fc5a"},
		{0x4f64c6, "8b8cbecc000000", "528b96cc0000008b0cba5a"},
		{0x4f64d6, "8b8cbecc000000", "528b96cc0000008b0cba5a"},
		{0x4f6518, "8d9fcc000000", ""},
		{0x4f6562, "8db5cc000000", ""},
		{0x4f6987, "8bbc86c8000000", "528b96cc0000008b7c82fc5a"},
		{0x4f6aff, "89948ecc000000", "508b86cc00000089148858"},
		{0x4f7dab, "8d8fcc000000", ""},
		{0x4f7e35, "899c8ecc000000", "528b96cc000000891c8a5a"},
		{0x4f8046, "8bbc86c8000000", "528b96cc0000008b7c82fc5a"},
	}};
	inline constexpr std::array<guard, 29> guards{{
		{0x4f6aa7, "83be1c01000013"},
		{constructor_call, "e83efaffff"},
		{0x500605, "89861c010000"},
		{0x500626, "e80562ffff"},
		{0x4f7a90, "a11c9e780085c07529"},
		{0x4f7a9d, "68fcae0100"},
		{0x500df8, "69ff48010000"},
		{0x4f6260, "53568bd98b8360a90100"},
		{0x430f86, "33ed8b17558bcfff5220d95cac2c4583fd137cee", "nineteen-value packet producer and stack layout"},
		{0x430d46, "538a9c24d400000084db5556578bf1", "packet frame saved registers"},
		{0x4613e0, "8b0dbc8f710085c9742b8b01ff50088b0dbc8f71008b116a00ff12a1bc8f71006a0e50e898f20f0083c408c705bc8f710000000000c3", "actor arena teardown"},
		{0x469a6c, "e86f79ffff", "actor arena teardown caller"},
		{0x430f9a, "8b8424d0000000", "timing packet capture"},
		{0x42c3cb, "8d8740060000", "scheduled timer destination"},
		{0x42c3dc, "8d4608d99f94060000", "timing source and native x87 store"},
		{0x42c486, "83fb1389542410894c24140f8c79ffffff", "timer initialization loop"},
		{0x42c4e5, "be130000008d9b00000000", "inactive source clear loop"},
		{0x430a4c, "8d9e40060000", "paused timer destination"},
		{0x430aa0, "8dae40060000", "active timer destination"},
		{0x430a8c, "83ff137cc1", "paused timer bound"},
		{0x430b3e, "83ff130f8c69ffffff", "active timer bound"},
		{0x42c28a, "898e40060000", "clear all actor timers"},
		{0x42c390, "83ec10568b742418", "timer initializer prologue"},
		{0x430a00, "5355568bf1", "timer tick prologue"},
		{0x430d40, "81ecb4000000", "move start prologue and packet lifetime"},
		{0x4315c7, "c22000", "move start consumes eight stack arguments"},
		{0x42c510, "c20800", "initializer consumes packet and scale"},
		{0x430a95, "c20400", "paused dispatch consumes delta"},
		{0x430c21, "c20400", "active dispatch consumes delta"},

	}};

	template <typename Check>
	DWORD mismatch(Check check, bool raised = false)
	{
		for (const auto& a : accesses) if (!check(a.va, a.retail)) return a.va;
		for (const auto& original : guards) { const auto g = descriptor_guard(original, raised); if (!check(g.va, g.hex)) return g.va; }
		return 0;
	}

	struct plan
	{
		std::vector<operand_write> writes;
		std::vector<std::uint8_t> code;
		DWORD original_initialize = 0, original_tick = 0, original_start = 0, original_retire = 0;
	};
	inline plan make_plan(const int cap, const DWORD init, const DWORD code_address, const scheduler_hooks* hooks = nullptr)
	{
		plan p;
		p.writes.push_back({0x4f6aad, 1, static_cast<DWORD>(cap)});
		p.writes.push_back({constructor_call + 1, 4, rel32(constructor_call, init)});
		for (const auto& a : accesses)
		{
			if (a.thunk.empty())
			{
				p.writes.push_back({a.va, 1, 0x8b});
				continue;
			}
			const DWORD entry = code_address + static_cast<DWORD>(p.code.size());
			for (std::size_t i = 0; i < a.thunk.size(); i += 2)
				p.code.push_back(hex_byte(a.thunk, i / 2));
			const DWORD jump = code_address + static_cast<DWORD>(p.code.size());
			const DWORD target = a.va + static_cast<DWORD>(hex_size(a.retail));
			p.code.push_back(0xe9);
			const DWORD relative = rel32(jump, target);
			for (int i = 0; i < 4; ++i) p.code.push_back(static_cast<std::uint8_t>(relative >> (i * 8)));
			p.writes.push_back({a.va, 1, 0xe9});
			p.writes.push_back({a.va + 1, 4, rel32(a.va, entry)});
			for (DWORD i = 5; i < hex_size(a.retail); ++i) p.writes.push_back({a.va + i, 1, 0x90});
		}
		if (hooks)
		{
			const auto bytes = [](std::string_view hex) { std::vector<std::uint8_t> v; for (std::size_t i = 0; i < hex_size(hex); ++i) v.push_back(hex_byte(hex, i)); return v; };
			const auto relative = [&](std::vector<std::uint8_t>& v, std::uint8_t op, DWORD target) {
				const DWORD r = rel32(code_address + static_cast<DWORD>(p.code.size() + v.size()), target);
				v.push_back(op); for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>(r >> (8 * i)));
			};
			const auto redirect = [&](DWORD va, DWORD size, std::vector<std::uint8_t> body, DWORD resume) {
				const DWORD entry = code_address + static_cast<DWORD>(p.code.size());
				relative(body, 0xe9, resume); p.code.insert(p.code.end(), body.begin(), body.end());
				p.writes.push_back({va, 1, 0xe9}); p.writes.push_back({va + 1, 4, rel32(va, entry)});
				for (DWORD i = 5; i < size; ++i) p.writes.push_back({va + i, 1, 0x90});
			};
			// pushfd/pushad protect all integer state. Helpers do no x87 arithmetic; the
			// original native loops keep their extended-precision scheduling and comparisons.
			const auto pointer = [&](DWORD va, DWORD size, std::string_view args, DWORD target, unsigned saved_register, std::string_view replay) {
				auto body = bytes("9c60" + std::string(args)); relative(body, 0xe8, target);
				const auto finish = bytes(args == "5657" ? "83c408" : "83c404"); body.insert(body.end(), finish.begin(), finish.end());
				body.insert(body.end(), {0x89, 0x44, 0x24, static_cast<std::uint8_t>(saved_register), 0x61, 0x9d});
				const auto tail = bytes(replay); body.insert(body.end(), tail.begin(), tail.end()); redirect(va, size, body, va + size);
			};
			pointer(0x42c3cb, 6, "57", hooks->timers, 28, "");
			pointer(0x42c3dc, 9, "5657", hooks->source, 28, "d99f94060000");
			pointer(0x430a4c, 6, "56", hooks->timers, 16, "");
			pointer(0x430aa0, 6, "56", hooks->timers, 8, "");
			auto collect = bytes("9c608d442448505756"); relative(collect, 0xe8, hooks->collect);
			const auto collect_tail = bytes("83c40c619d8b8424d0000000"); collect.insert(collect.end(), collect_tail.begin(), collect_tail.end());
			redirect(0x430f9a, 7, collect, 0x430fa1);
			auto clear = bytes("9c6056"); relative(clear, 0xe8, hooks->clear);
			const auto clear_tail = bytes("83c404619d898e40060000"); clear.insert(clear.end(), clear_tail.begin(), clear_tail.end());
			redirect(0x42c28a, 6, clear, 0x42c290);
			p.writes.push_back({0x42c488, 1, static_cast<DWORD>(cap)});
			p.writes.push_back({0x42c4e6, 4, static_cast<DWORD>(cap)});
			p.writes.push_back({0x430a8e, 1, static_cast<DWORD>(cap)});
			p.writes.push_back({0x430b40, 1, static_cast<DWORD>(cap)});
			const auto wrap = [&](DWORD va, std::string_view prologue, DWORD wrapper) {
				const DWORD entry = code_address + static_cast<DWORD>(p.code.size()); auto body = bytes(prologue);
				relative(body, 0xe9, va + static_cast<DWORD>(hex_size(prologue))); p.code.insert(p.code.end(), body.begin(), body.end());
				p.writes.push_back({va, 1, 0xe9}); p.writes.push_back({va + 1, 4, rel32(va, wrapper)});
				for (DWORD i = 5; i < hex_size(prologue); ++i) p.writes.push_back({va + i, 1, 0x90}); return entry;
			};
			p.original_initialize = wrap(0x42c390, "83ec10568b742418", hooks->initialize);
			p.original_tick = wrap(0x430a00, "5355568bf1", hooks->tick);
			if (hooks->start) p.original_start = wrap(0x430d40, "81ecb4000000", hooks->start);
			if (hooks->retire) p.original_retire = wrap(0x4613e0, "8b0dbc8f7100", hooks->retire);
		}
		return p;
	}
}
