// A37 presentation adapters exercised through their installed retail call sites.
// Invented rows, font manager, viewport, and RGBA service; no readiness writes.
namespace ready_fixture {
unsigned target(unsigned site) {
    assert(*reinterpret_cast<const unsigned char*>(site)==0xe8);
    return site+5+*reinterpret_cast<const int*>(site+1);
}
int measured_font=-1,drawn_font=-1,draws=0;
float measured_scale=0,drawn_scale=0;
std::string drawn_text;
bool viewport_ok=true;
std::array<float,4> rgba{};
float __fastcall width(void*,void*,const char* value,float scale,int index) {
    assert(std::string_view(value)=="Test player (Test hero): Not ready");
    measured_font=index;measured_scale=scale;return 123.25f;
}
float __fastcall height(void*,void*,int length,const char* value,float scale,int index,int flags) {
    assert(length==33 && std::string_view(value)=="Test player (Test hero): Not ready" && flags==9);
    measured_font=index;measured_scale=scale;return 17.25f;
}
void* __fastcall color(void* out,void*,float r,float g,float b,float a) {
    rgba={r,g,b,a};return out;
}
bool __fastcall viewport(void*,void*,int index) { assert(index==2);return viewport_ok; }
void __fastcall draw(void* renderer,void*,int index,int x,int y,int w,int h,float scale,int flags,const void* shade,const char* value) {
    assert(index==2 && x==11 && y==22 && w==333 && h==444 && flags==18 && shade==rgba.data());
    drawn_font=*reinterpret_cast<int*>(static_cast<char*>(renderer)+index*0x21634+0x2140c);
    drawn_scale=scale;drawn_text=value;++draws;
}
__declspec(naked) float __cdecl invoke_width(unsigned,void*,const char*,float,int) {
    __asm {
        mov edx,[esp+4]
        mov eax,[esp+8]
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        call edx
        ret
    }
}
__declspec(naked) float __cdecl invoke_height(unsigned,void*,const char*,float,int,bool) {
    __asm {
        push edi
        mov edx,[esp+8]
        mov eax,[esp+12]
        mov edi,eax
        push 9
        push dword ptr [esp+28]
        push dword ptr [esp+28]
        push dword ptr [esp+28]
        push 33
        call edx
        pop edi
        ret
    }
}
void run() {
    std::array<unsigned,16> methods{};
    methods[0x34/4]=unsigned(reinterpret_cast<std::uintptr_t>(&width));
    methods[0x38/4]=unsigned(reinterpret_cast<std::uintptr_t>(&height));
    auto* font=methods.data();
    constexpr char value[]="Test player (Test hero): Not ready";
    *reinterpret_cast<int*>(0xa0a000)=768;
    for(bool available:{false,true}) {
        lc_fonts::body_ready=available;
        for(const auto site:{0x5d958bu,0x5d9644u}) {
            assert(invoke_width(target(site),&font,value,1.f,0)==123.25f);
            assert(measured_font==(available?6:0) && measured_scale==(available?.3125f:1.f));
        }
        for(const auto site:{0x5d941du,0x5d9673u}) {
            assert(invoke_height(target(site),&font,value,1.f,0,true)==17.25f);
            assert(measured_font==(available?6:0) && measured_scale==(available?.3125f:1.f));
        }
        *reinterpret_cast<unsigned*>(0x67f9ac)=unsigned(reinterpret_cast<std::uintptr_t>(&color));
        for(const auto site:{0x5d9742u,0x5d97d7u,0x5d9c40u,0x5d9d14u}) {
            using fn=void*(__fastcall*)(void*,void*,float,float,float,float);
            assert(reinterpret_cast<fn>(target(site))(&font,nullptr,0,0,0,.5f)==&font);
            assert(rgba[0]==(available?17.f/255:0) && rgba[1]==(available?40.f/255:0) &&
                   rgba[2]==(available?74.f/255:0) && rgba[3]==(available?.92f:.5f));
        }
        redirect(0x5f0fd0,&viewport);redirect(0x5f11b0,&draw);
        std::vector<unsigned char> renderer(0x90000,0);
        auto& selected=*reinterpret_cast<int*>(renderer.data()+2*0x21634+0x2140c);selected=1;
        const auto before=renderer;
        for(const auto site:{0x5d9a78u,0x5d9b17u}) {
            using fn=void(__fastcall*)(void*,void*,int,int,int,int,int,float,int,const void*,const char*);
            const auto text=reinterpret_cast<fn>(target(site));
            // Status changes, a held hero, and the slow suffix pass through
            // byte-for-byte; drawing never sets ready/paused/member flags.
            for(const auto* label:{value,"Test player (Test hero): Ready","Test player (Spectating): Slow"}) {
                text(renderer.data(),nullptr,2,11,22,333,444,1.f,18,rgba.data(),label);
                assert(drawn_text==label && drawn_font==(available?6:1) && drawn_scale==(available?.3125f:1.f));
                assert(renderer==before);
            }
            viewport_ok=false;
            text(renderer.data(),nullptr,2,11,22,333,444,1.f,18,rgba.data(),value);
            assert(drawn_font==1 && drawn_scale==1 && renderer==before);
            viewport_ok=true;
        }
    }
    lc_fonts::body_ready=true;
    std::cout<<"PASS ready roster: 4 measure sites, 4 panel sites, 2 text sites; native/LC arguments, Ready/Not ready/Spectating/Slow text, viewport fallback, font restoration\n";
}

// Native text commands retain scale and string, but font is read at the later
// render traversal. This deferred fixture catches an early selector restore.
struct command { std::string text;float scale; };
std::map<unsigned,command> queued;
std::vector<std::tuple<std::string,int,float,int>> rendered;
std::map<unsigned,unsigned> freed_tokens;
unsigned next_token=0x01000000,passes=0;
constexpr unsigned queue_at=0x2142c,count_at=0x21630,font_at=0x2140c;
template<class T> T& f(void* p,unsigned offset) { return *reinterpret_cast<T*>(static_cast<char*>(p)+offset); }
void __fastcall enqueue(void* renderer,void*,int index,int,int,int,int,float scale,int,const void*,const char* value) {
    auto* stream=static_cast<char*>(renderer)+index*0x21634;
    auto& count=f<int>(stream,count_at);assert(count>=0 && count<int((count_at-queue_at)/4));
    const auto token=++next_token;queued.emplace(token,command{value,scale});
    f<unsigned>(stream,queue_at+count++*4)=token;
}
void __fastcall cleanup(void* stream,void*) {
    auto& count=f<int>(stream,count_at);
    for(int i=0;i<count;++i) {
        const auto token=f<unsigned>(stream,queue_at+i*4);
        assert(queued.erase(token)==1);assert(++freed_tokens[token]==1);
    }
    count=0;f<unsigned>(stream,0x21408)=f<unsigned>(stream,0x21404);
    f<unsigned>(stream,0x21404)=0;f<unsigned>(stream,0x21424)=0;
}
__declspec(naked) void cleanup_tail() { __asm {
    call cleanup
    pop edi
    pop ebp
    ret
} }
void __fastcall render_later(void* stream,void*,void* context,unsigned time,void* viewport_object) {
    assert(context==reinterpret_cast<void*>(0x11223344) && time==0x3f800000 && viewport_object==reinterpret_cast<void*>(0x55667788));
    ++passes;
    const auto font=f<int>(stream,font_at);
    if(font<0) return;
    auto& depth=f<unsigned>(stream,0x21424);
    for(int i=0;i<f<int>(stream,count_at);++i) {
        const auto& c=queued.at(f<unsigned>(stream,queue_at+i*4));
        if(c.text=="PUSH") { assert(depth<5);f<int>(stream,0x21410+depth++*4)=5; }
        if(c.text=="POP") { assert(depth);--depth; }
        const auto color=depth ? f<int>(stream,0x21410+(depth-1)*4) : 0;
        rendered.emplace_back(c.text,font,c.scale,color);
    }
    // Actual installed reset adapter captures cross-command colors before
    // native cleanup. The native tail is replaced by our token ledger only.
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5ef1a0)(stream,nullptr);
}
void deferred() {
    lc_fonts::body_ready=true;viewport_ok=true;
    redirect(0x5f0fd0,&viewport);redirect(0x5f11b0,&enqueue);
    redirect(0x5ef2e0,&render_later);redirect(0x5ef1aa,&cleanup_tail);
    std::vector<unsigned char> renderer(0x90000,0);
    auto* stream=renderer.data()+2*0x21634;f<int>(stream,font_at)=1;f<unsigned>(stream,0x21404)=123;
    using text_fn=void(__fastcall*)(void*,void*,int,int,int,int,int,float,int,const void*,const char*);
    using render_fn=void(__fastcall*)(void*,void*,void*,unsigned,void*);
    auto flush=[&] {
        reinterpret_cast<render_fn>(*reinterpret_cast<unsigned*>(0x6a362c))(stream,nullptr,
            reinterpret_cast<void*>(0x11223344),0x3f800000,reinterpret_cast<void*>(0x55667788));
    };
    // Negative control: retail's deferred callback alone sees the restored
    // native font, even though this command was measured/enqueued with LC.
    reinterpret_cast<text_fn>(target(0x5d9a78))(renderer.data(),nullptr,2,11,22,333,444,1,18,rgba.data(),"Early restore baseline");
    render_later(stream,nullptr,reinterpret_cast<void*>(0x11223344),0x3f800000,reinterpret_cast<void*>(0x55667788));
    assert(std::get<1>(rendered.back())==1 && std::get<2>(rendered.back())==.3125f);
    rendered.clear();freed_tokens.clear();passes=0;f<unsigned>(stream,0x21404)=123;
    enqueue(renderer.data(),nullptr,2,11,22,333,444,1,18,rgba.data(),"PUSH");
    for(const auto site:{0x5d9a78u,0x5d9b17u})
        reinterpret_cast<text_fn>(target(site))(renderer.data(),nullptr,2,11,22,333,444,1,18,rgba.data(),"Peer: Not ready");
    enqueue(renderer.data(),nullptr,2,11,22,333,444,1,18,rgba.data(),"POP");
    assert(rendered.empty() && f<int>(stream,font_at)==1 && queued.size()==4);
    flush();
    assert(rendered.size()==4 && passes==3 && queued.empty() && freed_tokens.size()==4);
    assert(rendered[0]==std::make_tuple(std::string("PUSH"),1,1.f,5));
    assert(rendered[1]==std::make_tuple(std::string("Peer: Not ready"),6,.3125f,5) && rendered[2]==rendered[1]);
    assert(rendered[3]==std::make_tuple(std::string("POP"),1,1.f,0));
    assert(f<int>(stream,font_at)==1 && f<int>(stream,count_at)==0 && f<unsigned>(stream,0x21408)==123 && f<unsigned>(stream,0x21424)==0);
    // The next native command retains its font, with no stale marks or replay.
    enqueue(renderer.data(),nullptr,2,0,0,0,0,1,0,nullptr,"Native after roster");flush();
    assert(std::get<1>(rendered.back())==1 && queued.empty() && passes==4);
    // A reused pool slot with another generation must not inherit the LC mark.
    reinterpret_cast<text_fn>(target(0x5d9a78))(renderer.data(),nullptr,2,11,22,333,444,1,18,rgba.data(),"Old generation");
    const auto old=f<unsigned>(stream,queue_at),replacement=old+0x01000000;
    queued.erase(old);queued.emplace(replacement,command{"New generation",1});f<unsigned>(stream,queue_at)=replacement;
    flush();assert(std::get<1>(rendered.back())==1 && queued.empty());
    // Native negative-font passes defer, rather than consume, their queue.
    reinterpret_cast<text_fn>(target(0x5d9a78))(renderer.data(),nullptr,2,11,22,333,444,1,18,rgba.data(),"Deferred while disabled");
    const auto before_disabled=rendered.size();f<int>(stream,font_at)=-1;flush();
    assert(rendered.size()==before_disabled && queued.size()==1);
    f<int>(stream,font_at)=1;flush();
    assert(std::get<1>(rendered.back())==6 && queued.empty() && f<int>(stream,font_at)==1);
    // The native inline list has 129 entries before its count word. Preserve
    // a roster row in the final valid slot, rather than reverting its font.
    for(unsigned i=0;i<128;++i) enqueue(renderer.data(),nullptr,2,0,0,0,0,1,0,nullptr,"Native queue filler");
    reinterpret_cast<text_fn>(target(0x5d9a78))(renderer.data(),nullptr,2,11,22,333,444,1,18,rgba.data(),"Roster at queue boundary");
    assert(f<unsigned>(stream,count_at)==129);flush();
    assert(std::get<1>(rendered.back())==6 && queued.empty() && f<int>(stream,font_at)==1);
    std::cout<<"PASS ready roster deferred render: real pass arguments, native/LC/native batches, font restoration, cross-command colors, generation-safe marks, one cleanup per token\n";
}
}
