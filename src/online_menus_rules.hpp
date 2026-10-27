#pragma once
#include "ini_rules.hpp"
#include "script_functions_rules.hpp"
#include <cstdint>
#include <array>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace online_menus_rules {
// This is an asset/API contract, not a release identity or gameplay rule.
inline constexpr char capability[] = "lc-online-menus/1\n";
// Additive input API probe: LC must require this before selecting intents 9..62.
inline constexpr char input_capability[] = "lc-online-input/1";
inline constexpr char lobby_capability[] = "lc-online-lobby/1";
inline constexpr char broker_capability[] = "lc-online-broker/1";
inline constexpr char function_name[] = "lcOnlineIntent";
enum class intent : int { enter=1, host, code, browse, other, back, unavailable, dismiss, edit_name, paste, erase, clear, done, rejoin, invalid_key,
    copy_code=16, confirm_leave=17, refresh=18, open_room=19, retry=20, visibility=21,
    previous=22, next=23, row_first=24, row_last=30, join_selected=31, symbol_first=32, symbol_last=62 };
enum class screen { none, main, hub, host, code, browse, lobby_host, lobby_join };
inline screen identify(std::string_view name) {
    if(name=="main") return screen::main;
    if(name=="lc_online") return screen::hub;
    if(name=="lc_host") return screen::host;
    if(name=="lc_code") return screen::code;
    if(name=="lc_browse") return screen::browse;
    if(name=="host") return screen::lobby_host;
    if(name=="join") return screen::lobby_join;
    return screen::none;
}
inline bool custom(screen s) { return s>=screen::hub && s<=screen::browse; }
inline bool lobby(screen s) { return s==screen::lobby_host || s==screen::lobby_join; }
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
template<class Clipboard> std::optional<std::string> read_clipboard(Clipboard& api) {
    if(!api.open()) return {};
    struct closer { Clipboard& api; ~closer() { api.close(); } } close{api};
    const auto bytes=api.size();
    if(bytes<2 || bytes>4096 || bytes%2) return {};
    const auto* data=api.lock(); if(!data) return {};
    struct unlocker { Clipboard& api; ~unlocker() { api.unlock(); } } unlock{api};
    std::string text;
    const auto count=std::min<std::size_t>(bytes/2,257);
    for(std::size_t i=0;i<count;++i) {
        const auto c=data[i];
        if(!c) {
            // Zero padding is fine; a hidden second string is not a code.
            for(std::size_t tail=i+1;tail<bytes/2;++tail) if(data[tail]) return {};
            return parse_code(text);
        }
        if(c>127) return {};
        text+=char(c);
    }
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
        if(action || !menu || value<int(intent::enter) || value>62) return false;
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
}
