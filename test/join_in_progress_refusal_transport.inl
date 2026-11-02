// Real retail enqueue, packet serializer, session send and peer retirement.
// Only memory-pool backing, accounting and the final datagram sink are invented.
namespace refusal_transport_fixture {
std::vector<unsigned char> transport(0x15000),peer(0x30c0),cache(0x149ac),nodes(0x3dac);
unsigned vtable[4]{};
std::vector<std::vector<unsigned char>> packets;
unsigned retired=0;
void* __cdecl pool() { return cache.data(); }
void* __cdecl node_pool() { return nodes.data(); }
void __fastcall stats(void*,void*,unsigned,unsigned) {}
void __fastcall stats_one(void*,void*) {}
float __fastcall stats_time(void*,void*) { return 0.f; }
char __fastcall no_gate(void*,void*,unsigned) { return 0; }
int __fastcall wire(void*,void*,const void*,unsigned short port,const unsigned char* data,unsigned size) {
    assert(port==*reinterpret_cast<unsigned short*>(peer.data()+8));
    assert(peer[0x19]); // the refusal must leave while the native peer is live
    packets.emplace_back(data,data+size);return int(size);
}
void clear_queue() {
    while(auto* n=*reinterpret_cast<unsigned char**>(peer.data()+0x30a0)) {
        auto* h=*reinterpret_cast<unsigned char**>(n+0x10);
        reinterpret_cast<void(__thiscall*)(void*,void*,unsigned)>(0x60e980)(cache.data(),*reinterpret_cast<void**>(h),*reinterpret_cast<unsigned short*>(h+8));
        reinterpret_cast<void(__thiscall*)(void*,void*)>(0x60e8f0)(cache.data(),h);
        reinterpret_cast<bool(__thiscall*)(void*,void*)>(0x611e90)(peer.data()+0x30a0,n);
    }
}
void __fastcall retire(void*,void*,unsigned) { ++retired;clear_queue(); }
void prepare(unsigned short port) {
    std::fill(transport.begin(),transport.end(),0);std::fill(peer.begin(),peer.end(),0);
    std::fill(cache.begin(),cache.end(),0);std::fill(nodes.begin(),nodes.end(),0);
    for(unsigned i=0;i<768;++i) { put(cache.data(),0x3c60+4*i,i);put(cache.data(),0x48d4+0xb404+4*i,i); }
    for(unsigned i=0;i<50;++i) put(cache.data(),0x1094c+0x3f7c+4*i,i);
    for(unsigned i=0;i<650;++i) put(nodes.data(),0x331c+4*i,i);
    put(cache.data(),0x486c,768u);put(cache.data(),0x48d4+0xc010,768u);put(cache.data(),0x1094c+0x4050,50u);put(nodes.data(),0x3d50,650u);
    put(peer.data(),4,peer.data());put(peer.data(),8,port);put(peer.data(),0x14,0x02004d0au);peer[0x19]=1;
    vtable[3]=unsigned(reinterpret_cast<std::uintptr_t>(&wire));put(transport.data(),0,vtable);
    put(transport.data(),0x137b8,1.f);put(s,0x41c,transport.data());
    assert(reinterpret_cast<bool(__thiscall*)(void*,void*,unsigned)>(0x60eb20)(transport.data()+0x124b8,peer.data(),~0u));
    packets.clear();retired=0;
}
void queued_refusal() {
    unsigned char h[0x20]{};reinterpret_cast<void(__thiscall*)(void*)>(0x615810)(h);
    put(h,0,join_in_progress_core::started_refusal.data());put(h,8,static_cast<unsigned short>(join_in_progress_core::started_refusal.size()));h[0xe]=1;h[0x10]=0x1c;
    assert(reinterpret_cast<char(__thiscall*)(void*,void*,void*,int)>(0x610f30)(s,peer.data(),h,0));
}
void run() {
    std::cout<<std::unitbuf;
    redirect(join_in_progress_rules::session_getter,session);
    redirect(0x60f240,pool);redirect(0x6067d0,node_pool);redirect(0x60d710,stats);redirect(0x60d6d0,stats);
    redirect(0x60d690,stats_one);redirect(0x60d550,stats_time);redirect(0x610770,no_gate);redirect(0x60e4f0,retire);
    for(unsigned short port:{static_cast<unsigned short>(5165),static_cast<unsigned short>(65496)}) {
        prepare(port);queued_refusal();assert(packets.empty() && *reinterpret_cast<unsigned*>(peer.data()+0x30ac)==1);
        reinterpret_cast<void(__thiscall*)(void*,int)>(0x6106f0)(peer.data(),0);
        reinterpret_cast<void(__thiscall*)(void*)>(0x60f0a0)(transport.data());
        assert(retired==1 && packets.empty() && !*reinterpret_cast<unsigned*>(peer.data()+0x30ac));
        std::cout<<"PASS control port "<<port<<": sent=1 only queued; native retirement discards unsent refusal\n";
        prepare(port);queued_refusal();
        reinterpret_cast<void(__thiscall*)(void*,void*)>(0x60f940)(transport.data(),peer.data());
        assert(packets.size()==1);
        reinterpret_cast<void(__thiscall*)(void*,int)>(0x6106f0)(peer.data(),0);
        reinterpret_cast<void(__thiscall*)(void*)>(0x60f0a0)(transport.data());
        assert(retired==1 && packets.size()==1);
        std::cout<<"PASS control port "<<port<<": flushing before retirement delivers the same queued refusal\n";
    }
    for(unsigned short port:{static_cast<unsigned short>(5165),static_cast<unsigned short>(65496)}) {
        prepare(port);unsigned char event[0x1c]{};put(event,0x10,peer.data());
        // Native service output can already precede the refusal. A variable
        // 0x0f ACK plus fixed two-byte 0x0a receipt reproduces the live 23-byte
        // packet (length word 0x15), without embedding captured game bytes.
        *reinterpret_cast<unsigned short*>(0xa3e490+0x0a*2)=2;
        unsigned char ack[0x20]{},receipt[2]{1,0};reinterpret_cast<void(__thiscall*)(void*)>(0x615810)(ack);
        put(ack,0,receipt);put(ack,8,static_cast<unsigned short>(2));ack[0x10]=0x0f;
        assert(reinterpret_cast<char(__thiscall*)(void*,void*,void*,int)>(0x610f30)(s,peer.data(),ack,0));
        join_in_progress::machine.started=true;join_in_progress::allow_late_join=false;
        assert(join_in_progress::refuse_started_admission(event));
        assert(packets.size()==1 && !peer[0x19]);
        const auto& bytes=packets.front();
        assert(bytes.size()==23 && bytes[0]==0x15);
        const auto& marker=join_in_progress_core::started_refusal;
        assert(std::search(bytes.begin(),bytes.end(),marker.begin(),marker.end())!=bytes.end());
        reinterpret_cast<void(__thiscall*)(void*)>(0x60f0a0)(transport.data());
        assert(retired==1 && packets.size()==1);
        std::cout<<"PASS refusal port "<<port<<": one tagged native packet before disconnect/retirement ("<<bytes.size()<<" bytes)\n";
    }
}
}
