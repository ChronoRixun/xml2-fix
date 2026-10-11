#include "explosive_first_hit.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace explosive_first_hit
{
	namespace
	{
		bool wanted = false, applied = false;
		constexpr std::uintptr_t image_base = 0x400000;
		constexpr std::uintptr_t site = 0x49ab58;
		// GetExplosionAttack copies the authored blast, then chooses its attack ID.
		// XML1 0x969d9 always increments; PC only does so with the authored flag.
		// Keep the native increment/store and both epilogues; bypass only that branch.
		constexpr std::array<std::uint8_t, 28> expected{
			0xf6, 0x87, 0xab, 0x00, 0x00, 0x00, 0x08, 0x74, 0x0d,
			0x8b, 0x47, 0x30, 0x40, 0x89, 0x46, 0x28,
			0x5e, 0x5b, 0x5f, 0xc2, 0x04, 0x00,
			0x8b, 0x4f, 0x30, 0x89, 0x4e, 0x28};
		bool retail()
		{
			__try { return std::memcmp(reinterpret_cast<const void*>(0x49ab51), expected.data(), expected.size()) == 0; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
	}
	bool installed() { return wanted && applied; }
	bool verified() { return !wanted || applied; }
	void install(const HMODULE game)
	{
		if (applied) return; // immutable successfully installed rule, never reread a changed INI
		wanted = ini::flag(L"Game", L"ExplosiveFirstHit", false);
		if (!wanted)
		{
			logger::write("explosive first hit: off - native authored flag");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != image_base || !retail())
		{
			logger::write("explosive first hit: image or 0x49AB51 isn't retail - nothing patched");
			return;
		}
		auto* code = reinterpret_cast<std::uint8_t*>(site);
		DWORD old;
		if (!VirtualProtect(code, 2, PAGE_EXECUTE_READWRITE, &old))
		{
			logger::write("explosive first hit: can't unprotect 0x49AB58 (error %lu) - nothing patched", GetLastError());
			return;
		}
		code[0] = code[1] = 0x90;
		const bool flushed = FlushInstructionCache(GetCurrentProcess(), code, 2) != FALSE;
		applied = flushed && code[0] == 0x90 && code[1] == 0x90;
		if (!applied) { code[0] = expected[7]; code[1] = expected[8]; FlushInstructionCache(GetCurrentProcess(), code, 2); }
		DWORD unused;
		VirtualProtect(code, 2, old, &unused);
		if (!applied) { logger::write("explosive first hit: install verification failed; online agreement refused"); return; }
		logger::write("explosive first hit: on - XML1 fresh blast attack ID at 0x49AB58; native AoE unchanged");
	}
}
