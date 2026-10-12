#include "save_content_guards.hpp"
#include "save_guard_rules.hpp"
#include "mod_loader.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "lcsave/internal.hpp"
#include "lcsave/xmlb.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace save_content_guards
{
	namespace
	{
		std::string runtime_error;
		std::string normalized(std::string s)
		{
			for (char& c : s) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}
		std::string stem(std::string s)
		{
			s = normalized(std::move(s));
			for (const auto ext : {".engb", ".xmlb", ".lcbind"})
				if (s.ends_with(ext)) return s.substr(0, s.size()-std::strlen(ext));
			return s;
		}
		bool identity_root(const std::string& s)
		{
			for (const auto root : {"data/talents/", "data/missions/", "data/entities/"})
				if (s.starts_with(root)) return true;
			for (const auto name : {"data/herostat", "data/npcstat", "data/shared_talents", "data/items", "data/zoneinfo",
								   "data/dangerroom", "data/review_paths", "data/xtraction_points", "data/trivia", "data/entities"})
				if (s == name) return true;
			return false;
		}
		bool refusal(const std::filesystem::path& file, const std::string& why, std::string& error)
		{ error = file.string() + ": " + why; return false; }

		constexpr save_format_rules::address refusal_va = 0x4c011e;
		constexpr save_format_rules::address file_guard_va = 0x4c04a8;
		// Rechecked against the retail image: this shared return is reached on a new registration at cap 100,
		// or an invalid registration index. Existing-name lookup bypasses it.
		constexpr const char* refusal_bytes = "5f5d5b33c0";

		bool matches_runtime(const HMODULE game)
		{
			__try
			{
				const auto* b = reinterpret_cast<const std::uint8_t*>(game);
				return game && reinterpret_cast<std::uintptr_t>(game) == save_format_rules::image_base &&
					   limits_rules::matches(b + (refusal_va-save_format_rules::image_base), refusal_bytes) &&
					   limits_rules::matches(b + (0x4c010f-save_format_rules::image_base), "8b8e54160000b8640000003bc8") &&
					   limits_rules::matches(b + (0x4c0133-save_format_rules::image_base), "3986f426000074e339869030000074db") &&
					   limits_rules::matches(b + (file_guard_va-save_format_rules::image_base), "83bb5803000009745d");
			}
			__except(EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		void set_registration_failure(const unsigned* counts, const char* name);
		void set_file_failure(const char* name);
		void __cdecl registration_refused(void* registry, const char* talent)
		{
			unsigned counts[3]{};
			char name[64]{};
			__try
			{
				const auto* p = static_cast<const std::uint8_t*>(registry);
				std::memcpy(&counts[0],p+0x1654,4);
				std::memcpy(&counts[1],p+0x26f4,4);
				std::memcpy(&counts[2],p+0x3090,4);
				if (talent) for (std::size_t i=0; i<sizeof(name)-1 && talent[i]; ++i) name[i] = talent[i];
			}
			__except(EXCEPTION_EXECUTE_HANDLER) {}
			if (!save_guard_rules::registry_full(counts[0],counts[1],counts[2])) return;
			// String construction is kept out of the SEH function by the helper below (MSVC C2712).
			set_registration_failure(counts, name);
		}
		__declspec(naked) void registration_bridge()
		{
			__asm
			{
				pushfd
				pushad
				push ebx
				push esi
				call registration_refused
				add esp, 8
				popad
				popfd
				pop edi
				pop ebp
				pop ebx
				xor eax, eax
				push 0x4c0123
				ret
			}
		}
		void __cdecl file_refused(void*, const char* file)
		{
			char name[128]{};
			__try { if (file) for (std::size_t i=0; i<sizeof(name)-1 && file[i]; ++i) name[i] = file[i]; }
			__except(EXCEPTION_EXECUTE_HANDLER) {}
			set_file_failure(name);
		}
		__declspec(naked) void file_bridge()
		{
			__asm
			{
				cmp dword ptr [ebx+0x358], 9
				je full
				push 0x4c04b1
				ret
			full:
				pushfd
				pushad
				push ebp
				push ebx
				call file_refused
				add esp, 8
				popad
				popfd
				push 0x4c050e
				ret
			}
		}
		void set_file_failure(const char* name)
		{
			if (runtime_error.empty())
			{
				runtime_error = "TalentRegistry refused file " + std::string(name) +
								" (9 file slots full); LC save/load refused for this session";
				logger::write("save guard: %s",runtime_error.c_str());
			}
		}
		void set_registration_failure(const unsigned* counts, const char* name)
		{
			if (runtime_error.empty())
			{
				runtime_error = "TalentRegistry refused " + std::string(name) + " (names " + std::to_string(counts[0]) +
								", objects " + std::to_string(counts[1]) + ", handles " + std::to_string(counts[2]) +
								", capacity 100); LC save/load refused for this session";
				logger::write("save guard: %s", runtime_error.c_str());
			}
		}
	}

	bool verify_mod_files(const std::filesystem::path& build, const std::vector<std::pair<std::wstring,std::wstring>>& winners, std::string& error)
	{
		error.clear();
		try
		{
			std::set<std::string> covered;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(build / "Data"))
				if (entry.is_regular_file() && normalized(entry.path().extension().string()) == ".lcbind")
					covered.insert(stem(entry.path().lexically_relative(build).string()));
			// Placement provenance binds the loose Maps XMLB/ENGB even without an .lcbind sidecar.
			for (const auto& entry : std::filesystem::recursive_directory_iterator(build / "Maps"))
				if (entry.is_regular_file() && (normalized(entry.path().extension().string()) == ".xmlb" ||
											   normalized(entry.path().extension().string()) == ".engb"))
					covered.insert(stem(entry.path().lexically_relative(build).string()));
			for (const auto& [relative, winner] : winners)
			{
				const auto rel = normalized(std::filesystem::path(relative).string());
				const auto key = stem(rel);
				const std::filesystem::path file(winner);
				if (rel.starts_with("data/lcsave/") || covered.contains(key))
					return refusal(file, "mod overrides bind-covered " + rel + " (root " + file.parent_path().string() + ")", error);
				if (identity_root(key) || (rel.starts_with("maps/") && (rel.ends_with(".xmlb") || rel.ends_with(".engb"))))
					return refusal(file, "unbound numbered/catalogue data " + rel + " has unsupported ID-0/unknown keys", error);
				// Packages are resource lists, not embedded content. Cosmetic entries are allowed; catalogue
				// data references must name a verified bound file. The DLL observes actual registration failures too.
				if (rel.starts_with("packages/") && rel.ends_with(".pkgb"))
				{
					std::vector<std::uint8_t> raw;
					if (!lcsave::detail::read_file_bytes(winner,raw)) return refusal(file,"cannot verify package",error);
					lcsave::detail::XNode root;
					try { root = lcsave::detail::decode_xmlb(raw.data(),raw.size()); }
					catch (const lcsave::detail::Error& e) { return refusal(file,"cannot verify package: " + e.message,error); }
					for (const auto& node : root.children)
					{
						const auto* fn = node.get("filename");
						if (!fn) continue;
						const auto resource = stem(*fn);
						if ((normalized(node.tag) == "xml_talents" || identity_root(resource)) && !covered.contains(resource))
							return refusal(file,"package brings unbound ID-0/unknown keyrefs from " + *fn,error);
					}
				}
			}
			return true;
		}
		catch (const lcsave::detail::Error& e) { error = "mod content cannot be verified: " + e.message; }
		catch (const std::exception& e) { error = "mod root cannot be verified: " + std::string(e.what()); }
		return false;
	}

	bool verify_mods(const std::filesystem::path& build, std::string& error)
	{ return verify_mod_files(build,mod_loader::effective_files(),error); }

	bool verify_keys(const std::vector<std::uint8_t>& bytes, std::string& error)
	{
		error.clear();
		try
		{
			const auto f = lcsave::detail::read_file(bytes.data(),bytes.size());
			const auto it = f.sections.find("KEYS");
			if (it != f.sections.end())
				for (const auto& k : it->second.data["keys"].as_list())
					if (!k["id"].as_int() || lcsave::is_keyref(static_cast<std::uint32_t>(k["id"].as_int())))
					{
						error = "KEYS contains unsupported ID-0/unknown keyref " + k["key"].as_text();
						return false;
					}
			return true;
		}
		catch (const lcsave::detail::Error& e) { error = e.message; }
		catch (const std::exception& e) { error = e.what(); }
		return false;
	}

	std::string runtime_failure() { return runtime_error; }

	void install(const HMODULE game)
	{
		if (save_format_rules::parse_mode(ini::text(L"Save",L"Format").value_or("")).value != save_format_rules::mode::lc) return;
		if (!matches_runtime(game))
		{
			runtime_error = "TalentRegistry refusal hook does not match the engine; LC save/load refused";
			logger::write("save guard: %s",runtime_error.c_str());
			return;
		}
		const std::pair<DWORD, std::pair<const void*, std::size_t>> sites[] = {
			{refusal_va,{reinterpret_cast<const void*>(&registration_bridge),5}},
			{file_guard_va,{reinterpret_cast<const void*>(&file_bridge),9}}
		};
		for (const auto& [va, target] : sites)
		{
			DWORD protection = 0;
			auto* at = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(va));
			if (!VirtualProtect(at,target.second,PAGE_EXECUTE_READWRITE,&protection))
			{
				runtime_error = "TalentRegistry refusal hook could not be installed; LC save/load refused";
				logger::write("save guard: %s",runtime_error.c_str());
				return;
			}
			std::memset(at,0x90,target.second);
			at[0] = 0xe9;
			const auto delta = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target.first)-va-5);
			std::memcpy(at+1,&delta,4);
			DWORD ignored = 0;
			VirtualProtect(at,target.second,protection,&ignored);
			FlushInstructionCache(GetCurrentProcess(),at,target.second);
		}
		logger::write("save guard: TalentRegistry refusal observed at 0x%08lX (100 talents) and 0x%08lX (9 files)",refusal_va,file_guard_va);
	}
}
