#pragma once
#include "d3d8_min.hpp"
#include "remastered_rules.hpp"
#include <array>
#include <map>
#include <string>
#include <cstdint>

// A bounded pilot core. Engine admission is deliberately separate: no inferred world/atlas bits.
namespace texture_filtering {
// Fixed-function world formats observed by the owned probe. A programmable
// shader handle can share low FVF bits; matching POSITION_MASK alone is unsafe.
constexpr bool world_fvf(unsigned value) {
    switch(value) {
    case 0x2:case 0x12:case 0x42:case 0x52:case 0x102:case 0x112:case 0x142:case 0x152:return true;
    default:return false;
    }
}
inline bool world_path(std::string_view path) {
    if(path.empty()) return false;
    std::string p;p.reserve(path.size()+1);
    p+='/';
    for(unsigned char c:path) {
        if(c<32 || c>=127) return false;
        p+=c=='\\' ? '/' : c>='A' && c<='Z' ? char(c+('a'-'A')) : char(c);
    }
    // Both separators and component boundaries matter. Never admit traversal or
    // a UI/atlas/glyph subfolder simply because its parent happens to be tilesets.
    if(p.find("/../")!=p.npos || p.find("/./")!=p.npos) return false;
    for(auto word:{"/ui/","/hud/","/fonts/","/font/","/menus/","/menu/","/movies/","/effects/","/fx/",
                   "atlas","glyph","font","spritesheet","sprite_sheet"})
        if(p.find(word)!=p.npos) return false;
    const auto tile=p.find("/assets/tilesets/"),actor=p.find("/art/models/actors/");
    const auto at=tile!=p.npos ? tile+17 : actor!=p.npos ? actor+19 : p.npos;
    return at!=p.npos && at<p.size() && p.back()!='/';
}
enum class pass { unknown, world_material, ui, movie, post_process };
class source_identity {
    std::string path_;
    bool world_=false;
public:
    static source_identity from_path(std::string path) {
        source_identity value;value.world_=world_path(path);value.path_=std::move(path);return value;
    }
    bool admitted() const {return world_;}
    std::string_view path() const {return path_;}
};
struct association {
    std::uintptr_t context=0;
    std::uint32_t handle=0;
    std::uint64_t allocation=0,source_revision=0,device_generation=0;
    void* object=nullptr; // comparison only, never dereferenced outside a current GetTexture reference
    unsigned authored_min=0,authored_mag=0;
    source_identity source;
};
class associations {
    std::uint64_t serial_=0;
    std::map<std::pair<std::uintptr_t,std::uint32_t>,association> records_;
public:
    association& create(std::uintptr_t context,std::uint32_t handle) {
        if(records_.contains({context,handle})) retire(context,handle);
        auto& r=records_[{context,handle}];r={};r.context=context;r.handle=handle;r.allocation=++serial_;return r;
    }
    const association* find(std::uintptr_t context,std::uint32_t handle) const {
        auto it=records_.find({context,handle});return it==records_.end() ? nullptr : &it->second;
    }
    association* find(std::uintptr_t context,std::uint32_t handle) {
        auto it=records_.find({context,handle});return it==records_.end() ? nullptr : &it->second;
    }
    void retire(std::uintptr_t context,std::uint32_t handle) {
        auto it=records_.find({context,handle});if(it==records_.end()) return;
        void* object=it->second.object;
        for(auto& [key,r]:records_) if(object && r.object==object) {r.object=nullptr;r.source={};++r.source_revision;}
        records_.erase(it);++serial_;
    }
    bool replace_source(std::uintptr_t context,std::uint32_t handle,source_identity source,void* object,
                        unsigned min,unsigned mag,std::uint64_t device_generation) {
        auto it=records_.find({context,handle});if(it==records_.end()) return false;
        // Replacement retires aliases before publishing a new, copied identity.
        void* old=it->second.object;
        for(auto& [key,r]:records_) if(old && r.object==old) {r.object=nullptr;r.source={};++r.source_revision;}
        auto& r=it->second;r.source=std::move(source);r.object=object;r.authored_min=min;r.authored_mag=mag;
        r.device_generation=device_generation;++r.source_revision;return true;
    }
    void reset_device() { for(auto& [key,r]:records_) {r.object=nullptr;r.device_generation=0;++r.source_revision;} }
};
struct scope_value { void* device=nullptr;std::uintptr_t context=0;pass kind=pass::unknown; };
inline thread_local scope_value current_scope;
class draw_scope {
    scope_value previous_;
public:
    explicit draw_scope(scope_value value):previous_(current_scope) {current_scope=value;}
    ~draw_scope() {current_scope=previous_;}
    draw_scope(const draw_scope&)=delete;
};
struct device_state {
    std::uint64_t generation=0;
    unsigned amount=0;
    unsigned stages=0;
    bool lost=false,disabled=false,state_invalid=false;
    bool on_point_art=false;
    std::array<bool,8> rejected_stage{};
    void reset(std::uint64_t gen,unsigned request,const d3d8::caps8& caps) {
        *this={};generation=gen;
        stages=std::min(8u,std::min(unsigned(caps.max_texture_blend_stages),unsigned(caps.max_simultaneous_textures)));
        if((caps.raster_caps&0x20000) && (caps.texture_filter_caps&0x400) && caps.max_anisotropy>=2)
            amount=std::min(request,unsigned(caps.max_anisotropy));
        if(amount<2) amount=0;
    }
};
struct binding { unsigned stage=0; const association* texture=nullptr; };
enum class inspection { eligible, excluded, failed };
constexpr bool authored_linear(unsigned value) { return value==1 || value==4 || value==5; }

// API supplies actual per-stage state, a current bound reference and real texture descriptions.
// Its inspect(stage,expected) checks 2D color/non-RT/non-depth/non-shadow type, mip count and
// bound object equality under a temporary COM reference; every GetTexture reference is released.
template<class API> class sampler_transaction {
    API& api_;device_state& state_;
    struct saved {unsigned stage,min,max;};
    std::array<saved,8> saved_{};unsigned count_=0;
    unsigned setters_=0,restoring_=0;
    bool set(unsigned stage,unsigned key,unsigned value) {
        ++setters_;
        struct leave {unsigned& depth;~leave(){--depth;}} exit{setters_};
        return api_.set(stage,key,value);
    }
public:
    sampler_transaction(API& api,device_state& state):api_(api),state_(state) {}
    bool restore() {
        if(!count_) return true;
        // A setter can itself draw through a wrapper. Keep the entire dirty
        // ledger published until all in-flight setters have returned. Bound
        // recursive cleanup; a draw that cannot obtain native state is refused.
        if(restoring_>=2) {state_.disabled=true;state_.state_invalid=true;return false;}
        ++restoring_;
        bool clean=true;
        for(unsigned i=count_;i--;) {
            const auto s=saved_[i];
            // Attempt both restores even when the first fails.
            const bool min=set(s.stage,17,s.min),max=set(s.stage,21,s.max);
            clean=min && max && clean;
        }
        --restoring_;
        if(!restoring_ && !setters_) count_=0;
        if(!clean) {state_.disabled=true;state_.state_invalid=true;}
        return clean;
    }
    ~sampler_transaction() {restore();}
    bool abandon() {state_.disabled=true;return restore();}
    bool apply(void* device,const binding* stages,unsigned count) {
        if(state_.lost || state_.disabled || !state_.amount || count>8 || current_scope.device!=device ||
           !current_scope.context || current_scope.kind!=pass::world_material) return false;
        std::array<bool,8> seen{};
        // An unexpected physical map refuses the whole draw before making any writes.
        for(unsigned i=0;i<count;++i) if(stages[i].stage>=state_.stages || stages[i].stage>=8 || seen[stages[i].stage]) return false;
            else seen[stages[i].stage]=true;
        for(unsigned i=0;i<count;++i) {
            const auto& b=stages[i];const auto* r=b.texture;
            if(!r || state_.rejected_stage[b.stage] || r->context!=current_scope.context ||
               r->device_generation!=state_.generation || !r->allocation || !r->source.admitted() ||
               (!authored_linear(r->authored_min) && !(state_.on_point_art && r->authored_min<=5)) ||
               !authored_linear(r->authored_mag)) continue;
            unsigned min=0,mag=0,mip=0,max=0;
            if(!api_.get(b.stage,17,min) || !api_.get(b.stage,16,mag) || !api_.get(b.stage,18,mip) || !api_.get(b.stage,21,max))
                {state_.rejected_stage[b.stage]=true;continue;}
            const auto check=api_.inspect(b.stage,r->object);
            if(check!=inspection::eligible) {
                if(check==inspection::failed) state_.rejected_stage[b.stage]=true;
                continue;
            }
            if(state_.disabled) return false;
            if((min==1 && !state_.on_point_art) || (mag!=2 && mag!=3) || (mip!=1 && mip!=2)) continue;
            if(min!=1 && min!=2 && min!=3) continue; // unknown shader sampler/filter stays native
            saved_[count_++]={b.stage,min,max};
            if(!set(b.stage,17,3) || state_.disabled || !set(b.stage,21,state_.amount) || state_.disabled) {
                state_.disabled=true;restore();return false;
            }
        }
        return count_!=0;
    }
};
template<class API> inline thread_local sampler_transaction<API>* active_transaction=nullptr;

// Recursion is evidence against the no-engine-work-inside-draw assumption. Restore before a
// nested call and disable this generation; never let a native nested UI draw inherit temporary AF.
template<class API,class Call>
HRESULT draw(API& api,device_state& state,void* device,const binding* stages,unsigned count,Call call) {
    auto*& active=active_transaction<API>;
    if(active) {const bool clean=active->abandon();state.disabled=true;return clean?call():d3d8::err_invalid_call;}
    sampler_transaction<API> transaction(api,state);
    auto* previous=active;active=&transaction;
    struct leave {sampler_transaction<API>*& slot;sampler_transaction<API>* previous;~leave(){slot=previous;}} leave_scope{active,previous};
    transaction.apply(device,stages,count);
    const HRESULT result=call();
    transaction.restore();return result;
}

struct d3d8_sampler_api {
    void* device;
    bool get(unsigned stage,unsigned type,unsigned& value) {
        DWORD native=0;
        const HRESULT hr=d3d8::method<HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,DWORD,DWORD*)>(device,62)(device,stage,type,&native);
        value=native;return SUCCEEDED(hr);
    }
    bool set(unsigned stage,unsigned type,unsigned value) {
        return SUCCEEDED(d3d8::method<HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,DWORD,DWORD)>(device,63)(device,stage,type,value));
    }
    inspection inspect(unsigned stage,void* expected) {
        void* texture=nullptr;
        const HRESULT hr=d3d8::method<HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,void**)>(device,60)(device,stage,&texture);
        inspection result=FAILED(hr) || texture!=expected ? inspection::failed : inspection::excluded;
        if(SUCCEEDED(hr) && texture && d3d8::method<DWORD(STDMETHODCALLTYPE*)(void*)>(texture,10)(texture)!=3)
            result=inspection::excluded; // cube/volume bindings have a different native record slot
        else if(SUCCEEDED(hr) && texture && texture==expected && d3d8::method<DWORD(STDMETHODCALLTYPE*)(void*)>(texture,13)(texture)>1) {
            d3d8::surface_desc desc{};
            const auto described=d3d8::method<HRESULT(STDMETHODCALLTYPE*)(void*,UINT,d3d8::surface_desc*)>(texture,14)(texture,0,&desc);
            if(FAILED(described)) result=inspection::failed;
            else if(desc.width>0 && desc.height>0 && !(desc.usage&3u)) result=inspection::eligible;
        }
        if(texture) d3d8::method<ULONG(STDMETHODCALLTYPE*)(void*)>(texture,2)(texture);
        return result;
    }
};
}
