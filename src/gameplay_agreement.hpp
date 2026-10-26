#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
namespace patch_transaction { class transaction; }
namespace gameplay_agreement
{
 // Call each registration once after its installer settles. False poisons online agreement.
 bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<std::uint32_t,4> effective,bool verified);
 // A rule whose effective state does not fit four words also binds the SHA-256 of its complete canonical data
 // (docs/gameplay-agreement.md, "Rule details"); null = none (the record then hashes as before).
 bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<std::uint32_t,4> effective,bool verified,const std::array<std::uint8_t,32>* detail);
 // Experimental gameplay awaiting an assigned ID must never negotiate as stock.
 void reject_unregistered_rule(const char* name);
 bool register_entity_layout(std::uint16_t revision,std::uint32_t actors,std::uint32_t generic,std::uint32_t world,std::uint32_t token_bits,bool verified);
 // The shared agreement's native sites (refusal and transport) as a contribution to the one startup transaction:
 // guards, writes and a publication callback; nothing is written here. False: not the retail executable or the
 // transaction is sealed.
 bool contribute(patch_transaction::transaction& transaction,HMODULE game);
 // After every installer registered its effective state and the transaction committed: freezes the canonical
 // record. Returns whether the record is ready (verified sites, no poisoned registration).
 bool commit_verified();
 // Late enlistment (a participating installer with no LC/first-hit key, after the startup transaction): the
 // sites in a transaction of their own. prepare() and finalize() keep their old contract for those callers.
 bool prepare(HMODULE game);
 bool finalize(HMODULE game);
 bool contributed();
 void show_install_failure();
 int receive_request(void* event);
 int receive_reply(void* event);
 int receive_info(void* event);
 int receive_ack(void* event);
}
