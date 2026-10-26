#pragma once
#include "patch_transaction.hpp"
#include "lcsave/layout.hpp"
namespace item_capacity {
unsigned installed_capacity(); // 0 until the complete transaction publishes
// Prepared W2 definition picker copy. A later union contributor may observe its
// entry while retaining W2's widened implementation and exclusive retail jump.
void* prepared_definition_picker();
bool contribute_w2(patch_transaction::contribution&,std::string& why);
bool preflight(unsigned instances,unsigned attached,unsigned world,const lcsave::NativeLayout&,std::string& error);
// Execution-test seam: exact production picker implementation, selected table
// backing and range. Production always uses the descriptor's 4096 capacity.
int pick(void* manager,int level,std::uint32_t handle,void* chain,bool prefix,std::uint32_t records_offset);
}
