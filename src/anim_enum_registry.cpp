#include "anim_enum_registry.hpp"
#include "anim_enum_names.hpp"
#include <set>

namespace anim_enum_registry
{
bool registry::load(std::string_view input, std::string& error)
{
    error.clear();
    std::vector<entry> pending;
    std::set<std::string> seen;
    for(auto n:native_names)seen.emplace(n);
    std::uint32_t hash=2166136261u;
    auto append=[&](std::string_view name) {
        for(const unsigned char c:name){hash^=c;hash*=16777619u;}
        hash^='\n';hash*=16777619u;
        pending.push_back({std::string(name),static_cast<int>(210+pending.size())});
        seen.emplace(name);
    };
    for(auto n:builtin_names)append(n);
    std::size_t line=0;
    while(!input.empty())
    {
        ++line;
        auto end=input.find('\n');
        auto text=input.substr(0,end);
        input=end==input.npos?std::string_view{}:input.substr(end+1);
        // Reject NULs even in comments: a text file must never hide a second spelling.
        if(text.find('\0')!=text.npos){error="NUL in sidecar line "+std::to_string(line);return false;}
        text=text.substr(0,text.find('#'));
        const auto first=text.find_first_not_of(" \t\r");
        if(first==text.npos)continue;
        text=text.substr(first,text.find_last_not_of(" \t\r")-first+1);
        bool valid=text.size()>=4&&text.size()<=63&&text.starts_with("ea_");
        if(valid)for(char c:text.substr(3))valid=valid&&((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_');
        if(!valid){error="bad animation enum name on line "+std::to_string(line)+": "+std::string(text);return false;}
        if(seen.count(std::string(text))){error="duplicate animation enum: "+std::string(text);return false;}
        if(pending.size()>=anim_enum_layout::raised.enums-210){error="animation enum capacity exceeded at "+std::string(text);return false;}
        append(text);
    }
    std::sort(pending.begin(),pending.end(),[](const auto& a,const auto& b){return a.name<b.name;});
    sorted_=std::move(pending);hash_=hash;
    return true;
}
int registry::lookup(std::string_view name) const
{
    const auto it=std::lower_bound(sorted_.begin(),sorted_.end(),name,[](const auto& e,auto n){return e.name<n;});
    return it!=sorted_.end()&&it->name==name?it->id:-1;
}
}
