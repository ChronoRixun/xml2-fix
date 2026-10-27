#pragma once
#include <Windows.h>
#include "patch_transaction.hpp"

// Presentation only. No broker, host, admission or participant allocation here.
namespace online_menus {
bool requested();
bool installed();
void install(HMODULE game);
// Shared with the host fixture for actual rollback/readback fault injection.
patch_transaction::contribution contribution();
}
