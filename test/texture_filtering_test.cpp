#ifdef AF_SAMPLER_RULES_HEADER
#include AF_SAMPLER_RULES_HEADER
#else
#include "texture_filtering_rules.hpp"
#endif
#include "texture_filtering.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <tuple>
#include <functional>
namespace { unsigned logs=0; }
namespace logger { void write(const char*,...) {++logs;} }
using namespace texture_filtering;
struct fake_api {
    std::array<std::array<unsigned,22>,8> states{};
    std::array<void*,8> objects{};
    unsigned sets=0,gets=0,fail_set=0,fail_get=0;bool bad_texture=false;
    std::vector<std::tuple<unsigned,unsigned,unsigned>> calls;
    std::function<void()> on_set;
    fake_api() { for(auto& s:states) {s[16]=2;s[17]=2;s[18]=1;s[21]=1;} }
    bool get(unsigned stage,unsigned key,unsigned& value) {++gets;value=states[stage][key];return gets!=fail_get;}
    bool set(unsigned stage,unsigned key,unsigned value) {
        ++sets;calls.push_back({stage,key,value});if(sets==fail_set) return false;
        if(on_set) on_set(); // wrapper may draw before its pending native write
        states[stage][key]=value;return true;
    }
    inspection inspect(unsigned stage,void* expected) {
        return objects[stage]!=expected?inspection::failed:bad_texture?inspection::excluded:inspection::eligible;
    }
};
source_identity world() {return source_identity::from_path("assets/tilesets/haarp/floor.png");}
d3d8::caps8 caps(unsigned max=8) {
    d3d8::caps8 result{};result.raster_caps=0x20000;result.texture_filter_caps=0x400;result.max_anisotropy=max;
    result.max_texture_blend_stages=8;result.max_simultaneous_textures=8;return result;
}
int main() {
    remastered_rules::settings config;configure(config);assert(logs==0);
    config.enabled=true;config.anisotropy=0;configure(config);assert(logs==0);
    config.anisotropy=8;configure(config);assert(logs==1); // no invented positive manifest
    device_state state;
    for(unsigned max:{8u,3u,1u}) {state.reset(1,16,caps(max));assert(state.amount==(max<2 ? 0 : max));}
    auto absent=caps();absent.raster_caps=0;state.reset(1,8,absent);assert(!state.amount);
    absent=caps();absent.texture_filter_caps=0;state.reset(1,8,absent);assert(!state.amount);
    associations records;
    auto& first=records.create(0x1234,0x10001);const auto allocation=first.allocation;
    void* texture=reinterpret_cast<void*>(0x9876);void* device=reinterpret_cast<void*>(0x4567);
    assert(records.replace_source(0x1234,0x10001,world(),texture,5,1,7));
    const association* r=records.find(0x1234,0x10001);assert(r && r->allocation==allocation);
    binding stages[]={{0,r},{1,r}};
    fake_api api;api.objects[0]=texture;api.objects[1]=texture;
    auto native=api.states;
    state.reset(7,16,caps(3));
    int draws=0;
    auto run=[&](auto call) {return draw(api,state,device,stages,2,call);};
    // Unknown/direct/BXIG calls never sample, query, or write.
    assert(run([&] {++draws;return S_FALSE;})==S_FALSE && api.sets==0 && api.gets==0);
    {
        draw_scope scope({device,0x1234,pass::world_material});
        assert(run([&] {
            ++draws;assert(api.states[0][17]==3 && api.states[0][21]==3);
            assert(api.states[0][16]==2 && api.states[0][18]==1);
            return E_FAIL;
        })==E_FAIL);
        assert(api.states==native); // HRESULT failure still restores both samplers
        const unsigned before=api.sets;
        {
            draw_scope ui_scope({device,0x1234,pass::ui});
            run([&] {++draws;assert(api.states==native);return S_OK;});
        }
        assert(api.sets==before);
        // A point asset remains excluded even when effective native filters became linear.
        auto point=*r;point.authored_min=0;stages[1].texture=&point;
        run([&] {assert(api.states[0][17]==3 && api.states[1][17]==2);return S_OK;});
        assert(api.states==native);
        const char* excluded_paths[]{"assets/tilesets/ui/foo.png","assets/tilesets/fonts/foo.png","art/models/actors/hero/glyph.png",
            "assets/tilesets/haarp/floor_atlas.png","textures/ui/lch.png","","unknown.png"};
        for(unsigned exclusion=0;exclusion<7;++exclusion) {
            auto excluded=*r;
            excluded.source=source_identity::from_path(excluded_paths[exclusion]);
            stages[0].texture=&excluded;stages[1].texture=nullptr;const auto calls=api.sets;
            run([&] {assert(api.states==native);return S_OK;});assert(api.sets==calls);
        }
        // The second switch changes only world POINT minification. MAG/MIP stay
        // authored, and both switches must admit the stage before writing.
        auto point_art=*r;point_art.authored_min=3;stages[0]={0,&point_art};stages[1].texture=nullptr;
        api.states[0][17]=1;auto point_native=api.states;
        run([&] {assert(api.states==point_native);return S_OK;});
        state.on_point_art=true;
        run([&] {assert(api.states[0][17]==3 && api.states[0][16]==2 && api.states[0][18]==1);return S_OK;});
        assert(api.states==point_native);state.on_point_art=false;api.states=native;
        stages[0].texture=r;stages[1].texture=nullptr;
        // Physical stage remapping; MIPFILTER remains native.
        stages[0].stage=3;api.objects[3]=texture;
        run([&] {assert(api.states[0][17]==2 && api.states[3][17]==3 && api.states[3][18]==1);return S_OK;});
        stages[0].stage=0;
        api.bad_texture=true;
        run([&] {assert(api.states==native);return S_OK;});
        api.bad_texture=false;
        assert(!state.rejected_stage[0]); // unsuitable texture must not poison the physical stage
        run([&] {assert(api.states[0][17]==3);return S_OK;});
        api.fail_set=api.sets+2; // MIN succeeded; MAX fails: native before forwarding
        run([&] {assert(api.states==native);return S_OK;});
        assert(state.disabled && !state.state_invalid && api.states==native);
        // Reset restores eligibility; query failure rejects until the next generation.
        state.reset(7,8,caps());api.fail_set=0;api.fail_get=api.gets+1;
        run([&] {assert(api.states==native);return S_OK;});assert(state.rejected_stage[0]);
        state.reset(7,8,caps());api.fail_get=0;api.fail_set=api.sets+3; // restoration MIN fails
        run([&] {return S_OK;});assert(state.disabled && state.state_invalid);
        api.states=native;api.fail_set=0;state.reset(7,8,caps());
        // Wrapper re-entry restores outer values before the nested UI/device call and disables AF.
        run([&] {
            assert(api.states[0][17]==3);
            draw_scope ui_scope({device,0x1234,pass::ui});
            return run([&] {assert(api.states==native);return S_FALSE;});
        });
        assert(state.disabled && api.states==native);
        // Reentry from each APPLY/RESTORE setter sees native states. A pending
        // outer override can complete after nested cleanup; it must be unwound again.
        for(unsigned trigger:{1u,2u,3u,4u}) {
            api.states=native;api.sets=0;api.fail_set=api.fail_get=0;state.reset(7,8,caps());
            bool seen=false;unsigned nested=0;
            api.on_set=[&] {
                if(seen || api.sets!=trigger) return;
                seen=true;draw_scope raw({});
                assert(run([&] {++nested;assert(api.states==native);return S_FALSE;})==S_FALSE);
            };
            run([&] {if(trigger<=2) assert(api.states==native);else assert(api.states[0][17]==3);return S_OK;});
            api.on_set={};assert(seen && nested==1 && state.disabled && api.states==native);
        }
        // A wrapper that draws from every cleanup setter cannot recurse forever.
        // Unclean nested submissions fail closed; every forwarded call is native.
        api.states=native;state.reset(7,8,caps());api.sets=0;
        unsigned callbacks=0,refused=0;
        api.on_set=[&] {
            assert(++callbacks<200);draw_scope raw({});
            const auto hr=run([&] {assert(api.states==native);return S_OK;});
            if(hr==d3d8::err_invalid_call) ++refused;else assert(hr==S_OK);
        };
        run([&] {assert(api.states==native);return S_OK;});api.on_set={};
        assert(refused && state.disabled && state.state_invalid && api.states==native);
        // Lost and stale generations do not issue any device queries or sampler writes.
        state.reset(8,8,caps());const auto calls=api.sets;
        run([&] {return S_OK;});assert(api.sets==calls);
        state.reset(7,8,caps());state.lost=true;run([&] {return S_OK;});assert(api.sets==calls);
        state.reset(7,8,caps());stages[0].stage=8;
        run([&] {return S_OK;});assert(api.sets==calls);
    }
    // Delete/reuse and source replacement invalidate aliases and never key only by the low word.
    records.create(0x1234,0x20001);assert(records.find(0x1234,0x10001)!=records.find(0x1234,0x20001));
    records.replace_source(0x1234,0x20001,world(),texture,5,1,7);
    records.retire(0x1234,0x10001);assert(!records.find(0x1234,0x20001)->source.admitted());
    const auto reused=records.create(0x1234,0x10001).allocation;assert(reused>allocation);
    records.replace_source(0x1234,0x10001,world(),texture,5,1,7);records.reset_device();
    assert(!records.find(0x1234,0x10001)->object && records.find(0x1234,0x10001)->device_generation==0);
    assert(draws==3);
    std::cout<<"PASS AF core: caps/path exclusions/POINT opt-in/scopes/stages/restoration/failures/reentry/generations\n";
}
