// XMen2.exe's own save format (SPEC-old-save section 1): the bounds-checked reader (file -> model) and the
// writer (model -> bytes), as oldsave.py. Every byte of the payload is represented (pads, stale entries and
// residue included) so that write(read(b)) == b for every file the engine wrote; the reader refuses anything the
// engine itself does not bound (old 1.18).

#include "internal.hpp"

#include <cstdio>
#include <cstring>

namespace lcsave::detail
{
	namespace
	{
		std::string fmt_hex(std::uint64_t v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "0x%llx", static_cast<unsigned long long>(v));
			return b;
		}

		struct Reader
		{
			const std::uint8_t* b;
			std::size_t size;
			std::size_t p;

			template <typename T> void get(T& out)
			{
				if (p + sizeof(T) > size)
				{
					fail("struct needs " + std::to_string(sizeof(T)) + " bytes at " + fmt_hex(p) + ", buffer has " + fmt_hex(size));
				}
				std::memcpy(&out, b + p, sizeof(T));
				p += sizeof(T);
			}
			template <typename T> T get()
			{
				T v{};
				get(v);
				return v;
			}
			std::string take(std::size_t n)
			{
				if (p + n > size)
				{
					fail("need " + std::to_string(n) + " bytes at " + fmt_hex(p) + ", payload ends at " + fmt_hex(size));
				}
				std::string v(reinterpret_cast<const char*>(b + p), n);
				p += n;
				return v;
			}
			std::string tag(const char* expect)
			{
				const auto raw = take(old_tag);
				const auto text = cstr(raw.data() + 1, 50);
				if (static_cast<u8>(raw[0]) != old_tag_marker || static_cast<u8>(raw[51]) != old_tag_marker ||
					text.compare(0, std::strlen(expect), expect) != 0)
				{
					fail("expected tag " + py_repr(expect, Enc::cp1252) + " at payload " + fmt_hex(p - old_tag) + ", found " +
						py_repr(text.substr(0, 40), Enc::cp1252));
				}
				return text;
			}
			bool peek_tag(const char* expect) const
			{
				if (p + old_tag > size || b[p] != old_tag_marker)
				{
					return false;
				}
				const auto n = std::strlen(expect);
				return std::memcmp(b + p + 1, expect, n) == 0;
			}
		};

		OldInstance read_instance(Reader& r)
		{
			static const char* const names[3] = {"prefixes", "own", "suffixes"};
			OldInstance inst;
			r.get(inst.head);
			for (int k = 0; k < 3; ++k)
			{
				const auto n = r.get<u32>();
				if (n > ceil_enh_per_list)
				{
					fail(std::string("enhancement list ") + names[k] + " count " + std::to_string(n) + " > " + std::to_string(ceil_enh_per_list));
				}
				inst.lists[k].resize(n);
				for (auto& e : inst.lists[k])
				{
					r.get(e);
				}
			}
			return inst;
		}

		template <typename T> void put(std::vector<std::uint8_t>& out, const T& v)
		{
			const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
			out.insert(out.end(), p, p + sizeof(T));
		}

		void write_instance(std::vector<std::uint8_t>& out, const OldInstance& inst)
		{
			put(out, inst.head);
			for (const auto& lst : inst.lists)
			{
				put(out, static_cast<u32>(lst.size()));
				for (const auto& e : lst)
				{
					put(out, e);
				}
			}
		}

		OldAttributes read_attributes(Reader& r)
		{
			OldAttributes a;
			r.get(a.scalars);
			for (auto& e : a.talents)
			{
				r.get(e);
			}
			r.get(a.tail);
			if (a.tail.count > old_talent_entries)
			{
				fail("talent count " + std::to_string(a.tail.count) + " > " + std::to_string(old_talent_entries));
			}
			return a;
		}

		void write_attributes(std::vector<std::uint8_t>& out, const OldAttributes& a)
		{
			put(out, a.scalars);
			for (const auto& e : a.talents)
			{
				put(out, e);
			}
			put(out, a.tail);
		}

		OldEquipment read_equipment(Reader& r)
		{
			const auto n = r.get<u8>();
			if (n > ceil_equipped)
			{
				fail("equipped count " + std::to_string(n) + " > " + std::to_string(ceil_equipped));
			}
			OldEquipment eq;
			for (u32 k = 0; k < n; ++k)
			{
				eq.equipped.push_back(read_instance(r));
			}
			r.get(eq.tail);
			return eq;
		}

		void write_equipment(std::vector<std::uint8_t>& out, const OldEquipment& eq)
		{
			if (eq.equipped.size() > 255)
			{
				fail("ubyte format requires 0 <= number <= 255");
			}
			put(out, static_cast<u8>(eq.equipped.size()));
			for (const auto& i : eq.equipped)
			{
				write_instance(out, i);
			}
			put(out, eq.tail);
		}

		// the writer's cstr rule (layout.write_struct): the text must encode as cp1252 and leave room for the NUL
		void check_cstr(const char* field, std::size_t size, const char* name)
		{
			const auto text = cstr(field, size);
			if (!encodable_cp1252(text, Enc::cp1252))
			{
				fail(std::string("'charmap' codec can't encode the text of ") + name);
			}
			if (text.size() >= size)
			{
				fail(std::string(name) + ": text of " + std::to_string(text.size()) + " bytes does not fit " + std::to_string(size));
			}
		}
		template <std::size_t N> void check_cstr(const char (&field)[N], const char* name) { check_cstr(field, N, name); }

		void check_snapshot(const OldSnapshot& s)
		{
			check_cstr(s.zone, "zone");
			for (const auto& n : s.seat_name)
			{
				check_cstr(n, "seat_name");
			}
		}

		void tag_bytes(std::vector<std::uint8_t>& out, const std::string& text)
		{
			if (!encodable_cp1252(text, Enc::cp1252))
			{
				fail("'charmap' codec can't encode a tag");
			}
			out.push_back(old_tag_marker);
			if (text.size() < 50)
			{
				out.insert(out.end(), text.begin(), text.end());
				out.push_back(0);
				out.insert(out.end(), 50 - text.size() - 1, '*');
			}
			else
			{
				out.insert(out.end(), text.begin(), text.begin() + 50);
			}
			out.push_back(old_tag_marker);
		}
	}

	OldSlot::OldSlot(const OldSlot& o)
	{
		*this = o;
	}

	OldSlot& OldSlot::operator=(const OldSlot& o)
	{
		if (this == &o)
		{
			return *this;
		}
		std::memcpy(description_raw, o.description_raw, sizeof description_raw);
		begin_tag = o.begin_tag;
		end_tag = o.end_tag;
		items = o.items;
		carried = o.carried;
		world = o.world;
		heroes = o.heroes;
		std::memcpy(unlocked, o.unlocked, sizeof unlocked);
		unlocked_count = o.unlocked_count;
		restore_flag = o.restore_flag;
		restore = o.restore;
		game = o.game;
		std::memcpy(side_frames, o.side_frames, sizeof side_frames);
		side = o.side;
		courses = o.courses;
		course_residue = o.course_residue;
		std::memcpy(grades, o.grades, sizeof grades);
		review = o.review;
		zone_strings = std::make_unique<OldZoneStrings>(*o.zone_strings);
		zones = o.zones;
		mission = o.mission;
		profile = o.profile;
		std::memcpy(zone_vars, o.zone_vars, sizeof zone_vars);
		std::memcpy(game_vars, o.game_vars, sizeof game_vars);
		town_centres = o.town_centres;
		slack = o.slack;
		return *this;
	}

	std::string cstr(const char* field, std::size_t size)
	{
		std::size_t n = 0;
		while (n < size && field[n])
		{
			++n;
		}
		return std::string(field, n);
	}

	void set_cstr(char* field, std::size_t size, const std::string& text, Enc enc)
	{
		if (!encodable_cp1252(text, enc))
		{
			fail("'charmap' codec can't encode character in " + py_repr(text, enc));
		}
		if (text.size() >= size)
		{
			fail("text of " + std::to_string(text.size()) + " bytes does not fit " + std::to_string(size));
		}
		std::memset(field, 0, size);
		std::memcpy(field, text.data(), text.size());
	}

	std::string zone_string(const OldZoneStrings& table, u32 handle)
	{
		if (handle == 0)
		{
			return {};
		}
		const u32 slot = handle & 0xffffff;
		if (slot >= old_zone_string_slots)
		{
			fail("zone string handle " + fmt_hex(handle) + ": slot beyond the table");
		}
		const u32 off = table.offsets[slot];
		if (off == 0 || off >= old_zone_arena)
		{
			fail("zone string handle " + fmt_hex(handle) + ": slot " + std::to_string(slot) + " is free or its offset " + fmt_hex(off) +
				" is outside the arena");
		}
		const void* end = std::memchr(table.arena + off, 0, old_zone_arena - off);
		if (!end)
		{
			fail("zone string at " + fmt_hex(off) + " is not terminated");
		}
		return std::string(reinterpret_cast<const char*>(table.arena + off), static_cast<const u8*>(end) - (table.arena + off));
	}

	u32 script_var_value(const OldScriptVar& v)
	{
		return u32(v.value_be[0]) << 24 | u32(v.value_be[1]) << 16 | u32(v.value_be[2]) << 8 | v.value_be[3];
	}

	namespace
	{
		OldProfile read_profile(Reader& r)
		{
			OldProfile p;
			r.get(p.head);
			for (auto& s : p.high_scores)
			{
				r.get(s);
			}
			r.get(p.tail);
			if (p.tail.score_count > old_high_scores)
			{
				fail("high-score count " + std::to_string(p.tail.score_count) + " > " + std::to_string(old_high_scores));
			}
			return p;
		}

		void check_indices(const OldSlot& m)
		{
			for (const auto& z : m.zones)
			{
				const u32 n = z.tail.state_count;
				for (u32 k = 0; k < old_zone_ordinals; ++k)
				{
					const u32 idx = z.tail.ordinal_map[k];
					if (idx && idx >= n)
					{
						fail("zone record: ordinal " + std::to_string(k + 1) + " names state record " + std::to_string(idx) + " >= count " +
							std::to_string(n));
					}
				}
				auto check_list = [&](const u16* lst, u32 cnt, const char* what)
				{
					for (u32 k = 0; k < cnt; ++k)
					{
						const u32 idx = lst[k];
						if (idx == 0 || idx >= n)
						{
							fail(std::string("zone record: ") + what + " list names state record " + std::to_string(idx) + " (count " +
								std::to_string(n) + ")");
						}
					}
				};
				check_list(z.tail.dynamic, z.tail.dynamic_count, "dynamic");
				check_list(z.tail.smart, z.tail.smart_count, "smart");
				zone_string(*m.zone_strings, z.name_handle);
				for (u32 idx = 1; idx < n; ++idx)
				{
					zone_string(*m.zone_strings, z.states[idx].def_name);
					zone_string(*m.zone_strings, z.states[idx].inst_name);
				}
			}
			for (u32 i = 0; i < m.unlocked_count; ++i)
			{
				if (m.unlocked[i] < 0)
				{
					fail("unlocked entry " + std::to_string(i) + " is negative");
				}
			}
		}
	}

	OldSlot read_slot(const std::uint8_t* data, std::size_t size)
	{
		if (size != old_slot_size)
		{
			fail("file size " + std::to_string(size) + " is not the slot size " + std::to_string(old_slot_size) + " (0x84 + 0x2fc00)");
		}
		u32 plen = 0;
		std::memcpy(&plen, data + 0x80, 4);
		if (plen != old_payload)
		{
			fail("payload length field " + fmt_hex(plen) + " is not " + fmt_hex(old_payload));
		}
		OldSlot m;
		std::memcpy(m.description_raw, data, 0x80);
		Reader r{data, size, old_envelope};
		m.begin_tag = r.tag("[SAVEGAMEBEGIN");
		// ---- items (old 1.4)
		r.get(m.items);
		const auto n1 = r.get<u32>();
		if (n1 > ceil_instances)
		{
			fail("carried item count " + std::to_string(n1) + " > " + std::to_string(ceil_instances));
		}
		for (u32 k = 0; k < n1; ++k)
		{
			m.carried.push_back(read_instance(r));
		}
		const auto n2 = r.get<u32>();
		if (n2 > ceil_world_slot)
		{
			fail("world item count " + std::to_string(n2) + " > " + std::to_string(ceil_world_slot));
		}
		for (u32 k = 0; k < n2; ++k)
		{
			OldWorld w;
			w.slot = r.get<u32>();
			if (w.slot >= ceil_world_slot)
			{
				fail("world item slot " + std::to_string(w.slot) + " >= " + std::to_string(ceil_world_slot));
			}
			w.instance = read_instance(r);
			m.world.push_back(std::move(w));
		}
		// ---- hero blocks (old 1.6): no count; until the next tag
		while (!r.peek_tag("[CHARACTERSUNLOCKED]"))
		{
			if (m.heroes.size() >= ceil_hero_blocks)
			{
				fail("no [CHARACTERSUNLOCKED] tag after " + std::to_string(ceil_hero_blocks) + " hero blocks");
			}
			OldHero h;
			h.attributes = read_attributes(r);
			h.equipment = read_equipment(r);
			r.get(h.powers);
			r.get(h.costume);
			m.heroes.push_back(std::move(h));
		}
		// ---- unlocked list and restore records (old 1.7)
		r.tag("[CHARACTERSUNLOCKED]");
		for (auto& e : m.unlocked)
		{
			r.get(e);
		}
		m.unlocked_count = r.get<u32>();
		if (m.unlocked_count > old_unlocked_slots)
		{
			fail("unlocked count " + std::to_string(m.unlocked_count) + " > " + std::to_string(old_unlocked_slots));
		}
		r.tag("[CHARACTERSRESTORE]");
		m.restore_flag = r.get<u8>();
		for (u32 k = 0; k < old_restore_records; ++k)
		{
			OldRestore rr;
			rr.attributes = read_attributes(r);
			rr.equipment = read_equipment(r);
			m.restore.push_back(std::move(rr));
		}
		// ---- game block (old 1.8)
		r.get(m.game);
		// ---- side-mission stack (old 1.9)
		for (auto& f : m.side_frames)
		{
			r.get(f);
		}
		r.get(m.side);
		if (m.side.count > old_side_frames)
		{
			fail("side-mission stack count " + std::to_string(m.side.count) + " > " + std::to_string(old_side_frames));
		}
		// ---- Danger Room (old 1.10): the live course count is not stored; find the split that lands on the next tag
		r.tag("[DANGERROOM: COURSES]");
		const std::size_t start = r.p;
		int live = -1;
		for (u32 n = 0; n <= old_dr_courses; ++n)
		{
			const std::size_t end = start + n * old_dr_course_bytes + (old_dr_courses - n);
			if (end + old_tag <= size && data[end] == old_tag_marker && std::memcmp(data + end + 1, "[DANGERROOM: GRADES]", 20) == 0)
			{
				live = static_cast<int>(n);
				break;
			}
		}
		if (live < 0)
		{
			fail("cannot find [DANGERROOM: GRADES] after the course records");
		}
		m.courses.resize(static_cast<std::size_t>(live));
		for (auto& c : m.courses)
		{
			r.get(c);
		}
		const auto residue = r.take(old_dr_courses - static_cast<u32>(live));
		m.course_residue.assign(residue.begin(), residue.end());
		r.tag("[DANGERROOM: GRADES]");
		for (auto& g : m.grades)
		{
			r.get(g);
		}
		r.tag("[DANGERROOM: END]");
		// ---- Review (old 1.11)
		r.get(m.review);
		// ---- map (old 1.12-1.14)
		r.tag("[MAP: ZONESTRINGS]");
		r.get(*m.zone_strings);
		r.tag("[MAP: ZONES]");
		const auto nz = r.get<u32>();
		if (nz > ceil_zone_records)
		{
			fail("zone record count " + std::to_string(nz) + " > " + std::to_string(ceil_zone_records));
		}
		for (u32 k = 0; k < nz; ++k)
		{
			OldZone z;
			z.name_handle = r.get<u32>();
			z.states.resize(old_zone_states);
			for (auto& s : z.states)
			{
				r.get(s);
			}
			r.get(z.tail);
			if (z.tail.state_count > old_zone_states || z.tail.dynamic_count > old_zone_dynamic || z.tail.smart_count > old_zone_smart)
			{
				fail("zone record counts beyond the engine tables");
			}
			m.zones.push_back(std::move(z));
		}
		r.tag("[MAP: MISSION STATE]");
		r.get(m.mission);
		// ---- profile, script variables, town centres (old 1.15-1.17)
		m.profile = read_profile(r);
		for (auto& v : m.zone_vars)
		{
			r.get(v);
		}
		for (auto& v : m.game_vars)
		{
			r.get(v);
		}
		m.town_centres = r.get<u32>();
		m.end_tag = r.tag("[SAVEGAMEEND");
		bool nonzero = false;
		for (std::size_t k = r.p; k < size; ++k)
		{
			nonzero |= data[k] != 0;
		}
		if (nonzero)
		{
			m.slack.assign(data + r.p, data + size);
		}
		check_indices(m);
		return m;
	}

	std::vector<std::uint8_t> write_slot(const OldSlot& m)
	{
		std::vector<std::uint8_t> out;
		out.reserve(old_slot_size);
		out.insert(out.end(), m.description_raw, m.description_raw + 0x80);
		put(out, old_payload);
		tag_bytes(out, m.begin_tag);
		put(out, m.items);
		put(out, static_cast<u32>(m.carried.size()));
		for (const auto& i : m.carried)
		{
			write_instance(out, i);
		}
		put(out, static_cast<u32>(m.world.size()));
		for (const auto& w : m.world)
		{
			put(out, w.slot);
			write_instance(out, w.instance);
		}
		for (const auto& h : m.heroes)
		{
			write_attributes(out, h.attributes);
			write_equipment(out, h.equipment);
			put(out, h.powers);
			put(out, h.costume);
		}
		tag_bytes(out, "[CHARACTERSUNLOCKED]");
		put(out, m.unlocked);
		put(out, m.unlocked_count);
		tag_bytes(out, "[CHARACTERSRESTORE]");
		put(out, m.restore_flag);
		for (const auto& rr : m.restore)
		{
			write_attributes(out, rr.attributes);
			write_equipment(out, rr.equipment);
		}
		check_cstr(m.game.timer_script, "timer_script");
		check_cstr(m.game.loading_name, "loading_name");
		check_cstr(m.game.recall_zone, "recall_zone");
		put(out, m.game);
		for (const auto& f : m.side_frames)
		{
			check_snapshot(f);
			put(out, f);
		}
		put(out, m.side);
		tag_bytes(out, "[DANGERROOM: COURSES]");
		for (const auto& c : m.courses)
		{
			put(out, c);
		}
		out.insert(out.end(), m.course_residue.begin(), m.course_residue.end());
		tag_bytes(out, "[DANGERROOM: GRADES]");
		put(out, m.grades);
		tag_bytes(out, "[DANGERROOM: END]");
		put(out, m.review);
		tag_bytes(out, "[MAP: ZONESTRINGS]");
		put(out, *m.zone_strings);
		tag_bytes(out, "[MAP: ZONES]");
		put(out, static_cast<u32>(m.zones.size()));
		for (const auto& z : m.zones)
		{
			put(out, z.name_handle);
			for (const auto& s : z.states)
			{
				put(out, s);
			}
			put(out, z.tail);
		}
		tag_bytes(out, "[MAP: MISSION STATE]");
		check_snapshot(m.mission);
		put(out, m.mission);
		check_cstr(m.profile.head.player_name, "player_name");
		put(out, m.profile.head);
		for (const auto& s : m.profile.high_scores)
		{
			put(out, s);
		}
		put(out, m.profile.tail);
		for (const auto& v : m.zone_vars)
		{
			check_cstr(v.name, "name");
			put(out, v);
		}
		for (const auto& v : m.game_vars)
		{
			check_cstr(v.name, "name");
			put(out, v);
		}
		put(out, m.town_centres);
		tag_bytes(out, m.end_tag);
		if (out.size() > old_slot_size)
		{
			fail("model does not fit the payload: " + std::to_string(out.size() - old_envelope) + " > " + std::to_string(old_payload));
		}
		if (!m.slack.empty())
		{
			out.insert(out.end(), m.slack.begin(), m.slack.end());
		}
		else
		{
			out.resize(old_slot_size, 0);
		}
		if (out.size() != old_slot_size)
		{
			fail("slack length");
		}
		return out;
	}

	OldProfile read_profile_file(const std::uint8_t* data, const std::size_t size, std::string& description)
	{
		if (size != old_envelope + old_profile_payload)
		{
			fail("file size " + std::to_string(size) + " is not the settings size " + std::to_string(old_envelope + old_profile_payload));
		}
		u32 plen = 0;
		std::memcpy(&plen, data + 0x80, 4);
		if (plen != old_profile_payload)
		{
			fail("payload length field " + fmt_hex(plen) + " is not " + fmt_hex(old_profile_payload));
		}
		description = cstr(reinterpret_cast<const char*>(data), 0x80);
		Reader r{data, size, old_envelope};
		return read_profile(r);
	}

	std::vector<std::uint8_t> write_profile_file(const std::string& description, const OldProfile& p)
	{
		std::vector<std::uint8_t> out(0x80, 0);
		set_cstr(reinterpret_cast<char*>(out.data()), 0x80, description);
		put(out, static_cast<u32>(old_profile_payload));
		check_cstr(p.head.player_name, "player_name");
		put(out, p.head);
		for (const auto& s : p.high_scores)
		{
			put(out, s);
		}
		put(out, p.tail);
		if (out.size() != old_envelope + old_profile_payload)
		{
			fail("profile size");
		}
		return out;
	}
}
