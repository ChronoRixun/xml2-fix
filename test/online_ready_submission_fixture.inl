// Execute the retail procedural-geometry visitor, font renderer and vertex
// writer. Only assets, allocation and the final graphics sink are synthetic.
// In particular, the sink runs AFTER the text callback has returned.
namespace ready_submission_fixture {
template<class T> T& f(void* p,unsigned offset) { return *reinterpret_cast<T*>(static_cast<char*>(p)+offset); }
template<class T> unsigned address(T p) { return unsigned(reinterpret_cast<std::uintptr_t>(p)); }
struct glyph { short width,height,advance,offset;int baseline;float s,t,s2,t2; };
static_assert(sizeof(glyph)==28);
std::array<std::array<glyph,256>,8> metrics{};
std::array<unsigned,32> font_vt{},texture_vt{},material_vt{},node_vt{},binding_vt{},root_vt{},writer_vt{},display_vt{},clock_vt{};
unsigned* font=font_vt.data();unsigned* display=display_vt.data();unsigned* clock=clock_vt.data();unsigned* root_object=root_vt.data();
struct texture { unsigned* vt=texture_vt.data();int font=0; };
std::array<texture,8> textures{};
struct material { unsigned* vt=material_vt.data();int font=0;unsigned pad[6]{};void* binding=nullptr; };
struct node {
    unsigned* vt=node_vt.data();float depth=0;material surface;unsigned* binding=binding_vt.data();void* stream=nullptr;
    std::array<unsigned char,0x30> geometry{};
    std::array<unsigned char,0x14> callbacks{};
    std::vector<std::tuple<unsigned,unsigned,int>> batches;
    std::vector<unsigned> expected_colors;
};
std::map<std::pair<void*,int>,std::unique_ptr<node>> nodes;
std::vector<node*> scene;
node* visiting=nullptr;
std::array<unsigned char,0x90> writer{};
std::array<std::array<float,3>,4096> positions{};
std::array<std::array<float,2>,4096> uv{};
std::array<unsigned,4096> colors{};
std::map<unsigned,std::string> strings;
std::map<unsigned,unsigned> retired;
unsigned token=0,submitted=0,body_glyphs=0,native_glyphs=0,released=0;int measured_font=-1,device_font=-1;
void* __cdecl fonts() { return &font; }
void* __cdecl screen() { return &display; }
void* __cdecl timer() { return &clock; }
float __fastcall one(void*,void*) { return 1; }
float __fastcall zero(void*,void*) { return 0; }
float __fastcall time(void*,void*,int) { return 0; }
const float* __fastcall palette(void*,void*,int index) {
    static const float values[3][4]{{1,1,1,1},{1,0,0,1},{0,1,0,1}};assert(index>=0 && index<3);return values[index];
}
void __cdecl unpack(float* out,unsigned value) { for(int i=0;i<4;++i) out[i]=float((value>>(i*8))&255)/255; }
int __fastcall dimension(void*,void*) { return 1024; }
int __fastcall line_height(void*,void*,int) { return 64; }
void* __fastcall metric_get(void*,void*,int index) { measured_font=index;return metrics.at(index).data(); }
void** __fastcall texture_get(void*,void*,void** out,int index) { *out=&textures.at(index);return out; }
void* __fastcall material_get(node* n,void*) { return &n->surface; }
void __fastcall texture_set(material* m,void*,texture* t) { m->font=t->font; }
void __fastcall apply(void*,void*,void*) { device_font=visiting->surface.font; }
void __fastcall copy3(void* to,void*,const void* from) { std::memcpy(to,from,12); }
void __fastcall copy2(void* to,void*,const void* from) { std::memcpy(to,from,8); }
double __cdecl floor_value(double value) { return std::floor(value); }
__declspec(naked) void integer() {
    __asm sub esp,8
    __asm fistp qword ptr [esp]
    __asm pop eax
    __asm pop edx
    __asm ret
}
__declspec(naked) void cookie() { __asm ret }
bool __fastcall prepare(void* renderer,void*,int index) {
    auto& n=nodes[{renderer,index}];
    if(!n) {
        n=std::make_unique<node>();n->stream=static_cast<char*>(renderer)+index*0x21634;
        n->surface.font=index;n->surface.binding=&n->binding;
        f<int>(n->stream,0x2140c)=index;
        f<void*>(n->geometry.data(),0x2c)=n->callbacks.data();
        f<int>(n->callbacks.data(),8)=1;f<void*>(n->callbacks.data(),0x10)=&n->stream;
        f<void*>(renderer,0x858d4+index*4)=n.get();
    }
    return true;
}
void __fastcall enqueue(void* renderer,void*,int index,int x,int y,int w,int h,float scale,int flags,const void*,const char* value) {
    prepare(renderer,nullptr,index);
    assert(f<unsigned char>(renderer,0x858e8)==1);
    auto* stream=static_cast<char*>(renderer)+index*0x21634;
    const unsigned id=++token;assert(id<256);strings[id]=value;
    auto* command=reinterpret_cast<void*>(0x8b6938+id*32);
    f<short>(command,0)=short(x);f<short>(command,2)=short(y);f<short>(command,4)=short(w);f<short>(command,6)=short(h);
    f<float>(command,8)=scale;f<int>(command,12)=flags;f<float>(command,16)=0;
    f<const char*>(command,20)=strings.at(id).c_str();f<short>(command,24)=short(strings.at(id).size());f<unsigned>(command,28)=0xff84a8cc;
    auto& count=f<int>(stream,0x21630);assert(count<129);f<unsigned>(stream,0x2142c+count++*4)=id;
}
void __fastcall cleanup(void* stream,void*) {
    for(int i=0;i<f<int>(stream,0x21630);++i) assert(++retired[f<unsigned>(stream,0x2142c+i*4)]==1);
    f<int>(stream,0x21630)=0;f<unsigned>(stream,0x21408)=f<unsigned>(stream,0x21404);
    f<unsigned>(stream,0x21404)=0;f<unsigned>(stream,0x21424)=0;
}
__declspec(naked) void cleanup_tail() {
    __asm call cleanup
    __asm pop edi
    __asm pop ebp
    __asm ret
}
void __fastcall finish(void* context,void*) {
    assert(context==writer.data());
    auto& start=f<unsigned>(context,0x14);auto& count=f<unsigned>(context,0x20);
    if(count) visiting->batches.emplace_back(start,count,measured_font);
    start+=count;count=0;f<unsigned>(context,0x18)=2;f<unsigned char>(context,0x1c)=0;
}
void __fastcall transform(void* renderer,void*,float depth) {
    for(auto& [key,n]:nodes) if(key.first==renderer) n->depth=depth;
}
void __fastcall add(void*,void*,node* n) { if(std::find(scene.begin(),scene.end(),n)==scene.end()) scene.push_back(n); }
void __fastcall remove(void*,void*,node* n) { scene.erase(std::remove(scene.begin(),scene.end(),n),scene.end()); }
void __stdcall vector_destroy(void* first,unsigned stride,int count,void(__thiscall* destroy)(void*)) {
    for(int i=count-1;i>=0;--i) destroy(static_cast<char*>(first)+i*stride);
}
void __cdecl release(void** slot) { ++released;*slot=nullptr; }
bool __fastcall is_type(void*,void*,void*) { return true; }
void __fastcall clear_geometry(void*,void*,int count) {
    assert(count==0);visiting->batches.clear();visiting->expected_colors.clear();
    for(int i=0;i<f<int>(visiting->stream,0x21630);++i) {
        const auto& text=strings.at(f<unsigned>(visiting->stream,0x2142c+i*4));
        if(text=="A") visiting->expected_colors.push_back(0xff84a8cc);
        else {
            assert(text=="~00~01A~~A~02A~~~~");
            for(unsigned color:{0xff0000ffu,0xffffffffu,0xff00ff00u}) visiting->expected_colors.push_back(color);
        }
    }
}
void* __fastcall geometry_attrs(void*,void*) { return &visiting->batches; }
__int64 __fastcall traversal_time(void*,void*) { return 0; }
int __cdecl submit(void*,void* geometry) {
    assert(geometry==visiting->geometry.data());
    // This is where igCommonTraverseGeometry captures the scene state for the
    // later graphics traversal. A temporary device bind during emission cannot
    // replace the geometry's material at this point.
    device_font=visiting->surface.font;
    for(const auto& [start,count,index]:visiting->batches) {
        std::cout<<"submit font="<<index<<" atlas="<<device_font<<" vertices="<<count<<" uv="<<uv[start][0]<<','<<uv[start][1]<<'\n';
        assert(device_font==index); // fails on the b2d font/atlas adapter
        assert(count%6==0);
        const auto& g=metrics[index]['A'];
        for(unsigned i=start;i<start+count;++i) {
            assert(uv[i][0]==g.s || uv[i][0]==g.s2);
            assert(uv[i][1]==1-1.f/1024-g.t || uv[i][1]==1-1.f/1024-g.t2);
            assert(std::isfinite(positions[i][0]) && std::isfinite(positions[i][2]) && positions[i][1]==0);
            assert(colors[i]==visiting->expected_colors.at(i/6));
        }
        (index==6 ? body_glyphs : native_glyphs)+=count/6;
        ++submitted;
    }
    return 0;
}
void run() {
    font_vt[8/4]=address(&metric_get);font_vt[0x18/4]=address(&line_height);font_vt[0x1c/4]=address(&texture_get);
    texture_vt[4/4]=texture_vt[8/4]=address(&dimension);
    material_vt[0x1c/4]=address(&texture_set);node_vt[0xc/4]=address(&material_get);binding_vt[0x54/4]=address(&apply);
    root_vt[0x2c/4]=address(&add);root_vt[0x30/4]=address(&remove);
    display_vt[0x5c/4]=display_vt[0x60/4]=address(&one);display_vt[0x64/4]=display_vt[0x68/4]=address(&zero);clock_vt[0x28/4]=address(&time);
    std::memcpy(writer_vt.data(),reinterpret_cast<void*>(0x69c904),7*4);writer_vt[0x14/4]=address(&finish);
    for(int i=0;i<8;++i) { textures[i].font=i;for(auto& g:metrics[i]) g={20,40,22,0,38,(64.f+i*64)/1024,128.f/1024,(84.f+i*64)/1024,168.f/1024}; }
    redirect(0x4705e0,&unpack);*reinterpret_cast<unsigned*>(0x6a236c+0x224)=address(&palette);
    redirect(0x597c20,&fonts);redirect(0x5f6e50,&screen);redirect(0x55b610,&timer);
    redirect(0x67217c,&integer);redirect(0x672161,&cookie);redirect(0x6721f8,&floor_value);
    redirect(0x5f0fd0,&prepare);redirect(0x5f11b0,&enqueue);redirect(0x5f1090,&transform);redirect(0x575980,&release);
    redirect(0x5ef1aa,&cleanup_tail);redirect(0x672274,&vector_destroy);
    *reinterpret_cast<unsigned*>(0x67fa2c)=address(&copy3);*reinterpret_cast<unsigned*>(0x67f970)=address(&copy2);
    *reinterpret_cast<unsigned*>(0x67f6b8)=address(&is_type);*reinterpret_cast<unsigned*>(0x67fcb0)=address(&clear_geometry);
    *reinterpret_cast<unsigned*>(0x67fbc0)=address(&geometry_attrs);*reinterpret_cast<unsigned*>(0x67fbc4)=address(&traversal_time);
    *reinterpret_cast<unsigned*>(0x67fbc8)=address(&submit);*reinterpret_cast<unsigned*>(0x8b9170)=255;
    static unsigned meta=0;*reinterpret_cast<void**>(0x67fbbc)=&meta;
    *reinterpret_cast<void**>(0xa37c4c)=writer.data();
    using text_fn=void(__fastcall*)(void*,void*,int,int,int,int,int,float,int,const void*,const char*);
    using scene_fn=void(__fastcall*)(void*,void*,void**,float);
    for(const auto size:{std::pair{800,600},std::pair{1280,960},std::pair{1280,720},std::pair{1920,1080}}) for(bool present_first:{false,true}) for(bool available:{false,true}) {
        lc_fonts::body_ready=available;
        body_glyphs=native_glyphs=0;
        *reinterpret_cast<int*>(0xa0a000)=size.second;
        std::vector<unsigned char> renderer(0x858ec);
        for(unsigned i=0;i<4;++i) reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5eec20)(renderer.data()+i*0x21634,nullptr);
        f<unsigned char>(renderer.data(),0x858e8)=1;
        void* parent=&root_object;scene.clear();
        if(present_first) reinterpret_cast<scene_fn>(0x5f1230)(renderer.data(),nullptr,&parent,1);
        enqueue(renderer.data(),nullptr,0,10,80,100,50,1,0,nullptr,"A");
        for(const auto site:{0x5d9a78u,0x5d9b17u}) for(int row=0;row<4;++row)
            reinterpret_cast<text_fn>(ready_fixture::target(site))(renderer.data(),nullptr,0,size.first-180,180+row*40,160,30,1,18,nullptr,"~00~01A~~A~02A~~~~");
        enqueue(renderer.data(),nullptr,0,10,120,100,50,1,0,nullptr,"A");
        if(!present_first) reinterpret_cast<scene_fn>(0x5f1230)(renderer.data(),nullptr,&parent,1);
        assert(!scene.empty());
        const auto visit_scene=[&] {
        unsigned glyphs=0;
        for(auto* n:scene) {
            assert(n->depth==1);
            visiting=n;writer.fill(0);f<void*>(writer.data(),0)=writer_vt.data();f<unsigned>(writer.data(),0xc)=4096;
            f<unsigned>(writer.data(),0x24)=1;f<void*>(writer.data(),0x44)=positions.data();f<unsigned>(writer.data(),0x48)=12;
            f<void*>(writer.data(),0x60)=colors.data();f<unsigned>(writer.data(),0x64)=4;f<void*>(writer.data(),0x7c)=uv.data();f<unsigned>(writer.data(),0x80)=8;
            std::array<unsigned char,0x100> traversal{};f<float>(traversal.data(),0x64+0x30)=float(size.first);f<float>(traversal.data(),0x64+0x34)=float(size.second);
            const auto before=traversal;
            reinterpret_cast<int(__cdecl*)(void*,void*)>(0x6024f0)(traversal.data(),n->geometry.data());
            assert(traversal==before); // no viewport/clip mutation
            glyphs+=f<unsigned>(writer.data(),0x14)/6;
        }
        return glyphs;
        };
        assert(visit_scene()==26 && body_glyphs==(available?24u:0u) && native_glyphs==(available?2u:26u) && retired.size()==10);
        // A discarded frame must retire both native and private commands once.
        reinterpret_cast<text_fn>(ready_fixture::target(0x5d9a78))(renderer.data(),nullptr,0,10,80,100,50,1,0,nullptr,"A");
        enqueue(renderer.data(),nullptr,0,10,120,100,50,1,0,nullptr,"A");
        reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5f0f60)(renderer.data(),nullptr);
        assert(visit_scene()==0 && retired.size()==12);
        void* absent_parent=nullptr;
        reinterpret_cast<scene_fn>(0x5f1230)(renderer.data(),nullptr,&absent_parent,1);
        reinterpret_cast<void(__fastcall*)(void*,void*,void**)>(0x5f0f90)(renderer.data(),nullptr,&parent);
        assert(scene.empty());nodes.clear();strings.clear();retired.clear();token=0;
        std::cout<<"PASS retail roster submission "<<size.first<<'x'<<size.second<<": 26 glyphs, correct texture/UV/nested colors, viewport unchanged\n";
    }
    lc_fonts::body_ready=true;
    // Destruction with pending text, both before and after parent attachment.
    for(bool attached:{false,true}) {
        std::vector<unsigned char> renderer(0x858ec);
        for(unsigned i=0;i<4;++i) reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5eec20)(renderer.data()+i*0x21634,nullptr);
        f<unsigned char>(renderer.data(),0x858e8)=1;void* parent=&root_object;scene.clear();
        if(attached) reinterpret_cast<scene_fn>(0x5f1230)(renderer.data(),nullptr,&parent,1);
        reinterpret_cast<text_fn>(ready_fixture::target(0x5d9a78))(renderer.data(),nullptr,0,10,80,100,50,1,0,nullptr,"A");
        const auto before=released;
        reinterpret_cast<void(__fastcall*)(void*,void*)>(0x59b520)(renderer.data(),nullptr);
        assert(released==before+1 && retired.size()==1 && scene.size()==(attached?4u:0u));
        reinterpret_cast<void(__fastcall*)(void*,void*,void**)>(0x5f0f90)(renderer.data(),nullptr,&parent);
        assert(scene.empty());nodes.clear();strings.clear();retired.clear();token=0;
    }
    assert(submitted>0);
    std::cout<<"PASS parent-before/after-enqueue, missing private font, source reset, pending destruction, handle release and one retirement per command\n";
}
}
