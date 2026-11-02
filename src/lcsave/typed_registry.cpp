// The typed entity-id registry and the Phase A feature decisions (PHASE-A-PLAN section 4), as
// tools/lcsave/typed_registry.py and container.feature_decision: which script variables hold World tokens, the
// fingerprint that binds the registry to the scripts tree it was derived from, and the one adapter that translates
// a legacy (feature-0, retail eight-bit) file's registered fields into contiguous ten-bit tokens at the LC -> native
// boundary. Nothing else in a save is ever reinterpreted.
//
// Registry file (text, LF, ASCII; the Python writes it, both read it):
//     lcsave-typed-entity-ids 1
//     fingerprint <64 hex digits>
//     files <count of *.py files hashed>
//     key<TAB><scope><TAB><key>        one per typed field, sorted by (scope, key); scope is Zone or Game

#include "internal.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace lcsave
{
	namespace
	{
		constexpr const char* registry_format = "lcsave-typed-entity-ids";
		constexpr const char* registry_version = "1";

		bool ascii_printable(const std::string& s)
		{
			for (const unsigned char c : s) if (c < 32 || c > 126) return false;
			return true;
		}
		bool hex64(const std::string& s)
		{
			if (s.size() != 64) return false;
			for (const char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
			return true;
		}
		std::vector<std::string> split(const std::string& line, char sep)
		{
			std::vector<std::string> out;
			std::size_t start = 0;
			for (;;)
			{
				const auto at = line.find(sep, start);
				out.push_back(line.substr(start, at == std::string::npos ? at : at - start));
				if (at == std::string::npos) return out;
				start = at + 1;
			}
		}
		std::vector<std::string> lower_components(const std::string& relative_posix)
		{
			auto parts = split(relative_posix, '/');
			for (auto& part : parts) for (auto& c : part) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			return parts;
		}
	}

	const char* const downgrade_refused = "this save requires ENTITY_LAYOUT_CONTIGUOUS10 (required feature bit 0): it needs the "
		"entity-capacity build and cannot be loaded by a retail-layout build";

	const char* const w2_refused = "this save requires NATIVE_W2 (required feature bit 1): it needs the W2 save layout "
		"([Limits] SaveLayout=w2; Legends Classic 0.4.2 / XML2 Fix 1.9 or newer); enable it or use the build that wrote it";

	const char* required_feature_name(const std::uint32_t bit)
	{
		switch (bit)
		{
		case feature_entity_layout_contiguous10: return "ENTITY_LAYOUT_CONTIGUOUS10";
		case feature_native_w2: return "NATIVE_W2";
#ifdef XML2_E4_CORE
		case feature_actor_capacity96:return "ACTOR_CAPACITY_96";
#endif
		default: return nullptr;
		}
	}

	const char* required_feature_needs(const std::uint32_t bit)
	{
		switch (bit)
		{
		case feature_entity_layout_contiguous10: return "the entity-capacity build";
		case feature_native_w2: return "Legends Classic 0.4.2 (XML2 Fix 1.9) or newer with [Limits] SaveLayout=w2";
#ifdef XML2_E4_CORE
		case feature_actor_capacity96:return "the actor-capacity build ([Limits] Actors=96)";
#endif
		default: return nullptr;
		}
	}

	FeatureDecision decide_features(const std::uint32_t required_features, const NativeLayout& native)
	{
		FeatureDecision d;
		d.required_features = required_features;
		if((required_features&feature_actor_capacity96)&&!(native.feature_bits&feature_actor_capacity96))
		{
			d.reason="required feature bits 0x4 not implemented (needs a newer version)";
			return d;
		}
		if (required_features & ~known_required_features)
		{
			char b[32];
			std::snprintf(b, sizeof b, "0x%x", static_cast<unsigned>(required_features));
			d.reason = std::string("required feature bits ") + b + " not implemented (needs a newer version)";
			return d;
		}
		const bool contiguous = (required_features & feature_entity_layout_contiguous10) != 0;
		// bit 1 (W2 section 7): known by name to this code; applicable only by the W2 layout. A W2 file is always
		// contiguous (W2 tokens are ten-bit); bit 1 without bit 0 is no file a writer makes.
		if (required_features & feature_native_w2)
		{
			if (!contiguous)
			{
				char b[32];
				std::snprintf(b, sizeof b, "0x%x", static_cast<unsigned>(required_features));
				d.reason = std::string("required feature bits ") + b + ": NATIVE_W2 without ENTITY_LAYOUT_CONTIGUOUS10 (damaged marker)";
				return d;
			}
			if (!native.w2())
			{
				d.reason = native.contiguous() ? w2_refused : downgrade_refused;
				return d;
			}
		}
		if (contiguous && !native.contiguous())
		{
			d.reason = downgrade_refused;
			return d;
		}
		d.ok = true;
		d.migrate_typed_fields = native.contiguous() && !contiguous;
		return d;
	}

	FeatureDecision decide_features(const std::vector<std::uint8_t>& new_file, const NativeLayout& native)
	{
		try
		{
			const auto f = detail::read_file(new_file.data(), new_file.size());
			return decide_features(f.header.required_features, native);
		}
		catch (const detail::Error& e)
		{
			FeatureDecision d;
			d.reason = e.message;
			if (new_file.size() >= sizeof(NewHeader)) std::memcpy(&d.required_features, new_file.data() + 0x10, 4);
			return d;
		}
	}

	bool TypedRegistry::typed(const std::string& scope, const std::string& key) const
	{
		return std::binary_search(keys.begin(), keys.end(), std::make_pair(scope, key));
	}

	bool load_typed_registry(const std::vector<std::uint8_t>& file, TypedRegistry& out, std::string& error)
	{
		out = TypedRegistry{};
		error.clear();
		for (const auto c : file) if (c >= 0x80) { error = "typed registry: not ASCII text"; return false; }
		std::string text(file.begin(), file.end());
		auto lines = split(text, '\n');
		if (!lines.empty() && lines.back().empty()) lines.pop_back();
		if (lines.empty() || lines[0] != std::string(registry_format) + " " + registry_version)
		{
			error = std::string("typed registry: not a ") + registry_format + " " + registry_version + " file";
			return false;
		}
		bool have_fingerprint = false, have_files = false;
		for (std::size_t n = 1; n < lines.size(); ++n)
		{
			const auto& line = lines[n];
			const auto where = "typed registry line " + std::to_string(n + 1);
			if (!line.empty() && line.back() == '\r') { error = where + ": CR line ends are not allowed"; return false; }
			if (line.rfind("fingerprint ", 0) == 0)
			{
				const auto v = line.substr(12);
				if (!hex64(v) || have_fingerprint) { error = where + ": bad fingerprint"; return false; }
				out.fingerprint = v;
				have_fingerprint = true;
			}
			else if (line.rfind("files ", 0) == 0)
			{
				const auto v = line.substr(6);
				if (v.empty() || v.size() > 9 || have_files || !std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; }))
				{
					error = where + ": bad file count";
					return false;
				}
				out.script_files = static_cast<std::uint32_t>(std::stoul(v));
				have_files = true;
			}
			else if (line.rfind("key\t", 0) == 0)
			{
				const auto parts = split(line, '\t');
				if (parts.size() != 3 || (parts[1] != "Zone" && parts[1] != "Game") || parts[2].empty() || !ascii_printable(parts[2]))
				{
					error = where + ": bad key line";
					return false;
				}
				const auto entry = std::make_pair(parts[1], parts[2]);
				if (std::find(out.keys.begin(), out.keys.end(), entry) != out.keys.end()) { error = where + ": duplicate key"; return false; }
				out.keys.push_back(entry);
			}
			else
			{
				error = where + ": unknown line";
				return false;
			}
		}
		if (!have_fingerprint || !have_files) { error = "typed registry: fingerprint or file count missing"; return false; }
		out.version = 1;
		std::sort(out.keys.begin(), out.keys.end());
		return true;
	}

	bool scripts_fingerprint(const std::wstring& scripts_root, std::string& fingerprint, std::uint32_t& files, std::string& error)
	{
		namespace fs = std::filesystem;
		fingerprint.clear();
		files = 0;
		error.clear();
		std::error_code ec;
		const fs::path root(scripts_root);
		if (!fs::is_directory(root, ec)) { error = "scripts root is not a directory"; return false; }
		// every *.py regular file (the extension compared without case, as Python's rglob does on Windows), its path
		// relative to the root with '/' separators; non-ASCII paths are refused (the Python refuses them too)
		std::vector<std::pair<std::vector<std::string>, std::pair<std::string, fs::path>>> found;
		for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
		{
			if (ec) { error = "scripts tree cannot be enumerated: " + ec.message(); return false; }
			if (!it->is_regular_file(ec)) continue;
			const auto& p = it->path();
			// The Python rule (`name.lower().endswith('.py')`): a file named exactly ".py" counts too, which
			// std::filesystem::path::extension() would not report (a leading-dot name has no extension there).
			const auto name = p.filename().wstring();
			if (name.size() < 3 || (name[name.size() - 3] != L'.') || (name[name.size() - 2] != L'p' && name[name.size() - 2] != L'P') || (name[name.size() - 1] != L'y' && name[name.size() - 1] != L'Y')) continue;
			const auto rel = p.lexically_relative(root).generic_wstring();
			std::string narrow;
			for (const auto c : rel)
			{
				if (c >= 0x80) { error = "script path is not ASCII"; return false; }
				narrow.push_back(static_cast<char>(c));
			}
			found.emplace_back(lower_components(narrow), std::make_pair(narrow, p));
		}
		std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		std::string input;
		for (const auto& [components, entry] : found)
		{
			std::vector<std::uint8_t> bytes;
			if (!detail::read_file_bytes(entry.second.wstring(), bytes)) { error = "script cannot be read: " + entry.first; return false; }
			input += entry.first;
			input.push_back('\0');
			input += hex(sha256(bytes.data(), bytes.size()));
			input.push_back('\n');
		}
		fingerprint = hex(sha256(reinterpret_cast<const std::uint8_t*>(input.data()), input.size()));
		files = static_cast<std::uint32_t>(found.size());
		return true;
	}

	bool verify_registry(const TypedRegistry& registry, const std::wstring& scripts_root, std::string& reason)
	{
		std::string fingerprint, error;
		std::uint32_t files = 0;
		if (!scripts_fingerprint(scripts_root, fingerprint, files, error))
		{
			reason = "typed registry cannot be verified: " + error;
			return false;
		}
		if (fingerprint != registry.fingerprint)
		{
			reason = "typed registry fingerprint " + registry.fingerprint.substr(0, 16) + "... does not match the scripts tree " +
				fingerprint.substr(0, 16) + "... (" + std::to_string(files) + " files): a changed script schema needs a regenerated registry";
			return false;
		}
		if (files != registry.script_files)
		{
			reason = "typed registry lists " + std::to_string(registry.script_files) + " files, the scripts tree has " + std::to_string(files);
			return false;
		}
		reason.clear();
		return true;
	}

	bool verify_registry(const std::vector<std::uint8_t>& registry_file, const std::wstring& scripts_root, std::string& reason)
	{
		TypedRegistry registry;
		std::string error;
		if (!load_typed_registry(registry_file, registry, error))
		{
			reason = "typed registry cannot be verified: " + error;
			return false;
		}
		return verify_registry(registry, scripts_root, reason);
	}

	std::string MigrationReport::line() const
	{
		return std::string("mode=") + (mode == Mode::migrate ? "migrate" : mode == Mode::validate ? "validate" : "none") +
			" typed_fields=" + std::to_string(typed_fields) + " translated=" + std::to_string(translated) +
			" normalized_to_null=" + std::to_string(normalized_to_null) + " validated=" + std::to_string(validated);
	}
}

namespace lcsave::detail
{
	const char* const no_registry = "legacy save needs a verified typed entity-id registry before its typed fields can be migrated (none supplied)";

	void migrate_script_vars(Value& zone_vars, Value& game_vars, const u32 required_features, const NativeLayout& native,
		const TypedRegistry* registry, MigrationReport& report)
	{
		report = MigrationReport{};
		const bool legacy = (required_features & feature_entity_layout_contiguous10) == 0;
		if (!native.contiguous()) return;                       // a feature-1 file was refused before this point
		if (legacy && !registry) fail(no_registry);
		report.mode = legacy ? MigrationReport::Mode::migrate : (registry ? MigrationReport::Mode::validate : MigrationReport::Mode::none);
		if (!registry) return;
		const std::pair<const char*, Value*> lists[2] = {{"Zone", &zone_vars}, {"Game", &game_vars}};
		for (const auto& [scope, list] : lists)
		{
			for (auto& v : list->as_list())
			{
				if (!registry->typed(scope, v["name"].as_text())) continue;
				++report.typed_fields;
				const auto value = static_cast<u32>(v["value"].as_int());
				u32 mapped = value;
				if (legacy)
				{
					mapped = migrate_typed_field(value);
					if (value && mapped) ++report.translated;
					else if (value && !mapped) ++report.normalized_to_null;
				}
				else
				{
					mapped = validate_typed_field(value);
					++report.validated;
					if (value && !mapped) ++report.normalized_to_null;
				}
				v.set("value", Value::integer(mapped));
			}
		}
	}

	std::string migration_note(const MigrationReport& report)
	{
		if (report.mode == MigrationReport::Mode::migrate)
		{
			return "typed entity-id migration: " + std::to_string(report.typed_fields) + " registered fields, " +
				std::to_string(report.translated) + " translated, " + std::to_string(report.normalized_to_null) + " normalized to null";
		}
		if (report.mode == MigrationReport::Mode::validate)
		{
			return "typed entity-id validation: " + std::to_string(report.typed_fields) + " registered fields, " +
				std::to_string(report.normalized_to_null) + " normalized to null";
		}
		return std::string();
	}
}
