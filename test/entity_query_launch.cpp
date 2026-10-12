// Launcher for the entity query test: a CRT-free executable based at 0x400000 whose only job is to own the fixture
// section at the retail addresses (the retail helpers the query code calls start at 0x419c90, below where any
// image with a C runtime can place a section) and to load the test body DLL next to it.
#include <windows.h>

#pragma section(".peerfix", read, write)
__declspec(allocate(".peerfix")) unsigned char fixture_space[0x380000] = {1};

extern "C" void __cdecl launch()
{
	fixture_space[1] = 1;
	const HMODULE body = LoadLibraryW(L"xml2_entity_query_test_body.dll");
	if (!body) ExitProcess(3);
	const auto run = reinterpret_cast<int (__cdecl*)()>(GetProcAddress(body, "run_entity_query_test"));
	ExitProcess(static_cast<UINT>(run ? run() : 4));
}
