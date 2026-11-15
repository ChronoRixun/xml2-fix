// Production presentation/admission paths with invented native services.
// Mapping the retail image is read-only input; this never starts the game.
namespace skirmish_fixture {
unsigned messages=0;
void* __cdecl scheduler() { return nullptr; }
void __fastcall message(void*,void*,const char* text,int,float) {
    assert(std::string_view(text)==join_in_progress::spectator_text);++messages;
}
void setup() {
    using namespace join_in_progress_rules;
    redirect(session_getter,session);redirect(pause_getter,pause_get);
    redirect(scheduler_getter,scheduler);redirect(message_show,message);
    put(s,0x26c,nodes[0]);put(s,0x278,1u);
    put(nodes[0],node_record,local);put(nodes[0],node_next,static_cast<void*>(nullptr));
    put(local,record_flags,1u);put(local,record_index_at,0);
    auto* game=reinterpret_cast<void*>(game_static);auto* zone=reinterpret_cast<void*>(zones_static);
    put(game,0,game_vtable);put(zone,0,zones_vtable);
    put(zone,zones_loading_at,static_cast<unsigned char>(0));
    std::memcpy(static_cast<char*>(zone)+zones_name_at,"synthetic/arena",16);
    for(unsigned i=0;i<4;++i) put(game,game_party+4*i,0u);
    put(pause,0x6c,static_cast<unsigned char>(0));
    join_in_progress::machine.reset();join_in_progress::machine.started=true;
    join_in_progress::spectator={};messages=0;
}
void banner() {
    setup();
    put(reinterpret_cast<void*>(0x782728),0,static_cast<unsigned char>(0xfd));
    for(unsigned kind=1;kind<=4;++kind) {
        put(reinterpret_cast<void*>(0x784824),0,kind);
        join_in_progress::spectator={};join_in_progress::spectate();
        assert(messages==0 && !join_in_progress::spectator.watching);
    }
    put(reinterpret_cast<void*>(0x782728),0,static_cast<unsigned char>(0xff));
    put(reinterpret_cast<void*>(0x784824),0,0u);
    join_in_progress::spectate();assert(messages==1 && join_in_progress::spectator.watching);
    std::cout<<"PASS Skirmish presentation: no story banner in four modes/results; campaign solo banner retained\n";
}
void admission() {
    // Run the real patched retail admission entry as well as the advertised
    // policy. Refusal precedes participant allocation even with LateJoin=1.
    checksum_fixture::admission_run();
    setup();join_in_progress::state=1;join_in_progress::allow_late_join=true;
    const auto allocations=checksum_fixture::admission_allocations;
    const auto refused=checksum_fixture::skirmish_refusals;
    unsigned char event[0x1c]{},peer[0x40]{};
    put(event,0x10,peer);put(peer,4,peer);
    const auto handler=reinterpret_cast<int(__cdecl*)(void*)>(0x609970);
    put(reinterpret_cast<void*>(0x782728),0,static_cast<unsigned char>(0xfd));
    for(unsigned kind=1;kind<=4;++kind) {
        put(reinterpret_cast<void*>(0x784824),0,kind);
        assert(!join_in_progress::late_join_enabled());
        handler(event);
        assert(checksum_fixture::admission_allocations==allocations && !join_in_progress::machine.any());
    }
    assert(checksum_fixture::skirmish_refusals==refused+4);
    join_in_progress::machine.started=false;handler(event);
    assert(checksum_fixture::admission_allocations==allocations+1); // pre-START lobby remains open
    put(reinterpret_cast<void*>(0x782728),0,static_cast<unsigned char>(0xff));
    put(reinterpret_cast<void*>(0x784824),0,0u);
    assert(join_in_progress::late_join_enabled() && join_in_progress::allow_late_join);
    std::cout<<"PASS Skirmish policy: effective drop-in off for every mode; configured campaign opt-in preserved\n";
}
}
