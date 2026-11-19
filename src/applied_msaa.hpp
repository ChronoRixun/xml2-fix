#pragma once
#include "d3d8_min.hpp"
#include "remastered_rules.hpp"
#include <cstdint>
#include <string>

namespace display {
enum class observation_validity { unavailable, ready, lost };
enum class observation_source { successful_parameters_unverified, backbuffer_desc };
struct applied_state {
    std::uint64_t device_generation=0, observation_revision=0;
    observation_validity validity=observation_validity::unavailable;
    int requested_saved_index=-1;
    std::optional<unsigned> requested_profile_samples;
    d3d8::present_parameters engine_pp_before_constraints{}, final_attempt_pp{}, successful_pp{};
    d3d8::surface_desc observed_backbuffer{}, observed_depth{};
    observation_source source=observation_source::successful_parameters_unverified;
    std::string fallback_reason;
    HRESULT last_hresult=S_OK, observation_hresult=E_FAIL, depth_hresult=E_FAIL;
    bool effective_windowed=false, capture_constraint=false, depth_matches=false;
};

// Device-thread only; a temporary surface reference never escapes the query.
inline HRESULT describe_surface(void* self, bool depth, d3d8::surface_desc& desc) {
    void* surface=nullptr;
    HRESULT hr=depth ? d3d8::method<d3d8::get_depth_stencil_surface_t>(self,33)(self,&surface)
                     : d3d8::method<d3d8::get_back_buffer_t>(self,16)(self,0,0,&surface);
    if (SUCCEEDED(hr)) hr=surface ? d3d8::method<d3d8::get_desc_t>(surface,8)(surface,&desc) : E_FAIL;
    if (surface) d3d8::method<ULONG(STDMETHODCALLTYPE*)(void*)>(surface,2)(surface);
    return hr;
}
inline void observe_applied(void* self, applied_state& state) {
    state.source=observation_source::successful_parameters_unverified;
    state.validity=observation_validity::unavailable;
    state.observation_hresult=describe_surface(self,false,state.observed_backbuffer);
    if (SUCCEEDED(state.observation_hresult)) {
        state.source=observation_source::backbuffer_desc;
        state.validity=observation_validity::ready;
    }
    state.depth_matches=false;
    if (state.successful_pp.enable_auto_depth_stencil) {
        state.depth_hresult=describe_surface(self,true,state.observed_depth);
        state.depth_matches=SUCCEEDED(state.depth_hresult) && SUCCEEDED(state.observation_hresult) &&
            state.observed_depth.multi_sample_type==state.observed_backbuffer.multi_sample_type;
    }
}
inline std::string msaa_status(const applied_state& state) {
    if (state.validity==observation_validity::lost || FAILED(state.last_hresult) || !state.device_generation) return "MSAA: unavailable";
    if (state.source!=observation_source::backbuffer_desc || !remastered_rules::known_samples(state.observed_backbuffer.multi_sample_type))
        return "MSAA: unknown";
    const unsigned n=state.observed_backbuffer.multi_sample_type;
    if (!n && state.capture_constraint) return "MSAA: Off (capture)";
    if (state.requested_profile_samples && *state.requested_profile_samples) {
        if (n) return "MSAA: "+std::to_string(n)+"x (Remastered)";
        return "MSAA: Off (requested "+std::to_string(*state.requested_profile_samples)+"x)";
    }
    return n ? "MSAA applied: "+std::to_string(n)+"x" : "MSAA applied: Off";
}
}

namespace remastered_rules {
// Called on a fresh candidate immediately before each API attempt. Query the final shell/formats.
template<class SampleSupport,class DepthMatch>
void constrain_msaa(d3d8::present_parameters& pp, bool capture, SampleSupport supported, DepthMatch match, std::string& reason) {
    if (capture) { pp.multi_sample_type=0; reason="capture"; return; }
    if (!pp.multi_sample_type) return;
    if (pp.swap_effect!=d3d8::swap_discard) { pp.multi_sample_type=0; reason="swap-requires-native"; return; }
    const unsigned requested=pp.multi_sample_type;
    pp.multi_sample_type=0;
    const bool pair_ok=!pp.enable_auto_depth_stencil || match(pp.back_buffer_format,pp.auto_depth_stencil_format);
    if (pair_ok) for (unsigned n:{8u,6u,4u,2u}) {
        if (n<=requested && supported(pp.back_buffer_format,pp.windowed,n) &&
            (!pp.enable_auto_depth_stencil || supported(pp.auto_depth_stencil_format,pp.windowed,n))) {
            pp.multi_sample_type=n; break;
        }
    }
    if (pp.multi_sample_type!=requested) reason=pair_ok ? "sample-fallback" : "depth-mismatch";
    if (pp.multi_sample_type) { if(pp.flags & 1u) reason += ";lockable-cleared"; pp.flags &= ~1u; }
}
}
