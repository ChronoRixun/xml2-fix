#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
namespace gameplay_agreement
{
 // Call each registration once after its installer settles. False poisons online agreement.
 bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<std::uint32_t,4> effective,bool verified);
 bool register_entity_layout(std::uint16_t revision,std::uint32_t actors,std::uint32_t generic,std::uint32_t world,std::uint32_t token_bits,bool verified);
 // Prepare only native refusal handling before installers; finalize after ALL installers.
 bool prepare(HMODULE game);
 bool finalize(HMODULE game);
 void show_install_failure();
 int receive_request(void* event);
 int receive_reply(void* event);
 int receive_info(void* event);
 int receive_ack(void* event);
}
