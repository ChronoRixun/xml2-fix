// The LC save container (SPEC-new-save sections 1-2, revision 2.3), as container.py: header, section table,
// sections made of schema-described records. The schema tables below ARE the format definition and match
// container.py entry for entry; encode_value / decode_value are the only code that interprets them.
//
// Schema grammar - a record schema is a list of (field name, type) where type is one of
//   u8 u16 u32 i32 u64 f32          scalars (little-endian)
//   id                              u32 identity (keys.hpp grammar)
//   str                             u16 byte count + bytes (cp1252)
//   f32x3 / u16x16                  three f32 / sixteen u16
//   bytes:N                         N raw bytes
//   tail                            every remaining byte of the record (only as the last field)
//   list:<T>                        u32 count + count x T (T: a scalar type, id, str or a record name)
//   rec:<Name>                      one embedded record (u32 rsize + fields)
//   packed:<Name>                   one embedded fixed element without rsize (the frozen element types)

#include "internal.hpp"

#include <cstdio>
#include <cstring>

namespace lcsave::detail
{
	namespace
	{
		// ---- frozen element types (SPEC 1.4: never change; 'packed', no rsize)
		const Field f_enh[] = {{"enhancement", "id"}, {"value", "u32"}};
		const Field f_tal[] = {{"talent", "id"}, {"rank", "u8"}, {"pending", "u8"}, {"cap", "u8"}, {"base", "u8"}, {"byte5", "u8"}};
		const Field f_obj[] = {{"objective", "id"}, {"counter", "u8"}, {"flags", "u8"}};
		const Field f_grd[] = {{"grade", "id"}, {"value", "u8"}};
		const Field f_defstate[] = {{"item", "id"}, {"quantity", "u32"}, {"flag", "u8"}};
		// ---- records (framed by rsize)
		const Field r_key[] = {{"id", "u32"}, {"domain", "u8"}, {"key", "str"}};
		const Field r_inst[] = {{"definition", "id"}, {"name_a", "id"}, {"name_b", "id"}, {"special", "id"}, {"location", "u8"},
			{"prefixes", "list:packed:ENH"}, {"own", "list:packed:ENH"}, {"suffixes", "list:packed:ENH"}};
		const Field r_witem[] = {{"handle", "u32"}, {"instance", "rec:INST"}};
		const Field r_pwr[] = {{"button", "u8"}, {"name", "str"}, {"flags", "u8"}};       // flags: HERO minor 1 (SPEC 2.6)
		const Field r_hrec[] = {{"hero", "id"}, {"traits", "u16x16"}, {"skill_points", "u32"}, {"trait_points", "u32"}, {"level", "u16"},
			{"ai_level", "u8"}, {"ai_attack", "u8"}, {"talents", "list:packed:TAL"}, {"equipped", "list:rec:INST"},
			{"vitals_valid", "u8"}, {"health", "f32"}, {"energy", "f32"}, {"player_seat", "i32"}, {"experience", "u32"},
			{"ack_level", "u16"}, {"health_boosts", "u16"}, {"energy_boosts", "u16"}, {"levels_purchased", "u8"},
			{"points_bought", "u8"}, {"respec_count", "u8"}, {"skirmish_boost", "u8"}, {"flags", "u32"},
			{"powers", "list:rec:PWR"}, {"costume", "id"}, {"costumes_unlocked", "list:id"}};
		const Field r_rrec[] = {{"owner", "id"}, {"traits", "u16x16"}, {"skill_points", "u32"}, {"trait_points", "u32"}, {"level", "u16"},
			{"ai_level", "u8"}, {"ai_attack", "u8"}, {"talents", "list:packed:TAL"}, {"equipped", "list:rec:INST"},
			{"vitals_valid", "u8"}, {"health", "f32"}, {"energy", "f32"}, {"player_seat", "i32"}, {"experience", "u32"},
			{"ack_level", "u16"}, {"health_boosts", "u16"}, {"energy_boosts", "u16"}, {"levels_purchased", "u8"},
			{"points_bought", "u8"}, {"respec_count", "u8"}, {"skirmish_boost", "u8"}, {"flags", "u32"}};
		const Field r_seat[] = {{"seat", "u8"}, {"hero", "id"}, {"position", "f32x3"}, {"orientation", "f32x3"}};
		const Field r_snap[] = {{"zone", "id"}, {"flags", "u32"}, {"seats", "list:rec:SEAT"}, {"objectives", "list:packed:OBJ"},
			{"anchor_pos", "f32x3"}, {"anchor_orient", "f32x3"}};
		const Field r_crs[] = {{"course", "id"}, {"flags", "u32"}, {"tiers", "bytes:3"}, {"largest_hit", "u32"}, {"largest_hit_hero", "id"},
			{"least_damage", "u16"}, {"least_damage_hero", "id"}, {"best_time", "u16"}, {"best_combo", "u8"},
			{"most_health_packs", "u8"}, {"most_health_packs_hero", "id"}, {"most_energy_packs", "u8"},
			{"most_energy_packs_hero", "id"}, {"most_defeats", "u8"}, {"most_defeats_hero", "id"}};
		const Field r_state[] = {{"list", "u8"}, {"entity_tag", "u32"}, {"ordinal", "u32"}, {"placement_index", "u32"}, {"def_name", "u32"},
			{"inst_name", "u32"}, {"flags_a", "u32"}, {"flags_b", "u32"}, {"position", "f32x3"}, {"payload_kind", "u8"},
			{"payload", "tail"}};
		const Field r_fog[] = {{"encoding", "u16"}, {"width", "u16"}, {"height", "u16"}, {"data", "tail"}};
		const Field r_zrec[] = {{"zone", "id"}, {"identity_mode", "u8"}, {"content_hash", "bytes:20"}, {"time_stamp", "f32"}, {"flag", "u8"},
			{"capture_flags", "u32"}, {"conversation", "list:u32"}, {"conversation_digest", "bytes:32"},
			{"names", "list:str"}, {"states", "list:rec:STATE"}, {"fog", "rec:FOG"}};
		const Field r_var[] = {{"name", "str"}, {"value", "u32"}};
		const Field r_score[] = {{"course", "id"}, {"party_a", "list:id"}, {"party_b", "list:id"}, {"score", "u32"}};

		template <std::size_t N> constexpr Schema S(const Field (&f)[N]) { return Schema{f, N}; }
		constexpr Schema empty{nullptr, 0};

		struct Named
		{
			const char* name;
			Schema schema;
		};
		const Named packed[] = {{"ENH", S(f_enh)}, {"TAL", S(f_tal)}, {"OBJ", S(f_obj)}, {"GRD", S(f_grd)}, {"DEFSTATE", S(f_defstate)}};
		const Named records[] = {{"KEY", S(r_key)}, {"INST", S(r_inst)}, {"WITEM", S(r_witem)}, {"PWR", S(r_pwr)}, {"HREC", S(r_hrec)},
			{"RREC", S(r_rrec)}, {"SEAT", S(r_seat)}, {"SNAP", S(r_snap)}, {"CRS", S(r_crs)}, {"STATE", S(r_state)}, {"FOG", S(r_fog)},
			{"ZREC", S(r_zrec)}, {"VAR", S(r_var)}, {"SCORE", S(r_score)}};

		// ---- sections: head schema and (list name, element type) in order
		const Field h_meta[] = {{"saved_at", "u64"}, {"description", "str"}, {"origin", "u8"}, {"zone", "id"}, {"act", "u16"},
			{"difficulty", "u32"}, {"play_time", "f32"}, {"writer", "str"}, {"slot", "u8"}};
		const Field l_meta[] = {{"mods", "str"}};
		const Field l_keys[] = {{"keys", "rec:KEY"}};
		const Field h_game[] = {{"timer_threshold", "f32"}, {"timer_remaining", "f32"}, {"timer_after_add", "f32"},
			{"timer_last_add", "f32"}, {"timer_script", "str"}, {"recall_cooldown", "f32"}, {"xtreme_charges", "i32"},
			{"xtreme_pips", "i32"}, {"loading_name", "str"}, {"recall_zone", "id"}, {"recall_pos", "f32x3"},
			{"act", "u16"}, {"value_608", "u32"}, {"difficulty", "u32"}, {"play_time", "f32"}, {"team_potion", "u8"},
			{"counter_a", "u8"}, {"counter_b", "u8"}, {"flags", "u32"}};
		const Field l_game[] = {{"trivia", "id"}};
		const Field h_item[] = {{"tech_bits", "u32"}, {"health_items", "u32"}, {"energy_items", "u32"}};
		const Field l_item[] = {{"def_state", "packed:DEFSTATE"}, {"key_counts", "packed:DEFSTATE"}, {"instances", "rec:INST"},
			{"world", "rec:WITEM"}};
		const Field l_hero[] = {{"heroes", "rec:HREC"}};
		const Field l_ulck[] = {{"unlocked", "id"}};
		const Field h_rest[] = {{"bank_valid", "u8"}};
		const Field l_rest[] = {{"records", "rec:RREC"}};
		const Field h_miss[] = {{"stack_depth", "u32"}};
		const Field l_miss[] = {{"snapshots", "rec:SNAP"}};
		const Field l_dngr[] = {{"courses", "rec:CRS"}, {"grades", "packed:GRD"}};
		const Field l_revw[] = {{"unlocked", "id"}, {"seen", "id"}};
		const Field h_zone[] = {{"newest_time", "f32"}, {"container_flag", "u8"}};
		const Field l_zone[] = {{"records", "rec:ZREC"}};
		const Field l_svar[] = {{"zone_vars", "rec:VAR"}, {"game_vars", "rec:VAR"}};
		const Field l_xtrc[] = {{"unlocked", "id"}};
		const Field h_prfo[] = {{"options", "u32"}, {"player_name", "str"}};
		const Field l_prfs[] = {{"scores", "rec:SCORE"}};
		const Field h_prfp[] = {{"highest_level", "u16"}, {"flags", "u32"}};
		const Field l_prfp[] = {{"characters", "id"}, {"review", "id"}};
		const Field h_lgcy[] = {{"original_name", "str"}, {"original_size", "u32"}, {"sha1", "bytes:20"}, {"start_era", "u32"},
			{"writer_era", "u32"}, {"def_count_byte", "u32"}, {"zone_records_dropped", "u32"},
			{"world_items_dropped", "u32"}, {"zone_vars_dropped", "u32"}, {"other_dropped", "u32"}};
		const Field l_lgcy[] = {{"notes", "str"}};

		const SectionSchema sections[] = {
			{"META", S(h_meta), S(l_meta)}, {"KEYS", empty, S(l_keys)}, {"GAME", S(h_game), S(l_game)}, {"ITEM", S(h_item), S(l_item)},
			{"HERO", empty, S(l_hero)}, {"ULCK", empty, S(l_ulck)}, {"REST", S(h_rest), S(l_rest)}, {"MISS", S(h_miss), S(l_miss)},
			{"DNGR", empty, S(l_dngr)}, {"REVW", empty, S(l_revw)}, {"ZONE", S(h_zone), S(l_zone)}, {"SVAR", empty, S(l_svar)},
			{"XTRC", empty, S(l_xtrc)}, {"PRFO", S(h_prfo), empty}, {"PRFS", empty, S(l_prfs)}, {"PRFP", S(h_prfp), S(l_prfp)},
			{"LGCY", S(h_lgcy), S(l_lgcy)}};

		const char* const campaign_sections[] = {"META", "KEYS", "GAME", "ITEM", "HERO", "ULCK", "REST", "MISS", "DNGR", "REVW",
			"ZONE", "SVAR", "XTRC", "PRFO", "PRFP", "PRFS", "LGCY"};

		// the list ceilings of SPEC 2.16 (reader ceilings, not format limits); default 65536
		u32 ceiling(std::string_view et)
		{
			struct C { const char* t; u32 n; };
			static const C table[] = {{"u32", 1u << 20}, {"id", 65536}, {"str", 16384}, {"rec:KEY", 65536}, {"rec:INST", 4096},
				{"rec:WITEM", 16384}, {"rec:PWR", 4}, {"rec:HREC", 1024}, {"rec:RREC", 1024}, {"rec:SEAT", 4}, {"rec:SNAP", 17},
				{"rec:CRS", 65536}, {"rec:STATE", 65536}, {"rec:ZREC", 256}, {"rec:VAR", 4096}, {"rec:SCORE", 1024},
				{"packed:ENH", 64}, {"packed:TAL", 256}, {"packed:OBJ", 65536}, {"packed:GRD", 256}, {"packed:DEFSTATE", 65536}};
			for (const auto& c : table)
			{
				if (et == c.t) return c.n;
			}
			return 65536;
		}

		constexpr u32 max_file = 64u << 20;
		constexpr u32 max_sections = 4096;

		std::string fmt_hex(std::uint64_t v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "0x%llx", static_cast<unsigned long long>(v));
			return b;
		}

		void put_bytes(std::string& out, const void* p, std::size_t n)
		{
			out.append(static_cast<const char*>(p), n);
		}

		template <typename T> void put_le(std::string& out, T v)
		{
			put_bytes(out, &v, sizeof v);
		}

		std::int64_t int_of(const Value& v)
		{
			if (v.type == Value::Type::integer) return v.i;
			fail("required argument is not an integer");
		}

		template <typename T> T ranged(const Value& v, std::int64_t lo, std::int64_t hi, const char* fmt)
		{
			const auto x = int_of(v);
			if (x < lo || x > hi)
			{
				fail(std::string("'") + fmt + "' format requires " + std::to_string(lo) + " <= number <= " + std::to_string(hi));
			}
			return static_cast<T>(x);
		}

		void put_f32(std::string& out, const Value& v)
		{
			const float f = static_cast<float>(v.as_real());
			put_le(out, f);
		}

		bool scalar(std::string_view t)
		{
			return t == "u8" || t == "u16" || t == "u32" || t == "i32" || t == "u64" || t == "f32" || t == "id";
		}
	}

	const Schema* record_schema(std::string_view name)
	{
		for (const auto& r : records)
		{
			if (name == r.name) return &r.schema;
		}
		return nullptr;
	}

	const Schema* packed_schema(std::string_view name)
	{
		for (const auto& r : packed)
		{
			if (name == r.name) return &r.schema;
		}
		return nullptr;
	}

	const SectionSchema* section_schema(std::string_view tag)
	{
		for (const auto& s : sections)
		{
			if (tag == s.tag) return &s;
		}
		return nullptr;
	}

	// ------------------------------------------------------------------------------------------------ encode
	std::string encode_value(std::string_view t, const Value& v)
	{
		std::string out;
		if (t == "u8") { put_le(out, ranged<u8>(v, 0, 255, "B")); return out; }
		if (t == "u16") { put_le(out, ranged<u16>(v, 0, 65535, "H")); return out; }
		if (t == "u32" || t == "id") { put_le(out, ranged<u32>(v, 0, 0xffffffffll, "I")); return out; }
		if (t == "i32") { put_le(out, ranged<i32>(v, -2147483648ll, 2147483647ll, "i")); return out; }
		if (t == "u64") { put_le(out, static_cast<u64>(int_of(v))); return out; }
		if (t == "f32") { put_f32(out, v); return out; }
		if (t == "str")
		{
			const auto& raw = v.as_text();
			if (!encodable_cp1252(raw, v.enc))
			{
				fail("'charmap' codec can't encode character in " + py_repr(raw, v.enc));
			}
			if (raw.size() > 65535)
			{
				fail("str too long");
			}
			put_le(out, static_cast<u16>(raw.size()));
			out += raw;
			return out;
		}
		if (t == "f32x3")
		{
			const auto& l = v.as_list();
			if (l.size() != 3) fail("pack expected 3 items for packing");
			for (const auto& x : l) put_f32(out, x);
			return out;
		}
		if (t == "u16x16")
		{
			const auto& l = v.as_list();
			if (l.size() != 16) fail("pack expected 16 items for packing");
			for (const auto& x : l) put_le(out, ranged<u16>(x, 0, 65535, "H"));
			return out;
		}
		if (t.substr(0, 6) == "bytes:")
		{
			const auto n = static_cast<std::size_t>(std::stoul(std::string(t.substr(6))));
			const auto& b = v.as_bytes();
			if (b.size() != n)
			{
				fail("bytes:" + std::to_string(n) + " given " + std::to_string(b.size()));
			}
			return b;
		}
		if (t == "tail")
		{
			return v.as_bytes();
		}
		if (t.substr(0, 5) == "list:")
		{
			const auto et = t.substr(5);
			const auto& l = v.as_list();
			put_le(out, static_cast<u32>(l.size()));
			for (const auto& x : l) out += encode_value(et, x);
			return out;
		}
		if (t.substr(0, 4) == "rec:")
		{
			return encode_record(t.substr(4), v);
		}
		if (t.substr(0, 7) == "packed:")
		{
			const auto* s = packed_schema(t.substr(7));
			if (!s) fail("unknown type " + std::string(t));
			for (std::size_t k = 0; k < s->count; ++k)
			{
				out += encode_value(s->fields[k].type, v[s->fields[k].name]);
			}
			return out;
		}
		fail("unknown type " + std::string(t));
	}

	std::string encode_record(std::string_view name, const Value& v)
	{
		const auto* s = record_schema(name);
		if (!s) fail("unknown record " + std::string(name));
		std::string body;
		for (std::size_t k = 0; k < s->count; ++k)
		{
			body += encode_value(s->fields[k].type, v[s->fields[k].name]);
		}
		std::string out;
		put_le(out, static_cast<u32>(body.size()));
		return out + body;
	}

	std::string encode_section(const std::string& tag, const Value& data)
	{
		const auto* sc = section_schema(tag);
		if (!sc) fail("KeyError: " + tag);
		std::string body;
		for (std::size_t k = 0; k < sc->head.count; ++k)
		{
			body += encode_value(sc->head.fields[k].type, data[sc->head.fields[k].name]);
		}
		std::string out;
		put_le(out, static_cast<u32>(body.size()));
		out += body;
		for (std::size_t k = 0; k < sc->lists.count; ++k)
		{
			out += encode_value(std::string("list:") + sc->lists.fields[k].type, data[sc->lists.fields[k].name]);
		}
		return out;
	}

	// ------------------------------------------------------------------------------------------------ decode
	namespace
	{
		struct Dec
		{
			const std::string& b;
			std::size_t p;
			std::size_t end;
			void need(std::size_t n) const
			{
				if (p + n > end)
				{
					fail("record truncated at " + fmt_hex(p) + " (needs " + std::to_string(n) + ", ends " + fmt_hex(end) + ")");
				}
			}
			template <typename T> T get()
			{
				need(sizeof(T));
				T v{};
				std::memcpy(&v, b.data() + p, sizeof v);
				p += sizeof v;
				return v;
			}
		};

		Value decode_value(Dec& d, std::string_view t);

		Value decode_record(Dec& d, std::string_view name)
		{
			const auto* s = record_schema(name);
			if (!s) fail("unknown record " + std::string(name));
			const auto rsize = d.get<u32>();
			if (d.p + rsize > d.end)
			{
				fail("record " + std::string(name) + ": rsize " + std::to_string(rsize) + " runs past its parent");
			}
			Dec sub{d.b, d.p, d.p + rsize};
			Value out = Value::dict();
			for (std::size_t k = 0; k < s->count; ++k)
			{
				const std::string_view t = s->fields[k].type;
				if (sub.p >= sub.end && t != "tail" && t.substr(0, 4) != "list")
				{
					break;                  // a shorter (older) record: later fields absent (section minor rules)
				}
				out.set(s->fields[k].name, decode_value(sub, t));
			}
			out.set("_extra", Value::bytes(sub.p < sub.end ? d.b.substr(sub.p, sub.end - sub.p) : std::string()));
			d.p = sub.end;
			return out;
		}

		Value decode_value(Dec& d, std::string_view t)
		{
			if (t == "u8") return Value::integer(d.get<u8>());
			if (t == "u16") return Value::integer(d.get<u16>());
			if (t == "u32" || t == "id") return Value::integer(d.get<u32>());
			if (t == "i32") return Value::integer(d.get<i32>());
			if (t == "u64") return Value::integer(static_cast<std::int64_t>(d.get<u64>()));
			if (t == "f32") return Value::real(d.get<f32>());
			if (t == "str")
			{
				const auto n = d.get<u16>();
				d.need(n);
				Value v = Value::text(d.b.substr(d.p, n));
				d.p += n;
				return v;
			}
			if (t == "f32x3")
			{
				d.need(12);
				Value v = Value::list();
				for (int k = 0; k < 3; ++k) v.push(Value::real(d.get<f32>()));
				return v;
			}
			if (t == "u16x16")
			{
				d.need(32);
				Value v = Value::list();
				for (int k = 0; k < 16; ++k) v.push(Value::integer(d.get<u16>()));
				return v;
			}
			if (t.substr(0, 6) == "bytes:")
			{
				const auto n = static_cast<std::size_t>(std::stoul(std::string(t.substr(6))));
				d.need(n);
				Value v = Value::bytes(d.b.substr(d.p, n));
				d.p += n;
				return v;
			}
			if (t == "tail")
			{
				Value v = Value::bytes(d.b.substr(d.p, d.end - d.p));
				d.p = d.end;
				return v;
			}
			if (t.substr(0, 5) == "list:")
			{
				const auto et = t.substr(5);
				const auto n = d.get<u32>();
				const auto c = ceiling(et);
				if (n > c)
				{
					fail("list of " + std::string(et) + ": count " + std::to_string(n) + " above the reader ceiling " + std::to_string(c));
				}
				Value v = Value::list();
				v.l.reserve(n < 4096 ? n : 4096);
				for (u32 k = 0; k < n; ++k) v.push(decode_value(d, et));
				return v;
			}
			if (t.substr(0, 4) == "rec:")
			{
				return decode_record(d, t.substr(4));
			}
			if (t.substr(0, 7) == "packed:")
			{
				const auto* s = packed_schema(t.substr(7));
				if (!s) fail("unknown type " + std::string(t));
				Value v = Value::dict();
				for (std::size_t k = 0; k < s->count; ++k)
				{
					v.set(s->fields[k].name, decode_value(d, s->fields[k].type));
				}
				return v;
			}
			fail("unknown type " + std::string(t));
		}
	}

	Value decode_section(const std::string& tag, const std::string& payload)
	{
		const auto* sc = section_schema(tag);
		if (!sc) fail("KeyError: " + tag);
		Dec d{payload, 0, payload.size()};
		const auto hsize = d.get<u32>();
		if (4ull + hsize > payload.size())
		{
			fail(tag + ": head size " + std::to_string(hsize) + " beyond the payload");
		}
		Dec hd{payload, 4, 4 + static_cast<std::size_t>(hsize)};
		Value out = Value::dict();
		for (std::size_t k = 0; k < sc->head.count; ++k)
		{
			if (hd.p >= hd.end) break;
			out.set(sc->head.fields[k].name, decode_value(hd, sc->head.fields[k].type));
		}
		d.p = 4 + static_cast<std::size_t>(hsize);
		for (std::size_t k = 0; k < sc->lists.count; ++k)
		{
			if (d.p >= d.end)
			{
				out.set(sc->lists.fields[k].name, Value::list());      // an older writer without this list
				continue;
			}
			out.set(sc->lists.fields[k].name, decode_value(d, std::string("list:") + sc->lists.fields[k].type));
		}
		if (d.p != d.end)
		{
			out.set("_extra", Value::bytes(payload.substr(d.p)));
		}
		return out;
	}

	// the newest minor of each section this code reads and writes (SPEC 1.4); a section not listed is at 0.
	// HERO 1: the power slot's flag byte appended to PWR (SPEC revision 2.3)
	u8 section_minor(std::string_view tag)
	{
		return tag == "HERO" ? 1 : 0;
	}

	// ------------------------------------------------------------------------------------------------ file
	std::vector<std::uint8_t> write_file(u8 kind, const std::vector<SectionOut>& secs, const HeaderIn& header)
	{
		struct P
		{
			const SectionOut* s;
			std::string payload;
			u32 records;
		};
		std::vector<P> payloads;
		bool keys = false;
		for (const auto& s : secs)
		{
			const auto* sc = section_schema(s.tag);
			if (!sc && s.data.type != Value::Type::bytes) fail("KeyError: " + s.tag);
			u32 nrec = 0;
			if (sc && sc->lists.count && s.data.type != Value::Type::bytes)
			{
				const char* first = sc->lists.fields[0].name;
				if (s.data.has(first)) nrec = static_cast<u32>(s.data[first].as_list().size());
			}
			payloads.push_back(P{&s, s.data.type == Value::Type::bytes ? s.data.as_bytes() : encode_section(s.tag, s.data), s.data.type == Value::Type::bytes ? s.retained_records : nrec});
			keys |= s.tag == "KEYS";
		}
		const u32 table_off = new_header_size;
		u32 off = table_off + new_section_entry_size * static_cast<u32>(payloads.size());
		std::vector<std::uint8_t> after;
		std::vector<std::uint8_t> body;
		for (const auto& p : payloads)
		{
			const u32 pad = static_cast<u32>((4 - p.payload.size() % 4) % 4);
			NewSectionEntry e{};
			for (int k = 0; k < 4; ++k) e.tag[k] = static_cast<u8>(k < static_cast<int>(p.s->tag.size()) ? p.s->tag[k] : 0);
			e.major = p.s->major;
			e.minor = p.s->minor;
			e.flags = p.s->flags;
			e.offset = off;
			e.length = static_cast<u32>(p.payload.size());
			e.records = p.records;
			e.crc = crc32(reinterpret_cast<const std::uint8_t*>(p.payload.data()), p.payload.size());
			const auto* ep = reinterpret_cast<const std::uint8_t*>(&e);
			after.insert(after.end(), ep, ep + sizeof e);
			body.insert(body.end(), p.payload.begin(), p.payload.end());
			body.insert(body.end(), pad, 0);
			off += static_cast<u32>(p.payload.size()) + pad;
		}
		after.insert(after.end(), body.begin(), body.end());
		NewHeader h{};
		std::memcpy(h.magic, new_magic, 8);
		h.kind = kind;
		h.header_size_8 = new_header_size / 8;
		h.major = 1;
		h.minor = 0;
		// required_features: bit 0 ENTITY_LAYOUT_CONTIGUOUS10 (PHASE-A-PLAN section 4) - the writer's native layout
		// decides it; a rewrite carries the read header's bits; a bit this code does not implement is never written
		if (header.required_features & ~known_required_features)
		{
			fail("required feature bits " + fmt_hex(header.required_features) + " not implemented by this writer");
		}
		h.required_features = header.required_features;
		h.optional_features = header.optional_features | (keys ? optional_keys_present : 0);
		h.file_length = new_header_size + static_cast<u32>(after.size());
		h.section_count = static_cast<u32>(payloads.size());
		h.section_table_offset = table_off;
		h.writer_version = header.writer_version;
		h.content_version = header.content_version;
		h.catalogue_revision = header.catalogue_revision;
		h.campaign_serial = header.campaign_serial;
		h.body_crc = crc32(after);
		h.save_sequence = header.save_sequence;
		const auto* hp = reinterpret_cast<const std::uint8_t*>(&h);
		h.header_crc = crc32(hp, 0x84);
		std::vector<std::uint8_t> out(hp, hp + sizeof h);
		out.insert(out.end(), after.begin(), after.end());
		return out;
	}

	const Value& FileIn::data(const char* tag) const
	{
		const auto it = sections.find(tag);
		if (it == sections.end() || it->second.data.is_none())
		{
			fail(std::string("KeyError: '") + tag + "'");
		}
		return it->second.data;
	}

	FileIn read_file(const std::uint8_t* buf, std::size_t size, int expect_kind)
	{
		if (size < new_header_size)
		{
			fail("file shorter than the header");
		}
		FileIn f;
		std::memcpy(&f.header, buf, sizeof f.header);
		const auto& h = f.header;
		if (std::memcmp(h.magic, new_magic, 8) != 0)
		{
			fail("not an LC save (magic)");
		}
		if (crc32(buf, 0x84) != h.header_crc)
		{
			fail("header CRC mismatch (damaged)");
		}
		if (h.header_size_8 * 8u != new_header_size || h.major != 1)
		{
			fail("format major " + std::to_string(h.major) + " / header size not supported (needs a newer version)");
		}
		if (h.required_features & ~known_required_features)
		{
			fail("required feature bits " + fmt_hex(h.required_features) + " not implemented (needs a newer version)");
		}
		if (expect_kind >= 0 && h.kind != expect_kind)
		{
			fail("file kind " + std::to_string(h.kind) + " is not the expected " + std::to_string(expect_kind));
		}
		if (h.file_length != size || size > max_file)
		{
			fail("file length field " + std::to_string(h.file_length) + " does not match the file (" + std::to_string(size) + ")");
		}
		if (h.legacy_length_guard != 0)
		{
			fail("legacy length guard is not 0");
		}
		if (h.section_count < 1 || h.section_count > max_sections)
		{
			fail("section count");
		}
		const std::uint64_t to = h.section_table_offset;
		if (to < new_header_size || to + std::uint64_t(h.section_count) * new_section_entry_size > size)
		{
			fail("section table outside the file");
		}
		if (crc32(buf + to, size - static_cast<std::size_t>(to)) != h.body_crc)
		{
			fail("body CRC mismatch (damaged)");
		}
		std::uint64_t last_end = to + std::uint64_t(h.section_count) * new_section_entry_size;
		std::vector<std::pair<std::string, SectionIn>> entries;
		for (u32 i = 0; i < h.section_count; ++i)
		{
			SectionIn s;
			std::memcpy(&s.entry, buf + to + i * new_section_entry_size, sizeof s.entry);
			const std::string tag(reinterpret_cast<const char*>(s.entry.tag), 4);
			for (const u8 c : s.entry.tag)
			{
				if (!(c > 32 && c < 127)) fail("section tag is not printable ASCII");
			}
			if (f.sections.count(tag))
			{
				fail("section " + tag + " appears twice");
			}
			if (s.entry.offset < last_end || std::uint64_t(s.entry.offset) + s.entry.length > size)
			{
				fail("section " + tag + ": not ascending / outside the file");
			}
			s.payload.assign(reinterpret_cast<const char*>(buf + s.entry.offset), s.entry.length);
			if (crc32(reinterpret_cast<const std::uint8_t*>(s.payload.data()), s.payload.size()) != s.entry.crc)
			{
				fail("section " + tag + ": CRC mismatch (damaged)");
			}
			last_end = std::uint64_t(s.entry.offset) + s.entry.length;
			f.sections[tag] = SectionIn{};          // reserve the name for the duplicate check
			entries.emplace_back(tag, std::move(s));
		}
		f.sections.clear();
		for (auto& [tag, s] : entries)
		{
			if (section_schema(tag))
			{
				if (s.entry.major > 1)
				{
					fail("section " + tag + ": major " + std::to_string(s.entry.major) + " not supported (needs a newer version)");
				}
				if (s.entry.minor > section_minor(tag) && !(s.entry.flags & section_ext_droppable))
				{
					fail("section " + tag + ": minor " + std::to_string(s.entry.minor) + " not supported (needs a newer version)");
				}
				s.known = true;
				s.data = decode_section(tag, s.payload);
				if (s.data.has("_extra") && s.data["_extra"].truthy() && !(s.entry.flags & section_ext_droppable))
				{
					fail("section " + tag + ": newer content that may not be dropped (needs a newer version)");
				}
			}
			else if (s.entry.flags & section_required)
			{
				fail("unknown section " + tag + " is REQUIRED (needs a newer version)");
			}
			f.order.push_back(tag);
			f.sections[tag] = std::move(s);
		}
		std::vector<const char*> required;
		if (h.kind == kind_campaign)
		{
			for (const char* t : campaign_sections)
			{
				if (std::strcmp(t, "KEYS") && std::strcmp(t, "LGCY")) required.push_back(t);
			}
		}
		else if (h.kind == kind_profile)
		{
			required = {"META", "PRFO", "PRFP", "PRFS"};
		}
		else if (h.kind == kind_transfer)
		{
			for (const char* t : campaign_sections)
			{
				if (std::strcmp(t, "KEYS") && std::strcmp(t, "LGCY") && std::strcmp(t, "PRFO") && std::strcmp(t, "PRFP") && std::strcmp(t, "PRFS"))
				{
					required.push_back(t);
				}
			}
		}
		for (const char* t : required)
		{
			if (!f.sections.count(t))
			{
				fail(std::string("section ") + t + " missing");
			}
		}
		return f;
	}
}
