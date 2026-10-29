// Socket-free teardown during the retail scheduler's ordinary task walk.
namespace gamespy_fixture {
alignas(8) unsigned char network[0x400]{},peer[0x2000]{},node_storage[0x3dac]{};
unsigned char* task_nodes[]{node_storage,node_storage+0x14};
unsigned teardowns=0,natneg_ticks=0,peer_ticks=0;
void* __cdecl network_get() { return network; }
void* __cdecl session_get() { return s; }
void* __cdecl node_pool() { return node_storage; }
void __cdecl empty() {}
void __fastcall empty_method(void*,void*) {}
void __cdecl natneg_tick() { ++natneg_ticks; }
void __cdecl peer_tick(void* value) { assert(value==peer);++peer_ticks; }
__declspec(naked) void peer_pump() {
    __asm { push esi
            call peer_tick
            add esp,4
            ret }
}
void __cdecl teardown(void*) {
    ++teardowns;
    join_in_progress::gamespy_teardown_bridge();
    assert(!*reinterpret_cast<void**>(0xa37d80));
    assert(!*reinterpret_cast<void**>(0xa37d80+0x84));
    assert(*reinterpret_cast<unsigned*>(network+0x18c)==1);
    assert(*reinterpret_cast<unsigned*>(node_storage+0x3da8)==1);
    assert(*reinterpret_cast<unsigned*>(node_storage+0x32c8)==1);
    std::cout<<"after native unlink payload="<<std::hex<<*reinterpret_cast<unsigned*>(task_nodes[1]+0x10)
        <<" callback="<<*reinterpret_cast<unsigned*>(network+0x40)<<std::dec<<std::endl;
}
// Enter exactly the retail ordinary-task walk, with its own saved ESI.
__declspec(naked) void walk(void*) {
    __asm {
        push esi
        mov esi,[esp+8]
        mov eax,60aa83h
        jmp eax
    }
}
int fault(EXCEPTION_POINTERS* e) {
    std::cout<<"ordinary task walk fault: code "<<std::hex<<e->ExceptionRecord->ExceptionCode
        <<" at "<<e->ExceptionRecord->ExceptionAddress<<" reading "<<e->ExceptionRecord->ExceptionInformation[1]<<std::dec<<std::endl;
    std::cout<<"eax="<<std::hex<<e->ContextRecord->Eax<<" esi="<<e->ContextRecord->Esi<<" esp="<<e->ContextRecord->Esp<<std::dec<<std::endl;
    for(unsigned i:{0u,3u,5u}) std::cout<<std::hex<<reinterpret_cast<unsigned*>(e->ContextRecord->Esp)[i]<<" ";std::cout<<std::dec<<std::endl;
    return EXCEPTION_EXECUTE_HANDLER;
}
bool run_walk() {
    __try { walk(network);return true; }
    __except(fault(GetExceptionInformation())) { return false; }
}
void run(bool patched) {
    // Install the exact production callback-entry detour, as the JIP
    // transaction does. No UI module or mouse/pad route participates.
    const auto patch=join_in_progress::rel(0xe9,join_in_progress_rules::gamespy_think,
        reinterpret_cast<const void*>(&join_in_progress::gamespy_think_bridge),join_in_progress_rules::gamespy_think_original.size());
    assert(std::memcmp(reinterpret_cast<void*>(join_in_progress_rules::gamespy_think),join_in_progress_rules::gamespy_think_original.data(),patch.size())==0);
    if(patched) std::memcpy(reinterpret_cast<void*>(join_in_progress_rules::gamespy_think),patch.data(),patch.size());
    redirect(0x60b210,network_get);redirect(0x612be0,session_get);redirect(0x6097b0,manager);
    // Unrelated room/browser/SDK cleanup and singleton addresses are synthetic.
    // Native 605480,
    // 60afe0, 611e90 and 608a50 unlink/destroy/release the task, then clear
    // the Peer. The real node destructor retains the callback payload.
    redirect(0x6052e0,empty_method);redirect(0x605370,empty_method);
    redirect(0x607130,noop_one);redirect(0x63b5a0,empty);redirect(0x63bfd0,empty);redirect(0x62ef80,empty);
    redirect(0x6067d0,node_pool);redirect(0x63bf90,natneg_tick);
    auto* manager=reinterpret_cast<unsigned char*>(0xa37d80);
    reinterpret_cast<void*(__cdecl*)()>(0x605420)();
    put(manager,0,peer);put(manager,0x84,network+0x40);
    put(network,0x38,reinterpret_cast<void*>(teardown));put(network,0x3c,manager);
    put(network,0x40,reinterpret_cast<void*>(0x604da0));put(network,0x44,manager);
    put(node_storage,0x32c8,3u);put(node_storage,0x3d54,3u);put(node_storage,0x3da8,2u);
    put(task_nodes[0],0,0x6a45bcu);put(task_nodes[1],0,0x6a45bcu);
    put(task_nodes[0],8,task_nodes[1]);put(task_nodes[0],0x10,network+0x38);
    put(task_nodes[1],0xc,task_nodes[0]);put(task_nodes[1],0x10,network+0x40);
    put(network,0x180,task_nodes[0]);put(network,0x184,task_nodes[1]);put(network,0x18c,2u);
    network[0x198]=1;s[0x421]=0;
    const bool ok=run_walk();
    assert(ok);assert(teardowns==1 && natneg_ticks==0);
    const auto callback=reinterpret_cast<void(__cdecl*)(void*)>(0x604da0);
    callback(manager);callback(manager);assert(natneg_ticks==0);
    // A newly live peer must still traverse the retail peerThink wrapper and
    // then NNThink. Keeping the host's QR2 advertisement also stays native.
    redirect(0x62e920,peer_pump);put(manager,0,peer);
    callback(manager);assert(peer_ticks==1 && natneg_ticks==1);
    put(peer,0xaf0,1u);join_in_progress::gamespy_teardown_bridge();
    assert(*reinterpret_cast<void**>(manager)==peer);
    callback(manager);assert(peer_ticks==2 && natneg_ticks==2);
    std::cout<<"PASS retail scheduler saved-next GameSpy callback after native teardown: retired peer skipped; live peer/natneg pumps and host QR2 preserved\n";
}
}
