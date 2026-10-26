#pragma once
#include "patch_transaction.hpp"
#include "xml1_economy_rules.hpp"
namespace xml1_economy {
void prepare(patch_transaction::transaction&,HMODULE game);
void after_commit(patch_transaction::outcome);
bool installed(); // table resolved, hooks and prerequisite verified
const xml1_economy_rules::record* record_for(unsigned definition);
const std::vector<xml1_economy_rules::record>& records();
bool is_economy(const void* definition);
const xml1_economy_rules::record* record_for_definition(const void* definition);
std::string status(); // empty OFF

struct descriptor {std::uint32_t handle;void* pool;};
// The production entry bridge; exposed for the mapped-retail execution fixture.
descriptor* __fastcall make_instance(void* manager,void*,descriptor* out,void* definition,int level,int prefixes,int suffixes,int special_a,int special_b);
// Called only after the native item-loader record walk. Also a test seam for a
// synthetic, already populated table (still resolves via the real vt+0x78).
void table_loaded(void* manager);
void prepare_for_testing(patch_transaction::transaction&,HMODULE,bool requested,std::string_view sidecar,xml1_economy_rules::plan);
}
