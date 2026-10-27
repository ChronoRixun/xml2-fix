#include "primitives_per_effect.hpp"
#include "primitives_per_effect_rules.hpp"
#include "effect_templates_memory.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"

namespace primitives_per_effect {
namespace {
unsigned effective=10;
bool attempted=false,installed=false,readable=false;
std::uint32_t address(const void* p){return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));}
}
void install(HMODULE game){
    if(attempted)return;attempted=true;
    using namespace primitives_per_effect_rules;
    const auto s=parse(ini::text(L"Limits",L"PrimitivesPerEffect"),10,32);
    std::uint8_t getter[5]{};
    readable=address(game)==0x400000 && patch_transaction::read_memory(0x4078d0,getter,5) &&
        std::memcmp(getter,"\xa1\x3c\x3c\x6f\x00",5)==0;
    if(s.requested&&s.valid&&readable){
        const auto d=effect_templates::layout();
        // The descriptor publication and native allocation/mask operands must
        // agree, including when W3-4 is stock. These are read-only prechecks.
        std::uint32_t base=0,mask=0;
        const bool compatible=patch_transaction::read_memory(0x40790f,&base,4)&&base==d.definition_base_offset&&
           patch_transaction::read_memory(0x405109,&mask,4)&&mask==d.mask_offset;
        if(compatible){
        auto* spill=static_cast<std::uint8_t*>(effect_templates_memory::allocate(d.capacity*spill_columns*4));
        auto* code=static_cast<std::uint8_t*>(effect_templates_memory::allocate(4096));
        bool retain=false;
        if(spill&&code){
            auto p=plan_for(s.value,d,address(code),address(spill));
            DWORD old=0;
            if(!p.code.empty()&&p.code.size()<=4096){
                std::memcpy(code,p.code.data(),p.code.size());
                if(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)&&FlushInstructionCache(GetCurrentProcess(),code,p.code.size())){
                    p.patches.publish=[&]{installed=true;effective=s.value;};
                    p.patches.retain=[&]{retain=true;game_version::refuse_startup();};
                    patch_transaction::transaction t;t.add(std::move(p.patches));const auto r=t.commit(effect_templates_memory::hooks());
                    if(r!=patch_transaction::outcome::committed)logger::write("PrimitivesPerEffect: refused: %s",t.report().c_str());
                }
            }
        }
        if(!installed&&!retain){if(spill)VirtualFree(spill,0,MEM_RELEASE);if(code)VirtualFree(code,0,MEM_RELEASE);}
        }else logger::write("PrimitivesPerEffect: descriptor does not match native layout");
    }
    if(s.requested){
        gameplay_agreement::register_rule(gameplay_capabilities::primitives_per_effect_id,1,{installed?effective:0u,0,0,0},installed);
        logger::write("PrimitivesPerEffect: %s; capacity %u",installed?"installed":"unverified request; stock retained",effective);
    }
}
unsigned capacity(){return effective;}
std::string status(){
    using namespace effect_templates_rules;
    std::uint32_t owner=0;
    if(!readable||!patch_transaction::read_memory(owner_pointer,&owner,4))return "prims/effect max -/"+std::to_string(effective);
    unsigned maximum=0;
    if(owner){
        const auto d=effect_templates::layout();const auto a=layout_for(d.capacity);
        for(unsigned slot=0;slot<d.capacity;++slot){
            std::uint32_t bits=0,count=0;
            if(!patch_transaction::read_memory(owner+d.definition_base_offset+a.defs.bitmap_b+(slot/32)*4,&bits,4))return "prims/effect max -/"+std::to_string(effective);
            if((bits&(1u<<(slot%32)))&&patch_transaction::read_memory(owner+d.definition_base_offset+slot*d.stride+0x30,&count,4))maximum=(std::max)(maximum,count);
        }
    }
    return "prims/effect max "+std::to_string(maximum)+"/"+std::to_string(effective);
}
}
