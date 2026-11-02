#pragma once
#include "patch_transaction.hpp"

// Prepared only for Menus=1. Add before the dispatcher contribution in the
// SAME transaction: no publication can observe a mixture of the two layouts.
namespace online_menus_anchors {
inline constexpr char capability[]="menu-anchors/512";
patch_transaction::contribution contribution();
}
