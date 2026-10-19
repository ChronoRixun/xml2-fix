#include "load_trace.hpp"
#include "iat_hook.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <intrin.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace load_trace
{
	namespace
	{
		bool enabled = false;
		std::atomic<unsigned> find_logged{0};
		constexpr unsigned find_log_limit = 64;

		using load_a_t = HMODULE(WINAPI*)(LPCSTR);
		using load_w_t = HMODULE(WINAPI*)(LPCWSTR);
		using load_ex_a_t = HMODULE(WINAPI*)(LPCSTR, HANDLE, DWORD);
		using load_ex_w_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
		using find_a_t = HANDLE(WINAPI*)(LPCSTR, LPWIN32_FIND_DATAA);
		using find_w_t = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
		using find_ex_a_t = HANDLE(WINAPI*)(LPCSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
		using find_ex_w_t = HANDLE(WINAPI*)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
		bool imported_load_a, imported_load_w, imported_load_ex_a, imported_load_ex_w, imported_find_a, imported_find_w, imported_find_ex_a, imported_find_ex_w;
		load_a_t real_load_a;
		load_w_t real_load_w;
		load_ex_a_t real_load_ex_a;
		load_ex_w_t real_load_ex_w;
		find_a_t real_find_a;
		find_w_t real_find_w;
		find_ex_a_t real_find_ex_a;
		find_ex_w_t real_find_ex_w;

		std::string narrow(LPCWSTR text)
		{
			if (!text) return "(null)";
			const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (size <= 1) return "";
			std::string out(size_t(size) - 1, '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
			return out;
		}

		// "module.dll+0xoffset" of a return address.
		std::string where(const void* address)
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                        static_cast<LPCWSTR>(address), &module) || !module)
				return "unknown";
			wchar_t path[MAX_PATH]{};
			GetModuleFileNameW(module, path, MAX_PATH);
			const wchar_t* base = std::wcsrchr(path, L'\\');
			char text[MAX_PATH * 3 + 32];
			std::snprintf(text, sizeof(text), "%s+0x%x", narrow(base ? base + 1 : path).c_str(),
			              unsigned(reinterpret_cast<const char*>(address) - reinterpret_cast<const char*>(module)));
			return text;
		}

		void hook_module(HMODULE module);

		void report_load(const char* api, const std::string& name, DWORD flags, bool has_flags, HMODULE result, DWORD error, const void* caller)
		{
			char outcome[64];
			if (result) std::snprintf(outcome, sizeof(outcome), "handle 0x%p", static_cast<void*>(result));
			else std::snprintf(outcome, sizeof(outcome), "FAILED GetLastError=%lu", error);
			if (has_flags)
				logger::write("loadlibrary: %s \"%s\" flags=0x%lx -> %s, called from %s", api, name.c_str(), flags, outcome, where(caller).c_str());
			else
				logger::write("loadlibrary: %s \"%s\" -> %s, called from %s", api, name.c_str(), outcome, where(caller).c_str());
			// A DLL the engine just loaded may load more: trace its imports too.
			if (result) hook_module(result);
		}

		HMODULE WINAPI hook_load_a(LPCSTR name)
		{
			const void* caller = _ReturnAddress();
			const HMODULE result = real_load_a(name);
			const DWORD error = GetLastError();
			report_load("LoadLibraryA", name ? name : "(null)", 0, false, result, error, caller);
			SetLastError(error);
			return result;
		}

		HMODULE WINAPI hook_load_w(LPCWSTR name)
		{
			const void* caller = _ReturnAddress();
			const HMODULE result = real_load_w(name);
			const DWORD error = GetLastError();
			report_load("LoadLibraryW", narrow(name), 0, false, result, error, caller);
			SetLastError(error);
			return result;
		}

		HMODULE WINAPI hook_load_ex_a(LPCSTR name, HANDLE file, DWORD flags)
		{
			const void* caller = _ReturnAddress();
			const HMODULE result = real_load_ex_a(name, file, flags);
			const DWORD error = GetLastError();
			report_load("LoadLibraryExA", name ? name : "(null)", flags, true, result, error, caller);
			SetLastError(error);
			return result;
		}

		HMODULE WINAPI hook_load_ex_w(LPCWSTR name, HANDLE file, DWORD flags)
		{
			const void* caller = _ReturnAddress();
			const HMODULE result = real_load_ex_w(name, file, flags);
			const DWORD error = GetLastError();
			report_load("LoadLibraryExW", narrow(name), flags, true, result, error, caller);
			SetLastError(error);
			return result;
		}

		// The first find_log_limit directory searches and any naming a plugin / Application Verifier file, so the log
		// stays short but shows the directory the plugin loader enumerates (the engine's "%s\*.*" pattern).
		bool want_find(const std::string& pattern)
		{
			std::string lowered = pattern;
			for (auto& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (lowered.find("plugin") != std::string::npos || lowered.find("appverif") != std::string::npos ||
			    lowered.find("vfcompat") != std::string::npos)
				return true;
			return find_logged.fetch_add(1) < find_log_limit;
		}

		void report_find(const char* api, const std::string& pattern, HANDLE result, DWORD error, const void* caller)
		{
			if (!want_find(pattern)) return;
			if (result != INVALID_HANDLE_VALUE)
				logger::write("loadlibrary: %s \"%s\" -> found, called from %s", api, pattern.c_str(), where(caller).c_str());
			else
				logger::write("loadlibrary: %s \"%s\" -> none (GetLastError=%lu), called from %s", api, pattern.c_str(), error, where(caller).c_str());
		}

		HANDLE WINAPI hook_find_a(LPCSTR pattern, LPWIN32_FIND_DATAA data)
		{
			const void* caller = _ReturnAddress();
			const HANDLE result = real_find_a(pattern, data);
			const DWORD error = GetLastError();
			report_find("FindFirstFileA", pattern ? pattern : "(null)", result, error, caller);
			SetLastError(error);
			return result;
		}

		HANDLE WINAPI hook_find_w(LPCWSTR pattern, LPWIN32_FIND_DATAW data)
		{
			const void* caller = _ReturnAddress();
			const HANDLE result = real_find_w(pattern, data);
			const DWORD error = GetLastError();
			report_find("FindFirstFileW", narrow(pattern), result, error, caller);
			SetLastError(error);
			return result;
		}

		HANDLE WINAPI hook_find_ex_a(LPCSTR pattern, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS ops, LPVOID filter, DWORD flags)
		{
			const void* caller = _ReturnAddress();
			const HANDLE result = real_find_ex_a(pattern, level, data, ops, filter, flags);
			const DWORD error = GetLastError();
			report_find("FindFirstFileExA", pattern ? pattern : "(null)", result, error, caller);
			SetLastError(error);
			return result;
		}

		HANDLE WINAPI hook_find_ex_w(LPCWSTR pattern, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS ops, LPVOID filter, DWORD flags)
		{
			const void* caller = _ReturnAddress();
			const HANDLE result = real_find_ex_w(pattern, level, data, ops, filter, flags);
			const DWORD error = GetLastError();
			report_find("FindFirstFileExW", narrow(pattern), result, error, caller);
			SetLastError(error);
			return result;
		}

		// The hooks forward to the function the first hooked module imported (so a previously installed hook, e.g. the mod
		// loader's file redirection or the plugin-scan guard, stays in the chain); the kernel32 function resolved in install()
		// is only the fallback for a function no hooked module imports.
		template <typename T>
		void hook_one(HMODULE module, const char* function, T& real, bool& from_import, void* replacement)
		{
			if (auto* previous = iat_hook::hook(module, "KERNEL32.dll", function, 0, replacement))
			{
				if (!from_import) { real = reinterpret_cast<T>(previous); from_import = true; }
			}
		}

		void hook_module(HMODULE module)
		{
			if (!module || module == reinterpret_cast<HMODULE>(&__ImageBase)) return;
			// Only the game's own modules (its folder), never Windows' or a driver's.
			wchar_t path[MAX_PATH]{}, exe[MAX_PATH]{};
			if (!GetModuleFileNameW(module, path, MAX_PATH) || !GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
			std::wstring folder(exe);
			folder.resize(folder.find_last_of(L'\\') + 1);
			if (_wcsnicmp(path, folder.c_str(), folder.size()) != 0) return;
			hook_one(module, "LoadLibraryA", real_load_a, imported_load_a, reinterpret_cast<void*>(&hook_load_a));
			hook_one(module, "LoadLibraryW", real_load_w, imported_load_w, reinterpret_cast<void*>(&hook_load_w));
			hook_one(module, "LoadLibraryExA", real_load_ex_a, imported_load_ex_a, reinterpret_cast<void*>(&hook_load_ex_a));
			hook_one(module, "LoadLibraryExW", real_load_ex_w, imported_load_ex_w, reinterpret_cast<void*>(&hook_load_ex_w));
			hook_one(module, "FindFirstFileA", real_find_a, imported_find_a, reinterpret_cast<void*>(&hook_find_a));
			hook_one(module, "FindFirstFileW", real_find_w, imported_find_w, reinterpret_cast<void*>(&hook_find_w));
			hook_one(module, "FindFirstFileExA", real_find_ex_a, imported_find_ex_a, reinterpret_cast<void*>(&hook_find_ex_a));
			hook_one(module, "FindFirstFileExW", real_find_ex_w, imported_find_ex_w, reinterpret_cast<void*>(&hook_find_ex_w));
		}
	}

	void install(HMODULE game)
	{
		if (!ini::flag(L"Debug", L"LoadLibraryTrace", false)) return;
		enabled = true;
		// Resolve the real functions first: a module that imports only some of them still forwards correctly.
		const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
		real_load_a = reinterpret_cast<load_a_t>(GetProcAddress(kernel, "LoadLibraryA"));
		real_load_w = reinterpret_cast<load_w_t>(GetProcAddress(kernel, "LoadLibraryW"));
		real_load_ex_a = reinterpret_cast<load_ex_a_t>(GetProcAddress(kernel, "LoadLibraryExA"));
		real_load_ex_w = reinterpret_cast<load_ex_w_t>(GetProcAddress(kernel, "LoadLibraryExW"));
		real_find_a = reinterpret_cast<find_a_t>(GetProcAddress(kernel, "FindFirstFileA"));
		real_find_w = reinterpret_cast<find_w_t>(GetProcAddress(kernel, "FindFirstFileW"));
		real_find_ex_a = reinterpret_cast<find_ex_a_t>(GetProcAddress(kernel, "FindFirstFileExA"));
		real_find_ex_w = reinterpret_cast<find_ex_w_t>(GetProcAddress(kernel, "FindFirstFileExW"));
		unsigned examined = 1;
		// libIGCore.dll first: its imports carry the whole chain (the plugin-scan guard, the mod loader), and that chain is the
		// forwarding target of every traced call.
		if (const HMODULE core = GetModuleHandleA("libIGCore.dll")) hook_module(core);
		hook_module(game);
		// By name, as the mod loader does (no module enumeration under the loader lock): the engine libraries the game links.
		static constexpr const char* engine[] = {"libIGCore.dll", "libIGAttrs.dll", "libIGAudio.dll", "libIGCollision.dll", "libIGDisplay.dll",
			"libIGGfx.dll", "libIGGui.dll", "libIGInsight.dll", "libIGLua.dll", "libIGMath.dll", "libIGOpt.dll", "libIGSg.dll",
			"libIGUtils.dll", "libIGViewer.dll", "libCriMovie.dll", "libMovie.dll"};
		for (const char* name : engine)
			if (const HMODULE module = GetModuleHandleA(name))
			{
				hook_module(module);
				++examined;
			}
		logger::write("loadlibrary trace: [Debug] LoadLibraryTrace=1 - %u loaded module(s) examined; LoadLibrary*/FindFirstFile* calls are logged", examined);
	}

	bool active() { return enabled; }
}
