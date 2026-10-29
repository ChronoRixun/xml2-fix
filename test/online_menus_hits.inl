// 0.6 preview fixes E and E2: the co-op screens' mouse hit boxes are their drawn focus bars (online_menus_hits_rules.hpp).
// Drives the INSTALLED hooks (base menu mouse slot 0x69e49c, the host lobby's 0x69f2f4) and the RETAIL mouse code of the mapped
// XMen2.exe image: the menu walk 0x5ae0a0 itself (first pass over desctext1..5 with the hover index and hover sound, the
// in-order walk of the menu's name-keyed item tree through 0x585be0 / 0x585c10 / 0x5ab7d0, set focus), the host lobby's
// 0x5ba2b0, and every item's own hit test (vt+0x74 = 0x5c6490 -> 0x5bc1b0, PtInRect / strstr / strncmp through the image's
// import slots). The menus carry a real item tree (nodes ordered by a name key, built as a chain or a balanced tree, under
// several key orders, so the engine's walk order is whatever the keys make it). What is replaced, and only that: the name
// -> key function 0x5ab780 (a hash we control), the menu's set-focus slot target (records the item), the hover refresh
// 0x5ae6b0 (records the hover index the engine would draw), the hover string 0x5877d0, the sound call (records it), the
// lobby navigation 0x5bb150, and each item's vtable slot 0x74, a recorder that forwards to the retail 0x5c6490.
// The bars and boxes below are the builder's (legends-classic online_menus.py hit_rows / scene_layout for lc_online and the
// host lobby skin); `extras` are focusable or picker items the table does not list.
namespace hits_fixture {
namespace H=online_menus_hits;
struct authored { const char* menu; const char* name; float ax,az,l,b,r,t; float L,B,R,T; };
constexpr authored layout[]{
    {"lc_online","host",46.4f,272.8f,0,-17.6f,188.8f,0,34.4f,253.2f,247.2f,277.8f},
    {"lc_online","code",46.4f,241.6f,0,-17.6f,188.8f,0,34.4f,222,247.2f,246.6f},
    {"lc_online","browse",46.4f,210.4f,0,-17.6f,188.8f,0,34.4f,190.8f,247.2f,215.4f},
    {"lc_online","party",46.4f,179.2f,0,-17.6f,188.8f,0,34.4f,159.6f,247.2f,184.2f},
    {"lc_online","other",46.4f,148,0,-17.6f,188.8f,0,34.4f,128.4f,247.2f,153},
    {"lc_online","change_name",428,236,0,-14.4f,47.2f,0,416,219.6f,487.2f,241},
    {"lc_online","rejoin",431.2f,143.2f,0,-13.6f,44,0,419.2f,127.6f,487.2f,148.2f},
    {"lc_online","desctext1",34.4f,20.8f,0,-13.6f,47.2f,0,22.6f,5.2f,83.0f,25.8f},
    {"lc_online","desctext2",84.8f,20.8f,0,-13.6f,44.8f,0,83.4f,5.2f,130.2f,25.8f},
    {"lc_online","desctext3",131.2f,20.8f,0,-13.6f,73.6f,0,130.6f,5.2f,205.8f,25.8f},
    {"lc_online","desctext4",207.2f,20.8f,0,-13.6f,51.2f,0,206.2f,5.2f,270.2f,25.8f},
    // Synthetic (LC's own prompts have box top 0): a short prompt whose box top (+0x7a) is -14, so the engine's second-pass box
    // sits 7 units below its first-pass box (the 0x5ae0a0 hover test) and the two share only a 3-unit strip; the centre of the
    // second-pass box is outside the first-pass box.
    {"lc_online","desctext5",300,60,0,-23.5f,60,-14,288,26,372,50},
    {"lc_online","retry",432,44,-10.4f,-11.2f,39.2f,1.6f,421.6f,32.8f,471.2f,45.6f},
    {"host","lc_copy_code",388.8f,256.8f,0,-12.8f,76.8f,0,376.8f,242,477.6f,261.8f},
    {"host","text_startgame",46.4f,66.4f,0,-17.6f,121.6f,0,34.4f,46.8f,180,71.4f},
    {"host","text_kickplayer",196,66.4f,0,-17.6f,76,0,184,46.8f,284,71.4f},
    {"host","text_leaveroom",300,66.4f,0,-17.6f,54.4f,0,288,46.8f,366.4f,71.4f},
    {"host","lc_leave_yes",320,97.6f,0,-19.2f,46.4f,0,320,78.4f,366.4f,97.6f},
    {"host","lc_leave_cancel",372.8f,97.6f,0,-19.2f,48,0,372.8f,78.4f,420.8f,97.6f},
    {"host","lc_lobby_dismiss",425.6f,97.6f,0,-19.2f,52,0,425.6f,78.4f,477.6f,97.6f},
};
// Items of the same menus that the table does not list. flags: the +0x54 byte (8 focusable, 4 hidden).
// kind: 0 a text item with the recorder (its text is its name), 1 a MENU_ITEM_MODEL (CMenuItemModel's vtable: the retail
// 0x5bc1b0 hit test, no text - it never answers), 2 a text item whose text is empty (the recorder; 0x5bc1b0 refuses it).
struct extra { const char* menu; const char* name; float ax,az,l,b,r,t; unsigned char flags; int kind=0; };
constexpr extra extras[]{
    {"lc_online","stray",100.4f,241.6f,0,-17.6f,64,0,8},       // focusable, unlisted, over the middle of the "code" bar's box
    {"lc_online","stray2",300,120,0,-20,60,0,8},               // focusable, unlisted, away from every bar
    {"host","text_playername1",43.2f,204.8f,0,-96,424,0,4},    // the Kick picker (hidden, not focusable): 0x5ba2b0 asks its box anyway
    {"host","lc_lobby_v1",0,0,0,-1,1,0,0},                     // the skin marker
    // Fix K - the real screens' population (060r built lc_online / host, online_menus.py scene_layout): every focusable row
    // has a <row>_focus MENU_ITEM_MODEL at the row's own anchor and box, shown (it swaps model_hide for the bar on focus) and
    // enabled (+0x54 bit 3 is "enabled": the parser sets it on every item) - fix E2 counted these as answering, so no point of
    // any row was free and every row kept its retail text box live. Labels (neverfocus, bit 5) with text DO answer in the
    // retail walk; models and empty texts never do.
    {"lc_online","host_focus",46.4f,272.8f,0,-17.6f,188.8f,0,0x28,1},
    {"lc_online","code_focus",46.4f,241.6f,0,-17.6f,188.8f,0,0x28,1},
    {"lc_online","browse_focus",46.4f,210.4f,0,-17.6f,188.8f,0,0x28,1},
    {"lc_online","party_focus",46.4f,179.2f,0,-17.6f,188.8f,0,0x28,1},
    {"lc_online","other_focus",46.4f,148,0,-17.6f,188.8f,0,0x28,1},
    {"lc_online","change_name_focus",428,236,0,-14.4f,47.2f,0,0x28,1},
    {"lc_online","rejoin_focus",431.2f,143.2f,0,-13.6f,44,0,0x28,1},
    {"lc_online","desctext1_focus",34.4f,20.8f,0,-13.6f,47.2f,0,0x28,1},
    {"lc_online","desctext2_focus",84.8f,20.8f,0,-13.6f,44.8f,0,0x28,1},
    {"lc_online","desctext3_focus",131.2f,20.8f,0,-13.6f,73.6f,0,0x28,1},
    {"lc_online","desctext4_focus",207.2f,20.8f,0,-13.6f,51.2f,0,0x28,1},
    {"lc_online","retry_focus",432,44,-10.4f,-11.2f,39.2f,1.6f,0x28,1},
    {"lc_online","lc_hub_host_left_veil",0,384,0,-385,511,0,0x2c,1},  // the host-left veil: hidden, the whole view
    {"lc_online","host_hint",144,270,0,-13,89,0,0x28,0},              // "Private or public": a label inside the host row (answers)
    {"lc_online","browse_blank",46.4f,210.4f,0,-17.6f,188.8f,0,0x28,2}, // an empty label over the whole browse row (never answers)
    // The host lobby's inline rows: the Leave confirm's panel model (shown while it is up) and the Code copied model (hidden)
    // cover the Leave / Cancel / Dismiss buttons; the shared focus model (lc_focus, one per plane) sits at its origin.
    {"host","lc_leave_confirm",34.4f,99.2f,0,-23.2f,442.4f,0,0x28,1},
    {"host","lc_code_copied",34.4f,99.2f,0,-23.2f,442.4f,0,0x2c,1},
    {"host","lc_focus",0,384,0,-1,1,0,0x28,1},
    // Every LC screen's first two items: the backdrop and field models (shown). Their box is the anchor's or, once the model is
    // attached, the art's - the whole view; either way a model never answers.
    {"lc_online","lc_o_backdrop",-200,384,0,-385,912,0,0x28,1},
    {"host","lc_o_backdrop",-200,384,0,-385,912,0,0x28,1},
};
struct fitem { std::string menu,name; float ax,az,l,b,r,t; unsigned char flags; const authored* bar=nullptr; std::array<unsigned char,0xb0> mem{}; int kind=0; };
std::vector<fitem> items;
std::array<unsigned char,0x1a00> retail_menu{},lobby_menu{};
std::vector<char> strings(0x4000);
std::array<unsigned,0x23> item_vtable{},model_vtable{};
bool marker_present=true;
unsigned key_seed=1;
bool balanced_tree=false;
// ---- recorders ---------------------------------------------------------------------------------------------------------------
struct reply {
    bool ret=false; std::string answered,focus_set; std::vector<std::string> asked;
    int hover=-1,hover_sounds=0,sounds=0,seen_x=0,seen_y=0; bool saw=false;
};
reply rep; int phase=0;
int pixel_x=0,pixel_y=0,screen_w=0,screen_h=0;
bool pixel_known=true; bool pixel_stale=false;
bool __cdecl probe(int& x,int& y,int& w,int& h) { if(!pixel_known) return false; x=pixel_x+(pixel_stale ? 3 : 0);y=pixel_y;w=screen_w;h=screen_h;return true; }
std::string name_of(void* it) { for(const auto& i:items) if(i.mem.data()==it) return i.name; return "?"; }
bool __fastcall rec_hit(void* item,void*,int x,int y,int flags,int message,int extra) {
    phase=2; rep.asked.push_back(name_of(item)); rep.seen_x=x; rep.seen_y=y; rep.saw=true;
    const bool hit=reinterpret_cast<bool(__thiscall*)(void*,int,int,int,int,int)>(0x5c6490)(item,x,y,flags,message,extra);
    if(hit) rep.answered=name_of(item);
    return hit;
}
void __fastcall set_focus_rec(void*,void*,void* item) { rep.focus_set=name_of(item); }
void __fastcall hover_refresh_rec(void*,void*) { rep.hover=*reinterpret_cast<int*>(0x6e4b18); }
void __fastcall sound_rec(void*,void*,int) { ++rep.sounds; if(phase==0) ++rep.hover_sounds; }
const char* __fastcall slot_text(void*,void*) { return "hover_snd"; }
std::vector<fitem*> lobby_pass; // text_startgame, text_kickplayer, text_leaveroom: the lobby's first-pass items
void* __fastcall nav_rec(void* menu,void*,void* request) {
    // 0x5bb150: the item `step` places from the focused one (request byte +1 = hit index - focus index).
    const int step=*(signed char*)(static_cast<char*>(request)+1);
    void* focused=*reinterpret_cast<void**>(static_cast<char*>(menu)+0x324);
    for(std::size_t i=0;i<lobby_pass.size();++i) if(lobby_pass[i]->mem.data()==focused) {
        const int to=int(i)+step; return to>=0 && to<int(lobby_pass.size()) ? lobby_pass[std::size_t(to)]->mem.data() : nullptr;
    }
    return lobby_pass.empty() ? nullptr : lobby_pass[std::size_t(std::max(0,std::min(int(lobby_pass.size())-1,step)))]->mem.data();
}
// ---- the item tree -----------------------------------------------------------------------------------------------------------
int key_of(const char* name) {
    unsigned h=2166136261u^(key_seed*2654435761u);
    for(const char* c=name;*c;++c) { h^=static_cast<unsigned char>(*c);h*=16777619u; }
    return int(h&0x1fffffff);
}
int* __fastcall key_stub(int* out,void*,const char* name) { *out=name && *name ? key_of(name) : 0;return out; }
void* __fastcall item_lookup(void* menu,void*,const char* name) {
    if(!name || !*name) return nullptr;
    if(!marker_present && std::string_view(name)=="lc_lobby_v1") return nullptr;
    int key=key_of(name);
    auto* list=static_cast<char*>(menu)+0x824;
    const int node=reinterpret_cast<int(__thiscall*)(void*,int*,int)>(0x5ab7d0)(list,&key,*reinterpret_cast<int*>(list+4));
    return node==0x3fffffff ? nullptr : *reinterpret_cast<void**>(list+0xde8+4*node);
}
void* __cdecl string_table() { return strings.data(); }
const char* __cdecl crt_strstr(const char* a,const char* b) { return std::strstr(a,b); }
int __cdecl crt_strncmp(const char* a,const char* b,std::size_t n) { return std::strncmp(a,b,n); }
int ftol(float v) { return static_cast<int>(v); } // _ftol2 (0x67217c): truncation
void lay_out(fitem& i,int id) {
    i.mem.fill(0);put(i.mem.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(item_vtable.data())));i.mem[0x54]=i.flags;
    put(i.mem.data(),0x24,unsigned(id));put(i.mem.data(),0x9c,unsigned(id));
    const std::int16_t x78=std::int16_t(ftol(i.l)),y7a=std::int16_t(ftol(i.t));
    const std::int16_t f[]{std::int16_t(ftol(i.ax)+x78),std::int16_t(ftol(i.az)+y7a),std::int16_t(ftol(i.r-i.l)),std::int16_t(ftol(i.t-i.b)),x78,y7a};
    std::memcpy(i.mem.data()+0x70,f,sizeof f);
}
void text_of(int id,const char* text) {
    const int offset=id*64;std::memcpy(strings.data()+4+id*4,&offset,4);std::strcpy(strings.data()+0x1008+offset,text);
}
constexpr int end_node=0x3fffffff;
// The tree of one menu: nodes at list+0xc+16 n = {parent, left, right, key}, root index at list+4, item pointers at list+0xde8.
int link(char* list,const std::vector<int>& sorted,int lo,int hi,int parent,bool balanced) {
    if(lo>hi) return end_node;
    const int mid=balanced ? (lo+hi)/2 : lo, n=sorted[std::size_t(mid)];
    auto* node=list+16*n+0xc;
    *reinterpret_cast<int*>(node)=parent;
    *reinterpret_cast<int*>(node+4)=balanced ? link(list,sorted,lo,mid-1,n,balanced) : end_node;
    *reinterpret_cast<int*>(node+8)=link(list,sorted,mid+1,hi,n,balanced);
    return n;
}
std::vector<fitem*> walk_order; // the items of the menu in the order the engine's in-order walk reaches them
void build_menu(unsigned char* menu,const char* name) {
    std::memset(menu+0x824,0,sizeof(retail_menu)-0x824);
    std::vector<fitem*> list;
    for(auto& i:items) if(i.menu==name && (marker_present || i.name!="lc_lobby_v1")) list.push_back(&i);
    std::vector<int> sorted(list.size());
    for(std::size_t n=0;n<list.size();++n) sorted[n]=int(n);
    std::sort(sorted.begin(),sorted.end(),[&](int a,int b) { return key_of(list[std::size_t(a)]->name.c_str())<key_of(list[std::size_t(b)]->name.c_str()); });
    for(std::size_t k=1;k<sorted.size();++k) assert(key_of(list[std::size_t(sorted[k-1])]->name.c_str())!=key_of(list[std::size_t(sorted[k])]->name.c_str()));
    auto* l=reinterpret_cast<char*>(menu)+0x824;
    for(std::size_t n=0;n<list.size();++n) {
        *reinterpret_cast<int*>(l+16*n+0xc+0xc)=key_of(list[n]->name.c_str());
        *reinterpret_cast<void**>(l+0xde8+4*n)=list[n]->mem.data();
    }
    *reinterpret_cast<int*>(l+4)=list.empty() ? end_node : link(l,sorted,0,int(sorted.size())-1,end_node,balanced_tree);
    if(list.empty()) *reinterpret_cast<int*>(l+4)=end_node;
    walk_order.clear();
    for(const int n:sorted) walk_order.push_back(list[std::size_t(n)]);
    for(int p=0;p<5;++p) put(menu,0x2f4+4*p,1); // a hover sound string per prompt
}
std::vector<fitem*> expected_walk(const char* menu) { // shown, focusable items in key order
    std::vector<fitem*> out; (void)menu;
    for(auto* i:walk_order) if(!(i->flags&4) && (i->flags&8) && i->menu==menu && i->kind!=1) out.push_back(i);
    return out;
}
void build_all() {
    build_menu(hub.data(),"lc_online"); auto hub_order=walk_order;
    build_menu(lobby_menu.data(),"host"); auto lobby_order=walk_order;
    build_menu(retail_menu.data(),"options");
    walk_order=hub_order; walk_order.insert(walk_order.end(),lobby_order.begin(),lobby_order.end());
}
void write_table_text(const std::string& text) { std::ofstream(root/"Data/lc_online_hits.txt",std::ios::binary)<<text;++asset_generation; }
std::string table_text(bool lobby_rows=true) {
    std::string t=std::string(H::contract)+"\n";char line[160];
    for(const auto& a:layout) {
        if(!lobby_rows && std::string_view(a.menu)=="host") continue;
        std::snprintf(line,sizeof line,"%s %s %g %g %g %g\n",a.menu,a.name,a.L,a.B,a.R,a.T);t+=line;
    }
    return t;
}
// XMen2.exe 0x5f9eb0, written out independently of the DLL's rule: iw = ftol(width), x = ftol(left + (px * iw) / W),
// y = 384 - (py * 384) / H.
void engine_point(int px,int py,int w,int h,int& x,int& y) {
    const float width=384.0f*float(w)/float(h),left=-(width-512.0f)/2.0f;const int iw=int(width);
    x=int(left+float((px*iw)/w));y=384-(py*384)/h;
}
float view_x(int px,int w,int h) { const float width=384.0f*float(w)/float(h);return -(width-512.0f)/2.0f+(float(px)+0.5f)*width/float(w); }
float view_y(int py,int h) { return 384.0f-(float(py)+0.5f)*384.0f/float(h); }
using mouse_t=std::uint32_t(__fastcall*)(void*,void*,int,int,int,int,int);
// One mouse message at a screen pixel through `mouse` into the retail walk; the recorders say what the engine did.
reply ask(mouse_t mouse,void* menu,int px,int py,int w,int h,unsigned message=0x200) {
    pixel_x=px;pixel_y=py;screen_w=w;screen_h=h;int x,y;engine_point(px,py,w,h,x,y);
    rep=reply();phase=0;*reinterpret_cast<int*>(0x6e4b18)=-1;
    rep.ret=mouse(menu,nullptr,x,y,0,int(message),0)&0xff;
    return rep;
}
reply ask_unit(mouse_t mouse,void* menu,int x,int y,unsigned message=0x200) {
    rep=reply();phase=0;*reinterpret_cast<int*>(0x6e4b18)=-1;
    rep.ret=mouse(menu,nullptr,x,y,0,int(message),0)&0xff;
    return rep;
}
float box_distance(const fitem& i,float x,float y) {
    std::int16_t f[6];std::memcpy(f,i.mem.data()+0x70,sizeof f);
    const float left=f[0],right=float(f[0]+f[2]-1),low=float(f[1]-f[3]-1+f[5]/2),high=float(f[1]+f[5]/2);
    const float dx=std::max({left-x,0.0f,x-right}),dy=std::max({low-y,0.0f,y-high});return dx*dx+dy*dy;
}
bool box_holds(const fitem& i,int x,int y,bool shifted) {
    std::int16_t f[6];std::memcpy(f,i.mem.data()+0x70,sizeof f);
    const int shift=shifted ? f[5]/2 : 0;
    return x>=f[0] && x<f[0]+f[2]-1 && y>=f[1]-f[3]-1+shift && y<f[1]+shift;
}
const authored* bar_of(const fitem& i) { return i.bar; }
// What the drawn bars (table rule: Direct3D's top-left fill, the focused item's bar wins over a neighbour's, else the nearest
// text box) and the dead-strip rules say the engine must answer for a pixel. "" = nothing; unlisted items answer for themselves
// where no bar and no listed box holds the point.
std::string expected(void* menu,int px,int py,int w,int h) {
    const char* screen=static_cast<char*>(menu)+0xc;
    const void* focused=*reinterpret_cast<void**>(static_cast<char*>(menu)+0x324);
    const float x=view_x(px,w,h),y=view_y(py,h);
    const fitem* best=nullptr;
    for(const auto& i:items) {
        const auto* a=bar_of(i);
        if(!a || i.menu!=screen || (i.flags&4) || !(x>=a->L && x<a->R && y>a->B && y<=a->T)) continue;
        if(i.mem.data()==focused) return i.name;
        if(!best || box_distance(i,x,y)<box_distance(*best,x,y)) best=&i;
    }
    if(best) return best->name;
    int ex,ey;engine_point(px,py,w,h,ex,ey);
    for(const auto& i:items) if(!bar_of(i) && i.menu==screen && i.kind==0 && (i.flags&8) && !(i.flags&4) && (box_holds(i,ex,ey,true) || box_holds(i,ex,ey,false))) return i.name;
    return "";
}
std::string hover_name(const std::string& answered) { return answered.starts_with("desctext") ? answered : ""; }
int px_of_view(float x,int w,int h) { const float width=384.0f*float(w)/float(h),left=-(width-512.0f)/2.0f;return int((x-left)*float(w)/width); }
int py_of_view(float y,int h) { return int((384.0f-y)*float(h)/384.0f); }
struct span { int first=-1,last=-1; };
span columns(const authored& a,int w,int h) { span s;for(int px=0;px<w;++px) { const float x=view_x(px,w,h); if(x>=a.L && x<a.R) { if(s.first<0) s.first=px; s.last=px; } } return s; }
span rows(const authored& a,int h) { span s;for(int py=0;py<h;++py) { const float y=view_y(py,h); if(y>a.B && y<=a.T) { if(s.first<0) s.first=py; s.last=py; } } return s; }
// One pixel through the retail walk: the engine's answer is the table rule's; the walk order is the key order; the hover index
// and hover sound (the first pass over the prompts) agree with the answer; the point the item hit tests saw lies in the
// answering item's own boxes.
unsigned check_pixel(mouse_t mouse,void* menu,const char* name,int px,int py,int w,int h,unsigned message=0x200) {
    const auto got=ask(mouse,menu,px,py,w,h,message);
    const auto want=expected(menu,px,py,w,h);
    if(got.answered!=want) std::cout<<"pixel "<<px<<","<<py<<" "<<w<<"x"<<h<<" "<<name<<": engine answered '"<<got.answered<<"', expected '"<<want<<"'\n";
    assert(got.answered==want);
    // The engine's walk reaches the items in key order and stops at the first that answers.
    const auto order=expected_walk(name);std::vector<std::string> prefix;
    for(auto* i:order) { prefix.push_back(i->name); if(i->name==got.answered) break; }
    if(!got.answered.empty() && got.saw) assert(got.asked==prefix);
    // First pass: the prompt under the pointer is the hovered one (index N-1 for desctextN) and plays the hover sound once.
    if(std::string_view(name)=="lc_online") {
        const int want_hover=got.answered.starts_with("desctext") ? got.answered.back()-'1' : -1;
        if(got.hover!=want_hover) std::cout<<"pixel "<<px<<","<<py<<": hover "<<got.hover<<" want "<<want_hover<<" (answered '"<<got.answered<<"')\n";
        assert(got.hover==want_hover && got.hover_sounds==(want_hover>=0 ? 1 : 0));
    }
    return 1;
}
unsigned edge_checks(mouse_t mouse,void* menu,const char* name,int w,int h,bool coarse_only=false,int scale=1) {
    unsigned n=0;
    auto*& focus=*reinterpret_cast<void**>(static_cast<char*>(menu)+0x324);
    for(auto& i:items) {
        const auto* ap=bar_of(i);
        if(!ap || i.menu!=name || coarse_only) continue;
        const auto& a=*ap;
        focus=i.mem.data(); // its bar is the one drawn
        const auto c=columns(a,w,h),r=rows(a,h);assert(c.first>0 && r.first>0 && c.last<w-1 && r.last<h-1);
        const int mx=(c.first+c.last)/2,my=(r.first+r.last)/2;
        // Every pixel of the bar's four edges answers with its item; the pixel one past each edge does not.
        for(int px=c.first;px<=c.last;++px) {
            assert(ask(mouse,menu,px,r.first,w,h).answered==a.name);assert(ask(mouse,menu,px,r.last,w,h).answered==a.name);
            assert(ask(mouse,menu,px,r.first-1,w,h).answered!=a.name);assert(ask(mouse,menu,px,r.last+1,w,h).answered!=a.name);n+=4;
        }
        for(int py=r.first;py<=r.last;++py) {
            assert(ask(mouse,menu,c.first,py,w,h).answered==a.name);assert(ask(mouse,menu,c.last,py,w,h).answered==a.name);
            assert(ask(mouse,menu,c.first-1,py,w,h).answered!=a.name);assert(ask(mouse,menu,c.last+1,py,w,h).answered!=a.name);n+=4;
        }
        n+=check_pixel(mouse,menu,name,mx,my,w,h);
        // The four corners, and one pixel diagonally outside each.
        for(const auto [x0,y0,dx,dy]:{std::array{c.first,r.first,-1,-1},std::array{c.last,r.first,1,-1},std::array{c.first,r.last,-1,1},std::array{c.last,r.last,1,1}}) {
            n+=check_pixel(mouse,menu,name,x0,y0,w,h);assert(ask(mouse,menu,x0+dx,y0+dy,w,h).answered!=a.name);n+=2;
        }
    }
    // Coarse whole-screen scan, with no focus and with each item focused: the answer is always the bar rule's, or nothing.
    focus=nullptr;
    for(int py=0;py<h;py+=5*scale) for(int px=0;px<w;px+=7*scale) n+=check_pixel(mouse,menu,name,px,py,w,h);
    for(auto& i:items) {
        if(!bar_of(i) || i.menu!=name) continue;
        focus=i.mem.data();
        for(int py=0;py<h;py+=23*scale) for(int px=0;px<w;px+=19*scale) n+=check_pixel(mouse,menu,name,px,py,w,h);
    }
    focus=nullptr;
    return n;
}
void install_recorders() {
    // The retail mouse code runs; these are the only replaced functions (see the file header).
    std::memcpy(item_vtable.data(),reinterpret_cast<void*>(0x6a0244),sizeof item_vtable);
    item_vtable[0x74/4]=unsigned(reinterpret_cast<std::uintptr_t>(&rec_hit));
    std::memcpy(model_vtable.data(),reinterpret_cast<void*>(0x6a0304),sizeof model_vtable); // CMenuItemModel: vt+0x74 = 0x5bc1b0
    assert(model_vtable[0x74/4]==0x5bc1b0 && model_vtable[1]==0x5bd8f0);
    online_menus::set_hits_text_alias(reinterpret_cast<std::uintptr_t>(&rec_hit)); // the recorder forwards to 0x5c6490
    redirect(0x5ab780,reinterpret_cast<const void*>(&key_stub));
    redirect(0x602200,reinterpret_cast<const void*>(&string_table));redirect(0x5adc10,reinterpret_cast<const void*>(&item_lookup));
    redirect(*reinterpret_cast<unsigned*>(0x69e41c+4),reinterpret_cast<const void*>(&set_focus_rec));   // menu vt+4: set focus
    redirect(*reinterpret_cast<unsigned*>(0x69e41c+0x68),reinterpret_cast<const void*>(&hover_refresh_rec)); // menu vt+0x68: 0x5ae6b0
    redirect(*reinterpret_cast<unsigned*>(0x69f274+0x40),reinterpret_cast<const void*>(&nav_rec));      // lobby vt+0x40: 0x5bb150
    redirect(0x5877d0,reinterpret_cast<const void*>(&slot_text));
    redirect(*reinterpret_cast<unsigned*>(0x6a236c+0xe8),reinterpret_cast<const void*>(&sound_rec)); // manager vt+0xe8: menu sound
    // The image's own imports the item hit tests call: PtInRect, and the click path's strstr / strncmp (jump stubs 0x67220a /
    // 0x6723bc).
    put(reinterpret_cast<void*>(0x67f230),0,unsigned(reinterpret_cast<std::uintptr_t>(&PtInRect)));
    put(reinterpret_cast<void*>(0x67f130),0,unsigned(reinterpret_cast<std::uintptr_t>(&crt_strstr)));
    put(reinterpret_cast<void*>(0x67f0f8),0,unsigned(reinterpret_cast<std::uintptr_t>(&crt_strncmp)));
}
void make_items() {
    items.clear();items.reserve(std::size(layout)+std::size(extras)+4);
    for(const auto& a:layout) { fitem i;i.menu=a.menu;i.name=a.name;i.ax=a.ax;i.az=a.az;i.l=a.l;i.b=a.b;i.r=a.r;i.t=a.t;i.flags=8;i.bar=&a;items.push_back(std::move(i)); }
    for(const auto& e:extras) { fitem i;i.menu=e.menu;i.name=e.name;i.ax=e.ax;i.az=e.az;i.l=e.l;i.b=e.b;i.r=e.r;i.t=e.t;i.flags=e.flags;i.kind=e.kind;items.push_back(std::move(i)); }
    for(std::size_t n=0;n<items.size();++n) {
        auto& i=items[n];lay_out(i,int(n)+1);text_of(int(n)+1,i.kind==2 ? "" : i.name.c_str());
        put(i.mem.data(),8,unsigned(n)+1);            // +8: the item's name (the diagnostic names items by it)
        if(i.kind==1) { put(i.mem.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(model_vtable.data())));put(i.mem.data(),0x24,0u);put(i.mem.data(),0x9c,0u); }
        if(i.kind==2) put(i.mem.data(),0x9c,0u);
    }
    text_of(10,"$MENU_OTHER Change name"); // desctext3's prompt: a click raises the token's code, not Accept
    lobby_pass.clear();
    for(const char* n:{"text_startgame","text_kickplayer","text_leaveroom"}) for(auto& i:items) if(i.name==n) lobby_pass.push_back(&i);
}
fitem& item_named(const char* menu,const char* name) { for(auto& i:items) if(i.menu==menu && i.name==name) return i; assert(false);return items.front(); }
void set_menus() {
    put(lobby_menu.data(),0,0x69f274);lobby_menu[4]=1;std::strcpy(reinterpret_cast<char*>(lobby_menu.data()+0xc),"host");
    put(retail_menu.data(),0,0x69e41c);retail_menu[4]=1;std::strcpy(reinterpret_cast<char*>(retail_menu.data()+0xc),"options");
}
// ---- the parser ----------------------------------------------------------------------------------------------------------------
void parse_cases(unsigned& checks) {
    const std::string good=std::string(H::contract)+"\nlc_online host 34.4 253.2 247.2 277.8\nhost text_startgame 34.4 46.8 180 71.4\n";
    H::table t;std::string why;
    auto refused=[&](const std::string& text,const char* expect) {
        std::string w;H::table table;const bool ok=H::parse(text,table,w);
        if(ok || w.find(expect)==std::string::npos || !table.entries.empty()) std::cout<<"parse case expecting '"<<expect<<"': ok="<<ok<<" why='"<<w<<"'\n";
        assert(!ok && w.find(expect)!=std::string::npos && table.entries.empty());++checks;
    };
    assert(H::parse(good,t,why) && t.entries.size()==2 && t.entries[1].item=="text_startgame" && t.entries[1].rect.top==71.4f);++checks;
    const std::string body="lc_online host 34.4 253.2 247.2 277.8\n";
    refused(std::string("\xEF\xBB\xBF")+good,"byte order mark");
    { std::string crlf=good;for(std::size_t i=0;(i=crlf.find('\n',i))!=std::string::npos;i+=2) crlf.insert(i,"\r");refused(crlf,"carriage return"); }
    refused(std::string(H::contract)+"\r\n"+body,"carriage return");
    refused(std::string(H::contract)+"\n"+"lc_online host 34.4 253.2 247.2 277.8\r\n","carriage return");
    refused(std::string(H::contract)+"\n"+"lc_online host 34.4 253.2 247.2 "+std::string(300,'7')+"\n","line too long");
    refused(std::string(H::contract)+"\n"+std::string(100,'a')+" host 1 2 3 4\n","bad entry");
    refused(good+"lc_online host 1 2 3 4\n","duplicate entry");
    refused(std::string(H::contract)+"\n"+body+body,"line 3: duplicate entry");
    refused("","empty file");
    refused(std::string(H::contract)+"\n","no entries");
    refused(std::string(H::contract)+"\n"+body.substr(0,body.size()-1),"last line not terminated");
    refused(std::string("lc-online-hits/3\n")+body,"bad header");
    refused(std::string(H::contract)+"\n\n"+body,"bad entry");
    refused(std::string(H::contract)+"\nlc_online  host 1 2 3 4\n","bad fields");
    refused(std::string(H::contract)+"\nlc_online host 1 2 3 4 \n","bad fields");
    refused(std::string(H::contract)+"\nlc_online host 1 2 3\n","bad entry");
    refused(std::string(H::contract)+"\nlc_online host 1 2 3 4 5\n","bad fields");
    refused(std::string(H::contract)+"\nlc-online host 1 2 3 4\n","bad entry");
    refused(std::string(H::contract)+"\nlc_online ho.st 1 2 3 4\n","bad entry");
    refused(std::string(H::contract)+"\nlc_online host 1e2 2 3 4\n","bad entry");
    refused(std::string(H::contract)+"\nlc_online host +1 2 3 4\n","bad entry");
    refused(std::string(H::contract)+"\nlc_online host 1,5 2 3 4\n","bad entry");
    refused(std::string(H::contract)+"\nlc_online host 1..5 2 3 4\n","bad entry");
    refused(std::string(H::contract)+"\nlc_online host - 2 3 4\n","bad entry");
    refused(std::string(H::contract)+"\nlc_online host 5000 2 6000 4\n","bad entry");
    refused(std::string(H::contract)+"\nlc_online host 3 2 3 4\n","bad entry");     // left >= right
    refused(std::string(H::contract)+"\nlc_online host 1 4 3 4\n","bad entry");     // bottom >= top
    { std::string many=std::string(H::contract)+"\n";for(std::size_t i=0;i<=H::max_entries;++i) many+="m i"+std::to_string(i)+" 1 2 3 4\n";refused(many,"too many entries"); }
    // Numbers are read locale-independently: a comma-decimal locale (strtod) would refuse every one of them.
    { const char* old=std::setlocale(LC_NUMERIC,nullptr);std::string saved=old ? old : "C";
      for(const char* loc:{"de-DE","German_Germany.1252","fr-FR"}) if(std::setlocale(LC_NUMERIC,loc)) {
          std::string w;H::table table;assert(H::parse(good,table,w) && table.entries[0].rect.left==34.4f);++checks;break; }
      std::setlocale(LC_NUMERIC,saved.c_str()); }
    float v=0;assert(H::number("-0.5",v) && v==-0.5f && H::number("5.",v) && H::number(".5",v) && H::number("4096",v) && !H::number("4096.001",v) && !H::number("-4097",v) && !H::number("",v));checks+=5;
}
// Through the DLL: a refused table is logged (once, loudly) and the engine's own hit test keeps working.
void refused_through_dll(mouse_t mouse,unsigned& checks) {
    const auto& a=layout[1];const auto r=rows(a,720);const auto c=columns(a,1280,720);
    const std::string cases[]{ table_text()+"lc_online bad 1 2 3\n", std::string("\xEF\xBB\xBF")+table_text(), [] { auto t=table_text();for(std::size_t i=0;(i=t.find('\n',i))!=std::string::npos;i+=2) t.insert(i,"\r");return t; }(),
        table_text()+"lc_online code 1 2 3 4\n" /* duplicate */, table_text()+std::string(400,'x')+"\n" };
    for(const auto& text:cases) {
        once_logs.erase("online:hits-refused");write_table_text(text);
        assert(ask(mouse,hub.data(),(c.first+c.last)/2,r.first,1280,720).answered=="");       // the top edge: dead again (retail)
        assert(ask(mouse,hub.data(),px_of_view(60,1280,720),py_of_view(232,720),1280,720).answered=="code"); // the engine's own box still works (left of the stray item)
        assert(once_logs.contains("online:hits-refused") && once_logs["online:hits-refused"].find("ERROR")!=std::string::npos &&
               once_logs["online:hits-refused"].find("REFUSED")!=std::string::npos && !once_logs.contains("online:hits"));
        checks+=3;
    }
}
// Fix K: the 060r live round's own probes (live-060r/evidence/mh-hub720.json, mh-hub1080.json, mh-lobby1080.json - pipe
// `mouse X Y move`, the row's amber read from the frame; every one of these was dead live) and the Leave confirm (er2: a hover
// on Leave left the bar on Cancel and a click on Leave acted as Cancel).
unsigned live_060r(mouse_t mouse,mouse_t host_mouse) {
    unsigned n=0;
    auto*& hub_focus=*reinterpret_cast<void**>(hub.data()+0x324);
    struct probe { const char* row; int x,y,w,h; } hub_probes[]{
        {"host",427,201,1280,720},{"host",230,222,1280,720},{"host",620,222,1280,720},{"host",232,201,1280,720},
        {"code",427,259,1280,720},{"browse",427,319,1280,720},{"other",427,436,1280,720},{"change_name",1010,271,1280,720},
        {"host",640,301,1920,1080},{"host",344,333,1920,1080},{"code",640,388,1920,1080},{"browse",640,478,1920,1080}};
    for(const auto& p:hub_probes) {
        hub_focus=item_named("lc_online","code").mem.data(); // mh.py parked the pointer on another row first
        if(std::string_view(p.row)=="code") hub_focus=item_named("lc_online","host").mem.data();
        const auto got=ask(mouse,hub.data(),p.x,p.y,p.w,p.h);
        if(got.answered!=p.row || got.focus_set!=p.row) std::cout<<"060r probe "<<p.row<<" "<<p.x<<","<<p.y<<" "<<p.w<<"x"<<p.h<<": answered '"<<got.answered<<"' focus '"<<got.focus_set<<"'\n";
        assert(got.answered==p.row && got.focus_set==p.row);++n;
    }
    hub_focus=nullptr;
    // The host lobby's START / KICK / LEAVE top strips at 1080p (focus on START GAME: the lobby's own first pass navigates).
    auto*& lobby_focus=*reinterpret_cast<void**>(lobby_menu.data()+0x324);
    struct probe lobby_probes[]{{"text_startgame",547,882,1920,1080},{"text_kickplayer",903,882,1920,1080},{"text_leaveroom",1165,882,1920,1080}};
    for(const auto& p:lobby_probes) {
        lobby_focus=item_named("host","text_startgame").mem.data();
        const auto got=ask(host_mouse,lobby_menu.data(),p.x,p.y,p.w,p.h);
        if(got.answered!=p.row && !(std::string_view(p.row)=="text_startgame" && !got.saw)) std::cout<<"060r lobby probe "<<p.row<<": answered '"<<got.answered<<"'\n";
        assert(got.answered==p.row || (std::string_view(p.row)=="text_startgame" && !got.saw)); // the focused button's own strip: the lobby pass keeps it
        ++n;
    }
    // The Leave confirm up, the bar on Cancel: a hover on Leave's text (er2: pixel 776,549 at 720p) moves the focus to Leave, a
    // click there is Leave's Accept (code 4 raised by Leave's own test) - never Cancel's.
    lobby_focus=item_named("host","lc_leave_cancel").mem.data();
    { const auto hover=ask(host_mouse,lobby_menu.data(),776,549,1280,720);
      if(hover.answered!="lc_leave_yes") std::cout<<"Leave confirm hover: answered '"<<hover.answered<<"' focus '"<<hover.focus_set<<"'\n";
      assert(hover.answered=="lc_leave_yes" && hover.focus_set=="lc_leave_yes");++n;
      const int player=*reinterpret_cast<int*>(0x6f3920);
      auto* code=reinterpret_cast<int*>(0xa09f54+player*4);auto* raised=reinterpret_cast<unsigned char*>(0xa09fa0+player);
      lobby_focus=item_named("host","lc_leave_cancel").mem.data();*code=0;*raised=0;
      const auto click=ask(host_mouse,lobby_menu.data(),776,549,1280,720,0x202);
      assert(click.answered=="lc_leave_yes" && click.focus_set=="lc_leave_yes" && *code==4 && *raised==1);++n;
      // ... and the panel's own strip around the buttons (inside the panel model, outside every button bar) is nobody's.
      lobby_focus=item_named("host","lc_leave_cancel").mem.data();
      const auto strip=ask(host_mouse,lobby_menu.data(),300,549,1280,720);
      assert(strip.answered!="lc_leave_yes" && strip.answered!="lc_leave_cancel");++n; }
    lobby_focus=nullptr;
    // The diagnostic (only with [Test] InputPipe=1): one line per message naming the bar, the handed point, the model's answer and
    // the engine's focus after its walk; a repeat of the same line is not written again.
    online_menus::set_hits_trace(true);r6_log.clear();
    hub_focus=item_named("lc_online","code").mem.data();
    ask(mouse,hub.data(),427,201,1280,720);ask(mouse,hub.data(),427,201,1280,720);
    online_menus::set_hits_trace(false);hub_focus=nullptr;
    if(r6_log.size()!=1) for(const auto& l:r6_log) std::cout<<"trace: "<<l<<"\n";
    assert(r6_log.size()==1);
    const auto& line=r6_log.front();
    for(const char* part:{"online menus: mouse lc_online 0x200 pixel 427,201 of 1280x720 (exact)","bar host;","remapped;","model answers host;","after: focus code, hover -1"}) // (the fixture's set-focus recorder records the item; +0x324 stays)
        if(line.find(part)==std::string::npos) { std::cout<<"trace line: "<<line<<"\n(missing '"<<part<<"')\n";assert(false); }
    ++n;
    return n;
}
void run(const std::string& variant) {
    const auto mouse=*reinterpret_cast<mouse_t*>(0x69e49c);
    const auto host_mouse=*reinterpret_cast<mouse_t*>(0x69f2f4);
    assert(reinterpret_cast<std::uintptr_t>(mouse)!=0x5ae0a0); // the installed hook, not the retail slot
    install_recorders();make_items();set_menus();build_all();
    online_menus::set_pointer_probe(&probe);
    online_menus::set_hits_trace(false); // the fixture's [Test] InputPipe reads "1"; the diagnostic is checked on its own (live_060r)
    unsigned checks=0;
    const std::pair<int,int> screens[]{{1280,720},{1920,1080},{960,720},{1440,1080},{1024,768}};
    if(variant=="r16-hits-parse") {
        parse_cases(checks);
        write_table_text(table_text());
        refused_through_dll(mouse,checks);
        std::cout<<"online menus "<<variant<<" PASS (parser: BOM, CRLF, long lines, duplicates, malformed fields, locale-independent numbers; refused table logged loudly once and the engine's hit test kept; "<<checks<<" checks)\n";return;
    }
    if(variant=="r16-hits-absent") {
        // No table (a build before the fix): the engine's own boxes, unchanged - the bar's top rows and both ends are dead.
        for(const auto& [w,h]:screens) {
            const auto& a=layout[1];const auto c=columns(a,w,h),r=rows(a,h);
            assert(ask(mouse,hub.data(),(c.first+c.last)/2,r.first,w,h).answered=="");   // top edge of "code": dead (the 060p report)
            assert(ask(mouse,hub.data(),c.first,(r.first+r.last)/2,w,h).answered=="");   // left end: dead
            const int mx=px_of_view(60,w,h),my=py_of_view(232,h);int x,y;engine_point(mx,my,w,h,x,y);
            const auto mid=ask(mouse,hub.data(),mx,my,w,h);
            assert(mid.answered=="code" && mid.seen_x==x && mid.seen_y==y);
            checks+=3;
        }
        assert(once_logs.contains("online:hits-absent"));
        std::cout<<"online menus "<<variant<<" PASS (no table: the engine's own hit test, points unchanged, logged; "<<checks<<" checks)\n";return;
    }
    if(variant=="r16-hits-refused") {
        write_table_text(table_text()+"lc_online bad 1 2 3\n"); // a malformed line refuses the whole table
        const auto& a=layout[1];const auto r=rows(a,720);const auto c=columns(a,1280,720);
        assert(ask(mouse,hub.data(),(c.first+c.last)/2,r.first,1280,720).answered=="");
        assert(once_logs.contains("online:hits-refused") && once_logs["online:hits-refused"].find("line "+std::to_string(std::size(layout)+2))!=std::string::npos);
        std::cout<<"online menus "<<variant<<" PASS (malformed table refused loudly: engine hit test kept)\n";return;
    }
    write_table_text(table_text());
    if(variant=="r16-hits-unit") {
        // No pixel from the window procedure (the fix's window filter not installed), a wheel message, or a stale pixel: the
        // engine's view-unit point, to half a unit. The unit cell whose centre lies in the bar answers; a unit wholly outside
        // does not. Each fallback reason is logged once.
        for(const unsigned message:{0x200u,0x20au}) for(const bool no_probe:{false,true}) {
            pixel_known=no_probe ? false : true;pixel_stale=!no_probe;   // false: no pixel at all; true: a pixel that disagrees
            for(const auto& a:layout) {
                if(std::string_view(a.menu)!="lc_online") continue;
                const int top=int(std::floor(a.T+0.5f)),bottom=int(std::floor(a.B+0.5f))+1,left=int(std::ceil(a.L-0.5f)),right=int(std::ceil(a.R-0.5f))-1;
                const int mx=(left+right)/2,my=(top+bottom)/2;
                auto unit=[&](int x,int y) { return ask_unit(mouse,hub.data(),x,y,message).answered; };
                *reinterpret_cast<void**>(hub.data()+0x324)=item_named("lc_online",a.name).mem.data();
                if(!(unit(mx,top)==a.name && unit(mx,bottom)==a.name && unit(left,my)==a.name && unit(right,my)==a.name))
                    std::cout<<"unit "<<a.name<<" top "<<unit(mx,top)<<" bottom "<<unit(mx,bottom)<<" left "<<unit(left,my)<<" right "<<unit(right,my)<<" ("<<mx<<","<<my<<" "<<left<<".."<<right<<" "<<bottom<<".."<<top<<")\n";
                assert(unit(mx,top)==a.name && unit(mx,bottom)==a.name && unit(left,my)==a.name && unit(right,my)==a.name);
                assert(unit(mx,top+1)!=a.name && unit(mx,bottom-1)!=a.name && unit(left-1,my)!=a.name && unit(right+1,my)!=a.name);
                checks+=8;
            }
        }
        pixel_known=true;pixel_stale=false;
        assert(once_logs.contains("online:hits-half-nomsg") && once_logs.contains("online:hits-half-mismatch"));
        once_logs.erase("online:hits-half-noprobe");online_menus::set_pointer_probe(nullptr);
        ask_unit(mouse,hub.data(),100,100);ask_unit(mouse,hub.data(),101,100);
        assert(once_logs.contains("online:hits-half-noprobe"));
        std::cout<<"online menus "<<variant<<" PASS (half-unit fallback: no pixel, wheel message, stale pixel; each reason logged; "<<checks<<" checks)\n";return;
    }
    if(variant=="r16-hits-orders") {
        // The walk order is whatever the name keys make it: the same answers under several key orders and both tree shapes
        // (a right-leaning chain, a balanced tree whose in-order successor climbs parents), over the whole screen.
        for(const bool balanced:{false,true}) for(unsigned seed=1;seed<=6;++seed) {
            balanced_tree=balanced;key_seed=seed;build_all();
            for(const auto& [w,h]:{std::pair{1280,720},std::pair{1024,768}}) { checks+=edge_checks(mouse,hub.data(),"lc_online",w,h,true,3);checks+=edge_checks(host_mouse,lobby_menu.data(),"host",w,h,true,3); }
            // The stray items sit in the walk too: whichever order, the bar wins where it holds the point.
            const auto c=columns(layout[1],1280,720),r=rows(layout[1],720);
            for(int px=c.first;px<=c.last;px+=3) { assert(ask(mouse,hub.data(),px,r.first+1,1280,720).answered=="code");++checks; }
        }
        balanced_tree=false;key_seed=1;build_all();
        std::cout<<"online menus "<<variant<<" PASS (12 key orders x chain / balanced item trees; retail walk order asserted; "<<checks<<" checks)\n";return;
    }
    for(const auto& [w,h]:screens) checks+=edge_checks(mouse,hub.data(),"lc_online",w,h);
    // A hidden item is not a target, and its bar is not either (the engine's shown test).
    { auto& code=item_named("lc_online","code");code.mem[0x54]|=4;
      assert(ask(mouse,hub.data(),px_of_view(60,1280,720),py_of_view(232,720),1280,720).answered=="");code.mem[0x54]&=~4; }
    // An unlisted focusable item that overlaps a bar: the bar's item still wins everywhere in its bar, and the stray still
    // answers away from every bar, at the unchanged point.
    { const auto c=columns(layout[1],1280,720),r=rows(layout[1],720);
      for(int px=c.first;px<=c.last;++px) for(int py=r.first;py<=r.last;py+=3) { assert(ask(mouse,hub.data(),px,py,1280,720).answered=="code");++checks; }
      const float width=384.0f*1280.0f/720.0f,left=-(width-512.0f)/2.0f;     // a pixel inside stray2's box (x 300..360, y 100..120)
      const auto stray2_hit=ask(mouse,hub.data(),int((330.0f-left)*1280.0f/width),int((384.0f-110.0f)*720.0f/384.0f),1280,720);
      assert(stray2_hit.answered=="stray2");++checks; }
    // A click on a bar's corner (outside the text box) is the engine's Accept for that row; on a prompt, its token's code.
    const int player=*reinterpret_cast<int*>(0x6f3920);
    auto* code=reinterpret_cast<int*>(0xa09f54+player*4);auto* raised=reinterpret_cast<unsigned char*>(0xa09fa0+player);
    { const auto c=columns(layout[0],1920,1080),r=rows(layout[0],1080);*code=0;*raised=0;
      assert(ask(mouse,hub.data(),c.first,r.first,1920,1080,0x202).answered=="host" && *code==4 && *raised==1); }
    put(item_named("lc_online","desctext3").mem.data(),0x9c,10u);
    *reinterpret_cast<void**>(hub.data()+0x324)=item_named("lc_online","desctext3").mem.data(); // the focused prompt: its bar is drawn
    { const auto c=columns(layout[9],1920,1080),r=rows(layout[9],1080);*code=0;*raised=0;
      assert(ask(mouse,hub.data(),c.last,r.first,1920,1080,0x202).answered=="desctext3" && *code==0x18 && *raised==1); }
    *reinterpret_cast<void**>(hub.data()+0x324)=nullptr;
    // A prompt hovered from the outer strip of its bar (not its text box): the hover index and the hover sound are the
    // prompt's, like a click's - once; the same spot again plays no second hover sound.
    { const auto c=columns(layout[8],1280,720),r=rows(layout[8],720);
      *reinterpret_cast<void**>(hub.data()+0x324)=item_named("lc_online","desctext2").mem.data(); // its bar is the drawn one (it overlaps desctext1's)
      const auto first=ask(mouse,hub.data(),c.first,r.first,1280,720);
      if(!(first.answered=="desctext2" && first.hover==1 && first.hover_sounds==1)) std::cout<<"hover: answered '"<<first.answered<<"' hover "<<first.hover<<" sounds "<<first.hover_sounds<<std::endl;
      assert(first.answered=="desctext2" && first.hover==1 && first.hover_sounds==1);
      pixel_x=c.first;pixel_y=r.first;screen_w=1280;screen_h=720;int x,y;engine_point(c.first,r.first,1280,720,x,y);
      rep=reply();phase=0;*reinterpret_cast<int*>(0x6e4b18)=1;mouse(hub.data(),nullptr,x,y,0,0x200,0);
      assert(rep.hover==1 && rep.hover_sounds==0);checks+=2;*reinterpret_cast<void**>(hub.data()+0x324)=nullptr; }
    // Retail menus: the same point reaches the engine unchanged (one item spanning the screen records what it was asked).
    { std::array<unsigned char,0xb0> all{};all.fill(0);put(all.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(item_vtable.data())));all[0x54]=8;
      const std::int16_t f[]{-2000,2000,4000,4000,0,0};std::memcpy(all.data()+0x70,f,sizeof f);
      items.push_back(fitem{});auto& everything=items.back();everything.menu="options";everything.name="everything";everything.flags=8;
      everything.mem=all;put(everything.mem.data(),0x24,99u);put(everything.mem.data(),0x9c,99u);text_of(99,"all");build_all();
      for(const auto& [w,h]:screens) for(int py=0;py<h;py+=37) for(int px=0;px<w;px+=41) {
          int x,y;engine_point(px,py,w,h,x,y);const auto got=ask(mouse,retail_menu.data(),px,py,w,h);
          assert(got.saw && got.seen_x==x && got.seen_y==y);++checks;
      }
      items.pop_back();build_all(); }
    // The skinned host lobby: its rows and prompts through the lobby's own mouse adapter (0x69f2f4) into the retail 0x5ba2b0.
    for(const auto& [w,h]:screens) checks+=edge_checks(host_mouse,lobby_menu.data(),"host",w,h);
    // The Kick picker (hidden, unlisted): a point inside its box and no bar's is never taken for a bar.
    { const auto c=columns(layout[13],1280,720),r=rows(layout[13],720);
      const auto picker=ask(host_mouse,lobby_menu.data(),c.first+40,r.first-70,1280,720);assert(picker.answered=="");++checks; }
    // Without the lobby skin marker the lobby is retail: the engine's own walk, points unchanged.
    { marker_present=false;build_all();
      const auto c=columns(layout[13],1280,720),r=rows(layout[13],720);int x,y;engine_point(c.first,r.first,1280,720,x,y);
      const auto got=ask(host_mouse,lobby_menu.data(),c.first,r.first,1280,720);
      assert(!got.saw || (got.seen_x==x && got.seen_y==y));
      marker_present=true;build_all(); }
    checks+=live_060r(mouse,host_mouse);
    std::cout<<"online menus "<<variant<<" PASS (hit boxes = drawn bars through the retail walk at 1280x720, 1920x1080, 960x720, 1440x1080, 1024x768; hub + host lobby; "
        <<checks<<" checks; first-pass hover and sound; unlisted items; retail menus and unskinned lobbies unchanged; fix K: focus models, "
        "labels, empty texts and the Leave confirm's panel in the walk, the 060r live probe pixels, the Leave confirm click, the diagnostic line)\n";
}
}
