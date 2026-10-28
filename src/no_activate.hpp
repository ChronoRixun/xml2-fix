#pragma once
#include <Windows.h>
namespace d3d8 { struct present_parameters; }

// Requested-only test isolation. Installed before display so its imports chain through us.
namespace no_activate
{
    void install(HMODULE game);
    bool enabled();
    void presentation(d3d8::present_parameters& parameters, const char* call);
}
