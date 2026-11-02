#pragma once
#include <Windows.h>
#include "anim_enum_layout.hpp"
#include <string>
namespace patch_transaction { class transaction; }
namespace anim_enums
{
void contribute(patch_transaction::transaction&, HMODULE game);
// Null until the common startup transaction has published this contribution.
const anim_enum_layout::descriptor* layout();
const char* builtin_table();
int extension_id(const char* lowercased_native_buffer);
std::string status();
}
