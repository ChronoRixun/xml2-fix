#pragma once
#include <Windows.h>

namespace pad_hotplug
{
    // Keep saved pad bindings usable when their physical controller is absent at startup.
    void install(HMODULE game);

    // Called by the DirectInput state hook. Only the gameplay input's own
    // keyboard drives refresh/rebinding, on the thread that reads its inputs.
    void on_keyboard_read(void* device, DWORD state_bytes);
}
