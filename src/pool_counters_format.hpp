#pragma once
#include "pool_counter_rules.hpp"
#include <string>

namespace pool_counters_format {
inline std::string number(std::int32_t n) {return n<0 ? "null" : std::to_string(n);}
inline std::string json(const pool_counter_rules::snapshot& s,bool observers) {
    std::string out="{\"_meta\":{\"zone\":\""+std::string(s.zone.data())+"\",\"active\":"+(s.active?"true":"false")+
        ",\"epoch\":"+std::to_string(s.epoch)+",\"refusals_observed\":"+(observers?"true":"false")+
        ",\"dropped_zone_logs\":"+std::to_string(s.dropped)+"}";
    for(std::size_t i=0;i<s.size;++i) {
        const auto& c=s.pools[i];
        out+=",\""+std::string(c.name)+"\":{\"cap\":"+std::to_string(c.cap)+",\"live\":"+number(c.live)+
            ",\"peak_zone\":"+number(c.peak_zone)+",\"peak_session\":"+number(c.peak_session)+
            ",\"refused\":"+std::to_string(c.refused)+",\"refused_zone\":"+std::to_string(c.refused_zone)+
            ",\"corrupt\":"+(c.corrupt?"true":"false");
        if(std::string_view(c.name)=="values")
            out+=",\"null_returns\":"+std::to_string(c.refused)+",\"null_returns_zone\":"+std::to_string(c.refused_zone);
        if(c.particle_array)
            out+=",\"exceeded\":"+std::to_string(c.exceeded)+",\"exceeded_zone\":"+std::to_string(c.exceeded_zone);
        out+="}";
    }
    return out+"}";
}
inline std::string line(const pool_counter_rules::snapshot& s) {
    std::string out="pools "+std::string(s.zone.data())+":";
    for(std::size_t i=0;i<s.size;++i) {
        const auto& c=s.pools[i];
        out+=" "+std::string(c.name)+" "+(c.live<0?"-":std::to_string(c.live))+"/"+std::to_string(c.cap)+
            " peak "+(c.peak_zone<0?"-":std::to_string(c.peak_zone));
        if(c.peak_zone>=0 && static_cast<std::uint32_t>(c.peak_zone)>=c.cap)out+=" SAT";
        if(c.corrupt)out+=" CORRUPT";
        out+=" refused "+std::to_string(c.refused_zone);
        if(std::string_view(c.name)=="values")out+=" null "+std::to_string(c.refused_zone);
        if(c.particle_array)out+=" exceeded "+std::to_string(c.exceeded_zone);
        out+=";";
    }
    return out;
}
}
