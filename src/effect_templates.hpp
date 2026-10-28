#pragma once
#include "effect_template_layout.hpp"
#include <Windows.h>
#include <string>
namespace effect_templates {
void install(HMODULE game);
std::string status();
}
