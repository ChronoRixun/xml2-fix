// lcsave_zone_records: which saved zone records a build applies, per file and zone, offline on real saves (the
// per-zone save-compatibility release gate, scripts/zone_records_gate.py). No engine, no game: the library in the
// order the DLL's load and write paths call it (save_format.cpp read_lc / write_lc).
//
//   lcsave_zone_records --list FILE --data BUILD --registry FILE --out FILE.jsonl
//
// FILE lists "<id>\t<path>" lines. A retail slot (.save) is imported first, as the game does; LC profiles, settings
// and other files are listed as skipped. For each campaign and each native layout (retail, E10A2) it writes the zones
// the file's ZONE section holds, the zones the load applies (new_to_old_stream), and the zones that still apply after
// one save of that load and a reload (the resave must keep every record the load applied). One JSON object per line.

#include "internal.hpp"
#include "lcsave.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace lcsave;
using namespace lcsave::detail;

namespace
{
	std::vector<std::uint8_t> read_all(const fs::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
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
	std::string list(const std::vector<std::string>& v)
	{
		std::string o = "[";
		for (std::size_t k = 0; k < v.size(); ++k) o += (k ? ", " : "") + esc(v[k]);
		return o + "]";
	}
	std::vector<std::string> applied_zones(const std::vector<std::uint8_t>& stream, const NativeLayout& L)
	{
		const auto m = read_slot(stream.data(), stream.size(), L);
		std::vector<std::string> out;
		for (const auto& z : m.zones) out.push_back(zone_string(*m.zone_strings, z.name_handle));
		return out;
	}
	std::vector<std::string> not_applied_notes(const std::string& ledger)
	{
		std::vector<std::string> out;
		std::istringstream in(ledger);
		std::string line;
		while (std::getline(in, line, ';'))
			if (line.find("not applied") != std::string::npos) out.push_back(line.substr(line.find_first_not_of(' ')));
		return out;
	}

	// one layout: load, save that load, reload
	std::string layout_run(const std::vector<std::uint8_t>& lc, const Tables& tables, const TypedRegistry& reg, const NativeLayout& L)
	{
		std::ostringstream o;
		const auto s1 = new_to_old_stream(lc, tables, L, &reg);
		o << "{\"load\": " << (s1.ok ? "true" : "false") << ", \"error\": " << esc(s1.error);
		if (!s1.ok) { o << "}"; return o.str(); }
		o << ", \"applied\": " << list(applied_zones(s1.bytes, L)) << ", \"notes\": " << list(not_applied_notes(s1.ledger));
		std::vector<ZoneCaptureMetadata> md;
		std::string error;
		if (!loaded_zone_metadata(lc, tables, md, error, L, &reg)) { o << ", \"resave_error\": " << esc("loaded_zone_metadata: " + error) << "}"; return o.str(); }
		const auto f = read_file(lc.data(), lc.size(), kind_campaign);
		NewFileContext ctx;
		ctx.saved_at = 0x01dc000000000000ull;
		ctx.campaign_serial = f.header.campaign_serial;
		ctx.save_sequence = f.header.save_sequence + 1;
		ctx.retained_file = lc;
		ctx.require_capture_metadata = true;
		ctx.zone_captures = md;
		ctx.optional_features = f.header.optional_features & ~optional_keys_present;
		const auto& meta = f.data("META");
		for (const auto& m : meta["mods"].as_list()) ctx.mods.push_back(m.as_text());
		ctx.slot = static_cast<std::uint8_t>(meta["slot"].as_int());
		ctx.origin = static_cast<std::uint8_t>(meta["origin"].as_int() == 2 ? 2 : 0);
		const auto l1 = old_stream_to_new(s1.bytes, tables, ctx, L);
		if (!l1.ok) { o << ", \"resave_error\": " << esc("save: " + l1.error) << "}"; return o.str(); }
		const auto s2 = new_to_old_stream(l1.bytes, tables, L, &reg);
		if (!s2.ok) { o << ", \"resave_error\": " << esc("reload: " + s2.error) << "}"; return o.str(); }
		o << ", \"resave_applied\": " << list(applied_zones(s2.bytes, L)) << "}";
		return o.str();
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::map<std::string, std::string> args;
	for (int k = 1; k + 1 < argc; k += 2) args[argv[k]] = argv[k + 1];
	if (!args.count("--list") || !args.count("--data") || !args.count("--registry") || !args.count("--out"))
	{
		std::printf("usage: lcsave_zone_records --list FILE --data BUILD --registry FILE --out FILE.jsonl\n");
		return 2;
	}
	Tables tables;
	std::string error;
	if (!load_tables(fs::path(args["--data"]).wstring(), tables, error)) { std::printf("FAIL: load_tables: %s\n", error.c_str()); return 1; }
	std::printf("tables: catalogue revision %u, content version %u\n", tables.catalogue_revision(), tables.content_version());
	TypedRegistry reg;
	if (!load_typed_registry(read_all(args["--registry"]), reg, error)) { std::printf("FAIL: registry: %s\n", error.c_str()); return 1; }
	std::ifstream in(args["--list"]);
	std::ofstream out(fs::path(args["--out"]), std::ios::binary);
	std::string line;
	int files = 0, campaigns = 0, failed = 0;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto tab = line.find('\t');
		if (tab == std::string::npos) continue;
		const auto id = line.substr(0, tab), text = line.substr(tab + 1);
		const fs::path path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
		const auto bytes = read_all(path);
		++files;
		std::ostringstream o;
		o << "{\"id\": " << esc(id);
		std::vector<std::uint8_t> lc;
		std::string kind, problem;
		const bool is_lc = bytes.size() >= new_header_size && std::memcmp(bytes.data(), new_magic, 8) == 0;
		if (is_lc) kind = bytes[8] == kind_campaign ? "lc-campaign" : "lc-other";
		else if (bytes.size() == old_slot_size)
		{
			kind = "retail-slot";
			ConvertOptions opt;
			opt.original_name = path.filename().string();
			opt.fixed_saved_at = true;
			const auto r = convert_old_to_new(bytes, tables, opt);
			if (!r.ok) problem = "retail import: " + r.error;
			else lc = r.bytes;
		}
		else kind = "other";
		if (is_lc && kind == "lc-campaign") lc = bytes;
		o << ", \"kind\": " << esc(kind);
		if (!lc.empty())
		{
			++campaigns;
			try
			{
				const auto f = read_file(lc.data(), lc.size(), kind_campaign);
				std::vector<std::string> saved;
				for (const auto& r : f.data("ZONE")["records"].as_list())
				{
					const auto found = tables.data->p_zone.find(static_cast<std::uint32_t>(r["zone"].as_int()));
					saved.push_back(found == tables.data->p_zone.end() ? "#" + std::to_string(r["zone"].as_int()) : found->second);
				}
				o << ", \"saved\": " << list(saved);
				o << ", \"retail\": " << layout_run(lc, tables, reg, retail_layout);
				o << ", \"e10a2\": " << layout_run(lc, tables, reg, e10a2_layout);
			}
			catch (const std::exception& x) { problem = std::string("exception: ") + x.what(); }
			catch (const Error& x) { problem = "library error: " + x.message; }
		}
		o << ", \"problem\": " << esc(problem) << "}\n";
		out << o.str();
		if (!problem.empty()) ++failed;
		std::printf("%s %s %s%s\n", problem.empty() ? "ok" : "FAIL", id.c_str(), kind.c_str(), problem.empty() ? "" : (": " + problem).c_str());
	}
	std::printf("zone records: %d files, %d campaigns, %d with a problem\n", files, campaigns, failed);
	return 0;
}
