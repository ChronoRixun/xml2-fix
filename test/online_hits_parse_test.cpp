// lc-online-hits/1 through the DLL's own parser (src/online_menus_hits_rules.hpp, the code online_menus.cpp reads
// Data/lc_online_hits.txt with). No arguments: the built-in accept / refuse cases. With file arguments: each file is parsed
// exactly as the DLL would and its entries printed ("ENTRY menu item left bottom right top"); exit 1 if any file is refused
// (the DLL would then turn the mouse hit boxes off and log "REFUSED"). Used by the legends-classic builder tests to feed the
// builder's real output through this parser.
#include "online_menus_hits_rules.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace H = online_menus_hits;

static int check_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { std::printf("REFUSED %s: cannot open\n", path); return 1; }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (text.size() > H::max_file) { std::printf("REFUSED %s: larger than 256 KiB\n", path); return 1; }
    H::table table; std::string why;
    if (!H::parse(text, table, why)) { std::printf("REFUSED %s: %s\n", path, why.c_str()); return 1; }
    std::printf("OK %s %zu entries\n", path, table.entries.size());
    for (const auto& e : table.entries)
        std::printf("ENTRY %s %s %.4f %.4f %.4f %.4f\n", e.menu.c_str(), e.item.c_str(), e.rect.left, e.rect.bottom, e.rect.right, e.rect.top);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1) { int bad = 0; for (int i = 1; i < argc; ++i) bad |= check_file(argv[i]); return bad; }
    const std::string good = std::string(H::contract) + "\nlc_online host 34.4 253.2 247.2 277.8\nhost text_startgame 34.4 46.8 180 71.4\n";
    H::table table; std::string why;
    if (!H::parse(good, table, why) || table.entries.size() != 2) { std::puts("FAIL good table refused"); return 1; }
    const std::string bad[] = { "\xEF\xBB\xBF" + good, std::string(H::contract) + "\r\nlc_online host 1 2 3 4\r\n",
        good + "lc_online host 1 2 3 4\n", std::string(H::contract) + "\nlc_online ho-st 1 2 3 4\n", std::string(H::contract) + "\nlc_online host 1 2 3\n" };
    for (const auto& text : bad) { H::table t; std::string w; if (H::parse(text, t, w)) { std::puts("FAIL bad table accepted"); return 1; } }
    std::puts("online hits parser PASS");
    return 0;
}
