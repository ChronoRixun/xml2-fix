// The PDA roster uses procedural text geometry, not CText's private material.
// Its callback writes vertices; igCommonTraverseGeometry submits them later.
// Give LC rows a separate native stream and scene node whose atlas stays fixed
// through both traversals. Native streams, their colors and tokens stay native.
namespace ready_roster {
constexpr int body_font=6;
constexpr float body_view_scale=10.0f/64.0f;
constexpr unsigned stream_bytes=0x21634,renderer_bytes=0x858ec;
constexpr unsigned font_at=0x2140c,nodes_at=0x858d4,flags_at=0x858e8;
template<class T> T& field(void* object,unsigned offset) { return *reinterpret_cast<T*>(static_cast<char*>(object)+offset); }
struct private_scene {
    void* source=nullptr;
    void* parent=nullptr;
    float depth=0;
    std::unique_ptr<unsigned char[]> renderer;
};
std::array<private_scene,16> scenes{};
private_scene* scene(void* source,bool create=false) {
    for(auto& s:scenes) if(s.source==source) return &s;
    if(create) for(auto& s:scenes) if(!s.source) { s.source=source;return &s; }
    return nullptr;
}
const unsigned present_resume=0x5f1235,detach_resume=0x5f0f95,reset_resume=0x5f0f69,destroy_resume=0x59b525;
__declspec(naked) void __fastcall native_present(void*,void*,void**,float) {
    __asm push ebx
    __asm mov ebx,[esp+8]
    __asm jmp dword ptr [present_resume]
}
__declspec(naked) void __fastcall native_detach(void*,void*,void**) {
    __asm push ebx
    __asm mov ebx,[esp+8]
    __asm jmp dword ptr [detach_resume]
}
__declspec(naked) void __fastcall native_reset(void*,void*) {
    __asm push esi
    __asm push edi
    __asm mov esi,ecx
    __asm mov edi,4
    __asm jmp dword ptr [reset_resume]
}
__declspec(naked) void __fastcall native_destroy(void*,void*) {
    __asm push 5ef890h
    __asm jmp dword ptr [destroy_resume]
}
void attach(private_scene& s) {
    if(!s.parent || !s.renderer) return;
    auto* node=field<void*>(s.renderer.get(),nodes_at);
    if(!node) return;
    at<void(__thiscall*)(void*,float)>(0x5f1090)(s.renderer.get(),s.depth);
    at<void(__thiscall*)(void*,void*)>((*static_cast<unsigned**>(s.parent))[0x2c/4])(s.parent,node);
}
void retire(private_scene& s) {
    if(s.renderer) {
        // Detach/release the scene handle before releasing its callback storage.
        auto* parent=s.parent;
        native_detach(s.renderer.get(),nullptr,&parent);
        if(!parent) for(unsigned i=0;i<4;++i) {
            auto& node=field<void*>(s.renderer.get(),nodes_at+i*4);
            if(node) at<void(__cdecl*)(void**)>(0x575980)(&node);
        }
        native_destroy(s.renderer.get(),nullptr);
    }
    s={};
}
void __fastcall present(void* source,void*,void** parent,float depth) {
    native_present(source,nullptr,parent,depth);
    // Native presentation does nothing for an empty parent handle. Preserve
    // the previous attachment so later teardown can still remove its child.
    if(!parent || !*parent) return;
    if(auto* s=scene(source,true)) {
        s->parent=parent ? *parent : nullptr;s->depth=depth;
        attach(*s);
    }
}
void __fastcall detach(void* source,void*,void** parent) {
    if(auto* s=scene(source)) retire(*s);
    native_detach(source,nullptr,parent);
}
void __fastcall reset(void* source,void*) {
    if(auto* s=scene(source);s && s->renderer) native_reset(s->renderer.get(),nullptr);
    native_reset(source,nullptr);
}
void __fastcall destroy(void* source,void*) {
    if(auto* s=scene(source)) retire(*s);
    native_destroy(source,nullptr);
}
void* private_renderer(void* source) {
    auto* s=scene(source,true);if(!s) return nullptr;
    if(!s->renderer) {
        auto memory=std::unique_ptr<unsigned char[]>(new(std::nothrow) unsigned char[renderer_bytes]{});
        if(!memory) return nullptr;
        for(unsigned i=0;i<4;++i) at<void*(__thiscall*)(void*)>(0x5eec20)(memory.get()+i*stream_bytes);
        s->renderer=std::move(memory);
    }
    auto* renderer=s->renderer.get();
    if(!field<void*>(renderer,nodes_at)) {
        at<bool(__thiscall*)(void*,int)>(0x5f0fd0)(renderer,0);
        auto* node=field<void*>(renderer,nodes_at);if(!node) return nullptr;
        auto* material=at<void*(__thiscall*)(void*)>((*static_cast<unsigned**>(node))[0xc/4])(node);
        auto* fonts=at<getter>(0x597c20)();void* texture=nullptr;
        auto* result=at<void**(__thiscall*)(void*,void**,int)>((*static_cast<unsigned**>(fonts))[0x1c/4])(fonts,&texture,body_font);
        at<void(__thiscall*)(void*,void*)>((*static_cast<unsigned**>(material))[0x1c/4])(material,*result);
        field<int>(renderer,font_at)=body_font;
        attach(*s); // covers a roster first drawn AFTER native scene attachment
    }
    return renderer;
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
__declspec(naked) void width_eax() {
    __asm mov ecx,eax
    __asm jmp width
}
__declspec(naked) void height_eax() {
    __asm mov ecx,eax
    __asm jmp height
}
__declspec(naked) void height_edi() {
    __asm mov ecx,edi
    __asm jmp height
}
using color_fn=void*(__thiscall*)(void*,float,float,float,float);
void* __fastcall panel(void* out,void*,float r,float g,float b,float a) {
    if(styled()) { r=17.0f/255;g=40.0f/255;b=74.0f/255;a=.92f; }
    return at<color_fn>(*at<unsigned*>(0x67f9ac))(out,r,g,b,a);
}
using draw_fn=void(__thiscall*)(void*,int,int,int,int,int,float,int,const void*,const char*);
void __fastcall text(void* source,void*,int channel,int x,int y,int w,int h,float scale,int flags,const void* color,const char* value) {
    const auto draw=at<draw_fn>(0x5f11b0);
    if(styled() && channel>=0 && channel<4) if(auto* renderer=private_renderer(source)) {
        field<unsigned char>(renderer,flags_at)=field<unsigned char>(source,flags_at);
        draw(renderer,0,x,y,w,h,body_scale(),flags,color,value);return;
    }
    draw(source,channel,x,y,w,h,scale,flags,color,value);
}
void contribute(patch_transaction::contribution& c) {
    const auto call=[&](unsigned site,const auto target,unsigned pad,const char* why) {
        c.writes.push_back(patch_transaction::relocation(site,"e8",true,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(target)),pad,why));
    };
    const auto hook=[&](unsigned site,const auto target,unsigned pad,const char* why) {
        c.writes.push_back(patch_transaction::relocation(site,"e9",true,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(target)),pad,why));
    };
    hook(0x5f1230,&present,0,"ready roster private scene follows native attachment");
    hook(0x5f0f90,&detach,0,"ready roster private scene follows native detachment");
    hook(0x5f0f60,&reset,4,"ready roster private queue follows source reset");
    hook(0x59b520,&destroy,0,"ready roster private storage follows source destruction");
    call(0x5d941d,&height_eax,0,"ready roster LC line height");
    call(0x5d958b,&width_eax,0,"ready roster LC hero width");
    call(0x5d9644,&width_eax,0,"ready roster LC row width");
    call(0x5d9673,&height_edi,0,"ready roster LC row height");
    for(const auto site:{0x5d9742u,0x5d97d7u,0x5d9c40u,0x5d9d14u}) call(site,&panel,1,"ready roster LC navy panel");
    for(const auto site:{0x5d9a78u,0x5d9b17u}) call(site,&text,0,"ready roster isolated LC body scene");
}
}
