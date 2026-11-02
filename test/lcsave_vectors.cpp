// lcsave_test (a) and (c): the vectors of legends-classic tests/unit/test_lcsave.py with the same invented build
// ("hero_a", "zone/x/y" - never game text) made in code, and a fuzz of the readers. The tables a conversion needs
// are built here the way the builder's files describe them (catalogue ids by position, canonical keys).

#include "align.hpp"
#include "internal.hpp"
#include "lcsave.hpp"
#include "xmlb.hpp"
#include "../src/save_format_rules.hpp"   // the engine-side stream walkers (W2-L-06)

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
using namespace lcsave;
using namespace lcsave::detail;

int vector_failures = 0;

namespace
{
	void check(bool ok, const std::string& what)
	{
		if (!ok)
		{
			++vector_failures;
			std::printf("FAIL: %s\n", what.c_str());
		}
	}

	template <typename F> std::string error_of(F&& f)
	{
		try
		{
			f();
		}
		catch (const Error& e)
		{
			return e.message;
		}
		return std::string();
	}

	bool contains(const std::string& s, const char* part)
	{
		return s.find(part) != std::string::npos;
	}

	// ------------------------------------------------------------------------------------------------ XMLB writer
	struct E
	{
		std::string tag;
		std::vector<std::pair<std::string, std::string>> attrs;
		std::vector<E> children;
		E& add(E e)
		{
			children.push_back(std::move(e));
			return children.back();
		}
	};

	// xmlb.encode: nodes depth first, then the string table (strings interned in first-use order)
	std::vector<std::uint8_t> encode_xmlb(const E& root)
	{
		std::vector<const E*> order;
		std::function<void(const E&)> collect = [&](const E& e)
		{
			order.push_back(&e);
			for (const auto& c : e.children) collect(c);
		};
		collect(root);
		std::map<const E*, std::uint32_t> offsets;
		std::uint32_t pos = 8;
		for (const auto* e : order)
		{
			offsets[e] = pos;
			pos += 16 + 8 * static_cast<std::uint32_t>(e->attrs.size());
		}
		std::map<std::string, std::uint32_t> strings;
		std::string table;
		auto s = [&](const std::string& text)
		{
			const auto f = strings.find(text);
			if (f != strings.end()) return f->second;
			const auto off = pos + static_cast<std::uint32_t>(table.size());
			strings[text] = off;
			table += text;
			table.push_back('\0');
			return off;
		};
		std::vector<std::uint8_t> out;
		auto u32 = [&](std::uint32_t v) { for (int k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k))); };
		u32(0x11B1);
		u32(1);
		std::function<void(const E&, const E*)> emit = [&](const E& e, const E* sibling)
		{
			const auto name = s(e.tag);
			std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
			for (const auto& [k, v] : e.attrs)
			{
				const auto ks = s(k);
				pairs.emplace_back(ks, s(v));
			}
			u32(name);
			u32(sibling ? offsets[sibling] : 0xFFFFFFFF);
			u32(e.children.empty() ? 0xFFFFFFFF : offsets[&e.children[0]]);
			u32(static_cast<std::uint32_t>(pairs.size()));
			for (const auto& [k, v] : pairs)
			{
				u32(k);
				u32(v);
			}
			for (std::size_t i = 0; i < e.children.size(); ++i) emit(e.children[i], i + 1 < e.children.size() ? &e.children[i + 1] : nullptr);
		};
		emit(root, nullptr);
		out.insert(out.end(), table.begin(), table.end());
		return out;
	}

	using Zone = std::vector<std::pair<std::string, std::vector<std::string>>>;
	const Zone default_zone = {{"gate", {"gate01", "gate02"}}, {"crate", {"crate01"}}, {"crate_pinned", {"crate02"}}, {"spawner_a", {"sp01"}},
		{"spawner_b", {"sp02"}}, {"lamp", {"lamp01"}}, {"spot", {"spot01"}}};

	E zone_file(const Zone& zone)
	{
		E w{"world", {}, {}};
		const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> defs = {
			{"gate", {{"classname", "gameent"}}}, {"crate", {{"classname", "physent"}}},
			{"crate_pinned", {{"classname", "physent"}, {"smartent", "false"}}}, {"spawner_a", {{"classname", "monsterspawnerent"}}},
			{"spawner_b", {{"classname", "monsterspawnerent"}, {"monster_smartent", "true"}, {"instantspawn", "true"}}},
			{"lamp", {{"classname", "lightent"}}}, {"spot", {{"classname", "ent"}}}};
		for (const auto& [name, attrs] : defs)
		{
			E d{"entity", {{"name", name}}, {}};
			for (const auto& a : attrs) d.attrs.push_back(a);
			w.add(std::move(d));
		}
		for (const auto& [t, insts] : zone)
		{
			E ei{"entinst", {{"type", t}}, {}};
			for (const auto& n : insts) ei.add(E{"inst", {{"name", n}}, {}});
			w.add(std::move(ei));
		}
		return w;
	}

	// ------------------------------------------------------------------------------------------------ synthetic build
	const std::vector<std::string> heroes = {"hero_a", "hero_b"};
	const std::vector<std::string> npcs = {"npc_a", "npc_b", "npc_c"};
	const std::vector<std::string> shared_default = {"sh_fight", "sh_might", "sh_boss", "sh_acro", "sh_tough"};
	const std::map<std::string, std::vector<std::string>> hero_tal = {{"hero_a", {"a_one", "a_two", "a_three"}}, {"hero_b", {"b_one", "b_two"}}};
	const char* const slot_names[9] = {"", "astonishing", "aoa", "60s", "70s", "weaponx", "future", "winter", "civilian"};

	struct Cat
	{
		Catalogue c;
		std::map<unsigned, u32> next;
		u32 add(unsigned dom, const std::vector<std::string_view>& fields)
		{
			const auto key = canonical_key(fields, Enc::latin1);
			const auto h = sha256(reinterpret_cast<const std::uint8_t*>(key.data()), key.size());
			if (const u32 v = c.lookup(dom, h)) return v;
			CatEntry e;
			e.domain = dom;
			e.id = make_id(dom, ++next[dom]);
			e.key = key;
			c.entries.push_back(e);
			c.index();
			return e.id;
		}
	};

	// the tables of one synthetic release (what legacy.lcm carries for it, and what the bind files give for a build)
	EraTables tables_of(Cat& cat, int cv, const std::vector<std::string>& shared, const std::string& extra_talent)
	{
		EraTables t;
		t.cv = cv;
		t.release = cv == 13 ? "0.1.10" : cv == 5 ? "0.1.3" : std::to_string(cv);
		t.STAT.push_back(0);
		for (const auto& h : heroes) t.STAT.push_back(cat.add(1, {h}));
		for (const auto& n : npcs) t.STAT.push_back(cat.add(1, {n}));
		t.hero_count = 2;
		const std::vector<std::pair<std::string, u32>> items = {{"belt_x", 1}, {"glove_x", 2}, {"potion_x", 0}, {"armor_x", 3}, {"KEY1", 0},
			{"KEY2", 0}, {"KEY3", 0}, {"KEYCARD1", 0}, {"KEYCARD2", 0}, {"KEYCARD3", 0}};
		for (const auto& [n, c] : items)
		{
			t.ITEM.push_back(cat.add(2, {n}));
			t.ITEM_cat.push_back(c);
		}
		t.ENHC = {cat.add(3, {"p", "p_one", "1"}), cat.add(3, {"p", "p_two", "1"}), cat.add(3, {"s", "s_one", "1"}), cat.add(3, {"t", "t1", "e1"}),
			cat.add(3, {"i", "belt_x", "e1"}), cat.add(3, {"i", "belt_x", "e2"}), cat.add(3, {"i", "glove_x", "e1"}), cat.add(3, {"i", "armor_x", "e1"})};
		t.prefix[0] = 0, t.prefix[1] = 2, t.suffix[0] = 2, t.suffix[1] = 3, t.trit[0] = 3, t.trit[1] = 4, t.own[0] = 4, t.own[1] = 8;
		t.ITEM_own.assign(t.ITEM.size(), {});
		t.ITEM_own[0] = {t.ENHC[4], t.ENHC[5]};
		t.ITEM_own[1] = {t.ENHC[6]};
		t.ITEM_own[3] = {t.ENHC[7]};
		t.NAMA = {cat.add(4, {"rare_a"})};
		t.NAMB[1] = {cat.add(5, {"belt", "cls_belt_a"})};
		t.TRIT = {cat.add(6, {"t1"})};
		t.TRIT_own = {{t.ENHC[3]}};
		for (const auto& n : shared) t.TALN_shared.push_back(cat.add(7, {"shared", n}));
		for (u32 si = 1; si <= 2; ++si)
		{
			const auto& h = heroes[si - 1];
			for (const auto& n : hero_tal.at(h)) t.TALN_hero[si].push_back(cat.add(7, {h, n}));
			for (u32 s = 0; s < 9; ++s)
			{
				const std::string attr = s == 0 ? "skin" : std::string("skin_") + slot_names[s];
				t.COST[si][s] = cat.add(8, {h, attr});
			}
			// the shared talents the herostat lists (sh_fight, sh_acro, sh_tough and the extra one), by this release's positions
			std::vector<u32> hold;
			for (std::size_t p = 0; p < shared.size(); ++p)
			{
				const auto& n = shared[p];
				if (n == "sh_fight" || n == "sh_acro" || n == "sh_tough" || n == extra_talent) hold.push_back(static_cast<u32>(p));
			}
			t.holdable[t.STAT[si]] = hold;
		}
		t.MSNF = {cat.add(9, {"act_x"})};
		for (const char* o : {"obj_a", "obj_b", "obj_c", "o1"}) t.OBJV.push_back(cat.add(10, {"act_x", o}));
		t.DRGR = {cat.add(12, {"grade_a"})};
		t.DRCS = {cat.add(11, {"course_a"}), cat.add(11, {"course_b"})};
		t.REVW[{0, 0}] = cat.add(13, {"load", "l1"});
		t.REVW[{0, 1}] = cat.add(13, {"load", "l2"});
		t.REVW[{1, 0}] = cat.add(13, {"cin", "c1"});
		t.REVW[{3, 0}] = cat.add(13, {"concept", "k1"});
		t.XPNT = {cat.add(14, {"zone/x/y", "centre_a"})};
		t.TRIV[{1, 0}] = cat.add(16, {"1", "q1"});
		t.TRIV[{1, 1}] = cat.add(16, {"1", "q2"});
		t.TRIV[{2, 0}] = cat.add(16, {"2", "q1"});
		t.ZONE = {cat.add(15, {"zone/x/y"}), cat.add(15, {"zone/x/z"})};
		t.has_digest = true;
		t.conversation_digest[0] = static_cast<std::uint8_t>(cv);
		return t;
	}

	std::vector<std::pair<std::string, std::string>> numbered_names(const Zone& zone)
	{
		const auto raw = encode_xmlb(zone_file(zone));
		std::vector<Numbered> num;
		std::vector<std::string> problems;
		numbered_entities(decode_xmlb(raw.data(), raw.size()), num, problems);
		std::vector<std::pair<std::string, std::string>> out;
		for (const auto& n : num) out.emplace_back(n.type, n.name);
		return out;
	}

	// a TablesData with the reference build (and its placement file in `folder`) and the eras given
	std::shared_ptr<TablesData> make_tables(const fs::path& folder, const std::string& extra_talent, bool with_old_era)
	{
		auto t = std::make_shared<TablesData>();
		Cat cat;
		auto ref = tables_of(cat, 13, shared_default, extra_talent);
		std::vector<EraTables> eras{ref};
		if (with_old_era)
		{
			auto old = tables_of(cat, 5, {"sh_fight", "sh_might", "sh_acro", "sh_tough"}, extra_talent);
			const Zone old_zone = {{"gate", {"gate01", "gate02"}}, {"crate", {"crate01"}}, {"spawner_a", {"sp01"}}, {"crate_pinned", {"crate02"}},
				{"spot", {"spot01"}}};
			const auto [m, amb] = align(numbered_names(old_zone), numbered_names(default_zone));
			old.zones[ref.ZONE[0]] = EraTables::ZoneDelta{m, amb};
			eras.push_back(old);
		}
		t->catalogue = cat.c;
		t->catalogue.revision = 1;
		t->catalogue.content_version = 13;
		t->reference = ref;
		t->eras = eras;
		t->legacy_map_id = 13;
		t->legacy_catalogue_revision = 1;
		auto b = std::make_unique<Build>();
		b->data_root = folder.wstring();
		b->bind_root = folder.wstring();
		b->T = ref;
		for (const auto& [position, id] : ref.TRIV) b->T.trivia_bits[position] = (position.first - 1) * 50 + position.second;
		b->hero_count = 2;
		b->stat.push_back(std::string());
		for (const auto& h : heroes) b->stat.push_back(h);
		for (const auto& n : npcs) b->stat.push_back(n);
		for (const char* n : {"belt_x", "glove_x", "potion_x", "armor_x", "key1", "key2", "key3", "keycard1", "keycard2", "keycard3"}) b->item.push_back(n);
		b->course = {"course_a", "course_b"};
		b->herostat_talents.assign(b->stat.size(), {});
		// positions (what read_binds fills from the bind files)
		for (u32 i = 1; i < ref.STAT.size(); ++i) t->p_stat[ref.STAT[i]] = i;
		for (u32 i = 0; i < ref.ITEM.size(); ++i) t->p_item[ref.ITEM[i]] = i;
		for (u32 i = 0; i < ref.ENHC.size(); ++i) t->p_enh[ref.ENHC[i]] = i;
		for (u32 i = 0; i < ref.NAMA.size(); ++i) t->p_nama[ref.NAMA[i]] = i;
		for (const auto& [c, l] : ref.NAMB) for (u32 i = 0; i < l.size(); ++i) t->p_namb[l[i]] = i;
		for (u32 i = 0; i < ref.TRIT.size(); ++i) t->p_trit[ref.TRIT[i]] = i;
		// talent codes (owner + 1, position): the layout makes the engine id (lcsave::detail::talent_engine_id)
		for (u32 i = 0; i < ref.TALN_shared.size(); ++i) t->p_tal[ref.TALN_shared[i]] = talent_code(0, i);
		for (const auto& [si, l] : ref.TALN_hero) for (u32 p = 0; p < l.size(); ++p) t->p_tal[l[p]] = talent_code(si + 1, p);
		for (const auto& [si, slots] : ref.COST) for (const auto& [s, id] : slots) t->p_cost[id] = s;
		for (u32 i = 0; i < ref.OBJV.size(); ++i) t->p_obj[ref.OBJV[i]] = i;
		for (u32 i = 0; i < ref.DRCS.size(); ++i) t->p_course[ref.DRCS[i]] = i;
		for (u32 i = 0; i < ref.DRGR.size(); ++i) t->p_grade[ref.DRGR[i]] = i;
		for (const auto& [k, id] : ref.REVW) t->p_review[id] = k;
		for (u32 i = 0; i < ref.XPNT.size(); ++i) t->p_xp[ref.XPNT[i]] = i;
		for (const auto& [k, id] : ref.TRIV) t->p_triv[id] = k;
		t->p_zone[ref.ZONE[0]] = "zone/x/y";
		t->p_zone[ref.ZONE[1]] = "zone/x/z";
		for (const auto& e : t->catalogue.entries) t->names[e.id] = unescape_key(e.key);
		t->build = std::move(b);
		// the placement file of zone/x/y
		fs::create_directories(folder / "Maps" / "zone" / "x");
		const auto raw = encode_xmlb(zone_file(default_zone));
		std::ofstream(folder / "Maps" / "zone" / "x" / "y.engb", std::ios::binary).write(reinterpret_cast<const char*>(raw.data()),
			static_cast<std::streamsize>(raw.size()));
		// Historical fixture digests are invented; supply their file/lane identities explicitly.
		const auto package_dir = folder / "Packages/generated/maps/zone/x";
		fs::create_directories(package_dir);
		const auto package = encode_xmlb(E{"packagedef", {}, {E{"xml", {{"filename", "conversations/invented"}}, {}}}});
		for (const auto* name : {"y.pkgb", "z.pkgb"})
			std::ofstream(package_dir / name, std::ios::binary).write(reinterpret_cast<const char*>(package.data()), static_cast<std::streamsize>(package.size()));
		const auto bits = conversation_bits(*t->build, "zone/x/y");
		for (const auto& era : t->eras) for (const auto* zone : {"zone/x/y", "zone/x/z"})
			t->conversation_tables[{era.conversation_digest, key_hash1(zone, Enc::latin1)}] = bits;
		for (const auto* zone : {"zone/x/y", "zone/x/z"})
			t->conversation_tables[{conversation_package_digest(key_hash1(zone, Enc::latin1), bits), key_hash1(zone, Enc::latin1)}] = bits;
		return t;
	}

	// ------------------------------------------------------------------------------------------------ synthetic old save
	OldInstance instance(u8 d, std::vector<std::pair<int, u32>> own = {}, std::vector<std::pair<int, u32>> prefixes = {},
		std::vector<std::pair<int, u32>> suffixes = {}, u8 name_a = 0xff, u8 name_b = 0xff, u8 special = 0xff, u8 location = 2)
	{
		OldInstance i;
		i.head = OldInstanceHead{d, name_a, name_b, special, location};
		for (const auto& [r, v] : prefixes) i.lists[0].push_back(OldEnhElement{static_cast<u16>(r), v});
		for (const auto& [r, v] : own) i.lists[1].push_back(OldEnhElement{static_cast<u16>(r), v});
		for (const auto& [r, v] : suffixes) i.lists[2].push_back(OldEnhElement{static_cast<u16>(r), v});
		return i;
	}

	OldAttributes attr(u8 level, std::vector<std::pair<int, int>> talents)
	{
		OldAttributes a;
		for (int k = 0; k < 4; ++k) a.scalars.trait_base[k] = 1;
		a.scalars.level = level;
		a.scalars.ai_level = 1;
		std::sort(talents.begin(), talents.end());
		for (std::size_t k = 0; k < talents.size(); ++k)
		{
			a.talents[k].id = static_cast<u16>(talents[k].first);
			a.talents[k].rank_bits = static_cast<u8>(talents[k].second | 0x40);
			a.talents[k].base_bits = 1 | 0x2c;
		}
		a.tail.count = static_cast<u32>(talents.size());
		a.tail.bitmap = (1u << talents.size()) - 1;
		return a;
	}

	OldEquipment equip(std::vector<OldInstance> items = {}, u32 xp = 0)
	{
		OldEquipment e;
		e.equipped = std::move(items);
		e.tail.player_seat = -1;
		e.tail.experience = xp;
		e.tail.ack_level = 1;
		return e;
	}

	OldSnapshot snapshot(const std::string& zone = "", std::vector<std::string> names = {"", "", "", ""}, std::map<int, int> objectives = {})
	{
		OldSnapshot s{};
		set_cstr(s.zone, zone);
		for (int k = 0; k < 4; ++k) set_cstr(s.seat_name[k], names[static_cast<std::size_t>(k)]);
		for (const auto& [i, v] : objectives) s.objectives[i] = static_cast<u8>(v);
		return s;
	}

	// the file `f` written again with section `tag` replaced by (minor, payload): header, table, CRCs as write_file does
	std::vector<std::uint8_t> rewrite_file(const FileIn& f, const std::string& tag, u8 minor, const std::string& payload)
	{
		const u32 table_off = new_header_size;
		u32 off = table_off + new_section_entry_size * static_cast<u32>(f.order.size());
		std::vector<std::uint8_t> after, body;
		for (const auto& t : f.order)
		{
			const auto& sec = f.sections.at(t);
			NewSectionEntry e = sec.entry;
			const std::string& pl = t == tag ? payload : sec.payload;
			if (t == tag) e.minor = minor;
			e.offset = off;
			e.length = static_cast<u32>(pl.size());
			e.crc = crc32(reinterpret_cast<const std::uint8_t*>(pl.data()), pl.size());
			const auto* ep = reinterpret_cast<const std::uint8_t*>(&e);
			after.insert(after.end(), ep, ep + sizeof e);
			body.insert(body.end(), pl.begin(), pl.end());
			const u32 pad = static_cast<u32>((4 - pl.size() % 4) % 4);
			body.insert(body.end(), pad, 0);
			off += static_cast<u32>(pl.size()) + pad;
		}
		after.insert(after.end(), body.begin(), body.end());
		NewHeader h = f.header;
		h.file_length = new_header_size + static_cast<u32>(after.size());
		h.body_crc = crc32(after);
		const auto* hp = reinterpret_cast<const std::uint8_t*>(&h);
		h.header_crc = crc32(hp, 0x84);
		std::vector<std::uint8_t> out(hp, hp + sizeof h);
		out.insert(out.end(), after.begin(), after.end());
		return out;
	}

	// a HERO section as SPEC revision 2.2 wrote it (HERO 1.0): PWR = button + name, no flag byte; unnamed slots left out
	std::string hero_payload_rev22(const Value& hero)
	{
		std::string out = encode_section("HERO", Value::dict({{"heroes", Value::list()}}));
		out.resize(out.size() - 4);                                 // head; the list count follows
		const auto& recs = hero["heroes"].as_list();
		const auto n = static_cast<u32>(recs.size());
		out.append(reinterpret_cast<const char*>(&n), 4);
		for (const auto& h : recs)
		{
			auto rec = encode_record("HREC", h);
			std::size_t first = std::string::npos;
			u32 named = 0;
			for (const auto& pw : h["powers"].as_list())
			{
				const auto now = encode_record("PWR", pw);
				std::string old;
				if (!pw["name"].as_text().empty())
				{
					old = now.substr(0, now.size() - 1);
					const u32 rsize = static_cast<u32>(old.size() - 4);
					std::memcpy(old.data(), &rsize, 4);
					++named;
				}
				const auto at = rec.find(now, first == std::string::npos ? 0 : first);
				if (at == std::string::npos) return std::string();
				if (first == std::string::npos) first = at;
				rec.replace(at, now.size(), old);
			}
			// the powers list count (just before the first record) and the record size
			if (first != std::string::npos) std::memcpy(rec.data() + first - 4, &named, 4);
			const u32 rsize = static_cast<u32>(rec.size() - 4);
			std::memcpy(rec.data(), &rsize, 4);
			out += rec;
		}
		return out;
	}

	OldSlot make_old_save(std::vector<int> shared_ids = {0, 3, 4}, u8 def_count = 10, bool zone = true, u32 costume_mask = 3,
		std::vector<u8> power_flags = {1, 1, 1, 1})
	{
		OldSlot m;
		set_cstr(reinterpret_cast<char*>(m.description_raw), 0x80, "desc");
		m.begin_tag = "[SAVEGAMEBEGIN: desc]";
		m.end_tag = "[SAVEGAMEEND: desc]";
		OldHero h0;
		h0.attributes = attr(5, {{shared_ids[0], 2}, {shared_ids[1], 3}, {shared_ids[2], 1}, {200, 4}, {202, 1}});
		h0.equipment = equip({instance(0, {{4, 7}}, {{1, 9}}, {}, 0xff, 0xff, 0xff, 1)}, 1234);
		set_cstr(h0.powers.power[0].name, "power1");
		set_cstr(h0.powers.power[2].name, "power3");
		for (int k = 0; k < 4; ++k) h0.powers.power[k].flags = power_flags[static_cast<std::size_t>(k)];
		h0.costume = OldCostume{3, costume_mask};
		OldHero h1;
		for (auto& slot : h1.powers.power) slot.flags = power_usable;
		h1.attributes = attr(1, {{0, 1}, {300, 1}});
		h1.equipment = equip();
		h1.costume = OldCostume{0, 3};
		m.heroes = {h0, h1};
		m.items.def_count = def_count;
		m.items.def_state[4] = 3;                                   // potion quantity 3
		m.items.tech_bits = 55;
		m.items.health_items = 2;
		m.items.key_counts[0] = 1;
		m.items.key_counts[3] = 2;
		m.carried = {instance(2), instance(1, {{6, 3}}, {}, {{2, 5}}, 0xff, 0xff, 0xff, 3), instance(0, {{5, 1}}, {}, {}, 0, 0, 0, 2)};
		m.world = {OldWorld{5, instance(3, {{7, 1}})}};
		for (u32 k = 0; k < old_restore_records; ++k) m.restore.push_back(OldRestore{attr(1, {}), equip()});
		auto& g = m.game;
		g.xtreme_charges = 1;
		g.xtreme_pips = 2;
		set_cstr(g.recall_zone, "none");
		g.act = 2;
		g.trivia[0] = 0b11;
		g.trivia[1] = 1u << 18;
		g.difficulty = 1;
		g.play_time = 99.5f;
		g.flags_d8 = 2;
		g.flags_d9 = 0x20 | 0x40;
		g.flags_da = 2 | 4;
		auto& st = *m.zone_strings;
		st.clear();
		st.generation = 1;
		st.used = 2;
		st.offsets[0] = 1;
		auto intern = [&](u32 slot, const std::string& text)
		{
			const u32 off = st.used;
			std::memcpy(st.arena.data() + off, text.c_str(), text.size() + 1);
			st.offsets[slot] = off;
			st.used = off + static_cast<u32>(text.size()) + 1;
			return (1u << 24) | slot;
		};
		if (zone)
		{
			const auto h_zone = intern(10, "zone/x/y"), h_gate = intern(11, "gate"), h_g1 = intern(12, "gate01"), h_g2 = intern(13, "gate02");
			const auto h_crate = intern(14, "crate_pinned"), h_c2 = intern(15, "crate02"), h_dyn = intern(16, "loot"), h_dn = intern(17, "loot01");
			OldZone z;
			z.name_handle = h_zone;
			z.states.assign(old_zone_states, OldStateRecord{});
			z.states[1] = OldStateRecord{h_gate, h_g1, {1, 2, 3, 4, 5, 6, 7, 8}, 0, 8, world_slot_none, {10, -20, 30}};
			z.states[2] = OldStateRecord{h_gate, h_g2, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}, 0, 8, world_slot_none, {1, 2, 3}};
			z.states[3] = OldStateRecord{h_crate, h_c2, {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22}, 0, 8 | 16, world_slot_none, {4, 5, 6}};
			z.states[4] = OldStateRecord{h_dyn, h_dn, {5, 3, 0, 0x33, 0x33, 0x33, 0x33, 0x33}, 1, 0, world_slot_none, {7, 8, 9}};
			z.tail.state_count = 5;
			z.tail.ordinal_map[0] = 1;
			z.tail.ordinal_map[1] = 2;
			z.tail.ordinal_map[2] = 3;
			z.tail.dynamic[0] = 4;
			z.tail.dynamic_count = 1;
			std::memset(z.tail.fog, 0x5a, old_fog_bytes);
			z.tail.conversation[0] = 1;
			z.tail.time_stamp = 12.5f;
			m.zones.push_back(z);
		}
		m.profile.head.options[0] = 0x0a;
		m.profile.head.options[1] = 0x87;
		m.profile.head.options[2] = 0x0f;
		set_cstr(m.profile.head.player_name, "player");
		m.profile.tail.char_unlock[0] = 0b110;
		m.profile.tail.review[0] = 1;
		m.profile.tail.highest_level = 5;
		set_cstr(m.game_vars[0].name, "gv_one");
		m.game_vars[0].value_be[3] = 7;
		m.unlocked[0] = 1;
		m.unlocked[1] = 2;
		m.unlocked[2] = 4;
		m.unlocked_count = 3;
		m.side_frames[0] = snapshot();
		m.side_frames[1] = snapshot();
		OldDrCourse c{};
		c.flags = 1, c.tier1 = 1, c.largest_hit = 100, c.largest_hit_hero = 2, c.least_damage = 5, c.best_time = 60, c.best_combo = 9;
		c.most_health_packs = 1, c.most_defeats = 3, c.most_defeats_hero = 1;
		m.courses = {c, c};
		m.course_residue.assign(old_dr_courses - 2, 0);
		m.grades[0] = 3;
		m.review.unlocked[0] = 0b11;
		m.review.seen[3] = 1;
		m.mission = snapshot("zone/x/y", {"hero_a", "", "hero_b", ""}, {{0, 1}, {2, 0x41}});
		m.town_centres = 1;
		return m;
	}

	// dump.semantic_diff == [] (zone states compared as multisets without their ordinals; LGCY ignored)
	bool semantic_equal(const Value& a, const Value& b, const std::string& path, std::string& why)
	{
		if (path == ".zones" && a.type == Value::Type::list && b.type == Value::Type::list)
		{
			if (a.l.size() != b.l.size())
			{
				why = path + ": record count";
				return false;
			}
			for (std::size_t i = 0; i < a.l.size(); ++i)
			{
				for (const auto& it : a.l[i].d)
				{
					if (it.key.s == "states")
					{
						auto keyed = [](const Value& states)
						{
							std::vector<std::string> out;
							for (const auto& s : states.l)
							{
								Value k = Value::dict();
								for (const auto& f : s.d) if (f.key.s != "ordinal") k.set(f.key, f.value);
								out.push_back(to_json(k));
							}
							std::sort(out.begin(), out.end());
							return out;
						};
						if (keyed(it.value) != keyed(b.l[i]["states"]))
						{
							why = path + ": states";
							return false;
						}
					}
					else if (!b.l[i].find(it.key) || *b.l[i].find(it.key) != it.value)
					{
						why = path + "." + it.key.s;
						return false;
					}
				}
			}
			return true;
		}
		if (a.type == Value::Type::dict && b.type == Value::Type::dict)
		{
			for (const auto* x : {&a, &b})
			{
				for (const auto& it : x->d)
				{
					if (it.key.type == Value::Type::text && it.key.s == "lgcy") continue;
					const auto* va = a.find(it.key);
					const auto* vb = b.find(it.key);
					if (!va || !vb)
					{
						why = path + "." + it.key.s + ": only in one";
						return false;
					}
					if (!semantic_equal(*va, *vb, path + "." + it.key.s, why)) return false;
				}
			}
			return true;
		}
		if (a.type == Value::Type::list && b.type == Value::Type::list)
		{
			if (a.l.size() != b.l.size())
			{
				why = path + ": length";
				return false;
			}
			for (std::size_t i = 0; i < a.l.size(); ++i)
			{
				if (!semantic_equal(a.l[i], b.l[i], path + "[" + std::to_string(i) + "]", why)) return false;
			}
			return true;
		}
		if (a != b)
		{
			why = path + ": " + to_json(a) + " vs " + to_json(b);
			return false;
		}
		return true;
	}

	std::string refusal(const std::vector<std::uint8_t>& raw, const TablesData& t)
	{
		ConvertState st;
		return error_of([&] { convert_slot(raw, t, ConvertOptions{}, st); });
	}

	// ------------------------------------------------------------------------------------------------ the vectors
	void test_keys()
	{
		check(canonical_key({"Hero_A", "Skin_60s"}, Enc::cp1252) == "hero_a/skin_60s", "canonical key lower-cases");
		check(canonical_key({"a/b", "50%"}, Enc::cp1252) == "a%2fb/50%25", "canonical key escapes / and %");
		check(key_hash1("x", Enc::cp1252) == key_hash1("X", Enc::cp1252), "key hash is case-insensitive");
		check(hex(key_hash1("x", Enc::cp1252)) == "2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881", "key hash of x");
		check(hex(key_hash1("x", Enc::cp1252)).size() == 64, "key hash is 64 hex characters");
		const auto v = make_id(7, 300);
		check(id_domain(v) == 7 && id_serial(v) == 300 && !is_keyref(v), "id grammar");
		const auto r = make_keyref(5);
		check(is_keyref(r) && id_domain(r) == 0, "key reference");
		check(crc32(reinterpret_cast<const std::uint8_t*>("123456789"), 9) == 0xcbf43926, "the CRC-32 check value");
		for (const auto& [d, s] : std::vector<std::pair<unsigned, u32>>{{0, 1}, {128, 1}, {1, 0}, {1, 0x1000000}})
		{
			check(make_id(d, s) == 0, "bad id parts refused");
		}
		const std::string abc = "abc";
		check(hex(sha256(reinterpret_cast<const std::uint8_t*>(abc.data()), 3)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc");
		check(hex(sha1(reinterpret_cast<const std::uint8_t*>(abc.data()), 3)) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 abc");
		const std::string a1000(1000, 'a');
		check(hex(sha256(reinterpret_cast<const std::uint8_t*>(a1000.data()), 1000)) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3", "SHA-256 of 1000 bytes");
		check(hex(sha1(reinterpret_cast<const std::uint8_t*>(a1000.data()), 1000)) == "291e9a6c66994949b57ba5e650361e98fc36b1ba", "SHA-1 of 1000 bytes");
		// Python behaviours the converter and the dumps depend on
		check(py_float_repr(1.5) == "1.5" && py_float_repr(0.1) == "0.1" && py_float_repr(1e16) == "1e+16" && py_float_repr(1e-05) == "1e-05" &&
			py_float_repr(0.0001) == "0.0001" && py_float_repr(-0.0) == "-0.0" && py_float_repr(123456789.0) == "123456789.0", "float repr");
		check(py_round3(0.0625) == 0.062 && py_round3(0.1875) == 0.188 && py_round3(2.0005) == 2.001 && py_float_repr(py_round3(-0.0004)) == "-0.0" &&
			py_round3(0.0005) == 0.001 && py_round3(1.0005) == 1.0, "round(x, 3) as Python (values from Python)");
		check(py_lower("ABC\xc9\x8a", Enc::cp1252) == "abc\xe9\x9a" && py_lower("ABC\xc9\x8a", Enc::latin1) == "abc\xe9\x8a", "str.lower by decoding");
	}

	void test_layout()
	{
		char field[6];
		set_cstr(field, "hi");
		check(std::memcmp(field, "hi\0\0\0\0", 6) == 0, "cstr is NUL-padded");
		std::memcpy(field, "hi\0ZZ\0", 6);
		check(cstr(field) == "hi", "cstr text ends at the first NUL (the bytes after it stay in the field)");
		check(!error_of([&] { set_cstr(field, "toolong"); }).empty(), "cstr that does not fit is refused");
		check(sizeof(NewHeader) == 0x88 && retail_layout.record_size == 0x55e0 && e10a2_layout.record_size == 0x19b88, "layout sizes");
	}

	void test_numbering(const fs::path& folder)
	{
		const auto raw = encode_xmlb(zone_file(default_zone));
		const auto root = decode_xmlb(raw.data(), raw.size());
		std::vector<Numbered> num;
		std::vector<std::string> problems;
		numbered_entities(root, num, problems);
		std::vector<std::pair<std::string, std::string>> names;
		std::vector<u32> placements;
		for (const auto& n : num)
		{
			names.emplace_back(n.type, n.name);
			placements.push_back(n.placement);
		}
		check(names == std::vector<std::pair<std::string, std::string>>{{"gate", "gate01"}, {"gate", "gate02"}, {"crate_pinned", "crate02"},
			{"spawner_a", "sp01"}, {"spot", "spot01"}}, "numbering rule: gameent, physent smartent=false, spawner, ent numbered");
		check(placements == std::vector<u32>{0, 1, 3, 4, 7}, "placement indices");
		check(problems.empty(), "no problems");
		// an unknown class and a missing definition make the zone unmodellable
		E w = zone_file({{"gate", {"g1"}}, {"nodef", {"n1"}}});
		w.add(E{"entity", {{"name", "odd"}, {"classname", "strangeent"}}, {}});
		w.add(E{"entinst", {{"type", "odd"}}, {E{"inst", {{"name", "o1"}}, {}}}});
		const auto raw2 = encode_xmlb(w);
		num.clear();
		problems.clear();
		numbered_entities(decode_xmlb(raw2.data(), raw2.size()), num, problems);
		check(problems.size() == 2 && problems[0] == "no entity definition for type 'nodef'" &&
			problems[1] == "unknown entity class 'strangeent' (type 'odd')", "numbering problems");
		check(!error_of([&] { decode_xmlb(raw.data(), 20); }).empty(), "truncated XMLB refused");
		(void)folder;
	}

	void test_oldsave()
	{
		const auto m = make_old_save();
		const auto raw = write_slot(m);
		check(raw.size() == old_slot_size, "slot size");
		const auto back = read_slot(raw.data(), raw.size());
		check(write_slot(back) == raw, "old save round trip");
		check(back.heroes.size() == 2 && back.world[0].slot == 5 && cstr(back.mission.zone) == "zone/x/y", "old save fields");
		auto bad1 = raw;
		bad1.pop_back();
		auto bad2 = raw;
		bad2[0x80] = 1, bad2[0x81] = 0, bad2[0x82] = 0, bad2[0x83] = 0;
		auto bad3 = raw;
		std::memset(bad3.data() + 0x84, 0, 52);
		for (const auto* b : {&bad1, &bad2, &bad3})
		{
			check(!error_of([&] { read_slot(b->data(), b->size()); }).empty(), "size / length field / tag refused");
		}
		auto m2 = make_old_save();
		m2.zones[0].tail.ordinal_map[0] = 700;
		const auto raw2 = write_slot(m2);
		check(contains(error_of([&] { read_slot(raw2.data(), raw2.size()); }), "ordinal"), "an ordinal outside the record refused");
	}

	void test_container()
	{
		HeaderIn h;
		h.writer_version = 0x00010001;
		h.content_version = 13;
		h.catalogue_revision = 1;
		h.campaign_serial = 42;
		h.save_sequence = 1;
		Value mods = Value::list();
		mods.push(Value::text("m1"));
		Value chars = Value::list();
		chars.push(Value::integer(make_id(1, 1)));
		Value party = Value::list();
		party.push(Value::integer(make_id(1, 1)));
		Value scores = Value::list();
		scores.push(Value::dict({{"course", Value::integer(make_id(11, 1))}, {"party_a", party}, {"party_b", Value::list()}, {"score", Value::integer(9)}}));
		const std::vector<SectionOut> sec = {
			{"META", 1, 0, section_required, Value::dict({{"saved_at", Value::integer(1)}, {"description", Value::text("d")}, {"origin", Value::integer(1)},
				{"zone", Value::integer(0)}, {"act", Value::integer(1)}, {"difficulty", Value::integer(1)}, {"play_time", Value::real(1.0)},
				{"writer", Value::text("w")}, {"slot", Value::integer(0)}, {"mods", mods}})},
			{"PRFO", 1, 0, 0, Value::dict({{"options", Value::integer(5)}, {"player_name", Value::text("p")}})},
			{"PRFP", 1, 0, 0, Value::dict({{"highest_level", Value::integer(3)}, {"flags", Value::integer(0)}, {"characters", chars}, {"review", Value::list()}})},
			{"PRFS", 1, 0, 0, Value::dict({{"scores", scores}})}};
		const auto buf = write_file(kind_profile, sec, h);
		const auto f = read_file(buf.data(), buf.size(), kind_profile);
		check(f.order == std::vector<std::string>{"META", "PRFO", "PRFP", "PRFS"} && f.data("PRFS")["scores"].l[0]["score"].as_int() == 9, "profile round trip");
		check(f.data("META")["mods"].l.size() == 1 && f.data("META")["mods"].l[0].s == "m1" && f.header.campaign_serial == 42, "META and header");
		int refused = 0, tried = 0;
		std::vector<std::size_t> offsets;
		for (std::size_t i = 0; i < 0x88; i += 7) offsets.push_back(i);
		for (std::size_t i = 0x88; i < buf.size(); i += 11) offsets.push_back(i);
		for (const auto i : offsets)
		{
			auto bad = buf;
			bad[i] ^= 0xff;
			++tried;
			refused += !error_of([&] { read_file(bad.data(), bad.size(), -1); }).empty();
		}
		check(refused == tried, "every damaged byte of the header and the body refused (" + std::to_string(refused) + "/" + std::to_string(tried) + ")");
		check(!error_of([&] { read_file(buf.data(), buf.size(), kind_campaign); }).empty(), "wrong kind refused");
		check(!error_of([&] { encode_section("XXXX", Value::dict()); }).empty(), "unknown section has no schema");
		auto data = encode_section("PRFO", Value::dict({{"options", Value::integer(5)}, {"player_name", Value::text("p")}}));
		data += std::string("\x01\x02", 2);
		const auto d = decode_section("PRFO", data);
		check(d.has("_extra") && d["_extra"].s == std::string("\x01\x02", 2), "newer content kept as _extra");
		// the frozen element sizes and a record's bytes (SPEC 2.5-2.10)
		check(encode_value("packed:ENH", Value::dict({{"enhancement", Value::integer(1)}, {"value", Value::integer(2)}})).size() == 8, "ENH 8 bytes");
		check(encode_value("packed:TAL", Value::dict({{"talent", Value::integer(1)}, {"rank", Value::integer(2)}, {"pending", Value::integer(3)},
			{"cap", Value::integer(1)}, {"base", Value::integer(1)}, {"byte5", Value::integer(0)}})).size() == 9, "TAL 9 bytes");
		check(encode_value("packed:OBJ", Value::dict({{"objective", Value::integer(1)}, {"counter", Value::integer(2)}, {"flags", Value::integer(1)}})).size() == 6,
			"OBJ 6 bytes");
		check(encode_value("packed:GRD", Value::dict({{"grade", Value::integer(1)}, {"value", Value::integer(2)}})).size() == 5, "GRD 5 bytes");
		check(encode_value("packed:DEFSTATE", Value::dict({{"item", Value::integer(1)}, {"quantity", Value::integer(2)}, {"flag", Value::integer(0)}})).size() == 9,
			"DEFSTATE 9 bytes");
		check(encode_record("VAR", Value::dict({{"name", Value::text("ab")}, {"value", Value::integer(7)}})) ==
			std::string("\x08\x00\x00\x00\x02\x00" "ab" "\x07\x00\x00\x00", 12), "VAR record bytes");
		// PWR (HERO minor 1): button, name, the slot's whole flag byte; a HERO 1.0 record (no flag byte) decodes without it
		check(encode_record("PWR", Value::dict({{"button", Value::integer(2)}, {"name", Value::text("ab")}, {"flags", Value::integer(0x80)}})) ==
			std::string("\x06\x00\x00\x00\x02\x02\x00" "ab" "\x80", 10), "PWR record bytes");
		check(section_minor("HERO") == 1 && section_minor("PRFO") == 0, "section minors: HERO 1, the rest 0");
		// a section minor above the reader's (SPEC 1.4): "needs a newer version" unless the writer marked it EXT-DROPPABLE
		for (const u16 flags : {u16(0), section_ext_droppable})
		{
			auto newer = sec;
			newer[1].minor = static_cast<u8>(section_minor("PRFO") + 1);
			newer[1].flags = flags;
			const auto nb = write_file(kind_profile, newer, h);
			const auto e = error_of([&] { read_file(nb.data(), nb.size(), kind_profile); });
			check(flags ? e.empty() : contains(e, "needs a newer version"), flags ? "a higher minor with EXT-DROPPABLE loads" : "a higher minor is refused");
		}
	}

	void test_catalogue()
	{
		Cat cat;
		const auto a = cat.add(2, {"item_a"});
		const auto b = cat.add(2, {"item_b"});
		check(id_domain(a) == 2 && id_serial(b) == 2 && cat.c.lookup(2, key_hash1("ITEM_A", Enc::cp1252)) == a, "catalogue ids and case-insensitive lookup");
		// a merged entry resolves to its successor; an alias resolves to its entry
		cat.c.entries[0].retired = true;
		cat.c.entries[0].successor = b;
		cat.c.entries[1].aliases.push_back("item_old");
		cat.c.index();
		check(cat.c.lookup(2, key_hash1("item_a", Enc::cp1252)) == b, "retired entry -> successor");
		check(cat.c.lookup(2, key_hash1("item_old", Enc::cp1252)) == b, "alias -> its entry");
		check(cat.c.lookup(3, key_hash1("item_b", Enc::cp1252)) == 0, "a hash belongs to its domain");
	}

	void test_forge_import(const fs::path& scratch)
	{
		const auto t = make_tables(scratch / "forge", "sh_might", true);
		Tables tables; tables.data = t;
		const char* note = "Forge contact unlock granted (0.1.x builds did not record it)";
		Cat cat; cat.c = t->catalogue;
		for (const auto& e : cat.c.entries) cat.next[e.domain] = std::max(cat.next[e.domain], e.id & 0xffffffu);
		for (const char* zone : {"nyc/alison/early", "mansion/man1a/first", "haarp/ext/later", "mansion/man8/later", "mansion/jugrnt/flashback", "arena/training", "nyc/riots/later", "haarp/extensive/unknown"})
		{
			const auto id = cat.add(15, {zone});
			t->reference.ZONE.push_back(id); t->p_zone[id] = zone; t->names[id] = zone;
		}
		t->catalogue = cat.c;
		for (int scenario = 0; scenario < 15; ++scenario)
		{
			auto old = scenario == 12 ? make_old_save({0, 2, 3}) : make_old_save();
			const char* zone = scenario == 0 ? "nyc/alison/early" : scenario == 1 ? "mansion/man1a/first" : scenario == 3 ? "mansion/man8/later" : scenario == 4 ? "mansion/jugrnt/flashback" : scenario == 5 ? "arena/training" : scenario == 6 ? "nyc/riots/later" : scenario == 7 ? "haarp/extensive/unknown" : "haarp/ext/later";
			set_cstr(old.mission.zone, zone);
			if (scenario < 9) { set_cstr(old.game_vars[1].name, "X1V03"); old.game_vars[1].value_be[0] = scenario == 8 ? 0xc0 : 0x80; old.game_vars[1].value_be[3] = 5; }
			if (scenario == 10 || scenario == 11 || scenario == 13) { set_cstr(old.mission.zone, "mansion/man1a/first"); set_cstr(old.side_frames[0].zone, "haarp/ext/later"); old.side.count = scenario == 10 ? 1 : 0; }
			if (scenario == 13)
			{
				old.side.count = 0;
				auto& strings = *old.zone_strings;
				const std::string historical = "haarp/ext/later";
				strings.offsets[40] = strings.used;
				std::memcpy(strings.arena.data() + strings.used, historical.c_str(), historical.size() + 1);
				strings.used += static_cast<u32>(historical.size() + 1);
				old.zones[0].name_handle = (1u << 24) | 40;
			}
			if (scenario == 14) for (u32 i = 0; i < old_script_game_vars; ++i) set_cstr(old.game_vars[i].name, "v" + std::to_string(i));
			const auto raw = write_slot(old);
			const auto r = convert_old_to_new(raw, tables);
			if (scenario == 14) { check(!r.ok && r.bytes.empty() && contains(r.error, "Forge contact unlock: game-variable store is full"), "Forge store capacity refuses without losing a variable"); continue; }
			check(r.ok, "Forge legacy import " + r.error); if (!r.ok) continue;
			const bool grant = scenario == 2 || scenario == 3 || scenario == 6 || scenario == 9 || scenario == 10 || scenario == 12 || scenario == 13;
			const auto f = read_file(r.bytes.data(), r.bytes.size(), kind_campaign);
			const auto& vars = f.data("SVAR")["game_vars"].l;
			const auto expected = scenario < 9 ? ((grant || scenario == 8) ? 0xc0000005u : 0x80000005u) : 0x40000000u;
			check(vars.size() == (scenario == 11 ? 1u : 2u), "Forge variable count");
			if (scenario != 11 && vars.size() > 1) check(vars[1]["value"].as_int() == expected, "Forge grant preserves other bits");
			check((r.ledger.find(note) != std::string::npos) == grant, "Forge one import ledger grant");
			if (scenario == 13) continue;
			NewFileContext ctx; ctx.saved_at = 0;
			const auto native = old_stream_to_new(raw, tables, ctx);
			check(native.ok, "Forge native isolation " + native.error);
			if (native.ok) { const auto nf = read_file(native.bytes.data(), native.bytes.size(), kind_campaign); check(nf.data("SVAR")["game_vars"].l.size() == (scenario < 9 ? 2u : 1u) && native.ledger.find(note) == std::string::npos, "Forge normal saver never grants"); if (scenario < 9) check(nf.data("SVAR")["game_vars"].l[1]["value"].as_int() == (scenario == 8 ? 0xc0000005u : 0x80000005u), "Forge native flags untouched"); }
		}
	}

	void test_align()
	{
		using S = std::vector<std::string>;
		using U = std::vector<u32>;
		auto r1 = align(S{"a", "b", "V", "c"}, S{"a", "b", "c", "V"});
		check(r1.first == U{1, 2, 4, 3} && r1.second.empty(), "align: a moved unique entity");
		auto r2 = align(S{"a", "c"}, S{"a", "b", "c"});
		check(r2.first == U{1, 3} && r2.second.empty(), "align: an insertion");
		auto r3 = align(S{"x", "x"}, S{"x", "x", "x"});
		check(r3.second == U{1, 2}, "align: twins undecidable");
		auto r4 = align(S{"a", "gone"}, S{"a"});
		check(r4.first == U{1, 0} && r4.second == U{2}, "align: a removed entity");
	}

	void test_convert(const fs::path& folder)
	{
		const auto t = make_tables(folder, "sh_might", true);
		ConvertOptions opt;
		opt.fixed_saved_at = true;
		opt.saved_at = 116444736000000000ull;
		// (1) a save written by the reference era: byte-exact reverse
		const auto raw = write_slot(make_old_save({0, 3, 4}, 10));
		ConvertState st;
		std::vector<std::uint8_t> file;
		const auto err = error_of([&] { file = convert_slot(raw, *t, opt, st); });
		check(err.empty(), "reference-era save converts (" + err + ")");
		if (!err.empty()) return;
		check(st.start_era == 13 && st.items_era == 13 && st.zone_eras == std::vector<int>{13}, "eras 13 / 13 / zone 13");
		const auto f = read_file(file.data(), file.size(), kind_campaign);
		check(f.sections.count("KEYS") && (f.header.optional_features & 1), "KEYS present");
		const auto old = read_slot(raw.data(), raw.size());
		std::vector<std::string> notes;
		check(write_slot(reverse_slot(file, *t, &old, notes)) == raw, "reverse with the original's dropped bytes is byte-exact");
		std::string why;
		const auto d_old = dump_old(old, *t, st.start_era, st.items_era);
		const auto d_new = dump_new(file, *t);
		check(semantic_equal(d_old, d_new, "", why), "old and new dumps agree (" + why + ")");
		check(d_new["heroes"].l[0]["talents"].l[0]["talent"].s == "hero_a/a_one", "talents by name");
		check(f.sections.at("HERO").entry.minor == 1, "HERO is written at minor 1");
		{
			// (1c) a slot whose flag is 0 (the engine clears slots 2/3 itself in some modes) and an empty slot with other
			// bits set survive old -> new -> old (loader, no template) -> new unchanged, and the dumps carry them
			const auto rawf = write_slot(make_old_save({0, 3, 4}, 10, true, 3, {1, 0x81, 0, 1}));
			ConvertState stf;
			std::vector<std::uint8_t> filef;
			const auto errf = error_of([&] { filef = convert_slot(rawf, *t, opt, stf); });
			check(errf.empty(), "a save with flag-0 slots converts (" + errf + ")");
			if (!errf.empty()) return;
			const auto ff = read_file(filef.data(), filef.size(), kind_campaign);
			std::vector<std::tuple<std::int64_t, std::string, std::int64_t>> pw;
			for (const auto& p : ff.data("HERO")["heroes"].l[0]["powers"].l) pw.emplace_back(p["button"].as_int(), p["name"].as_text(), p["flags"].as_int());
			check(pw == std::vector<std::tuple<std::int64_t, std::string, std::int64_t>>{{0, "power1", 1}, {1, "", 0x81}, {2, "power3", 0}},
				"PWR records: named slots and the empty slot with a byte other than 1");
			const auto oldf = read_slot(rawf.data(), rawf.size());
			const auto d_newf = dump_new(filef, *t);
			const auto& dp = d_newf["heroes"].l[0]["powers"];
			check(dp["0"]["flags"].as_int() == 1 && dp["1"]["flags"].as_int() == 0x81 && dp["1"]["name"].as_text().empty() &&
				dp["2"]["flags"].as_int() == 0 && dp["2"]["name"].as_text() == "power3" && !dp.find(Value::text("3")), "the dump carries the flag bytes");
			check(semantic_equal(dump_old(oldf, *t, stf.start_era, stf.items_era), d_newf, "", why), "dumps agree with flag-0 slots (" + why + ")");
			std::vector<std::string> nf;
			const auto plainf = reverse_slot(filef, *t, nullptr, nf);
			const auto plainf_raw = write_slot(plainf);
			const auto loadedf = read_slot(plainf_raw.data(), plainf_raw.size());
			bool same = true;
			for (int k = 0; k < 4; ++k)
			{
				same &= std::memcmp(&loadedf.heroes[0].powers.power[k], &oldf.heroes[0].powers.power[k], sizeof(OldPowerSlot)) == 0;
			}
			check(same && loadedf.heroes[0].powers.power[2].flags == 0, "the loader's direction writes the flag bytes back (slot 2 stays 0)");
			ConvertState sta;
			std::vector<std::uint8_t> again;
			const auto erra = error_of([&] { again = convert_slot(write_slot(plainf), *t, opt, sta); });
			check(erra.empty() && semantic_equal(d_newf["heroes"], dump_new(again, *t)["heroes"], "", why), "old -> new -> old -> new keeps the flags (" + why + ")");
			nf.clear();
			check(write_slot(reverse_slot(filef, *t, &oldf, nf)) == rawf, "reverse with template is byte-exact with flag-0 slots");
			// (1d) a HERO 1.0 file (SPEC revision 2.2: names only, written by the 0.2.0 candidates) still loads with every
			// slot usable - the flag byte it lacks is read as 1
			const auto pl10 = hero_payload_rev22(ff.data("HERO"));
			check(!pl10.empty(), "HERO 1.0 payload built");
			if (pl10.empty()) return;
			const auto file10 = rewrite_file(ff, "HERO", 0, pl10);
			const auto f10 = read_file(file10.data(), file10.size(), kind_campaign);
			check(f10.sections.at("HERO").entry.minor == 0 && !f10.data("HERO")["heroes"].l[0]["powers"].l[0].has("flags"), "the HERO 1.0 file has no flag bytes");
			nf.clear();
			const auto plain10 = reverse_slot(file10, *t, nullptr, nf);
			const auto& p10 = plain10.heroes[0].powers.power;
			check(p10[0].flags == 1 && p10[1].flags == 1 && p10[2].flags == 1 && p10[3].flags == 1 && cstr(p10[0].name) == "power1" &&
				cstr(p10[2].name) == "power3", "a HERO 1.0 file loads with every slot usable");
			const auto dump10 = dump_new(file10, *t);
			const auto& d10 = dump10["heroes"].l[0]["powers"];
			check(d10["0"]["flags"].as_int() == 1 && d10["2"]["flags"].as_int() == 1 && !d10.find(Value::text("1")), "the dump of a HERO 1.0 file");
		}
		// (2) the same ranks saved by the old era
		auto m = make_old_save({0, 2, 3}, 10);
		m.zones[0].tail.ordinal_map[2] = 0;
		m.zones[0].tail.ordinal_map[3] = 3;            // crate02 at ordinal 4 in the old era
		const auto raw2 = write_slot(m);
		ConvertState st2;
		std::vector<std::uint8_t> file2;
		const auto err2 = error_of([&] { file2 = convert_slot(raw2, *t, opt, st2); });
		check(err2.empty(), "old-era save converts (" + err2 + ")");
		if (!err2.empty()) return;
		const auto& dr = st2.dropped;
		check(st2.start_era == 5 && st2.zone_eras == std::vector<int>{5} && !(dr.zone_records || dr.world_items || dr.zone_vars || dr.other ||
			dr.talents || dr.states), "eras of the old-era save, nothing dropped");
		const auto old2 = read_slot(raw2.data(), raw2.size());
		const auto d_old2 = dump_old(old2, *t, st2.start_era, st2.items_era);
		const auto d_new2 = dump_new(file2, *t);
		check(semantic_equal(d_old2, d_new2, "", why), "old-era dumps agree (" + why + ")");
		std::map<std::string, std::int64_t> tal;
		for (const auto& x : d_new2["heroes"].l[0]["talents"].l) tal[x["talent"].s] = x["rank"].as_int();
		check(tal["shared/sh_acro"] == 3 && tal["shared/sh_tough"] == 1, "the ranks came back by name");
		std::int64_t crate_ordinal = -1;
		for (const auto& s : d_new2["zones"].l[0]["states"].l) if (s["instance"].s == "crate02") crate_ordinal = s["ordinal"].as_int();
		check(crate_ordinal == 3, "re-numbered for the reference zone");
		notes.clear();
		const auto back2 = reverse_slot(file2, *t, &old2, notes);
		check(back2.heroes[0].attributes.talents[1].id == 3, "sh_acro is position 3 in the reference");
		// (3) refusals
		auto m3 = make_old_save({0, 3, 4}, 10);
		m3.carried[1].lists[1][0].record = 4;
		check(contains(refusal(write_slot(m3), *t), "own enhancement"), "own record of another definition refused");
		auto m4 = make_old_save({0, 3, 4}, 10);
		m4.mission.objectives[200] = 1;
		check(contains(refusal(write_slot(m4), *t), "objective index"), "objective beyond the table refused");
		auto m5 = make_old_save({0, 3, 4}, 10);
		set_cstr(m5.mission.seat_name[1], "nobody");
		check(contains(refusal(write_slot(m5), *t), "seat"), "unknown seat refused");
		check(contains(refusal(write_slot(make_old_save({0, 3, 4}, 77)), *t), "definition count"), "definition count no release writes refused");
		// (4) a zone record no era explains is dropped, the rest converts
		auto m7 = make_old_save({0, 3, 4}, 10);
		m7.zones[0].tail.ordinal_map[0] = 2;
		m7.zones[0].tail.ordinal_map[1] = 1;
		ConvertState st7;
		const auto err7 = error_of([&] { convert_slot(write_slot(m7), *t, opt, st7); });
		check(err7.empty() && st7.dropped.zone_records == 1 && !st7.notes.empty() && contains(st7.notes[0], "no builder release"), "unexplained zone record dropped");
	}

	void test_talent_era(const fs::path& folder)
	{
		const auto t = make_tables(folder, "", true);
		std::vector<const EraTables*> eras;
		for (const auto& e : t->eras) eras.push_back(&e);
		std::vector<std::pair<std::int64_t, std::int64_t>> unexplained;
		const auto* e1 = detect_talent_era(make_old_save({0, 2, 3}), eras, t->reference, unexplained);
		check(e1 && e1->cv == 5 && unexplained.empty(), "talent era: the reading that keeps the ranks");
		const auto* e2 = detect_talent_era(make_old_save({0, 3, 4}), eras, t->reference, unexplained);
		check(e2 && e2->cv == 13, "talent era: both explain it, the newest wins");

		// Invented analogue of restore slot 16 being mistaken for stats index 17.
		EraTables reference, newer, older;
		reference.STAT.resize(22);
		for (u32 i = 1; i < 22; ++i) reference.STAT[i] = make_id(1, i);
		newer.cv = 13; older.cv = 12;
		newer.TALN_shared.resize(61); older.TALN_shared.resize(61);
		const std::vector<u32> ids{1, 9, 10, 59, 60};
		newer.holdable[reference.STAT[17]] = ids;
		older.holdable[reference.STAT[16]] = ids;
		OldSlot old;
		old.heroes.resize(21); old.restore.resize(22);
		auto& a = old.restore[16].attributes;
		a.tail.count = static_cast<u32>(ids.size());
		for (std::size_t i = 0; i < ids.size(); ++i) a.talents[i].id = static_cast<u16>(ids[i]);
		std::vector<const EraTables*> bank_eras{&newer, &older};
		const auto* bank = detect_talent_era(old, bank_eras, reference, unexplained);
		check(bank && bank->cv == 12 && unexplained.empty(), "restore slot 16 uses stats index 16");
		old.heroes[15].attributes = a;
		bank = detect_talent_era(old, bank_eras, reference, unexplained);
		check(bank && bank->cv == 12 && unexplained.empty(), "hero block 15 and restore 16 share ownership");

	}

	// Rule 52 part64: the loader replaces only rebuilt definitions' own lists.
	void test_rebuilt_own(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false);
		Tables tables; tables.data = t;
		auto model = make_old_save();
		const auto reward = instance(0, {{4, 0x3f400000}, {5, 0x3e800000}}, {{1, 0x3f000000}}, {{2, 0x3f800000}}, 0, 0, 0, 5);
		model.carried[2] = reward; // special name, location=stash (5); quantity belongs to DEFSTATE
		model.world[0].instance = reward;
		model.restore[1].equipment.equipped = {reward};
		model.items.def_state[0] = 7;
		model.items.def_state[1] = 9; // obtained bit plus another state bit
		const auto converted = convert_old_to_new(write_slot(model), tables);
		check(converted.ok, "rebuilt synthetic old save imports");
		if (!converted.ok) return;
		const auto plain = new_to_old_stream(converted.bytes, tables);
		check(plain.ok && !item_own_rebuilt_enabled(), "rebuilt migration defaults OFF");
		if (!plain.ok) return;
		auto* entry = const_cast<CatEntry*>(t->catalogue.entry_of(t->build->T.ITEM[0]));
		entry->attributes = ITEM_OWN_REBUILT;
		t->build->T.ITEM_own[0] = {t->build->T.ENHC[5], t->build->T.ENHC[4]}; // native slot order, not sorted
		const auto disabled = new_to_old_stream(converted.bytes, tables);
		check(disabled.ok && disabled.bytes == plain.bytes, "catalogue attribute without verified rule52 is byte-identical OFF");
		static bool verified = false;
		set_xml1_economy_verifier(+[] { return verified; });
		check(!item_own_rebuilt_enabled() && new_to_old_stream(converted.bytes, tables).bytes == plain.bytes,
			"requested but unverified rule52 keeps exact loader bytes");
		verified = true;
		const auto migrated = new_to_old_stream(converted.bytes, tables);
		check(migrated.ok && item_own_rebuilt_enabled(), "verified rule52 enables rebuilt migration");
		auto expected = read_slot(plain.bytes.data(), plain.bytes.size());
		unsigned count = 0;
		auto rebuild = [&](OldInstance& i)
		{
			if (i.head.definition == 0) { i.lists[1] = {{5, 0}, {4, 0}}; ++count; }
		};
		for (auto& i : expected.carried) rebuild(i);
		for (auto& w : expected.world) rebuild(w.instance);
		for (auto& h : expected.heroes) for (auto& i : h.equipment.equipped) rebuild(i);
		for (auto& r : expected.restore) for (auto& i : r.equipment.equipped) rebuild(i);
		check(count == 4 && migrated.bytes == write_slot(expected),
			"four rebuilt instances: equipped/carried/stash/world/restore, native slot order; affixes/names/location/quantity/state/obtained and all other bytes kept");
		if (migrated.ok)
		{
			NewFileContext context; context.description = "desc";
			const auto saved = old_stream_to_new(std::vector<u8>(migrated.bytes.begin() + old_envelope, migrated.bytes.end()), tables, context);
			const auto loaded = new_to_old_stream(saved.bytes, tables);
			check(saved.ok && loaded.ok && loaded.bytes == migrated.bytes, "rebuilt load-save-load fixed point");
		}
		entry->attributes = 2; // unrelated catalogue attribute
		check(new_to_old_stream(converted.bytes, tables).bytes == plain.bytes, "unmarked legacy equipment is byte-identical with economy verified");
		entry->attributes = ITEM_OWN_REBUILT;
		t->build->T.ITEM_own[0] = {t->build->T.ENHC[6]}; // old own records no longer belong to this definition
		check(new_to_old_stream(converted.bytes, tables).ok, "replace old own list before validating current definition ownership");
		t->build->T.ITEM_own[0].clear();
		const auto empty = new_to_old_stream(converted.bytes, tables);
		check(empty.ok && read_slot(empty.bytes.data(), empty.bytes.size()).carried[2].lists[1].empty(),
			"rebuilt zero-record definition also clears special item's old own list");
		t->build->T.ITEM_own[0].assign(8, t->build->T.ENHC[4]);
		const auto eight = new_to_old_stream(converted.bytes, tables);
		check(eight.ok && read_slot(eight.bytes.data(), eight.bytes.size()).carried[2].lists[1].size() == 8, "all eight native own slots allowed");
		t->build->T.ITEM_own[0].push_back(t->build->T.ENHC[4]);
		const auto nine = new_to_old_stream(converted.bytes, tables);
		check(!nine.ok && nine.bytes.empty() && contains(nine.error, "8 own records"), "oversized rebuilt own list refuses without output");
		t->build->T.ITEM_own[0] = {0x03ffffff};
		const auto unknown = new_to_old_stream(converted.bytes, tables);
		check(!unknown.ok && unknown.bytes.empty() && contains(unknown.error, "enhancement"), "unmapped rebuilt record refuses without output");
		t->build->T.ITEM_own[0] = {t->build->T.ENHC[5], t->build->T.ENHC[4]};
		verified = false;
		check(!item_own_rebuilt_enabled() && new_to_old_stream(converted.bytes, tables).bytes == plain.bytes,
			"live verification loss immediately restores byte-identical OFF behavior");
		set_xml1_economy_verifier(nullptr);
		std::printf("rebuilt synthetic: 4 migrated instances, 14 checks\n");
	}

	void test_rebuilt_removed_records(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false); Tables tables; tables.data = t;
		const auto converted = convert_old_to_new(write_slot(make_old_save()), tables);
		check(converted.ok, "removed-record fixture imports before definition rebuild");
		if (!converted.ok) return;
		const auto source = read_file(converted.bytes.data(), converted.bytes.size(), kind_campaign);
		auto item = source.data("ITEM"); auto instances = item["instances"];
		auto obsolete = Value::list();
		obsolete.push(Value::dict({{"enhancement", Value::integer(0x03ffffff)}, {"value", Value::integer(0x3f400000)}}));
		instances.as_list()[2].set("own", obsolete); item.set("instances", instances);
		const auto input = rewrite_file(source, "ITEM", source.sections.at("ITEM").entry.minor, encode_section("ITEM", item));
		auto* entry = const_cast<CatEntry*>(t->catalogue.entry_of(t->build->T.ITEM[0])); entry->attributes = ITEM_OWN_REBUILT;
		set_xml1_economy_verifier(+[] { return true; });
		t->build->T.ITEM_own[0] = {t->build->T.ENHC[5], t->build->T.ENHC[4]};
		const auto shrunk = new_to_old_stream(input, tables);
		check(shrunk.ok && read_slot(shrunk.bytes.data(), shrunk.bytes.size()).carried[2].lists[1].size() == 2,
			"rebuilt shortened own list replaces an unmapped saved record before resolution");
		t->build->T.ITEM_own[0].clear();
		const auto empty = new_to_old_stream(input, tables);
		check(empty.ok && read_slot(empty.bytes.data(), empty.bytes.size()).carried[2].lists[1].empty(),
			"rebuilt zero-record definition ignores its removed saved own record");
		instances.as_list()[2].set("prefixes", obsolete); item.set("instances", instances);
		const auto bad_prefix = rewrite_file(source, "ITEM", source.sections.at("ITEM").entry.minor, encode_section("ITEM", item));
		check(!new_to_old_stream(bad_prefix, tables).ok, "unmapped prefixes still refuse on rebuilt items");
		entry->attributes = 0;
		check(!new_to_old_stream(input, tables).ok, "unmarked item still refuses an unmapped own record");
		entry->attributes = ITEM_OWN_REBUILT; set_xml1_economy_verifier(nullptr);
		check(!new_to_old_stream(input, tables).ok, "rule52 OFF still refuses an unmapped saved own record");
		std::printf("rebuilt removed records: 6 checks\n");
	}

	void test_rebuilt_equipment_class(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false); Tables tables; tables.data = t;
		auto model = make_old_save(); model.carried.assign(20, instance(2));
		model.heroes[0].equipment.equipped = {instance(0, {{4, 7}}, {{1, 9}}, {{2, 5}}, 0xff, 0xff, 0xff, 1),
			instance(1, {{6, 3}}, {}, {}, 0xff, 0xff, 0xff, 1), instance(3, {{7, 1}}, {}, {}, 0xff, 0xff, 0xff, 1)};
		model.restore[1].equipment.equipped = {instance(0, {{4, 7}}, {}, {}, 0xff, 0xff, 0xff, 1)};
		for (const unsigned d : {0u, 1u, 3u}) { model.items.def_state[2*d] = 7; model.items.def_state[2*d+1] = 9; }
		const auto converted = convert_old_to_new(write_slot(model), tables);
		check(converted.ok, "class-change fixture imports with legacy belt/gloves/armor"); if (!converted.ok) return;
		const auto baseline = new_to_old_stream(converted.bytes, tables); check(baseline.ok, "class-change baseline loads"); if (!baseline.ok) return;
		for (const unsigned d : {0u, 1u, 3u}) const_cast<CatEntry*>(t->catalogue.entry_of(t->build->T.ITEM[d]))->attributes = ITEM_OWN_REBUILT;
		t->build->T.ITEM_cat[0] = t->build->T.ITEM_cat[3] = 2; // historical belt and armor now gloves
		t->catalogue.revision = 8; // actual catalogue that first marks the 19 rebuilt rewards
		const auto off = new_to_old_stream(converted.bytes, tables);
		check(off.ok && off.bytes == baseline.bytes, "rule52 OFF keeps worn reward bytes unchanged");
		set_xml1_economy_verifier(+[] { return true; });
		const auto moved = new_to_old_stream(converted.bytes, tables);
		check(moved.ok, "class-change migration loads without loss");
		if (moved.ok)
		{
			const auto loaded = read_slot(moved.bytes.data(), moved.bytes.size());
			check(loaded.carried.size() == 23 && loaded.heroes[0].equipment.equipped.size() == 1 &&
				loaded.heroes[0].equipment.equipped[0].head.definition == 1 && loaded.restore[1].equipment.equipped.empty(),
				"legacy belt/armor and restore reward move to carried even above twenty; fitting gloves stay equipped");
			check(loaded.carried.size() == 23 && loaded.carried[20].head.definition == 0 && loaded.carried[21].head.definition == 3 &&
				loaded.carried[22].head.definition == 0 && loaded.carried[20].head.location == 2 &&
				loaded.carried[21].head.location == 2 && loaded.carried[22].head.location == 2,
				"relocated instances retain order and gain carried location 2");
			check(std::equal(std::begin(loaded.items.def_state), std::end(loaded.items.def_state), std::begin(model.items.def_state)) && loaded.carried.size() == 23 &&
				loaded.carried[20].lists[0].size() == 1 && loaded.carried[20].lists[0][0].value == 9 &&
				loaded.carried[20].lists[2].size() == 1 && loaded.carried[20].lists[2][0].value == 5,
				"relocation preserves quantity/obtained/state and both affixes");
			std::size_t notes = 0, at = 0; while ((at = moved.ledger.find("moved to carried", at)) != std::string::npos) { ++notes; at += 16; }
			check(notes == 3, "one relocation ledger note per moved instance");
			NewFileContext context; context.description = "desc";
			const auto saved = old_stream_to_new(moved.bytes, tables, context);
			const auto again = new_to_old_stream(saved.bytes, tables);
			check(saved.ok && again.ok && again.bytes == moved.bytes && again.ledger.find("moved to carried") == std::string::npos,
				"class relocation load-save-load fixed point has no repeated notes");
			const auto imported = convert_old_to_new(write_slot(model), tables);
			const auto imported_loaded = new_to_old_stream(imported.bytes, tables);
			check(imported.ok && imported_loaded.ok && read_slot(imported_loaded.bytes.data(), imported_loaded.bytes.size()).carried.size() == 23,
				"retail import against catalogue8 still recovers historical worn classes without importer changes");
			if (loaded.carried.size() < 23) { set_xml1_economy_verifier(nullptr); return; } // keep the failing vector safe
			// A current-catalogue save can equip the rebuilt gloves normally; later catalogue growth must not undo it.
			auto current = loaded; current.heroes[0].equipment.equipped = {current.carried[20]};
			current.heroes[0].equipment.equipped[0].head.location = 1; current.carried.erase(current.carried.begin()+20);
			const auto equipped = old_stream_to_new(write_slot(current), tables, context);
			t->catalogue.revision = 9;
			const auto equipped_again = new_to_old_stream(equipped.bytes, tables);
			check(equipped.ok && equipped_again.ok &&
				read_slot(equipped_again.bytes.data(), equipped_again.bytes.size()).heroes[0].equipment.equipped.size() == 1 &&
				equipped_again.ledger.find("moved to carried") == std::string::npos,
				"catalogue8 and later saves keep newly equipped rebuilt gloves, including after catalogue growth");
		}
		set_xml1_economy_verifier(nullptr);
		std::printf("rebuilt equipment class: 11 checks\n");
	}

	// the API on the synthetic tables: missing talents, the saver's direction, dump of both formats
	void test_api(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false);
		auto* b = const_cast<Build*>(t->build.get());
		b->herostat_talents[1] = {talent_code(2, 0), talent_code(2, 1), talent_code(2, 2), 0, 3, 4, 1};   // engine ids 200..202 at M = 100
		b->herostat_talents[2] = {talent_code(3, 0), talent_code(3, 1), 0, 3, 4, 1};

		Tables tables;
		tables.data = t;
		const auto raw = write_slot(make_old_save({0, 3, 4}, 10));
		std::vector<TalentAdd> adds;
		check(missing_talents(raw, tables, adds), "missing_talents runs");
		std::vector<std::pair<int, int>> got;
		for (const auto& a : adds) got.emplace_back(a.hero_block, a.talent_id);
		check(got == std::vector<std::pair<int, int>>{{0, 201}, {0, 1}, {1, 301}, {1, 3}, {1, 4}, {1, 1}}, "missing talents by hero, in herostat order");
		check(adds.size() == 6 && !adds[0].shared && adds[1].shared && adds[0].stats_index == 1 && adds[0].rank == 0 && adds[0].base == 0, "TalentAdd fields");
		const auto r = convert_old_to_new(raw, tables);
		check(r.ok && r.refused == 0 && !r.bytes.empty() && contains(r.ledger, "converted; start era 13"), "convert_old_to_new");
		const auto back = new_to_old_stream(r.bytes, tables);
		check(back.ok && back.bytes.size() == old_slot_size, "new_to_old_stream");
		if (back.ok && back.bytes.size() == old_slot_size)
		{
			// the loader's stream (no template) gives every power slot its flag byte (the 21st byte, bit 0 = usable: without
			// it the game refuses the power, 0x4b83b0 / 0x4fd4de); the names stay as they were
			const auto loaded = read_slot(back.bytes.data(), back.bytes.size());
			const auto& p = loaded.heroes[0].powers.power;
			check(p[0].flags == 1 && p[1].flags == 1 && p[2].flags == 1 && p[3].flags == 1 &&
				cstr(p[0].name) == "power1" && cstr(p[1].name).empty() && cstr(p[2].name) == "power3",
				"the loader's power slots are usable, names unchanged");
		}
		{
			// the game's own path with a flag-0 slot: saver (old stream -> new) then loader (new -> old stream) keep it at 0
			const auto raw0 = write_slot(make_old_save({0, 3, 4}, 10, true, 3, {1, 1, 0, 1}));
			NewFileContext c0;
			c0.description = "desc";
			c0.has_conversation_digest = true;
			std::string digest_error;
			Hash digest{};
			check(conversation_digest(tables, "zone/x/y", digest, digest_error), "flag-0 fixture digest");
			std::copy(digest.begin(), digest.end(), c0.conversation_digest);
			const auto s0 = old_stream_to_new(std::vector<std::uint8_t>(raw0.begin() + old_envelope, raw0.end()), tables, c0);
			check(s0.ok, "saver with a flag-0 slot (" + s0.error + ")");
			if (s0.ok)
			{
				const auto l0 = new_to_old_stream(s0.bytes, tables);
				check(l0.ok && l0.bytes.size() == old_slot_size, "loader with a flag-0 slot");
				if (l0.ok && l0.bytes.size() == old_slot_size)
				{
					const auto q0 = read_slot(l0.bytes.data(), l0.bytes.size());
					const auto& q = q0.heroes[0].powers.power;
					check(q[0].flags == 1 && q[1].flags == 1 && q[2].flags == 0 && q[3].flags == 1 && cstr(q[2].name) == "power3",
						"save -> load keeps slot 2's flag at 0");
				}
			}
		}
		NewFileContext ctx;
		ctx.description = "desc";
		ctx.saved_at = 5;
		ctx.campaign_serial = 77;
		ctx.save_sequence = 3;
		const auto saved = old_stream_to_new(std::vector<std::uint8_t>(back.bytes.begin() + old_envelope, back.bytes.end()), tables, ctx);
		check(saved.ok, "old_stream_to_new (" + saved.error + ")");
		if (saved.ok)
		{
			const auto f = read_file(saved.bytes.data(), saved.bytes.size(), kind_campaign);
			check(f.header.campaign_serial == 77 && f.header.save_sequence == 3 && f.data("META")["origin"].as_int() == 0 && !f.sections.count("LGCY"),
				"saver header and META");
			check((f.header.optional_features & ~1u) == 0, "the saver sets no optional bit of its own but KEYS");
		}
		{
			// the caller's optional-feature bits (the fifth-level catch-up's 2 and 4) reach the header beside KEYS, the
			// reader ignores them, and the loader's stream is the same with or without them
			NewFileContext marked = ctx;
			marked.optional_features = 6;
			const auto with_bits = old_stream_to_new(std::vector<std::uint8_t>(back.bytes.begin() + old_envelope, back.bytes.end()), tables, marked);
			check(with_bits.ok && saved.ok, "old_stream_to_new with optional bits (" + with_bits.error + ")");
			if (with_bits.ok && saved.ok)
			{
				const auto f = read_file(with_bits.bytes.data(), with_bits.bytes.size(), kind_campaign);
				const auto plain = read_file(saved.bytes.data(), saved.bytes.size(), kind_campaign);
				check(f.header.optional_features == (6u | (plain.header.optional_features & 1u)), "optional bits 1 and 2 written beside KEYS");
				const auto l1 = new_to_old_stream(with_bits.bytes, tables), l2 = new_to_old_stream(saved.bytes, tables);
				check(l1.ok && l2.ok && l1.bytes == l2.bytes, "optional bits change nothing the loader rebuilds");
			}
			if (r.ok)
			{
				const auto imported = read_file(r.bytes.data(), r.bytes.size(), kind_campaign);
				check((imported.header.optional_features & 6u) == 0, "an import of an old save vouches for no fifth-level points");
			}
		}
		{
			// xpexempt by stats index, from the build's herostat (invented flags)
			b->herostat_xpexempt.assign(b->stat.size(), 0);
			b->herostat_xpexempt[2] = 1;
			check(!tables.xp_exempt(0) && !tables.xp_exempt(1) && tables.xp_exempt(2) && !tables.xp_exempt(100000), "xp_exempt by stats index");
			check(tables.stats_key(1) == b->stat[1] && tables.stats_key(100000).empty(), "stats_key by stats index");
			b->herostat_autospend.assign(b->stat.size(), std::string());
			b->herostat_autospend[1] = "class_x";
			check(tables.hero_autospend(1) == "class_x" && tables.hero_autospend(2).empty() && tables.hero_autospend(100000).empty(), "hero_autospend by stats index");
			Tables none;
			check(!none.xp_exempt(2) && none.stats_key(1).empty(), "no tables: nothing exempt, no key");
		}

		// Durable provenance must survive two runtime resaves, including opaque KEYS references.
		if (r.ok)
		{
			const auto original = read_file(r.bytes.data(), r.bytes.size());
			check(original.data("ZONE")["records"].as_list()[0]["capture_flags"].as_int() == 2, "legacy capture marked unknown");

			for (int kind = 0; kind < 4; ++kind)
			{
				std::vector<SectionOut> sections;
				for (const auto& tag : original.order)
				{
					const auto& section = original.sections.at(tag);
					Value data = section.data;
					if (kind == 0 && tag == "ITEM") { auto list = data["def_state"]; list.l[0].set("quantity", Value::integer(256)); data.set("def_state", list); }
					if (kind == 1 && tag == "MISS") data.set("stack_depth", Value::integer(3));
					if (kind >= 2 && tag == "ZONE") { auto records = data["records"]; auto states = records.l[0]["states"]; if (kind == 2) { Value position = Value::list(); position.push(Value::real(40000)); position.push(Value::real(0)); position.push(Value::real(0)); states.l[0].set("position", position); } else states.l[0].set("def_name", Value::integer(999)); records.l[0].set("states", states); data.set("records", records); }
					sections.push_back({tag, section.entry.major, section.entry.minor, section.entry.flags, data});
				}
				check(!new_to_old_stream(write_file(kind_campaign, sections, HeaderIn{}), tables).ok, "native malformed representability refused " + std::to_string(kind));
			}
			std::vector<SectionOut> retained;
			for (const auto& tag : original.order)
			{
				const auto& sec = original.sections.at(tag);
				Value data = sec.data;
				if (tag == "KEYS") { auto keys = data["keys"]; std::reverse(keys.l.begin(), keys.l.end()); data.set("keys", std::move(keys)); }
				retained.push_back({tag, sec.entry.major, sec.entry.minor, sec.entry.flags, data});
			}
			const std::string opaque("opaque\x03\0\0\x80", 10);
			retained.push_back({"FUTR", 1, 0, section_keep, Value::bytes(opaque), 4});
			HeaderIn h;
			ctx.retained_file = write_file(kind_campaign, retained, h);
			const auto prefix = read_file(ctx.retained_file.data(), ctx.retained_file.size()).data("KEYS")["keys"];
			for (int pass = 0; pass < 2; ++pass)
			{
				const auto round = old_stream_to_new(raw, tables, ctx);
				check(round.ok, "retained campaign resave " + round.error);
				if (!round.ok) break;
				const auto f = read_file(round.bytes.data(), round.bytes.size());
				check(f.sections.at("FUTR").payload == opaque && f.sections.at("FUTR").entry.records == 4, "opaque KEEP byte/record preservation");
				check(f.sections.at("LGCY").payload == original.sections.at("LGCY").payload, "LGCY preserved");
				check(f.data("KEYS")["keys"] == prefix, "retained KEYS prefix pinned");
				ctx.retained_file = round.bytes;
			}
			ctx.require_capture_metadata = true;
			check(!old_stream_to_new(raw, tables, ctx).ok, "fresh capture without observed metadata refused");
			std::vector<ZoneCaptureMetadata> captures;
			std::string error;
			check(loaded_zone_metadata(r.bytes, tables, captures, error), "loaded capture metadata " + error);
			ctx.zone_captures = captures;
			check(old_stream_to_new(raw, tables, ctx).ok, "cached capture metadata accepted");

			// Actual native ordinals may skip a factory; metadata identifies the actual placement, including a dead entity.
			if (!captures.empty() && captures[0].entities.size() >= 2)
			{
				auto observed = captures;
				observed[0].capture_flags = 0;
				observed[0].conversation_digest = t->reference.conversation_digest;
				observed[0].entities[0].ordinal = 2;
				observed[0].entities[1].ordinal = 1;
				auto native = make_old_save({0, 3, 4}, 10);
				native.zones[0].tail.ordinal_map[0] = native.zones[0].tail.ordinal_map[1];
				native.zones[0].tail.ordinal_map[1] = 0;
				ctx.zone_captures = observed;
				const auto actual = old_stream_to_new(write_slot(native), tables, ctx);
				check(actual.ok, "observed placement capture " + actual.error);
				if (actual.ok)
				{
					const auto restored = new_to_old_stream(actual.bytes, tables);
					check(restored.ok, "observed placement reverse " + restored.error);
					if (restored.ok)
					{
						const auto dead = read_slot(restored.bytes.data(), restored.bytes.size());
						const auto slot = dead.zones[0].tail.ordinal_map[0];
						check(slot != 0 && !(dead.zones[0].states[slot].flags_b & 8), "dead entity tombstone remains dead after placement remap");
						check(loaded_zone_metadata(actual.bytes, tables, ctx.zone_captures, error), "cached observed metadata read");
						for (int pass = 0; pass < 2; ++pass)
						{
							const auto cached = old_stream_to_new(restored.bytes, tables, ctx);
							check(cached.ok && read_file(cached.bytes.data(), cached.bytes.size()).data("ZONE")["records"].as_list()[0]["conversation_digest"].as_bytes() == std::string(reinterpret_cast<const char*>(captures[0].conversation_digest.data()), 32), "cached digest rebased after native reconstruction");
						}
					}
				}
			}
			ConvertOptions options;
			options.original_name = "settings.dat";
			const auto profile = convert_old_profile_to_new(write_profile_file("synthetic", make_old_save({0, 3, 4}, 10).profile), tables, options);
			check(profile.ok, "synthetic profile import " + profile.error);
			if (profile.ok)
			{
				const auto pf = read_file(profile.bytes.data(), profile.bytes.size());
				check(pf.sections.count("LGCY") == 1, "profile import provenance present");
				ctx.retained_file = profile.bytes;
				const auto ps = old_profile_to_new(write_profile_file("synthetic", make_old_save({0, 3, 4}, 10).profile), tables, ctx);
				check(ps.ok && read_file(ps.bytes.data(), ps.bytes.size()).sections.at("LGCY").payload == pf.sections.at("LGCY").payload,
					"profile import provenance retained");
			}
		}
		check(!dump(raw, tables).empty() && !dump(r.bytes, tables).empty(), "dump of both formats");
		std::string err;
		check(dump(std::vector<std::uint8_t>(10, 0), tables, err).empty() && !err.empty(), "dump of garbage is an error");
		const auto refused = convert_old_to_new(std::vector<std::uint8_t>(100, 0), tables);
		check(!refused.ok && refused.refused == 1 && contains(refused.ledger, "refused: file size"), "refusal result");
	}
	void test_capture_twins(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false);
		Tables tables; tables.data = t;
		const auto* model = t->build->zone_numbering("zone/x/y");
		check(model != nullptr, "same-name twin numbering exists");
		if (!model) return;
		auto twins = *model;
		twins.names[1] = twins.names[0];
		t->build->zones["zone/x/y"] = twins; // synthetic same-name definitions/instances, distinct document positions
		auto native = make_old_save({0, 3, 4}, 10);
		auto& z = native.zones[0];
		z.states[2].inst_name = z.states[1].inst_name;
		z.tail.ordinal_map[0] = 2; z.tail.ordinal_map[1] = 3; z.tail.ordinal_map[2] = 0;
		ZoneCaptureMetadata capture;
		capture.zone = "zone/x/y"; capture.time_stamp = z.tail.time_stamp; capture.capture_flags = 0;
		capture.placement_sha1 = twins.sha1;
		std::string error;
		check(conversation_digest(tables, "zone/x/y", capture.conversation_digest, error), "current conversation digest");
		capture.entities = {{1, 1, "gate", "gate01"}, {2, 3, "crate_pinned", "crate02"}};
		NewFileContext context; context.require_capture_metadata = true; context.zone_captures = {capture};
		const auto saved = old_stream_to_new(write_slot(native), tables, context);
		check(saved.ok, "same-name twin capture after skipped factory " + saved.error);
		if (!saved.ok) return;
		const auto restored = new_to_old_stream(saved.bytes, tables);
		check(restored.ok, "same-name twin restore " + restored.error);
		if (!restored.ok) return;
		const auto back = read_slot(restored.bytes.data(), restored.bytes.size());
		const auto& zone = back.zones[0];
		check(zone.tail.ordinal_map[0] == 0 && zone.tail.ordinal_map[1] != 0 && zone.states[zone.tail.ordinal_map[1]].payload[0] == 0x11,
			"same-name twins select actual second placement, not inferred first");
		std::vector<ZoneCaptureMetadata> cached;
		check(loaded_zone_metadata(saved.bytes, tables, cached, error) && !cached.empty() && cached[0].entities[0].ordinal == 2,
			"same-name twin cache ordinal matches translated native map");
	}

	void test_zone_prefix_review(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false);
		Tables tables; tables.data = t;
		const auto imported = convert_old_to_new(write_slot(make_old_save()), tables);
		check(imported.ok, "prefix review fixture imports");
		if (!imported.ok) return;
		auto current = *t->build->zone_numbering("zone/x/y");
		ZonePrefix prefix;
		prefix.zone = key_hash1("zone/x/y", Enc::cp1252);
		prefix.old = current.sha1;
		prefix.current.fill(7);
		prefix.digest.fill(9);
		prefix.placements = current.placement;
		current.sha1 = prefix.current;
		current.prefix_digests[prefix.placements.size()] = prefix.digest;
		current.names.emplace_back(key_hash1("tail", Enc::latin1), key_hash1("tail01", Enc::latin1));
		current.name_text.emplace_back("tail", "tail01");
		current.placement.push_back(99);
		t->build->zone_prefixes = {prefix};
		t->build->zones["zone/x/y"] = current;
		const std::string old(reinterpret_cast<const char*>(prefix.old.data()), prefix.old.size());
		check(t->build->zone_numbering("ZoNe/X/Y") == t->build->zone_numbering("zone/x/y") &&
			t->build->zone_prefix("ZoNe/X/Y", old, current) != nullptr, "mixed-case numbering and prefix lookup agree");
		prefix.zone = key_hash1("zone/\xe9tage", Enc::cp1252);
		t->build->zone_prefixes = {prefix};
		check(t->build->zone_prefix("ZONE/\xc9tage", old, current) != nullptr, "extended mixed-case prefix key uses numbering normalization");
		prefix.zone = key_hash1("zone/x/y", Enc::cp1252);
		t->build->zone_prefixes = {prefix};
		std::vector<ZoneCaptureMetadata> metadata;
		std::string error;
		check(loaded_zone_metadata(imported.bytes, tables, metadata, error) && metadata.size() == 1 && metadata[0].fresh_entities.size() == 1,
			"valid prefix metadata control: " + error);
		// Synthetic inconsistent cache: reverse still reconstructs the old prefix,
		// but fresh metadata lacks text. Discard instead of throwing out_of_range.
		t->build->zones["zone/x/y"]->name_text.resize(prefix.placements.size());
		check(loaded_zone_metadata(imported.bytes, tables, metadata, error) && metadata.empty() && error.empty(),
			"short fresh metadata discards cleanly");
		t->build->zones["zone/x/y"] = std::nullopt;
		check(!loaded_zone_metadata(imported.bytes, tables, metadata, error) && metadata.empty() && contains(error, "has no placement file"),
			"missing numbering returns a clean error before metadata dereference");
	}

	void test_native_transfer_metadata(const fs::path& folder)
	{
		auto t=make_tables(folder,"sh_might",false);Tables tables;tables.data=t;
		auto native=make_old_save({0,3,4},10);native.zones[0].tail.conversation[0]=1;std::memset(native.zones[0].states[1].payload,0xa5,8);std::memset(native.zones[0].tail.fog,0x55,old_fog_bytes);
		const auto raw=write_slot(native);
		for(int envelope=0;envelope<2;++envelope)
		{
			const std::vector<std::uint8_t> stream(raw.begin()+(envelope?old_envelope:0),raw.end());
			std::vector<ZoneCaptureMetadata> captures;std::string error;
			const bool ok=native_zone_metadata(stream,tables,captures,error);
			check(ok&&captures.size()==1,"native transfer metadata available before restore: "+error);if(!ok||captures.empty())continue;
			check(captures[0].capture_flags==2&&captures[0].conversation_digest==Hash{}&&captures[0].entities[0].ordinal==1,"native provenance inferred, not relabeled observed");
			NewFileContext context;context.zone_captures=captures;context.require_capture_metadata=true;
			const auto saved=old_stream_to_new(stream,tables,context);check(saved.ok,"native transfer recording with inferred associations: "+saved.error);
			if(saved.ok){const auto loaded=new_to_old_stream(saved.bytes,tables);check(loaded.ok,"native transfer with nonzero heard marks reloaded");check(contains(loaded.ledger,"conversation provenance unknown: heard marks dropped"),"native transfer heard reset is explained");if(loaded.ok){const auto m=read_slot(loaded.bytes.data(),loaded.bytes.size());const auto& z=m.zones[0];check(z.tail.conversation[0]==0&&z.states[z.tail.ordinal_map[0]].payload[0]==0xa5&&z.tail.fog[0]==0x55,"door/object payload and fog survive native transfer");}}
			check(write_slot(native)==raw,"native metadata extraction does not mutate source");
		}
		native.zones[0].states[1].def_name=native.zones[0].states[3].def_name;
		std::vector<ZoneCaptureMetadata> captures;std::string error;
		check(!native_zone_metadata(write_slot(native),tables,captures,error)&&captures.empty(),"unsupported native numbering refuses before application");
		for(const auto stamp : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
		{
			auto invalid=read_slot(raw.data(),raw.size());invalid.zones[0].tail.time_stamp=stamp;
			check(!native_zone_metadata(write_slot(invalid),tables,captures,error)&&captures.empty(),"nonfinite inferred timestamp refuses before native application");
		}
		{
			auto invalid=read_slot(raw.data(),raw.size());invalid.zones.push_back(invalid.zones[0]);
			check(!native_zone_metadata(write_slot(invalid),tables,captures,error)&&captures.empty(),"duplicate inferred zone refuses before native application");
		}
		// A regular file cannot be walked as a conversation directory. Metadata extraction must not touch it.
		std::ofstream(folder/"Conversations")<<"synthetic non-directory";
		check(native_zone_metadata(raw,tables,captures,error)&&captures.size()==1,"native transfer avoids conversation hashing on the join path");
	}

	void test_native_dangling_world_state(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false); Tables tables; tables.data = t;
		const auto base = make_old_save({0,3,4},10);
		const auto imported = convert_old_to_new(write_slot(base),tables);
		check(imported.ok,"dangling world fixture import");if(!imported.ok)return;
		NewFileContext context;context.require_capture_metadata=true;std::string error;
		check(loaded_zone_metadata(imported.bytes,tables,context.zone_captures,error),"dangling fixture metadata");
		for(int scenario=0;scenario<4;++scenario)
		{
			auto native=make_old_save({0,3,4},10);native.world.clear();
			for(u32 h=0;h<13;++h)native.world.push_back({h,instance(3,{{7,1}})});
			auto& z=native.zones[0];auto& state=z.states[scenario>=2?1:4];
			state.payload[0]=13;state.payload[1]=1;for(int k=2;k<8;++k)state.payload[k]=0;
			state.flags_a=1;state.flags_b=scenario==3?15:7;
			z.tail.dynamic_count=scenario==1?1:0;z.tail.smart_count=scenario==0?1:0;z.tail.smart[0]=4;
			const auto result=old_stream_to_new(write_slot(native),tables,context);
			if(scenario==3){check(!result.ok&&result.bytes.empty(),"live numbered dangling reference still refuses");continue;}
			if(scenario==2)
			{
				check(result.ok,"dead numbered missing item preserves removed identity: "+result.error);
				check(contains(result.ledger,"dead numbered item")&&contains(result.ledger,"slot 13"),"dead item normalization ledger");
				if(result.ok){const auto reverse=new_to_old_stream(result.bytes,tables);check(reverse.ok,"dead item tombstone reloads");if(reverse.ok){const auto m=read_slot(reverse.bytes.data(),reverse.bytes.size());const auto& rz=m.zones[0];const auto& dead=rz.states[rz.tail.ordinal_map[0]];check(rz.tail.ordinal_map[0]&&!(dead.flags_b&8)&&!(dead.flags_a&1)&&dead.flags_b==7,"native factory still removes authored entity without missing item reference");check(rz.states[rz.tail.ordinal_map[1]].payload[0]==0x11,"neighbour remains unchanged");}}
				continue;
			}
			check(result.ok,"unnumbered dangling reference saves "+std::to_string(scenario)+": "+result.error);
			check(contains(result.ledger,"dangling world item slot 13")&&contains(result.ledger,"native load"),"dangling normalization has ledger explanation");
			if(!result.ok)continue;
			const auto legacy=convert_old_to_new(write_slot(native),tables);
			check(legacy.ok&&read_file(legacy.bytes.data(),legacy.bytes.size()).data("LGCY")["other_dropped"].as_int()==1,"legacy importer keeps its recorded drop policy");
			const auto reverse=new_to_old_stream(result.bytes,tables);check(reverse.ok,"normalized dangling state reloads");
			if(reverse.ok){const auto m=read_slot(reverse.bytes.data(),reverse.bytes.size());check(m.world.size()==13&&m.zones[0].tail.dynamic_count==0&&m.zones[0].tail.smart_count==0,"no ghost item state, live world items intact");}
		}
	}

	void test_review_resets(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false);
		Tables tables; tables.data = t;
		const auto imported = convert_old_to_new(write_slot(make_old_save({0, 3, 4}, 10)), tables);
		check(imported.ok, "review fixture import"); if (!imported.ok) return;
		fs::copy_file(folder / "Maps/zone/x/y.engb", folder / "Maps/zone/x/z.engb", fs::copy_options::overwrite_existing);
		const auto original = read_file(imported.bytes.data(), imported.bytes.size());
		for (int scenario = 0; scenario < 7; ++scenario)
		{
			std::vector<SectionOut> sections;
			for (const auto& tag : original.order)
			{
				const auto& sec = original.sections.at(tag); Value data = sec.data;
				if (tag == "ZONE")
				{
					auto records = data["records"]; auto r = records.l[0];
					if (scenario == 0 || scenario == 4) r.set("content_hash", Value::bytes(std::string(20, '\0')));
					if (scenario == 1) { auto states = r["states"]; states.l[0].set("placement_index", Value::integer(999)); r.set("states", states); }
					if (scenario == 2 || scenario == 5) r.set("capture_flags", Value::integer(1));
					if (scenario == 3)
					{
						auto states = r["states"]; auto duplicate = states.l[0];
						duplicate.set("ordinal", Value::integer(4)); duplicate.set("payload", Value::bytes(std::string(8, '\x99')));
						states.push(duplicate); r.set("states", states);
					}
					if (scenario == 4) r.set("capture_flags", Value::integer(4));
					if (scenario == 6) r.set("conversation_digest", Value::bytes(std::string(32, '\0')));
					records.l[0] = r;
					if (scenario == 5)
					{
						auto survivor = data["records"].l[0]; survivor.set("zone", Value::integer(t->build->T.ZONE[1]));
						records.push(survivor);
					}
					data.set("records", records);
				}
				if (tag == "ITEM" && scenario < 3)
				{
					auto world = data["world"]; auto unowned = world.l[0]; unowned.set("handle", Value::integer(6));
					world.push(unowned); data.set("world", world);
				}
				sections.push_back({tag, sec.entry.major, sec.entry.minor, sec.entry.flags, data});
			}
			const auto result = new_to_old_stream(write_file(kind_campaign, sections, HeaderIn{}), tables);
			if (scenario == 3 || scenario == 4)
			{
				check(!result.ok && result.bytes.empty() && contains(result.error, scenario == 3 ? "duplicate destination placement" : "unsupported capture status bits"),
				      "review refused before stream exposed " + std::to_string(scenario)); continue;
			}
			if (scenario == 6) check(result.ok && contains(result.ledger, "conversation provenance unknown: heard marks dropped"), "unknown heard marks drop with a ledger instead of refusing load");
			check(result.ok, "review reset accepted " + std::to_string(scenario) + ": " + result.error); if (!result.ok) continue;
			const auto native = read_slot(result.bytes.data(), result.bytes.size());
			if (scenario < 3) check(native.zones.empty() && native.world.size() == 1 && native.world[0].slot == 6 && contains(result.ledger, "record not applied"),
			                       "discard removes exclusively owned world item and explains reset " + std::to_string(scenario));
			if (scenario == 5) check(native.zones.size() == 1 && native.world.size() == 1 && native.world[0].slot == 5, "discard preserves shared world dependency");
		}
	}

	void test_native_table_budgets(const fs::path& folder)
	{
		for (int scenario = 0; scenario < 12; ++scenario)
		{
			auto t = make_tables(folder / std::to_string(scenario), "sh_might", false);
			Tables tables; tables.data = t;
			const auto imported = convert_old_to_new(write_slot(make_old_save({0, 3, 4}, 10)), tables);
			if (!imported.ok) { check(false, "native table budget fixture " + imported.error); continue; }
			const auto& T = t->build->T;
			if (scenario == 0) t->p_nama[T.NAMA[0]] = 32;
			if (scenario == 1) t->p_namb[T.NAMB.begin()->second[0]] = 4;
			if (scenario == 2) t->p_trit[T.TRIT[0]] = 128;
			if (scenario == 3) t->p_cost[T.COST.at(1).at(3)] = 10;
			if (scenario == 4) t->p_stat[T.STAT[4]] = 297;
			if (scenario == 5) t->build->item.resize(161, "future");
			if (scenario == 6) t->build->hero_count = 22;
			if (scenario == 7) t->build->stat.resize(298, "future");
			if (scenario == 8) t->build->T.NAMA.resize(33, 0);
			if (scenario == 9) t->build->T.NAMB.begin()->second.resize(5, 0);
			if (scenario == 10) t->build->T.TRIT.resize(129, 0);
			if (scenario == 11) t->build->T.COST[1][10] = T.COST.at(1).at(3);

			if (scenario >= 5)
			{
				const auto raw = write_slot(make_old_save({0, 3, 4}, 10));
				const auto campaign = old_stream_to_new(raw, tables, NewFileContext{});
				const auto profile = old_profile_to_new(write_profile_file("synthetic", make_old_save({0, 3, 4}, 10).profile), tables, NewFileContext{});
				check(!campaign.ok && contains(campaign.error, "native"), "native campaign saver refuses unsupported table scenario " + std::to_string(scenario));
				check(!profile.ok && contains(profile.error, "native"), "native profile saver refuses unsupported table scenario " + std::to_string(scenario));
			}
			const auto translated = new_to_old_stream(imported.bytes, tables);
			check(!translated.ok && contains(translated.error, "native"), "confirmed native table budget refuses scenario " + std::to_string(scenario) + " (" + translated.error + ")");
		}
	}

	// ------------------------------------------------------------------------------------------------ Phase A
	// tests/unit/test_lcsave_native_layout.py: the native layout descriptors, the required-feature marker and the
	// typed entity-id migration at the LC -> native boundary (PHASE-A-PLAN sections 4 and 6). Invented data only.
	struct TypedCase { const char* name; u32 saved; u32 expected; };
	// generation 0 -> null; 1, 2, 0x3fff translate; 0x4000 and above -> null; negative / non-token -> null; null stays
	const TypedCase typed_cases[12] = {
		{"trig_a", 0x45, 0}, {"last_a", 0x101, 0x401}, {"actor_q", 0x2ff, 0x8ff}, {"actor_r", 0x3fff01, 0xfffc01},
		{"actor_s", 0x400001, 0}, {"actor_t", 0x80000001u, 0}, {"base_q", 0, 0}, {"part_q", 0x7fffffff, 0},
		{"part_r", 0xffffff, 0}, {"part_s", 0x3fffff, 0xfffcff}, {"chk_q", 0x4000ff, 0}, {"chk_r", 0x205, 0x805}};
	const std::pair<const char*, u32> untyped_zone[2] = {{"counter_a", 0x101}, {"base_r", 0x3fff01}};
	const std::pair<const char*, u32> game_vars[3] = {{"gv_one", 7}, {"door_q", 0x1ff}, {"base_q", 0x101}};

	std::string typed_registry_text()
	{
		std::string text = "lcsave-typed-entity-ids 1\nfingerprint " + std::string(64, '0') + "\nfiles 3\nkey\tGame\tdoor_q\n";
		std::vector<std::string> keys;
		for (const auto& c : typed_cases) keys.push_back(c.name);
		std::sort(keys.begin(), keys.end());
		for (const auto& k : keys) text += "key\tZone\t" + k + "\n";
		return text;
	}

	void write_text_file(const fs::path& path, const std::string& content)
	{
		fs::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary).write(content.data(), static_cast<std::streamsize>(content.size()));
	}

	void set_var(OldScriptVar& v, const char* name, u32 value)
	{
		set_cstr(v.name, name);
		v.value_be[0] = static_cast<u8>(value >> 24); v.value_be[1] = static_cast<u8>(value >> 16);
		v.value_be[2] = static_cast<u8>(value >> 8); v.value_be[3] = static_cast<u8>(value);
	}

	// the synthetic retail save with the typed and untyped script variables above
	OldSlot typed_old_save()
	{
		auto m = make_old_save({0, 3, 4}, 10);
		std::size_t k = 0;
		for (const auto& c : typed_cases) set_var(m.zone_vars[k++], c.name, c.saved);
		for (const auto& [name, value] : untyped_zone) set_var(m.zone_vars[k++], name, value);
		k = 0;
		for (const auto& [name, value] : game_vars) set_var(m.game_vars[k++], name, value);
		m.game.value_608 = 0x101;                       // GAME.value_608 is never a token
		return m;
	}

	std::map<std::string, u32> vars_of(const OldScriptVar* v, u32 n)
	{
		std::map<std::string, u32> out;
		for (u32 k = 0; k < n; ++k) if (v[k].name[0]) out[cstr(v[k].name)] = script_var_value(v[k]);
		return out;
	}

	std::vector<std::uint8_t> with_required_features(std::vector<std::uint8_t> file, u32 bits)
	{
		std::memcpy(file.data() + 0x10, &bits, 4);
		const auto crc = crc32(file.data(), 0x84);
		std::memcpy(file.data() + 0x84, &crc, 4);
		return file;
	}

	OldZone widen(const OldZone& z, const NativeLayout& n)
	{
		OldZone w(n);
		w.name_handle = z.name_handle;
		w.tail = z.tail;
		w.states = z.states;
		w.states.resize(n.states);
		w.tail.ordinal_map.resize(n.ordinals);
		w.tail.dynamic.resize(n.dynamic);
		w.tail.smart.resize(n.smart);
		return w;
	}

	void test_native_layouts()
	{
		const auto& r = retail_layout;
		const auto& e = e10a2_layout;
		check(r.states == 768 && r.ordinals == 256 && r.dynamic == 300 && r.smart == 768 && r.record_count == 5 && r.record_size == 0x55e0 &&
			r.manager_size == 110556 && r.payload == 0x2fc00 && r.usable_end == 0x2fc00 && r.wrapper == 0x2fc78 && r.slot_size == 0x2fc84 &&
			r.index_bits == 8 && r.feature_bits == 0 && !r.contiguous(), "retail layout");
		check(e.states == 4096 && e.ordinals == 1024 && e.dynamic == 1024 && e.smart == 1024 && e.record_count == 5 && e.record_size == 0x19b88 &&
			e.manager_size == 0x80c24 && e.payload == 0x200000 && e.usable_end == 0x200000 && e.wrapper == 0x200078 && e.origin_trailer_bytes == 0 &&
			e.slot_size == 0x200084 && e.index_bits == 10 && e.feature_bits == 1 && e.contiguous(), "E10A2 layout");
		check(native_layout("E10A2") == &e && native_layout("RETAIL") == &r && native_layout("X") == nullptr, "native_layout by id");
		for (const auto* n : {&r, &e})
		{
			check(n->zone_string_slots == 0x800 && n->zone_arena == 0x3800 && n->fog_bytes == 0x34c, "zone strings / fog unchanged");
			check(n->state_count == 4 + n->states * 0x18 + 4 && n->ordinals_offset == n->state_count + 4, "zone record offsets");
			u64 total = 0;
			for (u32 i = 0; i < n->transfer_chunks; ++i) { const auto [off, len] = n->chunk_bounds(i); check(off == i * 200 && len <= 200 && len > 0, "chunk bounds"); total += len; }
			check(total == n->payload && n->chunk_bounds(0).second == 200 && n->chunk_bounds(n->transfer_chunks).second == 0, "chunks tile the payload");
			check(n->record_offset(4) == 4 * n->record_size && n->record_offset(5) == 0, "record offsets");
		}
		OldZone z(e);
		check(z.states.size() == 4096 && z.tail.ordinal_map.size() == 1024 && z.tail.dynamic.size() == 1024 && z.tail.smart.size() == 1024 && z.matches(e) && !z.matches(r), "OldZone sized by the layout");
		check(OldZone{}.matches(r), "OldZone defaults to retail");
		// the boundary table of the typed helpers (round6 typed_normalize), feature 0 and feature 1
		for (const u32 value : {0u, 1u, 255u, 0x101u, 0x401u, 0x501u, 0x3fff01u, 0x400001u, 0xffffffu, 0x1000001u, 0x7fffffffu, 0x80000000u, 0xffffffffu})
		{
			const u32 g8 = value >> 8, g10 = value >> 10;
			const u32 exp0 = (value > 0 && value <= 0x7fffffff && g8 >= 1 && g8 <= 0x3fff) ? ((g8 << 10) | (value & 255)) : 0;
			const u32 exp1 = (value > 0 && value <= 0x7fffffff && g10 >= 1 && g10 <= 0x3fff) ? value : 0;
			check(migrate_typed_field(value) == exp0 && validate_typed_field(value) == exp1 && validate_typed_field(migrate_typed_field(value)) == migrate_typed_field(value),
				"typed helper boundary " + std::to_string(value));
		}
	}

	void test_oldsave_layouts()
	{
		const auto& e = e10a2_layout;
		auto m = typed_old_save();
		for (auto& z : m.zones)
		{
			z = widen(z, e);
			z.tail.ordinal_map[1000] = 4;              // an ordinal only the raised layout holds
			z.states[4000] = z.states[1];
			z.tail.smart[0] = 4000; z.tail.smart_count = 1;
			z.tail.state_count = 4001;
		}
		const auto raw = write_slot(m, e);
		u32 plen = 0; std::memcpy(&plen, raw.data() + 0x80, 4);
		check(raw.size() == e.slot_size && plen == 0x200000, "E10A2 slot size and length field");
		const auto back = read_slot(raw.data(), raw.size(), e);
		check(write_slot(back, e) == raw, "E10A2 round trip");
		check(back.zones[0].tail.ordinal_map[1000] == 4 && back.zones[0].tail.smart[0] == 4000 && back.zones[0].states.size() == 4096, "E10A2 fields");
		check(contains(error_of([&] { write_slot(m, retail_layout); }), "does not match the native layout RETAIL"), "an E10A2 model is refused by the retail writer");
		check(contains(error_of([&] { write_slot(make_old_save(), e); }), "does not match the native layout E10A2"), "a retail model is refused by the E10A2 writer");
		const auto retail_raw = write_slot(make_old_save());
		check(error_of([&] { read_slot(retail_raw.data(), retail_raw.size(), e); }) ==
			"file size " + std::to_string(retail_raw.size()) + " is not the slot size " + std::to_string(e.slot_size) + " (0x84 + 0x200000)", "a retail file is refused by the E10A2 reader (size, before any copy)");
		check(error_of([&] { read_slot(raw.data(), raw.size(), retail_layout); }) ==
			"file size " + std::to_string(raw.size()) + " is not the slot size " + std::to_string(retail_layout.slot_size) + " (0x84 + 0x2fc00)", "an E10A2 file is refused by the retail reader");
		auto bad = raw;
		const u32 wrong = 0x2fc00; std::memcpy(bad.data() + 0x80, &wrong, 4);
		check(error_of([&] { read_slot(bad.data(), bad.size(), e); }) == "payload length field 0x2fc00 is not 0x200000", "the length field is checked against the layout");
		auto m2 = m; m2.zones[0].tail.dynamic_count = 1025;
		const auto raw2 = write_slot(m2, e);
		check(contains(error_of([&] { read_slot(raw2.data(), raw2.size(), e); }), "beyond the engine tables"), "counts beyond the layout's tables are refused");
	}

	void test_feature_marker()
	{
		HeaderIn h; h.writer_version = 1; h.content_version = 13; h.catalogue_revision = 1; h.campaign_serial = 42; h.save_sequence = 1;
		const std::vector<SectionOut> sec = {
			{"META", 1, 0, section_required, Value::dict({{"saved_at", Value::integer(1)}, {"description", Value::text("d")}, {"origin", Value::integer(0)},
				{"zone", Value::integer(0)}, {"act", Value::integer(1)}, {"difficulty", Value::integer(1)}, {"play_time", Value::real(1.0)},
				{"writer", Value::text("w")}, {"slot", Value::integer(0)}, {"mods", Value::list()}})},
			{"PRFO", 1, 0, 0, Value::dict({{"options", Value::integer(5)}, {"player_name", Value::text("p")}})},
			{"PRFP", 1, 0, 0, Value::dict({{"highest_level", Value::integer(3)}, {"flags", Value::integer(0)}, {"characters", Value::list()}, {"review", Value::list()}})},
			{"PRFS", 1, 0, 0, Value::dict({{"scores", Value::list()}})}};
		const auto plain = write_file(kind_profile, sec, h);
		auto marked_h = h; marked_h.required_features = feature_entity_layout_contiguous10;
		const auto marked = write_file(kind_profile, sec, marked_h);
		u32 bits = 0; std::memcpy(&bits, marked.data() + 0x10, 4);
		check(read_file(plain.data(), plain.size(), kind_profile).header.required_features == 0 && bits == 1 && plain != marked && plain.size() == marked.size(), "the marker is written and read");
		const auto f = read_file(marked.data(), marked.size(), kind_profile);
		check(f.header.required_features == 1, "decoded marker");
		// a rewrite carrying the read header keeps the marker
		std::vector<SectionOut> secs;
		for (const auto& tag : f.order) { const auto& s = f.sections.at(tag); secs.push_back({tag, s.entry.major, s.entry.minor, s.entry.flags, s.data, 0}); }
		HeaderIn again; again.writer_version = f.header.writer_version; again.content_version = f.header.content_version; again.catalogue_revision = f.header.catalogue_revision;
		again.campaign_serial = f.header.campaign_serial; again.save_sequence = f.header.save_sequence; again.required_features = f.header.required_features;
		const auto rewritten = write_file(kind_profile, secs, again);
		check(read_file(rewritten.data(), rewritten.size(), kind_profile).header.required_features == 1, "a rewrite keeps the marker");
		auto bad_h = h; bad_h.required_features = 2;
		check(contains(error_of([&] { write_file(kind_profile, sec, bad_h); }), "not implemented by this writer"), "the writer never emits a bit it does not implement");
		for (const u32 b : {8u, 15u, 0x80000000u})
		{
			const auto patched = with_required_features(plain, b);
			char hexed[32]; std::snprintf(hexed, sizeof hexed, "0x%x", static_cast<unsigned>(b));
			check(error_of([&] { read_file(patched.data(), patched.size(), -1); }) == std::string("required feature bits ") + hexed + " not implemented (needs a newer version)", "unknown required bits refused");
			check(!decide_features(patched, retail_layout).ok && !decide_features(patched, e10a2_layout).ok && !decide_features(patched, w2_layout).ok, "decide_features refuses unknown bits");
		}
		// W2 (limits-wave2.md section 7): bit 1 is known by name; it needs bit 0 and the W2 layout
		{
			auto w2_h = h; w2_h.required_features = 3;
			check(error_of([&] { write_file(kind_profile, sec, w2_h); }).empty(), "the writer emits bits 0 | 1 (NATIVE_W2)");
			const auto two = with_required_features(plain, 2);
			check(contains(error_of([&] { read_file(two.data(), two.size(), -1); }), "NATIVE_W2 without ENTITY_LAYOUT_CONTIGUOUS10"), "bit 1 without bit 0 is a damaged marker");
			const auto three = with_required_features(plain, 3);
			check(error_of([&] { read_file(three.data(), three.size(), -1); }).empty(), "bits 0 | 1 read");
			auto d3 = decide_features(3u, retail_layout); check(!d3.ok && d3.reason == downgrade_refused, "RETAIL refuses a bit-1 file as a feature-1 file");
			d3 = decide_features(3u, e10a2_layout); check(!d3.ok && d3.reason == w2_refused && contains(d3.reason, "SaveLayout=w2"), "E10A2 refuses a bit-1 file by name");
			d3 = decide_features(3u, w2_layout); check(d3.ok && !d3.migrate_typed_fields, "W2 applies a bit-1 file as is");
			d3 = decide_features(1u, w2_layout); check(d3.ok && !d3.migrate_typed_fields, "W2 applies a feature-1 (E10A2) file as is");
			d3 = decide_features(0u, w2_layout); check(d3.ok && d3.migrate_typed_fields, "W2 migrates a feature-0 file");
			check(std::string(required_feature_name(2)) == "NATIVE_W2" && contains(required_feature_needs(2), "0.4.2"), "W2 feature name unchanged");
			auto actors=w2_layout;actors.feature_bits|=feature_actor_capacity96;
			check(decide_features(7u,actors).ok&&!decide_features(7u,w2_layout).ok,"actor bit accepted only by actor-capacity layout");
			check(decide_features(0u,actors).ok&&decide_features(1u,actors).ok&&decide_features(3u,actors).ok,"actor layout accepts every existing save marker unchanged");
		}
		auto d = decide_features(0u, retail_layout); check(d.ok && !d.migrate_typed_fields, "retail + feature 0");
		d = decide_features(1u, retail_layout); check(!d.ok && d.reason == downgrade_refused && contains(d.reason, "needs the entity-capacity build"), "retail + feature 1 refused");
		d = decide_features(0u, e10a2_layout); check(d.ok && d.migrate_typed_fields, "E10A2 + feature 0 migrates");
		d = decide_features(1u, e10a2_layout); check(d.ok && !d.migrate_typed_fields, "E10A2 + feature 1 applies as is");
		d = decide_features(marked, e10a2_layout); check(d.ok && d.required_features == 1 && !d.migrate_typed_fields, "decide_features over a file");
		d = decide_features(marked, retail_layout); check(!d.ok && d.reason == downgrade_refused && d.required_features == 1, "decide_features over a feature-1 file on retail");
	}

	// Data/autospend as the engine loads it: four classes, four rules each, record order strength/speed/body/mind
	void test_autospend(const fs::path& folder)
	{
		fs::create_directories(folder / "Data");
		std::vector<AutospendClass> classes;
		std::string error;
		check(!read_autospend(folder.wstring(), classes, error) && contains(error, "autospend"), "no Data/autospend: an error");
		E root{"AutoSpend", {}, {}};
		const char* names[5] = {"class_a", "Class_B", "class_c", "class_d", "class_e"};
		for (int i = 0; i < 5; ++i)
		{
			auto& c = root.add(E{"CharacterClass", {{"body", "10"}, {"name", names[i]}}, {}});
			for (int k = 0; k < (i == 1 ? 5 : 1); ++k)
				c.add(E{"bonus", {{"start", std::to_string(2 + k)}, {"rate", std::to_string(k)}, {"strength", std::to_string(i)}, {"mind", "-2"}, {"Speed", "3"}}, {}});
		}
		const auto raw = encode_xmlb(root);
		std::ofstream(folder / "Data" / "autospend.xmlb", std::ios::binary).write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
		check(read_autospend(folder.wstring(), classes, error), "read_autospend (" + error + ")");
		check(classes.size() == autospend_class_limit && classes[1].name == "class_b" && classes[3].name == "class_d", "the first four classes load, names lower case");
		check(classes.size() == 4 && classes[1].rules.size() == autospend_rule_limit && classes[0].rules.size() == 1, "four rules a class");
		if (classes.size() == 4 && classes[1].rules.size() == 4)
		{
			const auto& r = classes[1].rules[3];
			check(r.start == 5 && r.rate == 3 && r.delta[0] == 1 && r.delta[1] == 3 && r.delta[2] == 0 && r.delta[3] == -2, "a rule: start, rate, strength, speed, body, mind");
		}
		std::ofstream(folder / "Data" / "autospend.xmlb", std::ios::binary).write("junk", 4);
		check(!read_autospend(folder.wstring(), classes, error) && contains(error, "autospend"), "a damaged Data/autospend: an error, no crash");
	}

	void test_typed_registry(const fs::path& scratch)
	{
		TypedRegistry reg;
		std::string error;
		const auto text = typed_registry_text();
		check(load_typed_registry(std::vector<std::uint8_t>(text.begin(), text.end()), reg, error) && reg.keys.size() == 13 && reg.script_files == 3 &&
			reg.typed("Zone", "trig_a") && reg.typed("Game", "door_q") && !reg.typed("Game", "base_q") && !reg.typed("Zone", "counter_a"), "registry loads: " + error);
		const char* bad[] = {"", "lcsave-typed-entity-ids 2\n", "lcsave-typed-entity-ids 1\nfingerprint zz\n",
			"lcsave-typed-entity-ids 1\nfingerprint 0000000000000000000000000000000000000000000000000000000000000000\nfiles 1\nkey\tWorld\tx\n",
			"lcsave-typed-entity-ids 1\nfingerprint 0000000000000000000000000000000000000000000000000000000000000000\nfiles 1\nkey\tZone\tx\nkey\tZone\tx\n",
			"lcsave-typed-entity-ids 1\nfingerprint 0000000000000000000000000000000000000000000000000000000000000000\nkey\tZone\tx\n",
			"lcsave-typed-entity-ids 1\r\nfingerprint 0000000000000000000000000000000000000000000000000000000000000000\r\nfiles 1\r\n", "\xff\xfe"};
		for (const char* b : bad)
		{
			const std::string s(b);
			TypedRegistry scratch_reg;                    // load_typed_registry resets its output: never the registry under test
			check(!load_typed_registry(std::vector<std::uint8_t>(s.begin(), s.end()), scratch_reg, error) && contains(error, "typed registry"), "malformed registry refused: " + s.substr(0, 30));
		}
		// the scripts fingerprint: known answers computed by tools/lcsave/typed_registry.py over the same invented files
		const auto tree = scratch / "scripts";
		auto put = [&](const fs::path& rel, const std::string& content) { write_text_file(tree / rel, content); };
		put("zone_q/trap.py", "v0 = getID( me )\nsetZoneVar(\"trig_a\", v0 )\n");
		put("zone_q/reset.py", "setZoneVar(\"trig_a\", 0 )\n");
		put("zone_r/door.py", "  who = getIDString( me )\n  setGameVar(\"door_q\", who )\n");
		std::string fp; u32 files = 0;
		check(scripts_fingerprint(tree.wstring(), fp, files, error) && fp == "52ea9ba8d8a04149bcf5d53d7284deaf187b4a391e2d47699e3722a8089ef88e" && files == 3, "scripts fingerprint (Python known answer): " + fp + " " + error);
		const auto tree2 = scratch / "order";
		auto put2 = [&](const fs::path& rel, const std::string& content) { write_text_file(tree2 / rel, content); };
		put2("a-b/x.py", "# one\n"); put2("a/x.py", "# two\n"); put2("a/y.py", "# three\n"); put2("B/z.py", "# four\n"); put2("a/n.PY", "# five\n"); put2("a/no.txt", "");
		check(scripts_fingerprint(tree2.wstring(), fp, files, error) && fp == "5aa719be0eed6f5c953b123e383cab938beb24c304d67ba9c910bd123da54b94" && files == 5, "path order by lower-cased components, *.PY counted, .txt not (Python known answer): " + fp);
		TypedRegistry bound = reg;
		bound.fingerprint = "52ea9ba8d8a04149bcf5d53d7284deaf187b4a391e2d47699e3722a8089ef88e";
		std::string reason;
		const bool verified = verify_registry(bound, tree.wstring(), reason);
		check(verified && reason.empty(), "registry verifies: " + reason);
		check(!verify_registry(reg, tree.wstring(), reason) && contains(reason, "does not match the scripts tree") && contains(reason, "regenerated registry"), "fingerprint mismatch refused");
		put("zone_q/extra.py", "");
		check(!verify_registry(bound, tree.wstring(), reason), "an added file changes the tree");
		check(!verify_registry(bound, (scratch / "missing").wstring(), reason) && contains(reason, "cannot be verified"), "a missing tree cannot verify");
		check(!verify_registry(std::vector<std::uint8_t>{'x'}, tree.wstring(), reason) && contains(reason, "cannot be verified"), "a malformed registry file cannot verify");
		// the adapter over SVAR values
		auto lists = [&]()
		{
			Value zone = Value::list(), game = Value::list();
			for (const auto& c : typed_cases) zone.push(Value::dict({{"name", Value::text(c.name)}, {"value", Value::integer(c.saved)}}));
			for (const auto& [n, v] : untyped_zone) zone.push(Value::dict({{"name", Value::text(n)}, {"value", Value::integer(v)}}));
			for (const auto& [n, v] : game_vars) game.push(Value::dict({{"name", Value::text(n)}, {"value", Value::integer(v)}}));
			return std::make_pair(zone, game);
		};
		{
			auto [zone, game] = lists();
			MigrationReport rep;
			migrate_script_vars(zone, game, 0, e10a2_layout, &reg, rep);
			bool ok = true;
			for (std::size_t k = 0; k < 12; ++k) ok &= zone.l[k]["value"].as_int() == typed_cases[k].expected;
			ok &= zone.l[12]["value"].as_int() == 0x101 && zone.l[13]["value"].as_int() == 0x3fff01 && game.l[1]["value"].as_int() == 0x4ff && game.l[2]["value"].as_int() == 0x101;
			check(ok && rep.mode == MigrationReport::Mode::migrate && rep.typed_fields == 13 && rep.translated == 6 && rep.normalized_to_null == 6 && rep.validated == 0, "migrate: " + rep.line());
			check(rep.line() == "mode=migrate typed_fields=13 translated=6 normalized_to_null=6 validated=0" && migration_note(rep) == "typed entity-id migration: 13 registered fields, 6 translated, 6 normalized to null", "report line and note");
			MigrationReport rep2;
			migrate_script_vars(zone, game, 1, e10a2_layout, &reg, rep2);
			ok = true;
			for (std::size_t k = 0; k < 12; ++k) ok &= zone.l[k]["value"].as_int() == typed_cases[k].expected;
			check(ok && rep2.mode == MigrationReport::Mode::validate && rep2.typed_fields == 13 && rep2.validated == 13 && rep2.normalized_to_null == 0 && rep2.translated == 0, "validate is idempotent: " + rep2.line());
			zone.l[1].set("value", Value::integer((0x4000 << 10) | 7));
			migrate_script_vars(zone, game, 1, e10a2_layout, &reg, rep2);
			check(zone.l[1]["value"].as_int() == 0 && rep2.normalized_to_null == 1, "an out-of-domain ten-bit value is normalized and counted");
		}
		{
			auto [zone, game] = lists();
			MigrationReport rep;
			migrate_script_vars(zone, game, 0, retail_layout, &reg, rep);
			check(zone.l[1]["value"].as_int() == 0x101 && rep.mode == MigrationReport::Mode::none && rep.typed_fields == 0, "retail leaves every value");
			check(error_of([&] { migrate_script_vars(zone, game, 0, e10a2_layout, nullptr, rep); }) == no_registry, "legacy without a registry refused");
			migrate_script_vars(zone, game, 1, e10a2_layout, nullptr, rep);
			check(zone.l[1]["value"].as_int() == 0x101 && rep.mode == MigrationReport::Mode::none, "feature 1 without a registry: untouched");
		}
	}

	void test_e10a2_api(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false);
		Tables tables; tables.data = t;
		const auto& e = e10a2_layout;
		TypedRegistry reg;
		std::string error;

		const auto text = typed_registry_text();
		check(load_typed_registry(std::vector<std::uint8_t>(text.begin(), text.end()), reg, error), "registry");
		const auto raw = write_slot(typed_old_save());
		const auto legacy = convert_old_to_new(raw, tables);
		check(legacy.ok, "legacy import: " + legacy.error);
		if (!legacy.ok) return;
		check(read_file(legacy.bytes.data(), legacy.bytes.size(), kind_campaign).header.required_features == 0, "the offline converter writes feature 0");
		// (1) the retail loader keeps every value as saved
		const auto plain = new_to_old_stream(legacy.bytes, tables);
		check(plain.ok && plain.bytes.size() == retail_layout.slot_size, "retail loader");
		std::map<std::string, u32> saved_vars;
		for (const auto& c : typed_cases) saved_vars[c.name] = c.saved;
		for (const auto& [n, v] : untyped_zone) saved_vars[n] = v;
		if (plain.ok) check(vars_of(read_slot(plain.bytes.data(), plain.bytes.size()).zone_vars, old_script_zone_vars) == saved_vars && !contains(plain.ledger, "typed"), "retail loader leaves the values");
		// (2) the raised loader migrates the registered fields only, counts the normalizations, preserves the rest
		MigrationReport rep;
		const auto raised = new_to_old_stream(legacy.bytes, tables, e, &reg, &rep);
		check(raised.ok && raised.bytes.size() == e.slot_size, "E10A2 loader: " + raised.error);
		if (!raised.ok) return;
		std::map<std::string, u32> expected;
		for (const auto& c : typed_cases) expected[c.name] = c.expected;
		for (const auto& [n, v] : untyped_zone) expected[n] = v;
		const auto rm = read_slot(raised.bytes.data(), raised.bytes.size(), e);
		check(vars_of(rm.zone_vars, old_script_zone_vars) == expected, "E10A2 loader migrates registered zone variables");
		check(vars_of(rm.game_vars, old_script_game_vars) == std::map<std::string, u32>{{"gv_one", 7}, {"door_q", 0x4ff}, {"base_q", 0x101}}, "Game scope key translated, base_q (Game) untouched");
		check(rm.game.value_608 == 0x101 && rm.world[0].slot == 5 && rm.zones[0].matches(e) && rm.zones[0].tail.state_count == 5, "everything else preserved; zones laid out for E10A2");
		check(rep.line() == "mode=migrate typed_fields=13 translated=6 normalized_to_null=6 validated=0" && contains(raised.ledger, "typed entity-id migration: 13 registered fields, 6 translated, 6 normalized to null"), "migration report: " + rep.line());
		// (3) the raised engine's save: feature 1, the values unchanged, the retained LGCY/KEYS carried
		NewFileContext ctx;
		ctx.saved_at = 5; ctx.campaign_serial = 17; ctx.save_sequence = 2; ctx.retained_file = legacy.bytes; ctx.require_capture_metadata = true;
		check(loaded_zone_metadata(legacy.bytes, tables, ctx.zone_captures, error, e, &reg) && ctx.zone_captures.size() == 1 && ctx.zone_captures[0].entities.size() == 3, "E10A2 loaded metadata: " + error);
		const auto saved = old_stream_to_new(raised.bytes, tables, ctx, e);
		check(saved.ok, "E10A2 save: " + saved.error);
		if (!saved.ok) return;
		const auto f1 = read_file(saved.bytes.data(), saved.bytes.size(), kind_campaign);
		check(f1.header.required_features == 1 && f1.sections.count("LGCY") && f1.sections.count("KEYS"), "the E10A2 save carries feature bit 0 and the retained sections");
		std::map<std::string, u32> zv;
		for (const auto& v : f1.data("SVAR")["zone_vars"].as_list()) zv[v["name"].as_text()] = static_cast<u32>(v["value"].as_int());
		check(zv == expected, "saved SVAR values");
		const auto payload_only = old_stream_to_new(std::vector<std::uint8_t>(raised.bytes.begin() + old_envelope, raised.bytes.end()), tables, ctx, e);
		check(payload_only.ok && read_file(payload_only.bytes.data(), payload_only.bytes.size(), kind_campaign).header.required_features == 1, "the payload alone is accepted for the layout");
		const auto wrong = old_stream_to_new(std::vector<std::uint8_t>(plain.bytes.begin() + old_envelope, plain.bytes.end()), tables, NewFileContext{}, e);
		check(!wrong.ok && wrong.error == "file size " + std::to_string(retail_layout.payload) + " is not the slot size " + std::to_string(e.slot_size) + " (0x84 + 0x200000)", "a retail-size payload is refused by the E10A2 saver, never guessed: " + wrong.error);
		// (4) feature 1 on the raised loader: validate only, never shift again (idempotent); counts reported
		MigrationReport rep2;
		const auto again = new_to_old_stream(saved.bytes, tables, e, &reg, &rep2);
		check(again.ok && again.bytes == raised.bytes, "feature-1 load is idempotent");
		check(rep2.line() == "mode=validate typed_fields=13 translated=0 normalized_to_null=0 validated=13" && contains(again.ledger, "typed entity-id validation: 13 registered fields, 0 normalized to null"), "validation report: " + rep2.line());
		{
			std::vector<SectionOut> secs;
			for (const auto& tag : f1.order)
			{
				const auto& s = f1.sections.at(tag);
				if (s.known) secs.push_back({tag, s.entry.major, s.entry.minor, s.entry.flags, s.data, 0});
				else secs.push_back({tag, s.entry.major, s.entry.minor, s.entry.flags, Value::bytes(s.payload), s.entry.records});
			}
			for (auto& s : secs) if (s.tag == "SVAR")
			{
				Value zone_vars = s.data["zone_vars"];
				for (auto& v : zone_vars.as_list()) if (v["name"].as_text() == "last_a") v.set("value", Value::integer((0x4000 << 10) | 7));
				s.data.set("zone_vars", std::move(zone_vars));
			}
			HeaderIn h; h.writer_version = f1.header.writer_version; h.content_version = f1.header.content_version; h.catalogue_revision = f1.header.catalogue_revision;
			h.campaign_serial = f1.header.campaign_serial; h.save_sequence = f1.header.save_sequence; h.required_features = f1.header.required_features;
			const auto hot = write_file(kind_campaign, secs, h);
			MigrationReport rep3;
			const auto hm = new_to_old_stream(hot, tables, e, &reg, &rep3);
			check(hm.ok && vars_of(read_slot(hm.bytes.data(), hm.bytes.size(), e).zone_vars, old_script_zone_vars)["last_a"] == 0 && rep3.normalized_to_null == 1 && rep3.mode == MigrationReport::Mode::validate,
				"an out-of-domain ten-bit value in a feature-1 file is normalized to null and counted");
		}
		const auto no_reg_feature1 = new_to_old_stream(saved.bytes, tables, e);
		check(no_reg_feature1.ok && no_reg_feature1.bytes == raised.bytes, "without a registry a feature-1 file loads untouched");
		const auto no_reg_feature0 = new_to_old_stream(legacy.bytes, tables, e);
		check(!no_reg_feature0.ok && no_reg_feature0.bytes.empty() && no_reg_feature0.error == no_registry, "a feature-0 file without a registry is refused: " + no_reg_feature0.error);
		// (5) downgrade: the retail loader refuses the feature-1 file; the marker is never stripped by a rewrite
		const auto down = new_to_old_stream(saved.bytes, tables);
		check(!down.ok && down.bytes.empty() && down.error == downgrade_refused, "the retail loader refuses the feature-1 file: " + down.error);
		std::vector<ZoneCaptureMetadata> captures;
		check(!loaded_zone_metadata(saved.bytes, tables, captures, error) && error == downgrade_refused, "retail loaded_zone_metadata refuses too");
		NewFileContext retail_ctx; retail_ctx.retained_file = saved.bytes;
		const auto retail_resave = old_stream_to_new(plain.bytes, tables, retail_ctx);
		check(!retail_resave.ok && retail_resave.error == std::string("retained file: ") + downgrade_refused, "a retail writer never carries a feature-1 file: " + retail_resave.error);
		// (6) an unknown required bit refuses everywhere with the established message
		const auto unknown = with_required_features(legacy.bytes, 8);
		for (const auto* n : {&retail_layout, &e, &w2_layout})
		{
			const auto u = new_to_old_stream(unknown, tables, *n, &reg);
			check(!u.ok && u.error == "required feature bits 0x8 not implemented (needs a newer version)", "unknown bit refused on " + std::string(n->id));
		}
		const auto w2_marked = with_required_features(legacy.bytes, 3);
		const auto on_e10a2 = new_to_old_stream(w2_marked, tables, e, &reg);
		check(!on_e10a2.ok && on_e10a2.error == w2_refused, "a bit-1 file is refused by name on E10A2: " + on_e10a2.error);
		// (7) native transfer metadata and the other stream entries at the raised layout
		std::vector<ZoneCaptureMetadata> native_e, native_r;
		check(native_zone_metadata(raised.bytes, tables, native_e, error, e) && native_zone_metadata(plain.bytes, tables, native_r, error) &&
			native_e.size() == 1 && native_e[0].entities.size() == 3 && native_e[0].entities.size() == native_r[0].entities.size() &&
			native_e[0].entities[2].ordinal == native_r[0].entities[2].ordinal, "E10A2 native transfer metadata agrees with retail: " + error);
		check(!native_zone_metadata(raised.bytes, tables, native_r, error) && contains(error, "is not the slot size"), "a retail reader given the raised stream refuses");
		NativeLoadDemand demand;
		check(native_load_demand(raised.bytes, demand, error, e) && demand.world_items == 1 && validate_native_stream(raised.bytes, tables, error, e), "E10A2 demand / validation: " + error);
		std::vector<TalentAdd> adds;
		check(missing_talents(std::vector<std::uint8_t>(raised.bytes.begin() + old_envelope, raised.bytes.end()), tables, adds, error, e), "E10A2 missing_talents: " + error);
		// validate_native_sections bounds ordinals by the layout: 300 is an ordinal only the raised layout accepts
		auto fl = read_file(legacy.bytes.data(), legacy.bytes.size(), kind_campaign);
		{
			Value records = fl.sections["ZONE"].data["records"];
			Value states = records.l[0]["states"];
			states.l[0].set("ordinal", Value::integer(300));
			records.l[0].set("states", std::move(states));
			fl.sections["ZONE"].data.set("records", std::move(records));
		}
		check(contains(error_of([&] { validate_native_sections(fl); }), "duplicate or invalid ordinal") && error_of([&] { validate_native_sections(fl, e); }).empty(), "ordinal bound follows the layout");
	}


	// ---- limits wave 2 (docs/limits-wave2.md): the W2 descriptor, the stream at W2, the converter, the probe --------
	// a retail model laid out for W2 the way the W2 engine writes it: zone records sized by the layout, a state that owns
	// a world item carries its slot in the record's own u16 (payload byte 0 then 0), the zone strings in the W2 table
	OldSlot to_w2(const OldSlot& retail)
	{
		const auto& w = w2_layout;
		OldSlot m = retail;
		for (auto& z : m.zones)
		{
			z = widen(z, w);
			for (auto& s : z.states) if (s.flags_a & 1) { s.world_slot = s.payload[0]; s.payload[0] = 0; }
		}
		OldZoneStrings t(w.zone_string_slots, w.zone_arena);
		t.generation = retail.zone_strings->generation;
		t.used = retail.zone_strings->used;
		std::copy(retail.zone_strings->offsets.begin(), retail.zone_strings->offsets.end(), t.offsets.begin());
		std::copy(retail.zone_strings->arena.begin(), retail.zone_strings->arena.end(), t.arena.begin());
		*m.zone_strings = std::move(t);
		return m;
	}

	void test_w2_layout()
	{
		const auto& w = w2_layout;
		check(native_layout("W2") == &w && std::string(w.id) == "W2" && w.feature_bits == 3 && w.contiguous() && w.w2() && !e10a2_layout.w2() && !retail_layout.w2(),
			"W2 descriptor by id, feature bits 0 | 1");
		check(w.states == 4096 && w.ordinals == 4096 && w.dynamic == 2048 && w.smart == 2048 && w.record_count == 8 && w.state_record_size == 0x20 &&
			w.float_positions && w.world_slot_bytes == 2, "W2 zone record shape (3.5 / 3.6)");
		check(w.record_size == 0x24388 && w.manager_size == 0x122190 && w.mission == 0x121c48 && w.payload == 0x200000 && w.wrapper == 0x200078 &&
			w.slot_size == 0x200084 && w.transfer_chunks == 10486, "W2 record, manager and stream (3.6 - 3.8)");
		check(w.definitions == 1024 && w.definition_index_bytes == 2 && w.def_count_bytes == 2 && w.item_head_size == 0x826 && w.instance_fixed == 6,
			"W2 item heads (3.2 / 3.3)");
		check(w.enhancement_records == 4096 && w.attached_enhancements == 4096 && w.instances == 1024 && w.world_slots == 1024, "W2 pools (section 2)");
		check(w.objectives == 1024 && w.objectives_per_act == 127 && w.objective_files == 64 && w.snapshot_objectives == 1024 && w.snapshot_size == 0x540 &&
			w.side_block == 0xa88, "W2 objectives and snapshot (3.4)");
		check(w.talent_multiplier == 200 && w.talent_pool == 256 && w.talent_id(22, 10) == 4410 && w.zone_string_slots == 0x2000 && w.zone_arena == 0x20000 &&
			w.zone_strings_size == 0x28008 && w.loader_placements == 1024, "W2 talents (M = 200: stats index 21 position 10 = 4410) and zone strings");
		for (const auto* n : {&retail_layout, &e10a2_layout})
		{
			check(n->definitions == 160 && n->instances == 256 && n->world_slots == 150 && n->attached_enhancements == 375 && n->snapshot_size == 0x26c &&
				n->talent_multiplier == 100 && n->state_record_size == 0x18 && !n->float_positions && n->world_slot_bytes == 1 && n->snapshot_objectives == 300,
				std::string("pre-W2 descriptor values unchanged: ") + n->id);
		}
		// the talent id space (4.7): position < M, shared position < M - 1, and the target's M = 100
		check(talent_engine_id(talent_code(22, 199), w) == 22 * 200 + 199 && contains(error_of([&] { talent_engine_id(talent_code(22, 200), w); }), "talent multiplier 200"),
			"talent position M - 1 accepted, M refused");
		check(talent_engine_id(talent_code(0, 198), w) == 198 && !error_of([&] { talent_engine_id(talent_code(0, 199), w); }).empty(), "shared position M - 2 accepted (cap M - 1), M - 1 refused");
		check(talent_engine_id(talent_code(2, 99), e10a2_layout) == 299 && !error_of([&] { talent_engine_id(talent_code(2, 100), e10a2_layout); }).empty() &&
			!error_of([&] { talent_engine_id(talent_code(0, 99), retail_layout); }).empty(), "pre-W2 multiplier 100: position 99 / shared 98 are the last");
	}

	void test_w2_oldsave()
	{
		const auto& w = w2_layout;
		auto m = to_w2(typed_old_save());
		// boundary values: the largest valid index of every widened table, each beside its count
		m.items.def_count = 1024;
		m.items.def_state[2 * 1023] = 7; m.items.def_state[2 * 1023 + 1] = 1;     // the pair of definition 1023
		m.carried[0].head.definition = 1023;
		m.mission.objectives[1023] = 0x45; m.mission.objectives[300] = 2;          // objective 1023; 300 (the retail 301st byte)
		m.side_frames[1].objectives[256] = 1;
		std::memset(m.mission.pad_525, 0xa5, 3);
		auto& z = m.zones[0];
		z.states[1].position[0] = 0.5f; z.states[1].position[1] = -32768.5f; z.states[1].position[2] = 40000.0f;
		z.states[4000] = z.states[2];
		z.states[4095] = z.states[3];
		z.tail.ordinal_map[4095] = 4095;                                            // ordinal 4096 (slot 4095)
		z.tail.smart[2047] = 4000; z.tail.smart[0] = 4000; z.tail.smart_count = 1;
		z.tail.state_count = 4096;                                                  // 4096 records including record 0
		m.world[0].slot = 1023;
		for (auto& s : z.states) if ((s.flags_a & 1) && s.world_slot == 5) s.world_slot = 1023;
		auto& zs = *m.zone_strings;
		const std::string distant = "far/zone/name";
		zs.offsets[0x1fff] = zs.used;
		std::memcpy(zs.arena.data() + zs.used, distant.c_str(), distant.size() + 1);
		zs.used += static_cast<u32>(distant.size() + 1);
		z.states[4095].def_name = (1u << 24) | 0x1fff;                              // a name in slot 8191
		// eight zone records
		while (m.zones.size() < 8) m.zones.push_back(m.zones[0]);
		const auto raw = write_slot(m, w);
		u32 plen = 0; std::memcpy(&plen, raw.data() + 0x80, 4);
		check(raw.size() == w.slot_size && plen == 0x200000, "W2 slot size and length field");
		const auto back = read_slot(raw.data(), raw.size(), w);
		check(write_slot(back, w) == raw, "W2 round trip (deterministic bytes)");
		check(back.items.def_count == 1024 && back.items.def_state[2046] == 7 && back.carried[0].head.definition == 1023 && back.mission.objectives[1023] == 0x45 &&
			back.mission.objectives[300] == 2 && back.side_frames[1].objectives[256] == 1 && back.mission.pad_525[2] == 0xa5, "W2 item head, instance head, snapshot fields");
		const auto& bz = back.zones[0];
		const float want[3] = {0.5f, -32768.5f, 40000.0f};
		check(std::memcmp(bz.states[1].position, want, 12) == 0, "W2 positions bit-exact (0.5, -32768.5, 40000.0)");
		check(bz.tail.state_count == 4096 && bz.tail.ordinal_map[4095] == 4095 && bz.states[4095].def_name == ((1u << 24) | 0x1fff) &&
			zone_string(*back.zone_strings, bz.states[4095].def_name) == distant && back.zones.size() == 8 && back.world[0].slot == 1023,
			"W2 state record 4095 / count 4096, ordinal 4096, zone string slot 8191, 8 records, world slot 1023");
		bool slot_field = false;
		for (const auto& s : bz.states) slot_field |= (s.flags_a & 1) && s.world_slot == 1023 && s.payload[0] == 0;
		check(slot_field, "W2 world-item slot in the record's own u16 (payload byte 0 stays 0)");
		// the stream offsets of section 3: the item head is 0x826 bytes (u16 count at +0)
		const std::size_t head = old_envelope + old_tag;
		u16 count = 0; std::memcpy(&count, raw.data() + head, 2);
		u32 carried = 0; std::memcpy(&carried, raw.data() + head + 0x826, 4);
		check(count == 1024 && carried == m.carried.size() && raw[head + 2 + 2 * 1023] == 7, "W2 item head layout: u16 count, pairs, carried count at 0x826");
		u16 def = 0; std::memcpy(&def, raw.data() + head + 0x826 + 4, 2);
		check(def == 1023, "W2 instance head: u16 definition first");
		// the count + 1 / index + 1 cases refuse by name
		auto bad = m; bad.zones.push_back(m.zones[0]);
		check(contains(error_of([&] { write_slot(bad, w); }), "zone record count 9 > 8"), "a ninth zone record is refused");
		auto raw9 = raw;                                                               // a stream that claims 9 records
		{
			const auto at = std::search(raw9.begin(), raw9.end(), std::begin("[MAP: ZONES]"), std::end("[MAP: ZONES]") - 1);
			const u32 nine = 9; std::memcpy(&*(at + 51), &nine, 4);
			check(contains(error_of([&] { read_slot(raw9.data(), raw9.size(), w); }), "zone record count 9 > 8"), "the reader refuses 9 records by name");
		}
		bad = m; bad.world.resize(1025, m.world[0]);
		check(contains(error_of([&] { write_slot(bad, w); }), "world item count 1025 > 1024"), "1025 world items refused");
		bad = m; bad.world[0].slot = 1024;
		const auto raw_slot = write_slot(bad, w);
		check(contains(error_of([&] { read_slot(raw_slot.data(), raw_slot.size(), w); }), "world item slot 1024 >= 1024"), "world slot 1024 refused, 1023 read");
		bad = m; bad.zones[0].tail.smart_count = 2049;
		const auto raw_smart = write_slot(bad, w);
		check(contains(error_of([&] { read_slot(raw_smart.data(), raw_smart.size(), w); }), "beyond the engine tables"), "2049 smart entries refused");
		// a W2 model is never written as an older layout, and the widened fields refuse narrowing by name
		check(contains(error_of([&] { write_slot(m, e10a2_layout); }), "does not match the native layout E10A2"), "a W2 model is refused by the E10A2 writer");
		auto narrow_m = typed_old_save();
		narrow_m.carried[0].head.definition = 256;
		check(contains(error_of([&] { write_slot(narrow_m, retail_layout); }), "item definition index 256 does not fit the native layout RETAIL"), "definition 256 refused on a byte layout");
		narrow_m = typed_old_save(); narrow_m.items.def_state[2 * 160] = 1;
		check(contains(error_of([&] { write_slot(narrow_m, retail_layout); }), "definition state pair 160 does not fit"), "pair 160 refused on RETAIL");
		narrow_m = typed_old_save(); narrow_m.mission.objectives[300] = 1;
		check(contains(error_of([&] { write_slot(narrow_m, retail_layout); }), "objective index 300 does not fit the native layout RETAIL"), "objective 300 refused on RETAIL");
		narrow_m = typed_old_save(); narrow_m.zones[0].states[1].position[0] = 0.5f;
		check(contains(error_of([&] { write_slot(narrow_m, retail_layout); }), "state position does not fit the native layout RETAIL (i16)"), "position 0.5 refused on RETAIL");
		// a retail stream reads with the world slot field absent (0xffff) and integral float positions: unchanged bytes
		const auto retail_raw = write_slot(typed_old_save());
		const auto rm = read_slot(retail_raw.data(), retail_raw.size());
		bool none = true;
		for (const auto& s : rm.zones[0].states) none &= s.world_slot == world_slot_none;
		check(none && write_slot(rm) == retail_raw && rm.zone_strings->offsets.size() == 0x800 && rm.zone_strings->arena.size() == 0x3800, "RETAIL read / write unchanged");
		const auto e10_raw = write_slot([&] { auto x = typed_old_save(); for (auto& zz : x.zones) zz = widen(zz, e10a2_layout); return x; }(), e10a2_layout);
		const auto em = read_slot(e10_raw.data(), e10_raw.size(), e10a2_layout);
		check(write_slot(em, e10a2_layout) == e10_raw, "E10A2 read / write unchanged");
	}

	void test_w2_api(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false);
		Tables tables; tables.data = t;
		const auto& w = w2_layout;
		const auto& e = e10a2_layout;
		TypedRegistry reg;
		std::string error;
		{
			const auto old_shared=t->build->T.TALN_shared;
			const auto old_hero=t->build->T.TALN_hero;
			t->build->T.TALN_shared.resize(199);
			t->build->T.TALN_hero[21].resize(200);
			check(validate_talent_positions(tables,w,error), "W2 build validator accepts shared198 and hero199 positions");
			t->build->T.TALN_hero[21].resize(201);
			check(!validate_talent_positions(tables,w,error) && contains(error,"talent position 200"), "W2 build validator refuses position200 even if absent from save");
			t->build->T.TALN_hero[21].resize(200);t->build->T.TALN_shared.resize(200);
			check(!validate_talent_positions(tables,w,error) && contains(error,"shared talent position 199"), "W2 build validator refuses shared199");
			t->build->T.TALN_shared.clear();t->build->T.TALN_hero.clear();t->build->T.TALN_hero[327].resize(200);
			check(!validate_talent_positions(tables,w,error) && contains(error,"saved u16"), "W2 build validator refuses overflowing saved u16 talent id");
			t->build->T.TALN_shared=old_shared;t->build->T.TALN_hero=old_hero;
		}
		const auto text = typed_registry_text();
		check(load_typed_registry(std::vector<std::uint8_t>(text.begin(), text.end()), reg, error), "registry");
		const auto legacy = convert_old_to_new(write_slot(typed_old_save()), tables);
		check(legacy.ok, "legacy import: " + legacy.error);
		if (!legacy.ok) return;
		// (1) the W2 loader: a feature-0 file migrates exactly as on E10A2; the stream is W2's
		MigrationReport rep;
		const auto w2s = new_to_old_stream(legacy.bytes, tables, w, &reg, &rep);
		check(w2s.ok && w2s.bytes.size() == w.slot_size, "W2 loader: " + w2s.error);
		if (!w2s.ok) return;
		const auto e10 = new_to_old_stream(legacy.bytes, tables, e, &reg);
		const auto wm = read_slot(w2s.bytes.data(), w2s.bytes.size(), w);
		const auto em = read_slot(e10.bytes.data(), e10.bytes.size(), e);
		check(rep.line() == "mode=migrate typed_fields=13 translated=6 normalized_to_null=6 validated=0", "W2 migration report: " + rep.line());
		check(vars_of(wm.zone_vars, old_script_zone_vars) == vars_of(em.zone_vars, old_script_zone_vars) && wm.zones[0].matches(w) && wm.zone_strings->offsets.size() == 0x2000,
			"W2 loader: the same values as E10A2, W2 zone records and strings");
		// the talent ids are re-based to M = 200 (hero 1's own talents: 2 x 200 + position)
		bool rebased = false;
		for (u32 k = 0; k < wm.heroes[0].attributes.tail.count; ++k) rebased |= wm.heroes[0].attributes.talents[k].id == 400 || wm.heroes[0].attributes.talents[k].id == 402;
		bool old_ids = false;
		for (u32 k = 0; k < em.heroes[0].attributes.tail.count; ++k) old_ids |= em.heroes[0].attributes.talents[k].id == 200;
		check(rebased && old_ids, "talent ids: 200 x (stats + 1) + position on W2, 100 x on E10A2");
		bool slot_field = false;
		for (const auto& s : wm.zones[0].states) slot_field |= (s.flags_a & 1) && s.world_slot == 5 && s.payload[0] == 0;
		check(slot_field, "the W2 loader writes the world-item slot into the record's own u16");
		// the engine side's stream walkers take the profile (W2-L-06): a W2 stream walks with the W2 heads only
		{
			namespace R = save_format_rules;
			const auto hw = R::hero_heaps(w2s.bytes.data(), w2s.bytes.size(), R::w2_profile);
			const auto he = R::hero_heaps(e10.bytes.data(), e10.bytes.size(), R::e10a2_profile);
			const auto wrong = R::hero_heaps(w2s.bytes.data(), w2s.bytes.size(), R::e10a2_profile);
			check(hw && he && hw->size() == wm.heroes.size() && he->size() == hw->size() && (!wrong || wrong->size() != hw->size()), "hero_heaps walks a W2 stream with the W2 profile");
			const auto tw = R::old_play_time(w2s.bytes.data(), w2s.bytes.size(), R::w2_profile);
			const auto te = R::old_play_time(e10.bytes.data(), e10.bytes.size(), R::e10a2_profile);
			check(tw && te && *tw == *te && *tw == wm.game.play_time, "old_play_time finds the game block of a W2 stream");
			const auto pw = R::hero_points_records(w2s.bytes.data(), w2s.bytes.size(), R::w2_profile);
			const auto pe = R::hero_points_records(e10.bytes.data(), e10.bytes.size(), R::e10a2_profile);
			check(pw && pe && pw->records.size() == pe->records.size() && pw->restore_valid == pe->restore_valid, "hero_points_records walks a W2 stream");
			if (hw && hw->size()) check(std::memcmp(w2s.bytes.data() + (*hw)[0].block, e10.bytes.data() + (*he)[0].block, 0x18) == 0, "the first hero block's scalars agree");
		}
		// (2) the W2 saver: representable -> bit 0 only, the E10A2 build loads it; the stream reverses byte for byte
		NewFileContext ctx; ctx.saved_at = 5; ctx.campaign_serial = 17; ctx.save_sequence = 2; ctx.retained_file = legacy.bytes; ctx.require_capture_metadata = true;
		check(loaded_zone_metadata(legacy.bytes, tables, ctx.zone_captures, error, w, &reg) && ctx.zone_captures.size() == 1, "W2 loaded metadata: " + error);
		const auto saved = old_stream_to_new(w2s.bytes, tables, ctx, w);
		check(saved.ok, "W2 save: " + saved.error);
		if (!saved.ok) return;
		// E4 section 7, amended 10-27 after review: conservative marking. Bit 2
		// is independent of W2 bit 1. Every campaign written while the Stage 2
		// core is installed (the layout carries feature_actor_capacity96) is
		// marked; demand counting missed remembered zones and mixed demand.
		{
			auto actor_layout=w;actor_layout.feature_bits|=feature_actor_capacity96;
			const auto marked=[&](const Result& r){return r.ok&&(read_file(r.bytes.data(),r.bytes.size(),kind_campaign).header.required_features&feature_actor_capacity96)!=0;};
			const auto older_refuses=[&](const Result& r){const auto old_reader=new_to_old_stream(r.bytes,tables,w,&reg);return !old_reader.ok&&contains(old_reader.error,"needs a newer version");};
			{
				const auto result=old_stream_to_new(w2s.bytes,tables,ctx,actor_layout);
				check(result.ok,"E4 conservative marker save: "+result.error);
				if(result.ok){const auto bits=read_file(result.bytes.data(),result.bytes.size(),kind_campaign).header.required_features;
					check(bits==5u,"E4 every Stage2 write carries actor bit0x4 (bit0 + bit2, no W2 bit1)");
					const auto reload=new_to_old_stream(result.bytes,tables,actor_layout,&reg);
					check(reload.ok&&reload.bytes==w2s.bytes,"E4 marked save native round trip unchanged");
					check(older_refuses(result),"E4 older actor32 reader refuses readable newer-save marker");
				}
			}
			const auto old_load=new_to_old_stream(legacy.bytes,tables,actor_layout,&reg);
			check(old_load.ok&&old_load.bytes==w2s.bytes,"E4 existing feature0 save loads unchanged");
			auto float_state=wm;for(auto& state:float_state.zones[0].states)if(state.def_name){state.position[0]=0.5f;break;}
			const auto combined=old_stream_to_new(write_slot(float_state,w),tables,ctx,actor_layout);
			check(combined.ok&&read_file(combined.bytes.data(),combined.bytes.size(),kind_campaign).header.required_features==7,"E4 actor bit0x4 and NATIVE_W2 bit0x2 coexist");
			const auto old_w2=old_stream_to_new(write_slot(float_state,w),tables,ctx,w);
			const auto old_w2_load=new_to_old_stream(old_w2.bytes,tables,actor_layout,&reg);
			const auto old_w2_reference=new_to_old_stream(old_w2.bytes,tables,w,&reg);
			check(old_w2.ok&&!(read_file(old_w2.bytes.data(),old_w2.bytes.size(),kind_campaign).header.required_features&feature_actor_capacity96),"E4 a build without the core never marks bit0x4");
			check(old_w2_load.ok&&old_w2_load.bytes==old_w2_reference.bytes,"E4 existing bit0x2 W2 save loads unchanged");
			const auto prototype=*std::find_if(wm.zones[0].states.begin(),wm.zones[0].states.end(),[](const auto& s){return s.def_name!=0;});
			// Review case: a remembered zone with 40 living PLACED actors (no
			// dynamic list), saved while the current zone holds20.
			{
				auto placed=wm;auto& zone=placed.zones.back();zone.tail.dynamic_count=0;zone.tail.state_count=std::max(zone.tail.state_count,640u);
				for(unsigned i=0;i<40;++i)zone.states[600+i]=prototype;
				const auto stream=write_slot(placed,w);
				const auto remembered=old_stream_to_new(stream,tables,ctx,actor_layout);
				check(marked(remembered)&&older_refuses(remembered),"E4 remembered zone with40 placed actors requires the actor build: "+remembered.error);
				const auto unmarked=old_stream_to_new(stream,tables,ctx,w);
				const auto reload=new_to_old_stream(remembered.bytes,tables,actor_layout,&reg),reference=new_to_old_stream(unmarked.bytes,tables,w,&reg);
				check(reload.ok&&reference.ok&&reload.bytes==reference.bytes,"E4 remembered40 marker leaves the native stream unchanged: "+reload.error);
			}
			// Review case: mixed demand, 30 remembered dynamic actors plus the
			// party's heroes (34 actors in play with a four-hero party). Demand
			// counting saw only the 30 dynamic records. The synthetic tables
			// define two heroes, so the stream carries both hero blocks.
			{
				auto mixed=wm;check(!mixed.heroes.empty(),"E4 mixed fixture has hero blocks");auto& zone=mixed.zones.back();zone.tail.dynamic_count=30;zone.tail.state_count=std::max(zone.tail.state_count,630u);
				for(unsigned i=0;i<30;++i){zone.tail.dynamic[i]=static_cast<u16>(600+i);zone.states[600+i]=prototype;}
				const auto stream=write_slot(mixed,w);
				const auto result=old_stream_to_new(stream,tables,ctx,actor_layout);
				check(marked(result)&&older_refuses(result),"E4 mixed30 dynamic plus four heroes requires the actor build: "+result.error);
				const auto unmarked=old_stream_to_new(stream,tables,ctx,w);
				const auto reload=new_to_old_stream(result.bytes,tables,actor_layout,&reg),reference=new_to_old_stream(unmarked.bytes,tables,w,&reg);
				check(reload.ok&&reference.ok&&reload.bytes==reference.bytes,"E4 mixed marker leaves the native stream unchanged: "+reload.error);
			}
			// 33 remembered dynamic records of any class: marked, with no
			// classification query at save time.
			{
				auto dynamic=wm;auto& zone=dynamic.zones[0];
				zone.tail.dynamic_count=33;zone.tail.state_count=std::max(zone.tail.state_count,633u);
				for(unsigned i=0;i<33;++i){zone.tail.dynamic[i]=static_cast<u16>(600+i);zone.states[600+i]=prototype;}
				const auto stream=write_slot(dynamic,w);
				const auto result=old_stream_to_new(stream,tables,ctx,actor_layout);
				check(marked(result)&&older_refuses(result),"E4 remembered33 dynamic records marked without classification: "+result.error);
				const auto unmarked=old_stream_to_new(stream,tables,ctx,w);
				check(unmarked.ok&&!(read_file(unmarked.bytes.data(),unmarked.bytes.size(),kind_campaign).header.required_features&feature_actor_capacity96),"E4 same stream from a core-free build stays unmarked");
			}
		}
		const auto f = read_file(saved.bytes.data(), saved.bytes.size(), kind_campaign);
		check(f.header.required_features == 1 && representable(saved.bytes, tables, e).empty(), "a representable W2 save carries bit 0 only");
		const auto again = new_to_old_stream(saved.bytes, tables, w, &reg);
		check(again.ok && again.bytes == w2s.bytes, "W2 LC -> native -> LC -> native is byte-exact");
		const auto on_e10 = new_to_old_stream(saved.bytes, tables, e, &reg);
		check(on_e10.ok && on_e10.bytes == e10.bytes, "the E10A2 build loads the representable W2 save to its own stream");
		std::string why;
		check(semantic_equal(dump_new(saved.bytes, *t), dump_new(old_stream_to_new(e10.bytes, tables, ctx, e).bytes, *t), "", why),
			"the W2 and E10A2 saves of one campaign dump the same (" + why + ")");
		// (3) the probe's oracle (section 7): each case is a W2 native stream the saver translates
		auto save_with = [&](const std::function<void(OldSlot&)>& edit) -> Result
		{
			auto m = wm;
			edit(m);
			return old_stream_to_new(write_slot(m, w), tables, ctx, w);
		};
		auto marks = [&](const Result& r) { return r.ok && (read_file(r.bytes.data(), r.bytes.size(), kind_campaign).header.required_features & 2) != 0; };
		auto position = save_with([](OldSlot& m) { for (auto& s : m.zones[0].states) if (s.def_name) { s.position[0] = 0.5f; break; } });
		check(marks(position), "a state position 0.5 sets bit 1: " + position.error);
		if (position.ok)
		{
			const auto refused = new_to_old_stream(position.bytes, tables, e, &reg);
			check(!refused.ok && refused.error == w2_refused, "the E10A2 build refuses the bit-1 save by name: " + refused.error);
			const auto loads = new_to_old_stream(position.bytes, tables, w, &reg);
			bool half = false;
			if (loads.ok)
			{
				const auto lm = read_slot(loads.bytes.data(), loads.bytes.size(), w);
				for (const auto& s : lm.zones[0].states) half |= s.position[0] == 0.5f;
			}
			check(loads.ok && half, "the W2 build loads it with the float kept");
			check(contains(representable(position.bytes, tables, e), "state position not representable"), "the probe names the rule");
			// the slot list (W2-L-09): a build that knows bit 1 by name but runs E10A2 shows "(needs the W2 save layout)"
			namespace R = save_format_rules;
			const auto known = R::lc_known_required_features;
			R::lc_known_required_features = e.feature_bits;
			const auto on_e = R::inspect(position.bytes.data(), position.bytes.size(), R::kind_campaign);
			R::lc_known_required_features = w.feature_bits;
			const auto on_w = R::inspect(position.bytes.data(), position.bytes.size(), R::kind_campaign);
			R::lc_known_required_features = 0;
			const auto on_r = R::inspect(position.bytes.data(), position.bytes.size(), R::kind_campaign);
			R::lc_known_required_features = known;
			check(on_e.status == R::file_status::needs_newer && on_e.needs_layout && R::slot_text(on_e, false).rfind("(needs the W2 save layout) ", 0) == 0 &&
				contains(on_e.why, "SaveLayout=w2"), "the E10A2 slot list names the W2 layout: " + on_e.why);
			check(on_w.status == R::file_status::ok, "the W2 slot list lists the bit-1 save: " + on_w.why);
			check(on_r.status == R::file_status::needs_newer && !on_r.needs_layout && R::slot_text(on_r, false).rfind("(needs a newer version) ", 0) == 0,
				"a retail-layout build keeps the generic text");
		}
		auto big = save_with([](OldSlot& m) { for (auto& s : m.zones[0].states) if (s.def_name) { s.position[1] = 40000.0f; break; } });
		check(marks(big), "a position 40000 sets bit 1");
		auto handle = save_with([](OldSlot& m)
		{
			for (auto& z : m.zones) for (auto& s : z.states) if ((s.flags_a & 1) && s.world_slot == m.world[0].slot) s.world_slot = 150;
			m.world[0].slot = 150;
		});
		check(marks(handle) && contains(representable(handle.bytes, tables, e), "world handle outside native pool"), "a world-item handle 150 sets bit 1: " + handle.error + representable(handle.bytes, tables, e) + handle.ledger);
		auto handle149 = save_with([](OldSlot& m)
		{
			for (auto& z : m.zones) for (auto& s : z.states) if ((s.flags_a & 1) && s.world_slot == m.world[0].slot) s.world_slot = 149;
			m.world[0].slot = 149;
		});
		check(handle149.ok && !marks(handle149), "a world-item handle 149 does not");
		auto many = save_with([](OldSlot& m) { while (m.world.size() < 151) { auto x = m.world[0]; x.slot = static_cast<u32>(200 + m.world.size()); m.world.push_back(x); } });
		check(marks(many) && contains(representable(many.bytes, tables, e), "world handle outside native pool"), "more than 150 world items set bit 1: " + many.error + representable(many.bytes, tables, e));
		auto bare = [](OldSlot& m) { auto i = m.carried[0]; for (auto& l : i.lists) l.clear(); return i; };
		auto instances = save_with([&](OldSlot& m) { const auto i = bare(m); while (model_demand(m).instances < 257) m.carried.push_back(i); });
		check(marks(instances) && contains(representable(instances.bytes, tables, e), "native instance/enhancement/world pool exhausted"), "257 instances set bit 1: " + instances.error + representable(instances.bytes, tables, e));
		auto instances256 = save_with([&](OldSlot& m) { const auto i = bare(m); while (model_demand(m).instances < 256) m.carried.push_back(i); });
		check(instances256.ok && !marks(instances256), "256 instances do not: " + instances256.error);
		auto with_lists = [](const OldSlot& m) { for (const auto& c : m.carried) if (!c.lists[0].empty() || !c.lists[1].empty() || !c.lists[2].empty()) return c; return m.world[0].instance; };
		auto attached = save_with([&](OldSlot& m) { const auto i = with_lists(m); while (model_demand(m).attached_enhancements <= 375) m.carried.push_back(i); });
		check(marks(attached) && contains(representable(attached.bytes, tables, e), "native instance/enhancement/world pool exhausted"), "376 attached enhancements set bit 1: " + attached.error);
		auto attached375 = save_with([&](OldSlot& m)
		{
			const auto i = with_lists(m);
			std::uint32_t per = 0;
			for (const auto& l : i.lists) per += static_cast<std::uint32_t>(l.size());
			while (model_demand(m).attached_enhancements + per <= 375) m.carried.push_back(i);
		});
		check(attached375.ok && !marks(attached375), "at most 375 attached enhancements do not: " + attached375.error);
		// the shared definition of "does not set": a re-based talent id (catalogue ids in the file), retained sections
		check(!marks(saved), "re-based talent ids and retained LGCY / KEYS do not set bit 1");
		// (4) objectives: an objective this build numbers 256 is beyond the pre-W2 engine's byte index
		{
			auto t2 = make_tables(folder / "objectives", "sh_might", false);
			Tables tables2; tables2.data = t2;
			auto& T = t2->build->T;
			if (!T.OBJV.empty())
			{
				const auto base = T.OBJV.size();
				for (const u32 target : {255u, 256u, 300u, 1023u})
				{
					// synthetic objective ids up to the target index (the forward names each position by its id)
					T.OBJV.resize(base);
					for (u32 k = static_cast<u32>(base); k <= target; ++k) { T.OBJV.push_back(0x7f000000u + k); t2->p_obj[0x7f000000u + k] = k; }
					auto m = wm;
					m.mission.objectives[target] = 0x41;
					const auto r = old_stream_to_new(write_slot(m, w), tables2, ctx, w);
					const auto why2 = r.ok ? representable(r.bytes, tables2, e) : r.error;
					if (target == 255) check(r.ok && !marks(r), "objective index 255 does not set bit 1: " + why2);
					else check(marks(r), "objective index " + std::to_string(target) + " sets bit 1: " + why2);
					if (target == 1023 && r.ok)
					{
						const auto back = new_to_old_stream(r.bytes, tables2, w, &reg);
						check(back.ok && read_slot(back.bytes.data(), back.bytes.size(), w).mission.objectives[1023] == 0x41, "objective 1023 survives the W2 round trip");
					}
				}
			}
		}
		// (5) a build with more than 160 definitions: W2 carries it, E10A2 refuses its tables, the probe is per build
		{
			auto t3 = make_tables(folder / "items", "sh_might", false);
			Tables tables3; tables3.data = t3;
			t3->build->item.resize(300, "future");
			const auto loaded = new_to_old_stream(legacy.bytes, tables3, w, &reg);
			check(loaded.ok && read_slot(loaded.bytes.data(), loaded.bytes.size(), w).items.def_count == 300, "W2 loader with 300 definitions: " + loaded.error);
			const auto e10_refused = new_to_old_stream(legacy.bytes, tables3, e, &reg);
			check(!e10_refused.ok && contains(e10_refused.error, "native item definition budget exceeded (160)"), "E10A2 refuses a 300-definition build: " + e10_refused.error);
			if (loaded.ok)
			{
				const auto s3 = old_stream_to_new(loaded.bytes, tables3, ctx, w);
				check(s3.ok && !marks(s3), "the definition count is per build: a 300-definition W2 save is still representable: " + s3.error);
			}
			t3->build->item.resize(1025, "future");
			const auto over = new_to_old_stream(legacy.bytes, tables3, w, &reg);
			check(!over.ok && contains(over.error, "native item definition budget exceeded (1024)"), "W2 refuses 1025 definitions: " + over.error);
		}
		// (6) the record count: E10A2 refuses a W2 stream, W2 refuses an E10A2 stream (explicit layout, never by size)
		check(!old_stream_to_new(e10.bytes, tables, NewFileContext{}, w).ok && !old_stream_to_new(w2s.bytes, tables, NewFileContext{}, e).ok, "W2 and E10A2 streams are never confused");
		std::vector<TalentAdd> adds;
		check(missing_talents(w2s.bytes, tables, adds, error, w), "missing talents on a W2 stream: " + error);
		bool m200 = true;
		for (const auto& a : adds) m200 &= a.shared ? a.talent_id < 199 : a.talent_id >= 200 * (a.stats_index + 1) && a.talent_id < 200 * (a.stats_index + 2);
		check(m200, "missing talents use M = 200 and tell shared talents by their table");
		// (7) F1: the probe-oracle fixtures section 7 lists that (3)-(6) do not reach (W2 review, Fable): each case is
		// a boundary pair, the first value marks and the one below it does not
		{
			int f1_checks = 0;
			const auto f1 = [&](bool ok, const std::string& what) { ++f1_checks; check(ok, what); };
			// add a catalogue entry to a test build (the forward names it, the reverse finds its position)
			auto entry = [](TablesData& td, unsigned domain, const std::vector<std::string_view>& fields)
			{
				Cat cat; cat.c = td.catalogue;
				for (const auto& x : cat.c.entries) cat.next[x.domain] = std::max(cat.next[x.domain], x.id & 0xffffffu);
				const u32 id = cat.add(domain, fields);
				td.catalogue = cat.c;
				for (const auto& x : td.catalogue.entries) if (x.id == id) td.names[id] = unescape_key(x.key);
				return id;
			};
			// (7a) an enhancement record >= 512: a build whose enhancement table has 601 records, record N in an item's own list
			{
				auto t4 = make_tables(folder / "f1-enhancements", "sh_might", false);
				Tables tables4; tables4.data = t4;
				auto& T = t4->build->T;
				const u32 def = wm.carried[0].head.definition;
				while (T.ENHC.size() < 601)
				{
					const u32 id = entry(*t4, 3, {"i", "f1_item", std::to_string(T.ENHC.size())});
					t4->p_enh[id] = static_cast<u32>(T.ENHC.size());
					T.ENHC.push_back(id);
					T.ITEM_own[def].push_back(id);
				}
				for (const u32 record : {511u, 512u, 600u})
				{
					auto m = wm;
					m.carried[0].lists[1].push_back(OldEnhElement{static_cast<u16>(record), 7});
					const auto r = old_stream_to_new(write_slot(m, w), tables4, ctx, w);
					const auto why4 = r.ok ? representable(r.bytes, tables4, e) : r.error;
					if (record == 511) f1(r.ok && !marks(r), "F1 enhancement record 511 does not set bit 1: " + why4);
					else f1(marks(r) && contains(why4, ("enhancement record " + std::to_string(record) + " >= 512").c_str()), "F1 enhancement record " + std::to_string(record) + " sets bit 1: " + why4);
					if (record == 600 && r.ok)
					{
						const auto back = new_to_old_stream(r.bytes, tables4, w, &reg);
						bool kept = false;
						if (back.ok) for (const auto& c : read_slot(back.bytes.data(), back.bytes.size(), w).carried) for (const auto& x : c.lists[1]) kept |= x.record == 600 && x.value == 7;
						f1(kept, "F1 enhancement record 600 survives the W2 round trip: " + back.error);
					}
				}
			}
			// (7b) more than 5 zone records: N live (captured) records of N zones, saved by the W2 writer
			{
				auto t5 = make_tables(folder / "f1-zones", "sh_might", false);
				Tables tables5; tables5.data = t5;
				std::vector<u32> zones;
				std::vector<Value> digests;
				for (int k = 0; k < 7; ++k)
				{
					const std::string name = "zone/x/r" + std::to_string(k);
					const u32 id = entry(*t5, 15, {name});
					t5->reference.ZONE.push_back(id); t5->build->T.ZONE.push_back(id); t5->p_zone[id] = name;
					fs::copy_file(folder / "f1-zones" / "Maps" / "zone" / "x" / "y.engb", folder / "f1-zones" / "Maps" / "zone" / "x" / ("r" + std::to_string(k) + ".engb"),
						fs::copy_options::overwrite_existing);
					fs::copy_file(folder / "f1-zones" / "Packages/generated/maps/zone/x/y.pkgb", folder / "f1-zones" / "Packages/generated/maps/zone/x" / ("r" + std::to_string(k) + ".pkgb"),
						fs::copy_options::overwrite_existing);
					const auto bits = conversation_bits(*t5->build, name);
					const auto digest = conversation_package_digest(key_hash1(name, Enc::latin1), bits);
					t5->conversation_tables[{digest, key_hash1(name, Enc::latin1)}] = bits;
					digests.push_back(Value::bytes(digest.data(), digest.size()));
					zones.push_back(id);
				}
				const auto base = read_file(saved.bytes.data(), saved.bytes.size(), kind_campaign);
				auto with_records = [&](u32 n)
				{
					std::vector<SectionOut> sections;
					for (const auto& tag : base.order)
					{
						const auto& sec = base.sections.at(tag);
						Value data = sec.data;
						if (tag == "ZONE")
						{
							auto records = data["records"];
							const auto first = records.l[0];
							for (u32 k = 1; k < n; ++k)
							{
								auto r = first;
								r.set("zone", Value::integer(zones[k - 1]));
								r.set("conversation_digest", digests[k - 1]);
								r.set("states", Value::list());          // a visited zone whose entities kept their placed state
								r.set("time_stamp", Value::real(static_cast<double>(k)));
								records.push(r);
							}
							data.set("records", records);
						}
						sections.push_back({tag, sec.entry.major, sec.entry.minor, sec.entry.flags, data});
					}
					HeaderIn h;
					h.required_features = feature_entity_layout_contiguous10;
					h.campaign_serial = 17; h.save_sequence = 3;
					return write_file(kind_campaign, sections, h);
				};
				for (const u32 n : {5u, 6u, 8u})
				{
					const auto file = with_records(n);
					const auto loaded = new_to_old_stream(file, tables5, w, &reg);
					const auto lm = loaded.ok ? read_slot(loaded.bytes.data(), loaded.bytes.size(), w) : OldSlot{};
					f1(loaded.ok && lm.zones.size() == n, "F1 the W2 loader applies " + std::to_string(n) + " zone records: " + loaded.error + " " + loaded.ledger);
					if (!loaded.ok) continue;
					NewFileContext c5 = ctx;
					c5.retained_file = file;
					c5.zone_captures.clear();
					f1(loaded_zone_metadata(file, tables5, c5.zone_captures, error, w, &reg) && c5.zone_captures.size() == n, "F1 metadata of " + std::to_string(n) + " records: " + error);
					const auto r = old_stream_to_new(loaded.bytes, tables5, c5, w);
					const auto why5 = r.ok ? representable(r.bytes, tables5, e) : r.error;
					if (n == 5) f1(r.ok && !marks(r), "F1 5 zone records do not set bit 1: " + why5);
					else
					{
						f1(marks(r) && contains(why5, "zone record"), "F1 " + std::to_string(n) + " zone records set bit 1: " + why5);
						if (r.ok)
						{
							const auto refused = new_to_old_stream(r.bytes, tables5, e, &reg);
							f1(!refused.ok && refused.error == w2_refused, "F1 the E10A2 build refuses the " + std::to_string(n) + "-record save by name: " + refused.error);
							const auto back = new_to_old_stream(r.bytes, tables5, w, &reg);
							f1(back.ok && read_slot(back.bytes.data(), back.bytes.size(), w).zones.size() == n, "F1 the W2 build loads " + std::to_string(n) + " records back: " + back.error);
						}
					}
				}
			}
			// (7c) a talent position >= 100 / a shared talent id >= 99 on the target's mapping (M = 100); W2 holds them (M = 200)
			{
				auto t6 = make_tables(folder / "f1-talents", "sh_might", false);
				Tables tables6; tables6.data = t6;
				auto& T = t6->build->T;
				const u32 si = 1;                                  // wm.heroes[0]: stats index 1, own ids 2 x 200 + position
				while (T.TALN_hero[si].size() < 101)
				{
					const u32 id = entry(*t6, 7, {"hero_f1", std::to_string(T.TALN_hero[si].size())});
					t6->p_tal[id] = talent_code(si + 1, static_cast<u32>(T.TALN_hero[si].size()));
					T.TALN_hero[si].push_back(id);
				}
				while (T.TALN_shared.size() < 100)
				{
					const u32 id = entry(*t6, 7, {"shared", "f1_" + std::to_string(T.TALN_shared.size())});
					t6->p_tal[id] = talent_code(0, static_cast<u32>(T.TALN_shared.size()));
					T.TALN_shared.push_back(id);
				}
				auto with_talent = [&](u16 id)
				{
					auto m = wm;
					auto& a = m.heroes[0].attributes;
					std::vector<OldTalentEntry> heap(a.talents, a.talents + a.tail.count);
					auto x = heap.empty() ? OldTalentEntry{} : heap[0];
					x.id = id;
					heap.push_back(x);
					std::sort(heap.begin(), heap.end(), [](const OldTalentEntry& p, const OldTalentEntry& q) { return p.id < q.id; });
					for (std::size_t k = 0; k < heap.size(); ++k) a.talents[k] = heap[k];
					a.tail.count = static_cast<u32>(heap.size());
					return old_stream_to_new(write_slot(m, w), tables6, ctx, w);
				};
				const auto p99 = with_talent(static_cast<u16>(2 * 200 + 99));
				f1(p99.ok && !marks(p99), "F1 talent position 99 does not set bit 1: " + (p99.ok ? representable(p99.bytes, tables6, e) : p99.error));
				const auto p100 = with_talent(static_cast<u16>(2 * 200 + 100));
				const auto why100 = p100.ok ? representable(p100.bytes, tables6, e) : p100.error;
				f1(marks(p100) && contains(why100, "talent position 100"), "F1 talent position 100 sets bit 1: " + why100);
				if (p100.ok)
				{
					const auto back = new_to_old_stream(p100.bytes, tables6, w, &reg);
					bool kept = false;
					if (back.ok) for (u32 k = 0; k < read_slot(back.bytes.data(), back.bytes.size(), w).heroes[0].attributes.tail.count; ++k)
						kept |= read_slot(back.bytes.data(), back.bytes.size(), w).heroes[0].attributes.talents[k].id == 500;
					f1(kept, "F1 talent position 100 survives the W2 round trip as id 500: " + back.error);
				}
				const auto s98 = with_talent(98);
				f1(s98.ok && !marks(s98), "F1 shared talent id 98 does not set bit 1: " + (s98.ok ? representable(s98.bytes, tables6, e) : s98.error));
				const auto s99 = with_talent(99);
				const auto why99 = s99.ok ? representable(s99.bytes, tables6, e) : s99.error;
				f1(marks(s99) && contains(why99, "shared talent position 99"), "F1 shared talent id 99 sets bit 1: " + why99);
			}
			// (7d) "a build registering more than 256 objectives marks every save": no objective above 255 holds any state
			{
				auto t7 = make_tables(folder / "f1-objectives", "sh_might", false);
				Tables tables7; tables7.data = t7;
				auto& T = t7->build->T;
				const auto base = T.OBJV.size();
				for (const u32 registered : {256u, 257u})
				{
					T.OBJV.resize(base);
					for (u32 k = static_cast<u32>(base); k < registered; ++k) { T.OBJV.push_back(0x7e000000u + k); t7->p_obj[0x7e000000u + k] = k; }
					auto m = wm;
					for (u32 k = static_cast<u32>(base); k < max_snapshot_objectives; ++k) m.mission.objectives[k] = 0;
					for (auto& s : m.side_frames) for (u32 k = static_cast<u32>(base); k < max_snapshot_objectives; ++k) s.objectives[k] = 0;
					const auto r = old_stream_to_new(write_slot(m, w), tables7, ctx, w);
					const auto why7 = r.ok ? representable(r.bytes, tables7, e) : r.error;
					if (registered == 256) f1(r.ok && !marks(r), "F1 a build registering 256 objectives does not mark a save: " + why7);
					else f1(marks(r) && contains(why7, "objective index 256 > 255"), "F1 a build registering 257 objectives marks a save with no state above 255: " + why7);
				}
			}
			std::printf("F1 probe-oracle fixtures: %d checks\n", f1_checks);
		}
	}
}

void run_vectors(const fs::path& scratch)
{
	test_keys();
	test_layout();
	test_numbering(scratch);
	test_oldsave();
	test_container();
	test_catalogue();
	test_align();
	test_forge_import(scratch);
	test_convert(scratch / "convert");
	test_talent_era(scratch / "era");
	test_api(scratch / "api");
	test_rebuilt_own(scratch / "rebuilt");
	test_rebuilt_removed_records(scratch / "rebuilt-removed");
	test_rebuilt_equipment_class(scratch / "rebuilt-class");
	test_capture_twins(scratch / "twins");
	test_zone_prefix_review(scratch / "zone-prefix-review");
	test_review_resets(scratch / "review");
	test_native_dangling_world_state(scratch / "dangling-world");
	test_native_transfer_metadata(scratch / "native-transfer");
	test_native_table_budgets(scratch / "native-budgets");
	test_native_layouts();
	test_oldsave_layouts();
	test_feature_marker();
	test_typed_registry(scratch / "registry");
	test_autospend(scratch / "autospend");
	test_e10a2_api(scratch / "e10a2");
	test_w2_layout();
	test_w2_oldsave();
	test_w2_api(scratch / "w2");
}

// ------------------------------------------------------------------------------------------------ (c) fuzz
namespace
{
	struct Rng
	{
		std::uint64_t s;
		std::uint32_t next()
		{
			s ^= s << 13;
			s ^= s >> 7;
			s ^= s << 17;
			return static_cast<std::uint32_t>(s);
		}
		std::uint32_t below(std::uint32_t n) { return n ? next() % n : 0; }
	};

	// recompute the CRCs of a (damaged) LC file where its table allows, so that mutations reach the decoders
	void fix_crcs(std::vector<std::uint8_t>& b)
	{
		if (b.size() < new_header_size) return;
		NewHeader h;
		std::memcpy(&h, b.data(), sizeof h);
		const std::uint64_t to = h.section_table_offset;
		for (u32 i = 0; i < h.section_count && to + (i + 1ull) * new_section_entry_size <= b.size() && i < 64; ++i)
		{
			NewSectionEntry e;
			std::memcpy(&e, b.data() + to + i * new_section_entry_size, sizeof e);
			if (std::uint64_t(e.offset) + e.length <= b.size())
			{
				e.crc = crc32(b.data() + e.offset, e.length);
				std::memcpy(b.data() + to + i * new_section_entry_size, &e, sizeof e);
			}
		}
		h.file_length = static_cast<u32>(b.size());
		if (to <= b.size()) h.body_crc = crc32(b.data() + to, b.size() - static_cast<std::size_t>(to));
		std::memcpy(b.data(), &h, sizeof h);
		h.header_crc = crc32(b.data(), 0x84);
		std::memcpy(b.data(), &h, sizeof h);
	}
}

void fuzz_core(const Tables& tables, const std::vector<std::vector<std::uint8_t>>& olds, int iterations, const char* label);

void run_fuzz(const fs::path& scratch, int iterations, const std::vector<std::vector<std::uint8_t>>& extra_old)
{
	const auto t = make_tables(scratch / "fuzz", "sh_might", true);
	Tables tables;
	tables.data = t;
	std::vector<std::vector<std::uint8_t>> olds{write_slot(make_old_save({0, 3, 4}, 10)), write_slot(make_old_save({0, 2, 3}, 10))};
	for (const auto& e : extra_old) olds.push_back(e);
	fuzz_core(tables, olds, iterations, "synthetic saves");
}

void fuzz_core(const Tables& tables, const std::vector<std::vector<std::uint8_t>>& olds, int iterations, const char* label)
{
	std::vector<std::vector<std::uint8_t>> news;
	for (const auto& o : olds)
	{
		const auto r = convert_old_to_new(o, tables);
		if (r.ok) news.push_back(r.bytes);
	}
	Rng rng{0x9e3779b97f4a7c15ull};
	int old_errors = 0, old_ok = 0, new_errors = 0, new_ok = 0;
	for (int it = 0; it < iterations; ++it)
	{
		// old reader: truncate, flip bytes in the used part, or plant a large count at an aligned offset
		auto b = olds[rng.below(static_cast<std::uint32_t>(olds.size()))];
		const auto kind = rng.below(4);
		const std::uint32_t used = 0x20000;
		if (kind == 0)
		{
			b.resize(rng.below(static_cast<std::uint32_t>(b.size())));
		}
		else if (kind == 1 || kind == 2)
		{
			const auto n = 1 + rng.below(16);
			for (std::uint32_t k = 0; k < n; ++k) b[rng.below(kind == 1 ? 0x2000 : used)] ^= static_cast<std::uint8_t>(1 + rng.below(255));
		}
		else
		{
			const auto off = (rng.below(used) & ~3u);
			const std::uint32_t big[] = {0xffffffffu, 0x80000000u, 65, 4097, 151, 769, 300, 20, 6};
			std::memcpy(b.data() + off, &big[rng.below(9)], 4);
		}
		const auto r = convert_old_to_new(b, tables);
		(r.ok ? old_ok : old_errors)++;
		if (!r.ok && r.error.empty()) check(false, "an old-file refusal without a message");
		std::string err;
		dump(b, tables, err);
		std::vector<TalentAdd> adds;
		missing_talents(b, tables, adds, err);
		// new reader: flip bytes (CRCs fixed afterwards half of the time so the decoders see the damage)
		if (news.empty()) continue;
		auto n = news[rng.below(static_cast<std::uint32_t>(news.size()))];
		const auto nk = rng.below(3);
		if (nk == 0) n.resize(rng.below(static_cast<std::uint32_t>(n.size())));
		else
		{
			const auto cnt = 1 + rng.below(8);
			for (std::uint32_t k = 0; k < cnt; ++k) n[rng.below(static_cast<std::uint32_t>(n.size()))] ^= static_cast<std::uint8_t>(1 + rng.below(255));
			if (nk == 2) fix_crcs(n);
		}
		const auto back = new_to_old_stream(n, tables);
		(back.ok ? new_ok : new_errors)++;
		if (!back.ok && back.error.empty()) check(false, "a new-file refusal without a message");
		dump(n, tables, err);
	}
	std::printf("fuzz (%s): %d mutated old files (%d refused with a message, %d still converted), %d mutated LC files (%d refused, %d still read); "
		"no crash\n", label, iterations, old_errors, old_ok, iterations, new_errors, new_ok);
}
