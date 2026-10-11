#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <charconv>
namespace mold_combat_rules {
struct stage_binding {std::string flag;int first=0,width=0;};
inline std::optional<stage_binding> parse_binding(std::string_view text){
    const auto first=text.find(':');if(first==0||first>32)return std::nullopt;
    const auto second=text.find(':',first+1);if(second==std::string_view::npos)return std::nullopt;
    for(char c:text.substr(0,first))if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return std::nullopt;
    stage_binding result{std::string(text.substr(0,first))};
    const auto a=std::from_chars(text.data()+first+1,text.data()+second,result.first);
    const auto b=std::from_chars(text.data()+second+1,text.data()+text.size(),result.width);
    if(a.ec!=std::errc{}||a.ptr!=text.data()+second||b.ec!=std::errc{}||b.ptr!=text.data()+text.size()||
        result.first<1||result.width<1||result.width>32||result.first>33-result.width)return std::nullopt;
    return result;
}
enum class core_action { wait, abandon, fire };
enum class companion_action { leave, engage, engage_floor, follow };
inline companion_action companion(bool active,bool ai_controlled,float hero_z,float member_z,float boss_range_squared,int controller_kind){
    if(hero_z<-100&&boss_range_squared<300.f*300.f)return companion_action::engage_floor;
    if(active)return companion_action::engage;
    if(!ai_controlled)return companion_action::leave;
    if(hero_z>-100&&member_z>-100&&boss_range_squared<350.f*350.f&&controller_kind==1)return companion_action::engage;
    return companion_action::follow;
}
inline core_action ready_core(bool living_hero,bool valid_target,bool node_available){
    if(!living_hero)return core_action::wait;
    if(!valid_target)return core_action::abandon;
    return node_available?core_action::fire:core_action::wait;
}
struct deadlines {
    std::array<float,4> value{};
    explicit deadlines(float time=0){value[0]=time+10;}
    int floor_due(float time)const{
        for(int i=0;i<3;++i)if(time>value[i])return i;
        return -1;
    }
    void floor_result(int selected,float time,bool available){
        if(selected>=0&&selected<3)value[selected]=time+(available?10.f:.5f);
    }
};
inline std::uint32_t ranged_choice(float random){return random<.25f?0x90:random<.5f?0xc0:0x180;}
}
