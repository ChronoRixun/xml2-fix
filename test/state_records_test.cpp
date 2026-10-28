// Retail code executes only inside this test's fixed-base fixture. No game
// process or game DLL is loaded. External calls use ABI-correct recording fakes.
#include "state_records.hpp"
#include "state_records_rules.hpp"
#include "entity_capacity_sites.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

extern "C" std::uint8_t fixture_space[0x400000];
std::uint32_t g_entry, g_a, g_c, g_d, g_b, g_s, g_bp, g_saved;
std::uint32_t g_out_a, g_out_c, g_out_d, g_out_s, g_out_flags;
std::uint32_t g_before, g_after, g_fs_before, g_fs_after;
std::uint8_t g_stack[32];

__declspec(naked) void landing()
{
    __asm {
        mov g_out_a, eax
        mov g_out_c, ecx
        mov g_out_d, edx
        mov g_out_s, esi
        pushfd
        pop g_out_flags
        lea esi, [esp+18h]
        lea edi, g_stack
        mov ecx, 8
        cld
        rep movsd
        mov esp, g_saved
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

__declspec(naked) void enter_snippet()
{
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov g_saved, esp
        sub esp, 80h
        mov edi, esp
        mov eax, 0cccccccch
        mov ecx, 20h
        cld
        rep stosd
        // SF=1, OF=0: the loop tail must preserve this JL condition.
        push 282h
        popfd
        mov eax, g_a
        mov ecx, g_c
        mov edx, g_d
        mov ebx, g_b
        mov esi, g_s
        mov ebp, g_bp
        jmp g_entry
    }
}

__declspec(naked) void measured_call()
{
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov g_before, esp
        mov eax, fs:[0]
        mov g_fs_before, eax
        mov ecx, g_c
        mov ebp, g_bp
        call g_entry
        mov g_after, esp
        mov eax, fs:[0]
        mov g_fs_after, eax
        mov esp, g_before
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

namespace
{
    using namespace state_records_rules;
    int checks = 0, failures = 0;
    void check(bool ok, const std::string& s)
    { ++checks; failures += !ok; std::printf("%s %s\n", ok ? "PASS" : "FAIL", s.c_str()); }
    std::string hex(const std::vector<std::uint8_t>& b)
    {
        std::string s;
        for (auto n : b) { char h[3]; std::snprintf(h, sizeof h, "%02x", n); s += h; }
        return s;
    }
    void manifest()
    {
        std::puts("State-record contribution byte ledger (retail image base 0x400000).\nEvery quoted retail block is a guard. No startup installation.\nCave bodies below are emitted at illustrative DLL address 0x10000000; rel32 is rebased at contribution time.");
        for (const auto& s : sites)
            std::printf("%08x guard=%s W2=%s %s\n", s.va, s.retail, hex(replacement(s)).c_str(), s.what);
        std::printf("[%08x,%08x) restore frame owner SHA256=%s (read-only region guard)\n", restore_begin, restore_end, restore_sha256);
        for (const auto& g : invariants)
            std::printf("%08x guard=%s (read-only) %s\n", g.va, g.retail, g.what);
        for (const auto& s : caves)
            std::printf("[%08x,%08x) %s\n  guard=%s\n  SHA256=%s\n  W2=E9 rel32(cave), remaining original bytes unreachable\n  cave@10000000=%s\n",
                        s.va, s.end, s.what, s.retail, s.sha256, hex(cave(s, 0x10000000)).c_str());
        std::puts("E15-07 sweep [4654f0,466b00), [487330,487700):\n  465f17 83c618; 4660ee 83c718; 4662fd 83c618; 466415 83c618.\n  All four are inside other owners' whole replacements; no external stride advance found.\nFrame: 4667f5 allocation grows 24->2c; stack record remains esp+18 through esp+37.\n  Cookie/SEH references and epilogue move +8; funclet 673630 moves ebp-28 to ebp-30.\n  Per-invocation storage; reentrant calls have independent records. No scratch backing.\n  Retail FuncInfo at 6bbe30 and unwind-map entry at 6bbe28 remain valid: entry targets 673630.\nOverlaps: zero writes into Phase A save literals or any relocation site; no copied-block literals superseded.\n  4664d0 capture hook; bound bridges 466659/46667a/46668a/4666e7/4666f3;\n  numbering 48624c; restore_hook call 486260; zone strings 4668b0; smart manager [4b6e70,4b7744) untouched.\nLedger corrections: 465c2f result omits +4 until 465c3e 83c004; flags-B test is 465c35 f6401508.\n  465e7e f6461101 tests flags B, and 465e85 e8863d0100 calls 479c10; both retained.\n  Capture tail ends at 4667a3 (last store 46679f 66894616).\n  Restore tail ends at 466887 (last store 466882 668944242e).\n  Restore loop clear [466906,466932) and EH cleanup 673630 8d4dd8 require widening too.\n  No smart-manager bytes belong to this contribution.\nLimits: full native exception dispatch is not exercised; the native cleanup funclet itself is.\n  External game calls are fakes; startup integration and game validation remain with the integrator.");
    }
    void* at(std::uint32_t va) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va)); }
    std::uint32_t addr(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }
    template<class T> T load(const void* p) { T n; std::memcpy(&n, p, sizeof n); return n; }
    template<class T> void store(void* p, T n) { std::memcpy(p, &n, sizeof n); }
    void jump(std::uint32_t va, const void* target)
    {
        const auto w = patch_transaction::relocation(va, "e9", true, addr(target), 0, "fixture fake");
        std::memcpy(at(va), w.bytes.data(), w.bytes.size());
    }
    void pad(std::uint32_t va) { jump(va, reinterpret_cast<const void*>(&landing)); }
    std::vector<std::uint8_t> binary;
    bool map_image()
    {
        if (binary.size() < 0x100 || load<std::uint16_t>(binary.data()) != 0x5a4d) return false;
        const auto pe = load<std::uint32_t>(binary.data() + 0x3c);
        if (pe + 24 > binary.size()) return false;
        const auto opt = pe + 24, sections = opt + load<std::uint16_t>(binary.data() + pe + 20);
        if (sections + load<std::uint16_t>(binary.data() + pe + 6) * 40 > binary.size()) return false;
        if (load<std::uint32_t>(binary.data() + opt + 28) != 0x400000) return false;
        std::memset(fixture_space, 0, sizeof fixture_space);
        for (unsigned i = 0; i < load<std::uint16_t>(binary.data() + pe + 6); ++i)
        {
            const auto row = sections + 40 * i;
            const auto va = 0x400000 + load<std::uint32_t>(binary.data() + row + 12);
            const auto size = load<std::uint32_t>(binary.data() + row + 16);
            const auto off = load<std::uint32_t>(binary.data() + row + 20);
            if (off + std::uint64_t(size) > binary.size()) return false;
            const auto lo = std::max(va, addr(fixture_space));
            const auto hi = std::min<std::uint64_t>(std::uint64_t(va) + size, addr(fixture_space) + sizeof fixture_space);
            if (lo < hi) std::memcpy(at(lo), binary.data() + off + lo - va, static_cast<std::size_t>(hi - lo));
        }
        return true;
    }

    using native_record = void(__fastcall*)(void*, void*);
    using native_dispose = void(__fastcall*)(void*, void*, unsigned);
    unsigned disposal_calls = 0;
    void* disposal_record = nullptr;
    std::uint8_t disposal_flag = 0;
    void __cdecl dispose(void* r)
    { ++disposal_calls; disposal_record = r; disposal_flag = static_cast<std::uint8_t*>(r)[0x11]; }

    void __stdcall vector_clear(void* p, unsigned stride, unsigned count, native_record fn)
    { while (count) fn(static_cast<std::uint8_t*>(p) + --count * stride, nullptr); }
    void __stdcall vector_construct(void* p, unsigned stride, unsigned count, native_record fn, native_record)
    { for (unsigned i = 0; i < count; ++i) fn(static_cast<std::uint8_t*>(p) + i * stride, nullptr); }

    bool wide_mode = false;
    unsigned cookie_calls = 0;
    bool cookie_ok = true;
    std::vector<std::array<std::uint8_t, 32>> appended;
    const void* smart_vt[8]{};
    const void* defs_vt[9]{};
    struct object { const void* const* vt; } smart{smart_vt}, defs{defs_vt};
    void __fastcall smart_reset(void*, void*) { appended.clear(); }
    void __fastcall smart_append(void*, void*, const std::uint8_t* p)
    {
        std::array<std::uint8_t, 32> r; r.fill(0xcc);
        std::memcpy(r.data(), p, wide_mode ? 32 : 24); appended.push_back(r);
    }
    void* __cdecl smart_get() { return &smart; }
    void* __cdecl defs_get() { return &defs; }
    void* __fastcall defs_find(void*, void*, const char*) { return nullptr; }
    void __fastcall cookie_check(std::uint32_t value, void*)
    { ++cookie_calls; cookie_ok &= value == load<std::uint32_t>(at(0x6f38f8)); }
    void fakes()
    {
        jump(0x479c10, reinterpret_cast<const void*>(&dispose));
        jump(0x672274, reinterpret_cast<const void*>(&vector_clear));
        jump(0x6722d4, reinterpret_cast<const void*>(&vector_construct));
        jump(0x4b76f0, reinterpret_cast<const void*>(&smart_get));
        jump(0x4625c0, reinterpret_cast<const void*>(&defs_get));
        jump(0x672161, reinterpret_cast<const void*>(&cookie_check));
        smart_vt[3] = reinterpret_cast<const void*>(&smart_reset);
        smart_vt[7] = reinterpret_cast<const void*>(&smart_append);
        defs_vt[8] = reinterpret_cast<const void*>(&defs_find);
    }

    std::array<std::uint8_t, 32> expected_clear(bool wide, bool constructor)
    {
        std::array<std::uint8_t, 32> want; want.fill(0xa5);
        std::fill_n(want.begin(), 8, std::uint8_t(0));
        if (constructor) std::fill(want.begin() + 8, want.begin() + 0x12, std::uint8_t(0));
        want[8] = 0xff; want[9] = want[10] = want[0x10] = want[0x11] = 0;
        if (wide)
        {
            want[0x12] = want[0x13] = 0xff;
            std::fill(want.begin() + 0x14, want.end(), std::uint8_t(0));
        }
        else std::fill(want.begin() + 0x12, want.begin() + 0x18, std::uint8_t(0));
        return want;
    }

    void records(bool wide)
    {
        const std::string tag = wide ? "W2 " : "retail ";
        const unsigned stride = wide ? record_size : 24, count = wide ? profile.states : 768;
        for (const auto va : {0x487610u, 0x4664a0u, 0x465e70u})
        {
            std::vector<std::uint8_t> pool(count * stride + 32, 0xa5);
            const auto want = expected_clear(wide, va == 0x487610);
            for (unsigned i = 0; i < count; ++i)
            {
                auto* p = pool.data() + i * stride;
                if (va == 0x465e70) reinterpret_cast<native_dispose>(at(va))(p, nullptr, 0);
                else reinterpret_cast<native_record>(at(va))(p, nullptr);
            }
            bool ok = true;
            for (unsigned i = 0; i < count; ++i) ok &= !std::memcmp(pool.data() + i * stride, want.data(), stride);
            ok &= std::all_of(pool.begin() + count * stride, pool.end(), [](auto b) { return b == 0xa5; });
            check(ok, tag + patch_transaction::hex(va) + " initializes every field at stride " + std::to_string(stride) + " across " + std::to_string(count) + " records; trailing canary intact");
        }
        bool disposal_ok = true;
        for (unsigned requested : {0u, 1u}) for (unsigned flags_a : {0u, 1u}) for (unsigned flags_b : {0u, 1u})
        {
            std::array<std::uint8_t, 48> r; r.fill(0xa5); r[0x10] = static_cast<std::uint8_t>(flags_a); r[0x11] = static_cast<std::uint8_t>(flags_b);
            disposal_calls = 0; disposal_record = nullptr;
            reinterpret_cast<native_dispose>(at(0x465e70))(r.data(), nullptr, requested);
            const bool called = requested && flags_b;
            disposal_ok &= disposal_calls == unsigned(called) && (!called || (disposal_record == r.data() && disposal_flag == flags_b));
        }
        check(disposal_ok, tag + "465e70 retains request AND flags-B bit 0 disposal test, original record argument, exactly one cdecl call");

        // Execute the actual vector argument pushes and wrappers. The W2 count
        // is supplied by a separate fixture integration contribution below.
        for (const auto va : {0x487640u, 0x487660u})
        {
            const unsigned prefix = va == 0x487660 ? 4 : 0;
            std::vector<std::uint8_t> pool(prefix + count * stride + 32, 0xa5);
            reinterpret_cast<native_record>(at(va))(pool.data(), nullptr);
            const auto want = expected_clear(wide, false);
            bool ok = true;
            for (unsigned i = 0; i < count; ++i) ok &= !std::memcmp(pool.data() + prefix + i * stride, want.data(), stride);
            ok &= std::all_of(pool.begin(), pool.begin() + prefix, [](auto b) { return b == 0xa5; });
            ok &= std::all_of(pool.begin() + prefix + count * stride, pool.end(), [](auto b) { return b == 0xa5; });
            check(ok, tag + patch_transaction::hex(va) + " native clear vector count/stride and canaries");
        }
        std::vector<std::uint8_t> pool(4 + count * stride + 32, 0xa5);
        pad(0x4875ad);
        g_entry = 0x487589; g_c = addr(pool.data()); enter_snippet();
        const auto want = expected_clear(wide, true);
        bool ok = load<std::uint32_t>(pool.data()) == 0;
        for (unsigned i = 0; i < count; ++i) ok &= !std::memcmp(pool.data() + 4 + i * stride, want.data(), stride);
        ok &= std::all_of(pool.end() - 32, pool.end(), [](auto b) { return b == 0xa5; });
        check(ok, tag + "487595/48759a native construction vector pushes count/stride; every record and canary");
    }

    void restore_function(bool wide)
    {
        const auto& p = wide ? profile : save_format_rules::retail_profile;
        const std::string tag = wide ? "W2 " : "retail ";
        std::vector<std::uint8_t> zone(p.record_size, 0);
        const unsigned indices[] = {1, p.states - 1};
        store(zone.data() + save_format_rules::smart_count(p), 2u);
        std::vector<std::array<std::uint8_t, 32>> want;
        for (unsigned n = 0; n < 2; ++n)
        {
            store(zone.data() + save_format_rules::smart_list(p) + n * 2, static_cast<std::uint16_t>(indices[n]));
            auto* r = zone.data() + 4 + indices[n] * p.state_record_size;
            for (unsigned j = 0; j < p.state_record_size; ++j) r[j] = static_cast<std::uint8_t>(0x41 + n * 17 + j);
            store(r, 0u); // empty definition handle -> native empty string branch
            if (wide)
            {
                store(r + 0x12, static_cast<std::uint16_t>(0x321 + n));
                store(r + 0x14, 12345.25f); store(r + 0x18, -45678.5f); store(r + 0x1c, -0.0f);
            }
            std::array<std::uint8_t, 32> w; w.fill(0xcc); std::memcpy(w.data(), r, p.state_record_size); want.push_back(w);
        }
        const auto original = zone;
        cookie_calls = 0; cookie_ok = true;
        g_entry = 0x4667e0; g_c = addr(zone.data()); measured_call();
        check(appended == want && zone == original, tag + "full native restore: two iterations including last record; append-copy carries every byte, slot and floats; source unchanged");
        check(g_before == g_after && g_fs_before == g_fs_after && cookie_calls == 1 && cookie_ok,
              tag + "full native restore balances ESP, restores SEH registration, and checks the saved security cookie");
        store(zone.data() + save_format_rules::smart_count(p), 0u);
        measured_call();
        check(appended.empty() && g_before == g_after && g_fs_before == g_fs_after && cookie_ok,
              tag + "empty restore takes no-record epilogue with balanced stack and SEH");

        // Execute the actual exception cleanup funclet with the EH frame base
        // used by retail metadata. This also catches a stale -0x28 displacement.
        std::array<std::uint8_t, 96> frame; frame.fill(0xa5);
        const unsigned offset = wide ? 0x30 : 0x28;
        g_entry = 0x673630; g_bp = addr(frame.data() + 64); measured_call();
        const auto clear = expected_clear(wide, false);
        check(!std::memcmp(frame.data() + 64 - offset, clear.data(), p.state_record_size) && g_before == g_after,
              tag + "native exception cleanup funclet clears the correct temporary record");
    }

    void snippets(bool wide)
    {
        const std::string tag = wide ? "W2 " : "retail ";
        const unsigned stride = wide ? record_size : 24;
        std::vector<std::uint8_t> zone(4 + 4096 * record_size + 64, 0x5a);
        for (unsigned site = 0; site < 5; ++site)
        {
            const auto& s = sites[site];
            const auto end = s.va + static_cast<unsigned>(bytes(s.retail).size());
            std::array<std::uint8_t, 5> saved; std::memcpy(saved.data(), at(end), saved.size());
            pad(end);
            bool ok = true;
            for (unsigned i : {0u, 1u, 767u, 4095u})
            {
                g_entry = s.va; g_a = i; g_c = site == 3 ? i : addr(zone.data());
                g_b = g_bp = addr(zone.data()); enter_snippet();
                const auto expected = addr(zone.data()) + i * stride;
                const auto got = site == 0 || site == 4 ? g_out_a : site == 2 ? g_out_c : g_out_s;
                ok &= got == expected + (site == 0 ? 0 : 4);
                if (site == 0) ok &= got + 0x15 == expected + 4 + 0x11;
                if (site == 4) ok &= g_out_c == 0x5a5a5a5a;
            }
            check(ok, tag + patch_transaction::hex(s.va) + " scaled address for indices 0, 1, 767, 4095 (+4 placement and flags-B offset)");
            std::memcpy(at(end), saved.data(), saved.size());
        }
        std::array<std::uint8_t, 48> src, dest;
        for (unsigned j = 0; j < src.size(); ++j) src[j] = static_cast<std::uint8_t>(0x30 + j);
        store(src.data() + 0x12, std::uint16_t(1023));
        store(src.data() + 0x14, 65536.5f); store(src.data() + 0x18, -90000.25f); store(src.data() + 0x1c, -0.0f);
        dest.fill(0xa5); pad(0x4667a3);
        g_entry = 0x466745; g_a = addr(src.data()); g_s = addr(dest.data()); enter_snippet();
        check(!std::memcmp(dest.data(), src.data(), stride) && std::all_of(dest.begin() + stride, dest.end(), [](auto b) { return b == 0xa5; }),
              tag + "native capture copy carries complete record; next-record canary intact");
        pad(0x466887);
        g_entry = 0x466843; g_a = addr(src.data()); g_c = load<std::uint32_t>(src.data()); enter_snippet();
        check(!std::memcmp(g_stack, src.data(), stride) && std::all_of(g_stack + stride, g_stack + 32, [](auto b) { return b == 0xcc; }),
              tag + "native restore copy fills stack record with exact slot/float bits");
        pad(0x466932);
        g_entry = 0x466906; g_b = 0; g_a = 123; g_d = 456; enter_snippet();
        auto want = expected_clear(wide, false);
        for (auto& b : want) if (b == 0xa5) b = 0xcc;
        check(!std::memcmp(g_stack, want.data(), 32) && g_out_a == 123 && g_out_d == 456 && (g_out_flags & 0x880) == 0x80,
              tag + "native restore loop clear: slot/positions, payload preservation, loop registers and JL flags");
    }

    void integrate_counts(patch_transaction::transaction& tx)
    {
        patch_transaction::contribution integration; integration.owner = "fixture Phase A count/offset integration";
        for (unsigned va : {0x487595u, 0x487645u, 0x487665u})
        {
            integration.guards.push_back({va, bytes("6800030000"), "Phase A vector count"});
            auto b = bytes("68"); dword(b, profile.states);
            integration.writes.push_back({va, b, "Phase A W2 vector count"});
        }
        // Full native restore also consumes these three literal sites; they
        // remain owned by the integrator, never state_records.
        for (unsigned va : {0x466811u, 0x466825u, 0x4668f2u})
        {
            const bool list = va == 0x466825;
            const auto retail = bytes(va == 0x466811 ? "8b8574520000" : list ? "8d85704c0000" : "8b8d74520000");
            auto b = std::vector<std::uint8_t>(retail.begin(), retail.begin() + 2);
            dword(b, list ? save_format_rules::smart_list(profile) : save_format_rules::smart_count(profile));
            integration.guards.push_back({va, retail, "Phase A smart offset"});
            integration.writes.push_back({va, b, "Phase A W2 smart offset"});
        }
        tx.add(std::move(integration));
    }
}

int main(int argc, char** argv)
{
    std::setbuf(stdout, nullptr);
    if (argc > 1 && std::string(argv[1]) == "--describe") { manifest(); return 0; }
    const char* path = argc > 1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");
    if (!path || !*path) { std::puts("SKIP state records native execution: pass XMen2.exe or set XML2_FIX_TEST_EXE\nState records: 0 checks, 0 failure(s), 1 skip"); return 0; }
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000) || addr(fixture_space) > 0x465000 || addr(fixture_space) + sizeof fixture_space < 0x6f3900)
    { std::puts("FAIL fixture address range/base"); return 2; }
    std::ifstream input(path, std::ios::binary); binary.assign(std::istreambuf_iterator<char>(input), {});
    if (!input || binary.empty()) { std::puts("FAIL cannot read executable"); return 2; }
    DWORD old = 0;
    if (!VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old)) return 2;
    for (bool wide : {false, true})
    {
        if (!map_image()) { std::puts("FAIL invalid retail PE input"); return 2; }
        wide_mode = wide;
        patch_transaction::contribution c; std::string why;
        check(state_records::contribute_w2(c, why), "prepare production W2 contribution " + why);
        check(patch_transaction::verify_expectations(c, why), "all quoted retail guards and seven SHA-256 regions match executable " + why);
        if (!why.empty()) return 1;
        bool derived = true;
        for (const auto& s : sites) derived &= replacement(s) == bytes(s.w2);
        check(derived, "descriptor-derived instruction bytes equal the reviewed W2 site ledger");
        if (wide)
        {
            // Mutating an unpatched tail still fails its whole-region check.
            auto* tail = static_cast<std::uint8_t*>(at(0x48763c)); *tail ^= 1;
            check(!patch_transaction::verify_expectations(c, why), "foreign region tail is refused before writes"); *tail ^= 1;
            auto hashes_only = c; hashes_only.guards.clear(); *tail ^= 1;
            check(!patch_transaction::verify_expectations(hashes_only, why), "SHA-256 region check independently refuses foreign function tail"); *tail ^= 1;
            std::map<std::uint32_t, std::uint8_t> writes;
            for (const auto& w : c.writes) for (unsigned j = 0; j < w.bytes.size(); ++j) writes[w.va + j] = w.bytes[j];
            bool clean = true;
            using namespace entity_capacity_sites;
            for (const auto& s : literal_sites) if (std::string(s.owner) == "save")
                for (unsigned j = 0; j < bytes(s.old_hex).size(); ++j) clean &= !writes.count(s.va + j);
            for (const auto& s : relocation_sites)
                for (unsigned j = 0; j < bytes(s.old_hex).size(); ++j) clean &= !writes.count(s.va + j);
            for (auto range : {std::pair{0x4664d0u,0x4664d5u}, {0x4668b0u,0x4668b7u}, {0x4b6e70u,0x4b7744u}})
                for (auto va = range.first; va < range.second; ++va) clean &= !writes.count(va);
            check(clean, "no overlap with any Phase A save literal, relocation, capture hook, 4668b0, or smart-manager region");
            patch_transaction::transaction tx; integrate_counts(tx); tx.add(std::move(c));
            const auto result = tx.commit(); check(result == patch_transaction::outcome::committed, "production contribution commits with separate Phase A integration: " + tx.report());
            if (result != patch_transaction::outcome::committed) return 1;
        }
        // Fake external bodies after validating every guard/region, before any execution.
        fakes();
        restore_function(wide);
        records(wide);
        snippets(wide);
    }
    std::printf("State records: %d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
