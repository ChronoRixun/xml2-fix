// Synthetic retail-image host for the release gates (off-wire record, combined install, write union). No game runs:
// the retail XMen2.exe image is laid out at its own addresses inside this executable's reserved block, its imports are
// resolved from the engine folder, then the production dinput.dll is loaded so its complete DllMain (every install
// transaction) runs against those bytes. Afterwards every byte that changed in the retail image or in a loaded engine
// module is written to a TSV, and optionally a block of the DLL's own memory (the gameplay capability record) to a file.
//
// Usage: union_host.exe <dinput.dll> <retail XMen2.exe> <engine dir> <out.tsv> [<rva hex> <out.bin> [<bytes>]]
//   out.tsv rows: "I\t<module>\t<base hex>" for each module, then "W\t<module>\t<address hex>\t<old hex>\t<new hex>"
//   for each changed byte (XMen2.exe addresses are absolute, engine module addresses absolute too; subtract the I
//   base for an RVA). <rva> is the capability record object's RVA in the loaded DLL; <bytes> (default 108) are
//   written from the first "GCAP" within 64 KiB of it (the record's wire bytes follow its entry table).
// Exit codes: 0 done, 2 usage, 3 retail image, 4 imports, 5 the DLL did not load, 6 output.
// The process ends with TerminateProcess after the outputs are flushed: the DLL's threads and hooks never run on.
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

extern "C" unsigned char union_space[]; // union_space.asm: the code-class block right after the headers (.text$a)
namespace
{
	constexpr std::uintptr_t image_base = 0x400000;
	constexpr std::size_t space_size = 0x700000; // keep in step with union_space.asm

	std::vector<unsigned char> read_file(const wchar_t* path)
	{
		std::vector<unsigned char> out;
		FILE* f = _wfopen(path, L"rb");
		if (!f) return out;
		std::fseek(f, 0, SEEK_END);
		const long size = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		if (size > 0)
		{
			out.resize(std::size_t(size));
			if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
		}
		std::fclose(f);
		return out;
	}

	struct module_copy { std::string name; std::uintptr_t base = 0; std::vector<unsigned char> bytes; std::vector<bool> readable; };

	// The committed, readable pages of [base, base+size) copied (unreadable pages are skipped and marked).
	void snapshot(module_copy& m, std::size_t size)
	{
		m.bytes.assign(size, 0);
		m.readable.assign(size, false);
		std::uintptr_t at = m.base;
		while (at < m.base + size)
		{
			MEMORY_BASIC_INFORMATION info{};
			if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info))) break;
			const std::uintptr_t end = (std::min)(std::uintptr_t(reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize), std::uintptr_t(m.base + size));
			const DWORD p = info.Protect & 0xff;
			const bool ok = info.State == MEM_COMMIT && !(info.Protect & PAGE_GUARD) && p != PAGE_NOACCESS && p != 0;
			if (ok)
			{
				std::memcpy(m.bytes.data() + (at - m.base), reinterpret_cast<const void*>(at), end - at);
				std::fill(m.readable.begin() + (at - m.base), m.readable.begin() + (end - m.base), true);
			}
			at = end;
		}
	}

	// A notice the DLL shows during DllMain (e.g. "XML2 Fix: entity capacity unavailable") would wait for a click on
	// the host's private desktop: every dialog of this process is answered with OK and its title reported.
	BOOL CALLBACK answer_dialog(HWND window, LPARAM)
	{
		DWORD pid = 0;
		GetWindowThreadProcessId(window, &pid);
		char cls[32]{};
		GetClassNameA(window, cls, sizeof(cls));
		if (pid != GetCurrentProcessId() || std::strcmp(cls, "#32770") != 0) return TRUE;
		char title[256]{};
		GetWindowTextA(window, title, sizeof(title));
		static HWND reported = nullptr;
		if (window != reported) { reported = window; std::fprintf(stderr, "union host: dialog answered: %s\n", title); }
		const HWND button = GetDlgItem(window, IDOK);
		DWORD_PTR result = 0;
		if (button) SendMessageTimeoutW(button, BM_CLICK, 0, 0, SMTO_ABORTIFHUNG, 250, &result);
		PostMessageW(window, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), reinterpret_cast<LPARAM>(button));
		PostMessageW(window, WM_CLOSE, 0, 0);
		return TRUE;
	}
	DWORD WINAPI answer_dialogs(void* started)
	{
		SetEvent(static_cast<HANDLE>(started)); // running: past its own thread attach, so DllMain's loader lock is no wait
		for (;;) { EnumWindows(answer_dialog, 0); Sleep(200); }
	}

	std::size_t image_size(HMODULE module)
	{
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const unsigned char*>(module) + dos->e_lfanew);
		return nt->OptionalHeader.SizeOfImage;
	}
}

int wmain(int argc, wchar_t** argv)
{
	if (argc != 5 && argc != 7 && argc != 8)
	{
		std::fwprintf(stderr, L"usage: union_host <dinput.dll> <retail XMen2.exe> <engine dir> <out.tsv> [<rva hex> <out.bin> [<bytes>]]\n");
		return 2;
	}
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != image_base || reinterpret_cast<std::uintptr_t>(union_space) != image_base + 0x1000)
	{
		std::fwprintf(stderr, L"the host is not laid out at 0x400000 with its block at 0x401000\n");
		return 3;
	}

	// ---- The retail image at its own addresses ----------------------------------------------------------------
	const auto file = read_file(argv[2]);
	if (file.size() < 0x400) { std::fwprintf(stderr, L"cannot read %s\n", argv[2]); return 3; }
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.ImageBase != image_base ||
	    nt->OptionalHeader.SizeOfImage > space_size + 0x1000 || nt->OptionalHeader.SizeOfHeaders > 0x1000)
	{
		std::fwprintf(stderr, L"unexpected retail image layout\n");
		return 3;
	}
	const std::size_t size = nt->OptionalHeader.SizeOfImage;
	DWORD old = 0;
	if (!VirtualProtect(reinterpret_cast<void*>(image_base), 0x1000, PAGE_READWRITE, &old) ||
	    !VirtualProtect(union_space, space_size, PAGE_EXECUTE_READWRITE, &old))
	{
		std::fwprintf(stderr, L"VirtualProtect failed (%lu)\n", GetLastError());
		return 3;
	}
	std::memset(union_space, 0, size - 0x1000);
	const auto* sections = IMAGE_FIRST_SECTION(nt);
	for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
	{
		const auto& s = sections[i];
		if (s.VirtualAddress < 0x1000 || s.VirtualAddress + (std::max)(s.Misc.VirtualSize, s.SizeOfRawData) > size + 0xfff) { std::fwprintf(stderr, L"section outside the image\n"); return 3; }
		const DWORD raw = s.Misc.VirtualSize ? (std::min)(s.SizeOfRawData, s.Misc.VirtualSize) : s.SizeOfRawData;
		if (std::size_t(s.PointerToRawData) + raw > file.size()) { std::fwprintf(stderr, L"section past the file\n"); return 3; }
		std::memcpy(reinterpret_cast<void*>(image_base + s.VirtualAddress), file.data() + s.PointerToRawData, raw);
	}
	std::memcpy(reinterpret_cast<void*>(image_base), file.data(), nt->OptionalHeader.SizeOfHeaders);

	// ---- Its imports, from the engine folder (then the system) --------------------------------------------------
	SetDllDirectoryW(argv[3]);
	std::vector<std::pair<std::string, HMODULE>> engine;
	const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image_base + directory.VirtualAddress); directory.VirtualAddress && d->Name; ++d)
	{
		const char* name = reinterpret_cast<const char*>(image_base + d->Name);
		std::wstring path = std::wstring(argv[3]) + L"\\";
		for (const char* c = name; *c; ++c) path += wchar_t(static_cast<unsigned char>(*c));
		HMODULE module = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES ? LoadLibraryW(path.c_str()) : LoadLibraryA(name);
		if (!module) { std::fprintf(stderr, "cannot load %s (%lu)\n", name, GetLastError()); return 4; }
		engine.emplace_back(name, module);
		auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA32*>(image_base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
		auto* iat = reinterpret_cast<IMAGE_THUNK_DATA32*>(image_base + d->FirstThunk);
		for (; thunk->u1.AddressOfData; ++thunk, ++iat)
		{
			FARPROC proc = IMAGE_SNAP_BY_ORDINAL32(thunk->u1.Ordinal)
				? GetProcAddress(module, reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(IMAGE_ORDINAL32(thunk->u1.Ordinal))))
				: GetProcAddress(module, reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(image_base + thunk->u1.AddressOfData)->Name);
			if (!proc) { std::fprintf(stderr, "unresolved import in %s\n", name); return 4; }
			iat->u1.Function = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(proc));
		}
	}

	// The engine's own dinput import must be the system DLL: the production DLL is loaded below, after every import.
	if (const HMODULE early = GetModuleHandleW(L"dinput.dll"))
	{
		wchar_t path[MAX_PATH]{}, system[MAX_PATH]{};
		GetModuleFileNameW(early, path, MAX_PATH);
		GetSystemDirectoryW(system, MAX_PATH);
		if (_wcsnicmp(path, system, wcslen(system)) != 0) { std::fwprintf(stderr, L"a dinput.dll other than the system's was loaded with the engine: %s\n", path); return 4; }
	}

	// ---- Before: the retail image and every loaded engine module -----------------------------------------------
	std::vector<module_copy> before;
	before.push_back({"XMen2.exe", image_base});
	snapshot(before.back(), size);
	for (const auto& [name, module] : engine)
	{
		before.push_back({name, reinterpret_cast<std::uintptr_t>(module)});
		snapshot(before.back(), image_size(module));
	}

	// Diagnostics: UNION_IMAGE_OUT=<file> saves the laid-out retail image (headers and sections, imports resolved).
	wchar_t image_out[MAX_PATH]{};
	if (GetEnvironmentVariableW(L"UNION_IMAGE_OUT", image_out, MAX_PATH))
		if (FILE* f = _wfopen(image_out, L"wb")) { std::fwrite(before.front().bytes.data(), 1, before.front().bytes.size(), f); std::fclose(f); }

	// ---- The production DLL: its whole DllMain runs here ----------------------------------------------------------
	const HANDLE started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	CloseHandle(CreateThread(nullptr, 0, answer_dialogs, started, 0, nullptr));
	WaitForSingleObject(started, 10000);
	const HMODULE fix = LoadLibraryW(argv[1]);
	if (!fix) { std::fwprintf(stderr, L"cannot load %s (%lu)\n", argv[1], GetLastError()); return 5; }
	Sleep(1000);

	// ---- After ----------------------------------------------------------------------------------------------------
	FILE* tsv = _wfopen(argv[4], L"wb");
	if (!tsv) return 6;
	std::size_t changed = 0;
	for (const auto& m : before) std::fprintf(tsv, "I\t%s\t%08x\n", m.name.c_str(), unsigned(m.base));
	std::fprintf(tsv, "I\tdinput.dll\t%08x\n", unsigned(reinterpret_cast<std::uintptr_t>(fix)));
	for (const auto& m : before)
	{
		module_copy now{m.name, m.base};
		snapshot(now, m.bytes.size());
		for (std::size_t i = 0; i < m.bytes.size(); ++i)
			if (m.readable[i] && now.readable[i] && m.bytes[i] != now.bytes[i])
			{
				std::fprintf(tsv, "W\t%s\t%08x\t%02x\t%02x\n", m.name.c_str(), unsigned(m.base + i), m.bytes[i], now.bytes[i]);
				++changed;
			}
	}
	std::fclose(tsv);
	if (argc >= 7)
	{
		const auto rva = std::wcstoul(argv[5], nullptr, 16);
		const std::size_t bytes = argc == 8 ? std::wcstoul(argv[7], nullptr, 10) : 108;
		// <rva> names the gameplay capability record object (dinput.map); its wire bytes ("GCAP", protocol, ...) sit
		// after the entry table inside it, so the dump starts at the first "GCAP" within 64 KiB of the object.
		const auto* at = reinterpret_cast<const unsigned char*>(fix) + rva;
		for (std::size_t i = 0; i + 4 <= 0x10000; ++i)
			if (std::memcmp(at + i, "GCAP", 4) == 0) { at += i; break; }
		FILE* bin = _wfopen(argv[6], L"wb");
		if (!bin) return 6;
		std::fwrite(at, 1, bytes, bin);
		std::fclose(bin);
	}
	std::printf("union host: %zu changed bytes in %zu modules; dinput.dll at %p\n", changed, before.size(), static_cast<void*>(fix));
	std::fflush(stdout);
	std::fflush(stderr);
	TerminateProcess(GetCurrentProcess(), 0);
	return 0;
}
