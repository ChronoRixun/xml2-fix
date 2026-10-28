#pragma once
#include "ini_rules.hpp"
#include "script_functions_rules.hpp"
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <array>
#include <algorithm>
#include <optional>
#include <vector>
#include <string>
#include <string_view>

namespace online_menus_rules {
// This is an asset/API contract, not a release identity or gameplay rule.
inline constexpr char capability[] = "lc-online-menus/1\n";
// Additive input API probe: LC must require this before selecting intents 9..62.
inline constexpr char input_capability[] = "lc-online-input/1";
inline constexpr char lobby_capability[] = "lc-online-lobby/1";
// /2: per-row status/pip/build/dim scenes, list count + age, per-cause error scenes, service scene (docs/online-menus.md).
inline constexpr char broker_capability[] = "lc-online-broker/2";
inline constexpr char native_capability[] = "lc-online-native/1";
// Round 8: base lc_party scene + the pause menu's lc_party_link rename. LC authors both only when the DLL carries this.
inline constexpr char party_capability[] = "lc-online-party/1";
// Round 15: the complete error table (docs/online-menus.md "Round 15"): per-cause scenes beyond broker/2's eight, native
// failure notices, the lobby's inline service/toast scenes and the Hub's host-left panel.
inline constexpr char errors_capability[] = "lc-online-errors/1";
inline constexpr char function_name[] = "lcOnlineIntent";
enum class intent : int { enter=1, host, code, browse, other, back, unavailable, dismiss, edit_name, paste, erase, clear, done, rejoin, invalid_key,
    copy_code=16, confirm_leave=17, refresh=18, open_room=19, retry=20, visibility=21,
    previous=22, next=23, row_first=24, row_last=30, join_selected=31, symbol_first=32, symbol_last=62,
    // Round 8 (lc_party only; 68 and 70 are reserved and never accepted): the pause rail's Resume, Invite/Copy, the host's
    // Propose load placeholder, Leave, its confirmation, the host's Kick confirmation and Kick on card 1..4.
    party_resume=63, party_invite=64, party_propose=65, party_leave=66, party_leave_yes=67, party_kick_yes=69,
    party_kick_first=71, party_kick_last=74, last=74 };
// Browse rows per page. XMen2.exe holds at most 175 named anchors per menu scene (playfield 0x5882e0: two inline
// 175-entry vectors, count at +0x2d4, and the 175-entry name table whose add 0x588120 checks 0xaf); a 176th anchor
// overwrites the vectors' bit masks and counts and the next push writes to a wild address (060p Browse crash at
// 0x587654). Seven rows of the approved Browse need 241 anchors; five rows (LC tools/xml1build/online_menus.py
// BROWSE_ROWS, the same number) fit with the shared per-row focus anchor. Intents 24..30 stay reserved.
inline constexpr unsigned browse_rows = 5;
static_assert(browse_rows>=1 && browse_rows<=7);
enum class screen { none, main, hub, host, code, browse, lobby_host, lobby_join, party };
inline screen identify(std::string_view name) {
    if(name=="main") return screen::main;
    if(name=="lc_online") return screen::hub;
    if(name=="lc_host") return screen::host;
    if(name=="lc_code") return screen::code;
    if(name=="lc_browse") return screen::browse;
    if(name=="host") return screen::lobby_host;
    if(name=="join") return screen::lobby_join;
    if(name=="lc_party") return screen::party;
    return screen::none;
}
inline bool custom(screen s) { return (s>=screen::hub && s<=screen::browse) || s==screen::party; }
// The intents only lc_party owns. Everything else is refused there, and these are refused everywhere else.
inline bool party_intent(int value) {
    return (value>=int(intent::party_resume) && value<=int(intent::party_leave_yes)) || value==int(intent::party_kick_yes) ||
        (value>=int(intent::party_kick_first) && value<=int(intent::party_kick_last));
}
inline bool party_accepts(int value) { return value==int(intent::back) || value==int(intent::dismiss) || party_intent(value); }
inline bool lobby(screen s) { return s==screen::lobby_host || s==screen::lobby_join; }

// ---- Pad buttons on the co-op screens (the 060p live round, failure 8) ----------------------------------------------
// The menu input mask (controller vtable +0x13c, 0x5d4b00) has one bit per UI code, and the engine raises a code when the
// player's binding of the code's action (0x619c40: code -> action) is pressed. The fix's console pad layout binds Guard to
// X, Jump to Y and Power to RT, so on a pad:
//   X  = action 7 Guard = code 8 MENU_SUBTRACT (mask 0x100)
//   Y  = action 6 Jump  = code 0x18 MENU_OTHER (mask 0x1000000) (and code 6)
//   RT = action 8 Power = code 0x16 MENU_NEXT  (mask 0x400000)  (and code 7)
// The approved designs (INGAME-DESIGN, claude-design 01-04) put Change name / Clear / Kick / Ready on X and Rejoin / Paste /
// Refresh / Copy on Y. Rounds 2-8 had taken MENU_OTHER for X and MENU_NEXT for Y (the comments said so, the host fixtures
// fed the bits directly), so on a real pad X did nothing and Y did X's job. The keyboard keeps the engine's own keys
// (MENU_OTHER = Del, MENU_NEXT = KP5 in the menus' third binding slot), so the swap is for a pad only.
inline constexpr unsigned mask_subtract = 1u<<8;   // MENU_SUBTRACT: a pad's X
inline constexpr unsigned mask_next = 1u<<0x16;    // MENU_NEXT: RT on a pad, KP5 on the keyboard
inline constexpr unsigned mask_other = 1u<<0x18;   // MENU_OTHER: Y on a pad, Del on the keyboard
// What a custom co-op screen reads from the raw mask when the player's prompts show the pad: X is the screen's "other"
// button, Y its "next" button, RT stays an alias of next (it was the old Rejoin / Paste button). Every other bit is kept.
inline constexpr unsigned pad_menu_mask(unsigned raw) {
    unsigned out=raw & ~(mask_subtract|mask_next|mask_other);
    if(raw & mask_subtract) out|=mask_other;
    if(raw & (mask_other|mask_next)) out|=mask_next;
    return out;
}
// The UI code a pad prompt must name on those screens, so the label is the button that now does the job: the token
// $MENU_OTHER (code 0x18) is drawn as the binding of code 8 (X), $MENU_NEXT (0x16) as the binding of code 0x18 (Y).
inline constexpr int pad_prompt_code(int code) { return code==0x18 ? 8 : code==0x16 ? 0x18 : code; }
inline bool controller_valid(int p) { return p>=0 && p<4; }
inline bool enabled(std::optional<std::string_view> value) {
    return value && ini_rules::value_text(*value)=="1";
}
// Remote/display data uses a small ASCII allowlist, including no renderer
// controls or escape introducers. The variadic setter still MUST use "%s".
inline char display_byte(unsigned char c) {
    const bool safe=(c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') ||
        c==' ' || c=='-' || c=='_' || c=='.' || c=='\'' || c==',';
    return safe ? char(c) : '?';
}
inline std::string display(std::string_view in, std::size_t limit=96) {
    std::string out;
    for(unsigned char c:in) { if(out.size()==limit) break;out+=display_byte(c); }
    return out;
}
inline bool valid_name(std::string_view s) {
    if(s.empty() || s.size()>31 || s.find_first_not_of(' ')==std::string_view::npos) return false;
    for(unsigned char c:s) if(c<32 || c>126) return false;
    return true;
}

// Clipboard/storage grammar: outer ASCII whitespace and one display dash only.
inline constexpr std::string_view alphabet="ABCDEFGHJKMNPQRSTUVWXYZ23456789";
inline bool space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
inline std::optional<std::string> parse_code(std::string_view in) {
    if(in.size()>256) return {};
    while(!in.empty() && space(in.front())) in.remove_prefix(1);
    while(!in.empty() && space(in.back())) in.remove_suffix(1);
    if(in.size()!=8 && in.size()!=9) return {};
    std::string out;
    for(std::size_t i=0;i<in.size();++i) {
        char c=in[i];
        if(in.size()==9 && i==4) { if(c!='-') return {}; continue; }
        if(c>='a' && c<='z') c-=32;
        if(alphabet.find(c)==std::string_view::npos) return {};
        out+=c;
    }
    return out;
}
// Adapter is also driven by host lock/open/size failures. Never retries/waits.
// unavailable: the clipboard could not be opened or locked; invalid: it was read (or holds no text at all) and has no code.
enum class clip_result { ok, unavailable, invalid };
template<class Clipboard> clip_result read_clipboard_result(Clipboard& api, std::string& out) {
    out.clear();
    if(!api.open()) return clip_result::unavailable;
    struct closer { Clipboard& api; ~closer() { api.close(); } } close{api};
    const auto bytes=api.size();
    if(bytes<2 || bytes>4096 || bytes%2) return clip_result::invalid;
    const auto* data=api.lock(); if(!data) return clip_result::unavailable;
    struct unlocker { Clipboard& api; ~unlocker() { api.unlock(); } } unlock{api};
    std::string text;
    const auto count=std::min<std::size_t>(bytes/2,257);
    for(std::size_t i=0;i<count;++i) {
        const auto c=data[i];
        if(!c) {
            // The text ends at its first terminator, as for every clipboard reader. GlobalAlloc without GMEM_ZEROINIT
            // leaves the spare bytes after it unspecified, and real clipboards do carry them (the 060p live round
            // refused a valid abcd-efgh on a tail check); only what precedes the terminator can become a code.
            auto code=parse_code(text);
            if(!code) return clip_result::invalid;
            out=*code;return clip_result::ok;
        }
        if(c>127) return clip_result::invalid;
        text+=char(c);
    }
    return clip_result::invalid;
}
template<class Clipboard> std::optional<std::string> read_clipboard(Clipboard& api) {
    std::string out;
    if(read_clipboard_result(api,out)==clip_result::ok) return out;
    return {};
}
struct code_model {
    std::string text;
    std::uint64_t generation=1, submitted=0;
    bool active=false;
    void open() { active=true; text.clear(); ++generation; }
    void close() { active=false; ++generation; }
    bool replace(std::string value) {
        if(value.size()>8 || value.find_first_not_of(alphabet)!=std::string::npos) return false;
        if(text==value) return false;
        text=std::move(value); ++generation; return true;
    }
    bool append(char c) {
        if(!active || text.size()==8 || alphabet.find(c)==std::string_view::npos) return false;
        return replace(text+c);
    }
    void erase() { if(!text.empty()) replace(text.substr(0,text.size()-1)); }
    // One automatic request for each edited complete value. Done can retry.
    std::uint64_t resolve(bool retry=false) {
        if(!active || text.size()!=8 || (!retry && submitted==generation)) return 0;
        submitted=generation; return generation;
    }
    bool complete(std::uint64_t ticket) const { return active && ticket==generation && ticket==submitted; }
    std::string shown() const {
        std::string out=text; out.append(8-out.size(),'_'); out.insert(4,"-"); return out;
    }
};
struct key_edges {
    std::array<unsigned char,256> previous{};
    bool primed=false;
    void reset() { primed=false; }
    std::array<bool,256> sample(const unsigned char* state) {
        std::array<bool,256> edges{};
        for(unsigned i=0;i<256;++i) {
            edges[i]=primed && (state[i]&0x80) && !(previous[i]&0x80);
            previous[i]=state[i];
        }
        primed=true; return edges;
    }
};
// Bounded game-thread input inbox. Multiple rising edges from one DirectInput
// poll keep scan-code order; close/navigation invalidates even address reuse.
struct input_inbox {
    std::array<int,64> values{};
    std::uintptr_t source=0;
    unsigned count=0;
    void clear() { source=0;count=0; }
    void push(std::uintptr_t menu,int value) {
        if(source!=menu) { clear();source=menu; }
        if(count<values.size()) values[count++]=value;
    }
};
// Native setters read these FULL buffers, including the initialized tail.
struct name_request {
    bool owned=false,departed=false;
    std::uint64_t generation=0;
    std::array<char,256> seed{};
    std::array<char,64> title{};
    bool begin(std::string_view name) {
        if(owned || !valid_name(name)) return false;
        ++generation; departed=false; seed.fill(0); title.fill(0);
        std::copy(name.begin(),name.end(),seed.begin());
        constexpr std::string_view label="Screen name";
        std::copy(label.begin(),label.end(),title.begin()); owned=true; return true;
    }
    std::optional<std::string> finish(int tag,const char* result,std::size_t size) {
        const bool mine=owned; owned=false;
        if(!mine || tag!=0 || !result) return {};
        std::size_t n=0; while(n<size && result[n]) ++n;
        if(n==size || !valid_name(std::string_view(result,n)) || std::string_view(result,n)==seed.data()) return {};
        return std::string(result,n);
    }
};
// No addresses, credentials or shared recovery state. Entire local file is bounded.
struct recent_game { std::string code,name; std::uint64_t time=0; };
inline std::string encode_recent(const recent_game& r) {
    auto code=parse_code(r.code);
    if(!code || !valid_name(r.name) || !r.time) return {};
    return "lc-rejoin/1\n"+*code+"\n"+r.name+"\n"+std::to_string(r.time)+"\n";
}
inline std::optional<recent_game> decode_recent(std::string_view in) {
    if(in.size()>128 || in.substr(0,12)!="lc-rejoin/1\n") return {};
    in.remove_prefix(12); auto a=in.find('\n'); if(a==in.npos) return {};
    auto code=parse_code(in.substr(0,a)); in.remove_prefix(a+1);
    a=in.find('\n'); if(a==in.npos || !valid_name(in.substr(0,a))) return {};
    std::string name(in.substr(0,a)); in.remove_prefix(a+1);
    if(in.size()<2 || in.back()!='\n' || in.size()>21) return {};
    in.remove_suffix(1); std::uint64_t time=0;
    for(char c:in) { if(c<'0' || c>'9' || time>(UINT64_MAX-unsigned(c-'0'))/10) return {}; time=time*10+c-'0'; }
    if(!code || !time) return {};
    return recent_game{*code,name,time};
}


// ---- Round 5b: presentation selectors computed from fields the broker already sends. No invented data. ----
// A Browse row's status kind. The broker has no "loading" flag, so that kind does not exist here: a room is Full,
// In progress (started), Closed (listed but not taking players) or In lobby.
enum class row_status { lobby, progress, closed, full };
inline constexpr const char* row_status_key[] = {"lobby","progress","closed","full"};
inline row_status classify_row(int players,int max_players,bool joinable,bool in_progress) {
    if(players>=0 && players>=max_players) return row_status::full;
    if(in_progress) return row_status::progress;
    return joinable ? row_status::lobby : row_status::closed;
}
// SAME only for a known equal token; OTHER only when both tokens are known and differ; otherwise CHECK.
enum class build_relation { same, other, check };
inline constexpr const char* build_key[] = {"same","other","check"};
inline build_relation relate_build(std::string_view row_token,std::string_view own_token) {
    if(row_token.empty() || own_token.empty()) return build_relation::check;
    return row_token==own_token ? build_relation::same : build_relation::other;
}
// Pip scene index 0..4: filled pips = players; unknown counts as 0.
inline int pip_count(int players) { return players<0 ? 0 : players>4 ? 4 : players; }
// "Updated N s ago" from the elapsed milliseconds (clock may wrap: callers pass now-then as uint32).
inline std::string age_text(std::uint32_t ms) {
    const auto s=ms/1000;
    if(s<120) return "Updated "+std::to_string(s)+" s ago";
    if(s<100*60) return "Updated "+std::to_string(s/60)+" min ago";
    return "Updated long ago";
}
// Refresh-age bar: five two-second segments (the 10 s idle refresh interval).
inline int age_segments(std::uint32_t ms) { const auto n=ms/2000; return n>5 ? 5 : int(n); }
// "2 hours ago" from stored epoch seconds. A future/zero timestamp (clock change) is "just now"/"" - never invented.
inline std::string relative_age(std::uint64_t now,std::uint64_t then) {
    if(!then) return {};
    if(then>now) return "just now";
    const auto d=now-then;
    auto unit=[](std::uint64_t n,const char* name) { return std::to_string(n)+" "+name+(n==1 ? "" : "s")+" ago"; };
    if(d<60) return "just now";
    if(d<3600) return unit(d/60,"minute");
    if(d<86400) return unit(d/3600,"hour");
    if(d<86400ull*60) return unit(d/86400,"day");
    return "a long time ago";
}
// Error cause selectors (round 15 completes INGAME-DESIGN section 3.3). The DLL shows exactly one lc_broker_err_<key>
// per failure; the id stays a redacted diagnostic. The first eight keep their broker/2 meaning.
enum class error_cause { code_not_found, room_full, version_mismatch, rate_limited, timeout, offline, refused, generic,
    pin_mismatch, code_expired, room_closed, test_mode, unsupported, busy, invalid_name, count };
inline constexpr const char* error_cause_key[] = {"code_not_found","room_full","version_mismatch","rate_limited","timeout","offline","refused","generic",
    "pin_mismatch","code_expired","room_closed","test_mode","unsupported","busy","invalid_name"};
static_assert(std::size(error_cause_key)==std::size_t(error_cause::count));
// What the failure's copy tells the player to do next. LC's copy names it; retry is the only one that is a control.
enum class next_action { retry, edit_code, browse, other_ways, back, wait };
inline constexpr const char* next_action_key[] = {"retry","edit_code","browse","other_ways","back","wait"};
struct cause_row { error_cause cause; next_action next; bool retry; };
// One row per cause, in enum order.
inline constexpr cause_row cause_table[] = {
    {error_cause::code_not_found,  next_action::edit_code,  false},
    {error_cause::room_full,       next_action::browse,     false},
    {error_cause::version_mismatch,next_action::back,       false},
    {error_cause::rate_limited,    next_action::retry,      true},
    {error_cause::timeout,         next_action::retry,      true},
    {error_cause::offline,         next_action::retry,      true},   // the copy also names Other ways (LAN / IP still work)
    {error_cause::refused,         next_action::back,       false},
    {error_cause::generic,         next_action::retry,      true},
    {error_cause::pin_mismatch,    next_action::back,       false},  // never an insecure bypass
    {error_cause::code_expired,    next_action::edit_code,  false},
    {error_cause::room_closed,     next_action::retry,      true},   // bounded: the client's shared limiter paces every retry
    {error_cause::test_mode,       next_action::back,       false},
    {error_cause::unsupported,     next_action::other_ways, false},
    {error_cause::busy,            next_action::wait,       false},  // a refusal of the moment: no Retry control (the screen's own action repeats it)
    {error_cause::invalid_name,    next_action::back,       false}};
static_assert(std::size(cause_table)==std::size_t(error_cause::count));
inline constexpr const cause_row& row_of(error_cause c) { return cause_table[std::size_t(c)]; }
inline error_cause classify_error(std::string_view id) {
    auto is=[&](std::initializer_list<std::string_view> list) { for(auto v:list) if(id==v) return true; return false; };
    if(is({"no_such_room","code_not_found"})) return error_cause::code_not_found;
    if(is({"room_closed","code_expired"})) return error_cause::code_expired;
    if(id=="room_full") return error_cause::room_full;
    if(id=="room_not_joinable") return error_cause::room_closed;
    if(id=="version_mismatch") return error_cause::version_mismatch;
    if(id=="rate_limited") return error_cause::rate_limited;
    if(id=="timeout") return error_cause::timeout;
    if(is({"broker_unreachable","worker_unavailable","send_queue_full","inbox_overflow"})) return error_cause::offline;
    if(is({"tls_pin_mismatch","invalid_certificate"})) return error_cause::pin_mismatch;
    if(is({"broker_rejected","bad_auth","auth_failed","unsupported_version"})) return error_cause::refused;
    if(id=="test_mode_on") return error_cause::test_mode;
    // Install guards: the room layer's own refusal ids and its two guard sentences (online_room_core::room_guard_refusal).
    if(is({"online_dependencies_unavailable","native_discovery_guard"}) || id.substr(0,17)=="online rooms need" || id.substr(0,20)=="joining a room needs") return error_cause::unsupported;
    if(id=="transport_identity_busy") return error_cause::busy;
    if(id=="invalid_room_name") return error_cause::invalid_name;
    return error_cause::generic;
}
// Retry exists only where trying the same thing again can help: never for a wrong or expired code, a full room, another
// build, a refused/unverified service, a TestMode/guard refusal or an invalid name; and never once the client is in its
// non-retryable (refused) phase.
inline bool cause_retryable(error_cause c) { return row_of(c).retry; }
inline next_action cause_next(error_cause c) { return row_of(c).next; }
// Ids the client may legitimately carry. Anything else is broker-controlled text and is never echoed.
inline bool known_id(std::string_view id) {
    for(std::string_view v:{"no_such_room","code_not_found","room_closed","code_expired","room_full","room_not_joinable","version_mismatch","rate_limited",
        "timeout","broker_unreachable","worker_unavailable","send_queue_full","inbox_overflow","tls_pin_mismatch","invalid_certificate","broker_rejected",
        "bad_auth","auth_failed","unsupported_version","test_mode_on","online_dependencies_unavailable","native_discovery_guard","transport_identity_busy",
        "invalid_room_name","malformed_reply","internal"}) if(id==v) return true;
    return false;
}
// "join: room_full": the operation and a known id; a guard sentence reads install_guard and any unknown text "other".
// No code, address, name or secret can reach the screen through it, whatever the broker sent.
inline std::string diagnostic(std::string_view operation,std::string_view id) {
    const bool guard=classify_error(id)==error_cause::unsupported && !known_id(id);
    return std::string(operation)+": "+(known_id(id) ? std::string(id) : guard ? std::string("install_guard") : std::string(id.empty() ? "none" : "other"));
}
inline const char* operation_name(int op) { return op==1 ? "list" : op==2 ? "host" : op==3 ? "join" : "request"; }
// Whole seconds still to wait (rounded up) for the shared limiter/backoff; 0 = a new request may start.
inline unsigned wait_seconds(std::uint32_t remaining_ms) { return (remaining_ms+999)/1000; }

// Native Host setup / admission failures (online_native_flow.inl native_flow_error). Every id the flow can raise is
// registered; each selects one inline notice. An unregistered id shows the general setup notice.
enum class native_failure { setup, discovery, refused, service, timeout };
inline constexpr const char* native_failure_notice[] = {"lc_notice_admission","lc_notice_discovery","lc_notice_refused","lc_notice_service_lost","lc_notice_timeout"};
struct native_failure_row { const char* why; native_failure kind; };
inline constexpr native_failure_row native_failure_table[] = {
    {"native_open_queue_full",native_failure::setup},{"stale_native_activation",native_failure::setup},{"native_open_request",native_failure::setup},
    {"native_open_exception",native_failure::setup},{"native_options_seed",native_failure::setup},{"native_options_unavailable",native_failure::setup},
    {"native_options_activation",native_failure::setup},{"native_options_initialization",native_failure::setup},{"native_options_frame",native_failure::setup},
    {"native_options_exception",native_failure::setup},{"invalid_native_settings",native_failure::setup},{"host_post_refused",native_failure::setup},
    {"native_setup_return_unavailable",native_failure::setup},{"host_post_exception",native_failure::setup},{"native_lobby_activation",native_failure::setup},
    {"native_lobby_skin",native_failure::setup},{"native_lobby_generation",native_failure::setup},{"native_host_refused",native_failure::setup},
    {"native_join_unavailable",native_failure::setup},
    {"room_generation_or_service_lost",native_failure::service},{"native_timeout",native_failure::timeout},
    {"native_search_refused",native_failure::discovery},{"discovery_ambiguous_or_wrong",native_failure::discovery},{"discovery_query_refused",native_failure::discovery},
    {"host_changed",native_failure::discovery},
    {"admission_call_refused",native_failure::refused},{"admission_refused",native_failure::refused},{"admission_callback_cleanup",native_failure::refused}};
inline const native_failure_row* find_native_failure(std::string_view why) {
    for(const auto& row:native_failure_table) if(why==row.why) return &row;
    return nullptr;
}
inline const char* native_notice(std::string_view why) {
    const auto* row=find_native_failure(why);
    return native_failure_notice[std::size_t(row ? row->kind : native_failure::setup)];
}
// A refusal's rule names: only the settings the two PCs possibly disagree on (rule-summary buckets collide), allowlisted.
inline std::string refusal_rules_text(std::string_view names) {
    std::string out;
    for(unsigned char c:names) { if(out.size()==44) break;out+=display_byte(c); }
    return out;
}

// Lobby inline presentation state, pure so the fixture can drive it: the departed-guest toast (named, six seconds,
// duplicate/late departures ignored) and the Hub's host-left panel latch.
inline constexpr std::uint32_t toast_ms=6000;
struct toast {
    std::string name;
    std::uint32_t since=0;
    bool active=false;
    void show(std::string_view who,std::uint32_t now) {
        if(who.empty()) return;
        if(active && name==who && now-since<toast_ms) return; // a repeated report is the same departure
        name.assign(who);since=now;active=true;
    }
    bool visible(std::uint32_t now) { if(active && now-since>=toast_ms) active=false;return active; }
    void clear() { active=false;name.clear(); }
};
struct roster_watch {
    unsigned count=0;bool primed=false;
    unsigned ids[4]{};char names[4][32]{};
    void clear() { count=0;primed=false; }
    // The first (lowest row) member of the previous snapshot that is no longer present; "" on the first sight, when
    // nobody left, or for the local member. Rows are matched by native id AND name so a reused id is not a match.
    std::string observe(const unsigned* now_ids,const char (*now_names)[32],unsigned n,unsigned local,bool local_known) {
        std::string gone;
        if(primed) for(unsigned i=0;i<count && gone.empty();++i) {
            if(local_known && ids[i]==local) continue;
            bool present=false;
            for(unsigned j=0;j<n;++j) if(now_ids[j]==ids[i] && std::strncmp(now_names[j],names[i],31)==0) { present=true;break; }
            if(!present) gone.assign(names[i],strnlen(names[i],31));
        }
        count=n>4 ? 4 : n;
        for(unsigned i=0;i<count;++i) { ids[i]=now_ids[i];std::strncpy(names[i],now_names[i],31);names[i][31]=0; }
        primed=true;return gone;
    }
};
// The removed PC's side of the host's native Kick (0.6 preview fix E). The host's Kick (0x6133a0(session, record, 0x10ce,
// 0x10cf, 0x10cf, 1)) sends the removed participant the 12-byte removal notice 0x2b (0x611550: its index, its own reason
// 0x10ce, flag 1; everyone else 0x10cf, flag 0). The guest's handler 0x612f60 removes the named record through 0x612c40
// (record, reason, flag), whose terminal clear calls our patched 0x612070 sites with EBP = 0x612c40's frame. So the kicked
// guest is told apart from a lost host by what it already received: 0x612c40 entered from the 0x2b handler (return address
// 0x612f8e), for THIS PC's own record, with the Kick's reason and flag. The native slow-player drop sends 0x10ce with flag 0
// and a host loss / the watchdog other reasons: neither matches. No peer message, protocol field or rule id is involved.
inline constexpr std::uint32_t removal_notice_return=0x612f8e, kick_reason=0x10ce, kick_flag=1;
// frame: 0x612c40's [ebp+0 .. ebp+0x10] (saved EBP, return address, record, reason, flag); local: that record is this PC's
// own participant (0x614290, record +0x1c bit 0).
inline bool kicked_by_host(const std::uint32_t (&frame)[5],bool local) {
    return local && frame[2]!=0 && frame[1]==removal_notice_return && frame[3]==kick_reason && (frame[4]&0xffu)==kick_flag;
}
struct host_left {
    bool active=false;
    std::uint64_t generation=0;
    void raise() { if(!active) { active=true;++generation; } } // a second report of the same loss changes nothing
    void dismiss() { active=false; }
};

inline bool accept_fresh(int p,std::uint32_t captured,std::uint32_t now) {
    return controller_valid(p) && now-captured<=1000;
}
struct entry_latch {
    std::uintptr_t source=0;
    std::uint32_t started=0;
    bool pending=false;
    void clear() { pending=false; source=0; }
    bool waiting(std::uintptr_t menu,std::uint32_t now) {
        if(pending && now-started>=2500) clear();
        return pending && source==menu;
    }
    void start(std::uintptr_t menu,std::uint32_t now) { source=menu;started=now;pending=true; }
};

// One slot, first action wins. Never retain a menu pointer for dereferencing.
// A close invalidates even when a subsequent allocation reuses its address.
struct mailbox {
    std::uintptr_t source=0;
    screen from=screen::none;
    int action=0;
    void clear() { source=0; from=screen::none; action=0; }
    bool post(int value, std::uintptr_t menu, screen s) {
        if(action || !menu || value<int(intent::enter) || value>int(intent::last)) return false;
        if(s==screen::party) {
            if(!party_accepts(value)) return false;
            source=menu; from=s; action=value; return true;
        }
        if(value>62) return false;
        // Entry is a native queued openmenu request with captured real input;
        // scripts cannot manufacture it. Value 1 remains reserved.
        if(lobby(s)) {
            if(value!=int(intent::copy_code) && value!=int(intent::dismiss) &&
               value!=int(intent::back) && !(s==screen::lobby_host && value==int(intent::confirm_leave))) return false;
        } else if(!custom(s) || value==int(intent::enter) || (value>=int(intent::copy_code) && value<=int(intent::confirm_leave))) return false;
        if(value>=18 && value<=31) {
            const bool browse=value==18 || (value>=22 && value<=31);
            if(value==20 ? !custom(s) : browse ? s!=screen::browse : s!=screen::host) return false;
        }
        source=menu; from=s; action=value; return true;
    }
    int take(std::uintptr_t menu, screen s) {
        const int result=source==menu && from==s ? action : 0;
        clear(); return result;
    }
};

// Tests drive this same ordering with fake services and injected failures.
// Native initializers are idempotent; never proceed past a failed phase.
template<class Engine> bool initialize(Engine& e, int p) {
    if(!controller_valid(p) || !e.preflight(p)) return false;
    if(!e.bind(p) || !e.network()) return false;
    if(!e.name() || !e.session() || !e.campaign()) return false;
    if(!e.mode() || !e.persist()) return false;
    return true;
}
inline bool registration_fits(const script_functions_rules::overflow_table& table, int capacity) {
    return capacity>script_functions_rules::stock_capacity && table.count()<table.capacity() && !table.find(function_name);
}

// ---- Round 8: the in-session Party screen's pure model (docs/online-party-menu.md) --------------------------------------
// One row per displayed card, in card order: the native participants by index (the host is 0), then broker-only pending
// members. Facts only; nothing here is read from a peer's claim except what online_room already filtered.
struct party_row { int index=-1; std::string name; bool self=false,host=false,pending=false; };
struct party_roster {
    std::vector<party_row> rows;
    bool hosting=false,session=false; // this PC hosts the native session / a native session is running
    // A row a Kick can name: a remote, admitted participant (never the host index 0, never this PC, never pending).
    bool kickable(std::size_t card) const {
        if(card>=rows.size() || !hosting || !session) return false;
        const auto& r=rows[card];
        return !r.pending && !r.self && !r.host && r.index>0 && r.index<4;
    }
    const party_row* find(int index,const std::string& name) const {
        for(const auto& r:rows) if(!r.pending && r.index==index && r.name==name) return &r;
        return nullptr;
    }
};
enum class party_confirm { none, leave, kick };
enum class party_act { none, native_back, copy, dismissed, opened_leave, leave_run, opened_kick, kick_run, stale, refused };
struct party_result { party_act act=party_act::none; int kick_index=-1; };
// A Leave or Kick is handed to the native lifecycle exactly once: `departed` stops every later action of this menu, and a
// Kick's index stays marked until the roster stops listing it (a repeated or replayed Kick never reaches the transport twice).
struct party_model {
    party_confirm confirm=party_confirm::none;
    int kick_index=-1; std::string kick_name;
    bool departed=false;
    unsigned kicked=0; // bit n: a Kick of native index n was handed over and the roster still lists it
    void reset() { *this=party_model{}; }
    // The roster moved: drop a Kick confirmation whose target is gone or is now somebody else, and forget the marks of
    // indices that left. True when a confirmation was dropped.
    bool refresh(const party_roster& r) {
        for(int i=0;i<4;++i) if((kicked>>i)&1) { bool listed=false; for(const auto& row:r.rows) listed=listed || (!row.pending && row.index==i); if(!listed) kicked&=~(1u<<i); }
        if(confirm!=party_confirm::kick) return false;
        if(r.hosting && r.session && r.find(kick_index,kick_name)) return false;
        confirm=party_confirm::none; kick_index=-1; kick_name.clear(); return true;
    }
    party_result apply(int value,const party_roster& r) {
        party_result out;
        // After a Leave was handed over, Back/Resume still reach the native Back (the queued quit may never run); nothing else does.
        if(departed) { if(value==int(intent::back) || value==int(intent::party_resume)) out.act=party_act::native_back; return out; }
        const bool open=confirm!=party_confirm::none;
        auto close=[&] { confirm=party_confirm::none; kick_index=-1; kick_name.clear(); };
        switch(value) {
        case int(intent::back): if(open) { close(); out.act=party_act::dismissed; } else out.act=party_act::native_back; return out;
        case int(intent::dismiss): if(open) { close(); out.act=party_act::dismissed; } return out;
        case int(intent::party_resume): out.act=open ? party_act::refused : party_act::native_back; return out;
        case int(intent::party_invite): out.act=open ? party_act::refused : party_act::copy; return out;
        case int(intent::party_leave):
            if(open) { out.act=party_act::refused; return out; }
            // Outside a native session there is nothing to end for anyone: the native quit runs as the pause menu's own Quit does.
            if(!r.session) { departed=true; out.act=party_act::leave_run; return out; }
            confirm=party_confirm::leave; out.act=party_act::opened_leave; return out;
        case int(intent::party_leave_yes):
        case int(intent::party_kick_yes): {
            // ONE shared Yes item (67) completes whichever confirmation is open; 69 stays accepted for the kick.
            if(confirm==party_confirm::leave && value==int(intent::party_leave_yes)) { close(); departed=true; out.act=party_act::leave_run; return out; }
            if(confirm!=party_confirm::kick) { out.act=party_act::refused; return out; }
            const int index=kick_index; const auto name=kick_name; close();
            if(!r.hosting || !r.session || !r.find(index,name) || ((kicked>>index)&1)) { out.act=party_act::stale; return out; }
            kicked|=1u<<index; out.act=party_act::kick_run; out.kick_index=index; return out;
        }
        default: break;
        }
        if(value>=int(intent::party_kick_first) && value<=int(intent::party_kick_last)) {
            const auto card=std::size_t(value-int(intent::party_kick_first));
            if(open || !r.kickable(card) || ((kicked>>r.rows[card].index)&1)) { out.act=party_act::refused; return out; }
            confirm=party_confirm::kick; kick_index=r.rows[card].index; kick_name=r.rows[card].name; out.act=party_act::opened_kick; return out;
        }
        out.act=party_act::refused; return out; // Propose load (65) and every reserved value
    }
};
}
