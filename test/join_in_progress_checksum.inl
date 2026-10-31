// Exercise the retail checksum producer under invented world/camera services.
// No executable entry point, rendering, socket or game launch is involved.
namespace checksum_fixture {
std::array<std::array<unsigned char,0x400>,4> actors{};
std::array<unsigned,32> actor_type{};
unsigned actor_vtable[1]{},game_vtable_data[0x120/4]{},camera_vtable[0xd8/4]{};
unsigned* fake_game=game_vtable_data;unsigned* fake_camera=camera_vtable;
unsigned char frame_clock[0x30]{};
float camera_a[3]{1.f,2.f,3.f},camera_b[3]{4.f,5.f,6.f};
unsigned broadcasts=0,hash=0,admission_allocations=0,refusals=0,disconnects=0;
unsigned short wire_frame=0;
void* __cdecl frame_get() { return frame_clock; }
void* __cdecl game_get() { return &fake_game; }
void* __cdecl camera_get() { return &fake_camera; }
void* __fastcall local_get(void*,void*) { return local; }
unsigned __fastcall local_index(void*,void*) { return 0; }
void __fastcall party(void*,void*,void* list) {
    for(unsigned i=0;i<4;++i) put(list,i*4,i+1);
    put(list,0x14,4u);
}
void* __fastcall actor_get(const unsigned* handle,void*) {
    assert(*handle>=1 && *handle<=4);return actors[*handle-1].data();
}
void* __fastcall type(void*,void*) { return actor_type.data(); }
void __fastcall camera_first(void*,void*,void* out) { std::memcpy(out,camera_a,sizeof camera_a); }
void* __fastcall camera_second(void*,void*) { return camera_b; }
char __fastcall broadcast(void*,void*,const unsigned char* message,int,int,int) {
    assert(message[0x10]==0x2e && *reinterpret_cast<const unsigned short*>(message+8)==12);
    const auto* body=*reinterpret_cast<const unsigned char* const*>(message);
    std::memcpy(&hash,body+4,4);std::memcpy(&wire_frame,body+8,2);++broadcasts;return 1;
}
void patch(unsigned va,const std::vector<unsigned char>& bytes) { std::memcpy(reinterpret_cast<void*>(va),bytes.data(),bytes.size()); }
void produce_with_sentinel(void* checksums) {
    const auto produce=reinterpret_cast<void(__thiscall*)(void*)>(0x614a40);
    unsigned short before=0,after=0;float sentinel=0;
    __asm { fnstcw before
            fld1 }
    produce(checksums);
    __asm { fnstcw after
            fstp sentinel }
    assert(before==after && sentinel==1.f);
}
void run() {
    using namespace join_in_progress_rules;
    redirect(session_getter,session);redirect(0x6111f0,local_get);redirect(0x611250,local_index);
    redirect(0x606d00,frame_get);redirect(game_getter,game_get);redirect(entity_of_handle,actor_get);
    redirect(0x4457e0,camera_get);redirect(0x611270,broadcast);
    game_vtable_data[0x11c/4]=unsigned(reinterpret_cast<std::uintptr_t>(party));
    actor_vtable[0]=unsigned(reinterpret_cast<std::uintptr_t>(type));actor_type.fill(~0u);
    camera_vtable[0xd4/4]=unsigned(reinterpret_cast<std::uintptr_t>(camera_first));
    camera_vtable[0x84/4]=unsigned(reinterpret_cast<std::uintptr_t>(camera_second));
    for(unsigned i=0;i<4;++i) {
        put(actors[i].data(),0,actor_vtable);
        for(unsigned k=0;k<6;++k) put(actors[i].data(),0x20+k*4,float(i+k+1));
        put(actors[i].data(),actor_controller,i);put(actors[i].data(),hero_ai_flags,i>1 ? 1u : 0u);
    }
    put(reinterpret_cast<void*>(0x70b840),0,0u); // synthetic class id: native type validation stays active
    put(local,record_flags,1u);put(local,0x18,0u);put(local,0x30,0u);
    put(s,0x26c,nodes[0]);put(s,0x278,1u);put(nodes[0],node_record,local);put(nodes[0],node_next,static_cast<void*>(nullptr));
    put(reinterpret_cast<void*>(0x6e0874),0,0x123456u);put(reinterpret_cast<void*>(0x7ad8b4),0,7u);
    void* checksums=reinterpret_cast<void*>(0xa536d8);put(checksums,0xec,0x10u);
    const auto original_actors=actors;
    auto emit=[&](unsigned short frame) {
        put(frame_clock,0x1c,frame);put(checksums,8,frame);put(checksums,0xa,static_cast<unsigned short>(30));
        produce_with_sentinel(checksums);
        assert(hash==1200028 && wire_frame==frame && actors==original_actors);
        assert(*reinterpret_cast<unsigned*>(0x7ad8b4)==7 && *reinterpret_cast<unsigned*>(0x6e0874)==0x123456);
    };
    emit(30);assert(broadcasts==1); // exact uninstrumented negative/control image
    patch(0x614b05,join_in_progress::rel(0xe9,0x614b05,reinterpret_cast<void*>(join_in_progress::checksum_actor_bridge),6));
    patch(0x614b77,join_in_progress::rel(0xe9,0x614b77,reinterpret_cast<void*>(join_in_progress::checksum_hero_bridge),6));
    patch(0x614bad,join_in_progress::rel(0xe9,0x614bad,reinterpret_cast<void*>(join_in_progress::checksum_camera_bridge),5));
    patch(0x614bd4,join_in_progress::rel(0xe9,0x614bd4,reinterpret_cast<void*>(join_in_progress::checksum_produced_bridge),7));
    observed_jip_logs.clear();join_in_progress::arm_checksums();
    for(unsigned frame:{30u,60u,90u,120u,150u,180u}) emit(static_cast<unsigned short>(frame));
    assert(broadcasts==7 && join_in_progress::checksum_window.produced==4);
    unsigned produced=0,heroes=0;
    for(const auto& line:observed_jip_logs) {
        if(line.starts_with("jip checksum produced:")) {
            ++produced;
            assert(line.find("complete=1 hero_sum=0x00124F80 camera=0x00000015 rng_draws=7 seed=0x00123456")!=std::string::npos);
        }
        if(line.starts_with("jip checksum hero:")) ++heroes;
    }
    assert(produced==4 && heroes==16);
    std::cout<<"PASS AC retail checksum producer: exact hero/camera/RNG sum, identical native wire before/after all four bridges, x87 stack/control, actor/ownership/seed/count unchanged; 4 periods then silence\n";
}
char __fastcall capacity(void*,void*,void*) { return 1; }
void* __fastcall allocate(void*,void*,int) { ++admission_allocations;return nullptr; }
char __fastcall refusal_send(void*,void*,void*,const unsigned char* message,int) {
    assert(message[0x10]==0x1c && message[0xe]==1);
    assert(join_in_progress_core::is_started_refusal(*reinterpret_cast<const void* const*>(message),*reinterpret_cast<const unsigned short*>(message+8)));
    ++refusals;return 1;
}
void __fastcall disconnect(void*,void*,int) { ++disconnects; }
unsigned refusal_flushes=0;
void __fastcall refusal_flush(void*,void*,void*) { ++refusal_flushes;assert(refusal_flushes==refusals && disconnects+1==refusals); }
void admission_run() {
    using namespace join_in_progress_rules;
    redirect(session_getter,session);redirect(manager_getter,manager);
    redirect(0x615910,capacity);redirect(0x612a40,allocate);redirect(send_to,refusal_send);redirect(0x6106f0,disconnect);redirect(flush_peer,refusal_flush);
    unsigned char event[0x1c]{},peer[0x40]{},body[8]{};
    put(event,4,body);put(event,0x10,peer);put(peer,4,peer);
    const auto handler=reinterpret_cast<int(__cdecl*)(void*)>(0x609970);
    auto& machine=join_in_progress::machine;
    join_in_progress::state=1;machine.reset();
    assert(join_in_progress::late_join_configurable() && join_in_progress::set_late_join_enabled(true));
    assert(join_in_progress::late_join_enabled() && join_in_progress::set_late_join_enabled(false));
    machine.started=true;assert(!join_in_progress::set_late_join_enabled(true));
    assert(!join_in_progress::late_join_enabled());
    handler(event);assert(admission_allocations==1 && refusals==0); // int9 entered allocation after START
    patch(admission_entry,join_in_progress::rel(0xe9,admission_entry,reinterpret_cast<void*>(join_in_progress::admission_bridge),5));
    join_in_progress::allow_late_join=false;
    handler(event);assert(admission_allocations==1 && refusals==1 && disconnects==1);
    machine.started=false;handler(event);assert(admission_allocations==2 && refusals==1); // return to lobby
    machine.started=true;join_in_progress::allow_late_join=true;handler(event);assert(admission_allocations==3 && refusals==1);
    join_in_progress::allow_late_join=false;
    for(unsigned i=0;i<3;++i) handler(event);
    assert(admission_allocations==3 && refusals==4 && disconnects==4 && !machine.any());
    std::cout<<"PASS AC retail admission: int9 control enters allocation after START; safe default refuses first with tagged native 0x1c, no participant/controller/AI mutation; lobby and explicit opt-in retain allocation; repeated requests never become pending\n";
}
unsigned input_recipients=0;
char __fastcall input_enqueue(void*,void*,void* endpoint,const unsigned char* message) {
    assert(endpoint==reinterpret_cast<void*>(7) && message[0x10]==0x02);
    ++input_recipients;return 1;
}
void send_set_run() {
    using namespace join_in_progress_rules;
    redirect(session_getter,session);redirect(manager_getter,manager);redirect(pause_getter,pause_get);redirect(zones_getter,zones);
    redirect(0x60f620,input_enqueue);
    unsigned char peer[0x40]{};put(peer,4,7u);
    put(s,0x41c,m);put(s,0x26c,nodes[0]);put(s,0x278,2u);
    put(nodes[0],node_record,local);put(nodes[0],node_next,nodes[1]);put(nodes[1],node_record,remote);put(nodes[1],node_next,static_cast<void*>(nullptr));
    put(local,record_flags,1u);put(local,0x18,0u);put(local,record_active,static_cast<unsigned char>(1));
    put(remote,record_flags,0x2408u);put(remote,0x18,1u);put(remote,record_peer,peer);
    put(remote,record_active,static_cast<unsigned char>(1));
    auto& machine=join_in_progress::machine;machine.reset();machine.started=true;
    assert(machine.defer_add(remote,1,false,GetTickCount()));
    patch(broadcast_entry,join_in_progress::rel(0xe9,broadcast_entry,reinterpret_cast<void*>(join_in_progress::broadcast_entry_bridge),9));
    for(unsigned i=0;i<3;++i)patch(broadcast_local_tests[i],join_in_progress::rel(0xe8,broadcast_local_tests[i],reinterpret_cast<void*>(join_in_progress::broadcast_skip_bridge),5));
    unsigned char message[0x20]{};unsigned short frame=0;put(message,0,&frame);put(message,8,static_cast<unsigned short>(2));message[0x10]=0x02;message[0xe]=1;
    const auto broadcast_native=reinterpret_cast<char(__thiscall*)(void*,const void*,int,int,int)>(broadcast_entry);
    assert(broadcast_native(s,message,-1,1,0) && input_recipients==0); // pending cannot enter lockstep
    auto* seat=machine.find(remote);assert(seat);seat->at=join_in_progress_core::stage::starting;
    put(pause,pause_loaded+4,2u);put(reinterpret_cast<void*>(0x8aff18),0,static_cast<void*>(nullptr));
    join_in_progress::state=1;join_in_progress::epoch.begin();join_in_progress::epoch.start();join_in_progress::epoch.saw_loading=true;
    join_in_progress::advance(s,true,pause);join_in_progress::observe_epoch();
    assert(!machine.pending(remote) && !machine.holding() && !join_in_progress::epoch.active);
    for(frame=0;frame<60;++frame)assert(broadcast_native(s,message,-1,1,0));
    assert(input_recipients==60);
    join_in_progress::advance(s,true,pause);assert(!machine.any());
    assert(broadcast_native(s,message,-1,1,0) && input_recipients==61);
    std::cout<<"PASS AC send set: real retail broadcast excludes pending, sends all60 frames0..59 to the admitted peer after cohort sync and retains it after seat cleanup; no duplicate participant insertion\n";
}

}
