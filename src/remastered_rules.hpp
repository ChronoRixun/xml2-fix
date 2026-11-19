#pragma once
#include <algorithm>
#include <optional>
#include <string_view>

namespace remastered_rules {
struct settings {
    bool enabled=false;
    unsigned anisotropy=0, msaa_samples=4;
    bool centered_hud=false, trace=false;
    bool anisotropy_on_point_art=false;
};
inline std::optional<unsigned> choice(std::string_view text, std::initializer_list<unsigned> allowed) {
    for (unsigned n:allowed) {
        if ((n==0 && text=="0") || (n==1 && text=="1") || (n==2 && text=="2") ||
            (n==4 && text=="4") || (n==6 && text=="6") || (n==8 && text=="8") || (n==16 && text=="16")) return n;
    }
    return {};
}
constexpr int saved_samples(int index) { return index==0 ? 0 : index==1 ? 2 : index==2 ? 4 : index==3 ? 6 : index==4 ? 8 : -1; }
constexpr bool known_samples(unsigned samples) { return samples==0 || (samples>=2 && samples<=16); }
}
