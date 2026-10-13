#pragma once

// The script function limit, kept apart from the patching so the tests can check it without the game:
// [Limits] ScriptFunctions, the two code sites of XMen2.exe it changes with their retail bytes, and the
// overflow table that holds every name past the game's 320.
//
// The game's names (forced_teams_rules.hpp has the registration): the script system ([0x787740]) keeps
// its functions in a red-black tree of a fixed pool of 0x140 = 320 nodes inside the object - node i at
// sys+0x0c+16*i (key = the interned name at +0xc of the node), the used count at sys+0x1948, value i
// (the FuncEntry*) at sys+0x194c+4*i. 0x4d75a0 registers a table: for each entry it interns the name,
// looks it up (0x5ab7d0) and, if absent and the count isn't 0x140 (0x4d7637), inserts it (0x4d7310).
// A registration past 320 is skipped without a word (0x4d7641 je 0x4d768f). Scripts resolve names at
// compile time only: 0x4d8970 -> 0x4d6910(name) (its only caller is 0x4d8981), which returns the
// FuncEntry* or 0; the compiler copies entry->func into the statement node and types the statement by
// entry->ret, so no node index ever leaves the tree (value i has exactly two accessors in the whole
// exe: the insert's store 0x4d738d and the lookup's load 0x4d695d). The object is destroyed only at
// exit (0x401d02 -> 0x4d8740).
//
// The raise leaves the tree, its 320 nodes and every stock name's index as they are, and adds a table
// of the DLL's own behind it:
// - registration (0x4d7637, 12 bytes: the cap's cmp and je): when the tree is full, the entry goes to
//   the overflow table instead of being dropped; otherwise the game's insert runs as before;
// - lookup (0x4d696a, 5 bytes: the miss path's epilogue): a name the tree doesn't have is looked up in
//   the overflow table before the game is told "no such function".
// The tree is always searched first, so a name in both (impossible: registration skips a name the
// tree has, and the overflow skips one it has) resolves as before. Names compare byte for byte, as the
// game's interned handles do (case-sensitive).

#include "ini_rules.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace script_functions_rules
{
	// ---- [Limits] ScriptFunctions -------------------------------------------------------------------

	constexpr int stock_capacity = 0x140;   // the tree's pool (0x4d5fa4, 0x4d7637)
	constexpr int default_capacity = 1024;  // no key while the fix registers its own functions: they fill the 320 by themselves
	constexpr int max_capacity = 4096;

	struct choice
	{
		int capacity = stock_capacity;
		std::string note; // for the log; empty when the key is absent or valid
	};

	// No key: 1024 when the fix registers its own script functions ([Game] ForcedTeams - with them the
	// game's 320 are full), otherwise the game's own 320, as every [Limits] key without its value.
	// 320 keeps the game's own limit (the overflow isn't installed); 321..4096 sets the total; anything
	// else is logged and the no-key choice used.
	inline choice decide(const std::optional<std::string_view> text, const bool fix_functions)
	{
		choice result;
		const int fallback = fix_functions ? default_capacity : stock_capacity;
		result.capacity = fallback;
		if (!text) return result;
		const auto value = ini_rules::value_text(*text);
		int number = 0;
		bool ok = !value.empty() && value.size() <= 6;
		for (const char c : value)
		{
			if (c < '0' || c > '9') { ok = false; break; }
			number = number * 10 + (c - '0');
		}
		if (ok && number >= stock_capacity && number <= max_capacity)
		{
			result.capacity = number;
			return result;
		}
		result.note = "ScriptFunctions=" + std::string(value) + " isn't a number from 320 to 4096 (320 is the game's own) - using " + std::to_string(fallback);
		return result;
	}

	// ---- The shared gameplay agreement (rule 14, docs/gameplay-agreement.md) ----------------------------

	// Online play is lockstep: a peer whose raise was refused drops every script statement calling a function
	// past 320 while the other runs it. Rule 14, revision 1, values {installed capacity, 0, 0, 0}:
	// - plain XML2 (no key and no [Game] ForcedTeams): {0,0,0,0}, verified - such peers match each other;
	// - the game's own limit chosen (ScriptFunctions=320, or an invalid key without ForcedTeams): {320,0,0,0};
	// - a raise installed: {capacity,0,0,0}, verified;
	// - a raise requested but not installed: {320,0,0,0}, NOT verified - the record is poisoned, host/join closed.
	constexpr std::uint16_t agreement_id = 14;
	constexpr std::uint16_t agreement_revision = 1;

	struct agreement
	{
		std::array<std::uint32_t, 4> values{};
		bool verified = true;
	};

	inline agreement agreement_entry(const bool key_present, const bool fix_functions, const int chosen, const int installed)
	{
		if (chosen == stock_capacity)
		{
			if (!key_present && !fix_functions) return {{0, 0, 0, 0}, true};
			return {{static_cast<std::uint32_t>(stock_capacity), 0, 0, 0}, true};
		}
		return {{static_cast<std::uint32_t>(installed), 0, 0, 0}, installed == chosen};
	}

	// ---- The two sites ------------------------------------------------------------------------------

	constexpr std::uint32_t register_site = 0x4d7637;   // cmp dword [edi+0x1948],0x140 ; je 0x4d768f
	constexpr std::uint32_t register_insert = 0x4d7643; // the game's insert (the cap not reached)
	constexpr std::uint32_t register_next = 0x4d768f;   // the next entry
	constexpr std::uint32_t lookup_site = 0x4d696a;     // pop edi ; pop esi ; mov eax,ebx ; pop ebx (then ret 4)
	constexpr std::uint32_t count_offset = 0x1948;      // the tree's used count in the script system

	constexpr std::array<std::uint8_t, 12> register_before{0x81, 0xbf, 0x48, 0x19, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x74, 0x4c};
	constexpr std::array<std::uint8_t, 5> lookup_before{0x5f, 0x5e, 0x8b, 0xc3, 0x5b};

	struct guard
	{
		std::uint32_t va;
		std::string_view hex;
		const char* what;
	};

	// Everything the stubs assume: the registration loop's registers and frame around the cap (EDI = the
	// script system, EBP = &entry->name, the entry count at [esp+0xc] of the loop), the resume points, and
	// the whole lookup: on a miss EBX = 0 and ESI still holds the name's text - its argument slot does not,
	// 0x4d693c stores the interned handle there - then EDI/ESI/EBX are popped and it returns with ret 4.
	inline constexpr std::array<guard, 5> guards{{
		{0x4d75a0, "8b44240483ec0885c0578bf90f8ef1000000558b6c241856", "the registration's frame (0x4d75a0: count, EDI = system, EBP = table+4)"},
		{0x4d7630, "3dffffff3f755881bf4819000040010000744c8b750085f68d45fc", "the registration's cap (0x4d7637) and its insert (0x4d7643: entry = EBP-4)"},
		{0x4d768f, "8b44240c83c510488944240c0f851fffffff5e5d5f83c408c20800", "the registration's next entry (0x4d768f)"},
		{0x4d6910, "53568b74240c33db3bf3578bf97423381e741f5356e8f6b7120083c404405056e80bb812008bc8e8e439f4ff89442410eb04895c24108b4704508d4c2414518bcfe87a4e0d003dffffff3f740d8b84874c1900005f5e5bc204005f5e8bc35bc20400",
		 "the whole lookup (0x4d6910-0x4d6971: ESI = the name throughout, EBX = 0, its argument slot overwritten by the interned handle)"},
		{0x4d8981, "e88adfffff8bf885ff", "the lookup's one caller (0x4d8981)"},
	}};

	inline void put_jump(std::uint8_t* out, const std::uint32_t from, const std::uint32_t to)
	{
		out[0] = 0xe9;
		const std::uint32_t delta = to - (from + 5);
		std::memcpy(out + 1, &delta, 4);
	}

	inline std::array<std::uint8_t, 12> register_after(const std::uint32_t stub)
	{
		std::array<std::uint8_t, 12> bytes{};
		bytes.fill(0x90);
		put_jump(bytes.data(), register_site, stub);
		return bytes;
	}

	inline std::array<std::uint8_t, 5> lookup_after(const std::uint32_t stub)
	{
		std::array<std::uint8_t, 5> bytes{};
		put_jump(bytes.data(), lookup_site, stub);
		return bytes;
	}

	// ---- The overflow table -------------------------------------------------------------------------

	// The game's 16-byte entry {handler, name, return signature, argument signature}; the table keeps the
	// pointer, as the tree does, so the entry must outlive the game.
	struct func_entry
	{
		const void* handler;
		const char* name;
		const char* ret;
		const char* args;
	};

	enum class added : std::uint8_t
	{
		added,
		duplicate, // the name is here already: the first registration wins, as in the tree
		no_name,   // NULL or "": nothing a script could call
		full,
	};

	// Fixed storage (no allocation on the game's thread), searched linearly: it is consulted only when a
	// name isn't in the tree, once per call statement at compile time.
	class overflow_table
	{
	public:
		static constexpr std::size_t storage = max_capacity - stock_capacity;

		void reset(const std::size_t capacity)
		{
			capacity_ = capacity < storage ? capacity : storage;
			count_ = 0;
			refused_ = 0;
		}
		std::size_t capacity() const { return capacity_; }
		std::size_t count() const { return count_; }
		std::size_t refused() const { return refused_; }

		const func_entry* find(const char* name) const
		{
			if (!name || !*name) return nullptr;
			for (std::size_t i = 0; i < count_; ++i)
			{
				if (std::strcmp(entries_[i]->name, name) == 0) return entries_[i];
			}
			return nullptr;
		}

		added add(const func_entry* entry)
		{
			if (!entry || !entry->name || !*entry->name) return added::no_name;
			if (find(entry->name)) return added::duplicate;
			if (count_ >= capacity_)
			{
				++refused_;
				return added::full;
			}
			entries_[count_++] = entry;
			return added::added;
		}

	private:
		std::array<const func_entry*, storage> entries_{};
		std::size_t capacity_ = 0, count_ = 0, refused_ = 0;
	};
}
