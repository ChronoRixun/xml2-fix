// The reverse reader (convert.reverse_slot + PositionTables): LC campaign -> the old model the engine's loader
// reads, through the running build's tables (catalogue id -> key hash -> position). Fields the new format dropped
// are zero unless a template (the original's model) is given; the template is the reference's verification aid.

#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace lcsave::detail
{
	// Installed engine budgets are shared by both runtime saver directions and native loaders.
	void validate_native_tables(const TablesData& tables)
	{
		if (!tables.build) fail("native translation needs build tables");
		const auto& b = *tables.build;
		if (b.item.size() > 160 || b.T.ITEM.size() > 160) fail("native item definition budget exceeded (160)");
		if (b.hero_count > 21 || b.T.hero_count > 21) fail("native hero record budget exceeded (21)");
		// Conservatively 297 total slots, including reserved index 0, until the registry boundary is proven.
		if (b.stat.size() > 297 || b.T.STAT.size() > 297) fail("native stats table budget exceeded (297 slots including reserved zero)");
		if (b.T.NAMA.size() > 32) fail("native flat name budget exceeded (32)");
		for (const auto& [category, names] : b.T.NAMB) { (void)category; if (names.size() > 4) fail("native category name budget exceeded (4)"); }
		if (b.T.TRIT.size() > 128) fail("native special item budget exceeded (128)");
		for (const auto& [owner, costumes] : b.T.COST)
		{
			(void)owner;
			if (costumes.size() > 10) fail("native costume budget exceeded (10)");
			for (const auto& [slot, id] : costumes) { (void)id; if (slot >= 10) fail("native costume slot outside 0..9"); }
		}
	}

	namespace
	{
		const char* const key_item_names[6] = {"KEY1", "KEY2", "KEY3", "KEYCARD1", "KEYCARD2", "KEYCARD3"};

		template <typename T> std::string fmt_id(T v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "0x%x", static_cast<unsigned>(v));
			return b;
		}

		template <typename M> const typename M::mapped_type& get(const M& table, std::int64_t v, const char* what)
		{
			const auto it = table.find(static_cast<u32>(v));
			if (v < 0 || v > 0xffffffffll || it == table.end())
			{
				fail(std::string(what) + ": id " + fmt_id(v) + " is not in this build (parked / unknown)");
			}
			return it->second;
		}

		template <typename T> T narrow(std::int64_t v, std::int64_t lo, std::int64_t hi, const char* fmt)
		{
			if (v < lo || v > hi)
			{
				fail(std::string("'") + fmt + "' format requires " + std::to_string(lo) + " <= number <= " + std::to_string(hi));
			}
			return static_cast<T>(v);
		}
		u8 B(const Value& v) { return narrow<u8>(v.as_int(), 0, 255, "B"); }
		u16 H(const Value& v) { return narrow<u16>(v.as_int(), 0, 65535, "H"); }
		u32 U(const Value& v) { return narrow<u32>(v.as_int(), 0, 0xffffffffll, "I"); }

		void finite_values(const Value& v)
		{
			if (v.type == Value::Type::real && !std::isfinite(v.as_real())) fail("nonfinite native value");
			for (const auto& x : v.l) finite_values(x);
			for (const auto& x : v.d) finite_values(x.value);
		}
		void unique(const Value& rows, const char* key)
		{
			std::set<std::int64_t> seen;
			for (const auto& row : rows.as_list()) if (!seen.insert(row[key].as_int()).second) fail(std::string("duplicate ") + key);
		}
		void preflight(const FileIn& f)
		{
			for (const auto& [tag, sec] : f.sections) if (sec.known) finite_values(sec.data);
			if (f.header.kind == kind_profile) return;
			const auto& item = f.data("ITEM");
			unique(item["world"], "handle"); unique(item["def_state"], "item");
			std::set<u32> handles;
			for (const auto& w : item["world"].as_list()) { const auto h = U(w["handle"]); if (h >= 150) fail("world handle outside native pool"); handles.insert(h); }
			unique(f.data("HERO")["heroes"], "hero"); unique(f.data("REST")["records"], "owner"); unique(f.data("ZONE")["records"], "zone");
			for (const auto& snap : f.data("MISS")["snapshots"].as_list()) { unique(snap["seats"], "seat"); unique(snap["objectives"], "objective"); }
			for (const auto& z : f.data("ZONE")["records"].as_list())
			{
				const auto& names = z["names"].as_list();
				const auto& fog = z["fog"];
				if (fog["encoding"].as_int() != 1 || fog["width"].as_int() != 82 || fog["height"].as_int() != 82 || fog["data"].as_bytes().size() != old_fog_bytes) fail("unsupported native fog");
				std::set<u32> ordinals, placements;
				for (const auto& state : z["states"].as_list())
				{
					for (const auto key : {"def_name", "inst_name"}) if (U(state[key]) >= names.size()) fail("state name index outside names");
					const auto list = state["list"].as_int(); if (list < 1 || list > 3) fail("unknown native state list");
					if (list == 1 && (!ordinals.insert(U(state["ordinal"])).second || U(state["ordinal"]) < 1 || U(state["ordinal"]) > old_zone_ordinals)) fail("duplicate or invalid ordinal");
					if (list == 1 && !placements.insert(U(state["placement_index"])).second) fail("duplicate destination placement");
					for (const auto& value : state["position"].as_list()) { const auto x = value.as_real(); if (!std::isfinite(x) || x < -32768 || x > 32767 || x != std::nearbyint(x)) fail("state position not representable"); }
					B(state["flags_a"]); B(state["flags_b"]);
					const auto& payload = state["payload"].as_bytes();
					if (state["payload_kind"].as_int() == 2)
					{ u32 handle = 0; if (payload.size() != 13 || !(state["flags_a"].as_int() & 1)) fail("world payload contract"); std::memcpy(&handle, payload.data(), 4); if (!handles.count(handle)) fail("state world item reference missing"); i32 qty = 0; std::memcpy(&qty, payload.data() + 4, 4); if (qty < -32768 || qty > 32767) fail("world quantity outside native range"); }
					else if (state["payload_kind"].as_int() != 1 || payload.size() != 8 || (state["flags_a"].as_int() & 1)) fail("engine payload contract");
				}
			}
		}

		struct Pos
		{
			const TablesData& t;
			const Build& b;

			// Installed native arrays, independent of the LC reader's future-facing ceilings (SPEC 2.16).
			Pos(const TablesData& tables, const Build& build) : t(tables), b(build)
			{
				validate_native_tables(tables);
			}

			u32 stat(std::int64_t v) const { return narrow<u32>(get(t.p_stat, v, "character"), 1, 296, "native stats index"); }
			u32 item(std::int64_t v) const { return narrow<u32>(get(t.p_item, v, "item"), 0, 159, "native item definition"); }
			u32 course(std::int64_t v) const { return get(t.p_course, v, "course"); }
			u32 grade(std::int64_t v) const { return get(t.p_grade, v, "grade"); }
			u32 xpoint(std::int64_t v) const { return get(t.p_xp, v, "town centre"); }
			std::pair<int, int> trivia(std::int64_t v) const { return get(t.p_triv, v, "trivia"); }
			const std::string& zone_name(std::int64_t v) const { return get(t.p_zone, v, "zone"); }
			u32 costume_slot(std::int64_t v) const { return narrow<u32>(get(t.p_cost, v, "costume"), 0, 9, "native costume slot"); }

			OldInstance instance(const Value& r) const
			{
				OldInstance inst;
				inst.head.definition = narrow<u8>(item(r["definition"].as_int()), 0, 255, "B");
				inst.head.name_a = r["name_a"].truthy() ? narrow<u8>(get(t.p_nama, r["name_a"].as_int(), "item name"), 0, 31, "native flat name index") : 0xff;
				inst.head.name_b = r["name_b"].truthy() ? narrow<u8>(get(t.p_namb, r["name_b"].as_int(), "item name"), 0, 3, "native category name index") : 0xff;
				inst.head.special = r["special"].truthy() ? narrow<u8>(get(t.p_trit, r["special"].as_int(), "special item"), 0, 127, "native special item index") : 0xff;
				inst.head.location = B(r["location"]);
				const char* const lists[3] = {"prefixes", "own", "suffixes"};
				for (int k = 0; k < 3; ++k)
				{
					for (const auto& e : r[lists[k]].as_list())
					{
						OldEnhElement el{};
						el.record = narrow<u16>(get(t.p_enh, e["enhancement"].as_int(), "enhancement"), 0, 65535, "H");
						el.value = U(e["value"]);
						inst.lists[k].push_back(el);
					}
				}
				const auto& era = b.T;
				if (r["name_b"].truthy())
				{
					const auto category = inst.head.definition < era.ITEM_cat.size() ? era.ITEM_cat[inst.head.definition] : 0;
					const auto found = era.NAMB.find(category);
					if (found == era.NAMB.end() || std::find(found->second.begin(), found->second.end(), r["name_b"].as_int()) == found->second.end()) fail("item name category does not match definition");
				}
				for (const auto& e : inst.lists[0]) if (e.record < era.prefix[0] || e.record >= era.prefix[1]) fail("prefix enhancement not a prefix");
				for (const auto& e : inst.lists[2]) if (e.record < era.suffix[0] || e.record >= era.suffix[1]) fail("suffix enhancement not a suffix");
				std::set<u32> allowed;
				if (inst.head.definition < era.ITEM_own.size()) allowed.insert(era.ITEM_own[inst.head.definition].begin(), era.ITEM_own[inst.head.definition].end());
				if (inst.head.special != 0xff && inst.head.special < era.TRIT_own.size()) allowed.insert(era.TRIT_own[inst.head.special].begin(), era.TRIT_own[inst.head.special].end());
				for (const auto& e : inst.lists[1]) if (e.record >= era.ENHC.size() || !allowed.count(era.ENHC[e.record])) fail("own enhancement does not belong to definition");
				return inst;
			}

			void hero_block(const Value& h, OldAttributes& a, OldEquipment& eq) const
			{
				unique(h["talents"], "talent");
				const auto owner = stat(h[h.has("hero") ? "hero" : "owner"].as_int());
				const auto& tr = h["traits"].as_list();
				if (tr.size() != 16) fail("traits");
				a = OldAttributes{};
				for (int k = 0; k < 4; ++k)
				{
					a.scalars.trait_base[k] = B(tr[k * 4]);
					a.scalars.trait_pending[k] = B(tr[k * 4 + 1]);
					a.scalars.trait_bonus[k] = B(tr[k * 4 + 2]);
					a.scalars.trait_bonus2[k] = B(tr[k * 4 + 3]);
				}
				a.scalars.skill_points = H(h["skill_points"]);
				a.scalars.trait_points = H(h["trait_points"]);
				a.scalars.level = B(h["level"]);
				a.scalars.ai_level = B(h["ai_level"]);
				a.scalars.ai_attack = B(h["ai_attack"]);
				a.scalars.pad17 = 0;
				std::vector<OldTalentEntry> ents;
				for (const auto& tv : h["talents"].as_list())
				{
					OldTalentEntry e{};
					e.id = narrow<u16>(get(t.p_tal, tv["talent"].as_int(), "talent"), 0, 65535, "H");
					if (e.id >= 100 && static_cast<u32>(e.id / 100) != owner + 1) fail("talent does not belong to hero");
					e.rank_bits = static_cast<u8>(narrow<u8>(tv["rank"].as_int(), 0, 63, "talent rank"));
					e.pending_cap_bits = static_cast<u8>((narrow<u8>(tv["pending"].as_int(), 0, 63, "talent pending")) | ((narrow<u8>(tv["cap"].as_int(), 0, 3, "talent cap")) << 6));
					e.base_bits = static_cast<u8>(narrow<u8>(tv["base"].as_int(), 0, 3, "talent base"));
					e.byte5 = B(tv["byte5"]);
					ents.push_back(e);
				}
				std::stable_sort(ents.begin(), ents.end(), [](const OldTalentEntry& x, const OldTalentEntry& y) { return x.id < y.id; });
				if (ents.size() > old_talent_entries)
				{
					fail(std::to_string(ents.size()) + " talents do not fit the engine heap of " + std::to_string(old_talent_entries));
				}
				a.tail.count = static_cast<u32>(ents.size());
				a.tail.bitmap = static_cast<u32>((1ull << ents.size()) - 1);
				for (std::size_t k = 0; k < ents.size(); ++k) a.talents[k] = ents[k];
				eq = OldEquipment{};
				for (const auto& x : h["equipped"].as_list()) eq.equipped.push_back(instance(x));
				eq.tail.health = static_cast<float>(h["health"].as_real());
				eq.tail.energy = static_cast<float>(h["energy"].as_real());
				eq.tail.player_seat = narrow<i32>(h["player_seat"].as_int(), -1, 3, "player seat");
				eq.tail.vitals_valid = B(h["vitals_valid"]);
				eq.tail.experience = U(h["experience"]);
				eq.tail.ack_level = B(h["ack_level"]);
				eq.tail.health_boosts = B(h["health_boosts"]);
				eq.tail.energy_boosts = B(h["energy_boosts"]);
				eq.tail.levels_purchased = B(h["levels_purchased"]);
				eq.tail.points_bought = B(h["points_bought"]);
				eq.tail.respec_count = B(h["respec_count"]);
				eq.tail.skirmish_boost = B(h["skirmish_boost"]);
				eq.tail.flags = static_cast<u8>(narrow<u8>(h["flags"].as_int(), 0, 31, "hero flags"));
				if (eq.equipped.size() > 3) fail("more than 3 equipped items do not fit the engine");
			}

			OldSnapshot snapshot(const Value& s) const
			{
				OldSnapshot out{};
				if (s["zone"].truthy()) set_cstr(out.zone, zone_name(s["zone"].as_int()), Enc::latin1);
				for (const auto& seat : s["seats"].as_list())
				{
					const auto i = seat["seat"].as_int();
					if (i < 0 || i >= 4) fail("IndexError: list assignment index out of range");
					// the engine writes seat names lower-case (all 82 corpus saves); the build's name is its catalogue key
					set_cstr(out.seat_name[i], py_lower(unescape_key(b.stat[stat(seat["hero"].as_int())]), Enc::latin1), Enc::latin1);
					const auto& p = seat["position"].as_list();
					const auto& o = seat["orientation"].as_list();
					for (int k = 0; k < 3; ++k)
					{
						out.seat_pose[i * 6 + k] = static_cast<float>(p.at(static_cast<std::size_t>(k)).as_real());
						out.seat_pose[i * 6 + 3 + k] = static_cast<float>(o.at(static_cast<std::size_t>(k)).as_real());
					}
				}
				for (const auto& o : s["objectives"].as_list())
				{
					const auto i = get(t.p_obj, o["objective"].as_int(), "objective");
					if (i >= 300) fail("IndexError: list assignment index out of range");
					out.objectives[i] = static_cast<u8>((narrow<u8>(o["counter"].as_int(), 0, 63, "objective counter")) | ((narrow<u8>(o["flags"].as_int(), 0, 3, "objective flags")) << 6));
				}
				const auto& ap = s["anchor_pos"].as_list();
				const auto& ao = s["anchor_orient"].as_list();
				for (int k = 0; k < 3; ++k)
				{
					out.anchor_pos[k] = static_cast<float>(ap.at(static_cast<std::size_t>(k)).as_real());
					out.anchor_orient[k] = static_cast<float>(ao.at(static_cast<std::size_t>(k)).as_real());
				}
				return out;
			}

			void review_words(const Value& ids, u32* words) const
			{
				for (const auto& v : ids.as_list())
				{
					const auto [c, i] = get(t.p_review, v.as_int(), "review entry");
					words[c * 3 + i / 32] |= 1u << (i % 32);
				}
			}
		};

		std::uint32_t hash_string(const std::string& raw)
		{
			std::uint32_t h = 0;
			for (const unsigned char c : raw) h = h * 31 + c;
			return h;
		}

		void zone_records(const Pos& pos, const Value& recs, OldSlot& m, std::vector<std::string>& notes, bool legacy_import)
		{
			// Drop only dependencies owned exclusively by rejected records. Shared and unowned handles survive.
			std::set<u32> owned, applied;
			auto dependencies = [](const Value& record, std::set<u32>& handles)
			{
				for (const auto& state : record["states"].as_list()) if (state["payload_kind"].as_int() == 2)
				{
					u32 handle = 0;
					std::memcpy(&handle, state["payload"].as_bytes().data(), sizeof handle); // preflight checked framing
					handles.insert(handle);
				}
			};
			for (const auto& record : recs.as_list()) dependencies(record, owned);
			auto& strings = *m.zone_strings;
			std::memset(&strings, 0, sizeof strings);
			strings.used = 2;
			strings.offsets[0] = 1;
			std::vector<std::pair<std::string, u32>> interned;
			auto intern = [&](const std::string& text, Enc enc) -> u32
			{
				if (text.empty()) return 0;
				for (const auto& [k, v] : interned) if (k == text) return v;
				if (!encodable_cp1252(text, enc)) fail("'charmap' codec can't encode character in " + py_repr(text, enc));
				u32 slot = (hash_string(text) & 0x7ff);
				if (!slot) slot = 1;
				u32 tries = 0;
				while (strings.offsets[slot])
				{
					slot = (slot + 1) & 0x7ff;
					if (!slot) slot = 1;
					if (++tries > old_zone_string_slots) fail("zone string table full");
				}
				const u32 off = strings.used;
				if (off + text.size() + 1 > old_zone_arena) fail("zone string arena full");
				std::memcpy(strings.arena + off, text.data(), text.size());
				strings.arena[off + text.size()] = 0;
				strings.offsets[slot] = off;
				strings.used = off + static_cast<u32>(text.size()) + 1;
				interned.emplace_back(text, slot);
				return slot;
			};
			for (const auto& r : recs.as_list())
			{
				const auto& zone = pos.zone_name(r["zone"].as_int());
				const auto* num = pos.b.zone_numbering(zone);
				if (!num) fail("zone " + py_repr(zone, Enc::latin1) + " has no placement file in this build");
				if (r["identity_mode"].as_int() != 2) fail("identity mode 1 (entity tags) is not implemented by the reverse reader");
				if (r["capture_flags"].as_int() & ~3ll) fail("unsupported capture status bits");
				if (r["capture_flags"].as_int() & 2) notes.push_back("zone " + zone + ": legacy capture provenance inferred (unknown completeness)");
				if (r["capture_flags"].as_int() & 1)
				{ notes.push_back("zone " + zone + ": incomplete capture: record not applied"); continue; }
				if (r["content_hash"].as_bytes() != std::string(reinterpret_cast<const char*>(num->sha1.data()), 20))
				{
					notes.push_back("zone " + py_repr(zone, Enc::latin1) + ": placement file changed since the record was made: record not applied");
					continue;
				}

				std::map<std::int64_t, std::size_t> mapped;
				bool differs = false;
				const auto& saved_names = r["names"].as_list();
				for (const auto& state : r["states"].as_list()) if (state["list"].as_int() == 1)
				{
					const auto placement = state["placement_index"].as_int();
					const auto it = std::find(num->placement.begin(), num->placement.end(), placement);
					if (it == num->placement.end()) { differs = true; break; }
					const auto current = static_cast<std::size_t>(it - num->placement.begin());
					const auto& dn = saved_names.at(static_cast<std::size_t>(state["def_name"].as_int()));
					const auto& in = saved_names.at(static_cast<std::size_t>(state["inst_name"].as_int()));
					if (key_hash1(dn.as_text(), dn.enc) != num->names[current].first || key_hash1(in.as_text(), in.enc) != num->names[current].second)
					{ differs = true; break; }
					mapped[state["ordinal"].as_int()] = current + 1;
				}
				if (differs) { notes.push_back("zone " + zone + ": entity placement/name changed: record not applied"); continue; }
				// Legacy lists can contain placements that later builds pin. A list entry proves
				// existence; the missing numbered slot does not mean removal in this case.
				std::map<std::size_t, std::size_t> promoted;
				if (legacy_import || (r["capture_flags"].as_int() & 2))
				{
					std::map<std::size_t, std::size_t> candidates;
					std::map<std::size_t, std::size_t> counts;
					const auto& states = r["states"].as_list();
					for (std::size_t i = 0; i < states.size(); ++i)
					{
						const auto& state = states[i];
						if (state["list"].as_int() != 2 && state["list"].as_int() != 3) continue;
						const auto& dn = saved_names.at(static_cast<std::size_t>(state["def_name"].as_int()));
						const auto& in = saved_names.at(static_cast<std::size_t>(state["inst_name"].as_int()));
						const auto pair = std::make_pair(key_hash1(dn.as_text(), dn.enc), key_hash1(in.as_text(), in.enc));
						std::size_t ordinal = 0, matches = 0;
						for (std::size_t k = 0; k < num->names.size(); ++k)
							if (num->names[k] == pair) { ordinal = k + 1; ++matches; }
						if (matches != 1 || std::any_of(mapped.begin(), mapped.end(), [&](const auto& e) { return e.second == ordinal; })) continue;
						candidates[i] = ordinal; ++counts[ordinal];
					}
					for (const auto& [i, ordinal] : candidates) if (counts[ordinal] == 1) promoted[i] = ordinal;
				}
				OldZone z;
				z.states.assign(old_zone_states, OldStateRecord{});
				std::size_t n = 1;
				std::vector<u16> dynamic, smart;
				const auto& names = r["names"].as_list();
				auto name = [&](const Value& idx) -> const Value&
				{
					const auto k = idx.as_int();
					if (k < 0 || k >= static_cast<std::int64_t>(names.size())) fail("IndexError: list index out of range");
					return names[static_cast<std::size_t>(k)];
				};
				std::size_t state_index = 0;
				for (const auto& s : r["states"].as_list())
				{
					const auto promotion = promoted.find(state_index++);
					if (n >= old_zone_states) fail("more states than the engine record holds");
					OldStateRecord st{};
					const auto& payload = s["payload"].as_bytes();
					if (s["payload_kind"].as_int() == 2)
					{
						if (payload.size() < 8) fail("unpack requires a buffer of 8 bytes");
						u32 slot = 0;
						i32 qty = 0;
						std::memcpy(&slot, payload.data(), 4);
						std::memcpy(&qty, payload.data() + 4, 4);
						if (slot > 0xff || qty < -32768 || qty > 32767) fail("world item handle / quantity do not fit the engine record");
						st.payload[0] = static_cast<u8>(slot);
						const i16 q = static_cast<i16>(qty);
						std::memcpy(st.payload + 1, &q, 2);
						const std::string rest = payload.substr(8, 5);
						if (rest.size() != 5) fail("payload: 8 bytes, layout says 8");
						std::memcpy(st.payload + 3, rest.data(), 5);
					}
					else
					{
						if (payload.size() < 8) fail("payload: " + std::to_string(payload.size()) + " bytes, layout says 8");
						std::memcpy(st.payload, payload.data(), 8);
					}
					const auto& pp = s["position"].as_list();
					for (int k = 0; k < 3; ++k)
					{
						const double x = pp.at(static_cast<std::size_t>(k)).as_real();
						if (!std::isfinite(x) || x < -32768 || x > 32767 || x != std::nearbyint(x)) fail("state position not representable");   // round half to even
						st.position[k] = narrow<i16>(static_cast<std::int64_t>(x), -32768, 32767, "state position");
					}
					const auto& dn = name(s["def_name"]);
					st.def_name = intern(dn.as_text(), dn.enc);
					const auto& in = name(s["inst_name"]);
					st.inst_name = intern(in.as_text(), in.enc);
					st.flags_a = static_cast<u8>(B(s["flags_a"]));
					st.flags_b = static_cast<u8>(B(s["flags_b"]));
					const auto idx = static_cast<u16>(n);
					z.states[n++] = st;
					const auto list = s["list"].as_int();
					if (list == 1)
					{
						const auto k = static_cast<std::int64_t>(mapped.at(s["ordinal"].as_int()));
						if (!(1 <= k && k <= old_zone_ordinals)) fail("ordinal " + std::to_string(k) + " outside the engine map");
						if (k > static_cast<std::int64_t>(num->placement.size()))
						{
							fail("ordinal " + std::to_string(k) + " beyond the zone numbering (" + std::to_string(num->placement.size()) + ")");
						}
						z.tail.ordinal_map[k - 1] = idx;
					}
					else if (promotion != promoted.end())
					{
						const auto k = promotion->second;
						if (k > old_zone_ordinals) fail("promoted ordinal outside the engine map");
						z.tail.ordinal_map[k - 1] = idx;
						z.states[idx].flags_b |= 8;
						notes.push_back("zone " + py_repr(zone, Enc::latin1) + ": legacy unnumbered state restored as numbered placement " + std::to_string(k));
					}
					else if (list == 2) dynamic.push_back(idx);
					else smart.push_back(idx);
				}
				z.name_handle = intern(zone, Enc::latin1);
				z.tail.state_count = static_cast<u32>(1 + r["states"].as_list().size());
				if (dynamic.size() > old_zone_dynamic || smart.size() > old_zone_smart) fail("pack expected 300 items for packing");
				std::copy(dynamic.begin(), dynamic.end(), z.tail.dynamic);
				z.tail.dynamic_count = static_cast<u32>(dynamic.size());
				std::copy(smart.begin(), smart.end(), z.tail.smart);
				z.tail.smart_count = static_cast<u32>(smart.size());
				const auto& fog = r["fog"]["data"].as_bytes();
				if (fog.size() != old_fog_bytes) fail("fog: " + std::to_string(fog.size()) + " bytes, layout says 844");
				std::memcpy(z.tail.fog, fog.data(), old_fog_bytes);
				restore_conversation(r, zone, pos.t, z.tail.conversation, notes);
				z.tail.time_stamp = static_cast<float>(r["time_stamp"].as_real());
				z.tail.flag = B(r["flag"]);
				m.zones.push_back(std::move(z));
				dependencies(r, applied);
			}
			m.world.erase(std::remove_if(m.world.begin(), m.world.end(), [&](const OldWorld& item)
			{ return owned.count(item.slot) && !applied.count(item.slot); }), m.world.end());
			if (m.zones.size() > ceil_zone_records) fail("more zone records than the container holds");
		}

		void copy_if_same(char* field, const char* original, std::size_t size)
		{
			if (cstr(field, size) == cstr(original, size)) std::memcpy(field, original, size);
		}
		template <std::size_t N> void copy_if_same(char (&field)[N], const char (&original)[N]) { copy_if_same(field, original, N); }

		void template_block(OldAttributes& a, const OldAttributes& ta, OldEquipment& e, const OldEquipment& te)
		{
			a.scalars.pad17 = ta.scalars.pad17;
			for (u32 k = 0; k < old_talent_entries; ++k)
			{
				a.talents[k].rank_bits = static_cast<u8>((a.talents[k].rank_bits & 0x3f) | (ta.talents[k].rank_bits & 0xc0));
				a.talents[k].base_bits = static_cast<u8>((a.talents[k].base_bits & 3) | (ta.talents[k].base_bits & 0xfc));
				a.talents[k].pad6 = ta.talents[k].pad6;
			}
			for (u32 k = a.tail.count; k < old_talent_entries; ++k) a.talents[k] = ta.talents[k];
			a.tail.bitmap = ta.tail.bitmap;
			std::memcpy(e.tail.pad0d, te.tail.pad0d, 3);
			e.tail.vitals_valid = te.tail.vitals_valid;
			e.tail.flags = static_cast<u8>((e.tail.flags & 0x1f) | (te.tail.flags & 0xe0));
		}

		// convert._apply_template: the bytes the new format drops, from the original's model
		void apply_template(OldSlot& m, const OldSlot& t)
		{
			std::memcpy(m.description_raw, t.description_raw, 0x80);
			std::memcpy(m.game.unused_a5, t.game.unused_a5, 6);
			m.game.pad_ab = t.game.pad_ab;
			m.game.pad_db = t.game.pad_db;
			copy_if_same(m.game.loading_name, t.game.loading_name);
			copy_if_same(m.game.timer_script, t.game.timer_script);
			copy_if_same(m.game.recall_zone, t.game.recall_zone);
			const auto recall = cstr(m.game.recall_zone);
			if (recall.empty() || recall == "none") std::memcpy(m.game.recall_pos, t.game.recall_pos, sizeof m.game.recall_pos);
			for (std::size_t k = 0; k < m.heroes.size() && k < t.heroes.size(); ++k)
			{
				template_block(m.heroes[k].attributes, t.heroes[k].attributes, m.heroes[k].equipment, t.heroes[k].equipment);
				// a slot keeps the original's bytes (stale ones after the NUL) while its name and flag byte are the same
				for (int s = 0; s < 4; ++s)
				{
					auto& ms = m.heroes[k].powers.power[s];
					const auto& ts = t.heroes[k].powers.power[s];
					if (cstr(ms.name) == cstr(ts.name) && ms.flags == ts.flags) ms = ts;
				}
				m.heroes[k].powers.pad54 = t.heroes[k].powers.pad54;
			}
			for (std::size_t k = 0; k < m.restore.size() && k < t.restore.size(); ++k)
			{
				template_block(m.restore[k].attributes, t.restore[k].attributes, m.restore[k].equipment, t.restore[k].equipment);
			}
			OldSnapshot* ms[3] = {&m.mission, &m.side_frames[0], &m.side_frames[1]};
			const OldSnapshot* ts[3] = {&t.mission, &t.side_frames[0], &t.side_frames[1]};
			for (int k = 0; k < 3; ++k)
			{
				ms[k]->pad_a0 = ts[k]->pad_a0;
				ms[k]->pad_124 = ts[k]->pad_124;
				ms[k]->objective_301 = ts[k]->objective_301;
				ms[k]->pad_252 = ts[k]->pad_252;
				copy_if_same(ms[k]->zone, ts[k]->zone);
				for (int j = 0; j < 4; ++j) copy_if_same(ms[k]->seat_name[j], ts[k]->seat_name[j]);
			}
			for (u32 i = m.side.count; i < old_side_frames; ++i) m.side_frames[i] = t.side_frames[i];
			m.side.pad_4d8 = t.side.pad_4d8;
			m.course_residue = t.course_residue;
			for (u32 i = 6; i < old_dr_grades; ++i) m.grades[i] = t.grades[i];
			m.review.unlocked[15] = t.review.unlocked[15];
			m.review.seen[15] = t.review.seen[15];
			m.items.def_count = t.items.def_count;
			m.profile.tail.pad_1b4 = t.profile.tail.pad_1b4;
			m.profile.tail.pad_220 = t.profile.tail.pad_220;
			std::memcpy(m.profile.tail.pad_22a, t.profile.tail.pad_22a, 2);
			copy_if_same(m.profile.head.player_name, t.profile.head.player_name);
			for (u32 i = t.profile.tail.score_count; i < old_high_scores; ++i) m.profile.high_scores[i] = t.profile.high_scores[i];
			for (u32 i = 0; i < t.profile.tail.score_count && i < old_high_scores; ++i)
			{
				std::memcpy(m.profile.high_scores[i].pad1, t.profile.high_scores[i].pad1, 3);
			}
			for (u32 i = t.unlocked_count; i < old_unlocked_slots; ++i) m.unlocked[i] = t.unlocked[i];
			auto vars = [](OldScriptVar* v, const OldScriptVar* tv, u32 n)
			{
				for (u32 k = 0; k < n; ++k)
				{
					copy_if_same(v[k].name, tv[k].name);
					if (cstr(v[k].name).empty()) std::memcpy(v[k].value_be, tv[k].value_be, 4);
				}
			};
			vars(m.zone_vars, t.zone_vars, old_script_zone_vars);
			vars(m.game_vars, t.game_vars, old_script_game_vars);
			m.slack = t.slack;
			*m.zone_strings = *t.zone_strings;
			m.zones = t.zones;
		}
	}

	namespace
	{
		// PositionTables.profile: the three profile sections into the engine's 0x228-byte block
		void profile_from_sections(const Pos& pos, const FileIn& f, OldProfile& profile)
		{
			const auto& O = f.data("PRFO");
			const auto& P = f.data("PRFP");
			const auto& Sc = f.data("PRFS");
			const auto options = narrow<u32>(O["options"].as_int(), 0, 0xffffff, "profile options");
			profile.head.options[0] = static_cast<u8>(options & 0xff);
			profile.head.options[1] = static_cast<u8>((options >> 8) & 0xff);
			profile.head.options[2] = static_cast<u8>((options >> 16) & 0xff);
			set_cstr(profile.head.player_name, O["player_name"].as_text());
			const auto& scores = Sc["scores"].as_list();
			if (scores.size() > old_high_scores) fail("more high scores than the engine holds");
			for (std::size_t k = 0; k < scores.size(); ++k)
			{
				auto& s = profile.high_scores[k];
				s.course = narrow<u8>(pos.course(scores[k]["course"].as_int()), 0, 255, "B");
				const auto& a = scores[k]["party_a"].as_list();
				const auto& bb = scores[k]["party_b"].as_list();
				if (a.size() > 6 || bb.size() > 6) fail("pack expected 6 items for packing");
				for (std::size_t j = 0; j < a.size(); ++j) s.party_a[j] = narrow<u16>(pos.stat(a[j].as_int()), 0, 65535, "H");
				for (std::size_t j = 0; j < bb.size(); ++j) s.party_b[j] = narrow<u16>(pos.stat(bb[j].as_int()), 0, 65535, "H");
				s.count_a = static_cast<u32>(a.size());
				s.count_b = static_cast<u32>(bb.size());
				s.score = U(scores[k]["score"]);
			}
			profile.tail.score_count = static_cast<u32>(scores.size());
			for (const auto& v : P["characters"].as_list())
			{
				const auto i = pos.stat(v.as_int());
				if (i >= 320) fail("IndexError: list index out of range");
				profile.tail.char_unlock[i / 32] |= 1u << (i % 32);
			}
			u32 rw[16] = {};
			pos.review_words(P["review"], rw);
			std::memcpy(profile.tail.review, rw, sizeof profile.tail.review);
			profile.tail.highest_level = B(P["highest_level"]);
			profile.tail.flags_229 = B(P["flags"]);
		}
	}

	void validate_native_sections(const FileIn& file)
	{
		preflight(file);
	}

	OldSlot reverse_slot(const std::vector<std::uint8_t>& new_bytes, const TablesData& t, const OldSlot* tmpl, std::vector<std::string>& notes)
	{
		const auto f = read_file(new_bytes.data(), new_bytes.size(), kind_campaign);
		if (!t.build) fail("the reverse reader needs the build (--build)");
		preflight(f);
		const Pos pos{t, *t.build};
		const auto& b = *t.build;
		OldSlot m;
		const auto& meta = f.data("META");
		const auto description = meta["description"].as_text();
		set_cstr(reinterpret_cast<char*>(m.description_raw), 0x80, description, meta["description"].enc);
		m.begin_tag = "[SAVEGAMEBEGIN: " + description + "]";
		m.end_tag = "[SAVEGAMEEND: " + description + "]";
		// ---- GAME
		const auto& G = f.data("GAME");
		const std::string recall = G["recall_zone"].truthy() ? pos.zone_name(G["recall_zone"].as_int()) : std::string("none");
		u32 trivia[8] = {};
		for (const auto& v : G["trivia"].as_list())
		{
			const auto [act, q] = pos.trivia(v.as_int());
			const int bit = (act - 1) * 50 + q;
			if (bit < 0 || bit >= 256) fail("IndexError: list index out of range");
			trivia[bit / 32] |= 1u << (bit % 32);
		}
		const auto fl = narrow<u16>(G["flags"].as_int(), 0, 1023, "game flags");
		auto& g = m.game;
		g.timer_threshold = static_cast<float>(G["timer_threshold"].as_real());
		g.timer_remaining = static_cast<float>(G["timer_remaining"].as_real());
		g.timer_after_add = static_cast<float>(G["timer_after_add"].as_real());
		g.timer_last_add = static_cast<float>(G["timer_last_add"].as_real());
		set_cstr(g.timer_script, G["timer_script"].as_text());
		g.recall_cooldown = static_cast<float>(G["recall_cooldown"].as_real());
		g.xtreme_charges = narrow<i16>(G["xtreme_charges"].as_int(), -32768, 32767, "h");
		g.xtreme_pips = narrow<i16>(G["xtreme_pips"].as_int(), -32768, 32767, "h");
		set_cstr(g.loading_name, G["loading_name"].as_text());
		set_cstr(g.recall_zone, recall, Enc::latin1);
		const auto& rp = G["recall_pos"].as_list();
		for (int k = 0; k < 3; ++k) g.recall_pos[k] = static_cast<float>(rp.at(static_cast<std::size_t>(k)).as_real());
		g.act = B(G["act"]);
		std::memcpy(g.trivia, trivia, sizeof trivia);
		g.value_608 = U(G["value_608"]);
		g.difficulty = U(G["difficulty"]);
		g.play_time = static_cast<float>(G["play_time"].as_real());
		g.flags_d8 = static_cast<u8>(narrow<u8>(G["team_potion"].as_int(), 0, 3, "team potion") | (narrow<u8>(G["counter_a"].as_int(), 0, 31, "counter a") << 2));
		g.flags_d9 = static_cast<u8>((narrow<u8>(G["counter_b"].as_int(), 0, 31, "counter b")) | ((fl & 7) << 5));
		g.flags_da = static_cast<u8>((fl >> 3) & 0x7f);
		// ---- ITEM
		const auto& IT = f.data("ITEM");
		for (const auto& e : IT["def_state"].as_list())
		{
			const auto i = pos.item(e["item"].as_int());
			if (i >= old_def_state_pairs) fail("IndexError: list assignment index out of range");
			m.items.def_state[2 * i] = static_cast<u8>(narrow<u8>(e["quantity"].as_int(), 0, 255, "quantity"));
			m.items.def_state[2 * i + 1] = B(e["flag"]);
		}
		unique(IT["key_counts"], "item");
		for (const auto& e : IT["key_counts"].as_list())
		{
			// the six counters by definition name (the reference compares the build's display name with "KEY1"...; the
			// bind files carry the canonical key, so the comparison is on that)
			const auto& n = b.item[pos.item(e["item"].as_int())];
			bool found = false;
			for (int k = 0; k < 6; ++k)
			{
				if (n == canonical_field(key_item_names[k], Enc::latin1))
				{
					m.items.key_counts[k] = U(e["quantity"]);
					found = true;
					break;
				}
			}
			if (!found) fail("key counter definition is not a native key item");
		}
		m.items.def_count = narrow<u8>(static_cast<std::int64_t>(b.item.size()), 0, 160, "native definition count");
		m.items.tech_bits = U(IT["tech_bits"]);
		m.items.health_items = U(IT["health_items"]);
		m.items.energy_items = U(IT["energy_items"]);
		for (const auto& x : IT["instances"].as_list()) m.carried.push_back(pos.instance(x));
		for (const auto& w : IT["world"].as_list())
		{
			OldWorld ow;
			ow.slot = U(w["handle"]);
			ow.instance = pos.instance(w["instance"]);
			m.world.push_back(std::move(ow));
		}
		// ---- HERO
		for (const auto& h : f.data("HERO")["heroes"].as_list())
		{
			const auto owner = pos.stat(h["hero"].as_int());
			if (owner != m.heroes.size() + 1) fail("hero block order/identity differs from native table");
			const auto* costumes = b.T.cost(owner);
			if (!costumes) fail("hero has no costume table");
			bool costume_ok = false;
			for (const auto& [slot, costume] : *costumes) { (void)slot; costume_ok |= costume == h["costume"].as_int(); }
			if (!costume_ok) fail("costume does not belong to hero");
			OldHero hb;
			pos.hero_block(h, hb.attributes, hb.equipment);
			// convert.power_slots: a slot the file does not list is empty and usable (flag byte 1). Each listed slot carries
			// its whole flag byte (bit 0 = usable: get 0x4b83b0; the power handler 0x4fd4de refuses a slot without it). A
			// PWR record of a HERO 1.0 file (SPEC revision 2.2, written only by the 0.2.0 candidates) has no flag byte: it
			// loads as 1, the byte of every slot of the 82 corpus saves.
			for (auto& slot : hb.powers.power) slot.flags = power_usable;
			for (const auto& pw : h["powers"].as_list())
			{
				const auto k = pw["button"].as_int();
				if (k < 0 || k >= 4) fail("IndexError: list assignment index out of range");
				auto& slot = hb.powers.power[k];
				set_cstr(slot.name, pw["name"].as_text());
				slot.flags = pw.has("flags") ? narrow<u8>(pw["flags"].as_int(), 0, 255, "B") : power_usable;
			}
			hb.powers.pad54 = 0;
			hb.costume.costume = narrow<u8>(pos.costume_slot(h["costume"].as_int()), 0, 255, "B");
			u32 mask = 0;
			for (const auto& v : h["costumes_unlocked"].as_list())
			{
				bool belongs = false;
				for (const auto& [slot, costume] : *costumes) { (void)slot; belongs |= costume == v.as_int(); }
				if (!belongs) fail("unlocked costume does not belong to hero");
				const auto s = pos.costume_slot(v.as_int());
				if (s >= 10) fail("native costume slot outside 0..9");
				mask |= 1u << s;
			}
			hb.costume.costume_mask = mask;
			m.heroes.push_back(std::move(hb));
		}
		// ---- ULCK
		if (m.heroes.size() != b.hero_count) fail("hero block count differs from build");
		const auto& ul = f.data("ULCK")["unlocked"].as_list();
		if (ul.size() > old_unlocked_slots) fail("IndexError: list assignment index out of range");
		for (std::size_t i = 0; i < ul.size(); ++i) m.unlocked[i] = narrow<i16>(pos.stat(ul[i].as_int()), -32768, 32767, "h");
		m.unlocked_count = static_cast<u32>(ul.size());
		// ---- REST
		const auto& REST = f.data("REST");
		m.restore_flag = B(REST["bank_valid"]);
		m.restore.assign(old_restore_records, OldRestore{});
		for (auto& r : m.restore)
		{
			for (int k = 0; k < 4; ++k) r.attributes.scalars.trait_base[k] = 1;
			r.attributes.scalars.level = 1;
			r.attributes.scalars.ai_level = 1;
			r.equipment.tail.player_seat = -1;
			r.equipment.tail.ack_level = 1;
		}
		for (const auto& r : REST["records"].as_list())
		{
			const auto n = pos.stat(r["owner"].as_int());
			if (n >= old_restore_records) fail("IndexError: list assignment index out of range");
			pos.hero_block(r, m.restore[n].attributes, m.restore[n].equipment);
		}
		// ---- MISS
		const auto& MISS = f.data("MISS");
		const auto& snaps = MISS["snapshots"].as_list();
		if (MISS["stack_depth"].as_int() > old_side_frames || snaps.size() != 1 + MISS["stack_depth"].as_int()) fail("mission stack snapshots/depth not representable");
		m.mission = pos.snapshot(snaps[0]);
		for (std::size_t k = 1; k < snaps.size(); ++k)
		{
			const auto frame = pos.snapshot(snaps[k]);          // every snapshot is translated; frames beyond two are not kept
			if (k - 1 < old_side_frames) m.side_frames[k - 1] = frame;
		}
		m.side.pad_4d8 = 0;
		m.side.count = U(MISS["stack_depth"]);
		// ---- DNGR
		const auto& DN = f.data("DNGR");
		std::vector<const Value*> by_course(b.course.size(), nullptr);
		unique(DN["courses"], "course");
		for (const auto& c : DN["courses"].as_list())
		{
			const auto i = pos.course(c["course"].as_int());
			if (i < by_course.size()) by_course[i] = &c;
		}
		for (std::size_t i = 0; i < b.course.size(); ++i)
		{
			OldDrCourse d{};
			if (const auto* c = by_course[i])
			{
				const auto& cv = *c;
				d.flags = B(cv["flags"]);
				const auto& tiers = cv["tiers"].as_bytes();
				d.tier0 = static_cast<u8>(tiers.at(0));
				d.tier1 = static_cast<u8>(tiers.at(1));
				d.tier2 = static_cast<u8>(tiers.at(2));
				d.largest_hit = U(cv["largest_hit"]);
				d.least_damage = H(cv["least_damage"]);
				d.best_time = H(cv["best_time"]);
				d.best_combo = B(cv["best_combo"]);
				d.most_health_packs = B(cv["most_health_packs"]);
				d.most_energy_packs = B(cv["most_energy_packs"]);
				d.most_defeats = B(cv["most_defeats"]);
				auto hero = [&](const char* fld) -> u16
				{
					return cv[fld].truthy() ? narrow<u16>(pos.stat(cv[fld].as_int()), 0, 65535, "H") : 0;
				};
				d.largest_hit_hero = hero("largest_hit_hero");
				d.least_damage_hero = hero("least_damage_hero");
				d.most_health_packs_hero = hero("most_health_packs_hero");
				d.most_energy_packs_hero = hero("most_energy_packs_hero");
				d.most_defeats_hero = hero("most_defeats_hero");
			}
			m.courses.push_back(d);
		}
		if (m.courses.size() > old_dr_courses) fail("more courses than the engine holds");
		m.course_residue.assign(old_dr_courses - m.courses.size(), 0);
		unique(DN["grades"], "grade");
		for (const auto& gr : DN["grades"].as_list())
		{
			const auto i = pos.grade(gr["grade"].as_int());
			if (i >= old_dr_grades) fail("IndexError: list assignment index out of range");
			m.grades[i] = B(gr["value"]);
		}
		// ---- REVW
		const auto& RV = f.data("REVW");
		pos.review_words(RV["unlocked"], m.review.unlocked);
		pos.review_words(RV["seen"], m.review.seen);
		// ---- ZONE
		zone_records(pos, f.data("ZONE")["records"], m, notes, f.data("META")["origin"].as_int() == 1);
		// ---- profile
		profile_from_sections(pos, f, m.profile);
		// ---- script variables, town centres
		const auto& SV = f.data("SVAR");
		auto vars = [&](const Value& lst, OldScriptVar* out, u32 n)
		{
			const auto& l = lst.as_list();
			if (l.size() > n) fail("more script variables than the engine holds");
			for (std::size_t k = 0; k < l.size(); ++k)
			{
				set_cstr(out[k].name, l[k]["name"].as_text());
				const u32 v = U(l[k]["value"]);
				out[k].value_be[0] = static_cast<u8>(v >> 24);
				out[k].value_be[1] = static_cast<u8>(v >> 16);
				out[k].value_be[2] = static_cast<u8>(v >> 8);
				out[k].value_be[3] = static_cast<u8>(v);
			}
		};
		vars(SV["zone_vars"], m.zone_vars, old_script_zone_vars);
		vars(SV["game_vars"], m.game_vars, old_script_game_vars);
		u32 mask = 0;
		for (const auto& v : f.data("XTRC")["unlocked"].as_list())
		{
			const auto i = pos.xpoint(v.as_int());
			if (i >= 32) fail("'I' format requires 0 <= number <= 4294967295");
			mask |= 1u << i;
		}
		m.town_centres = mask;
		NativeLoadDemand demand;
		std::string demand_error;
		if (!native_load_demand(write_slot(m), demand, demand_error)) fail(demand_error);
		if (demand.instances > 256 || demand.attached_enhancements > 375 || demand.world_items > 150) fail("native instance/enhancement/world pool exhausted");
		if (tmpl) apply_template(m, *tmpl);
		return m;
	}

	std::vector<std::uint8_t> reverse_profile(const std::vector<std::uint8_t>& new_bytes, const TablesData& t)
	{
		const auto f = read_file(new_bytes.data(), new_bytes.size(), kind_profile);
		if (!t.build) fail("the reverse reader needs the build (--build)");
		preflight(f);
		const Pos pos{t, *t.build};
		OldProfile profile;
		profile_from_sections(pos, f, profile);
		const auto& meta = f.data("META");
		return write_profile_file(meta["description"].as_text(), profile);
	}
}
