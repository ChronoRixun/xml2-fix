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
    void write(const char*, ...) {}
    void write_once(std::string_view, const char*, ...) {}
}
namespace xinput_pad
{
    bool starting() { return false; }
    bool read_raw(int, raw_state&) { return false; }
    std::vector<int> connected_indices() { return {}; }
}
namespace pad_input { void on_read(int, xinput_pad::raw_state&, bool) {} }
namespace pad_hotplug { void on_keyboard_read(void*, DWORD) {} } // independent native-input integration has its own fixture
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
        std::thread waiter([&] { first = cache.wait_for_first(key); returned = true; });
        Sleep(10); check(!returned);
        SetEvent(obj.resume); worker.join(); waiter.join();
        check(first && same(first->devices, obj.devices)); check(obj.on_caller == 0);
        CloseHandle(obj.entered); CloseHandle(obj.resume);
    }
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
    std::printf("PASS controller scan cache: %u checks; 16 A/W/version/size combinations; no real enumeration on caller with ON\n", checks);
    std::fflush(stdout);
    // The production service intentionally has process lifetime; do not run fixture CRT
    // teardown while its retained fake COM object is available to that service.
    TerminateProcess(GetCurrentProcess(), 0);
}
