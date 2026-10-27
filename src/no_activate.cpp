#include "no_activate.hpp"
#include "d3d8_min.hpp"
#include "iat_hook.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <TlHelp32.h>
#include <cstring>

namespace no_activate
{
    namespace
    {
        bool active = false;
        // Allocate TLS only when requested. Static thread_local would add a TLS
        // directory even with the key absent and defeat DisableThreadLibraryCalls.
        DWORD cbt_index = TLS_OUT_OF_INDEXES;
        constexpr const char* previous_property = "XML2Fix.NoActivate.Previous";
        constexpr const char* game_class = "igWin32WindowClass";

        // Function pointers also let the host exercise production wrappers with fake windows.
        auto window_process = &GetWindowThreadProcessId;
        auto next_hook = &CallNextHookEx;
        auto set_hook = &SetWindowsHookExA;
        auto get_property = &GetPropA;
        auto set_property = &SetPropA;
        auto remove_property = &RemovePropA;
        auto get_long = &GetWindowLongA;
        auto set_long = &SetWindowLongA;
        auto call_procedure = &CallWindowProcA;
        auto real_create = &CreateWindowExA;
        auto real_show = &ShowWindow;
        auto real_show_async = &ShowWindowAsync;
        auto real_position = &SetWindowPos;
        auto real_foreground = &SetForegroundWindow;
        auto real_active = &SetActiveWindow;
        auto real_focus = &SetFocus;
        auto real_bring = &BringWindowToTop;
        auto real_switch = &SwitchToThisWindow;

        bool ours(HWND window)
        {
            if (!active || !window) return false;
            DWORD process = 0;
            window_process(window, &process);
            return process == GetCurrentProcessId();
        }

        HHOOK thread_hook()
        {
            return cbt_index == TLS_OUT_OF_INDEXES ? nullptr : static_cast<HHOOK>(TlsGetValue(cbt_index));
        }

        void suppressed(const char* call, HWND window)
        {
            // Deliberately not write_once: every attempted activation has its own evidence.
            logger::write("test: NoActivate suppressed %s (hwnd %p thread %lu)", call, window, GetCurrentThreadId());
        }

        LRESULT CALLBACK cbt_procedure(int code, WPARAM w, LPARAM l)
        {
            if ((code == HCBT_ACTIVATE || code == HCBT_SETFOCUS) && ours(reinterpret_cast<HWND>(w)))
            {
                suppressed(code == HCBT_ACTIVATE ? "CBT HCBT_ACTIVATE (including indirect/system calls)" : "CBT HCBT_SETFOCUS", reinterpret_cast<HWND>(w));
                return 1; // veto BEFORE activation, never recover the owner's focus afterwards
            }
            if (active && code == HCBT_CREATEWND && l)
            {
                auto* create = reinterpret_cast<CBT_CREATEWNDA*>(l);
                if (!(create->lpcs->style & WS_CHILD))
                {
                    create->hwndInsertAfter = HWND_BOTTOM;
                    suppressed("CBT HCBT_CREATEWND z-order", reinterpret_cast<HWND>(w));
                }
            }
            return next_hook(nullptr, code, w, l); // CallNextHookEx ignores its HHOOK argument
        }

        bool protect_thread()
        {
            if (thread_hook()) return true;
            if (cbt_index != TLS_OUT_OF_INDEXES)
            {
                if (const auto hook = set_hook(WH_CBT, &cbt_procedure, nullptr, GetCurrentThreadId()))
                {
                    if (TlsSetValue(cbt_index, hook)) return true;
                    UnhookWindowsHookEx(hook);
                }
            }
            logger::write("test: NoActivate ERROR: CBT installation failed (%lu); game window creation refused", GetLastError());
            return false;
        }

        void safe_position(WINDOWPOS& position, const char* call)
        {
            if (!(position.flags & SWP_NOACTIVATE) || !(position.flags & SWP_NOZORDER))
            {
                suppressed(call, position.hwnd);
                position.flags |= SWP_NOACTIVATE;
                position.flags &= ~static_cast<UINT>(SWP_NOZORDER);
                position.hwndInsertAfter = HWND_BOTTOM;
            }
        }

        LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w, LPARAM l)
        {
            auto previous = reinterpret_cast<WNDPROC>(get_property(window, previous_property));
            if (message == WM_MOUSEACTIVATE)
            {
                suppressed("WM_MOUSEACTIVATE/DefWindowProc", window);
                return MA_NOACTIVATEANDEAT;
            }
            if (message == WM_WINDOWPOSCHANGING && l)
                safe_position(*reinterpret_cast<WINDOWPOS*>(l), "WM_WINDOWPOSCHANGING (including D3D/DefWindowProc)");
            if (message == WM_STYLECHANGING && w == static_cast<WPARAM>(GWL_EXSTYLE) && l)
            {
                auto* style = reinterpret_cast<STYLESTRUCT*>(l);
                const DWORD safe = (style->styleNew | WS_EX_NOACTIVATE) & ~static_cast<DWORD>(WS_EX_TOPMOST);
                if (safe != style->styleNew) suppressed("WM_STYLECHANGING", window);
                style->styleNew = safe;
            }
            const LRESULT result = previous ? call_procedure(previous, window, message, w, l) : DefWindowProcA(window, message, w, l);
            if (message == WM_NCDESTROY) remove_property(window, previous_property);
            return result;
        }

        HWND WINAPI create_window(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int width, int height,
                                  HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
        {
            const bool protect = active && !(style & WS_CHILD);
            if (protect)
            {
                if (!protect_thread()) return nullptr;
                ex = (ex | WS_EX_NOACTIVATE) & ~static_cast<DWORD>(WS_EX_TOPMOST);
                suppressed("CreateWindowExA", nullptr);
            }
            const HWND result = real_create(ex, cls, title, style, x, y, width, height, parent, menu, instance, param);
            if (protect && result)
            {
                const auto previous = get_long(result, GWL_WNDPROC);
                if (!previous || !set_property(result, previous_property, reinterpret_cast<HANDLE>(previous)))
                {
                    logger::write("test: NoActivate ERROR: window subclass property failed; window destroyed");
                    DestroyWindow(result);
                    return nullptr;
                }
                SetLastError(0);
                if (!set_long(result, GWL_WNDPROC, reinterpret_cast<LONG>(&window_procedure)) && GetLastError())
                {
                    remove_property(result, previous_property);
                    DestroyWindow(result);
                    logger::write("test: NoActivate ERROR: window subclass failed; window destroyed");
                    return nullptr;
                }
            }
            return result;
        }

        int show_command(HWND window, int command, const char* call)
        {
            if (ours(window) && command != SW_HIDE && command != SW_SHOWNOACTIVATE && command != SW_SHOWNA && command != SW_SHOWMINNOACTIVE)
            {
                suppressed(call, window);
                return command == SW_MINIMIZE || command == SW_SHOWMINIMIZED || command == SW_FORCEMINIMIZE ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE;
            }
            return command;
        }

        BOOL WINAPI show_window(HWND window, int command) { return real_show(window, show_command(window, command, "ShowWindow")); }
        BOOL WINAPI show_async(HWND window, int command) { return real_show_async(window, show_command(window, command, "ShowWindowAsync")); }
        BOOL WINAPI position_window(HWND window, HWND after, int x, int y, int width, int height, UINT flags)
        {
            WINDOWPOS position{window, after, x, y, width, height, flags};
            if (ours(window)) safe_position(position, "SetWindowPos");
            return real_position(window, position.hwndInsertAfter, x, y, width, height, position.flags);
        }
        BOOL WINAPI foreground(HWND window) { if (ours(window)) { suppressed("SetForegroundWindow", window); return FALSE; } return real_foreground(window); }
        HWND WINAPI activate(HWND window) { if (ours(window)) { suppressed("SetActiveWindow", window); return nullptr; } return real_active(window); }
        HWND WINAPI focus(HWND window) { if (ours(window)) { suppressed("SetFocus", window); return nullptr; } return real_focus(window); }
        BOOL WINAPI bring(HWND window) { if (ours(window)) { suppressed("BringWindowToTop", window); return FALSE; } return real_bring(window); }
        void WINAPI switch_window(HWND window, BOOL alt) { if (ours(window)) { suppressed("SwitchToThisWindow", window); return; } real_switch(window, alt); }
    }

    bool enabled() { return active; }

    void presentation(d3d8::present_parameters& pp, const char* call)
    {
        if (!active || pp.windowed) return;
        suppressed(call, pp.device_window);
        pp.windowed = TRUE;
        pp.fullscreen_refresh_rate = 0;
        pp.fullscreen_presentation_interval = d3d8::present_interval_default;
    }

    void install(HMODULE game)
    {
        if (!ini::flag(L"Test", L"NoActivate", false)) return; // OFF: no hooks, logs or window changes
        active = true;
        cbt_index = TlsAlloc();
        protect_thread(); // also cover indirect creation/activation on the initializing game thread
        const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            // Hook the known creation owner even if enumeration fails.
            iat_hook::hook(GetModuleHandleA("libIGDisplay.dll"), "USER32.dll", "CreateWindowExA", 0, reinterpret_cast<void*>(&create_window));
            logger::write("test: NoActivate ERROR: module enumeration failed (%lu)", GetLastError());
            return;
        }
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&install), &self);
        MODULEENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        unsigned hooks = 0;
        if (Module32First(snapshot, &entry)) do
        {
            // Do not patch Windows or vendor DLLs. Their activation attempts reach CBT and the subclass.
            if (entry.hModule != game && entry.hModule != self && _strnicmp(entry.szModule, "libIG", 5) != 0) continue;
            struct replacement { const char* name; void* address; };
            const replacement table[]{
                {"CreateWindowExA", reinterpret_cast<void*>(&create_window)}, {"ShowWindow", reinterpret_cast<void*>(&show_window)},
                {"ShowWindowAsync", reinterpret_cast<void*>(&show_async)}, {"SetWindowPos", reinterpret_cast<void*>(&position_window)},
                {"SetForegroundWindow", reinterpret_cast<void*>(&foreground)}, {"SetActiveWindow", reinterpret_cast<void*>(&activate)},
                {"SetFocus", reinterpret_cast<void*>(&focus)}, {"BringWindowToTop", reinterpret_cast<void*>(&bring)},
                {"SwitchToThisWindow", reinterpret_cast<void*>(&switch_window)}
            };
            for (const auto& item : table)
                hooks += iat_hook::hook(entry.hModule, "USER32.dll", item.name, 0, item.address) != nullptr;
        } while (Module32Next(snapshot, &entry));
        CloseHandle(snapshot);
        logger::write("test: NoActivate enabled: %u imports; window-thread CBT veto and nonactivating window messages", hooks);
    }
}
