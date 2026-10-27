// Exercises the production functions, including publication while a slot read fails.
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "../src/xinput_pad.cpp"
namespace logger { void write(const char*, ...) {} }
namespace
{
    unsigned checks = 0;
    void check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "check %u failed\n", checks); std::abort(); } }
    std::atomic<unsigned> devices{1}, calls[4]{};
    HANDLE reached = nullptr, resume = nullptr;
    DWORD blocked_thread = 0;
    DWORD WINAPI fake_state(DWORD slot, XINPUT_STATE* out)
    {
        ++calls[slot];
        const bool connected = (devices.load() & (1u << slot)) != 0;
        if (reached && slot == 0 && GetCurrentThreadId() == blocked_thread)
        {
            SetEvent(reached); WaitForSingleObject(resume, 2000);
        }
        *out = {}; out->Gamepad.wButtons = XINPUT_GAMEPAD_A;
        return connected ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
    }
    void refresh()
    {
        std::thread worker([] { xinput_pad::refresh_connections(&fake_state); }); worker.join();
    }
    unsigned total() { unsigned n = 0; for (auto& item : calls) n += item.load(); return n; }
    double measure(bool cached, int frames)
    {
        xinput_pad::cache_connections = cached;
        LARGE_INTEGER frequency{}, start{}, end{};
        QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
        for (int i = 0; i < frames; ++i)
        {
            const auto slots = xinput_pad::connected_indices();
            xinput_pad::raw_state state{};
            if (!slots.empty()) xinput_pad::read_raw(slots[0], state);
        }
        QueryPerformanceCounter(&end);
        return double(end.QuadPart - start.QuadPart) * 1000 / double(frequency.QuadPart) / frames;
    }
}
int main(int argc, char** argv)
{
    xinput_pad::get_state.store(&fake_state);
    xinput_pad::finished = true;
    xinput_pad::refresh_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    check(xinput_pad::connected_indices().empty()); check(total() == 0);
    refresh(); check(total() == 4); check(xinput_pad::connected_indices() == std::vector<int>{0});
    const auto before = total();
    for (unsigned i = 0; i < 10000; ++i)
    {
        check(xinput_pad::connected_indices() == std::vector<int>{0});
        xinput_pad::raw_state out{}; check(xinput_pad::read_raw(0, out)); check(out.buttons == XINPUT_GAMEPAD_A);
    }
    check(total() - before == 10000); check(calls[1] == 1 && calls[2] == 1 && calls[3] == 1);
    devices = 0;
    xinput_pad::raw_state out{};
    check(!xinput_pad::read_raw(0, out)); check(xinput_pad::connected_indices().empty());
    check(WaitForSingleObject(xinput_pad::refresh_event, 0) == WAIT_OBJECT_0);
    devices = 10; refresh(); check(xinput_pad::connected_indices() == (std::vector<int>{1, 3}));
    check(!xinput_pad::read_raw(-1, out)); check(!xinput_pad::read_raw(4, out));
    // Disconnect after the worker has sampled slot 0, before it publishes.
    devices = 1; refresh();
    reached = CreateEventW(nullptr, TRUE, FALSE, nullptr); resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread worker([] { blocked_thread = GetCurrentThreadId(); xinput_pad::refresh_connections(&fake_state); });
    check(WaitForSingleObject(reached, 2000) == WAIT_OBJECT_0);
    devices = 0; check(!xinput_pad::read_raw(0, out));
    SetEvent(resume); worker.join(); check(xinput_pad::connected_indices().empty());
    CloseHandle(reached); CloseHandle(resume); reached = resume = nullptr;
    devices = 1; refresh(); xinput_pad::cache_connections = false;
    const auto old_count = total();
    check(xinput_pad::connected_indices() == std::vector<int>{0}); check(xinput_pad::read_raw(0, out));
    check(total() - old_count == 5); // OFF: four discovery calls plus the selected slot, as before
    std::printf("PASS XInput connections: %u checks; ON 1 call/frame, OFF 5 calls/frame; disconnect race retained\n", checks);
    if (argc > 1 && std::strcmp(argv[1], "--timing-real") == 0)
    {
        // Read-only input timing. No game launch, focus, injected input or desktop operations.
        bool available = false;
        std::thread warmup([&]
        {
            const auto proc = xinput_pad::load_get_state();
            if (proc) { available = true; xinput_pad::get_state = proc; xinput_pad::refresh_connections(proc); }
        }); warmup.join();
        const auto mask = xinput_pad::connected_mask.load();
        std::printf("real XInput connected mask: %X; frames/mode: 2000\n", mask);
        if (available && mask)
        {
            const double off = measure(false, 2000), on = measure(true, 2000);
            std::printf("real XInput per-frame mean: CacheDeviceScan=0 %.6f ms; CacheDeviceScan=1 %.6f ms\n", off, on);
        }
        else std::puts("real per-pad timing unavailable: no XInput slot connected");
    }
    CloseHandle(xinput_pad::refresh_event);
    return 0;
}
