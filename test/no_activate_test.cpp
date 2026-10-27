// No interactive desktop windows, input or game code: exercise the production
// wrappers, CBT callback and subclass against a fake Win32 dispatch.
#include "../src/no_activate.cpp"
#include "../src/display.cpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    int checks = 0, forwarded = 0, hook_calls = 0;
    bool fail_hook = false;
    HWND fake = reinterpret_cast<HWND>(0x1234), child = reinterpret_cast<HWND>(0x1235), other = reinterpret_cast<HWND>(0x2345);
    DWORD created_ex = 0;
    UINT positioned_flags = 0;
    HWND positioned_after = nullptr;
    int shown = -1;
    HANDLE previous = nullptr;
    LONG installed_procedure = 0;
    std::vector<std::string> logs;
    void check_at(bool value, int line) { ++checks; if (!value) { std::fprintf(stderr, "check %d failed at line %d\n", checks, line); std::fflush(stderr); } assert(value); }
#define check(value) check_at((value), __LINE__)
    DWORD WINAPI fake_process(HWND h, LPDWORD process) { *process = h == fake || h == child ? GetCurrentProcessId() : GetCurrentProcessId() + 1; return 1; }
    LRESULT WINAPI fake_next(HHOOK, int, WPARAM, LPARAM) { ++forwarded; return 17; }
    HHOOK WINAPI fake_hook(int kind, HOOKPROC proc, HINSTANCE, DWORD thread) { ++hook_calls; check(kind == WH_CBT && proc == &no_activate::cbt_procedure && thread == GetCurrentThreadId()); return fail_hook ? nullptr : reinterpret_cast<HHOOK>(1); }
    LRESULT CALLBACK fake_engine(HWND, UINT, WPARAM, LPARAM) { ++forwarded; return 19; }
    LONG WINAPI fake_get_long(HWND, int) { return reinterpret_cast<LONG>(&fake_engine); }
    LONG WINAPI fake_set_long(HWND, int, LONG value) { installed_procedure = value; return reinterpret_cast<LONG>(&fake_engine); }
    HANDLE WINAPI fake_get_prop(HWND, LPCSTR) { return previous; }
    BOOL WINAPI fake_set_prop(HWND, LPCSTR, HANDLE value) { previous = value; return TRUE; }
    HANDLE WINAPI fake_remove_prop(HWND, LPCSTR) { const auto p = previous; previous = nullptr; return p; }
    LRESULT WINAPI fake_call(WNDPROC proc, HWND h, UINT m, WPARAM w, LPARAM l) { return proc(h, m, w, l); }
    HWND WINAPI fake_create(DWORD ex, LPCSTR cls, LPCSTR, DWORD style, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID)
    {
        ++forwarded; created_ex = ex;
        CREATESTRUCTA cs{}; cs.lpszClass = cls; cs.style = style;
        CBT_CREATEWNDA cb{&cs, HWND_TOP};
        if (no_activate::active)
        {
            no_activate::cbt_procedure(HCBT_CREATEWND, reinterpret_cast<WPARAM>(fake), reinterpret_cast<LPARAM>(&cb));
            check(cb.hwndInsertAfter == ((style & WS_CHILD) ? HWND_TOP : HWND_BOTTOM));
        }
        return fake;
    }
    BOOL WINAPI fake_show(HWND, int command) { ++forwarded; shown = command; return TRUE; }
    BOOL WINAPI fake_position(HWND, HWND after, int, int, int, int, UINT flags) { ++forwarded; positioned_after = after; positioned_flags = flags; return TRUE; }
    BOOL WINAPI fake_bool(HWND) { ++forwarded; return TRUE; }
    HWND WINAPI fake_hwnd(HWND) { ++forwarded; return other; }
    void WINAPI fake_switch(HWND, BOOL) { ++forwarded; }

    int delivered = 0;
    int rendered = 0, paced = 0, sampled = 0;
    HRESULT STDMETHODCALLTYPE synthetic_present(void*, const RECT*, const RECT*, HWND, const void*) { ++rendered; return S_OK; }
    HRESULT STDMETHODCALLTYPE synthetic_cooperative(void*) { return S_OK; }
    int device_calls = 0, reset_calls = 0;
    void* device_vtable[16]{};
    void** synthetic_device = device_vtable;
    HRESULT STDMETHODCALLTYPE synthetic_reset(void*, d3d8::present_parameters* pp)
    {
        ++reset_calls;
        check(pp && pp->windowed && pp->fullscreen_refresh_rate == 0 && pp->fullscreen_presentation_interval == 0);
        return pp->multi_sample_type ? E_FAIL : S_OK;
    }
    HRESULT STDMETHODCALLTYPE synthetic_create(void*, UINT, DWORD, HWND, DWORD, d3d8::present_parameters* pp, void** out)
    {
        ++device_calls;
        check(pp && pp->windowed && pp->fullscreen_refresh_rate == 0 && pp->fullscreen_presentation_interval == 0);
        if (pp->multi_sample_type) return E_FAIL;
        *out = &synthetic_device;
        return S_OK;
    }

    void device_host(HWND window)
    {
        display::opts.window_mode = display_rules::mode::windowed;
        display::opts.run_in_background = true;
        display::stock_fallback = true; // exercise the engine-parameter fallback path
        display::real_create_device = &synthetic_create;
        device_vtable[d3d8::device_slot::reset] = reinterpret_cast<void*>(&synthetic_reset);
        device_vtable[d3d8::device_slot::test_cooperative_level] = reinterpret_cast<void*>(&synthetic_cooperative);
        device_vtable[d3d8::device_slot::present] = reinterpret_cast<void*>(&synthetic_present);
        d3d8::present_parameters pp{};
        pp.back_buffer_width = 640; pp.back_buffer_height = 480;
        pp.device_window = window; pp.multi_sample_type = 4;
        pp.fullscreen_refresh_rate = 75; pp.fullscreen_presentation_interval = 1;
        void* result = nullptr;
        check(display::hooked_create_device(nullptr, 0, d3d8::device_type_hal, window, 2, &pp, &result) == S_OK);
        check(device_calls == 2 && result == &synthetic_device && pp.windowed && pp.multi_sample_type == 0);
        // Both early stock fallback and the full reset retry path retain windowed parameters.
        pp.windowed = FALSE; pp.fullscreen_refresh_rate = 75; pp.fullscreen_presentation_interval = 1;
        check(display::hooked_reset(result, &pp) == S_OK && reset_calls == 1);
        display::stock_fallback = false; display::d3d = nullptr; display::have_applied = false;
        pp.windowed = FALSE; pp.multi_sample_type = 4; pp.fullscreen_refresh_rate = 75; pp.fullscreen_presentation_interval = 1;
        check(display::hooked_reset(result, &pp) == S_OK && reset_calls == 3 && pp.multi_sample_type == 0 && pp.windowed);
        std::puts("Production D3D CreateDevice/Reset: exclusive requests and retry paths stay windowed");
    }

    void render_host()
    {
        display::opts.window_mode = display_rules::mode::windowed;
        display::opts.run_in_background = true;
        display::stock_fallback = false;
        display::paused = true; // prove recovery from a previous pause as well
        display::frame_hook = nullptr;
        display::real_present = &synthetic_present;
        display::real_test_cooperative_level = &synthetic_cooperative;
        check(!display::game_in_foreground());
        for (int frame = 0; frame < 10; ++frame)
        {
            check(display::hooked_test_cooperative_level(nullptr) == S_OK);
            check(display::hooked_present(nullptr, nullptr, nullptr, nullptr, nullptr) == S_OK);
        }
        check(rendered == 10 && paced == 10 && sampled == 10 && !display::paused);
        display::opts.run_in_background = false;
        check(display::hooked_present(nullptr, nullptr, nullptr, nullptr, nullptr) == d3d8::err_device_lost);
        check(display::hooked_test_cooperative_level(nullptr) == d3d8::err_device_lost && rendered == 10);
        display::opts.run_in_background = true;
        check(display::hooked_test_cooperative_level(nullptr) == S_OK);
        check(display::hooked_present(nullptr, nullptr, nullptr, nullptr, nullptr) == S_OK && rendered == 11 && !display::paused);
        std::puts("Production display Present/cooperative hooks: background frames continue; explicit pause and resume preserved");
    }
    LRESULT CALLBACK synthetic_engine(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message == WM_APP + 1) { ++delivered; return 73; }
        return DefWindowProcA(window, message, w, l);
    }

    void system_host()
    {
        // Never SwitchDesktop. Every real API call below targets a synthetic window
        // on this private, unshown desktop, even when invoked outside the IAT wrappers.
        const auto original_desktop = GetThreadDesktop(GetCurrentThreadId());
        char name[80]; sprintf_s(name, "XML2-NoActivate-host-%lu", GetCurrentProcessId());
        const auto desktop = CreateDesktopA(name, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        check(desktop != nullptr && SetThreadDesktop(desktop));
        using namespace no_activate;
        window_process = &GetWindowThreadProcessId; next_hook = &CallNextHookEx; set_hook = &SetWindowsHookExA;
        get_long = &GetWindowLongA; set_long = &SetWindowLongA;
        get_property = &GetPropA; set_property = &SetPropA; remove_property = &RemovePropA; call_procedure = &CallWindowProcA;
        real_create = &CreateWindowExA; real_show = &ShowWindow; real_show_async = &ShowWindowAsync; real_position = &SetWindowPos;
        real_foreground = &SetForegroundWindow; real_bring = &BringWindowToTop; real_active = &SetActiveWindow; real_focus = &SetFocus; real_switch = &SwitchToThisWindow;
        active = true; check(TlsSetValue(cbt_index, nullptr));
        WNDCLASSA cls{}; cls.lpfnWndProc = &synthetic_engine; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = game_class;
        check(RegisterClassA(&cls) != 0);
        // The same composition as production: display saves NoActivate's import
        // wrappers, and its mouse filter chains through the NoActivate subclass.
        display::opts.window_mode = display_rules::mode::windowed;
        display::opts.topmost = true; // even an upstream topmost request is neutralized
        display::geometry_hooked = true;
        display::desktop = {1280, 720, 60, d3d8::format_x8r8g8b8};
        display::real_create_window_ex_a = &create_window;
        display::real_set_window_pos = &position_window;
        display::real_show_window = &show_window;
        const auto window = display::hooked_create_window_ex_a(WS_EX_TOPMOST, game_class, "NoActivate synthetic host", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                          0, 0, 320, 240, nullptr, nullptr, cls.hInstance, nullptr);
        check(window != nullptr); // runner STARTUPINFO may hide the first show
        show_window(window, SW_SHOW);
        check((GetWindowLongA(window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0);
        check((GetWindowLongA(window, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
        check(GetActiveWindow() == nullptr && GetFocus() == nullptr);
        check(display::hooked_set_window_pos(window, HWND_TOPMOST, 0, 0, 320, 240, SWP_SHOWWINDOW | SWP_NOZORDER));
        display::hooked_show_window(window, SW_RESTORE);
        check(display::hooked_move_window(window, 0, 0, 320, 240, TRUE));
        check(GetActiveWindow() == nullptr && !(GetWindowLongA(window, GWL_EXSTYLE) & WS_EX_TOPMOST));
        const auto before = logs.size();
        // Bypass production wrappers, as dynamically resolved / D3D-internal calls do.
        SetActiveWindow(window); check(GetActiveWindow() == nullptr);
        SetFocus(window); check(GetFocus() == nullptr);
        SetForegroundWindow(window); check(GetActiveWindow() == nullptr && GetFocus() == nullptr);
        BringWindowToTop(window); check(GetActiveWindow() == nullptr);
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 320, 240, SWP_SHOWWINDOW);
        check(GetActiveWindow() == nullptr && !(GetWindowLongA(window, GWL_EXSTYLE) & WS_EX_TOPMOST));
        ShowWindow(window, SW_RESTORE); check(GetActiveWindow() == nullptr);
        check(logs.size() > before);
        check(SendMessageA(window, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATEANDEAT);
        SetWindowLongA(window, GWL_EXSTYLE, WS_EX_TOPMOST);
        check((GetWindowLongA(window, GWL_EXSTYLE) & (WS_EX_NOACTIVATE | WS_EX_TOPMOST)) == WS_EX_NOACTIVATE);
        for (int frame = 0; frame < 10; ++frame)
            check(SendMessageA(window, WM_APP + 1, frame, 0) == 73);
        check(delivered == 10 && (GetWindowLongA(window, GWL_STYLE) & WS_VISIBLE));
        device_host(window);
        render_host();
        check(DestroyWindow(window));
        check(UnhookWindowsHookEx(thread_hook())); check(TlsSetValue(cbt_index, nullptr));
        check(UnregisterClassA(game_class, cls.hInstance));
        check(SetThreadDesktop(original_desktop));
        check(CloseDesktop(desktop));
        std::puts("Private desktop synthetic window: indirect OS activation vetoed; 10 background messages delivered");
    }
}
namespace ini
{
    bool flag(const wchar_t*, const wchar_t*, bool fallback) { return fallback; }
    std::optional<std::string> text(const wchar_t*, const wchar_t*) { return std::nullopt; }
    int number(const wchar_t*, const wchar_t*, int fallback) { return fallback; }
}
namespace iat_hook { void* hook(HMODULE, const char*, const char*, WORD, void*) { assert(false); return nullptr; } }
namespace frame_rate { void on_present() { ++paced; } void retarget(const frame_rate_rules::cap&, bool) {} bool paces() { return false; } }
namespace pool_counters { void sample() { ++sampled; } bool enabled() { return false; } }
// 0.4.2 tree: display.cpp's Present also samples W3's ScriptPools and the opt-in E4 observer; both are off in this host.
namespace script_pools { void sample() {} }
namespace e4_observe { void sample() {} bool enabled() { return false; } }
namespace options_menu { void install(HMODULE) {} bool installed() { return false; } }
namespace logger
{
    void write_once(std::string_view, const char*, ...) {}
    void write(const char* format, ...)
    {
        char line[1024]; va_list args; va_start(args, format); vsnprintf(line, sizeof(line), format, args); va_end(args);
        logs.emplace_back(line);
    }
}

int main()
{
    using namespace no_activate;
    window_process = &fake_process; next_hook = &fake_next; set_hook = &fake_hook;
    get_long = &fake_get_long; set_long = &fake_set_long;
    get_property = &fake_get_prop; set_property = &fake_set_prop; remove_property = &fake_remove_prop; call_procedure = &fake_call;
    real_create = &fake_create; real_show = real_show_async = &fake_show; real_position = &fake_position;
    real_foreground = real_bring = &fake_bool; real_active = real_focus = &fake_hwnd; real_switch = &fake_switch;

    install(nullptr); // absent key: even import patching is forbidden by stub above
    check(!enabled() && logs.empty() && hook_calls == 0 && cbt_index == TLS_OUT_OF_INDEXES);
    check(foreground(fake) && activate(fake) == other && focus(fake) == other && bring(fake));
    switch_window(fake, TRUE); check(forwarded == 5);
    show_window(fake, SW_SHOWDEFAULT); check(shown == SW_SHOWDEFAULT);
    position_window(fake, HWND_TOPMOST, 1, 2, 3, 4, SWP_SHOWWINDOW); check(positioned_flags == SWP_SHOWWINDOW && positioned_after == HWND_TOPMOST);
    create_window(WS_EX_TOPMOST, game_class, "fake", WS_VISIBLE, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    check(created_ex == WS_EX_TOPMOST && installed_procedure == 0 && hook_calls == 0 && logs.empty());
    d3d8::present_parameters pp{}; pp.fullscreen_refresh_rate = 75; pp.fullscreen_presentation_interval = 1;
    const auto off = pp; presentation(pp, "off"); check(std::memcmp(&pp, &off, sizeof(pp)) == 0 && logs.empty());
    check(cbt_procedure(HCBT_ACTIVATE, reinterpret_cast<WPARAM>(fake), 0) == 17);

    cbt_index = TlsAlloc(); check(cbt_index != TLS_OUT_OF_INDEXES);
    active = true;
    int count = forwarded;
    check(!foreground(fake) && activate(fake) == nullptr && focus(fake) == nullptr && !bring(fake));
    switch_window(fake, TRUE); check(forwarded == count && logs.size() == 5);
    check(!foreground(child)); // child focus is guarded through its root
    check(foreground(other) && activate(other) == other && focus(other) == other && bring(other));
    switch_window(other, FALSE); check(forwarded == count + 5);
    for (int command = SW_HIDE; command <= SW_FORCEMINIMIZE; ++command)
    {
        const auto before = logs.size();
        show_window(fake, command);
        const bool already_safe = command == SW_HIDE || command == SW_SHOWNOACTIVATE || command == SW_SHOWNA || command == SW_SHOWMINNOACTIVE;
        check(logs.size() == before + (already_safe ? 0 : 1));
        check(shown == (already_safe ? command : command == SW_MINIMIZE || command == SW_SHOWMINIMIZED || command == SW_FORCEMINIMIZE ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE));
        show_async(fake, command); check(logs.size() == before + (already_safe ? 0 : 2));
        show_window(other, command); check(shown == command);
    }
    for (UINT flags : {0u, UINT(SWP_SHOWWINDOW), UINT(SWP_NOACTIVATE), UINT(SWP_NOACTIVATE | SWP_NOZORDER)})
    {
        position_window(fake, HWND_TOPMOST, 0, 0, 1, 1, flags);
        check(positioned_flags & SWP_NOACTIVATE);
        check((positioned_flags & SWP_NOZORDER) || positioned_after == HWND_BOTTOM);
    }
    check(cbt_procedure(HCBT_ACTIVATE, reinterpret_cast<WPARAM>(fake), 0) == 1);
    check(cbt_procedure(HCBT_SETFOCUS, reinterpret_cast<WPARAM>(child), 0) == 1);
    check(cbt_procedure(-1, reinterpret_cast<WPARAM>(fake), 0) == 17);
    check(cbt_procedure(HCBT_ACTIVATE, reinterpret_cast<WPARAM>(other), 0) == 17);
    check(create_window(WS_EX_TOPMOST, game_class, "fake", WS_VISIBLE, 0, 0, 640, 480, nullptr, nullptr, nullptr, nullptr) == fake);
    check((created_ex & WS_EX_NOACTIVATE) && !(created_ex & WS_EX_TOPMOST));
    check(installed_procedure == reinterpret_cast<LONG>(&window_procedure) && hook_calls == 1);
    create_window(0, game_class, "fake", 0, 0, 0, 640, 480, nullptr, nullptr, nullptr, nullptr);
    check(hook_calls == 1); // one CBT hook per creation thread, including recreation
    create_window(0, MAKEINTATOM(1), "fake atom class", 0, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    check((created_ex & WS_EX_NOACTIVATE) && hook_calls == 1);
    create_window(0, "another owned class", "fake helper", 0, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    check((created_ex & WS_EX_NOACTIVATE) && hook_calls == 1);
    const auto before_child = logs.size();
    create_window(0, "child", "fake child", WS_CHILD, 0, 0, 1, 1, fake, nullptr, nullptr, nullptr);
    check(created_ex == 0 && logs.size() == before_child && hook_calls == 1);
    count = forwarded;
    check(window_procedure(fake, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATEANDEAT && forwarded == count);
    WINDOWPOS pos{fake, HWND_TOPMOST, 0, 0, 640, 480, SWP_SHOWWINDOW};
    window_procedure(fake, WM_WINDOWPOSCHANGING, 0, reinterpret_cast<LPARAM>(&pos));
    check((pos.flags & SWP_NOACTIVATE) && pos.hwndInsertAfter == HWND_BOTTOM);
    STYLESTRUCT style{0, WS_EX_TOPMOST};
    window_procedure(fake, WM_STYLECHANGING, static_cast<WPARAM>(GWL_EXSTYLE), reinterpret_cast<LPARAM>(&style));
    check((style.styleNew & WS_EX_NOACTIVATE) && !(style.styleNew & WS_EX_TOPMOST));
    // The loop's messages, including losing focus, are delivered unmodified.
    for (UINT m : {UINT(WM_ACTIVATEAPP), UINT(WM_ACTIVATE), UINT(WM_KILLFOCUS), UINT(WM_PAINT), UINT(WM_TIMER)})
        check(window_procedure(fake, m, 0, 0) == 19);
    window_procedure(fake, WM_NCDESTROY, 0, 0); check(previous == nullptr);
    presentation(pp, "D3D8 CreateDevice");
    check(pp.windowed && pp.fullscreen_refresh_rate == 0 && pp.fullscreen_presentation_interval == 0);
    pp = off; presentation(pp, "D3D8 Reset fallback"); check(pp.windowed);
    check(TlsSetValue(cbt_index, nullptr)); fail_hook = true; count = forwarded;
    check(create_window(0, game_class, "fake", WS_VISIBLE, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr) == nullptr && forwarded == count);
    check(!ours(nullptr));
    system_host();
    check(TlsFree(cbt_index)); cbt_index = TLS_OUT_OF_INDEXES;
    for (const auto& line : logs) std::puts(line.c_str());
    std::printf("NoActivate production fake-window host: %d checks, 0 failures; fake dispatch + private desktop, no game or input\n", checks);
}
