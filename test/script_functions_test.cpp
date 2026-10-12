// [Limits] ScriptFunctions: the decisions, the patch bytes and the overflow table on their own, then the game's own
// registration (0x4d75a0) and lookup (0x4d6910) copied from a retail XMen2.exe given on the command line (or
// XML2_FIX_TEST_EXE) and executed at their own addresses on a script system of the test's: first as the game has them
// (the 321st name is dropped), then patched with the DLL's own stubs (script_function_overflow.cpp), with ESP and the
// callee-saved registers measured around every call. Every function name here is invented.

#include "forced_teams_rules.hpp"
#include "script_function_overflow.hpp"
#include "script_functions_rules.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace script_functions_rules;

extern "C" std::uint8_t fixture_space[0x400000];

namespace
{
	int checks = 0, failures = 0;
	void check(const bool ok, const std::string& what)
	{
		++checks;
		if (!ok)
		{
			++failures;
			std::printf("FAIL %s\n", what.c_str());
		}
	}

	void* at(const std::uint32_t va) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va)); }
	bool in_fixture(const std::uint32_t va)
	{
		const auto b = reinterpret_cast<std::uintptr_t>(fixture_space);
		return va >= b && va + 0x200 <= b + sizeof fixture_space;
	}
	void jump(const std::uint32_t va, const void* target)
	{
		auto* p = static_cast<std::uint8_t*>(at(va));
		const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target) - (va + 5));
		p[0] = 0xe9;
		std::memcpy(p + 1, &rel, 4);
	}

	// ---- the script system of the test's: the tree's fields at the game's offsets -------------------------------
	constexpr std::uint32_t none = 0x3fffffff;
	alignas(16) std::uint8_t system_block[0x2000];
	std::uint32_t& field(const std::uint32_t offset) { return *reinterpret_cast<std::uint32_t*>(system_block + offset); }
	void reset_system()
	{
		std::memset(system_block, 0, sizeof system_block);
		field(4) = none; // the root
	}

	// The string table (0x602140 returns it, 0x41a320 interns a name into it): a handle per distinct text, byte-exact.
	std::uint8_t string_table[16];
	std::map<std::string, std::uint32_t> handles;
	void* __cdecl stub_table() { return string_table; }
	std::uint32_t __fastcall stub_intern(void* table, void*, const char* text, int length, int)
	{
		if (table != string_table) return 0;
		const std::string key(text, static_cast<std::size_t>(length > 0 ? length - 1 : 0));
		const auto found = handles.find(key);
		if (found != handles.end()) return found->second;
		const auto handle = static_cast<std::uint32_t>(handles.size() + 1);
		handles.emplace(key, handle);
		return handle;
	}

	// The insert (0x4d7310, thiscall(key*, value*) ret 8), unbalanced but with the game's node layout, so the game's
	// own find (0x5ab7d0) walks it: node i at +0x0c+16i, left +4, right +8, key +0xc; value i at +0x194c+4i.
	int inserts = 0;
	void __fastcall stub_insert(void* map, void*, const std::uint32_t* key, const std::uint32_t* value)
	{
		++inserts;
		if (map != system_block) return;
		const std::uint32_t index = field(count_offset);
		const std::uint32_t node = 0x0c + 16 * index;
		field(node + 4) = none;
		field(node + 8) = none;
		field(node + 0xc) = *key;
		field(0x194c + 4 * index) = *value;
		field(count_offset) = index + 1;
		if (field(4) == none)
		{
			field(4) = index;
			return;
		}
		std::uint32_t at_node = field(4);
		for (;;)
		{
			const std::uint32_t base = 0x0c + 16 * at_node;
			const auto there = static_cast<std::int32_t>(field(base + 0xc));
			std::uint32_t& next = field(base + (there > static_cast<std::int32_t>(*key) ? 4 : 8));
			if (next == none)
			{
				next = index;
				return;
			}
			at_node = next;
		}
	}

	// ---- calls with ESP and EBX/ESI/EDI measured ---------------------------------------------------------------
	std::uint32_t esp_errors = 0, register_errors = 0;
	std::uint32_t call_this2(const std::uint32_t fn, void* self, const std::uint32_t a1, const std::uint32_t a2, const int stack_args)
	{
		std::uint32_t before = 0, after = 0, result = 0, b = 0, s = 0, d = 0;
		__asm {
			mov before, esp
			mov ebx, 0x11111111
			mov esi, 0x22222222
			mov edi, 0x33333333
			cmp stack_args, 2
			jne one
			push a2
		one:
			push a1
			mov ecx, self
			call fn
			mov result, eax
			mov after, esp
			mov b, ebx
			mov s, esi
			mov d, edi
		}
		if (before != after) ++esp_errors;
		if (b != 0x11111111 || s != 0x22222222 || d != 0x33333333) ++register_errors;
		return result;
	}

	void register_table(const std::vector<func_entry>& table)
	{
		call_this2(0x4d75a0, system_block, static_cast<std::uint32_t>(table.size()), static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table.data())), 2);
	}
	const func_entry* lookup(const char* name)
	{
		return reinterpret_cast<const func_entry*>(static_cast<std::uintptr_t>(call_this2(0x4d6910, system_block, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(name)), 0, 1)));
	}

	// Invented names: fnA000.. for a table of n entries, each its own handler value.
	std::vector<std::string> name_store;
	std::vector<func_entry> invented(const char* prefix, const int n)
	{
		std::vector<func_entry> out;
		for (int i = 0; i < n; ++i)
		{
			char text[32];
			std::snprintf(text, sizeof text, "%s%03d", prefix, i);
			name_store.emplace_back(text);
		}
		// names are stable once all are in (reserve below)
		for (int i = 0; i < n; ++i)
		{
			const auto& s = name_store[name_store.size() - n + i];
			out.push_back({reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x1000 + i)), s.c_str(), "n", "i"});
		}
		return out;
	}

	void pure_checks()
	{
		check(decide(std::nullopt, true).capacity == 1024 && decide(std::nullopt, true).note.empty(), "no key with the fix's functions: 1024 names");
		check(decide(std::nullopt, false).capacity == 320 && decide(std::nullopt, false).note.empty(), "no key without them: the game's own 320 (nothing changes without keys)");
		check(decide("320", true).capacity == 320 && decide("320", true).note.empty(), "320 = the game's own limit");
		check(decide("4096", false).capacity == 4096 && decide(" 512 ; mods", true).capacity == 512, "321..4096 taken (inline comment ignored)");
		check(decide("4097", true).capacity == 1024 && !decide("4097", true).note.empty() && decide("319", true).capacity == 1024 && decide("-5", true).capacity == 1024 &&
		          decide("0x400", true).capacity == 1024 && decide("", true).capacity == 1024 && decide("9999", false).capacity == 320,
		      "out of range or not a count: logged, the no-key choice");

		const auto reg = register_after(0x10002000);
		const std::uint32_t reg_rel = 0x10002000 - (register_site + 5);
		check(reg[0] == 0xe9 && std::memcmp(&reg[1], &reg_rel, 4) == 0 && reg[5] == 0x90 && reg[11] == 0x90, "registration site: jmp stub + 7 nops over the cap's cmp/je");
		const auto look = lookup_after(0x10003000);
		const std::uint32_t look_rel = 0x10003000 - (lookup_site + 5);
		check(look[0] == 0xe9 && std::memcmp(&look[1], &look_rel, 4) == 0, "lookup site: jmp stub over the miss epilogue's first five bytes");
		check(register_before.size() == 12 && lookup_before.size() == 5, "site sizes");

		overflow_table t;
		t.reset(3);
		const func_entry a{nullptr, "alphaFn", "n", ""}, a2{reinterpret_cast<const void*>(1), "alphaFn", "n", ""}, b{nullptr, "betaFn", "n", ""}, c{nullptr, "gammaFn", "n", ""},
		    d{nullptr, "deltaFn", "n", ""}, empty{nullptr, "", "n", ""}, unnamed{nullptr, nullptr, "n", ""};
		check(t.add(&a) == added::added && t.add(&a2) == added::duplicate && t.find("alphaFn") == &a, "overflow: the first registration of a name wins");
		check(t.add(&empty) == added::no_name && t.add(&unnamed) == added::no_name && t.add(nullptr) == added::no_name && t.count() == 1, "overflow: NULL/empty names skipped");
		check(t.find("ALPHAFN") == nullptr && t.find("alphafn") == nullptr && t.find("") == nullptr && t.find(nullptr) == nullptr, "overflow: byte-exact, case-sensitive names");
		check(t.add(&b) == added::added && t.add(&c) == added::added && t.add(&d) == added::full && t.refused() == 1 && t.count() == 3 && t.find("deltaFn") == nullptr,
		      "overflow: full at its capacity, the rest refused and counted");
		t.reset(1000000);
		check(t.capacity() == overflow_table::storage && overflow_table::storage == 4096 - 320, "overflow: capacity clamped to the storage (4096 names in all)");

		using namespace forced_teams_rules;
		check(builtin_count + retail_count == 308 && builtin_count + table_count <= raised_capacity && raised_capacity == static_cast<DWORD>(default_capacity),
		      "the fix's functions fit the default raise");
		check(past_tree(tree_capacity - builtin_count) == 0 && past_tree(tree_capacity - builtin_count + 1) == 1 && past_tree(table_count + probe_max) == builtin_count + table_count + probe_max - 320,
		      "past_tree counts the names past the game's 320");
		const auto writes = registration_writes(0x12345678, table_count + 5);
		check(writes[1].value == table_count + 5 && registration_writes(0x12345678)[1].value == table_count, "registration count operand: the fix's functions plus probes");
	}
}

int main(int argc, char** argv)
{
	std::setbuf(stdout, nullptr);
	name_store.reserve(4096);
	pure_checks();

	const char* path = argc > 1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");
	if (!path)
	{
		std::printf("SKIPPED native: no executable given\nScript functions: %d checks, %d failure(s)\n", checks, failures);
		return failures ? 1 : 0;
	}
	if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || !in_fixture(0x41a320) || !in_fixture(0x602140))
	{
		std::puts("fixture must cover 0x41a320..0x602200 at image base 0x400000");
		return 2;
	}
	std::ifstream file(path, std::ios::binary);
	const std::vector<std::uint8_t> exe((std::istreambuf_iterator<char>(file)), {});
	if (exe.size() < 0x203000)
	{
		std::printf("SKIPPED native: executable unreadable\nScript functions: %d checks, %d failure(s)\n", checks, failures);
		return failures ? 1 : 0;
	}
	DWORD old = 0;
	VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old);
	// The game's own code: the registration, the lookup, the tree's find and strlen (file offset = VA - 0x400000).
	for (const auto& [va, end] : {std::pair{0x4d75a0u, 0x4d76aau}, std::pair{0x4d6910u, 0x4d6972u}, std::pair{0x5ab7d0u, 0x5ab80du}, std::pair{0x602120u, 0x602131u}})
		std::memcpy(at(va), exe.data() + (va - 0x400000), end - va);
	bool guards_ok = true;
	for (const auto& g : guards)
	{
		for (std::size_t i = 0; i < g.hex.size() / 2 && g.va != 0x4d8981; ++i) // 0x4d8981 (the caller) isn't copied
			guards_ok = guards_ok && static_cast<const std::uint8_t*>(at(g.va))[i] == static_cast<std::uint8_t>(std::stoul(std::string(g.hex.substr(i * 2, 2)), nullptr, 16));
	}
	check(guards_ok && std::memcmp(at(register_site), register_before.data(), register_before.size()) == 0 && std::memcmp(at(lookup_site), lookup_before.data(), lookup_before.size()) == 0,
	      "the executable's code matches every guard and both sites' retail bytes");
	jump(0x602140, reinterpret_cast<const void*>(&stub_table));
	jump(0x41a320, reinterpret_cast<const void*>(&stub_intern));
	jump(0x4d7310, reinterpret_cast<const void*>(&stub_insert));

	// ---- the game as it is: 330 names, the last 10 dropped ---------------------------------------------------------
	const auto first = invented("fnA", 330);
	reset_system();
	register_table(first);
	int found = 0, right = 0;
	for (int i = 0; i < 330; ++i)
	{
		const auto* e = lookup(first[i].name);
		found += e != nullptr;
		right += e == &first[i];
	}
	check(field(count_offset) == 320 && inserts == 320, "retail: the tree takes 320 names and skips the rest without a word");
	check(found == 320 && right == 320 && lookup(first[319].name) == &first[319] && lookup(first[320].name) == nullptr && lookup(first[329].name) == nullptr,
	      "retail: the first 320 resolve to their own entries, the 321st..330th don't resolve (the bug)");

	// ---- patched with the DLL's stubs ------------------------------------------------------------------------------
	const auto reg = register_after(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&script_function_overflow::register_stub)));
	const auto look = lookup_after(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&script_function_overflow::lookup_stub)));
	std::memcpy(at(register_site), reg.data(), reg.size());
	std::memcpy(at(lookup_site), look.data(), look.size());
	auto& overflow = script_function_overflow::table();
	overflow.reset(1024 - 320);
	reset_system();
	inserts = 0;
	register_table(first);
	found = right = 0;
	for (int i = 0; i < 330; ++i)
	{
		const auto* e = lookup(first[i].name);
		found += e != nullptr;
		right += e == &first[i];
	}
	check(field(count_offset) == 320 && inserts == 320 && overflow.count() == 10, "raised: the tree still takes exactly 320 (same nodes, same indices); the other 10 go to the overflow");
	check(found == 330 && right == 330, "raised: all 330 names resolve to their own entries (tree first, then the overflow)");
	bool same_index = true;
	for (std::uint32_t i = 0; i < 320; ++i) same_index = same_index && field(0x194c + 4 * i) == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&first[i]));
	check(same_index, "raised: value i of the tree is entry i, as without the raise (stock indices unchanged)");
	check(lookup("fnZ999") == nullptr && lookup("FNA325") == nullptr && lookup("fnA32") == nullptr, "raised: an unknown name (or another case) still resolves to NULL");

	// A second table: duplicates of a tree name and an overflow name are skipped, new names appended.
	std::vector<func_entry> second{{reinterpret_cast<const void*>(0x9001), first[5].name, "i", ""}, {reinterpret_cast<const void*>(0x9002), first[325].name, "i", ""}};
	for (const auto& e : invented("fnB", 3)) second.push_back(e);
	register_table(second);
	check(lookup(first[5].name) == &first[5] && lookup(first[325].name) == &first[325] && overflow.count() == 13 && lookup(second[4].name) == &second[4],
	      "raised: the first registration of a name wins (tree and overflow alike); new names appended");

	// Full: an overflow of 4 takes 4 of the 10 and refuses the rest; the game's loop carries on.
	overflow.reset(4);
	reset_system();
	register_table(first);
	check(overflow.count() == 4 && overflow.refused() == 6 && lookup(first[323].name) == &first[323] && lookup(first[324].name) == nullptr && field(count_offset) == 320,
	      "raised but full: names past the overflow are refused (counted), the rest still resolve");

	// Fewer than 320: the game's own insert, the overflow untouched.
	overflow.reset(704);
	reset_system();
	inserts = 0;
	const auto small = invented("fnC", 40);
	register_table(small);
	check(inserts == 40 && field(count_offset) == 40 && overflow.count() == 0 && lookup(small[39].name) == &small[39], "below the cap: the game's insert, exactly as without the raise");

	check(esp_errors == 0 && register_errors == 0, "every call: ESP balanced and EBX/ESI/EDI kept (stubs keep the game's thiscall ABI)");
	std::printf("Script functions: %d checks, %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
