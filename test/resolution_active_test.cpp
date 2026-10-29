// Synthetic fixture from the 060s mode-list facts; no device, network or game.
#include "resolution_rules.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace display_rules;
    const size desktop{2560,1440};
    std::vector<d3d8::display_mode> adapter;
    // 060s g1: the retained 20 start at 1152x864; 1024x768 was discarded.
    for(const auto s : {size{640,480},size{800,600},size{1024,768},size{1152,864},size{1176,664},
        size{1280,720},size{1280,768},size{1280,800},size{1280,960},size{1280,1024},size{1360,768},size{1366,768},
        size{1440,900},size{1440,1080},size{1600,900},size{1600,1024},size{1600,1200},size{1680,1050},
        size{1920,1080},size{1920,1200},size{1920,1440},size{2560,1080},size{2560,1440}})
        adapter.push_back({s.width,s.height,60,d3d8::format_x8r8g8b8});
    const auto has=[](const auto& list,size wanted) {
        return std::count_if(list.begin(),list.end(),[&](const auto& m){return m.width==wanted.width && m.height==wanted.height;});
    };
    const auto old=curate_modes(adapter,desktop,60,std::nullopt,20);
    assert(old.size()==20 && !has(old,{1024,768})); // negative control: the old unprotected path
    for(const auto setting : {resolution_list::stock,resolution_list::game,resolution_list::all}) {
        for(const size active : {size{640,480},size{800,600},size{1024,768},size{1280,720},size{2560,1440}}) {
            for(const auto cap : {1u,20u,64u}) {
                const auto list=resolution_rules::video_list(adapter,desktop,60,active,mode::windowed,setting,cap);
                assert(has(list,active)==1);
                assert(list.size()<=(setting==resolution_list::stock ? 20u : cap));
                assert(std::is_sorted(list.begin(),list.end(),[](const auto& a,const auto& b){
                    return a.width!=b.width ? a.width<b.width : a.height<b.height;
                }));
            }
        }
    }
    assert(curate_modes(adapter,desktop,60,size{1024,768},0).empty());
    assert(curate_modes({},desktop,60,size{1024,768},1).size()==1);
    assert(has(curate_modes({},desktop,60,size{1024,768},1),{1024,768}));
    std::cout<<"PASS 060s active resolution retained, unique, sorted and bounded (all settings/caps)\n";
}
