namespace network_fixture {
void (*draw_hook)(void*)=nullptr;
const char* hold_text=nullptr;
std::string drawn;
unsigned draws=0,formats=0;
const char* __fastcall format(void*,void*,const char* text) { ++formats;return text; }
void run() {
    assert(draw_hook);
    redirect(0x610df0,format);
    auto* session=reinterpret_cast<unsigned char*>(0xa53058);
    auto* network=reinterpret_cast<unsigned char*>(0xa3df68);
    std::array<char,256> text{};std::strcpy(text.data(),"Synthetic network notice");
    std::memset(network+0x1c8,0,0x34);put(network,0x1c8,unsigned(reinterpret_cast<std::uintptr_t>(text.data())));
    set_current(nullptr);session[0x44c]=1;
    const auto queue=std::vector<unsigned char>(network+0x1c8,network+0x1fc);
    draw_hook(reinterpret_cast<void*>(1));assert(drawn==text.data() && draws==1 && formats==1);
    assert(std::memcmp(queue.data(),network+0x1c8,queue.size())==0);
    // Priority and expiry remain the scheduler's: merely observe its current
    // slot, and never retire, reorder, renew, pause or resume it while drawing.
    put(network,0x1f8,1);drawn.clear();draw_hook(reinterpret_cast<void*>(1));assert(drawn.empty() && draws==1);
    std::strcpy(text.data(),"~05Synthetic ~04notice");put(network,0x1d8,unsigned(reinterpret_cast<std::uintptr_t>(text.data())));
    draw_hook(reinterpret_cast<void*>(1));assert(drawn=="Synthetic notice" && draws==2);
    const auto before=formats;hold_text="Joining";drawn.clear();draw_hook(reinterpret_cast<void*>(1));assert(drawn.empty() && formats==before);hold_text=nullptr;
    set_current(hub.data());draw_hook(reinterpret_cast<void*>(1));assert(drawn.empty());set_current(nullptr);
    session[0x44c]=0;draw_hook(reinterpret_cast<void*>(1));assert(drawn.empty());session[0x44c]=1;
    put(network,0x1f8,3);draw_hook(reinterpret_cast<void*>(1));assert(drawn.empty());put(network,0x1f8,1);
    text.fill('x');draw_hook(reinterpret_cast<void*>(1));assert(drawn.empty());
    text[255]=0;draw_hook(reinterpret_cast<void*>(1));assert(drawn.size()==255);
    put(network,0x1d8,0);drawn.clear();draw_hook(reinterpret_cast<void*>(1));assert(drawn.empty());
    std::cout<<"PASS native network notice selection, empty/retired/invalid slots, bounded copy, hold/menu priority and no queue/timer writes\n";
}
}
