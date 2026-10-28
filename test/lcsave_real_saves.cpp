// lcsave_real_saves: the W2 live-save gate G3 offline (docs/limits-wave2.md section 10, G3 steps a-e), on real saves
// and a real build's tables. No engine, no game: the library and the engine side's header-only stream walkers, in the
// order the DLL's load and write paths call them (save_format.cpp read_lc / write_lc).
//
//   lcsave_real_saves --list FILE --data BUILD --registry FILE --out DIR
//
// FILE lists "<id>\t<path>" lines (one per unique file). For each file it writes <out>/<id>.json (the verdicts and the
// census numbers) and the canonical dumps <out>/<id>.orig.json (the file as read), .w2.json (the W2 resave) and
// .e10a2.json (the E10A2 resave) for the dump comparison under the G1 difference list (done outside, by a script that
// compares the JSON trees).
// A retail slot file (.save) and settings.dat are first imported (the one-time converter, RETAIL), as the game does.

#include "internal.hpp"
#include "lcsave.hpp"
#include "../src/save_format_rules.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace lcsave;
using namespace lcsave::detail;
namespace R = save_format_rules;

namespace
{
	std::vector<std::uint8_t> read_all(const fs::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}
	void write_text(const fs::path& p, const std::string& s)
	{
		std::ofstream(p, std::ios::binary).write(s.data(), static_cast<std::streamsize>(s.size()));
	}
	std::string esc(const std::string& s)
	{
		std::string o = "\"";
		for (const unsigned char c : s)
		{
			if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
			else if (c < 0x20 || c >= 0x7f) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
			else o += static_cast<char>(c);
		}
		return o + "\"";
	}

	// a flat JSON object, keys in insertion order
	struct Obj
	{
		std::ostringstream s;
		bool first = true;
		Obj& raw(const char* k, const std::string& v) { s << (first ? "{" : ", ") << esc(k) << ": " << v; first = false; return *this; }
		Obj& b(const char* k, bool v) { return raw(k, v ? "true" : "false"); }
		Obj& n(const char* k, long long v) { return raw(k, std::to_string(v)); }
		Obj& t(const char* k, const std::string& v) { return raw(k, esc(v)); }
		std::string str() const { return s.str() + (first ? "{}" : "}"); }
	};

	struct Census
	{
		long long instances = 0, attached = 0, world = 0, max_world_handle = -1;
		long long zone_records = 0, max_states = 0, max_ordinals = 0, max_ordinal_index = -1, max_dynamic = 0, max_smart = 0;
		std::string per_record;     // "states/ordinals/dynamic/smart" per record
		long long max_objective_index = -1, objectives_nonzero = 0;
		long long def_count = 0, max_def_index = -1, max_def_state_index = -1;
		long long max_enhancement_record = -1;
		long long talents = 0, max_talent_id = -1, max_hero_position = -1, max_shared_id = -1, max_heap = 0;
		long long non_integral_positions = 0, out_of_range_positions = 0;
	};

	Census census(const OldSlot& m, const NativeLayout& L)
	{
		Census c;
		const auto d = model_demand(m);
		c.instances = d.instances; c.attached = d.attached_enhancements; c.world = d.world_items;
		for (const auto& w : m.world) c.max_world_handle = std::max<long long>(c.max_world_handle, w.slot);
		c.zone_records = static_cast<long long>(m.zones.size());
		for (const auto& z : m.zones)
		{
			long long ord = 0;
			for (std::size_t k = 0; k < z.tail.ordinal_map.size(); ++k)
			{
				if (z.tail.ordinal_map[k]) { ++ord; c.max_ordinal_index = std::max<long long>(c.max_ordinal_index, static_cast<long long>(k)); }
			}
			c.max_states = std::max<long long>(c.max_states, z.tail.state_count);
			c.max_ordinals = std::max(c.max_ordinals, ord);
			c.max_dynamic = std::max<long long>(c.max_dynamic, z.tail.dynamic_count);
			c.max_smart = std::max<long long>(c.max_smart, z.tail.smart_count);
			if (!c.per_record.empty()) c.per_record += " ";
			c.per_record += std::to_string(z.tail.state_count) + "/" + std::to_string(ord) + "/" + std::to_string(z.tail.dynamic_count) + "/" + std::to_string(z.tail.smart_count);
			for (u32 k = 1; k < z.tail.state_count && k < z.states.size(); ++k)
			{
				for (const float p : z.states[k].position)
				{
					if (p != std::floor(p)) ++c.non_integral_positions;
					if (p < -32767.0f || p > 32767.0f) ++c.out_of_range_positions;
				}
			}
		}
		auto objectives = [&](const OldSnapshot& s)
		{
			for (u32 k = 0; k < L.snapshot_objectives; ++k)
			{
				if (s.objectives[k]) { ++c.objectives_nonzero; c.max_objective_index = std::max<long long>(c.max_objective_index, k); }
			}
		};
		objectives(m.mission);
		for (const auto& s : m.side_frames) objectives(s);
		c.def_count = m.items.def_count;
		for (u32 k = 0; k < L.definitions; ++k) if (m.items.def_state[2 * k] || m.items.def_state[2 * k + 1]) c.max_def_state_index = k;
		auto inst = [&](const OldInstance& i)
		{
			c.max_def_index = std::max<long long>(c.max_def_index, i.head.definition);
			for (const auto& l : i.lists) for (const auto& e : l) c.max_enhancement_record = std::max<long long>(c.max_enhancement_record, e.record);
		};
		for (const auto& i : m.carried) inst(i);
		for (const auto& w : m.world) inst(w.instance);
		auto attrs = [&](const OldAttributes& a)
		{
			c.max_heap = std::max<long long>(c.max_heap, a.tail.count);
			for (u32 k = 0; k < a.tail.count && k < old_talent_entries; ++k)
			{
				const u32 id = a.talents[k].id;
				++c.talents;
				c.max_talent_id = std::max<long long>(c.max_talent_id, id);
				if (id / L.talent_multiplier == 0) c.max_shared_id = std::max<long long>(c.max_shared_id, id);
				else c.max_hero_position = std::max<long long>(c.max_hero_position, id % L.talent_multiplier);
			}
		};
		for (const auto& h : m.heroes) { attrs(h.attributes); for (const auto& i : h.equipment.equipped) inst(i); }
		for (const auto& r : m.restore) { attrs(r.attributes); for (const auto& i : r.equipment.equipped) inst(i); }
		return c;
	}

	void put(Obj& o, const Census& c)
	{
		o.n("instances", c.instances).n("attached", c.attached).n("world_items", c.world).n("max_world_handle", c.max_world_handle)
			.n("zone_records", c.zone_records).n("max_states", c.max_states).n("max_ordinals", c.max_ordinals).n("max_ordinal_index", c.max_ordinal_index)
			.n("max_dynamic", c.max_dynamic).n("max_smart", c.max_smart).t("records", c.per_record)
			.n("max_objective_index", c.max_objective_index).n("objectives_nonzero", c.objectives_nonzero)
			.n("def_count", c.def_count).n("max_def_index", c.max_def_index).n("max_def_state_index", c.max_def_state_index)
			.n("max_enhancement_record", c.max_enhancement_record)
			.n("talents", c.talents).n("max_talent_id", c.max_talent_id).n("max_hero_position", c.max_hero_position).n("max_shared_id", c.max_shared_id)
			.n("max_heap", c.max_heap).n("non_integral_positions", c.non_integral_positions).n("out_of_range_positions", c.out_of_range_positions);
	}

	// the DLL's add_missing_talents on a copy: shared talents only, through the profile's heap walker
	std::string insert_missing(std::vector<std::uint8_t> image, const Tables& tables, const NativeLayout& L, const R::native_profile& P, long long& added, long long& adds_total)
	{
		std::vector<TalentAdd> adds;
		std::string error;
		if (!missing_talents(image, tables, adds, error, L)) return "missing_talents: " + error;
		adds_total = static_cast<long long>(adds.size());
		if (adds.empty()) return {};
		const auto heaps = R::hero_heaps(image.data(), image.size(), P);
		if (!heaps) return "hero_heaps does not walk the stream";
		for (const auto& a : adds)
		{
			if (!a.shared) continue;
			if (a.hero_block >= heaps->size()) return "talent names hero block " + std::to_string(a.hero_block) + " of " + std::to_string(heaps->size());
			const auto r = R::insert_talent(image.data() + (*heaps)[a.hero_block].heap, a.talent_id);
			if (r == R::heap_insert::inserted) ++added;
			else if (r == R::heap_insert::full) return "heap full for talent " + std::to_string(a.talent_id);
		}
		return {};
	}

	// the canonical dump of a campaign (the library's dump); a profile (which the library's dump does not take):
	// the header numbers and every section's decoded data, as JSON with sorted keys
	Value hex_bytes(Value v)
	{
		if (v.type == Value::Type::bytes)
		{
			std::string h = "hex:";
			for (const unsigned char c : v.s) { char b[3]; std::snprintf(b, sizeof b, "%02x", c); h += b; }
			return Value::text(h);
		}
		for (auto& x : v.l) x = hex_bytes(std::move(x));
		for (auto& x : v.d) x.value = hex_bytes(std::move(x.value));
		return v;
	}
	std::string any_dump(const std::vector<std::uint8_t>& file, const Tables& tables, std::string& error)
	{
		if (file.size() > 8 && std::memcmp(file.data(), new_magic, 8) == 0 && file[8] == kind_profile)
		{
			try
			{
				const auto f = read_file(file.data(), file.size(), kind_profile);
				Value sections = Value::dict();
				for (const auto& tag : f.order)
				{
					const auto& sec = f.sections.at(tag);
					sections.set(tag.c_str(), hex_bytes(sec.known ? sec.data : Value::bytes(sec.payload)));
				}
				const auto& h = f.header;
				Value header = Value::dict({{"required_features", Value::integer(h.required_features)}, {"optional_features", Value::integer(h.optional_features)},
					{"writer_version", Value::integer(h.writer_version)}, {"content_version", Value::integer(h.content_version)},
					{"catalogue_revision", Value::integer(h.catalogue_revision)}, {"campaign_serial", Value::integer(static_cast<std::int64_t>(h.campaign_serial))},
					{"save_sequence", Value::integer(h.save_sequence)}, {"body_crc", Value::integer(h.body_crc)}, {"header_crc", Value::integer(h.header_crc)}});
				return to_json(Value::dict({{"header", header}, {"sections", sections}}));
			}
			catch (const Error& x) { error = x.message; return {}; }
		}
		return dump(file, tables, error);
	}
	std::string any_dump(const std::vector<std::uint8_t>& file, const Tables& tables) { std::string e; return any_dump(file, tables, e); }

	struct Paths { fs::path out; std::string id; };

	// which parts of two native models differ (to say what a non-identical round trip changed)
	template <class T> bool pod_eq(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
	bool inst_eq(const OldInstance& a, const OldInstance& b)
	{
		if (!pod_eq(a.head, b.head)) return false;
		for (int k = 0; k < 3; ++k)
		{
			if (a.lists[k].size() != b.lists[k].size()) return false;
			for (std::size_t j = 0; j < a.lists[k].size(); ++j) if (!pod_eq(a.lists[k][j], b.lists[k][j])) return false;
		}
		return true;
	}
	bool insts_eq(const std::vector<OldInstance>& a, const std::vector<OldInstance>& b)
	{
		if (a.size() != b.size()) return false;
		for (std::size_t k = 0; k < a.size(); ++k) if (!inst_eq(a[k], b[k])) return false;
		return true;
	}
	bool attrs_eq(const OldAttributes& a, const OldAttributes& b)
	{
		return pod_eq(a.scalars, b.scalars) && pod_eq(a.tail, b.tail) && std::memcmp(a.talents, b.talents, sizeof a.talents) == 0;
	}
	std::string model_diff(const OldSlot& a, const OldSlot& b)
	{
		std::vector<std::string> d;
		if (std::memcmp(a.description_raw, b.description_raw, 0x80)) d.push_back("description");
		if (!pod_eq(a.items, b.items)) d.push_back("item_head");
		if (!insts_eq(a.carried, b.carried)) d.push_back("carried");
		bool world = a.world.size() == b.world.size();
		for (std::size_t k = 0; world && k < a.world.size(); ++k) world = a.world[k].slot == b.world[k].slot && inst_eq(a.world[k].instance, b.world[k].instance);
		if (!world) d.push_back("world");
		bool heroes = a.heroes.size() == b.heroes.size();
		for (std::size_t k = 0; heroes && k < a.heroes.size(); ++k)
		{
			const auto& x = a.heroes[k]; const auto& y = b.heroes[k];
			if (!attrs_eq(x.attributes, y.attributes)) d.push_back("hero" + std::to_string(k) + ".attributes");
			if (!insts_eq(x.equipment.equipped, y.equipment.equipped) || !pod_eq(x.equipment.tail, y.equipment.tail)) d.push_back("hero" + std::to_string(k) + ".equipment");
			if (!pod_eq(x.powers, y.powers) || !pod_eq(x.costume, y.costume)) d.push_back("hero" + std::to_string(k) + ".powers/costume");
		}
		if (!heroes) d.push_back("heroes(count)");
		if (std::memcmp(a.unlocked, b.unlocked, sizeof a.unlocked) || a.unlocked_count != b.unlocked_count) d.push_back("unlocked");
		bool restore = a.restore_flag == b.restore_flag && a.restore.size() == b.restore.size();
		for (std::size_t k = 0; restore && k < a.restore.size(); ++k)
			restore = attrs_eq(a.restore[k].attributes, b.restore[k].attributes) && insts_eq(a.restore[k].equipment.equipped, b.restore[k].equipment.equipped) && pod_eq(a.restore[k].equipment.tail, b.restore[k].equipment.tail);
		if (!restore) d.push_back("restore");
		if (!pod_eq(a.game, b.game)) d.push_back("game");
		if (std::memcmp(a.side_frames, b.side_frames, sizeof a.side_frames) || !pod_eq(a.side, b.side)) d.push_back("side_frames");
		bool courses = a.courses.size() == b.courses.size() && a.course_residue == b.course_residue && std::memcmp(a.grades, b.grades, sizeof a.grades) == 0;
		for (std::size_t k = 0; courses && k < a.courses.size(); ++k) courses = pod_eq(a.courses[k], b.courses[k]);
		if (!courses) d.push_back("danger_room");
		if (!pod_eq(a.review, b.review)) d.push_back("review");
		if (a.zone_strings->generation != b.zone_strings->generation || a.zone_strings->offsets != b.zone_strings->offsets || a.zone_strings->used != b.zone_strings->used || a.zone_strings->arena != b.zone_strings->arena) d.push_back("zone_strings");
		if (a.zones.size() != b.zones.size()) d.push_back("zones(count " + std::to_string(a.zones.size()) + " vs " + std::to_string(b.zones.size()) + ")");
		for (std::size_t k = 0; k < a.zones.size() && k < b.zones.size(); ++k)
		{
			const auto& x = a.zones[k]; const auto& y = b.zones[k];
			const std::string z = "zone" + std::to_string(k);
			if (x.name_handle != y.name_handle) d.push_back(z + ".name");
			bool st = x.states.size() == y.states.size();
			for (std::size_t j = 0; st && j < x.states.size(); ++j) st = pod_eq(x.states[j], y.states[j]);
			if (!st) d.push_back(z + ".states");
			if (x.tail.state_count != y.tail.state_count) d.push_back(z + ".state_count");
			if (x.tail.ordinal_map != y.tail.ordinal_map) d.push_back(z + ".ordinals");
			if (x.tail.dynamic != y.tail.dynamic || x.tail.dynamic_count != y.tail.dynamic_count) d.push_back(z + ".dynamic");
			if (x.tail.smart != y.tail.smart || x.tail.smart_count != y.tail.smart_count) d.push_back(z + ".smart");
			if (std::memcmp(x.tail.fog, y.tail.fog, sizeof x.tail.fog)) d.push_back(z + ".fog");
			if (std::memcmp(x.tail.conversation, y.tail.conversation, sizeof x.tail.conversation)) d.push_back(z + ".conversation");
			if (x.tail.time_stamp != y.tail.time_stamp || x.tail.flag != y.tail.flag) d.push_back(z + ".time/flag");
			if (x.tail.pad_4804 != y.tail.pad_4804 || x.tail.pad_4a0c != y.tail.pad_4a0c || x.tail.pad_4c68 != y.tail.pad_4c68 || x.tail.pad_5270 != y.tail.pad_5270 || std::memcmp(x.tail.pad_55dd, y.tail.pad_55dd, 3)) d.push_back(z + ".pads");
		}
		if (!pod_eq(a.mission, b.mission)) d.push_back("mission");
		if (!pod_eq(a.profile.head, b.profile.head) || std::memcmp(a.profile.high_scores, b.profile.high_scores, sizeof a.profile.high_scores) || !pod_eq(a.profile.tail, b.profile.tail)) d.push_back("profile");
		if (std::memcmp(a.zone_vars, b.zone_vars, sizeof a.zone_vars)) d.push_back("zone_vars");
		if (std::memcmp(a.game_vars, b.game_vars, sizeof a.game_vars)) d.push_back("game_vars");
		if (a.town_centres != b.town_centres) d.push_back("town_centres");
		if (a.slack != b.slack) d.push_back("slack");
		std::string out;
		for (const auto& x : d) out += (out.empty() ? "" : ",") + x;
		return out.empty() ? "(model equal; bytes differ outside the model)" : out;
	}
	std::string byte_diff(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b)
	{
		if (a.size() != b.size()) return "size " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
		std::size_t n = 0, first = 0;
		for (std::size_t k = 0; k < a.size(); ++k) if (a[k] != b[k]) { if (!n) first = k; ++n; }
		char t[64];
		std::snprintf(t, sizeof t, "%zu bytes from 0x%zx", n, first);
		return t;
	}

	// the DLL's load (read_lc) and the next write (write_lc) of a loaded campaign on one layout, twice:
	// S1 = load(file), L1 = save(S1), S2 = load(L1), L2 = save(S2), S3 = load(L2)
	struct Cycle
	{
		Result s1, l1, s2, l2, s3;
		MigrationReport rep1, rep2;
		std::vector<ZoneCaptureMetadata> md1;
	};
	NewFileContext save_context(const FileIn& f, const std::vector<std::uint8_t>& retained, const std::vector<ZoneCaptureMetadata>& md)
	{
		NewFileContext ctx;
		ctx.saved_at = 0x01dc000000000000ull;
		ctx.campaign_serial = f.header.campaign_serial;
		ctx.save_sequence = f.header.save_sequence + 1;
		ctx.retained_file = retained;
		ctx.require_capture_metadata = true;
		ctx.zone_captures = md;
		ctx.optional_features = f.header.optional_features & ~optional_keys_present;
		const auto& meta = f.data("META");
		for (const auto& m : meta["mods"].as_list()) ctx.mods.push_back(m.as_text());
		ctx.slot = static_cast<std::uint8_t>(meta["slot"].as_int());
		ctx.origin = static_cast<std::uint8_t>(meta["origin"].as_int() == 2 ? 2 : 0);
		return ctx;
	}
	Cycle cycle(const std::vector<std::uint8_t>& file, const FileIn& f, const Tables& tables, const TypedRegistry& reg, const NativeLayout& L, const char* tag,
		Obj& o, std::vector<std::string>& failures)
	{
		auto fail = [&](const std::string& s) { failures.push_back(std::string(tag) + " " + s); };
		const std::string T = tag;
		Cycle c;
		std::string error;
		c.s1 = new_to_old_stream(file, tables, L, &reg, &c.rep1);
		o.b((T + "_load").c_str(), c.s1.ok).t((T + "_load_error").c_str(), c.s1.error).t((T + "_migration").c_str(), c.rep1.line()).t((T + "_load_ledger").c_str(), c.s1.ledger);
		if (!c.s1.ok) { fail("load: " + c.s1.error); return c; }
		if (c.s1.bytes.size() != L.slot_size) fail("stream size " + std::to_string(c.s1.bytes.size()));
		if (!loaded_zone_metadata(file, tables, c.md1, error, L, &reg)) { fail("loaded_zone_metadata: " + error); return c; }
		c.l1 = old_stream_to_new(c.s1.bytes, tables, save_context(f, file, c.md1), L);
		o.b((T + "_save").c_str(), c.l1.ok).t((T + "_save_error").c_str(), c.l1.error).t((T + "_save_ledger").c_str(), c.l1.ledger);
		if (!c.l1.ok) { fail("save: " + c.l1.error); return c; }
		c.s2 = new_to_old_stream(c.l1.bytes, tables, L, &reg, &c.rep2);
		if (!c.s2.ok) { fail("reload of its own save: " + c.s2.error); return c; }
		std::vector<ZoneCaptureMetadata> md2;
		if (!loaded_zone_metadata(c.l1.bytes, tables, md2, error, L, &reg)) { fail("loaded_zone_metadata of its own save: " + error); return c; }
		const auto f1 = read_file(c.l1.bytes.data(), c.l1.bytes.size(), kind_campaign);
		auto ctx2 = save_context(f1, c.l1.bytes, md2);
		ctx2.save_sequence = f1.header.save_sequence;   // the same header as L1: L2 must equal L1 byte for byte
		c.l2 = old_stream_to_new(c.s2.bytes, tables, ctx2, L);
		if (!c.l2.ok) { fail("second save: " + c.l2.error); return c; }
		c.s3 = new_to_old_stream(c.l2.bytes, tables, L, &reg);
		if (!c.s3.ok) { fail("third load: " + c.s3.error); return c; }
		const bool fixed = c.s2.bytes == c.s3.bytes && c.l1.bytes == c.l2.bytes;
		const bool first = c.s1.bytes == c.s2.bytes;
		o.b((T + "_fixed_point").c_str(), fixed).b((T + "_first_cycle_exact").c_str(), first).t((T + "_second_validation").c_str(), c.rep2.line());
		if (!fixed) fail("LC -> native -> LC is not a fixed point after one cycle (S2/S3 " + byte_diff(c.s2.bytes, c.s3.bytes) + ", L1/L2 " + byte_diff(c.l1.bytes, c.l2.bytes) + ")");
		if (c.rep2.normalized_to_null) fail("the reload of its own save normalised typed values: " + c.rep2.line());
		if (!first)
		{
			o.t((T + "_first_cycle_diff").c_str(), model_diff(read_slot(c.s1.bytes.data(), c.s1.bytes.size(), L), read_slot(c.s2.bytes.data(), c.s2.bytes.size(), L)) + "; " + byte_diff(c.s1.bytes, c.s2.bytes));
		}
		return c;
	}

	// --w2-only (limits wave 2 + the 0.4.2 economy): the build's tables exceed the E10A2 / retail budgets (185 item
	// definitions > 160), so a save of that build loads only through W2; the E10A2 half is not run (W2 on and off is
	// gated on a W2-off build of the same tree).
	bool w2_only = false;
	// one campaign LC file through the W2 and E10A2 loaders and savers
	void campaign(const std::vector<std::uint8_t>& file, const Tables& tables, const TypedRegistry& reg, const Paths& P, Obj& o, std::vector<std::string>& failures)
	{
		const auto& w = w2_layout;
		const auto& e = e10a2_layout;
		auto fail = [&](const std::string& s) { failures.push_back(s); };
		FileIn f;
		try { f = read_file(file.data(), file.size(), kind_campaign); }
		catch (const Error& x) { fail("container: " + x.message); return; }
		o.n("required_features", f.header.required_features).n("optional_features", f.header.optional_features)
			.n("writer_version", f.header.writer_version).n("content_version", f.header.content_version).n("catalogue_revision", f.header.catalogue_revision)
			.n("save_sequence", f.header.save_sequence);
		if (f.header.required_features & feature_native_w2) fail("the real save already carries bit 1");
		// the slot list (rules.hpp inspect) as each build reads it
		R::lc_known_required_features = w.feature_bits;
		const auto iw = R::inspect(file.data(), file.size(), R::kind_campaign);
		R::lc_known_required_features = e.feature_bits;
		const auto ie = R::inspect(file.data(), file.size(), R::kind_campaign);
		R::lc_known_required_features = 0;
		const auto ir = R::inspect(file.data(), file.size(), R::kind_campaign);
		o.b("slot_list_w2_ok", iw.status == R::file_status::ok).b("slot_list_e10a2_ok", ie.status == R::file_status::ok)
			.b("slot_list_retail_ok", ir.status == R::file_status::ok).t("slot_list_retail_why", ir.why);
		if (iw.status != R::file_status::ok) fail("W2 slot list: " + iw.why);
		if (!w2_only && ie.status != R::file_status::ok) fail("E10A2 slot list: " + ie.why);
		const auto dw = decide_features(file, w);
		const auto de = decide_features(file, e);
		const auto dr = decide_features(file, retail_layout);
		o.b("decide_w2", dw.ok).b("decide_w2_migrate", dw.migrate_typed_fields).b("decide_e10a2", de.ok).b("decide_retail", dr.ok).t("decide_retail_reason", dr.reason);
		if (!dw.ok) fail("W2 feature decision: " + dw.reason);
		if (!w2_only && !de.ok) fail("E10A2 feature decision: " + de.reason);
		// ---- W2 on and W2 off (E10A2, the shipping capacity layout): load, save, reload, save, reload
		const auto cw = cycle(file, f, tables, reg, w, "w2", o, failures);
		const auto ce = w2_only ? Cycle{} : cycle(file, f, tables, reg, e, "e10a2", o, failures);
		if (!w2_only && cw.s1.ok != ce.s1.ok) fail("W2 and E10A2 loaders disagree");
		if (cw.s1.ok)
		{
			const auto& sw = cw.s1;
			std::string error;
			NativeLoadDemand demand;
			if (!native_load_demand(sw.bytes, demand, error, w)) fail("W2 native_load_demand: " + error);
			else if (const auto why = demand_refusal(demand, w); !why.empty()) fail("W2 pools: " + why);
			if (!validate_native_stream(sw.bytes, tables, error, w)) fail("W2 validate_native_stream: " + error);
			std::vector<ZoneCaptureMetadata> nw;
			if (!native_zone_metadata(sw.bytes, tables, nw, error, w)) fail("W2 native_zone_metadata: " + error);
			long long added = 0, adds = 0;
			if (const auto why = insert_missing(sw.bytes, tables, w, R::w2_profile, added, adds); !why.empty()) fail("W2 missing talents: " + why);
			o.n("w2_missing_talents", adds).n("w2_shared_talents_inserted", added);
			const auto hw = R::hero_heaps(sw.bytes.data(), sw.bytes.size(), R::w2_profile);
			const auto pw = R::hero_points_records(sw.bytes.data(), sw.bytes.size(), R::w2_profile);
			const auto tw = R::old_play_time(sw.bytes.data(), sw.bytes.size(), R::w2_profile);
			const auto wm = read_slot(sw.bytes.data(), sw.bytes.size(), w);
			if (!hw || hw->size() != wm.heroes.size()) fail("W2 hero_heaps walk");
			if (!pw) fail("W2 hero_points_records walk");
			if (!tw || *tw != wm.game.play_time) fail("W2 old_play_time");
			const auto c = census(wm, w);
			put(o, c);
			if (c.non_integral_positions || c.out_of_range_positions) fail("a state position the pre-W2 layout cannot hold");
			if (ce.s1.ok)
			{
				const auto c2 = census(read_slot(ce.s1.bytes.data(), ce.s1.bytes.size(), e), e);
				o.n("e10a2_talents", c2.talents).n("e10a2_instances", c2.instances).n("e10a2_zone_records", c2.zone_records).n("e10a2_max_states", c2.max_states);
				if (c2.instances != c.instances || c2.attached != c.attached || c2.world != c.world || c2.zone_records != c.zone_records || c2.max_states != c.max_states ||
					c2.max_ordinals != c.max_ordinals || c2.max_dynamic != c.max_dynamic || c2.max_smart != c.max_smart || c2.talents != c.talents ||
					c2.max_objective_index != c.max_objective_index || c2.max_def_index != c.max_def_index || c2.max_enhancement_record != c.max_enhancement_record)
					fail("the W2 and E10A2 streams of one file hold different content");
				if (cw.rep1.line() != ce.rep1.line()) fail("migration differs: W2 " + cw.rep1.line() + " vs E10A2 " + ce.rep1.line());
			}
		}
		if (cw.l1.ok)
		{
			const auto& lw = cw.l1;
			const auto fw = read_file(lw.bytes.data(), lw.bytes.size(), kind_campaign);
			o.n("w2_resave_required_features", fw.header.required_features);
			if (fw.header.required_features != feature_entity_layout_contiguous10) fail("W2 resave required_features " + std::to_string(fw.header.required_features) + " (" + lw.ledger + ")");
			const auto probe = representable(lw.bytes, tables, e);
			o.t("e10a2_dry_run", probe.empty() ? "representable" : probe);
			if (!w2_only && !probe.empty()) fail("E10A2 dry run: " + probe);
			write_text(P.out / (P.id + ".w2.json"), dump(lw.bytes, tables));
			// the W2 save loads on the W2-off build to the stream the W2-off build makes of its own save
			const auto we = w2_only ? decltype(new_to_old_stream(lw.bytes, tables, e, &reg)){} : new_to_old_stream(lw.bytes, tables, e, &reg);
			if (!w2_only) o.b("w2_resave_on_e10a2", we.ok);
			if (w2_only) {}
			else if (!we.ok) fail("the W2 resave on E10A2: " + we.error);
			else if (ce.s2.ok && we.bytes != ce.s2.bytes) fail("the W2 resave loads on E10A2 to another stream than the E10A2 resave (" + byte_diff(we.bytes, ce.s2.bytes) + ")");
			if (ce.l1.ok)
			{
				o.b("w2_e10a2_resave_bytes_equal", ce.l1.bytes == lw.bytes);
				write_text(P.out / (P.id + ".e10a2.json"), dump(ce.l1.bytes, tables));
				// the E10A2 save loads on W2 to the W2 reload
				const auto ew = new_to_old_stream(ce.l1.bytes, tables, w, &reg);
				if (!ew.ok || (cw.s2.ok && ew.bytes != cw.s2.bytes)) fail("the E10A2 resave on W2: " + (ew.ok ? byte_diff(ew.bytes, cw.s2.bytes) : ew.error));
			}
		}
		// ---- W2 off, no capacity (RETAIL): the old reader's verdict (a feature-1 file is refused by design)
		const auto rr = new_to_old_stream(file, tables);
		o.b("retail_load", rr.ok).t("retail_load_error", rr.error);
	}

	void profile(const std::vector<std::uint8_t>& file, const Tables& tables, const Paths& P, Obj& o, std::vector<std::string>& failures)
	{
		auto fail = [&](const std::string& s) { failures.push_back(s); };
		const auto pw = new_to_old_profile(file, tables, w2_layout);
		const auto pe = new_to_old_profile(file, tables, e10a2_layout);
		const auto pr = new_to_old_profile(file, tables);
		o.b("w2_load", pw.ok).t("w2_load_error", pw.error).b("e10a2_load", pe.ok).b("retail_load", pr.ok);
		if (!pw.ok) { fail("W2 profile load: " + pw.error); return; }
		if (!w2_only && (!pe.ok || pe.bytes != pw.bytes)) fail("E10A2 profile load differs: " + pe.error);
		if (!w2_only && (!pr.ok || pr.bytes != pw.bytes)) fail("retail profile load differs: " + pr.error);
		const auto f = read_file(file.data(), file.size(), kind_profile);
		o.n("required_features", f.header.required_features);
		NewFileContext ctx;
		ctx.saved_at = 0x01dc000000000000ull;
		ctx.retained_file = file;
		for (const auto& m : f.data("META")["mods"].as_list()) ctx.mods.push_back(m.as_text());
		ctx.optional_features = f.header.optional_features & ~optional_keys_present;
		const auto lw = old_profile_to_new(pw.bytes, tables, ctx, w2_layout);
		o.b("w2_save", lw.ok).t("w2_save_error", lw.error);
		if (!lw.ok) { fail("W2 profile save: " + lw.error); return; }
		const auto fw = read_file(lw.bytes.data(), lw.bytes.size(), kind_profile);
		o.n("w2_resave_required_features", fw.header.required_features);
		if (fw.header.required_features & feature_native_w2) fail("W2 profile resave sets bit 1");
		const auto again = new_to_old_profile(lw.bytes, tables, w2_layout);
		o.b("w2_idempotent", again.ok && again.bytes == pw.bytes);
		if (!(again.ok && again.bytes == pw.bytes)) fail("profile LC -> old -> LC -> old not byte-exact");
		const auto le = w2_only ? decltype(old_profile_to_new(pe.bytes, tables, ctx, e10a2_layout)){} : old_profile_to_new(pe.bytes, tables, ctx, e10a2_layout);
		if (w2_only) {}
		else if (!le.ok) fail("E10A2 profile save: " + le.error);
		else
		{
			o.b("w2_e10a2_resave_bytes_equal", le.bytes == lw.bytes);
			write_text(P.out / (P.id + ".e10a2.json"), any_dump(le.bytes, tables));
		}
		write_text(P.out / (P.id + ".w2.json"), any_dump(lw.bytes, tables));
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::map<std::string, std::string> args;
	for (int k = 1; k + 1 < argc; k += 2) args[argv[k]] = argv[k + 1];
	if (!args.count("--list") || !args.count("--data") || !args.count("--registry") || !args.count("--out"))
	{
		std::printf("usage: lcsave_real_saves --list FILE --data BUILD --registry FILE --out DIR [--w2-only 1] [--economy 1]\n");
		return 2;
	}
	w2_only = args.count("--w2-only") && args["--w2-only"] == "1";
	// --economy 1: a build with [Game] Xml1Economy=1 (rule 52 verified) migrates the rebuilt Danger Room rewards of old
	// saves (part 64); the DLL binds this verifier when the economy installs (lcsave_rebuilt_corpus does the same)
	if (args.count("--economy") && args["--economy"] == "1") { set_xml1_economy_verifier(+[] { return true; }); std::printf("economy: rule 52 verifier bound (rebuilt-reward migration on)\n"); }
	if (w2_only) std::printf("W2 only: the E10A2 / retail halves are not run (a --save-layout w2 build's tables)\n");
	Tables tables;
	std::string error;
	const auto build = fs::path(args["--data"]).wstring();
	if (!load_tables(build, tables, error)) { std::printf("FAIL: load_tables: %s\n", error.c_str()); return 1; }
	std::printf("tables: catalogue revision %u, content version %u, %u heroes, %u stats, %u items, %u objectives\n", tables.catalogue_revision(),
		tables.content_version(), tables.hero_count(), tables.stats_count(), tables.item_count(), tables.objective_count());
	if (!validate_talent_positions(tables, w2_layout, error)) std::printf("FAIL: W2 talent positions: %s\n", error.c_str());
	TypedRegistry reg;
	if (!load_typed_registry(read_all(args["--registry"]), reg, error)) { std::printf("FAIL: registry: %s\n", error.c_str()); return 1; }
	const fs::path out = args["--out"];
	fs::create_directories(out);
	std::ifstream list(args["--list"]);
	std::string line;
	int files = 0, failed = 0;
	while (std::getline(list, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto tab = line.find('\t');
		if (tab == std::string::npos) continue;
		const Paths P{out, line.substr(0, tab)};
		const auto text = line.substr(tab + 1);
		const fs::path path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
		const auto bytes = read_all(path);
		++files;
		Obj o;
		std::vector<std::string> failures;
		o.t("id", P.id).n("size", static_cast<long long>(bytes.size()));
		std::string kind;
		std::vector<std::uint8_t> lc;
		const bool is_lc = bytes.size() >= new_header_size && std::memcmp(bytes.data(), new_magic, 8) == 0;
		std::string derr;
		const auto d0 = any_dump(bytes, tables, derr);
		o.b("dump", !d0.empty()).t("dump_error", derr);
		if (d0.empty() && bytes.size() != old_envelope + old_profile_payload) failures.push_back("dump on the current build: " + derr);   // settings.dat: dumped as its import
		else write_text(out / (P.id + ".orig.json"), d0);
		if (is_lc)
		{
			kind = bytes[8] == kind_campaign ? "lc-campaign" : bytes[8] == kind_profile ? "lc-profile" : "lc-other";
			lc = bytes;
		}
		else if (bytes.size() == old_slot_size)
		{
			kind = "retail-slot";
			ConvertOptions opt;
			opt.original_name = path.filename().string();
			opt.fixed_saved_at = true;
			const auto r = convert_old_to_new(bytes, tables, opt);
			o.b("import", r.ok).t("import_error", r.error).t("import_ledger", r.ledger).n("import_parked", r.parked);
			if (!r.ok) failures.push_back("retail import: " + r.error);
			else lc = r.bytes;
		}
		else if (bytes.size() == old_envelope + old_profile_payload)
		{
			kind = "retail-settings";
			ConvertOptions opt;
			opt.original_name = "settings.dat";
			opt.fixed_saved_at = true;
			const auto r = convert_old_profile_to_new(bytes, tables, opt);
			o.b("import", r.ok).t("import_error", r.error);
			if (!r.ok) failures.push_back("settings import: " + r.error);
			else lc = r.bytes;
		}
		else kind = "unknown";
		o.t("kind", kind);
		if (!lc.empty())
		{
			if (!is_lc) write_text(out / (P.id + ".import.json"), any_dump(lc, tables));
			try
			{
				if (lc[8] == kind_campaign) campaign(lc, tables, reg, P, o, failures);
				else if (lc[8] == kind_profile) profile(lc, tables, P, o, failures);
				else failures.push_back("LC kind " + std::to_string(lc[8]));
			}
			catch (const std::exception& x) { failures.push_back(std::string("exception: ") + x.what()); }
			catch (const Error& x) { failures.push_back("library error: " + x.message); }
		}
		else if (kind == "unknown") failures.push_back("not a save of a known kind");
		std::string fl = "[";
		for (std::size_t k = 0; k < failures.size(); ++k) fl += (k ? ", " : "") + esc(failures[k]);
		o.raw("failures", fl + "]");
		write_text(out / (P.id + ".json"), o.str());
		if (!failures.empty()) ++failed;
		std::printf("%s %s %s%s\n", failures.empty() ? "pass" : "FAIL", P.id.c_str(), kind.c_str(), failures.empty() ? "" : (": " + failures[0]).c_str());
	}
	std::printf("real saves: %d files, %d failed\n", files, failed);
	return failed ? 1 : 0;
}
