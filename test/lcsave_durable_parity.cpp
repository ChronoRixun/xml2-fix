// Full-byte codec parity over invented durable-save fixtures exported by the Python reference.
// cases.tsv: name<TAB>build_relative_path<TAB>mode<TAB>sequence; no game inputs are needed.
// The Phase A modes (tests/unit/export_lcsave_typed_vectors.py): e10a2-reverse (registry verifies; the E10A2
// loader's stream, report and ledger note; the E10A2 saver's feature-1 rewrite; loading it again is idempotent and
// the retail loader refuses it), refuse-downgrade, refuse-unknown, refuse-noregistry, refuse-stream and
// registry-mismatch (each refused with the Python's exact reason, exposing no bytes).
#include "lcsave.hpp"
#include "keys.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	bool relative_path(const std::string& text)
	{
		const fs::path path(text);
		if (text.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
		for (const auto& part : path) if (part == ".." || part == ".") return false;
		return true;
	}

	bool inside(const fs::path& root, const fs::path& path)
	{
		const auto resolved = fs::weakly_canonical(path);
		auto current = resolved.begin();
		for (const auto& part : root)
		{
			if (current == resolved.end() || *current != part) return false;
			++current;
		}
		return true;
	}

	bool read_bytes(const fs::path& path, std::vector<std::uint8_t>& bytes)
	{
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if (!stream) return false;
		const auto length = stream.tellg();
		if (length < 0 || length > 4 * 1024 * 1024) return false;
		bytes.resize(static_cast<std::size_t>(length));
		stream.seekg(0);
		if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return static_cast<bool>(stream);
	}

	std::string description(const std::vector<std::uint8_t>& image)
	{
		std::string text;
		for (std::size_t i = 0; i < 0x80 && i < image.size() && image[i]; ++i) text.push_back(static_cast<char>(image[i]));
		return text;
	}

	bool write_and_verify(const fs::path& path, const std::vector<std::uint8_t>& bytes)
	{
		{
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			if (!stream) return false;
			stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			stream.close();
			if (!stream) return false;
		}
		std::vector<std::uint8_t> saved;
		return read_bytes(path, saved) && saved == bytes;
	}

	template <typename Report>
	bool run_typed_case(const fs::path& root, const fs::path& folder, const fs::path& build, const std::string& mode, const std::string& name,
		std::uint32_t sequence, const Report& report)
	{
		std::string error;
		auto expected_error = [&](std::vector<std::uint8_t>& out) { return read_bytes(folder / "expected.error", out); };
		if (mode == "registry-mismatch")
		{
			std::vector<std::uint8_t> registry, expected;
			if (!read_bytes(folder / "registry.txt", registry) || !expected_error(expected)) return report("cannot read registry/expected");
			std::string reason;
			if (lcsave::verify_registry(registry, (folder / "scripts").wstring(), reason) || reason != std::string(expected.begin(), expected.end()))
				return report("expected the registry to be refused with: " + std::string(expected.begin(), expected.end()) + "; got: " + reason);
			std::cout << name << '\t' << mode << "\tPASS\trefused\n";
			return true;
		}
		lcsave::Tables tables;
		if (!lcsave::load_tables(build.wstring(), tables, error)) return report("tables: " + error);
		if (mode == "refuse-stream")
		{
			std::vector<std::uint8_t> input, expected;
			if (!read_bytes(folder / "input.raw", input) || !expected_error(expected)) return report("cannot read input/expected");
			lcsave::NewFileContext context;
			const auto saved = lcsave::old_stream_to_new(input, tables, context, lcsave::e10a2_layout);
			if (saved.ok || !saved.bytes.empty() || saved.error != std::string(expected.begin(), expected.end()))
				return report("expected the retail-size payload to be refused by the E10A2 saver: " + saved.error);
			std::cout << name << '\t' << mode << "\tPASS\trefused\n";
			return true;
		}
		std::vector<std::uint8_t> input;
		if (!read_bytes(folder / "input.lcsave", input)) return report("cannot read input");
		if (mode == "refuse-downgrade" || mode == "refuse-unknown" || mode == "refuse-noregistry")
		{
			std::vector<std::uint8_t> expected;
			if (!expected_error(expected)) return report("cannot read expected.error");
			const std::string reason(expected.begin(), expected.end());
			const auto& layout = mode == "refuse-downgrade" ? lcsave::retail_layout : lcsave::e10a2_layout;
			lcsave::MigrationReport migration;
			const auto translated = lcsave::new_to_old_stream(input, tables, layout, nullptr, &migration);
			if (translated.ok || !translated.bytes.empty() || translated.error != reason)
				return report("expected refusal '" + reason + "'; got '" + translated.error + "'");
			if (mode == "refuse-unknown")
			{
				const auto other = lcsave::new_to_old_stream(input, tables, lcsave::retail_layout);
				if (other.ok || other.error != reason) return report("the retail layout must refuse the unknown bit too: " + other.error);
				if (lcsave::decide_features(input, lcsave::e10a2_layout).ok) return report("decide_features accepted an unknown bit");
			}
			if (mode == "refuse-downgrade")
			{
				const auto decision = lcsave::decide_features(input, lcsave::retail_layout);
				if (decision.ok || decision.reason != reason || decision.required_features != 1) return report("decide_features did not refuse the downgrade");
				std::vector<lcsave::ZoneCaptureMetadata> captures;
				if (lcsave::loaded_zone_metadata(input, tables, captures, error) || error != reason) return report("loaded_zone_metadata must refuse too: " + error);
			}
			std::cout << name << '\t' << mode << "\tPASS\trefused\n";
			return true;
		}
		// e10a2-reverse
		std::vector<std::uint8_t> registry_bytes, expected_stream, expected_report, expected_feature1;
		if (!read_bytes(folder / "registry.txt", registry_bytes) || !read_bytes(folder / "expected.e10a2.old", expected_stream) ||
		    !read_bytes(folder / "expected.report", expected_report) || !read_bytes(folder / "expected.feature1.lcsave", expected_feature1))
			return report("cannot read the vector files");
		std::string reason;
		if (!lcsave::verify_registry(registry_bytes, (folder / "scripts").wstring(), reason)) return report("registry: " + reason);
		lcsave::TypedRegistry registry;
		if (!lcsave::load_typed_registry(registry_bytes, registry, error)) return report("registry: " + error);
		lcsave::MigrationReport migration;
		const auto translated = lcsave::new_to_old_stream(input, tables, lcsave::e10a2_layout, &registry, &migration);
		if (!translated.ok) return report("E10A2 reverse: " + translated.error);
		if (!inside(root, folder / "actual.e10a2.old") || !write_and_verify(folder / "actual.e10a2.old", translated.bytes)) return report("actual.e10a2.old verification failed");
		if (translated.bytes.size() != lcsave::e10a2_layout.slot_size) return report("E10A2 stream size " + std::to_string(translated.bytes.size()));
		if (translated.bytes != expected_stream) return report("E10A2 native stream bytes differ from Python");
		const std::string report_text(expected_report.begin(), expected_report.end());
		std::vector<std::string> lines;
		for (std::size_t start = 0; start < report_text.size();)
		{
			const auto nl = report_text.find('\n', start);
			lines.push_back(report_text.substr(start, nl == std::string::npos ? nl : nl - start));
			if (nl == std::string::npos) break;
			start = nl + 1;
		}
		if (lines.size() < 4) return report("expected.report needs four lines");
		if (migration.line() != lines[0]) return report("migration report differs: " + migration.line() + " vs " + lines[0]);
		if (translated.ledger.find(lines[1]) == std::string::npos) return report("migration ledger note missing: " + lines[1]);
		lcsave::NewFileContext context;
		context.saved_at = 0; context.writer_version = 0x01040000; context.writer = "XML2 Fix";
		context.slot = 0; context.campaign_serial = 17; context.save_sequence = sequence;
		context.retained_file = input; context.require_capture_metadata = true;
		if (!lcsave::loaded_zone_metadata(input, tables, context.zone_captures, error, lcsave::e10a2_layout, &registry)) return report("metadata: " + error);
		const auto saved = lcsave::old_stream_to_new(translated.bytes, tables, context, lcsave::e10a2_layout);
		if (!saved.ok) return report("E10A2 save: " + saved.error);
		if (!write_and_verify(folder / "actual.feature1.lcsave", saved.bytes)) return report("actual.feature1.lcsave verification failed");
		if (saved.bytes != expected_feature1)
		{
			std::size_t offset = 0;
			while (offset < saved.bytes.size() && offset < expected_feature1.size() && saved.bytes[offset] == expected_feature1[offset]) ++offset;
			return report("feature-1 bytes differ at " + std::to_string(offset));
		}
		const auto decision = lcsave::decide_features(saved.bytes, lcsave::e10a2_layout);
		if (!decision.ok || decision.migrate_typed_fields || decision.required_features != 1) return report("the rewrite must carry required feature bit 0");
		lcsave::MigrationReport validation;
		const auto again = lcsave::new_to_old_stream(saved.bytes, tables, lcsave::e10a2_layout, &registry, &validation);
		if (!again.ok || again.bytes != translated.bytes) return report("feature-1 load is not idempotent: " + again.error);
		if (validation.line() != lines[2] || again.ledger.find(lines[3]) == std::string::npos) return report("validation report differs: " + validation.line());
		if (lcsave::new_to_old_stream(saved.bytes, tables).ok) return report("the retail loader accepted the feature-1 rewrite");
		std::cout << name << '\t' << mode << "\tPASS\t" << translated.bytes.size() << " stream bytes, " << saved.bytes.size() << " feature-1 bytes\n";
		return true;
	}

	bool run_case(const fs::path& root, const std::vector<std::string>& fields)
	{
		const auto& name = fields[0];
		const auto& mode = fields[2];
		const auto report = [&](const std::string& error)
		{
			std::cout << name << '\t' << mode << "\tFAIL\t" << error << '\n';
			return false;
		};
		std::uint32_t sequence = 0;
		const auto parsed = std::from_chars(fields[3].data(), fields[3].data() + fields[3].size(), sequence);
		if (parsed.ec != std::errc() || parsed.ptr != fields[3].data() + fields[3].size()) return report("invalid sequence");
		if (!relative_path(name) || !relative_path(fields[1])) return report("case/build paths must be relative and contained");
		const auto folder = root / fs::path(name);
		const auto build = root / fs::path(fields[1]);
		if (!inside(root, folder) || !inside(root, build) || !inside(root, folder / "actual.lcsave")) return report("case/build path escapes fixture root");
		// the Phase A typed entity-id / native layout vectors (tests/unit/export_lcsave_typed_vectors.py)
		if (mode == "e10a2-reverse" || mode == "refuse-downgrade" || mode == "refuse-unknown" || mode == "refuse-noregistry" ||
		    mode == "refuse-stream" || mode == "registry-mismatch")
			return run_typed_case(root, folder, build, mode, name, sequence, report);
		if (mode == "refuse-tables")
		{
			std::vector<std::uint8_t> expected;
			if (!read_bytes(folder / "expected.error",expected)) return report("cannot read expected table error");
			lcsave::Tables tables; std::string error;
			if (lcsave::load_tables(build.wstring(),tables,error) || tables.loaded() || error != std::string(expected.begin(),expected.end()))
				return report("table refusal differs: " + error);
			std::cout << name << '\t' << mode << "\tPASS\trefused\n";
			return true;
		}
		if (mode != "campaign" && mode != "profile" && mode != "legacy-refuse" && mode != "legacy-import" && mode != "profile-import" && mode != "reverse" && mode != "refuse" && mode != "native-campaign" && mode != "native-transfer" && mode != "native-transfer-e10a2" && mode != "native-transfer-refuse" && mode != "native-dead-item" && mode != "native-trivia") return report("unsupported mode");
		std::vector<std::uint8_t> input, expected;
		if (!read_bytes(folder / (mode == "legacy-refuse" || mode == "legacy-import" || mode == "profile-import" || mode == "native-campaign" || (mode == "native-transfer" || mode == "native-transfer-e10a2" || mode == "native-transfer-refuse") || mode == "native-dead-item" || mode == "native-trivia" ? "input.raw" : "input.lcsave"), input)) return report("cannot read input");
		const auto expected_name = mode == "reverse" ? "expected.old" : (mode == "refuse" || mode == "legacy-refuse" || mode == "native-transfer-refuse") ? "expected.error" : "expected.lcsave";
		if (!read_bytes(folder / expected_name, expected)) return report("cannot read expected output");
		lcsave::Tables tables;
		std::string error;
		if (!lcsave::load_tables(build.wstring(), tables, error)) return report("tables: " + error);
		if (mode == "reverse" || mode == "refuse")
		{
			const auto translated = lcsave::new_to_old_stream(input, tables);
			if (mode == "refuse")
			{
				const std::string reason(expected.begin(), expected.end());
				if (translated.ok || !translated.bytes.empty() || translated.error.find(reason) == std::string::npos)
					return report("expected refusal before exposing a stream: " + reason + "; got " + translated.error);
			}
			else
			{
				if (!translated.ok) return report("reverse: " + translated.error);
				if (!inside(root, folder / "actual.old") || !write_and_verify(folder / "actual.old", translated.bytes)) return report("actual.old verification failed");
				if (translated.bytes != expected) return report("native stream bytes differ from Python");
				if (fs::exists(folder / "expected.metadata"))
				{
					std::vector<lcsave::ZoneCaptureMetadata> captures;
					if (!lcsave::loaded_zone_metadata(input, tables, captures, error)) return report("metadata: " + error);
					std::string actual;
					for (const auto& capture : captures) for (const auto& entity : capture.entities)
						actual += capture.zone + "\t" + std::to_string(entity.ordinal) + "\t" + std::to_string(entity.placement_index) +
						          "\t" + entity.definition_name + "\t" + entity.instance_name + "\n";
					std::vector<std::uint8_t> metadata;
					if (!read_bytes(folder / "expected.metadata", metadata) || actual != std::string(metadata.begin(), metadata.end()))
						return report("reconstructed placement metadata differs from Python");
				}

			}
			std::cout << name << '\t' << mode << "\tPASS\t" << expected.size() << " expected bytes\n";
			return true;
		}
		lcsave::Result result;
		if (mode == "legacy-import" || mode == "legacy-refuse")
		{
			lcsave::ConvertOptions options;
			options.original_name = "synthetic.save"; options.fixed_saved_at = true; options.saved_at = 0;
			result = lcsave::convert_old_to_new(input, tables, options);
			if (mode == "legacy-refuse")
			{
				const std::string reason(expected.begin(), expected.end());
				if (result.ok || !result.bytes.empty() || result.error.find(reason) == std::string::npos) return report("expected legacy refusal: " + result.error);
				std::cout << name << '\t' << mode << "\tPASS\trefused before exposing output\n";
				return true;
			}
		}
		else if (mode == "profile-import")
		{
			lcsave::ConvertOptions options;
			options.original_name = "settings.dat";
			options.fixed_saved_at = true;
			options.saved_at = 0;
			result = lcsave::convert_old_profile_to_new(input, tables, options);
		}
		else if (mode == "native-campaign" || (mode == "native-transfer" || mode == "native-transfer-e10a2" || mode == "native-transfer-refuse") || mode == "native-dead-item" || mode == "native-trivia")
		{
			lcsave::NewFileContext context;
			context.description = description(input);
			context.saved_at = 0; context.writer_version = 0x01040000; context.writer = "XML2 Fix";
			context.slot = 0; context.campaign_serial = 17; context.save_sequence = sequence;
			context.require_capture_metadata = true;
			if (mode == "native-trivia")
			{
				if (lcsave::trivia_bit(tables, 6, 0) != 26 || lcsave::trivia_bit(tables, 9, 7) != 49 ||
				    lcsave::trivia_bit(tables, 0, 0) != -1 || lcsave::trivia_bit(tables, 6, -1) != -1 ||
				    lcsave::trivia_bit(tables, 9, 8) != -1) return report("runtime trivia mapping differs");
				context.require_capture_metadata = false; // these invented fixtures have no cached zones
			}
			else if ((mode == "native-transfer" || mode == "native-transfer-e10a2" || mode == "native-transfer-refuse"))
			{
				const auto& layout = mode == "native-transfer-e10a2" ? lcsave::e10a2_layout : lcsave::retail_layout;
				const bool ok = lcsave::native_zone_metadata(input, tables, context.zone_captures, error, layout);
				if (mode == "native-transfer-refuse")
				{
					if (ok || !context.zone_captures.empty() || error != std::string(expected.begin(), expected.end()))
						return report("transfer refusal differs: " + error);
					std::cout << name << '\t' << mode << "\tPASS\trefused\n";
					return true;
				}
				if (!ok) return report("transfer metadata: " + error);
				if (fs::exists(folder / "expected.transfer"))
				{
					std::string actual;
					for (const auto& capture : context.zone_captures)
					{
						actual += capture.zone + "\t" + lcsave::hex(capture.placement_sha1) + "\t" + lcsave::hex(capture.source_placement_sha1) + "\n";
						for (const auto& entity : capture.entities)
							actual += "saved\t" + std::to_string(entity.ordinal) + "\t" + std::to_string(entity.placement_index) + "\t" +
								std::to_string(entity.source_placement_index) + "\t" + entity.definition_name + "\t" + entity.instance_name + "\n";
						for (const auto& entity : capture.fresh_entities)
							actual += "fresh\t" + std::to_string(entity.ordinal) + "\t" + std::to_string(entity.placement_index) + "\t" +
								entity.definition_name + "\t" + entity.instance_name + "\n";
					}
					std::vector<std::uint8_t> expected_metadata;
					if (!read_bytes(folder / "expected.transfer", expected_metadata) || actual != std::string(expected_metadata.begin(), expected_metadata.end()))
						return report("transfer metadata differs from Python");
				}
			}
			else if (!read_bytes(folder / "metadata.lcsave",context.retained_file) ||
			         !lcsave::loaded_zone_metadata(context.retained_file,tables,context.zone_captures,error)) return report("metadata: "+error);
			result = lcsave::old_stream_to_new(input,tables,context, mode == "native-transfer-e10a2" ? lcsave::e10a2_layout : lcsave::retail_layout);
			if (mode == "native-campaign" && result.ledger.find("dangling world item slot 13") == std::string::npos) return report("normalization ledger missing");
			if (mode == "native-dead-item" && result.ledger.find("dead numbered item slot 13") == std::string::npos) return report("dead item normalization ledger missing");
		}
		else
		{
			const auto translated = mode == "campaign" ? lcsave::new_to_old_stream(input, tables) : lcsave::new_to_old_profile(input, tables);
			if (!translated.ok) return report("reverse: " + translated.error);
			lcsave::NewFileContext context;
			context.description = description(translated.bytes);
			context.saved_at = 0;
			context.writer_version = 0x01040000;
			context.writer = "XML2 Fix";
			context.slot = 0;
			context.retained_file = input;
			if (mode == "campaign")
			{
				context.campaign_serial = 17;
				context.save_sequence = sequence;
				context.origin = 0;
				context.require_capture_metadata = true;
				if (!lcsave::loaded_zone_metadata(input, tables, context.zone_captures, error)) return report("metadata: " + error);
				result = lcsave::old_stream_to_new(translated.bytes, tables, context);
			}
			else
			{
				context.campaign_serial = 0;
				context.save_sequence = 0;
				result = lcsave::old_profile_to_new(translated.bytes, tables, context);
			}
		}
		if (!result.ok) return report("write: " + result.error);
		if (!write_and_verify(folder / "actual.lcsave", result.bytes)) return report("actual.lcsave write/read verification failed");
		if (result.bytes != expected)
		{
			std::size_t offset = 0;
			while (offset < result.bytes.size() && offset < expected.size() && result.bytes[offset] == expected[offset]) ++offset;
			return report("bytes differ at " + std::to_string(offset) + "; actual=" + std::to_string(result.bytes.size()) + "; expected=" + std::to_string(expected.size()));
		}
		std::cout << name << '\t' << mode << "\tPASS\t" << result.bytes.size() << " bytes\n";
		return true;
	}
}

int main(int argc, char** argv)
{
	if (argc != 2) { std::cerr << "usage: lcsave_durable_parity <fixture-root>\n"; return 1; }
	try
	{
		const auto root = fs::weakly_canonical(fs::absolute(fs::path(argv[1])));
		std::ifstream manifest(root / "cases.tsv");
		if (!manifest) { std::cerr << "cannot read cases.tsv\n"; return 1; }
		std::size_t cases = 0;
		int failures = 0;
		std::string line;
		while (std::getline(manifest, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty()) continue;
			std::vector<std::string> fields;
			std::size_t start = 0;
			for (;;)
			{
				const auto tab = line.find('\t', start);
				fields.push_back(line.substr(start, tab == std::string::npos ? tab : tab - start));
				if (tab == std::string::npos) break;
				start = tab + 1;
			}
			++cases;
			if (fields.size() != 4) { std::cerr << "invalid manifest row " << cases << '\n'; ++failures; continue; }
			if (!run_case(root, fields)) ++failures;
		}
		if (!manifest.eof()) { std::cerr << "cases.tsv read failed\n"; ++failures; }
		if (cases == 0) { std::cerr << "cases.tsv contains no cases\n"; return 1; }
		std::cout << cases << " cases, " << failures << " failures\n";
		return failures;
	}
	catch (const std::exception& error)
	{
		std::cerr << "fixture runner: " << error.what() << '\n';
		return 1;
	}
}
