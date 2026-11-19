#include "applied_msaa.hpp"
#include "centered_ui_trace.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

namespace { std::atomic<unsigned> lines{0}; bool close_to(double a,double b) { return std::abs(a-b)<1e-8; } }
namespace logger { void write(const char*,...) { ++lines;std::feclearexcept(FE_ALL_EXCEPT); } }
void math(unsigned w,unsigned h,double scale,double ox) {
    const auto t=centered_ui::make(w,h,7);assert(t && close_to(t->s,scale) && close_to(t->ox,ox) && close_to(t->oy,0));
    assert(t->generation==7 && t->wide_pilot);
    for(auto p:{centered_ui::point{0,0},{640,480},{33,299},{196,24},{320,240},{.25,479.25}}) {
        const auto inverse=t->logical(t->pixel(p));assert(close_to(inverse.x,p.x) && close_to(inverse.y,p.y));
    }
    for(auto p:{centered_ui::point{0,384},{512,0},{256,192},{33.25,299.5}}) {
        const auto inverse=t->view(t->view_pixel(p));assert(close_to(inverse.x,p.x) && close_to(inverse.y,p.y));
    }
    assert(t->inside(t->pixel({0,0})) && !t->inside(t->pixel({640,0})) && !t->inside(t->pixel({0,480})));
    assert(!t->inside({ox-.001,10}));
    const auto quantized=t->receiver(t->view_pixel({123.25,200.25}));
    if(t->legacy_4_3) assert(!quantized); // runtime must retain the complete original branch
    else {assert(quantized && quantized->x==123 && quantized->y==201);}
    assert(!t->receiver({ox-1,1}));
    const auto c=t->outward_clip({0,0},{640,480});
    assert(c.left==int(std::floor(ox)) && c.right==int(std::ceil(w-ox)) && c.top==0 && c.bottom==int(h));
    const auto clipped=t->outward_clip({-10000,-10000},{10000,10000});
    assert(clipped.left==0 && clipped.top==0 && clipped.right==int(w) && clipped.bottom==int(h));
    if(t->legacy_4_3) {
        for(int x=0;x<int(w);++x) {
            // No coordinate edits in the 4:3 renderer; raw logical identity at both reference sizes.
            const auto a=t->pixel({double(x)/scale,0});assert(close_to(a.x,x));
        }
    } else {
        const auto v=t->pixel_center_view(int(std::ceil(ox)),0);
        assert(close_to(v.x,(std::ceil(ox)+.5-ox)/t->q()));
    }
}
void traces() {
    using namespace centered_ui_trace;
    display::applied_state state;state.device_generation=9;state.validity=display::observation_validity::ready;
    state.source=display::observation_source::backbuffer_desc;state.observed_backbuffer.width=1920;state.observed_backbuffer.height=1080;
    state.successful_pp.back_buffer_width=999;state.successful_pp.back_buffer_height=999;
    for(auto values:{std::array<bool,3>{false,true,true},{true,false,true},{true,true,false}}) {
        configure(values[0],values[1],values[2]);const auto before=lines.load();
        applied(state);next_frame();record(owner::online,boundary::input);
        assert(!enabled() && lines==before);
    }
    configure(true,true,true);
    std::feraiseexcept(FE_INEXACT);const auto flags=std::fetestexcept(FE_ALL_EXCEPT);const auto mxcsr=_mm_getcsr();
    applied(state);assert(std::fetestexcept(FE_ALL_EXCEPT)==flags && _mm_getcsr()==mxcsr);
    std::feclearexcept(FE_ALL_EXCEPT);
    auto copy=snapshot();
    assert(copy.used==1 && copy.events[0].transform_known && copy.events[0].generation==9);
    assert(copy.events[0].canvas.width==1920 && copy.events[0].canvas.ox==240);
    next_frame();record(owner::bxig,boundary::layout,0x1234,1,{3,2.25,-85.3333,0});
    copy=snapshot();assert(copy.events[1].frame==1 && copy.events[1].object==0x1234 && copy.events[1].coordinates[0]==3);
    record(owner::raw_pointer,boundary::input,0,0,{239,10,1920,1080});
    record(owner::raw_pointer,boundary::input,0,0,{240,10,1920,1080});
    copy=snapshot();assert(!copy.events[2].inside && copy.events[3].inside);
    state.validity=display::observation_validity::lost;applied(state);record(owner::online,boundary::input);
    copy=snapshot();assert(!copy.events[copy.used-1].transform_known);
    state.validity=display::observation_validity::ready;state.source=display::observation_source::successful_parameters_unverified;
    applied(state);copy=snapshot();assert(!copy.events[copy.used-1].transform_known);
    state.source=display::observation_source::backbuffer_desc;state.observed_backbuffer.width=0;
    applied(state);copy=snapshot();assert(!copy.events[copy.used-1].transform_known);
    state.observed_backbuffer.width=3840;state.device_generation=10;applied(state);
    copy=snapshot();assert(copy.events[copy.used-1].canvas.ox==1200 && copy.events[copy.used-1].generation==10);
    configure(true,true,true);
    for(unsigned i=0;i<10000;++i) record(owner::menu_text,boundary::render,i);
    copy=snapshot();assert(copy.used==per_boundary && copy.suppressed==10000-per_boundary);
    // Bounded total storage even if every independently budgeted boundary fires.
    for(unsigned a=0;a<unsigned(owner::count);++a) for(unsigned b=0;b<unsigned(boundary::count);++b)
        for(unsigned i=0;i<32;++i) record(owner(a),boundary(b),i);
    assert(snapshot().used==capacity);
    configure(true,true,true);
    std::thread publisher([&]{for(unsigned i=1;i<100;++i) {state.device_generation=i;applied(state);}});
    for(unsigned i=0;i<1000;++i) record(owner::online,boundary::input);
    publisher.join();
    for(const auto& e:snapshot().events) if(e.transform_known) assert(e.generation==e.canvas.generation && e.canvas.ox==1200);
    configure(false,true,true);assert(!enabled());
}
int main(int argc,char** argv) {
    const std::string mode=argc>1 ? argv[1] : "all";
    if(mode=="all") {
        math(640,480,1,0);math(1280,960,2,0);math(1280,800,5./3,320./3);
        math(1920,1080,2.25,240);math(2520,1080,2.25,540);math(3440,1440,3,760);
        math(3840,1080,2.25,1200);math(1365,768,1.6,170.5);traces();
    }
    else if(mode=="4:3") { math(640,480,1,0);math(1280,960,2,0); }
    else if(mode=="16:10") math(1280,800,5./3,320./3);
    else if(mode=="16:9") math(1920,1080,2.25,240);
    else if(mode=="21:9") {math(2520,1080,2.25,540);math(3440,1440,3,760);}
    else if(mode=="32:9") math(3840,1080,2.25,1200);
    else if(mode=="rounding") {
        math(1365,768,1.6,170.5);
        const auto t=centered_ui::make(480,640,1);assert(t && !t->wide_pilot && t->oy==140 && !t->receiver({240,320}));
        assert(!centered_ui::make(0,480,1) && !centered_ui::make(640,0,1) && !centered_ui::make(640,480,0));
        assert(!centered_ui::make(0xffffffffu,480,1));
        const auto wide=centered_ui::make(1920,1080,1);const auto v=wide->receiver({960,540});assert(v && v->x==256 && v->y==192);
    }
    else if(mode=="trace") traces();
    else assert(false);
    std::cout<<"PASS centered-ui "<<mode<<"\n";
}
