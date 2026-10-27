#pragma once
#include "ini_rules.hpp"
#include "script_functions_rules.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace online_menus_rules {
// This is an asset/API contract, not a release identity or gameplay rule.
inline constexpr char capability[] = "lc-online-menus/1\n";
inline constexpr char function_name[] = "lcOnlineIntent";
enum class intent : int { enter=1, host, code, browse, other, back, unavailable, dismiss };
enum class screen { none, main, hub, host, code, browse };
inline screen identify(std::string_view name) {
    if(name=="main") return screen::main;
    if(name=="lc_online") return screen::hub;
    if(name=="lc_host") return screen::host;
    if(name=="lc_code") return screen::code;
    if(name=="lc_browse") return screen::browse;
    return screen::none;
}
inline bool custom(screen s) { return s>=screen::hub; }
inline bool controller_valid(int p) { return p>=0 && p<4; }
inline bool enabled(std::optional<std::string_view> value) {
    return value && ini_rules::value_text(*value)=="1";
}
// Renderer controls are separate from printf escaping. Percent remains literal:
// the native variadic setter MUST always receive the constant format "%s".
inline std::string display(std::string_view in, std::size_t limit=96) {
    std::string out;
    for(unsigned char c:in) {
        if(out.size()==limit) break;
        out += c<32 || c>126 || c=='~' || c=='$' || c=='#' || c=='|' ? '?' : char(c);
    }
    return out;
}
inline bool valid_name(std::string_view s) {
    if(s.empty() || s.size()>31 || s.find_first_not_of(' ')==std::string_view::npos) return false;
    for(unsigned char c:s) if(c<32 || c>126) return false;
    return true;
}

// One slot, first action wins. Never retain a menu pointer for dereferencing.
// A close invalidates even when a subsequent allocation reuses its address.
struct mailbox {
    std::uintptr_t source=0;
    screen from=screen::none;
    int action=0;
    void clear() { source=0; from=screen::none; action=0; }
    bool post(int value, std::uintptr_t menu, screen s) {
        if(action || !menu || value<int(intent::enter) || value>int(intent::dismiss)) return false;
        // Entry is a native queued openmenu request with captured real input;
        // scripts cannot manufacture it. Value 1 remains reserved.
        if(!custom(s) || value==int(intent::enter)) return false;
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
