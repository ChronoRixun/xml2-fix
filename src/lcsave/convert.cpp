// The one-time converter (SPEC-new-save 5, revision 2.2) - old slot file -> LC campaign file - and the saver's
// direction (the engine's stream re-expressed with the running build's numbering), as convert.py: fail closed on
// anything inconsistent, era detection per domain (legacy.py), the LGCY record, the KEYS documentation section.
// Messages are the reference's, character for character, so that refusals and notes agree.

#include "internal.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

namespace lcsave::detail
{
	namespace
	{
		constexpr unsigned STAT = 1, ITEM = 2, ENHC = 3, NAMA = 4, NAMB = 5, TRIT = 6, TALN = 7, COST = 8, OBJV = 10, DRCS = 11,
			DRGR = 12, REVW = 13, XPNT = 14, ZONE = 15, TRIV = 16;
		constexpr int trivia_per_act = 50;
		constexpr int review_per_category = 90;
		const char* const key_item_names[6] = {"KEY1", "KEY2", "KEY3", "KEYCARD1", "KEYCARD2", "KEYCARD3"};
		const u32 item_pools[2] = {375, 512};

		Value I(std::int64_t v) { return Value::integer(v); }
		Value R(double v) { return Value::real(v); }
		Value Tx(const std::string& s, Enc e = Enc::cp1252) { return Value::text(s, e); }
		Value L() { return Value::list(); }
		template <typename T> Value reals(const T* p, std::size_t n)
		{
			Value v = L();
			for (std::size_t k = 0; k < n; ++k) v.push(R(static_cast<double>(p[k])));
			return v;
		}
		std::string str(std::int64_t v) { return std::to_string(v); }
		std::string repr(const std::string& s, Enc e = Enc::cp1252) { return py_repr(s, e); }

		// the era tables by the reference's domain names
		const std::vector<u32>* table(const EraTables& era, const char* domain)
		{
			const std::string d = domain;
			if (d == "ITEM") return &era.ITEM;
			if (d == "ENHC") return &era.ENHC;
			if (d == "NAMA") return &era.NAMA;
			if (d == "TRIT") return &era.TRIT;
			if (d == "TALN_shared") return &era.TALN_shared;
			if (d == "OBJV") return &era.OBJV;
			if (d == "DRCS") return &era.DRCS;
			if (d == "DRGR") return &era.DRGR;
			if (d == "XPNT") return &era.XPNT;
			fail("internal: table " + d);
		}

		struct Conv
		{
			const TablesData& t;
			ConvertState& res;
			const EraTables& ref;          // the era model's reference (the build's own tables in the saver's direction)

			u32 id_of(unsigned dom, const Hash& h) const { return t.catalogue.lookup(dom, h); }

			// convert._id: the era tables carry catalogue ids already (legacy.lcm); 0 is None
			u32 id(unsigned, u32 v, const std::string& what) const
			{
				if (!v)
				{
					fail(what + ": no catalogue entry (key hash None); the save names something no builder release ever shipped");
				}
				return v;
			}

			u32 era_list(const EraTables& era, const char* domain, std::int64_t index, const std::string& what) const
			{
				if (std::strcmp(domain, "STAT") == 0)
				{
					if (index < 0 || index >= static_cast<std::int64_t>(era.STAT.size()) || !era.STAT[static_cast<std::size_t>(index)])
					{
						fail(what + ": position " + str(index) + " is outside the STAT table of builder " + era.release + " (" +
							str(static_cast<std::int64_t>(era.STAT.size())) + " entries)");
					}
					return era.STAT[static_cast<std::size_t>(index)];
				}
				const auto& tb = *table(era, domain);
				if (index < 0 || index >= static_cast<std::int64_t>(tb.size()))
				{
					fail(what + ": position " + str(index) + " is outside the " + domain + " table of builder " + era.release + " (" +
						str(static_cast<std::int64_t>(tb.size())) + " entries)");
				}
				return tb[static_cast<std::size_t>(index)];
			}

			void cross_check_instance(const EraTables& era, const OldInstance& inst, const std::string& what) const
			{
				for (const auto& e : inst.lists[0])
				{
					if (!(era.prefix[0] <= e.record && e.record < era.prefix[1]))
					{
						fail(what + ": prefix enhancement record " + str(e.record) + " is not a prefix in builder " + era.release);
					}
				}
				for (const auto& e : inst.lists[2])
				{
					if (!(era.suffix[0] <= e.record && e.record < era.suffix[1]))
					{
						fail(what + ": suffix enhancement record " + str(e.record) + " is not a suffix in builder " + era.release);
					}
				}
				// the own-record sets are enhancement ids (legacy.lcm IOWN / TOWN): a record is allowed when its id is
				std::set<u32> allowed;
				if (inst.head.definition < era.ITEM_own.size())
				{
					allowed.insert(era.ITEM_own[inst.head.definition].begin(), era.ITEM_own[inst.head.definition].end());
				}
				if (inst.head.special != 0xff && inst.head.special < era.TRIT_own.size())
				{
					allowed.insert(era.TRIT_own[inst.head.special].begin(), era.TRIT_own[inst.head.special].end());
				}
				allowed.erase(0);
				for (const auto& e : inst.lists[1])
				{
					const u32 rid = e.record < era.ENHC.size() ? era.ENHC[e.record] : 0;
					if (!allowed.count(rid))
					{
						fail(what + ": own enhancement record " + str(e.record) + " does not belong to definition " + str(inst.head.definition) +
							" in builder " + era.release + " (the item table of another release?)");
					}
				}
			}

			Value instance_keys(const EraTables& era, const OldInstance& inst, const std::string& what) const
			{
				const u32 d = inst.head.definition;
				const auto item_hash = era_list(era, "ITEM", d, what + ": definition index");
				Value rec = Value::dict();
				rec.set("definition", I(id(ITEM, item_hash, what + ": item")));
				const u8 vals[3] = {inst.head.name_a, inst.head.name_b, inst.head.special};
				const char* const names[3] = {"name_a", "name_b", "special"};
				for (int k = 0; k < 3; ++k)
				{
					const u8 v = vals[k];
					if (v == 0xff)
					{
						rec.set(names[k], I(0));
					}
					else if (k == 0)
					{
						const auto h = era_list(era, "NAMA", v, what + ": item name A");
						rec.set(names[k], I(id(NAMA, h, what)));
					}
					else if (k == 1)
					{
						if (d >= era.ITEM_cat.size()) fail("IndexError: list index out of range");
						static const char* const categories[4] = {"", "belt", "gloves", "armor"};
						const u32 cat = era.ITEM_cat[d];
						const auto f = era.NAMB.find(cat);
						const std::size_t n = f != era.NAMB.end() ? f->second.size() : 0;
						if (v >= n)
						{
							fail(what + ": item name B position " + str(v) + " outside the " + repr(cat < 4 ? categories[cat] : "", Enc::latin1) + " list");
						}
						rec.set(names[k], I(id(NAMB, f->second[v], what)));
					}
					else
					{
						const auto h = era_list(era, "TRIT", v, what + ": special item");
						rec.set(names[k], I(id(TRIT, h, what)));
					}
				}
				rec.set("location", I(inst.head.location));
				const char* const lists[3] = {"prefixes", "own", "suffixes"};
				for (int k = 0; k < 3; ++k)
				{
					Value l = L();
					for (const auto& e : inst.lists[k])
					{
						const u32 r = e.record;
						if (r & 0x8000)
						{
							fail(what + ": negative enhancement record " + str(r));
						}
						const auto h = era_list(era, "ENHC", r, what + ": " + lists[k] + " enhancement record");
						l.push(Value::dict({{"enhancement", I(id(ENHC, h, what))}, {"value", I(e.value)}}));
					}
					rec.set(lists[k], std::move(l));
				}
				cross_check_instance(era, inst, what);
				return rec;
			}

			Value hero_record(const EraTables& era, const EraTables& item_era, std::int64_t stats_index, const OldAttributes& a, const OldEquipment& eq,
				const std::string& what) const
			{
				const auto stat_hash = era_list(era, "STAT", stats_index, what + ": stats index");
				Value rec = Value::dict();
				rec.set("hero", I(id(STAT, stat_hash, what)));
				Value traits = L();
				for (int k = 0; k < 4; ++k)
				{
					traits.push(I(a.scalars.trait_base[k]));
					traits.push(I(a.scalars.trait_pending[k]));
					traits.push(I(a.scalars.trait_bonus[k]));
					traits.push(I(a.scalars.trait_bonus2[k]));
				}
				rec.set("traits", std::move(traits));
				rec.set("skill_points", I(a.scalars.skill_points));
				rec.set("trait_points", I(a.scalars.trait_points));
				rec.set("level", I(a.scalars.level));
				rec.set("ai_level", I(a.scalars.ai_level));
				rec.set("ai_attack", I(a.scalars.ai_attack));
				Value talents = L();
				const auto* hero_tal = era.taln_hero(static_cast<u32>(stats_index));
				const std::size_t nhero = hero_tal ? hero_tal->size() : 0;
				for (u32 k = 0; k < a.tail.count; ++k)
				{
					const auto& e = a.talents[k];
					const std::int64_t tid = e.id;
					u32 h = 0;
					if (tid < 100)
					{
						const auto& tb = era.TALN_shared;
						if (tid >= static_cast<std::int64_t>(tb.size()))
						{
							++res.dropped.talents;
							res.notes.push_back(what + ": shared talent id " + str(tid) + " beyond the " + str(static_cast<std::int64_t>(tb.size())) +
								"-entry table of builder " + era.release + ": dropped");
							continue;
						}
						h = tb[static_cast<std::size_t>(tid)];
					}
					else
					{
						const std::int64_t owner = tid / 100 - 1;
						const std::int64_t pos = tid % 100;
						if (owner != stats_index)
						{
							++res.dropped.talents;
							res.notes.push_back(what + ": talent id " + str(tid) + " belongs to stats index " + str(owner) + ", not this hero: dropped");
							continue;
						}
						if (pos >= static_cast<std::int64_t>(nhero))
						{
							++res.dropped.talents;
							res.notes.push_back(what + ": talent position " + str(pos) + " beyond the hero talent file (" +
								str(static_cast<std::int64_t>(nhero)) + "): dropped");
							continue;
						}
						h = (*hero_tal)[static_cast<std::size_t>(pos)];
					}
					talents.push(Value::dict({{"talent", I(id(TALN, h, what))}, {"rank", I(e.rank_bits & 0x3f)},
						{"pending", I(e.pending_cap_bits & 0x3f)}, {"cap", I(e.pending_cap_bits >> 6)}, {"base", I(e.base_bits & 3)},
						{"byte5", I(e.byte5)}}));
				}
				rec.set("talents", std::move(talents));
				Value equipped = L();
				for (std::size_t k = 0; k < eq.equipped.size(); ++k)
				{
					equipped.push(instance_keys(item_era, eq.equipped[k], what + ": equipped item " + str(static_cast<std::int64_t>(k))));
				}
				rec.set("equipped", std::move(equipped));
				const auto& tl = eq.tail;
				rec.set("vitals_valid", I(tl.vitals_valid & 1));
				rec.set("health", R(tl.health));
				rec.set("energy", R(tl.energy));
				rec.set("player_seat", I(tl.player_seat));
				rec.set("experience", I(tl.experience));
				rec.set("ack_level", I(tl.ack_level));
				rec.set("health_boosts", I(tl.health_boosts));
				rec.set("energy_boosts", I(tl.energy_boosts));
				rec.set("levels_purchased", I(tl.levels_purchased));
				rec.set("points_bought", I(tl.points_bought));
				rec.set("respec_count", I(tl.respec_count));
				rec.set("skirmish_boost", I(tl.skirmish_boost));
				rec.set("flags", I(tl.flags & 0x1f));
				if (!(-1 <= tl.player_seat && tl.player_seat <= 3))
				{
					fail(what + ": player seat " + str(tl.player_seat) + " is not -1..3");
				}
				return rec;
			}

			Value hero_full(const EraTables& era, const EraTables& item_era, std::int64_t stats_index, const OldHero& h, const std::string& what) const
			{
				Value rec = hero_record(era, item_era, stats_index, h.attributes, h.equipment, what);
				Value powers = L();
				// SPEC 2.6 (HERO minor 1): button, name and the slot's whole flag byte (bit 0 = usable); a slot is listed when
				// it has a name or its byte is not 1 (an unlisted slot reads back as empty and usable)
				for (int k = 0; k < 4; ++k)
				{
					const auto& slot = h.powers.power[k];
					const auto n = cstr(slot.name);
					if (!n.empty() || slot.flags != power_usable)
					{
						powers.push(Value::dict({{"button", I(k)}, {"name", Tx(n)}, {"flags", I(slot.flags)}}));
					}
				}
				rec.set("powers", std::move(powers));
				const auto* cost = era.cost(static_cast<u32>(stats_index));
				auto slot_hash = [&](u32 slot) -> const u32*
				{
					if (!cost) return nullptr;
					const auto f = cost->find(slot);
					return f == cost->end() ? nullptr : &f->second;
				};
				const u32 slot = h.costume.costume;
				const auto* sh = slot_hash(slot);
				if (!sh)
				{
					fail(what + ": costume slot " + str(slot) + " is not a slot the engine knows (0..8)");
				}
				rec.set("costume", I(id(COST, *sh, what)));
				Value unlocked = L();
				const u32 mask = h.costume.costume_mask;
				for (int bit = 0; bit < 32; ++bit)
				{
					if (mask >> bit & 1)
					{
						if (const auto* bh = slot_hash(static_cast<u32>(bit)))
						{
							unlocked.push(I(id(COST, *bh, what)));
						}
						else
						{
							++res.dropped.other;
							res.notes.push_back(what + ": costume mask bit " + str(bit) + " has no slot: dropped");
						}
					}
				}
				rec.set("costumes_unlocked", std::move(unlocked));
				return rec;
			}

			Value snapshot(const EraTables& era, const OldSnapshot& s, const std::string& what) const
			{
				const auto zone = cstr(s.zone);
				Value rec = Value::dict();
				if (!zone.empty())
				{
					const auto zh = key_hash1(zone, Enc::cp1252);
					rec.set("zone", I(id(ZONE, id_of(ZONE, zh), what + ": zone " + repr(zone))));
				}
				else
				{
					rec.set("zone", I(0));
				}
				Value seats = L();
				for (int k = 0; k < 4; ++k)
				{
					const auto n = cstr(s.seat_name[k]);
					if (n.empty()) continue;
					const u32 hero = id_of(STAT, key_hash1(n, Enc::cp1252));
					if (!hero)
					{
						fail(what + ": seat " + str(k) + " names a character no builder release ever had (" + repr(n) + ")");
					}
					seats.push(Value::dict({{"seat", I(k)}, {"hero", I(hero)}, {"position", reals(s.seat_pose + k * 6, 3)},
						{"orientation", reals(s.seat_pose + k * 6 + 3, 3)}}));
				}
				rec.set("seats", std::move(seats));
				Value objs = L();
				for (std::size_t k = 0; k < 300; ++k)
				{
					const u8 st = s.objectives[k];
					if (k < era.OBJV.size())
					{
						objs.push(Value::dict({{"objective", I(id(OBJV, era.OBJV[k], what))}, {"counter", I(st & 0x3f)}, {"flags", I((st >> 6) & 3)}}));
					}
					else if (st)
					{
						fail(what + ": objective index " + str(static_cast<std::int64_t>(k)) + " has state but builder " + era.release +
							" registers only " + str(static_cast<std::int64_t>(era.OBJV.size())));
					}
				}
				rec.set("objectives", std::move(objs));
				rec.set("anchor_pos", reals(s.anchor_pos, 3));
				rec.set("anchor_orient", reals(s.anchor_orient, 3));
				const bool anchor = s.anchor_pos[0] != 0 || s.anchor_pos[1] != 0 || s.anchor_pos[2] != 0;
				rec.set("flags", I(1 | (anchor ? 2 : 0)));
				return rec;
			}
		};

		std::vector<int> bits(const u32* words, int n)
		{
			std::vector<int> out;
			for (int k = 0; k < n; ++k)
			{
				if (words[k / 32] >> (k % 32) & 1) out.push_back(k);
			}
			return out;
		}

		// legacy.zone_record_eras: the eras whose numbered list has these names at every used ordinal
		std::vector<const EraTables*> zone_record_eras(const std::vector<std::pair<u32, std::pair<Hash, Hash>>>& names_by_ordinal,
			u32 zone_id, const std::vector<const EraTables*>& eras, const std::vector<std::pair<Hash, Hash>>& ref)
		{
			std::vector<const EraTables*> out;
			for (const auto* era : eras)
			{
				const auto* delta = era->zone_map(zone_id);
				bool ok = true;
				for (const auto& [k, names] : names_by_ordinal)
				{
					u32 ref_k = k;
					if (delta)
					{
						ref_k = k >= 1 && k <= delta->map.size() ? delta->map[k - 1] : 0;
					}
					if (ref_k == 0 || ref_k > ref.size() || ref[ref_k - 1] != names)
					{
						ok = false;
						break;
					}
				}
				if (ok) out.push_back(era);
			}
			return out;
		}

		// convert._zone_record: one old zone record -> ZREC, or nothing (dropped, the reason noted)
		bool zone_record(const Conv& c, const OldSlot& old, const OldZone& z, const std::vector<const EraTables*>& candidates,
			const std::string& what, Value& out, const Hash* digest_override, const NewFileContext* context)
		{
			const auto& strings = *old.zone_strings;
			const auto zone = py_lower(zone_string(strings, z.name_handle), Enc::cp1252);
			const ZoneCaptureMetadata* capture = nullptr;
			if (context) for (const auto& candidate : context->zone_captures)
			{
				if (py_lower(candidate.zone, Enc::cp1252) == zone && candidate.time_stamp == z.tail.time_stamp)
				{
					if (capture) fail(what + ": duplicate capture metadata");
					capture = &candidate;
				}
			}
			if (context && context->require_capture_metadata && !capture) fail(what + ": missing observed capture metadata");
			if (capture && (capture->capture_flags & ~3u)) fail(what + ": unsupported capture status bits");
			if (capture && (capture->capture_flags & 1u)) fail(what + ": incomplete capture cannot be saved");
			const auto& build = *c.t.build;
			const auto zone_hash = key_hash1(zone, Enc::cp1252);
			const u32 zid = c.id_of(ZONE, zone_hash);
			if (!zid)
			{
				c.res.notes.push_back(what + ": zone " + repr(zone) + " unknown to every release: record dropped");
				return false;
			}
			const auto* num = build.zone_numbering(zone);
			if (!num || !num->problems.empty())
			{
				std::string why = "missing";
				if (num)
				{
					Value l = L();
					for (const auto& p : num->problems) l.push(Tx(p, Enc::latin1));
					why = py_repr_value(l);
				}
				c.res.notes.push_back(what + ": zone " + repr(zone) + ": no placement file in the build or its numbering cannot be modelled (" +
					why + "): record dropped");
				return false;
			}
			const auto& ref_hashes = num->names;
			std::vector<std::pair<u32, std::pair<Hash, Hash>>> names_by_ordinal;
			for (u32 k = 1; k <= z.tail.ordinal_map.size(); ++k)          // the record's own ordinal map (its native layout's size)
			{
				const u32 idx = z.tail.ordinal_map[k - 1];
				if (idx)
				{
					const auto& s = z.states[idx];
					names_by_ordinal.emplace_back(k, std::make_pair(key_hash1(zone_string(strings, s.def_name), Enc::cp1252),
						key_hash1(zone_string(strings, s.inst_name), Enc::cp1252)));
				}
			}
			const auto eras = capture ? std::vector<const EraTables*>{&c.ref} : zone_record_eras(names_by_ordinal, zid, candidates, ref_hashes);
			if (eras.empty())
			{
				c.res.notes.push_back(what + ": zone " + repr(zone) + ": no builder release numbers its " + str(static_cast<std::int64_t>(names_by_ordinal.size())) +
					" numbered entities as the record does: record dropped (the zone starts fresh)");
				return false;
			}
			const auto& era = *eras[0];
			const auto* delta = era.zone_map(zid);
			Value names = L();
			names.push(Tx(""));
			std::vector<std::string> name_list{""};
			auto name_text = [&](const std::string& text) -> std::int64_t
			{
				for (std::size_t k = 0; k < name_list.size(); ++k)
				{
					if (name_list[k] == text) return static_cast<std::int64_t>(k);
				}
				name_list.push_back(text);
				names.push(Tx(text));
				return static_cast<std::int64_t>(name_list.size() - 1);
			};
			auto nm = [&](u32 handle) { return name_text(zone_string(strings, handle)); };
			std::set<u32> world_handles;
			for (const auto& w : old.world) world_handles.insert(w.slot);
			Value states = L();
			auto state_rec = [&](const OldStateRecord& s, int kind, std::int64_t ordinal, std::int64_t placement) -> bool
			{
				Value rec = Value::dict();
				rec.set("list", I(kind));
				rec.set("entity_tag", I(0));
				rec.set("ordinal", I(ordinal));
				rec.set("placement_index", I(placement));
				rec.set("def_name", I(nm(s.def_name)));
				rec.set("inst_name", I(nm(s.inst_name)));
				rec.set("flags_a", I(s.flags_a));
				rec.set("flags_b", I(s.flags_b));
				rec.set("position", reals(s.position, 3));
				if (s.flags_a & 1)
				{
					const u32 slot = s.payload[0];
					i16 qty = 0;
					std::memcpy(&qty, s.payload + 1, 2);
					if (!world_handles.count(slot))
					{
						// CORE-BEGIN: native factory 0x465c35 rejects a numbered record with
						// alive bit 8 clear before its virtual item restore can read the slot.
						// Preserve that removed placement; its stale item payload is unreachable.
						if (context && kind == 1 && !(s.flags_b & 8))
						{
							rec.set("flags_a", I(0));
							rec.set("payload_kind", I(1));
							rec.set("payload", Value::bytes(std::string(8, '\0')));
							states.push(std::move(rec));
							c.res.notes.push_back(what + ": dead numbered item slot " + str(slot) +
								": removed placement retained without stale item reference (native load skips item restore)");
							return true;
						}
						// CORE-END
						// Native item restore (0x479fc0) tolerates an absent slot. An unnumbered item
						// with no instance is a ghost, not a reconstructable pickup. Keep live numbered
						// identity loss strict and keep the legacy importer's recorded drop policy.
						if (context && kind != 1)
						{
							c.res.notes.push_back(what + ": dangling world item slot " + str(slot) +
								": unnumbered item state omitted (native load has no item instance)");
							return false;
						}
						++c.res.dropped.states;
						c.res.notes.push_back(what + ": a state owns world item slot " + str(slot) + " that the item section does not list: dropped");
						return false;
					}
					rec.set("payload_kind", I(2));
					std::string p(8, '\0');
					const i32 q = qty;
					std::memcpy(&p[0], &slot, 4);
					std::memcpy(&p[4], &q, 4);
					p.append(reinterpret_cast<const char*>(s.payload + 3), 5);
					rec.set("payload", Value::bytes(p));
				}
				else
				{
					rec.set("payload_kind", I(1));
					rec.set("payload", Value::bytes(s.payload, 8));
				}
				states.push(std::move(rec));
				return true;
			};
			for (const auto& [k, nh] : names_by_ordinal)
			{
				u32 ref_k = k;
				if (delta && !capture)
				{
					ref_k = k <= delta->map.size() ? delta->map[k - 1] : 0;
					const bool amb = std::find(delta->ambiguous.begin(), delta->ambiguous.end(), k) != delta->ambiguous.end();
					if (amb || ref_k == 0)
					{
						++c.res.dropped.states;
						c.res.notes.push_back(what + ": zone " + repr(zone) + " ordinal " + str(k) +
							": the entity has no unambiguous counterpart in the build (builder " + era.release + " numbering): state dropped");
						continue;
					}
				}
				u32 placement = 0;
				if (capture)
				{
					const PlacedEntityMetadata* entity = nullptr;
					for (const auto& e : capture->entities) if (e.ordinal == k)
					{
						if (entity) fail(what + ": duplicate captured ordinal");
						entity = &e;
					}
					if (!entity || key_hash1(entity->definition_name, Enc::cp1252) != nh.first ||
						key_hash1(entity->instance_name, Enc::cp1252) != nh.second) fail(what + ": captured entity association missing or differs");
					placement = capture->source_placement_sha1 == Sha1{} ? entity->placement_index : entity->source_placement_index;
				}
				else
				{
					if (ref_k > num->placement.size()) fail("IndexError: list index out of range");
					placement = num->placement[ref_k - 1];
				}
				state_rec(z.states[z.tail.ordinal_map[k - 1]], 1, ref_k, placement);
			}

			if (capture)
			{
				std::set<u32> ordinals, placements;
				for (const auto& e : capture->entities)
				{
					if (!e.ordinal || e.ordinal > z.tail.ordinal_map.size() || !ordinals.insert(e.ordinal).second || !placements.insert(e.placement_index).second)
						fail(what + ": invalid or duplicate observed entity association");
					if (!z.tail.ordinal_map[e.ordinal - 1])
					{
						// An observed entity absent from the native state map is dead/removed. Preserve that identity explicitly.
						states.push(Value::dict({{"list", I(1)}, {"entity_tag", I(0)}, {"ordinal", I(e.ordinal)}, {"placement_index", I(capture->source_placement_sha1 == Sha1{} ? e.placement_index : e.source_placement_index)},
							{"def_name", I(name_text(e.definition_name))}, {"inst_name", I(name_text(e.instance_name))}, {"flags_a", I(0)}, {"flags_b", I(0)},
							{"position", Value::list()}, {"payload_kind", I(1)}, {"payload", Value::bytes(std::string(8, '\0'))}}));
						Value position = L(); for (int k = 0; k < 3; ++k) position.push(R(0));
						states.l.back().set("position", std::move(position));
					}
				}
			}
			for (u32 k = 0; k < z.tail.dynamic_count; ++k) state_rec(z.states[z.tail.dynamic[k]], 2, 0, 0);
			for (u32 k = 0; k < z.tail.smart_count; ++k) state_rec(z.states[z.tail.smart[k]], 3, 0, 0);
			if (states.as_list().size() >= z.states.size()) fail(what + ": capture identities exceed native state capacity");
			Hash digest{};
			if (capture) digest = capture->conversation_digest;
			else if (context) digest = conversation_package_digest(key_hash1(zone, Enc::latin1), conversation_bits(*c.t.build, zone));
			else if (digest_override) digest = *digest_override;
			else if (era.has_digest) digest = era.conversation_digest;
			Value conv = L();
			for (const auto w : z.tail.conversation) conv.push(I(w));
			out = Value::dict();
			out.set("zone", I(zid));
			out.set("identity_mode", I(2));
			out.set("content_hash", Value::bytes(capture ? (capture->source_placement_sha1 == Sha1{} ? capture->placement_sha1.data() : capture->source_placement_sha1.data()) : num->sha1.data(), 20));
			out.set("time_stamp", R(z.tail.time_stamp));
			out.set("flag", I(z.tail.flag));
			out.set("capture_flags", I(capture ? capture->capture_flags : 2));
			out.set("conversation", std::move(conv));
			out.set("conversation_digest", Value::bytes(digest.data(), 32));
			out.set("names", std::move(names));
			out.set("states", std::move(states));
			out.set("fog", Value::dict({{"encoding", I(1)}, {"width", I(82)}, {"height", I(82)}, {"data", Value::bytes(z.tail.fog, old_fog_bytes)}}));
			c.res.zone_eras.push_back(era.cv);
			return true;
		}

		bool is_default_restore(const OldRestore& r)
		{
			const auto& s = r.attributes.scalars;
			for (int k = 0; k < 4; ++k)
			{
				if (s.trait_base[k] != 1 || s.trait_pending[k] || s.trait_bonus[k] || s.trait_bonus2[k]) return false;
			}
			if (s.skill_points || s.trait_points || s.level != 1 || s.ai_level != 1 || s.ai_attack || s.pad17) return false;
			if (r.attributes.tail.bitmap || r.attributes.tail.count) return false;
			const auto& t = r.equipment.tail;
			if (!(t.health == 0 && t.energy == 0 && t.player_seat == -1 && t.vitals_valid == 0 && !t.pad0d[0] && !t.pad0d[1] && !t.pad0d[2] &&
				t.experience == 0 && t.ack_level == 1 && !t.health_boosts && !t.energy_boosts && !t.levels_purchased && !t.points_bought &&
				!t.respec_count && !t.skirmish_boost && !t.flags))
			{
				return false;
			}
			if (!r.equipment.equipped.empty()) return false;
			for (const auto& e : r.attributes.talents)
			{
				if (e.id || e.rank_bits || e.pending_cap_bits || e.base_bits || e.byte5 || e.pad6) return false;
			}
			return true;
		}

		// legacy.talent_era: the newest era keeping the most held shared ranks explainable
		const EraTables* talent_era(const OldSlot& old, const std::vector<const EraTables*>& eras, const EraTables& ref,
			std::vector<std::pair<std::int64_t, std::int64_t>>& unexplained_out)
		{
			// Evidence is keyed by stats index: hero block n -> n + 1; restore record n -> n.
			std::vector<std::pair<std::int64_t, std::set<u32>>> held;
			auto slot_of = [&](std::int64_t n) -> std::set<u32>*
			{
				for (auto& [k, v] : held) if (k == n) return &v;
				return nullptr;
			};
			for (std::size_t n = 0; n < old.heroes.size(); ++n)
			{
				std::set<u32> ids;
				const auto& a = old.heroes[n].attributes;
				for (u32 k = 0; k < a.tail.count; ++k) if (a.talents[k].id < 100) ids.insert(a.talents[k].id);
				held.emplace_back(static_cast<std::int64_t>(n + 1), std::move(ids));
			}
			for (std::size_t n = 0; n < old.restore.size(); ++n)
			{
				std::set<u32> ids;
				const auto& a = old.restore[n].attributes;
				for (u32 k = 0; k < a.tail.count; ++k) if (a.talents[k].id < 100) ids.insert(a.talents[k].id);
				if (ids.empty()) continue;
				if (auto* s = slot_of(static_cast<std::int64_t>(n))) s->insert(ids.begin(), ids.end());
				else held.emplace_back(static_cast<std::int64_t>(n), std::move(ids));
			}
			const EraTables* best = nullptr;
			std::int64_t best_good = -1;
			std::vector<std::pair<std::int64_t, std::int64_t>> best_unexplained;
			for (const auto* era : eras)
			{
				std::int64_t good = 0;
				std::vector<std::pair<std::int64_t, std::int64_t>> unexplained;
				for (const auto& [si, ids] : held)
				{
					const u32 hero = si < static_cast<std::int64_t>(ref.STAT.size()) ? ref.STAT[static_cast<std::size_t>(si)] : 0;
					const std::vector<u32>* hold = nullptr;
					if (hero)
					{
						const auto it = era->holdable.find(hero);
						if (it != era->holdable.end()) hold = &it->second;
					}
					for (const auto tid : ids)
					{
						const bool holdable = hold && std::find(hold->begin(), hold->end(), tid) != hold->end();
						if (tid < era->TALN_shared.size() && holdable) ++good;
						else unexplained.emplace_back(si, tid);
					}
				}
				if (!best || good > best_good)
				{
					best = era;
					best_good = good;
					best_unexplained = unexplained;
				}
			}
			unexplained_out = best_unexplained;
			return best;
		}

		// convert._counts_by_era: {cv: {definition count the engine would load}}
		std::vector<std::pair<int, std::vector<u32>>> counts_by_era(const TablesData& t)
		{
			std::vector<std::pair<int, std::vector<u32>>> out;
			for (const auto& era : t.eras)
			{
				std::vector<u32> counts;
				for (const auto pool : item_pools)
				{
					u32 used = era.trit[1], k = 0;
					for (const auto& lst : era.ITEM_own)
					{
						if (used + lst.size() > pool) break;
						used += static_cast<u32>(lst.size());
						++k;
					}
					if (std::find(counts.begin(), counts.end(), k) == counts.end()) counts.push_back(k);
				}
				out.emplace_back(era.cv, std::move(counts));
			}
			return out;
		}

		std::vector<std::pair<std::string, const OldInstance*>> it_instances(const OldSlot& old)
		{
			std::vector<std::pair<std::string, const OldInstance*>> out;
			for (std::size_t i = 0; i < old.carried.size(); ++i) out.emplace_back("carried item " + std::to_string(i), &old.carried[i]);
			for (std::size_t i = 0; i < old.world.size(); ++i) out.emplace_back("world item " + std::to_string(i), &old.world[i].instance);
			for (std::size_t n = 0; n < old.heroes.size(); ++n)
			{
				for (std::size_t i = 0; i < old.heroes[n].equipment.equipped.size(); ++i)
				{
					out.emplace_back("hero block " + std::to_string(n) + " equipped item " + std::to_string(i), &old.heroes[n].equipment.equipped[i]);
				}
			}
			for (std::size_t n = 0; n < old.restore.size(); ++n)
			{
				for (std::size_t i = 0; i < old.restore[n].equipment.equipped.size(); ++i)
				{
					out.emplace_back("restore record " + std::to_string(n) + " equipped item " + std::to_string(i), &old.restore[n].equipment.equipped[i]);
				}
			}
			return out;
		}

		void legacy_forge_recovery(const OldSlot& old, const TablesData& tables, ConvertState& res, Value& game_vars)
		{
			// Campaign directories after mansion1's HAARP departure; no training/flashback/act heuristic.
			static constexpr const char* later_dirs[] = {
				"haarp/ext/", "haarp/int/", "haarp/ice_sq/",
				"mansion/man2/", "mansion/man3/", "mansion/man4/", "mansion/man5/",
					"mansion/man6/", "mansion/man7/", "mansion/man8/",
				"muir_is/muir1/", "muir_is/muir2/", "muir_is/muir3/",
				"sewers/hub/", "sewers/healer/", "sewers/grso/", "arbiter/deck/", "arbiter/a_int/",
				"nuke_plant/nuke/", "astral/ast1/", "astral/ast3/", "astral/savepx/",
				"weapon_x/old/", "weapon_x/secret/", "mount/mount/", "nyc/riots/",
				"hive/h_ext/", "hive/h_int/", "astroid_m/visit1/", "mastermold/"
			};
			auto later = [&](const std::string& name)
			{
				const auto zone = py_lower(name, Enc::cp1252);
				if (!tables.catalogue.lookup(ZONE, key_hash1(zone, Enc::cp1252))) return false;
				for (const auto* dir : later_dirs) if (zone.find(dir) == 0) return true;
				return zone == "arbiter/arb_fd1" || zone == "arbiter/arb_fd2";
			};
			bool progressed = later(cstr(old.mission.zone));
			for (u32 i = 0; i < old.side.count; ++i) progressed |= later(cstr(old.side_frames[i].zone));
			for (const auto& z : old.zones) progressed |= later(zone_string(*old.zone_strings, z.name_handle));
			if (!progressed) return;
			Value* target = nullptr;
			for (auto& v : game_vars.l) if (py_lower(v["name"].s, Enc::cp1252) == "x1v03") { target = &v; break; }
			if (!target)
			{
				if (game_vars.l.size() >= old_script_game_vars) fail("Forge contact unlock: game-variable store is full");
				game_vars.push(Value::dict({{"name", Tx("x1v03")}, {"value", I(0)}}));
				target = &game_vars.l.back();
			}
			// Script flag indices are one based; bit 31 is 0x40000000, not the sign bit.
			const auto value = static_cast<u32>((*target)["value"].as_int());
			if (!(value & 0x40000000u))
			{
				target->set("value", I(value | 0x40000000u));
				res.notes.push_back("Forge contact unlock granted (0.1.x builds did not record it)");
			}
		}

		struct Plan
		{
			bool converter = true;                 // false: the saver's direction (one era: the running build)
			std::vector<const EraTables*> eras_ok;
			const EraTables* start = nullptr;
			const EraTables* items = nullptr;
			const Hash* digest = nullptr;
			const NewFileContext* context = nullptr;
		};

		std::vector<SectionOut> profile_sections(const Conv& c, const OldProfile& p, const EraTables& era)
		{
			std::vector<SectionOut> out;
			const auto options = p.head.options[0] | (p.head.options[1] << 8) | (p.head.options[2] << 16);
			out.push_back({"PRFO", 1, 0, 0, Value::dict({{"options", I(options)}, {"player_name", Tx(cstr(p.head.player_name))}})});
			const auto& stat = era.STAT;
			const auto& ref = c.ref;
			Value scores = L();
			for (u32 k = 0; k < p.tail.score_count; ++k)
			{
				const auto& s = p.high_scores[k];
				if (s.course >= ref.DRCS.size())
				{
					fail("high score names course position " + str(s.course) + " beyond the table");
				}
				auto party = [&](const u16* lst, u32 n)
				{
					Value ids = L();
					for (u32 j = 0; j < n && j < 6; ++j)
					{
						const u32 v = lst[j];
						if (!(v < stat.size()) || !stat[v])
						{
							fail("high score participant index " + str(v) + " is outside the stats table");
						}
						ids.push(I(c.id(STAT, stat[v], "high score participant")));
					}
					return ids;
				};
				Value rec = Value::dict();
				rec.set("course", I(c.id(DRCS, ref.DRCS[s.course], "high score")));
				rec.set("party_a", party(s.party_a, s.count_a));
				rec.set("party_b", party(s.party_b, s.count_b));
				rec.set("score", I(s.score));
				scores.push(std::move(rec));
			}
			out.push_back({"PRFS", 1, 0, 0, Value::dict({{"scores", std::move(scores)}})});
			Value chars = L();
			for (const int i : bits(p.tail.char_unlock, 297))
			{
				if (static_cast<std::size_t>(i) >= stat.size() || !stat[static_cast<std::size_t>(i)])
				{
					fail("profile unlock bit " + str(i) + " is outside the stats table");
				}
				chars.push(I(c.id(STAT, stat[static_cast<std::size_t>(i)], "profile unlock")));
			}
			Value review = L();
			for (int cat = 0; cat < 5; ++cat)
			{
				for (const int i : bits(p.tail.review + cat * 3, review_per_category))
				{
					const auto it = ref.REVW.find({cat, i});
					if (it == ref.REVW.end())
					{
						fail("profile Review bit (category " + str(cat) + ", entry " + str(i) + ") names an entry no release has");
					}
					review.push(I(c.id(REVW, it->second, "profile review")));
				}
			}
			out.push_back({"PRFP", 1, 0, 0, Value::dict({{"highest_level", I(p.tail.highest_level)}, {"flags", I(p.tail.flags_229)},
				{"characters", std::move(chars)}, {"review", std::move(review)}})});
			return out;
		}

		std::uint64_t now_filetime()
		{
			FILETIME ft{};
			GetSystemTimeAsFileTime(&ft);
			return (std::uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
		}

		// convert._translate (and the saver's direction with plan.converter = false)
		std::vector<SectionOut> translate(const Conv& c, const OldSlot& old, Plan& plan, const std::string& original_name,
			const std::vector<std::uint8_t>& old_bytes, u8 slot, std::uint64_t saved_at, const std::string& writer,
			const std::vector<std::string>& mods, const std::string& description)
		{
			const auto& ref = c.ref;
			auto& res = c.res;
			const auto nheroes = old.heroes.size();
			if (plan.converter)
			{
				std::set<u32> counts;
				for (const auto& e : c.t.eras)
				{
					if (e.hero_count == nheroes) plan.eras_ok.push_back(&e);
					counts.insert(e.hero_count);
				}
				if (plan.eras_ok.empty())
				{
					std::string s = "{";
					for (const auto n : counts) s += (s.size() > 1 ? ", " : "") + std::to_string(n);
					fail(str(static_cast<std::int64_t>(nheroes)) + " hero blocks; no builder release has that many (" + (counts.empty() ? "set()" : s + "}") + ")");
				}
			}
			else if (nheroes != plan.eras_ok[0]->hero_count)
			{
				fail(str(static_cast<std::int64_t>(nheroes)) + " hero blocks; the build has " + str(plan.eras_ok[0]->hero_count));
			}
			const auto live_courses = old.courses.size();
			if (live_courses != ref.DRCS.size())
			{
				fail(str(static_cast<std::int64_t>(live_courses)) + " live Danger Room courses; the builder releases have " +
					str(static_cast<std::int64_t>(ref.DRCS.size())) + " (a developer build with another course table?)");
			}
			// ---- eras
			if (plan.converter)
			{
				std::vector<std::pair<std::int64_t, std::int64_t>> unexplained;
				plan.start = talent_era(old, plan.eras_ok, ref, unexplained);
				if (!unexplained.empty())
				{
					Value l = L();
					for (std::size_t k = 0; k < unexplained.size() && k < 6; ++k)
					{
						Value p = L();
						p.push(I(unexplained[k].first));
						p.push(I(unexplained[k].second));
						l.push(std::move(p));
					}
					res.notes.push_back(str(static_cast<std::int64_t>(unexplained.size())) +
						" shared talent ranks are talents the hero cannot hold in any release (kept under builder " + plan.start->release + " names): " +
						py_repr_value(l));
				}
				const auto counts = counts_by_era(c.t);
				std::vector<const EraTables*> item_eras;
				for (const auto* e : plan.eras_ok)
				{
					for (const auto& [cv, cs] : counts)
					{
						if (cv == e->cv && std::find(cs.begin(), cs.end(), old.items.def_count) != cs.end()) item_eras.push_back(e);
					}
				}
				if (item_eras.empty())
				{
					std::string s = "{";
					bool first = true;
					for (const auto& [cv, cs] : counts)
					{
						auto sorted = cs;
						std::sort(sorted.begin(), sorted.end());
						const auto* era = c.t.by_cv(cv);
						s += std::string(first ? "" : ", ") + repr(era ? era->release : std::to_string(cv)) + ": [";
						for (std::size_t k = 0; k < sorted.size(); ++k) s += (k ? ", " : "") + std::to_string(sorted[k]);
						s += "]";
						first = false;
					}
					fail("definition count byte " + str(old.items.def_count) + " matches no builder release (expected one of " + s + "})");
				}
				const auto all = it_instances(old);
				std::vector<std::string> reasons;
				for (const auto* e : item_eras)
				{
					try
					{
						for (const auto& [what, inst] : all) c.cross_check_instance(*e, *inst, what);
						plan.items = e;
						break;
					}
					catch (const Error& x)
					{
						reasons.push_back(e->release + ": " + x.message);
					}
				}
				if (!plan.items)
				{
					std::string s;
					for (std::size_t k = 0; k < reasons.size() && k < 3; ++k) s += (k ? "; " : "") + reasons[k];
					fail("the inventory agrees with no builder release: " + s);
				}
			}
			else
			{
				plan.start = plan.items = plan.eras_ok[0];
				for (const auto& [what, inst] : it_instances(old)) c.cross_check_instance(*plan.items, *inst, what);
			}
			res.start_era = plan.start->cv;
			res.items_era = plan.items->cv;
			// zone records: each record names its own era by the entity names it stores
			Value zone_recs = L();
			double newest = 0.0;
			bool any = false;
			for (std::size_t i = 0; i < old.zones.size(); ++i)
			{
				Value rec;
				if (!zone_record(c, old, old.zones[i], plan.eras_ok, "zone record " + std::to_string(i), rec, plan.digest, plan.context))
				{
					++res.dropped.zone_records;
					continue;
				}
				const double ts = rec["time_stamp"].as_real();
				if (!any || ts > newest) newest = ts;
				any = true;
				zone_recs.push(std::move(rec));
			}
			const auto& era_items = *plan.items;
			const auto& era_tal = *plan.start;
			// ---- META
			const auto& g = old.game;
			const auto cur_zone = py_lower(cstr(old.mission.zone), Enc::cp1252);
			std::vector<SectionOut> sections;
			std::int64_t cur_zone_id = 0;
			if (!cur_zone.empty())
			{
				cur_zone_id = c.id_of(ZONE, key_hash1(cur_zone, Enc::cp1252));
				if (!cur_zone_id) fail("current zone " + repr(cur_zone) + " is unknown to every builder release");
			}
			Value mods_v = L();
			for (const auto& m : mods) mods_v.push(Tx(m));
			sections.push_back({"META", 1, 0, section_required, Value::dict({{"saved_at", I(static_cast<std::int64_t>(saved_at))},
				{"description", Tx(description)}, {"origin", I(plan.converter ? 1 : (plan.context ? plan.context->origin : 0))}, {"zone", I(cur_zone_id)}, {"act", I(g.act)},
				{"difficulty", I(g.difficulty)}, {"play_time", R(g.play_time)}, {"writer", Tx(writer)}, {"slot", I(slot)},
				{"mods", std::move(mods_v)}})});
			// ---- GAME
			Value trivia = L();
			for (const int b : bits(g.trivia, 250))
			{
				int act = b / trivia_per_act + 1, q = b % trivia_per_act;
				if (!plan.converter)
				{
					const auto found = std::find_if(ref.trivia_bits.begin(), ref.trivia_bits.end(), [b](const auto& row) { return row.second == b; });
					if (found == ref.trivia_bits.end()) fail("native trivia bit " + str(b) + " names no question in this build");
					act = found->first.first; q = found->first.second;
				}
				const auto it = ref.TRIV.find({act, q});
				if (it == ref.TRIV.end())
				{
					fail("trivia bit " + str(b) + " (act " + str(act) + " question " + str(q + 1) + ") names a question no release has");
				}
				trivia.push(I(c.id(TRIV, it->second, "trivia")));
			}
			const auto recall = py_lower(cstr(g.recall_zone), Enc::cp1252);
			std::int64_t recall_id = 0;
			if (!recall.empty() && recall != "none")
			{
				recall_id = c.id_of(ZONE, key_hash1(recall, Enc::cp1252));
				if (!recall_id) fail("recall zone " + repr(recall) + " is unknown to every builder release");
			}
			const u32 d8 = g.flags_d8, d9 = g.flags_d9, da = g.flags_da;
			const u32 flags = ((d9 >> 5) & 7) | ((da & 0x7f) << 3);
			Value game = Value::dict({{"timer_threshold", R(g.timer_threshold)}, {"timer_remaining", R(g.timer_remaining)},
				{"timer_after_add", R(g.timer_after_add)}, {"timer_last_add", R(g.timer_last_add)}, {"timer_script", Tx(cstr(g.timer_script))},
				{"recall_cooldown", R(g.recall_cooldown)}, {"xtreme_charges", I(g.xtreme_charges)}, {"xtreme_pips", I(g.xtreme_pips)},
				{"loading_name", Tx(cstr(g.loading_name))}, {"recall_zone", I(recall_id)}});
			if (recall_id) game.set("recall_pos", reals(g.recall_pos, 3));
			else
			{
				const float zero[3] = {0, 0, 0};
				game.set("recall_pos", reals(zero, 3));
			}
			game.set("act", I(g.act));
			game.set("value_608", I(g.value_608));
			game.set("difficulty", I(g.difficulty));
			game.set("play_time", R(g.play_time));
			game.set("team_potion", I(d8 & 3));
			game.set("counter_a", I((d8 >> 2) & 0x1f));
			game.set("counter_b", I(d9 & 0x1f));
			game.set("flags", I(flags));
			game.set("trivia", std::move(trivia));
			sections.push_back({"GAME", 1, 0, 0, std::move(game)});
			// ---- ITEM
			const auto& it = old.items;
			Value def_state = L();
			for (u32 i = 0; i < old_def_state_pairs; ++i)
			{
				const u32 q = it.def_state[2 * i], f = it.def_state[2 * i + 1];
				if (q || f)
				{
					if (i >= era_items.ITEM.size())
					{
						fail("definition state pair " + str(i) + " is beyond the item table of builder " + era_items.release);
					}
					def_state.push(Value::dict({{"item", I(c.id(ITEM, era_items.ITEM[i], "definition state"))}, {"quantity", I(q)}, {"flag", I(f)}}));
				}
			}
			Value key_counts = L();
			for (int i = 0; i < 6; ++i)
			{
				const u32 v = c.id_of(ITEM, key_hash1(key_item_names[i], Enc::cp1252));
				if (!v) fail(std::string("key item ") + key_item_names[i] + " is not in the catalogue");
				key_counts.push(Value::dict({{"item", I(v)}, {"quantity", I(it.key_counts[i])}, {"flag", I(0)}}));
			}
			Value instances = L();
			for (std::size_t i = 0; i < old.carried.size(); ++i)
			{
				instances.push(c.instance_keys(era_items, old.carried[i], "carried item " + std::to_string(i)));
			}
			Value world = L();
			for (std::size_t i = 0; i < old.world.size(); ++i)
			{
				world.push(Value::dict({{"handle", I(old.world[i].slot)},
					{"instance", c.instance_keys(era_items, old.world[i].instance, "world item " + std::to_string(i))}}));
			}
			sections.push_back({"ITEM", 1, 0, 0, Value::dict({{"tech_bits", I(it.tech_bits)}, {"health_items", I(it.health_items)},
				{"energy_items", I(it.energy_items)}, {"def_state", std::move(def_state)}, {"key_counts", std::move(key_counts)},
				{"instances", std::move(instances)}, {"world", std::move(world)}})});
			// ---- HERO
			Value heroes = L();
			for (std::size_t n = 0; n < old.heroes.size(); ++n)
			{
				heroes.push(c.hero_full(era_tal, era_items, static_cast<std::int64_t>(n + 1), old.heroes[n], "hero block " + std::to_string(n)));
			}
			sections.push_back({"HERO", 1, section_minor("HERO"), 0, Value::dict({{"heroes", std::move(heroes)}})});
			// ---- ULCK
			const auto& stat = era_tal.STAT;
			Value unlocked = L();
			for (u32 k = 0; k < old.unlocked_count; ++k)
			{
				const std::int64_t i = old.unlocked[k];
				if (!(0 <= i && i < static_cast<std::int64_t>(stat.size())) || !stat[static_cast<std::size_t>(i)])
				{
					fail("unlocked character index " + str(i) + " is outside the stats table (" + str(static_cast<std::int64_t>(stat.size())) + ")");
				}
				unlocked.push(I(c.id(STAT, stat[static_cast<std::size_t>(i)], "unlocked character")));
			}
			sections.push_back({"ULCK", 1, 0, 0, Value::dict({{"unlocked", std::move(unlocked)}})});
			// ---- REST
			Value records = L();
			for (std::size_t n = 0; n < old.restore.size(); ++n)
			{
				const auto& r = old.restore[n];
				if (is_default_restore(r)) continue;
				if (n == 0)
				{
					++res.dropped.other;
					res.notes.push_back("restore record 0 (the reserved stats entry) is not default: dropped");
					continue;
				}
				Value rec = c.hero_record(era_tal, era_items, static_cast<std::int64_t>(n), r.attributes, r.equipment, "restore record " + std::to_string(n));
				Value out = Value::dict();
				out.set("owner", rec["hero"]);
				for (auto& item : rec.d)
				{
					if (item.key.s != "hero") out.set(item.key, item.value);
				}
				records.push(std::move(out));
			}
			sections.push_back({"REST", 1, 0, 0, Value::dict({{"bank_valid", I(old.restore_flag)}, {"records", std::move(records)}})});
			// ---- MISS
			Value snaps = L();
			snaps.push(c.snapshot(era_tal, old.mission, "mission state"));
			for (u32 i = 0; i < old.side.count; ++i)
			{
				snaps.push(c.snapshot(era_tal, old.side_frames[i], "side-mission frame " + std::to_string(i)));
			}
			sections.push_back({"MISS", 1, 0, 0, Value::dict({{"stack_depth", I(old.side.count)}, {"snapshots", std::move(snaps)}})});
			// ---- DNGR
			Value courses = L();
			for (std::size_t i = 0; i < old.courses.size(); ++i)
			{
				const auto& cr = old.courses[i];
				Value rec = Value::dict();
				rec.set("course", I(c.id(DRCS, ref.DRCS[i], "course")));
				rec.set("flags", I(cr.flags));
				const char tiers[3] = {static_cast<char>(cr.tier0 & 1), static_cast<char>(cr.tier1 & 1), static_cast<char>(cr.tier2 & 1)};
				rec.set("tiers", Value::bytes(std::string(tiers, 3)));
				rec.set("largest_hit", I(cr.largest_hit));
				rec.set("least_damage", I(cr.least_damage));
				rec.set("best_time", I(cr.best_time));
				rec.set("best_combo", I(cr.best_combo));
				rec.set("most_health_packs", I(cr.most_health_packs));
				rec.set("most_energy_packs", I(cr.most_energy_packs));
				rec.set("most_defeats", I(cr.most_defeats));
				const std::pair<const char*, u16> heroes_f[5] = {{"largest_hit_hero", cr.largest_hit_hero}, {"least_damage_hero", cr.least_damage_hero},
					{"most_health_packs_hero", cr.most_health_packs_hero}, {"most_energy_packs_hero", cr.most_energy_packs_hero},
					{"most_defeats_hero", cr.most_defeats_hero}};
				for (const auto& [f, v] : heroes_f)
				{
					if (v == 0 || v == 0xffff) rec.set(f, I(0));
					else if (v < stat.size() && stat[v]) rec.set(f, I(c.id(STAT, stat[v], "course " + std::to_string(i) + " " + f)));
					else fail("course " + std::to_string(i) + ": " + f + " index " + str(v) + " is outside the stats table");
				}
				courses.push(std::move(rec));
			}
			Value grades = L();
			for (std::size_t i = 0; i < old_dr_grades && i < ref.DRGR.size(); ++i)
			{
				grades.push(Value::dict({{"grade", I(c.id(DRGR, ref.DRGR[i], "grade"))}, {"value", I(old.grades[i])}}));
			}
			sections.push_back({"DNGR", 1, 0, 0, Value::dict({{"courses", std::move(courses)}, {"grades", std::move(grades)}})});
			// ---- REVW
			auto review_ids = [&](const u32* words)
			{
				Value out = L();
				for (int cat = 0; cat < 5; ++cat)
				{
					for (const int i : bits(words + cat * 3, review_per_category))
					{
						const auto f = ref.REVW.find({cat, i});
						if (f == ref.REVW.end())
						{
							fail("Review bit (category " + str(cat) + ", entry " + str(i) + ") names an entry no release has");
						}
						out.push(I(c.id(REVW, f->second, "review")));
					}
				}
				return out;
			};
			Value rv_unlocked = review_ids(old.review.unlocked);
			Value rv_seen = review_ids(old.review.seen);
			sections.push_back({"REVW", 1, 0, 0, Value::dict({{"unlocked", std::move(rv_unlocked)}, {"seen", std::move(rv_seen)}})});
			// ---- ZONE
			sections.push_back({"ZONE", 1, 0, 0, Value::dict({{"newest_time", R(newest)}, {"container_flag", I(0)}, {"records", std::move(zone_recs)}})});
			// ---- SVAR
			auto vars = [&](const OldScriptVar* v, u32 n)
			{
				Value out = L();
				for (u32 k = 0; k < n; ++k)
				{
					const auto name = cstr(v[k].name);
					if (!name.empty()) out.push(Value::dict({{"name", Tx(name)}, {"value", I(script_var_value(v[k]))}}));
				}
				return out;
			};
			auto game_vars = vars(old.game_vars, old_script_game_vars);
			if (plan.converter) legacy_forge_recovery(old, c.t, res, game_vars);
			sections.push_back({"SVAR", 1, 0, 0, Value::dict({{"zone_vars", vars(old.zone_vars, old_script_zone_vars)},
				{"game_vars", std::move(game_vars)}})});
			// ---- XTRC
			Value xp = L();
			for (const int i : bits(&old.town_centres, 32))
			{
				if (static_cast<std::size_t>(i) >= ref.XPNT.size())
				{
					fail("town centre bit " + str(i) + " beyond the extraction-point table (" + str(static_cast<std::int64_t>(ref.XPNT.size())) + ")");
				}
				xp.push(I(c.id(XPNT, ref.XPNT[static_cast<std::size_t>(i)], "town centre")));
			}
			sections.push_back({"XTRC", 1, 0, 0, Value::dict({{"unlocked", std::move(xp)}})});
			// ---- profile
			for (auto& s : profile_sections(c, old.profile, era_tal)) sections.push_back(std::move(s));
			// ---- LGCY
			if (plan.converter)
			{
				const auto h = sha1(old_bytes.data(), old_bytes.size());
				Value notes = L();
				for (const auto& n : res.notes) notes.push(Tx(n));
				sections.push_back({"LGCY", 1, 0, section_keep, Value::dict({{"original_name", Tx(original_name)},
					{"original_size", I(static_cast<std::int64_t>(old_bytes.size()))}, {"sha1", Value::bytes(h.data(), 20)},
					{"start_era", I(plan.start->cv)}, {"writer_era", I(plan.items->cv)}, {"def_count_byte", I(it.def_count)},
					{"zone_records_dropped", I(res.dropped.zone_records)}, {"world_items_dropped", I(res.dropped.world_items)},
					{"zone_vars_dropped", I(res.dropped.zone_vars)},
					{"other_dropped", I(res.dropped.other + res.dropped.talents + res.dropped.states)}, {"notes", std::move(notes)}})});
			}
			return sections;
		}

		// convert.add_keys_section: every catalogue id that occurs, with its key text from the build
		void collect_ids(std::string_view type, const Value& v, std::set<u32>& ids);
		void collect_schema(const Schema& s, const Value& data, std::set<u32>& ids)
		{
			for (std::size_t k = 0; k < s.count; ++k)
			{
				if (!data.has(s.fields[k].name)) continue;
				const std::string_view t = s.fields[k].type;
				const auto& v = data[s.fields[k].name];
				if (t == "id") ids.insert(static_cast<u32>(v.as_int()));
				else if (t.substr(0, 5) == "list:")
				{
					for (const auto& x : v.as_list()) collect_ids(t.substr(5), x, ids);
				}
				else if (t.substr(0, 4) == "rec:" || t.substr(0, 7) == "packed:") collect_ids(t, v, ids);
			}
		}
		void collect_ids(std::string_view et, const Value& x, std::set<u32>& ids)
		{
			if (et == "id") ids.insert(static_cast<u32>(x.as_int()));
			else if (et.substr(0, 4) == "rec:") collect_schema(*record_schema(et.substr(4)), x, ids);
			else if (et.substr(0, 7) == "packed:") collect_schema(*packed_schema(et.substr(7)), x, ids);
		}

		void add_keys_section(std::vector<SectionOut>& sections, const TablesData& t)
		{
			std::set<u32> ids;
			for (const auto& s : sections)
			{
				const auto* sc = section_schema(s.tag);
				collect_schema(sc->head, s.data, ids);
				for (std::size_t k = 0; k < sc->lists.count; ++k)
				{
					const char* name = sc->lists.fields[k].name;
					if (!s.data.has(name)) continue;
					for (const auto& x : s.data[name].as_list()) collect_ids(sc->lists.fields[k].type, x, ids);
				}
			}
			Value keys = L();
			for (const auto v : ids)
			{
				if (!v) continue;
				const auto* e = t.catalogue.entry_of(v);
				if (!e) continue;
				// the key the id has in the catalogue (SPEC 2.3): its canonical key bytes (catalogue.lcc). The reference
				// writes the build's display text here ('/'.join(fields)), which catalogue.lcc does not carry
				keys.push(Value::dict({{"id", I(v)}, {"domain", I(e->domain)}, {"key", Tx(e->key, Enc::cp1252)}}));
			}
			sections.insert(sections.begin() + 1, SectionOut{"KEYS", 1, 0, section_keep, Value::dict({{"keys", std::move(keys)}})});
		}
		void retain_sections(std::vector<SectionOut>& sections, const std::vector<u8>& retained, int kind, const NativeLayout& native = retail_layout)
		{
			if (retained.empty()) return;
			const auto prior = read_file(retained.data(), retained.size(), kind);
			// the retained file's marker: a writer of this layout may carry over only a file its layout can apply
			const auto decision = decide_features(prior.header.required_features, native);
			if (!decision.ok) fail("retained file: " + decision.reason);
			bool opaque = false;
			for (const auto& tag : prior.order)
			{
				const auto& sec = prior.sections.at(tag);
				if (tag != "KEYS" && (tag == "LGCY" || (!sec.known && (sec.entry.flags & section_keep))))
				{
					sections.push_back({tag, sec.entry.major, sec.entry.minor, sec.entry.flags, Value::bytes(sec.payload), sec.entry.records});
					opaque |= !sec.known;
				}
			}
			if (!opaque) return;
			if (!prior.sections.count("KEYS")) fail("opaque KEEP without KEYS contract cannot be resaved");
			for (auto& sec : sections) if (sec.tag == "KEYS")
			{
				if (prior.data("KEYS").has("_extra") && prior.data("KEYS")["_extra"].truthy()) fail("opaque KEEP with extended KEYS contract cannot be resaved");
				Value merged = prior.data("KEYS")["keys"];
				for (const auto& key : merged.as_list()) if (key.has("_extra") && key["_extra"].truthy()) fail("opaque KEEP with extended KEYS record cannot be resaved");
				for (const auto& key : sec.data["keys"].as_list())
				{
					bool present = false;
					for (const auto& old : merged.as_list()) if (old["id"] == key["id"] && old["domain"] == key["domain"] && old["key"] == key["key"]) present = true;
					if (!present) merged.push(key);
				}
				sec.data.set("keys", std::move(merged));
			}
		}

	}

	const EraTables* detect_talent_era(const OldSlot& old, const std::vector<const EraTables*>& eras, const EraTables& ref,
		std::vector<std::pair<std::int64_t, std::int64_t>>& unexplained)
	{
		return talent_era(old, eras, ref, unexplained);
	}

	std::vector<std::uint8_t> convert_slot(const std::vector<std::uint8_t>& old_bytes, const TablesData& t, const ConvertOptions& options,
		ConvertState& state)
	{
		const auto old = read_slot(old_bytes.data(), old_bytes.size(), retail_layout);   // the importer: retail saves only, explicitly
		Conv c{t, state, t.reference};
		Plan plan;
		const std::uint64_t saved_at = options.fixed_saved_at ? options.saved_at : now_filetime();
		auto sections = translate(c, old, plan, options.original_name, old_bytes, options.slot, saved_at, options.writer, {},
			cstr(reinterpret_cast<const char*>(old.description_raw), 0x80));
		add_keys_section(sections, t);
		HeaderIn h;
		h.writer_version = options.writer_version;
		h.content_version = static_cast<u32>(t.reference.cv);
		h.catalogue_revision = t.catalogue.revision;
		if (options.fixed_campaign_serial)
		{
			h.campaign_serial = options.campaign_serial;
		}
		else
		{
			const auto d = sha1(old_bytes.data(), old_bytes.size());
			std::memcpy(&h.campaign_serial, d.data(), 8);
		}
		h.save_sequence = 1;
		auto file = write_file(kind_campaign, sections, h);
		read_file(file.data(), file.size(), kind_campaign);       // the writer re-reads its own output (1.6 step 1)
		return file;
	}

	std::vector<std::uint8_t> stream_to_new(const std::vector<std::uint8_t>& stream, const TablesData& t, const NewFileContext& context,
		ConvertState& state, const NativeLayout& native)
	{
		validate_native_tables(t);
		// the whole slot image or the payload alone, of the layout the caller selected (never inferred from the size)
		std::vector<std::uint8_t> image;
		if (stream.size() == native.payload)
		{
			image.assign(old_envelope, 0);
			std::memcpy(image.data(), context.description.data(), std::min<std::size_t>(context.description.size(), 0x7f));
			const u32 plen = native.payload;
			std::memcpy(image.data() + 0x80, &plen, 4);
			image.insert(image.end(), stream.begin(), stream.end());
		}
		else
		{
			image = stream;
		}
		if (!t.build) fail("the saver needs build tables");
		if (context.origin > 2) fail("unsupported saver origin");
		const auto old = read_slot(image.data(), image.size(), native);
		const auto& T = t.build->T;
		Conv c{t, state, T};
		Plan plan;
		plan.converter = false;
		plan.context = &context;
		plan.eras_ok.push_back(&T);
		Hash digest{};
		if (context.has_conversation_digest) std::memcpy(digest.data(), context.conversation_digest, 32);
		plan.digest = &digest;
		const auto description = context.description.empty() ? cstr(reinterpret_cast<const char*>(old.description_raw), 0x80) : context.description;
		auto sections = translate(c, old, plan, std::string(), image, context.slot, context.saved_at, context.writer, context.mods, description);
		if (state.dropped.zone_records || state.dropped.world_items || state.dropped.zone_vars || state.dropped.other || state.dropped.talents || state.dropped.states)
			fail("native save translation would drop state; original retained");
		add_keys_section(sections, t);
		retain_sections(sections, context.retained_file, kind_campaign, native);
		HeaderIn h;
		h.required_features = native.feature_bits;          // the Phase A marker: the writer's layout decides (E10A2: bit 0)
		h.optional_features = context.optional_features;
		h.writer_version = context.writer_version;
		h.content_version = t.catalogue.content_version;
		h.catalogue_revision = t.catalogue.revision;
		h.campaign_serial = context.campaign_serial;
		h.save_sequence = context.save_sequence;
		auto file = write_file(kind_campaign, sections, h);
		read_file(file.data(), file.size(), kind_campaign);
		return file;
	}

	// ---- the profile file (kind 2): META, KEYS, PRFO, PRFS, PRFP over settings.dat's 0x228-byte block ----------
	namespace
	{
		std::vector<std::uint8_t> profile_file(const OldProfile& p, const TablesData& t, const EraTables& era, const bool converter,
			const std::string& description, u8 slot, std::uint64_t saved_at, const std::string& writer, const std::vector<std::string>& mods,
			const HeaderIn& header, ConvertState& state, const SectionOut* legacy = nullptr, const std::vector<u8>& retained = {})
		{
			Conv c{t, state, era};
			std::vector<SectionOut> sections;
			Value mods_v = L();
			for (const auto& m : mods) mods_v.push(Tx(m));
			sections.push_back({"META", 1, 0, section_required, Value::dict({{"saved_at", I(static_cast<std::int64_t>(saved_at))},
				{"description", Tx(description)}, {"origin", I(converter ? 1 : 0)}, {"zone", I(0)}, {"act", I(0)}, {"difficulty", I(0)},
				{"play_time", R(0.0)}, {"writer", Tx(writer)}, {"slot", I(slot)}, {"mods", std::move(mods_v)}})});
			for (auto& sec : profile_sections(c, p, era)) sections.push_back(std::move(sec));
			if (legacy) sections.push_back(*legacy);
			add_keys_section(sections, t);
			retain_sections(sections, retained, kind_profile);
			auto file = write_file(kind_profile, sections, header);
			read_file(file.data(), file.size(), kind_profile);
			return file;
		}
	}

	std::vector<std::uint8_t> convert_profile(const std::vector<std::uint8_t>& old_bytes, const TablesData& t, const ConvertOptions& options,
		ConvertState& state)
	{
		std::string description;
		const auto p = read_profile_file(old_bytes.data(), old_bytes.size(), description);
		HeaderIn h;
		h.writer_version = options.writer_version;
		h.content_version = static_cast<u32>(t.reference.cv);
		h.catalogue_revision = t.catalogue.revision;
		h.campaign_serial = 0;
		h.save_sequence = 0;
		const auto digest = sha1(old_bytes.data(), old_bytes.size());
		const SectionOut lgcy{"LGCY", 1, 0, section_keep, Value::dict({{"original_name", Tx(options.original_name)},
			{"original_size", I(old_bytes.size())}, {"sha1", Value::bytes(digest.data(), 20)}, {"start_era", I(t.reference.cv)},
			{"writer_era", I(t.reference.cv)}, {"def_count_byte", I(0)}, {"zone_records_dropped", I(0)}, {"world_items_dropped", I(0)},
			{"zone_vars_dropped", I(0)}, {"other_dropped", I(0)}, {"notes", L()}})};
		return profile_file(p, t, t.reference, true, description, options.slot, options.fixed_saved_at ? options.saved_at : now_filetime(),
			options.writer, {}, h, state, &lgcy);
	}

	std::vector<std::uint8_t> profile_to_new(const std::vector<std::uint8_t>& old_stream, const TablesData& t, const NewFileContext& context,
		ConvertState& state)
	{
		validate_native_tables(t);
		if (!t.build) fail("the saver's direction needs the build");
		std::vector<std::uint8_t> image;
		if (old_stream.size() == old_profile_payload)
		{
			image.assign(old_envelope, 0);
			std::memcpy(image.data(), context.description.data(), std::min<std::size_t>(context.description.size(), 0x7f));
			const u32 plen = old_profile_payload;
			std::memcpy(image.data() + 0x80, &plen, 4);
			image.insert(image.end(), old_stream.begin(), old_stream.end());
		}
		else
		{
			image = old_stream;
		}
		std::string description;
		const auto p = read_profile_file(image.data(), image.size(), description);
		if (!context.description.empty()) description = context.description;
		HeaderIn h;
		h.writer_version = context.writer_version;
		h.content_version = t.catalogue.content_version;
		h.catalogue_revision = t.catalogue.revision;
		h.campaign_serial = 0;
		h.save_sequence = 0;
		return profile_file(p, t, t.build->T, false, description, context.slot, context.saved_at, context.writer, context.mods, h, state, nullptr, context.retained_file);
	}
}
