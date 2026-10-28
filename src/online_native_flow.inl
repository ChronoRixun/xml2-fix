// Included inside online_menus' game-thread namespace. This bridge owns no menu
// pointers across frames: native Game Options / save dialogs and native Host/Join own their UI.
// All retail ABI dependencies are part of the atomic online-menus contribution.
enum class native_phase { idle, setup, posting, host, tunnel, discovery, admitting, guest };
struct native_flow_state {
    native_phase phase=native_phase::idle;
    std::uintptr_t serial=1;
    broker::ticket ticket{};
    screen departure=screen::none;
    DWORD started=0, searched=0;
    std::uint32_t address=0;
    int maximum=0,difficulty=-1;
    bool loaded=false,danger_room=false;
    bool callback=false,touched=false,accepted=false,refused=false,lobby_active=false,seeded=false,host_queued=false;
} native_flow;
struct native_open_token {
    native_phase role=native_phase::idle;
    std::uintptr_t serial=0;
    broker::ticket ticket{};
    bool pending=false,owned=true;
    std::uint64_t command=0;
};
struct native_token_queue {
    std::array<native_open_token,16> items{};
    unsigned count=0;
    std::array<unsigned,2> canceled_commands{};
    std::array<bool,2> retired{};
    static unsigned index(native_phase role) { return role==native_phase::host ? 0 : 1; }
    void cancel() {
        for(unsigned i=0;i<count;++i) if(items[i].owned) {
            const auto role=index(items[i].role);retired[role]=true;
            // Retain only a per-role denial count, not obsolete generations
            // taking capacity from a later request of the same role.
            if(canceled_commands[role]!=~0u) ++canceled_commands[role];
        }
        count=0;
    }
    bool push(native_open_token value) {
        if(count==items.size()) return false;
        items[count++]=value;return true;
    }
    bool pop(native_phase role,native_open_token& out) {
        if(auto& canceled=canceled_commands[index(role)];canceled) { --canceled;out={role};return true; }
        for(unsigned i=0;i<count;++i) if(items[i].role==role) {
            out=items[i];for(unsigned j=i+1;j<count;++j) items[j-1]=items[j];--count;return true;
        }
        return false;
    }
} lobby_opens;
struct native_command_queue {
    // One marker per actually accepted console command, consumed on dispatch.
    // Legacy commands participate too: omitting them shifts owned provenance.
    // This has no menu-activation capacity limit; the native console owns the
    // lifetime/bound of the queued commands themselves.
    std::deque<native_open_token> items;
    void cancel() {
        for(auto& item:items) if(item.owned) { item.pending=false;item.serial=0;item.ticket={}; }
    }
    bool pop(native_phase role,native_open_token& out) {
        if(items.empty() || items.front().role!=role) return false;
        out=items.front();items.pop_front();return true;
    }
    bool take(std::uint64_t command,native_open_token& out) {
        for(auto i=items.begin();i!=items.end();++i) if(i->command==command) {
            out=*i;items.erase(i);return true;
        }
        return false;
    }
} host_commands, setup_commands;
std::uint64_t next_setup_command=0;
std::array<std::uintptr_t,2> rejected_lobbies{}; // opaque identities only; never dereferenced
void native_flow_error(const char* why);
std::uint32_t attempt_refusal_serial=0; // the agreement layer's refusal counter when this attempt began
std::string native_rules; // the rule names a refusal gave, shown beside lc_notice_refusal_rules (allowlisted text)
const char* native_phase_word(native_phase p) {
    switch(p) {
    case native_phase::setup: return "setup";
    case native_phase::posting: return "opening";
    case native_phase::host: return "host";
    case native_phase::tunnel: return "connecting";
    case native_phase::discovery: return "discovery";
    case native_phase::admitting: return "admission";
    case native_phase::guest: return "guest";
    default: return "native";
    }
}
std::uintptr_t options_owner=0;
bool native_request(const char* name);
bool native_options_seed(const char* name);
bool native_options_abandon_name() {
    __try {
        auto* request=at<getter>(0x60c7e0)();
        if(request!=at<void*>(0xa3e168) || *static_cast<unsigned*>(request)!=0x6a4730) return false;
        if(at<int(__thiscall*)(void*)>(0x60b9a0)(request)==1) {
            if(at<bool(__thiscall*)(void*)>(0x60b960)(request)) at<const char*(__thiscall*)(void*)>(0x60c780)(request);
            else at<select_fn>(0x60b990)(request,4);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool native_token_current(const native_open_token& token) {
    return token.pending && token.owned && token.role==native_flow.phase && token.serial==native_flow.serial &&
        token.ticket==native_flow.ticket && token.ticket==broker_view().generation && broker_view().state==broker::phase::room && (native_flow.lobby_active || broker_view().authenticated);
}
// Queued native commands and menu activations each consume their own generation
// token. A canceled command is a tombstone until drained; it cannot become a
// legacy unbrokered Host on a later controller or room.
bool native_owned_room() {
    return native_flow.phase!=native_phase::idle || (broker_live() && broker_calls.admission_target && broker_view().state==broker::phase::room);
}
bool native_queue_open(native_phase role,bool owned) {
    // Legacy requests need no generation token and must never consume bounded
    // owned-request capacity. An explicit fresh legacy route retires old denial.
    if(!owned) {
        const auto index=native_token_queue::index(role);
        lobby_opens.retired[index]=false;lobby_opens.canceled_commands[index]=0;return true;
    }
    for(unsigned i=0;i<lobby_opens.count;++i) if(native_token_current(lobby_opens.items[i]) && lobby_opens.items[i].role==role) return true;
    if(lobby_opens.push({role,native_flow.serial,native_flow.ticket,true,owned})) return true;
    if(owned) native_flow_error("native_open_queue_full");
    return false;
}
bool native_route_request(const char* name,bool console) {
    if(!name) return true;
    if(!std::strcmp(name,"game_options") && native_flow.phase==native_phase::idle && !custom(which(current()))) options_owner=0;
    if(native_flow.phase==native_phase::setup && (!std::strcmp(name,"main") || !std::strcmp(name,"online") ||
        !std::strcmp(name,"lc_online") || !std::strcmp(name,"lc_code") || !std::strcmp(name,"lc_browse") || !std::strcmp(name,"campaign_lobby"))) broker_cancel();
    if(!std::strcmp(name,"host")) {
        native_open_token token;
        if(console && host_commands.pop(native_phase::host,token)) {
            if(token.owned) {
                if(!native_token_current(token) || which(current())!=screen::host) return false;
                if(!native_queue_open(native_phase::host,true)) return false;
                native_flow.departure=screen::host;
            } else {
                if(native_owned_room()) return false;
                if(!native_queue_open(native_phase::host,false)) return false;
            }
        } else if(native_owned_room()) {
            if(native_flow.phase!=native_phase::host || !native_flow.lobby_active) return false;
        } else if(!native_queue_open(native_phase::host,false)) return false;
    } else if(!std::strcmp(name,"join")) {
        if(native_owned_room()) {
            // Owned initial Join uses native_request, after admission, not a
            // console command. Existing native Join transitions stay native.
            if(native_flow.phase!=native_phase::guest || !native_flow.lobby_active) return false;
            if(!native_queue_open(native_phase::guest,true)) return false;
        } else if(!native_queue_open(native_phase::guest,false)) return false;
    }
    if(native_flow.departure!=screen::none && which(current())==native_flow.departure) {
        const char* expected=native_flow.phase==native_phase::setup ? "game_options" : native_flow.phase==native_phase::host ? "host" : "join";
        if(std::strcmp(name,expected)) broker_cancel(broker_view().state==broker::phase::room);
    }
    return true;
}
bool native_flow_departure(screen source) {
    if(native_flow.phase==native_phase::idle || native_flow.lobby_active) return false;
    if(native_flow.departure==source) { native_flow.departure=screen::none;return true; }
    broker_cancel(broker_view().state==broker::phase::room);return true;
}
native_phase native_lobby_role(void* menu) {
    return which(menu)==screen::lobby_host ? native_phase::host : which(menu)==screen::lobby_join ? native_phase::guest : native_phase::idle;
}
bool native_lobby_rejected(void* menu) {
    const auto id=reinterpret_cast<std::uintptr_t>(menu);
    return id && (rejected_lobbies[0]==id || rejected_lobbies[1]==id);
}
bool native_lobby_skip_deactivate(void* menu) {
    if(!native_lobby_rejected(menu)) return false;
    for(auto& id:rejected_lobbies) if(id==reinterpret_cast<std::uintptr_t>(menu)) id=0;return true;
}
bool native_lobby_permit(void* menu) {
    const auto role=native_lobby_role(menu);if(role==native_phase::idle) return false;
    native_open_token token;bool accept=false;
    if(lobby_opens.pop(role,token)) accept=token.owned ? native_token_current(token) : !native_owned_room();
    else accept=(!native_owned_room() && !lobby_opens.retired[native_token_queue::index(role)]) || (native_flow.lobby_active && native_flow.phase==role);
    if(accept) { lobby_opens.retired[native_token_queue::index(role)]=false;for(auto& id:rejected_lobbies) if(id==reinterpret_cast<std::uintptr_t>(menu)) id=0; }
    else rejected_lobbies[role==native_phase::host ? 0 : 1]=reinterpret_cast<std::uintptr_t>(menu);
    return accept;
}
void native_lobby_recover() {
    if(native_flow.phase==native_phase::host || native_flow.phase==native_phase::guest) {
        bool pending=native_flow.lobby_active;
        for(unsigned i=0;i<lobby_opens.count;++i) pending|=native_token_current(lobby_opens.items[i]);
        if(pending) { native_request(native_flow.phase==native_phase::host ? "host" : "join");return; }
    }
    if(native_flow.phase!=native_phase::idle) native_flow_error("stale_native_activation");
    native_request("lc_online");
}
// The console "openmenu" handler (0x5f1d80, registered at 0x5f4a73) ends in manager +0x6c = our `request` slot.
// Its call at 0x5f1f8b is the only place a queued "openmenu host" (0x60a095) reaches the menu manager, so console
// provenance is decided here, then the request continues through the same `request` path (with routing done).
// Round 6 hooked 0x5f1c2e instead: that is "closemenu" (0x5f1c10, registered at 0x5f4a47), so the owned Host's
// queued "openmenu host" arrived as a plain request and native_route_request refused it (0/4 live, native_timeout).
bool native_console_open(void* manager,const char* name,const char* args) {
    console_routed=true;
    request(manager,nullptr,name,args); // contains its own exceptions (and keeps the native request on a throw)
    console_routed=false;
    return true;
}
void __fastcall console_open(void* manager,void*,const char* name,const char* args) {
    try {
        if(ready && !native_route_request(name,true)) return;
        if(!native_console_open(manager,name,args) && native_flow.phase!=native_phase::idle) native_flow_error("native_open_request");
    } catch(...) { console_routed=false;if(native_flow.phase!=native_phase::idle) native_flow_error("native_open_exception"); }
}
__declspec(naked) void console_open_stub() {
    __asm { mov ecx,eax
            jmp console_open }
}

bool native_callback_clear() {
    __try { at<void(__thiscall*)(void*,void*,void*)>(0x606e30)(at<getter>(0x6097b0)(),nullptr,nullptr);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Invalidate ownership BEFORE calling native cleanup: callbacks can be synchronous.
void native_flow_cancel() {
    if(native_flow.phase==native_phase::idle) return;
    const auto old=native_flow.phase;const bool touched=native_flow.touched,callback=native_flow.callback;
    const bool setup_name=options_owner==native_flow.serial;
    const auto serial=native_flow.serial+1;
    native_flow={};native_flow.serial=serial;
    host_commands.cancel();setup_commands.cancel();lobby_opens.cancel();
    if(setup_name) native_options_abandon_name();
    if(callback) native_callback_clear();
    __try {
        auto* campaign=at<getter>(0x6097b0)();
        if(touched) {
            if(old==native_phase::host) at<void(__thiscall*)(void*,bool)>(0x607c40)(campaign,false);
            else at<tick_fn>(0x6097f0)(campaign);
            at<tick_fn>(0x608e10)(campaign);
        }
        if(old==native_phase::setup || old==native_phase::posting || old==native_phase::host)
            at<tick_fn>(0x606f50)(campaign); // release the selected native campaign image
    } __except(EXCEPTION_EXECUTE_HANDLER) { logger::write("online menus: native cleanup unavailable; ownership invalidated"); }
    pending.clear();keyboard_input.clear();bindings.clear();accepted=-1;
}
void native_flow_error(const char* why) {
    const bool setup=native_flow.phase==native_phase::setup;
    logger::write("online menus: native flow refused: %s",why);
    const auto phase=native_phase_word(native_flow.phase);
    broker_cancel(broker_view().state==broker::phase::room);
    // One localized inline notice per failure kind (online_menus_rules::native_failure_table); the phase and the fixed
    // internal reason are the only diagnostic text. A refusal names the rules the PCs possibly differ on when known.
    const char* notice=native_notice(why);
    std::string rules;
    if(std::strcmp(notice,"lc_notice_refused")==0 && broker_calls.refusal_rules) {
        const char* given=broker_calls.refusal_rules();
        // Only a refusal that happened during THIS attempt: stale text, or another host's refusal while browsing the LAN, is not ours.
        if(given && broker_calls.refusal_serial && broker_calls.refusal_serial()!=attempt_refusal_serial) rules=refusal_rules_text(std::string_view(given,strnlen(given,127))); // a bounded read of the agreement layer's text
    }
    if(!rules.empty()) notice="lc_notice_refusal_rules";
    message(notice); // localized LC adapter node
    native_rules=rules;
    logger::write("online menus: native flow notice %s (%s: %s)",notice,phase,find_native_failure(why) ? why : "other");
    if(setup) native_request("lc_host");
}
// Leave the broker room after a native session end. The retail quit notices (0x10cc / 0x1106) are queued on the room
// tunnel only moments before this runs; leaving at once closes the tunnel without lingering and peers saw "lost
// connection" while joiners waited out the 10 s watchdog. The room layer leaves after the next transport tick.
void native_session_leave() {
    if(!broker_live()) return;
    if(broker_calls.leave_deferred) broker_calls.leave_deferred();
    else if(broker_calls.leave) broker_calls.leave();
    resolve_edit=0;resolve_ticket={};
}
void __fastcall native_session_cleared(void* session,void*,bool keep_local) {
    const auto serial=native_flow.serial;
    at<void(__thiscall*)(void*,bool)>(0x612070)(session,keep_local);
    // Only terminal full-clear branches in 6123e0/612c40 call this wrapper.
    // Host setup/admission resets, keep-local transfer work, and single remote
    // removal do not. +421 is host authority, not the active-session byte +420.
    // Both callers are retail frames: nothing here may throw into them.
    try {
        if(ready && session==at<void*>(0xa53058) && native_flow.serial==serial && native_flow.lobby_active &&
            !keep_local && !*reinterpret_cast<void**>(static_cast<unsigned char*>(session)+0x78)) {
            // A guest whose connection to the host ended: the 0.6 host-left panel waits for the Hub (never a takeover). The
            // room layer's latch is read BEFORE the room is left below, which resets it.
            if(native_flow.phase==native_phase::guest && broker_calls.host_lost) {
                try { if(broker_calls.host_lost()) host_left_state.raise(); } catch(...) { }
            }
            native_flow.touched=false; // the native session is already torn down
            try { native_flow_cancel(); } catch(...) { logger::write("online menus: native session end: ownership cleanup failed"); }
            try { native_session_leave(); } catch(...) { logger::write("online menus: native session end: room leave failed"); }
        }
    } catch(...) { logger::write("online menus: native session end: cleanup failed"); }
}
bool native_request(const char* name) {
    __try { at<request_fn>(0x5d5db0)(manager(),name,nullptr);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_host_begin() {
    if(native_flow.phase==native_phase::setup && native_flow.departure==screen::none) broker_cancel();
    if(native_flow.phase!=native_phase::idle || broker_view().state==broker::phase::room) return false;
    int controller=-1;
    __try { controller=at<int(__thiscall*)(void*)>(0x5d8570)(manager()); }
    __except(EXCEPTION_EXECUTE_HANDLER) { }
    if(!controller_valid(controller)) { message("lc_notice_admission");return false; }
    error_visible=false;feedback.clear();
    // Native game names are 31 bytes, narrower than broker room-name metadata.
    // An overlong preset gets an explicit native choice, never silent truncation.
    if(!valid_name(host_room_name) || !broker::valid_room_name(host_room_name)) host_room_name="Online game";
    ++native_flow.serial;native_flow.phase=native_phase::setup;native_flow.departure=screen::host;
    options_owner=native_flow.serial;
    if(!native_options_abandon_name() || !native_options_seed(host_room_name.c_str())) {
        native_flow_error("native_options_seed");return false;
    }
    if(!native_request("game_options")) { native_flow_error("native_options_unavailable");return false; }
    return true;
}
struct native_host_settings { char name[32]{};int maximum=0,difficulty=-1;bool loaded=false,danger_room=false; };
bool native_host_read(native_host_settings& settings) {
    __try {
        auto* session=at<getter>(0x612be0)();
        if(session!=at<void*>(0xa53058) || *static_cast<unsigned*>(session)!=0x6a481c) return false;
        const auto* bytes=static_cast<const unsigned char*>(session);
        std::memcpy(settings.name,bytes+0x3b6,32);
        settings.maximum=bytes[0x3dd];
        settings.difficulty=bytes[0x3df]<3 ? bytes[0x3df] : -1;
        settings.danger_room=bytes[0x3e0]==2;
        settings.loaded=*reinterpret_cast<void**>(0xa3bc40+0x2250)!=nullptr;
        return settings.name[31]==0 && settings.maximum>=2 && settings.maximum<=4;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_options_seed(const char* name) {
    __try {
        auto* session=at<getter>(0x612be0)();
        if(session!=at<void*>(0xa53058) || *static_cast<unsigned*>(session)!=0x6a481c) return false;
        at<name_fn>(0x610dc0)(session,name);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_options_prepare(void* menu) {
    if(native_flow.seeded) return true;
    if(!native_options_seed(host_room_name.c_str())) return false;
    __try {
        if(!menu || *static_cast<unsigned*>(menu)!=0x69f65c) return false;
        *reinterpret_cast<int*>(static_cast<char*>(menu)+0x1960)=0;
        static_cast<char*>(menu)[0x1964]=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    native_flow.seeded=true;
    return true;
}
bool native_options_activate(void* menu,int argument) {
    __try { at<void(__thiscall*)(void*,int)>(0x5caff0)(menu,argument);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall options_activate(void* menu,void*,int argument) {
    if(options_owner && (native_flow.phase!=native_phase::setup || options_owner!=native_flow.serial)) {
        native_request("lc_online");return;
    }
    if(!native_options_activate(menu,argument)) { if(options_owner) native_flow_error("native_options_activation");return; }
    if(options_owner) {
        try {
            if(!native_options_prepare(menu)) { native_flow_error("native_options_initialization");return; }
            native_host_settings accepted_settings;
            const auto name=native_host_read(accepted_settings) ? std::string(accepted_settings.name) : host_room_name;
            bind_text(menu,"data_gamename",display(name,31).c_str());
        } catch(...) { native_flow_error("native_options_initialization"); }
    }
}
struct native_options_call { void* menu=nullptr;std::uintptr_t serial=0; };
thread_local native_options_call options_call;
bool native_options_current(void* menu) {
    __try { return menu && current()==menu && *static_cast<unsigned*>(menu)==0x69f65c; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_options_frame_impl(void* menu) {
    __try { at<tick_fn>(0x5b9110)(menu);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_options_frame(void* menu,std::uintptr_t serial) {
    const auto previous=options_call;options_call={menu,serial};
    const bool result=native_options_frame_impl(menu);options_call=previous;
    return result;
}
bool native_options_base(void* menu) {
    __try { at<tick_fn>(0x5aaeb0)(menu);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall options_tick(void* menu,void*) {
    bool called=false;
    const auto serial=options_owner;
    if(serial && !native_options_current(menu)) return;
    if(serial && (native_flow.phase!=native_phase::setup || serial!=native_flow.serial)) {
        native_options_base(menu); // keep native Back; no more actions in the canceled context
        if(native_options_current(menu)) native_request("lc_host");
        return;
    }
    try {
        if(native_flow.phase==native_phase::setup && !native_flow.seeded) {
            if(!native_options_prepare(menu)) { native_flow_error("native_options_seed");return; }
            bind_text(menu,"data_gamename",display(host_room_name,31).c_str());
        }
        called=true;
        if(!native_options_frame(menu,serial) && native_flow.phase!=native_phase::idle) native_flow_error("native_options_frame");
    } catch(...) {
        if(!called) native_options_frame(menu,serial);
        if(native_flow.phase!=native_phase::idle) native_flow_error("native_options_exception");
    }
}
// Run the actual retail handler, including address preparation, native Host
// participant/controller allocation and the native save/profile tail.
__declspec(naked) void setuphost_native() {
    // Replay the guarded first call, then resume the untouched handler. The
    // console entry itself is routed through dispatch, including setupHost().
    __asm { mov eax,0x6097b0
            call eax
            push 0x5f1bb5
            ret }
}
void __cdecl setuphost_dispatch() {
    if(!ready || native_flow.phase==native_phase::idle) { if(ready && (options_owner || native_owned_room())) return;setuphost_native();return; }
    if(native_flow.phase!=native_phase::setup) return; // queued duplicate Post
    try {
        native_host_settings settings;
        if(!native_host_read(settings) || !valid_name(settings.name) || !broker::valid_room_name(settings.name) || !broker_live() || !broker_calls.host_native) {
            native_flow_error("invalid_native_settings");return;
        }
        host_room_name=broker::trim(settings.name);
        if(!broker_calls.host_native(host_room_name,host_public,settings.maximum,settings.danger_room)) {
            native_flow_error("host_post_refused");return;
        }
        native_flow.maximum=settings.maximum;native_flow.difficulty=settings.difficulty;
        native_flow.loaded=settings.loaded;native_flow.danger_room=settings.danger_room;
        native_flow.phase=native_phase::posting;native_flow.ticket=broker_view().generation;native_flow.started=now_ms();
        if(!native_request("lc_host")) native_flow_error("native_setup_return_unavailable");
    } catch(...) { native_flow_error("host_post_exception"); }
}
struct native_setup_argument { bool marked=false,valid=false;std::uint64_t command=0; };
bool native_setup_argument_read(const char** cursor,native_setup_argument& result) {
    __try {
        // The console dispatch passes &cursor, not the text itself. Parse with
        // the retail tokenizer so its outer command loop also advances safely.
        const char* next=cursor ? *cursor : nullptr;
        if(!next) return true;
        const auto* word=at<const char*(__thiscall*)(void*,const char**)>(0x55b670)(at<getter>(0x55c890)(),&next);
        if(!word) return false;
        result.marked=std::memcmp(word,"lc-native:",10)==0;
        if(!result.marked) return true;
        *cursor=next; // only our private marker is an argument to this handler
        if(word[26]) return true;
        for(unsigned i=10;i<26;++i) {
            const auto c=word[i];const int digit=c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : -1;
            if(digit<0) return true;
            result.command=(result.command<<4)|unsigned(digit);
        }
        result.valid=result.command!=0;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __cdecl setuphost_command(const char** cursor) {
    native_setup_argument argument;
    if(!native_setup_argument_read(cursor,argument)) return;
    native_open_token token;
    if(argument.marked) {
        if(!argument.valid || !setup_commands.take(argument.command,token)) return;
        if(token.owned) {
            if(!token.pending || token.serial!=native_flow.serial || native_flow.phase!=native_phase::setup || !native_options_current(current())) return;
        } else if(native_owned_room() || options_owner) return;
    } else if(native_owned_room() || options_owner) return; // typed/usecmd never consumes a queued marker
    setuphost_dispatch();
}
bool __fastcall setuphost_script_queue(void* console,void*,const char* line);
bool __fastcall setuphost_queue(void* console,void*,const char* line) {
    if(!line || std::strcmp(line,"setuphost")) return at<command_fn>(0x55c410)(console,line);
    // Base input may cancel/deactivate this Game Options during the same tick.
    // Its later Post branch must consume the old frame, never go legacy.
    if(options_call.serial) {
        if(native_flow.phase==native_phase::setup && native_flow.serial==options_call.serial && native_options_current(options_call.menu)) setuphost_dispatch();
        return true;
    }
    if(native_flow.phase==native_phase::idle) { if(options_owner) return true;return setuphost_script_queue(console,nullptr,line); }
    if(native_flow.phase==native_phase::setup && native_options_current(current())) setuphost_dispatch();
    return true;
}
__declspec(naked) void setuphost_queue_stub() {
    __asm { mov ecx,eax
            jmp setuphost_queue }
}
int __fastcall options_back(void* menu,void*) {
    const bool mine=options_owner && options_owner==native_flow.serial && !native_flow.lobby_active &&
        (native_flow.phase==native_phase::setup || native_flow.phase==native_phase::posting || native_flow.phase==native_phase::host);
    const int result=at<int(__thiscall*)(void*)>(0x5b90d0)(menu);
    if(mine) broker_cancel(broker_view().state==broker::phase::room);
    return result;
}
bool native_local_admitted() {
    __try {
        auto* local=at<void*(__thiscall*)(void*)>(0x6111f0)(at<getter>(0x612be0)());
        return local && at<bool(__thiscall*)(void*)>(0x614290)(local);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int __cdecl native_admission_event(int event,void* generation,void*) {
    if(reinterpret_cast<std::uintptr_t>(generation)!=native_flow.serial || native_flow.phase!=native_phase::admitting) return 0;
    // Retail 0x60791b emits 8 after local participant admission; 0x609aec
    // emits 9 on refusal. Neither event changes readiness.
    if(event==8) { native_flow.accepted=native_local_admitted();native_flow.refused=!native_flow.accepted; }
    if(event==9) native_flow.refused=true;
    return 0;
}
bool native_search_start() {
    __try {
        auto* campaign=at<getter>(0x6097b0)();
        at<tick_fn>(0x608e10)(campaign); // stop old search; clear the actual native table
        at<void(__thiscall*)(void*,void*,void*)>(0x606e30)(campaign,reinterpret_cast<void*>(&native_admission_event),reinterpret_cast<void*>(native_flow.serial));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_search_query(std::uint32_t address) {
    char dotted[16]{};
    std::snprintf(dotted,sizeof(dotted),"%u.%u.%u.%u",address>>24,(address>>16)&255,(address>>8)&255,address&255);
    __try {
        // The established native direct-address search constructs the real
        // game-info request. This address can only come from the proven tunnel.
        std::memcpy(at<void*>(0xa3bc1c),dotted,sizeof(dotted));
        at<tick_fn>(0x608f10)(at<getter>(0x6097b0)());
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Zero host records waits. Only the proven host's source address participates;
// another room member's game-info response cannot make this host ambiguous.
// Do not retain this pointer across a frame, callback, table clear or generation.
int native_discovery_record(std::uint32_t address,void*& out) {
    out=nullptr;
    __try {
        auto* campaign=static_cast<unsigned char*>(at<getter>(0x6097b0)());
        auto* table=campaign+0x2208;
        const auto count=*reinterpret_cast<unsigned*>(table+0xc);
        if(!count) return 0;
        if(count>100) return -1;
        auto* node=*reinterpret_cast<unsigned char**>(table);
        int matched=0;
        for(unsigned i=0;i<count;++i) {
            if(!node) return -1;
            auto* record=*reinterpret_cast<unsigned char**>(node+0x10);
            const auto offset=reinterpret_cast<std::uintptr_t>(record)-reinterpret_cast<std::uintptr_t>(campaign+0x5b4);
            if(offset>=100*0x44 || offset%0x44) return -1;
            const auto ip=(std::uint32_t(record[8])<<24)|(std::uint32_t(record[9])<<16)|(std::uint32_t(record[10])<<8)|record[11];
            if(ip==address) {
                if(matched || !record[0x11] || record[0xf]!=1 || *reinterpret_cast<unsigned short*>(record+0xc)!=5165) return -1;
                const auto* version=at<const unsigned char*>(0xa53058+0x410);
                if(!std::memchr(record+0x3e,0,6) || !std::memchr(version,0,6) || std::strcmp(reinterpret_cast<const char*>(record+0x3e),reinterpret_cast<const char*>(version))) return -1;
                out=record;++matched;
            }
            node=*reinterpret_cast<unsigned char**>(node+8);
        }
        return node ? -1 : matched;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool native_admit(void* record) {
    __try { at<void(__thiscall*)(void*,void*)>(0x60a0a0)(at<getter>(0x6097b0)(),record);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_console_queue(void* console,const char* line) {
    __try { return at<command_fn>(0x55c410)(console,line); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_track_command(native_command_queue& queue,void* console,const char* line,native_open_token token) {
    try { queue.items.push_back(token); }
    catch(...) { return false; } // allocation failure, never a legacy capacity refusal
    if(native_console_queue(console,line)) return true;
    queue.items.pop_back();return false;
}
bool __fastcall setuphost_script_queue(void* console,void*,const char*) {
    const bool owned=native_flow.phase!=native_phase::idle || options_owner!=0;
    if(!++next_setup_command) return false;
    char marked[64]{};std::snprintf(marked,sizeof(marked),"setuphost lc-native:%016llx",static_cast<unsigned long long>(next_setup_command));
    return native_track_command(setup_commands,console,marked,{native_phase::setup,native_flow.serial,native_flow.ticket,
        native_flow.phase==native_phase::setup,owned,next_setup_command});
}
__declspec(naked) void setuphost_script_queue_stub() {
    __asm { mov ecx,eax
            jmp setuphost_script_queue }
}
bool __fastcall native_host_queue(void* console,void*,const char* line) {
    const bool own=native_flow.phase==native_phase::host && !native_flow.lobby_active;
    // Preserve mixed legacy/owned FIFO ordering without an activation-slot cap.
    const bool queued=native_track_command(host_commands,console,line,{native_phase::host,native_flow.serial,native_flow.ticket,true,own});
    if(own) native_flow.host_queued=queued;
    return queued;
}
__declspec(naked) void native_host_queue_stub() {
    __asm { mov ecx,eax
            jmp native_host_queue }
}
bool native_post_host() {
    __try {
        setuphost_native();
        auto* session=at<getter>(0x612be0)();
        return native_flow.host_queued && native_local_admitted() && at<bool(__thiscall*)(void*)>(0x610d10)(session);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool native_flow_tick(screen s) {
    if(!broker_live()) return false;
    const auto& view=broker_view();
    if(native_flow.phase==native_phase::idle && (s==screen::code || s==screen::browse) &&
        view.state==broker::phase::room && view.action==broker::operation::resolve && broker_calls.admission_target) {
        ++native_flow.serial;native_flow.phase=native_phase::tunnel;
        attempt_refusal_serial=broker_calls.refusal_serial ? broker_calls.refusal_serial() : 0;
        native_flow.ticket=view.generation;native_flow.started=now_ms();
    }
    const auto phase=native_flow.phase;
    if(phase==native_phase::setup && s==screen::host && native_flow.departure==screen::none) { broker_cancel();return false; }
    if(phase==native_phase::idle || phase==native_phase::setup) return false;
    if((phase==native_phase::host || phase==native_phase::guest) && native_flow.lobby_active) return false;
    if(view.generation!=native_flow.ticket || view.state==broker::phase::retry_wait || view.state==broker::phase::error ||
        view.state==broker::phase::refused || (view.state==broker::phase::room && !view.authenticated)) {
        native_flow_error("room_generation_or_service_lost");return false;
    }
    const DWORD deadline=phase==native_phase::tunnel ? 45000 : 15000;
    if(now_ms()-native_flow.started>=deadline) { native_flow_error("native_timeout");return false; }
    if(phase==native_phase::posting) {
        if(view.state!=broker::phase::room) return false;
        native_flow.phase=native_phase::host;native_flow.touched=true;native_flow.started=now_ms();
        if(!native_post_host()) { native_flow_error("native_host_refused");return false; }
        return true; // retail setuphost queued Host; never bind an outgoing menu
    }
    if(phase==native_phase::host || phase==native_phase::guest) return false; // wait for actual native activation
    std::uint32_t address=0;
    if(!broker_calls.admission_target || !broker_calls.admission_target(native_flow.ticket,address)) return false;
    if(phase==native_phase::tunnel) {
        native_flow.address=address;native_flow.touched=true;native_flow.callback=true;
        if(!native_search_start()) { native_flow_error("native_search_refused");return false; }
        native_flow.phase=native_phase::discovery;native_flow.started=now_ms();native_flow.searched=now_ms()-1000;
    } else if(address!=native_flow.address) { native_flow_error("host_changed");return false; }
    if(native_flow.phase==native_phase::discovery) {
        void* record=nullptr;const auto count=native_discovery_record(address,record);
        if(count<0) { native_flow_error("discovery_ambiguous_or_wrong");return false; }
        if(count==1) {
            native_flow.phase=native_phase::admitting;native_flow.started=now_ms();
            if(!native_admit(record)) native_flow_error("admission_call_refused");
        } else if(now_ms()-native_flow.searched>=1000) {
            native_flow.searched=now_ms();
            if(!native_search_query(address)) native_flow_error("discovery_query_refused");
        }
    }
    if(native_flow.phase==native_phase::admitting) {
        if(native_flow.refused) { native_flow_error("admission_refused");return false; }
        if(native_flow.accepted) {
            if(!native_callback_clear()) { native_flow_error("admission_callback_cleanup");return false; }
            native_flow.callback=false;native_flow.phase=native_phase::guest;native_flow.started=now_ms();
            native_flow.departure=s;if(!native_queue_open(native_phase::guest,true)) return false;
            code.close();pending.clear();keyboard_input.clear();bindings.clear();
            if(!native_request("join")) { native_flow_error("native_join_unavailable");return false; }
            return true;
        }
    }
    return false;
}
bool native_lobby_entered(void* menu) {
    const bool host=native_flow.phase==native_phase::host && which(menu)==screen::lobby_host;
    const bool guest=native_flow.phase==native_phase::guest && which(menu)==screen::lobby_join;
    if(!host && !guest) return true; // legacy native flow
    if(native_flow.ticket!=broker_view().generation || broker_view().state!=broker::phase::room ||
        (!native_flow.lobby_active && !broker_view().authenticated) ||
        (guest && !native_local_admitted()) ||
        (host && !native_flow.lobby_active && (!broker_calls.host_posted || !broker_calls.host_posted(native_flow.ticket)))) {
        native_flow_error("native_lobby_generation");return false;
    }
    native_flow.lobby_active=true;return true;
}
bool native_lobby_departing(void* menu) {
    __try {
        const auto* bytes=static_cast<unsigned char*>(menu);
        if(native_flow.phase==native_phase::host && which(menu)==screen::lobby_host) return !bytes[0x1972] && !bytes[0x1973];
        if(native_flow.phase==native_phase::guest && which(menu)==screen::lobby_join) return bytes[0x196e]!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    return false;
}
void native_lobby_left() {
    // Native host cleanup checks the host-authority latch before notifying or
    // removing participants. Complete a bare deactivation too; repeated native
    // Leave cannot reuse a previous room's local/controller record.
    broker_cancel(true);
}
