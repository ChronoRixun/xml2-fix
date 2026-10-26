#pragma once
#include "patch_transaction.hpp"

namespace xml1_economy::shop {
bool contribute(patch_transaction::transaction&,std::string& failure);
bool installed();
}
