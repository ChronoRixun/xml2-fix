#pragma once
#include "shared_handler_fixture.hpp"
#include "pad_profile.hpp"
#include <Xinput.h>
#include <cmath>
#include <utility>

// Raw devices are synthetic; conversion, binding evaluation, action-bit mapping,
// controller history/edges and move-handler scheduling execute production code.
namespace guard_input_fixture {
using namespace handler_fixture;
inline std::array<unsigned char,0x12a10> device{};
inline std::array<unsigned char,0xa00> configuration{};
inline std::array<float,30> values{};
inline u32 held_mask=0;
inline bool allocated=false;
inline u32 commit_end=0x550fde;
__declspec(naked) inline void commit_done(){__asm {pop edi} __asm {pop esi} __asm {pop ebx} __asm {ret 0ch}}
inline void bind(const image& pc){
 if(!allocated){DWORD old=0;check(VirtualProtect(at(0xa00000),0x100000,PAGE_EXECUTE_READWRITE,&old)!=0,"native input globals writable in reserved fixture image");allocated=true;}
 for(auto range:{std::pair<u32,u32>{0x619c40,0x1400},{0x6270e0,0x6a0},{0x629630,0xe0},{0x550a40,0x1a0},{0x550fb0,0x120},{0x5510d0,0x80},{0x551ed0,0x60},{0x550db0,0x10},{0x69a0f4,0xa0}})pc.load(range.first,range.second);
 for(u32 a:{0x680030u,0x68012cu,0x682c10u,0x68106cu,0x6848c4u})pc.load(a,4);
 // Stop only after the native controller has copied its previous/current state;
 // the omitted tail drives force-feedback devices, which this host has none of.
 jump(commit_end,&commit_done);
 field<u32>(at(0x79fa60),0)=1;field<void*>(at(0x79ebc0),0)=at(0x69a134);
 pc.load(0x69a134,0xa0);
 field<unsigned char>(at(0x79ebc0),0x24)=1;
 field<void*>(at(0x79ebfc),0)=at(0x69a0f4);
 field<void*>(at(0xa6abfc),0)=device.data();field<u32>(at(0xa6ac04),0)=0;
 field<void*>(at(0xa68f40),0)=configuration.data();field<int>(at(0x6f3920),0)=-1;
}
inline void reset(){
 device.fill(0);configuration.fill(0);values.fill(0);held_mask=0;
 std::memset(at(0xa09e70),0,0x180);std::memset(at(0xa68ca0),0,0xa0);std::memset(at(0xa6ad58),0,0x40);
 std::memset(at(0x79ebfc)+4,0,0x38c);
 // Four bindings per action: keyboard E/J/Space/W and console-layout pad X/A/Y/Y-axis.
 field<u32>(configuration.data(),0x18)=30;
 auto binding=[](int action,int slot,u32 dev,u32 control){auto* p=configuration.data()+0x1c+action*48+slot*12;field<u32>(p,0)=dev;field<u32>(p,4)=control;};
 binding(7,0,1,0x12);binding(7,1,3,21);
 binding(4,0,1,0x24);binding(4,1,3,22);
 binding(6,0,1,0x39);binding(6,1,3,24);
 binding(0,0,1,0x11);binding(0,1,3,4);
}
inline void frame(void* output,bool pad,bool guard,bool attack=false,bool jump_button=false,bool moving=false){
 std::memcpy(device.data()+0x26e4,device.data()+0x25e4,256);
 std::memcpy(device.data()+0xf90,device.data()+0x4f0,0x110);
 std::memset(device.data()+0x25e4,0,256);
 DIJOYSTATE state{};xinput_pad::raw_state raw{};
 if(pad){raw.buttons=(guard?XINPUT_GAMEPAD_X:0)|(attack?XINPUT_GAMEPAD_A:0)|(jump_button?XINPUT_GAMEPAD_Y:0);raw.left_y=moving?32767:0;}
 else {device[0x25e4+0x12]=guard?0x80:0;device[0x25e4+0x24]=attack?0x80:0;device[0x25e4+0x39]=jump_button?0x80:0;device[0x25e4+0x11]=moving?0x80:0;}
 pad_profile::axis_ranges ranges{};for(auto& a:ranges){a.min=-1000;a.max=1000;}
 pad_profile::logitech_dual_action.fill_state(state,raw,ranges);
 std::memcpy(device.data()+0x4f0,&state,sizeof state);
 held_mask=0;values.fill(0);
 reinterpret_cast<void(__cdecl*)(int,u32*,float*)>(0x61a810)(0,&held_mask,values.data());
 reinterpret_cast<void(__thiscall*)(void*,int,u32*,float*)>(0x550fb0)(at(0x79ebfc),0,&held_mask,values.data());
 std::memset(output,0,0x24);
 field<u32>(output,0)=reinterpret_cast<u32(__thiscall*)(void*)>(0x550a90)(at(0x79ebfc));
 field<u32>(output,4)=reinterpret_cast<u32(__thiscall*)(void*)>(0x550b70)(at(0x79ebfc));
 // The actor's input packet uses the controller's native movement values.
 field<float>(output,8)=std::abs(values[0])+std::abs(values[1])+std::abs(values[2])+std::abs(values[3]);
 field<float>(output,0x20)=1.f/60.f;
}
}
