#pragma once
#include <cstdint>
namespace e4_unwind {
using u32=std::uint32_t;
inline constexpr u32 magic=0xe403b001,begin=0xfffffffe,end=0xffffffff;
// The first two words are the x86 stack SEH registration ABI. All storage is on
// the native stack; the handler is registered in the image's SafeSEH table.
struct frame {
    u32 next,handler,signature,serial,previous;
    u32 borrows,borrow_overflow,writers,writer_overflow,damage,damage_overflow;
    u32 init,factory,boundaries,closed,resetting,point,thread,reserved[2];
};
static_assert(sizeof(frame)==80);
}
extern "C" void e4_unwind_handler();
