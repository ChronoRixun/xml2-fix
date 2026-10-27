#pragma once
#include <Windows.h>

namespace pad_hotplug
{
    // Keep saved pad bindings usable when their physical controller is absent at startup.
    void install(HMODULE game);
}
