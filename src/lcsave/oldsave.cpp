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

		OldInstance read_instance(Reader& r, const NativeLayout& native)
		{
			static const char* const names[3] = {"prefixes", "own", "suffixes"};
			OldInstance inst;
			if (native.definition_index_bytes == 2) inst.head.definition = r.get<u16>();
			else inst.head.definition = r.get<u8>();
			r.get(inst.head.name_a);
			r.get(inst.head.name_b);
			r.get(inst.head.special);
			r.get(inst.head.location);
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

		void write_instance(std::vector<std::uint8_t>& out, const OldInstance& inst, const NativeLayout& native)
		{
			if (native.definition_index_bytes == 2) put(out, inst.head.definition);
			else
			{
				if (inst.head.definition > 0xff) fail("item definition index " + std::to_string(inst.head.definition) + " does not fit the native layout " + native.id + " (byte)");
				put(out, static_cast<u8>(inst.head.definition));
			}
			put(out, inst.head.name_a);
			put(out, inst.head.name_b);
			put(out, inst.head.special);
			put(out, inst.head.location);
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

		OldEquipment read_equipment(Reader& r, const NativeLayout& native)
		{
			const auto n = r.get<u8>();
			if (n > ceil_equipped)
			{
				fail("equipped count " + std::to_string(n) + " > " + std::to_string(ceil_equipped));
			}
			OldEquipment eq;
			for (u32 k = 0; k < n; ++k)
			{
				eq.equipped.push_back(read_instance(r, native));
			}
			r.get(eq.tail);
			return eq;
		}

		void write_equipment(std::vector<std::uint8_t>& out, const OldEquipment& eq, const NativeLayout& native)
		{
			if (eq.equipped.size() > 255)
			{
				fail("ubyte format requires 0 <= number <= 255");
			}
			put(out, static_cast<u8>(eq.equipped.size()));
			for (const auto& i : eq.equipped)
			{
				write_instance(out, i, native);
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

		// the mission snapshot of the layout (old 1.9; W2 section 3.4)
		void read_snapshot(Reader& r, OldSnapshot& s, const NativeLayout& native)
		{
			s = OldSnapshot{};
			r.get(s.zone);
			r.get(s.seat_pose);
			r.get(s.pad_a0);
			r.get(s.seat_name);
			r.get(s.pad_124);
			if (r.p + native.snapshot_objectives > r.size) fail("snapshot needs " + std::to_string(native.snapshot_objectives) + " objective bytes at " + fmt_hex(r.p));
			std::memcpy(s.objectives, r.b + r.p, native.snapshot_objectives);
			r.p += native.snapshot_objectives;
			if (native.snapshot_objectives == old_snapshot_objectives)
			{
				r.get(s.objective_301);
				r.get(s.pad_252);
			}
			else r.get(s.pad_525);
			r.get(s.anchor_pos);
			r.get(s.anchor_orient);
		}

		void write_snapshot(std::vector<std::uint8_t>& out, const OldSnapshot& s, const NativeLayout& native)
		{
			const std::size_t start = out.size();
			put(out, s.zone);
			put(out, s.seat_pose);
			put(out, s.pad_a0);
			put(out, s.seat_name);
			put(out, s.pad_124);
			for (u32 k = native.snapshot_objectives; k < max_snapshot_objectives; ++k)
			{
				if (s.objectives[k]) fail("objective index " + std::to_string(k) + " does not fit the native layout " + native.id + " (" + std::to_string(native.snapshot_objectives) + " objectives)");
			}
			out.insert(out.end(), s.objectives, s.objectives + native.snapshot_objectives);
			if (native.snapshot_objectives == old_snapshot_objectives)
			{
				put(out, s.objective_301);
				put(out, s.pad_252);
			}
			else put(out, s.pad_525);
			put(out, s.anchor_pos);
			put(out, s.anchor_orient);
			if (out.size() - start != native.snapshot_size) fail("snapshot size");
		}

		// one entity state record of the layout (old 1.13; W2 section 3.5)
		void read_state(Reader& r, OldStateRecord& s, const NativeLayout& native)
		{
			r.get(s.def_name);
			r.get(s.inst_name);
			r.get(s.payload);
			r.get(s.flags_a);
			r.get(s.flags_b);
			if (native.float_positions)
			{
				r.get(s.world_slot);
				r.get(s.position);
			}
			else
			{
				i16 p[3];
				r.get(p);
				for (int k = 0; k < 3; ++k) s.position[k] = static_cast<f32>(p[k]);
				s.world_slot = world_slot_none;
			}
		}

		void write_state(std::vector<std::uint8_t>& out, const OldStateRecord& s, const NativeLayout& native)
		{
			put(out, s.def_name);
			put(out, s.inst_name);
			put(out, s.payload);
			put(out, s.flags_a);
			put(out, s.flags_b);
			if (native.float_positions)
			{
				put(out, s.world_slot);
				put(out, s.position);
			}
			else
			{
				if (s.world_slot != world_slot_none) fail(std::string("state record world-item slot field does not exist in the native layout ") + native.id);
				for (int k = 0; k < 3; ++k)
				{
					const f32 x = s.position[k];
					if (!(x >= -32768.0f && x <= 32767.0f) || x != static_cast<f32>(static_cast<i16>(x)))
					{
						fail(std::string("state position does not fit the native layout ") + native.id + " (i16)");
					}
					put(out, static_cast<i16>(x));
				}
			}
		}

		void read_zone_strings(Reader& r, OldZoneStrings& t, const NativeLayout& native)
		{
			t = OldZoneStrings(native.zone_string_slots, native.zone_arena);
			if (r.p + native.zone_strings_size > r.size) fail("zone string table needs " + std::to_string(native.zone_strings_size) + " bytes at " + fmt_hex(r.p));
			r.get(t.generation);
			std::memcpy(t.offsets.data(), r.b + r.p, 4u * native.zone_string_slots);
			r.p += 4u * native.zone_string_slots;
			r.get(t.used);
			std::memcpy(t.arena.data(), r.b + r.p, native.zone_arena);
			r.p += native.zone_arena;
		}

		void write_zone_strings(std::vector<std::uint8_t>& out, const OldZoneStrings& t, const NativeLayout& native)
		{
			if (t.offsets.size() != native.zone_string_slots || t.arena.size() != native.zone_arena)
			{
				fail(std::string("zone string table does not match the native layout ") + native.id);
			}
			put(out, t.generation);
			const auto* o = reinterpret_cast<const std::uint8_t*>(t.offsets.data());
			out.insert(out.end(), o, o + 4u * native.zone_string_slots);
			put(out, t.used);
			out.insert(out.end(), t.arena.begin(), t.arena.end());
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
		const u32 arena = static_cast<u32>(table.arena.size());
		if (slot >= table.offsets.size())
		{
			fail("zone string handle " + fmt_hex(handle) + ": slot beyond the table");
		}
		const u32 off = table.offsets[slot];
		if (off == 0 || off >= arena)
		{
			fail("zone string handle " + fmt_hex(handle) + ": slot " + std::to_string(slot) + " is free or its offset " + fmt_hex(off) +
				" is outside the arena");
		}
		const void* end = std::memchr(table.arena.data() + off, 0, arena - off);
		if (!end)
		{
			fail("zone string at " + fmt_hex(off) + " is not terminated");
		}
		return std::string(reinterpret_cast<const char*>(table.arena.data() + off), static_cast<const u8*>(end) - (table.arena.data() + off));
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

		// one zone record of the layout `native` (oldsave._read_zone_record): bounds checked against the descriptor
		// before anything is kept
		OldZone read_zone_record(Reader& r, const NativeLayout& native)
		{
			if (r.p + native.record_size > r.size)
			{
				fail("zone record needs " + std::to_string(native.record_size) + " bytes at " + fmt_hex(r.p) + ", payload ends at " + fmt_hex(r.size));
			}
			const std::size_t start = r.p;
			OldZone z(native);
			z.name_handle = r.get<u32>();
			for (auto& s : z.states) read_state(r, s, native);
			auto& t = z.tail;
			r.get(t.pad_4804);
			r.get(t.state_count);
			for (auto& v : t.ordinal_map) r.get(v);
			r.get(t.pad_4a0c);
			for (auto& v : t.dynamic) r.get(v);
			r.get(t.pad_4c68);
			r.get(t.dynamic_count);
			for (auto& v : t.smart) r.get(v);
			r.get(t.pad_5270);
			r.get(t.smart_count);
			r.get(t.fog);
			r.get(t.conversation);
			r.get(t.time_stamp);
			r.get(t.flag);
			r.get(t.pad_55dd);
			if (r.p - start != native.record_size) fail("zone record size");
			if (t.state_count > native.states || t.dynamic_count > native.dynamic || t.smart_count > native.smart)
			{
				fail("zone record counts beyond the engine tables");
			}
			return z;
		}

		void write_zone_record(std::vector<std::uint8_t>& out, const OldZone& z, const NativeLayout& native)
		{
			if (!z.matches(native)) fail(std::string("zone record does not match the native layout ") + native.id);
			const std::size_t start = out.size();
			put(out, z.name_handle);
			for (const auto& s : z.states) write_state(out, s, native);
			const auto& t = z.tail;
			put(out, t.pad_4804);
			put(out, t.state_count);
			for (const auto v : t.ordinal_map) put(out, v);
			put(out, t.pad_4a0c);
			for (const auto v : t.dynamic) put(out, v);
			put(out, t.pad_4c68);
			put(out, t.dynamic_count);
			for (const auto v : t.smart) put(out, v);
			put(out, t.pad_5270);
			put(out, t.smart_count);
			put(out, t.fog);
			put(out, t.conversation);
			put(out, t.time_stamp);
			put(out, t.flag);
			put(out, t.pad_55dd);
			if (out.size() - start != native.record_size) fail("zone record size");
		}

		void check_indices(const OldSlot& m)
		{
			for (const auto& z : m.zones)
			{
				const u32 n = z.tail.state_count;
				for (u32 k = 0; k < z.tail.ordinal_map.size(); ++k)
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
				check_list(z.tail.dynamic.data(), z.tail.dynamic_count, "dynamic");
				check_list(z.tail.smart.data(), z.tail.smart_count, "smart");
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

	OldSlot read_slot(const std::uint8_t* data, std::size_t size, const NativeLayout& native)
	{
		if (size != native.slot_size)
		{
			fail("file size " + std::to_string(size) + " is not the slot size " + std::to_string(native.slot_size) + " (0x84 + " + fmt_hex(native.payload) + ")");
		}
		u32 plen = 0;
		std::memcpy(&plen, data + 0x80, 4);
		if (plen != native.payload)
		{
			fail("payload length field " + fmt_hex(plen) + " is not " + fmt_hex(native.payload));
		}
		OldSlot m;
		std::memcpy(m.description_raw, data, 0x80);
		Reader r{data, size, old_envelope};
		m.begin_tag = r.tag("[SAVEGAMEBEGIN");
		// ---- items (old 1.4; W2 section 3.2)
		if (native.def_count_bytes == 2) m.items.def_count = r.get<u16>();
		else m.items.def_count = r.get<u8>();
		if (r.p + 2 * native.definitions > size) fail("item head needs " + std::to_string(2 * native.definitions) + " bytes at " + fmt_hex(r.p));
		std::memcpy(m.items.def_state, data + r.p, 2 * native.definitions);
		r.p += 2 * native.definitions;
		r.get(m.items.tech_bits);
		r.get(m.items.health_items);
		r.get(m.items.energy_items);
		r.get(m.items.key_counts);
		const auto n1 = r.get<u32>();
		if (n1 > ceil_instances)
		{
			fail("carried item count " + std::to_string(n1) + " > " + std::to_string(ceil_instances));
		}
		for (u32 k = 0; k < n1; ++k)
		{
			m.carried.push_back(read_instance(r, native));
		}
		const auto n2 = r.get<u32>();
		if (n2 > native.world_slots)
		{
			fail("world item count " + std::to_string(n2) + " > " + std::to_string(native.world_slots));
		}
		for (u32 k = 0; k < n2; ++k)
		{
			OldWorld w;
			w.slot = r.get<u32>();
			if (w.slot >= native.world_slots)
			{
				fail("world item slot " + std::to_string(w.slot) + " >= " + std::to_string(native.world_slots));
			}
			w.instance = read_instance(r, native);
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
			h.equipment = read_equipment(r, native);
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
			rr.equipment = read_equipment(r, native);
			m.restore.push_back(std::move(rr));
		}
		// ---- game block (old 1.8)
		r.get(m.game);
		// ---- side-mission stack (old 1.9)
		for (auto& f : m.side_frames)
		{
			read_snapshot(r, f, native);
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
		read_zone_strings(r, *m.zone_strings, native);
		r.tag("[MAP: ZONES]");
		const auto nz = r.get<u32>();
		if (nz > native.record_count)
		{
			fail("zone record count " + std::to_string(nz) + " > " + std::to_string(native.record_count));
		}
		for (u32 k = 0; k < nz; ++k)
		{
			m.zones.push_back(read_zone_record(r, native));
		}
		r.tag("[MAP: MISSION STATE]");
		read_snapshot(r, m.mission, native);
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

	std::vector<std::uint8_t> write_slot(const OldSlot& m, const NativeLayout& native)
	{
		for (const auto& z : m.zones)
		{
			if (!z.matches(native)) fail(std::string("zone record does not match the native layout ") + native.id);
		}
		if (m.zones.size() > native.record_count)
		{
			fail("zone record count " + std::to_string(m.zones.size()) + " > " + std::to_string(native.record_count));
		}
		if (m.world.size() > native.world_slots)
		{
			fail("world item count " + std::to_string(m.world.size()) + " > " + std::to_string(native.world_slots));
		}
		std::vector<std::uint8_t> out;
		out.reserve(native.slot_size);
		out.insert(out.end(), m.description_raw, m.description_raw + 0x80);
		put(out, native.payload);
		tag_bytes(out, m.begin_tag);
		// ---- the item head of the layout (W2 section 3.2): the count, the layout's pairs (a pair beyond them refuses)
		if (native.def_count_bytes == 2) put(out, m.items.def_count);
		else
		{
			if (m.items.def_count > 0xff) fail("definition count " + std::to_string(m.items.def_count) + " does not fit the native layout " + native.id + " (byte)");
			put(out, static_cast<u8>(m.items.def_count));
		}
		for (u32 k = 2 * native.definitions; k < 2 * max_definitions; ++k)
		{
			if (m.items.def_state[k]) fail("definition state pair " + std::to_string(k / 2) + " does not fit the native layout " + native.id + " (" + std::to_string(native.definitions) + " definitions)");
		}
		out.insert(out.end(), m.items.def_state, m.items.def_state + 2 * native.definitions);
		put(out, m.items.tech_bits);
		put(out, m.items.health_items);
		put(out, m.items.energy_items);
		put(out, m.items.key_counts);
		put(out, static_cast<u32>(m.carried.size()));
		for (const auto& i : m.carried)
		{
			write_instance(out, i, native);
		}
		put(out, static_cast<u32>(m.world.size()));
		for (const auto& w : m.world)
		{
			put(out, w.slot);
			write_instance(out, w.instance, native);
		}
		for (const auto& h : m.heroes)
		{
			write_attributes(out, h.attributes);
			write_equipment(out, h.equipment, native);
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
			write_equipment(out, rr.equipment, native);
		}
		check_cstr(m.game.timer_script, "timer_script");
		check_cstr(m.game.loading_name, "loading_name");
		check_cstr(m.game.recall_zone, "recall_zone");
		put(out, m.game);
		for (const auto& f : m.side_frames)
		{
			check_snapshot(f);
			write_snapshot(out, f, native);
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
		write_zone_strings(out, *m.zone_strings, native);
		tag_bytes(out, "[MAP: ZONES]");
		put(out, static_cast<u32>(m.zones.size()));
		for (const auto& z : m.zones)
		{
			write_zone_record(out, z, native);
		}
		tag_bytes(out, "[MAP: MISSION STATE]");
		check_snapshot(m.mission);
		write_snapshot(out, m.mission, native);
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
		if (out.size() > native.slot_size)
		{
			fail("model does not fit the payload: " + std::to_string(out.size() - old_envelope) + " > " + std::to_string(native.payload));
		}
		if (!m.slack.empty())
		{
			out.insert(out.end(), m.slack.begin(), m.slack.end());
		}
		else
		{
			out.resize(native.slot_size, 0);
		}
		if (out.size() != native.slot_size)
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

	u32 talent_engine_id(u32 code, const NativeLayout& native)
	{
		const u32 owner = talent_code_owner(code), position = talent_code_position(code);
		const u32 m = native.talent_multiplier;
		if (owner == 0 ? position >= m - 1 : position >= m)
		{
			fail(std::string(owner == 0 ? "shared talent position " : "talent position ") + std::to_string(position) + " does not fit the native layout " +
				native.id + " (talent multiplier " + std::to_string(m) + ")");
		}
		const u32 id = native.talent_id(owner, position);
		if (id > 0xffff) fail("talent id " + std::to_string(id) + " does not fit the saved u16");
		return id;
	}

	NativeLoadDemand model_demand(const OldSlot& m)
	{
		NativeLoadDemand out;
		auto count = [&](const OldInstance& instance)
		{
			++out.instances;
			for (const auto& list : instance.lists) out.attached_enhancements += static_cast<std::uint32_t>(list.size());
		};
		for (const auto& instance : m.carried) count(instance);
		for (const auto& world : m.world) count(world.instance);
		for (const auto& hero : m.heroes) for (const auto& instance : hero.equipment.equipped) count(instance);
		// Every restore block is serialized and read even when its valid flag is clear. Include it conservatively.
		for (const auto& restore : m.restore) for (const auto& instance : restore.equipment.equipped) count(instance);
		out.world_items = static_cast<std::uint32_t>(m.world.size());
		return out;
	}

	std::string demand_refusal(const NativeLoadDemand& d, const NativeLayout& native)
	{
		if (d.instances <= native.instances && d.attached_enhancements <= native.attached_enhancements && d.world_items <= native.world_slots) return {};
		std::string text = "native instance/enhancement/world pool exhausted";
		if (native.w2())
		{
			text += " (" + std::to_string(d.instances) + "/" + std::to_string(native.instances) + " instances, " + std::to_string(d.attached_enhancements) +
				"/" + std::to_string(native.attached_enhancements) + " attached, " + std::to_string(d.world_items) + "/" + std::to_string(native.world_slots) + " world items)";
		}
		return text;
	}
}
