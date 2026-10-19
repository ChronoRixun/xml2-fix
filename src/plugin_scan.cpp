#include "plugin_scan.hpp"
#include "iat_hook.hpp"
#include "log.hpp"
#include "plugin_scan_rules.hpp"

namespace plugin_scan
{
	namespace
	{
		using find_a_t = HANDLE(WINAPI*)(LPCSTR, LPWIN32_FIND_DATAA);
		using find_w_t = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
		find_a_t real_find_a = nullptr;
		find_w_t real_find_w = nullptr;
		bool hooked = false;
		unsigned skipped_count = 0;

		void note_skip()
		{
			// One line, the first time; later scans are counted.
			if (skipped_count++ == 0)
				logger::write("alchemy plugins: skipped the empty plugin repository (drive root scan)");
		}

		HANDLE WINAPI find_first_a(LPCSTR pattern, LPWIN32_FIND_DATAA data)
		{
			if (plugin_scan_rules::empty_repository_pattern(pattern))
			{
				note_skip();
				SetLastError(ERROR_FILE_NOT_FOUND);
				return INVALID_HANDLE_VALUE;
			}
			return real_find_a(pattern, data);
		}

		HANDLE WINAPI find_first_w(LPCWSTR pattern, LPWIN32_FIND_DATAW data)
		{
			if (plugin_scan_rules::empty_repository_pattern(pattern))
			{
				note_skip();
				SetLastError(ERROR_FILE_NOT_FOUND);
				return INVALID_HANDLE_VALUE;
			}
			return real_find_w(pattern, data);
		}
	}

	bool install_into(HMODULE module)
	{
		if (!module) return false;
		// The forwarding targets are what the module imported (the mod loader's hook, if any, or kernel32's function).
		// Take the kernel32 functions first so a module that imports only one of them still forwards the other correctly.
		if (!real_find_a || !real_find_w)
		{
			const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
			if (!real_find_a) real_find_a = reinterpret_cast<find_a_t>(GetProcAddress(kernel, "FindFirstFileA"));
			if (!real_find_w) real_find_w = reinterpret_cast<find_w_t>(GetProcAddress(kernel, "FindFirstFileW"));
		}
		bool any = false;
		if (auto* previous = iat_hook::hook(module, "KERNEL32.dll", "FindFirstFileA", 0, reinterpret_cast<void*>(&find_first_a)))
		{
			real_find_a = reinterpret_cast<find_a_t>(previous);
			any = true;
		}
		if (auto* previous = iat_hook::hook(module, "KERNEL32.dll", "FindFirstFileW", 0, reinterpret_cast<void*>(&find_first_w)))
		{
			real_find_w = reinterpret_cast<find_w_t>(previous);
			any = true;
		}
		if (any) hooked = true;
		return any;
	}

	void install()
	{
		const HMODULE core = GetModuleHandleA("libIGCore.dll");
		if (!core)
		{
			logger::write("alchemy plugins: libIGCore.dll is not loaded; the drive-root plugin scan is not guarded");
			return;
		}
		if (!install_into(core))
			logger::write("alchemy plugins: libIGCore.dll does not import FindFirstFile; the drive-root plugin scan is not guarded");
		else
			logger::write("alchemy plugins: libIGCore.dll plugin scan guarded (an empty repository location is not searched)");
	}

	bool installed() { return hooked; }
	unsigned skipped() { return skipped_count; }
}
