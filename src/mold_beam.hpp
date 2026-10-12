#pragma once
#include "xml1_hook_transaction.hpp"
namespace mold_beam {
// Native helpers the Master Mold controller calls plus the beam-direction site.
bool supported();
// The 0x45230b beam-direction patch; Master Mold applies it in its own transaction.
bool prepare(xml1_hook_transaction::patch& patch);
}
