#pragma once
#include "patch_transaction.hpp"
namespace xml1_economy::grab_bag {
bool contribute(patch_transaction::transaction&,std::string& failure);
bool installed();
// Replaces the complete native reward function. The buy caller owns the debit.
bool __fastcall x1_grab_bag(void* shop,void*);
}
