// Invented Alchemy services around the REAL playfield loader, recursive walk,
// vector push, table insertion, string/name lookup, and unload routines.
namespace anchor_fixture {
struct node { unsigned* vt=nullptr;int kind=0,id=0;node* child=nullptr;char name[64]{}; };
struct resource { unsigned* vt=nullptr;node* top=nullptr; };
std::array<unsigned,40> vt{};
std::array<unsigned,8> resource_vt{};
std::vector<node> nodes;
std::vector<resource> resources;
node root_node,key;
std::array<unsigned,4> constructor_vt{};
unsigned* constructor_context=constructor_vt.data();
std::array<unsigned,6> scene_info{};
std::array<unsigned char,0x1008+1024*64> strings{};
unsigned intern_count=0,refs=0,unrefs=0,live_count=0;
unsigned property_id=0;
std::map<std::string,unsigned> interned;
std::map<void*,int> ownership;
void __cdecl ref(void* p) { if(p) { ++refs;++ownership[p]; } }
void __cdecl unref(void* p) { if(p) { ++unrefs;--ownership[p]; } }
void* __cdecl user(void* p) { auto* n=static_cast<node*>(p);return n && n->kind==1 ? p : nullptr; }
void* __cdecl group(void* p) { return p==&root_node ? p : nullptr; }
void* __cdecl transform(void* p) { auto* n=static_cast<node*>(p);return n && n->kind==2 ? p : nullptr; }
int __fastcall size(void*,void*) { return 0; }
void* __fastcall child(void* p,void*,int) { return static_cast<node*>(p)->child; }
int __fastcall children(void*,void*) { return int(live_count); }
void* __fastcall at_child(void*,void*,int i) {
    assert(i>=0 && unsigned(i)<live_count && 2*size_t(i)+1<nodes.size());
    return &nodes[2*size_t(i)];
}
void* __cdecl property(void* p,const char*) { return p; }
void* __cdecl identity(void* p) { return p; }
void* __fastcall identity_this(void* p,void*) { return p; }
const char* __fastcall text(void* p,void*) {
    auto* n=static_cast<node*>(p);
    if(n->kind==1) property_id=unsigned(n->id);
    return n->name;
}
void __fastcall ignored_text(void*,void*,const char*) {}
void* __fastcall named_property(void* p,void*,void*) { return p; }
void* __cdecl string_key(void* out,int) { ref(&key);*static_cast<void**>(out)=&key;return out; }
void* __cdecl string_pool() { return strings.data(); }
unsigned __fastcall intern(void*,void*,const char* name,int,int) {
    if(const auto it=interned.find(name);it!=interned.end()) return it->second;
    const auto id=++intern_count;assert(id<1024);
    const auto offset=id*64;
    put(strings.data(),4+id*4,offset);
    std::strcpy(reinterpret_cast<char*>(strings.data()+0x1008+offset),name);
    interned.emplace(name,id);
    return id;
}
void __fastcall noop(void*,void*) {}
void __fastcall noop_pointer(void*,void*,void*) {}
void __cdecl noop_cdecl(void*) {}
// The real loader resolves a model resource for each ItemName property. These
// stand in for Alchemy/resource services, while 0x5882e0 and 0x587ba0 run intact.
void* __cdecl resource_context() { return &root_node; }
void* __cdecl load_resource(void** out,void*) {
    *out=&resources.at(property_id);ref(*out);return out;
}
void __cdecl resource_parameters(void*,const char*,void*) {}
void* __fastcall resource_root(void* p,void*) { return static_cast<resource*>(p)->top; }
void __fastcall matrix(void*,void*,void*,int) {}
void __cdecl release_resource(void** p) { unref(*p);*p=nullptr; }
void* __cdecl make_scene_root(void** out,int type) {
    assert(type==3);out[0]=&root_node;out[1]=nullptr;ref(&root_node);return out;
}
void* __cdecl scene_context() { return &constructor_context; }
void __fastcall scene_parent(void*,void*,void** out) { *out=nullptr; }
void __cdecl scene_bounds(void*,void*,void*,void*) {}
void install_services() {
    auto imp=[](unsigned va,const void* fn) { put(reinterpret_cast<void*>(va),0,unsigned(reinterpret_cast<std::uintptr_t>(fn))); };
    imp(0x67f65c,ref);imp(0x67f660,unref);imp(0x67fc60,user);imp(0x67fc70,group);
    imp(0x67fdac,transform);imp(0x67fc64,text);imp(0x67f548,ignored_text);
    imp(0x67fe5c,children);imp(0x67fe60,at_child);imp(0x67fc18,ignored_text);
    imp(0x67fc58,identity_this);imp(0x67fc5c,identity);
    // getName returns const char*, not an object pointer (owned PE import name).
    imp(0x67f668,text);imp(0x67fdc8,identity_this);imp(0x67fdcc,identity_this);
    imp(0x67fcb8,noop_pointer);
    imp(0x67fdc4,noop_pointer);
    redirect(0x58e9b0,property);redirect(0x58e760,identity);
    redirect(0x587ef0,string_key);redirect(0x602200,string_pool);redirect(0x41a460,intern);
    redirect(0x5875a0,noop);redirect(0x575980,release_resource);
    redirect(0x57a090,resource_context);redirect(0x575c90,load_resource);
    redirect(0x5647f0,resource_parameters);
    redirect(0x57bfc0,make_scene_root);redirect(0x582810,scene_context);
    redirect(0x58e900,scene_bounds);
    redirect(0x571180,noop);redirect(0x571100,noop);
    redirect(0x672170,reinterpret_cast<const void*>(&std::strncpy));
    redirect(0x672562,reinterpret_cast<const void*>(&std::strcmp));
    redirect(0x672161,noop); // cookie verifier, not a capacity operation
    vt[2]=unsigned(reinterpret_cast<std::uintptr_t>(&identity_this));
    vt[3]=unsigned(reinterpret_cast<std::uintptr_t>(&matrix));
    vt[0x54/4]=unsigned(reinterpret_cast<std::uintptr_t>(&size));
    vt[0x58/4]=unsigned(reinterpret_cast<std::uintptr_t>(&child));
    vt[0x8c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&named_property));
    resource_vt[2]=unsigned(reinterpret_cast<std::uintptr_t>(&identity_this));
    resource_vt[3]=unsigned(reinterpret_cast<std::uintptr_t>(&noop_pointer));
    resource_vt[4]=unsigned(reinterpret_cast<std::uintptr_t>(&resource_root));
    constructor_vt[3]=unsigned(reinterpret_cast<std::uintptr_t>(&scene_parent));
}
void construct(unsigned char* field) {
    std::memset(field,0xcc,0x2000);
    reinterpret_cast<void*(__fastcall*)(void*,void*,void*,void*,void*,void*)>(0x588780)(field,nullptr,scene_info.data(),nullptr,nullptr,nullptr);
    assert(*reinterpret_cast<unsigned*>(field+0x107c)==0 && *reinterpret_cast<unsigned*>(field+0x18c0)==0);
    for(unsigned i=0;i<16;++i) {
        assert(*reinterpret_cast<unsigned*>(field+0x103c+i*4)==0);
        assert(*reinterpret_cast<unsigned*>(field+0x1880+i*4)==0);
    }
}
void make_nodes(unsigned n) {
    nodes.assign(2*n,{});root_node={};key={};
    live_count=n;
    resources.assign(n,{});
    std::strcpy(key.name,"ItemName");
    for(unsigned i=0;i<n;++i) {
        auto& u=nodes[2*i];auto& t=nodes[2*i+1];
        u.vt=t.vt=vt.data();u.kind=1;t.kind=2;u.id=t.id=int(i);u.child=&t;
        resources[i]={resource_vt.data(),&t};
        std::snprintf(u.name,sizeof(u.name),"synthetic_%03u",i);
        std::strcpy(t.name,u.name);
    }
}
LONG CALLBACK fault(EXCEPTION_POINTERS* e) {
    if(e->ExceptionRecord->ExceptionCode==EXCEPTION_STACK_OVERFLOW) {
        std::cerr<<"anchor stack overflow eip="<<std::hex<<e->ContextRecord->Eip<<" esp="<<e->ContextRecord->Esp<<"\n";ExitProcess(97);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void run() {
    AddVectoredExceptionHandler(1,fault);
    ULONG guarantee=65536;SetThreadStackGuarantee(&guarantee);
    std::cout<<"anchor services\n";install_services();
    const auto manager=*reinterpret_cast<unsigned*>(0x5889a3);
    auto* backing=reinterpret_cast<unsigned char*>(manager);
    // Slot storage is poisoned outside the complete expanded object. Every
    // loader run must leave the padding/canaries and all other slots untouched.
    for(unsigned cycle=0;cycle<2;++cycle) {
        const unsigned counts[]{512,1,175,337,513};
        for(unsigned slot=0;slot<5;++slot) {
            const auto n=counts[(slot+cycle)%5];std::cout<<"anchor slot "<<slot<<" count "<<n<<"\n";make_nodes(n);intern_count=0;strings.fill(0);interned.clear();ownership.clear();
            auto* field=backing+slot*0x2000;
            construct(field);
            const auto refs_before=refs,unrefs_before=unrefs;
            reinterpret_cast<void(__fastcall*)(void*,void*,const char*)>(0x5882e0)(field,nullptr,"synthetic");
            std::cout<<"anchor loaded\n";
            const auto expected=n<=512 ? n : 0;
            assert(*reinterpret_cast<unsigned*>(field+0x18c0)==expected);
            assert(*reinterpret_cast<unsigned*>(field+0x107c)==expected);
            assert(reinterpret_cast<unsigned(__fastcall*)(void*,void*)>(0x588760)(field,nullptr)==expected);
            for(unsigned i=0;i<expected;++i) {
                // Retail walks group children backwards. Name lookup must
                // return each original anchor's index and bound transform.
                unsigned index=~0u;void* bound=nullptr;
                assert(reinterpret_cast<bool(__fastcall*)(void*,void*,const char*,unsigned*)>(0x587b00)(field+0x3c,nullptr,nodes[2*i].name,&index));
                assert(index==n-1-i);
                assert(reinterpret_cast<bool(__fastcall*)(void*,void*,const char*,void**)>(0x587a60)(field+0x3c,nullptr,nodes[2*i].name,&bound));
                assert(bound==&resources[i]);
                assert(*reinterpret_cast<void**>(field+0x1080+index*4)==&nodes[2*i+1]);
                void* indexed=nullptr;
                assert(reinterpret_cast<bool(__fastcall*)(void*,void*,unsigned,void**)>(0x587d50)(field,nullptr,index,&indexed));
                assert(indexed==bound);
                assert(std::strcmp(reinterpret_cast<const char*(__fastcall*)(void*,void*,unsigned)>(0x587d90)(field,nullptr,index),nodes[2*i].name)==0);
            }
            unsigned missing=~0u;
            assert(!reinterpret_cast<bool(__fastcall*)(void*,void*,const char*,unsigned*)>(0x587b00)(field+0x3c,nullptr,"missing",&missing));
            for(unsigned i=0x18c4;i<0x2000;++i) assert(field[i]==0xcc);
            reinterpret_cast<void(__fastcall*)(void*,void*)>(0x588640)(field,nullptr);
            // Real vector and name destructors clear all sixteen words.
            for(unsigned i=0;i<16;++i) {
                assert(*reinterpret_cast<unsigned*>(field+0x1880+i*4)==0);
                assert(*reinterpret_cast<unsigned*>(field+0x103c+i*4)==0);
            }
            // Services do not dereference null refs. The unload includes the
            // header's three null fields and root; table ownership is checked
            // independently by the exact expanded vector contents above.
            assert(refs>refs_before && unrefs>unrefs_before);
            for(const auto& [object,balance]:ownership) { (void)object;assert(balance==0); }
        }
    }
    // Exercise the relocated manager itself, including both pointer/stride
    // division sites and generation handles, with all five slots occupied.
    reinterpret_cast<void*(__fastcall*)(void*,void*)>(0x588b80)(backing,nullptr);
    make_nodes(513);intern_count=0;strings.fill(0);interned.clear();ownership.clear();
    std::array<unsigned,5> slots{},old_handles{};
    const unsigned simultaneous[]{512,1,175,337,513};
    for(unsigned round=0;round<2;++round) {
        for(unsigned i=0;i<5;++i) {
            const auto slot=reinterpret_cast<unsigned(__fastcall*)(void*,void*)>(0x587e90)(backing,nullptr);
            assert(slot<5 && (i==0 || std::find(slots.begin(),slots.begin()+i,slot)==slots.begin()+i));slots[i]=slot;
            auto& occupied=*reinterpret_cast<unsigned*>(backing+0xa000);occupied|=1u<<slot;
            auto* field=backing+slot*0x2000;
            construct(field);
            live_count=simultaneous[(i+round)%5];
            reinterpret_cast<void(__fastcall*)(void*,void*,const char*)>(0x5882e0)(field,nullptr,"synthetic");
            const auto n=live_count<=512 ? live_count : 0;
            assert(*reinterpret_cast<unsigned*>(field+0x107c)==n);
            if(n) {
                void* bound=nullptr;
                assert(reinterpret_cast<bool(__fastcall*)(void*,void*,const char*,void**)>(0x587a60)(field+0x3c,nullptr,nodes[2*(n-1)].name,&bound));
                assert(bound==&resources[n-1]);
            }
            for(unsigned b=0x18c4;b<0x2000;++b) assert(field[b]==0xcc);
            const auto handle=*reinterpret_cast<unsigned*>(backing+0xa030+slot*4);
            if(round) assert(handle!=old_handles[slot]);old_handles[slot]=handle;
            assert(reinterpret_cast<bool(__fastcall*)(void*,void*,void*)>(0x588b20)(backing,nullptr,field));
        }
        assert(*reinterpret_cast<unsigned*>(backing+0xa024)==0 && *reinterpret_cast<unsigned*>(backing+0xa02c)==5);
        // Invalid addresses must neither alias an occupied slot nor mutate the
        // allocator. Use integer addresses to avoid out-of-object C++ arithmetic.
        const std::vector<unsigned char> before(backing,backing+0xa04c);
        const int invalid_offsets[]{-0x4000,-0x2001,-0x2000,-0x1fff,-1,1,
                                    0x1fff,0x2001,0x9fff,0xa000,0xa001,0xc000};
        for(const int offset:invalid_offsets) {
            auto* invalid=reinterpret_cast<void*>(manager+unsigned(offset));
            assert(!reinterpret_cast<bool(__fastcall*)(void*,void*,void*)>(0x588b20)(backing,nullptr,invalid));
            reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x588c20)(backing,nullptr,invalid);
            assert(std::memcmp(backing,before.data(),before.size())==0);
        }
        assert(!reinterpret_cast<bool(__fastcall*)(void*,void*,void*)>(0x588b20)(backing,nullptr,nullptr));
        reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x588c20)(backing,nullptr,nullptr);
        assert(std::memcmp(backing,before.data(),before.size())==0);
        for(const auto slot:slots) {
            auto* field=backing+slot*0x2000;
            reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x588c20)(backing,nullptr,field);
            assert(!reinterpret_cast<bool(__fastcall*)(void*,void*,void*)>(0x588b20)(backing,nullptr,field));
        }
        assert(*reinterpret_cast<unsigned*>(backing+0xa024)==5 && *reinterpret_cast<unsigned*>(backing+0xa02c)==0);
        for(const auto& [object,balance]:ownership) { (void)object;assert(balance==0); }
    }
    std::cout<<"512 anchors PASS: real constructor/loader/walk/push/name insertion/lookups/unload; 513 refused; five sequential and simultaneous slots, reuse/handle generations; negative/past-end/misaligned pointers refused without mutation; padding/refcounts intact\n";
}
// Runs the menu manager's release call, rather than invoking the pool directly.
std::array<unsigned char,0x87000> close_manager{};
std::array<unsigned,0x100> close_vt{};
void* __cdecl close_manager_get() { return close_manager.data(); }
void __fastcall erase_resource(void* table,void*,unsigned index) {
    assert(table==close_manager.data()+4 && index==0);
    put(close_manager.data(),8,0x3fffffff);
}
void close_cycles() {
    install_services();
    redirect(0x5d8920,close_manager_get);
    redirect(0x601e50,erase_resource);
    close_vt[0x168/4]=0x5d78b0;
    const auto manager=*reinterpret_cast<unsigned*>(0x5889a3);
    auto* backing=reinterpret_cast<unsigned char*>(manager);
    reinterpret_cast<void*(__fastcall*)(void*,void*)>(0x588b80)(backing,nullptr);
    for(unsigned cycle=0;cycle<40;++cycle) {
        // Live scenes span small code/hub pages through the 214-anchor Browse.
        for(const unsigned count:{32u,64u,214u,337u,512u}) {
            make_nodes(count);intern_count=0;strings.fill(0);interned.clear();ownership.clear();
            const auto slot=reinterpret_cast<unsigned(__fastcall*)(void*,void*)>(0x587e90)(backing,nullptr);
            *reinterpret_cast<unsigned*>(backing+0xa000)|=1u<<slot;
            auto* field=backing+slot*0x2000;construct(field);
            reinterpret_cast<void(__fastcall*)(void*,void*,const char*)>(0x5882e0)(field,nullptr,"synthetic");
            close_manager.fill(0);
            put(close_manager.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(close_vt.data())));
            put(close_manager.data(),8,0);
            put(close_manager.data(),0x14,0x3fffffff);
            put(close_manager.data(),0xcc,unsigned(reinterpret_cast<std::uintptr_t>(field)));
            put(close_manager.data(),0xd0,1);
            std::array<unsigned char,0x1a00> menu{};
            put(menu.data(),0,0x69e41c);put(menu.data(),0x828,0x3fffffff);
            put(menu.data(),0xd4,unsigned(reinterpret_cast<std::uintptr_t>(field)));put(menu.data(),0xe0,1);
            put(menu.data(),0xe4,*reinterpret_cast<unsigned*>(0x69d058));
            put(menu.data(),0xe8,*reinterpret_cast<unsigned*>(0x69d058));
            // Retail CMenu::close -> manager vt+168 -> 588d00 -> pool free -> real destructor.
            reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5af5a0)(menu.data(),nullptr);
            assert(*reinterpret_cast<unsigned*>(backing+0xa024)==5);
            assert(*reinterpret_cast<unsigned*>(backing+0xa02c)==0);
            assert(*reinterpret_cast<unsigned*>(close_manager.data()+0xcc)==0);
            assert(*reinterpret_cast<unsigned*>(menu.data()+0xe0)==0);
            for(const auto& [object,balance]:ownership) { (void)object;assert(balance==0); }
        }
    }
    std::cout<<"menu close cycles PASS 200 opens/closes: retail 5af5a0 -> 5d78b0 -> 588d00 -> 588c20 -> 588640; pool free=5 used=0; references=0 after every close\n";
}

std::array<unsigned,0x100> model_manager_vt{};
std::array<unsigned,4> model_manager{};
resource replacement{},invisible{};
node replacement_node{},invisible_node{};
void* __cdecl model_manager_get() { return model_manager.data(); }
void __fastcall model_load(void*,void*,void** out,const char* name,int) {
    *out=std::string_view(name).find("m_invis")!=std::string_view::npos ? &invisible : &replacement;
    ref(*out);
}
float __fastcall model_time(void*,void*) { return 0; }
struct model_xml { const char* path;bool hidden; };
const char* __fastcall model_attribute(void* value,void*,const char*) { return static_cast<model_xml*>(value)->path; }
void __fastcall model_parse(void* value,void*,void* xml) {
    if(static_cast<model_xml*>(xml)->hidden) reinterpret_cast<void(__fastcall*)(void*,void*,bool)>(0x5c4b90)(value,nullptr,true);
}
void __fastcall model_note(void*,void*,const char*,int) {}
void model_cycles() {
    install_services();
    model_manager_vt[0x260/4]=unsigned(reinterpret_cast<std::uintptr_t>(model_load));
    model_manager_vt[0x24/4]=unsigned(reinterpret_cast<std::uintptr_t>(model_time));
    model_manager[0]=unsigned(reinterpret_cast<std::uintptr_t>(model_manager_vt.data()));
    redirect(0x5d8920,model_manager_get);redirect(0x5bc530,noop);redirect(0x5bc170,noop);
    redirect(0x5649c0,model_attribute);redirect(0x5bc7a0,model_parse);redirect(0x517c20,model_note);
    replacement_node.vt=invisible_node.vt=vt.data();replacement_node.kind=invisible_node.kind=2;
    replacement={resource_vt.data(),&replacement_node};invisible={resource_vt.data(),&invisible_node};
    const auto manager=*reinterpret_cast<unsigned*>(0x5889a3);
    auto* backing=reinterpret_cast<unsigned char*>(manager);
    for(unsigned cycle=0;cycle<100;++cycle) {
        make_nodes(1);intern_count=0;strings.fill(0);interned.clear();ownership.clear();
        construct(backing);
        reinterpret_cast<void(__fastcall*)(void*,void*,const char*)>(0x5882e0)(backing,nullptr,"synthetic");
        std::array<unsigned char,0x1a00> owner{};put(owner.data(),0,0x69e41c);put(owner.data(),0xd4,manager);put(owner.data(),0xe0,1);
        std::array<unsigned char,0x200> item{};
        put(item.data(),0,0x6a0304);put(item.data(),4,unsigned(reinterpret_cast<std::uintptr_t>(owner.data())));
        put(item.data(),8,intern(nullptr,nullptr,"synthetic_000",0,0));
        model_xml xml{"ui/models/lc_o_synthetic",cycle%2==0};
        (*reinterpret_cast<void(__fastcall**)(void*,void*,void*)>(0x6a0348))(item.data(),nullptr,&xml);
        for(unsigned i=0;i<20;++i) {
            reinterpret_cast<void(__fastcall*)(void*,void*,bool)>(0x5c4b90)(item.data(),nullptr,true);
            reinterpret_cast<void(__fastcall*)(void*,void*,bool)>(0x5c4b90)(item.data(),nullptr,false);
        }
        reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5bea50)(item.data(),nullptr);
        reinterpret_cast<void(__fastcall*)(void*,void*)>(0x588640)(backing,nullptr);
        for(const auto& [object,balance]:ownership) {
            if(balance) std::cerr<<"unbalanced model cycle "<<cycle<<" object "<<object<<" balance "<<balance<<"\n";
            assert(balance==0);
        }
    }
    std::cout<<"model cycles PASS real 5bbdd0 / 5c4b90 / 5c4c50 / 5bc3f0 / 5ab720 / 587ba0 / 5bea50 / 588640: references=0 after 100 load-close cycles, 20 hide/show cycles each\n";
}

}
