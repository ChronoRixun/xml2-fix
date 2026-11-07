#include "control_life.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <cstring>

namespace control_life {
namespace {
constexpr std::uint32_t setter = 0x4e8240, slot = 0x6912d4;
bool applied = false;
using setter_fn = bool(__thiscall*)(void*, const char*, const char*);

bool __fastcall parse(void* event, void*, const char* attribute, const char* value) {
    const auto native = reinterpret_cast<setter_fn>(setter);
    if (!attribute || _stricmp(attribute, "xml1_life_range") != 0)
        return native(event, attribute, value);
    // CCEPowerup's normal life setter writes BOTH its override and the CPUBase template. The override
    // sampler (0x4e7d44) drops the lower bound, then attachment initialization samples again. Keep the
    // parsed template, but leave the override empty so 0x537ba0 samples the XML1 range exactly once.
    const bool accepted = native(event, "life", value);
    if (accepted) std::memset(static_cast<unsigned char*>(event) + 0x24, 0, 12);
    return accepted;
}
}

bool installed() { return applied; }

void install(HMODULE game) {
    if (applied || ini::text(L"Game", L"MindControlLife").value_or("") != "xml1") return;
    xml1_hook_transaction::memory memory;
    const unsigned char entry[] = {0x53,0x8b,0x5c,0x24,0x08,0x56,0x57,0x68,0x60,0x1d,0x68,0x00};
    unsigned char actual[sizeof(entry)]{};
    if (reinterpret_cast<std::uintptr_t>(game) != 0x400000 ||
        !memory.read(setter, actual, sizeof(actual)) || std::memcmp(actual, entry, sizeof(entry))) {
        game_version::refuse_startup();
        logger::write("control life: parser guard refused; required XML1 timing adapter unavailable");
        return;
    }
    const auto pointer = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&parse));
    std::vector<unsigned char> target(sizeof(pointer));
    std::memcpy(target.data(), &pointer, sizeof(pointer));
    const std::vector<xml1_hook_transaction::patch> writes{{slot, {0x40,0x82,0x4e,0}, target}};
    bool safe = true;
    applied = xml1_hook_transaction::apply(memory, writes, &safe);
    // The content explicitly requires this parser extension. Even a clean refusal cannot run its ranges faithfully.
    if (!applied) game_version::refuse_startup();
    logger::write(applied ? "control life: xml1 template life range (parser revision 1)"
                          : "control life: required parser transaction refused; startup refused");
}
}
