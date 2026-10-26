#include "codex_menu.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "game_version.hpp"
#include "limits_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

namespace codex_menu
{
    namespace
    {
        bool installed=false;
        using action_t=int(__thiscall*)(void*);
        using mode_t=void(__thiscall*)(void*,int,int);
        using focus_t=void(__thiscall*)(void*,int);

        void __fastcall details(void* self,void*,int old_mode,int active)
        {
            auto* menu=static_cast<unsigned char*>(self);
            auto* list=*reinterpret_cast<void**>(menu+0x18dc);
            // Retail refreshes the biography after Accept, while Details only
            // switches modes. Bind Details to the current row synchronously so
            // navigating without Accept cannot show the prior hero's biography.
            if(old_mode==0 && active==0 && *reinterpret_cast<int*>(menu+0x18d8)==0 && list) {
                using selection_t=int(__thiscall*)(void*);
                using biography_t=void(__thiscall*)(void*,int);
                const int selected=reinterpret_cast<selection_t>(0x5bfc70)(list);
                if(selected>0) reinterpret_cast<biography_t>(0x5b1340)(self,selected);
            }
            reinterpret_cast<mode_t>(0x5acf30)(self,old_mode,active);
        }

        int __fastcall back(void* self,void*)
        {
            auto* menu=static_cast<unsigned char*>(self);
            auto& mode=*reinterpret_cast<int*>(menu+0x18d8);
            auto* list=*reinterpret_cast<void**>(menu+0x18dc);
            if(mode!=1 || !list) return reinterpret_cast<action_t>(0x5b9fe0)(self);
            // Match the native Details toggle (0x5b18ef..0x5b1919). Back from
            // a biography changes modes; only Back from the list queues Close.
            reinterpret_cast<mode_t>(0x5acf30)(self,1,0);
            mode=0;
            reinterpret_cast<mode_t>(0x5acf30)(self,0,1);
            reinterpret_cast<focus_t>((*static_cast<void***>(list))[0x4c/4])(list,1);
            return 2; // native handled-action result, without a queued Close
        }
    }

    void install(HMODULE game)
    {
        if(installed || !ini::flag(L"Game",L"ClassicCodex",false)) return;
        if(reinterpret_cast<std::uintptr_t>(game)!=0x400000) return;
        const limits_rules::guard guards[]{
            {0x69e700,"e09f5b00","Codex Back action slot"},
            {0x5b9fe0,"568bf1e838e901008b106a00","native queued menu Close"},
            {0x5acf30,"8b81280800003dffffff3f56","native mode activation"},
            {0x5b18ef,"8b86d81800006a00508bcee831b6ffff","Codex detail/list mode transition"},
            {0x5b190f,"8b8edc1800008b116a01ff524c","Codex retained list focus"},
            {0x5b18fa,"e831b6ffff","Details mode-change call"},
            {0x5bfc70,"568bf18b06ff90c800000085","selected Codex row registry ID"},
            {0x5b1340,"81eca0000000a1f8386f0055","native Codex biography/preview refresh"},
        };
        xml1_hook_transaction::memory memory;
        for(const auto& g:guards) {
            std::array<unsigned char,16> bytes{};
            if(!memory.read(g.va,bytes.data(),g.hex.size()/2) || !limits_rules::matches(bytes.data(),g.hex)) {
                logger::write("Codex Back: guard refused at %08X (%s)",g.va,g.what);return;
            }
        }
        std::vector<unsigned char> call{0xe8,0,0,0,0};
        const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&details))-0x5b18ff;
        std::memcpy(call.data()+1,&delta,4);
        const std::vector<xml1_hook_transaction::patch> patches{
            {0x69e700,{0xe0,0x9f,0x5b,0},xml1_hook_transaction::pointer_bytes(reinterpret_cast<const void*>(&back))},
            {0x5b18fa,{0xe8,0x31,0xb6,0xff,0xff},call},
        };
        bool safe=true;
        installed=xml1_hook_transaction::apply(memory,patches,&safe);
        if(!safe) game_version::refuse_startup();
        logger::write("Codex: Details binds current biography; Back returns to list (installed %d)",int(installed));
    }
}
