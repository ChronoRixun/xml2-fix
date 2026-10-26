#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <string_view>
namespace patch_transaction { class transaction; }
namespace motion_pool
{
struct descriptor
{
    std::uint32_t capacity,constructed,ring,enqueue,dequeue,free,active,live,generations,mask,shift,size;
};
void require_agreement(HMODULE);
void contribute(patch_transaction::transaction&,HMODULE);
const descriptor& layout();
std::string_view counter_guard_hex();
std::string telemetry_json();
}
