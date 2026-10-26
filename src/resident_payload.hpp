#pragma once
#include <cstdint>
#include <bit>
#include <cstring>
namespace resident_payload {
struct descriptor{std::uint32_t count,stride,words,constructed,ring,enqueue,dequeue,free,active,live,size;};
constexpr descriptor layout(std::uint32_t n,std::uint32_t s){const auto w=(n+31)/32,c=n*s,r=c+4*w,e=r+4*(n+1),a=e+12,l=a+4*w;return{n,s,w,c,r,e,e+4,e+8,a,l,l+4};}
inline std::uint32_t read(const void* p,std::uint32_t off){std::uint32_t v;std::memcpy(&v,static_cast<const unsigned char*>(p)+off,4);return v;}
inline void write(void* p,std::uint32_t off,std::uint32_t v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,4);}
// The ring algorithm itself does not clear either bitmap or live. The sole
// production caller is the constructor, which completes those clears first.
inline void ring_init(void* p,descriptor d){for(std::uint32_t i=0;i<d.count;++i)write(p,d.ring+i*4,i);write(p,d.enqueue,0);write(p,d.dequeue,0);write(p,d.free,d.count);}
inline std::uint32_t pop(void* p,descriptor d){
 auto q=read(p,d.dequeue),i=read(p,d.ring+q*4);write(p,d.active+(i>>5)*4,read(p,d.active+(i>>5)*4)|(1u<<(i&31)));
 ++q;if(std::bit_cast<std::int32_t>(q)>=static_cast<std::int32_t>(d.count))q=0;
 write(p,d.dequeue,q);write(p,d.free,read(p,d.free)-1);write(p,d.live,read(p,d.live)+1);return i;
}
inline void release(void* p,std::uint32_t i,descriptor d){
 const auto bit=~(1u<<(i&31)),word=(i>>5)*4;
 write(p,d.constructed+word,read(p,d.constructed+word)&bit);write(p,d.active+word,read(p,d.active+word)&bit);
 auto q=read(p,d.enqueue)+1;write(p,d.enqueue,q);write(p,d.free,read(p,d.free)+1);
 std::uint32_t slot;if(std::bit_cast<std::int32_t>(q)>=static_cast<std::int32_t>(d.count)){write(p,d.enqueue,0);slot=d.count-1;}else slot=q-1;
 write(p,d.ring+slot*4,i);write(p,d.live,read(p,d.live)-1);
}
inline unsigned char owns(void* p,void* probe,descriptor d){
 if(!probe)return 0;
 const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(probe))-static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
 const auto i=std::bit_cast<std::int32_t>(delta)/static_cast<std::int32_t>(d.stride);
 if(i<0||i>=static_cast<std::int32_t>(d.count))return 0;
 return static_cast<unsigned char>((read(p,d.active+(static_cast<std::uint32_t>(i)>>5)*4)>>(i&31))&1);
}
inline void destroy(void* p,descriptor d){std::memset(static_cast<unsigned char*>(p)+d.constructed,0,d.words*4);}
inline void initialize_constructor(void* p,descriptor d){destroy(p,d);std::memset(static_cast<unsigned char*>(p)+d.active,0,d.words*4);ring_init(p,d);}
}
