#pragma once
// Authored-hidden model scenes of the co-op screens (060q live failures 5 / 6 / 3 / 4 / 9 / 10; docs/online-menus.md
// "Hidden model scenes").
//
// XMen2.exe's MENU_ITEM_MODEL (vtable 0x6a0304) cannot show a scene that its XMLB authors `hide`: the base item read
// 0x5bc7a0 sets +0x54 bit 2 when the attribute is present (any value), BEFORE the model read 0x5c5080 hands the authored
// model name to vt+0x38 (0x5bbdd0 -> vt+0x3c = 0x5c4c50). While hidden, 0x5c4c50 loads ui/models/m_invis.igb instead and
// RELEASES the authored name (0x575980), with no save slot. The model setHidden 0x5c4b90 restores on unhide only the name it
// saved itself at +0x94 when it hid a VISIBLE item; for an item hidden from the start +0x94 is null and 0x5bc3f0 returns
// at once (its `cmp [name],0`). So every hidden-authored card frame, badge, READY / ping / build scene, status cell, Leave
// panel, error and strip scene stayed m_invis whatever the DLL asked (live 060q), while text items (setter 0x5bcdb0, the
// bit only) showed. Visible-authored scenes (PRIVATE, the ready summary) hid and came back because 0x5c4b90 saved them.
//
// The builder lists every hidden-authored model item with its model in Data/lc_online_models.txt (this contract). When the
// DLL shows such an item and its saved name is null, it loads the listed model through the item's own vt+0x38 - the same
// call the XMLB read makes for a visible item - so the engine's resource and layout code stay retail; from then on
// 0x5c4b90's own save / restore works.
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace online_menus_models {
inline constexpr char contract[] = "lc-online-models/1";
inline constexpr std::size_t max_entries = 2048;

struct entry { std::string menu, item, model; };
struct table { std::vector<entry> entries; };

inline bool name_ok(std::string_view s) {
    if(s.empty() || s.size()>63) return false;
    for(const char c:s) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return false;
    return true;
}
// A model resource the menus can load: relative ui/models/ path, path characters only, no extension, no "..".
inline bool model_ok(std::string_view s) {
    if(s.size()<11 || s.size()>127 || s.substr(0,10)!="ui/models/" || s.find("..")!=std::string_view::npos || s.back()=='/') return false;
    for(const char c:s) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='/')) return false;
    return true;
}
// The whole file or nothing: any malformed line refuses the table.
inline bool parse(std::string_view text,table& out,std::string& why) {
    out.entries.clear();
    auto next_line=[&](std::string_view& line)->bool {
        if(text.empty()) return false;
        const auto n=text.find('\n'); if(n==std::string_view::npos) { why="last line not terminated"; return false; }
        line=text.substr(0,n); text.remove_prefix(n+1); return true;
    };
    std::string_view line;
    if(!next_line(line) || line!=contract) { if(why.empty()) why="not "+std::string(contract); return false; }
    while(next_line(line)) {
        std::array<std::string_view,3> f{}; std::size_t n=0;
        while(!line.empty()) {
            const auto sp=line.find(' ');
            const auto token=line.substr(0,sp);
            if(token.empty() || n==f.size()) { why="bad fields"; out.entries.clear(); return false; }
            f[n++]=token; if(sp==std::string_view::npos) break; line.remove_prefix(sp+1);
            if(line.empty()) { why="bad fields"; out.entries.clear(); return false; }
        }
        if(n!=3 || !name_ok(f[0]) || !name_ok(f[1]) || !model_ok(f[2])) { why="bad entry"; out.entries.clear(); return false; }
        for(const auto& e:out.entries) if(e.menu==f[0] && e.item==f[1]) { why="duplicate entry"; out.entries.clear(); return false; }
        if(out.entries.size()==max_entries) { why="too many entries"; out.entries.clear(); return false; }
        out.entries.push_back({std::string(f[0]),std::string(f[1]),std::string(f[2])});
    }
    if(!why.empty()) { out.entries.clear(); return false; }
    if(out.entries.empty()) { why="no entries"; return false; }
    return true;
}
inline const entry* find(const table& t,std::string_view menu,std::string_view item) {
    for(const auto& e:t.entries) if(e.menu==menu && e.item==item) return &e;
    return nullptr;
}
}
