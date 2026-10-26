// [Game] LiftRule=xml1 (rule 38, docs/lift-rule.md): the lift decision for every might rank and XML1 heaviness
// against X-Men Legends' own table (default.xbe 0x38443..0x3846e), and every guard against the retail executable
// named by XML2_FIX_TEST_EXE (or the first argument; skipped without one). Nothing of the game runs.
#include <Windows.h>
#include "gameplay_capabilities.hpp"
#include "lift_rule_rules.hpp"
#include "lift_rule.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" unsigned char fixture_space[];
namespace ini {
	std::optional<std::string> lift_value;
	std::optional<std::string> text(const wchar_t*, const wchar_t*) { return lift_value; }
	bool flag(const wchar_t*, const wchar_t*, bool) { return false; }
}
namespace logger { void write(const char*, ...) {} }
namespace game_version { void refuse_startup() { std::abort(); } }

namespace
{
	int failures = 0, checks = 0;
	void check(bool condition, const char* name)
	{
		++checks;
		if (!condition) { ++failures; std::printf("FAIL %s\n", name); }
	}
	using namespace lift_rule_rules;
	address parse_site = parse_hook_va, impact_site = impact_hook_va, tk_site = tk_hook_va, carry_site = carry_hook_va;
	__declspec(naked) int run_parse(void*, void*, int)
	{
		__asm {
			push esi
			push edi
			mov esi, [esp+12]
			mov edi, [esp+16]
			mov eax, [esp+20]
			sub esp, 20
			mov [esp+16], eax
			call dword ptr [parse_site]
			add esp, 20
			pop edi
			pop esi
			ret
		}
	}
	__declspec(naked) int run_impact(void*)
	{
		__asm {
			push esi
			mov esi, [esp+8]
			call dword ptr [impact_site]
			movzx eax, al
			pop esi
			ret
		}
	}
	__declspec(naked) int run_tk(void*)
	{
		__asm {
			push esi
			mov esi, [esp+8]
			call dword ptr [tk_site]
			mov eax, edx
			pop esi
			ret
		}
	}
	__declspec(naked) int run_carry(void*)
	{
		__asm {
			mov eax, [esp+4]
			sub esp, 20
			mov [esp+12], eax
			call dword ptr [carry_site]
			add esp, 20
			ret
		}
	}
	struct reader { void** vt; int h; };
	void __fastcall read_number(reader* r, void*, const char*, std::int16_t* out) { *out = static_cast<std::int16_t>(r->h); }
	unsigned char fixture_might=0;
	unsigned char __fastcall might(void*,void*) { return fixture_might; }
	bool __cdecl ordinary_mode() { return false; }
	template<class T> void jump(address va,T target) {
		auto* p=reinterpret_cast<unsigned char*>(va);p[0]=0xe9;
		const auto relative=std::uint32_t(reinterpret_cast<std::uintptr_t>(target))-va-5;
		std::memcpy(p+1,&relative,4);
	}

	// X-Men Legends 1, written out: rows might rank 0..4, columns heaviness 0..5 (5 is never lifted; rank r lifts up to
	// r + 1). Rank 4 is what an npcstat `might level="4"` gives.
	constexpr bool xml1_table[5][6] = {
		{true, true, false, false, false, false},
		{true, true, true, false, false, false},
		{true, true, true, true, false, false},
		{true, true, true, true, true, false},
		{true, true, true, true, true, false},
	};

	void rules()
	{
		check(parse("") == setting::native && parse("xml2") == setting::native && parse(" OFF ") == setting::native, "no key, xml2, off: the game's own");
		check(parse("xml1") == setting::xml1 && parse(" XML1 ") == setting::xml1, "xml1 asks for the rule");
		check(parse("1") == setting::invalid && parse("on") == setting::invalid, "anything else is refused (closes host/join)");

		check(engine_heaviness(0) == 0 && engine_heaviness(1) == 0 && engine_heaviness(2) == 1 && engine_heaviness(3) == 2 &&
		      engine_heaviness(4) == 3 && engine_heaviness(5) == 3, "the builder's byte for each XML1 heaviness (x1schema)");

		for (int m = 0; m < 5; ++m)
			for (int h = 0; h <= 5; ++h)
			{
				char name[96];
				std::snprintf(name, sizeof name, "XML1 formula = table, might %d heaviness %d", m, h);
				check(xml1_liftable(m, h) == xml1_table[m][h], name);
				const auto v = decide(h, engine_heaviness(h), m);
				std::snprintf(name, sizeof name, "rule on, object with xml1heaviness: might %d heaviness %d as XML1", m, h);
				check(v == (xml1_table[m][h] ? verdict::lift : verdict::refuse), name);
				// Today's game rule on the same byte: identical except Might 3+ at XML1 heaviness 4.
				const bool game = game_liftable(m, engine_heaviness(h));
				std::snprintf(name, sizeof name, "game rule differs only at might >= 3, heaviness 4 (might %d heaviness %d)", m, h);
				check((game != xml1_table[m][h]) == (m >= 3 && h == 4), name);
			}
		check(decide(std::nullopt, 3, 3) == verdict::game && decide(std::nullopt, 0, 0) == verdict::game, "no xml1heaviness: the game's own test");
		check(decide(4, 2, 3) == verdict::game, "a byte a script changed (not what the builder wrote): the game's own test");
		check(decide(9, 3, 3) == verdict::game && decide(-1, 3, 3) == verdict::game, "an out-of-range attribute: the game's own test");
		check(decide(5, 3, 15) == verdict::refuse, "XML1 heaviness 5 is never lifted, whatever the might");

		heaviness_store s;
		s.set(0x1234, 4);
		check(s.get(0x1234) == 4 && !s.get(0x1234 + heaviness_store::slots), "kept by handle; a colliding handle does not inherit it");
		s.set(0x1234, std::nullopt);
		check(!s.get(0x1234), "a reused handle without the attribute forgets it");
		check(!s.get(0), "handle 0 is never known");

		check(gameplay_capabilities::lift_rule_id == 38 && std::string(gameplay_capabilities::rule_name(gameplay_capabilities::lift_rule_id)) == "LiftRule"
		          && agreement_revision == 2, "rule 38 LiftRule, revision 2");

		// XML1's results written out, not derived from the helpers under test. Damage is the corresponding
		// 0xe07ef signed damagelevel / 3, independent of h; ranks are synthetic inputs, not a Might bonus.
		constexpr int impacts[]{2,4,6,8,10,12};
		constexpr int levels[]{0,2,5,9,15}, damages[]{0,0,1,3,5}, limits[]{0,2,3,4,4};
		for (int rank = 0; rank < 5; ++rank)
			for (int h = 0; h <= 5; ++h)
			{
				const int resolved = consumer_heaviness(h, engine_heaviness(h));
				check(impact_level(resolved) == impacts[h], "each class x rank: thrown impact matches XML1");
				check(carried_damage(levels[rank]) == damages[rank], "each class x rank: carried throw damage matches XML1 (no heaviness factor)");
				check(telekinesis_allows(resolved, limits[rank]) == (h <= limits[rank]), "each class x rank: TK uses XML1 limit");
				if (h >= 2 && h <= 4)
					check(consumer_heaviness(std::nullopt, engine_heaviness(h)) == h, "legacy classes 2-4 use inverse scale");
			}
		check(consumer_heaviness(std::nullopt, 0) == 0 && consumer_heaviness(std::nullopt, 3) == 4,
		      "legacy merged classes have documented inverse (0 and 4); rebuild for exact 1/5");
		check(consumer_heaviness(4, 2) == 2 && consumer_heaviness(9, 3) == 3, "stale or invalid metadata keeps native consumer byte");
		check(carried_damage(-128) == -42 && carried_damage(-5) == -1 && carried_damage(127) == 42, "XML1 signed byte damagelevel and truncation toward zero");
		check(!telekinesis_allows(0,-1) && telekinesis_allows(5,5), "TK preserves signed limit, including limits outside Phoenix's ranks");
	}

	void retail(const char* path)
	{
		std::ifstream f(path, std::ios::binary);
		std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)), {});
		if (raw.size() < 0x1000) { check(false, "readable retail executable"); return; }
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
		check(nt->OptionalHeader.ImageBase == limits_rules::image_base, "retail image base 0x400000");
		const auto* sections = IMAGE_FIRST_SECTION(nt);
		auto bytes = [&](std::uint32_t va) -> const unsigned char* {
			for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
			{
				const auto& sec = sections[i];
				const std::uint32_t start = limits_rules::image_base + sec.VirtualAddress;
				if (va >= start && va < start + sec.SizeOfRawData) return raw.data() + sec.PointerToRawData + (va - start);
			}
			return nullptr;
		};
		for (const auto& g : guards)
		{
			const auto* b = bytes(g.va);
			const bool ok = b && limits_rules::matches(b, g.hex);
			if (!ok) std::printf("  guard 0x%08lX %s\n", static_cast<unsigned long>(g.va), g.what);
			check(ok, "guard matches the retail executable");
		}
		const auto* p = bytes(parse_hook_va);
		check(p && std::memcmp(p, parse_displaced.data(), parse_displaced.size()) == 0, "parse bridge displaces exactly the heaviness clamp");
		const auto* q = bytes(gate_hook_va);
		check(q && std::memcmp(q, gate_displaced.data(), gate_displaced.size()) == 0, "gate bridge displaces exactly cmp heaviness,3 / jae");
		// The displaced jae lands on the refusal and the compare continues at the might read.
		check(q && q[8] == 0x22 && gate_hook_va + 9 + 0x22 == gate_refuse_va && gate_hook_va + 9 == gate_native_va, "displaced branch targets");
		const auto displaced = [&](address va, const auto& expected, address resume) {
			const auto* b = bytes(va);
			check(b && std::memcmp(b, expected.data(), expected.size()) == 0 && va + expected.size() == resume,
			      "revision 2 displaced bytes and resume match retail");
		};
		displaced(impact_hook_va, impact_displaced, impact_resume_va);
		displaced(carry_hook_va, carry_displaced, carry_resume_va);
		displaced(tk_hook_va, tk_displaced, tk_resume_va);
		// The heaviness string the parser asks for.
		const auto* name = bytes(0x689dac);
		check(name && std::strcmp(reinterpret_cast<const char*>(name), "heaviness") == 0, "0x689dac is \"heaviness\"");
		const std::string blob(raw.begin(), raw.end());
		check(blob.find(std::string(heaviness_attribute) + '\0') == std::string::npos, "XMen2.exe never reads xml1heaviness");

		// A private test image, never a game process. Seed only the guarded retail bytes.
		check(reinterpret_cast<std::uintptr_t>(fixture_space) == 0x401000, "fixture owns game addresses");
		if (reinterpret_cast<std::uintptr_t>(fixture_space) != 0x401000) return;
		DWORD old = 0;
		check(VirtualProtect(fixture_space, 0x400000, PAGE_EXECUTE_READWRITE, &old) != 0, "fixture writable");
		for (const auto& g : guards) std::memcpy(reinterpret_cast<void*>(g.va), bytes(g.va), g.hex.size()/2);
		// Flying carry calls this same normal lift service. Retain its native
		// refusal prefix/tail around the installed XML1 heaviness bridge.
		std::memcpy(reinterpret_cast<void*>(0x427f60),bytes(0x427f60),0x75);
		const std::vector<unsigned char> before(fixture_space, fixture_space + 0x400000);
		ini::lift_value.reset();
		lift_rule::install(reinterpret_cast<HMODULE>(0x400000));
		check(!lift_rule::requested() && std::memcmp(before.data(),fixture_space,before.size())==0, "absent key preserves every fixture byte");
		for (const auto value : {"", "off", "xml2"}) {
			ini::lift_value = value;
			lift_rule::install(reinterpret_cast<HMODULE>(0x400000));
			check(!lift_rule::requested() && std::memcmp(before.data(),fixture_space,before.size())==0, "OFF install preserves every fixture byte");
		}
		ini::lift_value = "on";
		lift_rule::install(reinterpret_cast<HMODULE>(0x400000));
		check(lift_rule::requested() && !lift_rule::verified() && std::memcmp(before.data(),fixture_space,before.size())==0,
		      "invalid setting refuses without a write");
		ini::lift_value = "xml1";
		for (const auto& g : guards) {
			auto* site = reinterpret_cast<unsigned char*>(g.va);
			*site ^= 1;
			const std::vector<unsigned char> changed(fixture_space, fixture_space + 0x400000);
			lift_rule::install(reinterpret_cast<HMODULE>(0x400000));
			check(!lift_rule::installed() && !lift_rule::verified() && std::memcmp(changed.data(),fixture_space,changed.size())==0,
			      "each foreign guard refuses the entire install without a write");
			*site ^= 1;
		}
		lift_rule::install(reinterpret_cast<HMODULE>(0x400000));
		check(lift_rule::installed() && lift_rule::verified(), "all five bridges install together");
		if (!lift_rule::installed()) return;
		for (const auto va : {parse_hook_va,gate_hook_va,impact_hook_va,carry_hook_va,tk_hook_va})
			check(*reinterpret_cast<unsigned char*>(va)==0xe9, "installed bridge jumps at its retail site");
		// Stop at each continuation. The wrappers execute our emitted bridge with the real site's stack/register
		// contract; none of the game's functions run. The reader is synthetic, with its number getter at vt+0x18.
		for (const auto va : {parse_resume_va,impact_resume_va,carry_resume_va,tk_resume_va})
			*reinterpret_cast<unsigned char*>(va) = 0xc3;
		void* vtable[7]{}; vtable[6] = reinterpret_cast<void*>(&read_number);
		reader r{vtable,0};
		std::array<unsigned char,0x400> object{};
		const std::uint32_t handle = 0x1234;
		std::memcpy(object.data()+entity_handle,&handle,4);
		jump(might_getter_va,&might);jump(0x4c87a0,&ordinary_mode);
		std::array<unsigned char,0x600> actor{};
		const auto may_lift=reinterpret_cast<unsigned char(__fastcall*)(void*,void*,void*)>(0x427f60);
		for(int h=0;h<=5;++h) {
			r.h=h;object[object_heaviness]=static_cast<unsigned char>(engine_heaviness(h));
			run_parse(object.data(),&r,object[object_heaviness]);
			for(int m=0;m<5;++m)for(int carried=0;carried<2;++carried) {
				fixture_might=static_cast<unsigned char>(m);object[0x30d]=carried?4:0;
				check(bool(may_lift(actor.data(),nullptr,object.data()))==(!carried&&xml1_table[m][h]),
				      "carry object service: native refusal and XML1 heaviness 0..5 / Might 0..4 table");
			}
		}
		object[0x30d]=0;
		std::array<unsigned char,0x2c> entry{};
		constexpr int impacts[]{2,4,6,8,10,12}, levels[]{0,2,5,9,15}, damages[]{0,0,1,3,5}, limits[]{0,2,3,4,4};
		for (int h=0;h<=5;++h) {
			r.h=h; object[object_heaviness]=static_cast<unsigned char>(engine_heaviness(h));
			check((run_parse(object.data(),&r,object[object_heaviness])&255)==engine_heaviness(h), "parse bridge replays clamp and keeps metadata");
			for (int rank=0;rank<5;++rank) {
				entry[0xb]=static_cast<unsigned char>(levels[rank]);
				check(run_impact(object.data())==impacts[h], "emitted impact bridge: each class x rank");
				check((run_tk(object.data())<=limits[rank])==(h<=limits[rank]), "emitted TK bridge: each class x rank");
				check(run_carry(entry.data())==damages[rank], "emitted carry bridge: each class x rank");
			}
		}
		// The unchanged companion builder emits attributes only for classes 4/5. Exercise precisely its output.
		for (int h=2;h<=5;++h) {
			r.h=h>=4 ? h : -1; object[object_heaviness]=static_cast<unsigned char>(engine_heaviness(h));
			run_parse(object.data(),&r,object[object_heaviness]);
			for (int rank=0;rank<5;++rank) {
				entry[0xb]=static_cast<unsigned char>(levels[rank]);
				check(run_impact(object.data())==impacts[h] && (run_tk(object.data())<=limits[rank])==(h<=limits[rank]) &&
				      run_carry(entry.data())==damages[rank], "all three emitted bridges: existing builder classes 2-5 x rank");
			}
		}
		entry[0xb]=static_cast<unsigned char>(-5);
		check(run_carry(entry.data())==-1, "emitted carry bridge reads signed DamageLevel and truncates toward zero");
		r.h=-1; object[object_heaviness]=2; run_parse(object.data(),&r,2);
		check(run_impact(object.data())==8 && run_tk(object.data())==3, "emitted consumers use inverse scale without attribute");
		r.h=4; run_parse(object.data(),&r,2);
		check(run_impact(object.data())==6 && run_tk(object.data())==2, "emitted consumers retain native byte after script mutation");
	}
}

int main(int argc, char** argv)
{
	rules();
	const char* exe = argc > 1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");
	if (exe && *exe) retail(exe);
	else std::printf("SKIPPED retail guards (no XML2_FIX_TEST_EXE)\n");
	std::printf("%s lift_rule: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
