#include "mod_loader.hpp"
#include "iat_hook.hpp"
#include "log.hpp"

#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mod_loader
{
	namespace
	{
		bool tracing = false;
		std::wstring game_folder;                             // as Windows reports it
		std::wstring game_dir;                                // lower case, ends with '\'
		std::unordered_map<std::wstring, std::wstring> files; // lower-case path relative to the game -> file in a mod

		std::wstring lower(std::wstring text)
		{
			for (auto& c : text)
			{
				c = static_cast<wchar_t>(std::towlower(c));
			}
			return text;
		}

		std::wstring widen(const char* text)
		{
			const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
			std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
			if (length > 1)
			{
				MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), length);
			}
			return wide;
		}

		// The mod file's path for an ANSI API. Falls back to the short (8.3) name when the path has
		// characters the ANSI code page can't represent.
		std::string narrow(const std::wstring& text)
		{
			BOOL lossy = FALSE;
			const int length = WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, nullptr, 0, nullptr, &lossy);
			if (lossy)
			{
				wchar_t shorter[MAX_PATH]{};
				if (GetShortPathNameW(text.c_str(), shorter, MAX_PATH))
				{
					return narrow(shorter);
				}
			}
			std::string result(length > 0 ? length - 1 : 0, '\0');
			if (length > 1)
			{
				WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, result.data(), length, nullptr, nullptr);
			}
			return result;
		}

		bool has_wildcard(const std::wstring& path)
		{
			return path.find_first_of(L"*?") != std::wstring::npos;
		}

		// The mod file that replaces `path`, if any.
		std::optional<std::wstring> redirect(const std::wstring& path)
		{
			if (files.empty() || path.empty() || has_wildcard(path))
			{
				return std::nullopt;
			}

			wchar_t full[MAX_PATH * 2]{};
			const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
			if (!length || length >= std::size(full))
			{
				return std::nullopt;
			}

			auto normalized = lower(std::wstring(full, length));
			if (normalized.compare(0, game_dir.size(), game_dir) != 0)
			{
				return std::nullopt;
			}

			const auto found = files.find(normalized.substr(game_dir.size()));
			if (found == files.end())
			{
				return std::nullopt;
			}
			return found->second;
		}

		void trace(const char* api, const std::wstring& path, const std::optional<std::wstring>& target)
		{
			if (target)
			{
				logger::write_once("mod:" + narrow(lower(path)), "mods: %s %ls -> %ls", api, path.c_str(), target->c_str());
			}
			else if (tracing)
			{
				logger::write("files: %s %ls", api, path.c_str());
			}
		}

		bool reads_only(const DWORD access, const DWORD disposition)
		{
			constexpr DWORD writing = GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE | WRITE_DAC | WRITE_OWNER;
			return !(access & writing) && (disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS);
		}

		bool reads_only(const char* mode)
		{
			return mode && !std::strpbrk(mode, "wa+");
		}

		bool reads_only(const wchar_t* mode)
		{
			return mode && !std::wcspbrk(mode, L"wa+");
		}

		// Windows API: one implementation for every module.
		using create_file_a_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
		using create_file_w_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
		using get_attributes_a_t = DWORD(WINAPI*)(LPCSTR);
		using get_attributes_w_t = DWORD(WINAPI*)(LPCWSTR);
		using get_attributes_ex_a_t = BOOL(WINAPI*)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
		using get_attributes_ex_w_t = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
		using find_first_a_t = HANDLE(WINAPI*)(LPCSTR, LPWIN32_FIND_DATAA);
		using find_first_w_t = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);

		HANDLE WINAPI create_file_a(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templ)
		{
			if (name && reads_only(access, disposition))
			{
				const auto path = widen(name);
				const auto target = redirect(path);
				trace("CreateFileA", path, target);
				if (target)
				{
					return CreateFileW(target->c_str(), access, share, security, disposition, flags, templ);
				}
			}
			return CreateFileA(name, access, share, security, disposition, flags, templ);
		}

		HANDLE WINAPI create_file_w(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templ)
		{
			if (name && reads_only(access, disposition))
			{
				const auto target = redirect(name);
				trace("CreateFileW", name, target);
				if (target)
				{
					return CreateFileW(target->c_str(), access, share, security, disposition, flags, templ);
				}
			}
			return CreateFileW(name, access, share, security, disposition, flags, templ);
		}

		DWORD WINAPI get_attributes_a(LPCSTR name)
		{
			if (name)
			{
				const auto path = widen(name);
				const auto target = redirect(path);
				trace("GetFileAttributesA", path, target);
				if (target)
				{
					return GetFileAttributesW(target->c_str());
				}
			}
			return GetFileAttributesA(name);
		}

		DWORD WINAPI get_attributes_w(LPCWSTR name)
		{
			if (name)
			{
				const auto target = redirect(name);
				trace("GetFileAttributesW", name, target);
				if (target)
				{
					return GetFileAttributesW(target->c_str());
				}
			}
			return GetFileAttributesW(name);
		}

		BOOL WINAPI get_attributes_ex_a(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info)
		{
			if (name)
			{
				const auto path = widen(name);
				const auto target = redirect(path);
				trace("GetFileAttributesExA", path, target);
				if (target)
				{
					return GetFileAttributesExW(target->c_str(), level, info);
				}
			}
			return GetFileAttributesExA(name, level, info);
		}

		BOOL WINAPI get_attributes_ex_w(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info)
		{
			if (name)
			{
				const auto target = redirect(name);
				trace("GetFileAttributesExW", name, target);
				if (target)
				{
					return GetFileAttributesExW(target->c_str(), level, info);
				}
			}
			return GetFileAttributesExW(name, level, info);
		}

		HANDLE WINAPI find_first_a(LPCSTR pattern, LPWIN32_FIND_DATAA data)
		{
			if (pattern)
			{
				const auto path = widen(pattern);
				const auto target = redirect(path);
				trace(has_wildcard(path) ? "FindFirstFileA (search)" : "FindFirstFileA", path, target);
				if (target)
				{
					return FindFirstFileA(narrow(*target).c_str(), data);
				}
			}
			return FindFirstFileA(pattern, data);
		}

		HANDLE WINAPI find_first_w(LPCWSTR pattern, LPWIN32_FIND_DATAW data)
		{
			if (pattern)
			{
				const auto target = redirect(pattern);
				trace(has_wildcard(pattern) ? "FindFirstFileW (search)" : "FindFirstFileW", pattern, target);
				if (target)
				{
					return FindFirstFileW(target->c_str(), data);
				}
			}
			return FindFirstFileW(pattern, data);
		}

		// C runtimes: each module's FILE* must stay with the runtime that made it, so every
		// runtime gets its own hook instance and original.
		using fopen_t = FILE*(__cdecl*)(const char*, const char*);
		using findfirst_t = intptr_t(__cdecl*)(const char*, void*);

		constexpr const char* runtimes[] = {"MSVCRT.dll", "MSVCR71.dll", "MSVCR110.dll"};
		fopen_t real_fopen[std::size(runtimes)]{};
		findfirst_t real_findfirst[std::size(runtimes)]{};

		template <int Runtime>
		FILE* __cdecl crt_fopen(const char* name, const char* mode)
		{
			if (name && reads_only(mode))
			{
				const auto path = widen(name);
				const auto target = redirect(path);
				trace("fopen", path, target);
				if (target)
				{
					return real_fopen[Runtime](narrow(*target).c_str(), mode);
				}
			}
			return real_fopen[Runtime](name, mode);
		}

		template <int Runtime>
		intptr_t __cdecl crt_findfirst(const char* pattern, void* data)
		{
			if (pattern)
			{
				const auto path = widen(pattern);
				const auto target = redirect(path);
				trace(has_wildcard(path) ? "_findfirst (search)" : "_findfirst", path, target);
				if (target)
				{
					return real_findfirst[Runtime](narrow(*target).c_str(), data);
				}
			}
			return real_findfirst[Runtime](pattern, data);
		}

		template <int Runtime>
		void hook_runtime(const HMODULE module)
		{
			if (auto* original = iat_hook::hook(module, runtimes[Runtime], "fopen", 0, reinterpret_cast<void*>(&crt_fopen<Runtime>)))
			{
				real_fopen[Runtime] = reinterpret_cast<fopen_t>(original);
			}
			if (auto* original = iat_hook::hook(module, runtimes[Runtime], "_findfirst", 0, reinterpret_cast<void*>(&crt_findfirst<Runtime>)))
			{
				real_findfirst[Runtime] = reinterpret_cast<findfirst_t>(original);
			}
		}

		void hook_module(const HMODULE module)
		{
			iat_hook::hook(module, "KERNEL32.dll", "CreateFileA", 0, reinterpret_cast<void*>(&create_file_a));
			iat_hook::hook(module, "KERNEL32.dll", "CreateFileW", 0, reinterpret_cast<void*>(&create_file_w));
			iat_hook::hook(module, "KERNEL32.dll", "GetFileAttributesA", 0, reinterpret_cast<void*>(&get_attributes_a));
			iat_hook::hook(module, "KERNEL32.dll", "GetFileAttributesW", 0, reinterpret_cast<void*>(&get_attributes_w));
			iat_hook::hook(module, "KERNEL32.dll", "GetFileAttributesExA", 0, reinterpret_cast<void*>(&get_attributes_ex_a));
			iat_hook::hook(module, "KERNEL32.dll", "GetFileAttributesExW", 0, reinterpret_cast<void*>(&get_attributes_ex_w));
			iat_hook::hook(module, "KERNEL32.dll", "FindFirstFileA", 0, reinterpret_cast<void*>(&find_first_a));
			iat_hook::hook(module, "KERNEL32.dll", "FindFirstFileW", 0, reinterpret_cast<void*>(&find_first_w));
			hook_runtime<0>(module);
			hook_runtime<1>(module);
			hook_runtime<2>(module);
		}

		struct load_order_entry
		{
			std::wstring name;
			bool enabled;
		};

		std::vector<load_order_entry> read_load_order(const std::filesystem::path& file)
		{
			std::vector<load_order_entry> entries;
			std::ifstream in(file);
			std::string line;
			while (std::getline(in, line))
			{
				while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
				{
					line.pop_back();
				}
				if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) // UTF-8 byte order mark
				{
					line.erase(0, 3);
				}
				if (line.size() < 2 || line[0] == '#' || (line[0] != '+' && line[0] != '-'))
				{
					continue;
				}

				const std::string name = line.substr(1);
				const int length = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), static_cast<int>(name.size()), nullptr, 0);
				std::wstring wide(length, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, name.c_str(), static_cast<int>(name.size()), wide.data(), length);
				entries.push_back({wide, line[0] == '+'});
			}
			return entries;
		}

		// Indexes the enabled mods' files; later mods override earlier ones.
		void build_index()
		{
			const std::filesystem::path root = std::filesystem::path(game_folder) / L"mods";
			auto order = read_load_order(root / L"load-order.txt");

			// Mod folders the load order doesn't list yet (copied in by hand) load after the listed
			// ones, alphabetically - the launcher adds them to the list the same way.
			std::vector<std::wstring> unlisted;
			std::error_code scan_error;
			for (const auto& entry : std::filesystem::directory_iterator(root, scan_error))
			{
				const auto name = entry.path().filename().wstring();
				const auto listed = std::any_of(order.begin(), order.end(), [&](const auto& e) { return lower(e.name) == lower(name); });
				if (entry.is_directory(scan_error) && !name.empty() && name.front() != L'.' && !listed)
				{
					unlisted.push_back(name);
				}
			}
			std::sort(unlisted.begin(), unlisted.end(), [](const auto& a, const auto& b) { return lower(a) < lower(b); });
			for (const auto& name : unlisted)
			{
				order.push_back({name, true});
			}

			for (const auto& [name, enabled] : order)
			{
				const auto folder = root / name;
				std::error_code error;
				if (!enabled || !std::filesystem::is_directory(folder, error))
				{
					continue;
				}

				int count = 0;
				for (auto it = std::filesystem::recursive_directory_iterator(folder, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
				{
					if (!it->is_regular_file(error))
					{
						continue;
					}
					const auto relative = lower(it->path().lexically_relative(folder).wstring());
					if (relative == L"mod.json" || relative == L"readme.txt" || relative == L"readme.md")
					{
						continue; // the mod's own metadata
					}
					files[relative] = it->path().wstring();
					++count;
				}
				logger::write("mods: %ls - %d file(s)", name.c_str(), count);
			}
		}
	}

	void install(const std::initializer_list<const char*> modules, const bool trace_files)
	{
		tracing = trace_files;

		wchar_t exe[MAX_PATH]{};
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		game_folder = std::filesystem::path(exe).parent_path().wstring();
		game_dir = lower(game_folder);
		if (game_dir.empty() || game_dir.back() != L'\\')
		{
			game_dir += L'\\';
		}

		build_index();
		if (files.empty() && !tracing)
		{
			return; // nothing to load: leave file access alone
		}

		for (const char* name : modules)
		{
			if (const HMODULE module = name ? GetModuleHandleA(name) : GetModuleHandleW(nullptr))
			{
				hook_module(module);
			}
		}
		logger::write("mods: %zu file(s) from mods%s", files.size(), tracing ? " (tracing file access)" : "");
	}
}
