#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../src/join_in_progress_rules.hpp"

// Optional union-host fixture. Executes the installed production formatter and
// both GNS entry bridges on synthetic data, never an engine session. The removal
// continuation is replaced only in this disposable test process.
namespace coop_lifecycle_probe
{
inline int run(bool installed)
{
    int failures = 0;
    auto check = [&](bool ok, const char* text) {
        std::printf("%s coop fixture: %s\n", ok ? "PASS" : "FAIL", text);
        failures += !ok;
    };
    using namespace join_in_progress_rules;
    const auto* site = reinterpret_cast<const unsigned char*>(roster_hero_call);
    const bool retail = std::memcmp(site, roster_hero_original.data(), roster_hero_original.size()) == 0;
    check(installed ? !retail && site[0] == 0xe8 : retail,
          installed ? "production JoinInProgress roster hook installed" : "OFF/refused roster call remains retail");
    if (failures || !installed) return failures;
    std::int32_t displacement = 0;
    std::memcpy(&displacement, site + 1, 4);
    using format_t = int(__cdecl*)(char*, int, const char*, ...);
    const auto patched = reinterpret_cast<format_t>(roster_hero_call + 5 + displacement);
    const auto native = reinterpret_cast<format_t>(roster_format);
    for (const char* hero : {"", "Wolverine", "Cyclops"}) {
        char actual[128]{}, expected[128]{};
        const int a = patched(actual, 128, " (%s)", hero);
        const int b = native(expected, 128, " (%s)", hero[0] ? hero : "Spectating");
        check(a == b && std::strcmp(actual, expected) == 0,
              hero[0] ? "real native hero formatting/return preserved" : "B3 empty hero uses Spectating through the real formatter");
    }
    char actual[16], expected[16];
    std::memset(actual, 0x5a, sizeof actual); std::memset(expected, 0x5a, sizeof expected);
    const int a = patched(actual, 8, " (%s)", "");
    const int b = native(expected, 8, " (%s)", "Spectating");
    check(a == b && std::memcmp(actual, expected, sizeof actual) == 0,
          "actual native truncation, return and buffer canary preserved");

    check(*reinterpret_cast<const unsigned char*>(0x611550) == 0xe9 &&
          *reinterpret_cast<const unsigned char*>(0x612c40) == 0xe9, "production GNS notice/removal bridges installed");
    if (failures) return failures;
    // The notice's native continuation returns immediately for manager+0x41c=0.
    alignas(8) unsigned char session[0x500]{};
    using notice_t = void(__fastcall*)(void*, void*, int, unsigned, unsigned, unsigned);
    std::uintptr_t before = 0, after = 0;
    __asm mov before, esp
    reinterpret_cast<notice_t>(0x611550)(session, nullptr, 0, 0x10ce, 0x10cf, 0xabcd0001);
    __asm mov after, esp
    check(before == after, "notice bridge and native tail balance the original four arguments");

    // Owned synthetic tail for the lost bridge's replayed push ebp / mov ebp,esp
    // / and esp,-8. Restore the caller's original frame and pop its three args.
    constexpr unsigned char tail[]{0x8b,0xe5,0x5d,0xc2,0x0c,0x00};
    unsigned char saved[sizeof tail]{}; DWORD old = 0;
    auto* continuation = reinterpret_cast<void*>(0x612c46);
    if (!VirtualProtect(continuation, sizeof tail, PAGE_EXECUTE_READWRITE, &old)) return 1;
    std::memcpy(saved, continuation, sizeof saved); std::memcpy(continuation, tail, sizeof tail);
    FlushInstructionCache(GetCurrentProcess(), continuation, sizeof tail);
    using lost_t = unsigned(__fastcall*)(void*, void*, void*, unsigned, unsigned);
    __asm mov before, esp
    reinterpret_cast<lost_t>(0x612c40)(session, nullptr, session, 0x10ce, 0xabcd0001);
    __asm mov after, esp
    check(before == after, "removal bridge balances the original three arguments and aligned frame");
    std::memcpy(continuation, saved, sizeof saved); FlushInstructionCache(GetCurrentProcess(), continuation, sizeof saved);
    DWORD ignored = 0; check(VirtualProtect(continuation, sizeof tail, old, &ignored) != 0, "synthetic tail restored");
    std::printf("coop fixture: %d failures\n", failures);
    return failures;
}
}
