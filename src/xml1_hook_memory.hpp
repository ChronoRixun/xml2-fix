#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>
namespace xml1_hook_transaction {
struct memory {
    bool read(std::uint32_t a,void* p,std::size_t n) {
        __try { std::memcpy(p,reinterpret_cast<const void*>(a),n); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool write(std::uint32_t a,const void* p,std::size_t n) {
        __try { std::memcpy(reinterpret_cast<void*>(a),p,n); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool acquire(std::uint32_t a,std::uint32_t& old) {
        DWORD protection=0;
        if(!VirtualProtect(reinterpret_cast<void*>(a),4096,PAGE_EXECUTE_READWRITE,&protection))return false;
        old=protection; return true;
    }
    void restore(std::uint32_t a,std::uint32_t old) {
        DWORD ignored=0; VirtualProtect(reinterpret_cast<void*>(a),4096,old,&ignored);
    }
    bool flush(){return FlushInstructionCache(GetCurrentProcess(),nullptr,0)!=0;}
};
}
