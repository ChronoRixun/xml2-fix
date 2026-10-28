#include "stats_layout.hpp"
#include "motion_pool.hpp"
#include "motion_pool_sites.hpp"
#include "patch_transaction.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "limits.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <optional>

namespace motion_pool {
namespace {
using u32=std::uint32_t;
constexpr u32 capacity=22528, clips_per_owner=176;
constexpr descriptor describe(u32 n){
    const auto words=(n+31)/32,constructed=n*32,ring=constructed+words*4;
    const auto enqueue=ring+(n+1)*4,active=enqueue+12,live=active+words*4,generations=live+4,mask=generations+n*4;
    return {n,constructed,ring,enqueue,enqueue+4,enqueue+8,active,live,generations,mask,mask+4,mask+8};
}
constexpr auto stock=describe(500),raised=describe(capacity);
static_assert(raised.size==0xdd61c&&raised.live==0xc7610&&127*clips_per_owner<=capacity);
bool applied=false,requested=false;
std::atomic<u32> allocations{0},allocation_refusals{0},peak{0},clip_refusals{0},reader_refusals{0};
struct owner_state {std::atomic<u32> address{0},refusals{0};};
// ActorSlots publishes at most 127 fixed cache slots. The mathematical bound
// also admits equality at 128 owners, so reserve that final tracking slot.
std::array<owner_state,128> owners{};
std::array<char,21> counter_hex{};
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
u32 read32(const void* p){u32 v;std::memcpy(&v,p,4);return v;}
bool enabled(const std::optional<std::string>& key){return key&&*key!="500";}
void settle(bool verified){gameplay_agreement::register_rule(gameplay_capabilities::motion_pool_id,1,{capacity,0,0,0},verified);}
void abandon(){applied=false;stats_layout::publish_motion(false);settle(false);logger::write("MotionPool: refused; native row retained, rule 51 unverified");}
void retain(){applied=false;stats_layout::publish_motion(false);settle(false);game_version::refuse_startup();logger::write("MotionPool: uncertified rollback; DLL caves retained, startup refused");}
void __cdecl reset_owner(void* owner){for(auto& s:owners)if(s.address.load()==address(owner)){s.refusals=0;break;}}
void __cdecl clip_refused(void* owner,u32 clip,u32 count){
    ++clip_refusals;owner_state* state=nullptr;
    for(auto& s:owners)if(s.address.load()==address(owner)){state=&s;break;}
    if(!state)for(auto& s:owners)if(!s.address.load()){s.address=address(owner);state=&s;break;}
    if(state&&state->refusals.fetch_add(1)==0)
        logger::write("R3: animation list too long (owner 0x%08x, clip %u, count %u)",address(owner),clip,count);
}
void __cdecl reader_refused(){++reader_refusals;}
u32 __fastcall allocate(void* pool,void*){
    const auto handle=reinterpret_cast<u32(__fastcall*)(void*,void*)>(0x56a3e0)(pool,nullptr);
    ++allocations;const auto live=read32(static_cast<unsigned char*>(pool)+raised.live);
    if(live>peak.load())peak=live;
    if(!handle){++allocation_refusals;logger::write("R3: motion allocation refused (live %u/%u)",live,capacity);}
    return handle;
}
__declspec(naked) void owner_guard(){
    __asm {
        mov eax,[edi+2cch]
        cmp esi,0b0h
        jae refuse
        cmp esi,eax
        jl existing
        cmp eax,0b0h
        jae refuse
        push 056a66bh
        ret
    existing:
        push 056a687h
        ret
    refuse:
        pushfd
        pushad
        push eax
        push esi
        push edi
        call clip_refused
        add esp,12
        popad
        popfd
        push 056a7e4h
        ret
    }
}
__declspec(naked) void owner_constructed(){
    __asm {
        mov [esi+2cch],ebx
        pushfd
        pushad
        push esi
        call reset_owner
        add esp,4
        popad
        popfd
        push 056b6bfh
        ret
    }
}
// Four native ABIs: scalar readers use esp+4, vector readers esp+8.
// Compiled DLL RX text remains available even after an uncertified rollback.
#define MOTION_READER(name,arg_offset,continuation) \
__declspec(naked) void name(){ \
    __asm mov eax,[esp+arg_offset] \
    __asm cmp eax,[ecx+2cch] \
    __asm jae refused \
    __asm cmp eax,0b0h \
    __asm jae refused \
    __asm mov ecx,[ecx+eax*4+0ch] \
    __asm push continuation \
    __asm ret \
    __asm refused: \
    __asm pushfd \
    __asm pushad \
    __asm call reader_refused \
    __asm popad \
    __asm popfd \
    __asm xor ecx,ecx \
    __asm push continuation \
    __asm ret \
}
MOTION_READER(flag_reader,4,056a9e8h)
MOTION_READER(vector_reader,8,056aa08h)
MOTION_READER(magnitude_reader,4,056aa38h)
MOTION_READER(second_vector_reader,8,056aa58h)
#undef MOTION_READER
u32 cave(u32 va){switch(va){
    case 0x56a663:return address(reinterpret_cast<void*>(&owner_guard));
    case 0x56a9e0:return address(reinterpret_cast<void*>(&flag_reader));
    case 0x56aa00:return address(reinterpret_cast<void*>(&vector_reader));
    case 0x56aa30:return address(reinterpret_cast<void*>(&magnitude_reader));
    case 0x56aa50:return address(reinterpret_cast<void*>(&second_vector_reader));
    default:return 0;}}
}
void require_agreement(HMODULE game){if(enabled(ini::text(L"Limits",L"MotionPool")))game_version::require_gameplay_agreement(game);}
void contribute(patch_transaction::transaction& tx,HMODULE game){
    const auto key=ini::text(L"Limits",L"MotionPool");if(!enabled(key))return;
    requested=true;require_agreement(game);
    const auto owner_cap=limits::actor_capacity();u32 existing=0;
    if(*key!="22528"||address(game)!=0x400000||!owner_cap||std::uint64_t(owner_cap)*clips_per_owner>capacity||
       !patch_transaction::read_memory(0x7b05bc,&existing,4)||existing){
        logger::write("MotionPool: refused value %s, owners %u x176, existing pool 0x%08x; rule 51 unverified",key->c_str(),owner_cap,existing);settle(false);return;
    }
    patch_transaction::contribution c;c.owner="MotionPool";
    // Two rows edit different operands in 0x56a3e3. Merge before publication.
    for(const auto& s:motion_pool_sites::sites){
        const auto original=patch_transaction::bytes_of(s.hex);const auto label="MotionPool "+patch_transaction::hex(s.va);
        if(s.type==motion_pool_sites::kind::operand){
            auto i=std::find_if(c.writes.begin(),c.writes.end(),[&](const auto& w){return w.va==s.va;});
            if(i==c.writes.end()){c.guards.push_back({s.va,original,label});c.writes.push_back({s.va,original,label});i=std::prev(c.writes.end());}
            for(unsigned n=0;n<s.size;++n)i->bytes[s.offset+n]=static_cast<unsigned char>(s.new_value>>(8*n));
        }else{
            c.guards.push_back({s.va,original,label});
            c.regions.push_back({label,s.va,s.va+static_cast<u32>(original.size()),patch_transaction::sha256(original.data(),original.size())});
            c.writes.push_back(patch_transaction::relocation(s.va,"e9",true,cave(s.va),static_cast<unsigned>(original.size()-5),label));
        }
    }
    c.guards.push_back({0x7b05bc,{0,0,0,0},"MotionPool not created"});
    c.guards.push_back({0x56b2c9,{static_cast<unsigned char>(owner_cap)},"MotionPool published ActorSlots capacity"});
    c.guards.push_back({0x56a672,patch_transaction::bytes_of("e869fdffff"),"MotionPool allocation observer"});
    c.writes.push_back(patch_transaction::relocation(0x56a672,"e8",true,address(reinterpret_cast<void*>(&allocate)),0,"MotionPool allocation observer"));
    c.guards.push_back({0x56b6b9,patch_transaction::bytes_of("899ecc020000"),"MotionPool owner lifetime observer"});
    c.writes.push_back(patch_transaction::relocation(0x56b6b9,"e9",true,address(reinterpret_cast<void*>(&owner_constructed)),1,"MotionPool owner lifetime observer"));
    const auto counter=std::find_if(c.writes.begin(),c.writes.end(),[](const auto& w){return w.va==0x56a3e3;});
    if(counter==c.writes.end()){abandon();return;}
    constexpr char digits[]="0123456789abcdef";
    for(unsigned i=0;i<counter->bytes.size();++i){counter_hex[2*i]=digits[counter->bytes[i]>>4];counter_hex[2*i+1]=digits[counter->bytes[i]&15];}
    c.publish=[owner_cap]{
        allocations=0;allocation_refusals=0;peak=0;clip_refusals=0;reader_refusals=0;
        for(auto& s:owners){s.address=0;s.refusals=0;}
        applied=true;stats_layout::publish_motion(true);settle(true);
        logger::write("MotionPool: rule 51 revision 1 installed {22528,0,0,0}; owners %u x176 <=22528; 82 ledger rows, 5 bounds caves; pool not yet created",owner_cap);
    };
    c.abandon=&abandon;c.retain=&retain;if(!tx.add(std::move(c)))abandon();
}
const descriptor& layout(){return applied?raised:stock;}
std::string_view counter_guard_hex(){
    if(applied){for(const auto& row:motion_pool_sites::sites)if(row.va==0x56a3e3){
        const auto expected=stats_layout::expected(row.va,row.hex);
        if(expected&&expected->size()==20){std::copy(expected->begin(),expected->end(),counter_hex.begin());return {counter_hex.data(),20};}
    }return {}; }
    for(const auto& s:motion_pool_sites::sites)if(s.va==0x56a3e3)return s.hex;
    return {};
}
std::string telemetry_json(){
    std::string out="{\"ok\":true,\"installed\":"+std::string(applied?"true":"false")+",\"requested\":"+(requested?"true":"false")+
        ",\"capacity\":"+std::to_string(layout().capacity)+",\"peak\":"+std::to_string(peak.load())+
        ",\"allocations\":"+std::to_string(allocations.load())+",\"allocation_refusals\":"+std::to_string(allocation_refusals.load())+
        ",\"clip_refusals\":"+std::to_string(clip_refusals.load())+",\"reader_refusals\":"+std::to_string(reader_refusals.load())+",\"owners\":[";
    bool comma=false;for(const auto& s:owners){const auto owner=s.address.load();if(!owner)continue;if(comma)out+=",";comma=true;
        out+="{\"address\":"+std::to_string(owner)+",\"clip_refusals\":"+std::to_string(s.refusals.load())+"}";}
    return out+"]}";
}
}
