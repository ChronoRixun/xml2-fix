// [Engine] ModelPools (model_pools_rules.hpp, model_pools_sites.hpp): the setting, the model aggregate's layout
// rebuilt field for field for the retail capacities (every offset the retail constructors use) and for x4, every
// row of the site table against its own layout, the write plan (identity at the retail layout, no overlaps, the
// imm8 caves) and, given a retail XMen2.exe, every byte the plan relies on.
//
// usage: xml2_model_pools_test [XMen2.exe]

#include "model_pools_rules.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
	using namespace model_pools_rules;
	int failures = 0;
	int checks = 0;

#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

	struct expected_pool
	{
		kind k;
		DWORD offset, bitmap_a, ring, ring_write, ring_read, ring_free, bitmap_b, live, ids, id_mask, id_shift, size;
	};

	// The retail constructors' own numbers (0x574b40, 0x574bf0/0x5749f0, 0x574c60, 0x574d10, 0x574dc0,
	// 0x574e60/0x574a60, 0x574940, 0x574ed0) and the aggregate constructor's offsets (0x5754c0).
	constexpr expected_pool retail[] = {
		{kind::transform, 0x0, 0x2070, 0x20c8, 0x2b9c, 0x2ba0, 0x2ba4, 0x2ba8, 0x2c00, 0x2c04, 0x36d4, 0x36d8, 0x36dc},
		{kind::attributes, 0x36dc, 0x2cb0, 0x2cd4, 0x30e8, 0x30ec, 0x30f0, 0x30f4, 0x3118, 0x311c, 0x352c, 0x3530, 0x3534},
		{kind::shader_fx, 0x6c10, 0xd80, 0xdb8, 0x147c, 0x1480, 0x1484, 0x1488, 0x14c0, 0x14c4, 0x1b84, 0x1b88, 0x1b8c},
		{kind::igb, 0x879c, 0x5780, 0x57b4, 0x5df8, 0x5dfc, 0x5e00, 0x5e04, 0x5e38, 0x5e3c, 0x647c, 0x6480, 0x6484},
		{kind::actor, 0xec20, 0x1700, 0x1704, 0x1788, 0x178c, 0x1790, 0x1794, 0x1798, 0x179c, 0x181c, 0x1820, 0x1824},
		{kind::procedural_geometry, 0x10444, 0xdc0, 0xddc, 0x1150, 0x1154, 0x1158, 0x115c, 0x1178, 0x117c, 0x14ec, 0x14f0, 0x14f4},
		{kind::particle_cloud, 0x11938, 0xe60, 0xe68, 0xf0c, 0xf10, 0xf14, 0xf18, 0xf20, 0xf24, 0xfc4, 0xfc8, 0xfcc},
		{kind::model, 0x12938, 0x76f0, 0x7748, 0x821c, 0x8220, 0x8224, 0x8228, 0x8280, 0x8284, 0x8d54, 0x8d58, 0x8d5c},
	};

	void check_pool(const aggregate_layout& a, const expected_pool& e)
	{
		const auto& p = a.pool[index(e.k)];
		CHECK(a.offset[index(e.k)] == e.offset);
		CHECK(p.bitmap_a == e.bitmap_a);
		CHECK(p.ring == e.ring);
		CHECK(p.ring_write == e.ring_write);
		CHECK(p.ring_read == e.ring_read);
		CHECK(p.ring_free == e.ring_free);
		CHECK(p.bitmap_b == e.bitmap_b);
		CHECK(p.live == e.live);
		CHECK(p.ids == e.ids);
		CHECK(p.id_mask == e.id_mask);
		CHECK(p.id_shift == e.id_shift);
		CHECK(p.size == e.size);
	}

	void check_setting()
	{
		std::printf("setting\n");
		CHECK(!parse_setting(std::nullopt).raise && parse_setting(std::nullopt).valid);
		CHECK(parse_setting(std::string("x4")).raise == x4_capacities);
		CHECK(parse_setting(std::string(" X4 ; comment")).raise == x4_capacities);
		CHECK(parse_setting(std::string("x2")).raise == x2_capacities);
		for (const char* off : {"off", "x1", "0", "stock", "no", "false", ""})
		{
			const auto s = parse_setting(std::string(off));
			CHECK(!s.raise && s.valid);
		}
		for (const char* bad : {"x8", "4096", "yes", "x3"})
		{
			const auto s = parse_setting(std::string(bad));
			CHECK(!s.raise && !s.valid);
		}
		CHECK(capacities_valid(x4_capacities) && capacities_valid(x2_capacities) && capacities_valid(retail_capacities));
		// [Engine] ModelHandles (wave 1, E1): handles and transforms together, from the profile's count to 16384, only with a raise.
		{
			std::string note;
			const auto h8 = with_handles(parse_setting(std::string("x4")), std::string("8192 ; wave 1"), &note);
			CHECK(note.empty() && h8.raise && (*h8.raise)[index(kind::model)] == 8192 && (*h8.raise)[index(kind::transform)] == 8192 && capacities_valid(*h8.raise));
			auto rest = *h8.raise;
			rest[index(kind::model)] = rest[index(kind::transform)] = 4096;
			CHECK(rest == x4_capacities); // nothing else moves
			const auto rule = agreement_for(h8, *h8.raise);
			CHECK(rule.requested && rule.verified && rule.values == (std::array<std::uint32_t, 4>{1, 8192, 2048, 128})); // an x4 peer differs in rule 28's values
			CHECK(with_handles(parse_setting(std::string("x4")), std::nullopt).raise == x4_capacities);
			CHECK(with_handles(parse_setting(std::string("x4")), std::string("")).raise == x4_capacities);
			for (const char* bad : {"4095", "16385", "lots", "0x2000", "-1"})
			{
				note.clear();
				CHECK(with_handles(parse_setting(std::string("x4")), std::string(bad), &note).raise == x4_capacities && note.find("stay at 4096") != std::string::npos);
			}
			note.clear();
			CHECK(!with_handles(parse_setting(std::string("off")), std::string("8192"), &note).raise && note.find("needs ModelPools") != std::string::npos);
			CHECK((*with_handles(parse_setting(std::string("x2")), std::string("16384")).raise)[index(kind::model)] == 16384);
		}
		// Rule 28 (ModelPools): absent/off registers nothing; an installed raise its capacities; a failed or invalid
		// request is unverified.
		for (const char* off : {"off", "0", "stock", ""})
			CHECK(!agreement_for(parse_setting(std::string(off)), std::nullopt).requested);
		CHECK(!agreement_for(parse_setting(std::nullopt), std::nullopt).requested);
		{
			const auto x4 = agreement_for(parse_setting(std::string("x4")), x4_capacities);
			CHECK(x4.requested && x4.verified && x4.values == (std::array<std::uint32_t, 4>{1, 4096, 2048, 128}));
			const auto x2 = agreement_for(parse_setting(std::string("x2")), x2_capacities);
			CHECK(x2.requested && x2.verified && x2.values == (std::array<std::uint32_t, 4>{1, 2048, 1024, 64}));
			const auto failed = agreement_for(parse_setting(std::string("x4")), std::nullopt);
			CHECK(failed.requested && !failed.verified && failed.values == (std::array<std::uint32_t, 4>{}));
			const auto bad = agreement_for(parse_setting(std::string("x8")), std::nullopt);
			CHECK(bad.requested && !bad.verified);
			CHECK(agreement_revision == 1);
		}
		auto c = x4_capacities;
		c[index(kind::model)] = 2048; // the model and transform pools share findNext (0x56fcc0)
		CHECK(!capacities_valid(c));
		c = x4_capacities;
		c[index(kind::actor)] = 16; // never below the game's own
		CHECK(!capacities_valid(c));
		c = x4_capacities;
		c[index(kind::particle_cloud)] = 256; // the systems' records would start past the 8-bit displacement at 0x579c04
		CHECK(!capacities_valid(c));
		c[index(kind::particle_cloud)] = 129; // odd: the records would not start on 8 bytes
		CHECK(!capacities_valid(c));
	}

	void check_layouts()
	{
		std::printf("layouts\n");
		const auto r = aggregate_layout_for(retail_capacities);
		for (const auto& e : retail) check_pool(r, e);
		CHECK(r.offset[index(kind::movie)] == 0x12904);
		CHECK(r.size == retail_aggregate_size);
		CHECK(retail_aggregate + r.size == init_guard);
		// The live counts SPEC-runtime-limits 2.4 names (aggregate-relative, the can-allocate compares at 0x575730..).
		CHECK(pool_start(r, kind::transform) + r.pool[index(kind::transform)].live == 0x2c00);
		CHECK(pool_start(r, kind::attributes) + r.pool[index(kind::attributes)].live == 0x67f4);
		CHECK(pool_start(r, kind::shader_fx) + r.pool[index(kind::shader_fx)].live == 0x80d0);
		CHECK(pool_start(r, kind::igb) + r.pool[index(kind::igb)].live == 0xe5d4);
		CHECK(pool_start(r, kind::actor) + r.pool[index(kind::actor)].live == 0x103b8);
		CHECK(pool_start(r, kind::procedural_geometry) + r.pool[index(kind::procedural_geometry)].live == 0x115bc);
		CHECK(pool_start(r, kind::particle_cloud) + r.pool[index(kind::particle_cloud)].live == 0x12858);
		CHECK(pool_start(r, kind::model) + r.pool[index(kind::model)].live == 0x1abbc);
		CHECK(retail_aggregate + pool_start(r, kind::model) == 0x7d200c);                                    // 0x575cf5
		CHECK(retail_aggregate + pool_start(r, kind::model) + r.pool[index(kind::model)].bitmap_a == 0x7d96fc); // 0x575d04

		// x4, by hand: transforms 4096 x 12 bytes, 128-dword bitmaps.
		const auto x = aggregate_layout_for(x4_capacities);
		const auto& t = x.pool[index(kind::transform)];
		CHECK(t.bitmap_a == 0xc000 && t.ring == 0xc200 && t.ring_end == 0x10200 && t.ring_write == 0x10204 && t.ring_read == 0x10208 && t.ring_free == 0x1020c);
		CHECK(t.bitmap_b == 0x10210 && t.live == 0x10410 && t.ids == 0x10414 && t.id_mask == 0x14414 && t.id_shift == 0x14418 && t.size == 0x1441c);
		const auto& ac = x.pool[index(kind::actor)];
		CHECK(ac.words == 4 && ac.bitmap_a == 128 * 184 && ac.live == ac.bitmap_b + 16 && ac.size == ac.id_shift + 4);
		const auto& pc = x.pool[index(kind::particle_cloud)];
		CHECK(pc.words == 4 && pc.bitmap_a == 128 * 92 && pc.capacity == 128);
		// the particle-system manager (0x7dad78): retail, field for field (constructor 0x578b50), and x4 (one system per cloud)
		const auto ps = psys_layout_for(40);
		CHECK(ps.count == 0xa4 && ps.records == 0xa8 && ps.bitmap == 0x468 && ps.objects == 0x470 && ps.size == 0x1af0 && ps.words == 2);
		CHECK(retail_particle_manager + ps.size == particle_manager_guard);
		CHECK(r.psys.capacity == 40 && x.psys.capacity == 128);
		CHECK(x.psys.count == 0x204 && x.psys.records == 0x208 && x.psys.records / 8 <= 127 && x.psys.bitmap == 0x208 + 128 * 24 && x.psys.objects == x.psys.bitmap + 16);
		CHECK(psys_layout_for(max_particle_systems).records / 8 <= 127 && psys_layout_for(max_particle_systems + 2).records / 8 > 127);
		// back to back, the movie slot unchanged, the model manager's vtable in front of its pool
		DWORD at = 0;
		for (std::size_t k = 0; k < kinds; ++k)
		{
			CHECK(x.offset[k] == at);
			at += k == index(kind::movie) ? movie_size : x.pool[k].size + (k == index(kind::model) ? 4 : 0);
		}
		CHECK(x.size == at && x.size < 0x100000);
		for (std::size_t k = 0; k < kinds; ++k) CHECK(x.capacity[k] == x4_capacities[k]);
		std::printf("  retail 0x%lx bytes, x4 0x%lx bytes, x2 0x%lx bytes\n", r.size, x.size, aggregate_layout_for(x2_capacities).size);
	}

	void check_sites()
	{
		std::printf("sites (%zu rows, %zu caves)\n", model_pools_sites::sites.size(), model_pools_sites::caves.size());
		const auto r = aggregate_layout_for(retail_capacities);
		std::set<std::pair<DWORD, int>> seen;
		std::map<DWORD, std::string_view> hex_of;
		int per_kind[kinds]{};
		for (const auto& s : model_pools_sites::sites)
		{
			CHECK(limits_rules::valid_hex(s.hex));
			CHECK(s.size == 1 || s.size == 4);
			CHECK(static_cast<std::size_t>(s.offset) + s.size <= limits_rules::hex_size(s.hex));
			CHECK(seen.insert({s.va, s.offset}).second);
			if (const auto it = hex_of.find(s.va); it != hex_of.end()) CHECK(it->second == s.hex);
			hex_of[s.va] = s.hex;
			CHECK(operand(s.hex, s.offset, s.size) == s.retail);
			const auto v = value_of(s, r, blocks{});
			CHECK(v && *v == s.retail);
			if (!v || *v != s.retail) std::printf("  row 0x%06lx: retail 0x%lx, layout says 0x%lx\n", static_cast<unsigned long>(s.va), static_cast<unsigned long>(s.retail), static_cast<unsigned long>(v ? *v : 0));
			++per_kind[index(s.pool)];
		}
		for (std::size_t k = 0; k < kinds; ++k) std::printf("  %s: %d rows\n", kind_name[k], per_kind[k]);
		CHECK(model_pools_sites::caves.size() == 36);
		for (const auto& c : model_pools_sites::caves)
		{
			CHECK(c.count >= 1 && static_cast<std::size_t>(c.first) + c.count <= model_pools_sites::span_instructions.size());
			CHECK(model_pools_sites::span_instructions[c.first].va == c.va);
			DWORD at = c.va;
			for (std::size_t i = c.first; i < static_cast<std::size_t>(c.first) + c.count; ++i)
			{
				const auto& in = model_pools_sites::span_instructions[i];
				CHECK(in.va == at && limits_rules::valid_hex(in.hex));
				at += static_cast<DWORD>(limits_rules::hex_size(in.hex));
			}
			CHECK(at == c.resume && c.resume - c.va >= 5);
		}
	}

	struct image
	{
		std::vector<std::uint8_t> mem; // mapped at image_base
		struct section { char name[9]{}; DWORD va = 0, size = 0; };
		std::vector<section> secs;
		bool load(const char* path)
		{
			std::ifstream f(path, std::ios::binary);
			std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(f)), {});
			if (file.size() < 0x400) return false;
			const auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, &file[o], 4); return v; };
			const auto u16 = [&](std::size_t o) { std::uint16_t v; std::memcpy(&v, &file[o], 2); return v; };
			const auto pe = u32(0x3c);
			if (u32(pe) != 0x4550 || u32(pe + 0x34) != image_base) return false;
			const auto sections = u16(pe + 6), optional = u16(pe + 0x14);
			mem.assign(u32(pe + 0x50), 0);
			std::memcpy(mem.data(), file.data(), u32(pe + 0x54));
			for (std::size_t i = 0; i < sections; ++i)
			{
				const auto h = pe + 0x18 + optional + 40 * i;
				const auto va = u32(h + 12), raw = u32(h + 20), size = std::min(u32(h + 16), u32(h + 8) ? u32(h + 8) : u32(h + 16));
				if (raw + size > file.size() || va + size > mem.size()) return false;
				std::memcpy(&mem[va], &file[raw], size);
				section sec;
				std::memcpy(sec.name, &file[h], 8);
				sec.va = image_base + va;
				sec.size = size;
				secs.push_back(sec);
			}
			return true;
		}
		std::uint8_t* at(const DWORD va) { return &mem[va - image_base]; }
	};

	void check_plan(const char* exe)
	{
		std::printf("plan\n");
		// At the retail layout and address every in-place write is the retail bytes (the rows round-trip).
		const auto r = aggregate_layout_for(retail_capacities);
		const auto same = plan_for(r, blocks{}, 0x10000000);
		CHECK(same.has_value());
		if (same)
			for (const auto& w : same->writes)
				if (w.what == std::string("model pools field")) CHECK(w.bytes == w.before);

		const DWORD block = 0x12340000, cave_base = 0x23450000, systems = 0x34560000;
		const auto x = aggregate_layout_for(x4_capacities);
		const auto p = plan_for(x, blocks{block, systems}, cave_base);
		CHECK(p.has_value());
		if (!p) return;
		std::printf("  x4: %zu writes, %zu fields, %zu caves, %zu cave bytes\n", p->writes.size(), p->fields, p->caves, p->cave_code.size());
		CHECK(p->caves == 42 && p->cave_code.size() < 0x1000); // 36 imm8, 5 bitmap clears, 1 findNext copy
		// no two writes touch the same byte; each write's own size equals its retail bytes
		std::vector<std::pair<DWORD, DWORD>> ranges;
		for (const auto& w : p->writes)
		{
			CHECK(w.bytes.size() == w.before.size() && !w.bytes.empty());
			ranges.push_back({w.va, w.va + static_cast<DWORD>(w.bytes.size())});
		}
		std::sort(ranges.begin(), ranges.end());
		for (std::size_t i = 1; i < ranges.size(); ++i) CHECK(ranges[i - 1].second <= ranges[i].first);
		// the getter now builds and returns the block
		for (const auto& w : p->writes)
		{
			if (w.va == 0x575637) CHECK(w.bytes == std::vector<std::uint8_t>({0xb9, 0x00, 0x00, 0x34, 0x12}));
			if (w.va == 0x575654) CHECK(w.bytes == std::vector<std::uint8_t>({0xb8, 0x00, 0x00, 0x34, 0x12}));
			if (w.va == 0x67e1a0) CHECK(w.bytes == std::vector<std::uint8_t>({0xb9, 0x00, 0x00, 0x34, 0x12}));
			if (w.va == 0x575cf5)
			{
				std::uint32_t v;
				std::memcpy(&v, &w.bytes[1], 4);
				CHECK(w.bytes[0] == 0xb9 && v == block + pool_start(x, kind::model));
			}
			if (w.va == 0x578cea) CHECK(w.bytes == std::vector<std::uint8_t>({0xb9, 0x00, 0x00, 0x56, 0x34}));
			if (w.va == 0x579c04) CHECK(w.bytes.size() == 4 && w.bytes[3] == x.psys.records / 8);
			if (w.va == 0x578c29) CHECK(w.bytes == std::vector<std::uint8_t>({0xbb, 0x28, 0, 0, 0, 0x90, 0x90}));
			if (w.va == 0x575730) // cmp dword ptr [eax + transforms.live], 4096
			{
				std::uint32_t d, n;
				std::memcpy(&d, &w.bytes[2], 4);
				std::memcpy(&n, &w.bytes[6], 4);
				CHECK(d == x.pool[index(kind::transform)].live && n == 4096);
			}
		}
		// The first cave: the actor destructor's `cmp ecx, 0x20; jge 0x571691` -> cmp ecx, 128; jge 0x571691; jmp 0x5715c5
		const auto& c0 = p->cave_code;
		const std::uint8_t head[] = {0x81, 0xf9, 0x80, 0x00, 0x00, 0x00, 0x0f, 0x8d};
		CHECK(std::memcmp(c0.data(), head, sizeof(head)) == 0);
		std::int32_t rel;
		std::memcpy(&rel, &c0[8], 4);
		CHECK(cave_base + 12 + rel == 0x571691);
		CHECK(c0[12] == 0xe9);
		std::memcpy(&rel, &c0[13], 4);
		CHECK(cave_base + 17 + rel == 0x5715c5);
		for (const auto& w : p->writes)
			if (w.va == 0x5715bc)
			{
				std::memcpy(&rel, &w.bytes[1], 4);
				CHECK(w.bytes[0] == 0xe9 && 0x5715bc + 5 + rel == cave_base && w.bytes.size() == 9 && w.bytes[5] == 0x90);
			}

		if (!exe) return;
		image img;
		CHECK(img.load(exe));
		if (img.mem.empty()) return;
		std::printf("  against %s\n", exe);
		int mismatches = 0;
		for (const auto& s : model_pools_sites::sites)
			if (!limits_rules::matches(img.at(s.va), s.hex)) ++mismatches;
		for (const auto& in : model_pools_sites::span_instructions)
			if (!limits_rules::matches(img.at(in.va), in.hex)) ++mismatches;
		for (const auto& w : p->writes)
			if (std::memcmp(img.at(w.va), w.before.data(), w.before.size()) != 0) ++mismatches;
		CHECK(mismatches == 0);
		// Applied to the image: no byte of 0x7bf6d0 is left as an operand anywhere in the patched instructions.
		for (const auto& w : p->writes) std::memcpy(img.at(w.va), w.bytes.data(), w.bytes.size());
		int stale = 0;
		for (const auto& w : p->writes)
			for (std::size_t i = 0; i + 4 <= w.bytes.size(); ++i)
			{
				std::uint32_t v;
				std::memcpy(&v, img.at(w.va) + i, 4);
				if ((v >= retail_aggregate && v < init_guard) || (v >= retail_particle_manager && v < particle_manager_guard)) ++stale;
			}
		CHECK(stale == 0);
	}
}

namespace
{
	std::int32_t rel32(const std::uint8_t* p)
	{
		std::int32_t v;
		std::memcpy(&v, p, 4);
		return v;
	}

	// The x2 and x4 plans against the retail exe: every write's and every read-only expectation's bytes, every branch
	// of every cave (the moved one decoded from the exe, the generated one from the cave code, the entry jump and the
	// jump back), the five bitmap clears' sizes, the findNext copy, and once applied: no reference to the old aggregate
	// or particle-system manager left in any initialised byte of .text, .rdata or .data.
	void check_exe_plan(const image& retail_image, const capacities& caps, const char* name)
	{
		std::printf("plan %s against the exe\n", name);
		const DWORD block = 0x12340000, cave_base = 0x23450000, systems = 0x34560000;
		const auto a = aggregate_layout_for(caps);
		const auto p = plan_for(a, blocks{block, systems}, cave_base);
		CHECK(p.has_value());
		if (!p) return;
		image img = retail_image;
		std::printf("  %zu writes, %zu fields, %zu caves, %zu cave bytes, %zu read-only expectations\n", p->writes.size(), p->fields, p->caves, p->cave_code.size(), p->expects.size());
		CHECK(p->caves == 42 && p->cave_code.size() < 0x1000);
		std::vector<std::pair<DWORD, DWORD>> ranges;
		for (const auto& w : p->writes)
		{
			CHECK(w.bytes.size() == w.before.size() && std::memcmp(img.at(w.va), w.before.data(), w.before.size()) == 0);
			ranges.push_back({w.va, w.va + static_cast<DWORD>(w.bytes.size())});
		}
		std::sort(ranges.begin(), ranges.end());
		for (std::size_t i = 1; i < ranges.size(); ++i) CHECK(ranges[i - 1].second <= ranges[i].first);
		CHECK(p->expects.size() == helper_clones.size());
		for (const auto& e : p->expects) CHECK(e.bytes == e.before && std::memcmp(img.at(e.va), e.before.data(), e.before.size()) == 0);
		const auto& code = p->cave_code;
		const auto cave_at = [&](const DWORD va) -> const std::uint8_t* { return va >= cave_base && va < cave_base + code.size() ? &code[va - cave_base] : nullptr; };
		const auto write_at = [&](const DWORD va) -> const write* {
			for (const auto& w : p->writes)
				if (w.va == va) return &w;
			return nullptr;
		};
		// The imm8 caves.
		int branches = 0;
		for (const auto& c : model_pools_sites::caves)
		{
			const auto* w = write_at(c.va);
			CHECK(w && w->bytes.size() == c.resume - c.va && w->bytes[0] == 0xe9);
			if (!w) continue;
			for (std::size_t i = 5; i < w->bytes.size(); ++i) CHECK(w->bytes[i] == 0x90);
			DWORD at = c.va + 5 + rel32(&w->bytes[1]);
			for (std::size_t i = c.first; i < static_cast<std::size_t>(c.first) + c.count; ++i)
			{
				const auto& in = model_pools_sites::span_instructions[i];
				const auto original = bytes_of(in.hex);
				const auto* x = cave_at(at);
				CHECK(x != nullptr);
				if (!x) break;
				using model_pools_sites::op;
				switch (in.how)
				{
				case op::branch:
				{
					// the exe's own branch (rel8 7x or rel32 0f 8x): its target and condition are the table's
					DWORD target = 0;
					std::uint8_t cc = 0xff;
					if (original.size() == 2 && (original[0] & 0xf0) == 0x70)
					{
						target = in.va + 2 + static_cast<std::int8_t>(original[1]);
						cc = original[0] & 0x0f;
					}
					else if (original.size() == 6 && original[0] == 0x0f && (original[1] & 0xf0) == 0x80)
					{
						target = in.va + 6 + rel32(&original[2]);
						cc = original[1] & 0x0f;
					}
					CHECK(std::memcmp(img.at(in.va), original.data(), original.size()) == 0);
					CHECK(target == in.target && cc == in.condition);
					CHECK(!(target > c.va && target < c.resume)); // never back into the moved span
					CHECK(x[0] == 0x0f && x[1] == (0x80 | in.condition) && at + 6 + rel32(x + 2) == in.target);
					at += 6;
					++branches;
					break;
				}
				case op::copy:
					CHECK(x[0] == original[0]);
					at += static_cast<DWORD>(original.size());
					break;
				case op::widen_cmp:
					CHECK(x[0] == 0x81 && original[0] == 0x83 && x[1] == original[1]);
					at += static_cast<DWORD>(original.size()) + 3;
					break;
				case op::widen_push:
					CHECK(x[0] == 0x68 && original[0] == 0x6a);
					at += 5;
					break;
				}
			}
			const auto* back = cave_at(at);
			CHECK(back && back[0] == 0xe9 && at + 5 + rel32(back + 1) == c.resume);
		}
		std::printf("  %d cave branches aimed at their exe targets\n", branches);
		CHECK(branches > 0);
		// The bitmap clears: the whole bitmap, then back.
		for (const auto& c : bitmap_clears)
		{
			const auto* w = write_at(c.va);
			CHECK(w && w->bytes[0] == 0xe9 && w->bytes.size() == c.resume - c.va && w->before == bytes_of(c.hex));
			if (!w) continue;
			const DWORD entry = c.va + 5 + rel32(&w->bytes[1]);
			const auto* x = cave_at(entry);
			CHECK(x != nullptr);
			if (!x) continue;
			if (c.zero_edx)
			{
				CHECK(x[0] == 0x33 && x[1] == 0xd2);
				x += 2;
			}
			const std::uint8_t head[] = {0x50, 0x51, 0x57, 0x33, 0xc0, 0xb9};
			const std::uint8_t tail[] = {0xf3, 0xab, 0x5f, 0x59, 0x58, 0xe9};
			CHECK(std::memcmp(x, head, sizeof head) == 0 && std::memcmp(x + 10, tail, sizeof tail) == 0);
			std::uint32_t words;
			std::memcpy(&words, x + 6, 4);
			const DWORD want = c.particle_systems ? a.psys.words : a.pool[index(c.pool)].words;
			const int bits = c.particle_systems ? a.psys.capacity : a.pool[index(c.pool)].capacity;
			CHECK(words == want && 32 * words >= static_cast<DWORD>(bits));
			const DWORD jmp_at = entry + (c.zero_edx ? 2 : 0) + 15;
			CHECK(jmp_at + 5 + rel32(x + 16) == c.resume);
		}
		// The findNext copy: the helper's own bytes with the pool's capacity in its five numbers, the one call moved.
		for (const auto& h : helper_clones)
		{
			const auto* w = write_at(h.call);
			CHECK(w && w->bytes[0] == 0xe8 && w->before == bytes_of(h.call_hex));
			CHECK(static_cast<DWORD>(h.call + 5 + rel32(img.at(h.call) + 1)) == h.helper); // the exe calls the shared helper there
			CHECK(static_cast<DWORD>(0x4053c4 + 5 + rel32(img.at(0x4053c4) + 1)) == h.helper); // and the FX-template destructor keeps it
			if (!w) continue;
			const DWORD entry = h.call + 5 + rel32(&w->bytes[1]);
			const auto* x = cave_at(entry);
			const auto helper = bytes_of(h.hex);
			CHECK(x && entry % 16 == 0 && entry + helper.size() <= cave_base + code.size());
			if (!x) continue;
			CHECK(std::memcmp(img.at(h.helper), helper.data(), helper.size()) == 0);
			CHECK(helper.back() == 0x00 && helper[helper.size() - 3] == 0xc2); // through its last ret 8
			std::vector<std::uint8_t> want = helper;
			for (const auto o : h.capacity_at) put32(want, o, static_cast<std::uint32_t>(a.pool[index(h.pool)].capacity));
			CHECK(std::memcmp(x, want.data(), want.size()) == 0);
			int retail_numbers = 0; // the five are every 220 in the helper
			for (std::size_t i = 0; i + 4 <= helper.size(); ++i)
				if (operand(h.hex, i, 4) == static_cast<std::uint32_t>(retail_capacities[index(h.pool)])) ++retail_numbers;
			CHECK(retail_numbers == static_cast<int>(h.capacity_at.size()));
		}
		// Applied, over every initialised byte (unaligned) of .text, .rdata and .data: the plan leaves no old aggregate or
		// particle-system manager address in any byte it writes, adds none anywhere, and removes exactly the 11 references
		// (8 aggregate, 3 manager). Every raw match left is a retail byte run that is not an operand
		// (scripts/check_model_pools_text.py decodes the whole .text: 11 operand references, all rows). The stationary
		// init guards 0x7dad68 and 0x7dc868 sit just past each range and stay. Aligned dwords of .rdata/.data (pointers)
		// match only inside the import-name strings at 0x6d28d0.
		const auto scan = [](image& m)
		{
			std::set<DWORD> found;
			std::size_t positions = 0, pointers = 0;
			for (const auto& sec : m.secs)
			{
				const std::string n(sec.name);
				if (n != ".text" && n != ".rdata" && n != ".data") continue;
				for (DWORD va = sec.va; va + 4 <= sec.va + sec.size; ++va)
				{
					std::uint32_t v;
					std::memcpy(&v, m.at(va), 4);
					++positions;
					if ((v >= retail_aggregate && v < init_guard) || (v >= retail_particle_manager && v < particle_manager_guard))
					{
						found.insert(va);
						if (n != ".text" && va % 4 == 0 && va != 0x6d28d0) ++pointers;
					}
				}
			}
			return std::make_tuple(found, positions, pointers);
		};
		image retail_copy = retail_image;
		const auto [before, positions, retail_pointers] = scan(retail_copy);
		for (const auto& w : p->writes) std::memcpy(img.at(w.va), w.bytes.data(), w.bytes.size());
		const auto [after, positions_after, pointers] = scan(img);
		const auto in_write = [&](const DWORD va)
		{
			for (const auto& w : p->writes)
				if (va + 4 > w.va && va < w.va + w.bytes.size()) return true;
			return false;
		};
		int added = 0, left_in_writes = 0, removed = 0, removed_outside = 0;
		for (const auto va : after)
		{
			if (!before.count(va)) ++added;
			if (in_write(va))
			{
				++left_in_writes;
				std::printf("  old address left in a write at 0x%08lx\n", static_cast<unsigned long>(va));
			}
		}
		for (const auto va : before)
			if (!after.count(va))
			{
				++removed;
				if (!in_write(va)) ++removed_outside;
			}
		std::printf("  raw scan over %zu positions: retail %zu matches, patched %zu (%d removed, %d added, %d left in writes)\n", positions, before.size(), after.size(), removed, added, left_in_writes);
		CHECK(positions > 2500000 && positions == positions_after);
		CHECK(added == 0 && left_in_writes == 0 && removed == 11 && removed_outside == 0 && retail_pointers == 0 && pointers == 0);
	}
}

int main(int argc, char** argv)
{
	check_setting();
	check_layouts();
	check_sites();
	check_plan(argc > 1 ? argv[1] : nullptr);
	if (argc > 1)
	{
		image img;
		CHECK(img.load(argv[1]));
		if (!img.mem.empty())
		{
			check_exe_plan(img, x2_capacities, "x2");
			check_exe_plan(img, x4_capacities, "x4");
			check_exe_plan(img, *with_handles(parse_setting(std::string("x4")), std::string("8192")).raise, "x4 + ModelHandles 8192");
			check_exe_plan(img, *with_handles(parse_setting(std::string("x4")), std::string("16384")).raise, "x4 + ModelHandles 16384");
		}
	}
	std::printf("model pools: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
