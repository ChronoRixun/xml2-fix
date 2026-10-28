#include "pad_hotplug.hpp"
#include "pad_hotplug_rules.hpp"
#include "game_version.hpp"
#include "gamepad_fix.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "pad_prompts_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <cstdio>

namespace pad_hotplug
{
    namespace
    {
        bool enabled = false;
        constexpr unsigned players = 4, rows = 5, devices = 10;
        constexpr unsigned input_global = 0xa6abfc, maps_global = 0xa68f40;
        struct role
        {
            GUID guid{};
            unsigned source_slot = 0, binding_device = 0;
            bool present = false;
        };
        void* current_input = nullptr;
        void* current_keyboard = nullptr;
        ULONGLONG next_refresh = 0;
        std::array<role, players> roles{};
        std::array<GUID, devices> previous{};

        template<class T> T& field(void* object, unsigned offset)
        {
            return *reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
        }
        bool empty(const GUID& g) { const GUID zero{}; return std::memcmp(&g, &zero, sizeof(g)) == 0; }
        bool equal(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
        bool pad_device(unsigned n) { return n >= 3 && n < 3 + devices; }
        std::array<char, 37> describe(const GUID& g)
        {
            std::array<char, 37> out{};
            std::snprintf(out.data(), out.size(), "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
            return out;
        }
        GUID saved_guid(unsigned slot)
        {
            GUID out{};
            if (slot >= 4) return out;
            // Saved Gamepad1..4 strings loaded by 619770 before input setup;
            // the discovery worker updates names, not this GUID table.
            char text[38]{};
            std::memcpy(text, reinterpret_cast<const void*>(0xa6ac08 + slot * 0x25), 36);
            unsigned n[11]{};
            if (std::sscanf(text, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &n[0], &n[1], &n[2], &n[3], &n[4],
                &n[5], &n[6], &n[7], &n[8], &n[9], &n[10]) != 11) return out;
            out.Data1=n[0]; out.Data2=static_cast<WORD>(n[1]); out.Data3=static_cast<WORD>(n[2]);
            for(unsigned i=0;i<8;++i) out.Data4[i]=static_cast<BYTE>(n[i+3]);
            return out;
        }
        unsigned char* map_for(unsigned row, unsigned player)
        {
            auto* config = reinterpret_cast<void**>(maps_global)[row * players + player];
            return config ? static_cast<unsigned char*>(config) + 0x18 : nullptr;
        }
        unsigned first_pad(unsigned char* map)
        {
            if (!map || field<unsigned>(map, 0) > pad_hotplug_rules::max_map_actions) return 0;
            for(unsigned a=0;a<field<unsigned>(map,0);++a) for(unsigned s=0;s<4;++s)
            {
                const auto d=field<unsigned>(map,4+(a*4+s)*12);
                if (pad_device(d)) return d;
            }
            return 0;
        }
        unsigned rebind(unsigned player, unsigned old_device, unsigned new_device, bool preserve_values = false)
        {
            unsigned changed = 0;
            for(unsigned row=0;row<rows;++row)
            {
                auto* map=map_for(row,player);
                if (!map || field<unsigned>(map,0)>pad_hotplug_rules::max_map_actions) continue;
                for(unsigned a=0;a<field<unsigned>(map,0);++a) for(unsigned s=0;s<4;++s)
                {
                    auto* record=map+4+(a*4+s)*12;
                    if (field<unsigned>(record,0)==old_device)
                    {
                        if (old_device != new_device) { field<unsigned>(record,0)=new_device; ++changed; }
                        if (!preserve_values) field<unsigned>(record,8)=0;
                    }
                }
                // Rebuild the game's derived first-pad device (+964), including
                // a stale/unset active device. Control IDs and keyboard stay as saved.
                reinterpret_cast<void(__thiscall*)(void*)>(0x6295a0)(map);
            }
            return changed;
        }

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
        // Startup-only switch: OFF must leave both the Options bridge and
        // every gameplay arrival write absent. The callback stays inert.
        if (!ini::flag(L"Input", L"LatePads", true))
        {
            logger::write("input: LatePads=0 - stock late controller assignment retained");
            return;
        }
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
        // Reuse the existing native binding-map dependency guard, rather than
        // adding another overlapping literal signature for the same helper.
        for (const auto& g : pad_prompts_rules::guards) if (g.va == 0x6295a0)
        {
            std::vector<unsigned char> actual(g.hex.size()/2);
            if (!memory.read(g.va,actual.data(),actual.size()) || !limits_rules::matches(actual.data(),g.hex))
            {
                logger::write("input: late controller binding-map guard refused - arrival routing unavailable");
                return;
            }
        }
        // Advanced Options maps saved GUIDs to the currently enumerated devices. For a
        // missing saved pad it writes 666, then erases every binding using that
        // saved device. Preserve those actions when accepting that panel. The
        // separate gameplay-input arrival refresh below is what fixes late input.
        // Native GUID matches and control IDs survive;
        // no registry writes, artificial GUIDs or changes to DirectInput are needed.
        const auto target = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&mapping_bridge));
        std::vector<unsigned char> jump{0xe9, 0, 0, 0, 0, 0x90};
        const auto relative = target - (entry + 5);
        std::memcpy(jump.data() + 1, &relative, sizeof(relative));
        bool safe = true;
        if (xml1_hook_transaction::apply(memory, {{entry, {0x39, 0x1d, 0x48, 0xfa, 0x6e, 0x00}, jump}}, &safe))
        {
            enabled = true;
            logger::write("input: late controller assignment installed (arrival refresh on the main keyboard poll; waiting for startup map)");
        }
        else if (safe)
            logger::write("input: late controller transaction refused - stock assignment retained");
        else
        {
            game_version::refuse_startup();
            logger::write("input: late controller rollback/protection verification failed; startup refused");
        }
    }

    void on_keyboard_read(void* device, DWORD state_bytes)
    {
        if (!enabled || state_bytes != 256) return;
        auto* input = *reinterpret_cast<void**>(input_global);
        if (!input || field<void*>(input,4) != device || !field<void*>(input,0x27e4)) return;
        auto* play=map_for(0,0);
        if (!play || !field<unsigned>(play,0) || field<unsigned>(play,0)>pad_hotplug_rules::max_map_actions) return;
        const auto now=GetTickCount64();
        const bool startup=input!=current_input || device!=current_keyboard;
        if (!startup && now<next_refresh) return;
        next_refresh=now+500;
        if (startup)
        {
            current_input=input; current_keyboard=device; roles={}; previous={};
            logger::write("input: main input %p startup keyboard %p mouse %p; discovery input %p; arrival refresh every 500 ms",
                input,field<void*>(input,4),field<void*>(input,8),*reinterpret_cast<void**>(0xa6a7e4));
        }
        for(unsigned p=0;p<players;++p)
        {
            auto& r=roles[p]; const auto bound=first_pad(map_for(0,p));
            if (startup || bound!=r.binding_device)
            {
                r={}; r.binding_device=bound;
                if (pad_device(bound))
                {
                    r.source_slot=bound-3; r.guid=saved_guid(r.source_slot);
                    if (!startup && field<void*>(input,0xc+r.source_slot*4)) r.guid=field<GUID>(input,0x27e8+r.source_slot*16);
                    if (empty(r.guid)) r.guid=field<GUID>(input,0x27e8+r.source_slot*16);
                }
            }
            if (startup) logger::write("input: startup map player %u saved slot %u GUID %s -> binding device %u (%s); active device %u",
                p+1,pad_device(bound)?r.source_slot+1:0,describe(r.guid).data(),bound,
                pad_device(bound) && field<void*>(input,0xc+(bound-3)*4)?"present":"reserved/missing",map_for(0,p)?field<unsigned>(map_for(0,p),0x964):0);
        }

        // The retail worker scans a different CInput. Refresh the actual gameplay
        // CInput here, on its own polling thread, while its cache key stays active.
        // With CacheDeviceScan on, recurring enumeration only replays snapshots.
        struct sample
        {
            GUID guid{};
            bool present=false;
            std::array<unsigned char,0x110> now{}, before{};
        };
        std::array<sample,devices> state_before{};
        const auto roles_before=roles;
        for(unsigned i=0;i<devices;++i) if(field<void*>(input,0xc+i*4))
        {
            auto& s=state_before[i];s.guid=field<GUID>(input,0x27e8+i*16);s.present=true;
            std::memcpy(s.now.data(),static_cast<unsigned char*>(input)+0x4f0+i*0x110,0x110);
            std::memcpy(s.before.data(),static_cast<unsigned char*>(input)+0xf90+i*0x110,0x110);
        }
        reinterpret_cast<void(__thiscall*)(void*,bool)>(0x628e20)(input,true);
        std::array<GUID,devices> actual{};
        unsigned occupied=0,claimed=0;
        for(unsigned i=0;i<devices;++i) if(field<void*>(input,0xc+i*4))
        {
            actual[i]=field<GUID>(input,0x27e8+i*16); occupied|=1u<<i;
            // Native compaction clears the moved slot's state. Keep continuity
            // for the same attached GUID so an already-held button is not a new
            // press merely because another controller was unplugged.
            for(const auto& s:state_before) if(s.present && equal(s.guid,actual[i]))
            {
                std::memcpy(static_cast<unsigned char*>(input)+0x4f0+i*0x110,s.now.data(),0x110);
                std::memcpy(static_cast<unsigned char*>(input)+0xf90+i*0x110,s.before.data(),0x110);
                break;
            }
        }
        gamepad_fix::update_attached(actual.data(),devices);
        std::array<int,players> chosen{-1,-1,-1,-1};
        for(unsigned p=0;p<players;++p) if(pad_device(roles[p].binding_device) && !empty(roles[p].guid))
            for(unsigned i=0;i<devices;++i) if((occupied&(1u<<i)) && equal(roles[p].guid,actual[i]))
            { chosen[p]=static_cast<int>(i);claimed|=1u<<i;break; }
        for(unsigned p=0;p<players;++p) if(pad_device(roles[p].binding_device) && chosen[p]<0)
            for(unsigned i=0;i<devices;++i) if((occupied&(1u<<i)) && !(claimed&(1u<<i)))
            {
                bool arrived=startup;
                if (!arrived) { arrived=true; for(const auto& old:previous) if(equal(old,actual[i])) arrived=false; }
                if (arrived) { chosen[p]=static_cast<int>(i); claimed|=1u<<i; roles[p].guid=actual[i];break; }
            }
        unsigned reserved=occupied;
        for(unsigned p=0;p<players;++p)
        {
            auto& r=roles[p];
            if (!pad_device(r.binding_device))
            {
                rebind(p,0,0); // explicit keyboard-only/unbound controls also clear stale pad metadata
                continue;
            }
            int index=chosen[p]; const bool present=index>=0;
            if (!present)
            {
                // Keep an unused saved number until arrival. Move a missing
                // role only if native compaction put a different pad there.
                const auto old_index=r.binding_device-3;
                if (!(reserved&(1u<<old_index))) index=static_cast<int>(old_index);
                else for(unsigned i=0;i<devices;++i) if(!(reserved&(1u<<i))) { index=static_cast<int>(i);break; }
                if (index>=0) reserved|=1u<<static_cast<unsigned>(index);
            }
            if (index<0) continue;
            const auto desired=static_cast<unsigned>(index)+3, before=r.binding_device;
            const bool continuous=present && roles_before[p].present && equal(roles_before[p].guid,r.guid);
            const auto changed=rebind(p,before,desired,continuous);
            if (startup || present!=r.present || before!=desired)
                logger::write("input: %s player %u saved slot %u GUID %s -> actual device index %d, binding device %u (was %u), active device %u; %u bindings updated in 5 rows; main input %p",
                    present?"arrival bound":"missing reserved",p+1,r.source_slot+1,describe(r.guid).data(),index,desired,before,
                    field<unsigned>(map_for(0,p),0x964),changed,input);
            r.binding_device=desired;r.present=present;
        }
        previous=actual;
    }
}
