// b3: the native 4057 "Online play has lost synchronization." notice at the frame gate (0x606c97) and the checksum
// compare (0x614e62), and the lone-host close-on-release count (0x5ce829 / 0x5e6728). The production bridge runs
// against a synthetic network object; the native 0x60a600 is redirected to a counting stub. No game entry point runs.
namespace lost_sync_fixture {
alignas(16) unsigned char network[0x200]{};
unsigned shown=0;void* shown_network=nullptr;const char* shown_text=nullptr;int shown_priority=0;float shown_seconds=0;
// 0x60a600's thiscall (ecx = network, text, priority, seconds; `ret 0xc`) as a fastcall with a dummy edx.
void __fastcall show(void* self,void*,const char* text,int priority,float seconds) {
    ++shown;shown_network=self;shown_text=text;shown_priority=priority;shown_seconds=seconds;
}
using show_t=void(__thiscall*)(void*,const char*,int,float);
// Calls the bridge as both sites do and proves the stack is balanced (the skip path returns with `ret 0xc` itself).
void notice(void* self,const char* text,int priority=0x23,float seconds=5.f) {
    const auto bridge=reinterpret_cast<show_t>(&join_in_progress::lost_sync_message_bridge);
    unsigned before=0,after=0;
    __asm { mov before,esp }
    bridge(self,text,priority,seconds);
    __asm { mov after,esp }
    assert(before==after);
}
void slot(unsigned i,const char* text,int priority) {
    put(network,join_in_progress_rules::message_slots_at+i*join_in_progress_rules::message_slot_bytes,text);
    put(network,join_in_progress_rules::message_slots_at+i*join_in_progress_rules::message_slot_bytes+4,priority);
}
unsigned lines(const char* part) {
    unsigned n=0;for(const auto& line:observed_jip_logs) n+=line.find(part)!=std::string::npos;return n;
}
void run() {
    using namespace join_in_progress_rules;
    // The guarded retail bytes the transaction replaces, read from the mapped image.
    for(std::size_t i=0;i<lost_sync_message_calls.size();++i)
        assert(std::memcmp(reinterpret_cast<const void*>(lost_sync_message_calls[i]),lost_sync_message_originals[i].data(),5)==0);
    static const unsigned char pda[]{0x83,0xf8,0x01,0x0f,0x8e,0x22,0x03,0x00,0x00},team[]{0x83,0xf8,0x01,0x0f,0x8e,0xa8,0x02,0x00,0x00};
    assert(std::memcmp(reinterpret_cast<const void*>(0x5ce827),pda,sizeof pda)==0 && std::memcmp(reinterpret_cast<const void*>(0x5e6726),team,sizeof team)==0);
    assert(solo_release_counts[0]==0x5ce827+2 && solo_release_counts[1]==0x5e6726+2);
    assert(join_in_progress::mismatch()==nullptr);
    for(const auto va:{0x606c97u,0x614e62u,0x5ce829u,0x5e6728u}) { // every replaced byte lies inside a b3 guard
        bool covered=false;
        for(const auto& g:guards) covered|=g.va<=va && va<g.va+g.hex.size()/2 && std::string_view(g.what).starts_with("b3:");
        assert(covered);
    }
    // The transaction: applies all four sites at once and rolls them back to the exact retail bytes.
    const auto writes=join_in_progress::polish_patches();
    assert(writes.size()==4);
    xml1_hook_transaction::memory memory;bool safe=false;
    assert(xml1_hook_transaction::apply(memory,writes,&safe) && safe);
    for(std::size_t i=0;i<2;++i) {
        const auto* site=reinterpret_cast<const unsigned char*>(lost_sync_message_calls[i]);
        std::int32_t relative=0;std::memcpy(&relative,site+1,4);
        assert(site[0]==0xe8 && lost_sync_message_calls[i]+5+relative==std::uint32_t(reinterpret_cast<std::uintptr_t>(&join_in_progress::lost_sync_message_bridge)));
    }
    assert(*reinterpret_cast<const unsigned char*>(0x5ce829)==0 && *reinterpret_cast<const unsigned char*>(0x5e6728)==0);
    assert(*reinterpret_cast<const unsigned char*>(0x5ce827)==0x83 && *reinterpret_cast<const unsigned char*>(0x5ce82a)==0x0f); // cmp eax,0; jle stays
    assert(!xml1_hook_transaction::apply(memory,writes,&safe) && safe); // a second install refuses (the bytes are no longer retail)
    std::vector<xml1_hook_transaction::patch> undo;
    for(const auto& w:writes) undo.push_back({w.address,w.after,w.before});
    assert(xml1_hook_transaction::apply(memory,undo,&safe) && safe);
    for(std::size_t i=0;i<2;++i) assert(std::memcmp(reinterpret_cast<const void*>(lost_sync_message_calls[i]),lost_sync_message_originals[i].data(),5)==0);
    assert(std::memcmp(reinterpret_cast<const void*>(0x5ce827),pda,sizeof pda)==0 && std::memcmp(reinterpret_cast<const void*>(0x5e6726),team,sizeof team)==0);
    assert(join_in_progress::mismatch()==nullptr);
    // A partial write rolls every site back.
    {
        struct failing : xml1_hook_transaction::memory { int left=3; bool write(std::uint32_t a,const void* p,std::size_t n) { if(left--==0) return false; return xml1_hook_transaction::memory::write(a,p,n); } };
        failing broken;assert(!xml1_hook_transaction::apply(broken,writes,&safe) && safe);
        for(std::size_t i=0;i<2;++i) assert(std::memcmp(reinterpret_cast<const void*>(lost_sync_message_calls[i]),lost_sync_message_originals[i].data(),5)==0);
        assert(*reinterpret_cast<const unsigned char*>(0x5ce829)==1 && *reinterpret_cast<const unsigned char*>(0x5e6728)==1);
    }
    std::cout<<"PASS b3 transaction: 0x606c97/0x614e62 CALL -> lost_sync_message_bridge and 0x5ce829/0x5e6728 cmp eax,1 -> 0 under b3 guards; applied together, refused twice, undone/rolled back to the exact retail bytes\n";

    redirect(message_show,show);
    static const char text[]="Online play has lost synchronization.";
    static const char copy[]="Online play has lost synchronization.";
    static const char other[]="Player(s) have been dropped from the game.";
    // 1. The cohort checksum hold is on: nothing is queued, the native is not called, the counter counts.
    join_in_progress::state=1;join_in_progress::epoch.active=true;assert(join_in_progress::checksum_hold());
    observed_jip_logs.clear();join_in_progress::lost_sync_suppressed=0;
    const auto before=std::vector<unsigned char>(std::begin(network),std::end(network));
    notice(network,text,0x23,5.f);notice(network,text,0x23,8.f);
    assert(shown==0 && join_in_progress::lost_sync_suppressed==2 && std::equal(before.begin(),before.end(),network));
    assert(lines("native 'lost synchronization' notice from 0x")==2 && lines("not shown - the cohort checksum hold is on")==2);
    for(int i=0;i<8;++i) notice(network,text);
    assert(shown==0 && join_in_progress::lost_sync_suppressed==10 && lines("not shown - the cohort checksum hold is on")==4); // counts 1, 2, 4, 8 only
    std::cout<<"PASS b3 lost sync (hold on): the 4057 notice is not queued at either site while checksum_hold() is on; the native message is never entered, the stack balances through `ret 0xc`, and the log names the caller at counts 1, 2, 4, 8 only\n";
    // 2. The hold is off: the native runs once with the site's arguments.
    join_in_progress::epoch.active=false;assert(!join_in_progress::checksum_hold());
    notice(network,text,0x23,5.f);
    assert(shown==1 && shown_network==network && shown_text==text && shown_priority==0x23 && shown_seconds==5.f);
    join_in_progress::state=0;notice(network,text,0x23,8.f); // JIP not active at all: native as well
    assert(shown==2 && shown_seconds==8.f);
    std::cout<<"PASS b3 lost sync (hold off): the native 0x60a600 runs with the original network, text, priority 0x23 and 5 s / 8 s\n";
    // 3. Shown once: the same text queued in any slot (same pointer, or an equal string) is not queued again; another is.
    for(unsigned i=0;i<message_slot_count;++i) {
        std::memset(network,0,sizeof network);slot(i,text,0x23);
        notice(network,text);assert(shown==2);
        slot(i,copy,0x23);notice(network,text);assert(shown==2);
    }
    std::memset(network,0,sizeof network);slot(0,other,0x14);
    notice(network,text);assert(shown==3 && shown_text==text);
    std::memset(network,0,sizeof network);slot(2,text,0x23);
    notice(network,other,0x14,8.f);assert(shown==4 && shown_text==other); // a different message is not mistaken for the queued 4057; the two native sites normally pass only 4057
    std::cout<<"PASS b3 lost sync (once): an already queued 4057 (any of the 3 slots, same pointer or equal text) is not queued again; an empty queue or a different text is\n";
    // 4. An unreadable network object falls back to the native call.
    auto* unreadable=reinterpret_cast<void*>(0x10);
    notice(unreadable,text);assert(shown==5 && shown_network==unreadable);
    std::cout<<"PASS b3 lost sync (fallback): an unreadable message queue leaves the native call as it is\n";
}
}
