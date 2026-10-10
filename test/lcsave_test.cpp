// lcsave_test: (a) the vectors of legends-classic tests/unit/test_lcsave.py (numbers copied, data invented in code),
// (b) with --corpus: convert / reverse / dump every saveslot*.save under a folder and compare with the Python
// reference's outputs next to the same relative path (<python>/<rel>.lcsave, .old.json, .new.json, .report.json),
// (c) a fuzz of the old-file and new-file readers (truncated and mutated inputs: an error, never a crash).
//
//   lcsave_test                                    vectors and fuzz (CI)
//   lcsave_test --corpus DIR --python DIR --data BUILD --binds BINDTREE [--out DIR] [--fuzz N]

#include "internal.hpp"
#include "lcsave.hpp"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace lcsave;
using namespace lcsave::detail;

namespace
{
	int failures = 0;
	void check(bool ok, const std::string& what)
	{
		if (!ok)
		{
			++failures;
			std::printf("FAIL: %s\n", what.c_str());
		}
	}

	std::vector<std::uint8_t> read_all(const fs::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), {});
	}

	// the reference wrote its dumps through a Windows text-mode file: CRLF line ends; json.dumps itself gives LF
	std::string read_text(const fs::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		std::string s((std::istreambuf_iterator<char>(f)), {});
		std::string out;
		out.reserve(s.size());
		for (std::size_t k = 0; k < s.size(); ++k)
		{
			if (!(s[k] == 13 && k + 1 < s.size() && s[k + 1] == 10)) out.push_back(s[k]);
		}
		return out;
	}

	void write_all(const fs::path& p, const void* data, std::size_t n)
	{
		fs::create_directories(p.parent_path());
		std::ofstream f(p, std::ios::binary);
		f.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
	}

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
	};

	// the two files are equal but for the KEYS documentation text: same header fields (but the length and CRCs), same
	// sections with identical payloads, and KEYS with the same ids and domains whose texts agree once the reference's
	// display text ('/'.join(fields)) is lower-cased and ours (the catalogue's canonical key) is unescaped
	bool equal_but_keys_text(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, std::string& why)
	{
		try
		{
			const auto fa = read_file(a.data(), a.size(), kind_campaign);
			const auto fb = read_file(b.data(), b.size(), kind_campaign);
			auto ha = fa.header, hb = fb.header;
			ha.file_length = hb.file_length = 0;
			ha.body_crc = hb.body_crc = 0;
			ha.header_crc = hb.header_crc = 0;
			if (std::memcmp(&ha, &hb, sizeof ha) != 0) { why = "header"; return false; }
			if (fa.order != fb.order) { why = "section order"; return false; }
			for (const auto& tag : fa.order)
			{
				const auto& sa = fa.sections.at(tag);
				const auto& sb = fb.sections.at(tag);
				if (sa.entry.major != sb.entry.major || sa.entry.minor != sb.entry.minor || sa.entry.flags != sb.entry.flags ||
					sa.entry.records != sb.entry.records)
				{
					why = "section entry " + tag;
					return false;
				}
				if (tag != "KEYS")
				{
					if (sa.payload != sb.payload) { why = "section " + tag; return false; }
					continue;
				}
				const auto& ka = sa.data["keys"].as_list();
				const auto& kb = sb.data["keys"].as_list();
				if (ka.size() != kb.size()) { why = "KEYS count"; return false; }
				for (std::size_t k = 0; k < ka.size(); ++k)
				{
					if (ka[k]["id"] != kb[k]["id"] || ka[k]["domain"] != kb[k]["domain"]) { why = "KEYS id"; return false; }
					const auto mine = py_lower(unescape_key(ka[k]["key"].as_text()), Enc::latin1);
					const auto theirs = py_lower(kb[k]["key"].as_text(), Enc::latin1);
					if (mine != theirs) { why = "KEYS text " + mine + " / " + theirs; return false; }
				}
			}
			return true;
		}
		catch (const Error& e)
		{
			why = e.message;
			return false;
		}
	}

	// Expected associations come from the build's numbered placements, never loader metadata.
	unsigned normalize_promotions(Value& states, const ZoneNumbering& numbering)
	{
		using Names = std::pair<Hash, Hash>;
		std::map<Names, std::vector<std::size_t>> placements, saved;
		std::set<std::size_t> claimed;
		for (std::size_t i = 0; i < numbering.names.size(); ++i) placements[numbering.names[i]].push_back(i + 1);
		for (std::size_t i = 0; i < states.l.size(); ++i)
		{
			const auto& state = states.l[i];
			if (state["list"].as_int() == 1) claimed.insert(static_cast<std::size_t>(state["ordinal"].as_int()));
			else if (state["list"].as_int() == 2 || state["list"].as_int() == 3)
			{
				const auto& definition = state["definition"];
				const auto& instance = state["instance"];
				saved[{key_hash1(definition.as_text(), definition.enc), key_hash1(instance.as_text(), instance.enc)}].push_back(i);
			}
		}
		unsigned promoted = 0;
		for (const auto& [names, indices] : saved)
		{
			const auto found = placements.find(names);
			if (indices.size() != 1 || found == placements.end() || found->second.size() != 1) continue;
			const auto ordinal = found->second.front();
			if (claimed.count(ordinal)) continue;
			auto& state = states.l[indices.front()];
			state.set("list", Value::integer(1));
			state.set("ordinal", Value::integer(static_cast<std::int64_t>(ordinal)));
			Value flags = state["flags"];
			flags.l[1] = Value::integer(flags.l[1].as_int() | 8);
			state.set("flags", std::move(flags));
			++promoted;
		}
		return promoted;
	}

	bool associations_match_build(const ZoneCaptureMetadata& capture, const ZoneNumbering& numbering)
	{
		std::set<u32> ordinals;
		for (const auto& entity : capture.entities)
		{
			if (!entity.ordinal || entity.ordinal > numbering.names.size() || entity.ordinal > numbering.placement.size() ||
				!ordinals.insert(entity.ordinal).second) return false;
			const auto i = entity.ordinal - 1;
			if (entity.placement_index != numbering.placement[i] ||
				key_hash1(entity.definition_name, Enc::cp1252) != numbering.names[i].first ||
				key_hash1(entity.instance_name, Enc::cp1252) != numbering.names[i].second) return false;
		}
		return true;
	}

	Value oracle_list(std::initializer_list<Value> values)
	{
		Value out = Value::list();
		for (const auto& value : values) out.push(value);
		return out;
	}

	Value oracle_states()
	{
		auto state = [](const char* definition, const char* instance, int list, int flag)
		{
			return Value::dict({{"definition", Value::text(definition)}, {"instance", Value::text(instance)},
				{"list", Value::integer(list)}, {"ordinal", Value::integer(0)},
				{"flags", oracle_list({Value::integer(0), Value::integer(flag)})},
				{"position", oracle_list({Value::real(1), Value::real(2), Value::real(3)})},
				{"payload", Value::text("0123456789abcdef")}});
		};
		return oracle_list({state("gate", "door_a", 2, 3), state("crate", "box_a", 3, 1)});
	}

	bool oracle_accepts_wrong_associations()
	{
		ZoneNumbering numbering;
		numbering.names = {{key_hash1("gate", Enc::latin1), key_hash1("door_a", Enc::latin1)},
			{key_hash1("crate", Enc::latin1), key_hash1("box_a", Enc::latin1)}};
		numbering.placement = {5, 9};
		ZoneCaptureMetadata bad;
		bad.entities = {{2, 9, "gate", "door_a"}, {1, 5, "crate", "box_a"}};
		Value expected = oracle_states();
		normalize_promotions(expected, numbering);
		// Wrong loader output and matching wrong metadata; values are hand-derived from the injected swap.
		Value actual = oracle_states();
		for (std::size_t i = 0; i < 2; ++i)
		{
			actual.l[i].set("list", Value::integer(1));
			actual.l[i].set("ordinal", Value::integer(i == 0 ? 2 : 1));
			actual.l[i].set("flags", oracle_list({Value::integer(0), Value::integer(i == 0 ? 11 : 9)}));
		}
		return to_json(expected) == to_json(actual) || associations_match_build(bad, numbering);
	}

	void test_corpus_oracle()
	{
		const auto before = failures;
		unsigned checks = 0;
		auto expect = [&](bool ok, const char* what) { ++checks; check(ok, what); };
		expect(!oracle_accepts_wrong_associations(), "corpus oracle rejects swapped promotion ordinals with matching bad metadata");
		ZoneNumbering numbering;
		numbering.names = {{key_hash1("gate", Enc::latin1), key_hash1("door_a", Enc::latin1)},
			{key_hash1("crate", Enc::latin1), key_hash1("box_a", Enc::latin1)}};
		numbering.placement = {5, 9};
		ZoneCaptureMetadata good;
		good.entities = {{1, 5, "gate", "door_a"}, {2, 9, "crate", "box_a"}};
		expect(associations_match_build(good, numbering), "correct build ordinals and placement indices accepted");
		auto wrong_placement = good;
		wrong_placement.entities[0].placement_index = 9;
		expect(!associations_match_build(wrong_placement, numbering), "wrong placement index rejected");
		auto out_of_range = good;
		out_of_range.entities[0].ordinal = 3;
		expect(!associations_match_build(out_of_range, numbering), "out-of-range ordinal rejected");
		auto duplicate_ordinal = good;
		duplicate_ordinal.entities.push_back(good.entities[0]);
		expect(!associations_match_build(duplicate_ordinal, numbering), "duplicate association rejected");
		Value states = oracle_states();
		Value literal = states;
		literal.l[0].set("list", Value::integer(1)); literal.l[0].set("ordinal", Value::integer(1));
		literal.l[0].set("flags", oracle_list({Value::integer(0), Value::integer(11)}));
		literal.l[1].set("list", Value::integer(1)); literal.l[1].set("ordinal", Value::integer(2));
		literal.l[1].set("flags", oracle_list({Value::integer(0), Value::integer(9)}));
		expect(normalize_promotions(states, numbering) == 2 && states == literal, "unique names derive literal build ordinals; payload and position retained");
		auto twins = numbering;
		twins.names.push_back(numbering.names[0]); twins.placement.push_back(12);
		states = oracle_states();
		expect(normalize_promotions(states, twins) == 1 && states.l[0]["list"].as_int() == 2, "ambiguous build placements are not promoted");
		states = oracle_states(); states.push(states.l[0]);
		expect(normalize_promotions(states, numbering) == 1 && states.l[0]["list"].as_int() == 2 && states.l[2]["list"].as_int() == 2,
			"duplicate saved names are not promoted");
		states = oracle_states(); Value tombstone = states.l[0];
		tombstone.set("list", Value::integer(1)); tombstone.set("ordinal", Value::integer(1));
		tombstone.set("flags", oracle_list({Value::integer(0), Value::integer(0)})); states.push(tombstone);
		expect(normalize_promotions(states, numbering) == 1 && states.l[0]["list"].as_int() == 2 && states.l[2] == tombstone,
			"claimed tombstone ordinal blocks promotion and remains removed");
		std::printf("corpus oracle: %u checks, %d failures\n", checks, failures - before);
	}

	// ------------------------------------------------------------------------------------------------ corpus
	int run_corpus(const fs::path& corpus, const fs::path& python, const std::wstring& data, const std::wstring& binds, const fs::path& out)
	{
		Tables tables;
		std::string error;
		if (!load_tables(data, binds, tables, error))
		{
			std::printf("FAIL: load_tables: %s\n", error.c_str());
			return 1;
		}
		std::printf("tables: catalogue revision %u, content version %u, %u heroes, %u stats, %u items, %u courses, %u objectives\n",
			tables.catalogue_revision(), tables.content_version(), tables.hero_count(), tables.stats_count(), tables.item_count(),
			tables.course_count(), tables.objective_count());
		int files = 0, lcsave_equal = 0, lcsave_keys = 0, old_equal = 0, new_equal = 0, reverse_agree = 0, reverse_identical = 0, all_equal = 0;
		int saver_ok = 0, saver_same = 0, saver_policy_same = 0, conversation_resets = 0, retained_ok = 0, adds_files = 0, adds_total = 0, placement_promotions = 0;
		Hash running_digest{};
		if (!conversation_digest(tables, running_digest, error)) { std::printf("FAIL: conversation digest: %s\n", error.c_str()); return 1; }
		std::vector<fs::path> paths;
		for (const auto& e : fs::recursive_directory_iterator(corpus))
		{
			const auto name = e.path().filename().string();
			if (e.is_regular_file() && name.rfind("saveslot", 0) == 0 && e.path().extension() == ".save") paths.push_back(e.path());
		}
		std::sort(paths.begin(), paths.end());
		// Preserve every semantic field, including LGCY; only state-list order is irrelevant (a multiset).
		auto canonical_semantics = [](Value out)
		{
			if (out.has("zones"))
			{
				Value zones = out["zones"];
				for (auto& z : zones.l)
				{
					Value states = z["states"];
					std::stable_sort(states.l.begin(), states.l.end(), [](const Value& x, const Value& y) { return to_json(x) < to_json(y); });
					z.set("states", std::move(states));
				}
				out.set("zones", std::move(zones));
			}
			return out;
		};

		// Unknown record/section extensions are absent from dump_new; check them separately rather than hiding loss.
		auto extensions = [](const Value& value)
		{
			std::map<std::string, std::string> found;
			auto visit = [&](auto&& self, const Value& item, const std::string& path) -> void
			{
				for (const auto& member : item.d)
				{
					const auto key = to_json(member.key);
					if (member.key.type == Value::Type::text && member.key.s == "_extra" && member.value.truthy()) found[path + "." + key] = to_json(member.value);
					else self(self, member.value, path + "." + key);
				}
				for (std::size_t i = 0; i < item.l.size(); ++i) self(self, item.l[i], path + "[" + std::to_string(i) + "]");
			};
			visit(visit, value, "");
			return found;
		};
		for (const auto& p : paths)
		{
			++files;
			const auto rel = fs::relative(p, corpus);
			const auto base = python / rel;
			const auto old_bytes = read_all(p);
			const auto py_lcsave = read_all(base.string() + ".lcsave");
			const auto py_old = read_text(base.string() + ".old.json");
			const auto py_new = read_text(base.string() + ".new.json");
			const auto py_report = read_text(base.string() + ".report.json");
			ConvertOptions options;
			options.original_name = p.filename().string();
			try
			{
				const auto f = read_file(py_lcsave.data(), py_lcsave.size(), kind_campaign);
				options.fixed_saved_at = true;
				options.saved_at = static_cast<std::uint64_t>(f.data("META")["saved_at"].as_int());
			}
			catch (const Error& e)
			{
				std::printf("  %s: the Python .lcsave does not read (%s)\n", rel.string().c_str(), e.message.c_str());
			}
			const auto r = convert_old_to_new(old_bytes, tables, options);
			std::string status;
			bool same_bytes = false, same_old = false, same_new = false, same_reverse = false, same_policy = false, retained = false;
			if (!r.ok)
			{
				status = " REFUSED: " + r.error;
			}
			else
			{
				same_bytes = r.bytes == py_lcsave;
				std::string why;
				if (!same_bytes) status += " [lcsave full bytes differ]";
				std::string err;
				const auto new_json = dump(r.bytes, tables, err);
				same_new = !new_json.empty() && new_json == py_new;
				ConvertState st;
				std::string old_json;
				bool reversed_identical = false;
				try
				{
					convert_slot(old_bytes, *tables.data, options, st);
					const auto old = read_slot(old_bytes.data(), old_bytes.size());
					old_json = to_json(dump_old(old, *tables.data, st.start_era, st.items_era));
					std::vector<std::string> notes;
					const auto back = reverse_slot(r.bytes, *tables.data, &old, notes);
					reversed_identical = write_slot(back) == old_bytes;
					// Exercise the runtime metadata/retention contract, then require every semantic field to survive.
					// Permit only digest-confirmed conversation resets and independently build-confirmed legacy placement promotions.
					const auto plain = new_to_old_stream(r.bytes, tables);
					if (!plain.ok || plain.bytes.size() != old_slot_size)
					{
						status += " [loader stream failed: " + plain.error + "]";
					}
					else
					{
						NewFileContext ctx;
						ctx.description = cstr(reinterpret_cast<const char*>(old.description_raw), 0x80);
						ctx.saved_at = options.saved_at;
						ctx.campaign_serial = 1;
						ctx.retained_file = r.bytes;
						ctx.require_capture_metadata = true;
						if (!loaded_zone_metadata(r.bytes, tables, ctx.zone_captures, error)) fail("loaded capture metadata: " + error);
						const std::vector<std::uint8_t> payload(plain.bytes.begin() + old_envelope, plain.bytes.end());
						const auto saved = old_stream_to_new(payload, tables, ctx);
						if (!saved.ok)
						{
							status += " [saver refused: " + saved.error + "]";
						}
						else
						{
							++saver_ok;
							const auto input_file = read_file(r.bytes.data(), r.bytes.size(), kind_campaign);
							const auto saved_file = read_file(saved.bytes.data(), saved.bytes.size(), kind_campaign);
							const auto actual = canonical_semantics(dump_new(saved.bytes, *tables.data));
							const auto original = canonical_semantics(dump_new(r.bytes, *tables.data));
							const bool same = to_json(actual) == to_json(original);
							saver_same += same;
							Value expected = original;
							Value zones = expected["zones"];
							const auto& records = input_file.data("ZONE")["records"].as_list();
							if (zones.l.size() != records.size()) fail("semantic zone records do not match input container");
							int resets = 0;
							for (std::size_t i = 0; i < records.size(); ++i)
							{
								const auto& digest = records[i]["conversation_digest"].as_bytes();
								const bool invalid = digest == std::string(32, '\0') || digest != std::string(reinterpret_cast<const char*>(running_digest.data()), running_digest.size());
								const bool legacy = input_file.data("META")["origin"].as_int() == 1 || records[i]["capture_flags"].as_int() == 2;
								if (legacy)
								{
									Value states = zones.l[i]["states"];
									const auto* numbering = tables.data->build->zone_numbering(zones.l[i]["zone"].as_text());
									if (!numbering || !numbering->problems.empty()) fail("corpus oracle: invalid build numbering");
									placement_promotions += normalize_promotions(states, *numbering);
									zones.l[i].set("states", std::move(states));
								}
								if (!invalid) continue;
								Value cleared = Value::list();
								bool had_bits = false;
								for (const auto& word : records[i]["conversation"].as_list()) { had_bits |= word.as_int() != 0; cleared.push(Value::integer(0)); }
								if (had_bits) ++resets;
								zones.l[i].set("conversation", std::move(cleared));
							}
							expected.set("zones", std::move(zones));
							same_policy = to_json(actual) == to_json(canonical_semantics(expected));
							retained = true;
							bool opaque_keep = false;
							for (const auto& [tag, section] : input_file.sections)
							{
								const auto found = saved_file.sections.find(tag);
								const bool preserve_raw = tag == "LGCY" || !section.known;
								opaque_keep |= !section.known && (section.entry.flags & section_keep);
								if (preserve_raw && (found == saved_file.sections.end() || found->second.payload != section.payload ||
									found->second.entry.major != section.entry.major || found->second.entry.minor != section.entry.minor ||
									found->second.entry.flags != section.entry.flags || found->second.entry.records != section.entry.records))
								{ retained = false; status += " [retained payload/entry differs: " + tag + "]"; }
								if (section.known && (found == saved_file.sections.end() || extensions(section.data) != extensions(found->second.data)))
								{ retained = false; status += " [unknown fields differ: " + tag + "]"; }
							}
							if (opaque_keep)
							{
								const auto& keys = input_file.data("KEYS")["keys"].as_list();
								const auto& saved_keys = saved_file.data("KEYS")["keys"].as_list();
								if (saved_keys.size() < keys.size()) retained = false;
								else for (std::size_t i = 0; i < keys.size(); ++i) if (keys[i] != saved_keys[i]) retained = false;
							}

							// These recording fields are intentionally absent from the human-facing semantic dump.
							// Require the saver to use the cached metadata, including compatibility-normalized status.
							for (const auto& record : saved_file.data("ZONE")["records"].as_list())
							{
								const auto zone = tables.data->p_zone.find(static_cast<u32>(record["zone"].as_int()));
								const ZoneCaptureMetadata* capture = nullptr;
								if (zone != tables.data->p_zone.end()) for (const auto& candidate : ctx.zone_captures)
									if (candidate.zone == zone->second && candidate.time_stamp == record["time_stamp"].as_real()) capture = &candidate;
								if (!capture || record["capture_flags"].as_int() != capture->capture_flags ||
									record["content_hash"].as_bytes() != std::string(reinterpret_cast<const char*>(capture->placement_sha1.data()), capture->placement_sha1.size()) ||
									record["conversation_digest"].as_bytes() != std::string(reinterpret_cast<const char*>(capture->conversation_digest.data()), capture->conversation_digest.size()))
								{ retained = false; status += " [cached capture provenance differs]"; continue; }
								const auto* numbering = tables.data->build->zone_numbering(capture->zone);
								if (!numbering || !numbering->problems.empty() || !associations_match_build(*capture, *numbering))
								{ retained = false; status += " [capture association differs from build numbering]"; }
								const auto& names = record["names"].as_list();
								for (const auto& entity : capture->entities)
								{
									bool preserved = false;
									for (const auto& state : record["states"].as_list())
										if (state["list"].as_int() == 1 && state["ordinal"].as_int() == entity.ordinal &&
											state["placement_index"].as_int() == entity.placement_index &&
											names.at(static_cast<std::size_t>(state["def_name"].as_int())).as_text() == entity.definition_name &&
											names.at(static_cast<std::size_t>(state["inst_name"].as_int())).as_text() == entity.instance_name) preserved = true;
									if (!preserved) { retained = false; status += " [cached placement/name association differs]"; }
								}
							}
							retained_ok += retained;
							saver_policy_same += same_policy;
							if (same_policy) conversation_resets += resets;
							if (!same_policy) status += " [UNEXPLAINED saver round-trip difference]";
							else if (resets) status += " [digest-confirmed conversation resets: " + std::to_string(resets) + "]";
							if (!same_policy || !retained)
							{
								status += " [saver round trip differs]";
								if (!out.empty())
								{
									const auto a = to_json(dump_new(saved.bytes, *tables.data));
									write_all(out / (rel.string() + ".saver.json"), a.data(), a.size());
								}
							}
						}
					}
					std::vector<TalentAdd> adds;
					if (missing_talents(old_bytes, tables, adds) && !adds.empty())
					{
						++adds_files;
						adds_total += static_cast<int>(adds.size());
					}
				}
				catch (const Error& e)
				{
					status += " [reverse/dump error: " + e.message + "]";
				}
				same_old = !old_json.empty() && old_json == py_old;
				const bool py_identical = py_report.find("\"reverse_identical\": true") != std::string::npos;
				same_reverse = reversed_identical == py_identical;
				if (reversed_identical) ++reverse_identical;
				if (!out.empty())
				{
					write_all(out / (rel.string() + ".lcsave"), r.bytes.data(), r.bytes.size());
					write_all(out / (rel.string() + ".old.json"), old_json.data(), old_json.size());
					write_all(out / (rel.string() + ".new.json"), new_json.data(), new_json.size());
				}
				status += std::string(same_bytes ? " lcsave=equal" : " lcsave=DIFFERS") +
					(same_old ? " old.json=equal" : " old.json=DIFFERS") + (same_new ? " new.json=equal" : " new.json=DIFFERS") +
					" reverse_identical=" + (reversed_identical ? "yes" : "no") + (same_reverse ? " (as Python)" : " (NOT as Python)");
			}
			lcsave_equal += same_bytes;
			old_equal += same_old;
			new_equal += same_new;
			reverse_agree += same_reverse;
			const bool all = same_bytes && same_old && same_new && same_reverse;
			all_equal += all;
			std::printf("%s %s:%s\n", all && same_policy && retained ? "equal  " : "DIFFERS", rel.string().c_str(), status.c_str());
		}
		std::printf("corpus: %d files; .lcsave byte-identical %d, identical but for the KEYS text %d; old dump equal %d; new dump equal %d; "
			"reverse byte-exact %d (agrees with the Python report %d); all equal %d\n", files, lcsave_equal, lcsave_keys, old_equal, new_equal,
			reverse_identical, reverse_agree, all_equal);
		std::printf("legacy placement promotions normalized: %d\n", placement_promotions);
		std::printf("loader -> saver round trip: saved %d, raw unchanged game state %d, policy round trip %d, conversation resets %d, retained payloads/unknown fields %d; missing herostat talents: %d files, %d talents\n", saver_ok,
			saver_same, saver_policy_same, conversation_resets, retained_ok, adds_files, adds_total);
		return files && lcsave_equal == files && all_equal == files && saver_ok == files && saver_policy_same == files && retained_ok == files ? 0 : 1;
	}
}

// everything load_tables parsed, as JSON (keys in hex): compare_tables.py checks it against the builder's own readers
// (legends-classic lcsave.lcfiles read_catalogue_lcc / read_legacy_lcm / read_lcbind) table by table
static std::string tables_json(const Tables& tables)
{
	const auto& t = *tables.data;
	auto I = [](std::int64_t v) { return Value::integer(v); };
	auto H = [](const std::string& s) { return Value::text(hex(reinterpret_cast<const std::uint8_t*>(s.data()), s.size())); };
	auto row = [&](u32 g, u32 p, u32 id)
	{
		Value r = Value::list();
		r.push(I(g));
		r.push(I(p));
		r.push(I(id));
		return r;
	};
	Value cat = Value::dict({{"revision", I(t.catalogue.revision)}, {"content_version", I(t.catalogue.content_version)}});
	Value entries = Value::list();
	for (const auto& e : t.catalogue.entries)
	{
		Value aliases = Value::list();
		for (const auto& a : e.aliases) aliases.push(H(a));
		entries.push(Value::dict({{"domain", I(e.domain)}, {"id", I(e.id)}, {"attributes", I(e.attributes)}, {"flags", I(e.retired ? 1 : 0)},
			{"successor", I(e.successor)}, {"key", H(e.key)}, {"aliases", aliases}}));
	}
	cat.set("entries", entries);
	Value eras = Value::list();
	for (const auto& e : t.eras)
	{
		Value tabs = Value::dict();
		auto list = [&](const char* tag, const std::vector<u32>& l)
		{
			Value rows = Value::list();
			for (u32 i = 0; i < l.size(); ++i) rows.push(row(0, i, l[i]));
			tabs.set(tag, rows);
		};
		list("STAT", e.STAT);
		list("ENHC", e.ENHC);
		list("NAMA", e.NAMA);
		list("TRIT", e.TRIT);
		list("MSNF", e.MSNF);
		list("OBJV", e.OBJV);
		list("DRCS", e.DRCS);
		list("DRGR", e.DRGR);
		list("XPNT", e.XPNT);
		list("ZONE", e.ZONE);
		Value item = Value::list(), namb = Value::list(), taln = Value::list(), cost = Value::list(), revw = Value::list(), triv = Value::list(),
			iown = Value::list(), town = Value::list();
		for (u32 i = 0; i < e.ITEM.size(); ++i) item.push(row(i < e.ITEM_cat.size() ? e.ITEM_cat[i] : 0, i, e.ITEM[i]));
		for (const auto& [c, l] : e.NAMB) for (u32 i = 0; i < l.size(); ++i) namb.push(row(c, i, l[i]));
		for (u32 i = 0; i < e.TALN_shared.size(); ++i) taln.push(row(0, i, e.TALN_shared[i]));
		for (const auto& [si, l] : e.TALN_hero) for (u32 i = 0; i < l.size(); ++i) taln.push(row(si, i, l[i]));
		for (const auto& [si, slots] : e.COST) for (const auto& [s, id] : slots) cost.push(row(si, s, id));
		for (const auto& [k, id] : e.REVW) revw.push(row(static_cast<u32>(k.first), static_cast<u32>(k.second), id));
		for (const auto& [k, id] : e.TRIV) triv.push(row(static_cast<u32>(k.first), static_cast<u32>(k.second), id));
		for (u32 d = 0; d < e.ITEM_own.size(); ++d) for (u32 k = 0; k < e.ITEM_own[d].size(); ++k) iown.push(row(d, k, e.ITEM_own[d][k]));
		for (u32 d = 0; d < e.TRIT_own.size(); ++d) for (u32 k = 0; k < e.TRIT_own[d].size(); ++k) town.push(row(d, k, e.TRIT_own[d][k]));
		tabs.set("ITEM", item);
		tabs.set("NAMB", namb);
		tabs.set("TALN", taln);
		tabs.set("COST", cost);
		tabs.set("REVW", revw);
		tabs.set("TRIV", triv);
		tabs.set("IOWN", iown);
		tabs.set("TOWN", town);
		Value zones = Value::dict();
		for (const auto& [zid, d] : e.zones)
		{
			Value m = Value::list(), a = Value::list();
			for (const auto x : d.map) m.push(I(x));
			for (const auto x : d.ambiguous) a.push(I(x));
			zones.set(Value::text(std::to_string(zid)), Value::dict({{"map", m}, {"ambiguous", a}}));
		}
		Value hold = Value::dict();
		for (const auto& [sid, l] : e.holdable)
		{
			Value v = Value::list();
			for (const auto x : l) v.push(I(x));
			hold.set(Value::text(std::to_string(sid)), v);
		}
		Value ranges = Value::list();
		for (const auto* r : {e.prefix, e.suffix, e.trit, e.own})
		{
			Value p = Value::list();
			p.push(I(r[0]));
			p.push(I(r[1]));
			ranges.push(p);
		}
		eras.push(Value::dict({{"content_version", I(e.cv)}, {"tables", tabs}, {"zones", zones}, {"hero_shared_holdable", hold},
			{"conversation_digest", Value::text(e.has_digest ? hex(e.conversation_digest) : std::string())}, {"hero_count", I(e.hero_count)},
			{"enhc_ranges", ranges}}));
	}
	Value legacy = Value::dict({{"map_id", I(t.legacy_map_id)}, {"catalogue_revision", I(t.legacy_catalogue_revision)}, {"eras", eras},
		{"checks", Value::dict({{"hero_blocks", I(t.check_heroes)}, {"courses", I(t.check_courses)}, {"grades", I(t.check_grades)},
			{"objectives", I(t.check_objectives)}})}});
	Value binds = Value::list();
	for (const auto& f : t.build->bind_files)
	{
		Value lists = Value::list();
		for (const auto& l : f.lists)
		{
			Value rows = Value::list();
			for (const auto& r : l.rows)
			{
				Value x = row(r.group, r.position, r.id);
				x.push(H(r.key));
				rows.push(x);
			}
			lists.push(Value::dict({{"tag", Value::text(l.tag)}, {"rule", I(l.rule)}, {"rows", rows}}));
		}
		binds.push(Value::dict({{"path", Value::text(f.path)}, {"sha1", H(f.sha1)}, {"lists", lists}}));
	}
	Value counts = Value::dict({{"heroes", I(tables.hero_count())}, {"stats", I(tables.stats_count())}, {"items", I(tables.item_count())},
		{"courses", I(tables.course_count())}, {"objectives", I(tables.objective_count())}});
	return to_json(Value::dict({{"catalogue", cat}, {"legacy", legacy}, {"binds", binds}, {"counts", counts}}));
}

extern int vector_failures;
void run_vectors(const fs::path& scratch);
void run_fuzz(const fs::path& scratch, int iterations, const std::vector<std::vector<std::uint8_t>>& extra_old);
void fuzz_core(const Tables& tables, const std::vector<std::vector<std::uint8_t>>& olds, int iterations, const char* label);

int main(int argc, char** argv)
{
	std::map<std::string, std::string> args;
	for (int k = 1; k + 1 < argc; k += 2) args[argv[k]] = argv[k + 1];
	if (args.count("--oracle-negative"))
	{
		const bool accepted = oracle_accepts_wrong_associations();
		std::printf("corpus oracle negative: %s swapped ordinals with matching bad metadata\n", accepted ? "ACCEPTED" : "REJECTED");
		return accepted ? 0 : 1;
	}
	const auto scratch = fs::temp_directory_path() / ("lcsave_test_" + std::to_string(GetCurrentProcessId()));
	int result = 0;
	std::vector<std::vector<std::uint8_t>> corpus_files;
	if (args.count("--tables"))
	{
		// load a build's real set (bind files beside the data) and write what was parsed
		Tables tables;
		std::string error;
		const auto dir = fs::path(args["--tables"]).wstring();
		if (!load_tables(dir, args.count("--binds") ? fs::path(args["--binds"]).wstring() : dir, tables, error))
		{
			std::printf("FAIL: load_tables: %s\n", error.c_str());
			return 1;
		}
		const auto text = tables_json(tables);
		std::ofstream(args["--json"], std::ios::binary).write(text.data(), static_cast<std::streamsize>(text.size()));
		std::printf("load_tables OK: %u bind files, catalogue revision %u, content version %u (reference %u), %u heroes, %u stats, %u items, "
			"%u courses, %u objectives\n", static_cast<unsigned>(tables.data->build->bind_files.size()), tables.catalogue_revision(),
			tables.data->catalogue.content_version, tables.content_version(), tables.hero_count(), tables.stats_count(), tables.item_count(),
			tables.course_count(), tables.objective_count());
		return 0;
	}
	if (args.count("--corpus"))
	{
		auto widen = [](const std::string& s) { return fs::path(s).wstring(); };
		result = run_corpus(args["--corpus"], args["--python"], widen(args["--data"]), widen(args["--binds"]),
			args.count("--out") ? fs::path(args["--out"]) : fs::path());
		// (c) on real saves with the real tables: every 8th corpus file
		Tables tables;
		std::string error;
		if (load_tables(widen(args["--data"]), widen(args["--binds"]), tables, error))
		{
			std::vector<fs::path> paths;
			for (const auto& e : fs::recursive_directory_iterator(args["--corpus"]))
			{
				const auto name = e.path().filename().string();
				if (e.is_regular_file() && name.rfind("saveslot", 0) == 0 && e.path().extension() == ".save") paths.push_back(e.path());
			}
			std::sort(paths.begin(), paths.end());
			for (std::size_t k = 0; k < paths.size(); k += 8) corpus_files.push_back(read_all(paths[k]));
			fuzz_core(tables, corpus_files, args.count("--real-fuzz") ? std::atoi(args["--real-fuzz"].c_str()) : 1000, "real saves, real tables");
			corpus_files.clear();
		}
	}
	if (args.count("--profile"))
	{
		// the profile file's three directions over a settings.dat: convert, reverse (bytes back), save direction, reverse
		auto widen = [](const std::string& s) { return fs::path(s).wstring(); };
		Tables tables;
		std::string error;
		if (!load_tables(widen(args["--data"]), widen(args["--binds"]), tables, error))
		{
			std::printf("tables: %s\n", error.c_str());
			return 1;
		}
		const auto old = read_all(args["--profile"]);
		ConvertOptions options;
		options.original_name = "settings.dat";
		const auto converted = convert_old_profile_to_new(old, tables, options);
		check(converted.ok, "convert profile: " + converted.error);
		if (!converted.ok) return 1;
		const auto back = new_to_old_profile(converted.bytes, tables);
		check(back.ok, "reverse profile: " + back.error);
		check(back.ok && back.bytes == old, "reverse profile gives the original bytes");
		if (back.ok && back.bytes != old)
		{
			for (std::size_t i = 0; i < old.size() && i < back.bytes.size(); ++i)
			{
				if (old[i] != back.bytes[i]) { std::printf("first difference at 0x%zx: %02x vs %02x\n", i, old[i], back.bytes[i]); break; }
			}
		}
		NewFileContext context;
		context.description = "Settings";
		const auto saved = old_profile_to_new(old, tables, context);
		check(saved.ok, "profile save direction: " + saved.error);
		const auto back2 = saved.ok ? new_to_old_profile(saved.bytes, tables) : Result{};
		check(back2.ok && back2.bytes == old, "profile save direction reverses to the original bytes");
		std::string e1, e2;
		const auto d1 = dump(converted.bytes, tables, e1);
		std::printf("profile: converted %zu bytes, saved %zu bytes, dump %zu chars %s\n", converted.bytes.size(), saved.bytes.size(), d1.size(), e1.c_str());
		std::printf("%s: %d failures\n", failures ? "FAILED" : "passed", failures);
		return failures ? 1 : 0;
	}
	// (a) the reference's vectors, (c) the fuzz (on the synthetic saves; the corpus run adds nothing to it: the real
	// saves need the real tables, which the fuzz's synthetic tables can not read)
	test_corpus_oracle();
	run_vectors(scratch);
	const int iterations = args.count("--fuzz") ? std::atoi(args["--fuzz"].c_str()) : 2000;
	run_fuzz(scratch, iterations, corpus_files);
	std::error_code ec;
	fs::remove_all(scratch, ec);
	failures += vector_failures;
	std::printf("%s: %d failures\n", failures ? "FAILED" : "passed", failures);
	return failures || result ? 1 : 0;
}
