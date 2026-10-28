// 0.6 preview fix E, item 1: the co-op screens' mouse hit boxes are their drawn focus bars (online_menus_hits_rules.hpp).
// Drives the INSTALLED hooks (base menu mouse slot 0x69e49c, the host lobby's 0x69f2f4) and the RETAIL item hit tests of the
// mapped XMen2.exe image: every fixture item is a real text item (vtable 0x6a0244), so its shown test (vt+4 = 0x5bd8f0) and its
// hit test (vt+0x74 = 0x5c6490 -> 0x5bc1b0, PtInRect through the image's own import slot) run as shipped. Only the engine's
// menu-level walk (0x5ae0a0 / the lobby's 0x5ba2b0: ask each shown, focusable item in turn, first true wins) is a stub, and
// the item fields are laid out exactly as 0x5bc530 does (ftol of the anchor and the local box). The bars and boxes below are
// the builder's (legends-classic online_menus.py hit_rows / scene_layout for lc_online and the host lobby skin).
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
    {"lc_online","desctext1",34.4f,20.8f,0,-13.6f,47.2f,0,22.4f,5.2f,93.6f,25.8f},
    {"lc_online","desctext2",84.8f,20.8f,0,-13.6f,44.8f,0,72.8f,5.2f,141.6f,25.8f},
    {"lc_online","desctext3",131.2f,20.8f,0,-13.6f,73.6f,0,119.2f,5.2f,216.8f,25.8f},
    {"lc_online","desctext4",207.2f,20.8f,0,-13.6f,51.2f,0,195.2f,5.2f,270.4f,25.8f},
    {"lc_online","retry",432,44,-10.4f,-11.2f,39.2f,1.6f,421.6f,32.8f,471.2f,45.6f},
    {"host","lc_copy_code",388.8f,256.8f,0,-12.8f,76.8f,0,376.8f,242,477.6f,261.8f},
    {"host","text_startgame",46.4f,66.4f,-12,-20,129.6f,4.8f,34.4f,46.8f,180,71.4f},
    {"host","text_kickplayer",196,66.4f,-12,-20,84,4.8f,184,46.8f,284,71.4f},
    {"host","text_leaveroom",300,66.4f,-12,-20,62.4f,4.8f,288,46.8f,366.4f,71.4f},
    {"host","lc_leave_yes",320,97.6f,0,-19.2f,46.4f,0,320,78.4f,366.4f,97.6f},
    {"host","lc_leave_cancel",372.8f,97.6f,0,-19.2f,48,0,372.8f,78.4f,420.8f,97.6f},
    {"host","lc_lobby_dismiss",425.6f,97.6f,0,-19.2f,52,0,425.6f,78.4f,477.6f,97.6f},
};
struct fake_item { const authored* a; std::array<unsigned char,0xb0> mem{}; };
std::vector<fake_item> items(std::size(layout));
std::array<unsigned char,0x1a00> retail_menu{},lobby_menu{};
std::array<unsigned char,0x100> lobby_marker_item{};
std::vector<char> strings(0x4000);
bool active=false;
std::string hit;                 // the item the engine walk answered with ("" none)
int got_x=0,got_y=0;            // the point the engine walk received
int pixel_x=0,pixel_y=0,screen_w=0,screen_h=0;
bool pixel_known=true;
bool __cdecl probe(int& x,int& y,int& w,int& h) { if(!pixel_known) return false; x=pixel_x;y=pixel_y;w=screen_w;h=screen_h;return true; }
void* __fastcall item_lookup(void* menu,void*,const char* name) {
    const char* screen=static_cast<char*>(menu)+0xc;
    if(menu==lobby_menu.data() && std::string_view(name)=="lc_lobby_v1") return lobby_marker_item.data();
    for(auto& i:items) if(std::string_view(i.a->menu)==screen && std::string_view(i.a->name)==name) return i.mem.data();
    return nullptr;
}
void* __fastcall item_lookup_unskinned(void* menu,void* edx,const char* name) {
    return std::string_view(name)=="lc_lobby_v1" ? nullptr : item_lookup(menu,edx,name);
}
void* __cdecl string_table() { return strings.data(); }
const char* __cdecl crt_strstr(const char* a,const char* b) { return std::strstr(a,b); }
int __cdecl crt_strncmp(const char* a,const char* b,std::size_t n) { return std::strncmp(a,b,n); }
// The engine's menu-level walk: every shown (vt+4 false), focusable (+0x54 bit 3) item is asked with its own vt+0x74.
std::uint32_t walk(void* menu,int x,int y,int flags,int message,int extra) {
    got_x=x;got_y=y;hit.clear();
    const char* screen=static_cast<char*>(menu)+0xc;
    for(auto& i:items) {
        if(std::string_view(i.a->menu)!=screen) continue;
        void* p=i.mem.data();const auto vt=*reinterpret_cast<std::uintptr_t**>(p);
        if(reinterpret_cast<bool(__thiscall*)(void*)>(vt[1])(p) || !(i.mem[0x54]&8)) continue;
        if(reinterpret_cast<bool(__thiscall*)(void*,int,int,int,int,int)>(vt[0x74/4])(p,x,y,flags,message,extra)) { hit=i.a->name;return 1; }
    }
    return 0;
}
std::uint32_t __fastcall base_walk(void* menu,void*,int x,int y,int flags,int message,int extra) { return walk(menu,x,y,flags,message,extra); }
std::uint32_t __fastcall lobby_walk(void* menu,void*,int x,int y,int flags,int message,int extra) { return walk(menu,x,y,flags,message,extra); }
int ftol(float v) { return static_cast<int>(v); } // _ftol2 (0x67217c): truncation
void lay_out(fake_item& i,int id) {
    i.mem.fill(0);put(i.mem.data(),0,0x6a0244);i.mem[0x54]=8;
    put(i.mem.data(),0x24,unsigned(id));put(i.mem.data(),0x9c,unsigned(id));
    const auto& a=*i.a;
    const std::int16_t x78=std::int16_t(ftol(a.l)),y7a=std::int16_t(ftol(a.t));
    const std::int16_t f[]{std::int16_t(ftol(a.ax)+x78),std::int16_t(ftol(a.az)+y7a),std::int16_t(ftol(a.r-a.l)),std::int16_t(ftol(a.t-a.b)),x78,y7a};
    std::memcpy(i.mem.data()+0x70,f,sizeof f);
}
void text_of(int id,const char* text) {
    const int offset=id*64;std::memcpy(strings.data()+4+id*4,&offset,4);std::strcpy(strings.data()+0x1008+offset,text);
}
std::string table_text(bool lobby_rows=true) {
    std::string t=std::string(H::contract)+"\n";char line[160];
    for(const auto& a:layout) {
        if(!lobby_rows && std::string_view(a.menu)=="host") continue;
        std::snprintf(line,sizeof line,"%s %s %g %g %g %g\n",a.menu,a.name,a.L,a.B,a.R,a.T);t+=line;
    }
    return t;
}
void write_table(const std::string& text) {
    std::ofstream(root/"Data/lc_online_hits.txt",std::ios::binary)<<text;++asset_generation;
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
std::string at_pixel(mouse_t mouse,void* menu,int px,int py,int w,int h,unsigned message=0x200) {
    pixel_x=px;pixel_y=py;screen_w=w;screen_h=h;int x,y;engine_point(px,py,w,h,x,y);
    mouse(menu,nullptr,x,y,0,int(message),0);return hit;
}
// The first listed bar holding the pixel's centre (the drawn bar: Direct3D's top-left fill rule), "" for none.
// Overlapping bars (the prompts reach into each other): the focused item's bar - the one on screen - wins; with none of them
// focused, the item whose own text box (the engine's 0x5bc1b0 rectangle, from the laid-out fields) is nearest the point.
float box_distance(const fake_item& i,float x,float y) {
    std::int16_t f[6];std::memcpy(f,i.mem.data()+0x70,sizeof f);
    const float left=f[0],right=float(f[0]+f[2]-1),low=float(f[1]-f[3]-1+f[5]/2),high=float(f[1]+f[5]/2);
    const float dx=std::max({left-x,0.0f,x-right}),dy=std::max({low-y,0.0f,y-high});return dx*dx+dy*dy;
}
std::string expected(void* menu,int px,int py,int w,int h) {
    const char* screen=static_cast<char*>(menu)+0xc;
    const void* focused=*reinterpret_cast<void**>(static_cast<char*>(menu)+0x324);
    const float x=view_x(px,w,h),y=view_y(py,h);
    const fake_item* best=nullptr;
    for(const auto& i:items) {
        const auto& a=*i.a;
        if(std::string_view(a.menu)!=screen || !(x>=a.L && x<a.R && y>a.B && y<=a.T)) continue;
        if(i.mem.data()==focused) return a.name;
        if(!best || box_distance(i,x,y)<box_distance(*best,x,y)) best=&i;
    }
    return best ? best->a->name : "";
}
struct span { int first=-1,last=-1; };
span columns(const authored& a,int w,int h) { span s;for(int px=0;px<w;++px) { const float x=view_x(px,w,h); if(x>=a.L && x<a.R) { if(s.first<0) s.first=px; s.last=px; } } return s; }
span rows(const authored& a,int h) { span s;for(int py=0;py<h;++py) { const float y=view_y(py,h); if(y>a.B && y<=a.T) { if(s.first<0) s.first=py; s.last=py; } } return s; }
unsigned edge_checks(mouse_t mouse,void* menu,const char* name,int w,int h) {
    unsigned n=0;
    auto*& focus=*reinterpret_cast<void**>(static_cast<char*>(menu)+0x324);
    for(auto& i:items) {
        const auto& a=*i.a;
        if(std::string_view(a.menu)!=name) continue;
        focus=i.mem.data(); // its bar is the one drawn
        const auto c=columns(a,w,h),r=rows(a,h);assert(c.first>0 && r.first>0 && c.last<w-1 && r.last<h-1);
        const int mx=(c.first+c.last)/2,my=(r.first+r.last)/2;
        // Every pixel of the bar's four edges hits its item; the pixel one past each edge does not.
        for(int px=c.first;px<=c.last;++px) {
            assert(at_pixel(mouse,menu,px,r.first,w,h)==a.name);assert(at_pixel(mouse,menu,px,r.last,w,h)==a.name);
            assert(at_pixel(mouse,menu,px,r.first-1,w,h)!=a.name);assert(at_pixel(mouse,menu,px,r.last+1,w,h)!=a.name);n+=4;
        }
        for(int py=r.first;py<=r.last;++py) {
            assert(at_pixel(mouse,menu,c.first,py,w,h)==a.name);assert(at_pixel(mouse,menu,c.last,py,w,h)==a.name);
            assert(at_pixel(mouse,menu,c.first-1,py,w,h)!=a.name);assert(at_pixel(mouse,menu,c.last+1,py,w,h)!=a.name);n+=4;
        }
        assert(at_pixel(mouse,menu,mx,my,w,h)==a.name);
        // The four corners, and one pixel diagonally outside each.
        for(const auto [x0,y0,dx,dy]:{std::array{c.first,r.first,-1,-1},std::array{c.last,r.first,1,-1},std::array{c.first,r.last,-1,1},std::array{c.last,r.last,1,1}}) {
            assert(at_pixel(mouse,menu,x0,y0,w,h)==a.name);assert(at_pixel(mouse,menu,x0+dx,y0+dy,w,h)!=a.name);n+=2;
        }
    }
    // Coarse whole-screen scan, with no focus and with each item focused: the answer is always the bar rule's, or nothing.
    focus=nullptr;
    for(int py=0;py<h;py+=5) for(int px=0;px<w;px+=7) { assert(at_pixel(mouse,menu,px,py,w,h)==expected(menu,px,py,w,h));++n; }
    for(auto& i:items) {
        if(std::string_view(i.a->menu)!=name) continue;
        focus=i.mem.data();
        for(int py=0;py<h;py+=23) for(int px=0;px<w;px+=19) { assert(at_pixel(mouse,menu,px,py,w,h)==expected(menu,px,py,w,h));++n; }
    }
    focus=nullptr;
    return n;
}
void run(const std::string& variant) {
    const auto mouse=*reinterpret_cast<mouse_t*>(0x69e49c);
    const auto host_mouse=*reinterpret_cast<mouse_t*>(0x69f2f4);
    assert(reinterpret_cast<std::uintptr_t>(mouse)!=0x5ae0a0); // the installed hook, not the retail slot
    redirect(0x5ae0a0,reinterpret_cast<const void*>(&base_walk));redirect(0x5ba2b0,reinterpret_cast<const void*>(&lobby_walk));
    redirect(0x602200,reinterpret_cast<const void*>(&string_table));redirect(0x5adc10,reinterpret_cast<const void*>(&item_lookup));
    // The image's own imports the item hit tests call: PtInRect, and the click path's strstr / strncmp (jump stubs 0x67220a /
    // 0x6723bc).
    put(reinterpret_cast<void*>(0x67f230),0,unsigned(reinterpret_cast<std::uintptr_t>(&PtInRect)));
    put(reinterpret_cast<void*>(0x67f130),0,unsigned(reinterpret_cast<std::uintptr_t>(&crt_strstr)));
    put(reinterpret_cast<void*>(0x67f0f8),0,unsigned(reinterpret_cast<std::uintptr_t>(&crt_strncmp)));
    for(std::size_t i=0;i<items.size();++i) { items[i].a=&layout[i];lay_out(items[i],int(i)+1);text_of(int(i)+1,layout[i].name); }
    text_of(10,"$MENU_OTHER Change name"); // desctext3's prompt: a click raises the token's code, not Accept
    put(lobby_menu.data(),0,0x69f274);lobby_menu[4]=1;std::strcpy(reinterpret_cast<char*>(lobby_menu.data()+0xc),"host");
    put(retail_menu.data(),0,0x69e41c);retail_menu[4]=1;std::strcpy(reinterpret_cast<char*>(retail_menu.data()+0xc),"options");
    online_menus::set_pointer_probe(&probe);
    unsigned checks=0;
    const std::pair<int,int> screens[]{{1280,720},{1920,1080},{960,720},{1440,1080},{1024,768}};
    if(variant=="r16-hits-absent") {
        // No table (a build before the fix): the engine's own boxes, unchanged - the bar's top rows and both ends are dead.
        for(const auto& [w,h]:screens) {
            const auto& a=layout[1];const auto c=columns(a,w,h),r=rows(a,h);
            assert(at_pixel(mouse,hub.data(),(c.first+c.last)/2,r.first,w,h)=="");   // top edge of "code": dead (the 060p report)
            assert(at_pixel(mouse,hub.data(),c.first,(r.first+r.last)/2,w,h)=="");   // left end: dead
            int x,y;engine_point((c.first+c.last)/2,(r.first+r.last)/2,w,h,x,y);
            assert(at_pixel(mouse,hub.data(),(c.first+c.last)/2,(r.first+r.last)/2,w,h)=="code" && got_x==x && got_y==y);
            checks+=3;
        }
        std::cout<<"online menus "<<variant<<" PASS (no table: the engine's own hit test, points unchanged; "<<checks<<" checks)\n";return;
    }
    if(variant=="r16-hits-refused") {
        write_table(table_text()+"lc_online bad 1 2 3\n"); // a malformed line refuses the whole table
        const auto& a=layout[1];const auto r=rows(a,720);const auto c=columns(a,1280,720);
        assert(at_pixel(mouse,hub.data(),(c.first+c.last)/2,r.first,1280,720)=="");
        std::cout<<"online menus "<<variant<<" PASS (malformed table refused: engine hit test kept)\n";return;
    }
    write_table(table_text());
    if(variant=="r16-hits-unit") {
        // No pixel from the window procedure (the fix's window filter not installed): the engine's view-unit point, to half a
        // unit. The unit cell whose centre lies in the bar hits; a unit wholly outside misses.
        pixel_known=false;
        for(const auto& a:layout) {
            if(std::string_view(a.menu)!="lc_online") continue;
            const int top=int(std::floor(a.T+0.5f)),bottom=int(std::floor(a.B+0.5f))+1,left=int(std::ceil(a.L-0.5f)),right=int(std::ceil(a.R-0.5f))-1;
            const int mx=(left+right)/2,my=(top+bottom)/2;
            auto unit=[&](int x,int y) { mouse(hub.data(),nullptr,x,y,0,0x200,0);return hit; };
            *reinterpret_cast<void**>(hub.data()+0x324)=items[std::size_t(&a-layout)].mem.data();
            if(!(unit(mx,top)==a.name && unit(mx,bottom)==a.name && unit(left,my)==a.name && unit(right,my)==a.name))
                std::cout<<"unit "<<a.name<<" top "<<unit(mx,top)<<" bottom "<<unit(mx,bottom)<<" left "<<unit(left,my)<<" right "<<unit(right,my)<<" ("<<mx<<","<<my<<" "<<left<<".."<<right<<" "<<bottom<<".."<<top<<")\n";
            assert(unit(mx,top)==a.name && unit(mx,bottom)==a.name && unit(left,my)==a.name && unit(right,my)==a.name);
            assert(unit(mx,top+1)!=a.name && unit(mx,bottom-1)!=a.name && unit(left-1,my)!=a.name && unit(right+1,my)!=a.name);
            checks+=8;
        }
        pixel_known=true;
        std::cout<<"online menus "<<variant<<" PASS (view-unit fallback: "<<checks<<" checks)\n";return;
    }
    for(const auto& [w,h]:screens) checks+=edge_checks(mouse,hub.data(),"lc_online",w,h);
    // A hidden item is not a target, and its bar is not either (the engine's shown test).
    items[1].mem[0x54]|=4;
    { const auto c=columns(layout[1],1280,720),r=rows(layout[1],720); assert(at_pixel(mouse,hub.data(),(c.first+c.last)/2,(r.first+r.last)/2,1280,720)==""); }
    items[1].mem[0x54]&=~4;
    // A click on a bar's corner (outside the text box) is the engine's Accept for that row; on a prompt, its token's code.
    const int player=*reinterpret_cast<int*>(0x6f3920);
    auto* code=reinterpret_cast<int*>(0xa09f54+player*4);auto* raised=reinterpret_cast<unsigned char*>(0xa09fa0+player);
    { const auto c=columns(layout[0],1920,1080),r=rows(layout[0],1080);*code=0;*raised=0;
      assert(at_pixel(mouse,hub.data(),c.first,r.first,1920,1080,0x202)=="host" && *code==4 && *raised==1); }
    put(items[9].mem.data(),0x9c,10u);*reinterpret_cast<void**>(hub.data()+0x324)=items[9].mem.data(); // the focused prompt: its bar is drawn
    { const auto c=columns(layout[9],1920,1080),r=rows(layout[9],1080);*code=0;*raised=0;
      assert(at_pixel(mouse,hub.data(),c.last,r.first,1920,1080,0x202)=="desctext3" && *code==0x18 && *raised==1); }
    // Retail menus: the same point reaches the engine unchanged.
    for(const auto& [w,h]:screens) for(int py=0;py<h;py+=37) for(int px=0;px<w;px+=41) {
        int x,y;engine_point(px,py,w,h,x,y);pixel_x=px;pixel_y=py;screen_w=w;screen_h=h;
        mouse(retail_menu.data(),nullptr,x,y,0,0x200,0);assert(got_x==x && got_y==y);++checks;
    }
    // The skinned host lobby: its rows and prompts through the lobby's own mouse adapter (0x69f2f4).
    for(const auto& [w,h]:screens) checks+=edge_checks(host_mouse,lobby_menu.data(),"host",w,h);
    // Without the lobby skin marker the lobby is retail: points unchanged.
    { const auto c=columns(layout[13],1280,720),r=rows(layout[13],720);int x,y;engine_point(c.first,r.first,1280,720,x,y);
      redirect(0x5adc10,reinterpret_cast<const void*>(&item_lookup_unskinned));
      pixel_x=c.first;pixel_y=r.first;screen_w=1280;screen_h=720;
      host_mouse(lobby_menu.data(),nullptr,x,y,0,0x200,0);assert(got_x==x && got_y==y);
      redirect(0x5adc10,reinterpret_cast<const void*>(&item_lookup)); }
    std::cout<<"online menus "<<variant<<" PASS (hit boxes = drawn bars at 1280x720, 1920x1080, 960x720, 1440x1080, 1024x768; hub + host lobby; "
        <<checks<<" checks; retail menus and unskinned lobbies unchanged)\n";
}
}
