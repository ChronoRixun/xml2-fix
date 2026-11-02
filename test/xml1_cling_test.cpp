// [Game] Xml1WallCling=xml1 (rule 39, docs/xml1-cling.md): the wall-cling rules without the game, and every guard
// against the retail executable named by XML2_FIX_TEST_EXE (or the first argument; skipped without one).
#include <Windows.h>
#include "gameplay_capabilities.hpp"
#include "xml1_cling_rules.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	int failures = 0, checks = 0;
	void check(bool condition, const char* name)
	{
		++checks;
		if (!condition) { ++failures; std::printf("FAIL %s\n", name); }
	}
	using namespace xml1_cling_rules;
	bool close_to(float a, float b) { return std::fabs(a - b) < 1e-4f; }

	void rules()
	{
		check(parse("") == setting::native && parse("xml2") == setting::native && parse(" Off ") == setting::native && parse("0") == setting::native,
		      "no key, xml2, off and 0 are the game's own");
		check(parse("xml1") == setting::xml1 && parse("XML1") == setting::xml1 && parse("1") == setting::xml1 && parse(" on") == setting::xml1,
		      "xml1, 1 and on ask for the handlers");
		check(parse("cling") == setting::invalid, "anything else is refused");

		// The wall test: flat normal (|z| < 0.2, strict) facing the actor (facing . n < -0.5, strict).
		const vec east{1, 0, 0};
		check(wall({-1, 0, 0}, east), "a wall straight ahead");
		check(wall({-0.6f, 0.8f, 0}, east), "a wall at 53 degrees (dot -0.6)");
		check(!wall({-0.5f, 0.866f, 0}, east), "dot exactly -0.5 is refused (strict)");
		check(!wall({-0.4f, 0.9165f, 0}, east), "a wall seen at a glancing angle");
		check(!wall({1, 0, 0}, east), "a wall behind");
		check(!wall({-0.97f, 0, 0.2f}, east), "|n.z| exactly 0.2 is refused (strict)");
		check(wall({-0.98f, 0, 0.199f}, east), "|n.z| just below 0.2 clings");
		check(!wall({0, 0, 1}, east), "a floor");
		check(!wall({-0.7f, 0, -0.7f}, east), "an overhang");

		// Facing the wall: yaw = atan2(-n.y, -n.x) in [0, 2 pi).
		check(close_to(yaw_into({-1, 0, 0}), 0.f), "a wall to the east: yaw 0");
		check(close_to(yaw_into({0, -1, 0}), 1.5707963f), "a wall to the north: yaw pi/2");
		check(close_to(yaw_into({1, 0, 0}), 3.1415927f), "a wall to the west: yaw pi");
		check(close_to(yaw_into({0, 1, 0}), 4.712389f), "a wall to the south: yaw 3 pi/2 (never negative)");

		// The probe and the launch.
		const vec d = probe_delta({0.6f, 0.8f, 0});
		check(close_to(d.x, 14.4f) && close_to(d.y, 19.2f) && close_to(d.z, 0), "probe: facing x 24");
		const vec v = backflip_velocity({0.6f, 0.8f, 0});
		check(close_to(v.x, -36.f) && close_to(v.y, -48.f) && close_to(v.z, 500.f), "back-flip: 60 back, 500 up");

		// The decide's order: cling, then the move chain, then landing, else nothing.
		reading r;
		r.acrobatics = 2; r.jump_held = true; r.contact = true; r.wall_kind = true; r.normal = {-1, 0, 0}; r.facing = east;
		check(decide(r) == choice::cling, "rank 2, jump held, a wall: cling");
		r.acrobatics = 1;
		check(decide(r) == choice::none, "rank 1 does not cling and, with no move chain ready, does nothing");
		r.move_ready = true;
		check(decide(r) == choice::move, "rank 1 with the move chain ready: the air jump");
		r.acrobatics = 3; r.wall_kind = false;
		check(decide(r) == choice::move, "an actor or prop in front is not a wall");
		r.wall_kind = true; r.contact = false;
		check(decide(r) == choice::move, "no contact: the air jump");
		r.jump_held = false; r.contact = true;
		check(decide(r) == choice::none, "jump released in the air: nothing (the decide move plays on)");
		r.grounded = true;
		check(decide(r) == choice::land, "landed: special2 (jumpland)");
		r.jump_held = true;
		check(decide(r) == choice::cling, "a wall wins over landing while jump is held");

		check(gameplay_capabilities::wall_cling_id == 39 && std::string(gameplay_capabilities::rule_name(gameplay_capabilities::wall_cling_id)) == "Xml1WallCling"
		          && agreement_revision == 1,
		      "rule 39 Xml1WallCling, revision 1");
	}

	// Every guard against the file's bytes (sections mapped by hand; nothing executes).
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
				const auto& s = sections[i];
				const std::uint32_t start = limits_rules::image_base + s.VirtualAddress;
				if (va >= start && va < start + s.SizeOfRawData) return raw.data() + s.PointerToRawData + (va - start);
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
		const auto* entry = bytes(lookup_va);
		check(entry && std::memcmp(entry, retail_entry.data(), retail_entry.size()) == 0, "the hook replaces exactly push esi; mov esi,[esp+8]");
		// The handler names really are absent from the game's registration (otherwise the hook would shadow them).
		const std::string blob(raw.begin(), raw.end());
		check(blob.find(std::string("ch_clingwalldecide") + '\0') == std::string::npos, "XMen2.exe has no ch_clingwalldecide");
		check(blob.find(std::string("ch_clingwallbackflip") + '\0') == std::string::npos, "XMen2.exe has no ch_clingwallbackflip");
		check(blob.find(std::string("ea_cling_wall_backflip") + '\0') != std::string::npos, "the back-flip animation enum exists");
	}
}

int main(int argc, char** argv)
{
	rules();
	const char* exe = argc > 1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");
	if (exe && *exe) retail(exe);
	else std::printf("SKIPPED retail guards (no XML2_FIX_TEST_EXE)\n");
	std::printf("%s xml1_cling: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
