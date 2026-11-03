// A37: the engine draws this roster independently of the PDA player list.
// Keep its complete walk, readiness decisions, localized row formatter, hero
// suffix, viewport selection and temporary render flags. Only its font/scale
// arguments and background RGBA are adapted, at roster-local call sites.
// A missing/not-yet-loaded private body atlas leaves every argument native.
namespace ready_roster {
constexpr int body_font=6;
constexpr float body_view_scale=10.0f/64.0f; // LC body: 12.5 on the 640 canvas
constexpr unsigned stream_bytes=0x21634, queue_at=0x2142c, count_at=0x21630, font_at=0x2140c;
constexpr unsigned queue_capacity=(count_at-queue_at)/sizeof(unsigned), colors_at=0x21410, colors_bytes=24;
struct marks { void* stream=nullptr;unsigned count=0;std::array<unsigned,queue_capacity> tokens{}; };
std::array<marks,16> streams{}; // renderer thread; keyed by stream AND full generation-bearing command id
marks* marked(void* stream,bool create=false) {
    for(auto& m:streams) if(m.stream==stream) return &m;
    if(create) for(auto& m:streams) if(!m.stream) { m.stream=stream;return &m; }
    return nullptr;
}
void forget(void* stream) { if(auto* m=marked(stream)) *m={}; }
template<class T> T& field(void* stream,unsigned offset) { return *reinterpret_cast<T*>(static_cast<char*>(stream)+offset); }
struct split_state {
    void* stream;
    std::array<unsigned char,colors_bytes> colors{};
    bool captured=false;
};
thread_local split_state* splitting=nullptr;
const unsigned reset_resume=0x5ef1aa;
__declspec(naked) void __fastcall native_reset(void*,void*) {
    __asm { push ebp
        push edi
        mov edi,ecx
        mov eax,[edi+21404h]
        jmp dword ptr [reset_resume] }
}
void __fastcall reset(void* stream,void*) {
    // Native color scopes can span commands. Capture before the native reset
    // clears their depth, so splitting a batch never changes ~NN/~~ semantics.
    if(splitting && splitting->stream==stream) {
        std::memcpy(splitting->colors.data(),static_cast<char*>(stream)+colors_at,colors_bytes);
        splitting->captured=true;
    }
    forget(stream); // also covers native reset/destruction outside a draw pass
    native_reset(stream,nullptr);
}
using render_fn=void(__thiscall*)(void*,void*,unsigned,void*);
// A separate slow path keeps the large snapshot off every ordinary text-stream
// callback's stack. Validate and partition completely BEFORE consuming tokens.
__declspec(noinline) void render_marked(void* stream,void* context,unsigned time,void* viewport,const marks& selected) {
    const auto render=at<render_fn>(0x5ef2e0);
    const auto count=field<int>(stream,count_at), original_font=field<int>(stream,font_at);
    // A disabled font stream does not consume its native queue. Retain its
    // generation-bound marks until it renders or is reset/destroyed.
    if(original_font<0) { render(stream,context,time,viewport);return; }
    if(count<1 || count>int(queue_capacity)) { forget(stream);render(stream,context,time,viewport);return; }
    std::array<int,queue_capacity> fonts{};
    const auto* tokens=&field<unsigned>(stream,queue_at);
    bool any=false;
    for(int i=0;i<count;++i) {
        const bool ours=std::find(selected.tokens.begin(),selected.tokens.begin()+selected.count,tokens[i])!=selected.tokens.begin()+selected.count;
        fonts[i]=ours ? body_font : original_font;any|=ours;
    }
    forget(stream);
    if(!any) { render(stream,context,time,viewport);return; }
    std::array<unsigned char,stream_bytes> saved{};
    std::memcpy(saved.data(),stream,stream_bytes);
    split_state state{stream};
    struct render_scope {
        void* stream;int original_font;split_state* previous;
        ~render_scope() { field<int>(stream,font_at)=original_font;splitting=previous; }
    } scope{stream,original_font,splitting};
    splitting=&state;
    for(int begin=0;begin<count;) {
        int end=begin+1;while(end<count && fonts[end]==fonts[begin]) ++end;
        std::memcpy(stream,saved.data(),stream_bytes);
        if(state.captured) std::memcpy(static_cast<char*>(stream)+colors_at,state.colors.data(),colors_bytes);
        std::memcpy(static_cast<char*>(stream)+queue_at,saved.data()+queue_at+begin*4,(end-begin)*4);
        field<int>(stream,count_at)=end-begin;field<int>(stream,font_at)=fonts[begin];
        // Use the actual traversal's context/time/viewport. Each disjoint group
        // uses native draw+cleanup once. Remaining global command references
        // keep shared strings alive until the final group is consumed.
        render(stream,context,time,viewport);
        begin=end;
    }
}
void __fastcall render(void* stream,void*,void* context,unsigned time,void* viewport) {
    if(const auto* selected=marked(stream);selected && selected->count) {
        const auto copy=*selected;render_marked(stream,context,time,viewport,copy);return;
    }
    at<render_fn>(0x5ef2e0)(stream,context,time,viewport);
}
bool styled() { return ready && lc_fonts::online_body_ready(); }
float body_scale() {
    const int h=*at<const int*>(0xa0a000);
    return body_view_scale*(h>=240 && h<=16384 ? float(h)/384.0f : 1.0f);
}
using width_fn=float(__thiscall*)(void*,const char*,float,int);
float __fastcall width(void* font,void*,const char* text,float scale,int index) {
    if(styled()) { scale=body_scale();index=body_font; }
    return at<width_fn>((*at<unsigned**>(reinterpret_cast<std::uintptr_t>(font)))[0x34/4])(font,text,scale,index);
}
using height_fn=float(__thiscall*)(void*,int,const char*,float,int,int);
float __fastcall height(void* font,void*,int length,const char* text,float scale,int index,int flags) {
    if(styled()) { scale=body_scale();index=body_font; }
    return at<height_fn>((*at<unsigned**>(reinterpret_cast<std::uintptr_t>(font)))[0x38/4])(font,length,text,scale,index,flags);
}
__declspec(naked) void width_eax() { __asm { mov ecx,eax
    jmp width } }
__declspec(naked) void height_eax() { __asm { mov ecx,eax
    jmp height } }
__declspec(naked) void height_edi() { __asm { mov ecx,edi
    jmp height } }
using color_fn=void*(__thiscall*)(void*,float,float,float,float);
void* __fastcall panel(void* out,void*,float r,float g,float b,float a) {
    if(styled()) { r=17.0f/255;g=40.0f/255;b=74.0f/255;a=.92f; } // LC panel, PALETTE[1]
    return at<color_fn>(*at<unsigned*>(0x67f9ac))(out,r,g,b,a);
}
using draw_fn=void(__thiscall*)(void*,int,int,int,int,int,float,int,const void*,const char*);
void __fastcall text(void* renderer,void*,int viewport,int x,int y,int w,int h,float scale,int flags,const void* color,const char* value) {
    const auto draw=at<draw_fn>(0x5f11b0);
    // This is the exact native font selector used by the text command stream.
    // Initializing the viewport precedes the temporary selector (retail does
    // the same check inside draw); restore it before another UI draws.
    if(!styled() || viewport<0 || viewport>3 || !at<bool(__thiscall*)(void*,int)>(0x5f0fd0)(renderer,viewport)) {
        draw(renderer,viewport,x,y,w,h,scale,flags,color,value);return;
    }
    auto* stream=static_cast<char*>(renderer)+viewport*stream_bytes;
    auto& font=field<int>(stream,font_at);
    const int before=field<int>(stream,count_at);
    if(font<0 || before<0 || before>=int(queue_capacity)) { draw(renderer,viewport,x,y,w,h,scale,flags,color,value);return; }
    if(before==0) forget(stream); // a fresh queue cannot inherit marks from an earlier stream generation
    auto* selected=marked(stream,true);
    if(!selected || selected->count==queue_capacity) { draw(renderer,viewport,x,y,w,h,scale,flags,color,value);return; }
    const auto original=font;font=body_font;
    draw(renderer,viewport,x,y,w,h,body_scale(),flags,color,value);
    font=original;
    const int after=field<int>(stream,count_at);
    if(after>before && after<=int(queue_capacity))
        for(int i=before;i<after && selected->count<queue_capacity;++i) selected->tokens[selected->count++]=field<unsigned>(stream,queue_at+i*4);
}
void contribute(patch_transaction::contribution& c) {
    const auto call=[&](unsigned site,const auto target,unsigned pad,const char* why) {
        c.writes.push_back(patch_transaction::relocation(site,"e8",true,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(target)),pad,why));
    };
    c.writes.push_back(patch_transaction::relocation(0x6a362c,"",false,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&render)),0,"ready roster deferred font batches"));
    c.writes.push_back(patch_transaction::relocation(0x5ef1a0,"e9",true,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&reset)),5,"text stream reset retires roster font marks"));
    call(0x5d941d,&height_eax,0,"ready roster LC line height");
    call(0x5d958b,&width_eax,0,"ready roster LC hero width");
    call(0x5d9644,&width_eax,0,"ready roster LC row width");
    call(0x5d9673,&height_edi,0,"ready roster LC row height");
    for(const auto site:{0x5d9742u,0x5d97d7u,0x5d9c40u,0x5d9d14u})
        call(site,&panel,1,"ready roster LC navy panel");
    for(const auto site:{0x5d9a78u,0x5d9b17u}) call(site,&text,0,"ready roster scoped LC body font");
}
}
