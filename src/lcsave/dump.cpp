// Canonical dumps of both formats, by NAME, for semantic comparison (dump.py): the same dict shapes, the same
// rounding, so that to_json() of these values equals json.dumps(d, indent=1, sort_keys=True) of the reference.

#include "internal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace lcsave::detail
{
	namespace
	{
		Value I(std::int64_t v) { return Value::integer(v); }
		Value R3(double v) { return Value::real(py_round3(v)); }
		Value Tx(const std::string& s, Enc e = Enc::cp1252) { return Value::text(s, e); }
		Value L() { return Value::list(); }
		Value none() { return Value(); }
		std::string hexs(const std::string& b) { return hex(reinterpret_cast<const std::uint8_t*>(b.data()), b.size()); }

		std::vector<int> bits(const u32* words, int n)
		{
			std::vector<int> out;
			for (int k = 0; k < n; ++k)
			{
				if (words[k / 32] >> (k % 32) & 1) out.push_back(k);
			}
			return out;
		}

		Value sorted(Value l)
		{
			std::stable_sort(l.l.begin(), l.l.end(), [](const Value& a, const Value& b) { return py_less(a, b); });
			return l;
		}

		struct D
		{
			const TablesData& t;

			// dump._name: the key text of an id, lower-cased; a placeholder when the build does not name it (the
			// reference prints the first 12 hex digits of the key hash there; catalogue.lcc has no hash for a key the
			// build lacks, so the id is printed instead)
			Value name(unsigned dom, u32 id) const
			{
				if (!id) return none();
				if (const auto* n = t.name_of(id)) return Tx(py_lower(*n, Enc::latin1), Enc::latin1);
				char b[48];
				std::snprintf(b, sizeof b, "<%s:id 0x%x>", domain_tag(dom), id);
				return Tx(b);
			}

			Value era_name(const EraTables& era, const char* domain, unsigned dom, std::int64_t index) const
			{
				const std::vector<u32>* tb = nullptr;
				const std::string d = domain;
				if (d == "STAT")
				{
					if (index < 0 || index >= static_cast<std::int64_t>(era.STAT.size()) || !era.STAT[static_cast<std::size_t>(index)])
					{
						return Tx("<STAT position " + std::to_string(index) + " out of range>");
					}
					return name(dom, era.STAT[static_cast<std::size_t>(index)]);
				}
				if (d == "ITEM") tb = &era.ITEM;
				else if (d == "NAMA") tb = &era.NAMA;
				else if (d == "TRIT") tb = &era.TRIT;
				else if (d == "ENHC") tb = &era.ENHC;
				else if (d == "TALN_shared") tb = &era.TALN_shared;
				else fail("internal: era_name " + d);
				if (index < 0 || index >= static_cast<std::int64_t>(tb->size()))
				{
					return Tx("<" + d + " position " + std::to_string(index) + " out of range>");
				}
				return name(dom, (*tb)[static_cast<std::size_t>(index)]);
			}

			Value idn(std::int64_t v) const
			{
				if (!v) return none();
				const auto* e = t.catalogue.entry_of(static_cast<u32>(v));
				if (!e)
				{
					char b[48];
					std::snprintf(b, sizeof b, "<unknown id 0x%llx>", static_cast<unsigned long long>(v));
					return Tx(b);
				}
				return name(e->domain, e->id);
			}

			Value instance_old(const EraTables& era, const OldInstance& inst) const
			{
				Value d = Value::dict();
				d.set("definition", era_name(era, "ITEM", 2, inst.head.definition));
				d.set("location", I(inst.head.location));
				d.set("name_a", inst.head.name_a == 0xff ? none() : era_name(era, "NAMA", 4, inst.head.name_a));
				d.set("special", inst.head.special == 0xff ? none() : era_name(era, "TRIT", 6, inst.head.special));
				if (inst.head.name_b == 0xff) d.set("name_b", none());
				else
				{
					const u32 cat = inst.head.definition < era.ITEM_cat.size() ? era.ITEM_cat[inst.head.definition] : 0;
					const auto f = era.NAMB.find(cat);
					d.set("name_b", f != era.NAMB.end() && inst.head.name_b < f->second.size() ? name(5, f->second[inst.head.name_b]) : Tx("<out of range>"));
				}
				const char* const lists[3] = {"prefixes", "own", "suffixes"};
				for (int k = 0; k < 3; ++k)
				{
					Value l = L();
					for (const auto& e : inst.lists[k])
					{
						Value p = L();
						p.push(era_name(era, "ENHC", 3, e.record));
						p.push(I(e.value));
						l.push(std::move(p));
					}
					d.set(lists[k], std::move(l));
				}
				return d;
			}

			Value talents_old(const EraTables& era, std::int64_t si, const OldAttributes& a) const
			{
				Value out = L();
				const bool has_stat = si >= 0 && si < static_cast<std::int64_t>(era.STAT.size()) && era.STAT[static_cast<std::size_t>(si)];
				const std::vector<u32>* hero_tal = has_stat ? era.taln_hero(static_cast<u32>(si)) : nullptr;
				const std::size_t nh = hero_tal ? hero_tal->size() : 0;
				for (u32 k = 0; k < a.tail.count; ++k)
				{
					const auto& e = a.talents[k];
					const std::int64_t tid = e.id;
					Value nm;
					if (tid < 100) nm = era_name(era, "TALN_shared", 7, tid);
					else
					{
						const auto pos = tid % 100;
						if (tid / 100 - 1 == si && pos < static_cast<std::int64_t>(nh)) nm = name(7, (*hero_tal)[static_cast<std::size_t>(pos)]);
						else nm = Tx("<talent id " + std::to_string(tid) + ">");
					}
					out.push(Value::dict({{"talent", nm}, {"rank", I(e.rank_bits & 0x3f)}, {"pending", I(e.pending_cap_bits & 0x3f)},
						{"cap", I(e.pending_cap_bits >> 6)}, {"base", I(e.base_bits & 3)}, {"byte5", I(e.byte5)}}));
				}
				return sort_talents(std::move(out));
			}

			static Value sort_talents(Value l)
			{
				// sorted(key=lambda t: str(t['talent'])): str(None) is 'None'
				std::stable_sort(l.l.begin(), l.l.end(), [](const Value& a, const Value& b)
				{
					const auto key = [](const Value& v) { const auto& t = v["talent"]; return t.is_none() ? Value::text("None") : t; };
					return less_text(key(a), key(b));
				});
				return l;
			}

			Value hero_old(const EraTables& era, const EraTables& item_era, std::int64_t si, const OldAttributes& a, const OldEquipment& eq) const
			{
				Value traits = L();
				for (int k = 0; k < 4; ++k)
				{
					Value t4 = L();
					t4.push(I(a.scalars.trait_base[k]));
					t4.push(I(a.scalars.trait_pending[k]));
					t4.push(I(a.scalars.trait_bonus[k]));
					t4.push(I(a.scalars.trait_bonus2[k]));
					traits.push(std::move(t4));
				}
				Value equipped = L();
				for (const auto& i : eq.equipped) equipped.push(instance_old(item_era, i));
				Value vitals = L();
				vitals.push(I(eq.tail.vitals_valid & 1));
				vitals.push(R3(eq.tail.health));
				vitals.push(R3(eq.tail.energy));
				vitals.push(I(eq.tail.player_seat));
				Value counters = L();
				for (const int v : {eq.tail.ack_level, eq.tail.health_boosts, eq.tail.energy_boosts, eq.tail.levels_purchased,
					eq.tail.points_bought, eq.tail.respec_count, eq.tail.skirmish_boost, static_cast<u8>(eq.tail.flags & 0x1f)})
				{
					counters.push(I(v));
				}
				Value ai = L();
				ai.push(I(a.scalars.ai_level));
				ai.push(I(a.scalars.ai_attack));
				return Value::dict({{"hero", era_name(era, "STAT", 1, si)}, {"level", I(a.scalars.level)}, {"experience", I(eq.tail.experience)},
					{"traits", traits}, {"skill_points", I(a.scalars.skill_points)}, {"trait_points", I(a.scalars.trait_points)}, {"ai", ai},
					{"talents", talents_old(era, si, a)}, {"equipped", equipped}, {"vitals", vitals}, {"counters", counters}});
			}

			Value rounded3(const float* p, int n) const
			{
				Value l = L();
				for (int k = 0; k < n; ++k) l.push(R3(p[k]));
				return l;
			}

			Value snapshot_old(const EraTables& era, const OldSnapshot& s) const
			{
				Value objs = Value::dict();
				for (std::size_t i = 0; i < 300; ++i)
				{
					if (i < era.OBJV.size())
					{
						Value v = L();
						v.push(I(s.objectives[i] & 0x3f));
						v.push(I((s.objectives[i] >> 6) & 3));
						objs.set(name(10, era.OBJV[i]), std::move(v));
					}
				}
				Value seats = Value::dict();
				for (int i = 0; i < 4; ++i)
				{
					const auto n = cstr(s.seat_name[i]);
					if (n.empty()) continue;
					seats.set(Tx(std::to_string(i)), Value::dict({{"hero", Tx(py_lower(n, Enc::cp1252))},
						{"position", rounded3(s.seat_pose + i * 6, 3)}, {"orientation", rounded3(s.seat_pose + i * 6 + 3, 3)}}));
				}
				Value anchor = rounded3(s.anchor_pos, 3);
				for (const auto& v : rounded3(s.anchor_orient, 3).l) anchor.push(v);
				return Value::dict({{"zone", Tx(py_lower(cstr(s.zone), Enc::cp1252))}, {"seats", seats}, {"objectives", objs}, {"anchor", anchor}});
			}

			Value review_names(const u32* words) const
			{
				Value l = L();
				for (int c = 0; c < 5; ++c)
				{
					for (const int i : bits(words + c * 3, 90))
					{
						const auto it = t.reference.REVW.find({c, i});
						l.push(it == t.reference.REVW.end() ? none() : name(13, it->second));
					}
				}
				return sorted(std::move(l));
			}

			Value instance_new(const Value& r) const
			{
				Value d = Value::dict({{"definition", idn(r["definition"].as_int())}, {"location", r["location"]},
					{"name_a", idn(r["name_a"].as_int())}, {"name_b", idn(r["name_b"].as_int())}, {"special", idn(r["special"].as_int())}});
				for (const char* k : {"prefixes", "own", "suffixes"})
				{
					Value l = L();
					for (const auto& e : r[k].as_list())
					{
						Value p = L();
						p.push(idn(e["enhancement"].as_int()));
						p.push(e["value"]);
						l.push(std::move(p));
					}
					d.set(k, std::move(l));
				}
				return d;
			}

			Value hero_new(const Value& h, const char* owner_key) const
			{
				const auto& tr = h["traits"].as_list();
				Value traits = L();
				for (std::size_t k = 0; k < 4; ++k)
				{
					Value t4 = L();
					for (std::size_t j = 0; j < 4 && k * 4 + j < tr.size(); ++j) t4.push(tr[k * 4 + j]);
					traits.push(std::move(t4));
				}
				Value talents = L();
				for (const auto& tv : h["talents"].as_list())
				{
					talents.push(Value::dict({{"talent", idn(tv["talent"].as_int())}, {"rank", tv["rank"]}, {"pending", tv["pending"]},
						{"cap", tv["cap"]}, {"base", tv["base"]}, {"byte5", tv["byte5"]}}));
				}
				Value equipped = L();
				for (const auto& i : h["equipped"].as_list()) equipped.push(instance_new(i));
				Value vitals = L();
				vitals.push(h["vitals_valid"]);
				vitals.push(R3(h["health"].as_real()));
				vitals.push(R3(h["energy"].as_real()));
				vitals.push(h["player_seat"]);
				Value counters = L();
				for (const char* k : {"ack_level", "health_boosts", "energy_boosts", "levels_purchased", "points_bought", "respec_count",
					"skirmish_boost", "flags"})
				{
					counters.push(h[k]);
				}
				Value ai = L();
				ai.push(h["ai_level"]);
				ai.push(h["ai_attack"]);
				return Value::dict({{"hero", idn(h[owner_key].as_int())}, {"level", h["level"]}, {"experience", h["experience"]},
					{"traits", traits}, {"skill_points", h["skill_points"]}, {"trait_points", h["trait_points"]}, {"ai", ai},
					{"talents", sort_talents(std::move(talents))}, {"equipped", equipped}, {"vitals", vitals}, {"counters", counters}});
			}

			Value rounded_list(const Value& l) const
			{
				Value out = L();
				for (const auto& v : l.as_list()) out.push(R3(v.as_real()));
				return out;
			}

			Value snapshot_new(const Value& s) const
			{
				Value seats = Value::dict();
				for (const auto& x : s["seats"].as_list())
				{
					seats.set(Tx(std::to_string(x["seat"].as_int())), Value::dict({{"hero", idn(x["hero"].as_int())},
						{"position", rounded_list(x["position"])}, {"orientation", rounded_list(x["orientation"])}}));
				}
				Value objs = Value::dict();
				for (const auto& o : s["objectives"].as_list())
				{
					Value v = L();
					v.push(o["counter"]);
					v.push(o["flags"]);
					objs.set(idn(o["objective"].as_int()), std::move(v));
				}
				Value anchor = rounded_list(s["anchor_pos"]);
				for (const auto& v : rounded_list(s["anchor_orient"]).l) anchor.push(v);
				Value zone = idn(s["zone"].as_int());
				if (!zone.truthy()) zone = Tx("");
				return Value::dict({{"zone", zone}, {"seats", seats}, {"objectives", objs}, {"anchor", anchor}});
			}

			Value costume_slot(std::int64_t v) const
			{
				static const char* const slots[9] = {"", "astonishing", "aoa", "60s", "70s", "weaponx", "future", "winter", "civilian"};
				Value n = idn(v);
				if (n.is_none()) return n;
				const auto& s = n.s;
				const auto slash = s.find('/');
				if (slash == std::string::npos) return n;
				const auto attr = s.substr(slash + 1);
				if (attr == "skin") return I(0);
				const auto suffix = attr.size() > 5 ? attr.substr(5) : std::string();
				for (int k = 0; k < 9; ++k)
				{
					if (suffix == slots[k]) return I(k);
				}
				return n;
			}
		};
	}

	Value dump_old(const OldSlot& old, const TablesData& t, int start_cv, int items_cv, const bool native)
	{
		const D dd{t};
		const auto* era_t = t.by_cv(start_cv);
		const auto* era_i = t.by_cv(items_cv);
		if (!era_t || !era_i) fail("KeyError: era " + std::to_string(era_t ? items_cv : start_cv));
		const auto& ref = t.reference;
		const auto& g = old.game;
		const auto& it = old.items;
		Value d = Value::dict();
		d.set("description", Tx(cstr(reinterpret_cast<const char*>(old.description_raw), 0x80)));
		Value timer = L();
		for (const float v : {g.timer_threshold, g.timer_remaining, g.timer_after_add, g.timer_last_add}) timer.push(R3(v));
		timer.push(Tx(cstr(g.timer_script)));
		const auto recall = py_lower(cstr(g.recall_zone), Enc::cp1252);
		Value rec = L();
		rec.push(Tx(recall));
		rec.push(recall != "" && recall != "none" ? dd.rounded3(g.recall_pos, 3) : none());
		rec.push(R3(g.recall_cooldown));
		Value xtreme = L();
		xtreme.push(I(g.xtreme_charges));
		xtreme.push(I(g.xtreme_pips));
		Value counters = L();
		counters.push(I((g.flags_d8 >> 2) & 0x1f));
		counters.push(I(g.flags_d9 & 0x1f));
		Value trivia = L();
		for (const int b : bits(g.trivia, 250))
		{
			std::pair<int, int> position{b / 50 + 1, b % 50};
			const auto& trivia_ref = native && t.build ? t.build->T : ref;
			if (native && t.build)
			{
				const auto found = std::find_if(trivia_ref.trivia_bits.begin(), trivia_ref.trivia_bits.end(), [b](const auto& row) { return row.second == b; });
				if (found == trivia_ref.trivia_bits.end()) { trivia.push(none()); continue; }
				position = found->first;
			}
			const auto f = trivia_ref.TRIV.find(position);
			trivia.push(f == trivia_ref.TRIV.end() ? none() : dd.name(16, f->second));
		}
		d.set("game", Value::dict({{"timer", timer}, {"recall", rec}, {"xtreme", xtreme}, {"loading_name", Tx(cstr(g.loading_name))},
			{"act", I(g.act)}, {"value_608", I(g.value_608)}, {"difficulty", I(g.difficulty)}, {"play_time", R3(g.play_time)},
			{"potion", I(g.flags_d8 & 3)}, {"counters", counters}, {"flags", I(((g.flags_d9 >> 5) & 7) | ((g.flags_da & 0x7f) << 3))},
			{"trivia", sorted(std::move(trivia))}}));
		// items
		static const char* const keys[6] = {"key1", "key2", "key3", "keycard1", "keycard2", "keycard3"};
		Value kc = Value::dict();
		for (int k = 0; k < 6; ++k) kc.set(keys[k], I(it.key_counts[k]));
		Value ds = Value::dict();
		for (u32 i = 0; i < old_def_state_pairs; ++i)
		{
			if (it.def_state[2 * i] || it.def_state[2 * i + 1])
			{
				Value v = L();
				v.push(I(it.def_state[2 * i]));
				v.push(I(it.def_state[2 * i + 1]));
				ds.set(i < era_i->ITEM.size() ? dd.name(2, era_i->ITEM[i]) : Tx("<" + std::to_string(i) + ">"), std::move(v));
			}
		}
		Value carried = L();
		for (const auto& x : old.carried) carried.push(dd.instance_old(*era_i, x));
		Value world = Value::dict();
		for (const auto& w : old.world) world.set(Tx(std::to_string(w.slot)), dd.instance_old(*era_i, w.instance));
		d.set("items", Value::dict({{"tech_bits", I(it.tech_bits)}, {"health", I(it.health_items)}, {"energy", I(it.energy_items)},
			{"keys", kc}, {"def_state", ds}, {"carried", carried}, {"world", world}}));
		// heroes
		Value heroes = L();
		for (std::size_t n = 0; n < old.heroes.size(); ++n)
		{
			const auto& oh = old.heroes[n];
			Value h = dd.hero_old(*era_t, *era_i, static_cast<std::int64_t>(n + 1), oh.attributes, oh.equipment);
			Value powers = Value::dict();
			for (int k = 0; k < 4; ++k)
			{
				const auto& slot = oh.powers.power[k];
				const auto p = cstr(slot.name);
				if (!p.empty() || slot.flags != power_usable)
				{
					powers.set(Tx(std::to_string(k)), Value::dict({{"name", Tx(p)}, {"flags", I(slot.flags)}}));
				}
			}
			h.set("powers", std::move(powers));
			h.set("costume", I(oh.costume.costume));
			Value cu = L();
			for (int b = 0; b < 32; ++b) if (oh.costume.costume_mask >> b & 1) cu.push(I(b));
			h.set("costumes_unlocked", std::move(cu));
			heroes.push(std::move(h));
		}
		d.set("heroes", std::move(heroes));
		Value unlocked = L();
		for (u32 k = 0; k < old.unlocked_count; ++k) unlocked.push(dd.era_name(*era_t, "STAT", 1, old.unlocked[k]));
		d.set("unlocked", std::move(unlocked));
		Value records = Value::dict();
		for (std::size_t n = 0; n < old.restore.size(); ++n)
		{
			const auto& r = old.restore[n];
			if (r.attributes.tail.count || !r.equipment.equipped.empty() || r.equipment.tail.experience)
			{
				records.set(Tx(std::to_string(n)), dd.hero_old(*era_t, *era_i, static_cast<std::int64_t>(n), r.attributes, r.equipment));
			}
		}
		d.set("restore", Value::dict({{"valid", I(old.restore_flag)}, {"records", records}}));
		d.set("mission", dd.snapshot_old(*era_t, old.mission));
		Value side = L();
		for (u32 i = 0; i < old.side.count && i < old_side_frames; ++i) side.push(dd.snapshot_old(*era_t, old.side_frames[i]));
		d.set("side", std::move(side));
		Value courses = Value::dict();
		for (std::size_t i = 0; i < old.courses.size(); ++i)
		{
			const auto& c = old.courses[i];
			if (i >= ref.DRCS.size()) fail("IndexError: list index out of range");
			auto hero = [&](u16 v) { return v == 0 || v == 0xffff ? none() : dd.era_name(*era_t, "STAT", 1, v); };
			courses.set(dd.name(11, ref.DRCS[i]), Value::dict({{"flags", I(c.flags)}, {"tier0", I(c.tier0 & 1)}, {"tier1", I(c.tier1 & 1)},
				{"tier2", I(c.tier2 & 1)}, {"largest_hit", I(c.largest_hit)}, {"largest_hit_hero", hero(c.largest_hit_hero)},
				{"least_damage", I(c.least_damage)}, {"least_damage_hero", hero(c.least_damage_hero)}, {"best_time", I(c.best_time)},
				{"best_combo", I(c.best_combo)}, {"most_health_packs", I(c.most_health_packs)},
				{"most_health_packs_hero", hero(c.most_health_packs_hero)}, {"most_energy_packs", I(c.most_energy_packs)},
				{"most_energy_packs_hero", hero(c.most_energy_packs_hero)}, {"most_defeats", I(c.most_defeats)},
				{"most_defeats_hero", hero(c.most_defeats_hero)}}));
		}
		Value grades = Value::dict();
		for (std::size_t i = 0; i < old_dr_grades && i < ref.DRGR.size(); ++i) grades.set(dd.name(12, ref.DRGR[i]), I(old.grades[i]));
		d.set("danger_room", Value::dict({{"courses", courses}, {"grades", grades}}));
		d.set("review", Value::dict({{"unlocked", dd.review_names(old.review.unlocked)}, {"seen", dd.review_names(old.review.seen)}}));
		Value zones = L();
		for (const auto& z : old.zones)
		{
			const auto& strings = *old.zone_strings;
			Value states = L();
			auto st = [&](u32 idx, int kind, int ordinal)
			{
				const auto& s = z.states[idx];
				Value flags = L();
				flags.push(I(s.flags_a));
				flags.push(I(s.flags_b));
				Value pos = L();
				for (const auto p : s.position) pos.push(I(p));
				states.push(Value::dict({{"list", I(kind)}, {"ordinal", I(ordinal)}, {"definition", Tx(zone_string(strings, s.def_name))},
					{"instance", Tx(zone_string(strings, s.inst_name))}, {"flags", flags}, {"position", pos},
					{"payload", Tx(hex(s.payload, 8))}}));
			};
			for (u32 k = 0; k < old_zone_ordinals; ++k) if (z.tail.ordinal_map[k]) st(z.tail.ordinal_map[k], 1, static_cast<int>(k + 1));
			for (u32 k = 0; k < z.tail.dynamic_count; ++k) st(z.tail.dynamic[k], 2, 0);
			for (u32 k = 0; k < z.tail.smart_count; ++k) st(z.tail.smart[k], 3, 0);
			Value conv = L();
			for (const auto w : z.tail.conversation) conv.push(I(w));
			zones.push(Value::dict({{"zone", Tx(py_lower(zone_string(strings, z.name_handle), Enc::cp1252))}, {"time_stamp", R3(z.tail.time_stamp)},
				{"flag", I(z.tail.flag)}, {"conversation", conv}, {"fog", Tx(hex(z.tail.fog, old_fog_bytes))}, {"states", states}}));
		}
		d.set("zones", std::move(zones));
		auto vars = [&](const OldScriptVar* v, u32 n)
		{
			Value out = Value::dict();
			for (u32 k = 0; k < n; ++k)
			{
				const auto name = cstr(v[k].name);
				if (!name.empty()) out.set(Tx(name), I(script_var_value(v[k])));
			}
			return out;
		};
		d.set("script_vars", Value::dict({{"zone", vars(old.zone_vars, old_script_zone_vars)}, {"game", vars(old.game_vars, old_script_game_vars)}}));
		Value tc = L();
		for (const int i : bits(&old.town_centres, 32))
		{
			tc.push(static_cast<std::size_t>(i) < ref.XPNT.size() ? dd.name(14, ref.XPNT[static_cast<std::size_t>(i)]) : Tx("<" + std::to_string(i) + ">"));
		}
		d.set("town_centres", std::move(tc));
		const auto& p = old.profile;
		Value scores = L();
		for (u32 k = 0; k < p.tail.score_count; ++k)
		{
			const auto& s = p.high_scores[k];
			Value pa = L(), pb = L();
			for (u32 j = 0; j < s.count_a && j < 6; ++j) pa.push(dd.era_name(*era_t, "STAT", 1, s.party_a[j]));
			for (u32 j = 0; j < s.count_b && j < 6; ++j) pb.push(dd.era_name(*era_t, "STAT", 1, s.party_b[j]));
			scores.push(Value::dict({{"course", s.course < ref.DRCS.size() ? dd.name(11, ref.DRCS[s.course]) : none()}, {"party_a", pa},
				{"party_b", pb}, {"score", I(s.score)}}));
		}
		Value chars = L();
		for (const int i : bits(p.tail.char_unlock, 297)) chars.push(dd.era_name(*era_t, "STAT", 1, i));
		Value review = L();
		for (int c = 0; c < 5; ++c)
		{
			for (const int i : bits(p.tail.review + c * 3, 90))
			{
				const auto f = ref.REVW.find({c, i});
				review.push(f == ref.REVW.end() ? none() : dd.name(13, f->second));
			}
		}
		d.set("profile", Value::dict({{"options", I(p.head.options[0] | (p.head.options[1] << 8) | (p.head.options[2] << 16))},
			{"name", Tx(cstr(p.head.player_name))}, {"scores", scores}, {"characters", chars}, {"review", sorted(std::move(review))},
			{"highest_level", I(p.tail.highest_level)}, {"flags", I(p.tail.flags_229)}}));
		return d;
	}

	Value dump_new(const std::vector<std::uint8_t>& buf, const TablesData& t)
	{
		const D dd{t};
		const auto f = read_file(buf.data(), buf.size(), kind_campaign);
		const auto& G = f.data("GAME");
		const auto& IT = f.data("ITEM");
		Value d = Value::dict();
		d.set("description", f.data("META")["description"]);
		Value timer = L();
		for (const char* k : {"timer_threshold", "timer_remaining", "timer_after_add", "timer_last_add"}) timer.push(R3(G[k].as_real()));
		timer.push(G["timer_script"]);
		Value rec = L();
		Value rz = dd.idn(G["recall_zone"].as_int());
		rec.push(rz.truthy() ? rz : Tx("none"));
		rec.push(G["recall_zone"].truthy() ? dd.rounded_list(G["recall_pos"]) : none());
		rec.push(R3(G["recall_cooldown"].as_real()));
		Value xtreme = L();
		xtreme.push(G["xtreme_charges"]);
		xtreme.push(G["xtreme_pips"]);
		Value counters = L();
		counters.push(G["counter_a"]);
		counters.push(G["counter_b"]);
		Value trivia = L();
		for (const auto& v : G["trivia"].as_list()) trivia.push(dd.idn(v.as_int()));
		d.set("game", Value::dict({{"timer", timer}, {"recall", rec}, {"xtreme", xtreme}, {"loading_name", G["loading_name"]}, {"act", G["act"]},
			{"value_608", G["value_608"]}, {"difficulty", G["difficulty"]}, {"play_time", R3(G["play_time"].as_real())},
			{"potion", G["team_potion"]}, {"counters", counters}, {"flags", G["flags"]}, {"trivia", sorted(std::move(trivia))}}));
		Value keys = Value::dict();
		for (const auto& e : IT["key_counts"].as_list()) keys.set(dd.idn(e["item"].as_int()), e["quantity"]);
		Value ds = Value::dict();
		for (const auto& e : IT["def_state"].as_list())
		{
			Value v = L();
			v.push(e["quantity"]);
			v.push(e["flag"]);
			ds.set(dd.idn(e["item"].as_int()), std::move(v));
		}
		Value carried = L();
		for (const auto& x : IT["instances"].as_list()) carried.push(dd.instance_new(x));
		Value world = Value::dict();
		for (const auto& w : IT["world"].as_list()) world.set(Tx(std::to_string(w["handle"].as_int())), dd.instance_new(w["instance"]));
		d.set("items", Value::dict({{"tech_bits", IT["tech_bits"]}, {"health", IT["health_items"]}, {"energy", IT["energy_items"]},
			{"keys", keys}, {"def_state", ds}, {"carried", carried}, {"world", world}}));
		Value heroes = L();
		for (const auto& h : f.data("HERO")["heroes"].as_list())
		{
			Value hd = dd.hero_new(h, "hero");
			Value powers = Value::dict();
			for (const auto& p : h["powers"].as_list())
			{
				const auto flags = p.has("flags") ? p["flags"].as_int() : power_usable;      // a HERO 1.0 record has none
				if (p["name"].as_text().empty() && flags == power_usable) continue;
				powers.set(Tx(std::to_string(p["button"].as_int())), Value::dict({{"name", p["name"]}, {"flags", I(flags)}}));
			}
			hd.set("powers", std::move(powers));
			hd.set("costume", dd.costume_slot(h["costume"].as_int()));
			Value cu = L();
			for (const auto& v : h["costumes_unlocked"].as_list()) cu.push(dd.costume_slot(v.as_int()));
			hd.set("costumes_unlocked", sorted(std::move(cu)));
			heroes.push(std::move(hd));
		}
		d.set("heroes", std::move(heroes));
		Value unlocked = L();
		for (const auto& v : f.data("ULCK")["unlocked"].as_list()) unlocked.push(dd.idn(v.as_int()));
		d.set("unlocked", std::move(unlocked));
		const auto& REST = f.data("REST");
		Value records = Value::dict();
		for (const auto& r : REST["records"].as_list())
		{
			Value key = dd.idn(r["owner"].as_int());
			if (t.build)
			{
				std::int64_t index = -1;
				for (std::size_t i = 0; i < t.build->stat.size(); ++i)
				{
					if (key.type == Value::Type::text && !t.build->stat[i].empty() && py_lower(unescape_key(t.build->stat[i]), Enc::latin1) == key.s)
					{
						index = static_cast<std::int64_t>(i);
						break;
					}
				}
				if (index < 0) fail("ValueError: restore record owner is not in the build's stats list");
				key = Tx(std::to_string(index));
			}
			records.set(key, dd.hero_new(r, "owner"));
		}
		d.set("restore", Value::dict({{"valid", REST["bank_valid"]}, {"records", records}}));
		const auto& snaps = f.data("MISS")["snapshots"].as_list();
		if (snaps.empty()) fail("IndexError: list index out of range");
		d.set("mission", dd.snapshot_new(snaps[0]));
		Value side = L();
		for (std::size_t k = 1; k < snaps.size(); ++k) side.push(dd.snapshot_new(snaps[k]));
		d.set("side", std::move(side));
		const auto& DN = f.data("DNGR");
		Value courses = Value::dict();
		for (const auto& c : DN["courses"].as_list())
		{
			const auto& tiers = c["tiers"].as_bytes();
			Value cd = Value::dict({{"flags", c["flags"]}, {"tier0", I(static_cast<u8>(tiers.at(0)))}, {"tier1", I(static_cast<u8>(tiers.at(1)))},
				{"tier2", I(static_cast<u8>(tiers.at(2)))}, {"largest_hit", c["largest_hit"]}, {"least_damage", c["least_damage"]},
				{"best_time", c["best_time"]}, {"best_combo", c["best_combo"]}, {"most_health_packs", c["most_health_packs"]},
				{"most_energy_packs", c["most_energy_packs"]}, {"most_defeats", c["most_defeats"]}});
			for (const char* fld : {"largest_hit_hero", "least_damage_hero", "most_health_packs_hero", "most_energy_packs_hero", "most_defeats_hero"})
			{
				cd.set(fld, dd.idn(c[fld].as_int()));
			}
			courses.set(dd.idn(c["course"].as_int()), std::move(cd));
		}
		Value grades = Value::dict();
		for (const auto& g : DN["grades"].as_list()) grades.set(dd.idn(g["grade"].as_int()), g["value"]);
		d.set("danger_room", Value::dict({{"courses", courses}, {"grades", grades}}));
		const auto& RV = f.data("REVW");
		Value review = Value::dict();
		for (const char* k : {"unlocked", "seen"})
		{
			Value l = L();
			for (const auto& v : RV[k].as_list()) l.push(dd.idn(v.as_int()));
			review.set(k, sorted(std::move(l)));
		}
		d.set("review", std::move(review));
		Value zones = L();
		for (const auto& z : f.data("ZONE")["records"].as_list())
		{
			const auto& names = z["names"].as_list();
			auto nm = [&](const Value& i) -> Value
			{
				const auto k = i.as_int();
				if (k < 0 || k >= static_cast<std::int64_t>(names.size())) fail("IndexError: list index out of range");
				return names[static_cast<std::size_t>(k)];
			};
			Value states = L();
			for (const auto& s : z["states"].as_list())
			{
				const auto& p = s["payload"].as_bytes();
				std::string payload;
				if (s["payload_kind"].as_int() == 2)
				{
					if (p.size() < 8) fail("unpack requires a buffer of 8 bytes");
					u32 slot = 0;
					i32 qty = 0;
					std::memcpy(&slot, p.data(), 4);
					std::memcpy(&qty, p.data() + 4, 4);
					if (slot > 255) fail("bytes must be in range(0, 256)");
					if (qty < -32768 || qty > 32767) fail("'h' format requires -32768 <= number <= 32767");
					std::string raw(1, static_cast<char>(slot));
					const i16 q = static_cast<i16>(qty);
					raw.append(reinterpret_cast<const char*>(&q), 2);
					raw += p.substr(8, 5);
					payload = hexs(raw);
				}
				else
				{
					payload = hexs(p);
				}
				Value flags = L();
				flags.push(s["flags_a"]);
				flags.push(s["flags_b"]);
				Value pos = L();
				for (const auto& x : s["position"].as_list()) pos.push(I(static_cast<std::int64_t>(x.as_real())));
				states.push(Value::dict({{"list", s["list"]}, {"ordinal", s["ordinal"]}, {"definition", nm(s["def_name"])},
					{"instance", nm(s["inst_name"])}, {"flags", flags}, {"position", pos}, {"payload", Tx(payload)}}));
			}
			zones.push(Value::dict({{"zone", dd.idn(z["zone"].as_int())}, {"time_stamp", R3(z["time_stamp"].as_real())}, {"flag", z["flag"]},
				{"conversation", z["conversation"]}, {"fog", Tx(hexs(z["fog"]["data"].as_bytes()))}, {"states", states}}));
		}
		d.set("zones", std::move(zones));
		const auto& SV = f.data("SVAR");
		Value sv = Value::dict();
		for (const char* k : {"zone", "game"})
		{
			Value m = Value::dict();
			for (const auto& v : SV[(std::string(k) + "_vars").c_str()].as_list()) m.set(v["name"], v["value"]);
			sv.set(k, std::move(m));
		}
		d.set("script_vars", std::move(sv));
		Value tc = L();
		for (const auto& v : f.data("XTRC")["unlocked"].as_list()) tc.push(dd.idn(v.as_int()));
		d.set("town_centres", std::move(tc));
		const auto& O = f.data("PRFO");
		const auto& P = f.data("PRFP");
		const auto& Sc = f.data("PRFS");
		Value scores = L();
		for (const auto& s : Sc["scores"].as_list())
		{
			Value pa = L(), pb = L();
			for (const auto& v : s["party_a"].as_list()) pa.push(dd.idn(v.as_int()));
			for (const auto& v : s["party_b"].as_list()) pb.push(dd.idn(v.as_int()));
			scores.push(Value::dict({{"course", dd.idn(s["course"].as_int())}, {"party_a", pa}, {"party_b", pb}, {"score", s["score"]}}));
		}
		Value chars = L();
		for (const auto& v : P["characters"].as_list()) chars.push(dd.idn(v.as_int()));
		Value prv = L();
		for (const auto& v : P["review"].as_list()) prv.push(dd.idn(v.as_int()));
		d.set("profile", Value::dict({{"options", O["options"]}, {"name", O["player_name"]}, {"scores", scores}, {"characters", chars},
			{"review", sorted(std::move(prv))}, {"highest_level", P["highest_level"]}, {"flags", P["flags"]}}));
		const auto lg = f.sections.find("LGCY");
		if (lg != f.sections.end() && !lg->second.data.is_none())
		{
			Value l = Value::dict();
			for (const auto& item : lg->second.data.d)
			{
				l.set(item.key, item.value.type == Value::Type::bytes ? Tx(hexs(item.value.s)) : item.value);
			}
			d.set("lgcy", std::move(l));
		}
		else
		{
			d.set("lgcy", none());
		}
		return d;
	}
}
