// Production cache + production replay hook, with fake COM enumeration. No game or input injection.
#include <Windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include "controller_scan_cache.hpp"
#include "../src/gamepad_fix.cpp"

namespace logger
{
    std::atomic<unsigned> timeout_lines{0}, reentry_lines{0};
    void write(const char* fmt, ...) { if (std::strstr(fmt, "published no device list")) ++timeout_lines; }
    void write_once(std::string_view key, const char*, ...) { if (key == "scan-reentry") ++reentry_lines; }
}
namespace xinput_pad
{
    bool starting() { return false; }
    bool read_raw(int, raw_state&) { return false; }
    std::vector<int> connected_indices() { return {}; }
}
namespace pad_input { void on_read(int, xinput_pad::raw_state&, bool) {} }
namespace virtual_pad
{
    int virtual_count = 0;
    int count() { return virtual_count; }
    bool is_virtual(REFGUID) { return false; }
    HRESULT create(REFGUID, const IID*, bool, bool, void**) { return E_NOTIMPL; }
    bool enumerate(bool, bool wide, void* cb, void* user)
    {
        DIDEVICEINSTANCEW item{};
        item.dwSize = wide ? sizeof(DIDEVICEINSTANCEW) : sizeof(DIDEVICEINSTANCEA);
        item.guidInstance.Data1 = 123;
        return reinterpret_cast<LPDIENUMDEVICESCALLBACKW>(cb)(&item, user) != DIENUM_STOP;
    }
}
namespace
{
    unsigned checks = 0;
    void check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "check %u failed\n", checks); std::abort(); } }
    const DWORD caller_thread = GetCurrentThreadId();
    struct input
    {
        void** vtable;
        std::atomic<unsigned> refs{1}, calls{0}, on_caller{0};
        std::vector<controller_scan_cache::instance> devices;
        HRESULT result = DI_OK;
        HANDLE entered = nullptr, resume = nullptr;
    };
    ULONG STDMETHODCALLTYPE add_ref(void* p) { return ++static_cast<input*>(p)->refs; }
    ULONG STDMETHODCALLTYPE release(void* p) { return --static_cast<input*>(p)->refs; }
    HRESULT STDMETHODCALLTYPE enumerate(void* p, DWORD, LPDIENUMDEVICESCALLBACKW cb, void* user, DWORD)
    {
        auto& obj = *static_cast<input*>(p);
        ++obj.calls;
        if (GetCurrentThreadId() == caller_thread) ++obj.on_caller;
        if (!cb) return DIERR_INVALIDPARAM;
        if (obj.entered) SetEvent(obj.entered);
        if (obj.resume) WaitForSingleObject(obj.resume, 3000);
        for (const auto& item : obj.devices)
            if (cb(reinterpret_cast<LPCDIDEVICEINSTANCEW>(item.bytes.data()), user) == DIENUM_STOP) break;
        return obj.result;
    }
    void* table[10]{nullptr, reinterpret_cast<void*>(&add_ref), reinterpret_cast<void*>(&release), nullptr, reinterpret_cast<void*>(&enumerate)};

    // Wine's IDirectInput8A::EnumDevices: it calls the W interface through the W vtable - the hook's patched slot.
    void* forward_to = nullptr;
    std::atomic<DWORD> forward_thread{0};
    std::atomic<bool> probe_inside{false};
    HRESULT STDMETHODCALLTYPE enumerate_forward(void* p, DWORD type, LPDIENUMDEVICESCALLBACKW cb, void* user, DWORD flags)
    {
        ++static_cast<input*>(p)->calls;
        if (GetCurrentThreadId() == caller_thread) ++static_cast<input*>(p)->on_caller;
        forward_thread = GetCurrentThreadId();
        return gamepad_fix::enum_devices(forward_to, type, cb, user, flags);
    }
    // Records whether the cache's scan is marked as running on this thread.
    HRESULT STDMETHODCALLTYPE enumerate_probe(void* p, DWORD type, LPDIENUMDEVICESCALLBACKW cb, void* user, DWORD flags)
    {
        probe_inside = controller_scan_cache::scanning_here();
        return enumerate(p, type, cb, user, flags);
    }
    // A scan that never returns on the worker (until the test releases it); the caller's own scan works.
    HANDLE stall_entered = nullptr, stall_release = nullptr;
    HRESULT STDMETHODCALLTYPE enumerate_stall(void* p, DWORD type, LPDIENUMDEVICESCALLBACKW cb, void* user, DWORD flags)
    {
        if (GetCurrentThreadId() != caller_thread)
        {
            SetEvent(stall_entered);
            WaitForSingleObject(stall_release, INFINITE);
        }
        return enumerate(p, type, cb, user, flags);
    }
    void* forward_table[10]{nullptr, reinterpret_cast<void*>(&add_ref), reinterpret_cast<void*>(&release), nullptr, reinterpret_cast<void*>(&enumerate_forward)};
    void* wide_table[10]{nullptr, reinterpret_cast<void*>(&add_ref), reinterpret_cast<void*>(&release), nullptr, reinterpret_cast<void*>(&enumerate)};
    void* probe_table[10]{nullptr, reinterpret_cast<void*>(&add_ref), reinterpret_cast<void*>(&release), nullptr, reinterpret_cast<void*>(&enumerate_probe)};
    void* stall_table[10]{nullptr, reinterpret_cast<void*>(&add_ref), reinterpret_cast<void*>(&release), nullptr, reinterpret_cast<void*>(&enumerate_stall)};
    controller_scan_cache::instance fixture(bool wide, bool dx3, unsigned id)
    {
        controller_scan_cache::instance out;
        const DWORD size = wide ? (dx3 ? sizeof(DIDEVICEINSTANCE_DX3W) : sizeof(DIDEVICEINSTANCEW)) :
                                 (dx3 ? sizeof(DIDEVICEINSTANCE_DX3A) : sizeof(DIDEVICEINSTANCEA));
        for (DWORD i = 0; i < size; ++i) out.bytes[i] = std::byte((i * 17 + id) & 255);
        auto* header = reinterpret_cast<DIDEVICEINSTANCEW*>(out.bytes.data());
        header->dwSize = size;
        header->guidInstance.Data1 = id;
        // Logitech: the original bytes pass through the real spoof decision unchanged.
        header->guidProduct = virtual_pad_rules::product_guid(0x046d, 0xc216);
        header->dwDevType = 0x15;
        return out;
    }
    struct received
    {
        std::vector<controller_scan_cache::instance> devices;
        unsigned stop_at = 0;
        DWORD thread = 0;
    };
    BOOL CALLBACK callback(LPCDIDEVICEINSTANCEW item, void* user)
    {
        auto& target = *static_cast<received*>(user);
        target.thread = GetCurrentThreadId();
        controller_scan_cache::instance copy;
        std::memcpy(copy.bytes.data(), item, item->dwSize);
        target.devices.push_back(copy);
        return target.stop_at && target.devices.size() >= target.stop_at ? DIENUM_STOP : DIENUM_CONTINUE;
    }
    bool same(const std::vector<controller_scan_cache::instance>& a, const std::vector<controller_scan_cache::instance>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (a[i].bytes != b[i].bytes) return false;
        return true;
    }
    const controller_scan_cache::topology plugged{{1, RIM_TYPEHID, L"pad-one"}};
    void tick(controller_scan_cache::cache& cache, std::uint64_t now, std::optional<controller_scan_cache::topology> devices = plugged)
    {
        std::thread worker([&] { cache.tick(now, devices); }); worker.join();
    }
}
int main(int argc, char** argv)
{
    // Exact A/W and DX3 sizes, old versions, independent flags, callback STOP cannot poison a cache.
    for (int version : {3, 5, 7, 8}) for (bool wide : {false, true}) for (bool dx3 : {false, true})
    {
        input obj{table}; obj.devices = {fixture(wide, dx3, 1), fixture(wide, dx3, 2)};
        {
            controller_scan_cache::cache cache;
            controller_scan_cache::key key{&obj, version, wide, 4, DIEDFL_ATTACHEDONLY};
            check(!cache.request(key, &enumerate, 0)); check(obj.calls == 0); check(obj.refs == 2);
            tick(cache, 0);
            const auto list = cache.request(key, &enumerate, 1);
            check(list && same(list->devices, obj.devices)); check(obj.on_caller == 0);
            received actual;
            gamepad_fix::enum_context context{&callback, &actual, version == 8};
            for (const auto& item : list->devices)
                if (gamepad_fix::enum_callback(reinterpret_cast<LPCDIDEVICEINSTANCEW>(item.bytes.data()), &context) == DIENUM_STOP) break;
            check(same(actual.devices, obj.devices)); check(actual.thread == caller_thread);
            actual = {}; actual.stop_at = 1;
            context = {&callback, &actual, version == 8};
            for (const auto& item : list->devices)
                if (gamepad_fix::enum_callback(reinterpret_cast<LPCDIDEVICEINSTANCEW>(item.bytes.data()), &context) == DIENUM_STOP) break;
            check(context.stopped && actual.devices.size() == 1); check(list->devices.size() == 2);
            for (unsigned t = 1000; t < 30000; t += 1000) { cache.request(key, &enumerate, t); tick(cache, t); }
            check(obj.calls == 1);
            cache.request(key, &enumerate, 30000); tick(cache, 30000); check(obj.calls == 2);
            // A same-count replacement, unplug, and plug each invalidate the list.
            obj.devices = {fixture(wide, dx3, 3)};
            tick(cache, 30250, controller_scan_cache::topology{{1, RIM_TYPEHID, L"pad-replaced"}});
            check(same(cache.request(key, &enumerate, 30251)->devices, obj.devices));
            obj.devices.clear(); tick(cache, 30500, controller_scan_cache::topology{});
            check(cache.request(key, &enumerate, 30501)->devices.empty());
            obj.devices = {fixture(wide, dx3, 4)}; tick(cache, 30750);
            check(same(cache.request(key, &enumerate, 30751)->devices, obj.devices));
            // Failure is not a successful partial list; retries are bounded to one second.
            obj.result = E_FAIL; tick(cache, 31000, controller_scan_cache::topology{{2, RIM_TYPEHID, L"failure"}});
            check(FAILED(cache.request(key, &enumerate, 31001)->result));
            const auto calls = obj.calls.load(); tick(cache, 31250, {}); check(obj.calls == calls);
            obj.result = DI_OK; tick(cache, 32000, {}); check(obj.calls == calls + 1);
            auto other_flags = key; other_flags.flags = 0;
            check(!cache.request(other_flags, &enumerate, 32001)); tick(cache, 32250);
            check(cache.request(other_flags, &enumerate, 32251) != nullptr);
            tick(cache, 100000); check(obj.refs == 1); // idle queries release their retained COM references
        }
        check(obj.refs == 1);
    }
    if (argc > 1 && std::strcmp(argv[1], "--compare-real") == 0)
    {
        // Read-only comparison with Windows' actual enumeration. Both scans run
        // on owned test threads; no game, window, Acquire or input injection.
        wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
        using create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, LPUNKNOWN);
        for (int version : {7, 8}) for (bool wide : {false, true})
        {
            const auto dll = LoadLibraryW((std::wstring(system) + (version == 8 ? L"\\dinput8.dll" : L"\\dinput.dll")).c_str());
            const auto create = reinterpret_cast<create_t>(GetProcAddress(dll, version == 8 ? "DirectInput8Create" : "DirectInputCreateEx"));
            void* object = nullptr;
            const IID& iid = version == 8 ? (wide ? IID_IDirectInput8W : IID_IDirectInput8A) : (wide ? IID_IDirectInput7W : IID_IDirectInput7A);
            check(create && SUCCEEDED(create(GetModuleHandleW(nullptr), version == 8 ? 0x0800 : 0x0700, iid, &object, nullptr)));
            const auto real = reinterpret_cast<controller_scan_cache::enumerate_t>((*static_cast<void***>(object))[4]);
            received reference;
            HRESULT result = E_FAIL;
            std::thread real_scan([&] { result = real(object, 4, &callback, &reference, DIEDFL_ATTACHEDONLY); }); real_scan.join();
            check(SUCCEEDED(result));
            {
                controller_scan_cache::cache cache;
                controller_scan_cache::key key{object, version, wide, 4, DIEDFL_ATTACHEDONLY};
                cache.request(key, real, 0); tick(cache, 0);
                const auto replay = cache.request(key, real, 1);
                check(replay && replay->result == result && same(replay->devices, reference.devices));
                std::printf("real DirectInput %d%s: %zu device(s), exact raw snapshot comparison PASS\n", version, wide ? "W" : "A", reference.devices.size());
            }
            static_cast<IUnknown*>(object)->Release();
            FreeLibrary(dll);
        }
    }
    // A cold query preserves the complete first-list contract. Only this first
    // publication waits; the driver is still called exclusively by the worker.
    {
        input obj{table}; obj.devices = {fixture(false, false, 1)};
        obj.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr); obj.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        controller_scan_cache::cache cache;
        controller_scan_cache::key key{&obj, 8, false, 4, 1};
        cache.request(key, &enumerate, 0);
        std::thread worker([&] { cache.tick(0, plugged); });
        check(WaitForSingleObject(obj.entered, 2000) == WAIT_OBJECT_0);
        std::atomic<bool> returned{false};
        std::shared_ptr<const controller_scan_cache::snapshot> first;
        std::thread waiter([&] { first = cache.wait_for_first(key, INFINITE); returned = true; });
        Sleep(10); check(!returned);
        SetEvent(obj.resume); worker.join(); waiter.join();
        check(first && same(first->devices, obj.devices)); check(obj.on_caller == 0);
        CloseHandle(obj.entered); CloseHandle(obj.resume);
    }
    // Fix AT: a scan's own native enumeration is marked on its thread (the hook passes it straight through),
    // nowhere else and not after it returns.
    {
        input obj{probe_table}; obj.devices = {fixture(false, false, 41)};
        controller_scan_cache::cache cache;
        controller_scan_cache::key key{&obj, 8, false, 4, 1};
        check(!controller_scan_cache::scanning_here());
        cache.request(key, &enumerate_probe, 0);
        cache.tick(0, plugged); // on this thread
        check(probe_inside); check(!controller_scan_cache::scanning_here());
        probe_inside = false;
        cache.request(key, &enumerate_probe, 30000);
        std::atomic<bool> other_thread_inside{true};
        std::thread worker([&] { cache.tick(30000, plugged); other_thread_inside = controller_scan_cache::scanning_here(); });
        worker.join();
        check(probe_inside); check(!other_thread_inside); check(!controller_scan_cache::scanning_here());
    }
    // Fix AT: a stalled first scan - the cold query gives up after its limit (nullptr), not before; the stalled
    // scan still publishes once it returns.
    {
        input obj{table}; obj.devices = {fixture(false, false, 42)};
        obj.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr); obj.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        controller_scan_cache::cache cache;
        controller_scan_cache::key key{&obj, 8, false, 4, 1};
        cache.request(key, &enumerate, 0);
        std::thread worker([&] { cache.tick(0, plugged); });
        check(WaitForSingleObject(obj.entered, 2000) == WAIT_OBJECT_0);
        const auto before = GetTickCount64();
        check(cache.wait_for_first(key, 300) == nullptr);
        const auto waited = GetTickCount64() - before;
        check(waited >= 250 && waited < 2000);
        SetEvent(obj.resume); worker.join();
        const auto late = cache.wait_for_first(key, 300);
        check(late && same(late->devices, obj.devices));
        controller_scan_cache::key unknown{&obj, 8, false, 4, 0};
        check(cache.wait_for_first(unknown, INFINITE) == nullptr); // no such query: nothing to wait for
        CloseHandle(obj.entered); CloseHandle(obj.resume);
    }
    // Fix AT (c): the ini's valid value wins; without one the cache defaults to on, but off under Wine.
    check(gamepad_fix::cache_device_scan_choice(std::nullopt, false)); check(!gamepad_fix::cache_device_scan_choice(std::nullopt, true));
    check(gamepad_fix::cache_device_scan_choice(true, true)); check(gamepad_fix::cache_device_scan_choice(true, false));
    check(!gamepad_fix::cache_device_scan_choice(false, false)); check(!gamepad_fix::cache_device_scan_choice(false, true));
    if (const char* wine = gamepad_fix::wine_version()) std::printf("note: this test runs under Wine %s\n", wine);
    // A blocked refresh cannot hold up the caller or overwrite the immutable old snapshot.
    {
        input obj{table}; obj.devices = {fixture(false, false, 1)};
        controller_scan_cache::cache cache;
        controller_scan_cache::key key{&obj, 8, false, 4, 1};
        cache.request(key, &enumerate, 0); tick(cache, 0);
        const auto old = cache.request(key, &enumerate, 1);
        obj.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr); obj.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::thread worker([&] { cache.tick(250, controller_scan_cache::topology{}); });
        check(WaitForSingleObject(obj.entered, 2000) == WAIT_OBJECT_0);
        const auto before = GetTickCount64();
        for (unsigned i = 0; i < 10000; ++i) check(cache.request(key, &enumerate, 251) == old);
        check(GetTickCount64() - before < 100);
        SetEvent(obj.resume); worker.join();
        CloseHandle(obj.entered); CloseHandle(obj.resume);
    }
    // Real hook: OFF executes the original function on the caller, with identical callback output.
    // ON dispatches through the actual background service, with a complete first list.
    auto* obj = new input{table}; obj->devices = {fixture(false, false, 21), fixture(false, false, 22)};
    gamepad_fix::vtables[table] = {{{4, reinterpret_cast<void*>(&enumerate)}}, false, 8};
    gamepad_fix::cache_device_scan(false);
    received off;
    check(SUCCEEDED(gamepad_fix::enum_devices(obj, 4, &callback, &off, 1)));
    check(same(off.devices, obj->devices)); check(obj->on_caller == 1);
    received stop; stop.stop_at = 1;
    gamepad_fix::enum_devices(obj, 4, &callback, &stop, 1); check(stop.devices.size() == 1);
    check(gamepad_fix::enum_devices(obj, 4, nullptr, nullptr, 1) == DIERR_INVALIDPARAM);
    obj->on_caller = 0;
    gamepad_fix::cache_device_scan(true);
    received on;
    gamepad_fix::enum_devices(obj, 4, &callback, &on, 1);
    check(same(on.devices, obj->devices)); check(obj->on_caller == 0); check(on.thread == caller_thread);
    stop = {}; stop.stop_at = 1; gamepad_fix::enum_devices(obj, 4, &callback, &stop, 1); check(stop.devices.size() == 1);
    virtual_pad::virtual_count = 1;
    received virtuals;
    gamepad_fix::enum_devices(obj, 4, &callback, &virtuals, 1);
    check(virtuals.devices.size() == 1);
    check(reinterpret_cast<const DIDEVICEINSTANCEW*>(virtuals.devices[0].bytes.data())->guidInstance.Data1 == 123);
    virtual_pad::virtual_count = 0;
    for (int version : {3, 5, 7, 8}) for (bool wide : {false, true}) for (bool dx3 : {false, true})
    {
        auto* input = new ::input{table}; input->devices = {fixture(wide, dx3, unsigned(version))};
        gamepad_fix::vtables[table] = {{{4, reinterpret_cast<void*>(&enumerate)}}, wide, version};
        received output;
        gamepad_fix::enum_devices(input, 4, &callback, &output, 1);
        check(same(output.devices, input->devices)); check(input->on_caller == 0); check(output.thread == caller_thread);
    }
    // Fix AT (a), the Proton black screen: the worker's real A enumeration calls back into the hook through the W
    // vtable (as Wine's dinput does). The re-entered call goes straight to the original on the worker; the caller
    // gets the complete list at once - no wait on itself, no timeout.
    auto* narrow = new input{forward_table};
    auto* wide = new input{wide_table}; wide->devices = {fixture(false, false, 51), fixture(false, false, 52)};
    forward_to = wide;
    gamepad_fix::vtables[forward_table] = {{{4, reinterpret_cast<void*>(&enumerate_forward)}}, false, 8};
    gamepad_fix::vtables[wide_table] = {{{4, reinterpret_cast<void*>(&enumerate)}}, true, 8};
    {
        received forwarded;
        const auto before = GetTickCount64();
        check(SUCCEEDED(gamepad_fix::enum_devices(narrow, 4, &callback, &forwarded, 1)));
        const auto took = GetTickCount64() - before;
        check(took < controller_scan_cache::first_scan_timeout_ms / 2);
        check(same(forwarded.devices, wide->devices)); check(forwarded.thread == caller_thread);
        check(narrow->calls >= 1 && narrow->on_caller == 0); check(wide->calls >= 1 && wide->on_caller == 0);
        check(forward_thread != caller_thread); check(logger::reentry_lines == 1); check(logger::timeout_lines == 0);
        // Called directly by the game, the W interface is an ordinary cached query.
        received direct;
        gamepad_fix::enum_devices(wide, 4, &callback, &direct, 1);
        check(same(direct.devices, wide->devices)); check(wide->on_caller == 0);
    }
    // Fix AT (b): a first scan that never returns. The caller gives up after first_scan_timeout_ms, scans
    // synchronously itself (complete list), logs once, and every later query is synchronous at once - also a
    // previously cached one and the A->W forwarding path. (Last: the production worker stays stalled until released.)
    {
        stall_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr); stall_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        auto* stalled = new input{stall_table}; stalled->devices = {fixture(false, false, 61)};
        gamepad_fix::vtables[stall_table] = {{{4, reinterpret_cast<void*>(&enumerate_stall)}}, false, 8};
        received first;
        const auto before = GetTickCount64();
        check(SUCCEEDED(gamepad_fix::enum_devices(stalled, 4, &callback, &first, 1)));
        const auto took = GetTickCount64() - before;
        check(WaitForSingleObject(stall_entered, 0) == WAIT_OBJECT_0);
        check(took + 50 >= controller_scan_cache::first_scan_timeout_ms && took < controller_scan_cache::first_scan_timeout_ms + 3000);
        check(same(first.devices, stalled->devices)); check(stalled->on_caller == 1); check(logger::timeout_lines == 1);
        received again;
        const auto again_at = GetTickCount64();
        gamepad_fix::enum_devices(stalled, 4, &callback, &again, 0); // a new cold key
        check(GetTickCount64() - again_at < 1000); check(same(again.devices, stalled->devices)); check(stalled->on_caller == 2);
        received cached;
        gamepad_fix::enum_devices(wide, 4, &callback, &cached, 1); // published before the timeout
        check(same(cached.devices, wide->devices)); check(wide->on_caller == 1);
        received forwarded;
        gamepad_fix::enum_devices(narrow, 4, &callback, &forwarded, 1);
        check(same(forwarded.devices, wide->devices)); check(wide->on_caller == 2);
        check(GetTickCount64() - again_at < 1000); check(logger::timeout_lines == 1);
        SetEvent(stall_release);
    }
    std::printf("PASS controller scan cache: %u checks; 16 A/W/version/size combinations; no real enumeration on caller with ON; "
                "re-entered scan passed through; stalled first scan fell back after %lu ms\n", checks,
                static_cast<unsigned long>(controller_scan_cache::first_scan_timeout_ms));
    std::fflush(stdout);
    // The production service intentionally has process lifetime; do not run fixture CRT
    // teardown while its retained fake COM object is available to that service.
    TerminateProcess(GetCurrentProcess(), 0);
}
