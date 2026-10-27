#pragma once
#include <Windows.h>
namespace lc_fonts {
void install(HMODULE game);
// True only while a text item using a ready private slot is being rendered.
bool prompt_scope();
}
