#include "effect_templates.hpp"
#include "effect_templates_rules.hpp"
#include "effect_templates_memory.hpp"
#include "model_pools.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"

namespace effect_templates {
namespace {
effect_templates_rules::owner_layout active=effect_templates_rules::layout_for(220);
bool readable=false,installed=false,attempted=false;
std::uint32_t address(const void* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
}
void install(HMODULE game) {
    if(attempted)return;
    attempted=true;
    using namespace effect_templates_rules;
    const auto s=parse(ini::text(L"Limits",L"EffectTemplates"),220,2048);
    std::uint8_t getter[5]{};
    readable=address(game)==0x400000 && patch_transaction::read_memory(0x4078d0,getter,5) &&
        std::memcmp(getter,"\xa1\x3c\x3c\x6f\x00",5)==0;
    if(s.requested && s.valid && readable) {
        auto* code=static_cast<std::uint8_t*>(effect_templates_memory::allocate(4096));
        if(code) {
            auto p=plan_for(s.value,address(code),model_pools::installed_target(0x571724));
            DWORD old=0; bool retain=false;
            if(p.code.size()<=4096)std::memcpy(code,p.code.data(),p.code.size());
            if(p.code.size()<=4096 && VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old) && FlushInstructionCache(GetCurrentProcess(),code,p.code.size())) {
                p.patches.publish=[&]{active=layout_for(s.value);published_layout=descriptor_for(active);installed=true;};
                p.patches.retain=[&]{retain=true;game_version::refuse_startup();};
                patch_transaction::transaction t;t.add(std::move(p.patches));const auto r=t.commit(effect_templates_memory::hooks());
                if(r!=patch_transaction::outcome::committed)logger::write("EffectFiles: refused: %s",t.report().c_str());
            }
            if(!installed&&!retain)VirtualFree(code,0,MEM_RELEASE);
        }
    }
    if(s.requested) {
        gameplay_agreement::register_rule(gameplay_capabilities::effect_files_id,1,{installed?active.n:0u,0,0,0},installed);
        logger::write("EffectFiles: %s; capacity %u",installed?"installed":"unverified request; stock retained",active.n);
    }
}
std::string status() {
    std::uint32_t owner=0,files=0,defs=0;
    const bool have=readable && patch_transaction::read_memory(effect_templates_rules::owner_pointer,&owner,4);
    const auto count=[&](std::uint32_t off,std::uint32_t& v) {
        if(!have)return std::string("-");
        if(!owner)return std::string("0");
        return patch_transaction::read_memory(owner+off,&v,4)?std::to_string(v):std::string("-");
    };
    return "fx files "+count(12+active.files.live,files)+"/"+std::to_string(active.n)+
        "; fx defs "+count(active.base+active.defs.live,defs)+"/"+std::to_string(active.n);
}
}
