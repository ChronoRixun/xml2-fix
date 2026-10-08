// lcsave_test (a) and (c): the vectors of legends-classic tests/unit/test_lcsave.py with the same invented build
// ("hero_a", "zone/x/y" - never game text) made in code, and a fuzz of the readers. The tables a conversion needs
// are built here the way the builder's files describe them (catalogue ids by position, canonical keys).

#include "align.hpp"
#include "internal.hpp"
#include "lcsave.hpp"
#include "xmlb.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
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
		for (u32 i = 0; i < ref.TALN_shared.size(); ++i) t->p_tal[ref.TALN_shared[i]] = i;
		for (const auto& [si, l] : ref.TALN_hero) for (u32 p = 0; p < l.size(); ++p) t->p_tal[l[p]] = (si + 1) * 100 + p;
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
		std::memset(&st, 0, sizeof st);
		st.generation = 1;
		st.used = 2;
		st.offsets[0] = 1;
		auto intern = [&](u32 slot, const std::string& text)
		{
			const u32 off = st.used;
			std::memcpy(st.arena + off, text.c_str(), text.size() + 1);
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
			z.states[1] = OldStateRecord{h_gate, h_g1, {1, 2, 3, 4, 5, 6, 7, 8}, 0, 8, {10, -20, 30}};
			z.states[2] = OldStateRecord{h_gate, h_g2, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}, 0, 8, {1, 2, 3}};
			z.states[3] = OldStateRecord{h_crate, h_c2, {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22}, 0, 8 | 16, {4, 5, 6}};
			z.states[4] = OldStateRecord{h_dyn, h_dn, {5, 3, 0, 0x33, 0x33, 0x33, 0x33, 0x33}, 1, 0, {7, 8, 9}};
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
		check(sizeof(NewHeader) == 0x88 && 4 + old_zone_states * sizeof(OldStateRecord) + sizeof(OldZoneRecordTail) == 0x55e0, "layout sizes");
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
	}

	// the API on the synthetic tables: missing talents, the saver's direction, dump of both formats
	void test_api(const fs::path& folder)
	{
		auto t = make_tables(folder, "sh_might", false);
		auto* b = const_cast<Build*>(t->build.get());
		b->herostat_talents[1] = {200, 201, 202, 0, 3, 4, 1};
		b->herostat_talents[2] = {300, 301, 0, 3, 4, 1};
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
				observed[0].conversation_digest.fill('c');
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
							check(cached.ok && read_file(cached.bytes.data(), cached.bytes.size()).data("ZONE")["records"].as_list()[0]["conversation_digest"].as_bytes() == std::string(32, 'c'), "cached per-zone provenance not overwritten");
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
		check(conversation_digest(tables, capture.conversation_digest, error), "current conversation digest");
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
	test_convert(scratch / "convert");
	test_talent_era(scratch / "era");
	test_api(scratch / "api");
	test_capture_twins(scratch / "twins");
	test_native_table_budgets(scratch / "native-budgets");
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
