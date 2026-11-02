#pragma once

// W2-E15-06/07/09/10. Pure emission rules; addresses and guards refer to the
// retail SHA-1 7d95cdb4a9a599b982147a3389ea7aff211cb82b image only.
#include "save_format_rules.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

namespace state_records_rules
{
    inline constexpr auto profile = save_format_rules::w2_profile;
    inline constexpr unsigned record_size = profile.state_record_size;
    inline constexpr unsigned frame_growth = record_size - save_format_rules::retail_profile.state_record_size;
    static_assert(record_size == 0x20 && frame_growth == 8);

    inline std::vector<std::uint8_t> bytes(std::string_view s)
    {
        auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        std::vector<std::uint8_t> out;
        for (std::size_t i = 0; i < s.size(); i += 2)
            out.push_back(static_cast<std::uint8_t>(digit(s[i]) * 16 + digit(s[i + 1])));
        return out;
    }

    struct site { std::uint32_t va; const char* retail; const char* w2; const char* what; };
    // IMUL r,source,imm8 fits the original three-byte LEA. Every following
    // flag consumer is preceded by a TEST/CMP (or a virtual call); flags here
    // are dead. The second LEA/MOV uses scale 1, so no cave or literal overlap.
    inline constexpr site sites[] = {
        {0x465c2f, "8d04408d04c1", "6bc0208d0401", "ordinal record address (base + index*size; +4 added later)"},
        {0x465c87, "8d14408d74d304", "6bd0208d741304", "dynamic record address (+4 included)"},
        {0x4666ab, "8d04408d4cc504", "6bc0208d4c0504", "world capture record address"},
        {0x466736, "8d14498d74d504", "6bd1208d741504", "smart capture record address"},
        {0x466838, "8d14408b4cd5048d44d504", "6bd0208b4c15048d441504", "smart restore record address"},
        {0x48759a, "6a18", "6a20", "construction vector stride"},
        {0x48764a, "6a18", "6a20", "clear vector stride"},
        {0x48766a, "6a18", "6a20", "zone clear vector stride"},
        // The stack record stays at esp+0x18, grows to esp+0x37. Only
        // cookie, SEH state/registration and epilogue references move.
        {0x4667f5, "83ec24", "83ec2c", "restore local allocation"},
        {0x4667ff, "89442428", "89442430", "restore saved security cookie"},
        {0x466887, "895c243c", "895c2444", "restore constructed EH state"},
        {0x4668fe, "c744243cffffffff", "c7442444ffffffff", "restore cleared EH state"},
        {0x466942, "8b4c242c", "8b4c2434", "restore previous SEH registration"},
        {0x46694e, "8b4c2424", "8b4c242c", "restore security cookie load"},
        {0x466958, "83c430", "83c438", "restore stack release"},
        {0x673630, "8d4dd8", "8d4dd0", "restore exception cleanup record (ebp-0x30)"},
    };

    // Frame widening also relies on the unchanged prologue, all other stack
    // references, and the EH3 unwind-map binding. These are read-only guards,
    // so the Phase A offset writes and zone-string write still compose.
    inline constexpr std::uint32_t restore_begin = 0x4667e0, restore_end = 0x46695c;
    inline constexpr const char* restore_sha256 = "82b39e636238e5fda93afc56b4ce1eef933e76b62afaa8556fdec11620126185";
    struct invariant { std::uint32_t va; const char* retail; const char* what; };
    inline constexpr invariant invariants[] = {
        {0x673633, "e9682edfffb830be6b00e94aeaffff", "restore cleanup jump and EH handler metadata binding"},
        {0x6bbe28, "ffffffff30366700200593190100000028be6b0000000000000000000000000000000000", "restore EH3 unwind map and FuncInfo (no frame-size field)"},
    };

    inline std::vector<std::uint8_t> replacement(const site& s)
    {
        auto b = bytes(s.w2);
        if (s.va == 0x465c2f || s.va == 0x465c87 || s.va == 0x4666ab || s.va == 0x466736 || s.va == 0x466838)
            b[2] = static_cast<std::uint8_t>(record_size);
        if (s.va == 0x48759a || s.va == 0x48764a || s.va == 0x48766a)
            b[1] = static_cast<std::uint8_t>(record_size);
        // All frame immediates/displacements are retail + the descriptor's
        // size delta, except the cleanup's negative EBP displacement.
        if (s.va == 0x4667f5 || s.va == 0x4667ff || s.va == 0x466887 ||
            s.va == 0x4668fe || s.va == 0x466942 || s.va == 0x46694e || s.va == 0x466958)
        {
            const auto old = bytes(s.retail);
            const unsigned index = s.va == 0x4667f5 || s.va == 0x466958 ? 2 : 3;
            b[index] = static_cast<std::uint8_t>(old[index] + frame_growth);
        }
        if (s.va == 0x673630)
            b[2] = static_cast<std::uint8_t>(bytes(s.retail)[2] - frame_growth);
        return b;
    }

    enum class recipe { dispose_clear, clear, construct, capture_copy, restore_copy, restore_clear };
    struct cave_site
    {
        std::uint32_t va, end;
        const char* retail;
        const char* sha256;
        recipe kind;
        const char* what;
    };
    inline constexpr cave_site caves[] = {
        {0x465e70, 0x465eb3,
         "8a4424045333db3ac3568bf1740ff6461101740956e8863d010083c404891e895e0466895e1266895e1466895e16885e0a885e09885e11885e10c64608ff5e5bc20400",
         "68772dd0fd876a80b07972ba0f623611ceab3eb64e55f7f654e3d8e170a335a7", recipe::dispose_clear, "single record disposal/clear"},
        {0x4664a0, 0x4664c4,
         "33c08901894104668941126689411466894116c64108ff88410a884109884111884110c3",
         "5e2065ea3c8f56606e4bc3fb731b9d7978015c88984b695f3b81f6468787c6a6", recipe::clear, "single record clear"},
        {0x487610, 0x48763d,
         "8bc133c98908894804884811668948126689481466894816568d480833d28bf18916895604885608c601ff5ec3",
         "0b8c43855c3448c7fba4f8b17212272f2c100b16c589c2ae2b98de7a6b2625b6", recipe::construct, "single record constructor"},
        {0x466745, 0x4667a3,
         "8b08890e8b50048956048a4808884e088a50098856098a480a884e0a8a500b88560b8a480c884e0c8a500d88560d8a480e884e0e8a500f88560f8a4810884e108a5011885611668b481266894e12668b501466895614668b401666894616",
         "79e4824336ddd07f1421064b717554d669b04fab29968cc5d882600c6e2f690c", recipe::capture_copy, "whole smart capture record copy"},
        {0x466843, 0x466887,
         "894c24188b50048954241c8d48088b11895424208b5104895424248a4908884c24288a501188542429668b481266894c242a668b5014668954242c668b4016668944242e",
         "fe05591e57f41fdf40e84b959781bb9d4b5c3fb181bae91d93c3c04567c93374", recipe::restore_copy, "whole smart restore stack record copy"},
        {0x466906, 0x466932,
         "895c2418895c241c66895c242a66895c242c66895c242ec6442420ff885c2422885c2421885c2429885c2428",
         "97d9c079aac4ec482a50cf9640b2001dbe8580573b547271816a1b7b3d691f4d", recipe::restore_clear, "restore loop stack record clear"},
    };

    inline void dword(std::vector<std::uint8_t>& b, std::uint32_t n)
    { for (unsigned i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(n >> (8 * i))); }
    inline void branch(std::vector<std::uint8_t>& b, std::uint32_t base, std::uint32_t target, std::uint8_t opcode = 0xe9)
    { b.push_back(opcode); dword(b, target - (base + static_cast<std::uint32_t>(b.size()) + 4)); }

    inline std::vector<std::uint8_t> cave(const cave_site& s, std::uint32_t base)
    {
        std::vector<std::uint8_t> b;
        switch (s.kind)
        {
        case recipe::dispose_clear:
            // Retail flags-B test and cdecl disposal call, with the call relocated.
            b = bytes("8a4424045333db3ac3568bf1740ff6461101740956");
            branch(b, base, 0x479c10, 0xe8);
            { const auto tail = bytes("83c404891e895e0466c74612ffff895e14895e18895e1c885e0a885e09885e11885e10c64608ff5e5bc20400"); b.insert(b.end(), tail.begin(), tail.end()); }
            return b;
        case recipe::clear:
            return bytes("33c0890189410466c74112ffff89411489411889411cc64108ff88410a884109884111884110c3");
        case recipe::construct:
            return bytes("8bc133c9890889480488481166c74012ffff89481489481889481c568d480833d28bf18916895604885608c601ff5ec3");
        case recipe::capture_copy:
            b = bytes("9c608bfe8bf0b9"); dword(b, record_size / 4);
            { const auto tail = bytes("fcf3a5619d"); b.insert(b.end(), tail.begin(), tail.end()); }
            break;
        case recipe::restore_copy:
            // pushfd + pushad take 0x24 bytes: original esp+0x18 = current esp+0x3c.
            b = bytes("9c608bf08d7c243cb9"); dword(b, record_size / 4);
            { const auto tail = bytes("fcf3a5619d"); b.insert(b.end(), tail.begin(), tail.end()); }
            break;
        case recipe::restore_clear:
            // MOV only: preserve the CMP flags consumed by JL at 0x46693a,
            // EAX loop counter and EDX ordinal-list cursor. EBX is native zero.
            b = bytes("895c2418895c241c66c744242affff895c242c895c2430895c2434c6442420ff885c2422885c2421885c2429885c2428");
            break;
        }
        branch(b, base, s.end);
        return b;
    }

    // E15-07 sweep: ADD reg,0x18 in [4654f0,466b00), [487330,487700):
    // 465f17 83c618; 4660ee 83c718; 4662fd 83c618; 466415 83c618.
    // All belong to E15-08 / E15-14j whole replacements: no writes here.
    // No Phase A literal, capture/restore hook, bound bridge, numbering bridge,
    // 4668b0 zone-string displacement, or smart-manager byte is overwritten.
}
