// A42: 60a600 queues three notices; 60aa40/60a690 own expiry/retirement.
// 4029f0 POLLS 60a5d0 to keep GUI/HUD rendering during a stall; it is not a
// text drawing entry point. Draw that same active message at Present, as the
// admission hold already does (the timed native message was invisible there).
namespace network_status_ui {
bool read(char* out) {
    out[0]=0;
    if(!ready || join_in_progress::hold_overlay_text()) return false;
    __try {
        auto* session=static_cast<unsigned char*>(at<getter>(0x612be0)());
        if(session!=at<void*>(0xa53058) || *reinterpret_cast<unsigned*>(session)!=0x6a481c || !session[0x44c]) return false;
        auto* menu=current_peek();
        if(custom(which(menu)) || lobby(which(menu))) return false; // their existing inline/footer service text
        auto* network=static_cast<unsigned char*>(at<getter>(0x60b210)());
        if(network!=at<void*>(0xa3df68) || *reinterpret_cast<unsigned*>(network+0x1f8)>=3) return false;
        const auto* text=at<const char*(__thiscall*)(void*)>(0x60a5d0)(network);
        if(!text || !text[0]) return false;
        unsigned count=0;
        for(unsigned i=0;i<256;++i) {
            const auto c=static_cast<unsigned char>(text[i]);
            if(!c) { out[count]=0;return count!=0; }
            // Native inline colour scopes are renderer instructions, not text.
            if(c=='~' && i+2<256 && text[i+1]>='0' && text[i+1]<='9' && text[i+2]>='0' && text[i+2]<='9') { i+=2;continue; }
            if(count>=255) { out[0]=0;return false; }
            out[count++]=c<32 ? ' ' : char(c);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    out[0]=0;return false; // native queue and GUI keep their ordinary path
}
void draw(void* device) {
    char text[256]{};
    if(!read(text)) return;
    try {
        const auto result=hold_overlay::draw(device,text,false,true);
        if(result.drawn==hold_overlay::path::none && result.step)
            logger::write_once("online:network-notice-draw","online menus: network notice could not be drawn (%s); native queue retained",result.step);
    } catch(...) { /* the native message/timer/stall decisions are untouched */ }
}
}
