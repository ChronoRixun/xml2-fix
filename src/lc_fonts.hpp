#pragma once
#include <Windows.h>
namespace lc_fonts {
void install(HMODULE game);
// True only while a text item using a ready private slot is being rendered.
bool prompt_scope();
// True once the scoped font transaction committed (the private family is installed).
bool active();
// The online ready renderer uses the same body atlas/scale as LC menu items.
// False while loading or when that private font could not be built/loaded.
bool online_body_ready();
}
