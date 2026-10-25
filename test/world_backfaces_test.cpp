// [Game] WorldBackfaces=xml1 (rule 30, docs/world-backfaces.md): world_backfaces::install against the retail trace
// helper. The test image owns 0x401000..0x801000 (image_fixture.asm, /BASE:0x400000): the guards are seeded from the
// retail executable given on the command line, so the helper at 0x4a9650 that runs below is XMen2.exe's own code,
// driving a fake collider that records what the helper asks of it.
#include <Windows.h>
#include "gameplay_capabilities.hpp"
#include "ini.hpp"
#include "world_backfaces.hpp"
#include "world_backfaces_rules.hpp"
#include "world_backfaces_state.hpp"
#include "xml1_hook_transaction.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

extern "C" unsigned char fixture_space[];
namespace
{
	std::optional<std::string> ini_value;           // [Game] WorldBackfaces
	std::map<std::wstring, std::string> zone_keys;  // [WorldBackfaces] ZoneCount / Z1..Zn
	int refused = 0, failures = 0, checks = 0;
	void check(bool condition, const char* name)
	{
		++checks;
		if (!condition) { ++failures; std::printf("FAIL %s\n", name); }
	}
	std::vector<unsigned char> decode_hex(std::string_view s)
	{
		std::vector<unsigned char> out;
		for (std::size_t i = 0; i < s.size(); i += 2) { unsigned n = 0; std::sscanf(s.data() + i, "%2x", &n); out.push_back(static_cast<unsigned char>(n)); }
		return out;
	}
	unsigned char* at(std::uint32_t address) { return reinterpret_cast<unsigned char*>(address); }
	std::vector<std::vector<unsigned char>> retail; // each guard's bytes as read from the executable
	void seed()
	{
		for (std::size_t i = 0; i < world_backfaces_rules::guards.size(); ++i)
			std::memcpy(at(world_backfaces_rules::guards[i].va), retail[i].data(), retail[i].size());
		// The helper's entry (0x4a9650, mov eax,[esp+4]; sub esp,0x10), which no guard reads (EntityCapacity may own it).
		const unsigned char entry[] = {0x8b, 0x44, 0x24, 0x04, 0x83, 0xec, 0x10};
		std::memcpy(at(0x4a9650), entry, sizeof entry);
		// The helper's only direct callee, 0x4654d0 (is this handle live?): always yes here.
		const unsigned char live[] = {0xb0, 0x01, 0xc3};
		std::memcpy(at(0x4654d0), live, sizeof live);
	}
	bool untouched() { return !world_backfaces_rules::first_mismatch(at(0x400000)); }

	bool read_retail(const char* path)
	{
		std::ifstream f(path, std::ios::binary);
		std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)), {});
		if (raw.size() < 0x1000) { check(false, "readable retail executable"); return false; }
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
		if (dos->e_lfanew < 0 || std::size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > raw.size()) { check(false, "PE header bounds"); return false; }
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
		    std::size_t(dos->e_lfanew) + 24 + nt->FileHeader.SizeOfOptionalHeader + std::size_t(nt->FileHeader.NumberOfSections) * 40 > raw.size())
		{
			check(false, "PE section bounds"); return false;
		}
		bool all = true;
		for (const auto& g : world_backfaces_rules::guards)
		{
			const auto bytes = decode_hex(g.hex);
			bool match = false;
			const auto* sections = IMAGE_FIRST_SECTION(nt);
			for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
			{
				const auto& s = sections[i];
				const auto rva = g.va - 0x400000;
				if (rva < s.VirtualAddress || std::uint64_t(rva - s.VirtualAddress) + bytes.size() > s.SizeOfRawData) continue;
				const auto off = std::uint64_t(s.PointerToRawData) + rva - s.VirtualAddress;
				match = off + bytes.size() <= raw.size() && std::memcmp(raw.data() + off, bytes.data(), bytes.size()) == 0;
				if (match) retail.emplace_back(raw.begin() + static_cast<std::ptrdiff_t>(off), raw.begin() + static_cast<std::ptrdiff_t>(off + bytes.size()));
			}
			check(match, g.what);
			all = all && match;
		}
		return all;
	}

	// A collider whose virtual slots sit where the retail vtable 0x68d6a4 has them (+0x20 result, +0x30 exclude,
	// +0x40 group filter, +0x78 sweep, +0x88 back-face flag); MSVC lays the slots out in declaration order.
	struct collider
	{
		int excluded_mode = -1, group_mode = -1, group = -1, back_flag = -1, sweeps = 0, order = 0, back_order = 0, sweep_order = 0;
		unsigned excluded_handle = 0, hit = 0;
		float end[3]{}, start[3]{};
		virtual void s00() {} virtual void s04() {} virtual void s08() {} virtual void s0c() {}
		virtual void s10() {} virtual void s14() {} virtual void s18() {} virtual void s1c() {}
		virtual unsigned* result(unsigned* out) { *out = hit; return out; }                       // +0x20
		virtual void s24() {} virtual void s28() {} virtual void s2c() {}
		virtual void exclude(int mode, unsigned* handle) { excluded_mode = mode; excluded_handle = *handle; } // +0x30
		virtual void s34() {} virtual void s38() {} virtual void s3c() {}
		virtual void filter(int mode, int which) { group_mode = mode; group = which; }           // +0x40
		virtual void s44() {} virtual void s48() {} virtual void s4c() {} virtual void s50() {} virtual void s54() {}
		virtual void s58() {} virtual void s5c() {} virtual void s60() {} virtual void s64() {} virtual void s68() {}
		virtual void s6c() {} virtual void s70() {} virtual void s74() {}
		virtual void sweep(const float* from, const float* to, unsigned*)                          // +0x78
		{
			++sweeps; sweep_order = ++order;
			std::memcpy(start, from, sizeof start); std::memcpy(end, to, sizeof end);
		}
		virtual void s7c() {} virtual void s80() {} virtual void s84() {}
		virtual void back_faces(int flag) { back_flag = flag & 0xff; back_order = ++order; }     // +0x88
	};
	using helper_fn = bool(__cdecl*)(unsigned handle, unsigned other, const float* start, const float* delta, unsigned* hit, collider** holder);

	struct run_result { bool returned; unsigned hit; collider c; };
	run_result run_helper(unsigned hit)
	{
		collider c; c.hit = hit;
		collider* holder = &c;
		const float start[3]{334.0f, 84.0f, -118.99f}, delta[3]{0.5f, 0.75f, 0.0f};
		unsigned out = 0xdeadbeef;
		const auto helper = reinterpret_cast<helper_fn>(static_cast<std::uintptr_t>(0x4a9650));
		const bool returned = helper(0x1234, 0, start, delta, &out, &holder);
		return {returned, out, c};
	}
	void check_sweep_unchanged(const run_result& r, const char* what)
	{
		const bool shape = r.c.excluded_mode == 1 && r.c.excluded_handle == 0x1234 && r.c.group_mode == 1 && r.c.group == 1 &&
		                   r.c.sweeps == 1 && r.c.back_order == 1 && r.c.sweep_order == 2 &&
		                   r.c.start[0] == 334.0f && r.c.start[1] == 84.0f && r.c.start[2] == -118.99f &&
		                   r.c.end[0] == 334.5f && r.c.end[1] == 84.75f && r.c.end[2] == -118.99f;
		check(shape, what);
	}
}
namespace ini
{
	std::optional<std::string> text(const wchar_t* section, const wchar_t* key)
	{
		if (std::wstring(section) == L"Game") return ini_value;
		const auto it = zone_keys.find(key);
		return it == zone_keys.end() ? std::nullopt : std::optional<std::string>(it->second);
	}
	int number(const wchar_t* section, const wchar_t* key, int fallback)
	{
		const auto v = text(section, key);
		return v ? std::atoi(v->c_str()) : fallback;
	}
}
namespace
{
	// A zone manager at its static (0x72a578, inside the fixture image) naming `zone`; vtable 0 = not built yet.
	void zone_manager(const char* zone, std::uint32_t vtable = world_backfaces_rules::zones_vtable)
	{
		std::memcpy(at(world_backfaces_rules::zones_static), &vtable, 4);
		char name[world_backfaces_rules::zone_name_size]{};
		std::strncpy(name, zone, sizeof name);
		std::memcpy(at(world_backfaces_rules::zones_static + world_backfaces_rules::zone_name_at), name, sizeof name);
	}
	bool bridged() { return *at(world_backfaces_rules::flag_site) == 0xe9 && *at(world_backfaces_rules::flag_site + 5) == 0x90; }
	bool site_retail() { return std::memcmp(at(world_backfaces_rules::flag_site), world_backfaces_rules::flag_site_retail.data(), 6) == 0; }
}
namespace logger { void write(const char*, ...) {} }
namespace game_version { void refuse_startup() { ++refused; } }

int main(int argc, char** argv)
{
	using namespace world_backfaces_rules;
	if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != 0x400000 || reinterpret_cast<std::uintptr_t>(fixture_space) > 0x401000) return 2;

	// The leaf testers' face filter, as 0x5fd88c..0x5fd8df runs it (no executable needed).
	const float nan = std::numeric_limits<float>::quiet_NaN();
	check(face_tested(-0.7f, true, false) && face_tested(0.0f, true, false) && !face_tested(0.77f, true, false),
	      "XML1's (front 1, back 0): front and edge-on faces tested, a face seen from behind skipped");
	check(face_tested(-0.7f, true, true) && face_tested(0.0f, true, true) && face_tested(0.77f, true, true),
	      "XMen2.exe's movement (front 1, back 1): every face tested");
	check(!face_tested(-0.7f, false, true) && !face_tested(0.0f, false, true) && face_tested(0.77f, false, true) && !face_tested(0.77f, false, false),
	      "front clear: only faces seen from behind");
	check(face_tested(nan, true, false) && face_tested(nan, false, false), "an unordered dot product passes both tests, as the FPU compare lets it");
	// The haarp2_1 520 pocket: the y = 90 wall strip (x 356..360) faces +y; the officer runs on heading 0.883 rad.
	const float heading = 0.883f, d_strip = 1.0f * std::sin(heading), d_front_wall = -0.997f * std::cos(heading);
	check(!face_tested(d_strip, true, false) && face_tested(d_strip, true, true), "the pocket's back-facing wall stops only the two-sided sweep");
	check(face_tested(d_front_wall, true, false) && face_tested(d_front_wall, true, true), "the front-facing wall at x ~ 365.9 stops both");

	// Agreement: rule 30 is requested-only; OFF is the released record.
	{
		using namespace gameplay_capabilities;
		check(world_backfaces_id == 30 && std::string(rule_name(world_backfaces_id)) == "WorldBackfaces" && agreement_revision == 2, "rule 30 WorldBackfaces, revision 2 (scoped)");
		record released, off, on, failed;
		released.add(2, 1, {0, 0, 0, 0}, true); off.add(2, 1, {0, 0, 0, 0}, true);
		on.add(2, 1, {0, 0, 0, 0}, true); on.add(world_backfaces_id, agreement_revision, {1, 0, 0, 0}, true);
		failed.add(world_backfaces_id, agreement_revision, {0, 0, 0, 0}, false);
		check(released.finalize() && off.finalize() && on.finalize() && !failed.finalize(), "records finalize; a failed request poisons");
		check(off.compare(released.bytes()) == result::match, "OFF (no entry) admits the released record");
		difference diff;
		check(on.compare(off.bytes(), &diff) == result::entries, "xml1 and OFF refuse each other");
		char names[128]{};
		on.describe(diff.buckets, diff.local_only, true, names, sizeof names);
		check(std::string(names) == "30 WorldBackfaces (not on the other PC)", "the refusal names 30 WorldBackfaces");
		off.describe(diff.buckets, 0, true, names, sizeof names);
		check(std::string(names) == "30 WorldBackfaces (disabled on this PC)", "the OFF PC names the rule as disabled");
		// Revision 2: the zone list is the rule's detail; another list, no list or the unscoped revision 1 refuse.
		scope a, b, c;
		check(make_scope({"haarp/haarp2_1", "nyc/nyc1_1"}, a) && make_scope({"NYC\\nyc1_1", "haarp/haarp2_1"}, c) && make_scope({"haarp/haarp2_1"}, b),
		      "zone lists normalize (case, backslashes) and sort");
		check(digest(a) == digest(c) && digest(a) != digest(b), "the list digest covers every zone, not their order or spelling");
		const auto da = digest(a), db = digest(b);
		record ra, ra2, rb, rall, rev1;
		ra.add(world_backfaces_id, agreement_revision, {1, 1, 2, 0}, true, &da);
		ra2.add(world_backfaces_id, agreement_revision, {1, 1, 2, 0}, true, &da);
		rb.add(world_backfaces_id, agreement_revision, {1, 1, 1, 0}, true, &db);
		rall.add(world_backfaces_id, agreement_revision, {1, 0, 0, 0}, true);
		rev1.add(world_backfaces_id, 1, {1, 0, 0, 0}, true);
		for (auto* r : {&ra, &ra2, &rb, &rall, &rev1}) r->finalize();
		check(ra.compare(ra2.bytes()) == result::match, "the same zone list agrees");
		check(ra.compare(rb.bytes()) == result::value && ra.compare(rall.bytes()) == result::value && rall.compare(rev1.bytes()) == result::value,
		      "another list, no list, or the unscoped revision 1 candidate refuse");
		ra.compare(rb.bytes(), &diff);
		ra.describe(diff.buckets, diff.local_only, true, names, sizeof names);
		check(std::string(names) == "30 WorldBackfaces", "a list difference names 30 WorldBackfaces");
		// Zone names.
		scope s;
		for (const auto& bad : std::vector<std::vector<std::string>>{{}, {""}, {"a//b"}, {"/a"}, {"a/"}, {"../x"}, {"a b"}, {"a", "A"},
		                                                              {std::string(64, 'a')}})
			check(!make_scope(bad, s), "empty, malformed, oversized or duplicate zone lists are refused");
		check(make_scope({std::string(63, 'a')}, s), "a 63-character zone path fits the zone manager's 0x40 bytes");
		check(make_scope({"haarp/haarp2_1"}, s) && flag_for(s, "haarp/haarp2_1", 0x40) == 0 && flag_for(s, "HAARP\\haarp2_1", 0x40) == 0 &&
		      flag_for(s, "act0/tutorial/tutorial1", 0x40) == 1 && flag_for(s, "", 0x40) == 1 && flag_for(s, "haarp/haarp2_10", 0x40) == 1,
		      "listed zones get XML1's 0, every other zone (and no zone) XMen2.exe's 1");
		const char unterminated[0x40]{'h', 'a', 'a', 'r', 'p', '/', 'h', 'a', 'a', 'r', 'p', '2', '_', '1'};
		check(flag_for(s, unterminated, sizeof unterminated) == 0, "the name is read within the zone manager's 0x40 bytes");
	}

	if (argc < 2) { std::puts("SKIPPED retail executable argument (the installer and helper checks need XMen2.exe)"); return failures ? 1 : 0; }
	DWORD old = 0;
	if (!VirtualProtect(at(0x401000), 0x400000, PAGE_EXECUTE_READWRITE, &old)) return 2;
	if (!read_retail(argv[1])) { std::printf("%d of %d checks failed\n", failures, checks); return 1; }

	// The game's own: no key, empty, xml2, off.
	for (const char* word : {static_cast<const char*>(nullptr), "", "xml2", "off"})
	{
		if (word) ini_value = word; else ini_value.reset();
		seed(); world_backfaces::install(GetModuleHandleW(nullptr));
		check(!world_backfaces::requested() && !world_backfaces::installed() && world_backfaces::verified() && untouched() && *at(back_flag_operand) == retail_back_flag,
		      "no key / empty / xml2 / off: nothing patched, no agreement entry");
	}
	{
		seed();
		const auto r = run_helper(0x77);
		check(r.returned && r.hit == 0x77, "retail helper: a hit is returned");
		check_sweep_unchanged(r, "retail helper: exclusion, group filter, back-face call, then the sweep from start to start + delta");
		check(r.c.back_flag == 1, "retail helper: XMen2.exe asks for back faces (vt+0x88(1)) before every movement sweep");
		const auto miss = run_helper(0);
		check(!miss.returned && miss.hit == 0, "retail helper: no hit returns false");
	}

	// The rule needs its zone list; the refusal cases below carry a valid one, so each refuses for its own reason.
	const std::map<std::wstring, std::string> one_zone{{L"ZoneCount", "1"}, {L"Z1", "haarp/haarp2_1"}};
	zone_keys = one_zone;
	ini_value = "bogus"; seed(); world_backfaces::install(GetModuleHandleW(nullptr));
	check(world_backfaces::requested() && !world_backfaces::installed() && !world_backfaces::verified() && untouched(), "an unknown word changes no code and stays unverified");
	ini_value = "xml1"; seed(); world_backfaces::install(reinterpret_cast<HMODULE>(0x500000));
	check(world_backfaces::requested() && !world_backfaces::installed() && !world_backfaces::verified() && untouched(), "a relocated executable is refused");
	for (std::size_t i = 0; i < guards.size(); ++i)
	{
		seed(); *at(guards[i].va + 1) ^= 0x20;
		world_backfaces::install(GetModuleHandleW(nullptr));
		check(!world_backfaces::installed() && !world_backfaces::verified() && site_retail(), "each guard mismatch prevents the write");
	}

	// [Limits] EntityCapacity bridges the helper's first 7 bytes (0x4a9650 -> its DLL owner) before this rule installs:
	// no guard may read them, and the owner takes the flag from world_backfaces_state.
	for (const auto& g : guards)
		check(g.va >= 0x4a9657 || g.va + limits_rules::hex_size(g.hex) <= 0x4a9650, "no guard reads the entity-capacity bridge at 0x4a9650");
	check(world_backfaces_state::current() == 1 && !world_backfaces_state::source, "refused installs leave the entity-capacity owner's flag at 1");
	zone_manager("haarp/haarp2_1");
	seed(); *at(0x4a9650) = 0xe9;
	world_backfaces::install(GetModuleHandleW(nullptr));
	check(world_backfaces::installed() && bridged() && world_backfaces_state::current() == 0,
	      "with the helper's entry bridged (EntityCapacity) xml1 installs and the DLL owner passes 0 in a listed zone");
	world_backfaces_state::source = nullptr;

	seed();
	const std::vector<unsigned char> before(at(0x401000), at(0x801000));
	world_backfaces::install(GetModuleHandleW(nullptr));
	check(world_backfaces::requested() && world_backfaces::installed() && world_backfaces::verified() && bridged() &&
	      world_backfaces::values() == std::array<std::uint32_t, 4>{1, 1, 1, 0} && world_backfaces::detail(),
	      "xml1 with a one-zone list: installed and verified (rule 30 {1,1,1,0} and the list's digest)");
	bool footprint = true;
	for (std::size_t i = 0; i < before.size(); ++i)
		if (at(0x401000)[i] != before[i] && (0x401000 + i < flag_site || 0x401000 + i >= flag_site + 6)) footprint = false;
	check(footprint, "xml1 changes exactly the six bytes of 0x4a96c0 (push 1; fstp -> jmp bridge; nop)");
	{
		const auto r = run_helper(0x77);
		check(r.returned && r.hit == 0x77, "patched helper: a hit is still returned");
		check_sweep_unchanged(r, "patched helper: the same calls in the same order, the same sweep");
		check(r.c.back_flag == 0, "patched helper: in the listed zone the back-face flag is cleared before every movement sweep (XML1's one-sided level)");
	}
	world_backfaces::install(GetModuleHandleW(nullptr));
	check(!world_backfaces::installed() && bridged(), "a second install refuses the already bridged helper");
	world_backfaces_state::source = nullptr;

	// Revision 2: only the listed zones (owner decision 2026-10-25: X-Men Legends 1 levels only).
	zone_keys = {{L"ZoneCount", "2"}, {L"Z1", "haarp/haarp2_1"}, {L"Z2", "NYC\\nyc1_1"}};
	seed(); zone_manager("act0/tutorial/tutorial1");
	world_backfaces::install(GetModuleHandleW(nullptr));
	{
		scope want; make_scope({"haarp/haarp2_1", "nyc/nyc1_1"}, want);
		const auto d = digest(want);
		check(world_backfaces::installed() && bridged() && world_backfaces::values() == std::array<std::uint32_t, 4>{1, 1, 2, 0} &&
		      world_backfaces::detail() && *world_backfaces::detail() == d, "a zone list installs as rule 30 {1,1,2,0} with the list's digest");
		const auto x2 = run_helper(0x77);
		check(x2.returned && x2.c.back_flag == 1, "an X-Men Legends II zone keeps XMen2.exe's two-sided sweep (vt+0x88(1))");
		check_sweep_unchanged(x2, "bridged helper (XML2 zone): the same calls in the same order, the same sweep");
		zone_manager("haarp/haarp2_1");
		const auto x1 = run_helper(0x77);
		check(x1.returned && x1.hit == 0x77 && x1.c.back_flag == 0, "a listed XML1 zone gets the one-sided sweep (vt+0x88(0))");
		check_sweep_unchanged(x1, "bridged helper (XML1 zone): the same calls in the same order, the same sweep");
		check(world_backfaces_state::current() == 0, "the entity-capacity owner asks the same zone flag");
		zone_manager("nyc/nyc1_1");
		check(run_helper(0).c.back_flag == 0 && world_backfaces_state::current() == 0, "the second listed zone (written with a backslash)");
		zone_manager("nyc/nyc1_10");
		check(run_helper(0).c.back_flag == 1, "a zone whose name only starts like a listed one is not listed");
		zone_manager("", 0);
		check(run_helper(0).c.back_flag == 1 && world_backfaces_state::current() == 1, "no zone manager yet: XMen2.exe's own");
		zone_manager("haarp/haarp2_1");
		check(run_helper(0).c.back_flag == 0, "back in a listed zone");
	}
	world_backfaces_state::source = nullptr;
	// The DLL itself enforces "listed zones only" (review round 2): no list, or any count that is not 1..512 in decimal
	// (-1, a sign, a space, a leading zero, junk, 0, 513, revision 1's unscoped form), refuses the rule.
	for (const auto& keys : std::vector<std::map<std::wstring, std::string>>{
	         {}, {{L"Z1", "haarp/haarp2_1"}}, {{L"Count", "1"}, {L"Z1", "haarp/haarp2_1"}},
	         {{L"ZoneCount", "-1"}, {L"Z1", "haarp/haarp2_1"}}, {{L"ZoneCount", "+1"}, {L"Z1", "haarp/haarp2_1"}},
	         {{L"ZoneCount", " 1"}, {L"Z1", "haarp/haarp2_1"}}, {{L"ZoneCount", "01"}, {L"Z1", "haarp/haarp2_1"}},
	         {{L"ZoneCount", "1x"}, {L"Z1", "haarp/haarp2_1"}}, {{L"ZoneCount", ""}, {L"Z1", "haarp/haarp2_1"}},
	         {{L"ZoneCount", "0"}}, {{L"ZoneCount", "2"}, {L"Z1", "a"}}, {{L"ZoneCount", "2"}, {L"Z1", "a"}, {L"Z2", "a"}},
	         {{L"ZoneCount", "1"}, {L"Z1", "a//b"}}, {{L"ZoneCount", "513"}}})
	{
		zone_keys = keys; seed();
		world_backfaces::install(GetModuleHandleW(nullptr));
		check(world_backfaces::requested() && !world_backfaces::installed() && !world_backfaces::verified() && site_retail() &&
		      !world_backfaces_state::source && world_backfaces::values()[0] == 0,
		      "no list, an invalid count, or an empty, short, duplicate, malformed or oversized list patches nothing and stays unverified");
	}
	check(parse_zone_count("1") == 1u && parse_zone_count("512") == 512u && !parse_zone_count("513") && !parse_zone_count("-1") &&
	      !parse_zone_count("0") && !parse_zone_count("1 ") && !parse_zone_count("0x10"), "ZoneCount is 1..512 in plain decimal");
	zone_keys.clear();
	check(refused == 0, "normal refusals do not terminate startup");

	std::printf("%s: %d of %d checks failed\n", failures ? "FAIL" : "PASS", failures, checks);
	return failures ? 1 : 0;
}
