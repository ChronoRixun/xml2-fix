// Original synthetic fixtures. No game launch, personal saves or desktop input.
// Include the production module so the native getter seams can stand in for the
// retail objects; all eligibility, scanning, notification and queuing is real.
#include "../src/profile_progress.cpp"
#include "lcsave/internal.hpp"
#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

namespace logger { void write(const char*, ...) {} }
extern "C" unsigned char union_space[];

namespace
{
    namespace P = profile_progress;
    namespace R = profile_progress_rules;
    unsigned checks = 0, failures = 0;
    void check(bool ok, const char* text)
    { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n",text); } }

    std::array<std::uint8_t,0x500> profile{};
    std::array<std::uint8_t,0x500> another_profile{};
    std::array<std::uint8_t,0x700> game{};
    std::array<std::uint8_t,0x430> session{};
    std::array<std::uint8_t,8> registry{};
    std::array<std::array<std::uint8_t,0x300>,5> heroes{};
    int roster_count = 4, bad_index = -1;
    bool has_stats[5]{}, herostat[5]{}, in_campaign[5]{};

    template<class T> void put(void* p, std::size_t offset, T value)
    { std::memcpy(static_cast<std::uint8_t*>(p)+offset,&value,sizeof value); }
    std::uint32_t address(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }
    std::uint8_t* memory(std::uint32_t va) { return reinterpret_cast<std::uint8_t*>(va); }
    void* __cdecl fake_profile() { return profile.data(); }
    void* __cdecl other_profile() { return another_profile.data(); }
    void* __cdecl fake_registry() { return registry.data(); }
    void* __cdecl fake_game() { return game.data(); }
    void* __cdecl fake_session() { return session.data(); }
    int __fastcall count(void*,void*) { return roster_count; }
    short __fastcall index(void*,void*,int n) { return static_cast<short>(n==bad_index ? 0x7fff : n+1); }
    bool __fastcall has(void*,void*,short n) { return has_stats[n]; }
    bool __fastcall normal(void*,void*,short n) { return herostat[n]; }
    bool __fastcall campaign(void*,void*,short n) { return in_campaign[n]; }
    void* __fastcall stats(void*,void*,short n) { return heroes[n].data(); }
    void bind(std::uint32_t vt, unsigned slot, const void* f) { put(memory(vt),slot,address(f)); }

    void context(bool online=false,bool host=false,bool story=false,std::uint32_t drmode=0,
                 std::uint8_t danger=0xff,std::uint32_t kind=0)
    {
        session[0x420]=online; session[0x421]=host; game[0x616]=story?4:0;
        put(memory(skirmish_rules::drmode),0,drmode);
        put(memory(skirmish_rules::danger_state),0,danger);
        put(memory(skirmish_rules::kind),0,kind);
    }
    void setup(unsigned history=16)
    {
        P::progress={};
        P::fallback_active=false;
        profile.fill(0); game.fill(0); session.fill(0); registry.fill(0);
        put(profile.data(),0,R::profile_vtable);
        put(game.data(),0,R::game_vtable);
        put(session.data(),0,R::session_vtable);
        put(registry.data(),0,R::registry_vtable);
        profile[R::highest_level]=static_cast<std::uint8_t>(history);
        profile[4]=5; std::memcpy(profile.data()+7,"synthetic",10);
        put(profile.data(),0x1c0,std::uint32_t(2)); // unrelated character unlock
        profile[0x229]=8;
        roster_count=4; bad_index=-1;
        for(unsigned n=1;n<=4;++n)
        {
            heroes[n].fill(0); heroes[n][R::stats_level]=static_cast<std::uint8_t>(31+n);
            put(heroes[n].data(),R::stats_kind,R::hero_kind);
            has_stats[n]=herostat[n]=in_campaign[n]=true;
        }
        context();
    }
    unsigned highest() { return profile[R::highest_level]; }
    bool pending(std::array<std::uint8_t,R::profile_size>& bytes)
    { return P::pending_profile(bytes.data(),bytes.size()); }
    void loaded(bool ok=true,int mode=3,bool disk=true)
    { P::begin_load(); P::finish_load(mode,disk,ok); }

    void regression_cases()
    {
        std::array<std::uint8_t,R::profile_size> bytes{};
        setup();
        // Native setter's loaded-story gate is false: the old result is 16.
        check(!(game[0x616]&4) && highest()==16,"legacy loaded-campaign control stays 16");
        const auto original_profile=profile;
        const auto original_heroes=heroes;
        P::begin_load();
        P::observe_level(heroes[4].data());
        check(highest()==16 && !pending(bytes),"native application cannot publish partial progress");
        P::finish_load(3,true,true);
        check(highest()==35 && P::progress.owned,"validated owned load repairs stale 16 to eligible 35");
        check(heroes==original_heroes && !(game[0x616]&4),"repair preserves hero stats and native story flag");
        auto expected=original_profile;expected[R::highest_level]=35;
        check(profile==expected,"repair changes exactly the profile high-water byte");
        check(pending(bytes) && bytes[R::serialized_highest_level]==35,"personal profile snapshot includes repair");
        // No acknowledgment on a failed atomic write: retry remains available.
        check(pending(bytes),"failed persistence retains pending metadata");
        P::profile_written(bytes.data(),bytes.size());
        check(!pending(bytes),"successful guarded profile write retires pending metadata");

        heroes[2][R::stats_level]=36;
        P::observe_level(heroes[2].data());
        check(highest()==36 && pending(bytes),"legitimate level-up after saved load advances history");
        P::profile_written(bytes.data(),bytes.size());
        heroes[2][R::stats_level]=20;P::observe_level(heroes[2].data());
        check(highest()==36 && !pending(bytes),"lower current hero level never lowers history");

        setup(60);loaded();
        check(highest()==60 && !pending(bytes),"loading lower campaign preserves historical max");
        setup(60);P::begin_load();profile[R::highest_level]=16;P::finish_load(3,true,true);
        check(highest()==60 && pending(bytes),"campaign embedded PRFP cannot overwrite greater pre-load personal history");
        setup();P::begin_load();profile[R::highest_level]=35;P::finish_load(3,true,true);
        check(highest()==35 && pending(bytes) && bytes[R::serialized_highest_level]==35,
            "accepted native import matching current heroes still queues personal profile persistence");
        setup();P::begin_load();profile[R::highest_level]=40;P::finish_load(3,true,true);
        check(highest()==40 && pending(bytes) && bytes[R::serialized_highest_level]==40,
            "accepted owned historical max above current heroes persists without lowering");
        setup();P::begin_load();profile[R::highest_level]=40;
        another_profile=profile;P::get_profile=&other_profile;P::finish_load(3,true,true);P::get_profile=&fake_profile;
        check(!P::progress.owned && !pending(bytes),"successful load cannot import history across a replaced profile pointer");
        setup();P::begin_load();P::observe_level(heroes[4].data());
        P::finish_load(3,true,false);
        check(highest()==16 && !P::progress.owned && !pending(bytes),"native failure grants and persists nothing");
        P::observe_level(heroes[4].data());
        check(highest()==16,"failed load cannot promote on a later recompute");
        for(const auto imported:{5,90})
        {
            setup(60);P::begin_load();profile[R::highest_level]=static_cast<std::uint8_t>(imported);
            P::finish_load(3,true,false);
            check(highest()==60 && !pending(bytes),"failed partial native profile import restores prior high-water exactly");
        }
        setup(0);P::begin_load();profile[R::highest_level]=90;P::finish_load(3,true,false);
        check(highest()==0 && !pending(bytes),"zero personal history is preserved on rejected partial import");
        setup();P::begin_load();profile[R::highest_level]=90;P::finish_load(3,false,true);
        check(highest()==16 && !pending(bytes),"unvalidated non-network import is not personal progress");
        setup();P::begin_load();profile[R::highest_level]=90;
        another_profile=profile;another_profile[R::highest_level]=70;P::get_profile=&other_profile;
        P::finish_load(3,true,false);P::get_profile=&fake_profile;
        check(highest()==90 && another_profile[R::highest_level]==70,"rollback never writes a replaced profile pointer");
        setup();P::begin_load();profile[R::highest_level]=90;put(profile.data(),0,std::uint32_t(0));
        P::finish_load(3,true,false);
        check(highest()==90,"rollback never writes a profile whose class changed");
        setup();
        // A staged/selected disk read has no completion callback at all when cancelled.
        check(highest()==16 && !pending(bytes),"cancelled selection never grants progress");
        loaded(true,3,false);P::observe_level(heroes[4].data());
        check(highest()==16,"buffer without validated owned disk proof cannot grant loaded progress");

        setup();context(true,true);loaded();
        check(highest()==35 && P::progress.owned,"online host can reconcile its validated disk campaign");
        P::profile_written((pending(bytes),bytes.data()),bytes.size());
        loaded(true,2,false);heroes[1][R::stats_level]=37;P::observe_level(heroes[1].data());
        check(highest()==37 && P::progress.owned,"proven host ownership survives its canonical cohort restart");
        setup();context(true,true);loaded(true,2,false);P::observe_level(heroes[4].data());
        check(highest()==16 && !P::progress.owned,"hosting alone is not proof of campaign ownership");
        setup();context(true,false,true);loaded(true,2,false);P::observe_level(heroes[4].data());
        check(highest()==16 && !pending(bytes),"remote guest transfer and recompute never grant personal history");
        setup();context(false,false,true);loaded(true,2,false);P::observe_level(heroes[4].data());
        check(highest()==16 && !pending(bytes) && P::progress.remote,
            "mode2 without proven online host ownership remains remote while session setup is false");
        context(false,false,true);loaded(true,1,false);P::observe_level(heroes[4].data());
        check(highest()==16 && !pending(bytes) && P::progress.remote,
            "local stats-only restore after disconnect cannot clear borrowed campaign provenance");
        loaded(true,3,true);
        check(highest()==35 && P::progress.owned && !P::progress.remote,
            "confirmed owned full campaign load clears borrowed provenance");
        setup();loaded();check(pending(bytes),"owned progress queued before remote transition");
        context(true,false);loaded(true,2,false);
        check(!pending(bytes) && !P::progress.owned,"remote transition invalidates prior deferred write");
        context(false,false,true);P::observe_level(heroes[4].data());
        check(!pending(bytes),"disconnect does not turn remote progress into an owned campaign");

        setup();loaded();heroes[1][R::stats_level]=36;P::observe_level(heroes[1].data());
        loaded(true,1,false);
        check(highest()==36 && pending(bytes) && bytes[R::serialized_highest_level]==36,
            "stats-only local reload retains already earned pending personal history");
        setup();context(true,true);loaded();heroes[1][R::stats_level]=36;P::observe_level(heroes[1].data());
        loaded(true,2,false);
        check(highest()==36 && pending(bytes) && bytes[R::serialized_highest_level]==36,
            "proven host cohort restart retains pending personal history");
        setup();loaded();heroes[1][R::stats_level]=36;P::observe_level(heroes[1].data());
        P::begin_load();profile[R::highest_level]=90;P::finish_load(3,true,false);
        check(highest()==36 && pending(bytes) && bytes[R::serialized_highest_level]==36,
            "failed native import rolls back to36 and retains only pre-existing earned pending history");
        setup();loaded();heroes[1][R::stats_level]=36;P::observe_level(heroes[1].data());
        std::array<std::uint8_t,R::profile_size> authorized{};
        check(pending(authorized),"pre-load personal snapshot captures authorized pending36");
        P::begin_load();profile[R::highest_level]=90;profile[4]=0x70;
        put(profile.data(),0x1c0,std::uint32_t(4));P::finish_load(3,true,false);
        check(highest()==36 && profile[4]==0x70 && P::read<std::uint32_t>(profile.data(),0x1c0)==4,
            "failure rollback changes only high-water, preserving native unrelated-field behavior");
        check(pending(bytes) && bytes==authorized,
            "automatic retry uses complete pre-load personal snapshot and excludes failed-import options/unlocks");
        // A verified later owned load establishes a trusted live snapshot again.
        P::begin_load();profile[4]=9;put(profile.data(),0x1c0,std::uint32_t(8));P::finish_load(3,true,true);
        check(pending(bytes) && bytes[0]==9 && P::read<std::uint32_t>(bytes.data(),0x1bc)==8,
            "accepted owned load replaces failed-import snapshot fallback with trusted live metadata");
        P::profile_written(profile.data()+4,R::profile_size);
        check(!pending(bytes),"native personal profile writer still acknowledges same-or-higher durable progress");
        setup();loaded();heroes[1][R::stats_level]=36;P::observe_level(heroes[1].data());
        loaded();
        check(highest()==36 && pending(bytes) && bytes[R::serialized_highest_level]==36,
            "unchanged owned disk reload retains personal history still awaiting persistence");

        setup();loaded();P::profile_written((pending(bytes),bytes.data()),bytes.size());
        context();loaded(true,1,false);heroes[1][R::stats_level]=38;P::observe_level(heroes[1].data());
        check(highest()==38 && P::progress.owned,"stats-only load preserves established owned campaign proof");

        for(unsigned mode=1;mode<=4;++mode)
        {
            setup();loaded();P::profile_written((pending(bytes),bytes.data()),bytes.size());
            heroes[1][R::stats_level]=99;
            context(false,false,true,1,0xfd,mode);P::observe_level(heroes[1].data());
            check(highest()==35 && !pending(bytes),"PvP artificial levels excluded in every native mode");
            context(false,false,true,1,0xff,0);P::observe_level(heroes[1].data());
            check(highest()==35,"Danger Room setup also excludes artificial level assignment");
        }
        setup();heroes[4][R::stats_flags]=R::xpexempt;heroes[3][R::stats_kind]=8;
        in_campaign[2]=false;loaded();
        check(highest()==32,"load filters exempt, nonhero and out-of-campaign records");
        heroes[1][R::stats_flags]=R::xpexempt;heroes[1][R::stats_level]=99;P::observe_level(heroes[1].data());
        check(highest()==32,"ongoing xpexempt progress remains excluded");
        setup();bad_index=3;loaded();
        check(highest()==16,"invalid late roster index prevents partial profile promotion");
        setup();roster_count=R::max_roster+1;loaded();
        check(highest()==16,"unbounded roster is refused");
        setup();for(unsigned n=1;n<=4;++n) has_stats[n]=false;loaded();
        check(highest()==16,"absent native stats never establish a hero level");
        setup();for(unsigned n=1;n<=4;++n) herostat[n]=false;loaded();
        check(highest()==16,"non-herostat roster entries are excluded");
        setup();roster_count=0;loaded();
        check(highest()==16,"empty owned campaign grants nothing");
        setup();loaded();P::profile_written((pending(bytes),bytes.data()),bytes.size());
        for(const auto value:{0,100,255})
        {
            heroes[1][R::stats_level]=static_cast<std::uint8_t>(value);P::observe_level(heroes[1].data());
            check(highest()==35 && !pending(bytes),"invalid level byte outside 1..99 grants nothing");
        }
        heroes[1][R::stats_level]=99;P::observe_level(heroes[1].data());
        check(highest()==99 && pending(bytes),"native maximum level is accepted");
        setup();context(false,false,true);put(game.data(),0,std::uint32_t(0));
        P::observe_level(heroes[4].data());
        check(highest()==16 && !pending(bytes),"unreadable game context fails closed during ordinary notification");
        loaded();put(game.data(),0,R::game_vtable);P::observe_level(heroes[4].data());
        check(highest()==16 && !pending(bytes),"unknown load ownership stays blocked when context later recovers");

        setup();context(false,false,true);P::observe_level(heroes[4].data());
        check(highest()==35 && pending(bytes),"fresh Begin Story retains native eligible notification semantics");
        P::reset_campaign();context();heroes[1][R::stats_level]=40;P::observe_level(heroes[1].data());
        check(highest()==35 && !P::progress.owned,"reset removes loaded-campaign proof without lowering history");
        check(pending(bytes),"reset retains already earned personal metadata for persistence at the menu");
        setup(60);context(true,true,true);P::observe_level(heroes[4].data());
        check(highest()==60 && P::progress.owned && !pending(bytes),
            "eligible fresh host campaign establishes ownership even below personal history");
        loaded(true,2,false);heroes[1][R::stats_level]=61;P::observe_level(heroes[1].data());
        check(highest()==61 && P::progress.owned && pending(bytes),
            "fresh host campaign retains legitimate future level progress across canonical restart");
        setup(60);context(true,true,true);loaded(true,2,false);
        heroes[1][R::stats_level]=61;P::observe_level(heroes[1].data());
        check(highest()==61 && P::progress.owned && pending(bytes),
            "trusted pre-load fresh host state covers canonical restart before first level notification");
    }

    void persisted_round_trip()
    {
        setup();P::begin_load();profile[R::highest_level]=35;P::finish_load(3,true,true);
        std::array<std::uint8_t,R::profile_size> bytes{};
        check(pending(bytes),"round-trip has an actual production snapshot");
        auto data=std::make_shared<lcsave::detail::TablesData>();
        data->build=std::make_unique<lcsave::detail::Build>();
        lcsave::detail::CatEntry hero;hero.domain=1;hero.id=0x01000001;hero.key="synthetichero";
        data->catalogue.entries.push_back(hero);data->catalogue.index();
        data->reference.STAT={0,hero.id};data->build->T=data->reference;
        data->build->stat={"","synthetichero"};data->p_stat[hero.id]=1;
        lcsave::Tables tables;tables.data=data;
        const auto saved=lcsave::old_profile_to_new(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),tables,lcsave::NewFileContext{});
        check(saved.ok,"repaired profile uses unchanged LC writer");
        if (!saved.ok) { std::printf("writer error %s\n",saved.error.c_str());return; }
        const auto file=lcsave::detail::read_file(saved.bytes.data(),saved.bytes.size(),lcsave::kind_profile);
        check(file.data("PRFP")["highest_level"].as_int()==35,"PRFP.highest_level persists repaired value");
        const auto restored=lcsave::new_to_old_profile(saved.bytes,tables);
        check(restored.ok,"persisted LC profile reloads without schema change");
        if (!restored.ok) { std::printf("reader error %s\n",restored.error.c_str());return; }
        check(restored.bytes.size()==0x84+bytes.size() &&
            std::memcmp(restored.bytes.data()+0x84,bytes.data(),bytes.size())==0,
            "save/reload preserves complete profile, including character unlocks and options");
        P::profile_written(bytes.data(),bytes.size());
        setup();std::memcpy(profile.data()+4,restored.bytes.data()+0x84,bytes.size());
        check(highest()==35,"fresh process profile starts with durable repaired history");
    }

    void hook_abi(bool legacy)
    {
        setup();loaded();
        // Synthetic entry supplies the same stack/ESI contract as the native
        // notification tail. The production patch and native delta tail execute.
        const std::uint8_t entry[]{0x51,0x56,0x8b,0x74,0x24,0x10,0x8b,0x44,0x24,0x0c,0x89,0x44,0x24,0x04,0xe9,4,0,0,0};
        std::memcpy(memory(0x4b7e00),entry,sizeof entry);
        for(const auto& g:R::guards) if(g.va==R::notification_site)
            for(std::size_t i=0;i<limits_rules::hex_size(g.hex);++i) memory(g.va)[i]=limits_rules::hex_byte(g.hex,i);
        const std::uint8_t tail[]{0x0f,0xb6,0x46,0x1c,0x2b,0x44,0x24,0x04,0x5e,0x59,0xc3};
        std::memcpy(memory(0x4b7e3b),tail,sizeof tail);
        FlushInstructionCache(GetCurrentProcess(),memory(0x4b7e00),0x50);
        using tail_t=int(__cdecl*)(int,void*);
        heroes[1][R::stats_level]=36;
        check(reinterpret_cast<tail_t>(0x4b7e00)(35,heroes[1].data())==1 && highest()==35,
            "real legacy notification reproduces stale profile after loaded campaign level-up");
        if(!legacy)
        {
            const auto patch=P::notification_bytes();std::memcpy(memory(R::notification_site),patch.data(),patch.size());
            FlushInstructionCache(GetCurrentProcess(),memory(0x4b7e00),0x50);
        }
        check(reinterpret_cast<tail_t>(0x4b7e00)(35,heroes[1].data())==1 && highest()==36,
            "production notification patch preserves stack and native level-delta return");
    }
}

int main(int argc,char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    // union_space.asm reserves these addresses in the executable image before
    // the compiler's code. The loader therefore owns the span before a process
    // heap or another early allocation can occupy a native fixture address.
    if(address(GetModuleHandleW(nullptr))!=0x400000 || address(union_space)!=0x401000)
    { std::printf("fixture image is not based at 0x400000 with union space at 0x401000\n");return 2; }
    DWORD fixture_protection=0;
    if(!VirtualProtect(union_space,0x700000,PAGE_EXECUTE_READWRITE,&fixture_protection))
    { std::printf("fixture protection failed: %lu\n",GetLastError());return 2; }
    // Exercise the guarded native monotonic setter itself, supplied by its
    // interoperability signature, never copied from a game file.
    for(const auto& g:R::guards) if(g.va==0x48f280)
        for(std::size_t i=0;i<limits_rules::hex_size(g.hex);++i) memory(g.va)[i]=limits_rules::hex_byte(g.hex,i);
    bind(R::profile_vtable,0x2c,memory(0x48f280));
    // The legacy control invokes native notification signatures against getters
    // returning synthetic objects, with the native story predicate itself.
    for(const auto& g:R::guards) if(g.va==0x469d50)
        for(std::size_t i=0;i<limits_rules::hex_size(g.hex);++i) memory(g.va)[i]=limits_rules::hex_byte(g.hex,i);
    bind(R::game_vtable,0x27c,memory(0x469d50));
    for(const auto pair:{std::pair{R::game_getter,game.data()},std::pair{R::profile_getter,profile.data()}})
    {
        memory(pair.first)[0]=0xb8;put(memory(pair.first),1,address(pair.second));memory(pair.first)[5]=0xc3;
    }
    bind(R::registry_vtable,0x48,reinterpret_cast<const void*>(&count));
    bind(R::registry_vtable,0x4c,reinterpret_cast<const void*>(&index));
    bind(R::registry_vtable,0x74,reinterpret_cast<const void*>(&stats));
    bind(R::registry_vtable,0x78,reinterpret_cast<const void*>(&has));
    bind(R::registry_vtable,0x7c,reinterpret_cast<const void*>(&normal));
    bind(R::registry_vtable,0x84,reinterpret_cast<const void*>(&campaign));
    P::get_profile=&fake_profile;P::get_registry=&fake_registry;P::get_game=&fake_game;P::get_session=&fake_session;
    if(!FlushInstructionCache(GetCurrentProcess(),union_space,0x700000))
    { std::printf("fixture instruction flush failed: %lu\n",GetLastError());return 2; }
    P::activate();
    regression_cases();persisted_round_trip();hook_abi(argc>1 && std::strcmp(argv[1],"--legacy")==0);
    DWORD ignored=0;
    if(!VirtualProtect(union_space,0x700000,fixture_protection,&ignored))
    { std::printf("fixture protection restore failed: %lu\n",GetLastError());return 2; }
    std::printf("profile progress: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
