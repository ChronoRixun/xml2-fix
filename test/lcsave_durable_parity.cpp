// Full-byte codec parity over invented durable-save fixtures exported by the Python reference.
// cases.tsv: name<TAB>build_relative_path<TAB>mode<TAB>sequence; no game inputs are needed.
#include "lcsave.hpp"

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
		if (mode != "campaign" && mode != "profile" && mode != "legacy-refuse" && mode != "legacy-import" && mode != "profile-import" && mode != "reverse" && mode != "refuse" && mode != "native-campaign" && mode != "native-transfer" && mode != "native-dead-item" && mode != "native-trivia") return report("unsupported mode");
		std::vector<std::uint8_t> input, expected;
		if (!read_bytes(folder / (mode == "legacy-refuse" || mode == "legacy-import" || mode == "profile-import" || mode == "native-campaign" || mode == "native-transfer" || mode == "native-dead-item" || mode == "native-trivia" ? "input.raw" : "input.lcsave"), input)) return report("cannot read input");
		const auto expected_name = mode == "reverse" ? "expected.old" : (mode == "refuse" || mode == "legacy-refuse") ? "expected.error" : "expected.lcsave";
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
		else if (mode == "native-campaign" || mode == "native-transfer" || mode == "native-dead-item" || mode == "native-trivia")
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
			else if (mode == "native-transfer")
			{
				if (!lcsave::native_zone_metadata(input,tables,context.zone_captures,error)) return report("transfer metadata: "+error);
			}
			else if (!read_bytes(folder / "metadata.lcsave",context.retained_file) ||
			         !lcsave::loaded_zone_metadata(context.retained_file,tables,context.zone_captures,error)) return report("metadata: "+error);
			result = lcsave::old_stream_to_new(input,tables,context);
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
