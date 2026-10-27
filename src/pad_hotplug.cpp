#include "pad_hotplug.hpp"
#include "pad_hotplug_rules.hpp"
#include "game_version.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

namespace pad_hotplug
{
    namespace
    {
        void __cdecl resolve_mapping()
        {
            auto* slots = reinterpret_cast<std::array<std::uint32_t, 4>*>(pad_hotplug_rules::mapping);
            *slots = pad_hotplug_rules::resolve(*slots);
        }
        std::uint32_t resume = pad_hotplug_rules::next;
        __declspec(naked) void mapping_bridge()
        {
            __asm {
                pushfd
                pushad
                call resolve_mapping
                popad
                popfd
                cmp dword ptr ds:[6efa48h], ebx
                jmp dword ptr [resume]
            }
        }
    }
    void install(HMODULE game)
    {
        using namespace pad_hotplug_rules;
        if (reinterpret_cast<std::uintptr_t>(game) != image_base) return;
        xml1_hook_transaction::memory memory;
        for (const auto& g : guards)
        {
            std::vector<unsigned char> expected;
            const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
            for (std::size_t i = 0; i < g.hex.size(); i += 2)
                expected.push_back(static_cast<unsigned char>((nibble(g.hex[i]) << 4) | nibble(g.hex[i+1])));
            std::vector<unsigned char> actual(expected.size());
            if (!memory.read(g.va, actual.data(), actual.size()) || actual != expected)
            {
                logger::write("input: late controller guard refused at 0x%08x - stock assignment retained", g.va);
                return;
            }
        }
        // The loader maps saved GUIDs to the currently enumerated devices. For a
        // missing saved pad it writes 666, then erases every binding using that
        // saved device. Recurring scans discover late pads but never reload those
        // erased actions. Resolve absent/new/empty slots to unused ordinals before
        // the native binding remap. Native GUID matches and control IDs survive;
        // no registry writes, artificial GUIDs or changes to DirectInput are needed.
        const auto target = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&mapping_bridge));
        std::vector<unsigned char> jump{0xe9, 0, 0, 0, 0, 0x90};
        const auto relative = target - (entry + 5);
        std::memcpy(jump.data() + 1, &relative, sizeof(relative));
        bool safe = true;
        if (xml1_hook_transaction::apply(memory, {{entry, {0x39, 0x1d, 0x48, 0xfa, 0x6e, 0x00}, jump}}, &safe))
            logger::write("input: late controller assignment installed (saved bindings retain distinct device slots)");
        else if (safe)
            logger::write("input: late controller transaction refused - stock assignment retained");
        else
        {
            game_version::refuse_startup();
            logger::write("input: late controller rollback/protection verification failed; startup refused");
        }
    }
}
