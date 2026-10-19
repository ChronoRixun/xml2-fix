// Alchemy plugin enumeration of the drive root: the pattern rule, then the production guard hooked into this test
// executable's own FindFirstFileA / FindFirstFileW imports (standing in for libIGCore.dll's): the empty repository is
// answered "nothing there", a real folder is searched as before.
#include "plugin_scan.hpp"
#include "plugin_scan_rules.hpp"
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace logger
{
	std::vector<std::string> logged;
	void write(const char* fmt, ...)
	{
		char line[512];
		va_list args;
		va_start(args, fmt);
		std::vsnprintf(line, sizeof(line), fmt, args);
		va_end(args);
		logged.push_back(line);
	}
}

namespace
{
	int failures = 0, checks = 0;
	void expect(bool ok, const char* what)
	{
		++checks;
		if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
	}
}

// Each call loads the import afresh (the compiler may otherwise reuse one load of the read-only import for the whole of main).
__declspec(noinline) HANDLE call_a(const char* pattern, WIN32_FIND_DATAA* data) { return FindFirstFileA(pattern, data); }
__declspec(noinline) HANDLE call_w(const wchar_t* pattern, WIN32_FIND_DATAW* data) { return FindFirstFileW(pattern, data); }

int main()
{
	using plugin_scan_rules::empty_repository_pattern;
	// The rule.
	expect(empty_repository_pattern("\\*.*"), "\\*.* is the drive root");
	expect(empty_repository_pattern("\\\\*.*"), "\\\\*.* is the drive root");
	expect(empty_repository_pattern("/*.*"), "/*.* is the drive root");
	expect(empty_repository_pattern(L"\\*.*"), "wide \\*.* is the drive root");
	expect(!empty_repository_pattern("*.*"), "a bare *.* is the current directory, not the drive root");
	expect(!empty_repository_pattern("plugins\\*.*"), "plugins\\*.* has a directory");
	expect(!empty_repository_pattern("D:\\Games\\X\\plugins\\*.*"), "a full path has a directory");
	expect(!empty_repository_pattern("C:\\*.*"), "C:\\*.* names the drive (not what an empty location produces)");
	expect(!empty_repository_pattern("\\*.dll"), "a different mask is left alone");
	expect(!empty_repository_pattern("\\\\\\*.*"), "three separators are not the empty repository");
	expect(!empty_repository_pattern("\\*.*x"), "trailing text is left alone");
	expect(!empty_repository_pattern(static_cast<const char*>(nullptr)), "null is left alone");
	expect(!empty_repository_pattern(""), "empty is left alone");

	// The production hook, into this image. The calls below go through this image's FindFirstFile imports.
	WIN32_FIND_DATAA a{};
	WIN32_FIND_DATAW w{};
	char temp[MAX_PATH]{};
	GetTempPathA(MAX_PATH, temp);
	const std::string real_pattern = std::string(temp) + "*.*";

	HANDLE before = call_a(real_pattern.c_str(), &a);
	expect(before != INVALID_HANDLE_VALUE, "the temp folder is searchable before the guard");
	if (before != INVALID_HANDLE_VALUE) FindClose(before);
	// The drive root may or may not be searchable here; only the guarded answer matters below.

	expect(plugin_scan::install_into(GetModuleHandleW(nullptr)), "the guard hooks this image's FindFirstFile import");
	expect(plugin_scan::installed(), "installed() is true after a hook");

	SetLastError(0);
	HANDLE root = call_a("\\*.*", &a);
	expect(root == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, "the empty repository is answered ERROR_FILE_NOT_FOUND (A)");
	SetLastError(0);
	root = call_w(L"\\*.*", &w);
	expect(root == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, "the empty repository is answered ERROR_FILE_NOT_FOUND (W)");
	expect(plugin_scan::skipped() == 2, "both skips were counted");
	int skip_lines = 0;
	for (const auto& line : logger::logged) if (line.find("alchemy plugins: skipped the empty plugin repository (drive root scan)") != std::string::npos) ++skip_lines;
	expect(skip_lines == 1, "the skip is logged once");

	HANDLE after = call_a(real_pattern.c_str(), &a);
	expect(after != INVALID_HANDLE_VALUE, "a real folder is still searched after the guard (A)");
	if (after != INVALID_HANDLE_VALUE) FindClose(after);
	const std::wstring wide_pattern(real_pattern.begin(), real_pattern.end());
	HANDLE after_w = call_w(wide_pattern.c_str(), &w);
	expect(after_w != INVALID_HANDLE_VALUE, "a real folder is still searched after the guard (W)");
	if (after_w != INVALID_HANDLE_VALUE) FindClose(after_w);
	expect(plugin_scan::skipped() == 2, "real searches are not counted as skips");

	expect(!plugin_scan::install_into(nullptr), "no module, no hook");
	std::printf("plugin scan: %d check(s), %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
