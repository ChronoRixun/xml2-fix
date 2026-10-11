#include <WinSock2.h>
#include "game_version.hpp"
#include "ini.hpp"
#include "iat_hook.hpp"
#include "xml1_hook_transaction.hpp"
#include "mold_target_rules.hpp"
#include <iostream>
#include <map>

namespace ini {
std::map<std::wstring,std::string> values;
std::optional<std::string> text(const wchar_t* section,const wchar_t* key) {
    const auto it=values.find(std::wstring(section)+L"/"+key);
    return it==values.end()?std::nullopt:std::optional<std::string>(it->second);
}
bool flag(const wchar_t* section,const wchar_t* key,bool fallback) {
    auto v=text(section,key);return v?*v=="1":fallback;
}
int number(const wchar_t* section,const wchar_t* key,int fallback) {
    auto v=text(section,key);return v?std::stoi(*v):fallback;
}
}
namespace logger {
void write(const char*,...){}
void write_once(std::string_view,const char*,...){}
}
namespace iat_hook {
bool fail=false;void* installed=nullptr;int calls=0,forwarded=0;
SOCKET WSAAPI fake_socket(int,int,int){++forwarded;return 71;}
void* hook(HMODULE,const char*,const char*,WORD,void* replacement){
    ++calls;if(fail)return nullptr;installed=replacement;return reinterpret_cast<void*>(&fake_socket);
}
}
int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::cerr<<__LINE__<<": " #x "\n";}}while(false)
struct fake_memory {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(12288,0x11);
    std::map<std::uint32_t,std::uint32_t> protections{{0,2},{4096,2},{8192,2}};
    int acquire_calls=0,fail_acquire=0,write_calls=0,fail_write=0,flush_calls=0;
    bool bad_readback=false,fail_flush=false;
    bool read(std::uint32_t a,void* p,std::size_t n){
        if(a+n>bytes.size())return false;
        std::memcpy(p,bytes.data()+a,n);
        if(bad_readback&&write_calls)*static_cast<unsigned char*>(p)^=1;
        return true;
    }
    bool write(std::uint32_t a,const void* p,std::size_t n){
        if(++write_calls==fail_write)return false;
        if(a+n>bytes.size()||protections[a&~4095u]!=0x40)return false;
        std::memcpy(bytes.data()+a,p,n);return true;
    }
    bool acquire(std::uint32_t a,std::uint32_t& old){
        if(++acquire_calls==fail_acquire)return false;
        old=protections[a];protections[a]=0x40;return true;
    }
    void restore(std::uint32_t a,std::uint32_t old){protections[a]=old;}
    bool flush(){return !(++flush_calls==1&&fail_flush);}
};
int main(){
    // Catch the old extra 360-unit center-distance restriction and nearest-core
    // policy. These synthetic query results overlap the native search sphere;
    // their centers need not lie within that radius.
    using mold_target_rules::candidate;
    constexpr std::uint32_t hero_target=(1u<<20)|(1u<<24)|(1u<<29);
    std::array<candidate,3> targets{{{41,hero_target,true,true,300000,25},
                                   {42,hero_target,true,true,700000,25},
                                   {43,hero_target,true,true,800000,-1}}};
    CHECK(mold_target_rules::choose(false,100000,targets)==42);
    CHECK(mold_target_rules::choose(true,999999,targets)==41);
    CHECK(mold_target_rules::choose(false,144401,targets)==0);
    CHECK(mold_target_rules::choose(false,144400,targets)==0);
    CHECK(mold_target_rules::choose(false,144399,targets)==42);
    targets[2].activation_distance=0;
    CHECK(mold_target_rules::choose(false,100000,targets)==43);
    targets[2].activation_distance=-1;
    targets[0].flags|=1u<<28;targets[1].can_activate=false;
    CHECK(mold_target_rules::choose(false,100000,targets)==0);
    CHECK(mold_target_rules::choose(true,999999,targets)==43);
    targets[2].flags|=1u<<30;
    CHECK(mold_target_rules::choose(true,0,targets)==0);
    targets[2].flags=hero_target|(1u<<31);
    CHECK(mold_target_rules::choose(true,0,targets)==0);
    targets[2].flags=hero_target&~(1u<<24);
    CHECK(mold_target_rules::choose(true,0,targets)==0);
    targets[2].flags=hero_target;targets[2].core=false;
    CHECK(mold_target_rules::choose(true,0,targets)==0);
    targets[2].core=true;targets[2].flags&=~(1u<<20);
    CHECK(mold_target_rules::choose(true,0,targets)==0);
    using namespace xml1_hook_transaction;
    const std::vector<patch> writes{{12,{0x11},{0x22}},{24,{0x11},{0x33}},{8200,{0x11},{0x44}}};
    for(int failure=0;failure<5;++failure){
        fake_memory mem;const auto before=mem.bytes;const auto protection=mem.protections;
        mem.fail_acquire=failure==1?2:0;mem.fail_write=failure==2?2:0;
        mem.bad_readback=failure==3;mem.fail_flush=failure==4;
        CHECK(apply(mem,writes)==(failure==0));
        CHECK(mem.protections==protection);
        CHECK(mem.acquire_calls==2); // Same-page destinations acquire only once.
        if(failure)CHECK(mem.bytes==before);
        else CHECK(mem.bytes[12]==0x22&&mem.bytes[24]==0x33&&mem.bytes[8200]==0x44);
    }
    fake_memory mismatch;mismatch.bytes[24]=0x55;const auto original=mismatch.bytes;
    CHECK(!apply(mismatch,writes));CHECK(mismatch.acquire_calls==0);CHECK(mismatch.bytes==original);
    fake_memory empty;CHECK(!apply(empty,{{12,{},{}}}));CHECK(empty.acquire_calls==0);

    using socket_t=SOCKET(WSAAPI*)(int,int,int);
    for(const auto* key:{L"Game/Xml1MasterMold",L"Game/Xml1Turrets",L"Test/NativeProjectileProbe"}){
        ini::values={{key,"1"}};iat_hook::fail=false;game_version::install(nullptr);
        CHECK(game_version::startup_allowed());CHECK(game_version::offline_gameplay_ready());
        auto socket=reinterpret_cast<socket_t>(iat_hook::installed);
        const auto forwarded=iat_hook::forwarded;
        CHECK(socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)==INVALID_SOCKET);
        CHECK(WSAGetLastError()==WSAEACCES);CHECK(iat_hook::forwarded==forwarded);
        ini::values.clear(); // Runtime INI drift must not reopen an installed experiment.
        CHECK(socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)==INVALID_SOCKET);
        ini::values={{key,"1"}};iat_hook::fail=true;game_version::install(nullptr);
        CHECK(!game_version::startup_allowed());CHECK(!game_version::offline_gameplay_ready());
    }
    ini::values.clear();iat_hook::fail=false;const auto calls=iat_hook::calls;
    game_version::install(nullptr);CHECK(game_version::startup_allowed());
    CHECK(!game_version::offline_gameplay_ready());CHECK(iat_hook::calls==calls);
    ini::values={{L"Save/Format","lc"},{L"Game/Xml1MasterMold","1"},{L"Online/GameVersion","LC24"}};
    game_version::install(nullptr);CHECK(game_version::startup_allowed());
    CHECK(reinterpret_cast<socket_t>(iat_hook::installed)(AF_INET,SOCK_DGRAM,IPPROTO_UDP)==INVALID_SOCKET);
    std::cout<<"XML1 controller guards: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
