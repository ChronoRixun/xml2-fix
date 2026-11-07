// Execute the production parser bridge in a synthetic image. Optional retail bytes verify its ABI guards.
#include "control_life.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

extern "C" unsigned char fixture_space[0x400000];
namespace ini {
std::optional<std::string> setting;
std::optional<std::string> text(const wchar_t*, const wchar_t*) { return setting; }
}
namespace logger { void write(const char*, ...) {} }
namespace game_version { bool refused = false; void refuse_startup() { refused = true; } }

namespace {
constexpr std::uint32_t setter = 0x4e8240, slot = 0x6912d4;
const unsigned char entry[] = {0x53,0x8b,0x5c,0x24,0x08,0x56,0x57,0x68,0x60,0x1d,0x68,0x00};
int failures = 0, checks = 0, calls = 0;
bool accepts = true;
std::string received;
std::vector<unsigned char> binary;
std::size_t file_offset(std::uint32_t va) {
    auto u16=[](std::size_t o){std::uint16_t v;std::memcpy(&v,binary.data()+o,2);return v;};
    auto u32=[](std::size_t o){std::uint32_t v;std::memcpy(&v,binary.data()+o,4);return v;};
    const auto pe=u32(0x3c),sections=pe+24+u16(pe+20);
    for(unsigned i=0;i<u16(pe+6);++i){const auto row=sections+i*40,rva=u32(row+12);
        if(va-0x400000>=rva && va-0x400000<rva+u32(row+16))return u32(row+20)+va-0x400000-rva;}
    return binary.size();
}
void check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }
unsigned char* at(std::uint32_t va) { return reinterpret_cast<unsigned char*>(va); }
void seed() {
    std::memcpy(at(setter), entry, sizeof(entry));
    std::memcpy(at(slot), &setter, sizeof(setter));
    game_version::refused = false;
}
bool __fastcall native(void* object, void*, const char* attribute, const char* value) {
    ++calls; received = attribute;
    if (!accepts) return false;
    auto* b = static_cast<unsigned char*>(object);
    std::memset(b + 0x24, 0x5a, 12); // the event's independent override
    std::memcpy(b + 0x50, value, std::strlen(value) + 1); // invented template storage, outside the override
    return true;
}
void retail(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    const std::vector<unsigned char> b{std::istreambuf_iterator<char>(stream), {}};
    if (b.size() < 256) { check(false, "retail input readable"); return; }
    binary=b;
    auto u16 = [&](std::size_t o) { std::uint16_t v; std::memcpy(&v,b.data()+o,2); return v; };
    auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v,b.data()+o,4); return v; };
    const auto pe = u32(0x3c), sections = pe+24+u16(pe+20);
    auto offset = [&](std::uint32_t va) -> std::size_t {
        for (unsigned i=0;i<u16(pe+6);++i) {
            const auto row=sections+i*40, rva=u32(row+12), size=u32(row+16);
            if (va-0x400000>=rva && va-0x400000<rva+size) return u32(row+20)+va-0x400000-rva;
        }
        return b.size();
    };
    const auto code=offset(setter), cell=offset(slot);
    check(code+sizeof(entry)<=b.size() && !std::memcmp(b.data()+code,entry,sizeof(entry)), "retail setter guard");
    check(cell+4<=b.size() && u32(cell)==setter, "retail parser vtable slot");
}
void redirect(std::uint32_t va,const void* target) {
    at(va)[0]=0xe9;
    const auto rel=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target))-va-5;
    std::memcpy(at(va)+1,&rel,4);
}
int __cdecl compare(const char* a,const char* b){return _stricmp(a,b);}
void __fastcall override_parse(void* field,void*,const char*){std::memset(field,0x5a,12);}
std::array<void*,32> pool_vtable{},template_vtable{};
void** pool=pool_vtable.data();
std::array<unsigned char,128> power_template{};
bool __fastcall valid(void*,void*,std::uint32_t){return true;}
void* __fastcall get_template(void*,void*,std::uint32_t){return power_template.data();}
bool __fastcall template_parse(void*,void*,const char* attribute,const char* value){
    received=attribute;std::memcpy(power_template.data()+0x50,value,std::strlen(value)+1);return true;
}
void retail_parser_trial(bool(__thiscall* parse)(void*,const char*,const char*)) {
    if(binary.empty())return;
    // Execute the actual owned CCEPowerup setter, with only its external parser/pool dependencies stubbed.
    const auto offset=file_offset(setter);
    check(offset+0x112<=binary.size(),"retail setter body available");
    if(offset+0x112>binary.size())return;
    std::memcpy(at(setter),binary.data()+offset,0x112);
    redirect(0x672562,&compare);redirect(0x4e7890,&override_parse);
    for(const auto& row : {std::pair<std::uint32_t,const char*>{0x681d60,"life"},
                          {0x6844c8,"damage"},{0x6912fc,"remove_tag"},{0x6912f0,"shared_tag"}})
        std::memcpy(at(row.first),row.second,std::strlen(row.second)+1);
    pool_vtable[0]=reinterpret_cast<void*>(&valid);pool_vtable[2]=reinterpret_cast<void*>(&get_template);
    template_vtable[0x44/4]=reinterpret_cast<void*>(&template_parse);
    *reinterpret_cast<void***>(power_template.data())=template_vtable.data();
    std::array<unsigned char,128> event{};
    *reinterpret_cast<std::uint32_t*>(event.data()+0x14)=7;
    *reinterpret_cast<void**>(event.data()+0x18)=&pool;
    check(parse(event.data(),"life","3 17") && event[0x24]==0x5a,"owned setter retains ordinary event override");
    check(parse(event.data(),"xml1_life_range","3 17"),"bridge through owned setter accepts template range");
    bool zero=true;for(unsigned i=0x24;i<0x30;++i)zero &= event[i]==0;
    check(zero && received=="life","owned setter override cleared after native template dispatch");
    check(!std::strcmp(reinterpret_cast<char*>(power_template.data()+0x50),"3 17"),"owned setter retains both template bounds");
    check(*reinterpret_cast<std::uint32_t*>(event.data()+0x14)==7 &&
          *reinterpret_cast<void**>(event.data()+0x18)==&pool,"owned setter handle and pool preserved");
}
void retail_team_trial() {
    if(binary.empty())return;
    // Native reconciliation compares the current team-switch aggregate to the entity's inversion flag and
    // calls the native setter in BOTH directions. Supply a clean aggregate as the expiry/rebuild path does.
    for(const auto& range : {std::pair<std::uint32_t,std::uint32_t>{0x474c80,0x474d38},
                            {0x41a290,0x41a2b3},{0x41a190,0x41a1e6},{0x41a220,0x41a275}}) {
        const auto offset=file_offset(range.first),size=range.second-range.first;
        check(offset+size<=binary.size(),"owned team routine available");
        if(offset+size>binary.size())return;
        std::memcpy(at(range.first),binary.data()+offset,size);
    }
    at(0x45fe40)[0]=0xc3; // the fixture has no spatial index to dirty
    std::memset(at(0x680030),0,4);
    std::array<unsigned char,0x800> actor{};
    std::array<void*,0x90/4> vtable{};
    vtable[0x80/4]=reinterpret_cast<void*>(0x41a290);
    *reinterpret_cast<void***>(actor.data())=vtable.data();
    *reinterpret_cast<std::uint32_t*>(actor.data()+4)=0x80000000; // native enemy team
    actor[0x258]=1; // native actor classification, avoiding an unrelated class-manager dependency
    using update_fn=bool(__thiscall*)(void*);
    using team_fn=unsigned char(__thiscall*)(void*);
    const auto update=reinterpret_cast<update_fn>(0x474c80);
    const auto team=reinterpret_cast<team_fn>(0x41a220);
    check(team(actor.data())==0x1f,"owned getter starts hostile");
    actor[0x579]=1;
    check(update(actor.data()) && team(actor.data())==0x1d,"owned reconciliation switches enemy to hero team");
    check(!update(actor.data()),"unchanged team aggregate does not toggle inversion");
    actor[0x579]=0;
    check(update(actor.data()) && team(actor.data())==0x1f,"owned reconciliation restores hostile team at expiry");
    check(!(actor[0x6c]&8),"native inversion flag is cleared after control ends");
}
}

int main(int argc, char** argv) {
    if (argc > 1) retail(argv[1]);
    DWORD old=0;
    check(VirtualProtect(fixture_space,sizeof(fixture_space),PAGE_EXECUTE_READWRITE,&old)!=0,"fixture protection");
    seed();
    control_life::install(reinterpret_cast<HMODULE>(0x400000));
    check(!control_life::installed() && !game_version::refused,"absent key leaves native path");
    ini::setting="xml1";
    control_life::install(reinterpret_cast<HMODULE>(0x500000));
    check(!control_life::installed() && game_version::refused,"wrong image refuses required adapter");
    seed(); at(setter)[0]^=1;
    control_life::install(reinterpret_cast<HMODULE>(0x400000));
    check(!control_life::installed() && game_version::refused,"parser mutation refused");
    seed(); at(slot)[0]^=1;
    control_life::install(reinterpret_cast<HMODULE>(0x400000));
    check(!control_life::installed() && game_version::refused,"slot mutation refused");
    seed();
    control_life::install(reinterpret_cast<HMODULE>(0x400000));
    check(control_life::installed() && !game_version::refused,"production transaction installs");
    // Replace only the fixture's original parser with a test double AFTER the real guard has passed.
    at(setter)[0]=0xe9;
    const auto rel=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&native))-setter-5;
    std::memcpy(at(setter)+1,&rel,4);
    using parse_fn=bool(__thiscall*)(void*,const char*,const char*);
    const auto parse=*reinterpret_cast<parse_fn*>(slot);
    std::array<unsigned char,128> object;
    for (const char* attr : {"xml1_life_range", "XML1_LIFE_RANGE"}) {
        object.fill(0xa5); calls=0;
        check(parse(object.data(),attr,"3 17"),"adapter propagates success");
        check(calls==1 && received=="life","template parsed once through native life setter");
        bool zero=true;for(unsigned i=0x24;i<0x30;++i)zero &= object[i]==0;
        check(zero,"event override cleared: attachment uses single template sample");
        check(!std::strcmp(reinterpret_cast<char*>(object.data()+0x50),"3 17"),"template keeps both endpoints");
        check(object[0x23]==0xa5 && object[0x30]==0xa5,"override neighbors preserved");
    }
    for (const char* attr : {"life", "apply_chance", "xml1_life_range_extra"}) {
        object.fill(0xa5);calls=0;
        check(parse(object.data(),attr,"7"),"unrelated attribute accepted by native parser");
        check(calls==1 && received==attr && object[0x24]==0x5a,"native attribute behavior preserved");
    }
    accepts=false;object.fill(0xa5);
    check(!parse(object.data(),"xml1_life_range","bad") && object[0x24]==0xa5,"native refusal propagated without mutation");
    retail_parser_trial(parse);
    retail_team_trial();
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
